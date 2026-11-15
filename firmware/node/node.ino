// Node (bedroom): XIAO ESP32-C6 + SCD41 on a LiPo. No display.
// Wakes every 5 minutes, takes a single-shot CO2 / temperature / humidity measurement and goes back to
// deep sleep. Samples wait in RTC memory and go to Supabase in one request: every 30 minutes, or straight
// away when the CO2 has moved enough to matter. The Node has no clock, so every sample is sent with its
// age and the server works out the time.
//
// While a computer is connected over USB it stays awake instead of sleeping, so the Serial Monitor and
// uploading sketches keep working (build with SLEEP_ON_USB=1 to sleep anyway).
//
// Setup:
//   - Copy secrets.example.h to secrets.h (same folder) and fill it in. secrets.h is git-ignored.
//   - Library (Arduino Library Manager): Sensirion I2C SCD4x (pulls in Sensirion Core).
//   - Board: XIAO_ESP32C6, USB CDC On Boot = Enabled.
//
// Wiring: SCD41 VDD -> 3V3, GND -> GND, SDA -> D9 (GPIO20), SCL -> D10 (GPIO18). Address 0x62.
// The sensor stays powered and idle between measurements (see POWER_DOWN_BETWEEN_READINGS for why).

#include <Wire.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_sleep.h>
#include <time.h>
#include <SensirionI2cScd4x.h>
#include "secrets.h"

#define FW_VERSION "node 0.2"
#ifndef SLEEP_ON_USB
#define SLEEP_ON_USB 0
#endif

// Per-device values: set them in secrets.h to override these defaults
#ifndef DEVICE_ALTITUDE_M
#define DEVICE_ALTITUDE_M 132       // metres above sea level, for CO2 pressure compensation
#endif
#ifndef DEVICE_TEMP_OFFSET_C
#define DEVICE_TEMP_OFFSET_C 0.0f   // self-heating; to be tuned against a reference thermometer
#endif

// ---------- Settings ----------
const uint32_t READ_SECONDS = 300;        // the SCD41's self-calibration counts on one measurement every 5 minutes
const uint8_t UPLOAD_EVERY = 6;           // upload at least every N readings (30 minutes)
const float CO2_CHANGE_PPM = 50;          // upload early when CO2 moved this much since the last upload...
const float CO2_LIMITS_PPM[] = {1000, 1400};  // ...or crossed one of the Hub's "open window" limits
const float CO2_LIMIT_MARGIN_PPM = 20;    // ...by at least this much, so hovering at a limit doesn't upload every time
// The SCD41 datasheet (v1.5, section 3.10) on powering the sensor down between single shots:
//   - "After a power cycle, the initial single shot reading should be discarded", so it takes two shots per wake;
//   - "for power-cycled single shot operation, ASC is not available", so the sensor never recalibrates itself.
// Left idle instead it draws about 0.45 mA on average at one shot every 5 minutes, needs one shot per wake and
// keeps its self-calibration. Sensirion's low-power note puts the break-even near 6 minutes, so at 5 minutes
// powering down saves nothing. Measured here: first shots after a wake were up to 66 ppm off the second ones.
const bool POWER_DOWN_BETWEEN_READINGS = false;
const uint8_t SHOTS_PER_WAKE = POWER_DOWN_BETWEEN_READINGS ? 2 : 1;
const uint8_t UPLOAD_RETRIES = 2;         // after an upload that fails out of the blue, try again at the next readings
const uint8_t MAX_BACKOFF = 2;            // in a long outage the upload interval doubles this many times (2 hours)
const uint8_t DHCP_EVERY_UPLOADS = 48;    // reuse the last IP address this many times before asking the router again
const uint32_t WIFI_FAST_MS = 5000;       // time allowed for a connection with remembered channel and address
const uint32_t WIFI_FULL_MS = 12000;      // time allowed for a connection from scratch
const uint32_t HTTP_TIMEOUT_MS = 8000;
const uint32_t TLS_HANDSHAKE_S = 8;       // the default is 120 s of radio on a bad connection
const uint32_t SHOT_MS = 5000;            // one single-shot measurement

const size_t QUEUE_CAPACITY = 240;        // 20 hours of readings while offline; has to fit in RTC memory

// ---------- Pins ----------
const int PIN_SDA = 20;
const int PIN_SCL = 18;
const uint8_t SCD41_ADDRESS = 0x62;

SensirionI2cScd4x scd4x;

