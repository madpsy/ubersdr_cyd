#include "notifications.h"

#include "debug_log.h"
#include "slide_base.h"   // palette, screen geometry, drawText

namespace {

constexpr int      kMaxQueue    = 6;
constexpr uint32_t kToastMs     = 6000;   // how long each toast is shown
constexpr uint16_t kMaxMsgChars = 160;

struct Item {
  String channel;
  String message;
};

Item     g_items[kMaxQueue];
int      g_count        = 0;
uint32_t g_shownSinceMs = 0;   // 0 = head not yet shown
bool     g_dirty        = false;

// Drop the head of the queue.
void popHead() {
  for (int i = 1; i < g_count; ++i) g_items[i - 1] = g_items[i];
  if (g_count > 0) {
    g_items[g_count - 1] = Item{};   // release the Strings
    --g_count;
  }
  g_shownSinceMs = 0;
  g_dirty = true;
}

constexpr int kMaxLines = 3;

// Greedy word-wrap of msg into at most maxLines lines fitting maxW in font 2.
// Returns the number of lines produced; the last line is truncated with ".."
// when the text still doesn't fit.
int wrapLines(TFT_eSPI& tft, const String& msg, int16_t maxW,
              String* lines, int maxLines) {
  String rest = msg;
  int n = 0;
  while (n < maxLines && rest.length()) {
    if (tft.textWidth(rest, 2) <= maxW) {
      lines[n++] = rest;
      rest = "";
      break;
    }
    // Longest prefix that fits.
    unsigned int cut = rest.length();
    while (cut > 1 && tft.textWidth(rest.substring(0, cut), 2) > maxW) --cut;
    // Prefer breaking at a space when one is reasonably close.
    const int sp = rest.substring(0, cut).lastIndexOf(' ');
    unsigned int next = cut;
    if (sp > static_cast<int>(cut) / 2) {
      cut = sp;
      next = cut + 1;   // skip the space
    }
    lines[n++] = rest.substring(0, cut);
    rest = rest.substring(next);
  }
  if (rest.length() && n > 0 && lines[n - 1].length() > 2) {
    lines[n - 1] = lines[n - 1].substring(0, lines[n - 1].length() - 2) + "..";
  }
  return n;
}

int16_t g_lastPaintH = 0;   // height of the previously painted card

void paint(TFT_eSPI& tft) {
  const int16_t x = 6;
  const int16_t w = kScreenW - 12;

  // Wrap first — the card height depends on the line count (1..3 lines).
  String lines[kMaxLines];
  const int n = wrapLines(tft, g_items[0].message, w - 16, lines, kMaxLines);
  const int16_t h = 26 + n * 16;   // 21px header + 16px per line + padding
  const int16_t y = kScreenH - h - 4;

  // If the previous toast's card was taller, blank the strip it no longer
  // covers (the underlying slide is restored properly on final dismissal).
  if (g_lastPaintH > h) {
    tft.fillRect(x, kScreenH - g_lastPaintH - 4, w, g_lastPaintH - h, kColBg);
  }
  g_lastPaintH = h;

  tft.fillRoundRect(x, y, w, h, 6, kColPanel);
  tft.drawRoundRect(x, y, w, h, 6, kColAccent2);

  // Header line: title (left) + queued-count badge (right).
  String hdr = g_items[0].channel;
  hdr.toUpperCase();
  if (hdr.length() == 0) hdr = "NOTIFICATION";
  drawText(tft, hdr, x + 8, y + 5, 2, kColAccent2, TL_DATUM);
  if (g_count > 1) {
    char more[8];
    snprintf(more, sizeof(more), "+%d", g_count - 1);
    drawText(tft, more, x + w - 8, y + 5, 2, kColMuted, TR_DATUM);
  }

  for (int i = 0; i < n; ++i) {
    drawText(tft, lines[i], x + 8, y + 23 + i * 16, 2, kColText, TL_DATUM);
  }
}

}  // namespace

void notificationsPush(const String& channel, const String& message) {
  String msg = message;
  // Control characters (newlines from multi-line messages) become spaces.
  for (unsigned int i = 0; i < msg.length(); ++i) {
    if (static_cast<uint8_t>(msg[i]) < 32) msg.setCharAt(i, ' ');
  }
  msg.trim();
  if (msg.length() == 0) return;
  if (msg.length() > kMaxMsgChars) msg = msg.substring(0, kMaxMsgChars);

  if (g_count == kMaxQueue) popHead();   // full — drop the oldest

  g_items[g_count].channel = channel;
  g_items[g_count].message = msg;
  ++g_count;
  g_dirty = true;   // repaint so the +N badge updates
  debugLogf("notify: [%s] %s (queued=%d)", channel.c_str(), msg.c_str(), g_count);
}

bool notificationsActive() {
  return g_count > 0;
}

void notificationsDismiss() {
  if (g_count > 0) popHead();
}

bool notificationsRender(TFT_eSPI& tft, bool underlayRepainted) {
  if (g_count == 0) {
    g_lastPaintH = 0;   // underlying screen gets a full repaint on dismissal
    return false;
  }

  const uint32_t now = millis();
  if (g_shownSinceMs == 0) {
    g_shownSinceMs = now;
    g_dirty = true;
  } else if (now - g_shownSinceMs >= kToastMs) {
    popHead();
    if (g_count == 0) return false;
    g_shownSinceMs = now;
  }

  // A repaint of the screen beneath wiped the toast — the card region is now
  // slide content, so the shrink-blanking bookkeeping must start over too.
  if (underlayRepainted) {
    g_lastPaintH = 0;
    g_dirty = true;
  }

  if (g_dirty) {
    paint(tft);
    g_dirty = false;
  }
  return true;
}
