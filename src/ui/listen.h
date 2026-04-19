// src/ui/listen.h
//
// Shared "listen mode" toggle for the parent pi UI. One global state shared
// between the home-screen button and the camera-screen switch so both stay
// in sync when either is tapped. Fires HTTP calls to the baby pi on a worker
// thread and updates registered widgets on the LVGL thread via lv_async_call.
#pragma once

extern "C" {
#include "lvgl.h"
}

namespace cg::ui {

// Query the current latched state (true = streaming mic audio from baby pi).
bool listen_is_on();

// Register a widget (switch or button) whose visual state should track the
// listen flag. The widget is auto-unregistered when destroyed. Call this
// right after creating the widget on a screen; one registration per widget.
void listen_register_widget(lv_obj_t* widget);

// Toggle listen mode. Called by both the home-screen button's click handler
// and the camera-screen switch's value-changed handler. Fires the HTTP call
// on a background thread so the UI doesn't block.
void listen_toggle();

} // namespace cg::ui
