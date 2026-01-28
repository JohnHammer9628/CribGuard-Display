// src/config.cpp
//
// Runtime configuration loader for the Parent Pi application.
//
// Reads `settings.cfg` (key=value, '#' comments) from the working directory.
// These values are used by:
// - HTTP client (`src/baby_pi_api.*`) to reach the Baby Pi
// - GStreamer receiver (`src/camera_rx_gst.*`) for UDP port selection
// - UI (`src/ui/*`) for record seconds + audio device when sending commands
#include "config.h"

#include "logging.h"

#include <cstdlib>
#include <fstream>

std::string g_baby_pi_ip = "10.0.0.153";
int g_baby_pi_port = 8000;
int g_camera_stream_port = 5000;
int g_audio_record_seconds = 30;
std::string g_audio_play_device = "default";

// Load `settings.cfg` into globals in `src/config.h`.
// Missing file is not fatal: defaults remain in effect.
void load_config() {
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