struct Sample {
  uint32_t takenS;  // seconds on the Node's own counter, which keeps running through deep sleep
  float t, h;
  uint16_t co2;
};

// RTC memory survives deep sleep; a power-up or reset loads these starting values again
RTC_DATA_ATTR Sample pending[QUEUE_CAPACITY];
RTC_DATA_ATTR size_t qHead = 0, qCount = 0;
RTC_DATA_ATTR bool sensorStopped = false;  // the sensor is known to be idle, not measuring periodically
RTC_DATA_ATTR uint32_t cycles = 0;
RTC_DATA_ATTR uint8_t readingsSinceUpload = 0;
RTC_DATA_ATTR uint8_t retriesLeft = 0;
RTC_DATA_ATTR uint8_t failures = 0;        // uploads failed in a row
RTC_DATA_ATTR bool skipFirstUpload = false;
RTC_DATA_ATTR float uploadedCo2 = NAN;     // CO2 of the newest sample the server has
RTC_DATA_ATTR uint32_t lastAwakeMs = 0;    // how long the last cycle kept the chip busy
// What the last good connection looked like, to skip the scan and the address request next time
RTC_DATA_ATTR bool apKnown = false;
RTC_DATA_ATTR uint8_t apBssid[6];
RTC_DATA_ATTR uint8_t apChannel = 0;
RTC_DATA_ATTR uint32_t ipLocal = 0, ipGateway = 0, ipSubnet = 0, ipDns = 0;
RTC_DATA_ATTR uint8_t uploadsSinceDhcp = 0;

bool onUsb = false;  // decided once per wake: a computer is on the USB port

uint32_t nowSeconds() {
  return (uint32_t)time(nullptr);
}

bool stayAwake() {
  return onUsb && !SLEEP_ON_USB;
}

// Waits with the processor in light sleep; a plain delay when staying awake for the Serial Monitor
void nap(uint32_t ms) {
  const uint32_t startedMs = millis();
  if (!stayAwake()) {
    esp_sleep_enable_timer_wakeup(ms * 1000ULL);
    esp_light_sleep_start();
  }
  // Light sleep can return early or not start at all: never come back before the time is up
  while (millis() - startedMs < ms) delay(10);
}

// ---------- Sensor ----------

// One measurement. Starts it with the raw command so the processor can sleep through the 5 s it takes.
bool takeShot(Sample& s) {
  Wire.beginTransmission(SCD41_ADDRESS);
  Wire.write(0x21);
  Wire.write(0x9D);  // measure_single_shot
  if (Wire.endTransmission() != 0) return false;
  nap(SHOT_MS + 50);
  uint16_t co2 = 0;
  float t = NAN, h = NAN;
  if (scd4x.readMeasurement(co2, t, h) != 0 || co2 == 0) return false;
  s.co2 = co2;
  s.t = t;
  s.h = h;
  return true;
}

bool measure(Sample& s) {
  if (POWER_DOWN_BETWEEN_READINGS || !sensorStopped) {
    scd4x.wakeUp();  // the sensor doesn't acknowledge this command, so its result says nothing
  }
  if (!sensorStopped) {
    scd4x.stopPeriodicMeasurement();  // it may still be measuring from before an ESP32 reset
    sensorStopped = true;
  }
  // The sensor keeps these in RAM only and loses them when it powers down (measured: the temperature fell
  // back by the factory 4 C offset). Sending them at every wake is cheap and covers every case.
  scd4x.setSensorAltitude(DEVICE_ALTITUDE_M);
  scd4x.setTemperatureOffset(DEVICE_TEMP_OFFSET_C);

  bool ok = false;
  for (uint8_t shot = 0; shot < SHOTS_PER_WAKE; shot++) {
    ok = takeShot(s);
    if (ok) Serial.printf("  shot %u: CO2=%u ppm  T=%.2f C  RH=%.1f %%\n", shot + 1, s.co2, s.t, s.h);
    else Serial.printf("  shot %u: no answer from the SCD41\n", shot + 1);
  }
  if (POWER_DOWN_BETWEEN_READINGS) scd4x.powerDown();
  if (!ok) sensorStopped = false;  // start from scratch next time, in case it lost power
  s.takenS = nowSeconds();
  return ok;
}

// ---------- Queue ----------

