# Progress

Updated: 2026-11-15 · Working branch: `hub-node`

## Current phase

**Phase 1 · Hub hardware**: exit criteria met on the board and reviewer pass done; waiting only for Pedro's ok to close it. Phase 0 closed with Pedro's ok on 2026-11-15.

## Done

- Phase 0: orientation, `CLAUDE.md`, agents.
- Installed `Seeed_GFX2` (from GitHub); esp32 core updated from 3.0.7 to 3.3.12 because the library did not compile.
- Ports confirmed by Pedro: COM4 = Hub (EE05), COM10 = Node (C6).
- `firmware/hub_check` on the Hub (built for `XIAO_ESP32S3_Plus`). Proven from the serial log and by Pedro at the bench:
  - SHT41 found at 0x44 on GPIO5/GPIO6, reading about 21 C and 45 % RH.
  - Display draws and redraws (2.3 s per full update).
  - All three keys register and redraw the screen.
  - Battery sense on GPIO1 with GPIO39 as enable: 4.02 V with no battery (charger output), 3.66 V once the battery was plugged in.
- Found and worked around a Seeed_GFX2 bug: the panel reset pin was never pulsed, so every update after the first timed out. See `CLAUDE.md`.
- Reviewer pass on the branch: no secrets, nothing against HANDOFF.md; its findings on the sketch, `.gitignore` and notes are fixed.

## Open from Phase 1

- Battery reading not compared with a multimeter. Planned for Phase 7.
- In the last key test Key1 counted 2 while Key2 and Key3 counted 1; Pedro doesn't remember whether he pressed it twice. `hub_check`'s key handler can also get stuck "held" if the release edge is read while the pin is still low. Write the key code for `firmware/hub` fresh and watch for double or missed presses in Phase 2.
- Not exercised yet: SHT41 losing power when GPIO43 is low, key wake from deep sleep.

## Notes

- The only battery Pedro has for now is on the Hub. The Node runs from USB until a second one arrives.
- The bench-test firmware that was on the Hub was overwritten with Pedro's ok.
- Pedro switched the conversation to English on 2026-11-15.
- The `entrance` node has not uploaded since 2026-11-14 21:29 UTC.

## Next step

Phase 2 · Hub screens with fake data: every state of HANDOFF.md section 6, one per key press, on a new `firmware/hub` sketch. Pedro confirms readability with a photo.

## Needed from Pedro

- Ok to close Phase 1 and start Phase 2.
- What happens to `entrance`: does it stay, or did its hardware move to the Node?
- `HANDOFF.md` and `Hardware/` are not in git: should they go in the (public) repo or stay local?
- Where the bench-test (ESP-NOW) code is, if he wants it kept.
