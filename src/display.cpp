#include "display.h"

#include <Preferences.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <time.h>

#include "connectivity.h"
#include "notifications.h"
#include "settings.h"
#include "slideshow.h"
#include "ubersdr_api.h"

// ── Hardware ──────────────────────────────────────────────────────────────────

namespace {

// Constructed with (320,240) so rotation 0 = landscape on the CYD ILI9341.
TFT_eSPI tft(320, 240);
constexpr uint8_t kRotation = 0;   // landscape, USB on the right

// ── Touch — XPT2046_Touchscreen library on VSPI ──────────────────────────────
// The CYD wires the XPT2046 to pins 25/32/39/33 (CLK/MOSI/MISO/CS).
// The XPT2046_Touchscreen library handles the SPI protocol correctly.
// We use VSPI (same bus IDs as the calibration sketch) and setRotation(2).

constexpr uint8_t kTouchCs  = 33;
constexpr uint8_t kTouchClk = 25;
constexpr uint8_t kTouchMiso= 39;
constexpr uint8_t kTouchMosi= 32;

// Do NOT pass an IRQ pin — GPIO 36 on the CYD has no pull-up and floats,
// making tirqTouched() unreliable. We gate on z-pressure only.
SPIClass            touchSpi(VSPI);
XPT2046_Touchscreen touchscreen(kTouchCs);   // no IRQ pin

// ── Affine-transform calibration ─────────────────────────────────────────────
// Converts raw XPT2046 coordinates to screen pixels using:
//   x_screen = alphaX * x_touch + betaX  * y_touch + deltaX
//   y_screen = alphaY * x_touch + betaY  * y_touch + deltaY
// Solved from 3 non-collinear calibration points via Cramer's rule.
// Stored in NVS as 6 floats (key "touchcal6").

constexpr char kCalNs[]  = "ubersdr";
constexpr char kCalKey[] = "touchcal6";

struct TouchCal {
  float alphaX, betaX, deltaX;
  float alphaY, betaY, deltaY;
};

TouchCal g_cal      = {0, 0, 0, 0, 0, 0};
bool     g_calValid = false;

void loadCalibration() {
  Preferences prefs;
  prefs.begin(kCalNs, true);
  const size_t len = prefs.getBytesLength(kCalKey);
  if (len == sizeof(TouchCal)) {
    prefs.getBytes(kCalKey, &g_cal, sizeof(TouchCal));
    g_calValid = true;
    Serial.printf("Touch cal loaded: aX=%.4f bX=%.4f dX=%.1f aY=%.4f bY=%.4f dY=%.1f\n",
                  g_cal.alphaX, g_cal.betaX, g_cal.deltaX,
                  g_cal.alphaY, g_cal.betaY, g_cal.deltaY);
  }
  prefs.end();
}

void saveCalibration() {
  Preferences prefs;
  prefs.begin(kCalNs, false);
  prefs.putBytes(kCalKey, &g_cal, sizeof(TouchCal));
  prefs.end();
  Serial.printf("Touch cal saved: aX=%.4f bX=%.4f dX=%.1f aY=%.4f bY=%.4f dY=%.1f\n",
                g_cal.alphaX, g_cal.betaX, g_cal.deltaX,
                g_cal.alphaY, g_cal.betaY, g_cal.deltaY);
}

// ── Touch reading ─────────────────────────────────────────────────────────────

// Minimum z-pressure to accept as a real touch.
// XPT2046 returns ~0 when untouched; real touches are typically 500-3000.
constexpr int16_t kMinPressure = 800;

bool getTouchPoint(uint16_t& sx, uint16_t& sy) {
  TS_Point p = touchscreen.getPoint();
  if (p.z < kMinPressure) return false;

  // Average 4 samples for stability
  int32_t xSum = p.x, ySum = p.y;
  uint8_t n = 1;
  for (uint8_t i = 0; i < 3; ++i) {
    TS_Point p2 = touchscreen.getPoint();
    if (p2.z >= kMinPressure) { xSum += p2.x; ySum += p2.y; ++n; }
  }

  const float tx = static_cast<float>(xSum) / static_cast<float>(n);
  const float ty = static_cast<float>(ySum) / static_cast<float>(n);

  const float fx = g_cal.alphaX * tx + g_cal.betaX * ty + g_cal.deltaX;
  const float fy = g_cal.alphaY * tx + g_cal.betaY * ty + g_cal.deltaY;

  sx = static_cast<uint16_t>(constrain(static_cast<int>(fx), 0, tft.width()  - 1));
  sy = static_cast<uint16_t>(constrain(static_cast<int>(fy), 0, tft.height() - 1));

  static uint32_t lastDbgMs = 0;
  if (millis() - lastDbgMs > 200) {
    lastDbgMs = millis();
    Serial.printf("touch raw(%.0f,%.0f) -> screen(%u,%u)\n", tx, ty, sx, sy);
  }
  return true;
}

// ── Calibration wizard ────────────────────────────────────────────────────────
// Collects 3 non-collinear taps and solves the 6-coefficient affine system.

void drawCross(int16_t x, int16_t y, uint16_t color) {
  tft.drawFastHLine(x - 15, y, 31, color);
  tft.drawFastVLine(x, y - 15, 31, color);
  tft.drawCircle(x, y, 6, color);
}

// Wait for a stable tap; returns averaged raw touch coordinates.
// Relies purely on z-pressure (no IRQ pin — GPIO 36 floats on CYD).
bool waitForTap(int32_t& tx, int32_t& ty) {
  // Wait for any current touch to release (z < threshold, 3s timeout)
  Serial.println("waitForTap: waiting for release...");
  uint32_t t0 = millis();
  while (millis() - t0 < 3000) {
    TS_Point p = touchscreen.getPoint();
    if (p.z < kMinPressure) break;
    Serial.printf("  release-wait: z=%d\n", p.z);
    delay(20);
  }
  delay(200);

  // Wait for a new press (60s timeout)
  Serial.println("waitForTap: waiting for press...");
  t0 = millis();
  while (millis() - t0 < 60000) {
    TS_Point p = touchscreen.getPoint();
    if (p.z < kMinPressure) {
      delay(20);
      continue;
    }
    // Real touch confirmed — collect samples for 400ms while held
    Serial.printf("  press detected: z=%d xy=(%d,%d)\n", p.z, p.x, p.y);
    int32_t xSum = p.x, ySum = p.y;
    uint8_t n = 1;
    const uint32_t t1 = millis();
    while (millis() - t1 < 400) {
      TS_Point p2 = touchscreen.getPoint();
      if (p2.z >= kMinPressure) { xSum += p2.x; ySum += p2.y; ++n; }
      delay(20);
    }
    tx = xSum / n;
    ty = ySum / n;
    Serial.printf("  averaged: n=%d raw(%ld,%ld)\n", n, tx, ty);
    // Wait for release (3s timeout)
    t0 = millis();
    while (millis() - t0 < 3000) {
      TS_Point pr = touchscreen.getPoint();
      if (pr.z < kMinPressure) break;
      delay(10);
    }
    delay(150);
    return true;
  }
  Serial.println("waitForTap: timeout");
  return false;
}

void runCalibration() {
  const int16_t W = tft.width();
  const int16_t H = tft.height();
  constexpr int16_t kM = 30;

  // Intro screen
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("TOUCH CALIBRATION", W / 2, H / 2 - 30, 4);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Tap the cross on each screen", W / 2, H / 2 + 10, 2);
  tft.drawString("3 points required", W / 2, H / 2 + 30, 2);
  delay(3000);

  // 3 calibration points (non-collinear): TL, TR, BL
  const int16_t scrX[3] = {kM,                        static_cast<int16_t>(W - kM), kM                       };
  const int16_t scrY[3] = {kM,                        kM,                           static_cast<int16_t>(H - kM)};
  const char*   names[3]= {"TOP-LEFT", "TOP-RIGHT", "BOTTOM-LEFT"};

  int32_t tx[3], ty[3];

  for (uint8_t i = 0; i < 3; ++i) {
    tft.fillScreen(TFT_BLACK);
    // Draw a large, obvious cross
    drawCross(scrX[i], scrY[i], TFT_YELLOW);
    // Label in centre
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(String("Point ") + (i + 1) + " of 3", W / 2, H / 2 - 16, 2);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString(String("TAP ") + names[i], W / 2, H / 2 + 4, 4);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString("(tap the yellow cross)", W / 2, H / 2 + 36, 2);
    Serial.printf("Cal: waiting for %s (screen %d,%d)...\n", names[i], scrX[i], scrY[i]);

    if (!waitForTap(tx[i], ty[i])) {
      // Timeout — show message and retry from beginning
      Serial.printf("Cal: timeout on %s, restarting\n", names[i]);
      tft.fillScreen(TFT_BLACK);
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(TFT_ORANGE, TFT_BLACK);
      tft.drawString("Timed out — retrying", W / 2, H / 2, 2);
      delay(2000);
      i = 0;  // restart loop (will be incremented to 0 by for-loop)
      --i;
      continue;
    }
    Serial.printf("Cal: %s touch(%ld,%ld)\n", names[i], tx[i], ty[i]);
    // Flash the cross green to confirm
    drawCross(scrX[i], scrY[i], TFT_GREEN);
    delay(300);
  }

  // Solve affine system using Cramer's rule.
  // For X: scrX[i] = alphaX*tx[i] + betaX*ty[i] + deltaX
  // For Y: scrY[i] = alphaY*tx[i] + betaY*ty[i] + deltaY
  const float t0x = static_cast<float>(tx[0]);
  const float t0y = static_cast<float>(ty[0]);
  const float t1x = static_cast<float>(tx[1]);
  const float t1y = static_cast<float>(ty[1]);
  const float t2x = static_cast<float>(tx[2]);
  const float t2y = static_cast<float>(ty[2]);
  const float s0x = static_cast<float>(scrX[0]);
  const float s1x = static_cast<float>(scrX[1]);
  const float s2x = static_cast<float>(scrX[2]);
  const float s0y = static_cast<float>(scrY[0]);
  const float s1y = static_cast<float>(scrY[1]);
  const float s2y = static_cast<float>(scrY[2]);

  // det of [[t0x,t0y,1],[t1x,t1y,1],[t2x,t2y,1]]
  const float det = t0x*(t1y - t2y) - t0y*(t1x - t2x) + (t1x*t2y - t2x*t1y);
  if (fabsf(det) < 1.0f) {
    Serial.println("Cal: degenerate matrix, calibration failed");
    g_calValid = false;
    return;
  }

  // Solve for X coefficients
  g_cal.alphaX = (s0x*(t1y-t2y) - t0y*(s1x-s2x) + (s1x*t2y-s2x*t1y)) / det;
  g_cal.betaX  = (t0x*(s1x-s2x) - s0x*(t1x-t2x) + (t1x*s2x-t2x*s1x)) / det;
  g_cal.deltaX = (t0x*(t1y*s2x-t2y*s1x) - t0y*(t1x*s2x-t2x*s1x) + s0x*(t1x*t2y-t2x*t1y)) / det;

  // Solve for Y coefficients
  g_cal.alphaY = (s0y*(t1y-t2y) - t0y*(s1y-s2y) + (s1y*t2y-s2y*t1y)) / det;
  g_cal.betaY  = (t0x*(s1y-s2y) - s0y*(t1x-t2x) + (t1x*s2y-t2x*s1y)) / det;
  g_cal.deltaY = (t0x*(t1y*s2y-t2y*s1y) - t0y*(t1x*s2y-t2x*s1y) + s0y*(t1x*t2y-t2x*t1y)) / det;

  saveCalibration();
  g_calValid = true;

  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("Calibration saved!", W / 2, H / 2, 2);
  delay(1000);
}

// ── Colours ───────────────────────────────────────────────────────────────────

constexpr uint16_t kBg    = TFT_BLACK;
constexpr uint16_t kPanel = 0x2945;
constexpr uint16_t kText  = TFT_WHITE;
constexpr uint16_t kMuted = TFT_LIGHTGREY;
constexpr uint16_t kAccent= TFT_CYAN;
constexpr uint16_t kWarn  = TFT_ORANGE;
constexpr uint16_t kGood  = TFT_GREEN;
constexpr uint16_t kKeyBg = 0x2104;
constexpr uint16_t kKeyFg = TFT_WHITE;
constexpr uint16_t kKeyHL = 0x051F;

// ── Render throttle ───────────────────────────────────────────────────────────

constexpr uint32_t kRenderIntervalMs = 500;
constexpr uint32_t kTouchDebounceMs  = 200;

// ── Screen state machine ──────────────────────────────────────────────────────

enum Screen : uint8_t {
  kScreenStatus = 0,
  kScreenWifiSsid,
  kScreenWifiPass,
  kScreenWifiConnecting,
  kScreenSlideShow,
};

Screen   g_screen       = kScreenStatus;
bool     g_dirty        = true;
uint32_t g_lastRenderMs = 0;
bool     g_lastWifiConnected = false;   // rising-edge detect for auto-navigate

// Cached status-screen field values (dirty-field tracking to avoid fillScreen).
String g_lastWifiLine;
String g_lastNtpLine;
String g_lastTimeLine;
String g_lastUptimeLine;

// Touch state
bool     g_touchWasDown      = false;
uint32_t g_lastTouchActionMs = 0;

// Notification toast overlay state (see notifications.h contract).
bool g_toastWasActive = false;

// Auto-brightness: how often to re-sample the LDR and update the backlight.
constexpr uint32_t kAutoBrightIntervalMs = 2000;
uint32_t g_lastAutoBrightMs = 0;

// ── Screen-off / wake state ───────────────────────────────────────────────────
bool     g_screenOff      = false;
uint32_t g_lastActivityMs = 0;   // reset on touch or wakeDisplay()
uint8_t  g_blDuty         = 255; // last non-zero backlight duty (restored on wake)

// Write a duty value to the backlight LEDC channel.
void setBacklightDuty(uint8_t duty) {
#ifdef TFT_BL
  constexpr uint8_t  kBlChannel    = 0;
  constexpr uint32_t kBlFrequency  = 5000;
  constexpr uint8_t  kBlResolution = 8;
  ledcSetup(kBlChannel, kBlFrequency, kBlResolution);
  ledcAttachPin(TFT_BL, kBlChannel);
  ledcWrite(kBlChannel, duty);
#endif
}

// Render the toast overlay and repaint the underlying screen when the last
// toast is dismissed.  Only called for screens that may show toasts.
// underlayRepainted = the caller just repainted the screen beneath the toast.
void renderToastOverlay(TFT_eSPI& tft, bool onSlideshow, bool underlayRepainted) {
  const bool active = notificationsRender(tft, underlayRepainted);
  if (g_toastWasActive && !active) {
    if (onSlideshow) slideshowForceRedraw();
    else             g_dirty = true;
  }
  g_toastWasActive = active;
}

// ── On-screen keyboard ────────────────────────────────────────────────────────

constexpr uint8_t kKbCols = 10;
constexpr uint8_t kKbRows = 4;

const char kKbLower[kKbRows][kKbCols + 1] = {
  "qwertyuiop",
  "asdfghjkl ",
  "zxcvbnm   ",   // cols 7-9 → Shift / Bksp / OK
  "1234567890",
};
const char kKbUpper[kKbRows][kKbCols + 1] = {
  "QWERTYUIOP",
  "ASDFGHJKL ",
  "ZXCVBNM   ",
  "!@#$%^&*()",
};

constexpr uint8_t kKbSpecialStart = 7;

bool    g_kbShift    = false;
String  g_inputSsid;
String  g_inputPass;
String* g_activeInput = nullptr;

// Keyboard occupies the bottom 140 px of the 240-px-tall landscape screen.
constexpr int16_t kKbX  = 0;
constexpr int16_t kKbY  = 100;
constexpr int16_t kKbW  = 320;
constexpr int16_t kKbH  = 140;
constexpr int16_t kKeyW = kKbW / kKbCols;   // 32
constexpr int16_t kKeyH = kKbH / kKbRows;   // 35

// ── Drawing helpers ───────────────────────────────────────────────────────────

void drawCentered(const String& text, int16_t y, uint8_t font,
                  uint16_t color = kText) {
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(color, kBg);
  tft.drawString(text, tft.width() / 2, y, font);
}

void drawLeft(const String& text, int16_t x, int16_t y, uint8_t font,
              uint16_t color = kText) {
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(color, kBg);
  tft.drawString(text, x, y, font);
}

// Repaint a left-aligned field only when its value changes.
void drawStatusField(String& last, const String& value,
                     int16_t x, int16_t y, uint8_t font, uint16_t color,
                     int16_t clearW = 312) {
  if (value == last) return;
  tft.fillRect(x, y - 1, clearW, tft.fontHeight(font) + 2, kBg);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(color, kBg);
  tft.drawString(value, x, y, font);
  last = value;
}

// ── Keyboard drawing ──────────────────────────────────────────────────────────

void drawKey(uint8_t col, uint8_t row, const String& label,
             uint16_t bg = kKeyBg, uint16_t fg = kKeyFg) {
  const int16_t x = kKbX + col * kKeyW;
  const int16_t y = kKbY + row * kKeyH;
  tft.fillRoundRect(x + 2, y + 2, kKeyW - 4, kKeyH - 4, 4, bg);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(fg, bg);
  tft.drawString(label, x + kKeyW / 2, y + kKeyH / 2, 2);
}

void drawKeyboard() {
  tft.fillRect(kKbX, kKbY, kKbW, kKbH, kBg);

  const char (*layout)[kKbCols + 1] = g_kbShift ? kKbUpper : kKbLower;

  for (uint8_t row = 0; row < kKbRows; ++row) {
    for (uint8_t col = 0; col < kKbCols; ++col) {
      if (row == 2 && col >= kKbSpecialStart) {
        const uint8_t idx = col - kKbSpecialStart;
        if (idx == 0) drawKey(col, row, g_kbShift ? "SHF" : "shf",
                              g_kbShift ? kKeyHL : kKeyBg);
        else if (idx == 1) drawKey(col, row, "<--", kWarn, kBg);
        else if (idx == 2) drawKey(col, row, "OK",  kGood, kBg);
      } else {
        const char c = layout[row][col];
        if (c == ' ' || c == '\0') continue;
        drawKey(col, row, String(c));
      }
    }
  }

  // Wide space bar below the 4 key rows.
  const int16_t spaceY = kKbY + kKbRows * kKeyH;
  tft.fillRoundRect(kKbX + 60, spaceY + 2, 200, 20, 4, kKeyBg);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(kKeyFg, kKeyBg);
  tft.drawString("SPACE", kKbX + 160, spaceY + 12, 2);
}

// ── Input screen ──────────────────────────────────────────────────────────────

void drawInputScreen(const String& title, const String& prompt,
                     const String& value, bool maskValue) {
  tft.fillRect(0, 0, tft.width(), kKbY, kBg);
  drawCentered(title, 4, 4, kAccent);
  tft.drawFastHLine(0, 30, tft.width(), kPanel);
  drawLeft(prompt + ":", 8, 36, 2, kMuted);

  tft.drawRect(8, 54, tft.width() - 16, 28, kPanel);
  tft.fillRect(9, 55, tft.width() - 18, 26, 0x0841);

  String display = maskValue ? String(value.length(), '*') : value;
  display += "_";
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(kText, 0x0841);
  while (display.length() > 1 &&
         tft.textWidth(display, 2) > tft.width() - 24) {
    display = display.substring(1);
  }
  tft.drawString(display, 14, 68, 2);
  drawLeft("Tap keys below. OK to confirm.", 8, 86, 1, kMuted);
}

// ── Status screen ─────────────────────────────────────────────────────────────

void drawStatusScreen(const SystemSnapshot& snap, bool fullRedraw) {
  if (fullRedraw) {
    tft.fillScreen(kBg);
    drawCentered("UberSDR", 4, 4, kAccent);
    tft.drawFastHLine(0, 28, tft.width(), kPanel);
    tft.drawFastHLine(0, 116, tft.width(), kPanel);
    drawLeft("Tap header for overview slides",     8, 122, 2, kAccent);
    drawLeft("Tap lower half to configure WiFi",   8, 142, 2, kMuted);
    drawLeft("Hold BOOT 5s to factory reset",      8, 162, 1, kMuted);
    drawLeft("Or: UberSDR-Setup AP -> 192.168.4.1",8, 178, 1, kMuted);
    g_lastWifiLine   = "";
    g_lastNtpLine    = "";
    g_lastTimeLine   = "";
    g_lastUptimeLine = "";
  }

  const String wifiLine = snap.wifiConnected
      ? ("WiFi: " + snap.localIp)
      : "WiFi: not connected";
  drawStatusField(g_lastWifiLine, wifiLine, 8, 34, 2,
                  snap.wifiConnected ? kGood : kWarn);

  const String ntpLine = snap.timeValid ? "NTP: synced" : "NTP: waiting";
  drawStatusField(g_lastNtpLine, ntpLine, 8, 54, 2,
                  snap.timeValid ? kGood : kWarn);

  String timeLine = "--:--:-- UTC";
  if (snap.timeValid) {
    char buf[16];
    tm utcTm;
    gmtime_r(&snap.epoch, &utcTm);
    strftime(buf, sizeof(buf), "%H:%M:%S UTC", &utcTm);
    timeLine = String(buf);
  }
  drawStatusField(g_lastTimeLine, timeLine, 8, 74, 2, kText);

  char upBuf[24];
  snprintf(upBuf, sizeof(upBuf), "Up: %luh %02lum %02lus",
           static_cast<unsigned long>(snap.uptimeSeconds / 3600),
           static_cast<unsigned long>((snap.uptimeSeconds % 3600) / 60),
           static_cast<unsigned long>(snap.uptimeSeconds % 60));
  drawStatusField(g_lastUptimeLine, String(upBuf), 8, 94, 2, kMuted);
}

// ── Connecting screen ─────────────────────────────────────────────────────────

void drawConnectingScreen() {
  tft.fillScreen(kBg);
  drawCentered("Connecting to WiFi...", 80,  4, kAccent);
  drawCentered(g_inputSsid,            120,  2, kText);
  drawCentered("Please wait",          150,  2, kMuted);
}

// ── Keyboard touch handler ────────────────────────────────────────────────────

void handleKeyboardTouch(uint16_t tx, uint16_t ty) {
  if (!g_activeInput) return;

  // Space bar strip below the 4 key rows.
  const int16_t spaceY = kKbY + kKbRows * kKeyH;
  if (ty >= static_cast<uint16_t>(spaceY) &&
      ty <  static_cast<uint16_t>(spaceY + 24) &&
      tx >= 60 && tx < 260) {
    if (g_activeInput->length() < 64) *g_activeInput += ' ';
    g_dirty = true;
    return;
  }

  if (ty < static_cast<uint16_t>(kKbY) ||
      ty >= static_cast<uint16_t>(kKbY + kKbRows * kKeyH)) return;

  const uint8_t col = static_cast<uint8_t>(
      constrain(static_cast<int>((tx - kKbX) / kKeyW), 0, kKbCols - 1));
  const uint8_t row = static_cast<uint8_t>((ty - kKbY) / kKeyH);
  if (row >= kKbRows) return;

  if (row == 2 && col >= kKbSpecialStart) {
    const uint8_t idx = col - kKbSpecialStart;
    if (idx == 0) {
      g_kbShift = !g_kbShift;
      drawKeyboard();
    } else if (idx == 1) {
      if (g_activeInput->length() > 0) {
        *g_activeInput = g_activeInput->substring(0, g_activeInput->length() - 1);
        g_dirty = true;
      }
    } else if (idx == 2) {
      if (g_screen == kScreenWifiSsid) {
        g_screen      = kScreenWifiPass;
        g_kbShift     = false;
        g_activeInput = &g_inputPass;
        g_dirty       = true;
      } else if (g_screen == kScreenWifiPass) {
        AppSettings settings = getSettings();
        settings.wifiSsid     = g_inputSsid;
        settings.wifiPassword = g_inputPass;
        saveSettings(settings);
        g_screen = kScreenWifiConnecting;
        g_dirty  = true;
        drawConnectingScreen();
        reconnectWifi();
      }
    }
    return;
  }

  const char (*layout)[kKbCols + 1] = g_kbShift ? kKbUpper : kKbLower;
  const char c = layout[row][col];
  if (c == ' ' || c == '\0') return;

  if (g_activeInput->length() < 64) {
    *g_activeInput += c;
    if (g_kbShift) {
      g_kbShift = false;
      drawKeyboard();
    }
    g_dirty = true;
  }
}

// ── Touch dispatch ────────────────────────────────────────────────────────────

void handleTouch() {
  uint16_t tx, ty;
  const bool touched = getTouchPoint(tx, ty);
  const uint32_t nowMs = millis();

  if (touched && !g_touchWasDown &&
      nowMs - g_lastTouchActionMs >= kTouchDebounceMs) {
    g_lastTouchActionMs = nowMs;

    // If the screen is off, this tap wakes it — consume the tap entirely.
    if (g_screenOff) {
      g_screenOff = false;
      g_lastActivityMs = nowMs;
      setBacklightDuty(g_blDuty);
      g_touchWasDown = touched;
      return;
    }

    // Screen is on — record activity and dispatch normally.
    g_lastActivityMs = nowMs;

    switch (g_screen) {
      case kScreenStatus:
        if (ty < 116) {
          // Top half (header area) → switch to overview slideshow
          g_screen = kScreenSlideShow;
          slideshowActivate();
          g_dirty  = true;
        } else {
          // Bottom half → WiFi setup wizard
          g_screen      = kScreenWifiSsid;
          g_inputSsid   = getSettings().wifiSsid;
          g_inputPass   = "";
          g_kbShift     = false;
          g_activeInput = &g_inputSsid;
          g_dirty       = true;
        }
        break;

      case kScreenSlideShow:
        // Top-edge tap (header, 24 px) returns to status screen; a tap while a
        // notification toast is up dismisses it; otherwise let the slideshow
        // handle prev/next/pause via its own left/centre/right zones.
        if (ty < 24) {
          g_screen = kScreenStatus;
          g_dirty  = true;
        } else if (notificationsActive()) {
          notificationsDismiss();
        } else {
          slideshowHandleTouch(tx, ty);
        }
        break;

      case kScreenWifiSsid:
      case kScreenWifiPass:
        handleKeyboardTouch(tx, ty);
        break;

      case kScreenWifiConnecting:
        g_screen = kScreenStatus;
        g_dirty  = true;
        break;
    }
  }

  g_touchWasDown = touched;
}

// ── LDR helpers ───────────────────────────────────────────────────────────────

constexpr uint8_t  kLdrPin      = 34;
constexpr uint8_t  kLdrSamples  = 8;
constexpr uint32_t kLdrSettleUs = 50;
// Practical ADC range with ADC_0db attenuation: 0 (bright) … ~750 (dark).
constexpr int      kLdrDarkMax  = 750;

// Returns ambient light as 0 (dark) … 100 (bright).
int readLdrPercent() {
  int32_t sum = 0;
  for (uint8_t i = 0; i < kLdrSamples; ++i) {
    sum += analogRead(kLdrPin);
    delayMicroseconds(kLdrSettleUs);
  }
  const int raw     = static_cast<int>(sum / kLdrSamples);
  const int clamped = constrain(raw, 0, kLdrDarkMax);
  return static_cast<int>(map(clamped, 0, kLdrDarkMax, 100, 0));
}

}  // namespace

