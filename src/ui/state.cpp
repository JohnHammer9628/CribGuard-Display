// src/ui/state.cpp
//
// Definitions for the shared UI state declared in `src/ui/state.h`.
#include "ui/state.h"

namespace cg::ui::state {
bool connected = true;
bool quiet_hours = false;
int quiet_start = 18;
int quiet_end = 7;
int volume = 50;

lv_obj_t* lbl_status = nullptr;
lv_obj_t* lbl_conn = nullptr;
lv_obj_t* btn_quiet = nullptr;
lv_obj_t* btn_conn = nullptr;

lv_obj_t* dash_card = nullptr;
lv_obj_t* dash_ring = nullptr;
lv_obj_t* stat_conn = nullptr;
lv_obj_t* stat_vol = nullptr;
lv_obj_t* stat_qh = nullptr;
} // namespace cg::ui::state

