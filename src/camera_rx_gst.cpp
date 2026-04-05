// src/camera_rx_gst.cpp
//
// Parent Pi camera receiver (GStreamer).
//
// Receives the Baby Pi's H264-over-RTP UDP stream and converts frames into RGBA.
// The latest frame is copied into a buffer and pushed to the UI via `lv_async_call`.
//
// Build behavior:
// - If GStreamer headers are available, this compiles the real receiver.
// - Otherwise, `start_gstreamer_receiver()`/`stop_gstreamer_receiver()` are no-ops that log.
#include "camera_rx_gst.h"

#include "config.h"
#include "logging.h"

extern "C" {
  #include "lvgl.h"
}

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if __has_include(<gst/gst.h>)
  #include <gst/gst.h>
  #include <gst/app/gstappsink.h>
  #define CG_HAVE_GSTREAMER 1
#else
  #define CG_HAVE_GSTREAMER 0
#endif

#if CG_HAVE_GSTREAMER

// ---------- GStreamer receiver state ----------
static GstElement* g_gst_pipeline = nullptr;
static GMainLoop* g_gst_loop = nullptr;
static std::thread* g_gst_thread = nullptr;

// One-time GStreamer init. Safe to call multiple times.
static void ensure_gst_init() {
    static std::once_flag once;
    std::call_once(once, []() {
        int argc = 0;
        char** argv = nullptr;
        gst_init(&argc, &argv);
        log_line("[GST] init (lazy)");
    });
}

// Check whether a specific decoder element is available from the local registry.
static bool gst_has_element(const char* element_name) {
    GstElementFactory* factory = gst_element_factory_find(element_name);
    if (!factory) return false;
    gst_object_unref(factory);
    return true;
}

// Log helper for GStreamer-related messages.
static void gst_log_cb(const gchar* message) {
    if (g_log_file) {
        std::fprintf(g_log_file, "[GST] %s\n", message);
        std::fflush(g_log_file);
    }
}

// Bus callback: handles pipeline errors, EOS, and state transitions.
static gboolean gst_bus_callback(GstBus* /*bus*/, GstMessage* msg, gpointer /*data*/) {
    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR: {
            GError* err;
            gchar* debug;
            gst_message_parse_error(msg, &err, &debug);
            gst_log_cb((std::string("Error: ") + err->message).c_str());
            g_error_free(err);
            g_free(debug);
            if (g_gst_loop) g_main_loop_quit(g_gst_loop);
            break;
        }
        case GST_MESSAGE_EOS:
            gst_log_cb("End of stream");
            if (g_gst_loop) g_main_loop_quit(g_gst_loop);
            break;
        case GST_MESSAGE_STATE_CHANGED:
            if (GST_MESSAGE_SRC(msg) == GST_OBJECT(g_gst_pipeline)) {
                GstState old_state, new_state;
                gst_message_parse_state_changed(msg, &old_state, &new_state, nullptr);
                gst_log_cb((std::string("State changed: ") +
                    gst_element_state_get_name(old_state) + " -> " +
                    gst_element_state_get_name(new_state)).c_str());
            }
            break;
        default:
            break;
    }
    return TRUE;
}

// ---------- Appsink → LVGL bridge ----------
// Stage buffer is written by GStreamer thread.
static std::vector<uint8_t> g_cam_pixels_stage;
static int g_cam_w_stage = 0, g_cam_h_stage = 0;
// UI buffer is consumed by LVGL on the main thread only.
static std::vector<uint8_t> g_cam_pixels_ui;
static int g_cam_w_ui = 0, g_cam_h_ui = 0;
static std::mutex g_cam_frame_mtx;
static std::atomic<bool> g_frame_update_pending{false};
static lv_image_dsc_t g_cam_dsc{};
static std::atomic<bool> g_logged_first_frame{false};

// Declared in UI globals (in the UI layer)
extern lv_obj_t* g_cam_img;
extern lv_obj_t* g_cam_stats_label;

