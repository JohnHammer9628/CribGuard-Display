#include "lvgl.h"
extern "C" {
#include "sdl.h"
}
#include <chrono>
#include <thread>
#include "ui/ui.h"

static void hal_init() {
    sdl_init();
    // 7” Touch Display 2 native res: 720x1280 (portrait)
    lv_display_t* disp = lv_sdl_window_create(720, 1280);

    // If you built your UI for landscape and want to rotate temporarily:
    // lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90);

    // Pointer input (mouse acts like single-touch on Windows)
    lv_indev_t* mouse = lv_sdl_mouse_create();
    (void)mouse;
}

int main() {
    lv_init();
    hal_init();

    ui_build();   // your LVGL object tree (we’ll port from ESP32-S3)
    ui_start();   // start timers/sim

    while (true) {
        lv_timer_handler();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return 0;
}
