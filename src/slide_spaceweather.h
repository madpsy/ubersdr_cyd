#pragma once

#include "slide_base.h"

// Space-weather slide: propagation quality banner + K / A / Solar-flux cards.
class SpaceWeatherSlide : public Slide {
 public:
  const char* name() const override { return "SPACE"; }
  void draw(TFT_eSPI& tft, const UberSDRSnapshot& snap, bool full) override;
};
