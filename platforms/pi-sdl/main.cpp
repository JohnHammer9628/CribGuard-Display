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
static lv_obj_t* g_camera_modal = nullptr;
static lv_obj_t* g_camera_surface = nullptr;
static lv_obj_t* g_camera_sheet = nullptr;
static lv_obj_t* g_cam_spinner = nullptr;
static lv_obj_t* g_cam_live_label = nullptr;
static lv_obj_t* g_cam_meta_label = nullptr;
static lv_obj_t* g_cam_btn_full = nullptr;
static lv_obj_t* g_cam_sw_mute = nullptr;

// Main dashboard elements
static lv_obj_t* g_dash_card = nullptr;
static lv_obj_t* g_dash_hero = nullptr;
static lv_obj_t* g_dash_ring = nullptr;
static lv_obj_t* g_stat_conn = nullptr;
static lv_obj_t* g_stat_vol  = nullptr;
static lv_obj_t* g_stat_qh   = nullptr;

// Top bar quick-action buttons (replace switches)
static lv_obj_t* g_btn_quiet = nullptr;
static lv_obj_t* g_btn_conn = nullptr;

static bool   g_connected   = true;
static bool   g_quiet_hours = false;
static int    g_quiet_start = 18; // 24h
static int    g_quiet_end   = 7;  // 24h
static int    g_volume      = 50;
static bool   g_quit        = false;
static bool   g_cam_fullscreen = false;
static bool   g_cam_muted = false;
static bool   g_cam_playing = false;
static bool   g_cam_show_spinner = false;
static lv_timer_t* g_cam_spinner_timer = nullptr;

// ---------- Helpers ----------
// Forward declaration so it can be used in helpers below
static void update_dashboard();
static void apply_top_buttons_state();
static void on_top_quiet_click(lv_event_t* e);
static void on_top_conn_click(lv_event_t* e);
static std::string format_hour12(int hour24) {
    int h = ((hour24 % 24) + 24) % 24;
    int display = h % 12;
    if (display == 0) display = 12;
    bool pm = h >= 12;
    return std::to_string(display) + (pm ? " PM" : " AM");
}
static void set_status_text(const char* txt, lv_color_t color) {
    lv_label_set_text(g_lbl_status, txt);
    lv_obj_set_style_text_color(g_lbl_status, color, 0);
    log_line((std::string("[UI] status: ") + txt).c_str());
    update_dashboard();
}

