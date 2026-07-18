#include "reset_button.h"

#include <Arduino.h>

#include "display.h"
#include "settings.h"

namespace {
// GPIO0 is the BOOT button on the back of the ESP32-2432S028R, next to USB.
// It is only needed for flash mode at power-on, so it is free at runtime.
constexpr int8_t kResetButtonPin = 0;
constexpr uint32_t kHoldMsToReset = 5000;
constexpr uint32_t kFeedbackIntervalMs = 250;

bool pressActive = false;
uint32_t pressStartMs = 0;
uint32_t lastFeedbackMs = 0;
}  // namespace

void resetButtonBegin() {
  pinMode(kResetButtonPin, INPUT_PULLUP);
}

bool resetButtonLoop() {
  const bool pressed = digitalRead(kResetButtonPin) == LOW;
  const uint32_t nowMs = millis();

  if (!pressed) {
    if (pressActive) {
      pressActive = false;
      requestDisplayRedraw();
    }
    return false;
  }

  if (!pressActive) {
    pressActive = true;
    pressStartMs = nowMs;
    lastFeedbackMs = 0;
  }

  const uint32_t heldMs = nowMs - pressStartMs;
  if (heldMs >= kHoldMsToReset) {
    displayShowMessage("Factory reset", "Restoring defaults...");
    factoryResetSettings();
    delay(500);
    ESP.restart();
    return true;
  }

  if (nowMs - lastFeedbackMs >= kFeedbackIntervalMs) {
    lastFeedbackMs = nowMs;
    const uint32_t remainingSec = (kHoldMsToReset - heldMs + 999) / 1000;
    displayShowMessage("Hold to reset",
                       String(remainingSec) + "s — release to cancel");
  }

  return true;
}
