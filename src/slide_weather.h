#pragma once

#include "slide_base.h"

// Terrestrial weather slide: condition + temperature, with humidity / pressure /
// wind cards.  Mirrors the admin.html top-right weather widget.
class WeatherSlide : public Slide {
 public:
  const char* name() const override { return "WEATHER"; }
  void draw(TFT_eSPI& tft, const UberSDRSnapshot& snap, bool full) override;
};
