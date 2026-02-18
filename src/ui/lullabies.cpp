// src/ui/lullabies.cpp
//
// Lullabies modal + Rename modal:
// - Fetches the lullaby list from the Baby Pi (HTTP) via `baby_pi_api.*`
// - Plays a lullaby (tap) and opens rename dialog (long-press)
//
// On Windows, libcurl may be unavailable; in that case the Baby Pi API functions
// no-op/log and the list will show "Unable to fetch lullabies."
#include "ui/lullabies.h"

#include "baby_pi_api.h"
#include "config.h"
#include "logging.h"

#include "ui/common.h"

#include <string>
#include <vector>

namespace cg::ui {

static lv_obj_t* g_lullabies_modal = nullptr;
static lv_obj_t* g_lullabies_sheet = nullptr;
static lv_obj_t* g_lullabies_list = nullptr;
static std::vector<std::string> g_lullabies_files;

// Rename dialog state
static lv_obj_t* g_rename_modal = nullptr;
static lv_obj_t* g_rename_sheet = nullptr;
static lv_obj_t* g_rename_text = nullptr;
static lv_obj_t* g_rename_kbd = nullptr;
static std::string g_rename_old_name;

static void close_lullabies();
static void close_rename();
static void build_rename_dialog(const char* old_name);

// Tap handler: play the lullaby whose name is shown on the tapped row.
static void on_lullaby_click_play(lv_event_t* e) {
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    lv_obj_t* child = btn ? lv_obj_get_child(btn, 0) : nullptr;
    const char* fname = child ? lv_label_get_text(child) : nullptr;
    if (fname) baby_pi_play_lullaby(fname);
}

// Long-press handler: open the rename dialog for the pressed lullaby.
static void on_lullaby_longpress_rename(lv_event_t* e) {
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    lv_obj_t* child = btn ? lv_obj_get_child(btn, 0) : nullptr;
    const char* fname = child ? lv_label_get_text(child) : nullptr;
    if (fname) build_rename_dialog(fname);
}

// Create one lullaby row button and wire its click/long-press handlers.
static void lullabies_add_entry_button(const std::string& name) {
    if (!g_lullabies_list) return;
    lv_obj_t* b = lv_btn_create(g_lullabies_list);
    style_button_tonal(b);
    { lv_obj_t* l = lv_label_create(b); lv_label_set_text(l, name.c_str()); lv_obj_center(l); }
    lv_obj_add_event_cb(b, on_lullaby_click_play, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(b, on_lullaby_longpress_rename, LV_EVENT_LONG_PRESSED, nullptr);
}

// Rebuild the lullaby list UI by querying the Baby Pi and recreating buttons.
static void lullabies_rebuild_list() {
    if (!g_lullabies_list) return;
    lv_obj_clean(g_lullabies_list);
    if (baby_pi_list_lullabies(g_lullabies_files)) {
        for (const auto& name : g_lullabies_files) {
            lullabies_add_entry_button(name);
        }
    } else {
        lv_obj_t* lbl = lv_label_create(g_lullabies_list);
        lv_label_set_text(lbl, "Unable to fetch lullabies.");
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFF6B6B), 0);
    }
}

// Close and destroy the rename modal if it exists.
static void close_rename() {
    if (g_rename_modal) {
        lv_obj_del(g_rename_modal);
        g_rename_modal = nullptr;
        g_rename_sheet = nullptr;
        g_rename_text = nullptr;
        if (g_rename_kbd) { lv_obj_del(g_rename_kbd); g_rename_kbd = nullptr; }
        log_line("[UI] rename closed");
    }
}

