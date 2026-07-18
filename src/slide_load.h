#pragma once

#include "slide_base.h"

// System load & CPU temperature slide: 1/5/15-min load cards + temp gauge bar.
class LoadSlide : public Slide {
 public:
  const char* name() const override { return "LOAD"; }
  void draw(TFT_eSPI& tft, const UberSDRSnapshot& snap, bool full) override;
};
