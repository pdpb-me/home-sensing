// The Hub screen: layout A of HANDOFF.md section 6, 250x122 px, long edge horizontal.
// tools/hub_screen/screens.js draws the same layout on a PC; change both together.
#pragma once
#include <Seeed_GFX.h>
#include "fonts.h"
#include "art.h"

extern Seeed_GFX display;

// ---------- Thresholds ----------
const float HUMIDITY_DRY = 40;           // below: "dry"
const float HUMIDITY_WET = 60;           // above: "wet"
const float CO2_ALARM_ON_PPM = 1400;     // APRI FINESTRA switches on here...
const float CO2_ALARM_OFF_PPM = 1000;    // ...and off only down here, so it doesn't flicker around one value
const uint16_t STALE_MINUTES = 90;       // older bedroom data is shown with its age

// ---------- Layout ----------
const int16_t SCREEN_W = 250, SCREEN_H = 122;
const int16_t BAR_H = 30;                // includes the 1 px rule at its bottom
const int16_t COL_W[3] = {95, 90, 65};   // the brief says about 93 / 88 / 69; the worst-case room values need 4 px more
const int16_t COL_X[3] = {0, 95, 185};
const int16_t COL_PAD[3] = {10, 5, 5};   // 10 on the left edge: the printed case will have rounded corners
const int16_t LABEL_BASE = 8;
const int16_t VALUE_BASE = 24;
const int16_t AREA_Y = BAR_H, AREA_H = SCREEN_H - BAR_H;  // the big area: 250 x 92
const int16_t BAND_H = 12;               // low-battery band
const int16_t WEATHER_TEXT_X = 50, WEATHER_RULE_X = 142;
const size_t MESSAGE_BYTES = 32 * 92;    // 250 x 92, rows padded to 32 bytes

struct RoomReading {
  float tempC;
  float humidity;
};

struct ScreenState {
  RoomReading living;
  bool livingKnown;        // false: the Hub's own sensor didn't answer
  RoomReading bedroom;
  float co2Ppm;
  bool bedroomKnown;       // false: nothing received from the Node yet
  uint16_t bedroomAgeMin;  // how old the bedroom reading is
  bool co2Alarm;           // from nextCo2Alarm(), not straight from co2Ppm
  bool showMessage;        // false: weather view
  bool weatherKnown;
  float outsideNowC, outsideMaxC;
  uint8_t rainPercent;
  uint8_t weatherCode;     // WMO code, as Open-Meteo reports it
  char place[16];          // upper case, shown as "OGGI A <place>"
  bool batteryLow;
};

bool nextCo2Alarm(bool alarm, float co2Ppm) {
  if (co2Ppm >= CO2_ALARM_ON_PPM) return true;
  if (co2Ppm <= CO2_ALARM_OFF_PPM) return false;
  return alarm;
}

// Same table as iconForCode() in tools/hub_screen/screens.js
WeatherIcon weatherIconFor(uint8_t code) {
  if (code == 0) return ICON_CLEAR;
  if (code == 1 || code == 2) return ICON_PARTLY;
  if (code == 45 || code == 48) return ICON_FOG;
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return ICON_RAIN;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return ICON_SNOW;
  if (code >= 95) return ICON_THUNDER;
  return ICON_CLOUD;
}

// ---------- Text and bitmaps ----------

// Reads one character from UTF-8 text (one or two bytes is all the fonts hold)
static uint16_t nextCode(const char*& p) {
  uint8_t c = *p++;
  if (c < 0x80) return c;
  if ((c & 0xE0) == 0xC0 && *p) return ((c & 0x1F) << 6) | (*p++ & 0x3F);
  return '?';
}

static const Glyph* findGlyph(const Font& font, uint16_t code) {
  for (uint8_t i = 0; i < font.count; i++) {
    if (font.glyphs[i].code == code) return &font.glyphs[i];
  }
  return nullptr;
}

int16_t textWidth(const Font& font, const char* text) {
  int16_t w = 0;
  while (*text) {
    const Glyph* g = findGlyph(font, nextCode(text));
    if (g) w += g->advance;
  }
  return w;
}

// Rows padded to a whole byte, most significant bit first; only the set bits are drawn
void drawBits(const uint8_t* bits, int16_t w, int16_t h, int16_t x, int16_t y, bool ink) {
  const int16_t stride = (w + 7) / 8;
  const uint32_t color = ink ? TFT_BLACK : TFT_WHITE;
  for (int16_t j = 0; j < h; j++) {
    for (int16_t i = 0; i < w; i++) {
      if (bits[j * stride + (i >> 3)] & (0x80 >> (i & 7))) display.drawPixel(x + i, y + j, color);
    }
  }
}