// ── Public API ────────────────────────────────────────────────────────────────

void displayBegin() {
  // Configure the front LDR (GPIO 34) ADC attenuation before any analogRead.
  // ADC_0db = 0-1.1 V input range, giving the best resolution for the LDR's
  // low-voltage swing (~0 raw in daylight, ~750 raw fully covered).
  pinMode(kLdrPin, INPUT);
  analogSetPinAttenuation(kLdrPin, ADC_0db);

  tft.init();
  tft.setRotation(kRotation);
  tft.fillScreen(kBg);

  // Initialise XPT2046_Touchscreen on VSPI
  touchSpi.begin(kTouchClk, kTouchMiso, kTouchMosi, kTouchCs);
  touchscreen.begin(touchSpi);
  touchscreen.setRotation(0);   // raw unrotated; affine cal handles orientation

  // Load or run calibration
  loadCalibration();
  if (!g_calValid) {
    runCalibration();
  }

  applyDisplaySettings();
  g_dirty = true;

  // Wire the slideshow to the shared TFT instance.
  slideshowBegin(tft);

  // Ignore any touch lingering from the calibration wizard for 1.5 s.
  g_lastTouchActionMs = millis() + 1500;
  g_touchWasDown = false;

  // Start the inactivity timer now so the screen doesn't blank immediately.
  g_lastActivityMs = millis();
}

