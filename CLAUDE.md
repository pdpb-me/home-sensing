# Home sensing

Two battery devices and a web app. Full brief, screen spec, phases and decisions: **HANDOFF.md** (read it first; it wins over this file). Session state: **PROGRESS.md**.

## Working with Pedro

- Mechanical / product design engineer, not a firmware or backend developer. Explain briefly, no jargon.
- Answer in the language he writes in (usually Italian). Code, comments and commits stay in English.
- Physical actions: one step at a time, exact command, what he should see.
- Never say "done" without proof (serial log, query result, or a photo you ask for).
- Two failures in a row: stop and present options.
- Only the coordinator (main session) talks to Pedro. Every phase goes through the `reviewer` agent before it is closed.

## System

| Part | Where | Hardware | Role |
|---|---|---|---|
| Node | bedroom | XIAO ESP32-C6 + SCD41, LiPo 900 mAh | T / RH / CO2, uploads in batches |
| Hub | living room | Seeed EE05 (XIAO ESP32-S3 Plus) + 2.13" e-paper SSD1680 + SHT41, LiPo 1200 mAh | shows both rooms, message, weather |
| Backend | Supabase project `kaldnqbfgbglhvoummxg` (EU) | | mailbox between the two sleeping devices |
| Web app | `dashboard/`, GitHub Pages, deployed by Actions on push to `main` | | history, message editor, device status |

Devices never talk to each other (no ESP-NOW, no gateway). Each uses home Wi-Fi and Supabase.

## Repo layout

| Path | What |
|---|---|
| `firmware/home_sensor/` | Old "entrance" node (S3 Plus + SCD41 + WeAct 1.54" e-paper, GxEPD2). Reference for SCD41 single-shot, RTC queue, batch upload, LiPo curve. |
| `firmware/sensor_check/` | Old I2C scan sketch |
| `firmware/hub/`, `firmware/node/` | New firmware (to be created) |
| `supabase/schema.sql`, `supabase/migrations/` | Database. Live DB also has `readings.pressure_hpa` and a `readings_archive` table that the repo files do not describe. |
| `dashboard/index.html` | Single-file web app (supabase-js from CDN, magic-link login) |

## Backend facts (verified 2026-11-15)

- Tables: `devices` (id, name, key_hash), `readings`, `viewers`, `readings_archive`. RLS on everywhere; only `viewers` emails can read.
- Devices write through RPC `ingest(p_device_id, p_key, p_readings)` with the anon key plus a per-device secret (SHA-256 in `devices.key_hash`). Readings carry a Unix `ts`.
- No edge functions exist yet. One device registered: `entrance`.

## Hub hardware facts (verified on the board 2026-11-15)

- **Display reset bug:** Seeed_GFX2 configures the panel reset pin (GPIO38) with `gpio_set_direction()` but pulses it with `digitalWrite()`, which the ESP32 core ignores for pins that never went through `pinMode()`. The panel is then never woken from deep sleep and every update after the first times out (2 x 30 s). Fix: `pinMode(38, OUTPUT); digitalWrite(38, HIGH);` right after `display.begin()`. For the same reason `digitalRead()`/`digitalWrite()` on any pin the library owns (43, 44, 10, 38, 4) do nothing unless `pinMode()` was called first.
- Full update takes about 2.3 s and blocks. Read keys with interrupts, not polling.
- Battery: ADC on GPIO1 through a 10k/10k divider (multiply by 2), enabled by **GPIO39** active high (EE05 schematic "ADC_EN"). Not GPIO6, which is free.
- GPIO43 (PWR_EN) switches the `VCC_3V3` rail that feeds the panel **and** the 3V3 hole on the expansion header, so the SHT41 is only powered while GPIO43 is high. The library sets it high in `begin()` and leaves it there.
- Expansion header holes (schematic J2/J4), counted from the USB end. Right row: VBUS, GND, VCC_3V3, GPIO11, GPIO12, GPIO13, NFC. Other row: GPIO5 (SDA), GPIO6 (SCL), PWR_EN, EDP_RES, ADC_EN, GPIO40, NFC. Pedro confirmed the 3V3 hole by measurement; the SHT41 answers at 0x44 on GPIO5/GPIO6.
- Keys: GPIO2, GPIO3, GPIO8, active low, external 10k pull-ups and 100 nF. They bounce: a press and its release each fire a falling-edge interrupt (measured: 2 counts per press).
- XIAO S3 Plus numbering: D11 = GPIO38, D12 = GPIO39, D13 = GPIO40, D14 = GPIO41, D15 = GPIO42, D16 = GPIO10.
- Opening the serial port can reset the board (USB CDC), and reading the flash with esptool stalls at about 315 KB.

## Toolchain (verified 2026-11-15)

- Arduino IDE 2 with bundled CLI: `%LOCALAPPDATA%\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe` (not on PATH).
- Core `esp32:esp32` 3.3.12 (Seeed_GFX2 does not compile on 3.0.7). Boards: `esp32:esp32:XIAO_ESP32C6`, `esp32:esp32:XIAO_ESP32S3_Plus` (what the Seeed wiki recommends for the EE05), `esp32:esp32:XIAO_ESP32S3`. Hub options: `CDCOnBoot=default,PSRAM=opi`.
- Libraries live in `%OneDrive%\Documents\Arduino\libraries`. Present: Seeed_GFX2 (git clone, needs `core.longpaths`), GxEPD2, Sensirion I2C SCD4x, Adafruit SHT4x.
- Ports as confirmed by Pedro on 2026-11-15: COM4 = Hub, COM10 = Node.
- No Supabase CLI, no Deno, no `gh`: admin installs are blocked on this laptop. Use the Supabase MCP tools.
- Serial ports change; confirm with Pedro before flashing. Identify a chip with `esptool flash_id`.

## Rules

- Work on branch `hub-node`. Never push to `main` or change the public site without asking.
- Ask before: Supabase migrations or data changes, edge function deploys, anything that costs money or creates accounts, deleting files you did not create, erasing a configured board's flash/NVS, wiring that differs from HANDOFF.md, reopening a decision in HANDOFF.md section 9 or the screen spec.
- Never: secrets in git (`secrets.h` is git-ignored), `service_role` key in firmware or web app. Never print device keys or `key_hash`.
- The repo is public: no personal data in committed files.
- Battery first: radio time is the main cost (TLS handshake). Log awake time per cycle.
