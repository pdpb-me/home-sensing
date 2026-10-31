// Home sensor: XIAO ESP32-S3 Plus + SCD41 (CO2, temperature, humidity) + WeAct 1.54" e-paper (SSD1681) + LiPo
// Wakes from deep sleep every 5 minutes to read the sensor and redraw the e-paper, and uploads to Supabase
// over Wi-Fi every 30 minutes, or right away when the air turns poor. Stays awake while a USB host is
// connected, so the Serial Monitor and uploading sketches keep working.
//
// Setup:
//   - Copy secrets.example.h to secrets.h (same folder) and fill it in. secrets.h is git-ignored.
//   - Libraries (Arduino Library Manager):
//       GxEPD2 by Jean-Marc Zingg (pulls in Adafruit GFX), Sensirion I2C SCD4x (pulls in Sensirion Core)
//
// Wiring:
//   SCD41:       SDA -> D4 (GPIO5), SCL -> D5 (GPIO6), VDD -> 3V3, GND -> GND (address 0x62)
//   E-paper:     SCL -> D8 (GPIO7), SDA -> D10 (GPIO9), CS -> D1 (GPIO2),
//                D/C -> D2 (GPIO3), RES -> D3 (GPIO4), BUSY -> D0 (GPIO1)
//   Battery:     BAT+ -> 100k -> D9 (GPIO8) -> 100k -> GND, 0.22 uF from D9 to GND

#include <Wire.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>
#include <esp_sleep.h>
#include <esp_sntp.h>
#include <driver/gpio.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <SensirionI2cScd4x.h>
#include "secrets.h"

// Per-device values: set them in secrets.h to override these defaults
#ifndef DEVICE_ALTITUDE_M
#define DEVICE_ALTITUDE_M 132       // metres above sea level, for CO2 pressure compensation
#endif
#ifndef DEVICE_TEMP_OFFSET_C
#define DEVICE_TEMP_OFFSET_C 0.5f   // self-heating, small in single-shot mode; provisional, tune against a reference thermometer
#endif

// ---------- Settings ----------
const uint32_t READ_SECONDS = 300;      // read + redraw interval; the SCD41's self-calibration assumes 5 minutes
const uint8_t UPLOAD_EVERY = 6;         // upload every N readings
const uint8_t FULL_REFRESH_EVERY = 12;  // full refresh every N redraws to clear ghosting
const float CO2_FAIR_PPM = 800;         // below this the air is good
const float CO2_POOR_PPM = 1200;        // from here on, time to open the window
const float HUMIDITY_DRY = 40;          // comfortable humidity is between these two
const float HUMIDITY_WET = 60;
const float BATTERY_DIVIDER = 2.0f;     // calibrate against a multimeter
const float BATTERY_MIN_V = 2.5f;       // below this, no battery is connected
const float CHARGER_MIN_V = 4.25f;      // above this, only a charger can be driving the pin
const char* TIMEZONE = "CET-1CEST,M3.5.0,M10.5.0/3";  // Central Europe, with daylight saving

const size_t QUEUE_CAPACITY = 288;      // keeps 24 h of readings while offline; must fit in RTC memory
const size_t UPLOAD_BATCH = 60;         // readings per upload request

// ---------- Pins ----------
const int PIN_SDA = 5;
const int PIN_SCL = 6;
const int PIN_EPD_SCK = 7;
const int PIN_EPD_MOSI = 9;
const int PIN_EPD_CS = 2;
const int PIN_EPD_DC = 3;
const int PIN_EPD_RST = 4;
const int PIN_EPD_BUSY = 1;
const int PIN_BATTERY = 8;

GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display(
    GxEPD2_154_D67(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY));
SensirionI2cScd4x scd4x;

enum AirLevel { AIR_UNKNOWN, AIR_GOOD, AIR_FAIR, AIR_POOR };

struct Reading {
  float tempC = NAN;
  float humidity = NAN;
  float co2Ppm = NAN;
  float batteryV = NAN;
  bool charging = false;
};

