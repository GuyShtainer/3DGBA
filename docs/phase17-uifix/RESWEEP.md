# Phase-17 independent RE-SWEEP — visual regression check

**Date:** 2026-08-09 · **Binary:** `3DGBA.3dsx` built 11:40 from the phase-17 working tree
(`git status`: assets/main/rompicker/theme/touch/ui/wireless modified, `uigeom.*`/`uihit.*` new;
`celiolink.c` untouched).
**Method:** my own captures only. Same harness (`tools/emutest/`), same ground as the phase-16 sweep
(`docs/phase16-emutest/sweep/REPORT.md`). The fix slices' BUILDLOG was read for *scope*, never
trusted for *verdicts* — every FIXED below points at a frame I opened myself in this pass.
**Evidence root:** `tools/emutest/runs/resweep/` (`r1-picker/`, `r2-*/`, …, `zooms/`, `movies/`).

Hygiene each run: one Azahar, `azctl stop` after, fixtures staged as copies, originals re-hashed
untouched, `qt-config.ini` restored byte-identically.

---

## Run R1 — cold start → picker → session (Indigo, 2-game, touch off)

`movies/r1_picker.json`, `--fresh-sd-fixtures` + `mkroms.py` (11 dummies + gameA/gameB = 13 ROMs).
285 frames @3 fps → `r1-picker/`.

| Defect | Verdict | What I SAW |
|---|---|---|
| **D21** boot splash copy | **FIXED** | `r1-picker/bottom_00012.png` — the gold pill reads **"TAP TO START"**, crisp, single string. Top screen splash plate clean. |
| **D1** (picker mode pill) | **FIXED** | `bottom_00030.png`: the selected gold pill reads exactly **"2 Games"**; `bottom_00100.png` after the left-half tap reads exactly **"1 Game"**. No "Aspect-fit" anywhere, no double-strike, unselected neighbour clean. |
| **D2** (picker touch dead / every tap = 1 Game) | **FIXED** | Frame-diff scan of the whole run gives change points at exactly the taps. `bottom_00100.png` — tap on the LEFT half flipped to 1 Game *and* the top screen re-titled "CHOOSE A GAME" / "1 GAME" and the bottom re-laid out to single mode. `bottom_00114`→ right half flipped back to 2 Games. `top_00176.png` — GOLF carries a green **A** badge after the slot-A card tap. `bottom_00196.png` — both slot cards filled, START lit. `bottom_00220.png` — the settings chip took focus. `top_00240.png` — the START tap actually launched the session. Every documented target responded. |
| **D7** (settings chip painted over the baked footer hint) | **FIXED** | `zooms/z-settings-chip.png` (4×): "tap a game above ↑ · ⚡" is intact and unbroken, and the "settings · ZR" chip sits on its own row **below** it. Same in 1-game mode (`bottom_00100.png`, "one game · full 3D + touch · link locally or online" complete). |
| **D20** (identical cart chip on every row) | **FIXED** | `top_00030.png` / `top_00136.png`: caps alternate coral / gold / **purple** down the list; Emerald (coral) and FireRed (gold) differ; none is the Game-A green or Game-B blue. |
| **D18** (scrollbar = one near-white strip, thumb inverted) — picker instance | **FIXED** | `zooms/z-scrollbar-top.png` vs `z-scrollbar-mid.png` (4×): a dark muted-purple track with a clearly lighter lavender **pill** thumb. At topRow 0 the thumb is at the **top**; after the drag it is in the lower two-thirds — the mapping is right way up. (The pause-tab instance is checked in R2.) |
| **D6** (in-game identity dot = plus) | **FIXED** | `zooms/z-hud-dot-3d.png` / `top_00240.png`: the mark before "GOLF" is a **round dot** (green top, blue bottom), and the FOCUS chip has a rounded frame. No cross. |
| **D11** (3D badge dark-navy on black) | **FIXED** | `zooms/z-hud-3d.png` (5×): a rounded blue frame with **light-blue "3D"** glyphs, plainly readable beside gold "36fps" and near-white "02:01". |

