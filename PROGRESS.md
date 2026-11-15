# Progress

Updated: 2026-11-15 · Working branch: `hub-node`

## Current phase

**Phase 3 · Backend**: applied and working except the weather. Blocked on a decision from Pedro about where the weather comes from (see below).

Phases 0, 1 and 2 are closed with Pedro's ok (2026-11-15).

## Done

- Phase 0: orientation, `CLAUDE.md`, agents.
- Phase 1: Hub hardware proven with `firmware/hub_check` (SHT41, display, keys, battery pins). Found and worked around the Seeed_GFX2 reset-pin bug.
- Phase 2: `firmware/hub` draws every screen state with made-up data; Pedro judged the panel readable from photos ("fine for now"). Eight states step one per key press. Reviewer findings fixed: font licence file, humidity capped at 99 %, word decided on the printed value, keys read by their own task.
- `tools/hub_screen`: PC simulator of the screen and generator of `fonts.h` / `art.h` (`npm install`, then `node screens.js`, `node export.js`).
- Phase 3: migration `hub_node` applied, edge function `sync` deployed (version 2), both endpoints tested with temporary devices that were removed afterwards. Details in `CLAUDE.md`.

## Blocked: weather source

Open-Meteo refuses requests coming from Supabase edge functions (HTTP 429, shared outgoing IPs). Options put to Pedro:

1. The database fetches Open-Meteo itself every 15 minutes (extensions `pg_cron` + `pg_net`) and `sync` only reads the cache. Not tested: it works only if the database's outgoing IP is not rate-limited too.
2. Switch to MET Norway (free, no key, wants an identifying User-Agent).
3. The Hub fetches the weather itself over home Wi-Fi: a second HTTPS call, against the brief.

## Open points

- Low-battery band covers the message signature. Pedro to decide.
- "APRI FINESTRA" is drawn in regular weight: bold does not fit the column.
- If bedroom data is stale while the CO2 alarm is latched, the screen keeps APRI FINESTRA and hides the age. Suggest dropping the alarm on stale data (Phase 5).
- Weather icon is always rain; icons per WMO code come with live weather (Phase 5).
- Thin dark lines on the top and left panel edges in Pedro's photos; not drawn by the firmware. Check whether they are ghosting from the test frame or the panel border.
- Battery reading not compared with a multimeter (Phase 7). Not exercised: SHT41 losing power when GPIO43 is low, key wake from deep sleep.
- `supabase/schema.sql` still describes the old schema.
- Device keys for `bedroom` and `living` are not generated yet (needed in Phases 4 and 5).

## Notes

- The only battery Pedro has is on the Hub. The Node runs from USB until a second one arrives.
- Pedro switched the conversation to English on 2026-11-15.
- The `entrance` node has not uploaded since 2026-11-14 21:29 UTC; its 4112 readings are untouched.

## Next step

Settle the weather source, re-test `sync` with weather, then ask Pedro to close Phase 3. After that Phase 4 (Node firmware).

## Needed from Pedro

- Weather source decision.
- What happens to `entrance`: does it stay, or did its hardware move to the Node?
- `HANDOFF.md` and `Hardware/` are not in git: should they go in the (public) repo or stay local?