// Readings waiting to be uploaded (ring buffer, oldest first)
struct Sample {
  uint32_t ts;  // Unix seconds, UTC
  float t, h, co2, batteryV;
  bool charging;
};

// RTC memory survives deep sleep; everything else starts over at each wake
RTC_DATA_ATTR Sample pending[QUEUE_CAPACITY];
RTC_DATA_ATTR size_t qHead = 0, qCount = 0;
RTC_DATA_ATTR bool uploadOk = false;  // whether the last upload attempt worked
RTC_DATA_ATTR uint32_t readingsSinceUpload = 0;
RTC_DATA_ATTR uint32_t displayCount = 0;
RTC_DATA_ATTR AirLevel shownAir = AIR_UNKNOWN;
RTC_DATA_ATTR bool scdReady = false;

// ---------- Sensors ----------

// Keeps its settings while powered, so this only runs after a power-up or a sensor error
void configureScd41() {
  // The sensor keeps measuring through an ESP32 reset, and only accepts settings when stopped
  scd4x.wakeUp();
  scd4x.stopPeriodicMeasurement();
  delay(500);
  scd4x.setSensorAltitude(DEVICE_ALTITUDE_M);
  scd4x.setTemperatureOffset(DEVICE_TEMP_OFFSET_C);
}

// One measurement; takes 5 s
bool measureScd41(Reading& r) {
  uint16_t co2 = 0;
  float t = NAN, h = NAN;
  if (scd4x.measureSingleShot() != 0 || scd4x.readMeasurement(co2, t, h) != 0 || co2 == 0) {
    Serial.println("SCD41 not found");
    return false;
  }
  r.co2Ppm = co2;
  r.tempC = t;
  r.humidity = h;
  return true;
}

AirLevel airLevel(float co2) {
  if (isnan(co2)) return AIR_UNKNOWN;
  if (co2 < CO2_FAIR_PPM) return AIR_GOOD;
  return co2 < CO2_POOR_PPM ? AIR_FAIR : AIR_POOR;
}

float readBatteryVolts() {
  uint32_t sum = 0;
  for (int i = 0; i < 16; i++) sum += analogReadMilliVolts(PIN_BATTERY);
  float v = sum / 16.0f / 1000.0f * BATTERY_DIVIDER;
  return v < BATTERY_MIN_V ? NAN : v;
}

// LiPo discharge curve, interpolated: voltage is far from linear with charge
float batteryPercent(float v) {
  static const float volts[] = {4.20, 4.08, 3.95, 3.85, 3.80, 3.75, 3.70, 3.60, 3.30};
  static const float pct[] = {100, 85, 70, 55, 40, 25, 12, 5, 0};
  const int n = sizeof(volts) / sizeof(volts[0]);
  if (isnan(v)) return NAN;
  if (v >= volts[0]) return 100;
  for (int i = 1; i < n; i++) {
    if (v >= volts[i]) {
      return pct[i] + (pct[i - 1] - pct[i]) * (v - volts[i]) / (volts[i - 1] - volts[i]);
    }
  }
  return 0;
}

Reading readSensors() {
  Reading r;

  if (!scdReady) {
    configureScd41();
    scdReady = measureScd41(r);  // the first measurement after power-up is not reliable, so it only tests the sensor
  }
  if (scdReady) scdReady = measureScd41(r);

  r.batteryV = readBatteryVolts();
  // A USB host is detected directly. A plain wall charger can only be spotted by its voltage.
  r.charging = !isnan(r.batteryV) && (HWCDC::isPlugged() || r.batteryV >= CHARGER_MIN_V);

  return r;
}

// ---------- Wi-Fi, clock, upload ----------

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  for (int i = 0; i < 150 && WiFi.status() != WL_CONNECTED; i++) {
    delay(100);
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf(" connected, IP %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println(" failed");
  }
}

void disconnectWiFi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

bool clockIsSet() {
  return time(nullptr) > 1700000000;  // any date after Nov 2023 means NTP has synced
}

