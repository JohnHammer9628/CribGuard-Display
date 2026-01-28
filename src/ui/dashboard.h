// src/ui/dashboard.h
#pragma once

extern "C" {
#include "lvgl.h"
}

namespace cg::ui {
// Build the main (non-modal) UI on the active screen.
void build_main_screen();

// Called by other modules when a shared setting changes.
void update_top_label();
} // namespace cg::ui

