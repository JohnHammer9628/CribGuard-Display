// platforms/pi-sdl/main.cpp
#define _CRT_SECURE_NO_WARNINGS

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <chrono>
#include <thread>
#include <csignal>

extern "C" {
  #include "lvgl.h"
}

// SDL header for runtime display size detection (auto-fit to active mode)
#if __has_include("SDL.h")
  #include "SDL.h"
  #define HAVE_SDL2_HEADER 1
#elif __has_include("SDL2/SDL.h")
  #include "SDL2/SDL.h"
  #define HAVE_SDL2_HEADER 1
#else
  #define HAVE_SDL2_HEADER 0
#endif

// ---- Probe LVGL v9 SDL headers under different include roots ----
#if __has_include("lvgl/src/drivers/sdl/lv_sdl_window.h")
  #include "lvgl/src/drivers/sdl/lv_sdl_window.h"
  #define HAVE_SDL_WIN 1
#elif __has_include("src/drivers/sdl/lv_sdl_window.h")
  #include "src/drivers/sdl/lv_sdl_window.h"
  #define HAVE_SDL_WIN 1
#elif __has_include("lvgl/drivers/sdl/lv_sdl_window.h")
  #include "lvgl/drivers/sdl/lv_sdl_window.h"
  #define HAVE_SDL_WIN 1
#elif __has_include("drivers/sdl/lv_sdl_window.h")
  #include "drivers/sdl/lv_sdl_window.h"
  #define HAVE_SDL_WIN 1
#else
  #define HAVE_SDL_WIN 0
#endif

#if __has_include("lvgl/src/drivers/sdl/lv_sdl_mouse.h")
  #include "lvgl/src/drivers/sdl/lv_sdl_mouse.h"
  #define HAVE_SDL_MOUSE 1
#elif __has_include("src/drivers/sdl/lv_sdl_mouse.h")
  #include "src/drivers/sdl/lv_sdl_mouse.h"
  #define HAVE_SDL_MOUSE 1
#elif __has_include("lvgl/drivers/sdl/lv_sdl_mouse.h")
  #include "lvgl/drivers/sdl/lv_sdl_mouse.h"
  #define HAVE_SDL_MOUSE 1
#elif __has_include("drivers/sdl/lv_sdl_mouse.h")
  #include "drivers/sdl/lv_sdl_mouse.h"
  #define HAVE_SDL_MOUSE 1
#else
  #define HAVE_SDL_MOUSE 0
#endif

#if __has_include("lvgl/src/drivers/sdl/lv_sdl_keyboard.h")
  #include "lvgl/src/drivers/sdl/lv_sdl_keyboard.h"
  #define HAVE_SDL_KBD 1
#elif __has_include("src/drivers/sdl/lv_sdl_keyboard.h")
  #include "src/drivers/sdl/lv_sdl_keyboard.h"
  #define HAVE_SDL_KBD 1
#elif __has_include("lvgl/drivers/sdl/lv_sdl_keyboard.h")
  #include "lvgl/drivers/sdl/lv_sdl_keyboard.h"
  #define HAVE_SDL_KBD 1
#elif __has_include("drivers/sdl/lv_sdl_keyboard.h")
  #include "drivers/sdl/lv_sdl_keyboard.h"
  #define HAVE_SDL_KBD 1
#else
  #define HAVE_SDL_KBD 0
#endif

#if __has_include("lvgl/src/drivers/sdl/lv_sdl_mousewheel.h")
  #include "lvgl/src/drivers/sdl/lv_sdl_mousewheel.h"
  #define HAVE_SDL_WHEEL 1
#elif __has_include("src/drivers/sdl/lv_sdl_mousewheel.h")
  #include "src/drivers/sdl/lv_sdl_mousewheel.h"
  #define HAVE_SDL_WHEEL 1
#elif __has_include("lvgl/drivers/sdl/lv_sdl_mousewheel.h")
  #include "lvgl/drivers/sdl/lv_sdl_mousewheel.h"
  #define HAVE_SDL_WHEEL 1
#elif __has_include("drivers/sdl/lv_sdl_mousewheel.h")
  #include "drivers/sdl/lv_sdl_mousewheel.h"
  #define HAVE_SDL_WHEEL 1
#else
  #define HAVE_SDL_WHEEL 0
#endif

#if !HAVE_SDL_WIN
  #error "LVGL SDL driver headers not found. Ensure LV_USE_SDL=1 in lv_conf.h and drivers are in include path."
#endif

// ---------- GStreamer and HTTP support ----------
#if __has_include(<gst/gst.h>)
  #include <gst/gst.h>
  #define HAVE_GSTREAMER 1
#else
  #define HAVE_GSTREAMER 0
#endif
#if HAVE_GSTREAMER
  #include <gst/app/gstappsink.h>
#endif

#if __has_include(<curl/curl.h>)
  #include <curl/curl.h>
  #define HAVE_CURL 1
#else
  #define HAVE_CURL 0
#endif

#include <fstream>
#include <sstream>
#include <map>
#include <vector>
#include <cstring>

// ---------- Simple logging ----------
static FILE* g_log_file = nullptr;
static void log_line(const char* msg) {
    if (g_log_file) { std::fprintf(g_log_file, "%s\n", msg); std::fflush(g_log_file); }
}

// ---------- Configuration ----------
static std::string g_baby_pi_ip = "10.0.0.153";
static int g_baby_pi_port = 8000;
static int g_camera_stream_port = 5000;
static int g_audio_record_seconds = 30;
static std::string g_audio_play_device = "default";

static void load_config() {
    std::ifstream cfg("settings.cfg");
    if (!cfg.is_open()) {
        log_line("[CFG] settings.cfg not found, using defaults");
        return;
    }
    
    std::string line;
    while (std::getline(cfg, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#') continue;
        
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        
        // Trim whitespace
        key.erase(0, key.find_first_not_of(" \t\r\n"));
        key.erase(key.find_last_not_of(" \t\r\n") + 1);
        val.erase(0, val.find_first_not_of(" \t\r\n"));
        val.erase(val.find_last_not_of(" \t\r\n") + 1);
        
        if (key == "baby_pi_ip") {
            g_baby_pi_ip = val;
            log_line((std::string("[CFG] baby_pi_ip = ") + val).c_str());
        } else if (key == "baby_pi_port") {
            g_baby_pi_port = std::atoi(val.c_str());
            log_line((std::string("[CFG] baby_pi_port = ") + val).c_str());
        } else if (key == "camera_stream_port") {
            g_camera_stream_port = std::atoi(val.c_str());
            log_line((std::string("[CFG] camera_stream_port = ") + val).c_str());
        } else if (key == "audio_record_seconds") {
            g_audio_record_seconds = std::atoi(val.c_str());
            if (g_audio_record_seconds <= 0) g_audio_record_seconds = 30;
            log_line((std::string("[CFG] audio_record_seconds = ") + std::to_string(g_audio_record_seconds)).c_str());
        } else if (key == "audio_play_device") {
            g_audio_play_device = val.empty() ? "default" : val;
            log_line((std::string("[CFG] audio_play_device = ") + g_audio_play_device).c_str());
        }
    }
    cfg.close();
}

#if HAVE_GSTREAMER
// ---------- GStreamer receiver state ----------
static GstElement* g_gst_pipeline = nullptr;
static GMainLoop* g_gst_loop = nullptr;
static std::thread* g_gst_thread = nullptr;

static void gst_log_cb(const gchar* message) {
    if (g_log_file) {
        std::fprintf(g_log_file, "[GST] %s\n", message);
        std::fflush(g_log_file);
    }
}

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

// Forward declaration for appsink callback
static GstFlowReturn on_new_sample(GstAppSink* sink, gpointer user_data);

static void start_gstreamer_receiver() {
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

static void stop_gstreamer_receiver() {
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

// ---------- Appsink → LVGL bridge ----------
static std::vector<uint8_t> g_cam_pixels;
static int g_cam_w = 0, g_cam_h = 0;

static lv_image_dsc_t g_cam_dsc{};

static void ui_set_frame_cb(void*) {
  extern lv_obj_t* g_cam_img; // declared among UI globals
  if (!g_cam_img || g_cam_w <= 0 || g_cam_h <= 0 || g_cam_pixels.empty()) return;
  g_cam_dsc.header.cf = LV_COLOR_FORMAT_ARGB8888;
  g_cam_dsc.header.w = g_cam_w;
  g_cam_dsc.header.h = g_cam_h;
  g_cam_dsc.data = g_cam_pixels.data();
  g_cam_dsc.data_size = g_cam_pixels.size();
  lv_image_set_src(g_cam_img, &g_cam_dsc);
}

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
#endif

#if HAVE_CURL
// ---------- HTTP client for Baby Pi control ----------
static size_t http_write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

static bool http_post(const std::string& url, const std::string& json_data, std::string& response) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        log_line("[HTTP] ERROR: curl_easy_init failed");
        return false;
    }
    
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    
    CURLcode res = curl_easy_perform(curl);
    
    bool success = (res == CURLE_OK);
    if (!success) {
        log_line((std::string("[HTTP] ERROR: ") + curl_easy_strerror(res)).c_str());
    }
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    return success;
}

static bool http_get(const std::string& url, std::string& response) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        log_line("[HTTP] ERROR: curl_easy_init failed");
        return false;
    }
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    
    CURLcode res = curl_easy_perform(curl);
    
    bool success = (res == CURLE_OK);
    if (!success) {
        log_line((std::string("[HTTP] ERROR: ") + curl_easy_strerror(res)).c_str());
    }
    
    curl_easy_cleanup(curl);
    
    return success;
}

static void baby_pi_start_camera() {
    std::string url = "http://" + g_baby_pi_ip + ":" + std::to_string(g_baby_pi_port) + "/api/start";
    std::string json = "{\"parent_port\":" + std::to_string(g_camera_stream_port) + "}";
    std::string response;
    
    log_line((std::string("[CAM] starting camera on Baby Pi: ") + url).c_str());
    
    if (http_post(url, json, response)) {
        log_line((std::string("[CAM] Baby Pi response: ") + response).c_str());
    } else {
        log_line("[CAM] ERROR: failed to start camera on Baby Pi");
    }
}

static void baby_pi_stop_camera() {
    std::string url = "http://" + g_baby_pi_ip + ":" + std::to_string(g_baby_pi_port) + "/api/stop";
    std::string response;
    
    log_line((std::string("[CAM] stopping camera on Baby Pi: ") + url).c_str());
    
    if (http_post(url, "{}", response)) {
        log_line((std::string("[CAM] Baby Pi response: ") + response).c_str());
    } else {
        log_line("[CAM] ERROR: failed to stop camera on Baby Pi");
    }
}

