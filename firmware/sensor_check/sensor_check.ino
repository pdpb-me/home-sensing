// Sensor check for XIAO ESP32-S3 Plus + GY-BME/PMP280 board.
// Scans the I2C bus and reads the chip ID to tell BMP280 from BME280.
// Wiring: SDA -> D4 (GPIO5), SCL -> D5 (GPIO6), VCC -> 3V3, GND -> GND,
//         CSB -> 3V3 (I2C mode), SDO -> GND (address 0x76).

#include <Wire.h>

const int PIN_SDA = 5;
const int PIN_SCL = 6;
const uint8_t REG_CHIP_ID = 0xD0;

int readChipId(uint8_t addr) {
  Wire.beginTransmission(addr);
  Wire.write(REG_CHIP_ID);
  if (Wire.endTransmission(false) != 0) return -1;
  if (Wire.requestFrom(addr, (uint8_t)1) != 1) return -1;
  return Wire.read();
}

void setup() {
  Serial.begin(115200);
  delay(2000);  // give the USB serial time to connect
  Wire.begin(PIN_SDA, PIN_SCL);
}

void loop() {
  Serial.println("\n--- I2C scan ---");
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("Device found at 0x%02X\n", addr);
      found++;
    }
  }

  if (found == 0) {
    Serial.println("Nothing found: check wiring, solder joints, and that VCC is on 3V3.");
  }

  for (uint8_t addr : {0x76, 0x77}) {
    int id = readChipId(addr);
    if (id < 0) continue;
    Serial.printf("Chip ID at 0x%02X = 0x%02X -> ", addr, id);
    switch (id) {
      case 0x60: Serial.println("BME280 (temperature + humidity + pressure)"); break;
      case 0x58: Serial.println("BMP280 (temperature + pressure, NO humidity)"); break;
      case 0x56:
      case 0x57: Serial.println("BMP280 engineering sample (NO humidity)"); break;
      default:   Serial.println("unknown chip"); break;
    }
  }

  delay(5000);
}
