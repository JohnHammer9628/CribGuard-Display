// platforms/pi-sdl/main.cpp
//
// CribGuard (Parent Pi) - Windows/Linux simulator entrypoint.
//
// Responsibilities of this file:
// - Initialize logging (`sim.log`) and load runtime config (`settings.cfg`)
// - Bring up LVGL + the LVGL SDL driver (window + input devices)
// - Call `build_ui()` to construct the UI (implemented in `src/ui/*`)
// - Run the LVGL tick/handler loop until `g_quit` is set (signal handler)
//
// Notes:
// - The real Raspberry Pi build may use a different platform entrypoint; this file
//   is intentionally "thin" so UI changes are isolated to `src/ui/*`.
// - Window size is fixed to 1280x720 to avoid fullscreen/maximize surprises.
#define _CRT_SECURE_NO_WARNINGS

#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <csignal>

extern "C" {
  #include "lvgl.h"
}

// SDL header (used by LVGL SDL driver; we keep the include flexible)
#if __has_include("SDL.h")
  #include "SDL.h"
  #define HAVE_SDL2_HEADER 1
#elif __has_include("SDL2/SDL.h")
  #include "SDL2/SDL.h"
  #define HAVE_SDL2_HEADER 1
#else
  #define HAVE_SDL2_HEADER 0
#endif

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
  #error "LVGL SDL driver headers not found. Ensure LV_USE_SDL=1 in lv_conf.h and drivers are in include path."
#endif

#include "camera_rx_gst.h"
#include "config.h"
#include "logging.h"
#include "ui_app.h"

bool g_quit = false;

// Signal handler used by the simulator (Ctrl+C / SIGTERM).
// Sets the global quit flag so the main loop exits cleanly.
void handle_shutdown_signal(int /*sig*/) {
	log_line("[SIM] signal received, initiating graceful shutdown");
	if (g_log_file) std::fflush(g_log_file);
	g_quit = true;
}

// Log startup diagnostics that are useful when bringing up Pi 5 kiosk mode.
static void log_startup_diagnostics(int req_w, int req_h) {
    char buf[256];
    const char* env_driver = std::getenv("SDL_VIDEODRIVER");
    const char* env_rotate = std::getenv("SDL_VIDEO_KMSDRM_ROTATION");
    std::snprintf(buf, sizeof(buf),
        "[SIM] env SDL_VIDEODRIVER=%s SDL_VIDEO_KMSDRM_ROTATION=%s",
        env_driver ? env_driver : "(unset)",
        env_rotate ? env_rotate : "(unset)");
    log_line(buf);

    std::snprintf(buf, sizeof(buf), "[SIM] requested render size=%dx%d", req_w, req_h);
    log_line(buf);

#if HAVE_SDL2_HEADER
    const char* active_driver = SDL_GetCurrentVideoDriver();
    std::snprintf(buf, sizeof(buf), "[SIM] SDL active video driver=%s",
        active_driver ? active_driver : "(null)");
    log_line(buf);

    int displays = SDL_GetNumVideoDisplays();
    if (displays < 0) {
        std::snprintf(buf, sizeof(buf), "[SIM] WARNING: SDL_GetNumVideoDisplays failed: %s", SDL_GetError());
        log_line(buf);
        return;
    }

    std::snprintf(buf, sizeof(buf), "[SIM] SDL display count=%d", displays);
    log_line(buf);

    SDL_DisplayMode mode{};
    if (SDL_GetCurrentDisplayMode(0, &mode) == 0) {
        std::snprintf(buf, sizeof(buf), "[SIM] SDL display0 mode=%dx%d@%dHz", mode.w, mode.h, mode.refresh_rate);
        log_line(buf);
    } else {
        std::snprintf(buf, sizeof(buf), "[SIM] WARNING: SDL_GetCurrentDisplayMode(0) failed: %s", SDL_GetError());
        log_line(buf);
    }
#else
    log_line("[SIM] SDL diagnostics unavailable (SDL headers not detected)");
#endif
}

// Simulator entrypoint:
// - open log file
// - load config (settings.cfg)
// - init LVGL + SDL display/input
// - build UI
// - run LVGL loop until `g_quit`
int main(int /*argc*/, char** /*argv*/) {
    g_log_file = std::fopen("sim.log", "w");
    
    load_config();
    
    lv_init();
    log_line("[SIM] lv_init ok");

#if HAVE_SDL2_HEADER
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "[SIM] ERROR: SDL_Init failed: %s", SDL_GetError());
        log_line(buf);
        if (g_log_file) std::fclose(g_log_file);
        return 1;
    }
    log_line("[SIM] SDL_Init ok");
#endif

  // Display size. On Pi DSI panel the native mode is 720x1280 (portrait).
  // We match the native resolution so SDL/KMSDRM can set the CRTC mode.
  int SCR_W = 1280;
  int SCR_H = 720;

#if HAVE_SDL2_HEADER
  {
      SDL_DisplayMode dm;
      if (SDL_GetCurrentDisplayMode(0, &dm) == 0 && dm.w > 0 && dm.h > 0) {
          char buf[128];
          std::snprintf(buf, sizeof(buf), "[SIM] native display mode: %dx%d", dm.w, dm.h);
          log_line(buf);
          SCR_W = dm.w;
          SCR_H = dm.h;
      }
  }
#endif

    lv_display_t* disp = lv_sdl_window_create(SCR_W, SCR_H);
    if (!disp) {
        log_line("[SIM] ERROR: lv_sdl_window_create failed");
        if (g_log_file) std::fclose(g_log_file);
        return 1;
    }
  {
      char buf[96];
      std::snprintf(buf, sizeof(buf), "[SIM] SDL window created %dx%d", SCR_W, SCR_H);
      log_line(buf);
  }

    log_startup_diagnostics(SCR_W, SCR_H);
    log_gstreamer_support_status();

    // Install signal handlers so UI exits cleanly on gpio-shutdown (power button) or Ctrl+C
    std::signal(SIGINT,  handle_shutdown_signal);
    std::signal(SIGTERM, handle_shutdown_signal);
#ifdef SIGHUP
    std::signal(SIGHUP,  handle_shutdown_signal);
#endif
#ifdef SIGQUIT
    std::signal(SIGQUIT, handle_shutdown_signal);
#endif

  // Attach input devices for simulator convenience.
#if HAVE_SDL_MOUSE
    {
        lv_indev_t* m = lv_sdl_mouse_create();
      if (m) log_line("[SIM] SDL mouse attached");
        else   log_line("[SIM] WARNING: pointer input create returned null");
    }
#endif
#if HAVE_SDL_KBD
  {
      lv_indev_t* k = lv_sdl_keyboard_create();
      if (k) log_line("[SIM] SDL keyboard attached");
  }
#endif
#if HAVE_SDL_WHEEL
    {
        lv_indev_t* w = lv_sdl_mousewheel_create();
      if (w) log_line("[SIM] SDL mousewheel attached");
    }
#endif

    build_ui();
    log_line("[SIM] entering loop");

    while (!g_quit) {
        lv_timer_handler();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        lv_tick_inc(5); // advance LVGL tick so events/timeouts fire
    }

    log_line("[SIM] loop exit");
    
  // Cleanup (best-effort; safe even if unavailable)
        stop_gstreamer_receiver();
    
    if (g_log_file) std::fclose(g_log_file);
    return 0;
}
