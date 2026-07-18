#pragma once

#include "slide_base.h"

// Antenna slide: shows the live antenna-switch state (active port label, or
// GROUNDED) and the rotator position (azimuth + compass) when available.
// Only appears when the server reports an enabled antenna switch or a
// connected rotator.
class AntennaSlide : public Slide {
 public:
  const char* name() const override { return "ANTENNA"; }
  void draw(TFT_eSPI& tft, const UberSDRSnapshot& snap, bool full) override;
  bool hasData(const UberSDRSnapshot& snap) const override {
    return snap.antSwitchEnabled ||
           (snap.rotatorEnabled && snap.rotatorConnected);
  }
};
