// src/ui/dashboard.cpp
//
// Main (non-modal) screen:
// - Top bar (quick actions + buttons to open modals)
// - Dashboard card and status tiles
// - "Calm/Cry/Motion" status buttons (simulator-only inputs)
//
// This file also owns `update_top_label()` because it updates multiple widgets
// in response to state changes from other modules (e.g. Settings dialog).
#include "ui/dashboard.h"

#include "logging.h"

#include "ui/common.h"
#include "ui/state.h"

#include "ui/camera.h"
#include "ui/library.h"
#include "ui/listen.h"
#include "ui/lullabies.h"
#include "ui/settings.h"

#include "baby_pi_api.h"
#include <string>
#include <cstring>
#include <atomic>
#include <cstdint>
#include <thread>

namespace cg::ui {

static void update_dashboard();
static void apply_top_buttons_state();
static lv_obj_t* g_top_bar = nullptr;
static lv_obj_t* g_screen = nullptr;
static lv_obj_t* g_dash_hero = nullptr;
static lv_obj_t* g_stats_row = nullptr;
static lv_obj_t* g_row1 = nullptr;
static lv_obj_t* g_row2 = nullptr;
static lv_obj_t* g_vol_icon = nullptr;
static lv_obj_t* g_conn_chip = nullptr;
static lv_obj_t* g_conn_chip_dot = nullptr;
static lv_obj_t* g_conn_chip_lbl = nullptr;
static std::atomic<bool> g_rebuild_pending{false};
static std::atomic<bool> g_rebuild_in_progress{false};
static lv_obj_t* g_tile[3] = {nullptr, nullptr, nullptr};
static lv_obj_t* g_tile_title[3] = {nullptr, nullptr, nullptr};
static lv_obj_t* g_tile_value[3] = {nullptr, nullptr, nullptr};
static lv_obj_t* g_btn_status[3] = {nullptr, nullptr, nullptr};
static lv_obj_t* g_btn_media[3] = {nullptr, nullptr, nullptr};
static lv_obj_t* g_btn_library = nullptr;
static lv_obj_t* g_btn_lullabies = nullptr;
static lv_obj_t* g_btn_cam = nullptr;
static lv_obj_t* g_btn_settings = nullptr;

// Convert a 24-hour integer (0-23) into a display string ("H:00 AM/PM").
static std::string format_time12(int hour24) {
    int h = ((hour24 % 24) + 24) % 24;
    int display = h % 12;
    if (display == 0) display = 12;
    bool pm = h >= 12;
    return std::to_string(display) + ":00" + (pm ? " PM" : " AM");
}

// Update the main "status" label and refresh the dashboard visuals.
static void set_status_text(const char* txt, lv_color_t color) {
    if (state::lbl_status) {
        lv_label_set_text(state::lbl_status, txt);
        lv_obj_set_style_text_color(state::lbl_status, color, 0);
    }
    log_line((std::string("[UI] status: ") + txt).c_str());
    update_dashboard();
}

static void set_btn_label_color(lv_obj_t* btn, lv_color_t color) {
    if (!btn) return;
    lv_obj_t* lbl = lv_obj_get_child(btn, 0);
    if (lbl) lv_obj_set_style_text_color(lbl, color, 0);
}

static void set_btn_tonal_style(lv_obj_t* btn) {
    if (!btn) return;
    lv_obj_set_style_bg_color(btn, theme::tonal_bg(), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_40, 0);
    lv_obj_set_style_border_color(btn, theme::tonal_border(), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
}

// Refresh the top-left brand label and quick-action buttons, then update dashboard tiles.
void update_top_label() {
    if (g_rebuild_in_progress.load()) return;
    // Keep brand short in the top-left; reflect state via quick-action pills
    if (state::lbl_conn) lv_label_set_text(state::lbl_conn, "CribGuard");
    if (g_conn_chip_dot) {
        lv_color_t dot = state::connected ? lv_color_hex(0x3BD16F) : lv_color_hex(0xD15C5C);
        lv_obj_set_style_bg_color(g_conn_chip_dot, dot, 0);
    }
    if (g_conn_chip_lbl) {
        lv_label_set_text(g_conn_chip_lbl, state::connected ? "Connected" : "Offline");
        lv_obj_set_style_text_color(g_conn_chip_lbl, theme::text_main(), 0);
    }
    apply_top_buttons_state();
    std::string log_s = std::string("brand=CribGuard, conn=") + (state::connected ? "on" : "off")
        + ", vol=" + std::to_string(state::volume)
        + ", quiet=" + (state::quiet_hours ? "on" : "off");
    log_line((std::string("[UI] topbar: ") + log_s).c_str());
    update_dashboard();
}

// Handler for the simulator status buttons ("Calm/Cry/Motion").
static void on_btn_status(lv_event_t* e) {
    const char* role = (const char*)lv_event_get_user_data(e);
    if (!role) return;

    if (std::strcmp(role, "calm") == 0) {
        set_status_text("Calm", lv_color_hex(0x22AA22));
    } else if (std::strcmp(role, "cry") == 0) {
        set_status_text("Cry", lv_color_hex(0xCC2222));
    } else if (std::strcmp(role, "motion") == 0) {
        set_status_text("Motion", lv_color_hex(0xD08770));
    }
}

// Handler for the simple media Play button (simulator-only).
static void on_btn_play(lv_event_t* /*e*/) {
    set_status_text("Playing", lv_color_hex(0x3366FF));
}

// Handler for the simple media Pause button (simulator-only).
static void on_btn_pause(lv_event_t* /*e*/) {
    set_status_text("Paused", lv_color_hex(0x777777));
}

// Handler for the simple media Stop button (simulator-only).
static void on_btn_stop(lv_event_t* /*e*/) {
    set_status_text("Stopped", lv_color_hex(0x444444));
}

static void on_status_async(void* p) {
    bool ok = (reinterpret_cast<std::uintptr_t>(p) != 0);
    state::connected = ok;
    update_top_label();
}

static std::atomic<bool> g_status_in_flight{false};
static lv_timer_t* g_status_timer = nullptr;

static void request_status_check() {
    if (g_status_in_flight.exchange(true)) return;
    std::thread([](){
        const bool ok = baby_pi_check_status();
        lv_async_call(on_status_async, reinterpret_cast<void*>(static_cast<std::uintptr_t>(ok)));
        g_status_in_flight.store(false);
    }).detach();
}

static void vol_apply_visuals();

static void set_volume_value(int v) {
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    if (state::volume == v) return;
    state::volume = v;
    update_top_label();
    vol_apply_visuals(); // keep bottom volume control in sync (even if volume changes elsewhere)
    log_line("[UI] volume changed");
}

struct VolCtrl {
    lv_obj_t* stack{};
    lv_obj_t* track{};
    lv_obj_t* fill{};
    lv_obj_t* knob{};
    int track_w{360};
    int track_h{10};
    int knob_sz{16};
    int stack_h{16};
};

static VolCtrl g_vol;

static void apply_theme() {
    if (g_screen) lv_obj_set_style_bg_color(g_screen, theme::app_bg(), 0);
    if (g_top_bar) {
        lv_obj_set_style_bg_color(g_top_bar, theme::header_bg(), 0);
        lv_obj_set_style_border_color(g_top_bar, theme::border(), 0);
    }
    if (g_conn_chip) lv_obj_set_style_bg_color(g_conn_chip, theme::tonal_bg(), 0);
    if (g_conn_chip_lbl) lv_obj_set_style_text_color(g_conn_chip_lbl, theme::text_main(), 0);
    if (state::lbl_conn) lv_obj_set_style_text_color(state::lbl_conn, theme::text_main(), 0);

    if (state::dash_card) {
        lv_obj_set_style_bg_color(state::dash_card, theme::surface_bg(), 0);
        lv_obj_set_style_border_color(state::dash_card, theme::border(), 0);
    }
    if (g_dash_hero) {
        lv_obj_set_style_bg_color(g_dash_hero, theme::header_bg(), 0);
        lv_obj_set_style_border_color(g_dash_hero, theme::border(), 0);
    }

    for (int i = 0; i < 3; ++i) {
        if (g_tile[i]) {
            lv_obj_set_style_bg_color(g_tile[i], theme::header_bg(), 0);
            lv_obj_set_style_border_color(g_tile[i], theme::border(), 0);
        }
        if (g_tile_title[i]) lv_obj_set_style_text_color(g_tile_title[i], theme::text_subtle(), 0);
        if (g_tile_value[i]) lv_obj_set_style_text_color(g_tile_value[i], theme::text_main(), 0);
    }

    if (g_row1) {
        lv_obj_set_style_bg_color(g_row1, theme::surface_bg(), 0);
        lv_obj_set_style_border_color(g_row1, theme::border(), 0);
    }
    if (g_row2) {
        lv_obj_set_style_bg_color(g_row2, theme::surface_bg(), 0);
        lv_obj_set_style_border_color(g_row2, theme::border(), 0);
    }
    if (g_vol_icon) lv_obj_set_style_text_color(g_vol_icon, theme::text_subtle(), 0);
    if (g_vol.track) lv_obj_set_style_bg_color(g_vol.track, theme::tonal_bg(), 0);
    if (g_vol.fill) lv_obj_set_style_bg_color(g_vol.fill, theme::primary_accent(), 0);
    if (g_vol.knob) lv_obj_set_style_bg_color(g_vol.knob, theme::text_main(), 0);

    set_btn_tonal_style(g_btn_library);
    set_btn_tonal_style(g_btn_lullabies);
    set_btn_tonal_style(g_btn_cam);
    set_btn_tonal_style(g_btn_settings);
    set_btn_label_color(g_btn_library, theme::text_main());
    set_btn_label_color(g_btn_lullabies, theme::text_main());
    set_btn_label_color(g_btn_cam, theme::text_main());
    set_btn_label_color(g_btn_settings, theme::text_main());
    for (int i = 0; i < 3; ++i) {
        set_btn_tonal_style(g_btn_status[i]);
        set_btn_tonal_style(g_btn_media[i]);
        set_btn_label_color(g_btn_status[i], theme::text_main());
        set_btn_label_color(g_btn_media[i], theme::text_main());
    }

    apply_top_buttons_state();
    update_top_label();
}

static void vol_apply_visuals() {
    if (!g_vol.stack || !g_vol.track || !g_vol.fill || !g_vol.knob) return;
    const int v = state::volume;
    const int w = g_vol.track_w;
    const int k = g_vol.knob_sz;
    const int usable = (w - k) > 0 ? (w - k) : 1;
    const int knob_x = (v * usable) / 100;
    const int knob_y = (g_vol.stack_h - k) / 2;
    const int track_y = (g_vol.stack_h - g_vol.track_h) / 2;
    const int fill_w = knob_x + (k / 2);
    lv_obj_set_width(g_vol.fill, fill_w);
    lv_obj_set_x(g_vol.knob, knob_x);
    lv_obj_set_y(g_vol.knob, knob_y);
    lv_obj_set_y(g_vol.track, track_y);
}

static void vol_set_from_point(lv_obj_t* track, const lv_point_t& p) {
    lv_area_t a;
    lv_obj_get_coords(track, &a);
    const int w = lv_area_get_width(&a);
    int rel = p.x - a.x1;
    if (rel < 0) rel = 0;
    if (rel > w) rel = w;
    // Map pointer position to knob *center* so ends feel natural.
    const int k = g_vol.knob_sz;
    const int usable = (w - k) > 0 ? (w - k) : 1;
    int rel2 = rel - (k / 2);
    if (rel2 < 0) rel2 = 0;
    if (rel2 > usable) rel2 = usable;
    int v = (rel2 * 100) / usable;
    set_volume_value(v);
    vol_apply_visuals();
}

static void on_vol_track(lv_event_t* e) {
    const lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING) return;
    // Always map using the track's coordinates.
    lv_obj_t* track = g_vol.track ? g_vol.track : (lv_obj_t*)lv_event_get_target(e);
    lv_indev_t* indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    vol_set_from_point(track, p);
}

// Toggle quiet-hours state from the top bar pill.
static void on_top_quiet_click(lv_event_t* /*e*/) {
    state::quiet_hours = !state::quiet_hours;
    update_top_label();
    apply_top_buttons_state();
}

// Toggle connection state from the top bar pill.
static void on_top_conn_click(lv_event_t* /*e*/) {
    state::connected = !state::connected;
    update_top_label();
    apply_top_buttons_state();
}

// Toggle light/dark theme from the top bar.
static void on_top_theme_click(lv_event_t* /*e*/) {
    if (g_rebuild_pending.load() || g_rebuild_in_progress.load()) return;
    state::light_mode = !state::light_mode;
    log_line(state::light_mode ? "[UI] theme: light" : "[UI] theme: dark");
    apply_theme();
}

// Refresh the dashboard ring and tiles based on current UI state.
static void update_dashboard() {
    if (g_rebuild_in_progress.load()) return;
    if (!state::dash_card) return;

    // Determine status color and apply to hero ring
    const char* st = state::lbl_status ? lv_label_get_text(state::lbl_status) : "Calm";
    lv_color_t ring = lv_color_hex(0x22AA22);
    if (st && std::strcmp(st, "Cry") == 0)          ring = lv_color_hex(0xCC2222);
    else if (st && std::strcmp(st, "Motion") == 0)  ring = lv_color_hex(0xD08770);
    if (state::dash_ring) {
        lv_obj_set_style_bg_opa(state::dash_ring, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(state::dash_ring, 10, 0);
        lv_obj_set_style_border_color(state::dash_ring, ring, 0);
        lv_obj_set_style_radius(state::dash_ring, LV_RADIUS_CIRCLE, 0);
    }

    // Connection stat
    if (state::stat_conn) {
        lv_label_set_text(state::stat_conn, state::connected ? "Connected" : "Disconnected");
        lv_obj_set_style_text_color(state::stat_conn, state::connected ? theme::primary_accent() : lv_color_hex(0xFF6B6B), 0);
    }
    // Volume stat
    if (state::stat_vol) {
        std::string s = std::to_string(state::volume) + "%";
        lv_label_set_text(state::stat_vol, s.c_str());
    }
    // Quiet Hours stat
    if (state::stat_qh) {
        if (state::quiet_hours) {
            std::string s = "On  ";
            s += format_time12(state::quiet_start);
            s += " - ";
            s += format_time12(state::quiet_end);
            lv_label_set_text(state::stat_qh, s.c_str());
            lv_obj_set_style_text_color(state::stat_qh, theme::primary_accent(), 0);
        } else {
            lv_label_set_text(state::stat_qh, "Off");
            lv_obj_set_style_text_color(state::stat_qh, theme::text_subtle(), 0);
        }
    }
}

// Update visual state (colors/text) of the top-bar quick-action pills.
static void apply_top_buttons_state() {
    if (g_rebuild_in_progress.load()) return;
    const bool light = state::light_mode;
    // Quiet button
    if (state::btn_quiet) {
        lv_color_t bg = state::quiet_hours
            ? (light ? lv_color_hex(0xE3EEF9) : lv_color_hex(0x2F3B46))
            : theme::tonal_bg();
        lv_color_t fg = state::quiet_hours
            ? (light ? theme::primary_accent() : lv_color_hex(0xB0D7FF))
            : theme::text_main();
        lv_obj_set_style_bg_color(state::btn_quiet, bg, 0);
        lv_obj_set_style_bg_opa(state::btn_quiet, LV_OPA_40, 0);
        lv_obj_t* lbl = lv_obj_get_child(state::btn_quiet, 0);
        if (lbl) {
            lv_label_set_text(lbl, state::quiet_hours ? LV_SYMBOL_MUTE " Quiet On" : LV_SYMBOL_VOLUME_MAX " Quiet Off");
            lv_obj_set_style_text_color(lbl, fg, 0);
        }
    }
    // Connection button
    if (state::btn_conn) {
        lv_color_t bg = state::connected
            ? (light ? lv_color_hex(0xE4F6EC) : lv_color_hex(0x294236))
            : (light ? lv_color_hex(0xFBEAEA) : lv_color_hex(0x3F2A2A));
        lv_color_t fg = state::connected
            ? (light ? lv_color_hex(0x1D7A46) : lv_color_hex(0xB2F5DC))
            : (light ? lv_color_hex(0xB83B3B) : lv_color_hex(0xFFB3B3));
        lv_obj_set_style_bg_color(state::btn_conn, bg, 0);
        lv_obj_set_style_bg_opa(state::btn_conn, LV_OPA_40, 0);
        lv_obj_t* lbl = lv_obj_get_child(state::btn_conn, 0);
        if (lbl) {
            lv_label_set_text(lbl, state::connected ? LV_SYMBOL_WIFI " Connected" : LV_SYMBOL_CLOSE " Offline");
            lv_obj_set_style_text_color(lbl, fg, 0);
        }
    }
    // Theme button
    if (state::btn_theme) {
        lv_color_t bg = state::light_mode ? lv_color_hex(0xE3EEF9) : theme::tonal_bg();
        lv_color_t fg = state::light_mode ? theme::primary_accent() : theme::text_main();
        lv_obj_set_style_bg_color(state::btn_theme, bg, 0);
        lv_obj_set_style_bg_opa(state::btn_theme, LV_OPA_40, 0);
        lv_obj_t* lbl = lv_obj_get_child(state::btn_theme, 0);
        if (lbl) {
            lv_label_set_text(lbl, state::light_mode ? "Light" : "Dark");
            lv_obj_set_style_text_color(lbl, fg, 0);
        }
    }
}

// Build the main screen (non-modal UI) on a fresh LVGL screen object.
void build_main_screen() {
    // Root screen (no scroll)
    lv_obj_t* screen = lv_obj_create(nullptr);
    g_screen = screen;
    // Ensure no default padding/margins affect bottom-aligned bars.
    lv_obj_remove_style_all(screen);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(screen, theme::app_bg(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_screen_load(screen);

    // Top bar (fixed)
    lv_obj_t* top = lv_obj_create(screen);
    lv_obj_set_size(top, LV_PCT(100), 64);
    lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(top, LV_SCROLLBAR_MODE_OFF);

    g_top_bar = top;
    lv_obj_set_style_bg_color(top, theme::header_bg(), 0);
    lv_obj_set_style_bg_opa(top, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(top, theme::sp16, 0);
    lv_obj_set_style_pad_ver(top, theme::sp10, 0);
    lv_obj_set_style_border_width(top, 1, 0);
    lv_obj_set_style_border_color(top, theme::border(), 0);
    lv_obj_set_style_border_side(top, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_outline_opa(top, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_opa(top, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(top, [](lv_event_t* e){
        if (lv_event_get_code(e) == LV_EVENT_DELETE) {
            if (lv_event_get_target(e) != g_top_bar) return;
            g_top_bar = nullptr;
            g_conn_chip = nullptr;
            g_conn_chip_dot = nullptr;
            g_conn_chip_lbl = nullptr;
            state::lbl_conn = nullptr;
            state::btn_quiet = nullptr;
            state::btn_conn = nullptr;
            state::btn_theme = nullptr;
        }
    }, LV_EVENT_DELETE, nullptr);

    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Left group (brand + connection chip)
    lv_obj_t* left_grp = lv_obj_create(top);
    lv_obj_clear_flag(left_grp, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(left_grp, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(left_grp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left_grp, 0, 0);
    lv_obj_set_style_pad_all(left_grp, 0, 0);
    lv_obj_set_style_pad_column(left_grp, theme::sp8, 0);
    lv_obj_set_flex_flow(left_grp, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left_grp, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_size(left_grp, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    state::lbl_conn = lv_label_create(left_grp);
    lv_obj_set_style_text_color(state::lbl_conn, theme::text_main(), 0);
    lv_obj_set_style_pad_left(state::lbl_conn, 2, 0);
    lv_obj_set_style_pad_right(state::lbl_conn, theme::sp8, 0);

    g_conn_chip = lv_obj_create(left_grp);
    lv_obj_clear_flag(g_conn_chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(g_conn_chip, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(g_conn_chip, theme::tonal_bg(), 0);
    lv_obj_set_style_bg_opa(g_conn_chip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_conn_chip, 0, 0);
    lv_obj_set_style_radius(g_conn_chip, theme::radius_pill, 0);
    lv_obj_set_style_pad_hor(g_conn_chip, theme::sp8, 0);
    lv_obj_set_style_pad_ver(g_conn_chip, theme::sp4, 0);
    lv_obj_set_style_pad_column(g_conn_chip, theme::sp6, 0);
    lv_obj_set_flex_flow(g_conn_chip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_conn_chip, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_size(g_conn_chip, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    g_conn_chip_dot = lv_obj_create(g_conn_chip);
    lv_obj_set_size(g_conn_chip_dot, 8, 8);
    lv_obj_set_style_radius(g_conn_chip_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(g_conn_chip_dot, 0, 0);
    lv_obj_set_style_bg_color(g_conn_chip_dot, lv_color_hex(0x3BD16F), 0);
    lv_obj_set_style_bg_opa(g_conn_chip_dot, LV_OPA_COVER, 0);

    g_conn_chip_lbl = lv_label_create(g_conn_chip);
    lv_label_set_text(g_conn_chip_lbl, "Connected");
    lv_obj_set_style_text_color(g_conn_chip_lbl, theme::text_main(), 0);

    update_top_label();

    // Right controls (quick-action pills and dialogs)
    lv_obj_t* right_grp = lv_obj_create(top);
    lv_obj_clear_flag(right_grp, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(right_grp, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(right_grp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_grp, 0, 0);
    lv_obj_set_style_pad_all(right_grp, 0, 0);
    lv_obj_set_style_pad_column(right_grp, 10, 0);
    lv_obj_set_flex_flow(right_grp, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(right_grp, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    // Library button
    {
        lv_obj_t* btn = lv_btn_create(right_grp);
        g_btn_library = btn;
        style_button_tonal(btn);
        set_centered_button_label(btn, LV_SYMBOL_LIST " Library");
        lv_obj_add_event_cb(btn, [](lv_event_t* /*e*/){ build_library_dialog((lv_obj_t*)lv_screen_active()); }, LV_EVENT_CLICKED, nullptr);
    }

    // Lullabies button
    {
        lv_obj_t* btn = lv_btn_create(right_grp);
        g_btn_lullabies = btn;
        style_button_tonal(btn);
        set_centered_button_label(btn, LV_SYMBOL_AUDIO " Lullabies");
        lv_obj_add_event_cb(btn, [](lv_event_t* /*e*/){ build_lullabies_dialog((lv_obj_t*)lv_screen_active()); }, LV_EVENT_CLICKED, nullptr);
    }

    // Quick-action pills instead of switches
    state::btn_quiet = lv_btn_create(right_grp);
    style_button_pill(state::btn_quiet);
    set_centered_button_label(state::btn_quiet, LV_SYMBOL_MUTE " Quiet");
    lv_obj_add_event_cb(state::btn_quiet, on_top_quiet_click, LV_EVENT_CLICKED, nullptr);

    state::btn_conn = lv_btn_create(right_grp);
    style_button_pill(state::btn_conn);
    set_centered_button_label(state::btn_conn, LV_SYMBOL_WIFI " Conn");
    lv_obj_add_event_cb(state::btn_conn, on_top_conn_click, LV_EVENT_CLICKED, nullptr);

    state::btn_theme = lv_btn_create(right_grp);
    style_button_pill(state::btn_theme);
    set_centered_button_label(state::btn_theme, "Dark");
    lv_obj_add_event_cb(state::btn_theme, on_top_theme_click, LV_EVENT_CLICKED, nullptr);

    // Listen toggle button (mic streaming from baby pi -> parent pi speaker).
    // Shares state with the Listen switch on the camera screen; either flips
    // both. Styled as a pill so it reads as a toggle, not a nav button.
    {
        lv_obj_t* btn_listen = lv_btn_create(right_grp);
        style_button_pill(btn_listen);
        lv_obj_add_flag(btn_listen, LV_OBJ_FLAG_CHECKABLE);
        set_centered_button_label(btn_listen, LV_SYMBOL_AUDIO " Listen");
        lv_obj_add_event_cb(btn_listen, [](lv_event_t* /*e*/){ listen_toggle(); }, LV_EVENT_CLICKED, nullptr);
        listen_register_widget(btn_listen);
    }

    // Camera open button
    lv_obj_t* btn_cam = lv_btn_create(right_grp);
    g_btn_cam = btn_cam;
    style_button_tonal(btn_cam);
    set_centered_button_label(btn_cam, LV_SYMBOL_VIDEO " Cam");
    lv_obj_add_event_cb(btn_cam, [](lv_event_t* /*e*/){ build_camera_dialog((lv_obj_t*)lv_screen_active()); }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* btn_gear = lv_btn_create(right_grp);
    g_btn_settings = btn_gear;
    style_button_tonal(btn_gear);
    set_centered_button_label(btn_gear, LV_SYMBOL_SETTINGS);
    lv_obj_add_event_cb(btn_gear, [](lv_event_t* /*e*/){ build_settings_dialog((lv_obj_t*)lv_screen_active()); }, LV_EVENT_CLICKED, nullptr);

    apply_top_buttons_state();

    // Center dashboard card
    state::dash_card = lv_obj_create(screen);
    lv_obj_set_size(state::dash_card, LV_PCT(92), 360);
    lv_obj_align(state::dash_card, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_bg_color(state::dash_card, theme::surface_bg(), 0);
    lv_obj_set_style_bg_opa(state::dash_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(state::dash_card, 1, 0);
    lv_obj_set_style_border_color(state::dash_card, theme::border(), 0);
    lv_obj_set_style_radius(state::dash_card, 10, 0);
    lv_obj_set_style_shadow_width(state::dash_card, 24, 0);
    lv_obj_set_style_shadow_opa(state::dash_card, LV_OPA_20, 0);
    lv_obj_set_style_shadow_color(state::dash_card, lv_color_hex(0x000000), 0);
    lv_obj_set_style_pad_hor(state::dash_card, theme::sp16, 0);
    lv_obj_set_style_pad_ver(state::dash_card, theme::sp14, 0);
    lv_obj_set_flex_flow(state::dash_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(state::dash_card, theme::sp12, 0);

    // Hero area with ring and status label
    lv_obj_t* dash_hero = lv_obj_create(state::dash_card);
    g_dash_hero = dash_hero;
    lv_obj_set_size(dash_hero, LV_PCT(100), 220);
    lv_obj_set_style_bg_color(dash_hero, theme::header_bg(), 0);
    lv_obj_set_style_bg_opa(dash_hero, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dash_hero, 1, 0);
    lv_obj_set_style_border_color(dash_hero, theme::border(), 0);
    lv_obj_set_style_radius(dash_hero, 8, 0);
    lv_obj_set_style_pad_all(dash_hero, 0, 0);

    state::dash_ring = lv_obj_create(dash_hero);
    lv_obj_set_size(state::dash_ring, 200, 200);
    lv_obj_set_style_bg_opa(state::dash_ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(state::dash_ring, 10, 0);
    lv_obj_set_style_border_color(state::dash_ring, lv_color_hex(0x22AA22), 0);
    lv_obj_set_style_radius(state::dash_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_center(state::dash_ring);

    state::lbl_status = lv_label_create(dash_hero);
    lv_label_set_text(state::lbl_status, "Calm");
    // Use default font to avoid missing font symbols in LVGL builds.
    lv_obj_set_style_text_font(state::lbl_status, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(state::lbl_status, lv_color_hex(0x22AA22), 0);
    lv_obj_set_style_text_align(state::lbl_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_letter_space(state::lbl_status, 1, 0);
    lv_obj_center(state::lbl_status);

    // Stats row
    lv_obj_t* stats = lv_obj_create(state::dash_card);
    g_stats_row = stats;
    lv_obj_set_size(stats, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(stats, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(stats, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(stats, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(stats, 0, 0);
    lv_obj_set_style_pad_all(stats, 0, 0);
    lv_obj_set_style_pad_column(stats, theme::sp16, 0);
    lv_obj_set_flex_flow(stats, LV_FLEX_FLOW_ROW);
    // Distribute the three tiles across the full row so they read visually centered.
    lv_obj_set_flex_align(stats, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    int tile_idx = 0;
    auto make_tile = [&](const char* title, lv_obj_t** outVal){
        lv_obj_t* tile = lv_obj_create(stats);
        lv_obj_set_size(tile, LV_PCT(32), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(tile, theme::header_bg(), 0);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(tile, 1, 0);
        lv_obj_set_style_border_color(tile, theme::border(), 0);
        lv_obj_set_style_radius(tile, 8, 0);
        lv_obj_set_style_pad_hor(tile, 12, 0);
        lv_obj_set_style_pad_ver(tile, 10, 0);
        lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(tile, 6, 0);

        lv_obj_t* t = lv_label_create(tile);
        lv_label_set_text(t, title);
        lv_obj_set_style_text_color(t, theme::text_subtle(), 0);
        // Center label text within the tile.
        lv_obj_set_width(t, LV_PCT(100));
        lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);

        lv_obj_t* v = lv_label_create(tile);
        lv_obj_set_style_text_color(v, theme::text_main(), 0);
        lv_label_set_text(v, "--");
        // Center value text within the tile.
        lv_obj_set_width(v, LV_PCT(100));
        lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_CENTER, 0);
        if (outVal) *outVal = v;
        if (tile_idx < 3) {
            g_tile[tile_idx] = tile;
            g_tile_title[tile_idx] = t;
            g_tile_value[tile_idx] = v;
        }
        tile_idx++;
        return tile;
    };

    make_tile(LV_SYMBOL_WIFI " Connection", &state::stat_conn);
    make_tile(LV_SYMBOL_VOLUME_MAX " Volume",     &state::stat_vol);
    make_tile(LV_SYMBOL_MUTE " Quiet Hours",&state::stat_qh);

    update_dashboard();

    // Status buttons row
    lv_obj_t* row1 = lv_obj_create(screen);
    g_row1 = row1;
    lv_obj_set_size(row1, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_align(row1, LV_ALIGN_BOTTOM_MID, 0, -100);
    lv_obj_set_style_bg_color(row1, theme::surface_bg(), 0);
    lv_obj_set_style_bg_opa(row1, LV_OPA_COVER, 0);
    lv_obj_clear_flag(row1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(row1, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(row1, theme::sp12, 0);
    lv_obj_set_style_border_width(row1, 0, 0);
    lv_obj_set_style_border_color(row1, theme::border(), 0);
    lv_obj_set_style_radius(row1, 8, 0);
    lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row1, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row1, theme::sp14, 0);

    auto make_btn = [&](const char* txt, const char* role) {
        lv_obj_t* b = lv_btn_create(row1);
        lv_obj_set_size(b, 160, 56);
        style_button_tonal_ex(b, 8, theme::sp12, theme::sp8);
        set_centered_button_label(b, txt);
        lv_obj_add_event_cb(b, on_btn_status, LV_EVENT_CLICKED, (void*)role);
        return b;
    };

    g_btn_status[0] = make_btn("Calm", "calm");
    g_btn_status[1] = make_btn("Cry", "cry");
    g_btn_status[2] = make_btn("Motion", "motion");

    // Media controls + volume slider row (simulator-only convenience).
    lv_obj_t* row2 = lv_obj_create(screen);
    g_row2 = row2;
    lv_obj_set_size(row2, LV_PCT(100), LV_SIZE_CONTENT);
    // Keep this bar flush to the bottom edge of the window.
    lv_obj_align(row2, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(row2, theme::surface_bg(), 0);
    lv_obj_set_style_bg_opa(row2, LV_OPA_COVER, 0);
    lv_obj_clear_flag(row2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(row2, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(row2, theme::sp12, 0);
    lv_obj_set_style_border_width(row2, 1, 0);
    lv_obj_set_style_border_color(row2, theme::border(), 0);
    lv_obj_set_style_border_side(row2, LV_BORDER_SIDE_TOP, 0);
    // Square corners so the bar sits flush to the bottom edge (no visible "gap").
    lv_obj_set_style_radius(row2, 0, 0);
    lv_obj_set_flex_flow(row2, LV_FLEX_FLOW_ROW);
    // Keep controls left-aligned and let the volume slider expand to the right.
    // This prevents the slider from looking "floated" and keeps spacing consistent.
    lv_obj_set_flex_align(row2, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row2, theme::sp14, 0);

    auto make_ctrl_btn = [&](const char* txt, lv_event_cb_t cb) {
        lv_obj_t* b = lv_btn_create(row2);
        lv_obj_set_size(b, 80, 56);
        style_button_tonal_ex(b, 8, theme::sp12, theme::sp8);
        set_centered_button_label(b, txt);
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
        return b;
    };

    g_btn_media[0] = make_ctrl_btn(LV_SYMBOL_PLAY,  on_btn_play);
    g_btn_media[1] = make_ctrl_btn(LV_SYMBOL_PAUSE, on_btn_pause);
    g_btn_media[2] = make_ctrl_btn(LV_SYMBOL_STOP,  on_btn_stop);

    // Flexible spacer so the slider hugs the right side (removes awkward empty space).
    {
        lv_obj_t* spacer = lv_obj_create(row2);
        lv_obj_set_size(spacer, 1, 1);
        lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(spacer, 0, 0);
        lv_obj_clear_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_grow(spacer, 1);
    }

    // Volume control (professional: icon + slider; value is shown in the tile above)
    lv_obj_t* vol_grp = lv_obj_create(row2);
    lv_obj_clear_flag(vol_grp, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(vol_grp, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(vol_grp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(vol_grp, 0, 0);
    lv_obj_set_style_pad_all(vol_grp, 0, 0);
    lv_obj_set_style_pad_column(vol_grp, theme::sp6, 0);
    lv_obj_set_flex_flow(vol_grp, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(vol_grp, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_size(vol_grp, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    lv_obj_t* vol_icon = lv_label_create(vol_grp);
    g_vol_icon = vol_icon;
    lv_label_set_text(vol_icon, LV_SYMBOL_VOLUME_MAX);
    lv_obj_set_style_text_color(vol_icon, theme::text_subtle(), 0);

    // Custom volume control (track + fill + knob). Avoids LVGL slider knob clipping entirely.
    g_vol = VolCtrl{};
    g_vol.track_w = 360;
    g_vol.track_h = 10;
    g_vol.knob_sz = 16;
    g_vol.stack_h = 16;

    // Stack container so the knob can be larger than the track without being clipped.
    lv_obj_t* stack = lv_obj_create(vol_grp);
    g_vol.stack = stack;
    lv_obj_remove_style_all(stack);
    lv_obj_clear_flag(stack, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(stack, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(stack, g_vol.track_w, g_vol.stack_h);
    lv_obj_set_style_bg_opa(stack, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(stack, 0, 0);
    lv_obj_set_style_pad_all(stack, 0, 0);
    lv_obj_add_flag(stack, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(stack, on_vol_track, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(stack, on_vol_track, LV_EVENT_PRESSING, nullptr);
    // If the screen is rebuilt/destroyed, prevent stale pointers from being used.
    lv_obj_add_event_cb(stack, [](lv_event_t* e){
        if (lv_event_get_code(e) == LV_EVENT_DELETE) {
            if (lv_event_get_target(e) == g_vol.stack) {
                g_vol = VolCtrl{};
            }
        }
    }, LV_EVENT_DELETE, nullptr);

    lv_obj_t* track = lv_obj_create(stack);
    g_vol.track = track;
    lv_obj_remove_style_all(track);
    lv_obj_clear_flag(track, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(track, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(track, g_vol.track_w, g_vol.track_h);
    lv_obj_set_style_bg_color(track, theme::tonal_bg(), 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_40, 0);
    lv_obj_set_style_radius(track, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(track, 0, 0);
    lv_obj_set_pos(track, 0, (g_vol.stack_h - g_vol.track_h) / 2);

    lv_obj_t* fill = lv_obj_create(track);
    g_vol.fill = fill;
    lv_obj_remove_style_all(fill);
    lv_obj_clear_flag(fill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(fill, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_height(fill, g_vol.track_h);
    lv_obj_set_width(fill, 0);
    lv_obj_set_style_bg_color(fill, theme::primary_accent(), 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(fill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(fill, 0, 0);
    lv_obj_set_pos(fill, 0, 0);

    lv_obj_t* knob = lv_obj_create(stack);
    g_vol.knob = knob;
    lv_obj_remove_style_all(knob);
    lv_obj_clear_flag(knob, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(knob, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(knob, g_vol.knob_sz, g_vol.knob_sz);
    lv_obj_set_style_bg_color(knob, theme::text_main(), 0);
    lv_obj_set_style_bg_opa(knob, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(knob, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(knob, 0, 0);
    lv_obj_set_style_outline_width(knob, 0, 0);
    lv_obj_add_flag(knob, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(knob, on_vol_track, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(knob, on_vol_track, LV_EVENT_PRESSING, nullptr);
    lv_obj_move_foreground(knob);

    // Initial position
    vol_apply_visuals();

    log_line("[UI] UI built");

    // Periodically check Baby Pi status and reflect it in the UI.
    if (!g_status_timer) {
        request_status_check();
        g_status_timer = lv_timer_create([](lv_timer_t* /*t*/){
            request_status_check();
        }, 5000, nullptr);
    }
}

// Rebuild the UI (used when switching between light/dark modes).
void rebuild_main_screen() {
    log_line("[UI] rebuild: start");
    // Clear widget pointers to avoid touching old-screen objects during rebuild.
    g_top_bar = nullptr;
    g_conn_chip = nullptr;
    g_conn_chip_dot = nullptr;
    g_conn_chip_lbl = nullptr;
    state::lbl_conn = nullptr;
    state::btn_quiet = nullptr;
    state::btn_conn = nullptr;
    state::btn_theme = nullptr;
    state::dash_card = nullptr;
    state::dash_ring = nullptr;
    state::stat_conn = nullptr;
    state::stat_vol = nullptr;
    state::stat_qh = nullptr;
    state::lbl_status = nullptr;
    g_vol = VolCtrl{};
    build_main_screen();
    log_line("[UI] rebuild: end");
}

static void on_rebuild_async(void* /*p*/) {
    g_rebuild_in_progress.store(true);
    g_rebuild_pending.store(false);
    log_line("[UI] rebuild: async");
    // Release any active input to avoid dangling pressed objects.
    lv_indev_reset(NULL, NULL);
    rebuild_main_screen();
    g_rebuild_in_progress.store(false);
}

void request_rebuild() {
    if (g_rebuild_pending.exchange(true)) return;
    lv_async_call(on_rebuild_async, nullptr);
}

} // namespace cg::ui

