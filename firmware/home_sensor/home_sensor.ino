// Home sensor: XIAO ESP32-S3 Plus + SCD41 (CO2, temperature, humidity) + WeAct 1.54" e-paper (SSD1681) + LiPo
// Reads every minute, uploads to Supabase over Wi-Fi, refreshes the e-paper every 5 minutes.
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
#define DEVICE_TEMP_OFFSET_C 1.0f   // self-heating; provisional, tune against a reference thermometer in the final position
#endif

// ---------- Settings ----------
const uint32_t READ_SECONDS = 60;       // read + upload interval
const uint8_t DISPLAY_EVERY = 5;        // redraw the e-paper every N readings
const uint8_t FULL_REFRESH_EVERY = 12;  // full refresh every N redraws to clear ghosting
const uint32_t SENSOR_POLL_MS = 1000;   // how often to ask the SCD41 for new data (it makes some every 5 s)
const uint32_t SENSOR_MAX_AGE_MS = 30000;  // older data counts as missing and restarts the sensor
const float CO2_FAIR_PPM = 800;         // below this the air is good
const float CO2_POOR_PPM = 1200;        // from here on, time to open the window
const float BATTERY_DIVIDER = 2.0f;     // calibrate against a multimeter
const float BATTERY_MIN_V = 2.5f;       // below this, no battery is connected
const float CHARGER_MIN_V = 4.25f;      // above this, only a charger can be driving the pin
const char* TIMEZONE = "CET-1CEST,M3.5.0,M10.5.0/3";  // Central Europe, with daylight saving

const size_t QUEUE_CAPACITY = 1440;     // keeps 24 h of readings while offline
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
Sample pending[QUEUE_CAPACITY];
size_t qHead = 0, qCount = 0;

time_t lastUploadAt = 0;
uint32_t readingCount = 0;
uint32_t displayCount = 0;
AirLevel shownAir = AIR_UNKNOWN;
uint32_t lastReadMs = 0;
uint32_t lastPollMs = 0;

// Latest SCD41 data
float scdTempC = NAN, scdHumidity = NAN, scdCo2Ppm = NAN;
bool scdHasData = false;
uint32_t scdDataAtMs = 0;

// ---------- Sensors ----------

void startScd41() {
  // The sensor keeps measuring through an ESP32 reset, and only accepts settings when stopped
  scd4x.wakeUp();
  scd4x.stopPeriodicMeasurement();
  delay(500);
  scd4x.setSensorAltitude(DEVICE_ALTITUDE_M);
  scd4x.setTemperatureOffset(DEVICE_TEMP_OFFSET_C);
  if (scd4x.startPeriodicMeasurement() != 0) Serial.println("SCD41 not found");
  scdHasData = false;
  scdDataAtMs = millis();
}

// Keeps the newest measurement; the SCD41 has one ready every 5 s
void pollScd41() {
  bool ready = false;
  if (scd4x.getDataReadyStatus(ready) != 0 || !ready) return;
  uint16_t co2 = 0;
  float t = NAN, h = NAN;
  if (scd4x.readMeasurement(co2, t, h) != 0 || co2 == 0) return;
  scdCo2Ppm = co2;
  scdTempC = t;
  scdHumidity = h;
  scdHasData = true;
  scdDataAtMs = millis();
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

  pollScd41();
  if (millis() - scdDataAtMs > SENSOR_MAX_AGE_MS) {
    Serial.println("No fresh SCD41 data, restarting it");
    startScd41();
  } else if (scdHasData) {
    r.tempC = scdTempC;
    r.humidity = scdHumidity;
    r.co2Ppm = scdCo2Ppm;
  }

  r.batteryV = readBatteryVolts();
  // A USB host is detected directly. A plain wall charger can only be spotted by its voltage.
  r.charging = !isnan(r.batteryV) && (HWCDC::isPlugged() || r.batteryV >= CHARGER_MIN_V);

  return r;
}

// ---------- Wi-Fi, clock, upload ----------

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  for (int i = 0; i < 30 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500);
    Serial.print('.');
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf(" connected, IP %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println(" not yet, will keep trying");
  }
}

