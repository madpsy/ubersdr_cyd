#include <Arduino.h>

#include "connectivity.h"
#include "display.h"
#include "reset_button.h"
#include "settings.h"
#include "setup_portal.h"
#include "ubersdr_api.h"

namespace {
constexpr uint32_t kLoopDelayMs = 10;
}

void setup() {
  Serial.begin(115200);
  delay(100);

  settingsBegin();
  displayBegin();      // initialises TFT + touch + calibration + slideshow
  resetButtonBegin();
  setupPortalBegin();
  connectivityBegin();
  ubersdrApiBegin();   // overview-display API poller
}

void loop() {
  setupPortalLoop();
  connectivityLoop();
  ubersdrApiLoop();    // polls UberSDR endpoints on a timer (no-op until WiFi up)

  const bool resetting = resetButtonLoop();
  if (!resetting) {
    displayUpdate(getSystemSnapshot());
  }

  delay(kLoopDelayMs);
}
