# Sweep g4 — feature-driven visual states (tilt / effects / touch modes / wireless / themes)

Harness: `tools/emutest` (Azahar 2125.1.2). Evidence root:
`tools/emutest/runs/sweep-g4-features-modes/`. Every claim is read off a PNG I captured;
source is cited only as a *suspected* cause.

## Harness unlocks used by this group (worth reusing)

1. **`--keep-n3ds` WORKS with a synthesized movie.** The HD-2D tilt gate is
   `!isN3DS -> level 0` (tilt.c:145, G2), so the standard Old-3DS movie pin makes tilt
   *unobservable*. `azctl boot --gdb --keep-n3ds --fresh-sd-fixtures --movie …` played my
   movie through the picker into a dual session with no desync (run 1, ~19 min of movie).
   The documented ir:rst desync did not materialise on this app/movie.
2. **`sdmc:/3DGBA/settings.bin` is a legitimate state channel.** 25 x s32 LE (main.c
   Settings). `runs/sweep-g4-features-modes/setbin.py` writes it by name
   (`setbin.py tilt=3 theme=2 touch=1 …`), so a run can START in the state under test
   instead of touching it through the (touch-buggy) UI. `--backup`/`--restore` keep the
   user's file.
3. **The D4 control channel (`sdmc drop move 1 "…"`) drives the emulated GAME while a
   movie drives the 3DS.** They are independent seams, so a movie can work the pause menu
   while `move_p1.txt` walks the game into the overworld (which the tilt gate requires).
4. **Read the live level over gdb**: `gdbio read-u32 g_prefs+0x1c` == `g_prefs.tiltLevel`.

## Blocked in this harness (NOT defect claims — flagged so nobody re-runs it blind)

The **DoF / Bloom / Time-of-day-light passes cannot render** under the managed profile:
all three are gated by `s3dOn = osGet3DSliderState() > 0.03f` (main.c:3576-3577,
3776-3779) and the profile pins `factor_3d=0` + `render_3d=0` (azctl.py:219-220). Turning
those on would also change Azahar's own output mode, so the pixels would not be
representative. I therefore make **no claim about how DoF/Bloom/Light look**.

---

## Run 1 — TILT = Max (level 3) in a live dual session, New 3DS

Repro:
```
python3 tools/emutest/runs/sweep-g4-features-modes/setbin.py tilt=3 gameMode=0 touch=0 hud=3 theme=0
tools/emutest/run ctm make tools/emutest/runs/sweep-g4-features-modes/m1_tilt.json  m1_tilt.ctm
tools/emutest/run sdmc arm-control
tools/emutest/run azctl boot --gdb --keep-n3ds --fresh-sd-fixtures --movie …/m1_tilt.ctm
tools/emutest/run gdbio resume
tools/emutest/run sdmc drop move 1 "W60 a W90 a W90 a W90 a W90 a"   # game A -> overworld
tools/emutest/run see shot both …/shots/r1_t1.png
```
Evidence: `shots/r1_t1.top.png`, `shots/r1_t1.bottom.png`, `shots/r1_t0.top.png`,
zooms `zooms/z_r1_tiltmax_*.png`.

### TILT GEOMETRY: measured CORRECT (two claims raised and then RETRACTED)

I first measured the in-game tilted quad (`shots/r1_t1.top.png`, Pokemon Center) as
~32 px left of centre and too narrow, and was about to file it. It is **not** a defect:
that map simply has black void down its right side. Proof:
* `shots/r2_flat_ingame.top.png` — the SAME scene with **tilt off** has the identical
  black right-hand void (zoom `zooms/z_r2_flat_rightvoid.png` vs
  `zooms/z_r1_tiltmax_rightvoid.png`).
* On a full-bleed frame the tilt is exact. `shots/r1_t0.top.png` (Game Freak intro,
  TILT=Max) measures far row 93.8..305.8, near row 51.6..348.0, rows dev-y 39.6..199.1,
  centre **199.8 on every row** — against the theory in tilt.h (a=20 deg, k=1.033,
  d=160): far 94.1..305.9, near 50.4..349.6, height exactly 160 px, centre 200. Match.