// LVGL async callback that installs the latest frame into the UI image widget.
static void ui_set_frame_cb(void*) {
  struct PendingResetGuard {
    ~PendingResetGuard() {
      g_frame_update_pending.store(false, std::memory_order_release);
    }
  } reset_guard;

  if (!g_cam_img) return;

  {
    std::lock_guard<std::mutex> lock(g_cam_frame_mtx);
    if (g_cam_w_stage <= 0 || g_cam_h_stage <= 0 || g_cam_pixels_stage.empty()) return;
    g_cam_w_ui = g_cam_w_stage;
    g_cam_h_ui = g_cam_h_stage;
    g_cam_pixels_ui = g_cam_pixels_stage;
  }

  if (g_cam_w_ui <= 0 || g_cam_h_ui <= 0 || g_cam_pixels_ui.empty()) return;

  g_cam_dsc.header.cf = LV_COLOR_FORMAT_ARGB8888;
  g_cam_dsc.header.w = g_cam_w_ui;
  g_cam_dsc.header.h = g_cam_h_ui;
  g_cam_dsc.data = g_cam_pixels_ui.data();
  g_cam_dsc.data_size = g_cam_pixels_ui.size();
  lv_image_set_src(g_cam_img, &g_cam_dsc);

  static uint32_t s_win_start_ms = 0;
  static uint32_t s_frame_count = 0;
  static float s_fps_smoothed = 0.0f;

  const uint32_t now_ms = lv_tick_get();
  if (s_win_start_ms == 0) s_win_start_ms = now_ms;
  s_frame_count++;

  const uint32_t elapsed_ms = now_ms - s_win_start_ms;
  if (elapsed_ms >= 1000) {
    const float fps = (1000.0f * static_cast<float>(s_frame_count)) / static_cast<float>(elapsed_ms);
    s_fps_smoothed = (s_fps_smoothed <= 0.0f) ? fps : (0.7f * s_fps_smoothed + 0.3f * fps);
    s_win_start_ms = now_ms;
    s_frame_count = 0;
  }

  if (g_cam_stats_label) {
    lv_label_set_text_fmt(g_cam_stats_label, "%dx%d  %.1f fps", g_cam_w_ui, g_cam_h_ui, s_fps_smoothed);
  }

}

// Appsink callback: pulls newest frame and schedules UI update.
static GstFlowReturn on_new_sample(GstAppSink* sink, gpointer /*user_data*/) {
  GstSample* sample = gst_app_sink_pull_sample(sink);
  if (!sample) return GST_FLOW_OK;

  GstCaps* caps = gst_sample_get_caps(sample);
  if (!caps) {
    gst_sample_unref(sample);
    return GST_FLOW_OK;
  }
  const GstStructure* s = gst_caps_get_structure(caps, 0);
  if (!s) {
    gst_sample_unref(sample);
    return GST_FLOW_OK;
  }
  int w = 0, h = 0;
  gst_structure_get_int(s, "width", &w);
  gst_structure_get_int(s, "height", &h);
  if (w <= 0 || h <= 0) {
    gst_sample_unref(sample);
    return GST_FLOW_OK;
  }

  GstBuffer* buf = gst_sample_get_buffer(sample);
  if (!buf) {
    gst_sample_unref(sample);
    return GST_FLOW_OK;
  }
  GstMapInfo map;
  if (gst_buffer_map(buf, &map, GST_MAP_READ)) {
    const size_t expected_size = static_cast<size_t>(w) * static_cast<size_t>(h) * 4U;
    {
      std::lock_guard<std::mutex> lock(g_cam_frame_mtx);
      g_cam_w_stage = w;
      g_cam_h_stage = h;
      g_cam_pixels_stage.resize(expected_size);
      std::fill(g_cam_pixels_stage.begin(), g_cam_pixels_stage.end(), 0);
      std::memcpy(g_cam_pixels_stage.data(), map.data,
                  std::min(g_cam_pixels_stage.size(), static_cast<size_t>(map.size)));
    }
    gst_buffer_unmap(buf, &map);

    if (!g_logged_first_frame.exchange(true, std::memory_order_acq_rel)) {
      log_line((std::string("[GST] first frame received: ") + std::to_string(w) + "x" + std::to_string(h) +
               " bytes=" + std::to_string(static_cast<unsigned long long>(map.size))).c_str());
    }

    if (!g_frame_update_pending.exchange(true, std::memory_order_acq_rel)) {
      if (lv_async_call(ui_set_frame_cb, nullptr) != LV_RESULT_OK) {
        g_frame_update_pending.store(false, std::memory_order_release);
      }
    }
  }
  gst_sample_unref(sample);
  return GST_FLOW_OK;
}

