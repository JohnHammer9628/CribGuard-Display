#pragma once

#include <cstdint>
#include <vector>

// Starts/stops the Baby Pi camera UDP receiver (GStreamer pipeline).
//
// Implemented as no-op stubs if GStreamer headers are not available at build time.
void start_gstreamer_receiver();
void stop_gstreamer_receiver();

// Logs whether GStreamer support is compiled in and whether required plugins
// (notably avdec_h264) are available at runtime.
void log_gstreamer_support_status();

// Pulls the latest decoded RGBA frame from the receiver.
// Returns true if a newer frame was copied into `pixels` since the last pull.
// `frame_id` is monotonic and can be used by UI code to detect updates.
bool pull_gstreamer_frame(std::vector<uint8_t>& pixels, int& width, int& height, uint64_t& frame_id);

