#include "slide_health.h"

namespace {

// Map a uint8_t status code to a palette colour.
uint16_t statusColor(uint8_t s) {
  if (s == 2) return kColBad;
  if (s == 1) return kColWarn;
  return kColGood;
}

// Map a uint8_t status code to a short label.
const char* statusLabel(uint8_t s) {
  if (s == 2) return "CRITICAL";
  if (s == 1) return "WARNING";
  return "OK";
}

// Count items that have issues to display (status > 0 AND issueCount > 0).
int countDetailPages(const UberSDRSnapshot& snap) {
  int count = 0;
  for (int i = 0; i < snap.healthItemCount; ++i) {
    if (snap.healthItems[i].status > 0 && snap.healthItems[i].issueCount > 0) {
      count++;
    }
  }
  return count;
}

// Draw page 0: overall banner + two-column dot-grid.
void drawOverviewPage(TFT_eSPI& tft, const UberSDRSnapshot& snap) {
  const int16_t margin = 8;

  // ── Overall status banner ─────────────────────────────────────────────────
  const int16_t bannerY = kContentY + 38;
  const int16_t bannerH = 24;
  const uint16_t ovCol = statusColor(snap.healthOverall);
  drawCard(tft, margin, bannerY, kScreenW - 2 * margin, bannerH);

  tft.fillCircle(margin + 14, bannerY + bannerH / 2, 5, ovCol);

  char ovBuf[24];
  snprintf(ovBuf, sizeof(ovBuf), "OVERALL: %s", statusLabel(snap.healthOverall));
  drawText(tft, ovBuf, margin + 24, bannerY + bannerH / 2, 2, ovCol, ML_DATUM);

  // ── Component dot-grid ────────────────────────────────────────────────────
  const int n = snap.healthItemCount;
  if (n == 0) return;

  const int16_t gridY = bannerY + bannerH + 6;
  const int16_t gridH = kContentY + kContentH - gridY - 2;
  const int16_t colW  = (kScreenW - 2 * margin) / 2;
  const int rows      = (n + 1) / 2;
  const int16_t rowH  = (rows > 0) ? (gridH / rows) : gridH;

  const int16_t dotR  = 4;
  const int16_t textX = dotR * 2 + 4;

  for (int i = 0; i < n; ++i) {
    const int col  = i % 2;
    const int row  = i / 2;
    const int16_t cx = margin + col * colW + dotR + 2;
    const int16_t cy = gridY + row * rowH + rowH / 2;

    tft.fillCircle(cx, cy, dotR, statusColor(snap.healthItems[i].status));
    drawText(tft, snap.healthItems[i].name, cx + textX, cy, 1, kColText, ML_DATUM);
  }
}

// Draw a detail page for one unhealthy item (page >= 1).
// Shows the component name as a sub-title, then each issue string on its own
// line.  Long issue strings are word-wrapped at ~42 chars (font-1, ~6px/char,
// 320px wide with margins → ~50 chars, but we wrap conservatively).
void drawDetailPage(TFT_eSPI& tft, const HealthItem& item) {
  const int16_t margin = 8;

  // Component name banner.
  const int16_t bannerY = kContentY + 38;
  const int16_t bannerH = 24;
  const uint16_t col = statusColor(item.status);
  drawCard(tft, margin, bannerY, kScreenW - 2 * margin, bannerH);
  tft.fillCircle(margin + 14, bannerY + bannerH / 2, 5, col);
  drawText(tft, item.name, margin + 24, bannerY + bannerH / 2, 2, col, ML_DATUM);

  // Issue strings — one per line at font-1 (8px tall + 4px gap = 12px/row).
  const int16_t issueX = margin + 4;
  const int16_t issueW = kScreenW - 2 * margin - 8;
  int16_t y = bannerY + bannerH + 10;
  const int16_t lineH = 13;

  // Max chars that fit in issueW at font-1 (~6px per char).
  const int maxChars = issueW / 6;

  for (int i = 0; i < item.issueCount; ++i) {
    if (y + lineH > kContentY + kContentH) break;   // no more room

    const String& issue = item.issues[i];
    int pos = 0;
    const int len = issue.length();

    // Simple word-wrap: emit up to maxChars per line, breaking at spaces.
    while (pos < len && y + lineH <= kContentY + kContentH) {
      int end = pos + maxChars;
      if (end >= len) {
        end = len;
      } else {
        // Walk back to last space to avoid mid-word break.
        int sp = end;
        while (sp > pos && issue[sp] != ' ') sp--;
        if (sp > pos) end = sp;
      }
      String line = issue.substring(pos, end);
      line.trim();
      drawText(tft, line, issueX, y, 1, kColMuted, TL_DATUM);
      y += lineH;
      pos = end;
      // Skip leading space on next segment.
      while (pos < len && issue[pos] == ' ') pos++;
    }
    // Small gap between issues.
    y += 2;
  }
}

}  // namespace

// ── HealthSlide public API ────────────────────────────────────────────────────

int HealthSlide::pageCount(const UberSDRSnapshot& snap) const {
  // Page 0 = overview grid; pages 1..N = one per unhealthy item with issues.
  return 1 + countDetailPages(snap);
}

int HealthSlide::detailItemIndex(const UberSDRSnapshot& snap, int page) {
  // page 1 → first unhealthy item with issues, page 2 → second, etc.
  int found = 0;
  for (int i = 0; i < snap.healthItemCount; ++i) {
    if (snap.healthItems[i].status > 0 && snap.healthItems[i].issueCount > 0) {
      found++;
      if (found == page) return i;
    }
  }
  return -1;
}

// Layout:
//   Page 0  — "HEALTH" title + overall banner + dot-grid
//   Page 1+ — "HEALTH" title + component banner + issue strings
void HealthSlide::draw(TFT_eSPI& tft, const UberSDRSnapshot& snap, bool full) {
  (void)full;
  slideTitle(tft, "HEALTH");

  if (page_ == 0) {
    drawOverviewPage(tft, snap);
  } else {
    const int idx = detailItemIndex(snap, page_);
    if (idx >= 0) {
      drawDetailPage(tft, snap.healthItems[idx]);
    }
  }
}
