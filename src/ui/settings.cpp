// src/ui/settings.cpp
//
// Settings modal:
// - Adjusts simulator UI state (volume, quiet hours enable + start/end time)
// - Calls `update_top_label()` to refresh the dashboard/top bar after saving
//
// Times are stored internally as 0-23 (24h), but displayed as "H:00 AM/PM".
#include "ui/settings.h"

#include "logging.h"
#include "ui/common.h"
#include "ui/state.h"

namespace cg::ui {

// Implemented in dashboard.cpp (kept there because it updates multiple widgets).
void update_top_label();

static lv_obj_t* g_settings_modal = nullptr;

// Close and destroy the Settings modal if it exists.
static void close_settings() {
    if (g_settings_modal) {
        lv_obj_del(g_settings_modal);
        g_settings_modal = nullptr;
        log_line("[UI] settings closed");
    }
}

// Trigger a system shutdown on the target device (no-op on Windows unless script exists).
static void on_shutdown(lv_event_t* /*e*/) {
    log_line("[UI] shutdown requested");
    std::system("sudo /usr/local/bin/cribguard-shutdown.sh &");
}

// Build and show the Settings modal (singleton).
void build_settings_dialog(lv_obj_t* parent) {
    if (g_settings_modal) return;

    // dim backdrop
    g_settings_modal = lv_obj_create(parent);
    lv_obj_remove_style_all(g_settings_modal);
    lv_obj_set_size(g_settings_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(g_settings_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_settings_modal, LV_OPA_30, 0);
    lv_obj_add_flag(g_settings_modal, LV_OBJ_FLAG_CLICKABLE);

    // sheet
    lv_obj_t* sheet = lv_obj_create(g_settings_modal);
    lv_obj_set_size(sheet, LV_PCT(92), LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(sheet, LV_PCT(90), 0);
    lv_obj_center(sheet);

    style_sheet(sheet);

    // header row
    lv_obj_t* hdr_btns = nullptr;
    build_header(sheet, "Settings", &hdr_btns);

    lv_obj_t* btn_cancel = lv_btn_create(hdr_btns);
    style_button_tonal(btn_cancel);
    set_centered_button_label(btn_cancel, "Cancel");
    lv_obj_add_event_cb(btn_cancel, [](lv_event_t* /*e*/){ close_settings(); }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* btn_save = lv_btn_create(hdr_btns);
    style_button_tonal(btn_save);
    set_centered_button_label(btn_save, "Save");

    // Shutdown button
    lv_obj_t* btn_shutdown = lv_btn_create(hdr_btns);
    style_button_tonal(btn_shutdown);
    set_centered_button_label(btn_shutdown, "Shutdown");
    lv_obj_add_event_cb(btn_shutdown, on_shutdown, LV_EVENT_CLICKED, nullptr);

    auto make_row = [&](const char* left, lv_obj_t** outRow){
        lv_obj_t* r = lv_obj_create(sheet);
        lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(r, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(r, 0, 0);
        lv_obj_set_style_pad_all(r, 0, 0);
        lv_obj_set_style_pad_bottom(r, 6, 0);
        lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* l = lv_label_create(r);
        lv_label_set_text(l, left);
        lv_obj_set_width(l, LV_PCT(42));
        lv_obj_set_style_text_color(l, lv_color_hex(0xEDEFF2), 0);
        if (outRow) *outRow = r;
        return r;
    };

    // Default volume
    lv_obj_t* row_vol=nullptr; make_row("Default Volume", &row_vol);
    lv_obj_t* vol_col = lv_obj_create(row_vol);
    lv_obj_clear_flag(vol_col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(vol_col, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(vol_col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(vol_col, 0, 0);
    lv_obj_set_size(vol_col, LV_PCT(50), LV_SIZE_CONTENT);

    lv_obj_t* sld_default = lv_slider_create(vol_col);
    lv_obj_set_width(sld_default, LV_PCT(100));
    lv_obj_set_style_pad_ver(sld_default, 6, 0);
    lv_slider_set_range(sld_default, 0, 100);
    lv_slider_set_value(sld_default, state::volume, LV_ANIM_OFF);
    // slider styling
    lv_obj_set_style_bg_color(sld_default, lv_color_hex(0x2A2F36), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sld_default, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sld_default, lv_color_hex(0x7FB3FF), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(sld_default, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sld_default, lv_color_hex(0xEDEFF2), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(sld_default, LV_OPA_COVER, LV_PART_KNOB);

    // Quiet Hours Enabled
    lv_obj_t* row_qh=nullptr; make_row("Quiet Hours Enabled", &row_qh);
    lv_obj_t* sw_qh = lv_switch_create(row_qh);
    if (state::quiet_hours) lv_obj_add_state(sw_qh, LV_STATE_CHECKED);

    // Quiet Start Time
    lv_obj_t* row_qstart=nullptr; make_row("Quiet Start Time", &row_qstart);
    lv_obj_t* dd_start = lv_dropdown_create(row_qstart);
    // Slightly wider so the collapsed text shows full "12:00 PM" on Pi screens too.
    lv_obj_set_width(dd_start, 190);
    lv_dropdown_set_options(dd_start,
        "12:00 AM\n1:00 AM\n2:00 AM\n3:00 AM\n4:00 AM\n5:00 AM\n6:00 AM\n7:00 AM\n8:00 AM\n9:00 AM\n10:00 AM\n11:00 AM\n"
        "12:00 PM\n1:00 PM\n2:00 PM\n3:00 PM\n4:00 PM\n5:00 PM\n6:00 PM\n7:00 PM\n8:00 PM\n9:00 PM\n10:00 PM\n11:00 PM");
    lv_dropdown_set_selected(dd_start, state::quiet_start);
    lv_obj_set_style_bg_color(dd_start, lv_color_hex(0x2A2F36), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dd_start, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_text_color(dd_start, lv_color_hex(0xEDEFF2), LV_PART_MAIN);

    // Quiet End Time
    lv_obj_t* row_qend=nullptr; make_row("Quiet End Time", &row_qend);
    lv_obj_t* dd_end = lv_dropdown_create(row_qend);
    lv_obj_set_width(dd_end, 190);
    lv_dropdown_set_options(dd_end,
        "12:00 AM\n1:00 AM\n2:00 AM\n3:00 AM\n4:00 AM\n5:00 AM\n6:00 AM\n7:00 AM\n8:00 AM\n9:00 AM\n10:00 AM\n11:00 AM\n"
        "12:00 PM\n1:00 PM\n2:00 PM\n3:00 PM\n4:00 PM\n5:00 PM\n6:00 PM\n7:00 PM\n8:00 PM\n9:00 PM\n10:00 PM\n11:00 PM");
    lv_dropdown_set_selected(dd_end, state::quiet_end);
    lv_obj_set_style_bg_color(dd_end, lv_color_hex(0x2A2F36), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dd_end, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_text_color(dd_end, lv_color_hex(0xEDEFF2), LV_PART_MAIN);

    // Save handler
    lv_obj_add_event_cb(btn_save, [](lv_event_t* e){
        lv_obj_t* btn       = (lv_obj_t*)lv_event_get_target(e);
        lv_obj_t* hdr_btns  = (lv_obj_t*)lv_obj_get_parent(btn);
        lv_obj_t* row_hdr   = (lv_obj_t*)lv_obj_get_parent(hdr_btns);
        lv_obj_t* sheet     = (lv_obj_t*)lv_obj_get_parent(row_hdr);

        lv_obj_t* row_vol   = (lv_obj_t*)lv_obj_get_child(sheet, 1);
        lv_obj_t* vol_col   = (lv_obj_t*)lv_obj_get_child(row_vol, 1);
        lv_obj_t* sld_def   = (lv_obj_t*)lv_obj_get_child(vol_col, 0);

        lv_obj_t* row_qh    = (lv_obj_t*)lv_obj_get_child(sheet, 2);
        lv_obj_t* sw_qh     = (lv_obj_t*)lv_obj_get_child(row_qh, 1);

        lv_obj_t* row_qstart= (lv_obj_t*)lv_obj_get_child(sheet, 3);
        lv_obj_t* dd_start  = (lv_obj_t*)lv_obj_get_child(row_qstart, 1);

        lv_obj_t* row_qend  = (lv_obj_t*)lv_obj_get_child(sheet, 4);
        lv_obj_t* dd_end    = (lv_obj_t*)lv_obj_get_child(row_qend, 1);

        state::volume      = lv_slider_get_value(sld_def);
        state::quiet_hours = lv_obj_has_state(sw_qh, LV_STATE_CHECKED);
        state::quiet_start = lv_dropdown_get_selected(dd_start);
        state::quiet_end   = lv_dropdown_get_selected(dd_end);

        update_top_label();
        log_line("[UI] settings saved");
        close_settings();
    }, LV_EVENT_CLICKED, nullptr);

    // click outside to dismiss
    lv_obj_add_event_cb(g_settings_modal, [](lv_event_t* e){
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            log_line("[UI] settings dismissed (backdrop)");
            close_settings();
        }
    }, LV_EVENT_CLICKED, nullptr);

    // Debug/verification log: helps confirm dropdowns are showing AM/PM options.
    {
        char buf_start[32] = {0};
        char buf_end[32] = {0};
        lv_dropdown_get_selected_str(dd_start, buf_start, sizeof(buf_start));
        lv_dropdown_get_selected_str(dd_end, buf_end, sizeof(buf_end));
        std::string msg = std::string("[UI] settings times: start=") + buf_start + " end=" + buf_end;
        log_line(msg.c_str());
    }

    log_line("[UI] settings opened");
}

} // namespace cg::ui

