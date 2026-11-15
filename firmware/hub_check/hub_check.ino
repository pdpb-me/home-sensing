// Hub hardware check: Seeed XIAO ePaper Display Board EE05 (XIAO ESP32-S3 Plus) + 2.13" e-paper + SHT41.
// Proves the I2C wiring, the SHT41, the display, the three keys and the battery sense pins.
// Prints a report on Serial every 5 s; pressing a key redraws the display with the press counts.
//
// Board settings: XIAO_ESP32S3, USB CDC On Boot = Enabled, PSRAM = OPI.
// Library: Seeed_GFX2 (from GitHub, not the Library Manager).
//
// Wiring: SHT41 VIN -> 3V3 header hole (VCC_3V3, switched on by the display library through GPIO43), GND -> GND, SDA -> D4 (GPIO5), SCL -> D5 (GPIO6)

#include <Wire.h>
#include <Seeed_GFX.h>
#include "board/boards/XIAO_ePaper_Boards.h"
#include "panel/configs/Seeed_Panel_Configs.h"
#include "driver/epaper/Driver_SSD1680.h"
#include "panel/Panel_EPaper.h"

const int PIN_SDA = 5;
const int PIN_SCL = 6;
const int PIN_KEYS[3] = {2, 3, 8};         // Key1, Key2, Key3: active low, 10k pull-ups on the board
const int PIN_PANEL_RESET = 38;            // EDP_RES
const int PIN_BATTERY = 1;                 // BAT_ADC: VBAT through a 10k/10k divider
const int PIN_BATTERY_ENABLE = 39;         // ADC_EN, active high: connects the divider to the battery
const uint8_t SHT41_ADDRESS = 0x44;
const uint32_t REPORT_MS = 5000;
// Wiring test: holds SDA at 3.3 V and SCL at 0 V so each wire can be checked with a multimeter at the sensor
const bool WIRING_TEST = false;

Seeed_GFX display;
bool displayOk = false;
uint32_t lastReportMs = 0;

// Presses are counted in interrupts, so none is missed while the display redraws (about 2.3 s)
volatile uint32_t keyPresses[3] = {0, 0, 0};
volatile uint32_t keyLastMs[3] = {0, 0, 0};
uint32_t shownPresses[3] = {0, 0, 0};

void IRAM_ATTR onKey(void* arg) {
  int i = (int)(intptr_t)arg;
  uint32_t now = millis();
  if (now - keyLastMs[i] < 150) return;  // contact bounce
  keyLastMs[i] = now;
  keyPresses[i] = keyPresses[i] + 1;
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

// The divider is only connected while the enable pin is high, so it draws nothing in between
float readBatteryVolts() {
  digitalWrite(PIN_BATTERY_ENABLE, HIGH);
  delay(20);
  uint32_t sum = 0;
  for (int i = 0; i < 16; i++) sum += analogReadMilliVolts(PIN_BATTERY);
  digitalWrite(PIN_BATTERY_ENABLE, LOW);
  return sum / 16 / 1000.0f * 2.0f;
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
  uint32_t startedMs = millis();
  display.update();
  Serial.printf("Display update \"%s\" took %u ms\n", line, (unsigned)(millis() - startedMs));
}

void report() {
  float t = NAN, h = NAN;
  bool shtOk = readSht41(t, h);
  scanI2c();
  if (shtOk) Serial.printf("SHT41: T=%.2f C  RH=%.1f %%\n", t, h);
  else Serial.println("SHT41: no answer");
  Serial.printf("Battery: %.2f V\n", readBatteryVolts());
  Serial.printf("Key presses since boot: %u %u %u   Levels now: %d %d %d (0 = pressed)   Display: %s\n",
                (unsigned)keyPresses[0], (unsigned)keyPresses[1], (unsigned)keyPresses[2],
                digitalRead(PIN_KEYS[0]), digitalRead(PIN_KEYS[1]), digitalRead(PIN_KEYS[2]),
                displayOk ? "ok" : "FAILED");
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\nHub check starting");

  pinMode(PIN_BATTERY_ENABLE, OUTPUT);
  digitalWrite(PIN_BATTERY_ENABLE, LOW);
  Wire.begin(PIN_SDA, PIN_SCL);

  displayOk = display.begin<Board_XIAO_ePaper_EE05, Config_Seeed_ePaper_2inch13_BW_SSD1680>();
  if (displayOk) display.setRotation(3);  // 250x122, long edge horizontal
  else Serial.println(display.lastResult().message);

  // Seeed_GFX2 sets the reset pin up with gpio_set_direction() but pulses it with digitalWrite(), and the
  // ESP32 core ignores digitalWrite() on a pin that never went through pinMode(). Without this the panel
  // is never reset out of deep sleep, so every update after the first one times out (2 x 30 s).
  pinMode(PIN_PANEL_RESET, OUTPUT);
  digitalWrite(PIN_PANEL_RESET, HIGH);

  // After the display is set up, in case the library reconfigures pins
  for (int i = 0; i < 3; i++) {
    pinMode(PIN_KEYS[i], INPUT);
    attachInterruptArg(PIN_KEYS[i], onKey, (void*)(intptr_t)i, FALLING);
  }
  float t = NAN, h = NAN;
  bool shtOk = readSht41(t, h);
  uint32_t startedMs = millis();
  drawScreen(WIRING_TEST ? "SDA 3.3V SCL 0V" : "Press a key", t, h, shtOk);
  Serial.printf("Display update took %u ms\n", (unsigned)(millis() - startedMs));
  report();
}

void loop() {
  if (WIRING_TEST) {
    Wire.end();
    pinMode(PIN_SDA, OUTPUT);
    pinMode(PIN_SCL, OUTPUT);
    digitalWrite(PIN_SDA, HIGH);
    digitalWrite(PIN_SCL, LOW);
    Serial.println("Wiring test: SDA (GPIO5) held at 3.3 V, SCL (GPIO6) held at 0 V, 3V3 rail on");
    delay(2000);
    return;
  }
  // Second, independent check: the pin level itself, in case the interrupts don't fire
  static bool wasDown[3] = {false, false, false};
  static uint32_t polledPresses[3] = {0, 0, 0};
  bool changed = false;
  for (int i = 0; i < 3; i++) {
    bool down = digitalRead(PIN_KEYS[i]) == LOW;
    if (down != wasDown[i]) {
      wasDown[i] = down;
      if (down) polledPresses[i]++;
      Serial.printf("Key%d level went %s (polled presses %u, interrupt presses %u)\n", i + 1, down ? "LOW" : "high",
                    (unsigned)polledPresses[i], (unsigned)keyPresses[i]);
    }
    uint32_t count = max(polledPresses[i], (uint32_t)keyPresses[i]);
    if (count == shownPresses[i]) continue;
    shownPresses[i] = count;
    changed = true;
  }
  if (changed) {
    char line[24];
    snprintf(line, sizeof(line), "K1:%u K2:%u K3:%u", (unsigned)shownPresses[0], (unsigned)shownPresses[1],
             (unsigned)shownPresses[2]);
    float t = NAN, h = NAN;
    bool shtOk = readSht41(t, h);
    drawScreen(line, t, h, shtOk);
  }
  if (millis() - lastReportMs >= REPORT_MS) {
    lastReportMs = millis();
    report();
  }
  delay(20);
}