* Bottom screen likewise centred (159.8 of 160) in `shots/r1_t1.bottom.png`.
So: symmetric, correctly anchored, full height, no void fill problem, edges clean (a hard
aliased staircase, `zooms/z_r1_tiltmax_corner_tl.png`, acceptable at 1x).
Noted, not filed: the near row spills ~28 px outside the flat frame box on each side
(SPEC-render R3.6's scissor is not applied) — it stays well inside both screens, so
nothing is clipped and there is nothing to see.

### G4-3 (MAJOR) — tilt engages OUTSIDE the overworld (the intro/title screens are tilted)

`shots/r1_t0.top.png`: the Game Freak boot logo screen is already tilted (HUD chip reads
`TILT3`), and `shots/r1_t1.bottom.png` shows game B's *title-screen* scenery tilted while
game B has never left the title. The feature's own status string promises "(overworld
only)" (main.c:3232) and the gate is written to enforce it (tilt.c:151 field predicate).

## Run 2 — tilt=0 baseline + an accidental find on the pause TOP screen

Movie `m2_tiltsweep.json` (button-driven menu: START+SELECT, R x3, DOWN x5, RIGHT, B).
**The button route did not work** — the run landed on the LINK tab with row 5 ("Load .sav")
focused instead of ENHANCE/TILT, and `g_prefs+0x1c` stayed 0 for all three iterations, so
no level ever changed. Use TOUCH for the rail + segments (g3 proved those work). The run
still produced the flat baseline above and:

### G4-4 (MAJOR) — pause TOP screen: the live game frame is composited OVER the pause panel, colliding with the game-name row

`shots/r2_pre.top.png` + zoom `zooms/z_r2_pausetop_logo.png`: the paused game (here the
Pokemon Emerald title logo) is drawn at full saturation ON TOP of the grey "PAUSED" plate
and UNDER the white "Pokemon Emerald ⇄ Pokemon Emerald" row, so the names and the ⇄ glyph
sit on top of the logo art and are hard to read; the plate reads as a hole rather than a
panel. The design (`05-pause-menu.png`) puts a heavily dimmed game image BEHIND the panel.
(This is the same z-order family g1 F4 / g2 F5 found elsewhere.)

## Run 3 — touch modes + wireless lobby (Indigo)

Movie `m3_sweep.json` (touch-driven: rail tap + segment tap + B), recording `rec3/`
(3 fps, both screens, 300 frames). Contact sheets `zooms/sheet_early.png` (frames 0-20 =
GAMEPAD in game, then the TOUCH tab) and `zooms/sheet_touchmodes.png` (frames 20-72 =
SMART in game, then the lobby).

### G4-5 (MAJOR) — the virtual gamepad's L and R buttons are drawn ON TOP of the bottom-screen HUD text

`rec3/bottom_00002.png` → zoom `zooms/z_pad_hud.png`: the L button's box covers the middle
of "Pokemon Emerald" (the game name is bisected by the button's border and fill) and the R
button's box covers the fps/clock readout ("20fps 02:02" — the clock is half-eaten). The
design (`06-touch-gamepad.png`) puts L/R in the top corners *of the game area*, clear of
the header row.

### G4-6 (MAJOR) — the START button overlaps the footer hint and is clipped by the bottom edge

Same frame → zoom `zooms/z_pad_start.png`: "START+SELECT · pause menu" runs straight
through the lower half of the START button — the button's border cuts the words and
"SELECT" sits behind the fill. The button also has no bottom border: it runs off the
bottom edge of the screen.

### G4-7 (MAJOR) — the gamepad buttons are overlapping squares with dark corner blocks (broken 9-slice), not the design's rounded pad

Zoom `zooms/z_pad_dpad.png` (3x): each D-pad direction is a square with a 1 px border and
**four opaque dark squares at its corners**; the four direction squares overlap each other
so their borders cross through the neighbouring buttons, making a lattice. A/B/L/R/START
carry the same corner blocks. Design `06-touch-gamepad.png`: small rounded, cleanly
separated keys. The pad is also drawn far larger than the design's (the D-pad alone is
~100 x 100 device px). Note `padEdge` was **0 = "Round"** for this run.

### G4-8 (MINOR) — SMART mode shows no mode indicator, no hints, and a raw debug readout

`rec3/bottom_00044.png` + zooms `zooms/z_smart_header.png`, `zooms/z_smart_footer.png`:
in SMART the bottom header has NO "TOUCH · SMART POINTER" chip (GAMEPAD does get its
"TOUCH · GAMEPAD" chip), none of the design's hint labels
(`07-touch-smart.png`: "tap → walk here", "double-tap = START", "tap target / party"), and
a cyan debug string `field p=9,4 key=-` is printed at the bottom-left in a different font
next to the footer hint. (SMART deliberately draws no overlay — that part is by design.)

