// platforms/pi-sdl/main.cpp
#define _CRT_SECURE_NO_WARNINGS

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <chrono>
#include <thread>

extern "C" {
  #include "lvgl.h"
}

// ---- Probe LVGL v9 SDL headers under different include roots ----
#if __has_include("lvgl/src/drivers/sdl/lv_sdl_window.h")
  #include "lvgl/src/drivers/sdl/lv_sdl_window.h"
  #define HAVE_SDL_WIN 1
#elif __has_include("src/drivers/sdl/lv_sdl_window.h")
  #include "src/drivers/sdl/lv_sdl_window.h"
  #define HAVE_SDL_WIN 1
#elif __has_include("lvgl/drivers/sdl/lv_sdl_window.h")
  #include "lvgl/drivers/sdl/lv_sdl_window.h"
  #define HAVE_SDL_WIN 1
#elif __has_include("drivers/sdl/lv_sdl_window.h")
  #include "drivers/sdl/lv_sdl_window.h"
  #define HAVE_SDL_WIN 1
#else
  #define HAVE_SDL_WIN 0
#endif

#if __has_include("lvgl/src/drivers/sdl/lv_sdl_mouse.h")
  #include "lvgl/src/drivers/sdl/lv_sdl_mouse.h"
  #define HAVE_SDL_MOUSE 1
#elif __has_include("src/drivers/sdl/lv_sdl_mouse.h")
  #include "src/drivers/sdl/lv_sdl_mouse.h"
  #define HAVE_SDL_MOUSE 1
#elif __has_include("lvgl/drivers/sdl/lv_sdl_mouse.h")
  #include "lvgl/drivers/sdl/lv_sdl_mouse.h"
  #define HAVE_SDL_MOUSE 1
#elif __has_include("drivers/sdl/lv_sdl_mouse.h")
  #include "drivers/sdl/lv_sdl_mouse.h"
  #define HAVE_SDL_MOUSE 1
#else
  #define HAVE_SDL_MOUSE 0
#endif

#if __has_include("lvgl/src/drivers/sdl/lv_sdl_keyboard.h")
  #include "lvgl/src/drivers/sdl/lv_sdl_keyboard.h"
  #define HAVE_SDL_KBD 1
#elif __has_include("src/drivers/sdl/lv_sdl_keyboard.h")
  #include "src/drivers/sdl/lv_sdl_keyboard.h"
  #define HAVE_SDL_KBD 1
#elif __has_include("lvgl/drivers/sdl/lv_sdl_keyboard.h")
  #include "lvgl/drivers/sdl/lv_sdl_keyboard.h"
  #define HAVE_SDL_KBD 1
#elif __has_include("drivers/sdl/lv_sdl_keyboard.h")
  #include "drivers/sdl/lv_sdl_keyboard.h"
  #define HAVE_SDL_KBD 1
#else
  #define HAVE_SDL_KBD 0
#endif

#if __has_include("lvgl/src/drivers/sdl/lv_sdl_mousewheel.h")
  #include "lvgl/src/drivers/sdl/lv_sdl_mousewheel.h"
  #define HAVE_SDL_WHEEL 1
#elif __has_include("src/drivers/sdl/lv_sdl_mousewheel.h")
  #include "src/drivers/sdl/lv_sdl_mousewheel.h"
  #define HAVE_SDL_WHEEL 1
#elif __has_include("lvgl/drivers/sdl/lv_sdl_mousewheel.h")
  #include "lvgl/drivers/sdl/lv_sdl_mousewheel.h"
  #define HAVE_SDL_WHEEL 1
#elif __has_include("drivers/sdl/lv_sdl_mousewheel.h")
  #include "drivers/sdl/lv_sdl_mousewheel.h"
  #define HAVE_SDL_WHEEL 1
#else
  #define HAVE_SDL_WHEEL 0
#endif

#if !HAVE_SDL_WIN
  #error "LVGL SDL driver headers not found. Ensure LV_USE_SDL=1 in lv_conf.h and drivers are in include path."
#endif

// ---------- Simple logging ----------
static FILE* g_log = nullptr;
static void log_line(const char* msg) {
    if (g_log) { std::fprintf(g_log, "%s\n", msg); std::fflush(g_log); }
}

// ---------- App state ----------
static lv_obj_t* g_lbl_status = nullptr; // Center status
static lv_obj_t* g_lbl_conn   = nullptr; // Top bar label
static lv_obj_t* g_settings_modal = nullptr;

static bool   g_connected   = true;
static bool   g_quiet_hours = false;
static int    g_quiet_start = 18; // 24h
static int    g_quiet_end   = 7;  // 24h
static int    g_volume      = 50;
static bool   g_quit        = false;

