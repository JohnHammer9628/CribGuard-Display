#ifndef LV_DRV_CONF_H
#define LV_DRV_CONF_H

#include "lv_conf.h"

/* Enable SDL backend from lv_drivers, guard if LVGL also defines LV_USE_SDL */
#ifndef LV_USE_SDL
#  define LV_USE_SDL 1
#endif

#if LV_USE_SDL
#  define LV_SDL_FULLSCREEN          0
#  define LV_SDL_DIRECT_EXIT         1
#  define LV_SDL_WAIT_EVENT_TIMEOUT  10
#  define LV_SDL_BUF_COUNT           1
#  define LV_SDL_DISP_DISABLE_REFR   0
#endif

#endif /* LV_DRV_CONF_H */
