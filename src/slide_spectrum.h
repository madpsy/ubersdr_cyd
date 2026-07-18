#pragma once

#include "slide_base.h"

// Spectrum slide: a grid of mini spectrum charts, one per band, 6 per page.
// Paginates across multiple pages when more than 6 bands are available; all
// pages share the slide's on-screen time window.
class SpectrumSlide : public Slide {
 public:
  const char* name() const override { return "SPECTRUM"; }
  void draw(TFT_eSPI& tft, const UberSDRSnapshot& snap, bool full) override;
  bool hasData(const UberSDRSnapshot& snap) const override {
    return snap.spectrumValid && snap.spectrumCount > 0;
  }
  int pageCount(const UberSDRSnapshot& snap) const override;
  void setPage(int page) override { page_ = page; }

 private:
  int page_ = 0;
};