static void baby_pi_record_audio(int seconds) {
    if (seconds <= 0) seconds = g_audio_record_seconds;
    std::string url = "http://" + g_baby_pi_ip + ":" + std::to_string(g_baby_pi_port) + "/api/record";
    std::string json = "{\"seconds\":" + std::to_string(seconds) + "}";
    std::string response;

    log_line((std::string("[AUDIO] record request: ") + std::to_string(seconds) + "s").c_str());

    if (http_post(url, json, response)) {
        log_line((std::string("[AUDIO] Baby Pi response: ") + response).c_str());
    } else {
        log_line("[AUDIO] ERROR: failed to start recording on Baby Pi");
    }
}

static bool baby_pi_list_lullabies(std::vector<std::string>& out_files) {
    std::string url = "http://" + g_baby_pi_ip + ":" + std::to_string(g_baby_pi_port) + "/api/lullabies";
    std::string response;
    out_files.clear();
    if (!http_get(url, response)) return false;
    // very simple JSON scan: look for "name":"..."
    size_t pos = 0;
    while (true) {
        pos = response.find("\"name\"", pos);
        if (pos == std::string::npos) break;
        size_t colon = response.find(':', pos);
        if (colon == std::string::npos) break;
        size_t q1 = response.find('"', colon + 1);
        if (q1 == std::string::npos) break;
        size_t q2 = response.find('"', q1 + 1);
        if (q2 == std::string::npos) break;
        std::string name = response.substr(q1 + 1, q2 - (q1 + 1));
        if (!name.empty()) out_files.push_back(name);
        pos = q2 + 1;
    }
    return true;
}

static void baby_pi_play_lullaby(const std::string& filename) {
    std::string url = "http://" + g_baby_pi_ip + ":" + std::to_string(g_baby_pi_port) + "/api/play";
    std::string json = std::string("{\"file\":\"") + filename + "\",\"device\":\"" + g_audio_play_device + "\"}";
    std::string response;
    log_line((std::string("[AUDIO] play: ") + filename).c_str());
    if (http_post(url, json, response)) {
        log_line((std::string("[AUDIO] Baby Pi response: ") + response).c_str());
    } else {
        log_line("[AUDIO] ERROR: failed to start playback");
    }
}

static bool baby_pi_rename_lullaby(const std::string& old_name, const std::string& new_name_with_ext) {
    std::string url = "http://" + g_baby_pi_ip + ":" + std::to_string(g_baby_pi_port) + "/api/rename";
    // Ensure .wav extension on destination
    std::string dst = new_name_with_ext;
    if (dst.size() < 4 || dst.substr(dst.size() - 4) != ".wav") {
        dst += ".wav";
    }
    // Very basic sanitation client-side (server also sanitizes)
    auto sanitize = [](std::string s) {
        for (char& c : s) {
            bool ok = (c >= '0' && c <= '9') ||
                      (c >= 'A' && c <= 'Z') ||
                      (c >= 'a' && c <= 'z') ||
                      c == '_' || c == '-' || c == ' ' || c == '.';
            if (!ok) c = '_';
        }
        return s;
    };
    std::string json = std::string("{\"old\":\"") + sanitize(old_name) + "\",\"new\":\"" + sanitize(dst) + "\"}";
    std::string response;
    log_line((std::string("[AUDIO] rename: ") + old_name + " -> " + dst).c_str());
    if (http_post(url, json, response)) {
        log_line((std::string("[AUDIO] Baby Pi response: ") + response).c_str());
        // Heuristic: check for success true
        if (response.find("\"success\": true") != std::string::npos ||
            response.find("\"success\":true") != std::string::npos) {
            return true;
        }
    } else {
        log_line("[AUDIO] ERROR: failed to call rename");
    }
    return false;
}

static void baby_pi_stop_playback() {
    std::string url = "http://" + g_baby_pi_ip + ":" + std::to_string(g_baby_pi_port) + "/api/play/stop";
    std::string response;
    log_line("[AUDIO] stop playback");
    if (http_post(url, "{}", response)) {
        log_line((std::string("[AUDIO] Baby Pi response: ") + response).c_str());
    } else {
        log_line("[AUDIO] ERROR: failed to stop playback");
    }
}

static bool baby_pi_check_status() {
    std::string url = "http://" + g_baby_pi_ip + ":" + std::to_string(g_baby_pi_port) + "/api/status";
    std::string response;
    
    if (http_get(url, response)) {
        log_line((std::string("[CAM] Baby Pi status: ") + response).c_str());
        return true;
    } else {
        log_line("[CAM] ERROR: failed to get status from Baby Pi");
        return false;
    }
}
#endif

// ---------- Theme (tokens & helpers) ----------
namespace theme {
    static inline lv_color_t surface_bg()      { return lv_color_hex(0x16191D); }
    static inline lv_color_t header_bg()       { return lv_color_hex(0x1E232A); }
    static inline lv_color_t border()          { return lv_color_hex(0x2A2F36); }
    static inline lv_color_t text_main()       { return lv_color_hex(0xEDEFF2); }
    static inline lv_color_t text_subtle()     { return lv_color_hex(0x9AA3AD); }
    static inline lv_color_t tonal_bg()        { return lv_color_hex(0x2A2F36); }
    static inline lv_color_t tonal_border()    { return lv_color_hex(0x3A4048); }
    static inline lv_color_t primary_accent()  { return lv_color_hex(0x7FB3FF); }

    static constexpr int sp4  = 4;
    static constexpr int sp6  = 6;
    static constexpr int sp8  = 8;
    static constexpr int sp10 = 10;
    static constexpr int sp12 = 12;
    static constexpr int sp14 = 14;
    static constexpr int sp16 = 16;

    static constexpr int radius_sm = 6;
    static constexpr int radius_md = 8;
    static constexpr int radius_lg = 10;
    static constexpr int radius_pill = 999;
}

static void style_button_tonal(lv_obj_t* btn) {
    lv_obj_set_style_radius(btn, theme::radius_sm, 0);
    lv_obj_set_style_bg_color(btn, theme::tonal_bg(), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_40, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, theme::tonal_border(), 0);
    lv_obj_set_style_pad_hor(btn, theme::sp12, 0);
    lv_obj_set_style_pad_ver(btn, theme::sp8, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_50, LV_STATE_PRESSED);
}

static void style_button_pill(lv_obj_t* btn) {
    lv_obj_set_style_radius(btn, theme::radius_pill, 0);
    lv_obj_set_style_bg_color(btn, theme::tonal_bg(), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_40, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, theme::tonal_border(), 0);
    lv_obj_set_style_pad_hor(btn, theme::sp12, 0);
    lv_obj_set_style_pad_ver(btn, theme::sp6, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_50, LV_STATE_PRESSED);
}

