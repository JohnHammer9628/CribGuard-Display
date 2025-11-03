#include "lvgl.h"
extern "C" { #include "sdl.h" }
#include <chrono>
#include <thread>

static void hal_init() {
    sdl_init();
    lv_display_t* disp = lv_sdl_window_create(800, 480);
    (void)disp;
    lv_indev_t* mouse = lv_sdl_mouse_create();
    (void)mouse;
}

int main() {
    lv_init();
    hal_init();
    lv_obj_t* label = lv_label_create(lv_screen_active());
    lv_label_set_text(label, "Crib-Guard UI (Pi SDL bootstrap)");
    lv_obj_center(label);
    while (true) {
        lv_timer_handler();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return 0;
}