// The clock drifts during deep sleep, so it is corrected at every connection
void syncClock() {
  sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
  configTzTime(TIMEZONE, "pool.ntp.org", "time.google.com");
  for (int i = 0; i < 50 && sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED; i++) {
    delay(100);
  }
}

void enqueue(const Sample& s) {
  if (qCount == QUEUE_CAPACITY) {  // full: drop the oldest reading
    qHead = (qHead + 1) % QUEUE_CAPACITY;
    qCount--;
  }
  pending[(qHead + qCount) % QUEUE_CAPACITY] = s;
  qCount++;
}

void appendNumber(String& s, float v, unsigned decimals) {
  if (isnan(v)) s += "null";
  else s += String(v, decimals);
}

// Sends the oldest readings in one request; removes them from the queue on success
bool uploadBatch() {
  size_t n = qCount < UPLOAD_BATCH ? qCount : UPLOAD_BATCH;

  String body;
  body.reserve(120 + n * 90);
  body += "{\"p_device_id\":\"" DEVICE_ID "\",\"p_key\":\"" DEVICE_KEY "\",\"p_readings\":[";
  for (size_t i = 0; i < n; i++) {
    const Sample& s = pending[(qHead + i) % QUEUE_CAPACITY];
    if (i > 0) body += ',';
    body += "{\"ts\":";
    body += s.ts;
    body += ",\"t\":";
    appendNumber(body, s.t, 2);
    body += ",\"h\":";
    appendNumber(body, s.h, 1);
    body += ",\"c\":";
    appendNumber(body, s.co2, 0);
    body += ",\"bv\":";
    appendNumber(body, s.batteryV, 2);
    body += ",\"bp\":";  // a charging battery's voltage says nothing about its charge
    appendNumber(body, s.charging ? NAN : batteryPercent(s.batteryV), 0);
    body += ",\"chg\":";
    body += s.charging ? "true" : "false";
    body += '}';
  }
  body += "]}";

  WiFiClientSecure client;
  client.setInsecure();  // traffic is encrypted, but the server certificate isn't verified
  HTTPClient http;
  http.begin(client, SUPABASE_URL "/rest/v1/rpc/ingest");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", SUPABASE_ANON_KEY);
  http.setTimeout(10000);
  int code = http.POST(body);
  String response = http.getString();
  http.end();

  if (code != 200) {
    Serial.printf("Upload failed: HTTP %d %s\n", code, response.c_str());
    return false;
  }
  qHead = (qHead + n) % QUEUE_CAPACITY;
  qCount -= n;
  Serial.printf("Uploaded %u reading(s), %s new, %u still queued\n",
                (unsigned)n, response.c_str(), (unsigned)qCount);
  return true;
}

void uploadQueue() {
  if (qCount == 0) return;
  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("No Wi-Fi, %u reading(s) queued\n", (unsigned)qCount);
    uploadOk = false;
    return;
  }
  for (int i = 0; i < 5 && qCount > 0; i++) {  // after an outage, catch up a few batches at a time
    uploadOk = uploadBatch();
    if (!uploadOk) return;
  }
}

// ---------- Display ----------

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

// Droplet + Dry / OK / Humid, centered as one group
void drawHumidityStatus(float humidity, int16_t cx, int16_t top, int16_t baseline) {
  const char* text = isnan(humidity) ? "--" : humidity < HUMIDITY_DRY ? "Dry" : humidity > HUMIDITY_WET ? "Humid" : "OK";
  int16_t x1, y1;
  uint16_t tw, th;
  display.setFont(&FreeSans9pt7b);
  display.getTextBounds(text, 0, 0, &x1, &y1, &tw, &th);

  const int16_t dropW = 16, gap = 6;
  int16_t left = cx - (dropW + gap + tw) / 2;
  int16_t dropX = left + dropW / 2;
  display.fillCircle(dropX, top + 17, 8, GxEPD_BLACK);
  display.fillTriangle(dropX, top, dropX - 7, top + 13, dropX + 7, top + 13, GxEPD_BLACK);

  display.setCursor(left + dropW + gap - x1, baseline);
  display.print(text);
}

