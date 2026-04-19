// src/ui/listen.cpp
//
// See listen.h for the design overview. This file owns the single source of
// truth for the listen-mode flag, a list of widgets that mirror it, and the
// worker thread that calls the baby pi's /api/listen/start|stop endpoints.
#include "ui/listen.h"

#include "baby_pi_api.h"
#include "logging.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace cg::ui {

namespace {

std::atomic<bool> g_listen_on{false};
std::atomic<bool> g_request_inflight{false};

// Registered widgets + mutex. All mutations happen on the LVGL thread (the
// only thread that touches LVGL objects), but we still lock for safety when
// async callbacks iterate the list to update visuals.
std::mutex g_widgets_mtx;
std::vector<lv_obj_t*> g_widgets;

// Remove a widget from our tracking list when LVGL deletes it so we never
// touch a dangling pointer. Wired via LV_EVENT_DELETE on each registered
// widget.
void on_widget_deleted(lv_event_t* e) {
    lv_obj_t* w = static_cast<lv_obj_t*>(lv_event_get_target(e));
    std::lock_guard<std::mutex> lock(g_widgets_mtx);
    for (auto it = g_widgets.begin(); it != g_widgets.end(); ) {
        if (*it == w) it = g_widgets.erase(it);
        else ++it;
    }
}

// Push the current `g_listen_on` value to every registered widget. Runs on
// the LVGL thread (the async callback drops us here).
void apply_state_to_widgets() {
    const bool on = g_listen_on.load(std::memory_order_acquire);
    std::lock_guard<std::mutex> lock(g_widgets_mtx);
    for (lv_obj_t* w : g_widgets) {
        if (!w) continue;
        if (on) lv_obj_add_state(w, LV_STATE_CHECKED);
        else    lv_obj_clear_state(w, LV_STATE_CHECKED);
    }
}

// Worker thread body: issue the HTTP call for the desired state, then push
// the final confirmed state back onto the LVGL thread. If the call fails we
// revert the optimistic flip.
void http_worker(bool want_on) {
    bool ok = want_on ? baby_pi_listen_start() : baby_pi_listen_stop();
    if (!ok) {
        g_listen_on.store(!want_on, std::memory_order_release);
        log_line(want_on ? "[UI] listen start failed, reverting"
                         : "[UI] listen stop failed, reverting");
    }
    lv_async_call([](void*){
        apply_state_to_widgets();
        g_request_inflight.store(false, std::memory_order_release);
    }, nullptr);
}

} // namespace

bool listen_is_on() {
    return g_listen_on.load(std::memory_order_acquire);
}

void listen_register_widget(lv_obj_t* widget) {
    if (!widget) return;
    {
        std::lock_guard<std::mutex> lock(g_widgets_mtx);
        g_widgets.push_back(widget);
    }
    // Keep visuals consistent with current state right away.
    if (g_listen_on.load(std::memory_order_acquire)) {
        lv_obj_add_state(widget, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(widget, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(widget, on_widget_deleted, LV_EVENT_DELETE, nullptr);
}

void listen_toggle() {
    bool expected = false;
    if (!g_request_inflight.compare_exchange_strong(expected, true,
            std::memory_order_acq_rel)) {
        log_line("[UI] listen toggle ignored (request in flight)");
        return;
    }
    const bool want_on = !g_listen_on.load(std::memory_order_acquire);
    g_listen_on.store(want_on, std::memory_order_release);
    apply_state_to_widgets();
    log_line(want_on ? "[UI] listen -> on" : "[UI] listen -> off");
    std::thread(http_worker, want_on).detach();
}

} // namespace cg::ui
