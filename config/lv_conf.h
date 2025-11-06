/* config/lv_conf.h - minimal LVGL v9 config for SDL simulator + SquareLine */

#ifndef LV_CONF_H
#define LV_CONF_H

/*******************
 * CORE SETTINGS
 *******************/
#define LV_CONF_INCLUDE_SIMPLE 1
#define LV_COLOR_DEPTH        32
#define LV_MEM_SIZE           (64U * 1024U)   /* Adjust if you need more */

#define LV_TICK_CUSTOM        1
#if LV_TICK_CUSTOM
#  include <stdint.h>
   /* We call lv_tick_inc() manually in main.cpp, so no function pointer needed */
#endif

/*******************
 * DEFAULT THEME / FONTS
 *******************/
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_DEFAULT       &lv_font_montserrat_20

/*******************
 * LOGGING (optional)
 *******************/
#define LV_USE_LOG            1
#if LV_USE_LOG
#  define LV_LOG_LEVEL        LV_LOG_LEVEL_WARN
#  define LV_LOG_TRACE_MEM    0
#  define LV_LOG_TRACE_TIMER  0
#  define LV_LOG_TRACE_INDEV  0
#  define LV_LOG_TRACE_DISP   0
#  define LV_LOG_TRACE_EVENT  0
#  define LV_LOG_TRACE_OBJ_CREATE 0
#endif

/*******************
 * SDL DRIVER
 *******************/
#define LV_USE_SDL            1
#if LV_USE_SDL
#  define LV_SDL_MOUSEWHEEL   1
#  define LV_SDL_FULLSCREEN   0
#  define LV_SDL_DIRECT_EXIT  1
#endif

/*******************
 * MISC WIDGET/FEATURE TRIMS
 * (Keep default ON; SquareLine generates only what it needs)
 *******************/
#define LV_USE_FLEX           1
#define LV_USE_GRID           1

/* Demos off to speed up build */
#define LV_USE_DEMO_WIDGETS   0
#define LV_USE_DEMO_BENCHMARK 0
#define LV_USE_DEMO_STRESS    0
#define LV_USE_DEMO_KEYPAD_AND_ENCODER 0
#define LV_USE_DEMO_MUSIC     0

#endif /* LV_CONF_H */
