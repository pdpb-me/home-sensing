// Home sensor: XIAO ESP32-S3 Plus + BMP280 (+ optional SHT4x) + WeAct 1.54" e-paper (SSD1681)
//
// Libraries (Arduino Library Manager):
//   - GxEPD2 by Jean-Marc Zingg       (pulls in Adafruit GFX)
//   - Adafruit BMP280 Library
//   - Adafruit SHT4x Library
//
// Wiring:
//   I2C sensors: SDA -> D4 (GPIO5), SCL -> D5 (GPIO6), VCC -> 3V3, GND -> GND
//                BMP280: CSB -> 3V3, SDO -> GND (address 0x76)
//                SHT4x (optional, address 0x44): just share SDA/SCL/3V3/GND
//   E-paper:     SCL -> D8 (GPIO7), SDA -> D10 (GPIO9), CS -> D1 (GPIO2),
//                D/C -> D2 (GPIO3), RES -> D3 (GPIO4), BUSY -> D0 (GPIO1)

#include <Wire.h>
#include <SPI.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_SHT4x.h>

// ---------- Settings ----------
// 0 = stay awake between updates (easy to re-upload while developing)
// 1 = deep sleep between updates (battery mode; to re-upload, hold BOOT and tap RESET)
#define USE_DEEP_SLEEP 0

const uint32_t UPDATE_SECONDS = 10;    // how often to read + redraw
const uint8_t FULL_REFRESH_EVERY = 12;  // full refresh every N updates to clear ghosting
const float ALTITUDE_M = 132;             // your altitude, to show sea-level pressure like weather apps

// ---------- Pins ----------
const int PIN_SDA = 5;
const int PIN_SCL = 6;
const int PIN_EPD_SCK = 7;
const int PIN_EPD_MOSI = 9;
const int PIN_EPD_CS = 2;
const int PIN_EPD_DC = 3;
const int PIN_EPD_RST = 4;
const int PIN_EPD_BUSY = 1;

GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display(
    GxEPD2_154_D67(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY));
Adafruit_BMP280 bmp(&Wire);
Adafruit_SHT4x sht4;

// Survives deep sleep, so we know when a full refresh is due
RTC_DATA_ATTR uint32_t updateCount = 0;

struct Reading {
  float tempC = NAN;
  float humidity = NAN;
  float pressureHpa = NAN;
};

Reading readSensors() {
  Reading r;

  if (bmp.begin(0x76)) {
    bmp.setSampling(Adafruit_BMP280::MODE_FORCED,
                    Adafruit_BMP280::SAMPLING_X2,   // temperature
                    Adafruit_BMP280::SAMPLING_X16,  // pressure
                    Adafruit_BMP280::FILTER_OFF,
                    Adafruit_BMP280::STANDBY_MS_1);
    if (bmp.takeForcedMeasurement()) {
      r.tempC = bmp.readTemperature();
      float p = bmp.readPressure() / 100.0f;
      r.pressureHpa = p / pow(1.0f - ALTITUDE_M / 44330.0f, 5.255f);
    }
  } else {
    Serial.println("BMP280 not found");
  }

  // Optional humidity sensor: used automatically when present
  if (sht4.begin(&Wire)) {
    sht4.setPrecision(SHT4X_HIGH_PRECISION);
    sht4.setHeater(SHT4X_NO_HEATER);
    sensors_event_t hum, temp;
    if (sht4.getEvent(&hum, &temp)) {
      r.humidity = hum.relative_humidity;
      r.tempC = temp.temperature;  // SHT4x temperature is more accurate than BMP280
    }
  }

  return r;
}

void drawCentered(const char* text, int16_t cx, int16_t baseline, const GFXfont* font) {
  int16_t x1, y1;
  uint16_t w, h;
  display.setFont(font);
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(cx - w / 2 - x1, baseline);
  display.print(text);
}

// Big "21.4°C"; the built-in fonts have no degree sign, so it is drawn as a circle
void drawTemperature(float t, int16_t baseline) {
  char num[8];
  if (isnan(t)) strcpy(num, "--.-");
  else snprintf(num, sizeof(num), "%.1f", t);

  int16_t numX1, cX1, y1;
  uint16_t numW, numH, cW, cH;
  display.setFont(&FreeSansBold24pt7b);
  display.getTextBounds(num, 0, 0, &numX1, &y1, &numW, &numH);
  display.setFont(&FreeSans18pt7b);
  display.getTextBounds("C", 0, 0, &cX1, &y1, &cW, &cH);

  const int16_t degR = 4, gap = 5;
  int16_t total = numW + gap + 2 * degR + 3 + cW;
  int16_t x = (display.width() - total) / 2;

  display.setFont(&FreeSansBold24pt7b);
  display.setCursor(x - numX1, baseline);
  display.print(num);

  int16_t cx = x + numW + gap + degR;
  int16_t cy = baseline - numH + degR;
  display.drawCircle(cx, cy, degR, GxEPD_BLACK);
  display.drawCircle(cx, cy, degR - 1, GxEPD_BLACK);

  display.setFont(&FreeSans18pt7b);
  display.setCursor(cx + degR + 3 - cX1, baseline);
  display.print("C");
}

void drawScreen(const Reading& r) {
  char buf[12];
  const int16_t w = display.width();

  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);

  drawTemperature(r.tempC, 70);
  display.drawFastHLine(10, 100, w - 20, GxEPD_BLACK);

  drawCentered("Humidity", w / 4, 128, &FreeSans9pt7b);
  if (isnan(r.humidity)) strcpy(buf, "--%");
  else snprintf(buf, sizeof(buf), "%.0f%%", r.humidity);
  drawCentered(buf, w / 4, 168, &FreeSansBold18pt7b);

  display.drawFastVLine(w / 2, 112, 70, GxEPD_BLACK);

  drawCentered("hPa", 3 * w / 4, 128, &FreeSans9pt7b);
  if (isnan(r.pressureHpa)) strcpy(buf, "----");
  else snprintf(buf, sizeof(buf), "%.0f", r.pressureHpa);
  drawCentered(buf, 3 * w / 4, 168, &FreeSansBold18pt7b);
}

void update() {
  Reading r = readSensors();
  Serial.printf("T=%.2f C  RH=%.1f %%  P=%.1f hPa\n", r.tempC, r.humidity, r.pressureHpa);

  if (updateCount % FULL_REFRESH_EVERY == 0) display.setFullWindow();
  else display.setPartialWindow(0, 0, display.width(), display.height());

  display.firstPage();
  do {
    drawScreen(r);
  } while (display.nextPage());

  updateCount++;
}

void setup() {
  Serial.begin(115200);
  Wire.begin(PIN_SDA, PIN_SCL);

  SPI.begin(PIN_EPD_SCK, -1, PIN_EPD_MOSI, PIN_EPD_CS);  // display is write-only, no MISO
  display.epd2.selectSPI(SPI, SPISettings(4000000, MSBFIRST, SPI_MODE0));
  display.init(115200, updateCount == 0, 2, false);  // full clear only on first power-up
  display.setRotation(3);  // 0 = normal, each step turns the image 90° clockwise

#if USE_DEEP_SLEEP
  update();
  display.hibernate();
  esp_sleep_enable_timer_wakeup((uint64_t)UPDATE_SECONDS * 1000000ULL);
  esp_deep_sleep_start();
#endif
}

void loop() {
  update();
  display.powerOff();
  delay(UPDATE_SECONDS * 1000UL);
}
