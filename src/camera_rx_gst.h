#pragma once

// Starts/stops the Baby Pi camera UDP receiver (GStreamer pipeline).
//
// Implemented as no-op stubs if GStreamer headers are not available at build time.
void start_gstreamer_receiver();
void stop_gstreamer_receiver();

