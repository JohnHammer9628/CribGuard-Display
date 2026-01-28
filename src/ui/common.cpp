// src/ui/common.cpp
#include "ui/common.h"

namespace cg::ui {

namespace theme {
    // Background color for cards/sheets.
    lv_color_t surface_bg()      { return lv_color_hex(0x16191D); }
    // Background color for header bars.
    lv_color_t header_bg()       { return lv_color_hex(0x1E232A); }
    // Default border color used on cards/sheets.
    lv_color_t border()          { return lv_color_hex(0x2A2F36); }
    // Primary text color.
    lv_color_t text_main()       { return lv_color_hex(0xEDEFF2); }
    // Secondary/subtle text color.
    lv_color_t text_subtle()     { return lv_color_hex(0x9AA3AD); }
    // Tonal button background.
    lv_color_t tonal_bg()        { return lv_color_hex(0x2A2F36); }
    // Tonal button border.
    lv_color_t tonal_border()    { return lv_color_hex(0x3A4048); }
    // Accent color used for highlights.
    lv_color_t primary_accent()  { return lv_color_hex(0x7FB3FF); }

    const int sp4  = 4;
    const int sp6  = 6;
    const int sp8  = 8;
    const int sp10 = 10;
    const int sp12 = 12;
    const int sp14 = 14;
    const int sp16 = 16;

    const int radius_sm = 6;
    const int radius_md = 8;
    const int radius_lg = 10;
    const int radius_pill = 999;
} // namespace theme

// Apply the standard tonal button style used across the UI.
void style_button_tonal(lv_obj_t* btn) {
    lv_obj_set_style_radius(btn, theme::radius_sm, 0);
    lv_obj_set_style_bg_color(btn, theme::tonal_bg(), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_40, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, theme::tonal_border(), 0);
    lv_obj_set_style_pad_hor(btn, theme::sp12, 0);
    lv_obj_set_style_pad_ver(btn, theme::sp8, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_50, LV_STATE_PRESSED);
}

// Apply the standard "pill" button style used for top-bar quick actions.
void style_button_pill(lv_obj_t* btn) {
    lv_obj_set_style_radius(btn, theme::radius_pill, 0);
    lv_obj_set_style_bg_color(btn, theme::tonal_bg(), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_40, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, theme::tonal_border(), 0);
    lv_obj_set_style_pad_hor(btn, theme::sp12, 0);
    lv_obj_set_style_pad_ver(btn, theme::sp6, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_50, LV_STATE_PRESSED);
}

// Apply base styling for a modal "sheet" container.
void style_sheet(lv_obj_t* sheet) {
    lv_obj_clear_flag(sheet, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(sheet, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(sheet, theme::surface_bg(), 0);
    lv_obj_set_style_bg_opa(sheet, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(sheet, theme::text_main(), 0);
    lv_obj_set_style_border_width(sheet, 1, 0);
    lv_obj_set_style_border_color(sheet, theme::border(), 0);
    lv_obj_set_style_radius(sheet, theme::radius_lg, 0);
    lv_obj_set_style_shadow_width(sheet, 18, 0);
    lv_obj_set_style_shadow_opa(sheet, LV_OPA_30, 0);
    lv_obj_set_style_shadow_color(sheet, lv_color_hex(0x000000), 0);
    lv_obj_set_style_pad_hor(sheet, theme::sp16, 0);
    lv_obj_set_style_pad_ver(sheet, theme::sp14, 0);
    lv_obj_set_flex_flow(sheet, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(sheet, theme::sp12, 0);
}

// Create a standard header row for a modal sheet and return its container.
// If `out_actions` is provided, it receives the right-side actions container.
lv_obj_t* build_header(lv_obj_t* sheet, const char* title, lv_obj_t** out_actions) {
    lv_obj_t* hdr = lv_obj_create(sheet);
    lv_obj_set_size(hdr, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(hdr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(hdr, theme::header_bg(), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hdr, 1, 0);
    lv_obj_set_style_border_color(hdr, theme::border(), 0);
    lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(hdr, theme::sp8, 0);
    lv_obj_set_style_pad_right(hdr, theme::sp8, 0);

    lv_obj_t* lbl_title = lv_label_create(hdr);
    lv_label_set_text(lbl_title, title);
    lv_obj_set_style_text_color(lbl_title, theme::text_main(), 0);
    lv_obj_set_style_pad_left(lbl_title, theme::sp8, 0);

    lv_obj_t* actions = lv_obj_create(hdr);
    lv_obj_clear_flag(actions, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(actions, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(actions, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(actions, 0, 0);
    lv_obj_set_style_pad_all(actions, 0, 0);
    lv_obj_set_style_pad_column(actions, theme::sp8, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(actions, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    if (out_actions) *out_actions = actions;
    return hdr;
}

} // namespace cg::ui