void displayUpdate(const SystemSnapshot& snap) {
  handleTouch();

  // ── Screen-off timeout & auto-brightness poll ─────────────────────────────
  {
    const uint32_t nowMs = millis();
    const AppSettings& s = getSettings();

    // Screen-off timeout: blank the backlight after inactivity.
    if (!g_screenOff && s.screenOffTimeoutSec > 0 &&
        nowMs - g_lastActivityMs >= static_cast<uint32_t>(s.screenOffTimeoutSec) * 1000UL) {
      g_screenOff = true;
      setBacklightDuty(0);
    }

    // Auto-brightness: re-sample LDR periodically (only when screen is on).
    if (!g_screenOff && s.autoBrightness &&
        nowMs - g_lastAutoBrightMs >= kAutoBrightIntervalMs) {
      g_lastAutoBrightMs = nowMs;
      const uint8_t maxBright = constrain(s.brightnessPercent,
                                          static_cast<uint8_t>(5),
                                          static_cast<uint8_t>(100));
      const int ldrPct = readLdrPercent();
      const int scaled = map(ldrPct, 0, 100, 5, maxBright);
      uint8_t duty = static_cast<uint8_t>(
          map(constrain(scaled, 5, 100), 0, 100, 0, 255));
#if TFT_BACKLIGHT_ON == LOW
      duty = 255 - duty;
#endif
      g_blDuty = duty;
      setBacklightDuty(duty);
    }
  }

  // Auto-navigate to the overview slideshow on the rising edge of WiFi becoming
  // connected, regardless of which screen we're currently on (handles both the
  // manual WiFi-setup flow and the auto-connect-on-boot case).
  if (snap.wifiConnected && !g_lastWifiConnected) {
    if (g_screen == kScreenWifiConnecting || g_screen == kScreenStatus) {
      g_screen = kScreenSlideShow;
      slideshowActivate();
      g_dirty  = true;
    }
  }
  g_lastWifiConnected = snap.wifiConnected;

  const uint32_t nowMs = millis();

  // Slideshow screen manages its own rendering (chrome clock tick, auto-advance,
  // and content refresh on new poll data).  It bypasses the status-screen throttle.
  if (g_screen == kScreenSlideShow) {
    if (g_dirty) {
      // Screen just became active — force a full chrome+content redraw.
      slideshowActivate();
      g_dirty = false;
    }

    // ── API reachability toasts (fired on Core 1 to keep notifications thread-safe)
    if (g_apiWentDown) {
      g_apiWentDown = false;
      notificationsPush("API", "Cannot reach UberSDR server");
    }
    if (g_apiWentUp) {
      g_apiWentUp = false;
      notificationsPush("API", "UberSDR server recovered");
    }

    // Hold auto-advance while a toast overlays the content (clock keeps ticking).
    slideshowTick(!notificationsActive());
    const bool contentRepainted = slideshowDraw();
    renderToastOverlay(tft, true, contentRepainted);
    return;
  }

  // The status screen updates on a timer (clock ticks every second).
  // All other screens only redraw when g_dirty is set by user interaction.
  const bool timeForStatusUpdate =
      (g_screen == kScreenStatus) &&
      (nowMs - g_lastRenderMs >= kRenderIntervalMs);

  if (!g_dirty && !timeForStatusUpdate) return;

  if (timeForStatusUpdate) g_lastRenderMs = nowMs;

  const bool fullRedraw = g_dirty;
  g_dirty = false;

  switch (g_screen) {
    case kScreenStatus:
      drawStatusScreen(snap, fullRedraw);
      renderToastOverlay(tft, false, fullRedraw);
      break;
    case kScreenWifiSsid:
      drawInputScreen("WiFi Setup", "SSID", g_inputSsid, false);
      drawKeyboard();
      break;
    case kScreenWifiPass:
      drawInputScreen("WiFi Setup", "Password", g_inputPass, true);
      drawKeyboard();
      break;
    case kScreenWifiConnecting:
      drawConnectingScreen();
      break;
  }
}

