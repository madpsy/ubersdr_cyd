#pragma once

#include <stdint.h>

// ── CYD onboard RGB LED — health status indicator ────────────────────────────
//
// The ESP32-2432S028R uses a bare ESP32-D0WDQ6 chip (not a WROOM-32 module),
// so GPIO 16 is free — it is NOT the flash /CS on this board.
//
// Common-anode RGB LED: GPIO 4 (Red), 16 (Green), 17 (Blue). Active-LOW.
//
// LED colour reflects the UberSDR monitor-health overall status:
//   Blue  — no health data received yet (waiting for first API poll)
//   Green — healthOverall == 0  (OK)
//   Amber — healthOverall == 1  (WARNING, red + green)
//   Red   — healthOverall == 2  (CRITICAL)

// Initialise GPIO pins and set the LED to blue (no data).
// Call once in setup(), after Serial.begin().
void statusLedBegin();

// Update the LED colour to reflect the current health state.
// Call every loop() iteration (or whenever the snapshot changes).
//   healthValid   — false until the first successful /admin/monitor-health poll
//   healthOverall — 0=ok, 1=warning, 2=critical  (from UberSDRSnapshot)
void statusLedUpdate(bool healthValid, uint8_t healthOverall);
