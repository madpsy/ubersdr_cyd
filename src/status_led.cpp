#include "status_led.h"

#include <Arduino.h>

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
  Serial.println("[LED] statusLedBegin: configuring R=4 G=16 B=17");
  pinMode(kLedR, OUTPUT);
  Serial.println("[LED] R pin configured");
  pinMode(kLedG, OUTPUT);
  Serial.println("[LED] G pin configured");
  pinMode(kLedB, OUTPUT);
  Serial.println("[LED] B pin configured");

  // All off first
  digitalWrite(kLedR, HIGH);
  digitalWrite(kLedG, HIGH);
  digitalWrite(kLedB, HIGH);
  Serial.println("[LED] all off");

  // Blue = waiting for first health data
  digitalWrite(kLedB, LOW);
  Serial.println("[LED] blue on (waiting for health data)");
}

void statusLedUpdate(bool healthValid, uint8_t healthOverall) {
  if (!healthValid) {
    Serial.println("[LED] blue (no health data)");
    setRgb(false, false, true);   // Blue  — no data yet
  } else if (healthOverall == 2) {
    Serial.println("[LED] red (critical)");
    setRgb(true,  false, false);  // Red   — critical
  } else if (healthOverall == 1) {
    Serial.println("[LED] amber/yellow (warning)");
    setRgb(true,  true,  false);  // Amber — warning (red + green)
  } else {
    Serial.println("[LED] green (OK)");
    setRgb(false, true,  false);  // Green — OK
  }
}
