#pragma once

#include <string>
#include <vector>

// Parent Pi -> Baby Pi HTTP API helpers.
//
// Note: These functions are implemented as no-op stubs if libcurl headers
// are not available at build time.

void baby_pi_start_camera();
void baby_pi_stop_camera();
bool baby_pi_check_status();
bool baby_pi_get_wet_status(std::string& out_state);
bool baby_pi_get_cry_status(std::string& out_state);

// Get/set the wet detector's ROI box (raw pixels in the 160x120 frame).
// Set returns true only when the detector was hot-swapped via SIGHUP.
bool baby_pi_get_wet_roi(int& x_start, int& y_start, int& x_end, int& y_end);
bool baby_pi_set_wet_roi(int x_start, int y_start, int x_end, int y_end);
bool baby_pi_listen_start();
bool baby_pi_listen_stop();

void baby_pi_record_audio(int seconds);

bool baby_pi_list_lullabies(std::vector<std::string>& out_files);
void baby_pi_play_lullaby(const std::string& filename, int volume_percent = -1);
void baby_pi_play_default_lullaby(int volume_percent = -1);
void baby_pi_set_lullaby_volume(int volume_percent);
bool baby_pi_rename_lullaby(const std::string& old_name, const std::string& new_name_with_ext);
void baby_pi_stop_playback();

