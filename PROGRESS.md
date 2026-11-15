# Progress

Updated: 2026-11-15 · Working branch: `hub-node`

## Current phase

**Phase 1 · Hub hardware**: all checks passed on the board; waiting for the reviewer pass and Pedro's ok to close it. Phase 0 closed with Pedro's ok on 2026-11-15.

## Done

- Phase 0: orientation, `CLAUDE.md`, agents.
- Installed `Seeed_GFX2` (from GitHub); esp32 core updated from 3.0.7 to 3.3.12 because the library did not compile.
- Ports confirmed by Pedro: COM4 = Hub (EE05), COM10 = Node (C6).
- `firmware/hub_check` on the Hub. Proven from the serial log and by Pedro at the bench:
  - SHT41 found at 0x44 on GPIO5/GPIO6, reading about 21.2 C and 44 % RH.
  - Display draws and redraws (2.3 s per full update).
  - All three keys register and redraw the screen.
  - Battery reads 4.02 V on GPIO1 with GPIO39 as enable.
- Found and worked around a Seeed_GFX2 bug: the panel reset pin was never pulsed, so every update after the first timed out. See `CLAUDE.md`.

## Notes

- The bench-test firmware that was on the Hub was overwritten with Pedro's ok.
- The SHT41 is powered from the switched `VCC_3V3` hole, so it is off whenever GPIO43 is low. Good for sleep current; the firmware must power the rail before reading.
- The `entrance` node has not uploaded since 2026-11-14 21:29 UTC.

## Next step

Phase 2 · Hub screens with fake data: every state of HANDOFF.md section 6, one per key press, on a new `firmware/hub` sketch. Pedro confirms readability with a photo.

## Needed from Pedro

- Ok to close Phase 1.
- What happens to `entrance`: does it stay, or did its hardware move to the Node?
- `HANDOFF.md` and `Hardware/` are not in git: should they go in the (public) repo or stay local?
- Where the bench-test (ESP-NOW) code is, if he wants it kept.
