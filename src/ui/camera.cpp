// src/ui/camera.cpp
//
// Camera modal:
// - Ensures the Baby Pi camera stream (HTTP) and local UDP receiver (GStreamer) stay running
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
#include "ui/listen.h"

#include <atomic>
#include <cstring>
#include <string>
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

static lv_timer_t* g_cam_spinner_timer = nullptr;
static std::atomic<bool> g_cam_start_worker_running{false};

// Wet-state notification
static lv_obj_t* g_cam_wet_banner = nullptr;
static lv_timer_t* g_cam_wet_timer = nullptr;
static std::atomic<bool> g_cam_wet_fetch_inflight{false};
static std::atomic<bool> g_cam_wet_shutdown{false};
static std::string g_cam_wet_last_state = "none";

struct WetFetchResult {
    bool ok;
    std::string state;
};

// Show/hide the banner based on latest state. Must run on the LVGL thread.
static void apply_wet_banner_state(const char* state) {
    if (!g_cam_wet_banner) return;
    bool is_wet = state && std::strcmp(state, "none") != 0;
    if (!is_wet) {
        lv_obj_add_flag(g_cam_wet_banner, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    const char* text;
    lv_color_t bg;
    if (std::strcmp(state, "cold") == 0) {
        text = LV_SYMBOL_WARNING "  Wetness detected (cold)";
        bg = lv_color_hex(0x1E88E5);
    } else {
        text = LV_SYMBOL_WARNING "  Wetness detected (warm)";
        bg = lv_color_hex(0xE53935);
    }
    lv_label_set_text(g_cam_wet_banner, text);
    lv_obj_set_style_bg_color(g_cam_wet_banner, bg, 0);
    lv_obj_clear_flag(g_cam_wet_banner, LV_OBJ_FLAG_HIDDEN);
}

// Runs on the LVGL thread via lv_async_call after the HTTP worker finishes.
static void wet_apply_async(void* param) {
    WetFetchResult* r = static_cast<WetFetchResult*>(param);
    if (!g_cam_wet_shutdown.load(std::memory_order_acquire) && r->ok) {
        if (r->state != g_cam_wet_last_state) {
            log_line((std::string("[WET] state: ") + g_cam_wet_last_state + " -> " + r->state).c_str());
            g_cam_wet_last_state = r->state;
        }
        apply_wet_banner_state(r->state.c_str());
    }
    delete r;
    g_cam_wet_fetch_inflight.store(false, std::memory_order_release);
}

// lv_timer callback: kick off one HTTP fetch if none is in flight.
static void wet_poll_cb(lv_timer_t* /*t*/) {
    if (g_cam_wet_shutdown.load(std::memory_order_acquire)) return;
    bool expected = false;
    if (!g_cam_wet_fetch_inflight.compare_exchange_strong(expected, true,
            std::memory_order_acq_rel)) {
        return;
    }
    std::thread([]() {
        std::string st = "none";
        bool ok = baby_pi_get_wet_status(st);
        lv_async_call(wet_apply_async, new WetFetchResult{ok, std::move(st)});
    }).detach();
}

static void apply_camera_ui_state();
static void request_camera_start_async(const char* reason);

// Show the spinner immediately; optionally auto-hide after N milliseconds.
static void cam_spinner_show(uint32_t auto_hide_ms) {
    if (g_cam_spinner) lv_obj_clear_flag(g_cam_spinner, LV_OBJ_FLAG_HIDDEN);
    if (g_cam_spinner_timer) { lv_timer_del(g_cam_spinner_timer); g_cam_spinner_timer = nullptr; }
    if (auto_hide_ms > 0) {
        g_cam_spinner_timer = lv_timer_create([](lv_timer_t* t){
            if (g_cam_spinner) lv_obj_add_flag(g_cam_spinner, LV_OBJ_FLAG_HIDDEN);
            lv_timer_del(t);
            g_cam_spinner_timer = nullptr;
        }, auto_hide_ms, nullptr);
        lv_timer_set_repeat_count(g_cam_spinner_timer, 1);
    }
}

// Ensure camera stream + receiver are running.
static void request_camera_start_async(const char* reason) {
    // Receiver start is idempotent.
    start_gstreamer_receiver();

    // Parent->Baby start call can block up to HTTP timeout; run off UI thread.
    bool expected = false;
    if (!g_cam_start_worker_running.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        log_line("[UI] camera start already in progress");
        return;
    }
    if (reason) log_line(reason);

    std::thread([]() {
        baby_pi_start_camera();
        g_cam_start_worker_running.store(false, std::memory_order_release);
        log_line("[UI] camera start worker done");
    }).detach();
}

// Close and destroy the camera modal.
static void close_camera() {
    if (g_camera_modal) {
        lv_obj_t* camera_screen = g_camera_modal;
        lv_obj_t* prev_screen = g_camera_prev_screen;

        // Signal the wet-state worker to drop any pending UI update and stop the timer.
        g_cam_wet_shutdown.store(true, std::memory_order_release);
        if (g_cam_wet_timer) { lv_timer_del(g_cam_wet_timer); g_cam_wet_timer = nullptr; }

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
        g_cam_wet_banner = nullptr;
        g_cam_wet_last_state = "none";
        log_line("[UI] camera closed");
    }
}

// Placeholder snapshot handler (no-op for now).
static void on_cam_snapshot(lv_event_t* /*e*/) {
    log_line("[UI] camera snapshot requested");
    // No-op for now; integrate capture once streaming is wired
}

// Fires when the user taps the Listen switch on the camera screen. We
// delegate to the shared listen module so the home-screen button stays in
// sync. The widget's visual state is driven by the shared module, so we
// don't toggle the switch ourselves here.
static void on_cam_listen_toggle(lv_event_t* /*e*/) {
    listen_toggle();
}

// Apply UI state to widgets (labels, layout). The listen-mode switch is
// driven by the shared listen module, so we don't touch its checked state
// here.
static void apply_camera_ui_state() {
    // Always-on showcase mode uses LIVE state.
    if (g_cam_live_label) {
        lv_label_set_text(g_cam_live_label, "LIVE");
        lv_obj_set_style_text_color(g_cam_live_label, lv_color_hex(0xFF6B6B), 0);
    }
    if (g_cam_meta_label) {
        lv_label_set_text(g_cam_meta_label, "Live");
        lv_obj_set_style_text_color(g_cam_meta_label, theme::primary_accent(), 0);
    }
    // Spinner visibility is controlled explicitly via cam_spinner_show.
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
    lv_label_set_text(g_cam_meta_label, "Live");
    lv_obj_set_style_text_color(g_cam_meta_label, theme::primary_accent(), 0);

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
    // LIVE label (simple text, no chip)
    g_cam_live_label = lv_label_create(g_camera_surface);
    lv_obj_set_style_text_color(g_cam_live_label, lv_color_white(), 0);
    lv_label_set_text(g_cam_live_label, "LIVE");
    lv_obj_align(g_cam_live_label, LV_ALIGN_TOP_LEFT, 12, 12);
    // Stream stats (resolution/fps), updated from camera_rx_gst.cpp
    g_cam_stats_label = lv_label_create(g_camera_surface);
    lv_obj_set_style_text_color(g_cam_stats_label, theme::text_subtle(), 0);
    lv_label_set_text(g_cam_stats_label, "Waiting for stream...");
    lv_obj_align(g_cam_stats_label, LV_ALIGN_TOP_RIGHT, -12, 12);

    // Wet-state banner (hidden until a wet_alert is observed).
    g_cam_wet_banner = lv_label_create(g_camera_surface);
    lv_label_set_long_mode(g_cam_wet_banner, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_cam_wet_banner, LV_PCT(80));
    lv_obj_set_style_text_align(g_cam_wet_banner, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(g_cam_wet_banner, lv_color_white(), 0);
    lv_obj_set_style_bg_color(g_cam_wet_banner, lv_color_hex(0xE53935), 0);
    lv_obj_set_style_bg_opa(g_cam_wet_banner, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g_cam_wet_banner, 8, 0);
    lv_obj_set_style_pad_hor(g_cam_wet_banner, 14, 0);
    lv_obj_set_style_pad_ver(g_cam_wet_banner, 8, 0);
    lv_label_set_text(g_cam_wet_banner, LV_SYMBOL_WARNING "  Wetness detected");
    lv_obj_align(g_cam_wet_banner, LV_ALIGN_TOP_MID, 0, 44);
    lv_obj_add_flag(g_cam_wet_banner, LV_OBJ_FLAG_HIDDEN);
    // spinner
    g_cam_spinner = lv_spinner_create(g_camera_surface);
    lv_spinner_set_anim_params(g_cam_spinner, 1000, 60);
    lv_obj_set_size(g_cam_spinner, 48, 48);
    lv_obj_center(g_cam_spinner);
    lv_obj_set_style_arc_color(g_cam_spinner, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_arc_color(g_cam_spinner, theme::primary_accent(), LV_PART_INDICATOR);

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

    // Listen toggle (was the "Mute" switch before — repurposed since the old
    // mute widget was a UI-only placeholder). Styled as a checkable pill to
    // match the home-screen Listen button and give the toggle a visible text
    // identity instead of a bare switch. State is shared with the home
    // screen via the listen module.
    g_cam_sw_mute = lv_btn_create(row_ctrls);
    style_button_pill(g_cam_sw_mute);
    lv_obj_add_flag(g_cam_sw_mute, LV_OBJ_FLAG_CHECKABLE);
    set_centered_button_label(g_cam_sw_mute, LV_SYMBOL_AUDIO " Listen");
    lv_obj_add_event_cb(g_cam_sw_mute, on_cam_listen_toggle, LV_EVENT_CLICKED, nullptr);
    listen_register_widget(g_cam_sw_mute);

    log_line("[UI] camera opened");
    apply_camera_ui_state();
    cam_spinner_show(0);
    request_camera_start_async("[UI] camera opened: ensuring stream live");

    // Start the wet-state polling timer.
    g_cam_wet_shutdown.store(false, std::memory_order_release);
    g_cam_wet_last_state = "none";
    if (g_cam_wet_timer) { lv_timer_del(g_cam_wet_timer); g_cam_wet_timer = nullptr; }
    g_cam_wet_timer = lv_timer_create(wet_poll_cb, 1000, nullptr);
    lv_timer_ready(g_cam_wet_timer);

    lv_screen_load(g_camera_modal);
}

void start_camera_always_on() {
    request_camera_start_async("[UI] camera always-on: start requested");
}

} // namespace cg::ui