static void update_top_label() {
    // Keep brand short in the top-left; reflect state via quick-action pills
    if (g_lbl_conn) lv_label_set_text(g_lbl_conn, "CribGuard");
    apply_top_buttons_state();
    std::string log_s = std::string("brand=CribGuard, conn=") + (g_connected ? "on" : "off")
        + ", vol=" + std::to_string(g_volume)
        + ", quiet=" + (g_quiet_hours ? "on" : "off");
    log_line((std::string("[UI] topbar: ") + log_s).c_str());
    update_dashboard();
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
static void build_camera_dialog(lv_obj_t* parent);
static void close_camera();
static void apply_camera_ui_state();
static void on_cam_fullscreen(lv_event_t* e);
static void on_cam_snapshot(lv_event_t* e);
static void on_cam_play(lv_event_t* e);
static void on_cam_pause(lv_event_t* e);
static void on_cam_stop(lv_event_t* e);
static void on_cam_mute_toggle(lv_event_t* e);

static void cam_spinner_show(uint32_t auto_hide_ms);
static void cam_spinner_hide();
static void update_dashboard();
static void apply_top_buttons_state();
static void on_top_quiet_click(lv_event_t* e);
static void on_top_conn_click(lv_event_t* e);

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
    lv_obj_set_style_bg_color(sheet, lv_color_hex(0x16191D), 0);
    lv_obj_set_style_bg_opa(sheet, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(sheet, lv_color_hex(0xEDEFF2), 0);
    lv_obj_set_style_border_width(sheet, 1, 0);
    lv_obj_set_style_border_color(sheet, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_radius(sheet, 10, 0);
    lv_obj_set_style_shadow_width(sheet, 18, 0);
    lv_obj_set_style_shadow_opa(sheet, LV_OPA_30, 0);
    lv_obj_set_style_shadow_color(sheet, lv_color_hex(0x000000), 0);

    lv_obj_set_flex_flow(sheet, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(sheet, 12, 0);

    // header row
    lv_obj_t* row_hdr = lv_obj_create(sheet);
    lv_obj_set_size(row_hdr, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(row_hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(row_hdr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(row_hdr, lv_color_hex(0x1E232A), 0);
    lv_obj_set_style_bg_opa(row_hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row_hdr, 1, 0);
    lv_obj_set_style_border_color(row_hdr, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_border_side(row_hdr, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_pad_all(row_hdr, 0, 0);
    lv_obj_set_flex_flow(row_hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row_hdr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(row_hdr, 6, 0);
    lv_obj_set_style_pad_right(row_hdr, 8, 0);

    lv_obj_t* lbl_title = lv_label_create(row_hdr);
    lv_label_set_text(lbl_title, "Settings");
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0xEDEFF2), 0);

    lv_obj_t* hdr_btns = lv_obj_create(row_hdr);
    lv_obj_clear_flag(hdr_btns, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(hdr_btns, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(hdr_btns, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hdr_btns, 0, 0);
    lv_obj_set_style_pad_all(hdr_btns, 0, 0);
    lv_obj_set_style_pad_column(hdr_btns, 8, 0);
    lv_obj_set_flex_flow(hdr_btns, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(hdr_btns, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    lv_obj_t* btn_cancel = lv_btn_create(hdr_btns);
    lv_obj_set_size(btn_cancel, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(btn_cancel, 6, 0);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_bg_opa(btn_cancel, LV_OPA_40, 0);
    lv_obj_set_style_border_width(btn_cancel, 1, 0);
    lv_obj_set_style_border_color(btn_cancel, lv_color_hex(0x3A4048), 0);
    lv_obj_set_style_pad_hor(btn_cancel, 12, 0);
    lv_obj_set_style_pad_ver(btn_cancel, 8, 0);
    { lv_obj_t* lbl = lv_label_create(btn_cancel); lv_label_set_text(lbl, "Cancel"); lv_obj_center(lbl); }
    lv_obj_add_event_cb(btn_cancel, [](lv_event_t* /*e*/){ close_settings(); }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* btn_save = lv_btn_create(hdr_btns);
     lv_obj_set_size(btn_save, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(btn_save, 6, 0);
    lv_obj_set_style_bg_color(btn_save, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_bg_opa(btn_save, LV_OPA_40, 0);
    lv_obj_set_style_border_width(btn_save, 1, 0);
    lv_obj_set_style_border_color(btn_save, lv_color_hex(0x3A4048), 0);
    lv_obj_set_style_pad_hor(btn_save, 12, 0);
    lv_obj_set_style_pad_ver(btn_save, 8, 0);
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
    lv_slider_set_value(sld_default, g_volume, LV_ANIM_OFF);
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
    if (g_quiet_hours) lv_obj_add_state(sw_qh, LV_STATE_CHECKED);

    // Quiet Start Hour
    lv_obj_t* row_qstart=nullptr; make_row("Quiet Start Hour", &row_qstart);
    lv_obj_t* dd_start = lv_dropdown_create(row_qstart);
    lv_obj_set_width(dd_start, 120);
    lv_dropdown_set_options(dd_start,
        "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23");
    lv_dropdown_set_selected(dd_start, g_quiet_start);
    lv_obj_set_style_bg_color(dd_start, lv_color_hex(0x2A2F36), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dd_start, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_text_color(dd_start, lv_color_hex(0xEDEFF2), LV_PART_MAIN);

    // Quiet End Hour
    lv_obj_t* row_qend=nullptr; make_row("Quiet End Hour", &row_qend);
    lv_obj_t* dd_end = lv_dropdown_create(row_qend);
    lv_obj_set_width(dd_end, 120);
    lv_dropdown_set_options(dd_end,
        "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23");
    lv_dropdown_set_selected(dd_end, g_quiet_end);
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

// ---------- Camera dialog ----------
static void build_camera_dialog(lv_obj_t* parent) {
    if (g_camera_modal) return;

    // dim backdrop
    g_camera_modal = lv_obj_create(parent);
    lv_obj_remove_style_all(g_camera_modal);
    lv_obj_set_size(g_camera_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(g_camera_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_camera_modal, LV_OPA_30, 0);
    lv_obj_add_flag(g_camera_modal, LV_OBJ_FLAG_CLICKABLE);

    // sheet
    lv_obj_t* sheet = lv_obj_create(g_camera_modal);
    lv_obj_set_size(sheet, LV_PCT(94), LV_PCT(94));
    lv_obj_center(sheet);
    g_camera_sheet = sheet;

    lv_obj_clear_flag(sheet, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(sheet, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(sheet, lv_color_hex(0x16191D), 0);
    lv_obj_set_style_bg_opa(sheet, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(sheet, lv_color_hex(0xEDEFF2), 0);
    lv_obj_set_style_border_width(sheet, 1, 0);
    lv_obj_set_style_border_color(sheet, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_radius(sheet, 10, 0);
    lv_obj_set_style_shadow_width(sheet, 18, 0);
    lv_obj_set_style_shadow_opa(sheet, LV_OPA_30, 0);
    lv_obj_set_style_shadow_color(sheet, lv_color_hex(0x000000), 0);
    lv_obj_set_style_pad_hor(sheet, 16, 0);
    lv_obj_set_style_pad_ver(sheet, 14, 0);
    lv_obj_set_flex_flow(sheet, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(sheet, 12, 0);

    // header row
    lv_obj_t* row_hdr = lv_obj_create(sheet);
    lv_obj_set_size(row_hdr, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(row_hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(row_hdr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(row_hdr, lv_color_hex(0x1E232A), 0);
    lv_obj_set_style_bg_opa(row_hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row_hdr, 1, 0);
    lv_obj_set_style_border_color(row_hdr, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_border_side(row_hdr, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_pad_all(row_hdr, 0, 0);
    lv_obj_set_flex_flow(row_hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row_hdr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(row_hdr, 6, 0);
    lv_obj_set_style_pad_right(row_hdr, 8, 0);

    // left group: title + meta
    lv_obj_t* hdr_left = lv_obj_create(row_hdr);
    lv_obj_clear_flag(hdr_left, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(hdr_left, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(hdr_left, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hdr_left, 0, 0);
    lv_obj_set_style_pad_all(hdr_left, 0, 0);
    lv_obj_set_style_pad_left(hdr_left, 8, 0);
    lv_obj_set_style_pad_column(hdr_left, 10, 0);
    lv_obj_set_flex_flow(hdr_left, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(hdr_left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    lv_obj_t* lbl_title = lv_label_create(hdr_left);
    lv_label_set_text(lbl_title, "Camera");
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0xEDEFF2), 0);

    g_cam_meta_label = lv_label_create(hdr_left);
    lv_label_set_text(g_cam_meta_label, "Idle");
    lv_obj_set_style_text_color(g_cam_meta_label, lv_color_hex(0x9AA3AD), 0);

    lv_obj_t* hdr_btns = lv_obj_create(row_hdr);
    lv_obj_clear_flag(hdr_btns, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(hdr_btns, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(hdr_btns, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hdr_btns, 0, 0);
    lv_obj_set_style_pad_all(hdr_btns, 0, 0);
    lv_obj_set_style_pad_column(hdr_btns, 8, 0);
    lv_obj_set_flex_flow(hdr_btns, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(hdr_btns, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    // Snapshot button
    lv_obj_t* btn_snap = lv_btn_create(hdr_btns);
    lv_obj_set_size(btn_snap, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(btn_snap, 6, 0);
    lv_obj_set_style_bg_color(btn_snap, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_bg_opa(btn_snap, LV_OPA_40, 0);
    lv_obj_set_style_border_width(btn_snap, 1, 0);
    lv_obj_set_style_border_color(btn_snap, lv_color_hex(0x3A4048), 0);
    lv_obj_set_style_pad_hor(btn_snap, 12, 0);
    lv_obj_set_style_pad_ver(btn_snap, 8, 0);
    { lv_obj_t* lbl = lv_label_create(btn_snap); lv_label_set_text(lbl, "Snapshot"); lv_obj_center(lbl); }
    lv_obj_add_event_cb(btn_snap, on_cam_snapshot, LV_EVENT_CLICKED, nullptr);

    // Fullscreen button
    g_cam_btn_full = lv_btn_create(hdr_btns);
    lv_obj_set_size(g_cam_btn_full, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(g_cam_btn_full, 6, 0);
    lv_obj_set_style_bg_color(g_cam_btn_full, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_bg_opa(g_cam_btn_full, LV_OPA_40, 0);
    lv_obj_set_style_border_width(g_cam_btn_full, 1, 0);
    lv_obj_set_style_border_color(g_cam_btn_full, lv_color_hex(0x3A4048), 0);
    lv_obj_set_style_pad_hor(g_cam_btn_full, 12, 0);
    lv_obj_set_style_pad_ver(g_cam_btn_full, 8, 0);
    { lv_obj_t* lbl = lv_label_create(g_cam_btn_full); lv_label_set_text(lbl, "Fullscreen"); lv_obj_center(lbl); }
    lv_obj_add_event_cb(g_cam_btn_full, on_cam_fullscreen, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* btn_close = lv_btn_create(hdr_btns);
    lv_obj_set_size(btn_close, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(btn_close, 6, 0);
    lv_obj_set_style_bg_color(btn_close, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_bg_opa(btn_close, LV_OPA_40, 0);
    lv_obj_set_style_border_width(btn_close, 1, 0);
    lv_obj_set_style_border_color(btn_close, lv_color_hex(0x3A4048), 0);
    lv_obj_set_style_pad_hor(btn_close, 12, 0);
    lv_obj_set_style_pad_ver(btn_close, 8, 0);
    { lv_obj_t* lbl = lv_label_create(btn_close); lv_label_set_text(lbl, "Close"); lv_obj_center(lbl); }
    lv_obj_add_event_cb(btn_close, [](lv_event_t* /*e*/){ close_camera(); }, LV_EVENT_CLICKED, nullptr);

    // video surface (placeholder for GStreamer rendering)
    g_camera_surface = lv_obj_create(sheet);
    lv_obj_set_size(g_camera_surface, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(g_camera_surface, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_camera_surface, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_camera_surface, 1, 0);
    lv_obj_set_style_border_color(g_camera_surface, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_radius(g_camera_surface, 6, 0);
    lv_obj_clear_flag(g_camera_surface, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(g_camera_surface, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_grow(g_camera_surface, 1);
    // LIVE/IDLE label (simple text, no chip)
    g_cam_live_label = lv_label_create(g_camera_surface);
    lv_obj_set_style_text_color(g_cam_live_label, lv_color_white(), 0);
    lv_label_set_text(g_cam_live_label, "IDLE");
    lv_obj_align(g_cam_live_label, LV_ALIGN_TOP_LEFT, 12, 12);
    // spinner
    g_cam_spinner = lv_spinner_create(g_camera_surface);
    lv_spinner_set_anim_params(g_cam_spinner, 1000, 60);
    lv_obj_set_size(g_cam_spinner, 48, 48);
    lv_obj_center(g_cam_spinner);
    lv_obj_set_style_arc_color(g_cam_spinner, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_arc_color(g_cam_spinner, lv_color_hex(0x7FB3FF), LV_PART_INDICATOR);
    lv_obj_add_flag(g_cam_spinner, LV_OBJ_FLAG_HIDDEN);

    // controls row (moved below video)
    lv_obj_t* row_ctrls = lv_obj_create(sheet);
    lv_obj_set_size(row_ctrls, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(row_ctrls, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(row_ctrls, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(row_ctrls, lv_color_hex(0x1E232A), 0);
    lv_obj_set_style_bg_opa(row_ctrls, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row_ctrls, 1, 0);
    lv_obj_set_style_border_color(row_ctrls, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_border_side(row_ctrls, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_pad_all(row_ctrls, 8, 0);
    lv_obj_set_style_pad_column(row_ctrls, 10, 0);
    lv_obj_set_flex_flow(row_ctrls, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row_ctrls, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* btn_play = lv_btn_create(row_ctrls);
    lv_obj_set_style_radius(btn_play, 6, 0);
    lv_obj_set_style_bg_color(btn_play, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_bg_opa(btn_play, LV_OPA_40, 0);
    lv_obj_set_style_border_width(btn_play, 1, 0);
    lv_obj_set_style_border_color(btn_play, lv_color_hex(0x3A4048), 0);
    lv_obj_set_style_pad_hor(btn_play, 12, 0);
    lv_obj_set_style_pad_ver(btn_play, 8, 0);
    { lv_obj_t* lbl = lv_label_create(btn_play); lv_label_set_text(lbl, "Play"); lv_obj_center(lbl); }
    lv_obj_add_event_cb(btn_play, on_cam_play, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* btn_pause = lv_btn_create(row_ctrls);
    lv_obj_set_style_radius(btn_pause, 6, 0);
    lv_obj_set_style_bg_color(btn_pause, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_bg_opa(btn_pause, LV_OPA_40, 0);
    lv_obj_set_style_border_width(btn_pause, 1, 0);
    lv_obj_set_style_border_color(btn_pause, lv_color_hex(0x3A4048), 0);
    lv_obj_set_style_pad_hor(btn_pause, 12, 0);
    lv_obj_set_style_pad_ver(btn_pause, 8, 0);
    { lv_obj_t* lbl = lv_label_create(btn_pause); lv_label_set_text(lbl, "Pause"); lv_obj_center(lbl); }
    lv_obj_add_event_cb(btn_pause, on_cam_pause, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* btn_stop = lv_btn_create(row_ctrls);
    lv_obj_set_style_radius(btn_stop, 6, 0);
    lv_obj_set_style_bg_color(btn_stop, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_bg_opa(btn_stop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(btn_stop, 1, 0);
    lv_obj_set_style_border_color(btn_stop, lv_color_hex(0x3A4048), 0);
    lv_obj_set_style_pad_hor(btn_stop, 12, 0);
    lv_obj_set_style_pad_ver(btn_stop, 8, 0);
    { lv_obj_t* lbl = lv_label_create(btn_stop); lv_label_set_text(lbl, "Stop"); lv_obj_center(lbl); }
    lv_obj_add_event_cb(btn_stop, on_cam_stop, LV_EVENT_CLICKED, nullptr);

    // spacer
    lv_obj_t* spacer = lv_obj_create(row_ctrls);
    lv_obj_set_size(spacer, 1, 1);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_grow(spacer, 1);

    // mute switch + label
    g_cam_sw_mute = lv_switch_create(row_ctrls);
    if (g_cam_muted) lv_obj_add_state(g_cam_sw_mute, LV_STATE_CHECKED);
    lv_obj_add_event_cb(g_cam_sw_mute, on_cam_mute_toggle, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(g_cam_sw_mute, on_cam_mute_toggle, LV_EVENT_CLICKED, nullptr);
    { lv_obj_t* lbl = lv_label_create(row_ctrls); lv_label_set_text(lbl, "Mute"); }

    // click outside to dismiss
    lv_obj_add_event_cb(g_camera_modal, [](lv_event_t* e){
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            log_line("[UI] camera dismissed (backdrop)");
            close_camera();
        }
    }, LV_EVENT_CLICKED, nullptr);

    log_line("[UI] camera opened");
    apply_camera_ui_state();
}

static void close_camera() {
    if (g_camera_modal) {
        cam_spinner_hide();
        lv_obj_del(g_camera_modal);
        g_camera_modal = nullptr;
        g_camera_surface = nullptr;
        g_camera_sheet = nullptr;
        g_cam_spinner = nullptr;
        g_cam_live_label = nullptr;
        g_cam_meta_label = nullptr;
        g_cam_btn_full = nullptr;
        g_cam_sw_mute = nullptr;
        g_cam_fullscreen = false;
        g_cam_playing = false;
        log_line("[UI] camera closed");
    }
}

// ---------- Camera UI interactions ----------
static void apply_camera_ui_state() {
    // Update fullscreen button label
    if (g_cam_btn_full) {
        lv_obj_t* lbl = lv_obj_get_child(g_cam_btn_full, 0);
        if (lbl) lv_label_set_text(lbl, g_cam_fullscreen ? "Exit Fullscreen" : "Fullscreen");
    }
    // Update mute switch visual state
    if (g_cam_sw_mute) {
        if (g_cam_muted) lv_obj_add_state(g_cam_sw_mute, LV_STATE_CHECKED);
        else lv_obj_clear_state(g_cam_sw_mute, LV_STATE_CHECKED);
    }
    // Update LIVE/IDLE label and spinner
    if (g_cam_live_label) {
        lv_label_set_text(g_cam_live_label, g_cam_playing ? "LIVE" : "IDLE");
        lv_obj_set_style_text_color(g_cam_live_label, g_cam_playing ? lv_color_hex(0xFF6B6B) : lv_color_white(), 0);
    }
    if (g_cam_meta_label) {
        lv_label_set_text(g_cam_meta_label, g_cam_playing ? "Live" : "Idle");
        lv_obj_set_style_text_color(g_cam_meta_label, g_cam_playing ? lv_color_hex(0xB0D7FF) : lv_color_hex(0x9AA3AD), 0);
    }
    // Spinner visibility is controlled explicitly via cam_spinner_show/hide
    // Apply sheet sizing for fullscreen
    if (g_camera_sheet) {
        if (g_cam_fullscreen) {
            lv_obj_set_size(g_camera_sheet, LV_PCT(100), LV_PCT(100));
            lv_obj_set_style_radius(g_camera_sheet, 0, 0);
            lv_obj_set_style_pad_hor(g_camera_sheet, 8, 0);
            lv_obj_set_style_pad_ver(g_camera_sheet, 8, 0);
        } else {
            lv_obj_set_size(g_camera_sheet, LV_PCT(94), LV_PCT(94));
            lv_obj_set_style_radius(g_camera_sheet, 10, 0);
            lv_obj_set_style_pad_hor(g_camera_sheet, 16, 0);
            lv_obj_set_style_pad_ver(g_camera_sheet, 14, 0);
        }
    }
}

// ---------- Dashboard updater ----------
static void update_dashboard() {
    if (!g_dash_card) return;

    // Determine status color and apply to hero ring
    const char* st = g_lbl_status ? lv_label_get_text(g_lbl_status) : "Calm";
    lv_color_t ring = lv_color_hex(0x22AA22);
    if (st && std::string(st) == "Cry")       ring = lv_color_hex(0xCC2222);
    else if (st && std::string(st) == "Motion") ring = lv_color_hex(0xD08770);
    if (g_dash_ring) {
        lv_obj_set_style_bg_opa(g_dash_ring, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(g_dash_ring, 10, 0);
        lv_obj_set_style_border_color(g_dash_ring, ring, 0);
        lv_obj_set_style_radius(g_dash_ring, LV_RADIUS_CIRCLE, 0);
    }

    // Connection stat
    if (g_stat_conn) {
        lv_label_set_text(g_stat_conn, g_connected ? "Connected" : "Disconnected");
        lv_obj_set_style_text_color(g_stat_conn, g_connected ? lv_color_hex(0xB0D7FF) : lv_color_hex(0xFF6B6B), 0);
    }
    // Volume stat
    if (g_stat_vol) {
        std::string s = std::to_string(g_volume) + "%";
        lv_label_set_text(g_stat_vol, s.c_str());
    }
    // Quiet Hours stat
    if (g_stat_qh) {
        if (g_quiet_hours) {
            std::string s = "On  ";
            s += format_hour12(g_quiet_start);
            s += " - ";
            s += format_hour12(g_quiet_end);
            lv_label_set_text(g_stat_qh, s.c_str());
            lv_obj_set_style_text_color(g_stat_qh, lv_color_hex(0xB0D7FF), 0);
        } else {
            lv_label_set_text(g_stat_qh, "Off");
            lv_obj_set_style_text_color(g_stat_qh, lv_color_hex(0x9AA3AD), 0);
        }
    }
}

// ---------- Top bar quick-action state ----------
static void apply_top_buttons_state() {
    // Quiet button
    if (g_btn_quiet) {
        lv_color_t bg = g_quiet_hours ? lv_color_hex(0x2F3B46) : lv_color_hex(0x2A2F36);
        lv_color_t fg = g_quiet_hours ? lv_color_hex(0xB0D7FF) : lv_color_hex(0xEDEFF2);
        lv_obj_set_style_bg_color(g_btn_quiet, bg, 0);
        lv_obj_set_style_bg_opa(g_btn_quiet, LV_OPA_40, 0);
        lv_obj_t* lbl = lv_obj_get_child(g_btn_quiet, 0);
        if (lbl) {
            lv_label_set_text(lbl, g_quiet_hours ? "Quiet On" : "Quiet Off");
            lv_obj_set_style_text_color(lbl, fg, 0);
        }
    }
    // Connection button
    if (g_btn_conn) {
        lv_color_t bg = g_connected ? lv_color_hex(0x294236) : lv_color_hex(0x3F2A2A);
        lv_color_t fg = g_connected ? lv_color_hex(0xB2F5DC) : lv_color_hex(0xFFB3B3);
        lv_obj_set_style_bg_color(g_btn_conn, bg, 0);
        lv_obj_set_style_bg_opa(g_btn_conn, LV_OPA_40, 0);
        lv_obj_t* lbl = lv_obj_get_child(g_btn_conn, 0);
        if (lbl) {
            lv_label_set_text(lbl, g_connected ? "Connected" : "Offline");
            lv_obj_set_style_text_color(lbl, fg, 0);
        }
    }
}

static void on_cam_fullscreen(lv_event_t* /*e*/) {
    g_cam_fullscreen = !g_cam_fullscreen;
    log_line(g_cam_fullscreen ? "[UI] camera fullscreen on" : "[UI] camera fullscreen off");
    apply_camera_ui_state();
}

static void on_cam_snapshot(lv_event_t* /*e*/) {
    log_line("[UI] camera snapshot requested");
    // No-op for now; integrate capture once streaming is wired
}

static void on_cam_play(lv_event_t* /*e*/) {
    g_cam_playing = true;
    log_line("[UI] camera play");
    cam_spinner_show(900);
    apply_camera_ui_state();
}

static void on_cam_pause(lv_event_t* /*e*/) {
    g_cam_playing = false;
    log_line("[UI] camera pause");
    cam_spinner_hide();
    apply_camera_ui_state();
}

static void on_cam_stop(lv_event_t* /*e*/) {
    g_cam_playing = false;
    log_line("[UI] camera stop");
    cam_spinner_hide();
    apply_camera_ui_state();
}

static void on_cam_mute_toggle(lv_event_t* e) {
    lv_obj_t* sw = (lv_obj_t*)lv_event_get_target(e);
    g_cam_muted = lv_obj_has_state(sw, LV_STATE_CHECKED);
    log_line(g_cam_muted ? "[UI] camera muted" : "[UI] camera unmuted");
    apply_camera_ui_state();
}

// ---------- Spinner helpers ----------
static void cam_spinner_show(uint32_t auto_hide_ms) {
    g_cam_show_spinner = true;
    if (g_cam_spinner) lv_obj_clear_flag(g_cam_spinner, LV_OBJ_FLAG_HIDDEN);
    if (g_cam_spinner_timer) { lv_timer_del(g_cam_spinner_timer); g_cam_spinner_timer = nullptr; }
    if (auto_hide_ms > 0) {
        g_cam_spinner_timer = lv_timer_create([](lv_timer_t* t){
            g_cam_show_spinner = false;
            if (g_cam_spinner) lv_obj_add_flag(g_cam_spinner, LV_OBJ_FLAG_HIDDEN);
            lv_timer_del(t);
            g_cam_spinner_timer = nullptr;
        }, auto_hide_ms, nullptr);
        lv_timer_set_repeat_count(g_cam_spinner_timer, 1);
    }
}

static void cam_spinner_hide() {
    g_cam_show_spinner = false;
    if (g_cam_spinner) lv_obj_add_flag(g_cam_spinner, LV_OBJ_FLAG_HIDDEN);
    if (g_cam_spinner_timer) { lv_timer_del(g_cam_spinner_timer); g_cam_spinner_timer = nullptr; }
}

// ---------- Top bar quick-action handlers ----------
static void on_top_quiet_click(lv_event_t* /*e*/) {
    g_quiet_hours = !g_quiet_hours;
    update_top_label();
    apply_top_buttons_state();
}

static void on_top_conn_click(lv_event_t* /*e*/) {
    g_connected = !g_connected;
    update_top_label();
    apply_top_buttons_state();
}
// ---------- Build UI ----------
static void build_ui() {
    // Root screen (no scroll)
    lv_obj_t* screen = lv_obj_create(nullptr);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x0E1116), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_screen_load(screen);

    // Top bar (fixed)
    lv_obj_t* top = lv_obj_create(screen);
    lv_obj_set_size(top, LV_PCT(100), 64);
    lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(top, LV_SCROLLBAR_MODE_OFF);

    lv_obj_set_style_bg_color(top, lv_color_hex(0x1E232A), 0);
    lv_obj_set_style_bg_opa(top, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(top, 16, 0);
    lv_obj_set_style_pad_ver(top, 10, 0);
    lv_obj_set_style_border_width(top, 1, 0);
    lv_obj_set_style_border_color(top, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_border_side(top, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_outline_opa(top, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_opa(top, LV_OPA_TRANSP, 0);

    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Left label (ellipsis)
    g_lbl_conn = lv_label_create(top);
    lv_obj_set_style_text_color(g_lbl_conn, lv_color_hex(0xEDEFF2), 0);
    lv_obj_set_style_pad_left(g_lbl_conn, 2, 0);
    lv_obj_set_style_pad_right(g_lbl_conn, 8, 0);
    update_top_label();

    // Right controls (quiet switch, connected switch, settings gear)
    lv_obj_t* right_grp = lv_obj_create(top);
    lv_obj_clear_flag(right_grp, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(right_grp, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(right_grp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_grp, 0, 0);
    lv_obj_set_style_pad_all(right_grp, 0, 0);
    lv_obj_set_style_pad_column(right_grp, 10, 0);
    lv_obj_set_flex_flow(right_grp, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(right_grp, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    // Quick-action pills instead of switches
    g_btn_quiet = lv_btn_create(right_grp);
    lv_obj_set_style_radius(g_btn_quiet, 999, 0);
    lv_obj_set_style_pad_hor(g_btn_quiet, 12, 0);
    lv_obj_set_style_pad_ver(g_btn_quiet, 6, 0);
    lv_obj_set_style_border_width(g_btn_quiet, 1, 0);
    lv_obj_set_style_border_color(g_btn_quiet, lv_color_hex(0x3A4048), 0);
    { lv_obj_t* lbl = lv_label_create(g_btn_quiet); lv_label_set_text(lbl, "Quiet"); lv_obj_center(lbl); }
    lv_obj_add_event_cb(g_btn_quiet, on_top_quiet_click, LV_EVENT_CLICKED, nullptr);

    g_btn_conn = lv_btn_create(right_grp);
    lv_obj_set_style_radius(g_btn_conn, 999, 0);
    lv_obj_set_style_pad_hor(g_btn_conn, 12, 0);
    lv_obj_set_style_pad_ver(g_btn_conn, 6, 0);
    lv_obj_set_style_border_width(g_btn_conn, 1, 0);
    lv_obj_set_style_border_color(g_btn_conn, lv_color_hex(0x3A4048), 0);
    { lv_obj_t* lbl = lv_label_create(g_btn_conn); lv_label_set_text(lbl, "Conn"); lv_obj_center(lbl); }
    lv_obj_add_event_cb(g_btn_conn, on_top_conn_click, LV_EVENT_CLICKED, nullptr);

    // Camera open button
    lv_obj_t* btn_cam = lv_btn_create(right_grp);
    lv_obj_set_style_radius(btn_cam, 6, 0);
    lv_obj_set_style_bg_color(btn_cam, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_bg_opa(btn_cam, LV_OPA_40, 0);
    lv_obj_set_style_border_width(btn_cam, 1, 0);
    lv_obj_set_style_border_color(btn_cam, lv_color_hex(0x3A4048), 0);
    lv_obj_set_style_pad_hor(btn_cam, 12, 0);
    lv_obj_set_style_pad_ver(btn_cam, 8, 0);
    { lv_obj_t* lbl = lv_label_create(btn_cam); lv_label_set_text(lbl, "Cam"); lv_obj_center(lbl); }
    lv_obj_add_event_cb(btn_cam, [](lv_event_t* /*e*/){ build_camera_dialog((lv_obj_t*)lv_screen_active()); }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* btn_gear = lv_btn_create(right_grp);
    lv_obj_set_style_radius(btn_gear, 6, 0);
    lv_obj_set_style_bg_color(btn_gear, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_bg_opa(btn_gear, LV_OPA_40, 0);
    lv_obj_set_style_border_width(btn_gear, 1, 0);
    lv_obj_set_style_border_color(btn_gear, lv_color_hex(0x3A4048), 0);
    lv_obj_set_style_pad_hor(btn_gear, 12, 0);
    lv_obj_set_style_pad_ver(btn_gear, 8, 0);
    { lv_obj_t* lbl = lv_label_create(btn_gear); lv_label_set_text(lbl, LV_SYMBOL_SETTINGS); lv_obj_center(lbl); }
    lv_obj_add_event_cb(btn_gear, [](lv_event_t* /*e*/){ build_settings_dialog((lv_obj_t*)lv_screen_active()); }, LV_EVENT_CLICKED, nullptr);

    apply_top_buttons_state();

    // Center dashboard card
    g_dash_card = lv_obj_create(screen);
    lv_obj_set_size(g_dash_card, LV_PCT(92), 360);
    lv_obj_align(g_dash_card, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_bg_color(g_dash_card, lv_color_hex(0x16191D), 0);
    lv_obj_set_style_bg_opa(g_dash_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_dash_card, 1, 0);
    lv_obj_set_style_border_color(g_dash_card, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_radius(g_dash_card, 10, 0);
    lv_obj_set_style_pad_hor(g_dash_card, 16, 0);
    lv_obj_set_style_pad_ver(g_dash_card, 14, 0);
    lv_obj_set_flex_flow(g_dash_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(g_dash_card, 12, 0);

    // Hero area with ring and status label
    g_dash_hero = lv_obj_create(g_dash_card);
    lv_obj_set_size(g_dash_hero, LV_PCT(100), 220);
    lv_obj_set_style_bg_color(g_dash_hero, lv_color_hex(0x1E232A), 0);
    lv_obj_set_style_bg_opa(g_dash_hero, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_dash_hero, 1, 0);
    lv_obj_set_style_border_color(g_dash_hero, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_radius(g_dash_hero, 8, 0);
    lv_obj_set_style_pad_all(g_dash_hero, 0, 0);

    g_dash_ring = lv_obj_create(g_dash_hero);
    lv_obj_set_size(g_dash_ring, 200, 200);
    lv_obj_set_style_bg_opa(g_dash_ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_dash_ring, 10, 0);
    lv_obj_set_style_border_color(g_dash_ring, lv_color_hex(0x22AA22), 0);
    lv_obj_set_style_radius(g_dash_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_center(g_dash_ring);

    g_lbl_status = lv_label_create(g_dash_hero);
    lv_label_set_text(g_lbl_status, "Calm");
    lv_obj_set_style_text_font(g_lbl_status, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(g_lbl_status, lv_color_hex(0x22AA22), 0);
    lv_obj_set_style_text_align(g_lbl_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_letter_space(g_lbl_status, 2, 0);
    lv_obj_center(g_lbl_status);

    // Stats row
    lv_obj_t* stats = lv_obj_create(g_dash_card);
    lv_obj_set_size(stats, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(stats, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(stats, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(stats, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(stats, 0, 0);
    lv_obj_set_style_pad_all(stats, 0, 0);
    lv_obj_set_style_pad_column(stats, 12, 0);
    lv_obj_set_flex_flow(stats, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(stats, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto make_tile = [&](const char* title, lv_obj_t** outVal){
        lv_obj_t* tile = lv_obj_create(stats);
        lv_obj_set_size(tile, LV_PCT(32), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(tile, lv_color_hex(0x1E232A), 0);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(tile, 1, 0);
        lv_obj_set_style_border_color(tile, lv_color_hex(0x2A2F36), 0);
        lv_obj_set_style_radius(tile, 8, 0);
        lv_obj_set_style_pad_hor(tile, 12, 0);
        lv_obj_set_style_pad_ver(tile, 10, 0);
        lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(tile, 6, 0);

        lv_obj_t* t = lv_label_create(tile);
        lv_label_set_text(t, title);
        lv_obj_set_style_text_color(t, lv_color_hex(0x9AA3AD), 0);

        lv_obj_t* v = lv_label_create(tile);
        lv_obj_set_style_text_color(v, lv_color_hex(0xEDEFF2), 0);
        lv_label_set_text(v, "--");
        if (outVal) *outVal = v;
        return tile;
    };

    make_tile("Connection", &g_stat_conn);
    make_tile("Volume",     &g_stat_vol);
    make_tile("Quiet Hours",&g_stat_qh);

    update_dashboard();

    // Status buttons row
    lv_obj_t* row1 = lv_obj_create(screen);
    lv_obj_set_size(row1, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_align(row1, LV_ALIGN_BOTTOM_MID, 0, -100);
    lv_obj_set_style_bg_color(row1, lv_color_hex(0x16191D), 0);
    lv_obj_set_style_bg_opa(row1, LV_OPA_COVER, 0);
    lv_obj_clear_flag(row1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(row1, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(row1, 12, 0);
    lv_obj_set_style_border_width(row1, 1, 0);
    lv_obj_set_style_border_color(row1, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_radius(row1, 8, 0);
    lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row1, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row1, 14, 0);

    auto make_btn = [&](const char* txt, lv_event_cb_t cb, const char* role) {
        lv_obj_t* b = lv_btn_create(row1);
        lv_obj_set_size(b, 160, 56);
        lv_obj_set_style_radius(b, 8, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x2A2F36), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_40, 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_border_color(b, lv_color_hex(0x3A4048), 0);
        lv_obj_set_style_pad_hor(b, 12, 0);
        lv_obj_set_style_pad_ver(b, 8, 0);
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
    lv_obj_set_style_bg_color(row2, lv_color_hex(0x1E232A), 0);
    lv_obj_set_style_bg_opa(row2, LV_OPA_COVER, 0);
    lv_obj_clear_flag(row2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(row2, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_border_width(row2, 1, 0);
    lv_obj_set_style_border_color(row2, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_radius(row2, 8, 0);
    lv_obj_set_style_pad_all(row2, 10, 0);
    lv_obj_set_flex_flow(row2, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row2, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row2, 18, 0);

    lv_obj_t* btnPlay = lv_btn_create(row2);
    lv_obj_set_size(btnPlay, 150, 56);
    lv_obj_set_style_radius(btnPlay, 8, 0);
    lv_obj_set_style_bg_color(btnPlay, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_bg_opa(btnPlay, LV_OPA_40, 0);
    lv_obj_set_style_border_width(btnPlay, 1, 0);
    lv_obj_set_style_border_color(btnPlay, lv_color_hex(0x3A4048), 0);
    lv_obj_set_style_pad_hor(btnPlay, 12, 0);
    lv_obj_set_style_pad_ver(btnPlay, 8, 0);
    { lv_obj_t* l = lv_label_create(btnPlay); lv_label_set_text(l, "Play"); lv_obj_center(l); }
    lv_obj_add_event_cb(btnPlay, on_btn_play, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* btnStop = lv_btn_create(row2);
    lv_obj_set_size(btnStop, 150, 56);
    lv_obj_set_style_radius(btnStop, 8, 0);
    lv_obj_set_style_bg_color(btnStop, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_bg_opa(btnStop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(btnStop, 1, 0);
    lv_obj_set_style_border_color(btnStop, lv_color_hex(0x3A4048), 0);
    lv_obj_set_style_pad_hor(btnStop, 12, 0);
    lv_obj_set_style_pad_ver(btnStop, 8, 0);
    { lv_obj_t* l = lv_label_create(btnStop); lv_label_set_text(l, "Stop"); lv_obj_center(l); }
    lv_obj_add_event_cb(btnStop, on_btn_stop, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* slider = lv_slider_create(row2);
    lv_obj_set_size(slider, 360, 10);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, g_volume, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, on_volume, LV_EVENT_VALUE_CHANGED, nullptr);
    // slider styling
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x2A2F36), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x7FB3FF), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xEDEFF2), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);

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
