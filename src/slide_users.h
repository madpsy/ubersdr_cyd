#pragma once

#include "slide_base.h"

// Users & capacity slide: big current/max count, capacity bar, free + bypass.
class UsersSlide : public Slide {
 public:
  const char* name() const override { return "USERS"; }
  void draw(TFT_eSPI& tft, const UberSDRSnapshot& snap, bool full) override;
};
