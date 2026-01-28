#pragma once

#include <string>

// Parent Pi -> Baby Pi connectivity / runtime configuration.
extern std::string g_baby_pi_ip;
extern int g_baby_pi_port;
extern int g_camera_stream_port;
extern int g_audio_record_seconds;
extern std::string g_audio_play_device;

// Loads `settings.cfg` in the current working directory (if present).
void load_config();

