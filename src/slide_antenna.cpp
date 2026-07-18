#include "slide_antenna.h"

#include <math.h>

namespace {
// Draw a compass dial with a needle pointing at `azimuth` degrees (0 = N, CW).
// Centred at (cx, cy) with the given radius.
void drawCompass(TFT_eSPI& tft, int16_t cx, int16_t cy, int16_t r, int az) {
  // Outer ring.
  tft.drawCircle(cx, cy, r, kColCardHi);
  tft.drawCircle(cx, cy, r - 1, kColPanel);

  // Cardinal ticks + labels (N omitted — needle indicates north; label would
  // overlap the card title above the dial).
  const char* card[4] = {nullptr, "E", "S", "W"};
  for (int i = 0; i < 4; ++i) {
    const float a = i * 90.0f * (float)M_PI / 180.0f;
    const int16_t x0 = cx + static_cast<int16_t>(sinf(a) * (r - 2));
    const int16_t y0 = cy - static_cast<int16_t>(cosf(a) * (r - 2));
    const int16_t x1 = cx + static_cast<int16_t>(sinf(a) * (r - 8));
    const int16_t y1 = cy - static_cast<int16_t>(cosf(a) * (r - 8));
    tft.drawLine(x0, y0, x1, y1, kColMuted);
    if (card[i]) {
      const int16_t lx = cx + static_cast<int16_t>(sinf(a) * (r + 8));
      const int16_t ly = cy - static_cast<int16_t>(cosf(a) * (r + 8));
      drawText(tft, card[i], lx, ly, 2, kColMuted, MC_DATUM);
    }
  }

  // Needle.
  if (az >= 0) {
    const float a = az * (float)M_PI / 180.0f;
    const int16_t nx = cx + static_cast<int16_t>(sinf(a) * (r - 6));
    const int16_t ny = cy - static_cast<int16_t>(cosf(a) * (r - 6));
    // Tail (opposite direction), shorter and muted.
    const int16_t tx = cx - static_cast<int16_t>(sinf(a) * (r / 2));
    const int16_t ty = cy + static_cast<int16_t>(cosf(a) * (r / 2));
    tft.drawLine(cx, cy, tx, ty, kColMuted);
    // Draw a thicker pointer by stacking a few offset lines.
    tft.drawLine(cx, cy, nx, ny, kColGood);
    tft.drawLine(cx - 1, cy, nx, ny, kColGood);
    tft.drawLine(cx + 1, cy, nx, ny, kColGood);
  }
  tft.fillCircle(cx, cy, 3, kColAccent);
}

// 16-point compass label for an azimuth.
const char* compass16(int deg) {
  static const char* d[16] = {
    "N","NNE","NE","ENE","E","ESE","SE","SSE",
    "S","SSW","SW","WSW","W","WNW","NW","NNW"};
  if (deg < 0) return "--";
  return d[static_cast<int>((deg + 11) / 22.5f) % 16];
}
}  // namespace

// Layout:
//   Title "ANTENNA"
//   Left: antenna-switch card (active port(s) / GROUNDED) — when switch enabled
//   Right: rotator compass dial + azimuth readout — when rotator connected
// When only one is present it takes the full width / centre.
void AntennaSlide::draw(TFT_eSPI& tft, const UberSDRSnapshot& snap, bool full) {
  (void)full;
  slideTitle(tft, "ANTENNA");

  const bool hasSwitch  = snap.antSwitchEnabled;
  const bool hasRotator = snap.rotatorEnabled && snap.rotatorConnected;

  const int16_t margin = 10;
  const int16_t topY   = kContentY + 42;
  const int16_t areaH  = kContentH - 46;

  // ── Antenna switch (left column or full width) ──
  if (hasSwitch) {
    const int16_t cardW = hasRotator ? (kScreenW / 2 - margin - 4) : (kScreenW - 2 * margin);
    drawCard(tft, margin, topY, cardW, areaH);
    const int16_t cx = margin + cardW / 2;
    drawText(tft, "ANTENNA SWITCH", cx, topY + 12, 2, kColMuted, TC_DATUM);

    String value;
    uint16_t col;
    if (snap.antSwitchGrounded) {
      value = "GROUNDED";
      col = kColWarn;
    } else if (snap.antSwitchLabels.length()) {
      value = snap.antSwitchLabels;
      col = kColGood;
    } else {
      value = "none";
      col = kColMuted;
    }
    drawText(tft, value, cx, topY + areaH / 2, 4, col, MC_DATUM);
  }

  // ── Rotator compass (right column or centred) ──
  if (hasRotator) {
    const int16_t colX = hasSwitch ? (kScreenW / 2 + 4) : margin;
    const int16_t colW = hasSwitch ? (kScreenW / 2 - margin - 4) : (kScreenW - 2 * margin);
    drawCard(tft, colX, topY, colW, areaH);
    drawText(tft, "ROTATOR", colX + colW / 2, topY + 12, 2, kColMuted, TC_DATUM);

    const int16_t cx = colX + colW / 2;
    const int16_t cy = topY + areaH / 2 + 4;
    // Scale the dial to the card: leave ~14px margin for cardinal labels and
    // room for the title (top) and azimuth readout (bottom).  Larger when the
    // rotator has the whole slide to itself.
    int16_t r = (colW / 2) - 24;
    const int16_t rMaxByHeight = (areaH / 2) - 26;
    if (r > rMaxByHeight) r = rMaxByHeight;
    if (r < 30) r = 30;
    if (r > 78) r = 78;
    drawCompass(tft, cx, cy, r, snap.rotatorAzimuth);

    // Azimuth readout below the dial.
    char azBuf[16];
    if (snap.rotatorAzimuth >= 0) {
      snprintf(azBuf, sizeof(azBuf), "%d\xB0 %s", snap.rotatorAzimuth,
               compass16(snap.rotatorAzimuth));
    } else {
      snprintf(azBuf, sizeof(azBuf), "--");
    }
    drawText(tft, azBuf, cx, topY + areaH - 12, 2, kColText, MC_DATUM);
  }
}
