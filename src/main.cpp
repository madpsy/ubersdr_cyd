#include <Arduino.h>

#include "connectivity.h"
#include "display.h"
#include "reset_button.h"
#include "settings.h"
#include "setup_portal.h"
#include "status_led.h"
#include "ubersdr_api.h"

namespace {
constexpr uint32_t kLoopDelayMs = 10;
constexpr uint32_t kLedUpdateMs = 2000;  // re-evaluate LED every 2 s
uint32_t           g_lastLedMs  = 0;
}

void setup() {
  Serial.begin(115200);
  delay(100);

  settingsBegin();
  statusLedBegin();       // RGB LED → blue while waiting for health data
  displayBegin();         // initialises TFT + touch + calibration + slideshow
  resetButtonBegin();
  setupPortalBegin();
  connectivityBegin();
  ubersdrApiBegin();      // initialise snapshot + mutex
  ubersdrApiTaskBegin();  // start background polling task on Core 0
}

void loop() {
  setupPortalLoop();
  connectivityLoop();

  const bool resetting = resetButtonLoop();
  if (!resetting) {
    displayUpdate(getSystemSnapshot());
  }

  // Update the onboard RGB LED to reflect health status (throttled to 2 s).
  // Always call statusLedUpdate() so ledEnabled changes take effect promptly.
  // We read only the two health fields via a small helper to avoid placing the
  // full UberSDRSnapshot (several KB) on the already-tight loopTask stack.
  const uint32_t nowMs = millis();
  if (nowMs - g_lastLedMs >= kLedUpdateMs) {
    g_lastLedMs = nowMs;
    bool    hValid;
    uint8_t hOverall;
    ubersdrApiGetHealth(hValid, hOverall);
    statusLedUpdate(hValid, hOverall);
  }

  delay(kLoopDelayMs);
}
