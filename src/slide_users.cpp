#include "slide_users.h"

namespace {

// Format a throughput value given in kbps: "847k" or "4.2M".
void fmtKbps(char* buf, size_t n, int kbps) {
  if (kbps >= 1000) {
    snprintf(buf, n, "%d.%dM", kbps / 1000, (kbps % 1000) / 100);
  } else {
    snprintf(buf, n, "%dk", kbps);
  }
}

}  // namespace

// Layout (content area y = 24..220):
//   Title            "USERS"                       y≈30
//   Big count        "3 / 20"  centred             y≈60   font 7 (large digits)
//   Sub-label        "users connected"             y≈110
//   Capacity bar     full width, h=14              y≈132
//   Stats row        "17 free"  "56 sess"  "2 bypass"  y≈152
//   Network card     TOTAL / AUDIO / WFALL kbps    y≈172  (compact servers only)
void UsersSlide::draw(TFT_eSPI& tft, const UberSDRSnapshot& snap, bool full) {
  (void)full;
  slideTitle(tft, "USERS");

  const int max = snap.maxSessions > 0 ? snap.maxSessions : 1;
  const int used = snap.userCount;
  const float ratio = static_cast<float>(used) / static_cast<float>(max);
  const uint16_t col = colorForRatio(ratio);

  // ── Big count "N / MAX" using the 7-segment font for the numbers ──
  // Compose with the large 7-seg font (font 7) for the current number and a
  // smaller font for "/ MAX" so it reads as a fraction.
  char curBuf[8];
  snprintf(curBuf, sizeof(curBuf), "%d", used);
  char maxBuf[12];
  snprintf(maxBuf, sizeof(maxBuf), "/ %d", max);

  const int16_t cx = kScreenW / 2;
  const int16_t bigY = kContentY + 36;

  // Measure widths to centre the combined "N  / MAX" group.
  const int16_t wCur = tft.textWidth(curBuf, 7);
  const int16_t wMax = tft.textWidth(maxBuf, 4);
  const int16_t gap  = 12;
  const int16_t totalW = wCur + gap + wMax;
  int16_t startX = cx - totalW / 2;

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(col, kColBg);
  tft.drawString(curBuf, startX, bigY, 7);

  tft.setTextColor(kColMuted, kColBg);
  tft.drawString(maxBuf, startX + wCur + gap, bigY + 22, 4);

  // ── Sub-label ──
  drawText(tft, "users connected", cx, kContentY + 86, 2, kColMuted, TC_DATUM);

  // ── Capacity bar ──
  const int16_t barX = 24;
  const int16_t barW = kScreenW - 48;
  const int16_t barY = kContentY + 108;
  drawBar(tft, barX, barY, barW, 14, ratio, col);

  // ── Stats row: free (left) / sessions (centre) / bypass (right) ──
  const int16_t statY = kContentY + 128;
  int free = max - used;
  if (free < 0) free = 0;

  char freeBuf[16];
  snprintf(freeBuf, sizeof(freeBuf), "%d free", free);
  drawText(tft, freeBuf, barX, statY, 2, kColGood, TL_DATUM);

  if (snap.netValid) {
    char sessBuf[16];
    snprintf(sessBuf, sizeof(sessBuf), "%d sess", snap.externalSessions);
    drawText(tft, sessBuf, cx, statY, 2, kColMuted, TC_DATUM);
  }

  char bypBuf[20];
  snprintf(bypBuf, sizeof(bypBuf), "%d bypass", snap.bypassCount);
  const uint16_t bypCol = snap.bypassCount > 0 ? kColAccent : kColMuted;
  drawText(tft, bypBuf, barX + barW, statY, 2, bypCol, TR_DATUM);

  // ── Network throughput card (only when the compact endpoint provided it) ──
  if (!snap.netValid) return;

  const int16_t nx = 8;
  const int16_t nw = kScreenW - 16;
  const int16_t ny = kContentY + 148;
  const int16_t nh = 44;
  drawCard(tft, nx, ny, nw, nh);

  struct { const char* label; int kbps; } cols[3] = {
    { "TOTAL", snap.totalKbps     },
    { "AUDIO", snap.audioKbps     },
    { "WFALL", snap.waterfallKbps },
  };

  const int16_t colW = nw / 3;
  for (int i = 0; i < 3; ++i) {
    const int16_t ccx = nx + colW * i + colW / 2;
    drawText(tft, cols[i].label, ccx, ny + 2, 2, kColMuted, TC_DATUM);
    char vBuf[12];
    fmtKbps(vBuf, sizeof(vBuf), cols[i].kbps);
    drawText(tft, vBuf, ccx, ny + 18, 4,
             i == 0 ? kColAccent : kColText, TC_DATUM);
    // Column separators.
    if (i > 0) {
      tft.drawFastVLine(nx + colW * i, ny + 6, nh - 12, kColCardHi);
    }
  }
}