### G4-9 (MINOR) — the wireless lobby's bottom screen never left "scanning…"

`rec3/bottom_00299.png`, `rec4/bottom_00150.png`: opening "Wireless lobby…" lands on a
bottom screen that shows only `NEARBY SESSIONS`, a centred `scanning...` and `« back` —
for >75 s in two independent runs, with ~80 % of the screen empty. The design's entry
screen (`08-wireless-lobby.png`: "Host a session" / "Scan for lobbies" /
"Connect via online server") never appeared, in either run, at either tap coordinate
(y=91 and y=79). The lobby TOP screen (`rec3/top_00299.png`) does match the design well:
title, `UDS · Local · scanning`, four seat cards, RTT/LOSS boxes — only much airier.
Caveat: there is no second console here, so a scan legitimately finds nothing; what I am
reporting is that the screen offers no host/online affordance at all while scanning.

## Run 4 — a non-Indigo theme (DAYLIGHT) + the lobby again

`setbin.py theme=2 tilt=1 touch=1 padColor=3 padEdge=2`, movie `m4_theme_lobby.json`,
recording `rec4/` (2 fps, both screens, 340 frames). `padColor`/`padEdge`/`tilt` all took
effect (the pad is salmon, the HUD chip reads `TILT1`), so the theme was applied too.

### G4-10 (CRITICAL) — in the Daylight theme, button labels turn dark-on-dark and become unreadable

The plates stay Indigo-dark (known: only Indigo plates are built) but the *text* colours
follow the theme, so:
* LINK tab (`rec4/bottom_00099.png`, zoom `zooms/z_theme_link_buttons.png`): **"Save
  state", "Load state", "Load .sav" are dark navy on dark navy** — effectively invisible.
  The same three labels are crisp white in Indigo (`shots/r2_pre.bottom.png`).
* SESSION tab (`rec4/bottom_00076.png` vs Indigo `rec3/bottom_00060.png`, 3-panel
  `zooms/diff_theme_session.png`): "Change games" goes dark-on-dark; "Resume" flips to
  white-on-gold (washed out where Indigo had dark ink on gold).
* Resume prompt (`rec4/top_00020.png`, zoom `zooms/z_theme_resume_cards.png`): the game
  names inside the A/B cards are dark navy on the dark navy card.

### G4-11 (MAJOR) — with a light theme the app is half dark, half light — including the two halves of ONE screen

`rec4/top_00150.png` (dark, plate-drawn lobby top) and `rec4/bottom_00150.png` (near-white,
code-drawn scan screen) are the SAME moment of the SAME screen. In-game the letterbox
around the GBA frame is near-white while the HUD bar stays dark
(`rec4/top_00045.png`, `rec4/top_00060.png`).

### G4-4 confirmed again in a second theme + scene

`rec4/top_00080.png` → zoom `zooms/z_r4_pausetop_gamefreak.png`: the "GAME FREAK" logo of
the paused game is drawn across the "Pokemon Emerald ⇄ Pokemon Emerald" row of the pause
panel.

### TILT = Low measured exact
`rec4/top_00060.png` (Game Freak, full-bleed, TILT1): rows dev-y 40.0..199.6, far
89.3..310.2, near 68.0..331.6, centre 199.8 everywhere — theory for 10 deg: 40..200,
88.8..311.2, 67.6..332.4, centre 200. Match.

## Run 5 — TILT = Mid + the tween (Indigo)

`setbin.py theme=0 tilt=2`, movie `m5_tween.json` (session, then 4 x
[open pause menu, wait, B]), recording `rec5/` (5 fps, top only, 320 frames). Opening the
menu closes the tilt gate (G3), so each open/close is a full tween down and up, on screen.

* **Level 2 settled** (`shots/e_tilt_mid.top.png` = `rec5/top_00150.png`): rows dev-y
  39.6..199.1, far 91.6..308.0, near 60.4..339.1, centre 199.8 — theory for 15 deg:
  40..200, 91.8..308.2, 59.7..340.3, centre 200. Match.
* **Mid-tween** (`shots/e_tilt_tween.top.png` = `rec5/top_00216.png`, and f288): far
  85.8..313.8, near 73.3..326.2 — a clean intermediate trapezoid, still symmetric, still
  centred, still full height. **No tween artefact**: no seam, no tear, no corner
  stretching, no void flash. The per-frame near-edge width scan
  (flat 240.4 -> 238.7 -> 250.2 -> 276.9 settled) is in the run log.