**Touch drag-scroll (the user's third ask) — WORKS in the picker.** `top_00030.png` (rows 1-8,
thumb top) → `top_00136.png` after a 5-step upward drag on the *bottom* screen (rows 6-13, GOLF at
the top, thumb moved down) → `top_00176.png` after the reverse drag (back to row 1). The highlight
follows the scroll and the thumb tracks it.

**Button nav — works.** d-pad DOWN inside the list moves the highlight, then walks out to the
bottom-screen controls; `bottom_00220.png` shows the focus ring on the settings chip; the tap on
START launched. `bottom_00030` vs `bottom_00196`: START's label is washed-out gold-on-gold while no
game is picked and turns to crisp dark-on-gold once both slots are filled — a deliberate disabled
state, not the D1 family.

---

## Run R2 — resume prompt → session → all six pause tabs (Indigo, touch off)

`movies/r2_tabs.json`, staged fixtures (R1 left a `recent.bin`, so this run entered through the
resume prompt and resumed the GOLF/GOLF dummy pairing — the pause chrome is identical either way).
329 frames @3 fps → `r2-tabs/`.

| Defect | Verdict | What I SAW |
|---|---|---|
| **D1** (baked "Aspect-fit" under every selected segment) | **FIXED — all eight rows** | `bottom_00180.png` DISPLAY: SCALE·TOP = "**1:1**", SCALE·BOTTOM = "**1:1**", FILTER·FOCUSED = "**Sharp**", HUD = "**both**". `bottom_00200.png` AUDIO: MIX MODE = "**Solo**". `bottom_00232.png` ENHANCE: TILT = "**Off**". `bottom_00268.png` TOUCH: TOUCH MODE = "**Off**"; `bottom_00290.png` EDGES = "**Round**". Every selected chip carries exactly its own word, one string, no smear; unselected neighbours clean. The word "Aspect-fit" appears **only** as the genuine middle option of the two SCALE rows. |
| **D6** (pills / rounded rects render as a plus-cross) | **FIXED** | `top_00090.png`: all eight pause chips (3D · DoF · Bloom · Light · Tilt · Touch Off · Co-op · Link) are clean rounded pills with their labels **inside**; no protruding bars, no crossing borders. `bottom_00268.png`: the five GAMEPAD·COLOR swatches are five rounded squares in five distinct colours with a white ring on the active one — no crosses. (The in-game dot was proven in R1.) |
| **D9** (TOUCH: EDGES row collides with the hint line, caption sliced by the screen edge) | **FIXED** | `zooms/z-touch-bottom.png` (4×): "Round / Soft / Sharp" sits complete on its own row, the hint "L/R tab  A select  B resume  (or tap)" is a clear row below it, no glyph touches another. The tab now **scrolls**, so nothing falls past y=240. |
| **D15** (stray "A"/"B" glyph on the volume bar, no numeric level) | **FIXED** | `bottom_00200.png`: the rows are captioned "VOLUME · A" / "VOLUME · B" above the bar and the value "**100**" is right-aligned above the bar's right end, exactly as `pause-tab-3-audio.png` puts it. No glyph sitting on the bar. |
| **D16** (missing touch-mode explainer, empty band) | **FIXED** | `bottom_00268.png`: "Off — a touch opens the pause menu. No game input from the touch screen." on two lines directly under the TOUCH MODE row. Band filled. |
| **D18** (scrollbar one near-white strip, thumb inverted) | **FIXED** | `zooms/z-display-scrollbar.png` (3×): dark muted track, clearly lighter lavender **pill** thumb, ~2/3 of the track, sitting at the **top** while the tab is scrolled to the top. It moves down as the tab scrolls (`bottom_00268` → `bottom_00290`). |
| **D19** (overlay labels smaller/dimmer than baked labels) | **FIXED** | `bottom_00250.png` LINK: "**Co-op presence**" is the same face, size and ink as "Link cable" / "Net link (loopback)" above it. `bottom_00232.png` ENHANCE: "DIORAMA · TILT" is now a small dim all-caps **caption above** its row, matching the plate's own "SCALE · TOP" caption style. Same for GAMEPAD · EDGES. |
| **D10** (Indigo baseline for the theme check) | baseline | `bottom_00250.png`: "Save state" / "Load state" / "Load .sav" crisp white; "Wireless lobby…" dark ink on gold. |
| §5 "pause top has no dimmed backdrop" | improved | `top_00090.png` now draws a proper dimmed PAUSED panel with the game pair and the chip row over the dimmed video. |

