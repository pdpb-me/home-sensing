# Progress

Updated: 2026-11-15 (evening) · Working branch: `hub-node`

## Current phase

**Phase 5 (Hub online) is open.** Every part of the cycle has been exercised; what is left is its exit criterion: the Hub runs by itself for an evening without anyone touching it. The run started on 2026-11-15 at about 19:10. Check the `living` rows the next time a session opens (a row about every 5 minutes until 23:30, every 30 minutes at night), then ask Pedro to close it.

**Hub firmware is `hub 0.5` since 19:48 on 2026-11-15**: at Pedro's request a second key press during the weather view puts the message back at once instead of after 60 s (a change to HANDOFF.md section 5, "Tasti"; that wake does not sync). Flashed and reporting; the key behaviour itself still has to be tried by Pedro. The evening run for Phase 5 counts from 19:48.

**Rooms tab side by side** (Pedro's request): Camera and Salotto share the tab, one small screen each, both values per row, one plot per room. Published at 19:59 (`main` = `4827afd`); Pedro looked at it on his phone and said it looks good.

**Phase 6 met its exit criterion** at about 19:40: Pedro sent messages from his phone (ids 13 and 14), saw them on the Hub, and the first was marked seen at 19:40:53, which he saw in the web app. It needs his word to close. What follows is how it got there.

**Phase 6 (web app) is online** (published with his ok on 2026-11-15, evening: `main` fast-forwarded to `hub-node` and pushed; GitHub Pages serves the new page). It was started with Pedro's ok on 2026-11-15 while Phase 5's evening run goes on. `dashboard/index.html` on `hub-node` has three tabs: Rooms (as before, now for Camera, Salotto and the old Entrance, with only the rows each device measures), Message (the editor and the list of sent messages) and Devices (last contact, battery, Wi-Fi, awake time, firmware). No migration was needed: the backend from Phase 3 already had everything.

Checked so far: in a local browser with made-up data (text in two fonts, accents, emoji, freehand pen and eraser, undo, send, list, remove, phone and desktop widths; the packed bitmap unpacks to the same pixels and is 3928 characters ending in `==`), and on the real database as a signed-in viewer inside a rolled-back transaction (insert, list, `clear_message` work; a bad bitmap and a direct update are refused). Reviewer pass done; its six findings are fixed.

Not checked: the page with a real login, and on a real phone. Exit criterion: a message written on the phone shows on the Hub and its "seen" comes back in the web app.

Phases 0 to 4 are closed with Pedro's ok (3 and 4 on 2026-11-15, evening).

## Done

- Phase 0: orientation, `CLAUDE.md`, agents.
- Phase 1: Hub hardware proven with `firmware/hub_check`. Found and worked around the Seeed_GFX2 reset-pin bug.
- Phase 2: every screen state with made-up data, judged readable by Pedro from photos.
- `tools/hub_screen`: PC simulator of the screen and generator of `fonts.h` / `art.h`.
- Phase 3: migrations `hub_node`, `weather_timer`, `backend_hardening` applied; edge function `sync` version 4; weather fetched by the database every 15 minutes. Both endpoints tested, reviewer pass done and its findings fixed.
- Phase 4: `firmware/node` (version `node 0.2`) on the C6. Verified: SCD41 single shot, upload with ages, quick Wi-Fi reconnect (about 200 ms), real deep sleep (rows about 298 s apart while the USB port was gone between wakes). Exit criterion met: 22 rows from 16:54 to 18:38, every gap between 295 and 301 s, none missing. Reviewer pass done and fixed.
- Phase 5 (partly): `firmware/hub` (version `hub 0.4`) online. Verified: SHT41 before Wi-Fi, one sync call, redraw only on change, timer wake from deep sleep (rows about 306 s apart, two hours without a gap), battery reading. Pedro looked at the panel at about 18:50 and said the live values and the weather look right (his word, no photo); the Hub was asleep at the time, so the panel keeps its image with the display rail off. Reviewer pass done and fixed. Weather icons per WMO code.
- Phase 5, message path (2026-11-15, about 19:00, Pedro at the bench): test message inserted with his ok (`messages` id 9, author `bench-test`, the Phase 2 sample drawing). The Hub fetched it at its next timer wake (19:00:18) and Pedro saw it on the panel. Key wake from deep sleep: off-schedule sync rows at 18:54:52, 18:55:11 and 19:04:52, each a key press Pedro confirmed. After the 19:04:52 press the weather showed and the message came back on its own; `read_at` was set at 19:04:52 by that same sync. The return to the message does not sync (no row a minute later). The test message was cleared at 19:07:11 (`cleared_at`); Pedro confirmed the Hub went back to the room values and weather.
- Device keys for `bedroom` and `living` generated with `tools/set_device_key.js`; only their hashes are on Supabase.

## What is on the boards now

Both run builds made with `-DSLEEP_ON_USB=1`: they deep-sleep although they are plugged into the PC, so their USB serial ports appear only for a few seconds at each wake. For normal bench work re-flash the plain builds (they stay awake on USB and log to serial).

## Not exercised yet (needs Pedro at the bench)

- Sleep current of both boards. The Hub now holds GPIO43 and GPIO39 low in sleep.

## Decided by Pedro on 2026-11-15 (evening)

- The SCD41 stays idle between readings instead of being powered down (a departure from HANDOFF.md section 3; reason in `CLAUDE.md`, section "Node facts").
- The Hub's key stays registered on Supabase.
- The CO2 alarm switches off when the bedroom data is older than 90 minutes (not in HANDOFF.md section 6).
- Phases 3 and 4 are closed.

## Decisions waiting for Pedro

1. Phase 7 candidate with the biggest battery effect: let the Hub call the database directly instead of the edge function. The sync takes about 2 s per wake, mostly the function starting cold; a database call takes about 0.2 s. It changes HANDOFF.md section 5.
2. The Rooms tab says "Open the window" from 1200 ppm (the old node's limits), the Hub shows APRI FINESTRA from 1400 and drops it at 1000. Same numbers on both, or leave it?
3. With the Hub's battery low, its 12 px band covers the message signature completely (already accepted for the band; noted again by the reviewer).

## Open points

- The first six `bedroom` rows (16:16 to 16:36) were taken with the sensor's settings lost at each wake: their temperatures (17.8 to 18.7 C) are about 4 C low, and 16:16 is high (sensor still warm from the test sketch). Deleting or keeping them is Pedro's call.
- Node temperature offset is 0; it reads about 0.4 to 0.8 C above the Hub's SHT41 on the same bench. Calibrate in Phase 7.
- "APRI FINESTRA" is drawn in regular weight: bold does not fit the column.
- Thin dark lines on the top and left panel edges in Pedro's Phase 2 photos; not drawn by the firmware.
- Battery reading not compared with a multimeter (Phase 7).
- TLS without certificate verification on both devices (as on the old node).
- Wi-Fi setup page when the connection fails: Phase 7.
- `main` on GitHub is at `32293fb`; commits on `hub-node` after that are local only.
- Web app, left as they are: the supabase-js script tag has a pinned version but no `integrity` attribute; list thumbnails are scaled down, so the tiny signature can look broken in them; "Removed from the Hub" appears at once although the Hub drops the message at its next wake.
- HANDOFF.md still describes Open-Meteo being called from the edge function and `power_down` on the SCD41.

## Notes

- The only battery Pedro has is on the Hub. The Node runs from USB.
- The `entrance` node is gone: on 2026-11-15 (evening) Pedro had its device row and its 4112 readings deleted from Supabase. `firmware/home_sensor` stays in the repo as reference. The Rooms menu ("All rooms" / a retired device) no longer appears, since no `legacy` device is left.
- `HANDOFF.md` and `Hardware/` stay local (git-ignored). The low-battery band may cover the message signature.
- Pedro switched the conversation to English on 2026-11-15 and wants to be told when he has to do something, otherwise the work goes on without check-ins.

## Next step

With Pedro: clear the test message (id 9) and watch the Hub return to the room values, then decide the points above. After that Phase 6 (web app: message editor, device status, second room).