void enqueue(const Sample& s) {
  if (qCount == QUEUE_CAPACITY) {  // full: drop the oldest reading
    qHead = (qHead + 1) % QUEUE_CAPACITY;
    qCount--;
  }
  pending[(qHead + qCount) % QUEUE_CAPACITY] = s;
  qCount++;
}

void dropOldest(size_t n) {
  qHead = (qHead + n) % QUEUE_CAPACITY;
  qCount -= n;
}

// ---------- Wi-Fi and upload ----------

bool waitForWiFi(uint32_t timeoutMs) {
  uint32_t startedMs = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedMs < timeoutMs) delay(20);
  return WiFi.status() == WL_CONNECTED;
}

bool connectWiFi() {
  uint32_t startedMs = millis();
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);

  if (apKnown) {
    // Known channel and access point: no scan. Known address: no waiting for the router to hand one out.
    const bool reuseAddress = ipLocal != 0 && uploadsSinceDhcp < DHCP_EVERY_UPLOADS;
    if (reuseAddress) WiFi.config(IPAddress(ipLocal), IPAddress(ipGateway), IPAddress(ipSubnet), IPAddress(ipDns));
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD, apChannel, apBssid, true);
    if (waitForWiFi(WIFI_FAST_MS)) {
      uploadsSinceDhcp = reuseAddress ? uploadsSinceDhcp + 1 : 0;
    } else {
      Serial.println("  quick Wi-Fi connection failed, starting over");
      apKnown = false;
      ipLocal = 0;
      WiFi.disconnect(true);
      WiFi.config(IPAddress((uint32_t)0), IPAddress((uint32_t)0), IPAddress((uint32_t)0));  // back to asking the router
    }
  }
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    if (!waitForWiFi(WIFI_FULL_MS)) {
      Serial.printf("  Wi-Fi failed after %u ms\n", (unsigned)(millis() - startedMs));
      return false;
    }
    uploadsSinceDhcp = 0;
  }

  memcpy(apBssid, WiFi.BSSID(), 6);
  apChannel = WiFi.channel();
  apKnown = true;
  ipLocal = (uint32_t)WiFi.localIP();
  ipGateway = (uint32_t)WiFi.gatewayIP();
  ipSubnet = (uint32_t)WiFi.subnetMask();
  ipDns = (uint32_t)WiFi.dnsIP();
  Serial.printf("  Wi-Fi up in %u ms, %d dBm\n", (unsigned)(millis() - startedMs), WiFi.RSSI());
  return true;
}

void disconnectWiFi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

void appendNumber(String& s, float v, unsigned decimals) {
  if (isnan(v)) s += "null";
  else s += String(v, decimals);
}

// Sends every queued sample in one request. Returns the HTTP status, or a negative number if the request
// never got an answer.
int postQueue() {
  const uint32_t now = nowSeconds();
  String body;
  body.reserve(160 + qCount * 48);
  body += "{\"p_device_id\":\"" DEVICE_ID "\",\"p_key\":\"" DEVICE_KEY "\",\"p_samples\":[";
  for (size_t i = 0; i < qCount; i++) {
    const Sample& s = pending[(qHead + i) % QUEUE_CAPACITY];
    if (i > 0) body += ',';
    body += "{\"age\":";
    body += now - s.takenS;
    body += ",\"t\":";
    appendNumber(body, s.t, 2);
    body += ",\"h\":";
    appendNumber(body, s.h, 1);
    body += ",\"c\":";
    body += s.co2;
    body += '}';
  }
  body += "],\"p_status\":{\"rssi\":";
  body += WiFi.RSSI();
  body += ",\"fw\":\"" FW_VERSION "\",\"ms\":";
  body += lastAwakeMs;
  body += "}}";

  WiFiClientSecure client;
  client.setInsecure();  // traffic is encrypted, but the server certificate isn't verified
  client.setHandshakeTimeout(TLS_HANDSHAKE_S);
  HTTPClient http;
  http.begin(client, SUPABASE_URL "/rest/v1/rpc/ingest_node");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", SUPABASE_ANON_KEY);
  http.setTimeout(HTTP_TIMEOUT_MS);
  int code = http.POST(body);
  String response = code > 0 ? http.getString() : String();
  http.end();
  if (code == 200) Serial.printf("  uploaded %u sample(s), %s new\n", (unsigned)qCount, response.c_str());
  else Serial.printf("  upload failed: HTTP %d %s\n", code, response.substring(0, 120).c_str());
  return code;
}

