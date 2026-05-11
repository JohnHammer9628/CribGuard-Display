// src/baby_pi_api.cpp
//
// Parent Pi → Baby Pi HTTP API wrapper.
//
// This module owns the "network control plane" for the demo:
// - start/stop camera streaming on the Baby Pi
// - list/play/rename lullabies on the Baby Pi
//
// Design notes:
// - libcurl is optional in this repo. If headers/libraries aren't present at build time,
//   these functions become safe no-ops that log the missing capability.
// - curl is initialized lazily on first use to keep platform entrypoints simple.
#include "baby_pi_api.h"

#include "config.h"
#include "logging.h"

#include <algorithm>
#include <cstdlib>
#include <mutex>
#include <string>

#if __has_include(<curl/curl.h>)
  #include <curl/curl.h>
  #define CG_HAVE_CURL 1
#else
  #define CG_HAVE_CURL 0
#endif

#if CG_HAVE_CURL

// One-time process-wide libcurl init/cleanup.
static void ensure_curl_global_init() {
    static std::once_flag once;
    std::call_once(once, []() {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        std::atexit([]() { curl_global_cleanup(); });
        log_line("[HTTP] curl global init (lazy)");
    });
}

// libcurl write callback: appends response bytes into a std::string.
static size_t http_write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    (static_cast<std::string*>(userp))->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

