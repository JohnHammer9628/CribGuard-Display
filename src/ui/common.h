// src/ui/common.h
//
// Shared UI building blocks:
// - Theme tokens (colors/spacing/radii)
// - Common styling helpers (buttons, sheets)
// - Small widget helpers used across multiple dialogs
//
// Keep this file "stateless": it should not own modal state or app settings.
#pragma once

#include <string>

extern "C" {
#include "lvgl.h"
}

// Shared theme tokens + UI helper functions used across dialogs/screens.
namespace cg::ui {
namespace theme {
    lv_color_t surface_bg();
    lv_color_t header_bg();
    lv_color_t border();
    lv_color_t text_main();
    lv_color_t text_subtle();
    lv_color_t tonal_bg();
    lv_color_t tonal_border();
    lv_color_t primary_accent();

    extern const int sp4;
    extern const int sp6;
    extern const int sp8;
    extern const int sp10;
    extern const int sp12;
    extern const int sp14;
    extern const int sp16;

    extern const int radius_sm;
    extern const int radius_md;
    extern const int radius_lg;
    extern const int radius_pill;
}

void style_button_tonal(lv_obj_t* btn);
void style_button_pill(lv_obj_t* btn);
void style_sheet(lv_obj_t* sheet);

// Builds a header row inside a sheet. Returns the header container.
lv_obj_t* build_header(lv_obj_t* sheet, const char* title, lv_obj_t** out_actions);

// Common micro-helper used throughout the UI.
inline void set_centered_button_label(lv_obj_t* btn, const char* text) {
    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
}
} // namespace cg::ui