// The advice line: black when it is time to act, so it can be read from across the room
void drawAirAdvice(AirLevel level) {
  const int16_t w = display.width();
  if (level == AIR_POOR) {
    display.fillRect(0, 152, w, 48, GxEPD_BLACK);
    display.setTextColor(GxEPD_WHITE);
    drawCentered("Open window", w / 2, 184, &FreeSansBold12pt7b);
    display.setTextColor(GxEPD_BLACK);
  } else {
    const char* text = level == AIR_GOOD ? "Air good" : level == AIR_FAIR ? "Air fair" : "Air --";
    drawCentered(text, w / 2, 184, &FreeSansBold12pt7b);
  }
}

// Fill shows the charge, a bolt means charging, a strike-through means no battery
void drawBatteryIcon(const Reading& r, int16_t x, int16_t top) {
  const int16_t iconW = 26, iconH = 12, nubW = 2;
  display.drawRect(x, top, iconW, iconH, GxEPD_BLACK);
  display.drawRect(x + 1, top + 1, iconW - 2, iconH - 2, GxEPD_BLACK);
  display.fillRect(x + iconW, top + 3, nubW, iconH - 6, GxEPD_BLACK);

  int16_t mid = x + iconW / 2;
  if (isnan(r.batteryV)) {
    display.drawLine(x + 3, top + iconH - 3, x + iconW - 4, top + 2, GxEPD_BLACK);
  } else if (r.charging) {
    display.fillTriangle(mid + 2, top + 2, mid - 4, top + 6, mid, top + 6, GxEPD_BLACK);
    display.fillTriangle(mid - 2, top + 9, mid + 4, top + 5, mid, top + 5, GxEPD_BLACK);
  } else {
    display.fillRect(x + 3, top + 3, (iconW - 6) * batteryPercent(r.batteryV) / 100, iconH - 6, GxEPD_BLACK);
  }
}

// Wi-Fi arcs: the last upload worked. Struck through: it failed, so readings are waiting for the next try.
void drawWifiIcon(int16_t cx, int16_t dotY) {
  display.fillRect(cx - 2, dotY - 2, 4, 4, GxEPD_BLACK);
  for (int16_t r = 5; r <= 13; r += 4) {
    for (int16_t thick = 0; thick < 2; thick++) {
      for (int16_t deg = 45; deg <= 135; deg++) {
        float a = deg * DEG_TO_RAD;
        display.drawPixel(cx + (r + thick) * cos(a), dotY - (r + thick) * sin(a), GxEPD_BLACK);
      }
    }
  }

  if (!uploadOk) {
    for (int16_t i = -2; i <= 3; i++) {
      bool black = i == 0 || i == 1;
      display.drawLine(cx - 11, dotY + 1 + i, cx + 11, dotY - 14 + i, black ? GxEPD_BLACK : GxEPD_WHITE);
    }
  }
}

void drawScreen(const Reading& r) {
  char buf[32];
  const int16_t w = display.width();

  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);

  drawWifiIcon(46, 16);
  drawBatteryIcon(r, 139, 4);
  display.drawFastHLine(0, 20, w, GxEPD_BLACK);

  drawTemperature(r.tempC, 67);
  display.drawFastHLine(10, 79, w - 20, GxEPD_BLACK);

  drawHumidityStatus(r.humidity, w / 4, 84, 102);
  if (isnan(r.humidity)) strcpy(buf, "--%");
  else snprintf(buf, sizeof(buf), "%.0f%%", r.humidity);
  drawCentered(buf, w / 4, 141, &FreeSansBold18pt7b);

  display.drawFastVLine(w / 2, 84, 60, GxEPD_BLACK);

  drawCentered("CO2", 3 * w / 4, 102, &FreeSans9pt7b);
  if (isnan(r.co2Ppm)) strcpy(buf, "----");
  else snprintf(buf, sizeof(buf), "%.0f", r.co2Ppm);
  drawCentered(buf, 3 * w / 4, 141, &FreeSansBold18pt7b);

  drawAirAdvice(airLevel(r.co2Ppm));
}

