#include "slide_load.h"

namespace {
// Colour a load value relative to core count (mirrors monitor_display.go).
uint16_t loadColor(float load, int cores) {
  if (cores <= 0) return kColText;
  if (load >= cores)          return kColBad;
  if (load >= cores * 0.75f)  return kColWarn;
  return kColGood;
}

// Draw one load-average card: label on top, value below, coloured.
void drawLoadCard(TFT_eSPI& tft, int16_t x, int16_t y, int16_t w, int16_t h,
                  const char* label, float value, int cores) {
  drawCard(tft, x, y, w, h);
  const int16_t cx = x + w / 2;
  drawText(tft, label, cx, y + 8, 2, kColMuted, TC_DATUM);
  char buf[12];
  snprintf(buf, sizeof(buf), "%.2f", value);
  drawText(tft, buf, cx, y + 28, 4, loadColor(value, cores), TC_DATUM);
}
}  // namespace

// Layout:
//   Title "SYSTEM LOAD"
//   Three cards: 1 MIN / 5 MIN / 15 MIN         y≈60  h=66
//   "N cores" chip centred under the cards
//   CPU TEMP label + value + threshold, gauge bar
void LoadSlide::draw(TFT_eSPI& tft, const UberSDRSnapshot& snap, bool full) {
  (void)full;
  slideTitle(tft, "SYSTEM LOAD");

  if (!snap.loadValid) {
    drawText(tft, "waiting for data...", kScreenW / 2, kContentY + 90, 2,
             kColMuted, TC_DATUM);
    return;
  }

  // ── Three load cards ──
  const int16_t margin = 10;
  const int16_t gap = 8;
  const int16_t cardW = (kScreenW - 2 * margin - 2 * gap) / 3;   // ~98
  const int16_t cardY = kContentY + 42;
  const int16_t cardH = 62;

  drawLoadCard(tft, margin,                        cardY, cardW, cardH,
               "1 MIN",  snap.load1,  snap.cpuCores);
  drawLoadCard(tft, margin + (cardW + gap),        cardY, cardW, cardH,
               "5 MIN",  snap.load5,  snap.cpuCores);
  drawLoadCard(tft, margin + 2 * (cardW + gap),    cardY, cardW, cardH,
               "15 MIN", snap.load15, snap.cpuCores);

  // Cores chip (right-aligned under cards).
  if (snap.cpuCores > 0) {
    char coreBuf[16];
    snprintf(coreBuf, sizeof(coreBuf), "%d cores", snap.cpuCores);
    drawText(tft, coreBuf, kScreenW - margin, cardY + cardH + 4, 2,
             kColMuted, TR_DATUM);
  }

  // ── CPU temperature gauge ──
  const int16_t tempY = kContentY + 128;
  if (snap.cpuTempAvailable) {
    drawText(tft, "CPU TEMP", margin, tempY, 2, kColMuted, TL_DATUM);

    char tBuf[12];
    snprintf(tBuf, sizeof(tBuf), "%.0f\xB0" "C", snap.cpuTempC);
    const uint16_t tCol = colorForStatus(snap.cpuTempStatus);
    drawText(tft, tBuf, kScreenW - margin, tempY - 4, 4, tCol, TR_DATUM);

    // Gauge: 0..threshold mapped to bar.  Fall back to 100°C span if no threshold.
    const float thresh = snap.cpuTempThresholdC > 0 ? snap.cpuTempThresholdC : 100.0f;
    const float ratio = snap.cpuTempC / thresh;
    const int16_t barY = tempY + 26;
    drawBar(tft, margin, barY, kScreenW - 2 * margin, 14, ratio, tCol);

    char thBuf[24];
    snprintf(thBuf, sizeof(thBuf), "limit %.0f\xB0" "C", thresh);
    drawText(tft, thBuf, margin, barY + 20, 2, kColMuted, TL_DATUM);
  } else {
    drawText(tft, "CPU temp unavailable", kScreenW / 2, tempY + 10, 2,
             kColMuted, TC_DATUM);
  }
}
