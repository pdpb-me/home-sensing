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
- Sensirion SCD41 (CO2 + temperature + humidity)
- WeAct 1.54" e-paper, 200x200, SSD1681
- LiPo ~1000 mAh on BAT+/BAT-, sensed through a 100k/100k divider (0.22 uF on the midpoint)

| Signal | XIAO pin |
|---|---|
| I2C SDA / SCL | D4 (GPIO5) / D5 (GPIO6) |
| E-paper SCK / MOSI | D8 (GPIO7) / D10 (GPIO9) |
| E-paper CS / DC / RST / BUSY | D1 (GPIO2) / D2 (GPIO3) / D3 (GPIO4) / D0 (GPIO1) |
| Battery sense (VBAT/2) | D9 (GPIO8) |

## Setup

1. Run `supabase/schema.sql` in the Supabase SQL Editor (fresh project). Existing projects apply `supabase/migrations/` instead.
2. Register each node: insert into `devices` its id, name and the SHA-256 hash of a random key.
3. Copy `firmware/home_sensor/secrets.example.h` to `secrets.h` and fill it in (Wi-Fi, Supabase URL + publishable key, device id + key).

## Power

The node deep-sleeps between readings: it reads and redraws every 5 minutes and uploads every 30 minutes, or right away when the air turns poor. It stays awake while connected to a computer over USB, so to upload a sketch, plug it in and wait up to 5 minutes for the next wake, or press RESET.

## Known limitations

- Uploads use HTTPS but don't verify the server certificate yet (`setInsecure()`). Worst case on a compromised network, someone could capture a device key, which only allows adding readings for that device.
