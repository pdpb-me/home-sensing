// Hub hardware check: Seeed XIAO ePaper Display Board EE05 (XIAO ESP32-S3 Plus) + 2.13" e-paper + SHT41.
// Proves the I2C wiring, the SHT41, the display, the three keys and the battery sense pins.
// Prints a report on Serial every 5 s; pressing a key redraws the display with that key's name.
//
// Board settings: XIAO_ESP32S3, USB CDC On Boot = Enabled, PSRAM = OPI.
// Library: Seeed_GFX2 (from GitHub, not the Library Manager).
//
// Wiring: SHT41 VIN -> 3V3 header hole (switched by GPIO43), GND -> GND, SDA -> D4 (GPIO5), SCL -> D5 (GPIO6)

#include <Wire.h>
#include <Seeed_GFX.h>
#include "board/boards/XIAO_ePaper_Boards.h"
#include "panel/configs/Seeed_Panel_Configs.h"
#include "driver/epaper/Driver_SSD1680.h"
#include "panel/Panel_EPaper.h"

const int PIN_SDA = 5;
const int PIN_SCL = 6;
const int PIN_KEYS[3] = {2, 3, 8};         // Key1, Key2, Key3: active low, 10k pull-ups on the board
const int PIN_PANEL_POWER = 43;            // PWR_EN: switches VCC_3V3, which feeds the display and the 3V3 header hole
const int PIN_BATTERY = 1;                 // BAT_ADC: VBAT through a 10k/10k divider
const int PIN_BATTERY_ENABLE_A = 39;       // ADC_EN per the EE05 schematic's pin naming
const int PIN_BATTERY_ENABLE_B = 41;       // ADC_EN if the other XIAO pin naming applies
const uint8_t SHT41_ADDRESS = 0x44;
const uint32_t REPORT_MS = 5000;

Seeed_GFX display;
bool displayOk = false;
bool keyDown[3] = {false, false, false};
uint32_t lastReportMs = 0;

// The display library may switch the rail off after an update; the SHT41 on the header needs it on
void powerRailOn() {
  pinMode(PIN_PANEL_POWER, OUTPUT);
  digitalWrite(PIN_PANEL_POWER, HIGH);
  delay(5);  // SHT41 power-up time is 1 ms
}

// High-precision measurement; false if the sensor doesn't answer or a checksum is wrong
bool readSht41(float& tempC, float& humidity) {
  Wire.beginTransmission(SHT41_ADDRESS);
  Wire.write(0xFD);
  if (Wire.endTransmission() != 0) return false;
  delay(10);
  uint8_t b[6];
  if (Wire.requestFrom(SHT41_ADDRESS, (uint8_t)6) != 6) return false;
  for (int i = 0; i < 6; i++) b[i] = Wire.read();
  for (int word = 0; word < 2; word++) {
    uint8_t crc = 0xFF;
    for (int i = 0; i < 2; i++) {
      crc ^= b[word * 3 + i];
      for (int bit = 0; bit < 8; bit++) crc = crc & 0x80 ? (crc << 1) ^ 0x31 : crc << 1;
    }
    if (crc != b[word * 3 + 2]) return false;
  }
  tempC = -45.0f + 175.0f * ((b[0] << 8) | b[1]) / 65535.0f;
  humidity = constrain(-6.0f + 125.0f * ((b[3] << 8) | b[4]) / 65535.0f, 0.0f, 100.0f);
  return true;
}

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

// Millivolts on the battery sense pin with the given enable pin high (-1 = none)
uint32_t batterySenseMv(int enablePin) {
  if (enablePin >= 0) digitalWrite(enablePin, HIGH);
  delay(20);
  uint32_t sum = 0;
  for (int i = 0; i < 16; i++) sum += analogReadMilliVolts(PIN_BATTERY);
  if (enablePin >= 0) digitalWrite(enablePin, LOW);
  return sum / 16;
}

void drawScreen(const char* line, float tempC, float humidity, bool shtOk) {
  if (!displayOk) return;
  char buf[40];
  display.fillScreen(TFT_WHITE);
  display.setTextColor(TFT_BLACK);
  display.drawRect(0, 0, display.width(), display.height(), TFT_BLACK);
  display.setTextSize(2);
  display.drawString("Hub check", 10, 8);
  display.drawLine(10, 30, display.width() - 10, 30, TFT_BLACK);
  if (shtOk) snprintf(buf, sizeof(buf), "%.1f C  %.0f %%", tempC, humidity);
  else snprintf(buf, sizeof(buf), "SHT41 missing");
  display.drawString(buf, 10, 42);
  display.drawString(line, 10, 72);
  display.setTextSize(1);
  snprintf(buf, sizeof(buf), "%d x %d", display.width(), display.height());
  display.drawString(buf, 10, 104);
  display.update();
  powerRailOn();
}

void report() {
  Serial.printf("3V3 rail enable (GPIO%d) reads %d before forcing it on\n", PIN_PANEL_POWER, digitalRead(PIN_PANEL_POWER));
  powerRailOn();
  float t = NAN, h = NAN;
  bool shtOk = readSht41(t, h);
  scanI2c();
  if (shtOk) Serial.printf("SHT41: T=%.2f C  RH=%.1f %%\n", t, h);
  else Serial.println("SHT41: no answer");
  Serial.printf("Battery sense on GPIO%d: %u mV idle, %u mV with GPIO%d high, %u mV with GPIO%d high\n",
                PIN_BATTERY, (unsigned)batterySenseMv(-1),
                (unsigned)batterySenseMv(PIN_BATTERY_ENABLE_A), PIN_BATTERY_ENABLE_A,
                (unsigned)batterySenseMv(PIN_BATTERY_ENABLE_B), PIN_BATTERY_ENABLE_B);
  Serial.printf("Keys: %d %d %d (1 = pressed)   Display: %s\n", keyDown[0], keyDown[1], keyDown[2],
                displayOk ? "ok" : "FAILED");
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\nHub check starting");

  for (int pin : PIN_KEYS) pinMode(pin, INPUT);
  pinMode(PIN_BATTERY_ENABLE_A, OUTPUT);
  pinMode(PIN_BATTERY_ENABLE_B, OUTPUT);
  digitalWrite(PIN_BATTERY_ENABLE_A, LOW);
  digitalWrite(PIN_BATTERY_ENABLE_B, LOW);
  Wire.begin(PIN_SDA, PIN_SCL);

  displayOk = display.begin<Board_XIAO_ePaper_EE05, Config_Seeed_ePaper_2inch13_BW_SSD1680>();
  if (displayOk) display.setRotation(3);  // 250x122, long edge horizontal
  else Serial.println(display.lastResult().message);

  powerRailOn();
  float t = NAN, h = NAN;
  bool shtOk = readSht41(t, h);
  uint32_t startedMs = millis();
  drawScreen("Press a key", t, h, shtOk);
  Serial.printf("Display update took %u ms\n", (unsigned)(millis() - startedMs));
  report();
}

void loop() {
  for (int i = 0; i < 3; i++) {
    bool down = digitalRead(PIN_KEYS[i]) == LOW;
    if (down == keyDown[i]) continue;
    keyDown[i] = down;
    Serial.printf("Key%d %s\n", i + 1, down ? "pressed" : "released");
    if (down) {
      char line[16];
      snprintf(line, sizeof(line), "Key%d pressed", i + 1);
      float t = NAN, h = NAN;
      bool shtOk = readSht41(t, h);
      drawScreen(line, t, h, shtOk);
    }
  }
  if (millis() - lastReportMs >= REPORT_MS) {
    lastReportMs = millis();
    report();
  }
  delay(20);
}