void applyDisplaySettings() {
  const AppSettings& settings = getSettings();

  uint8_t brightness = constrain(settings.brightnessPercent,
                                 static_cast<uint8_t>(5),
                                 static_cast<uint8_t>(100));

  if (settings.autoBrightness) {
    // Scale the manual brightness setting as the maximum; dim proportionally
    // to ambient light (bright room → full max, dark room → 5 % floor).
    const int ldrPct = readLdrPercent();                    // 0=dark … 100=bright
    const int scaled = map(ldrPct, 0, 100, 5, brightness); // 5 % floor
    brightness = static_cast<uint8_t>(constrain(scaled, 5, 100));
  }

  uint8_t duty = static_cast<uint8_t>(map(brightness, 0, 100, 0, 255));
#if TFT_BACKLIGHT_ON == LOW
  duty = 255 - duty;
#endif
  g_blDuty = duty;   // cache so wake can restore it
  if (!g_screenOff) {
    setBacklightDuty(duty);
  }

  g_dirty = true;
}

void wakeDisplay() {
  g_lastActivityMs = millis();
  if (g_screenOff) {
    g_screenOff = false;
    setBacklightDuty(g_blDuty);
    // Force a full redraw so the content is fresh when the screen comes back on.
    g_dirty = true;
    if (g_screen == kScreenSlideShow) slideshowForceRedraw();
  }
}

void requestDisplayRedraw() {
  g_dirty = true;
}

void displayShowMessage(const String& title, const String& subtitle) {
  tft.fillScreen(kBg);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(kAccent, kBg);
  tft.drawString(title,    tft.width() / 2, tft.height() / 2 - 20, 4);
  tft.setTextColor(kMuted, kBg);
  tft.drawString(subtitle, tft.width() / 2, tft.height() / 2 + 20, 2);
}
