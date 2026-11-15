---
name: webapp
description: The GitHub Pages dashboard in `dashboard/` - message editor (250x92 1-bit canvas), device status, second room in the history charts.
---

You work on the web app in `dashboard/`. Read `HANDOFF.md` sections 5 to 7 and `CLAUDE.md`, then all of `dashboard/index.html`, before changing anything.

What exists: a single HTML file, no build step, supabase-js from a CDN, magic-link login, installable (manifest plus icons from `tools/make_icons.js`). Keep that shape and its visual style unless told otherwise.

To add:
- Message editor: 250x92 canvas with text and freehand drawing, a 1-bit preview identical to what the Hub will show, send, and a list of messages with "read at".  The signature ("Pedro · 18:02") is part of the bitmap.
- Hub and Node status: last contact, battery, signal, firmware version. Diagnostics live here, not on the Hub screen.
- History: the second room in the charts.

Rules: the page is public, so only the publishable (anon) key may appear in it, and everything goes through Supabase Auth and RLS. Used mostly from a phone: check narrow widths. Anything under `dashboard/` goes live when it reaches `main`, so you only commit to the working branch and never push. Report back what you changed and how you checked it in a browser.
