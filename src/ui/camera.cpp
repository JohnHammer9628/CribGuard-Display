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

namespace cg::ui {

static lv_obj_t* g_camera_modal = nullptr;
static lv_obj_t* g_camera_prev_screen = nullptr;
static lv_obj_t* g_camera_surface = nullptr;
static lv_obj_t* g_camera_sheet = nullptr;
static lv_obj_t* g_cam_live_label = nullptr;
static lv_obj_t* g_cam_meta_label = nullptr;
static lv_obj_t* g_cam_sw_mute = nullptr;
static lv_obj_t* g_cam_controls_row = nullptr;

static std::atomic<bool> g_cam_start_worker_running{false};

// Wet-state notification
static lv_obj_t* g_cam_wet_banner = nullptr;
static lv_timer_t* g_cam_wet_timer = nullptr;
static std::atomic<bool> g_cam_wet_fetch_inflight{false};
static std::atomic<bool> g_cam_wet_shutdown{false};
static std::string g_cam_wet_last_state = "none";

// ROI editor — sliders + a translucent rectangle drawn over the camera image
// so the user can drag-to-set the wetness ROI. Coordinates are stored in
// 160x120 frame space; the overlay rect maps them onto whatever pixel size
// the camera image is rendered at.
static constexpr int CAM_FRAME_W = 160;
static constexpr int CAM_FRAME_H = 120;
static lv_obj_t* g_roi_overlay = nullptr;     // child of g_camera_surface, hidden by default
static lv_obj_t* g_roi_edit_panel = nullptr;  // sliders + Save/Cancel; sibling of g_cam_controls_row
static lv_obj_t* g_roi_set_btn = nullptr;     // "Set ROI" button in the controls row
static lv_obj_t* g_roi_x_start_slider = nullptr;
static lv_obj_t* g_roi_y_start_slider = nullptr;
static lv_obj_t* g_roi_x_end_slider = nullptr;
static lv_obj_t* g_roi_y_end_slider = nullptr;
static lv_obj_t* g_roi_size_label = nullptr;
static int g_roi_x_start = 80;
static int g_roi_y_start = 0;
static int g_roi_x_end   = CAM_FRAME_W;
static int g_roi_y_end   = CAM_FRAME_H;
static bool g_roi_edit_mode = false;

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

// Clamp the staged ROI to frame bounds and ensure it's non-empty.
static void roi_clamp() {
    if (g_roi_x_start < 0) g_roi_x_start = 0;
    if (g_roi_y_start < 0) g_roi_y_start = 0;
    if (g_roi_x_end > CAM_FRAME_W) g_roi_x_end = CAM_FRAME_W;
    if (g_roi_y_end > CAM_FRAME_H) g_roi_y_end = CAM_FRAME_H;
    if (g_roi_x_start > CAM_FRAME_W - 1) g_roi_x_start = CAM_FRAME_W - 1;
    if (g_roi_y_start > CAM_FRAME_H - 1) g_roi_y_start = CAM_FRAME_H - 1;
    if (g_roi_x_end <= g_roi_x_start) g_roi_x_end = g_roi_x_start + 1;
    if (g_roi_y_end <= g_roi_y_start) g_roi_y_end = g_roi_y_start + 1;
}

// Reposition the overlay rectangle on the camera image based on the staged
// ROI coords. Maps frame-space pixels onto the image's actual rendered size.
static void roi_overlay_refresh() {
    if (!g_roi_overlay || !g_cam_img) return;
    lv_coord_t img_w = lv_obj_get_width(g_cam_img);
    lv_coord_t img_h = lv_obj_get_height(g_cam_img);
    if (img_w <= 0 || img_h <= 0) return;
    int rx = (g_roi_x_start * img_w) / CAM_FRAME_W;
    int ry = (g_roi_y_start * img_h) / CAM_FRAME_H;
    int rw = ((g_roi_x_end - g_roi_x_start) * img_w) / CAM_FRAME_W;
    int rh = ((g_roi_y_end - g_roi_y_start) * img_h) / CAM_FRAME_H;
    lv_obj_set_pos(g_roi_overlay, rx, ry);
    lv_obj_set_size(g_roi_overlay, rw, rh);
    if (g_roi_size_label) {
        lv_label_set_text_fmt(g_roi_size_label, "ROI: %d,%d  %dx%d",
                              g_roi_x_start, g_roi_y_start,
                              g_roi_x_end - g_roi_x_start, g_roi_y_end - g_roi_y_start);
    }
}

// Push the staged coords into the four sliders without firing their value-
// changed callbacks (we just edited the underlying state ourselves).
static void roi_sync_sliders_from_state() {
    if (g_roi_x_start_slider) lv_slider_set_value(g_roi_x_start_slider, g_roi_x_start, LV_ANIM_OFF);
    if (g_roi_y_start_slider) lv_slider_set_value(g_roi_y_start_slider, g_roi_y_start, LV_ANIM_OFF);
    if (g_roi_x_end_slider)   lv_slider_set_value(g_roi_x_end_slider,   g_roi_x_end,   LV_ANIM_OFF);
    if (g_roi_y_end_slider)   lv_slider_set_value(g_roi_y_end_slider,   g_roi_y_end,   LV_ANIM_OFF);
}

// Slider VALUE_CHANGED handler. user_data is a pointer to one of the four
// staged ints (g_roi_x_start etc.).
static void on_roi_slider_changed(lv_event_t* e) {
    int* target = static_cast<int*>(lv_event_get_user_data(e));
    lv_obj_t* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (!target || !slider) return;
    *target = lv_slider_get_value(slider);
    int prev_xs = g_roi_x_start, prev_ys = g_roi_y_start;
    int prev_xe = g_roi_x_end,   prev_ye = g_roi_y_end;
    roi_clamp();
    // If clamping moved a value, push it back to the corresponding slider so
    // the thumb visually snaps to the legal range.
    if (g_roi_x_start != prev_xs || g_roi_y_start != prev_ys ||
        g_roi_x_end   != prev_xe || g_roi_y_end   != prev_ye) {
        roi_sync_sliders_from_state();
    }
    roi_overlay_refresh();
}

// Result of a background fetch of the current ROI from the Baby Pi.
struct RoiFetchResult {
    bool ok;
    int x_start, y_start, x_end, y_end;
};

// Runs on the LVGL thread after baby_pi_get_wet_roi returns. Applies the
// fetched coords to the staged state and reveals the editor.
static void roi_enter_apply_async(void* param) {
    RoiFetchResult* r = static_cast<RoiFetchResult*>(param);
    if (r->ok) {
        g_roi_x_start = r->x_start;
        g_roi_y_start = r->y_start;
        g_roi_x_end   = r->x_end;
        g_roi_y_end   = r->y_end;
        roi_clamp();
    }
    delete r;
    if (g_roi_overlay) lv_obj_clear_flag(g_roi_overlay, LV_OBJ_FLAG_HIDDEN);
    if (g_roi_edit_panel) lv_obj_clear_flag(g_roi_edit_panel, LV_OBJ_FLAG_HIDDEN);
    if (g_cam_controls_row) lv_obj_add_flag(g_cam_controls_row, LV_OBJ_FLAG_HIDDEN);
    roi_sync_sliders_from_state();
    roi_overlay_refresh();
}

static void roi_enter_edit() {
    if (g_roi_edit_mode) return;
    g_roi_edit_mode = true;
    log_line("[ROI] entering edit mode");
    // Fetch current ROI from the Baby Pi off the UI thread, then apply.
    std::thread([]() {
        int xs = 80, ys = 0, xe = CAM_FRAME_W, ye = CAM_FRAME_H;
        bool ok = baby_pi_get_wet_roi(xs, ys, xe, ye);
        lv_async_call(roi_enter_apply_async, new RoiFetchResult{ok, xs, ys, xe, ye});
    }).detach();
}

static void roi_exit_edit() {
    if (!g_roi_edit_mode) return;
    g_roi_edit_mode = false;
    if (g_roi_overlay) lv_obj_add_flag(g_roi_overlay, LV_OBJ_FLAG_HIDDEN);
    if (g_roi_edit_panel) lv_obj_add_flag(g_roi_edit_panel, LV_OBJ_FLAG_HIDDEN);
    if (g_cam_controls_row) lv_obj_clear_flag(g_cam_controls_row, LV_OBJ_FLAG_HIDDEN);
    log_line("[ROI] exited edit mode");
}

static void on_roi_save_clicked(lv_event_t* /*e*/) {
    int xs = g_roi_x_start, ys = g_roi_y_start, xe = g_roi_x_end, ye = g_roi_y_end;
    log_line((std::string("[ROI] save: ") +
              std::to_string(xs) + "," + std::to_string(ys) + " -> " +
              std::to_string(xe) + "," + std::to_string(ye)).c_str());
    std::thread([xs, ys, xe, ye]() {
        baby_pi_set_wet_roi(xs, ys, xe, ye);
    }).detach();
    roi_exit_edit();
}

static void on_roi_cancel_clicked(lv_event_t* /*e*/) {
    log_line("[ROI] cancel");
    roi_exit_edit();
}

static void on_roi_set_clicked(lv_event_t* /*e*/) {
    roi_enter_edit();
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
        g_cam_live_label = nullptr;
        g_cam_meta_label = nullptr;
        g_cam_stats_label = nullptr;
        g_cam_sw_mute = nullptr;
        g_cam_controls_row = nullptr;
        g_cam_wet_banner = nullptr;
        g_cam_wet_last_state = "none";
        g_roi_overlay = nullptr;
        g_roi_edit_panel = nullptr;
        g_roi_set_btn = nullptr;
        g_roi_x_start_slider = nullptr;
        g_roi_y_start_slider = nullptr;
        g_roi_x_end_slider = nullptr;
        g_roi_y_end_slider = nullptr;
        g_roi_size_label = nullptr;
        g_roi_edit_mode = false;
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

    // ROI overlay rectangle: drawn on top of the camera image while editing.
    // Hidden by default; geometry is set by roi_overlay_refresh() once we
    // know the rendered image size.
    g_roi_overlay = lv_obj_create(g_camera_surface);
    lv_obj_remove_style_all(g_roi_overlay);
    lv_obj_set_style_border_color(g_roi_overlay, lv_color_hex(0xFFEB3B), 0);
    lv_obj_set_style_border_width(g_roi_overlay, 2, 0);
    lv_obj_set_style_border_opa(g_roi_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(g_roi_overlay, lv_color_hex(0xFFEB3B), 0);
    lv_obj_set_style_bg_opa(g_roi_overlay, LV_OPA_20, 0);
    lv_obj_set_style_radius(g_roi_overlay, 0, 0);
    lv_obj_clear_flag(g_roi_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(g_roi_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_roi_overlay, LV_OBJ_FLAG_HIDDEN);

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

    g_roi_set_btn = lv_btn_create(row_ctrls);
    style_button_pill(g_roi_set_btn);
    set_centered_button_label(g_roi_set_btn, LV_SYMBOL_EDIT " Set ROI");
    lv_obj_add_event_cb(g_roi_set_btn, on_roi_set_clicked, LV_EVENT_CLICKED, nullptr);

    // ROI edit panel: hidden by default. Replaces the controls row visually
    // while editing. Four sliders (one per edge) drive the staged coords;
    // overlay updates live as the user drags. Save POSTs; Cancel discards.
    g_roi_edit_panel = lv_obj_create(sheet);
    lv_obj_set_size(g_roi_edit_panel, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(g_roi_edit_panel, theme::header_bg(), 0);
    lv_obj_set_style_bg_opa(g_roi_edit_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_roi_edit_panel, 1, 0);
    lv_obj_set_style_border_color(g_roi_edit_panel, theme::border(), 0);
    lv_obj_set_style_border_side(g_roi_edit_panel, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_pad_all(g_roi_edit_panel, 8, 0);
    lv_obj_set_style_pad_row(g_roi_edit_panel, 6, 0);
    lv_obj_set_flex_flow(g_roi_edit_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(g_roi_edit_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_roi_edit_panel, LV_OBJ_FLAG_HIDDEN);

    auto add_slider_row = [&](const char* label_text, int min, int max, int* target) -> lv_obj_t* {
        lv_obj_t* row = lv_obj_create(g_roi_edit_panel);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 8, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* lbl = lv_label_create(row);
        lv_label_set_text(lbl, label_text);
        lv_obj_set_width(lbl, 64);

        lv_obj_t* slider = lv_slider_create(row);
        lv_obj_set_flex_grow(slider, 1);
        lv_slider_set_range(slider, min, max);
        lv_slider_set_value(slider, *target, LV_ANIM_OFF);
        lv_obj_add_event_cb(slider, on_roi_slider_changed, LV_EVENT_VALUE_CHANGED, target);
        return slider;
    };

    g_roi_x_start_slider = add_slider_row("X start",  0, CAM_FRAME_W,     &g_roi_x_start);
    g_roi_y_start_slider = add_slider_row("Y start",  0, CAM_FRAME_H,     &g_roi_y_start);
    g_roi_x_end_slider   = add_slider_row("X end",    1, CAM_FRAME_W,     &g_roi_x_end);
    g_roi_y_end_slider   = add_slider_row("Y end",    1, CAM_FRAME_H,     &g_roi_y_end);

    // Bottom row: size readout + Save/Cancel buttons.
    lv_obj_t* roi_btns_row = lv_obj_create(g_roi_edit_panel);
    lv_obj_remove_style_all(roi_btns_row);
    lv_obj_set_size(roi_btns_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(roi_btns_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(roi_btns_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(roi_btns_row, 10, 0);
    lv_obj_clear_flag(roi_btns_row, LV_OBJ_FLAG_SCROLLABLE);

    g_roi_size_label = lv_label_create(roi_btns_row);
    lv_obj_set_style_text_color(g_roi_size_label, theme::text_subtle(), 0);
    lv_label_set_text(g_roi_size_label, "ROI: 80,0  80x120");

    lv_obj_t* save_btn = lv_btn_create(roi_btns_row);
    style_button_tonal(save_btn);
    set_centered_button_label(save_btn, LV_SYMBOL_OK " Save");
    lv_obj_add_event_cb(save_btn, on_roi_save_clicked, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* cancel_btn = lv_btn_create(roi_btns_row);
    style_button_tonal(cancel_btn);
    set_centered_button_label(cancel_btn, LV_SYMBOL_CLOSE " Cancel");
    lv_obj_add_event_cb(cancel_btn, on_roi_cancel_clicked, LV_EVENT_CLICKED, nullptr);

    log_line("[UI] camera opened");
    apply_camera_ui_state();
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
