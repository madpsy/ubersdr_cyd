#pragma once
#include "slide_base.h"

// RANKING slide — shows PSKReporter, WSPR Live, and/or RBN skimmer rankings.
// The slide is skipped entirely when no source has data.
// Cards are laid out dynamically based on what is available:
//   PSK only            → full-width combined card (spots + DXCC)
//   WSPR only           → full-width panel (3 time windows)
//   RBN only            → full-width RBN card
//   PSK + WSPR          → PSK left half, WSPR right half
//   PSK + RBN           → PSK left half, RBN right half
//   WSPR + RBN          → WSPR left half, RBN right half
//   All three           → PSK left third, WSPR middle third, RBN right third
class RankingSlide : public Slide {
 public:
  const char* name() const override { return "RANKING"; }
  void draw(TFT_eSPI& tft, const UberSDRSnapshot& snap, bool full) override;
  bool hasData(const UberSDRSnapshot& snap) const override {
    return snap.pskValid || snap.wsprValid || snap.rbnValid;
  }
};