// Draws with the pen at (x, baseline); returns the pen position after the text
int16_t drawText(const Font& font, const char* text, int16_t x, int16_t baseline, bool ink = true) {
  while (*text) {
    const Glyph* g = findGlyph(font, nextCode(text));
    if (!g) continue;
    drawBits(font.bits + g->offset, g->w, g->h, x + g->xo, baseline + g->yo, ink);
    x += g->advance;
  }
  return x;
}

void fillBox(int16_t x, int16_t y, int16_t w, int16_t h, bool ink) {
  display.fillRect(x, y, w, h, ink ? TFT_BLACK : TFT_WHITE);
}

// A small-caps label, bold when it fits the given width and regular otherwise
static void drawLabelAt(const char* text, int16_t x, int16_t baseline, int16_t maxWidth, bool ink = true) {
  const Font& font = textWidth(FONT_LABEL_BOLD, text) <= maxWidth ? FONT_LABEL_BOLD : FONT_LABEL;
  drawText(font, text, x, baseline, ink);
}

// ---------- Top bar ----------

// Only called above STALE_MINUTES, so the smallest value is 2 H
static void ageLabel(char* out, size_t size, const char* prefix, uint16_t minutes) {
  int hours = (minutes + 30) / 60;
  if (hours < 48) snprintf(out, size, "%s · %d H FA", prefix, hours);
  else snprintf(out, size, "%s · %d GG FA", prefix, min(99, (hours + 12) / 24));
}

static void drawRoom(uint8_t col, const char* label, const RoomReading& r, bool known, bool showWord) {
  const int16_t x = COL_X[col] + COL_PAD[col];
  char buf[16];
  drawLabelAt(label, x, LABEL_BASE, COL_W[col] - COL_PAD[col] - 3);
  if (!known) {
    int16_t pen = drawText(FONT_VALUE, "--.-°", x, VALUE_BASE) + 2;
    drawText(FONT_PERCENT, "--%", pen, VALUE_BASE);
    return;
  }
  snprintf(buf, sizeof(buf), "%.1f°", r.tempC);
  int16_t pen = drawText(FONT_VALUE, buf, x, VALUE_BASE) + 2;
  // Whole percent, capped at 99 so the widest row still fits its column
  const long humidity = constrain(lroundf(r.humidity), 0L, 99L);
  snprintf(buf, sizeof(buf), "%ld%%", humidity);
  pen = drawText(FONT_PERCENT, buf, pen, VALUE_BASE) + 2;
  if (!showWord) return;
  // Decided on the printed value, so the word never contradicts the number next to it
  if (humidity < HUMIDITY_DRY) drawText(FONT_WORD_BOLD, "dry", pen, VALUE_BASE);
  else if (humidity > HUMIDITY_WET) drawText(FONT_WORD_BOLD, "wet", pen, VALUE_BASE);
  else drawText(FONT_WORD, "ok", pen, VALUE_BASE);
}

static void drawCo2(const char* label, float ppm, bool known, bool alarm) {
  if (alarm) fillBox(COL_X[2], 0, COL_W[2], BAR_H, true);
  const bool ink = !alarm;
  const int16_t x = COL_X[2] + COL_PAD[2];
  char buf[8] = "----";
  drawLabelAt(label, x, LABEL_BASE, COL_W[2] - COL_PAD[2] - 3, ink);
  if (known) snprintf(buf, sizeof(buf), "%ld", lroundf(ppm));
  int16_t pen = drawText(FONT_VALUE, buf, x, VALUE_BASE, ink) + 3;
  drawText(FONT_WORD, "ppm", pen, VALUE_BASE, ink);
}