// ---------- Helpers ----------
static void set_status_text(const char* txt, lv_color_t color) {
    lv_label_set_text(g_lbl_status, txt);
    lv_obj_set_style_text_color(g_lbl_status, color, 0);
    log_line((std::string("[UI] status: ") + txt).c_str());
}

static void update_top_label() {
    char hh[8];  std::snprintf(hh, sizeof(hh), "%02d", g_quiet_start);
    char he[8];  std::snprintf(he, sizeof(he), "%02d", g_quiet_end);
    std::string s = "CribGuard • ";
    s += (g_connected ? "Connected" : "Disconnected");
    s += " • Vol ";
    s += std::to_string(g_volume);
    s += " • Quiet: ";
    s += (g_quiet_hours ? "On" : "Off");
    s += " (";
    s += hh; s += "│"; s += he; s += ")";
    lv_label_set_text(g_lbl_conn, s.c_str());
    log_line((std::string("[UI] topbar: ") + s).c_str());
}

// ---------- Events ----------
static void on_btn_status(lv_event_t* e) {
    const char* role = (const char*)lv_event_get_user_data(e);
    if (!role) return;

    if (std::string(role) == "calm") {
        set_status_text("Calm", lv_color_hex(0x22AA22));
    } else if (std::string(role) == "cry") {
        set_status_text("Cry", lv_color_hex(0xCC2222));
    } else if (std::string(role) == "motion") {
        set_status_text("Motion", lv_color_hex(0xD08770));
    }
}

static void on_btn_play(lv_event_t* /*e*/) { set_status_text("Playing", lv_color_hex(0x3366FF)); }
static void on_btn_stop(lv_event_t* /*e*/) { set_status_text("Stopped", lv_color_hex(0x444444)); }

static void on_volume(lv_event_t* e) {
    lv_obj_t* sld = (lv_obj_t*)lv_event_get_target(e);
    g_volume = lv_slider_get_value(sld);
    update_top_label();
    log_line("[UI] volume changed");
}

static void on_switch_connected(lv_event_t* e) {
    lv_obj_t* sw = (lv_obj_t*)lv_event_get_target(e);
    g_connected = lv_obj_has_state(sw, LV_STATE_CHECKED);
    update_top_label();
    log_line("[UI] connected switch toggled");
}
static void on_switch_quiet_toggle(lv_event_t* e) {
    lv_obj_t* sw = (lv_obj_t*)lv_event_get_target(e);
    g_quiet_hours = lv_obj_has_state(sw, LV_STATE_CHECKED);
    update_top_label();
    log_line("[UI] quiet-hours switch toggled");
}

static void close_settings();

