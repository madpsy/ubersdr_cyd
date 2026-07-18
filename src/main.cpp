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

  delay(kLoopDelayMs);
}
