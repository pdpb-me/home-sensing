# Home sensing

Two battery devices and a web app. Full brief, screen spec, phases and decisions: **HANDOFF.md** (read it first; on decisions and the screen spec it wins over this file). Session state: **PROGRESS.md**.

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

- Migrations `hub_node`, `weather_timer` and `backend_hardening` (files in `supabase/migrations/`) are applied and edge function `sync` (version 4, JWT verification off, own device-key check) is deployed. `supabase/schema.sql` is only the starting point; its header says which migrations follow.
- Postgres regular expressions allow a repetition count of at most 255: `{3926}` is accepted when a constraint is created and then fails on every insert. Test a new constraint with a real insert before relying on it.
- Tables: `devices` (now with kind, last_seen_at, battery_v, rssi_dbm, fw_version, awake_ms; `key_hash` is no longer readable through the API), `readings`, `messages`, `hub_settings`, `weather_cache`, `viewers`, `readings_archive`. RLS on everywhere; only `viewers` emails can read.
- Devices and their `kind`, which decides the one endpoint a key works on: `entrance` (`legacy`, retired, old RPC `ingest` with Unix `ts`), `bedroom` (`node`, key set on 2026-11-15), `living` (`hub`, `key_hash = 'unset'` until Phase 5). New keys: `node tools/set_device_key.js <firmware folder>` writes the key into that folder's `secrets.h` and prints only the hash to store.
- Node endpoint: RPC `ingest_node(p_device_id, p_key, p_samples, p_status)` with the publishable key in the `apikey` header; samples carry `age` in seconds. A sample without an age or without any measurement is skipped; a non-numeric value is stored as null. Tested: samples stored, a repeat stored 0, wrong key or wrong kind gives HTTP 403.
- Hub endpoint: `POST /functions/v1/sync`, request and response documented at the top of `supabase/functions/sync/index.ts`. Tested: bedroom reading with age, newest message id, bitmap only when the Hub's `have` differs, `seen` sets `read_at`, a cleared newest message gives `msg: null`, wrong key or kind gives 401. About 0.3 s warm, 1.4 to 2.4 s cold. `sleep` is cut short at the day/night change.
- Weather: Open-Meteo answers HTTP 429 "Daily API request limit exceeded" to Supabase edge functions (they share outgoing IP addresses with other projects), so, with Pedro's ok, the database fetches it instead (`supabase/migrations/20261115_weather_timer.sql`): `pg_cron` runs `weather_request()` every 15 minutes and `weather_store()` a minute later, through `pg_net`, into the one row of `weather_cache`. `sync` only reads that row and returns null weather when it is older than `hub_settings.weather_max_age_s` (3 h). A failed refresh leaves its reason in `weather_cache.last_error`. Do not move the fetch back into an edge function.
- The project uses the new `sb_publishable_...` key (not a JWT): send it in `apikey`, never in `Authorization: Bearer`.

## Hub hardware facts (2026-11-15)

Where this section and HANDOFF.md section 3 disagree on a pin that HANDOFF marks "to be confirmed", this section is newer.

Measured on the board with `firmware/hub_check`:

- **Display reset bug:** Seeed_GFX2 configures the panel reset pin (GPIO38) with `gpio_set_direction()` but pulses it with `digitalWrite()`, and the ESP32 core ignores `digitalWrite()` on a pin that never went through `pinMode()`. The panel is then never woken from deep sleep and every update after the first times out (2 x 30 s). Fix: `pinMode(38, OUTPUT); digitalWrite(38, HIGH);` **before** `display.begin()`. The same applies to any other pin the library sets up that way (43, 44, 10): `digitalWrite()` on them does nothing, and `digitalRead()` on GPIO43 returned 0 while the rail was on, so don't trust it.
- Full update takes about 2.3 s and blocks. Read keys with interrupts, not polling.
- Battery: ADC on GPIO1, enabled by **GPIO39** active high; 2 x the pin voltage gave 4.02 V. Not compared against a multimeter yet. GPIO41 as enable gives 0.
- SHT41 answers at 0x44 on GPIO5 (SDA) / GPIO6 (SCL), powered from the third hole of the right header row, which Pedro measured at 3.3 V.
- Keys: GPIO2, GPIO3, GPIO8, active low. A press and its release each fire a falling edge (2 counts per press with a plain falling-edge counter), so debounce on level, as `hub_check` does.
- Opening the serial port can reset the board (USB CDC), and reading the flash with esptool stalled twice at about 315 KB.

From the EE05 schematic, not exercised yet:

- GPIO43 (PWR_EN) switches the `VCC_3V3` rail that feeds the panel and that header hole, so the SHT41 should lose power when GPIO43 is low. The library sets it high in `begin()`.
- Battery divider is 10k/10k behind a load switch; keys have 10k pull-ups to the always-on 3V3 and 100 nF.
- Expansion header holes (J2/J4). Right row: VBUS, GND, VCC_3V3, GPIO11, GPIO12, GPIO13, NFC. Other row: GPIO5, GPIO6, PWR_EN, EDP_RES, ADC_EN, GPIO40, NFC. Which end is hole 1 is only confirmed for the right row.
- Key wake from deep sleep is untested.
- XIAO S3 Plus numbering (matches the core's variant file): D11 = GPIO38, D12 = GPIO39, D13 = GPIO40, D14 = GPIO41, D15 = GPIO42, D16 = GPIO10.

## Toolchain (verified 2026-11-15)

- Arduino IDE 2 with bundled CLI: `%LOCALAPPDATA%\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe` (not on PATH).
- Core `esp32:esp32` 3.3.12 (Seeed_GFX2 does not compile on 3.0.7). Boards: `esp32:esp32:XIAO_ESP32C6`, `esp32:esp32:XIAO_ESP32S3_Plus` (what the Seeed wiki recommends for the EE05), `esp32:esp32:XIAO_ESP32S3`. Hub FQBN: `esp32:esp32:XIAO_ESP32S3_Plus:CDCOnBoot=default,PSRAM=opi,PartitionScheme=default_8MB` (same partition layout as what is on the board now).
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
