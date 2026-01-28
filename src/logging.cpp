// src/logging.cpp
//
// Minimal logging utility used across the project.
// The simulator writes to `sim.log` (opened in `platforms/pi-sdl/main.cpp`).
#include "logging.h"

FILE* g_log_file = nullptr;

// Write a single line to the active log file (if set).
// Safe to call even if logging is not initialized (no-op).
void log_line(const char* msg) {
    if (g_log_file) {
        std::fprintf(g_log_file, "%s\n", msg);
        std::fflush(g_log_file);
    }
}

