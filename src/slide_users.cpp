#include "slide_users.h"

#include <time.h>

namespace {

// Format a throughput value given in kbps: "847k" or "4.2M".
void fmtKbps(char* buf, size_t n, int kbps) {
  if (kbps >= 1000) {
    snprintf(buf, n, "%d.%dM", kbps / 1000, (kbps % 1000) / 100);
  } else {
    snprintf(buf, n, "%dk", kbps);
  }
}

// Format a duration in seconds as a compact human string:
//   < 60 s  → "47s"
//   < 1 h   → "12m 34s"
//   >= 1 h  → "1h 02m"
void fmtDuration(char* buf, size_t n, long secs) {
  if (secs < 0) secs = 0;
  if (secs < 60) {
    snprintf(buf, n, "%lds", secs);
  } else if (secs < 3600) {
    snprintf(buf, n, "%ldm %02lds", secs / 60, secs % 60);
  } else {
    snprintf(buf, n, "%ldh %02ldm", secs / 3600, (secs % 3600) / 60);
  }
}

// ── Page 0: summary (existing layout) ────────────────────────────────────────
void drawSummaryPage(TFT_eSPI& tft, const UberSDRSnapshot& snap) {
  const int max = snap.maxSessions > 0 ? snap.maxSessions : 1;
  const int used = snap.userCount;
  const float ratio = static_cast<float>(used) / static_cast<float>(max);
  const uint16_t col = colorForRatio(ratio);

  // ── Big count "N / MAX" using the 7-segment font for the numbers ──
  char curBuf[8];
  snprintf(curBuf, sizeof(curBuf), "%d", used);
  char maxBuf[12];
  snprintf(maxBuf, sizeof(maxBuf), "/ %d", max);

  const int16_t cx = kScreenW / 2;
  const int16_t bigY = kContentY + 36;

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

// ── Page 1: per-user list ─────────────────────────────────────────────────────
// Layout: 2 columns × up to 8 users per column = 16 users max.
// Each user occupies 2 sub-rows:
//   Row 0: country name (left, kColText/kColMuted) + duration (right, kColAccent)
//   Row 1: "14.200 MHz  USB" (left, kColMuted) — omitted when frequencyHz == 0
// A thin separator line is drawn between users.
//
// Geometry (content area y=24..220, title+underline ends at y=57):
//   listY = kContentY + 38 = 62  (clear of title underline)
//   listH = 220 - 62 = 158 px
//   userH = 8 + 8 + 2 + 1 = 19 px per user (row0 + row1 + gap + separator)
//   maxRows = 158 / 19 = 8 users per column
void drawListPage(TFT_eSPI& tft, const UberSDRSnapshot& snap) {
  const int n = snap.userDetailCount;
  if (n == 0) return;

  const time_t now = time(nullptr);

  const int16_t margin   = 8;
  const int16_t listY    = kContentY + 38;                 // y=62, clear of title
  const int16_t listH    = kContentY + kContentH - listY;  // 158 px
  const int16_t userH    = 19;                             // px per user slot
  const int     maxPerCol = listH / userH;                 // 8

  // Vertical column separator.
  tft.drawFastVLine(kScreenW / 2, listY, listH, kColCardHi);

  constexpr int16_t kColInner = 5;   // gap between divider line and column content

  for (int i = 0; i < n && i < kMaxUserDetails; ++i) {
    const int col = i % 2;
    const int row = i / 2;
    if (row >= maxPerCol) break;

    const int16_t uy  = listY + row * userH;               // top of this user slot
    // Left column: starts at margin, ends kColInner before the divider.
    // Right column: starts kColInner after the divider, ends at screen edge - margin.
    const int16_t lx  = (col == 0)
                        ? margin
                        : (kScreenW / 2) + kColInner;
    const int16_t rx  = (col == 0)
                        ? (kScreenW / 2) - kColInner
                        : (kScreenW - margin);

    const UserDetail& ud = snap.userDetails[i];

    // ── Sub-row 0: country (left) + duration (right) ──
    const bool unknown = (ud.country[0] == '\0' ||
                          strcmp(ud.country, "Unknown") == 0);
    const uint16_t nameCol = unknown ? kColMuted : kColText;
    drawText(tft, unknown ? "Unknown" : ud.country, lx, uy, 1, nameCol, TL_DATUM);

    if (ud.connectedSince > 0 && now > ud.connectedSince) {
      char durBuf[16];
      fmtDuration(durBuf, sizeof(durBuf),
                  static_cast<long>(now - ud.connectedSince));
      drawText(tft, durBuf, rx, uy, 1, kColAccent, TR_DATUM);
    }

    // ── Sub-row 1: frequency + mode (left, muted) ──
    if (ud.frequencyHz > 0) {
      // Convert Hz → MHz with 3 decimal places: 7150000 → "7.150 MHz"
      const uint32_t mhzInt  = ud.frequencyHz / 1000000;
      const uint32_t mhzFrac = (ud.frequencyHz % 1000000) / 1000;
      char freqBuf[24];
      if (ud.mode[0]) {
        snprintf(freqBuf, sizeof(freqBuf), "%lu.%03lu MHz  %s",
                 (unsigned long)mhzInt, (unsigned long)mhzFrac, ud.mode);
      } else {
        snprintf(freqBuf, sizeof(freqBuf), "%lu.%03lu MHz",
                 (unsigned long)mhzInt, (unsigned long)mhzFrac);
      }
      drawText(tft, freqBuf, lx, uy + 9, 1, kColMuted, TL_DATUM);
    }

    // ── Separator between users (skip after last in column) ──
    const int nextRow = row + 1;
    if (nextRow < maxPerCol && (i + 2) < n) {
      const int16_t sepY = uy + userH - 1;
      tft.drawFastHLine(lx, sepY, rx - lx, kColCardHi);
    }
  }
}

}  // namespace

// Layout (content area y = 24..220):
//   Page 0 — summary (existing)
//     Title            "USERS"                       y≈30
//     Big count        "3 / 20"  centred             y≈60   font 7 (large digits)
//     Sub-label        "users connected"             y≈110
//     Capacity bar     full width, h=14              y≈132
//     Stats row        "17 free"  "56 sess"  "2 bypass"  y≈152
//     Network card     TOTAL / AUDIO / WFALL kbps    y≈172  (compact servers only)
//
//   Page 1 — user list (only when userDetailCount > 0)
//     Title            "USERS"                       y≈30
//     2-column grid    country + duration            rows below title
void UsersSlide::draw(TFT_eSPI& tft, const UberSDRSnapshot& snap, bool full) {
  (void)full;
  slideTitle(tft, "USERS");

  if (page_ == 0) {
    drawSummaryPage(tft, snap);
  } else {
    drawListPage(tft, snap);
  }
}
