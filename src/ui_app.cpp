// src/ui_app.cpp
//
// UI entrypoint used by platform code (`platforms/pi-sdl/main.cpp`).
// Kept intentionally small: the actual UI is implemented in `src/ui/*`.
#include "ui_app.h"

#include "ui/dashboard.h"

// Build the LVGL UI on the active screen.
void build_ui() {
    cg::ui::build_main_screen();
}

