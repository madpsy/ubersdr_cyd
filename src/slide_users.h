#pragma once

#include "slide_base.h"

// Users & capacity slide: big current/max count, capacity bar, free + bypass.
// When the compact sessions endpoint returns non_bypassed_users detail, a
// second page is shown listing each user's country and connection duration.
// pageCount() returns 1 (no detail data) or 2 (detail available), so the
// slideshow's existing time-splitting logic gives 8 s when there is only the
// summary page and 4 s per page (8 s ÷ 2) when the list page is also shown.
class UsersSlide : public Slide {
 public:
  const char* name() const override { return "USERS"; }
  void draw(TFT_eSPI& tft, const UberSDRSnapshot& snap, bool full) override;

  int  pageCount(const UberSDRSnapshot& snap) const override {
    return snap.userDetailCount > 0 ? 2 : 1;
  }
  void setPage(int page) override { page_ = page; }

  // Returns a key that changes whenever the displayed user data changes.
  // Encodes counts + per-user frequency so any join/leave/tune is detected.
  String dataKey(const UberSDRSnapshot& snap) const override {
    char buf[64];
    uint32_t freqSum = 0;
    for (int i = 0; i < snap.userDetailCount; ++i)
      freqSum += snap.userDetails[i].frequencyHz;
    snprintf(buf, sizeof(buf), "%d/%d/%d/%lu",
             snap.userCount, snap.bypassCount,
             snap.userDetailCount, (unsigned long)freqSum);
    return String(buf);
  }

 private:
  int page_ = 0;
};