bool clockIsSet() {
  return time(nullptr) > 1700000000;  // any date after Nov 2023 means NTP has synced
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
  lastUploadAt = time(nullptr);
  Serial.printf("Uploaded %u reading(s), %s new, %u still queued\n",
                (unsigned)n, response.c_str(), (unsigned)qCount);
  return true;
}

void uploadQueue() {
  if (qCount == 0) return;
  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("No Wi-Fi, %u reading(s) queued\n", (unsigned)qCount);
    WiFi.reconnect();
    return;
  }
  for (int i = 0; i < 5 && qCount > 0; i++) {  // after an outage, catch up a few batches per minute
    if (!uploadBatch()) return;
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

void drawDroplet(int16_t cx, int16_t top) {
  display.fillCircle(cx, top + 17, 8, GxEPD_BLACK);
  display.fillTriangle(cx, top, cx - 7, top + 13, cx + 7, top + 13, GxEPD_BLACK);
}

// The advice line: black when it is time to act, so it can be read from across the room
void drawAirAdvice(AirLevel level) {
  const int16_t w = display.width();
  if (level == AIR_POOR) {
    display.fillRect(0, 130, w, 30, GxEPD_BLACK);
    display.setTextColor(GxEPD_WHITE);
    drawCentered("Open window", w / 2, 153, &FreeSansBold12pt7b);
    display.setTextColor(GxEPD_BLACK);
  } else {
    const char* text = level == AIR_GOOD ? "Air good" : level == AIR_FAIR ? "Air fair" : "Air --";
    drawCentered(text, w / 2, 153, &FreeSansBold12pt7b);
  }
}

// Fill shows the charge, a bolt means charging, a strike-through means no battery
void drawBatteryIcon(const Reading& r, int16_t cx, int16_t top) {
  const int16_t iconW = 36, iconH = 18, nubW = 3;
  int16_t x = cx - (iconW + nubW) / 2;
  display.drawRect(x, top, iconW, iconH, GxEPD_BLACK);
  display.drawRect(x + 1, top + 1, iconW - 2, iconH - 2, GxEPD_BLACK);
  display.fillRect(x + iconW, top + 5, nubW, iconH - 10, GxEPD_BLACK);

  int16_t mid = x + iconW / 2;
  if (isnan(r.batteryV)) {
    display.drawLine(x + 4, top + iconH - 4, x + iconW - 5, top + 3, GxEPD_BLACK);
  } else if (r.charging) {
    display.fillTriangle(mid + 3, top + 2, mid - 6, top + 10, mid + 1, top + 10, GxEPD_BLACK);
    display.fillTriangle(mid - 3, top + iconH - 3, mid + 6, top + 7, mid - 1, top + 7, GxEPD_BLACK);
  } else {
    display.fillRect(x + 3, top + 3, (iconW - 6) * batteryPercent(r.batteryV) / 100, iconH - 6, GxEPD_BLACK);
  }
}

// Solid cloud: everything has reached the cloud. Struck through: offline, or readings still queued.
void drawCloudIcon(int16_t cx, int16_t top) {
  display.fillCircle(cx - 10, top + 13, 6, GxEPD_BLACK);
  display.fillCircle(cx + 1, top + 9, 9, GxEPD_BLACK);
  display.fillCircle(cx + 10, top + 13, 6, GxEPD_BLACK);
  display.fillRect(cx - 10, top + 13, 20, 7, GxEPD_BLACK);

  bool allSent = WiFi.status() == WL_CONNECTED && lastUploadAt != 0 && qCount == 0;
  if (!allSent) {
    for (int16_t i = -2; i <= 3; i++) {
      bool black = i == 0 || i == 1;
      display.drawLine(cx - 15, top + 19 + i, cx + 15, top + i, black ? GxEPD_BLACK : GxEPD_WHITE);
    }
  }
}

void drawScreen(const Reading& r) {
  char buf[32];
  const int16_t w = display.width();

  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);

  drawTemperature(r.tempC, 46);
  display.drawFastHLine(10, 58, w - 20, GxEPD_BLACK);

  drawDroplet(w / 4, 65);
  if (isnan(r.humidity)) strcpy(buf, "--%");
  else snprintf(buf, sizeof(buf), "%.0f%%", r.humidity);
  drawCentered(buf, w / 4, 122, &FreeSansBold18pt7b);

  display.drawFastVLine(w / 2, 64, 60, GxEPD_BLACK);

  drawCentered("CO2", 3 * w / 4, 84, &FreeSans9pt7b);
  if (isnan(r.co2Ppm)) strcpy(buf, "----");
  else snprintf(buf, sizeof(buf), "%.0f", r.co2Ppm);
  drawCentered(buf, 3 * w / 4, 122, &FreeSansBold18pt7b);

  drawAirAdvice(airLevel(r.co2Ppm));

  drawBatteryIcon(r, w / 4, 170);
  drawCloudIcon(3 * w / 4, 169);
}