**Tilt verdict: the feature renders correctly at Off / Low / Mid / Max and through the
tween.** Its one defect is the gate (G4-3).

---

# Summary of findings (10)

| id | sev | screen | one-liner |
|---|---|---|---|
| G4-10 | major | any screen, non-Indigo theme | button labels go dark-on-dark and become unreadable |
| G4-5 | major | in-game bottom, touch=Gamepad | L/R buttons drawn over the HUD name/fps/clock |
| G4-6 | major | in-game bottom, touch=Gamepad | START button overlaps the footer hint + is clipped by the screen edge |
| G4-7 | major | in-game bottom, touch=Gamepad | pad keys are overlapping squares with opaque corner blocks (9-slice) |
| G4-4 | major | pause TOP | the live game frame is composited over the pause panel, across the names |
| G4-3 | major | in-game top/bottom, tilt on | tilt engages on intro/title screens ("overworld only" is not enforced) |
| G4-11 | major | wireless lobby / in-game, light theme | top screen dark, bottom screen near-white, same moment |
| G4-9 | minor | wireless lobby bottom | stuck on "scanning…" with no host/online affordance |
| G4-8 | minor | in-game bottom, touch=Smart | no mode chip, no hint labels, raw `field p=9,4 key=-` debug text |
| G4-12 | minor | in-game bottom, touch=Gamepad | EDGES Round vs Sharp look identical |

### G4-12 (MINOR) — the gamepad EDGES option makes no visible difference

`zooms/z_pad_dpad.png` (padEdge=0 "Round", padColor=0) vs `zooms/z_pad_sharp_dpad.png`
(padEdge=2 "Sharp", padColor=3): same rectangles, same size, same corner marks, square in
both. padColor DID change (gold -> salmon), so the settings reached the renderer.

## What looked CORRECT in this group (checked, no defect)

* Tilt geometry and tween (above) — all four levels, both screens, measured against theory.
* Tilt turns off cleanly: `shots/r2_flat_ingame.top.png` is a normal flat frame.
* The pad's colour swatch works (gold vs salmon) and the pad's key set matches the design
  (D-pad, A, B, L, R, START, "≡ menu").
* The `TOUCH · GAMEPAD` chip appears in the bottom header in PAD mode.
* The wireless lobby TOP screen matches `08-wireless-lobby.png` structurally: title,
  `UDS · Local · scanning` status with a dot, 2x2 seat map (HOST + three OPEN), RTT and
  LOSS boxes — nothing clipped, nothing overlapping.
* SMART drawing no overlay is by design (touch.h:63).

## Repro (any agent) — the shape of every run in this group

```
cd projects/3DGBA
R=tools/emutest/runs/sweep-g4-features-modes
python3 $R/setbin.py <field=value ...>            # start in the state under test
tools/emutest/run ctm  make $R/<movie>.json $R/<movie>.ctm
tools/emutest/run sdmc arm-control                # once, before the session starts
tools/emutest/run azctl boot --gdb --keep-n3ds --fresh-sd-fixtures --movie $R/<movie>.ctm
tools/emutest/run gdbio resume                    # MANDATORY
tools/emutest/run sdmc drop move 1 "W60 a W90 a W90 a W90 a W90 a"   # game A past the intro
tools/emutest/run see   rec --seconds N --fps F --screen both --out $R/recN
tools/emutest/run azctl stop && tools/emutest/run azctl clean-fixtures
python3 $R/setbin.py --restore
```
| run | setbin | movie | evidence |
|---|---|---|---|
| 1 | `tilt=3` | `m1_tilt.json` | `shots/r1_t*.png` |
| 2 | `tilt=0` | `m2_tiltsweep.json` (button route — does NOT work) | `shots/r2_*.png` |
| 3 | `tilt=0 touch=0` | `m3_sweep.json` | `rec3/` |
| 4 | `theme=2 tilt=1 touch=1 padColor=3 padEdge=2` | `m4_theme_lobby.json` | `rec4/` |
| 5 | `theme=0 tilt=2 touch=0` | `m5_tween.json` | `rec5/` |

Teardown verified after every run: `azctl stop` restored `qt-config.ini` byte-identically,
`clean-fixtures` re-hashed the `sdmc/dual-gba` originals untouched, the user's
`settings.bin` was restored from `settings.bin.orig`, and no Azahar is left running.

