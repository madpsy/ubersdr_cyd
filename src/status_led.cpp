#include "status_led.h"

#include <Arduino.h>

#include "settings.h"

// ── CYD onboard RGB LED pin assignments ───────────────────────────────────────
// ESP32-2432S028R uses a bare ESP32-D0WDQ6 chip (not a WROOM-32 module).
// GPIO 16 is therefore free — it is NOT the flash /CS on this board.
// All three LED channels are safe to use as outputs.
//
// Common-anode RGB LED: HIGH = off, LOW = on.
constexpr uint8_t kLedR = 4;
constexpr uint8_t kLedG = 16;
constexpr uint8_t kLedB = 17;

// ── Internal helpers ──────────────────────────────────────────────────────────

static void setRgb(bool r, bool g, bool b) {
  digitalWrite(kLedR, r ? LOW : HIGH);
  digitalWrite(kLedG, g ? LOW : HIGH);
  digitalWrite(kLedB, b ? LOW : HIGH);
}

// ── Public API ────────────────────────────────────────────────────────────────

void statusLedBegin() {
  pinMode(kLedR, OUTPUT);
  pinMode(kLedG, OUTPUT);
  pinMode(kLedB, OUTPUT);
  // Initial state: blue (waiting for health data), or all off if LED disabled.
  statusLedUpdate(false, 0);
}

void statusLedUpdate(bool healthValid, uint8_t healthOverall) {
  if (!getSettings().ledEnabled) {
    setRgb(false, false, false);
    return;
  }
  if (!healthValid)        setRgb(false, false, true);   // Blue  — no data
  else if (healthOverall == 2) setRgb(true,  false, false);  // Red   — critical
  else if (healthOverall == 1) setRgb(true,  true,  false);  // Amber — warning
  else                         setRgb(false, true,  false);  // Green — OK
}
