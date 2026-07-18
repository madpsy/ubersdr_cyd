#include "slide_spaceweather.h"

namespace {
uint16_t propColor(const String& q) {
  String l = q;
  l.toLowerCase();
  if (l == "excellent" || l == "good") return kColGood;
  if (l == "fair")                     return kColWarn;
  if (l == "poor")                     return kColBad;
  return kColMuted;
}

// K-index descriptor: 0-2 quiet, 3-4 unsettled, 5+ storm.
const char* kIndexWord(int k) {
  if (k <= 2) return "quiet";
  if (k <= 4) return "unsettled";
  return "storm";
}

uint16_t kIndexColor(int k) {
  if (k <= 2) return kColGood;
  if (k <= 4) return kColWarn;
  return kColBad;
}

// Draw a metric card: label (top), big value (centre), sub-note (bottom).
void drawMetricCard(TFT_eSPI& tft, int16_t x, int16_t y, int16_t w, int16_t h,
                    const char* label, const String& value, uint16_t valColor,
                    const char* note) {
  drawCard(tft, x, y, w, h);
  const int16_t cx = x + w / 2;
  drawText(tft, label, cx, y + 8, 2, kColMuted, TC_DATUM);
  drawText(tft, value, cx, y + 26, 6, valColor, TC_DATUM);
  if (note && note[0]) {
    drawText(tft, note, cx, y + h - 18, 2, kColMuted, TC_DATUM);
  }
}
}  // namespace

// Screen geometry reminder:
//   kHeaderH  = 24   (top chrome)
//   kContentY = 24   (content starts here)
//   kFooterH  = 20   (bottom chrome)
//   kScreenH  = 240
//   Content ends at y = 240 - 20 = 220
//
// slideTitle() draws at kContentY+6 (font 4) with underline at kContentY+33 = y57.
// So usable content below title starts at y ≈ 58.
//
// Layout:
//   y=62   "PROPAGATION" label  (font 2, ~14 px)
//   y=76   propQuality          (font 4, ~26 px → bottom ≈ 102)
//   y=106  separator line
//   y=112  three metric cards   (height = 220 - 4 - 112 = 104 px)
//
void SpaceWeatherSlide::draw(TFT_eSPI& tft, const UberSDRSnapshot& snap, bool full) {
  (void)full;
  slideTitle(tft, "SPACE WEATHER");

  // ── Propagation quality banner ──────────────────────────────────────────────
  String prop = snap.propQuality.length() ? snap.propQuality : String("Unknown");
  const uint16_t propCol = propColor(prop);

  drawText(tft, "PROPAGATION", 10, 62, 2, kColMuted, TL_DATUM);
  drawText(tft, prop,          10, 76, 4, propCol,   TL_DATUM);

  // Thin separator line.
  tft.drawFastHLine(10, 106, kScreenW - 20, kColCardHi);

  // ── Metric cards ────────────────────────────────────────────────────────────
  const int16_t margin = 10;
  const int16_t gap    = 8;
  const int16_t cardW  = (kScreenW - 2 * margin - 2 * gap) / 3;
  const int16_t cardY  = 112;
  const int16_t cardH  = (kScreenH - kFooterH) - 4 - cardY;  // fills to bottom

  char kBuf[8];  snprintf(kBuf,  sizeof(kBuf),  "%d", snap.kIndex);
  char aBuf[8];  snprintf(aBuf,  sizeof(aBuf),  "%d", snap.aIndex);
  char sBuf[12]; snprintf(sBuf,  sizeof(sBuf),  "%d", static_cast<int>(snap.solarFlux + 0.5f));

  drawMetricCard(tft, margin,                     cardY, cardW, cardH,
                 "K-INDEX",    kBuf, kIndexColor(snap.kIndex), kIndexWord(snap.kIndex));
  drawMetricCard(tft, margin + (cardW + gap),     cardY, cardW, cardH,
                 "A-INDEX",    aBuf, kColText, "");
  drawMetricCard(tft, margin + 2 * (cardW + gap), cardY, cardW, cardH,
                 "SOLAR FLUX", sBuf, kColAccent, "SFU");
}