// True if the upload worked. A failed upload keeps its samples: the queue is 20 hours long and simply
// loses its oldest readings if an outage lasts longer.
bool upload() {
  if (qCount == 0) return true;
  if (!connectWiFi()) return false;
  const size_t sent = qCount;
  const uint16_t newestCo2 = pending[(qHead + qCount - 1) % QUEUE_CAPACITY].co2;
  int code = postQueue();
  disconnectWiFi();
  if (code == 200) {
    dropOldest(sent);
    uploadedCo2 = newestCo2;
    return true;
  }
  // Connected but no answer at all: the remembered address may belong to another device by now
  if (code < 0) ipLocal = 0;
  return false;
}

// ---------- Main ----------

bool co2NeedsUpload(uint16_t co2) {
  if (isnan(uploadedCo2)) return true;
  if (fabsf(co2 - uploadedCo2) >= CO2_CHANGE_PPM) return true;
  for (float limit : CO2_LIMITS_PPM) {
    if ((uploadedCo2 < limit) != (co2 < limit) && fabsf(co2 - limit) >= CO2_LIMIT_MARGIN_PPM) return true;
  }
  return false;
}

void runCycle() {
  const uint32_t startedMs = millis();
  Serial.printf("Cycle %u, %u sample(s) queued\n", (unsigned)cycles, (unsigned)qCount);
  Sample s;
  const bool measured = measure(s);
  if (measured) enqueue(s);

  readingsSinceUpload++;
  const bool retrying = retriesLeft > 0;
  // In a long outage the regular interval stretches, so a dead router doesn't cost a Wi-Fi attempt every half hour
  const uint8_t interval = UPLOAD_EVERY << min<uint8_t>(failures / 2, MAX_BACKOFF);
  const bool first = cycles == 0 && !skipFirstUpload;
  const bool early = measured && failures == 0 && co2NeedsUpload(s.co2);
  const bool due = first || retrying || readingsSinceUpload >= interval || early;

  if (due && qCount > 0) {
    const bool ok = upload();
    readingsSinceUpload = 0;
    if (ok) {
      failures = 0;
      retriesLeft = 0;
    } else {
      // Quick retries only for a failure out of the blue; after that it is the regular (stretching) interval
      retriesLeft = retrying ? retriesLeft - 1 : (failures == 0 ? UPLOAD_RETRIES : 0);
      if (failures < 255) failures++;
    }
  }
  cycles++;
  lastAwakeMs = millis() - startedMs;
}

void sleepFor(uint32_t seconds) {
  esp_sleep_enable_timer_wakeup(seconds * 1000000ULL);
  esp_deep_sleep_start();
}

void setup() {
  setCpuFrequencyMhz(80);  // plenty for this job
  Serial.begin(115200);
  const esp_reset_reason_t reason = esp_reset_reason();
  if (reason != ESP_RST_DEEPSLEEP) {
    delay(300);  // the USB port needs a moment before it can tell whether a computer is there
    onUsb = HWCDC::isPlugged();
    if (stayAwake()) delay(1200);  // time for the Serial Monitor to attach
    Serial.printf("\nNode starting (" FW_VERSION "), reset reason %d\n", (int)reason);
    const bool crashed = reason == ESP_RST_BROWNOUT || reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT ||
                         reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT;
    if (crashed) {
      // A weak battery that browns out when the radio starts would otherwise reset, upload, and brown out
      // again every few seconds. Sleep first, and don't force an upload on the first cycle.
      skipFirstUpload = true;
      Serial.println("Restarted after a crash or brownout: sleeping before trying again");
      if (!stayAwake()) sleepFor(READ_SECONDS);
    }
  } else {
    onUsb = HWCDC::isPlugged();
  }
  Wire.begin(PIN_SDA, PIN_SCL);
  scd4x.begin(Wire, SCD41_ADDRESS);
}

void loop() {
  const uint32_t startedS = nowSeconds();
  runCycle();
  onUsb = HWCDC::isPlugged();  // by now the port has had time to notice a computer
  const uint32_t elapsedS = nowSeconds() - startedS;
  const uint32_t waitS = elapsedS + 10 < READ_SECONDS ? READ_SECONDS - elapsedS : 10;

  if (stayAwake()) {  // deep sleep would drop the USB connection
    Serial.printf("USB connected: staying awake, next reading in %u s\n", (unsigned)waitS);
    delay(waitS * 1000UL);
    return;
  }
  sleepFor(waitS);
}