static void drawTopBar(const ScreenState& s) {
  const bool stale = s.bedroomKnown && s.bedroomAgeMin > STALE_MINUTES;
  char bedroomLabel[24] = "CAMERA", co2Label[24] = "CO2 CAMERA";
  if (stale) {
    ageLabel(bedroomLabel, sizeof(bedroomLabel), "CAMERA", s.bedroomAgeMin);
    ageLabel(co2Label, sizeof(co2Label), "CO2", s.bedroomAgeMin);
  }
  drawRoom(0, "SALOTTO", s.living, s.livingKnown, true);
  drawRoom(1, bedroomLabel, s.bedroom, s.bedroomKnown, !stale);
  drawCo2(s.co2Alarm ? "APRI FINESTRA" : co2Label, s.co2Ppm, s.bedroomKnown, s.co2Alarm);
  fillBox(COL_X[1] - 1, 0, 1, BAR_H, true);
  fillBox(COL_X[2] - 1, 0, 1, BAR_H, true);
  fillBox(0, BAR_H - 1, SCREEN_W, 1, true);
}

// ---------- Big area ----------

static void drawWeather(const ScreenState& s, int16_t areaH) {
  const bool compact = areaH < AREA_H;
  char now[8] = "--°", buf[24];
  if (s.weatherKnown) snprintf(now, sizeof(now), "%ld°", lroundf(s.outsideNowC));
  // The large digits when they fit before the rule, the smaller ones for values like -10°
  const bool useBig = !compact && textWidth(FONT_BIG, now) <= WEATHER_RULE_X - 4 - WEATHER_TEXT_X;
  const Font& big = useBig ? FONT_BIG : FONT_BIG_SMALL;

  const Icon& icon = (compact ? ART_WEATHER_SMALL : ART_WEATHER)[s.weatherKnown ? weatherIconFor(s.weatherCode) : ICON_CLOUD];
  const int16_t midY = AREA_Y + areaH / 2;
  drawBits(icon.bits, icon.w, icon.h, 8, midY - icon.h / 2, true);

  const int16_t labelBase = AREA_Y + (compact ? 12 : 14);
  drawText(FONT_LABEL_BOLD, "FUORI ADESSO", WEATHER_TEXT_X, labelBase);
  drawText(big, now, WEATHER_TEXT_X - 2, labelBase + (useBig ? 45 : compact ? 38 : 42));

  fillBox(WEATHER_RULE_X, AREA_Y + (compact ? 8 : 10), 1, areaH - (compact ? 16 : 20), true);
  const int16_t left = WEATHER_RULE_X + 10, right = SCREEN_W - 10;
  snprintf(buf, sizeof(buf), "OGGI A %s", s.place[0] ? s.place : "MILANO");
  drawLabelAt(buf, left, labelBase, right - left);

  int16_t base = labelBase + (compact ? 20 : 24);
  drawText(FONT_ROW, "Max", left, base);
  if (s.weatherKnown) snprintf(buf, sizeof(buf), "%ld°", lroundf(s.outsideMaxC));
  else snprintf(buf, sizeof(buf), "--°");
  drawText(FONT_ROW_BOLD, buf, right - textWidth(FONT_ROW_BOLD, buf), base);

  base += compact ? 18 : 20;
  drawText(FONT_ROW, "Pioggia", left, base);
  if (s.weatherKnown) snprintf(buf, sizeof(buf), "%u%%", s.rainPercent);
  else snprintf(buf, sizeof(buf), "--%%");
  drawText(FONT_ROW_BOLD, buf, right - textWidth(FONT_ROW_BOLD, buf), base);
}

static void drawBatteryBand() {
  const int16_t y = SCREEN_H - BAND_H;
  fillBox(0, y, SCREEN_W, BAND_H, true);
  // battery outline with one bar left
  fillBox(10, y + 3, 12, 1, false);
  fillBox(10, y + 8, 12, 1, false);
  fillBox(10, y + 3, 1, 6, false);
  fillBox(21, y + 3, 1, 6, false);
  fillBox(22, y + 5, 1, 2, false);
  fillBox(12, y + 5, 2, 2, false);
  drawText(FONT_LABEL_BOLD, "BATTERIA HUB BASSA", 28, y + 8, false);
}

// Draws the whole screen into the display's buffer; the caller sends it with display.update().
// messageBits is a 250x92 bitmap (MESSAGE_BYTES long), copied as it is. With the low-battery band
// showing, the band covers the bottom 12 rows of the message (Pedro's choice).
void drawScreen(const ScreenState& s, const uint8_t* messageBits) {
  display.fillScreen(TFT_WHITE);
  drawTopBar(s);
  if (s.showMessage && messageBits) drawBits(messageBits, SCREEN_W, AREA_H, 0, AREA_Y, true);
  else drawWeather(s, s.batteryLow ? AREA_H - BAND_H : AREA_H);
  if (s.batteryLow) drawBatteryBand();
}
