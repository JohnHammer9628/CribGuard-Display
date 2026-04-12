// src/ui/camera.cpp
//
// Camera modal:
// - Starts/stops the Baby Pi camera stream (HTTP) and the local UDP receiver (GStreamer)
// - Displays frames by updating `g_cam_img` (an LVGL image widget)
//
// Important linkage note:
// - `src/camera_rx_gst.cpp` references `g_cam_img` as a global symbol, so we keep it
//   in the global namespace (not inside `cg::ui`).
#include "ui/camera.h"

#include "baby_pi_api.h"
#include "camera_rx_gst.h"
#include "logging.h"

#include "ui/common.h"

#include <atomic>
#include <chrono>
#include <thread>

// Global (not namespaced) for `src/camera_rx_gst.cpp` compatibility.
lv_obj_t* g_cam_img = nullptr;
lv_obj_t* g_cam_stats_label = nullptr;
lv_obj_t* g_cam_spinner = nullptr;

namespace cg::ui {

static lv_obj_t* g_camera_modal = nullptr;
static lv_obj_t* g_camera_prev_screen = nullptr;
static lv_obj_t* g_camera_surface = nullptr;
static lv_obj_t* g_camera_sheet = nullptr;
static lv_obj_t* g_cam_live_label = nullptr;
static lv_obj_t* g_cam_meta_label = nullptr;
static lv_obj_t* g_cam_sw_mute = nullptr;
static lv_obj_t* g_cam_controls_row = nullptr;

static bool g_cam_muted = false;
static bool g_cam_playing = false;
static bool g_cam_show_spinner = false;
static lv_timer_t* g_cam_spinner_timer = nullptr;
static std::atomic<bool> g_cam_stop_worker_running{false};

static void cam_spinner_hide();
static void apply_camera_ui_state();
static void request_camera_stop_async(const char* reason);

// Show the spinner immediately; optionally auto-hide after N milliseconds.
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

// Hide the spinner and cancel any pending auto-hide timer.
static void cam_spinner_hide() {
    g_cam_show_spinner = false;
    if (g_cam_spinner) lv_obj_add_flag(g_cam_spinner, LV_OBJ_FLAG_HIDDEN);
    if (g_cam_spinner_timer) { lv_timer_del(g_cam_spinner_timer); g_cam_spinner_timer = nullptr; }
}

// Stop camera stream and local receiver on a worker thread so UI events never block.
static void request_camera_stop_async(const char* reason) {
    bool expected = false;
    if (!g_cam_stop_worker_running.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        log_line("[UI] camera stop already in progress");
        return;
    }
    if (reason) log_line(reason);

    std::thread([]() {
        baby_pi_stop_camera();
        stop_gstreamer_receiver();
        g_cam_stop_worker_running.store(false, std::memory_order_release);
        log_line("[UI] camera stop worker done");
    }).detach();
}

// Close and destroy the camera modal; stops streaming if currently playing.
static void close_camera() {
    if (g_camera_modal) {
        lv_obj_t* camera_screen = g_camera_modal;
        lv_obj_t* prev_screen = g_camera_prev_screen;

        // Stop streaming if active
        if (g_cam_playing) {
            request_camera_stop_async("[UI] camera close: stopping stream");
        }

        cam_spinner_hide();

        // Restore the previous app screen before deleting the camera screen.
        if (prev_screen) {
            lv_screen_load(prev_screen);
        }

        lv_obj_del(camera_screen);
        g_camera_modal = nullptr;
        g_camera_prev_screen = nullptr;
        g_camera_surface = nullptr;
        g_camera_sheet = nullptr;
        g_cam_img = nullptr;
        g_cam_spinner = nullptr;
        g_cam_live_label = nullptr;
        g_cam_meta_label = nullptr;
        g_cam_stats_label = nullptr;
        g_cam_sw_mute = nullptr;
        g_cam_controls_row = nullptr;
        g_cam_playing = false;
        log_line("[UI] camera closed");
    }
}

// Placeholder snapshot handler (no-op for now).
static void on_cam_snapshot(lv_event_t* /*e*/) {
    log_line("[UI] camera snapshot requested");
    // No-op for now; integrate capture once streaming is wired
}

// Start camera streaming + receiver and update UI state.
static void on_cam_play(lv_event_t* /*e*/) {
    if (g_cam_stop_worker_running.load(std::memory_order_acquire)) {
        log_line("[UI] camera play ignored: stop in progress");
        return;
    }
    g_cam_playing = true;
    log_line("[UI] camera play");
    cam_spinner_show(2000);  // Show spinner for 2 seconds while starting

    // Start GStreamer receiver first (no-op if unavailable)
    start_gstreamer_receiver();

    // Start Baby Pi camera streaming (no-op if unavailable)
    baby_pi_start_camera();

    apply_camera_ui_state();
}

// Pause camera display (does not stop the stream).
static void on_cam_pause(lv_event_t* /*e*/) {
    g_cam_playing = false;
    log_line("[UI] camera pause");
    cam_spinner_hide();
    // Note: This just pauses display, doesn't stop streaming
    apply_camera_ui_state();
}

// Stop camera streaming + receiver and update UI state.
static void on_cam_stop(lv_event_t* /*e*/) {
    g_cam_playing = false;
    log_line("[UI] camera stop requested");
    cam_spinner_hide();

    request_camera_stop_async("[UI] camera stop: background worker");

    apply_camera_ui_state();
}

// Toggle "mute" state (currently UI-only).
static void on_cam_mute_toggle(lv_event_t* e) {
    lv_obj_t* sw = (lv_obj_t*)lv_event_get_target(e);
    g_cam_muted = lv_obj_has_state(sw, LV_STATE_CHECKED);
    log_line(g_cam_muted ? "[UI] camera muted" : "[UI] camera unmuted");
    apply_camera_ui_state();
}

// Apply UI state to widgets (labels, layout, mute toggle).
static void apply_camera_ui_state() {
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
        lv_obj_set_style_text_color(g_cam_meta_label, g_cam_playing ? theme::primary_accent() : theme::text_subtle(), 0);
    }
    // Spinner visibility is controlled explicitly via cam_spinner_show/hide
    if (g_camera_sheet) {
        lv_obj_set_size(g_camera_sheet, LV_PCT(100), LV_PCT(100));
        lv_obj_center(g_camera_sheet);
    }
    if (g_cam_controls_row) {
        lv_obj_set_flex_flow(g_cam_controls_row, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(g_cam_controls_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    }
}

// Build and show the camera modal (singleton).
void build_camera_dialog(lv_obj_t* parent) {
    if (g_camera_modal) return;

    g_camera_prev_screen = lv_screen_active();

    // Dedicated full-screen camera screen (not a popup overlay).
    g_camera_modal = lv_obj_create(nullptr);
    lv_obj_remove_style_all(g_camera_modal);
    lv_obj_clear_flag(g_camera_modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(g_camera_modal, theme::surface_bg(), 0);
    lv_obj_set_style_bg_opa(g_camera_modal, LV_OPA_COVER, 0);

    // Camera "page" fills the entire display.
    lv_obj_t* sheet = lv_obj_create(g_camera_modal);
    lv_obj_set_size(sheet, LV_PCT(100), LV_PCT(100));
    lv_obj_center(sheet);
    g_camera_sheet = sheet;

    lv_obj_clear_flag(sheet, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(sheet, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(sheet, theme::surface_bg(), 0);
    lv_obj_set_style_bg_opa(sheet, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(sheet, theme::text_main(), 0);
    lv_obj_set_style_border_width(sheet, 0, 0);
    lv_obj_set_style_radius(sheet, 0, 0);
    lv_obj_set_style_pad_hor(sheet, 10, 0);
    lv_obj_set_style_pad_ver(sheet, 10, 0);
    lv_obj_set_flex_flow(sheet, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(sheet, 10, 0);

    // header row (title + actions)
    lv_obj_t* hdr_btns = nullptr;
    lv_obj_t* camera_hdr = build_header(sheet, "Camera", &hdr_btns);
    g_cam_meta_label = lv_label_create(camera_hdr);
    lv_label_set_text(g_cam_meta_label, "Idle");
    lv_obj_set_style_text_color(g_cam_meta_label, theme::text_subtle(), 0);

    // Snapshot button
    lv_obj_t* btn_snap = lv_btn_create(hdr_btns);
    style_button_tonal(btn_snap);
    set_centered_button_label(btn_snap, "Snapshot");
    lv_obj_add_event_cb(btn_snap, on_cam_snapshot, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* btn_close = lv_btn_create(hdr_btns);
    style_button_tonal(btn_close);
    set_centered_button_label(btn_close, "Close");
    lv_obj_add_event_cb(btn_close, [](lv_event_t* /*e*/){ close_camera(); }, LV_EVENT_CLICKED, nullptr);

    // video surface (placeholder for GStreamer rendering)
    g_camera_surface = lv_obj_create(sheet);
    lv_obj_set_size(g_camera_surface, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(g_camera_surface, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_camera_surface, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_camera_surface, 1, 0);
    lv_obj_set_style_border_color(g_camera_surface, theme::border(), 0);
    lv_obj_set_style_radius(g_camera_surface, 8, 0);
    lv_obj_clear_flag(g_camera_surface, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(g_camera_surface, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_grow(g_camera_surface, 1);
    // embedded image target for frames
    g_cam_img = lv_image_create(g_camera_surface);
    lv_obj_set_size(g_cam_img, LV_PCT(100), LV_PCT(100));
    lv_obj_center(g_cam_img);
    lv_obj_set_style_image_opa(g_cam_img, LV_OPA_COVER, LV_PART_MAIN);
    // LVGL v9.2.x doesn't provide LV_IMAGE_ALIGN_FIT; keep portable behavior.
    lv_image_set_inner_align(g_cam_img, LV_IMAGE_ALIGN_STRETCH);
    // LIVE/IDLE label (simple text, no chip)
    g_cam_live_label = lv_label_create(g_camera_surface);
    lv_obj_set_style_text_color(g_cam_live_label, lv_color_white(), 0);
    lv_label_set_text(g_cam_live_label, "IDLE");
    lv_obj_align(g_cam_live_label, LV_ALIGN_TOP_LEFT, 12, 12);
    // Stream stats (resolution/fps), updated from camera_rx_gst.cpp
    g_cam_stats_label = lv_label_create(g_camera_surface);
    lv_obj_set_style_text_color(g_cam_stats_label, theme::text_subtle(), 0);
    lv_label_set_text(g_cam_stats_label, "No stream");
    lv_obj_align(g_cam_stats_label, LV_ALIGN_TOP_RIGHT, -12, 12);
    // spinner
    g_cam_spinner = lv_spinner_create(g_camera_surface);
    lv_spinner_set_anim_params(g_cam_spinner, 1000, 60);
    lv_obj_set_size(g_cam_spinner, 48, 48);
    lv_obj_center(g_cam_spinner);
    lv_obj_set_style_arc_color(g_cam_spinner, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_arc_color(g_cam_spinner, theme::primary_accent(), LV_PART_INDICATOR);
    lv_obj_add_flag(g_cam_spinner, LV_OBJ_FLAG_HIDDEN);

    // controls row (moved below video)
    lv_obj_t* row_ctrls = lv_obj_create(sheet);
    g_cam_controls_row = row_ctrls;
    lv_obj_set_size(row_ctrls, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(row_ctrls, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(row_ctrls, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(row_ctrls, theme::header_bg(), 0);
    lv_obj_set_style_bg_opa(row_ctrls, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row_ctrls, 1, 0);
    lv_obj_set_style_border_color(row_ctrls, theme::border(), 0);
    lv_obj_set_style_border_side(row_ctrls, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_pad_all(row_ctrls, 8, 0);
    lv_obj_set_style_pad_row(row_ctrls, 8, 0);
    lv_obj_set_style_pad_column(row_ctrls, 10, 0);
    lv_obj_set_flex_flow(row_ctrls, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(row_ctrls, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* btn_play = lv_btn_create(row_ctrls);
    style_button_tonal_ex(btn_play, 6, theme::sp12, theme::sp8);
    set_centered_button_label(btn_play, LV_SYMBOL_PLAY " Play");
    lv_obj_add_event_cb(btn_play, on_cam_play, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* btn_pause = lv_btn_create(row_ctrls);
    style_button_tonal_ex(btn_pause, 6, theme::sp12, theme::sp8);
    set_centered_button_label(btn_pause, LV_SYMBOL_PAUSE " Pause");
    lv_obj_add_event_cb(btn_pause, on_cam_pause, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* btn_stop = lv_btn_create(row_ctrls);
    style_button_tonal_ex(btn_stop, 6, theme::sp12, theme::sp8);
    set_centered_button_label(btn_stop, LV_SYMBOL_STOP " Stop");
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

    log_line("[UI] camera opened");
    apply_camera_ui_state();
    lv_screen_load(g_camera_modal);
}

} // namespace cg::ui
