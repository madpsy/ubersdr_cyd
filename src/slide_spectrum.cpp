#include "slide_spectrum.h"

namespace {
constexpr int kPerPage = 6;   // 2 columns × 3 rows

// Distinct chart colours (RGB565), cycled per chart position on a page so
// adjacent charts are always visually different.  Palette chosen to read well
// on black: cyan, amber, green, magenta, orange, sky-blue.
const uint16_t kChartColors[kPerPage] = {
  0x05FF,  // cyan
  0xFD20,  // amber
  0x2FEA,  // green
  0xF81F,  // magenta
  0xFBE0,  // orange
  0x5D9F,  // sky blue
};

// Draw one mini spectrum chart in the box (x,y,w,h) using the given colour.
void drawMiniChart(TFT_eSPI& tft, int16_t x, int16_t y, int16_t w, int16_t h,
                   const BandSpectrum& bs, uint16_t color) {
  // Card background + border.
  drawCard(tft, x, y, w, h, kColCard, kColCardHi);

  // Band label (top-left, in the chart colour).
  drawText(tft, bs.band, x + 5, y + 3, 2, color, TL_DATUM);

  // Plot area — leave room for the label row at the top.
  const int16_t plotX = x + 4;
  const int16_t plotY = y + 18;
  const int16_t plotW = w - 8;
  const int16_t plotH = h - 22;
  if (plotW <= 2 || plotH <= 2) return;

  // A dim baseline grid line at the bottom.
  tft.drawFastHLine(plotX, plotY + plotH - 1, plotW, kColBarTrack);

  // ── FT8 bandwidth overlay ─────────────────────────────────────────────────
  // Draw before the spectrum so the spectrum trace renders on top.
  const float bandSpanMhz = bs.endFreqMhz - bs.startFreqMhz;
  if (bs.ft8FreqMhz > 0.0f && bandSpanMhz > 0.0f) {
    // Map FT8 start and end frequencies to pixel columns.
    const float ft8Start = bs.ft8FreqMhz;
    const float ft8End   = ft8Start + bs.ft8BwMhz;
    auto freqToPx = [&](float fMhz) -> int16_t {
      float frac = (fMhz - bs.startFreqMhz) / bandSpanMhz;
      if (frac < 0.0f) frac = 0.0f;
      if (frac > 1.0f) frac = 1.0f;
      return plotX + (int16_t)(frac * (plotW - 1));
    };
    const int16_t ft8X0 = freqToPx(ft8Start);
    const int16_t ft8X1 = freqToPx(ft8End);

    // Shaded fill for the 3 kHz bandwidth region — dim white so it reads on
    // any chart colour.
    constexpr uint16_t kFt8Shade = 0x2104;  // ~12% white (dark grey)
    if (ft8X1 > ft8X0) {
      tft.fillRect(ft8X0 + 1, plotY, ft8X1 - ft8X0, plotH, kFt8Shade);
    }
    // Bright white vertical line at the FT8 start frequency.
    tft.drawFastVLine(ft8X0, plotY, plotH, TFT_WHITE);
  }

  // ── Spectrum trace ────────────────────────────────────────────────────────
  // Dim fill = colour scaled ~40% for the body, bright line at the top.
  const uint16_t fillCol = color;
  const uint16_t dim = ((color >> 1) & 0x7BEF);  // halve each channel (approx)

  int16_t prevYtop = -1;
  for (int16_t px = 0; px < plotW; ++px) {
    const int idx = (int)((int32_t)px * kSpectrumPoints / plotW);
    const uint8_t mag = bs.pts[idx < kSpectrumPoints ? idx : kSpectrumPoints - 1];
    const int16_t barH = (int16_t)((int32_t)mag * plotH / 255);
    const int16_t colX = plotX + px;
    const int16_t yTop = plotY + plotH - barH;
    // Filled body.
    if (barH > 0) tft.drawFastVLine(colX, yTop, barH, dim);
    // Bright trace cap — connect to previous top for a continuous line.
    if (prevYtop >= 0) {
      tft.drawLine(colX - 1, prevYtop, colX, yTop, fillCol);
    } else {
      tft.drawPixel(colX, yTop, fillCol);
    }
    prevYtop = yTop;
  }
}
}  // namespace

int SpectrumSlide::pageCount(const UberSDRSnapshot& snap) const {
  const int n = snap.spectrumCount;
  if (n <= 0) return 1;
  return (n + kPerPage - 1) / kPerPage;
}

// 2×3 grid of mini charts for the current page.
void SpectrumSlide::draw(TFT_eSPI& tft, const UberSDRSnapshot& snap, bool full) {
  (void)full;

  // Title includes the page indicator when multiple pages exist.
  const int pages = pageCount(snap);
  if (pages > 1) {
    char t[24];
    snprintf(t, sizeof(t), "SPECTRUM  %d/%d", page_ + 1, pages);
    slideTitle(tft, t);
  } else {
    slideTitle(tft, "SPECTRUM");
  }

  if (!snap.spectrumValid || snap.spectrumCount == 0) {
    drawText(tft, "waiting for data...", kScreenW / 2, kContentY + 90, 2,
             kColMuted, TC_DATUM);
    return;
  }

  const int total = snap.spectrumCount;
  const int start = page_ * kPerPage;
  if (start >= total) return;

  // Grid geometry: 2 columns × 3 rows within the content area (below title).
  const int16_t gridY = kContentY + 40;
  const int16_t gridH = kContentH - 44;
  const int16_t margin = 6;
  const int16_t gap = 6;
  const int16_t cols = 2, rows = 3;
  const int16_t cw = (kScreenW - 2 * margin - (cols - 1) * gap) / cols;
  const int16_t ch = (gridH - (rows - 1) * gap) / rows;

  for (int slot = 0; slot < kPerPage; ++slot) {
    const int bandIdx = start + slot;
    if (bandIdx >= total) break;
    const BandSpectrum& bs = snap.spectrum[bandIdx];
    if (!bs.valid) continue;

    const int r = slot / cols;
    const int c = slot % cols;
    const int16_t x = margin + c * (cw + gap);
    const int16_t y = gridY + r * (ch + gap);
    drawMiniChart(tft, x, y, cw, ch, bs, kChartColors[slot % kPerPage]);
  }
}