// Build and show the rename modal (singleton).
static void build_rename_dialog(const char* old_name) {
    if (g_rename_modal) return;
    if (!old_name) return;
    // Only allow renaming .wav files per requirement
    std::string s = old_name;
    if (!(s.size() >= 4 && s.substr(s.size() - 4) == ".wav")) {
        log_line("[UI] rename only allowed for .wav files");
        return;
    }
    g_rename_old_name = s;

    g_rename_modal = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_rename_modal);
    lv_obj_set_size(g_rename_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(g_rename_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_rename_modal, LV_OPA_30, 0);
    lv_obj_add_flag(g_rename_modal, LV_OBJ_FLAG_CLICKABLE);

    g_rename_sheet = lv_obj_create(g_rename_modal);
    lv_obj_set_size(g_rename_sheet, LV_PCT(92), LV_SIZE_CONTENT);
    lv_obj_center(g_rename_sheet);
    style_sheet(g_rename_sheet);

    // header
    lv_obj_t* hdr_btns = nullptr;
    build_header(g_rename_sheet, "Rename Lullaby", &hdr_btns);

    // Cancel
    lv_obj_t* btn_cancel = lv_btn_create(hdr_btns);
    style_button_tonal(btn_cancel);
    set_centered_button_label(btn_cancel, "Cancel");
    lv_obj_add_event_cb(btn_cancel, [](lv_event_t* /*e*/){ close_rename(); }, LV_EVENT_CLICKED, nullptr);

    // Save
    lv_obj_t* btn_save = lv_btn_create(hdr_btns);
    style_button_tonal(btn_save);
    set_centered_button_label(btn_save, "Save");
    lv_obj_add_event_cb(btn_save, [](lv_event_t* /*e*/){
        if (!g_rename_text) return;
        const char* new_text = lv_textarea_get_text(g_rename_text);
        if (!new_text || std::string(new_text).empty()) return;
        std::string new_name = new_text;
        // Append .wav if not present (server enforces too)
        if (!(new_name.size() >= 4 && new_name.substr(new_name.size() - 4) == ".wav")) {
            new_name += ".wav";
        }
        bool ok = baby_pi_rename_lullaby(g_rename_old_name, new_name);
        if (!ok) log_line("[UI] rename failed (see Baby Pi logs)");
        lullabies_rebuild_list();
        close_rename();
    }, LV_EVENT_CLICKED, nullptr);

    // content
    lv_obj_t* content = lv_obj_create(g_rename_sheet);
    lv_obj_set_size(content, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_style_bg_color(content, theme::header_bg(), 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(content, 1, 0);
    lv_obj_set_style_border_color(content, theme::border(), 0);
    lv_obj_set_style_radius(content, 8, 0);
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(content, theme::sp12, 0);
    lv_obj_set_style_pad_row(content, theme::sp8, 0);

    // Current name
    {
        lv_obj_t* lbl_cur = lv_label_create(content);
        std::string cur = std::string("Current: ") + g_rename_old_name;
        lv_label_set_text(lbl_cur, cur.c_str());
        lv_obj_set_style_text_color(lbl_cur, theme::text_subtle(), 0);
    }

    // New name label
    {
        lv_obj_t* lbl_new = lv_label_create(content);
        lv_label_set_text(lbl_new, "New name");
        lv_obj_set_style_text_color(lbl_new, theme::text_subtle(), 0);
    }

    // Text area pre-filled with base name (without .wav)
    g_rename_text = lv_textarea_create(content);
    lv_obj_set_width(g_rename_text, LV_PCT(92));
    // Style for visibility
    lv_obj_set_style_bg_color(g_rename_text, theme::tonal_bg(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_rename_text, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_rename_text, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(g_rename_text, theme::tonal_border(), LV_PART_MAIN);
    lv_obj_set_style_text_color(g_rename_text, theme::text_main(), LV_PART_MAIN);
    {
        std::string base = g_rename_old_name;
        if (base.size() >= 4 && base.substr(base.size() - 4) == ".wav") {
            base = base.substr(0, base.size() - 4);
        }
        lv_textarea_set_text(g_rename_text, base.c_str());
    }
    lv_textarea_set_cursor_pos(g_rename_text, LV_TEXTAREA_CURSOR_LAST);
    lv_textarea_set_placeholder_text(g_rename_text, "Enter new name");
    lv_textarea_set_one_line(g_rename_text, true);
    lv_textarea_set_password_mode(g_rename_text, false);

    // On-screen keyboard (optional; helpful on touch)
    g_rename_kbd = lv_keyboard_create(g_rename_modal);
    lv_obj_set_width(g_rename_kbd, LV_PCT(92));
    lv_obj_align(g_rename_kbd, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_keyboard_set_textarea(g_rename_kbd, g_rename_text);

    // backdrop click to close
    lv_obj_add_event_cb(g_rename_modal, [](lv_event_t* e){
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) { close_rename(); }
    }, LV_EVENT_CLICKED, nullptr);

    log_line("[UI] rename opened");
}

// Build and show the lullabies modal (singleton).
void build_lullabies_dialog(lv_obj_t* parent) {
    if (g_lullabies_modal) return;

    g_lullabies_modal = lv_obj_create(parent);
    lv_obj_remove_style_all(g_lullabies_modal);
    lv_obj_set_size(g_lullabies_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(g_lullabies_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_lullabies_modal, LV_OPA_30, 0);
    lv_obj_add_flag(g_lullabies_modal, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* sheet = lv_obj_create(g_lullabies_modal);
    lv_obj_set_size(sheet, LV_PCT(94), LV_PCT(94));
    lv_obj_center(sheet);
    g_lullabies_sheet = sheet;

    style_sheet(sheet);

    // header
    lv_obj_t* hdr_btns = nullptr;
    build_header(sheet, "Lullabies", &hdr_btns);

    lv_obj_t* btn_close2 = lv_btn_create(hdr_btns);
    style_button_tonal(btn_close2);
    set_centered_button_label(btn_close2, LV_SYMBOL_CLOSE " Close");
    lv_obj_add_event_cb(btn_close2, [](lv_event_t* /*e*/){ close_lullabies(); }, LV_EVENT_CLICKED, nullptr);

    // Record Audio button in Lullabies header
    lv_obj_t* btn_rec = lv_btn_create(hdr_btns);
    style_button_tonal(btn_rec);
    set_centered_button_label(btn_rec, LV_SYMBOL_PLUS " Record");
    lv_obj_add_event_cb(btn_rec, [](lv_event_t* /*e*/){
        baby_pi_record_audio(g_audio_record_seconds);
    }, LV_EVENT_CLICKED, nullptr);

    // Refresh list button
    lv_obj_t* btn_ref = lv_btn_create(hdr_btns);
    style_button_tonal(btn_ref);
    set_centered_button_label(btn_ref, LV_SYMBOL_REFRESH " Refresh");
    lv_obj_add_event_cb(btn_ref, [](lv_event_t* /*e*/){
        lullabies_rebuild_list();
    }, LV_EVENT_CLICKED, nullptr);

    // Stop playback button
    lv_obj_t* btn_stop_play = lv_btn_create(hdr_btns);
    style_button_tonal(btn_stop_play);
    set_centered_button_label(btn_stop_play, LV_SYMBOL_STOP " Stop Play");
    lv_obj_add_event_cb(btn_stop_play, [](lv_event_t* /*e*/){
        baby_pi_stop_playback();
    }, LV_EVENT_CLICKED, nullptr);

    // content placeholder
    lv_obj_t* content = lv_obj_create(sheet);
    lv_obj_set_size(content, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_style_bg_color(content, theme::header_bg(), 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(content, 1, 0);
    lv_obj_set_style_border_color(content, theme::border(), 0);
    lv_obj_set_style_radius(content, 8, 0);
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);

    // list container
    g_lullabies_list = lv_obj_create(content);
    lv_obj_set_size(g_lullabies_list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(g_lullabies_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(g_lullabies_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(g_lullabies_list, 4, 0);
    lv_obj_set_style_pad_row(g_lullabies_list, 6, 0);

    // initial populate
    lullabies_rebuild_list();

    lv_obj_add_event_cb(g_lullabies_modal, [](lv_event_t* e){
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) { close_lullabies(); }
    }, LV_EVENT_CLICKED, nullptr);

    log_line("[UI] lullabies opened");
}

// Close and destroy the lullabies modal if it exists.
static void close_lullabies() {
    if (g_lullabies_modal) {
        lv_obj_del(g_lullabies_modal);
        g_lullabies_modal = nullptr;
        g_lullabies_sheet = nullptr;
        g_lullabies_list = nullptr;
        log_line("[UI] lullabies closed");
    }
}

} // namespace cg::ui

