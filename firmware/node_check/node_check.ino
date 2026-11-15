// Node hardware check: XIAO ESP32-C6 + SCD41.
// Proves the I2C wiring and the sensor before the real firmware goes on: scans the bus, then takes a
// single-shot CO2 / temperature / humidity measurement every 10 s and prints it on Serial.
//
// Board settings: XIAO_ESP32C6, USB CDC On Boot = Enabled.
// Library (Arduino Library Manager): Sensirion I2C SCD4x (pulls in Sensirion Core).
//
// Wiring: SCD41 VDD -> 3V3, GND -> GND, SDA -> D9 (GPIO20), SCL -> D10 (GPIO18). Address 0x62.

#include <Wire.h>
#include <SensirionI2cScd4x.h>

const int PIN_SDA = 20;
const int PIN_SCL = 18;
const uint32_t REPORT_MS = 10000;

SensirionI2cScd4x scd4x;
uint32_t lastReportMs = 0;
uint32_t reports = 0;

void scanI2c() {
  Serial.print("I2C devices:");
  int found = 0;
  for (uint8_t address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.printf(" 0x%02X", address);
      found++;
    }
  }
  Serial.println(found ? "" : " none");
}

void report() {
  scanI2c();
  uint16_t co2 = 0;
  float t = NAN, h = NAN;
  uint32_t startedMs = millis();
  int16_t measureError = scd4x.measureSingleShot();  // blocks for about 5 s
  int16_t readError = measureError ? measureError : scd4x.readMeasurement(co2, t, h);
  uint32_t tookMs = millis() - startedMs;
  if (readError != 0) {
    Serial.printf("SCD41: no answer (error %d after %u ms)\n", readError, (unsigned)tookMs);
  } else if (co2 == 0) {
    Serial.println("SCD41: answered, but the reading is not valid yet");
  } else {
    // The first reading after power-up is not reliable; the ones after it are
    Serial.printf("SCD41: CO2=%u ppm  T=%.2f C  RH=%.1f %%  (%u ms)%s\n", co2, t, h, (unsigned)tookMs,
                  reports == 0 ? "  [first after start: ignore]" : "");
  }
  reports++;
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\nNode check starting");

  Wire.begin(PIN_SDA, PIN_SCL);
  scd4x.begin(Wire, SCD41_I2C_ADDR_62);
  // The sensor keeps measuring through an ESP32 reset, and only accepts commands when stopped
  scd4x.wakeUp();
  scd4x.stopPeriodicMeasurement();
  delay(500);
  report();
  lastReportMs = millis();
}

void loop() {
  if (millis() - lastReportMs >= REPORT_MS) {
    lastReportMs = millis();
    report();
  }
  delay(20);
}
