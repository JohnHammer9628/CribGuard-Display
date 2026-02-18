// src/ui/library.cpp
//
// Library modal (placeholder).
// Left in the UI to show where a future "content library" would live.
#include "ui/library.h"

#include "logging.h"
#include "ui/common.h"

namespace cg::ui {

static lv_obj_t* g_library_modal = nullptr;
static lv_obj_t* g_library_sheet = nullptr;

// Close and destroy the Library modal if it exists.
static void close_library() {
    if (g_library_modal) {
        lv_obj_del(g_library_modal);
        g_library_modal = nullptr;
        g_library_sheet = nullptr;
        log_line("[UI] library closed");
    }
}

// Build and show the Library modal (singleton).
void build_library_dialog(lv_obj_t* parent) {
    if (g_library_modal) return;

    g_library_modal = lv_obj_create(parent);
    lv_obj_remove_style_all(g_library_modal);
    lv_obj_set_size(g_library_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(g_library_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_library_modal, LV_OPA_30, 0);
    lv_obj_add_flag(g_library_modal, LV_OBJ_FLAG_CLICKABLE);

    g_library_sheet = lv_obj_create(g_library_modal);
    lv_obj_set_size(g_library_sheet, LV_PCT(92), LV_SIZE_CONTENT);
    lv_obj_center(g_library_sheet);
    style_sheet(g_library_sheet);

    // header
    lv_obj_t* hdr_btns = nullptr;
    build_header(g_library_sheet, "Library", &hdr_btns);

    lv_obj_t* btn_close = lv_btn_create(hdr_btns);
    style_button_tonal(btn_close);
    set_centered_button_label(btn_close, LV_SYMBOL_CLOSE " Close");
    lv_obj_add_event_cb(btn_close, [](lv_event_t* /*e*/){ close_library(); }, LV_EVENT_CLICKED, nullptr);

    // content placeholder
    lv_obj_t* content = lv_obj_create(g_library_sheet);
    lv_obj_set_size(content, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(content, lv_color_hex(0x1E232A), 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(content, 1, 0);
    lv_obj_set_style_border_color(content, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_radius(content, 8, 0);
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    {
        lv_obj_t* lbl = lv_label_create(content);
        lv_label_set_text(lbl, "Library coming soon");
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x9AA3AD), 0);
        lv_obj_center(lbl);
    }

    // backdrop click to close
    lv_obj_add_event_cb(g_library_modal, [](lv_event_t* e){
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) { close_library(); }
    }, LV_EVENT_CLICKED, nullptr);

    log_line("[UI] library opened");
}

} // namespace cg::ui

