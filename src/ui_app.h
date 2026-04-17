#pragma once

// Set by signal handler / UI events to exit the main loop.
extern bool g_quit;
extern int g_exit_code;

// Exit code used when user explicitly exits to desktop.
// deploy/pi/cribguard.service uses RestartPreventExitStatus=200.
constexpr int kExitCodeExitToDesktop = 200;

// Signal handler used by the platform entrypoint.
void handle_shutdown_signal(int sig);

// Builds the LVGL UI (dashboard + modals).
void build_ui();

