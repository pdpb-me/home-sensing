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

## Toolchain (verified 2026-11-15)

- Arduino IDE 2 with bundled CLI: `%LOCALAPPDATA%\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe` (not on PATH).
- Core `esp32:esp32` 3.0.7. Boards: `esp32:esp32:XIAO_ESP32C6`, `esp32:esp32:XIAO_ESP32S3` (no "S3 Plus" entry in this core version).
- Libraries live in `%OneDrive%\Documents\Arduino\libraries`. Present: GxEPD2, Sensirion I2C SCD4x, Adafruit SHT4x. **Missing: Seeed_GFX2.**
- No Supabase CLI, no Deno, no `gh`: admin installs are blocked on this laptop. Use the Supabase MCP tools.
- Serial ports change; confirm with Pedro before flashing. Identify a chip with `esptool flash_id`.

## Rules

- Work on branch `hub-node`. Never push to `main` or change the public site without asking.
- Ask before: Supabase migrations or data changes, edge function deploys, anything that costs money or creates accounts, deleting files you did not create, erasing a configured board's flash/NVS, wiring that differs from HANDOFF.md, reopening a decision in HANDOFF.md section 9 or the screen spec.
- Never: secrets in git (`secrets.h` is git-ignored), `service_role` key in firmware or web app. Never print device keys or `key_hash`.
- The repo is public: no personal data in committed files.
- Battery first: radio time is the main cost (TLS handshake). Log awake time per cycle.
