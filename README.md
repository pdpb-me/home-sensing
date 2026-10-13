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

## Setup

1. Run `supabase/schema.sql` in the Supabase SQL Editor.
2. Register each node: insert into `devices` its id, name and the SHA-256 hash of a random key.
3. Copy `firmware/home_sensor/secrets.example.h` to `secrets.h` and fill it in (Wi-Fi, Supabase URL + publishable key, device id + key).

## Known limitations

- Uploads use HTTPS but don't verify the server certificate yet (`setInsecure()`). Worst case on a compromised network, someone could capture a device key, which only allows adding readings for that device.
