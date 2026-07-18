#include "slide_base.h"

// ── Colour mapping ────────────────────────────────────────────────────────────

uint16_t colorForQuality(const String& q) {
  if (q == "EXCELLENT" || q == "GOOD") return kColGood;
  if (q == "FAIR")                     return kColWarn;
  if (q == "POOR")                     return kColBad;
  return kColMuted;
}

uint16_t colorForStatus(const String& s) {
  if (s == "ok")       return kColGood;
  if (s == "warning")  return kColWarn;
  if (s == "critical") return kColBad;
  return kColMuted;
}

uint16_t colorForRatio(float ratio) {
  if (ratio >= 1.0f)  return kColBad;
  if (ratio >= 0.75f) return kColWarn;
  return kColGood;
}

// ── Drawing helpers ───────────────────────────────────────────────────────────

void slideTitle(TFT_eSPI& tft, const String& text) {
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(kColAccent, kColBg);
  tft.drawString(text, 10, kContentY + 6, 4);
  // Accent underline beneath the title.
  const int16_t w = tft.textWidth(text, 4);
  tft.drawFastHLine(10, kContentY + 33, w, kColAccent);
}

void drawCard(TFT_eSPI& tft, int16_t x, int16_t y, int16_t w, int16_t h,
              uint16_t fill, uint16_t border) {
  tft.fillRoundRect(x, y, w, h, 6, fill);
  tft.drawRoundRect(x, y, w, h, 6, border);
}

void drawBar(TFT_eSPI& tft, int16_t x, int16_t y, int16_t w, int16_t h,
             float ratio, uint16_t fill, uint16_t track) {
  if (ratio < 0.0f) ratio = 0.0f;
  if (ratio > 1.0f) ratio = 1.0f;
  const int16_t r = h / 2;
  tft.fillRoundRect(x, y, w, h, r, track);
  const int16_t fw = static_cast<int16_t>(w * ratio);
  if (fw >= h) {
    tft.fillRoundRect(x, y, fw, h, r, fill);
  } else if (fw > 0) {
    tft.fillRect(x, y, fw, h, fill);
  }
}

void drawText(TFT_eSPI& tft, const String& text, int16_t x, int16_t y,
              uint8_t font, uint16_t color, uint8_t datum) {
  tft.setTextDatum(datum);
  // Transparent background — text composes over cards/bars without leaving an
  // opaque black rectangle.  The slideshow clears the content area each full
  // frame, so there is no stale-pixel ghosting for static layouts.
  tft.setTextColor(color);
  tft.drawString(text, x, y, font);
}
