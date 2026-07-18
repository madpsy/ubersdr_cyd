#pragma once

#include "slide_base.h"

// Health slide: overall status banner + two-column dot-grid (page 0), then one
// detail page per unhealthy component that has issue strings (pages 1..N).
// All pages share the slide's on-screen time window (same as SpectrumSlide).
class HealthSlide : public Slide {
 public:
  const char* name() const override { return "HEALTH"; }
  void draw(TFT_eSPI& tft, const UberSDRSnapshot& snap, bool full) override;
  bool hasData(const UberSDRSnapshot& snap) const override {
    return snap.healthValid;
  }
  int  pageCount(const UberSDRSnapshot& snap) const override;
  void setPage(int page) override { page_ = page; }

 private:
  int page_ = 0;

  // Returns the index into snap.healthItems[] for the n-th detail page
  // (page >= 1).  Returns -1 if out of range.
  static int detailItemIndex(const UberSDRSnapshot& snap, int page);
};
