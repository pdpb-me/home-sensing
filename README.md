# Home sensing

Battery/USB-powered room sensors that show readings on an e-paper display and upload them to a cloud dashboard.

```
Sensor node (XIAO ESP32-S3) --Wi-Fi--> Supabase (database + login) <-- Dashboard (GitHub Pages) <-- phone / PC
```

## Folders

| Folder | What's inside |
|---|---|
| `firmware/sensor_check` | I2C scan + chip ID test for the BMP280/BME280 |
| `firmware/home_sensor` | Main sensor firmware (e-paper display, sensors) |
| `supabase/` | Database setup SQL |
| `dashboard/` | Web dashboard |

## Hardware (node "Entrance")

- Seeed XIAO ESP32-S3 Plus
- BMP280 (temperature + pressure); SHT40 planned for humidity
- WeAct 1.54" e-paper, 200x200, SSD1681

| Signal | XIAO pin |
|---|---|
| I2C SDA / SCL | D4 (GPIO5) / D5 (GPIO6) |
| E-paper SCK / MOSI | D8 (GPIO7) / D10 (GPIO9) |
| E-paper CS / DC / RST / BUSY | D1 (GPIO2) / D2 (GPIO3) / D3 (GPIO4) / D0 (GPIO1) |
