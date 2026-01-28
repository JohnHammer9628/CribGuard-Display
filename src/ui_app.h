#pragma once

// Set by signal handler / UI events to exit the main loop.
extern bool g_quit;

// Signal handler used by the platform entrypoint.
void handle_shutdown_signal(int sig);

// Builds the LVGL UI (dashboard + modals).
void build_ui();

