// src/ui/state.h
//
// Shared UI state for the simulator UI.
//
// This centralizes the handful of settings/widgets that multiple modules need to
// read/update (e.g., Settings dialog updates the top bar + dashboard tiles).
//
// These values are UI-only for now (not persisted to disk).
#pragma once

extern "C" {
#include "lvgl.h"
}

namespace cg::ui::state {
// UI-level state (settings that affect multiple screens).
extern bool connected;
extern bool quiet_hours;
extern bool light_mode;
extern int quiet_start; // 0-23
extern int quiet_end;   // 0-23
extern int volume;      // 0-100

// Shared widgets updated from multiple modules.
extern lv_obj_t* lbl_status; // dashboard center status text
extern lv_obj_t* lbl_conn;   // top bar left label
extern lv_obj_t* btn_quiet;  // top bar "quiet" pill button
extern lv_obj_t* btn_conn;   // top bar "conn" pill button
extern lv_obj_t* btn_theme;  // top bar "theme" pill button

// Dashboard elements (for live updates).
extern lv_obj_t* dash_card;
extern lv_obj_t* dash_ring;
extern lv_obj_t* stat_conn;
extern lv_obj_t* stat_vol;
extern lv_obj_t* stat_qh;
} // namespace cg::ui::state

