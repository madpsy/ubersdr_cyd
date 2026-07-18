#pragma once

#include "slide_base.h"

// GPSDO slide: lock status, oscillator mode/frequency, GPS fix quality.
// Only shown when /admin/gpsdo-health reports enabled:true.
class GpsdoSlide : public Slide {
 public:
  const char* name() const override { return "GPSDO"; }
  void draw(TFT_eSPI& tft, const UberSDRSnapshot& snap, bool full) override;
  bool hasData(const UberSDRSnapshot& snap) const override {
    return snap.gpsdoValid && snap.gpsdoEnabled;
  }
};
