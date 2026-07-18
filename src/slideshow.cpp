#include "slideshow.h"

#include <TFT_eSPI.h>
#include <WiFi.h>
#include <time.h>

#include "slide_antenna.h"
#include "slide_base.h"
#include "slide_bands.h"
#include "slide_gpsdo.h"
#include "slide_load.h"
#include "slide_ranking.h"
#include "slide_spaceweather.h"
#include "slide_spectrum.h"
#include "slide_users.h"
#include "slide_weather.h"
#include "ubersdr_api.h"

namespace {

// ── Slide registry ────────────────────────────────────────────────────────────
// Add new slides here.  Order defines the rotation order.
UsersSlide        s_users;
LoadSlide         s_load;
BandsSlide        s_bands;
SpaceWeatherSlide s_space;
WeatherSlide      s_weather;
AntennaSlide      s_antenna;
SpectrumSlide     s_spectrum;
RankingSlide      s_ranking;
GpsdoSlide        s_gpsdo;

Slide* const kSlides[] = {
  &s_users,
  &s_load,
  &s_bands,
  &s_ranking,
  &s_spectrum,
  &s_space,
  &s_weather,
  &s_antenna,
  &s_gpsdo,
};
constexpr int kSlideCount = sizeof(kSlides) / sizeof(kSlides[0]);

// ── Tunables ──────────────────────────────────────────────────────────────────
constexpr uint32_t kAutoAdvanceMs = 8000;   // 8 s per slide
constexpr uint32_t kClockTickMs   = 1000;   // header clock refresh

// ── State ─────────────────────────────────────────────────────────────────────
TFT_eSPI* s_tft = nullptr;

int      s_current       = 0;        // index into kSlides
int      s_page          = 0;        // current page within a multi-page slide
bool     s_paused        = false;    // auto-advance paused by centre tap
bool     s_fullRedraw    = true;     // draw chrome + content next frame
uint32_t s_slideStartMs  = 0;        // millis() when current slide began
uint32_t s_pageStartMs   = 0;        // millis() when current page began
uint32_t s_lastClockMs   = 0;        // last header-clock refresh
uint32_t s_lastDataMs    = 0;        // lastSuccessMs seen at last content draw
bool     s_drewPlaceholder = false;  // current slide was drawn without data
String   s_lastClockStr;             // last drawn HH:MM:SS to avoid flicker
String   s_lastUsersStr;             // last drawn "N/max" user count

// ── Slide selection helpers ───────────────────────────────────────────────────
bool slideVisible(int idx, const UberSDRSnapshot& snap) {
  return kSlides[idx]->hasData(snap);
}

// Find the next visible slide index in the given direction (+1 / -1).
// Falls back to the current index if none are visible.
int nextVisible(int from, int dir, const UberSDRSnapshot& snap) {
  for (int i = 0; i < kSlideCount; ++i) {
    from = (from + dir + kSlideCount) % kSlideCount;
    if (slideVisible(from, snap)) return from;
  }
  return from;  // nothing visible — stay put
}

// ── Chrome (header + footer) ──────────────────────────────────────────────────
void drawHeader(const UberSDRSnapshot& snap, bool full) {
  TFT_eSPI& tft = *s_tft;

  if (full) {
    tft.fillRect(0, 0, kScreenW, kHeaderH, kColPanel);
    tft.drawFastHLine(0, kHeaderH - 1, kScreenW, kColCardHi);

    // Accent dot + brand (left), followed by the instance callsign in amber.
    tft.fillCircle(10, kHeaderH / 2, 4, kColAccent);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(kColText, kColPanel);
    tft.drawString("UberSDR", 20, kHeaderH / 2, 2);
    int16_t brandRight = 20 + tft.textWidth("UberSDR", 2);
    if (snap.callsign.length()) {
      tft.setTextColor(kColAccent2, kColPanel);   // amber
      tft.drawString(snap.callsign, brandRight + 8, kHeaderH / 2, 2);
    }

    // Slide counter (centre-right, before the clock).
    char cnt[12];
    snprintf(cnt, sizeof(cnt), "%d/%d", s_current + 1, kSlideCount);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(kColMuted, kColPanel);
    tft.drawString(cnt, kScreenW / 2 + 40, kHeaderH / 2, 2);

    s_lastClockStr = "";   // force clock repaint
    s_lastUsersStr = "";   // force users-chip repaint
  }

  // ── Users chip — live "N/max" with a tiny person glyph (left of the slide
  // counter).  Redrawn in place whenever the count changes.
  String usersStr;
  if (snap.usersValid) {
    char ub[16];
    if (snap.maxSessions > 0) {
      snprintf(ub, sizeof(ub), "%d/%d", snap.userCount, snap.maxSessions);
    } else {
      snprintf(ub, sizeof(ub), "%d", snap.userCount);
    }
    usersStr = ub;
  }
  if (usersStr != s_lastUsersStr || full) {
    const int16_t ux = 132;              // icon left edge
    const int16_t cy = kHeaderH / 2;
    tft.fillRect(ux - 2, 2, 188 - ux, kHeaderH - 4, kColPanel);
    if (usersStr.length()) {
      const uint16_t uc =
          (snap.userCount >= snap.maxSessions && snap.maxSessions > 0)
              ? kColWarn : kColGood;
      // Person glyph: head + shoulders.
      tft.fillCircle(ux + 4, cy - 4, 2, uc);
      tft.fillRoundRect(ux, cy - 1, 9, 6, 3, uc);
      tft.setTextDatum(ML_DATUM);
      tft.setTextColor(uc, kColPanel);
      tft.drawString(usersStr, ux + 13, cy, 2);
    }
    s_lastUsersStr = usersStr;
  }

  // ── Live clock (right) ──
  // Show the UberSDR instance's LOCAL time when we know its UTC offset
  // (receiver.timezone_offset, DST-adjusted minutes); otherwise fall back to
  // UTC.  We add the offset to the epoch and format with gmtime so no local
  // TZ database is needed on the ESP32.
  String clockStr = "--:--:--";
  const time_t now = time(nullptr);
  if (now > 1704067200) {   // valid time
    time_t shown = now;
    if (snap.tzValid) shown += static_cast<time_t>(snap.tzOffsetMinutes) * 60;
    tm t;
    gmtime_r(&shown, &t);
    char buf[12];
    strftime(buf, sizeof(buf), "%H:%M:%S", &t);
    clockStr = buf;
  }

  if (clockStr != s_lastClockStr || full) {
    // Clear the clock region then redraw.
    const int16_t clkW = 96;
    tft.fillRect(kScreenW - clkW - 6, 2, clkW + 4, kHeaderH - 4, kColPanel);
    tft.setTextDatum(MR_DATUM);
    const uint16_t clkCol = (WiFi.status() == WL_CONNECTED) ? kColAccent : kColMuted;
    tft.setTextColor(clkCol, kColPanel);
    tft.drawString(clockStr, kScreenW - 6, kHeaderH / 2, 4);
    s_lastClockStr = clockStr;
  }
}

void drawFooter(bool full) {
  if (!full) return;
  TFT_eSPI& tft = *s_tft;
  const int16_t y = kScreenH - kFooterH;
  tft.fillRect(0, y, kScreenW, kFooterH, kColPanel);
  tft.drawFastHLine(0, y, kScreenW, kColCardHi);

  tft.setTextColor(kColMuted, kColPanel);
  tft.setTextDatum(ML_DATUM);
  tft.drawString("< prev", 8, y + kFooterH / 2, 2);

  tft.setTextDatum(MC_DATUM);
  tft.drawString(s_paused ? "paused" : "auto", kScreenW / 2, y + kFooterH / 2, 2);

  tft.setTextDatum(MR_DATUM);
  tft.drawString("next >", kScreenW - 8, y + kFooterH / 2, 2);
}

void clearContent() {
  s_tft->fillRect(kContentX, kContentY, kContentW, kContentH, kColBg);
}

}  // namespace