**Touch drag-scroll in the pause menu — WORKS.** `bottom_00268.png` (TOUCH tab at the top) → a
3-step upward drag → `bottom_00290.png` (TOUCH MODE scrolled off, GAMEPAD·EDGES revealed, thumb
moved down). The DISPLAY tab scrolls the same way (thumb present at `bottom_00180.png`).

**Button nav in the pause menu — WORKS.** `bottom_00320.png`: two DOWNs + RIGHT moved the selection
onto "Preview Smart" with a gold ring; the L/R tab hint is honoured by the rail highlight.

---

## Run R3 — virtual gamepad (Indigo, `sTop=1 sBot=1 touch=1 padColor=0 padEdge=0`)

`movies/r3_pad.json`. 315 frames @3 fps → `r3-pad/`. The run also flips EDGES to **Sharp** from the
pause TOUCH tab, so Round and Sharp are the same run's before/after.

| Defect | Verdict | What I SAW |
|---|---|---|
| **D3** (L / R drawn over the bottom HUD text) | **FIXED** | `zooms/z-pad-lr-start.png` / `r3-pad/bottom_00060.png`: the top strip carries **L** (left), the gold "**TOUCH · GAMEPAD**" chip (centre) and **R** (right), each on its own space, nothing overlapping. This matches `screenshots/06-touch-gamepad.png`, which likewise has no game-name/fps row on the bottom screen in gamepad mode. |
| **D4** (START overlaps the footer hint, clipped by the screen edge) | **FIXED** | `bottom_00060.png`: START sits bottom-centre with a **complete four-sided border inside the 240 px screen**, and there is **no centred footer hint** under it any more (the hint is gated to touch = Off). The "≡ menu" chip at bottom-right is fully on-screen with three crisp bars + the word "menu". |
| **D5** (Round keys = overlapping squares with opaque black corner blocks) | **FIXED** | `zooms/z-dpad-round.png` (4×): four **separate rounded keys** in a cross with a clean gap in the middle, one continuous gold outline each, **zero black corner blocks**. `bottom_00190.png` shows the Sharp control after the in-run EDGES change — also clean. The only residue is a 1-2 px yellow pinch where two adjacent keys' rounded corners meet; cosmetic. |
| **D22** (HUD bar is a translucent scrim over the game at non-1:1 scale) | **STILL BROKEN — deliberately deferred** | `zooms/z-hud-scrim.png` (5×): the hard vertical seam is still there at the video's left edge (x≈20) — black bar to the left, **grey = white game pixels dimmed through the translucent bar** to the right, with "GOLF" legible over the game. This is exactly the original description. It is *documented* as deferred: `SPEC-layout L8.4` defers it because the fix needs an origin offset inside `render_game`, which is the HD-2D dispatch site the phase is forbidden to touch, and BUILDLOG slice F5 repeats that. At 1:1 (R1/R4) the video is inset below the bar and there is no overlap, as before. |

---

## Run R4 — real ROMs, Smart touch, 1:1 (Indigo, `sTop=0 sBot=0 touch=2`)

`movies/r4_smart.json` — picked Pokémon Emerald + Pokémon FireRed through the picker (`Pick new
games` → A → DOWN×3 → A → START), ran the session, then paused and drag-scrolled the DISPLAY tab.
600 frames @4 fps → `r4-smart/`.

