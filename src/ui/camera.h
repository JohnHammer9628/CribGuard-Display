// src/ui/camera.h
#pragma once

extern "C" {
#include "lvgl.h"
}

namespace cg::ui {
void build_camera_dialog(lv_obj_t* parent);
void start_camera_always_on();
} // namespace cg::ui

// Needed by the GStreamer receiver (`src/camera_rx_gst.cpp`) to push frames into LVGL.
// This is intentionally in the global namespace for compatibility.
extern lv_obj_t* g_cam_img;