void updateDisplay(const Reading& r) {
  if (displayCount % FULL_REFRESH_EVERY == 0) display.setFullWindow();
  else display.setPartialWindow(0, 0, display.width(), display.height());

  display.firstPage();
  do {
    drawScreen(r);
  } while (display.nextPage());
  display.powerOff();

  displayCount++;
  shownAir = airLevel(r.co2Ppm);
}

// ---------- Main ----------

void takeReading() {
  Reading r = readSensors();
  Serial.printf("T=%.2f C  RH=%.1f %%  CO2=%.0f ppm  Battery=%.2f V (%.0f %%)%s\n", r.tempC, r.humidity, r.co2Ppm,
                r.batteryV, batteryPercent(r.batteryV), r.charging ? " charging" : "");

  bool hasData = !isnan(r.tempC) || !isnan(r.co2Ppm);
  if (hasData && clockIsSet()) {
    enqueue({(uint32_t)time(nullptr), r.tempC, r.humidity, r.co2Ppm, r.batteryV, r.charging});
  } else if (hasData) {
    Serial.println("Clock not synced yet: reading shown but not stored");
  }
  uploadQueue();

  // A change in air quality is shown right away, without waiting for the next regular redraw
  if (readingCount % DISPLAY_EVERY == 0 || airLevel(r.co2Ppm) != shownAir) updateDisplay(r);
  readingCount++;
}

void setup() {
  setCpuFrequencyMhz(80);  // plenty for this job, and less self-heating near the sensor
  Serial.begin(115200);
  Wire.begin(PIN_SDA, PIN_SCL);
  scd4x.begin(Wire, SCD41_I2C_ADDR_62);
  startScd41();

  SPI.begin(PIN_EPD_SCK, -1, PIN_EPD_MOSI, PIN_EPD_CS);  // display is write-only, no MISO
  display.epd2.selectSPI(SPI, SPISettings(4000000, MSBFIRST, SPI_MODE0));
  display.init(0, true, 2, false);  // 0 = no display-library debug output on Serial
  display.setRotation(3);  // 0 = normal, each step turns the image 90° clockwise

  connectWiFi();
  configTzTime(TIMEZONE, "pool.ntp.org", "time.google.com");
  struct tm now;
  if (getLocalTime(&now, 10000)) {
    Serial.printf("Clock synced: %04d-%02d-%02d %02d:%02d\n",
                  now.tm_year + 1900, now.tm_mon + 1, now.tm_mday, now.tm_hour, now.tm_min);
  }

  // First SCD41 data arrives about 5 s after starting it
  while (!scdHasData && millis() - scdDataAtMs < 7000) {
    pollScd41();
    delay(200);
  }

  lastReadMs = millis();
  takeReading();
}

void loop() {
  if (millis() - lastPollMs >= SENSOR_POLL_MS) {
    lastPollMs = millis();
    pollScd41();
  }
  if (millis() - lastReadMs >= READ_SECONDS * 1000UL) {
    lastReadMs = millis();
    takeReading();
  }
  delay(100);
}