| Defect | Verdict | What I SAW |
|---|---|---|
| **D17** (Smart mode: no chip, no guidance, raw cyan developer readout) | **FIXED** | `bottom_00255.png`: a gold "**TOUCH · SMART POINTER**" chip top-centre, "**≡ menu**" bottom-right with three crisp gold bars, **zero cyan text**, and no centred footer hint fighting it. |
| **D8** (DISPLAY: Swap/Skip row overprinted by the hint line and clipped) | **FIXED** | `bottom_00290.png` (tab top) → `bottom_00340.png` (dragged down): "**Swap screens**" and "**Frameskip**" appear complete with their toggles, on their own rows, clear of the hint line, nothing cut by y=240. The tab is a scrolling viewport now, which is what makes the extra rows reachable at all. |
| **D14** (~1 s of uninitialised white / flat-grey texture at session start) | **PARTIALLY FIXED / re-diagnosed, and I agree with the re-diagnosis** | The dummy-ROM session in R1 (`top_00240.png` / `bottom_00240.png`) still shows a solid white top rect and flat mid-grey bottom rect — but those cores never load a ROM, so nothing is ever uploaded and there is nothing to show. In the **real-ROM** session here the white/grey phase is the game's own boot output (`bottom_00255.png` is the genuine GAME FREAK screen a few frames later). The undefined-memory case is removed by construction (memset + `everUploaded` gate); what remains is authentic game output, exactly as BUILDLOG F5 says. **Not a defect.** However a ROM that fails to load still parks on a flat white/grey rect with no message — see NEW-2 below. |
| **D6 / D11 / D22 (1:1)** | **FIXED (re-confirmed on real ROMs)** | `top_00255.png`: round green dot before "Pokemon Emerald", readable light-blue "3D" badge, opaque HUD bar with the 240×160 video inset cleanly below it. |

---

## Run R5 — Daylight theme end-to-end (`theme=2`, real ROMs, 1:1)

`movies/r5_theme.json` — resume prompt → session → pause SESSION / LINK / AUDIO / TOUCH → the
wireless lobby. 405 frames @3 fps → `r5-daylight/`.

| Defect | Verdict | What I SAW |
|---|---|---|
| **D10** (Daylight: button and card labels dark-on-dark) | **FIXED** | `bottom_00025.png` resume prompt: "Resume this pairing" **dark ink on gold**, "Pick new games" **crisp white**, "Use defaults" light grey; `top_00025.png` the two game names "Pokemon Emerald" / "Pokemon FireRed" **crisp white** inside the A/B cards. `bottom_00160.png` LINK tab: "Save state", "Load state", "Load .sav" all **crisp white**, "Wireless lobby…" dark-on-gold, "Co-op presence" matching the rows above. `bottom_00230.png` lobby: "Host a session" dark-on-gold, "Scan for lobbies" white, "‹ back to menu" readable. Every label the report named as illegible now reads at 1×. |
| **D12** (Daylight in-game: half light, half dark on one screen) | **PARTIALLY FIXED — the unreadable half is gone, the coherence split remains (documented as deferred)** | `top_00110.png`: the letterbox is Daylight's near-white beige, the HUD bar above it is still a dark scrim — one screen, two colour worlds, exactly as reported. **But** every element on that bar is now readable: white "Pokemon Emerald", warm-sand FOCUS chip and "18fps", white "02:00", blue "3D". The residual is the theme-invariant scrim over Indigo-baked plates, which `SPEC-widgets W4.3.b` defers to a per-theme plate pack. |
| Wireless lobby (regression check — `wireless.c` was edited) | **no regression** | `bottom_00230.png` / `top_00230.png`: the full design entry screen and the four seat cards + RTT/LOSS panels render correctly on a non-Indigo theme. |

---

## Run R6 — ROM-less empty state + standalone settings (Retro Purple, `theme=4`)

