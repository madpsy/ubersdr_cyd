#include "slide_ranking.h"

namespace {

// Format a large integer with comma thousands separator into buf.
void fmtNum(char* buf, int n) {
  if (n >= 1000000) {
    snprintf(buf, 12, "%d.%dM", n / 1000000, (n % 1000000) / 100000);
  } else if (n >= 1000) {
    snprintf(buf, 12, "%d,%03d", n / 1000, n % 1000);
  } else {
    snprintf(buf, 12, "%d", n);
  }
}

// Colour for a rank value: green for podium, cyan for top-10, white otherwise.
uint16_t rankColor(int rank) {
  return (rank <= 3) ? kColGood : (rank <= 10) ? kColAccent : kColText;
}

// Draw a combined PSK card showing both spots rank and DXCC rank.
// Layout: header, then two centred stacks (label / rank / per-day count) so the
// card works at any column width.
void drawPskCard(TFT_eSPI& tft, int16_t x, int16_t y, int16_t w, int16_t h,
                 int spotsRank, int spotsDay, int dxccRank, int dxccDay) {
  drawCard(tft, x, y, w, h);
  const int16_t cx = x + w / 2;

  // Header — full name only when it fits the card width.
  const char* hdr =
      (tft.textWidth("PSKREPORTER", 2) <= w - 12) ? "PSKREPORTER" : "PSK";
  drawText(tft, hdr, cx, y + 6, 2, kColMuted, TC_DATUM);

  // Two sections: Spots and DXCC.
  struct { const char* label; int rank; int day; } rows[2] = {
    { "SPOTS", spotsRank, spotsDay },
    { "DXCC",  dxccRank,  dxccDay  },
  };

  const int16_t secH = (h - 24) / 2;
  for (int i = 0; i < 2; ++i) {
    const int16_t sy = y + 24 + i * secH;

    drawText(tft, rows[i].label, cx, sy + 2, 2, kColMuted, TC_DATUM);

    if (rows[i].rank > 0) {
      char rbuf[8]; snprintf(rbuf, sizeof(rbuf), "#%d", rows[i].rank);
      drawText(tft, rbuf, cx, sy + 20, 4, rankColor(rows[i].rank), TC_DATUM);

      if (rows[i].day > 0) {
        char nbuf[12]; fmtNum(nbuf, rows[i].day);
        char dbuf[16]; snprintf(dbuf, sizeof(dbuf), "%s/d", nbuf);
        drawText(tft, dbuf, cx, sy + 48, 2, kColMuted, TC_DATUM);
      }
    } else {
      drawText(tft, "--", cx, sy + 24, 2, kColMuted, TC_DATUM);
    }

    // Separator between sections.
    if (i == 0) {
      tft.drawFastHLine(x + 4, sy + secH - 2, w - 8, kColCardHi);
    }
  }
}

// Draw the WSPR panel (3 time-window rows) in the given box.
void drawWsprPanel(TFT_eSPI& tft, int16_t x, int16_t y, int16_t w, int16_t h,
                   const UberSDRSnapshot& snap) {
  drawCard(tft, x, y, w, h);
  const int16_t cx = x + w / 2;

  // Header.
  drawText(tft, "WSPR LIVE", cx, y + 6, 2, kColMuted, TC_DATUM);

  // Three rows: 24h / Today / Yesterday.
  struct { const char* label; int rank; int uniq; } rows[3] = {
    { "24h",   snap.wsprRank24h,   snap.wsprUnique24h   },
    { "Today", snap.wsprRankToday, snap.wsprUniqueToday },
    { "Yest",  snap.wsprRankYest,  snap.wsprUniqueYest  },
  };

  // Each row is two lines: label (left) + unique count (right), then a
  // centred rank badge — nothing shares a line with the wide font-4 badge.
  const int16_t rowH = (h - 24) / 3;
  for (int i = 0; i < 3; ++i) {
    const int16_t ry = y + 22 + i * rowH;

    drawText(tft, rows[i].label, x + 8, ry + 2, 2, kColMuted, TL_DATUM);

    if (rows[i].rank > 0) {
      if (rows[i].uniq > 0) {
        char ubuf[12]; fmtNum(ubuf, rows[i].uniq);
        drawText(tft, ubuf, x + w - 8, ry + 2, 2, kColMuted, TR_DATUM);
      }
      char rbuf[8]; snprintf(rbuf, sizeof(rbuf), "#%d", rows[i].rank);
      drawText(tft, rbuf, cx, ry + 17, 4, rankColor(rows[i].rank), TC_DATUM);
    } else {
      drawText(tft, "--", cx, ry + 20, 2, kColMuted, TC_DATUM);
    }

    // Separator (not after last row).
    if (i < 2) {
      tft.drawFastHLine(x + 4, ry + rowH - 1, w - 8, kColCardHi);
    }
  }
}

// Draw the RBN skimmer card in the given box.
// Shows: rank badge, total skimmers, spot count.
void drawRbnCard(TFT_eSPI& tft, int16_t x, int16_t y, int16_t w, int16_t h,
                 int rank, int total, int spots) {
  drawCard(tft, x, y, w, h);
  const int16_t cx = x + w / 2;

  // Header.
  drawText(tft, "RBN", cx, y + 6, 2, kColMuted, TC_DATUM);

  if (rank > 0) {
    // Centred stack: rank badge, skimmer total, spot count.
    char rbuf[8]; snprintf(rbuf, sizeof(rbuf), "#%d", rank);
    drawText(tft, rbuf, cx, y + 30, 4, rankColor(rank), TC_DATUM);

    if (total > 0) {
      char buf[16];
      snprintf(buf, sizeof(buf), "of %d", total);
      drawText(tft, buf, cx, y + 62, 2, kColMuted, TC_DATUM);
      drawText(tft, "skimmers", cx, y + 78, 2, kColMuted, TC_DATUM);
    }

    // Spot count at bottom.
    if (spots > 0) {
      char nbuf[12]; fmtNum(nbuf, spots);
      drawText(tft, nbuf, cx, y + h - 38, 2, kColText, TC_DATUM);
      drawText(tft, "spots", cx, y + h - 22, 2, kColMuted, TC_DATUM);
    }
  } else {
    drawText(tft, "--", cx, y + h / 2 - 8, 2, kColMuted, TC_DATUM);
  }
}

}  // namespace

