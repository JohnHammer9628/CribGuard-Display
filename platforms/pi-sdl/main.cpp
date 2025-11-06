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
  #error "LVGL SDL driver headers not found. Verify LV_USE_SDL=1 and drivers/sdl is in the include path."
#endif

// ---------- Simple logging ----------
static FILE* g_log = nullptr;
static void log_line(const char* msg) {
    if (g_log) { std::fprintf(g_log, "%s\n", msg); std::fflush(g_log); }
}

// ---------- UI globals ----------
static lv_obj_t* g_lbl_status = nullptr;  // Center "Calm/Cry/Motion"
static lv_obj_t* g_lbl_conn   = nullptr;  // Top bar
static bool g_quit            = false;

// ---------- Helpers ----------
static void set_status_text(const char* txt, lv_color_t color) {
    lv_label_set_text(g_lbl_status, txt);
    lv_obj_set_style_text_color(g_lbl_status, color, 0);
}

// ---------- Events ----------
static void on_key(lv_event_t* e) {
    auto code = (lv_key_t)(uintptr_t)lv_event_get_param(e);
    if (code == LV_KEY_ESC) g_quit = true;
}

static void on_btn_status(lv_event_t* e) {
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);            // explicit cast
    (void)btn;
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

static void on_btn_play(lv_event_t* e) {
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);            // explicit cast
    lv_obj_add_state(btn, LV_STATE_CHECKED);
    set_status_text("Playing", lv_color_hex(0x3366FF));
}

static void on_btn_stop(lv_event_t* /*e*/) {
    set_status_text("Stopped", lv_color_hex(0x444444));
}

static void on_volume(lv_event_t* e) {
    lv_obj_t* sld = (lv_obj_t*)lv_event_get_target(e);            // explicit cast
    int32_t v = lv_slider_get_value(sld);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "CribGuard • Connected • Vol %d", (int)v);
    lv_label_set_text(g_lbl_conn, buf);
}

// ---------- Build UI ----------
static void build_ui() {
    // Root screen
    lv_obj_t* screen = lv_obj_create(nullptr);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_screen_load(screen);

    // Top bar
    lv_obj_t* top = lv_obj_create(screen);
    lv_obj_set_size(top, LV_PCT(100), 60);
    lv_obj_set_style_bg_color(top, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_bg_opa(top, LV_OPA_COVER, 0);
    lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 0);

    g_lbl_conn = lv_label_create(top);
    lv_label_set_text(g_lbl_conn, "CribGuard • Connected • Vol 50");
    lv_obj_set_style_text_color(g_lbl_conn, lv_color_white(), 0);
    lv_obj_align(g_lbl_conn, LV_ALIGN_LEFT_MID, 16, 0);

    // Center status label
    g_lbl_status = lv_label_create(screen);
    lv_label_set_text(g_lbl_status, "Calm");
    lv_obj_set_style_text_font(g_lbl_status, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(g_lbl_status, lv_color_hex(0x22AA22), 0);
    lv_obj_set_style_text_align(g_lbl_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_lbl_status, LV_ALIGN_CENTER, 0, -80);

    // Status buttons row
    lv_obj_t* row1 = lv_obj_create(screen);
    lv_obj_set_size(row1, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_align(row1, LV_ALIGN_CENTER, 0, 40);
    lv_obj_set_style_pad_all(row1, 0, 0);
    lv_obj_set_style_bg_opa(row1, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row1, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(row1, 0, 0);
    lv_obj_set_style_pad_column(row1, 12, 0);

    auto make_btn = [&](const char* txt, lv_event_cb_t cb, const char* role) {
        lv_obj_t* b = lv_btn_create(row1);
        lv_obj_set_size(b, 140, 60);
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
    lv_obj_align(row2, LV_ALIGN_CENTER, 0, 120);
    lv_obj_set_style_bg_opa(row2, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(row2, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row2, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row2, 16, 0);

    lv_obj_t* btnPlay = lv_btn_create(row2);
    lv_obj_set_size(btnPlay, 120, 60);
    lv_obj_t* lblPlay = lv_label_create(btnPlay);
    lv_label_set_text(lblPlay, "Play");
    lv_obj_center(lblPlay);
    lv_obj_add_event_cb(btnPlay, on_btn_play, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* btnStop = lv_btn_create(row2);
    lv_obj_set_size(btnStop, 120, 60);
    lv_obj_t* lblStop = lv_label_create(btnStop);
    lv_label_set_text(lblStop, "Stop");
    lv_obj_center(lblStop);
    lv_obj_add_event_cb(btnStop, on_btn_stop, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* slider = lv_slider_create(row2);
    lv_obj_set_size(slider, 280, 10);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, 50, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, on_volume, LV_EVENT_VALUE_CHANGED, nullptr);

    // Add ESC handler
    lv_obj_add_event_cb(screen, on_key, LV_EVENT_KEY, nullptr);
}

// ---------- Main ----------
int main() {
    g_log = std::fopen("sim.log", "w");

    lv_init();
    log_line("[SIM] lv_init ok");

    // Portrait to match your panel
    constexpr int SCR_W = 720;
    constexpr int SCR_H = 1280;

    lv_display_t* disp = lv_sdl_window_create(SCR_W, SCR_H);
    if (!disp) {
        log_line("[SIM] ERROR: lv_sdl_window_create failed");
        if (g_log) std::fclose(g_log);
        return 1;
    }
    log_line("[SIM] SDL window created 720x1280");

#if HAVE_SDL_MOUSE
    (void)lv_sdl_mouse_create();
#endif
#if HAVE_SDL_KBD
    (void)lv_sdl_keyboard_create();
#endif
#if HAVE_SDL_WHEEL
    (void)lv_sdl_mousewheel_create();
#endif
    log_line("[SIM] SDL input attached");

    build_ui();
    log_line("[SIM] UI built; entering loop");

    while (!g_quit) {
        lv_timer_handler();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    log_line("[SIM] loop exit");
    if (g_log) std::fclose(g_log);
    return 0;
}
