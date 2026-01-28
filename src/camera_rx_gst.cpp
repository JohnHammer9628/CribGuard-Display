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
static std::vector<uint8_t> g_cam_pixels;
static int g_cam_w = 0, g_cam_h = 0;
static lv_image_dsc_t g_cam_dsc{};

// Declared in UI globals (in the UI layer)
extern lv_obj_t* g_cam_img;

// LVGL async callback that installs the latest frame into the UI image widget.
static void ui_set_frame_cb(void*) {
  if (!g_cam_img || g_cam_w <= 0 || g_cam_h <= 0 || g_cam_pixels.empty()) return;
  g_cam_dsc.header.cf = LV_COLOR_FORMAT_ARGB8888;
  g_cam_dsc.header.w = g_cam_w;
  g_cam_dsc.header.h = g_cam_h;
  g_cam_dsc.data = g_cam_pixels.data();
  g_cam_dsc.data_size = g_cam_pixels.size();
  lv_image_set_src(g_cam_img, &g_cam_dsc);
}

// Appsink callback: pulls newest frame and schedules UI update.
static GstFlowReturn on_new_sample(GstAppSink* sink, gpointer /*user_data*/) {
  GstSample* sample = gst_app_sink_pull_sample(sink);
  if (!sample) return GST_FLOW_OK;

  GstCaps* caps = gst_sample_get_caps(sample);
  const GstStructure* s = gst_caps_get_structure(caps, 0);
  int w = 0, h = 0; gst_structure_get_int(s, "width", &w); gst_structure_get_int(s, "height", &h);

  GstBuffer* buf = gst_sample_get_buffer(sample);
  GstMapInfo map;
  if (gst_buffer_map(buf, &map, GST_MAP_READ)) {
    g_cam_w = w; g_cam_h = h;
    g_cam_pixels.resize(static_cast<size_t>(w) * h * 4);
    std::memcpy(g_cam_pixels.data(), map.data,
                std::min(g_cam_pixels.size(), static_cast<size_t>(map.size)));
    gst_buffer_unmap(buf, &map);
    lv_async_call(ui_set_frame_cb, nullptr);
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

    // Build pipeline string
    std::string pipeline_str =
        "udpsrc port=" + std::to_string(g_camera_stream_port) +
        " caps=\"application/x-rtp,media=video,encoding-name=H264,payload=96\" ! "
        "rtph264depay ! h264parse ! "
        "avdec_h264 ! "
        "videoconvert ! "
        "video/x-raw,format=RGBA ! "
        "appsink name=appsink emit-signals=true sync=false max-buffers=1 drop=true";

    log_line((std::string("[GST] creating pipeline: ") + pipeline_str).c_str());

    GError* error = nullptr;
    g_gst_pipeline = gst_parse_launch(pipeline_str.c_str(), &error);

    if (error) {
        log_line((std::string("[GST] ERROR: ") + error->message).c_str());
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

    log_line("[GST] receiver stopped");
}

#else  // CG_HAVE_GSTREAMER

void start_gstreamer_receiver() { log_line("[GST] unavailable (headers not found)"); }
void stop_gstreamer_receiver() { log_line("[GST] unavailable (headers not found)"); }

#endif