// ---------- Settings dialog ----------
static void build_settings_dialog(lv_obj_t* parent) {
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

    lv_obj_clear_flag(sheet, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(sheet, LV_SCROLLBAR_MODE_OFF);

    lv_obj_set_style_pad_hor(sheet, 16, 0);
    lv_obj_set_style_pad_ver(sheet, 14, 0);
    lv_obj_set_style_bg_color(sheet, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(sheet, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sheet, 1, 0);
    lv_obj_set_style_radius(sheet, 10, 0);

    lv_obj_set_flex_flow(sheet, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(sheet, 12, 0);

    // header row
    lv_obj_t* row_hdr = lv_obj_create(sheet);
    lv_obj_set_size(row_hdr, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(row_hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(row_hdr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(row_hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row_hdr, 0, 0);
    lv_obj_set_style_pad_all(row_hdr, 0, 0);
    lv_obj_set_flex_flow(row_hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row_hdr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* lbl_title = lv_label_create(row_hdr);
    lv_label_set_text(lbl_title, "Settings");

    lv_obj_t* hdr_btns = lv_obj_create(row_hdr);
    lv_obj_clear_flag(hdr_btns, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(hdr_btns, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(hdr_btns, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hdr_btns, 0, 0);
    lv_obj_set_style_pad_all(hdr_btns, 0, 0);
    lv_obj_set_style_pad_column(hdr_btns, 8, 0);
    lv_obj_set_flex_flow(hdr_btns, LV_FLEX_FLOW_ROW);

    lv_obj_t* btn_cancel = lv_btn_create(hdr_btns);
    { lv_obj_t* lbl = lv_label_create(btn_cancel); lv_label_set_text(lbl, "Cancel"); lv_obj_center(lbl); }
    lv_obj_add_event_cb(btn_cancel, [](lv_event_t* /*e*/){ close_settings(); }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* btn_save = lv_btn_create(hdr_btns);
    { lv_obj_t* lbl = lv_label_create(btn_save); lv_label_set_text(lbl, "Save"); lv_obj_center(lbl); }

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
    lv_slider_set_value(sld_default, g_volume, LV_ANIM_OFF);

    // Quiet Hours Enabled
    lv_obj_t* row_qh=nullptr; make_row("Quiet Hours Enabled", &row_qh);
    lv_obj_t* sw_qh = lv_switch_create(row_qh);
    if (g_quiet_hours) lv_obj_add_state(sw_qh, LV_STATE_CHECKED);

    // Quiet Start Hour
    lv_obj_t* row_qstart=nullptr; make_row("Quiet Start Hour", &row_qstart);
    lv_obj_t* dd_start = lv_dropdown_create(row_qstart);
    lv_obj_set_width(dd_start, 96);
    lv_dropdown_set_options(dd_start,
        "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23");
    lv_dropdown_set_selected(dd_start, g_quiet_start);

    // Quiet End Hour
    lv_obj_t* row_qend=nullptr; make_row("Quiet End Hour", &row_qend);
    lv_obj_t* dd_end = lv_dropdown_create(row_qend);
    lv_obj_set_width(dd_end, 96);
    lv_dropdown_set_options(dd_end,
        "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23");
    lv_dropdown_set_selected(dd_end, g_quiet_end);

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

        g_volume      = lv_slider_get_value(sld_def);
        g_quiet_hours = lv_obj_has_state(sw_qh, LV_STATE_CHECKED);
        g_quiet_start = lv_dropdown_get_selected(dd_start);
        g_quiet_end   = lv_dropdown_get_selected(dd_end);

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

    log_line("[UI] settings opened");
}

static void close_settings() {
    if (g_settings_modal) {
        lv_obj_del(g_settings_modal);
        g_settings_modal = nullptr;
        log_line("[UI] settings closed");
    }
}

// ---------- Build UI ----------
static void build_ui() {
    // Root screen (no scroll)
    lv_obj_t* screen = lv_obj_create(nullptr);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);
    lv_screen_load(screen);

    // Top bar (fixed)
    lv_obj_t* top = lv_obj_create(screen);
    lv_obj_set_size(top, LV_PCT(100), 56);
    lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(top, LV_SCROLLBAR_MODE_OFF);

    lv_obj_set_style_bg_color(top, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_bg_opa(top, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(top, 12, 0);
    lv_obj_set_style_pad_ver(top, 8, 0);
    lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_set_style_outline_opa(top, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_opa(top, LV_OPA_TRANSP, 0);

    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Left label (ellipsis)
    g_lbl_conn = lv_label_create(top);
    lv_obj_set_style_text_color(g_lbl_conn, lv_color_white(), 0);
    lv_obj_set_width(g_lbl_conn, LV_PCT(72));
    lv_label_set_long_mode(g_lbl_conn, LV_LABEL_LONG_DOT);
    lv_obj_set_style_pad_left(g_lbl_conn, 2, 0);
    lv_obj_set_style_pad_right(g_lbl_conn, 2, 0);
    update_top_label();

    // Right controls (quiet switch, connected switch, settings gear)
    lv_obj_t* right_grp = lv_obj_create(top);
    lv_obj_clear_flag(right_grp, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(right_grp, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(right_grp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_grp, 0, 0);
    lv_obj_set_style_pad_all(right_grp, 0, 0);
    lv_obj_set_style_pad_column(right_grp, 8, 0);
    lv_obj_set_flex_flow(right_grp, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(right_grp, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    lv_obj_t* sw_quiet = lv_switch_create(right_grp);
    if (g_quiet_hours) lv_obj_add_state(sw_quiet, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw_quiet, on_switch_quiet_toggle, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(sw_quiet, on_switch_quiet_toggle, LV_EVENT_CLICKED,       nullptr);

    lv_obj_t* sw_conn = lv_switch_create(right_grp);
    if (g_connected) lv_obj_add_state(sw_conn, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw_conn, on_switch_connected,   LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(sw_conn, on_switch_connected,   LV_EVENT_CLICKED,       nullptr);

    lv_obj_t* btn_gear = lv_btn_create(right_grp);
    { lv_obj_t* lbl = lv_label_create(btn_gear); lv_label_set_text(lbl, LV_SYMBOL_SETTINGS); lv_obj_center(lbl); }
    lv_obj_add_event_cb(btn_gear, [](lv_event_t* /*e*/){ build_settings_dialog((lv_obj_t*)lv_screen_active()); }, LV_EVENT_CLICKED, nullptr);

    // Center status label
    g_lbl_status = lv_label_create(screen);
    lv_label_set_text(g_lbl_status, "Calm");
    lv_obj_set_style_text_font(g_lbl_status, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(g_lbl_status, lv_color_hex(0x22AA22), 0);
    lv_obj_set_style_text_align(g_lbl_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_lbl_status, LV_ALIGN_CENTER, 0, -140);

    // Status buttons row
    lv_obj_t* row1 = lv_obj_create(screen);
    lv_obj_set_size(row1, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_align(row1, LV_ALIGN_BOTTOM_MID, 0, -100);
    lv_obj_set_style_bg_opa(row1, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(row1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(row1, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(row1, 0, 0);
    lv_obj_set_style_border_opa(row1, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row1, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row1, 14, 0);

    auto make_btn = [&](const char* txt, lv_event_cb_t cb, const char* role) {
        lv_obj_t* b = lv_btn_create(row1);
        lv_obj_set_size(b, 160, 56);
        lv_obj_t* l = lv_label_create(b);
        lv_label_set_text(l, txt);
        lv_obj_center(l);
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void*)role);
        return b;
    };
    make_btn("Calm",   on_btn_status, (const char*)"calm");
    make_btn("Cry",    on_btn_status, (const char*)"cry");
    make_btn("Motion", on_btn_status, (const char*)"motion");

    // Controls row (Play/Stop/Volume)
    lv_obj_t* row2 = lv_obj_create(screen);
    lv_obj_set_size(row2, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_align(row2, LV_ALIGN_BOTTOM_MID, 0, -24);
    lv_obj_set_style_bg_opa(row2, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(row2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(row2, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_border_opa(row2, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(row2, 0, 0);
    lv_obj_set_flex_flow(row2, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row2, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row2, 18, 0);

    lv_obj_t* btnPlay = lv_btn_create(row2);
    lv_obj_set_size(btnPlay, 150, 56);
    { lv_obj_t* l = lv_label_create(btnPlay); lv_label_set_text(l, "Play"); lv_obj_center(l); }
    lv_obj_add_event_cb(btnPlay, on_btn_play, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* btnStop = lv_btn_create(row2);
    lv_obj_set_size(btnStop, 150, 56);
    { lv_obj_t* l = lv_label_create(btnStop); lv_label_set_text(l, "Stop"); lv_obj_center(l); }
    lv_obj_add_event_cb(btnStop, on_btn_stop, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* slider = lv_slider_create(row2);
    lv_obj_set_size(slider, 360, 10);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, g_volume, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, on_volume, LV_EVENT_VALUE_CHANGED, nullptr);

    log_line("[SIM] UI built");
}

// ---------- Main ----------
int main() {
    g_log = std::fopen("sim.log", "w");
    lv_init();
    log_line("[SIM] lv_init ok");

    // 1280x720 landscape
    constexpr int SCR_W = 1280;
    constexpr int SCR_H = 720;

    lv_display_t* disp = lv_sdl_window_create(SCR_W, SCR_H);
    if (!disp) {
        log_line("[SIM] ERROR: lv_sdl_window_create failed");
        if (g_log) std::fclose(g_log);
        return 1;
    }
    log_line("[SIM] SDL window created 1280x720");

    // Attach input devices (mouse, keyboard, wheel) if headers were found
#if HAVE_SDL_MOUSE
    {
        lv_indev_t* m = lv_sdl_mouse_create();
        if (m) log_line("[SIM] SDL mouse attached");
        else   log_line("[SIM] WARNING: SDL mouse create returned null");
    }
#else
    log_line("[SIM] WARNING: SDL mouse header not found; no pointer input");
#endif

#if HAVE_SDL_KBD
    {
        lv_indev_t* k = lv_sdl_keyboard_create();
        if (k) log_line("[SIM] SDL keyboard attached");
        else   log_line("[SIM] WARNING: SDL keyboard create returned null");
    }
#else
    log_line("[SIM] NOTE: SDL keyboard header not found");
#endif

#if HAVE_SDL_WHEEL
    {
        lv_indev_t* w = lv_sdl_mousewheel_create();
        if (w) log_line("[SIM] SDL mousewheel attached");
        else   log_line("[SIM] WARNING: SDL mousewheel create returned null");
    }
#endif

    build_ui();
    log_line("[SIM] entering loop");

    while (!g_quit) {
        lv_timer_handler();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        lv_tick_inc(5); // advance LVGL tick so events/timeouts fire
    }

    log_line("[SIM] loop exit");
    if (g_log) std::fclose(g_log);
    return 0;
}
