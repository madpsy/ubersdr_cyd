#include "slide_bands.h"

namespace {
// Abbreviate a quality label to fit the pill.
const char* qualityAbbrev(const String& q) {
  if (q == "EXCELLENT") return "EXC";
  if (q == "GOOD")      return "GOOD";
  if (q == "FAIR")      return "FAIR";
  if (q == "POOR")      return "POOR";
  return "?";
}

// Darken a colour for use as a pill background (keeps text readable).
uint16_t darken(uint16_t c) {
  const uint16_t r = (c >> 11) & 0x1F;
  const uint16_t g = (c >> 5)  & 0x3F;
  const uint16_t b =  c        & 0x1F;
  return ((r / 4) << 11) | ((g / 4) << 5) | (b / 4);
}
}  // namespace

// Grid layout: up to 4 columns × 3 rows = 12 pills.
// Each pill: band name (top, white) + quality (bottom, coloured).
void BandsSlide::draw(TFT_eSPI& tft, const UberSDRSnapshot& snap, bool full) {
  (void)full;
  slideTitle(tft, "BAND CONDITIONS");

  const int n = snap.bandCount;
  if (n <= 0) {
    drawText(tft, "No band data yet", kScreenW / 2, kContentY + 90, 2,
             kColMuted, TC_DATUM);
    return;
  }

  // Choose column count so pills stay comfortably sized.
  int cols = 4;
  if (n <= 3) cols = n;
  else if (n <= 4) cols = 4;
  else if (n <= 6) cols = 3;
  else cols = 4;
  const int rows = (n + cols - 1) / cols;

  const int16_t margin = 8;
  const int16_t gap = 6;
  const int16_t gridY = kContentY + 42;
  const int16_t gridH = kContentH - 46;

  const int16_t pillW = (kScreenW - 2 * margin - (cols - 1) * gap) / cols;
  const int16_t pillH = (gridH - (rows - 1) * gap) / rows;

  for (int i = 0; i < n; ++i) {
    const int r = i / cols;
    const int c = i % cols;
    const int16_t x = margin + c * (pillW + gap);
    const int16_t y = gridY + r * (pillH + gap);

    const BandCondition& b = snap.bands[i];
    const uint16_t qc = colorForQuality(b.quality);
    const uint16_t bg = darken(qc);

    tft.fillRoundRect(x, y, pillW, pillH, 6, bg);
    tft.drawRoundRect(x, y, pillW, pillH, 6, qc);

    const int16_t cx = x + pillW / 2;
    // Strip trailing 'm' from band name (e.g. "20m" → "20") to save space.
    String bandLabel = b.band;
    if (bandLabel.endsWith("m") || bandLabel.endsWith("M"))
      bandLabel.remove(bandLabel.length() - 1);
    // Band name in white, upper portion.
    // Font 4 is ~26 px tall; anchor its top at ~25% down the pill so it sits
    // comfortably in the upper half with clear space below.
    drawText(tft, bandLabel, cx, y + pillH / 4 - 4, 4, kColText, TC_DATUM);
    // Quality abbreviation in its colour, lower portion.
    // Font 2 is ~16 px tall; start it a few px below the pill midpoint.
    drawText(tft, qualityAbbrev(b.quality), cx, y + pillH * 3 / 5, 2, qc,
             TC_DATUM);
  }
}