// ── Public API ────────────────────────────────────────────────────────────────

void slideshowBegin(TFT_eSPI& tft) {
  s_tft = &tft;
  s_current = 0;
  s_paused = false;
  s_fullRedraw = true;
  s_slideStartMs = millis();
  s_lastClockMs = 0;
  s_lastDataMs = 0;
  s_lastClockStr = "";
  s_lastUsersStr = "";
}

void slideshowActivate() {
  const UberSDRSnapshot& snap = getUberSDRSnapshot();
  // Land on the first slide that has data.
  if (!slideVisible(s_current, snap)) {
    s_current = nextVisible(s_current, +1, snap);
  }
  s_page = 0;
  s_fullRedraw = true;
  s_paused = false;
  s_slideStartMs = millis();
  s_pageStartMs = s_slideStartMs;
}

bool slideshowDraw() {
  if (!s_tft) return false;
  const UberSDRSnapshot& snap = getUberSDRSnapshot();

  const bool full = s_fullRedraw;
  bool contentPainted = false;

  // Content is drawn ONCE per slide activation (on `full`).  We deliberately do
  // not repaint on every incoming poll: the 5-endpoint round-robin bumps the
  // snapshot ~every 2 s, and clearing+redrawing the content area each time
  // caused a visible flicker.  A slide captures the latest data when it becomes
  // active; since each slide is shown for 8 s and a full poll cycle is ~10 s,
  // the on-screen values are always fresh enough.  The header clock still
  // ticks live (drawn in-place without clearing) via slideshowTick().
  //
  // Exception: if the slide was drawn while its section had no data yet, allow a
  // single content refresh once data arrives so the placeholder is replaced
  // without waiting for the next rotation.
  if (full) {
    clearContent();
    drawHeader(snap, true);
    drawFooter(true);
    kSlides[s_current]->setPage(s_page);
    kSlides[s_current]->draw(*s_tft, snap, true);
    s_lastDataMs = snap.lastSuccessMs;
    s_drewPlaceholder = !kSlides[s_current]->hasData(snap);
    contentPainted = true;
  } else if (s_drewPlaceholder && kSlides[s_current]->hasData(snap)) {
    // One-shot upgrade from placeholder to real data.
    clearContent();
    kSlides[s_current]->setPage(s_page);
    kSlides[s_current]->draw(*s_tft, snap, false);
    s_lastDataMs = snap.lastSuccessMs;
    s_drewPlaceholder = false;
    contentPainted = true;
  }

  s_fullRedraw = false;
  return contentPainted;
}

