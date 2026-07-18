#pragma once

#include "slide_base.h"

// Band-conditions slide: a colour-coded grid of band pills (band + quality).
class BandsSlide : public Slide {
 public:
  const char* name() const override { return "BANDS"; }
  void draw(TFT_eSPI& tft, const UberSDRSnapshot& snap, bool full) override;
};