`azctl clean-fixtures` + `mkroms.py clean` first (`sdmc:/3DGBA` back to just `settings.bin`),
`movies/r6_empty.json`. 285 frames @3 fps → `r6-empty-retro/`.

| Defect | Verdict | What I SAW |
|---|---|---|
| **D13** (ROM-less start drops into a black dead session) | **FIXED** | `top_00035.png`: "**No games found**" in bold, "put .gba files in sdmc:/3DGBA/" under it. `bottom_00035.png`: "**Rescan**" (focused, white ring) and "**Start without a game**", the plate's baked footer hint intact, the "settings · ZR" chip below. No silent black session. d-pad moves the ring; the tap at (40,230) opened the standalone Settings screen (`bottom_00080.png`). |
| Second non-Indigo theme (Retro Purple) regression check | **no unreadable text found** | `bottom_00080.png` DISPLAY and `bottom_00145.png` TOUCH: every caption, chip label, button label and explainer line reads. `zooms/z-done-btn.png` (6×): the "Done" pill is fully rounded, fully on-screen, dark ink on gold. |
| Touch scroll in the standalone settings | **WORKS** | Tab taps switch tabs, the drag scrolls the viewport, and the scrollbar thumb tracks it — same behaviour as the in-session pause menu. |

---

## Verdict table — all 22 originally-confirmed defects

| # | Sev (orig) | Verdict | Proof (mine) |
|---|---|---|---|
| D1 | critical | **FIXED** | `r1-picker/bottom_00030,00100`; `r2-tabs/bottom_00180,00200,00232,00268,00290` |
| D2 | major | **FIXED** | `r1-picker/bottom_00100,00114,00196,00220`, `top_00176`, `top_00240` |
| D3 | major | **FIXED** | `r3-pad/bottom_00060`, `zooms/z-pad-lr-start.png` |
| D4 | major | **FIXED** | `r3-pad/bottom_00060` |
| D5 | major | **FIXED** | `zooms/z-dpad-round.png`; control `r3-pad/bottom_00190` (Sharp) |
| D6 | major | **FIXED** | `r2-tabs/top_00090` (chip row), `r2-tabs/bottom_00268` (swatches), `zooms/z-hud-dot-3d.png` (dot) |
| D7 | major | **FIXED** | `zooms/z-settings-chip.png`, `r1-picker/bottom_00030,00100` |
| D8 | major | **FIXED** | `r4-smart/bottom_00290` → `bottom_00340` |
| D9 | major | **FIXED** | `zooms/z-touch-bottom.png` |
| D10 | major | **FIXED** | `r5-daylight/bottom_00025,00160,00230`, `top_00025` |
| D11 | major | **FIXED** | `zooms/z-hud-3d.png`, `r4-smart/top_00255` |
| D12 | major | **PARTIALLY FIXED** | `r5-daylight/top_00110` — unreadable half gone, light/dark coherence split remains (deferred: W4.3.b needs a per-theme plate pack) |
| D13 | minor | **FIXED** | `r6-empty-retro/top_00035`, `bottom_00035` |
| D14 | minor | **not a defect (re-diagnosed, I agree)** | `r4-smart/bottom_00255` — the white/grey is the ROM's own boot output; the undefined-texture case is removed by construction |
| D15 | minor | **FIXED** | `r2-tabs/bottom_00200` |
| D16 | minor | **FIXED** | `r2-tabs/bottom_00268`, `r3-pad/bottom_00155` (explainer changes with the mode) |
| D17 | minor | **FIXED** | `r4-smart/bottom_00255` |
| D18 | polish | **FIXED** | `zooms/z-scrollbar-top.png` vs `z-scrollbar-mid.png`, `zooms/z-display-scrollbar.png` |
| D19 | polish | **FIXED** | `r2-tabs/bottom_00250` (LINK), `bottom_00232` (ENHANCE) |
| D20 | polish | **FIXED** | `r1-picker/top_00030`, `top_00136` |
| D21 | polish | **FIXED** | `r1-picker/bottom_00012` |
| D22 | polish | **STILL BROKEN — deliberately deferred** | `zooms/z-hud-scrim.png` (aspect-fit); reason recorded in SPEC-layout L8.4 + BUILDLOG F5 |