void slideshowTick(bool allowAdvance) {
  if (!s_tft) return;
  const uint32_t nowMs = millis();

  // Header clock tick (independent of auto-advance).
  if (nowMs - s_lastClockMs >= kClockTickMs) {
    s_lastClockMs = nowMs;
    drawHeader(getUberSDRSnapshot(), false);
  }

  if (!allowAdvance || s_paused) return;

  const UberSDRSnapshot& snap = getUberSDRSnapshot();

  // Pages within the current slide share the kAutoAdvanceMs window.  Each page
  // gets an equal slice (minimum 2 s) so a multi-page slide still cycles at a
  // readable pace while the whole slide fits roughly in one auto-advance tick.
  const int pages = kSlides[s_current]->pageCount(snap);
  if (pages > 1) {
    uint32_t perPage = kAutoAdvanceMs / static_cast<uint32_t>(pages);
    if (perPage < 2000) perPage = 2000;
    if (nowMs - s_pageStartMs >= perPage) {
      if (s_page + 1 < pages) {
        // Next page of the same slide — redraw content only.
        s_page++;
        s_pageStartMs = nowMs;
        s_fullRedraw = true;   // full content redraw for the new page
        return;
      }
      // Last page done — fall through to advance to the next slide.
    } else {
      return;   // still within this page's time slice
    }
  } else if (nowMs - s_slideStartMs < kAutoAdvanceMs) {
    return;     // single-page slide, still within its window
  }

  // Advance to the next visible slide.
  const int next = nextVisible(s_current, +1, snap);
  if (next != s_current) {
    s_current = next;
    s_fullRedraw = true;
  }
  s_page = 0;
  s_slideStartMs = nowMs;
  s_pageStartMs = nowMs;
}

void slideshowForceRedraw() {
  s_fullRedraw = true;
}

bool slideshowHandleTouch(uint16_t x, uint16_t y) {
  (void)y;
  const UberSDRSnapshot& snap = getUberSDRSnapshot();

  if (x < kScreenW / 3) {
    // Left third → previous slide.
    s_current = nextVisible(s_current, -1, snap);
    s_page = 0;
    s_fullRedraw = true;
    s_slideStartMs = millis();
    s_pageStartMs = s_slideStartMs;
  } else if (x > (kScreenW * 2) / 3) {
    // Right third → next page (if any) else next slide.
    const int pages = kSlides[s_current]->pageCount(snap);
    if (pages > 1 && s_page + 1 < pages) {
      s_page++;
    } else {
      s_current = nextVisible(s_current, +1, snap);
      s_page = 0;
    }
    s_fullRedraw = true;
    s_slideStartMs = millis();
    s_pageStartMs = s_slideStartMs;
  } else {
    // Centre → toggle pause and repaint footer state.
    s_paused = !s_paused;
    s_slideStartMs = millis();
    s_pageStartMs = s_slideStartMs;
    drawFooter(true);
  }
  return true;
}
