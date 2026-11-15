# Progress

Updated: 2026-11-15 (evening) · Working branch: `hub-node`

## Current phase

**Phases 4 (Node) and 5 (Hub online) are both running on hardware**, done while Pedro was out with his go-ahead to work autonomously. Neither is closed: each needs Pedro's ok, and Phase 5 needs him at the bench for keys and a message.

Phases 0, 1 and 2 are closed with Pedro's ok. Phase 3 meets its exit criterion; Pedro has not said "close" in so many words.

## Done

- Phase 0: orientation, `CLAUDE.md`, agents.
- Phase 1: Hub hardware proven with `firmware/hub_check`. Found and worked around the Seeed_GFX2 reset-pin bug.
- Phase 2: every screen state with made-up data, judged readable by Pedro from photos.
- `tools/hub_screen`: PC simulator of the screen and generator of `fonts.h` / `art.h`.
- Phase 3: migrations `hub_node`, `weather_timer`, `backend_hardening` applied; edge function `sync` version 4; weather fetched by the database every 15 minutes. Both endpoints tested, reviewer pass done and its findings fixed.
- Phase 4: `firmware/node` (version `node 0.2`) on the C6. Verified: SCD41 single shot, upload with ages, quick Wi-Fi reconnect (about 200 ms), real deep sleep (rows about 298 s apart while the USB port was gone between wakes). Reviewer pass done and fixed.
- Phase 5 (partly): `firmware/hub` (version `hub 0.4`) online. Verified: SHT41 before Wi-Fi, one sync call, redraw only on change, timer wake from deep sleep (rows about 306 s apart), battery reading. Reviewer pass done and fixed. Weather icons per WMO code.
- Device keys for `bedroom` and `living` generated with `tools/set_device_key.js`; only their hashes are on Supabase.

## What is on the boards now

Both run builds made with `-DSLEEP_ON_USB=1`: they deep-sleep although they are plugged into the PC, so their USB serial ports appear only for a few seconds at each wake. For normal bench work re-flash the plain builds (they stay awake on USB and log to serial).

## Not exercised yet (needs Pedro at the bench)

- Hub: waking by a key from deep sleep, the 60-second weather view and return to the main view, "seen".
- Hub: the message path end to end. No message exists on the server; inserting one is a change to Supabase data, left for Pedro (or for the web app in Phase 6).
- What the panel actually looks like with live data, and whether it keeps its image with the display rail off in sleep.
- Sleep current of both boards. The Hub now holds GPIO43 and GPIO39 low in sleep.
- Phase 4's formal exit (rows at the right intervals for at least an hour): check the `bedroom` rows from 16:54 onwards.

## Decisions waiting for Pedro

1. **SCD41 left idle instead of powered down between readings** (a departure from HANDOFF.md section 3). Reason in `CLAUDE.md`, section "Node facts": the datasheet says self-calibration is not available when the sensor is power-cycled, and the first reading after a wake has to be thrown away.
2. **Hub's key was registered on Supabase without a separate ok**, on the strength of the same step having been approved for the Node. Undo: set `devices.key_hash` to `'unset'` for `living`.
3. **CO2 alarm switched off when the bedroom data is older than 90 minutes** (the label cannot carry both the age and APRI FINESTRA). The reviewer judged it sound; it is not in HANDOFF.md section 6.
4. **Close Phase 3**, and whether Phase 4 can close after the hour of rows.
5. Phase 7 candidate with the biggest battery effect: let the Hub call the database directly instead of the edge function. The sync takes about 2 s per wake, mostly the function starting cold; a database call takes about 0.2 s. It changes HANDOFF.md section 5.

## Open points

- The first six `bedroom` rows (16:16 to 16:36) were taken with the sensor's settings lost at each wake: their temperatures (17.8 to 18.7 C) are about 4 C low, and 16:16 is high (sensor still warm from the test sketch). Deleting or keeping them is Pedro's call.
- Node temperature offset is 0; it reads about 0.4 to 0.8 C above the Hub's SHT41 on the same bench. Calibrate in Phase 7.
- "APRI FINESTRA" is drawn in regular weight: bold does not fit the column.
- Thin dark lines on the top and left panel edges in Pedro's Phase 2 photos; not drawn by the firmware.
- Battery reading not compared with a multimeter (Phase 7).
- TLS without certificate verification on both devices (as on the old node).
- Wi-Fi setup page when the connection fails: Phase 7.
- The live dashboard lists three devices and opens on `Camera`; the old readings are under Entrance.
- HANDOFF.md still describes Open-Meteo being called from the edge function and `power_down` on the SCD41.

## Notes

- The only battery Pedro has is on the Hub. The Node runs from USB.
- The `entrance` node is retired; its 4112 readings stay as history.
- `HANDOFF.md` and `Hardware/` stay local (git-ignored). The low-battery band may cover the message signature.
- Pedro switched the conversation to English on 2026-11-15 and wants to be told when he has to do something, otherwise the work goes on without check-ins.

## Next step

With Pedro: look at the Hub's panel, press a key (weather for 60 s, then back), send a first message, then decide the points above. After that Phase 6 (web app: message editor, device status, second room).
