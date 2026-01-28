#pragma once

#include <cstdio>

// Shared log sink. Owned/managed by the platform entrypoint.
extern FILE* g_log_file;

// Simple line logger used across subsystems.
void log_line(const char* msg);

