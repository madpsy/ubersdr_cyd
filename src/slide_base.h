#pragma once

#include <TFT_eSPI.h>

#include "ubersdr_api.h"

// ── Slide interface ───────────────────────────────────────────────────────────
//
// Each overview slide is a self-contained module implementing the Slide
// interface.  To add a new slide:
//   1. Create src/slide_<name>.{h,cpp} with a class deriving from Slide.
//   2. Register a single instance in slideshow.cpp's slide table.
//
// The slideshow owns the header/footer chrome; a slide only draws into the
// content area (kContentY .. kContentY + kContentH).  Slides must not call
// tft.fillScreen(); the slideshow clears the content area before each draw.

// ── Display geometry (320×240 landscape) ──────────────────────────────────────
constexpr int16_t kScreenW  = 320;
constexpr int16_t kScreenH  = 240;

constexpr int16_t kHeaderH  = 24;                 // top chrome bar
constexpr int16_t kFooterH  = 20;                 // bottom hint bar
constexpr int16_t kContentY = kHeaderH;           // 24
constexpr int16_t kContentH = kScreenH - kHeaderH - kFooterH;   // 196
constexpr int16_t kContentX = 0;
constexpr int16_t kContentW = kScreenW;

// ── Modern colour palette (RGB565) ────────────────────────────────────────────
constexpr uint16_t kColBg      = 0x0000;   // pure black — best contrast on IPS
constexpr uint16_t kColPanel   = 0x10A2;   // dark slate panel
constexpr uint16_t kColCard    = 0x18C3;   // slightly lighter card fill
constexpr uint16_t kColCardHi  = 0x2145;   // card highlight/border
constexpr uint16_t kColText    = 0xFFFF;   // white
constexpr uint16_t kColMuted   = 0x8C71;   // cool grey
constexpr uint16_t kColAccent  = 0x05FF;   // vivid cyan
constexpr uint16_t kColAccent2 = 0xFD20;   // amber accent
constexpr uint16_t kColGood    = 0x2FEA;   // bright green
constexpr uint16_t kColWarn    = 0xFCC0;   // warm amber
constexpr uint16_t kColBad     = 0xF9C4;   // soft red
constexpr uint16_t kColBarTrack= 0x2124;   // progress-bar track

// Map an UberSDR status/quality string to a palette colour.
uint16_t colorForQuality(const String& q);   // EXCELLENT/GOOD/FAIR/POOR
uint16_t colorForStatus(const String& s);    // ok/warning/critical
uint16_t colorForRatio(float ratio);         // 0..1 → green→amber→red

// ── Shared drawing helpers (implemented in slide_base.cpp) ────────────────────
// All coordinates are absolute screen pixels unless noted.

// Section title inside the content area (large cyan label, top-left).
void slideTitle(TFT_eSPI& tft, const String& text);

// A rounded card panel with an optional 1px highlight border.
void drawCard(TFT_eSPI& tft, int16_t x, int16_t y, int16_t w, int16_t h,
              uint16_t fill = kColCard, uint16_t border = kColCardHi);

// Horizontal progress/level bar with rounded ends.
void drawBar(TFT_eSPI& tft, int16_t x, int16_t y, int16_t w, int16_t h,
             float ratio, uint16_t fill, uint16_t track = kColBarTrack);

// Text drawn with a datum, colour and font, transparent background.
void drawText(TFT_eSPI& tft, const String& text, int16_t x, int16_t y,
              uint8_t font, uint16_t color, uint8_t datum = TL_DATUM);

// ── Slide base class ──────────────────────────────────────────────────────────
class Slide {
 public:
  virtual ~Slide() = default;

  // Short label shown in the header (e.g. "USERS").  Also used for logging.
  virtual const char* name() const = 0;

  // Render the slide's content into the content area.  The slideshow has
  // already cleared the content region to kColBg before this call.
  // `full` is true on the first frame after becoming active (draw everything);
  // when false the slide may skip static chrome and only refresh live values.
  virtual void draw(TFT_eSPI& tft, const UberSDRSnapshot& snap, bool full) = 0;

  // Whether this slide has meaningful data to show right now.  Slides with no
  // data are skipped by the slideshow rotation.  Default: always visible.
  virtual bool hasData(const UberSDRSnapshot& snap) const { (void)snap; return true; }

  // ── Multi-page support (optional) ────────────────────────────────────────
  // A slide may render across several pages that share the slide's on-screen
  // time window (e.g. band spectra, 6 per page).  Return >1 to paginate.
  virtual int pageCount(const UberSDRSnapshot& snap) const { (void)snap; return 1; }

  // Set the page to render on the next draw() call.  The slideshow calls this
  // before draw() when advancing pages.  Default: no-op for single-page slides.
  virtual void setPage(int page) { (void)page; }
};
