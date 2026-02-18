// src/ui/dashboard.h
#pragma once

extern "C" {
#include "lvgl.h"
}

namespace cg::ui {
// Build the main (non-modal) UI on the active screen.
void build_main_screen();
// Rebuild the main UI after theme changes.
void rebuild_main_screen();
// Schedule a rebuild (safe from event callbacks).
void request_rebuild();

// Called by other modules when a shared setting changes.
void update_top_label();
} // namespace cg::ui