void RankingSlide::draw(TFT_eSPI& tft, const UberSDRSnapshot& snap, bool full) {
  (void)full;
  slideTitle(tft, "RANKING");

  const bool hasPsk  = snap.pskValid;
  const bool hasWspr = snap.wsprValid;
  const bool hasRbn  = snap.rbnValid;

  const int16_t margin = 8;
  const int16_t gap    = 6;
  const int16_t top    = kContentY + 36;
  const int16_t avH    = kContentH - 40;

  const int count = (hasPsk ? 1 : 0) + (hasWspr ? 1 : 0) + (hasRbn ? 1 : 0);

  if (count == 3) {
    // ── All three: equal thirds ────────────────────────────────────────────
    const int16_t colW = (kScreenW - 2 * margin - 2 * gap) / 3;
    drawPskCard(tft, margin, top, colW, avH,
                snap.pskSpotsRank, snap.pskSpotsDay,
                snap.pskDxccRank,  snap.pskDxccDay);
    drawWsprPanel(tft, margin + colW + gap, top, colW, avH, snap);
    drawRbnCard(tft, margin + 2 * (colW + gap), top, colW, avH,
                snap.rbnRank, snap.rbnTotal, snap.rbnSpots);

  } else if (count == 2) {
    // ── Two panels: left half + right half ────────────────────────────────
    const int16_t halfW = (kScreenW - 2 * margin - gap) / 2;
    int16_t leftX  = margin;
    int16_t rightX = margin + halfW + gap;

    if (hasPsk && hasWspr) {
      drawPskCard(tft, leftX, top, halfW, avH,
                  snap.pskSpotsRank, snap.pskSpotsDay,
                  snap.pskDxccRank,  snap.pskDxccDay);
      drawWsprPanel(tft, rightX, top, halfW, avH, snap);
    } else if (hasPsk && hasRbn) {
      drawPskCard(tft, leftX, top, halfW, avH,
                  snap.pskSpotsRank, snap.pskSpotsDay,
                  snap.pskDxccRank,  snap.pskDxccDay);
      drawRbnCard(tft, rightX, top, halfW, avH,
                  snap.rbnRank, snap.rbnTotal, snap.rbnSpots);
    } else {  // hasWspr && hasRbn
      drawWsprPanel(tft, leftX, top, halfW, avH, snap);
      drawRbnCard(tft, rightX, top, halfW, avH,
                  snap.rbnRank, snap.rbnTotal, snap.rbnSpots);
    }

  } else {
    // ── Single source: full width ──────────────────────────────────────────
    const int16_t fullW = kScreenW - 2 * margin;
    if (hasPsk) {
      drawPskCard(tft, margin, top, fullW, avH,
                  snap.pskSpotsRank, snap.pskSpotsDay,
                  snap.pskDxccRank,  snap.pskDxccDay);
    } else if (hasWspr) {
      drawWsprPanel(tft, margin, top, fullW, avH, snap);
    } else if (hasRbn) {
      drawRbnCard(tft, margin, top, fullW, avH,
                  snap.rbnRank, snap.rbnTotal, snap.rbnSpots);
    }
  }
}