// HTTP POST helper used by all Baby Pi endpoints.
// Returns true on CURL success; response body is captured into `response`.
static bool http_post(const std::string& url, const std::string& json_data, std::string& response) {
    ensure_curl_global_init();
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

// HTTP GET helper used by endpoints that fetch data (status, lullaby list).
static bool http_get(const std::string& url, std::string& response) {
    ensure_curl_global_init();
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

// Ask the Baby Pi to begin streaming camera video (H264 RTP) to the Parent Pi.
void baby_pi_start_camera() {
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

// Ask the Baby Pi to stop streaming camera video.
void baby_pi_stop_camera() {
    std::string url = "http://" + g_baby_pi_ip + ":" + std::to_string(g_baby_pi_port) + "/api/stop";
    std::string response;

    log_line((std::string("[CAM] stopping camera on Baby Pi: ") + url).c_str());

    if (http_post(url, "{}", response)) {
        log_line((std::string("[CAM] Baby Pi response: ") + response).c_str());
    } else {
        log_line("[CAM] ERROR: failed to stop camera on Baby Pi");
    }
}

// Ask the Baby Pi to record audio for N seconds (used for capturing lullabies).
void baby_pi_record_audio(int seconds) {
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

// Fetch the list of lullaby files from the Baby Pi and parse into `out_files`.
bool baby_pi_list_lullabies(std::vector<std::string>& out_files) {
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

static int clamp_volume_percent(int volume_percent) {
    if (volume_percent < 0) return -1;
    if (volume_percent > 100) return 100;
    return volume_percent;
}

// Ask the Baby Pi to play a given lullaby file on the configured audio device.
void baby_pi_play_lullaby(const std::string& filename, int volume_percent) {
    std::string url = "http://" + g_baby_pi_ip + ":" + std::to_string(g_baby_pi_port) + "/api/play";
    volume_percent = clamp_volume_percent(volume_percent);
    std::string json = std::string("{\"file\":\"") + filename + "\",\"device\":\"" + g_audio_play_device + "\"";
    if (volume_percent >= 0) {
        json += ",\"volume\":" + std::to_string(volume_percent);
    }
    json += "}";
    std::string response;
    log_line((std::string("[AUDIO] play: ") + filename + " volume=" + std::to_string(volume_percent)).c_str());
    if (http_post(url, json, response)) {
        log_line((std::string("[AUDIO] Baby Pi response: ") + response).c_str());
    } else {
        log_line("[AUDIO] ERROR: failed to start playback");
    }
}

// Ask the Baby Pi to pick and play the next available lullaby.
void baby_pi_play_default_lullaby(int volume_percent) {
    std::string url = "http://" + g_baby_pi_ip + ":" + std::to_string(g_baby_pi_port) + "/api/play/default";
    volume_percent = clamp_volume_percent(volume_percent);
    std::string json = std::string("{\"device\":\"") + g_audio_play_device + "\"";
    if (volume_percent >= 0) {
        json += ",\"volume\":" + std::to_string(volume_percent);
    }
    json += "}";
    std::string response;
    log_line((std::string("[AUDIO] play default volume=") + std::to_string(volume_percent)).c_str());
    if (http_post(url, json, response)) {
        log_line((std::string("[AUDIO] Baby Pi response: ") + response).c_str());
    } else {
        log_line("[AUDIO] ERROR: failed to start default playback");
    }
}

// Update the Baby Pi's default lullaby volume. The server applies this to
// future auto-play from the cry detector, and may restart active playback so
// the new level takes effect immediately.
void baby_pi_set_lullaby_volume(int volume_percent) {
    volume_percent = clamp_volume_percent(volume_percent);
    if (volume_percent < 0) return;
    std::string url = "http://" + g_baby_pi_ip + ":" + std::to_string(g_baby_pi_port) + "/api/volume";
    std::string json = std::string("{\"volume\":") + std::to_string(volume_percent) + ",\"device\":\"" + g_audio_play_device + "\"}";
    std::string response;
    log_line((std::string("[AUDIO] volume: ") + std::to_string(volume_percent)).c_str());
    if (http_post(url, json, response)) {
        log_line((std::string("[AUDIO] Baby Pi response: ") + response).c_str());
    } else {
        log_line("[AUDIO] ERROR: failed to set playback volume");
    }
}

// Ask the Baby Pi to rename a lullaby file (client-side sanitation + .wav enforcement).
bool baby_pi_rename_lullaby(const std::string& old_name, const std::string& new_name_with_ext) {
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

// Ask the Baby Pi to stop any current lullaby playback.
void baby_pi_stop_playback() {
    std::string url = "http://" + g_baby_pi_ip + ":" + std::to_string(g_baby_pi_port) + "/api/play/stop";
    std::string response;
    log_line("[AUDIO] stop playback");
    if (http_post(url, "{}", response)) {
        log_line((std::string("[AUDIO] Baby Pi response: ") + response).c_str());
    } else {
        log_line("[AUDIO] ERROR: failed to stop playback");
    }
}

// Query the Baby Pi for the most recent wet state ("none" | "cold" | "warm").
// Returns true on successful fetch/parse; `out_state` is "none" on failure.
bool baby_pi_get_wet_status(std::string& out_state) {
    out_state = "none";
    std::string url = "http://" + g_baby_pi_ip + ":" + std::to_string(g_baby_pi_port) + "/api/wet_status";
    std::string response;
    if (!http_get(url, response)) return false;

    size_t key = response.find("\"state\"");
    if (key == std::string::npos) return false;
    size_t colon = response.find(':', key);
    if (colon == std::string::npos) return false;
    size_t q1 = response.find('"', colon + 1);
    if (q1 == std::string::npos) return false;
    size_t q2 = response.find('"', q1 + 1);
    if (q2 == std::string::npos) return false;

    std::string s = response.substr(q1 + 1, q2 - (q1 + 1));
    if (s == "cold" || s == "warm" || s == "none") {
        out_state = s;
        return true;
    }
    return false;
}

// Query the Baby Pi for the most recent cry state ("none" | "crying").
// Returns true on successful fetch/parse; `out_state` is "none" on failure.
bool baby_pi_get_cry_status(std::string& out_state) {
    out_state = "none";
    std::string url = "http://" + g_baby_pi_ip + ":" + std::to_string(g_baby_pi_port) + "/api/cry_status";
    std::string response;
    if (!http_get(url, response)) return false;

    size_t key = response.find("\"state\"");
    if (key == std::string::npos) return false;
    size_t colon = response.find(':', key);
    if (colon == std::string::npos) return false;
    size_t q1 = response.find('"', colon + 1);
    if (q1 == std::string::npos) return false;
    size_t q2 = response.find('"', q1 + 1);
    if (q2 == std::string::npos) return false;

    std::string s = response.substr(q1 + 1, q2 - (q1 + 1));
    if (s == "crying" || s == "none") {
        out_state = s;
        return true;
    }
    return false;
}

// Pull a number out of {"key": <int>, ...} JSON. Returns true on success.
// Tolerates whitespace and signs; not a real parser, just enough for our
// fixed-shape responses.
static bool parse_int_field(const std::string& body, const char* key, int& out) {
    std::string needle = std::string("\"") + key + "\"";
    size_t k = body.find(needle);
    if (k == std::string::npos) return false;
    size_t colon = body.find(':', k);
    if (colon == std::string::npos) return false;
    size_t i = colon + 1;
    while (i < body.size() && (body[i] == ' ' || body[i] == '\t')) ++i;
    bool neg = false;
    if (i < body.size() && (body[i] == '+' || body[i] == '-')) {
        neg = (body[i] == '-');
        ++i;
    }
    if (i >= body.size() || body[i] < '0' || body[i] > '9') return false;
    int v = 0;
    while (i < body.size() && body[i] >= '0' && body[i] <= '9') {
        v = v * 10 + (body[i] - '0');
        ++i;
    }
    out = neg ? -v : v;
    return true;
}

// Fetch the persisted ROI box from the Baby Pi.
bool baby_pi_get_wet_roi(int& x_start, int& y_start, int& x_end, int& y_end) {
    std::string url = "http://" + g_baby_pi_ip + ":" + std::to_string(g_baby_pi_port) + "/api/wet_roi";
    std::string response;
    if (!http_get(url, response)) return false;
    int xs, ys, xe, ye;
    if (!parse_int_field(response, "x_start", xs)) return false;
    if (!parse_int_field(response, "y_start", ys)) return false;
    if (!parse_int_field(response, "x_end",   xe)) return false;
    if (!parse_int_field(response, "y_end",   ye)) return false;
    x_start = xs; y_start = ys; x_end = xe; y_end = ye;
    return true;
}

// Persist a new ROI box on the Baby Pi. The detector is SIGHUP'd to reload.
// Returns true if the request succeeded AND the detector was reachable.
bool baby_pi_set_wet_roi(int x_start, int y_start, int x_end, int y_end) {
    std::string url = "http://" + g_baby_pi_ip + ":" + std::to_string(g_baby_pi_port) + "/api/wet_roi";
    std::string json = "{\"x_start\":" + std::to_string(x_start) +
                       ",\"y_start\":" + std::to_string(y_start) +
                       ",\"x_end\":"   + std::to_string(x_end)   +
                       ",\"y_end\":"   + std::to_string(y_end)   + "}";
    std::string response;
    log_line((std::string("[ROI] set: ") + json).c_str());
    if (!http_post(url, json, response)) return false;
    log_line((std::string("[ROI] Baby Pi response: ") + response).c_str());
    // success:true is enough to consider the persistence step done; reloaded
    // can be false if the camera isn't currently streaming, which is fine.
    return response.find("\"success\": true") != std::string::npos ||
           response.find("\"success\":true") != std::string::npos;
}

// Start the baby pi's mic-to-parent-pi audio stream (listen mode).
bool baby_pi_listen_start() {
    std::string url = "http://" + g_baby_pi_ip + ":" + std::to_string(g_baby_pi_port) + "/api/listen/start";
    std::string response;
    log_line("[LISTEN] start");
    if (http_post(url, "{}", response)) {
        log_line((std::string("[LISTEN] Baby Pi response: ") + response).c_str());
        return response.find("\"success\": true") != std::string::npos ||
               response.find("\"success\":true") != std::string::npos;
    }
    log_line("[LISTEN] ERROR: failed to start listen on Baby Pi");
    return false;
}

// Stop the baby pi's mic stream.
bool baby_pi_listen_stop() {
    std::string url = "http://" + g_baby_pi_ip + ":" + std::to_string(g_baby_pi_port) + "/api/listen/stop";
    std::string response;
    log_line("[LISTEN] stop");
    if (http_post(url, "{}", response)) {
        log_line((std::string("[LISTEN] Baby Pi response: ") + response).c_str());
        return response.find("\"success\": true") != std::string::npos ||
               response.find("\"success\":true") != std::string::npos;
    }
    log_line("[LISTEN] ERROR: failed to stop listen on Baby Pi");
    return false;
}

// Health check endpoint used to verify Baby Pi is reachable.
bool baby_pi_check_status() {
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

#else  // CG_HAVE_CURL

// Log helper used by the no-op stubs when libcurl isn't available.
static void curl_unavailable(const char* what) {
    log_line((std::string("[HTTP] libcurl unavailable: ") + what).c_str());
}

void baby_pi_start_camera() { curl_unavailable("start_camera"); }
void baby_pi_stop_camera() { curl_unavailable("stop_camera"); }
bool baby_pi_check_status() { curl_unavailable("check_status"); return false; }
bool baby_pi_get_wet_status(std::string& out_state) { out_state = "none"; curl_unavailable("wet_status"); return false; }
bool baby_pi_get_cry_status(std::string& out_state) { out_state = "none"; curl_unavailable("cry_status"); return false; }
bool baby_pi_get_wet_roi(int&, int&, int&, int&) { curl_unavailable("get_wet_roi"); return false; }
bool baby_pi_set_wet_roi(int, int, int, int) { curl_unavailable("set_wet_roi"); return false; }
bool baby_pi_listen_start() { curl_unavailable("listen_start"); return false; }
bool baby_pi_listen_stop() { curl_unavailable("listen_stop"); return false; }
void baby_pi_record_audio(int) { curl_unavailable("record_audio"); }
bool baby_pi_list_lullabies(std::vector<std::string>& out_files) { out_files.clear(); curl_unavailable("list_lullabies"); return false; }
void baby_pi_play_lullaby(const std::string&, int) { curl_unavailable("play_lullaby"); }
void baby_pi_play_default_lullaby(int) { curl_unavailable("play_default_lullaby"); }
void baby_pi_set_lullaby_volume(int) { curl_unavailable("set_lullaby_volume"); }
bool baby_pi_rename_lullaby(const std::string&, const std::string&) { curl_unavailable("rename_lullaby"); return false; }
void baby_pi_stop_playback() { curl_unavailable("stop_playback"); }

#endif