static void style_sheet(lv_obj_t* sheet) {
    lv_obj_clear_flag(sheet, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(sheet, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(sheet, theme::surface_bg(), 0);
    lv_obj_set_style_bg_opa(sheet, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(sheet, theme::text_main(), 0);
    lv_obj_set_style_border_width(sheet, 1, 0);
    lv_obj_set_style_border_color(sheet, theme::border(), 0);
    lv_obj_set_style_radius(sheet, theme::radius_lg, 0);
    lv_obj_set_style_shadow_width(sheet, 18, 0);
    lv_obj_set_style_shadow_opa(sheet, LV_OPA_30, 0);
    lv_obj_set_style_shadow_color(sheet, lv_color_hex(0x000000), 0);
    lv_obj_set_style_pad_hor(sheet, theme::sp16, 0);
    lv_obj_set_style_pad_ver(sheet, theme::sp14, 0);
    lv_obj_set_flex_flow(sheet, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(sheet, theme::sp12, 0);
}

static lv_obj_t* build_header(lv_obj_t* sheet, const char* title, lv_obj_t** out_actions) {
    lv_obj_t* hdr = lv_obj_create(sheet);
    lv_obj_set_size(hdr, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(hdr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(hdr, theme::header_bg(), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hdr, 1, 0);
    lv_obj_set_style_border_color(hdr, theme::border(), 0);
    lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(hdr, theme::sp8, 0);
    lv_obj_set_style_pad_right(hdr, theme::sp8, 0);

    lv_obj_t* lbl_title = lv_label_create(hdr);
    lv_label_set_text(lbl_title, title);
    lv_obj_set_style_text_color(lbl_title, theme::text_main(), 0);
    lv_obj_set_style_pad_left(lbl_title, theme::sp8, 0);

    lv_obj_t* actions = lv_obj_create(hdr);
    lv_obj_clear_flag(actions, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(actions, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(actions, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(actions, 0, 0);
    lv_obj_set_style_pad_all(actions, 0, 0);
    lv_obj_set_style_pad_column(actions, theme::sp8, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(actions, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    if (out_actions) *out_actions = actions;
    return hdr;
}

// ---------- App state ----------
static lv_obj_t* g_lbl_status = nullptr; // Center status
static lv_obj_t* g_lbl_conn   = nullptr; // Top bar label
static lv_obj_t* g_settings_modal = nullptr;
static lv_obj_t* g_camera_modal = nullptr;
static lv_obj_t* g_camera_surface = nullptr;
static lv_obj_t* g_camera_sheet = nullptr;
static lv_obj_t* g_cam_spinner = nullptr;
static lv_obj_t* g_cam_live_label = nullptr;
static lv_obj_t* g_cam_meta_label = nullptr;
static lv_obj_t* g_cam_btn_full = nullptr;
static lv_obj_t* g_cam_sw_mute = nullptr;
lv_obj_t* g_cam_img = nullptr;

// Main dashboard elements
static lv_obj_t* g_dash_card = nullptr;
static lv_obj_t* g_dash_hero = nullptr;
static lv_obj_t* g_dash_ring = nullptr;
static lv_obj_t* g_stat_conn = nullptr;
static lv_obj_t* g_stat_vol  = nullptr;
static lv_obj_t* g_stat_qh   = nullptr;

// Top bar quick-action buttons (replace switches)
static lv_obj_t* g_btn_quiet = nullptr;
static lv_obj_t* g_btn_conn = nullptr;

// Library & Lullabies modals
static lv_obj_t* g_library_modal = nullptr;
static lv_obj_t* g_library_sheet = nullptr;
static lv_obj_t* g_lullabies_modal = nullptr;
static lv_obj_t* g_lullabies_sheet = nullptr;
static lv_obj_t* g_lullabies_list = nullptr;
static std::vector<std::string> g_lullabies_files;
// Rename dialog state
static lv_obj_t* g_rename_modal = nullptr;
static lv_obj_t* g_rename_sheet = nullptr;
static lv_obj_t* g_rename_text = nullptr;
static lv_obj_t* g_rename_kbd = nullptr;
static std::string g_rename_old_name;

static bool   g_connected   = true;
static bool   g_quiet_hours = false;
static int    g_quiet_start = 18; // 24h
static int    g_quiet_end   = 7;  // 24h
static int    g_volume      = 50;
static bool   g_quit        = false;
static bool   g_cam_fullscreen = false;
static bool   g_cam_muted = false;
static bool   g_cam_playing = false;
static bool   g_cam_show_spinner = false;
static lv_timer_t* g_cam_spinner_timer = nullptr;

// ---------- Signal handling ----------
static void handle_shutdown_signal(int /*sig*/) {
	log_line("[SIM] signal received, initiating graceful shutdown");
	if (g_log_file) std::fflush(g_log_file);
	g_quit = true;
}

// ---------- Helpers ----------
// Forward declaration so it can be used in helpers below
static void update_dashboard();
static void apply_top_buttons_state();
static void on_top_quiet_click(lv_event_t* e);
static void on_top_conn_click(lv_event_t* e);
static void build_library_dialog(lv_obj_t* parent);
static void close_library();
static void build_lullabies_dialog(lv_obj_t* parent);
static void close_lullabies();
static void build_rename_dialog(const char* old_name);
static void close_rename();
static std::string format_hour12(int hour24) {
    int h = ((hour24 % 24) + 24) % 24;
    int display = h % 12;
    if (display == 0) display = 12;
    bool pm = h >= 12;
    return std::to_string(display) + (pm ? " PM" : " AM");
}
static void set_status_text(const char* txt, lv_color_t color) {
    lv_label_set_text(g_lbl_status, txt);
    lv_obj_set_style_text_color(g_lbl_status, color, 0);
    log_line((std::string("[UI] status: ") + txt).c_str());
    update_dashboard();
}

static void update_top_label() {
    // Keep brand short in the top-left; reflect state via quick-action pills
    if (g_lbl_conn) lv_label_set_text(g_lbl_conn, "CribGuard");
    apply_top_buttons_state();
    std::string log_s = std::string("brand=CribGuard, conn=") + (g_connected ? "on" : "off")
        + ", vol=" + std::to_string(g_volume)
        + ", quiet=" + (g_quiet_hours ? "on" : "off");
    log_line((std::string("[UI] topbar: ") + log_s).c_str());
    update_dashboard();
}

// ---------- Events ----------
static void on_btn_status(lv_event_t* e) {
    const char* role = (const char*)lv_event_get_user_data(e);
    if (!role) return;

    if (std::string(role) == "calm") {
        set_status_text("Calm", lv_color_hex(0x22AA22));
    } else if (std::string(role) == "cry") {
        set_status_text("Cry", lv_color_hex(0xCC2222));
    } else if (std::string(role) == "motion") {
        set_status_text("Motion", lv_color_hex(0xD08770));
    }
}

// ---------- Library dialog ----------
static void build_library_dialog(lv_obj_t* parent) {
    if (g_library_modal) return;

    g_library_modal = lv_obj_create(parent);
    lv_obj_remove_style_all(g_library_modal);
    lv_obj_set_size(g_library_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(g_library_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_library_modal, LV_OPA_30, 0);
    lv_obj_add_flag(g_library_modal, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* sheet = lv_obj_create(g_library_modal);
    lv_obj_set_size(sheet, LV_PCT(94), LV_PCT(94));
    lv_obj_center(sheet);
    g_library_sheet = sheet;

    style_sheet(sheet);

    // header
    lv_obj_t* hdr_btns = nullptr;
    build_header(sheet, "Library", &hdr_btns);

    lv_obj_t* btn_close = lv_btn_create(hdr_btns);
    style_button_tonal(btn_close);
    { lv_obj_t* lbl = lv_label_create(btn_close); lv_label_set_text(lbl, "Close"); lv_obj_center(lbl); }
    lv_obj_add_event_cb(btn_close, [](lv_event_t* /*e*/){ close_library(); }, LV_EVENT_CLICKED, nullptr);

    // tabs row
    lv_obj_t* tabs = lv_obj_create(sheet);
    lv_obj_set_size(tabs, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(tabs, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(tabs, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(tabs, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tabs, 0, 0);
    lv_obj_set_style_pad_all(tabs, 0, 0);
    lv_obj_set_style_pad_column(tabs, 10, 0);
    lv_obj_set_flex_flow(tabs, LV_FLEX_FLOW_ROW);

    auto lib_pill = [&](const char* text){
        lv_obj_t* b = lv_btn_create(tabs);
        lv_obj_set_style_radius(b, 999, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x2A2F36), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_40, 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_border_color(b, lv_color_hex(0x3A4048), 0);
        lv_obj_set_style_pad_hor(b, 12, 0);
        lv_obj_set_style_pad_ver(b, 6, 0);
        { lv_obj_t* l = lv_label_create(b); lv_label_set_text(l, text); lv_obj_center(l); }
        return b;
    };
    lib_pill("All"); lib_pill("Photos"); lib_pill("Recordings");

    // content placeholder
    lv_obj_t* content = lv_obj_create(sheet);
    lv_obj_set_size(content, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_style_bg_color(content, lv_color_hex(0x1E232A), 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(content, 1, 0);
    lv_obj_set_style_border_color(content, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_radius(content, 8, 0);
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    {
        lv_obj_t* lbl = lv_label_create(content);
        lv_label_set_text(lbl, "Library coming soon");
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x9AA3AD), 0);
        lv_obj_center(lbl);
    }

    // backdrop click to close
    lv_obj_add_event_cb(g_library_modal, [](lv_event_t* e){
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) { close_library(); }
    }, LV_EVENT_CLICKED, nullptr);

    log_line("[UI] library opened");
}

static void close_library() {
    if (g_library_modal) {
        lv_obj_del(g_library_modal);
        g_library_modal = nullptr;
        g_library_sheet = nullptr;
        log_line("[UI] library closed");
    }
}

// ---------- Rename dialog ----------
static void close_rename() {
    if (g_rename_modal) {
        lv_obj_del(g_rename_modal);
        g_rename_modal = nullptr;
        g_rename_sheet = nullptr;
        g_rename_text = nullptr;
        if (g_rename_kbd) { lv_obj_del(g_rename_kbd); g_rename_kbd = nullptr; }
        log_line("[UI] rename closed");
    }
}

static void build_rename_dialog(const char* old_name) {
    if (g_rename_modal) return;
    if (!old_name) return;
    // Only allow renaming .wav files per requirement
    std::string s = old_name;
    if (!(s.size() >= 4 && s.substr(s.size() - 4) == ".wav")) {
        log_line("[UI] rename only allowed for .wav files");
        return;
    }
    g_rename_old_name = s;

    g_rename_modal = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_rename_modal);
    lv_obj_set_size(g_rename_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(g_rename_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_rename_modal, LV_OPA_30, 0);
    lv_obj_add_flag(g_rename_modal, LV_OBJ_FLAG_CLICKABLE);

    g_rename_sheet = lv_obj_create(g_rename_modal);
    lv_obj_set_size(g_rename_sheet, LV_PCT(92), LV_SIZE_CONTENT);
    lv_obj_center(g_rename_sheet);
    style_sheet(g_rename_sheet);

    // header
    lv_obj_t* hdr_btns = nullptr;
    build_header(g_rename_sheet, "Rename Lullaby", &hdr_btns);

    // Cancel
    lv_obj_t* btn_cancel = lv_btn_create(hdr_btns);
    style_button_tonal(btn_cancel);
    { lv_obj_t* lbl = lv_label_create(btn_cancel); lv_label_set_text(lbl, "Cancel"); lv_obj_center(lbl); }
    lv_obj_add_event_cb(btn_cancel, [](lv_event_t* /*e*/){ close_rename(); }, LV_EVENT_CLICKED, nullptr);

    // Save
    lv_obj_t* btn_save = lv_btn_create(hdr_btns);
    style_button_tonal(btn_save);
    { lv_obj_t* lbl = lv_label_create(btn_save); lv_label_set_text(lbl, "Save"); lv_obj_center(lbl); }
    lv_obj_add_event_cb(btn_save, [](lv_event_t* /*e*/){
#if HAVE_CURL
        if (!g_rename_text) return;
        const char* new_text = lv_textarea_get_text(g_rename_text);
        if (!new_text || std::string(new_text).empty()) return;
        std::string new_name = new_text;
        // Append .wav if not present (server enforces too)
        if (!(new_name.size() >= 4 && new_name.substr(new_name.size() - 4) == ".wav")) {
            new_name += ".wav";
        }
        bool ok = baby_pi_rename_lullaby(g_rename_old_name, new_name);
        // Refresh list regardless; best-effort
        if (g_lullabies_list) {
            if (baby_pi_list_lullabies(g_lullabies_files)) {
                lv_obj_clean(g_lullabies_list);
                for (const auto& name : g_lullabies_files) {
                    lv_obj_t* b = lv_btn_create(g_lullabies_list);
                    style_button_tonal(b);
                    { lv_obj_t* l = lv_label_create(b); lv_label_set_text(l, name.c_str()); lv_obj_center(l); }
                    // click to play
                    lv_obj_add_event_cb(b, [](lv_event_t* e){
                        lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
                        lv_obj_t* child = lv_obj_get_child(btn, 0);
                        const char* fname = child ? lv_label_get_text(child) : nullptr;
                        if (fname) baby_pi_play_lullaby(fname);
                    }, LV_EVENT_CLICKED, nullptr);
                    // long-press to rename
                    lv_obj_add_event_cb(b, [](lv_event_t* e){
                        lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
                        lv_obj_t* child = lv_obj_get_child(btn, 0);
                        const char* fname = child ? lv_label_get_text(child) : nullptr;
                        if (fname) build_rename_dialog(fname);
                    }, LV_EVENT_LONG_PRESSED, nullptr);
                }
            }
        }
        close_rename();
#else
        (void)g_rename_text;
        close_rename();
#endif
    }, LV_EVENT_CLICKED, nullptr);

    // content
    lv_obj_t* content = lv_obj_create(g_rename_sheet);
    lv_obj_set_size(content, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_style_bg_color(content, lv_color_hex(0x1E232A), 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(content, 1, 0);
    lv_obj_set_style_border_color(content, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_radius(content, 8, 0);
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(content, theme::sp12, 0);
    lv_obj_set_style_pad_row(content, theme::sp8, 0);

    // Current name
    {
        lv_obj_t* lbl_cur = lv_label_create(content);
        std::string cur = std::string("Current: ") + g_rename_old_name;
        lv_label_set_text(lbl_cur, cur.c_str());
        lv_obj_set_style_text_color(lbl_cur, theme::text_subtle(), 0);
    }

    // New name label
    {
        lv_obj_t* lbl_new = lv_label_create(content);
        lv_label_set_text(lbl_new, "New name");
        lv_obj_set_style_text_color(lbl_new, theme::text_subtle(), 0);
    }

    // Text area pre-filled with base name (without .wav)
    g_rename_text = lv_textarea_create(content);
    lv_obj_set_width(g_rename_text, LV_PCT(92));
    // Style for visibility
    lv_obj_set_style_bg_color(g_rename_text, theme::tonal_bg(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_rename_text, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_rename_text, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(g_rename_text, theme::tonal_border(), LV_PART_MAIN);
    lv_obj_set_style_text_color(g_rename_text, theme::text_main(), LV_PART_MAIN);
    {
        std::string base = g_rename_old_name;
        if (base.size() >= 4 && base.substr(base.size() - 4) == ".wav") {
            base = base.substr(0, base.size() - 4);
        }
        lv_textarea_set_text(g_rename_text, base.c_str());
    }
    lv_textarea_set_cursor_pos(g_rename_text, LV_TEXTAREA_CURSOR_LAST);
    lv_textarea_set_placeholder_text(g_rename_text, "Enter new name");
    lv_textarea_set_one_line(g_rename_text, true);
    lv_textarea_set_password_mode(g_rename_text, false);

    // On-screen keyboard (optional; helpful on touch)
    g_rename_kbd = lv_keyboard_create(g_rename_modal);
    lv_obj_set_width(g_rename_kbd, LV_PCT(92));
    lv_obj_align(g_rename_kbd, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_keyboard_set_textarea(g_rename_kbd, g_rename_text);

    // backdrop click to close
    lv_obj_add_event_cb(g_rename_modal, [](lv_event_t* e){
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) { close_rename(); }
    }, LV_EVENT_CLICKED, nullptr);

    log_line("[UI] rename opened");
}

// ---------- Lullabies dialog ----------
static void build_lullabies_dialog(lv_obj_t* parent) {
    if (g_lullabies_modal) return;

    g_lullabies_modal = lv_obj_create(parent);
    lv_obj_remove_style_all(g_lullabies_modal);
    lv_obj_set_size(g_lullabies_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(g_lullabies_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_lullabies_modal, LV_OPA_30, 0);
    lv_obj_add_flag(g_lullabies_modal, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* sheet = lv_obj_create(g_lullabies_modal);
    lv_obj_set_size(sheet, LV_PCT(94), LV_PCT(94));
    lv_obj_center(sheet);
    g_lullabies_sheet = sheet;

    style_sheet(sheet);

    // header
    lv_obj_t* hdr_btns = nullptr;
    build_header(sheet, "Lullabies", &hdr_btns);

    lv_obj_t* btn_close2 = lv_btn_create(hdr_btns);
    style_button_tonal(btn_close2);
    { lv_obj_t* lbl = lv_label_create(btn_close2); lv_label_set_text(lbl, "Close"); lv_obj_center(lbl); }
    lv_obj_add_event_cb(btn_close2, [](lv_event_t* /*e*/){ close_lullabies(); }, LV_EVENT_CLICKED, nullptr);

    // Record Audio button in Lullabies header
    lv_obj_t* btn_rec = lv_btn_create(hdr_btns);
    style_button_tonal(btn_rec);
    { lv_obj_t* lbl = lv_label_create(btn_rec); lv_label_set_text(lbl, "Record"); lv_obj_center(lbl); }
    lv_obj_add_event_cb(btn_rec, [](lv_event_t* /*e*/){
#if HAVE_CURL
        baby_pi_record_audio(g_audio_record_seconds);
#endif
    }, LV_EVENT_CLICKED, nullptr);

    // Refresh list button
    lv_obj_t* btn_ref = lv_btn_create(hdr_btns);
    style_button_tonal(btn_ref);
    { lv_obj_t* lbl = lv_label_create(btn_ref); lv_label_set_text(lbl, "Refresh"); lv_obj_center(lbl); }
    lv_obj_add_event_cb(btn_ref, [](lv_event_t* /*e*/){
#if HAVE_CURL
        // rebuild list
        if (baby_pi_list_lullabies(g_lullabies_files) && g_lullabies_list) {
            lv_obj_clean(g_lullabies_list);
            for (const auto& name : g_lullabies_files) {
                lv_obj_t* b = lv_btn_create(g_lullabies_list);
                style_button_tonal(b);
                { lv_obj_t* l = lv_label_create(b); lv_label_set_text(l, name.c_str()); lv_obj_center(l); }
                lv_obj_add_event_cb(b, [](lv_event_t* e){
                    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
                    lv_obj_t* child = lv_obj_get_child(btn, 0);
                    const char* fname = child ? lv_label_get_text(child) : nullptr;
                    if (fname) baby_pi_play_lullaby(fname);
                }, LV_EVENT_CLICKED, nullptr);
                lv_obj_add_event_cb(b, [](lv_event_t* e){
                    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
                    lv_obj_t* child = lv_obj_get_child(btn, 0);
                    const char* fname = child ? lv_label_get_text(child) : nullptr;
                    if (fname) build_rename_dialog(fname);
                }, LV_EVENT_LONG_PRESSED, nullptr);
            }
        }
#endif
    }, LV_EVENT_CLICKED, nullptr);

    // Stop playback button
    lv_obj_t* btn_stop_play = lv_btn_create(hdr_btns);
    style_button_tonal(btn_stop_play);
    { lv_obj_t* lbl = lv_label_create(btn_stop_play); lv_label_set_text(lbl, "Stop Play"); lv_obj_center(lbl); }
    lv_obj_add_event_cb(btn_stop_play, [](lv_event_t* /*e*/){
#if HAVE_CURL
        baby_pi_stop_playback();
#endif
    }, LV_EVENT_CLICKED, nullptr);

    // content placeholder
    lv_obj_t* content = lv_obj_create(sheet);
    lv_obj_set_size(content, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_style_bg_color(content, lv_color_hex(0x1E232A), 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(content, 1, 0);
    lv_obj_set_style_border_color(content, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_radius(content, 8, 0);
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);

    // list container
    g_lullabies_list = lv_obj_create(content);
    lv_obj_set_size(g_lullabies_list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(g_lullabies_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(g_lullabies_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(g_lullabies_list, 4, 0);
    lv_obj_set_style_pad_row(g_lullabies_list, 6, 0);

#if HAVE_CURL
    // initial populate
    if (baby_pi_list_lullabies(g_lullabies_files)) {
        lv_obj_clean(g_lullabies_list);
        for (const auto& name : g_lullabies_files) {
            lv_obj_t* b = lv_btn_create(g_lullabies_list);
            style_button_tonal(b);
            { lv_obj_t* l = lv_label_create(b); lv_label_set_text(l, name.c_str()); lv_obj_center(l); }
            lv_obj_add_event_cb(b, [](lv_event_t* e){
                lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
                lv_obj_t* child = lv_obj_get_child(btn, 0);
                const char* fname = child ? lv_label_get_text(child) : nullptr;
                if (fname) baby_pi_play_lullaby(fname);
            }, LV_EVENT_CLICKED, nullptr);
            lv_obj_add_event_cb(b, [](lv_event_t* e){
                lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
                lv_obj_t* child = lv_obj_get_child(btn, 0);
                const char* fname = child ? lv_label_get_text(child) : nullptr;
                if (fname) build_rename_dialog(fname);
            }, LV_EVENT_LONG_PRESSED, nullptr);
        }
    } else {
        lv_obj_t* lbl = lv_label_create(g_lullabies_list);
        lv_label_set_text(lbl, "Unable to fetch lullabies.");
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFF6B6B), 0);
    }
#endif

    lv_obj_add_event_cb(g_lullabies_modal, [](lv_event_t* e){
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) { close_lullabies(); }
    }, LV_EVENT_CLICKED, nullptr);

    log_line("[UI] lullabies opened");
}

static void close_lullabies() {
    if (g_lullabies_modal) {
        lv_obj_del(g_lullabies_modal);
        g_lullabies_modal = nullptr;
        g_lullabies_sheet = nullptr;
        log_line("[UI] lullabies closed");
    }
}

static void on_btn_play(lv_event_t* /*e*/) { set_status_text("Playing", lv_color_hex(0x3366FF)); }
static void on_btn_stop(lv_event_t* /*e*/) { set_status_text("Stopped", lv_color_hex(0x444444)); }

static void on_volume(lv_event_t* e) {
    lv_obj_t* sld = (lv_obj_t*)lv_event_get_target(e);
    g_volume = lv_slider_get_value(sld);
    update_top_label();
    log_line("[UI] volume changed");
}

static void on_switch_connected(lv_event_t* e) {
    lv_obj_t* sw = (lv_obj_t*)lv_event_get_target(e);
    g_connected = lv_obj_has_state(sw, LV_STATE_CHECKED);
    update_top_label();
    log_line("[UI] connected switch toggled");
}
static void on_switch_quiet_toggle(lv_event_t* e) {
    lv_obj_t* sw = (lv_obj_t*)lv_event_get_target(e);
    g_quiet_hours = lv_obj_has_state(sw, LV_STATE_CHECKED);
    update_top_label();
    log_line("[UI] quiet-hours switch toggled");
}

static void close_settings();
static void build_camera_dialog(lv_obj_t* parent);
static void close_camera();
static void apply_camera_ui_state();
static void on_cam_fullscreen(lv_event_t* e);
static void on_cam_snapshot(lv_event_t* e);
static void on_cam_play(lv_event_t* e);
static void on_cam_pause(lv_event_t* e);
static void on_cam_stop(lv_event_t* e);
static void on_cam_mute_toggle(lv_event_t* e);
static void on_shutdown(lv_event_t* e);

static void cam_spinner_show(uint32_t auto_hide_ms);
static void cam_spinner_hide();
static void update_dashboard();
static void apply_top_buttons_state();
static void on_top_quiet_click(lv_event_t* e);
static void on_top_conn_click(lv_event_t* e);

// ---------- Settings dialog ----------
static void build_settings_dialog(lv_obj_t* parent) {
    if (g_settings_modal) return;

    // dim backdrop
    g_settings_modal = lv_obj_create(parent);
    lv_obj_remove_style_all(g_settings_modal);
    lv_obj_set_size(g_settings_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(g_settings_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_settings_modal, LV_OPA_30, 0);
    lv_obj_add_flag(g_settings_modal, LV_OBJ_FLAG_CLICKABLE);

    // sheet
    lv_obj_t* sheet = lv_obj_create(g_settings_modal);
    lv_obj_set_size(sheet, LV_PCT(92), LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(sheet, LV_PCT(90), 0);
    lv_obj_center(sheet);

    style_sheet(sheet);

    // header row
    lv_obj_t* hdr_btns = nullptr;
    build_header(sheet, "Settings", &hdr_btns);

    lv_obj_t* btn_cancel = lv_btn_create(hdr_btns);
    style_button_tonal(btn_cancel);
    { lv_obj_t* lbl = lv_label_create(btn_cancel); lv_label_set_text(lbl, "Cancel"); lv_obj_center(lbl); }
    lv_obj_add_event_cb(btn_cancel, [](lv_event_t* /*e*/){ close_settings(); }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* btn_save = lv_btn_create(hdr_btns);
    style_button_tonal(btn_save);
    { lv_obj_t* lbl = lv_label_create(btn_save); lv_label_set_text(lbl, "Save"); lv_obj_center(lbl); }

    // Shutdown button
    lv_obj_t* btn_shutdown = lv_btn_create(hdr_btns);
    style_button_tonal(btn_shutdown);
    { lv_obj_t* lbl = lv_label_create(btn_shutdown); lv_label_set_text(lbl, "Shutdown"); lv_obj_center(lbl); }
    lv_obj_add_event_cb(btn_shutdown, on_shutdown, LV_EVENT_CLICKED, nullptr);

    auto make_row = [&](const char* left, lv_obj_t** outRow){
        lv_obj_t* r = lv_obj_create(sheet);
        lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(r, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(r, 0, 0);
        lv_obj_set_style_pad_all(r, 0, 0);
        lv_obj_set_style_pad_bottom(r, 6, 0);
        lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* l = lv_label_create(r);
        lv_label_set_text(l, left);
        lv_obj_set_width(l, LV_PCT(42));
        lv_obj_set_style_text_color(l, lv_color_hex(0xEDEFF2), 0);
        if (outRow) *outRow = r;
        return r;
    };

    // Default volume
    lv_obj_t* row_vol=nullptr; make_row("Default Volume", &row_vol);
    lv_obj_t* vol_col = lv_obj_create(row_vol);
    lv_obj_clear_flag(vol_col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(vol_col, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(vol_col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(vol_col, 0, 0);
    lv_obj_set_size(vol_col, LV_PCT(50), LV_SIZE_CONTENT);

    lv_obj_t* sld_default = lv_slider_create(vol_col);
    lv_obj_set_width(sld_default, LV_PCT(100));
    lv_obj_set_style_pad_ver(sld_default, 6, 0);
    lv_slider_set_range(sld_default, 0, 100);
    lv_slider_set_value(sld_default, g_volume, LV_ANIM_OFF);
    // slider styling
    lv_obj_set_style_bg_color(sld_default, lv_color_hex(0x2A2F36), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sld_default, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sld_default, lv_color_hex(0x7FB3FF), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(sld_default, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sld_default, lv_color_hex(0xEDEFF2), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(sld_default, LV_OPA_COVER, LV_PART_KNOB);

    // Quiet Hours Enabled
    lv_obj_t* row_qh=nullptr; make_row("Quiet Hours Enabled", &row_qh);
    lv_obj_t* sw_qh = lv_switch_create(row_qh);
    if (g_quiet_hours) lv_obj_add_state(sw_qh, LV_STATE_CHECKED);

    // Quiet Start Hour
    lv_obj_t* row_qstart=nullptr; make_row("Quiet Start Hour", &row_qstart);
    lv_obj_t* dd_start = lv_dropdown_create(row_qstart);
    lv_obj_set_width(dd_start, 160);
    lv_dropdown_set_options(dd_start,
        "12 AM\n1 AM\n2 AM\n3 AM\n4 AM\n5 AM\n6 AM\n7 AM\n8 AM\n9 AM\n10 AM\n11 AM\n"
        "12 PM\n1 PM\n2 PM\n3 PM\n4 PM\n5 PM\n6 PM\n7 PM\n8 PM\n9 PM\n10 PM\n11 PM");
    lv_dropdown_set_selected(dd_start, g_quiet_start);
    lv_obj_set_style_bg_color(dd_start, lv_color_hex(0x2A2F36), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dd_start, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_text_color(dd_start, lv_color_hex(0xEDEFF2), LV_PART_MAIN);

    // Quiet End Hour
    lv_obj_t* row_qend=nullptr; make_row("Quiet End Hour", &row_qend);
    lv_obj_t* dd_end = lv_dropdown_create(row_qend);
    lv_obj_set_width(dd_end, 160);
    lv_dropdown_set_options(dd_end,
        "12 AM\n1 AM\n2 AM\n3 AM\n4 AM\n5 AM\n6 AM\n7 AM\n8 AM\n9 AM\n10 AM\n11 AM\n"
        "12 PM\n1 PM\n2 PM\n3 PM\n4 PM\n5 PM\n6 PM\n7 PM\n8 PM\n9 PM\n10 PM\n11 PM");
    lv_dropdown_set_selected(dd_end, g_quiet_end);
    lv_obj_set_style_bg_color(dd_end, lv_color_hex(0x2A2F36), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dd_end, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_text_color(dd_end, lv_color_hex(0xEDEFF2), LV_PART_MAIN);

    // Save handler
    lv_obj_add_event_cb(btn_save, [](lv_event_t* e){
        lv_obj_t* btn       = (lv_obj_t*)lv_event_get_target(e);
        lv_obj_t* hdr_btns  = (lv_obj_t*)lv_obj_get_parent(btn);
        lv_obj_t* row_hdr   = (lv_obj_t*)lv_obj_get_parent(hdr_btns);
        lv_obj_t* sheet     = (lv_obj_t*)lv_obj_get_parent(row_hdr);

        lv_obj_t* row_vol   = (lv_obj_t*)lv_obj_get_child(sheet, 1);
        lv_obj_t* vol_col   = (lv_obj_t*)lv_obj_get_child(row_vol, 1);
        lv_obj_t* sld_def   = (lv_obj_t*)lv_obj_get_child(vol_col, 0);

        lv_obj_t* row_qh    = (lv_obj_t*)lv_obj_get_child(sheet, 2);
        lv_obj_t* sw_qh     = (lv_obj_t*)lv_obj_get_child(row_qh, 1);

        lv_obj_t* row_qstart= (lv_obj_t*)lv_obj_get_child(sheet, 3);
        lv_obj_t* dd_start  = (lv_obj_t*)lv_obj_get_child(row_qstart, 1);

        lv_obj_t* row_qend  = (lv_obj_t*)lv_obj_get_child(sheet, 4);
        lv_obj_t* dd_end    = (lv_obj_t*)lv_obj_get_child(row_qend, 1);

        g_volume      = lv_slider_get_value(sld_def);
        g_quiet_hours = lv_obj_has_state(sw_qh, LV_STATE_CHECKED);
        g_quiet_start = lv_dropdown_get_selected(dd_start);
        g_quiet_end   = lv_dropdown_get_selected(dd_end);

        update_top_label();
        log_line("[UI] settings saved");
        close_settings();
    }, LV_EVENT_CLICKED, nullptr);

    // click outside to dismiss
    lv_obj_add_event_cb(g_settings_modal, [](lv_event_t* e){
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            log_line("[UI] settings dismissed (backdrop)");
            close_settings();
        }
    }, LV_EVENT_CLICKED, nullptr);

    log_line("[UI] settings opened");
}

static void close_settings() {
    if (g_settings_modal) {
        lv_obj_del(g_settings_modal);
        g_settings_modal = nullptr;
        log_line("[UI] settings closed");
    }
}

// ---------- Camera dialog ----------
static void build_camera_dialog(lv_obj_t* parent) {
    if (g_camera_modal) return;

    // dim backdrop
    g_camera_modal = lv_obj_create(parent);
    lv_obj_remove_style_all(g_camera_modal);
    lv_obj_set_size(g_camera_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(g_camera_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_camera_modal, LV_OPA_30, 0);
    lv_obj_add_flag(g_camera_modal, LV_OBJ_FLAG_CLICKABLE);

    // sheet
    lv_obj_t* sheet = lv_obj_create(g_camera_modal);
    lv_obj_set_size(sheet, LV_PCT(94), LV_PCT(94));
    lv_obj_center(sheet);
    g_camera_sheet = sheet;

    lv_obj_clear_flag(sheet, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(sheet, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(sheet, lv_color_hex(0x16191D), 0);
    lv_obj_set_style_bg_opa(sheet, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(sheet, lv_color_hex(0xEDEFF2), 0);
    lv_obj_set_style_border_width(sheet, 1, 0);
    lv_obj_set_style_border_color(sheet, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_radius(sheet, 10, 0);
    lv_obj_set_style_shadow_width(sheet, 18, 0);
    lv_obj_set_style_shadow_opa(sheet, LV_OPA_30, 0);
    lv_obj_set_style_shadow_color(sheet, lv_color_hex(0x000000), 0);
    lv_obj_set_style_pad_hor(sheet, 16, 0);
    lv_obj_set_style_pad_ver(sheet, 14, 0);
    lv_obj_set_flex_flow(sheet, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(sheet, 12, 0);

    // header row (title + actions)
    lv_obj_t* hdr_btns = nullptr;
    lv_obj_t* camera_hdr = build_header(sheet, "Camera", &hdr_btns);
    g_cam_meta_label = lv_label_create(camera_hdr);
    lv_label_set_text(g_cam_meta_label, "Idle");
    lv_obj_set_style_text_color(g_cam_meta_label, theme::text_subtle(), 0);

    // Snapshot button
    lv_obj_t* btn_snap = lv_btn_create(hdr_btns);
    style_button_tonal(btn_snap);
    { lv_obj_t* lbl = lv_label_create(btn_snap); lv_label_set_text(lbl, "Snapshot"); lv_obj_center(lbl); }
    lv_obj_add_event_cb(btn_snap, on_cam_snapshot, LV_EVENT_CLICKED, nullptr);

    // Fullscreen button
    g_cam_btn_full = lv_btn_create(hdr_btns);
    style_button_tonal(g_cam_btn_full);
    { lv_obj_t* lbl = lv_label_create(g_cam_btn_full); lv_label_set_text(lbl, "Fullscreen"); lv_obj_center(lbl); }
    lv_obj_add_event_cb(g_cam_btn_full, on_cam_fullscreen, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* btn_close = lv_btn_create(hdr_btns);
    style_button_tonal(btn_close);
    { lv_obj_t* lbl = lv_label_create(btn_close); lv_label_set_text(lbl, "Close"); lv_obj_center(lbl); }
    lv_obj_add_event_cb(btn_close, [](lv_event_t* /*e*/){ close_camera(); }, LV_EVENT_CLICKED, nullptr);

    // video surface (placeholder for GStreamer rendering)
    g_camera_surface = lv_obj_create(sheet);
    lv_obj_set_size(g_camera_surface, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(g_camera_surface, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_camera_surface, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_camera_surface, 1, 0);
    lv_obj_set_style_border_color(g_camera_surface, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_radius(g_camera_surface, 6, 0);
    lv_obj_clear_flag(g_camera_surface, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(g_camera_surface, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_grow(g_camera_surface, 1);
    // LIVE/IDLE label (simple text, no chip)
    g_cam_live_label = lv_label_create(g_camera_surface);
    lv_obj_set_style_text_color(g_cam_live_label, lv_color_white(), 0);
    lv_label_set_text(g_cam_live_label, "IDLE");
    lv_obj_align(g_cam_live_label, LV_ALIGN_TOP_LEFT, 12, 12);
    // embedded image target for frames
    g_cam_img = lv_image_create(g_camera_surface);
    lv_obj_center(g_cam_img);
    // spinner
    g_cam_spinner = lv_spinner_create(g_camera_surface);
    lv_spinner_set_anim_params(g_cam_spinner, 1000, 60);
    lv_obj_set_size(g_cam_spinner, 48, 48);
    lv_obj_center(g_cam_spinner);
    lv_obj_set_style_arc_color(g_cam_spinner, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_arc_color(g_cam_spinner, lv_color_hex(0x7FB3FF), LV_PART_INDICATOR);
    lv_obj_add_flag(g_cam_spinner, LV_OBJ_FLAG_HIDDEN);

    // controls row (moved below video)
    lv_obj_t* row_ctrls = lv_obj_create(sheet);
    lv_obj_set_size(row_ctrls, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(row_ctrls, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(row_ctrls, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(row_ctrls, lv_color_hex(0x1E232A), 0);
    lv_obj_set_style_bg_opa(row_ctrls, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row_ctrls, 1, 0);
    lv_obj_set_style_border_color(row_ctrls, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_border_side(row_ctrls, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_pad_all(row_ctrls, 8, 0);
    lv_obj_set_style_pad_column(row_ctrls, 10, 0);
    lv_obj_set_flex_flow(row_ctrls, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row_ctrls, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* btn_play = lv_btn_create(row_ctrls);
    lv_obj_set_style_radius(btn_play, 6, 0);
    lv_obj_set_style_bg_color(btn_play, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_bg_opa(btn_play, LV_OPA_40, 0);
    lv_obj_set_style_border_width(btn_play, 1, 0);
    lv_obj_set_style_border_color(btn_play, lv_color_hex(0x3A4048), 0);
    lv_obj_set_style_pad_hor(btn_play, 12, 0);
    lv_obj_set_style_pad_ver(btn_play, 8, 0);
    { lv_obj_t* lbl = lv_label_create(btn_play); lv_label_set_text(lbl, "Play"); lv_obj_center(lbl); }
    lv_obj_add_event_cb(btn_play, on_cam_play, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* btn_pause = lv_btn_create(row_ctrls);
    lv_obj_set_style_radius(btn_pause, 6, 0);
    lv_obj_set_style_bg_color(btn_pause, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_bg_opa(btn_pause, LV_OPA_40, 0);
    lv_obj_set_style_border_width(btn_pause, 1, 0);
    lv_obj_set_style_border_color(btn_pause, lv_color_hex(0x3A4048), 0);
    lv_obj_set_style_pad_hor(btn_pause, 12, 0);
    lv_obj_set_style_pad_ver(btn_pause, 8, 0);
    { lv_obj_t* lbl = lv_label_create(btn_pause); lv_label_set_text(lbl, "Pause"); lv_obj_center(lbl); }
    lv_obj_add_event_cb(btn_pause, on_cam_pause, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* btn_stop = lv_btn_create(row_ctrls);
    lv_obj_set_style_radius(btn_stop, 6, 0);
    lv_obj_set_style_bg_color(btn_stop, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_bg_opa(btn_stop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(btn_stop, 1, 0);
    lv_obj_set_style_border_color(btn_stop, lv_color_hex(0x3A4048), 0);
    lv_obj_set_style_pad_hor(btn_stop, 12, 0);
    lv_obj_set_style_pad_ver(btn_stop, 8, 0);
    { lv_obj_t* lbl = lv_label_create(btn_stop); lv_label_set_text(lbl, "Stop"); lv_obj_center(lbl); }
    lv_obj_add_event_cb(btn_stop, on_cam_stop, LV_EVENT_CLICKED, nullptr);

    // spacer
    lv_obj_t* spacer = lv_obj_create(row_ctrls);
    lv_obj_set_size(spacer, 1, 1);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_grow(spacer, 1);

    // mute switch + label
    g_cam_sw_mute = lv_switch_create(row_ctrls);
    if (g_cam_muted) lv_obj_add_state(g_cam_sw_mute, LV_STATE_CHECKED);
    lv_obj_add_event_cb(g_cam_sw_mute, on_cam_mute_toggle, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(g_cam_sw_mute, on_cam_mute_toggle, LV_EVENT_CLICKED, nullptr);
    { lv_obj_t* lbl = lv_label_create(row_ctrls); lv_label_set_text(lbl, "Mute"); }

    // click outside to dismiss
    lv_obj_add_event_cb(g_camera_modal, [](lv_event_t* e){
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            log_line("[UI] camera dismissed (backdrop)");
            close_camera();
        }
    }, LV_EVENT_CLICKED, nullptr);

    log_line("[UI] camera opened");
    apply_camera_ui_state();
}

static void close_camera() {
    if (g_camera_modal) {
        // Stop streaming if active
        if (g_cam_playing) {
#if HAVE_CURL
            baby_pi_stop_camera();
#endif
#if HAVE_GSTREAMER
            stop_gstreamer_receiver();
#endif
        }
        
        cam_spinner_hide();
        lv_obj_del(g_camera_modal);
        g_camera_modal = nullptr;
        g_camera_surface = nullptr;
        g_camera_sheet = nullptr;
        g_cam_spinner = nullptr;
        g_cam_live_label = nullptr;
        g_cam_meta_label = nullptr;
        g_cam_btn_full = nullptr;
        g_cam_sw_mute = nullptr;
        g_cam_fullscreen = false;
        g_cam_playing = false;
        log_line("[UI] camera closed");
    }
}

// ---------- Camera UI interactions ----------
static void apply_camera_ui_state() {
    // Update fullscreen button label
    if (g_cam_btn_full) {
        lv_obj_t* lbl = lv_obj_get_child(g_cam_btn_full, 0);
        if (lbl) lv_label_set_text(lbl, g_cam_fullscreen ? "Exit Fullscreen" : "Fullscreen");
    }
    // Update mute switch visual state
    if (g_cam_sw_mute) {
        if (g_cam_muted) lv_obj_add_state(g_cam_sw_mute, LV_STATE_CHECKED);
        else lv_obj_clear_state(g_cam_sw_mute, LV_STATE_CHECKED);
    }
    // Update LIVE/IDLE label and spinner
    if (g_cam_live_label) {
        lv_label_set_text(g_cam_live_label, g_cam_playing ? "LIVE" : "IDLE");
        lv_obj_set_style_text_color(g_cam_live_label, g_cam_playing ? lv_color_hex(0xFF6B6B) : lv_color_white(), 0);
    }
    if (g_cam_meta_label) {
        lv_label_set_text(g_cam_meta_label, g_cam_playing ? "Live" : "Idle");
        lv_obj_set_style_text_color(g_cam_meta_label, g_cam_playing ? lv_color_hex(0xB0D7FF) : lv_color_hex(0x9AA3AD), 0);
    }
    // Spinner visibility is controlled explicitly via cam_spinner_show/hide
    // Apply sheet sizing for fullscreen
    if (g_camera_sheet) {
        if (g_cam_fullscreen) {
            lv_obj_set_size(g_camera_sheet, LV_PCT(100), LV_PCT(100));
            lv_obj_set_style_radius(g_camera_sheet, 0, 0);
            lv_obj_set_style_pad_hor(g_camera_sheet, 8, 0);
            lv_obj_set_style_pad_ver(g_camera_sheet, 8, 0);
        } else {
            lv_obj_set_size(g_camera_sheet, LV_PCT(94), LV_PCT(94));
            lv_obj_set_style_radius(g_camera_sheet, 10, 0);
            lv_obj_set_style_pad_hor(g_camera_sheet, 16, 0);
            lv_obj_set_style_pad_ver(g_camera_sheet, 14, 0);
        }
    }
}

// ---------- Dashboard updater ----------
static void update_dashboard() {
    if (!g_dash_card) return;

    // Determine status color and apply to hero ring
    const char* st = g_lbl_status ? lv_label_get_text(g_lbl_status) : "Calm";
    lv_color_t ring = lv_color_hex(0x22AA22);
    if (st && std::string(st) == "Cry")       ring = lv_color_hex(0xCC2222);
    else if (st && std::string(st) == "Motion") ring = lv_color_hex(0xD08770);
    if (g_dash_ring) {
        lv_obj_set_style_bg_opa(g_dash_ring, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(g_dash_ring, 10, 0);
        lv_obj_set_style_border_color(g_dash_ring, ring, 0);
        lv_obj_set_style_radius(g_dash_ring, LV_RADIUS_CIRCLE, 0);
    }

    // Connection stat
    if (g_stat_conn) {
        lv_label_set_text(g_stat_conn, g_connected ? "Connected" : "Disconnected");
        lv_obj_set_style_text_color(g_stat_conn, g_connected ? lv_color_hex(0xB0D7FF) : lv_color_hex(0xFF6B6B), 0);
    }
    // Volume stat
    if (g_stat_vol) {
        std::string s = std::to_string(g_volume) + "%";
        lv_label_set_text(g_stat_vol, s.c_str());
    }
    // Quiet Hours stat
    if (g_stat_qh) {
        if (g_quiet_hours) {
            std::string s = "On  ";
            s += format_hour12(g_quiet_start);
            s += " - ";
            s += format_hour12(g_quiet_end);
            lv_label_set_text(g_stat_qh, s.c_str());
            lv_obj_set_style_text_color(g_stat_qh, lv_color_hex(0xB0D7FF), 0);
        } else {
            lv_label_set_text(g_stat_qh, "Off");
            lv_obj_set_style_text_color(g_stat_qh, lv_color_hex(0x9AA3AD), 0);
        }
    }
}

// ---------- Top bar quick-action state ----------
static void apply_top_buttons_state() {
    // Quiet button
    if (g_btn_quiet) {
        lv_color_t bg = g_quiet_hours ? lv_color_hex(0x2F3B46) : lv_color_hex(0x2A2F36);
        lv_color_t fg = g_quiet_hours ? lv_color_hex(0xB0D7FF) : lv_color_hex(0xEDEFF2);
        lv_obj_set_style_bg_color(g_btn_quiet, bg, 0);
        lv_obj_set_style_bg_opa(g_btn_quiet, LV_OPA_40, 0);
        lv_obj_t* lbl = lv_obj_get_child(g_btn_quiet, 0);
        if (lbl) {
            lv_label_set_text(lbl, g_quiet_hours ? "Quiet On" : "Quiet Off");
            lv_obj_set_style_text_color(lbl, fg, 0);
        }
    }
    // Connection button
    if (g_btn_conn) {
        lv_color_t bg = g_connected ? lv_color_hex(0x294236) : lv_color_hex(0x3F2A2A);
        lv_color_t fg = g_connected ? lv_color_hex(0xB2F5DC) : lv_color_hex(0xFFB3B3);
        lv_obj_set_style_bg_color(g_btn_conn, bg, 0);
        lv_obj_set_style_bg_opa(g_btn_conn, LV_OPA_40, 0);
        lv_obj_t* lbl = lv_obj_get_child(g_btn_conn, 0);
        if (lbl) {
            lv_label_set_text(lbl, g_connected ? "Connected" : "Offline");
            lv_obj_set_style_text_color(lbl, fg, 0);
        }
    }
}

static void on_cam_fullscreen(lv_event_t* /*e*/) {
    g_cam_fullscreen = !g_cam_fullscreen;
    log_line(g_cam_fullscreen ? "[UI] camera fullscreen on" : "[UI] camera fullscreen off");
    apply_camera_ui_state();
}

static void on_cam_snapshot(lv_event_t* /*e*/) {
    log_line("[UI] camera snapshot requested");
    // No-op for now; integrate capture once streaming is wired
}

static void on_cam_play(lv_event_t* /*e*/) {
    g_cam_playing = true;
    log_line("[UI] camera play");
    cam_spinner_show(2000);  // Show spinner for 2 seconds while starting
    
#if HAVE_GSTREAMER
    // Start GStreamer receiver first
    start_gstreamer_receiver();
    
    // Small delay to let receiver bind port
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
#endif

#if HAVE_CURL
    // Start Baby Pi camera streaming
    baby_pi_start_camera();
#endif
    
    apply_camera_ui_state();
}

static void on_cam_pause(lv_event_t* /*e*/) {
    g_cam_playing = false;
    log_line("[UI] camera pause");
    cam_spinner_hide();
    // Note: This just pauses display, doesn't stop streaming
    apply_camera_ui_state();
}

static void on_cam_stop(lv_event_t* /*e*/) {
    g_cam_playing = false;
    log_line("[UI] camera stop");
    cam_spinner_hide();
    
#if HAVE_CURL
    // Stop Baby Pi camera streaming
    baby_pi_stop_camera();
#endif

#if HAVE_GSTREAMER
    // Stop GStreamer receiver
    stop_gstreamer_receiver();
#endif
    
    apply_camera_ui_state();
}

static void on_cam_mute_toggle(lv_event_t* e) {
    lv_obj_t* sw = (lv_obj_t*)lv_event_get_target(e);
    g_cam_muted = lv_obj_has_state(sw, LV_STATE_CHECKED);
    log_line(g_cam_muted ? "[UI] camera muted" : "[UI] camera unmuted");
    apply_camera_ui_state();
}

static void on_shutdown(lv_event_t* /*e*/) {
    log_line("[UI] shutdown requested");
    std::system("sudo /usr/local/bin/cribguard-shutdown.sh &");
}

// ---------- Spinner helpers ----------
static void cam_spinner_show(uint32_t auto_hide_ms) {
    g_cam_show_spinner = true;
    if (g_cam_spinner) lv_obj_clear_flag(g_cam_spinner, LV_OBJ_FLAG_HIDDEN);
    if (g_cam_spinner_timer) { lv_timer_del(g_cam_spinner_timer); g_cam_spinner_timer = nullptr; }
    if (auto_hide_ms > 0) {
        g_cam_spinner_timer = lv_timer_create([](lv_timer_t* t){
            g_cam_show_spinner = false;
            if (g_cam_spinner) lv_obj_add_flag(g_cam_spinner, LV_OBJ_FLAG_HIDDEN);
            lv_timer_del(t);
            g_cam_spinner_timer = nullptr;
        }, auto_hide_ms, nullptr);
        lv_timer_set_repeat_count(g_cam_spinner_timer, 1);
    }
}

static void cam_spinner_hide() {
    g_cam_show_spinner = false;
    if (g_cam_spinner) lv_obj_add_flag(g_cam_spinner, LV_OBJ_FLAG_HIDDEN);
    if (g_cam_spinner_timer) { lv_timer_del(g_cam_spinner_timer); g_cam_spinner_timer = nullptr; }
}

// ---------- Top bar quick-action handlers ----------
static void on_top_quiet_click(lv_event_t* /*e*/) {
    g_quiet_hours = !g_quiet_hours;
    update_top_label();
    apply_top_buttons_state();
}

static void on_top_conn_click(lv_event_t* /*e*/) {
    g_connected = !g_connected;
    update_top_label();
    apply_top_buttons_state();
}
// ---------- Build UI ----------
static void build_ui() {
    // Root screen (no scroll)
    lv_obj_t* screen = lv_obj_create(nullptr);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x0E1116), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_screen_load(screen);

    // Top bar (fixed)
    lv_obj_t* top = lv_obj_create(screen);
    lv_obj_set_size(top, LV_PCT(100), 64);
    lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(top, LV_SCROLLBAR_MODE_OFF);

    lv_obj_set_style_bg_color(top, lv_color_hex(0x1E232A), 0);
    lv_obj_set_style_bg_opa(top, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(top, 16, 0);
    lv_obj_set_style_pad_ver(top, 10, 0);
    lv_obj_set_style_border_width(top, 1, 0);
    lv_obj_set_style_border_color(top, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_border_side(top, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_outline_opa(top, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_opa(top, LV_OPA_TRANSP, 0);

    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Left label (ellipsis)
    g_lbl_conn = lv_label_create(top);
    lv_obj_set_style_text_color(g_lbl_conn, lv_color_hex(0xEDEFF2), 0);
    lv_obj_set_style_pad_left(g_lbl_conn, 2, 0);
    lv_obj_set_style_pad_right(g_lbl_conn, 8, 0);
    update_top_label();

    // Right controls (quiet switch, connected switch, settings gear)
    lv_obj_t* right_grp = lv_obj_create(top);
    lv_obj_clear_flag(right_grp, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(right_grp, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(right_grp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_grp, 0, 0);
    lv_obj_set_style_pad_all(right_grp, 0, 0);
    lv_obj_set_style_pad_column(right_grp, 10, 0);
    lv_obj_set_flex_flow(right_grp, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(right_grp, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    // Library button
    {
        lv_obj_t* btn = lv_btn_create(right_grp);
        style_button_tonal(btn);
        { lv_obj_t* lbl = lv_label_create(btn); lv_label_set_text(lbl, "Library"); lv_obj_center(lbl); }
        lv_obj_add_event_cb(btn, [](lv_event_t* /*e*/){ build_library_dialog((lv_obj_t*)lv_screen_active()); }, LV_EVENT_CLICKED, nullptr);
    }

    // Lullabies button
    {
        lv_obj_t* btn = lv_btn_create(right_grp);
        style_button_tonal(btn);
        { lv_obj_t* lbl = lv_label_create(btn); lv_label_set_text(lbl, "Lullabies"); lv_obj_center(lbl); }
        lv_obj_add_event_cb(btn, [](lv_event_t* /*e*/){ build_lullabies_dialog((lv_obj_t*)lv_screen_active()); }, LV_EVENT_CLICKED, nullptr);
    }

    // Quick-action pills instead of switches
    g_btn_quiet = lv_btn_create(right_grp);
    style_button_pill(g_btn_quiet);
    { lv_obj_t* lbl = lv_label_create(g_btn_quiet); lv_label_set_text(lbl, "Quiet"); lv_obj_center(lbl); }
    lv_obj_add_event_cb(g_btn_quiet, on_top_quiet_click, LV_EVENT_CLICKED, nullptr);

    g_btn_conn = lv_btn_create(right_grp);
    style_button_pill(g_btn_conn);
    { lv_obj_t* lbl = lv_label_create(g_btn_conn); lv_label_set_text(lbl, "Conn"); lv_obj_center(lbl); }
    lv_obj_add_event_cb(g_btn_conn, on_top_conn_click, LV_EVENT_CLICKED, nullptr);

    // Camera open button
    lv_obj_t* btn_cam = lv_btn_create(right_grp);
    style_button_tonal(btn_cam);
    { lv_obj_t* lbl = lv_label_create(btn_cam); lv_label_set_text(lbl, "Cam"); lv_obj_center(lbl); }
    lv_obj_add_event_cb(btn_cam, [](lv_event_t* /*e*/){ build_camera_dialog((lv_obj_t*)lv_screen_active()); }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* btn_gear = lv_btn_create(right_grp);
    style_button_tonal(btn_gear);
    { lv_obj_t* lbl = lv_label_create(btn_gear); lv_label_set_text(lbl, LV_SYMBOL_SETTINGS); lv_obj_center(lbl); }
    lv_obj_add_event_cb(btn_gear, [](lv_event_t* /*e*/){ build_settings_dialog((lv_obj_t*)lv_screen_active()); }, LV_EVENT_CLICKED, nullptr);

    apply_top_buttons_state();

    // Center dashboard card
    g_dash_card = lv_obj_create(screen);
    lv_obj_set_size(g_dash_card, LV_PCT(92), 360);
    lv_obj_align(g_dash_card, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_bg_color(g_dash_card, lv_color_hex(0x16191D), 0);
    lv_obj_set_style_bg_opa(g_dash_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_dash_card, 1, 0);
    lv_obj_set_style_border_color(g_dash_card, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_radius(g_dash_card, 10, 0);
    lv_obj_set_style_pad_hor(g_dash_card, 16, 0);
    lv_obj_set_style_pad_ver(g_dash_card, 14, 0);
    lv_obj_set_flex_flow(g_dash_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(g_dash_card, 12, 0);

    // Hero area with ring and status label
    g_dash_hero = lv_obj_create(g_dash_card);
    lv_obj_set_size(g_dash_hero, LV_PCT(100), 220);
    lv_obj_set_style_bg_color(g_dash_hero, lv_color_hex(0x1E232A), 0);
    lv_obj_set_style_bg_opa(g_dash_hero, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_dash_hero, 1, 0);
    lv_obj_set_style_border_color(g_dash_hero, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_radius(g_dash_hero, 8, 0);
    lv_obj_set_style_pad_all(g_dash_hero, 0, 0);

    g_dash_ring = lv_obj_create(g_dash_hero);
    lv_obj_set_size(g_dash_ring, 200, 200);
    lv_obj_set_style_bg_opa(g_dash_ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_dash_ring, 10, 0);
    lv_obj_set_style_border_color(g_dash_ring, lv_color_hex(0x22AA22), 0);
    lv_obj_set_style_radius(g_dash_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_center(g_dash_ring);

    g_lbl_status = lv_label_create(g_dash_hero);
    lv_label_set_text(g_lbl_status, "Calm");
    lv_obj_set_style_text_font(g_lbl_status, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(g_lbl_status, lv_color_hex(0x22AA22), 0);
    lv_obj_set_style_text_align(g_lbl_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_letter_space(g_lbl_status, 1, 0);
    lv_obj_center(g_lbl_status);

    // Stats row
    lv_obj_t* stats = lv_obj_create(g_dash_card);
    lv_obj_set_size(stats, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(stats, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(stats, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(stats, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(stats, 0, 0);
    lv_obj_set_style_pad_all(stats, 0, 0);
    lv_obj_set_style_pad_column(stats, 12, 0);
    lv_obj_set_flex_flow(stats, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(stats, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto make_tile = [&](const char* title, lv_obj_t** outVal){
        lv_obj_t* tile = lv_obj_create(stats);
        lv_obj_set_size(tile, LV_PCT(32), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(tile, lv_color_hex(0x1E232A), 0);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(tile, 1, 0);
        lv_obj_set_style_border_color(tile, lv_color_hex(0x2A2F36), 0);
        lv_obj_set_style_radius(tile, 8, 0);
        lv_obj_set_style_pad_hor(tile, 12, 0);
        lv_obj_set_style_pad_ver(tile, 10, 0);
        lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(tile, 6, 0);

        lv_obj_t* t = lv_label_create(tile);
        lv_label_set_text(t, title);
        lv_obj_set_style_text_color(t, lv_color_hex(0x9AA3AD), 0);

        lv_obj_t* v = lv_label_create(tile);
        lv_obj_set_style_text_color(v, lv_color_hex(0xEDEFF2), 0);
        lv_label_set_text(v, "--");
        if (outVal) *outVal = v;
        return tile;
    };

    make_tile("Connection", &g_stat_conn);
    make_tile("Volume",     &g_stat_vol);
    make_tile("Quiet Hours",&g_stat_qh);

    update_dashboard();

    // Status buttons row
    lv_obj_t* row1 = lv_obj_create(screen);
    lv_obj_set_size(row1, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_align(row1, LV_ALIGN_BOTTOM_MID, 0, -100);
    lv_obj_set_style_bg_color(row1, lv_color_hex(0x16191D), 0);
    lv_obj_set_style_bg_opa(row1, LV_OPA_COVER, 0);
    lv_obj_clear_flag(row1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(row1, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(row1, 12, 0);
    lv_obj_set_style_border_width(row1, 1, 0);
    lv_obj_set_style_border_color(row1, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_radius(row1, 8, 0);
    lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row1, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row1, 14, 0);

    auto make_btn = [&](const char* txt, lv_event_cb_t cb, const char* role) {
        lv_obj_t* b = lv_btn_create(row1);
        lv_obj_set_size(b, 160, 56);
        lv_obj_set_style_radius(b, 8, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x2A2F36), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_40, 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_border_color(b, lv_color_hex(0x3A4048), 0);
        lv_obj_set_style_pad_hor(b, 12, 0);
        lv_obj_set_style_pad_ver(b, 8, 0);
        lv_obj_t* l = lv_label_create(b);
        lv_label_set_text(l, txt);
        lv_obj_center(l);
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void*)role);
        return b;
    };
    make_btn("Calm",   on_btn_status, (const char*)"calm");
    make_btn("Cry",    on_btn_status, (const char*)"cry");
    make_btn("Motion", on_btn_status, (const char*)"motion");

    // Controls row (Play/Stop/Volume)
    lv_obj_t* row2 = lv_obj_create(screen);
    lv_obj_set_size(row2, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_align(row2, LV_ALIGN_BOTTOM_MID, 0, -24);
    lv_obj_set_style_bg_color(row2, lv_color_hex(0x1E232A), 0);
    lv_obj_set_style_bg_opa(row2, LV_OPA_COVER, 0);
    lv_obj_clear_flag(row2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(row2, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_border_width(row2, 1, 0);
    lv_obj_set_style_border_color(row2, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_radius(row2, 8, 0);
    lv_obj_set_style_pad_all(row2, 10, 0);
    lv_obj_set_flex_flow(row2, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row2, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row2, 18, 0);

    lv_obj_t* btnPlay = lv_btn_create(row2);
    lv_obj_set_size(btnPlay, 150, 56);
    lv_obj_set_style_radius(btnPlay, 8, 0);
    lv_obj_set_style_bg_color(btnPlay, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_bg_opa(btnPlay, LV_OPA_40, 0);
    lv_obj_set_style_border_width(btnPlay, 1, 0);
    lv_obj_set_style_border_color(btnPlay, lv_color_hex(0x3A4048), 0);
    lv_obj_set_style_pad_hor(btnPlay, 12, 0);
    lv_obj_set_style_pad_ver(btnPlay, 8, 0);
    { lv_obj_t* l = lv_label_create(btnPlay); lv_label_set_text(l, "Play"); lv_obj_center(l); }
    lv_obj_add_event_cb(btnPlay, on_btn_play, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* btnStop = lv_btn_create(row2);
    lv_obj_set_size(btnStop, 150, 56);
    lv_obj_set_style_radius(btnStop, 8, 0);
    lv_obj_set_style_bg_color(btnStop, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_bg_opa(btnStop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(btnStop, 1, 0);
    lv_obj_set_style_border_color(btnStop, lv_color_hex(0x3A4048), 0);
    lv_obj_set_style_pad_hor(btnStop, 12, 0);
    lv_obj_set_style_pad_ver(btnStop, 8, 0);
    { lv_obj_t* l = lv_label_create(btnStop); lv_label_set_text(l, "Stop"); lv_obj_center(l); }
    lv_obj_add_event_cb(btnStop, on_btn_stop, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* slider = lv_slider_create(row2);
    lv_obj_set_size(slider, 360, 10);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, g_volume, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, on_volume, LV_EVENT_VALUE_CHANGED, nullptr);
    // slider styling
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x2A2F36), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x7FB3FF), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xEDEFF2), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);

    log_line("[SIM] UI built");
}

// ---------- Main ----------
int main(int argc, char** argv) {
    g_log_file = std::fopen("sim.log", "w");
    
    // Load configuration
    load_config();
    
    // Initialize GStreamer if available
#if HAVE_GSTREAMER
    gst_init(&argc, &argv);
    log_line("[GST] init complete");
#else
    log_line("[GST] WARNING: GStreamer not available, camera streaming disabled");
#endif

#if HAVE_CURL
    curl_global_init(CURL_GLOBAL_DEFAULT);
    log_line("[HTTP] curl initialized");
#else
    log_line("[HTTP] WARNING: libcurl not available, camera control disabled");
#endif
    
    lv_init();
    log_line("[SIM] lv_init ok");

  // Determine target window size. Default to 1280x720 landscape,
  // but if SDL is available at compile-time, query current display mode
  // and normalize to landscape so the UI lays out correctly.
  int SCR_W = 1280;
  int SCR_H = 720;
#if HAVE_SDL2_HEADER
  {
      if (SDL_InitSubSystem(SDL_INIT_VIDEO) == 0) {
          SDL_DisplayMode mode;
          if (SDL_GetCurrentDisplayMode(0, &mode) == 0 && mode.w > 0 && mode.h > 0) {
              SCR_W = mode.w;
              SCR_H = mode.h;
              if (SCR_W < SCR_H) { int t = SCR_W; SCR_W = SCR_H; SCR_H = t; }
          }
          SDL_QuitSubSystem(SDL_INIT_VIDEO);
      }
  }
#endif

    lv_display_t* disp = lv_sdl_window_create(SCR_W, SCR_H);
    if (!disp) {
        log_line("[SIM] ERROR: lv_sdl_window_create failed");
        if (g_log_file) std::fclose(g_log_file);
        return 1;
    }
  {
      char buf[96];
      std::snprintf(buf, sizeof(buf), "[SIM] SDL window created %dx%d", SCR_W, SCR_H);
      log_line(buf);
  }

    // Install signal handlers so UI exits cleanly on gpio-shutdown (power button) or Ctrl+C
    std::signal(SIGINT,  handle_shutdown_signal);
    std::signal(SIGTERM, handle_shutdown_signal);
#ifdef SIGHUP
    std::signal(SIGHUP,  handle_shutdown_signal);
#endif
#ifdef SIGQUIT
    std::signal(SIGQUIT, handle_shutdown_signal);
#endif

    // Attach pointer input for touchscreen simulation (uses SDL pointer)
#if HAVE_SDL_MOUSE
    {
        lv_indev_t* m = lv_sdl_mouse_create();
        if (m) log_line("[SIM] pointer input attached");
        else   log_line("[SIM] WARNING: pointer input create returned null");
    }
#else
    log_line("[SIM] WARNING: SDL pointer header not found; no touch input");
#endif

#if HAVE_SDL_WHEEL
    {
        lv_indev_t* w = lv_sdl_mousewheel_create();
        if (w) log_line("[SIM] mousewheel attached");
        else   log_line("[SIM] WARNING: mousewheel create returned null");
    }
#endif

    build_ui();
    log_line("[SIM] entering loop");

    while (!g_quit) {
        lv_timer_handler();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        lv_tick_inc(5); // advance LVGL tick so events/timeouts fire
    }

    log_line("[SIM] loop exit");
    
    // Cleanup GStreamer
#if HAVE_GSTREAMER
    if (g_gst_pipeline) {
        stop_gstreamer_receiver();
    }
    gst_deinit();
    log_line("[GST] cleanup complete");
#endif

#if HAVE_CURL
    curl_global_cleanup();
    log_line("[HTTP] curl cleanup complete");
#endif
    
    if (g_log_file) std::fclose(g_log_file);
    return 0;
}
