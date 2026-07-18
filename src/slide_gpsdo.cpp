#include "slide_gpsdo.h"

namespace {

// Colour-code HDOP: <1.0 = excellent (green), <2.0 = fair (amber), else poor (red).
uint16_t hdopColor(float hdop) {
  if (hdop < 1.0f) return kColGood;
  if (hdop < 2.0f) return kColWarn;
  return kColBad;
}

// Extract the time portion of an ISO-8601 UTC string "YYYY-MM-DDTHH:MM:SSZ"
// and write it into buf as "HH:MM:SSZ".  Falls back to the full string on
// parse failure.
void extractTimeZ(char* buf, size_t n, const String& iso) {
  const int tPos = iso.indexOf('T');
  if (tPos >= 0 && (size_t)(iso.length() - tPos) >= 9) {
    snprintf(buf, n, "%sZ", iso.substring(tPos + 1, tPos + 9).c_str());
  } else {
    snprintf(buf, n, "%s", iso.c_str());
  }
}

// Draw a boolean status row: label on the left, OK/-- on the right.
void drawStatusRow(TFT_eSPI& tft, int16_t x, int16_t y, int16_t w, int16_t rowH,
                   const char* label, bool ok) {
  const int16_t cy = y + rowH / 2;
  drawText(tft, label, x + 6, cy, 2, kColMuted, ML_DATUM);
  const uint16_t col = ok ? kColGood : kColBad;
  drawText(tft, ok ? "OK" : "--", x + w - 6, cy, 2, col, MR_DATUM);
}

}  // namespace

