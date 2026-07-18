#pragma once

#include <Arduino.h>

class TFT_eSPI;

// ── Slideshow controller ──────────────────────────────────────────────────────
//
// Owns the ordered list of overview Slides, the header/footer chrome, the
// auto-advance timer, and touch handling.  The display layer delegates to this
// module whenever the slideshow screen is active.
//
// Adding a slide: create src/slide_<name>.{h,cpp} deriving from Slide, then add
// one instance to the table in slideshow.cpp (kSlides[]).

// Initialise the slideshow (call once, after displayBegin()).
// The slideshow renders onto the shared TFT instance owned by display.cpp.
void slideshowBegin(TFT_eSPI& tft);

// Called when the slideshow screen becomes active — forces a full redraw and
// resets the auto-advance timer.
void slideshowActivate();

// Render the current slide.  Redraws chrome + content on demand:
//   - full frame when the slide changed or slideshowActivate() was called
//   - live-value refresh on the 1 s header clock tick and on new poll data
// Call every displayUpdate() while the slideshow screen is active.
// Returns true when the content area was (re)painted this call, so overlays
// (notification toasts) know they were wiped and must repaint.
bool slideshowDraw();

// Advance timer + data-change detection.  Call every displayUpdate().
// Pass allowAdvance=false to keep the header clock ticking while holding the
// current slide/page (used while a notification toast overlays the content).
void slideshowTick(bool allowAdvance = true);

// Request a full chrome+content repaint on the next slideshowDraw() without
// resetting the auto-advance timer or pause state (unlike slideshowActivate).
// Used after a notification toast is dismissed to restore the slide beneath.
void slideshowForceRedraw();

// Touch handling — tap left third = previous, right third = next, middle =
// pause/resume auto-advance.  Coordinates are screen pixels.
// Returns true if the touch was consumed by the slideshow.
bool slideshowHandleTouch(uint16_t x, uint16_t y);
