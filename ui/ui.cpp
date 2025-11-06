// ui/ui.cpp
#include "lvgl.h"
#include <cstdio>

// ---------- Forward declarations ----------
void ui_build();
void ui_start();

// ---------- File-local UI state ----------
static lv_obj_t* root              = nullptr;
static lv_obj_t* title_label       = nullptr;
static lv_obj_t* vol_slider        = nullptr;
static lv_obj_t* vol_value_label   = nullptr;

// Helper: cast LVGL user_data (void*) back to an lv_obj_t*
static inline lv_obj_t* as_obj(void* p) {
    return static_cast<lv_obj_t*>(p);
}

// Slider value-changed handler (LVGL v9)
static void on_vol_changed(lv_event_t* e) {
    // Event target is the slider itself
    lv_obj_t* target = lv_event_get_target(e);
    int value = lv_slider_get_value(target);

    // Show value on the label
    if (vol_value_label) {
        lv_label_set_text_fmt(vol_value_label, "Volume: %d", value);
    }
}

// Build a very simple UI so we can prove the sim + input work
void ui_build() {
    // Use the active screen as our root in LVGL v9
    lv_obj_t* scr = lv_screen_active();
    root = lv_obj_create(scr);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(root, 16, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // Title
    title_label = lv_label_create(root);
    lv_label_set_text(title_label, "CribGuard — LVGL Sim");

    // Slider + value label
    vol_slider = lv_slider_create(root);
    lv_obj_set_width(vol_slider, LV_PCT(90));
    lv_slider_set_range(vol_slider, 0, 100);
    lv_slider_set_value(vol_slider, 50, LV_ANIM_OFF);

    vol_value_label = lv_label_create(root);
    lv_label_set_text(vol_value_label, "Volume: 50");

    // Hook up the event (no user_data needed here, but if you use it later, cast with as_obj(...))
    lv_obj_add_event_cb(vol_slider, on_vol_changed, LV_EVENT_VALUE_CHANGED, nullptr);
}

void ui_start() {
    // Nothing async needed yet. If you add timers or tasks, start them here.
    // Example of a timer tick to force an initial label update:
    if (vol_slider) {
        int value = lv_slider_get_value(vol_slider);
        if (vol_value_label) lv_label_set_text_fmt(vol_value_label, "Volume: %d", value);
    }
}