**20 FIXED · 1 partially fixed (documented residual) · 1 still broken (documented deferral) ·
1 re-diagnosed as not-a-defect.**

---

## New defects the fixes introduced or left behind

| # | Sev | Defect | Evidence |
|---|---|---|---|
| **N1** | minor | **Selection / focus rings are still SQUARE on widgets that are now ROUNDED.** `main.c`'s pause rows use `ui_border` (`PK_BTN` / `PK_SEG` / `PK_STEP` / `PK_SWATCH` selection) and `assets_button`'s own focus ring is `ui_border` too, while the widgets they ring are drawn with the new rounded routines. The result is a dark right-angle notch at each corner and, on a gold-on-gold pair, a rounded button that reads as square. The picker got this right (`focus_ring` → `ui_border_round`); the pause menu and `assets_button` did not. | `zooms/z-squarering-rounded-btn.png` (7×, "Preview Smart"), `zooms/z-seg-pill-corner.png` (6×, SCALE·TOP), `zooms/z-focusring-btn.png` (8×, SESSION "Resume") |
| **N2** | minor | **A stray unlabelled dark pill outline on the pause TOP screen**, ~21×10 px at ≈(121,148), below the feature-pill row. Present in real-ROM sessions, absent with dead cores; invisible over a bright backdrop, clearly a code-drawn rounded outline over a dark one. I could not attribute it from the source in the time available (it is not `draw_paused_summary`'s pill row, not the plate art, and `presence_surfaces` returns no plate for an empty name). Now visible because `ui_fill`/`ui_border_round` actually draw rounded rects. | `zooms/z-stray-pill.png` (4×), `zooms/z-stray-pill2.png` (12×), `r4-smart/top_00267.png` |
| **N3** | polish | **The ROM-less empty state reuses the picker's `select-dual-bot` plate**, so two empty "A · TOP SCREEN" / "B · BOTTOM SCREEN" game cards sit above the Rescan / Start buttons with nothing in them, and the baked footer hint still says "tap a game above ↑" when there is nothing above to tap. | `r6-empty-retro/bottom_00035.png` |
| **N4** | minor | **A `.gba` that fails to load still starts a silent dead session.** The dummy header-only ROMs pick and START normally, then the session shows a flat white (top) / grey (bottom) rect forever with a full HUD and no message. D13 covered the *zero-ROM* case; this is the *unloadable-ROM* case and it has the same "black dead session" shape the report complained about. Pre-existing, outside the sweep's 22. | `r1-picker/top_00240.png`, `bottom_00240.png` (persists to the end of the run) |

---

## Suites (re-run by me on this tree)

celiolink **1259/0** · netlink **66/0** · diag **376/0** · control **6897/0** · trace_replay **58/0
(4 skips)** · tilt **1723/0** · presence **49756/0** · uigeom **18301/0** · uihit **1718/0** ·
theme **82047/0** · `tools/emutest/tests/run_host_tests.sh` **Ran 147 tests — OK** (its internal
`see` row SKIPs inside the sandboxed test run; my own `see rec` captured 2 000+ frames this pass).
`git diff --stat source/celiolink.c source/netlink.c source/gbacore.c` = **0 lines**.

## Harness hygiene

Six Azahar runs, one at a time, `azctl stop` after every one — each reported "originals re-hashed,
all untouched" and "restored qt-config.ini byte-identically, backup deleted (CLEAN)". Fixtures and
dummy ROMs removed at the end (`sdmc:/3DGBA` is back to `settings.bin` only) and `settings.bin`
restored from `resweep/settings.bin.orig`. `sdmc/dual-gba` was never written. No Azahar left running.
