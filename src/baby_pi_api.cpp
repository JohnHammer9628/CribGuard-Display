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

// Ask the Baby Pi to play a given lullaby file on the configured audio device.
void baby_pi_play_lullaby(const std::string& filename) {
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
void baby_pi_record_audio(int) { curl_unavailable("record_audio"); }
bool baby_pi_list_lullabies(std::vector<std::string>& out_files) { out_files.clear(); curl_unavailable("list_lullabies"); return false; }
void baby_pi_play_lullaby(const std::string&) { curl_unavailable("play_lullaby"); }
bool baby_pi_rename_lullaby(const std::string&, const std::string&) { curl_unavailable("rename_lullaby"); return false; }
void baby_pi_stop_playback() { curl_unavailable("stop_playback"); }

#endif