// Start the UDP camera receiver pipeline (no-op if already running).
void start_gstreamer_receiver() {
    ensure_gst_init();
    if (g_gst_pipeline) {
        log_line("[GST] receiver already started");
        return;
    }

    if (!gst_has_element("avdec_h264")) {
        log_line("[GST] ERROR: required decoder 'avdec_h264' is missing. Install: sudo apt install -y gstreamer1.0-libav");
        return;
    }

    // Build pipeline string
    std::string pipeline_str =
        "udpsrc port=" + std::to_string(g_camera_stream_port) +
        " caps=\"application/x-rtp,media=video,encoding-name=H264,payload=96\" ! "
        "rtph264depay ! h264parse ! "
        "avdec_h264 ! "
        "videoconvert ! "
        "video/x-raw,format=BGRA ! "
        "appsink name=appsink emit-signals=true sync=false max-buffers=1 drop=true";

    log_line((std::string("[GST] creating pipeline: ") + pipeline_str).c_str());

    GError* error = nullptr;
    g_gst_pipeline = gst_parse_launch(pipeline_str.c_str(), &error);

    if (error) {
        log_line((std::string("[GST] ERROR: ") + error->message).c_str());
        if (std::string(error->message).find("avdec_h264") != std::string::npos) {
            log_line("[GST] HINT: install software H264 decoder: sudo apt install -y gstreamer1.0-libav");
        }
        g_error_free(error);
        return;
    }

    if (!g_gst_pipeline) {
        log_line("[GST] ERROR: failed to create pipeline");
        return;
    }

    // Hook appsink to feed LVGL
    {
        GstElement* sink = gst_bin_get_by_name(GST_BIN(g_gst_pipeline), "appsink");
        if (sink) {
            gst_app_sink_set_emit_signals(GST_APP_SINK(sink), TRUE);
            g_signal_connect(sink, "new-sample", G_CALLBACK(on_new_sample), nullptr);
            gst_object_unref(sink);
        }
    }

    // Add bus watch
    GstBus* bus = gst_pipeline_get_bus(GST_PIPELINE(g_gst_pipeline));
    gst_bus_add_watch(bus, gst_bus_callback, nullptr);
    gst_object_unref(bus);

    // Start pipeline
    GstStateChangeReturn ret = gst_element_set_state(g_gst_pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        log_line("[GST] ERROR: failed to start pipeline");
        gst_object_unref(g_gst_pipeline);
        g_gst_pipeline = nullptr;
        return;
    }

    // Create main loop in separate thread
    g_gst_loop = g_main_loop_new(nullptr, FALSE);
    g_gst_thread = new std::thread([](){
        log_line("[GST] main loop started");
        g_main_loop_run(g_gst_loop);
        log_line("[GST] main loop exited");
    });

    log_line("[GST] receiver started");
}

// Stop the UDP camera receiver pipeline and clean up all GStreamer resources.
void stop_gstreamer_receiver() {
    if (!g_gst_pipeline) {
        log_line("[GST] receiver not running");
        return;
    }

    log_line("[GST] stopping receiver");

    // Stop pipeline
    gst_element_set_state(g_gst_pipeline, GST_STATE_NULL);

    // Quit main loop
    if (g_gst_loop) {
        g_main_loop_quit(g_gst_loop);
    }

    // Wait for thread
    if (g_gst_thread) {
        if (g_gst_thread->joinable()) {
            g_gst_thread->join();
        }
        delete g_gst_thread;
        g_gst_thread = nullptr;
    }

    // Cleanup
    if (g_gst_loop) {
        g_main_loop_unref(g_gst_loop);
        g_gst_loop = nullptr;
    }

    if (g_gst_pipeline) {
        gst_object_unref(g_gst_pipeline);
        g_gst_pipeline = nullptr;
    }

    {
        std::lock_guard<std::mutex> lock(g_cam_frame_mtx);
        g_cam_pixels_stage.clear();
        g_cam_pixels_ui.clear();
        g_cam_w_stage = 0;
        g_cam_h_stage = 0;
        g_cam_w_ui = 0;
        g_cam_h_ui = 0;
    }
    g_frame_update_pending.store(false, std::memory_order_release);
    g_logged_first_frame.store(false, std::memory_order_release);

    log_line("[GST] receiver stopped");
}

void log_gstreamer_support_status() {
    ensure_gst_init();
    log_line("[GST] support: compiled=yes");

    if (gst_has_element("avdec_h264")) {
        log_line("[GST] decoder: avdec_h264 available");
    } else {
        log_line("[GST] WARNING: decoder avdec_h264 not found. Install: sudo apt install -y gstreamer1.0-libav");
    }
}

#else  // CG_HAVE_GSTREAMER

void start_gstreamer_receiver() { log_line("[GST] unavailable (headers not found)"); }
void stop_gstreamer_receiver() { log_line("[GST] unavailable (headers not found)"); }
void log_gstreamer_support_status() {
    log_line("[GST] support: compiled=no (headers missing at build time)");
    log_line("[GST] HINT: install libgstreamer dev packages and rebuild if camera RX is required");
}

#endif

