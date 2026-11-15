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

- Low-battery band covers the message signature. Pedro to decide.
- "APRI FINESTRA" is drawn in regular weight: bold does not fit the column.
- If bedroom data is stale while the CO2 alarm is latched, the screen keeps APRI FINESTRA and hides the age. Suggest dropping the alarm on stale data (Phase 5).
- Weather icon is always rain; icons per WMO code come with live weather (Phase 5).
- Thin dark lines on the top and left panel edges in Pedro's photos; not drawn by the firmware. Check whether they are ghosting from the test frame or the panel border.
- Battery reading not compared with a multimeter (Phase 7). Not exercised: SHT41 losing power when GPIO43 is low, key wake from deep sleep.
- `supabase/schema.sql` still describes the old schema.
- Device keys for `bedroom` and `living` are not generated yet (needed in Phases 4 and 5).
- The Phase 3 SQL and edge function have not been through the reviewer agent yet.

## Notes

- The only battery Pedro has is on the Hub. The Node runs from USB until a second one arrives.
- Pedro switched the conversation to English on 2026-11-15.
- The `entrance` node has not uploaded since 2026-11-14 21:29 UTC; its 4112 readings are untouched.

## Next step

Reviewer pass on the backend, then Phase 4 (Node firmware): single-shot SCD41 every 5 minutes, batch upload through `ingest_node`, deep sleep. Needs a device key for `bedroom` and Wi-Fi credentials in a git-ignored `secrets.h`.

## Needed from Pedro

- Ok to close Phase 3.
- What happens to `entrance`: does it stay, or did its hardware move to the Node?
- `HANDOFF.md` and `Hardware/` are not in git: should they go in the (public) repo or stay local?
