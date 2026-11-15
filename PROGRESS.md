# Progress

Updated: 2026-11-15 · Working branch: `hub-node`

## Current phase

**Phase 3 · Backend**: applied, tested and working, weather included. Waiting for Pedro to close it.

Phases 0, 1 and 2 are closed with Pedro's ok (2026-11-15).

## Done

- Phase 0: orientation, `CLAUDE.md`, agents.
- Phase 1: Hub hardware proven with `firmware/hub_check` (SHT41, display, keys, battery pins). Found and worked around the Seeed_GFX2 reset-pin bug.
- Phase 2: `firmware/hub` draws every screen state with made-up data; Pedro judged the panel readable from photos ("fine for now"). Eight states step one per key press. Reviewer findings fixed: font licence file, humidity capped at 99 %, word decided on the printed value, keys read by their own task.
- `tools/hub_screen`: PC simulator of the screen and generator of `fonts.h` / `art.h` (`npm install`, then `node screens.js`, `node export.js`).
- Phase 3: migrations `hub_node` and `weather_timer` applied, edge function `sync` deployed (version 3). Both device endpoints tested with temporary devices that were removed afterwards. Details in `CLAUDE.md`.

## Weather source (decided 2026-11-15)

Open-Meteo refuses requests from Supabase edge functions (HTTP 429, shared outgoing IPs). Pedro chose the database timer: `pg_cron` + `pg_net` fetch Open-Meteo every 15 minutes into `weather_cache`, and `sync` only reads it. Tested: `sync` returned now 9.4, max 9.5, rain 60 %, code 2.

## Open points

- "APRI FINESTRA" is drawn in regular weight: bold does not fit the column.
- If bedroom data is stale while the CO2 alarm is latched, the screen keeps APRI FINESTRA and hides the age. Suggest dropping the alarm on stale data (Phase 5).
- Weather icon is always rain; icons per WMO code come with live weather (Phase 5).
- Thin dark lines on the top and left panel edges in Pedro's photos; not drawn by the firmware. Check whether they are ghosting from the test frame or the panel border.
- Battery reading not compared with a multimeter (Phase 7). Not exercised: SHT41 losing power when GPIO43 is low, key wake from deep sleep.
- `supabase/schema.sql` still describes the old schema.
- Device keys for `bedroom` and `living` are not generated yet (needed in Phases 4 and 5).
- Reviewer pass on the backend done. Its fixes are written but NOT applied: `supabase/migrations/20261115_backend_hardening.sql` and a new `sync` (repo version is ahead of the deployed version 3). Waiting for Pedro's ok.
- The live dashboard now lists three devices and opens on `Camera`, which has no readings until the Node uploads (it picks the first by name). The old readings are under Entrance in the list.
- For the Node and Hub firmware: a non-200 answer has no `sleep`, so fall back to a default interval; never retry a rejected batch for ever; check the decoded bitmap is exactly 2944 bytes before copying it.
- HANDOFF.md section 5 still says the edge function calls Open-Meteo; the database does now.

## Notes

- The only battery Pedro has is on the Hub. The Node runs from USB until a second one arrives.
- Pedro switched the conversation to English on 2026-11-15.
- The `entrance` node is retired (Pedro, 2026-11-15). It stopped uploading on 2026-11-14 21:29 UTC; its 4112 readings stay as history.
- Decided by Pedro on 2026-11-15: the low-battery band may cover the message signature; `HANDOFF.md` and `Hardware/` stay local (they are git-ignored now).
- `firmware/node/secrets.h` exists (git-ignored): Wi-Fi and Supabase lines copied from the old node's file by `tools/make_node_secrets.js` without showing them; `DEVICE_KEY` is still a placeholder.
- The Node is on COM10 (ESP32-C6, confirmed with esptool).

## Next step

Apply the backend hardening once Pedro agrees, then Phase 4 (Node firmware): single-shot SCD41 every 5 minutes, batch upload through `ingest_node`, deep sleep. Needs a device key for `bedroom` and Wi-Fi credentials in a git-ignored `secrets.h`.

## Needed from Pedro

- Ok to apply the backend hardening and to store the Node's key hash, then to close Phase 3.