// Layout (content area y = 24..220, height = 196):
//
//   Title "GPSDO"                                              y≈24
//
//   Banner  "● LOCKED  PLL 27MHz  11:36:05Z"                  y≈38  (h≈26)
//
//   Left card  "DEVICE"       Right card  "GPS FIX"           y≈70  (h≈126)
//     GPS  OK/--                Fix: GPS 3D
//     PLL  OK/--                Sats: 11 / 14
//     ANT  OK/--                HDOP: 0.88
//     OUT1 OK/--                Alt:  25.4 m
//
void GpsdoSlide::draw(TFT_eSPI& tft, const UberSDRSnapshot& snap, bool full) {
  (void)full;
  slideTitle(tft, "GPSDO");

  const int16_t margin = 8;

  // ── Status banner ────────────────────────────────────────────────────────────
  // Contains: ● LOCKED/UNLOCKED (left) | PLL NNMHz (centre) | HH:MM:SSZ (right)
  // Start below the slideTitle underline (kContentY+6 text, kContentY+33 line).
  const int16_t bannerY = kContentY + 38;
  const int16_t bannerH = 26;
  drawCard(tft, margin, bannerY, kScreenW - 2 * margin, bannerH);

  const bool locked = snap.gpsdoHealthy;
  const uint16_t lockCol = locked ? kColGood : kColBad;
  tft.fillCircle(margin + 14, bannerY + bannerH / 2, 5, lockCol);
  drawText(tft, locked ? "LOCKED" : "UNLOCKED",
           margin + 24, bannerY + bannerH / 2, 2, lockCol, ML_DATUM);

  // Centre: mode + compact frequency, e.g. "PLL 27MHz".
  if (snap.gpsdoMode.length()) {
    char modeBuf[20];
    if (snap.gpsdoFreqHz > 0) {
      snprintf(modeBuf, sizeof(modeBuf), "%s %luMHz",
               snap.gpsdoMode.c_str(),
               (unsigned long)(snap.gpsdoFreqHz / 1000000UL));
    } else {
      snprintf(modeBuf, sizeof(modeBuf), "%s", snap.gpsdoMode.c_str());
    }
    drawText(tft, modeBuf, kScreenW / 2, bannerY + bannerH / 2, 2,
             kColAccent, MC_DATUM);
  }

  // Right: GPS UTC time as "HH:MM:SSZ".
  if (snap.gpsdoUtc.length()) {
    char timeBuf[12];
    extractTimeZ(timeBuf, sizeof(timeBuf), snap.gpsdoUtc);
    drawText(tft, timeBuf, kScreenW - margin - 4, bannerY + bannerH / 2, 2,
             kColMuted, MR_DATUM);
  }

  // ── Two-column cards (fill remaining content area) ───────────────────────────
  const int16_t cardY = bannerY + bannerH + 6;
  const int16_t cardH = kContentY + kContentH - cardY - 4;  // reach near footer
  const int16_t gap   = 4;
  const int16_t cardW = (kScreenW - 2 * margin - gap) / 2;

  // Left card — Device / oscillator status.
  const int16_t lx = margin;
  drawCard(tft, lx, cardY, cardW, cardH);
  drawText(tft, "DEVICE", lx + cardW / 2, cardY + 10, 2, kColMuted, TC_DATUM);

  const int16_t rowH  = (cardH - 22) / 4;
  const int16_t row1Y = cardY + 22;
  drawStatusRow(tft, lx, row1Y,           cardW, rowH, "GPS",  snap.gpsdoGpsLock);
  drawStatusRow(tft, lx, row1Y + rowH,    cardW, rowH, "PLL",  snap.gpsdoPllLock);
  drawStatusRow(tft, lx, row1Y + rowH*2,  cardW, rowH, "ANT",  snap.gpsdoAntennaOk);
  drawStatusRow(tft, lx, row1Y + rowH*3,  cardW, rowH, "OUT1", snap.gpsdoOutput1Enabled);

  // Right card — GPS fix quality.
  const int16_t rx = margin + cardW + gap;
  drawCard(tft, rx, cardY, cardW, cardH);
  drawText(tft, "GPS FIX", rx + cardW / 2, cardY + 10, 2, kColMuted, TC_DATUM);

  const int16_t labelX = rx + 6;
  const int16_t valX   = rx + cardW - 6;
  const int16_t gRowH  = (cardH - 22) / 4;
  int16_t gy = cardY + 22;

  // Fix type + mode: "GPS  3D"
  {
    String fixStr = snap.gpsdoFix;
    if (snap.gpsdoFixMode.length()) fixStr += "  " + snap.gpsdoFixMode;
    const bool hasFix = (snap.gpsdoFixMode == "3D" || snap.gpsdoFixMode == "2D");
    drawText(tft, "Fix", labelX, gy + gRowH / 2, 2, kColMuted, ML_DATUM);
    drawText(tft, fixStr, valX, gy + gRowH / 2, 2,
             hasFix ? kColGood : kColWarn, MR_DATUM);
    gy += gRowH;
  }

  // Sats used / in view.
  {
    char satBuf[16];
    const int inView = snap.gpsdoGpsInView + snap.gpsdoGloInView;
    snprintf(satBuf, sizeof(satBuf), "%d / %d", snap.gpsdoSatsUsed, inView);
    drawText(tft, "Sats", labelX, gy + gRowH / 2, 2, kColMuted, ML_DATUM);
    drawText(tft, satBuf, valX, gy + gRowH / 2, 2, kColText, MR_DATUM);
    gy += gRowH;
  }

  // HDOP.
  {
    char hdopBuf[10];
    snprintf(hdopBuf, sizeof(hdopBuf), "%.2f", snap.gpsdoHdop);
    drawText(tft, "HDOP", labelX, gy + gRowH / 2, 2, kColMuted, ML_DATUM);
    drawText(tft, hdopBuf, valX, gy + gRowH / 2, 2,
             hdopColor(snap.gpsdoHdop), MR_DATUM);
    gy += gRowH;
  }

  // Altitude.
  {
    char altBuf[14];
    snprintf(altBuf, sizeof(altBuf), "%.1f m", snap.gpsdoAltitudeM);
    drawText(tft, "Alt", labelX, gy + gRowH / 2, 2, kColMuted, ML_DATUM);
    drawText(tft, altBuf, valX, gy + gRowH / 2, 2, kColText, MR_DATUM);
  }
}
