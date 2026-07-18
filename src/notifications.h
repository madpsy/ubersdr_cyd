#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

// ── Notification toasts ───────────────────────────────────────────────────────
//
// Small FIFO of messages received via the /notify webhook endpoint (see
// setup_portal.cpp).  The display layer renders the head of the queue as a
// toast card overlaid on the bottom of the screen for a few seconds each.
//
// Contract with the display layer:
//   - call notificationsRender() every frame while a screen that may show
//     toasts is visible; it paints only when something changed
//   - while notificationsActive() the slideshow should hold auto-advance so
//     the slide underneath doesn't repaint over the toast
//   - when notificationsRender() transitions from true to false the caller
//     must force a full repaint of the underlying screen

// Queue a notification (called from the web server).  Empty messages are
// ignored; over-long messages are truncated; control characters stripped.
void notificationsPush(const String& channel, const String& message);

// True while at least one notification is queued/being shown.
bool notificationsActive();

// Skip the currently shown toast (advance to the next, if any).
void notificationsDismiss();

// Draw/refresh the toast overlay.  Paints only when the toast changed, or when
// underlayRepainted is true (the caller repainted the screen beneath, wiping
// the toast).  Returns true while a toast is on screen.
bool notificationsRender(TFT_eSPI& tft, bool underlayRepainted = false);