void updateDisplay(const Reading& r) {
  if (displayCount % FULL_REFRESH_EVERY == 0) display.setFullWindow();
  else display.setPartialWindow(0, 0, display.width(), display.height());

  display.firstPage();
  do {
    drawScreen(r);
  } while (display.nextPage());
  display.hibernate();  // draws almost nothing and keeps the image, ready for the next partial refresh

  displayCount++;
  shownAir = airLevel(r.co2Ppm);
}

// ---------- Main ----------

void runCycle() {
  Reading r = readSensors();
  Serial.printf("T=%.2f C  RH=%.1f %%  CO2=%.0f ppm  Battery=%.2f V (%.0f %%)%s\n", r.tempC, r.humidity, r.co2Ppm,
                r.batteryV, batteryPercent(r.batteryV), r.charging ? " charging" : "");

  // A failed upload waits for the next regular one, so being offline doesn't drain the battery
  AirLevel air = airLevel(r.co2Ppm);
  readingsSinceUpload++;
  bool firstCycle = displayCount == 0;
  bool uploadDue = firstCycle || readingsSinceUpload >= UPLOAD_EVERY || (air == AIR_POOR && shownAir != AIR_POOR);

  bool useWiFi = uploadDue || !clockIsSet();
  if (useWiFi) {
    connectWiFi();
    if (WiFi.status() == WL_CONNECTED) syncClock();
  }

  bool hasData = !isnan(r.tempC) || !isnan(r.co2Ppm);
  if (hasData && clockIsSet()) {
    enqueue({(uint32_t)time(nullptr), r.tempC, r.humidity, r.co2Ppm, r.batteryV, r.charging});
  } else if (hasData) {
    Serial.println("Clock not synced yet: reading shown but not stored");
  }

  if (uploadDue) {
    uploadQueue();
    readingsSinceUpload = 0;
  }
  if (useWiFi) disconnectWiFi();

  updateDisplay(r);
}

void setup() {
  setCpuFrequencyMhz(80);  // plenty for this job
  Serial.begin(115200);
  bool wokeFromSleep = esp_reset_reason() == ESP_RST_DEEPSLEEP;

  gpio_deep_sleep_hold_dis();
  gpio_hold_dis((gpio_num_t)PIN_EPD_CS);
  gpio_hold_dis((gpio_num_t)PIN_EPD_RST);

  Wire.begin(PIN_SDA, PIN_SCL);
  scd4x.begin(Wire, SCD41_I2C_ADDR_62);

  SPI.begin(PIN_EPD_SCK, -1, PIN_EPD_MOSI, PIN_EPD_CS);  // display is write-only, no MISO
  display.epd2.selectSPI(SPI, SPISettings(4000000, MSBFIRST, SPI_MODE0));
  display.init(0, !wokeFromSleep, 2, false);  // 0 = no display-library debug output; after a wake, keep the image
  display.setRotation(3);  // 0 = normal, each step turns the image 90° clockwise
}

void loop() {
  uint32_t startedMs = millis();
  runCycle();
  uint32_t elapsedMs = millis() - startedMs;
  uint32_t waitMs = elapsedMs < READ_SECONDS * 1000UL ? READ_SECONDS * 1000UL - elapsedMs : 1000;

  if (HWCDC::isPlugged()) {  // deep sleep would drop the USB connection
    Serial.printf("USB connected: staying awake, next reading in %u s\n", (unsigned)(waitMs / 1000));
    delay(waitMs);
    return;
  }

  // Keeps the display's control lines steady during sleep
  gpio_hold_en((gpio_num_t)PIN_EPD_CS);
  gpio_hold_en((gpio_num_t)PIN_EPD_RST);
  gpio_deep_sleep_hold_en();
  esp_sleep_enable_timer_wakeup(waitMs * 1000ULL);
  esp_deep_sleep_start();
}
