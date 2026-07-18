#include "slide_weather.h"

namespace {
// Colour for the OWM "main" condition string (mirrors monitor_display.go).
uint16_t conditionColor(const String& main) {
  String m = main;
  m.toLowerCase();
  if (m == "thunderstorm")            return kColBad;
  if (m == "drizzle" || m == "rain")  return kColAccent;   // blue-ish cyan
  if (m == "snow")                    return kColAccent;
  if (m == "clear")                   return kColWarn;      // sunny amber
  return kColText;                                          // clouds/mist/etc.
}

// Temperature → colour band.
uint16_t tempColor(float c) {
  if (c >= 30) return kColBad;
  if (c >= 20) return kColWarn;
  if (c >= 10) return kColGood;
  return kColAccent;   // cold → cyan
}

// A metric card: label (top), value (centre).
void drawWxCard(TFT_eSPI& tft, int16_t x, int16_t y, int16_t w, int16_t h,
                const char* label, const String& value, uint16_t valColor) {
  drawCard(tft, x, y, w, h);
  const int16_t cx = x + w / 2;
  drawText(tft, label, cx, y + 7, 2, kColMuted, TC_DATUM);
  drawText(tft, value, cx, y + 26, 4, valColor, TC_DATUM);
}
}  // namespace

// Layout:
//   Title "WEATHER"
//   Condition (big, coloured) + temperature (right)   y≈50
//   Location (muted)                                  y≈92
//   Three cards: HUMIDITY / PRESSURE / WIND           y≈112
void WeatherSlide::draw(TFT_eSPI& tft, const UberSDRSnapshot& snap, bool full) {
  (void)full;
  slideTitle(tft, "WEATHER");

  if (!snap.weatherValid) {
    drawText(tft, "waiting for data...", kScreenW / 2, kContentY + 90, 2,
             kColMuted, TC_DATUM);
    return;
  }

  const int16_t margin = 10;

  // ── Condition (left) + temperature (right) ──
  const uint16_t cCol = conditionColor(snap.wxMain);
  // Condition may be long; scroll-free by using font 4 and clipping via width.
  drawText(tft, snap.wxDescription, margin, kContentY + 44, 4, cCol, TL_DATUM);

  // Temperature: the big number uses font 6 (digits only — no letters/degree),
  // so draw the "°C" suffix separately in font 4.  \xB0 is the degree glyph in
  // the TFT_eSPI font codepage (a literal UTF-8 "°" would print as garbage).
  const uint16_t tCol = tempColor(snap.wxTempC);
  char numBuf[8];
  snprintf(numBuf, sizeof(numBuf), "%.0f", snap.wxTempC);
  const char* unit = "\xB0" "C";
  const int16_t unitW = tft.textWidth(unit, 4);
  // Right-align the whole "NN°C" group at the right margin.
  drawText(tft, unit, kScreenW - margin, kContentY + 46, 4, tCol, TR_DATUM);
  drawText(tft, numBuf, kScreenW - margin - unitW - 2, kContentY + 40, 6, tCol,
           TR_DATUM);

  // ── Location ──
  if (snap.wxLocation.length()) {
    drawText(tft, snap.wxLocation, margin, kContentY + 78, 2, kColMuted, TL_DATUM);
  }

  // ── Metric cards ──
  const int16_t gap = 8;
  const int16_t cardW = (kScreenW - 2 * margin - 2 * gap) / 3;
  const int16_t cardY = kContentY + 100;
  const int16_t cardH = 62;

  char hBuf[10]; snprintf(hBuf, sizeof(hBuf), "%d%%", snap.wxHumidity);
  char pBuf[10]; snprintf(pBuf, sizeof(pBuf), "%d", snap.wxPressure);

  // Wind: "NN km/h DIR" — put speed in the card value, direction under it.
  char wBuf[16];
  if (snap.wxWindDir.length()) {
    snprintf(wBuf, sizeof(wBuf), "%d %s", snap.wxWindKmh, snap.wxWindDir.c_str());
  } else {
    snprintf(wBuf, sizeof(wBuf), "%d", snap.wxWindKmh);
  }

  drawWxCard(tft, margin, cardY, cardW, cardH, "HUMIDITY", hBuf, kColAccent);
  drawWxCard(tft, margin + (cardW + gap), cardY, cardW, cardH,
             "hPa", pBuf, kColText);
  // Wind card uses a smaller value font to fit "NN DIR".
  drawCard(tft, margin + 2 * (cardW + gap), cardY, cardW, cardH);
  {
    const int16_t cx = margin + 2 * (cardW + gap) + cardW / 2;
    drawText(tft, "WIND km/h", cx, cardY + 7, 2, kColMuted, TC_DATUM);
    drawText(tft, wBuf, cx, cardY + 28, 4, kColText, TC_DATUM);
  }

  // Gusts (optional) under the wind card.
  if (snap.wxGustKmh > 0) {
    char gBuf[20];
    snprintf(gBuf, sizeof(gBuf), "gusts %d", snap.wxGustKmh);
    drawText(tft, gBuf, kScreenW - margin, cardY + cardH + 4, 2, kColMuted,
             TR_DATUM);
  }
}
