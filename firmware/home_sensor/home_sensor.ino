// Home sensor: XIAO ESP32-S3 Plus + BMP280 (+ optional SHT4x) + WeAct 1.54" e-paper (SSD1681)
// Reads every minute, uploads to Supabase over Wi-Fi, refreshes the e-paper every 5 minutes.
//
// Setup:
//   - Copy secrets.example.h to secrets.h (same folder) and fill it in. secrets.h is git-ignored.
//   - Libraries (Arduino Library Manager):
//       GxEPD2 by Jean-Marc Zingg (pulls in Adafruit GFX), Adafruit BMP280 Library, Adafruit SHT4x Library
//
// Wiring:
//   I2C sensors: SDA -> D4 (GPIO5), SCL -> D5 (GPIO6), VCC -> 3V3, GND -> GND
//                BMP280: CSB -> 3V3, SDO -> GND (address 0x76)
//                SHT4x (optional, address 0x44): just share SDA/SCL/3V3/GND
//   E-paper:     SCL -> D8 (GPIO7), SDA -> D10 (GPIO9), CS -> D1 (GPIO2),
//                D/C -> D2 (GPIO3), RES -> D3 (GPIO4), BUSY -> D0 (GPIO1)

#include <Wire.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_SHT4x.h>
#include "secrets.h"

// ---------- Settings ----------
const uint32_t READ_SECONDS = 60;       // read + upload interval
const uint8_t DISPLAY_EVERY = 5;        // redraw the e-paper every N readings
const uint8_t FULL_REFRESH_EVERY = 12;  // full refresh every N redraws to clear ghosting
const float ALTITUDE_M = 132;           // your altitude, to show sea-level pressure like weather apps
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

GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display(
    GxEPD2_154_D67(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY));
Adafruit_BMP280 bmp(&Wire);
Adafruit_SHT4x sht4;

struct Reading {
  float tempC = NAN;
  float humidity = NAN;
  float pressureHpa = NAN;
};

// Readings waiting to be uploaded (ring buffer, oldest first)
struct Sample {
  uint32_t ts;  // Unix seconds, UTC
  float t, h, p;
};
Sample pending[QUEUE_CAPACITY];
size_t qHead = 0, qCount = 0;

time_t lastUploadAt = 0;
uint32_t readingCount = 0;
uint32_t displayCount = 0;
uint32_t lastReadMs = 0;

// ---------- Sensors ----------

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
  body.reserve(120 + n * 60);
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
    body += ",\"p\":";
    appendNumber(body, s.p, 1);
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

// Bottom line: when data last reached the cloud, or why it hasn't
void statusText(char* out, size_t len) {
  if (WiFi.status() != WL_CONNECTED) {
    snprintf(out, len, "Offline, %u queued", (unsigned)qCount);
  } else if (lastUploadAt == 0) {
    snprintf(out, len, "Wi-Fi ok, not sent yet");
  } else {
    struct tm t;
    localtime_r(&lastUploadAt, &t);
    if (qCount > 0) snprintf(out, len, "Sent %02d:%02d, %u queued", t.tm_hour, t.tm_min, (unsigned)qCount);
    else snprintf(out, len, "Sent %02d:%02d", t.tm_hour, t.tm_min);
  }
}

void drawScreen(const Reading& r) {
  char buf[32];
  const int16_t w = display.width();

  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);

  drawTemperature(r.tempC, 64);
  display.drawFastHLine(10, 92, w - 20, GxEPD_BLACK);

  drawCentered("Humidity", w / 4, 118, &FreeSans9pt7b);
  if (isnan(r.humidity)) strcpy(buf, "--%");
  else snprintf(buf, sizeof(buf), "%.0f%%", r.humidity);
  drawCentered(buf, w / 4, 156, &FreeSansBold18pt7b);

  display.drawFastVLine(w / 2, 102, 62, GxEPD_BLACK);

  drawCentered("hPa", 3 * w / 4, 118, &FreeSans9pt7b);
  if (isnan(r.pressureHpa)) strcpy(buf, "----");
  else snprintf(buf, sizeof(buf), "%.0f", r.pressureHpa);
  drawCentered(buf, 3 * w / 4, 156, &FreeSansBold18pt7b);

  display.drawFastHLine(10, 172, w - 20, GxEPD_BLACK);
  statusText(buf, sizeof(buf));
  drawCentered(buf, w / 2, 192, &FreeSans9pt7b);
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
}

// ---------- Main ----------

void takeReading() {
  Reading r = readSensors();
  Serial.printf("T=%.2f C  RH=%.1f %%  P=%.1f hPa\n", r.tempC, r.humidity, r.pressureHpa);

  bool hasData = !isnan(r.tempC) || !isnan(r.pressureHpa);
  if (hasData && clockIsSet()) {
    enqueue({(uint32_t)time(nullptr), r.tempC, r.humidity, r.pressureHpa});
  } else if (hasData) {
    Serial.println("Clock not synced yet: reading shown but not stored");
  }
  uploadQueue();

  if (readingCount % DISPLAY_EVERY == 0) updateDisplay(r);
  readingCount++;
}

void setup() {
  setCpuFrequencyMhz(80);  // plenty for this job, and less self-heating near the sensor
  Serial.begin(115200);
  Wire.begin(PIN_SDA, PIN_SCL);

  SPI.begin(PIN_EPD_SCK, -1, PIN_EPD_MOSI, PIN_EPD_CS);  // display is write-only, no MISO
  display.epd2.selectSPI(SPI, SPISettings(4000000, MSBFIRST, SPI_MODE0));
  display.init(115200, true, 2, false);
  display.setRotation(3);  // 0 = normal, each step turns the image 90° clockwise

  connectWiFi();
  configTzTime(TIMEZONE, "pool.ntp.org", "time.google.com");
  struct tm now;
  if (getLocalTime(&now, 10000)) {
    Serial.printf("Clock synced: %04d-%02d-%02d %02d:%02d\n",
                  now.tm_year + 1900, now.tm_mon + 1, now.tm_mday, now.tm_hour, now.tm_min);
  }

  lastReadMs = millis();
  takeReading();
}

void loop() {
  if (millis() - lastReadMs >= READ_SECONDS * 1000UL) {
    lastReadMs = millis();
    takeReading();
  }
  delay(100);
}
