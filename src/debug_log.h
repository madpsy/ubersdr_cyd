#pragma once

#include <Arduino.h>

// ── Debug ring-buffer log ─────────────────────────────────────────────────────
//
// A small in-memory circular buffer of recent log lines, exposed over HTTP at
// /debug (see setup_portal.cpp).  Every line is also echoed to Serial.
//
// Usage:
//   debugLog("plain message");
//   debugLogf("status=%d value=%.2f", code, x);
//
// The buffer holds the most recent kDebugLogLines entries; older lines are
// overwritten.  Each entry is prefixed with the millis() timestamp.

constexpr int kDebugLogLines = 100;

// Append a pre-formatted line to the buffer (and Serial).
void debugLog(const String& line);

// printf-style variant.
void debugLogf(const char* fmt, ...);

// Return the whole buffer, oldest-first, as a single newline-joined string.
String debugLogDump();
