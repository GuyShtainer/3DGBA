# Sweep g3 — in-game PAUSE MENU (6 tabs), visual defect report

Harness: emutest (Azahar 2125.1.2), Tier A (ROM-less dead-core session).
Evidence root: `tools/emutest/runs/sweep-g3-pause-tabs/`

## Run 1 — tab tour (all six tabs, default control states)

Movie: `tools/emutest/runs/sweep-g3-pause-tabs/m1_tabtour.json` -> `m1_tabtour.ctm`
(boot wait 1200f, START+SELECT, then touch each rail tab at x=40, y=23/53/83/113/143/173,
480f dwell each). Capture: `see rec --seconds 130 --fps 1 --screen both` ->
`rec1/` (130 frames/screen + manifest). Representative frames copied to `shots/`:

| tab | rec1 frame |
|---|---|
| SESSION | 20 |
| DISPLAY | 30 |
| AUDIO   | 39 |
| ENHANCE | 47 |
| LINK    | 55 |
| TOUCH   | 70 |

### D1 (CRITICAL) — every segmented control draws TWO overlapping labels on the selected chip
Seen on **DISPLAY** (SCALE·TOP, SCALE·BOTTOM, FILTER·FOCUSED, HUD), **AUDIO** (MIX MODE),
**ENHANCE** (TILT), **TOUCH** (TOUCH MODE, EDGES) — i.e. every PK_SEG in the menu.
The selected (yellow) chip shows two dark texts superimposed at a slight offset; on FILTER,
HUD, MIX MODE, TILT and EDGES the result is completely illegible mush. Only the two SCALE
rows survive (both texts happen to read "Aspect-fit", so it just looks smeared/bold).
Evidence: `shots/tab-display-bot.png` + zooms `z-display-seg1.png`, `z-display-filter.png`,
`z-display-hud.png`; `shots/tab-audio-bot.png`; `z-enhance-tilt.png`; `z-touch-edges.png`.

### D2 (MAJOR) — DISPLAY: the Swap/Skip toggle row collides with the status hint line
The two toggles + their "Swap"/"Skip" labels sit at y≈224-238 and the hint
"L/R tab  A select  B resume  (or tap)" is drawn straight through them at y≈231.
"Skip" is unreadable. Evidence: `shots/tab-display-bot.png`,
zoom `shots/z-display-bottomstrip.png`.

### D3 (MAJOR) — TOUCH: the EDGES row collides with the same hint line, and a baked label is cut off by the screen edge
"Round/Soft/Sharp" overlap the hint text word-for-word; below them a baked
"GAMEPAD · EDGES" caption is clipped by the bottom screen edge.
Evidence: `shots/tab-touch-bot.png`, zooms `z-touch-edges.png`, `z-touch-bottomcut.png`.

### D4 (MAJOR) — pause TOP screen: the feature pills render as plus/cross shapes, labels overflow the pill
The rounded pill fills draw as a cross/plus (a horizontal bar sticking out past the body on
both sides); the labels are wider than the coloured body ("Touch Off", "Co-op", "Light").
Design ref `05-pause-menu.png` / `pause-tab-2-display.png` show clean rounded pills.
Evidence: `shots/tab-session-top.png`, zoom `z-top-pills.png`.

## Run 2 — control states (movie `m2_states.json` -> `m2_states.ctm`, capture `rec2/`)

Taps: DISPLAY filter=Smooth, scale-top=1:1, HUD=off, Swap, Skip; AUDIO mix=Mixed, mix=Split,
Vol A "-" x3, Vol B "+" x2, Mute; ENHANCE 3D off, DoF off, TILT=Low, TILT=Max; LINK link-cable,
CO-OP; TOUCH mode=Gamepad, mode=Smart, swatch #3, EDGES=Sharp. 1 fps `see rec`, 175 s.
Frame index of each state is in the finding lines below.

### D1 CONFIRMED and characterised (the selected chip's label is ALWAYS the string "Aspect-fit")
Moving the selection proves it: `rec2/bottom_00033.png` — SCALE·TOP set to **1:1**, and the 1:1
chip reads "Aspect-fit"; `rec2/bottom_00070.png` — AUDIO MIX MODE set to **Split**, and the
Split chip reads "Aspect-fit" over the baked "Split" (zoom `shots/z-audio-mixsplit.png`, the
single clearest shot). Same on FILTER=Smooth (`bottom_00022.png`), HUD=off (`bottom_00033.png`),
TILT=Max (`bottom_00098.png`), TOUCH MODE=Smart (`bottom_00121.png`), EDGES=Sharp
(`bottom_00174.png`). So every segmented control in the pause menu mislabels its selected
option AND double-draws over the baked label.

### D5 (MAJOR) — rounded-rect fills render as a plus/cross
The gamepad-colour swatches on the TOUCH tab and the feature pills on the pause TOP screen are
drawn with cut-out corners and protruding mid-bars — a plus/cross, not a rounded rect.
Evidence: `shots/z-touch-swatches.png` (5 swatches), `shots/z-top-pills.png`,
`shots/z-top-pills-mixed.png` (`rec2/top_00098.png`).

### D6 (MINOR) — AUDIO: volume rows show a stray "A"/"B" glyph half over the bar, and no value
Design `pause-tab-3-audio.png` puts the numeric level (80 / 70) right-aligned above each bar.
The build draws a tiny "A"/"B" instead, sitting on the bar's top-left corner next to the "-"
button, and no number anywhere. Evidence: `shots/z-audio-vol.png` (`rec2/bottom_00070.png`).

### D7 (MINOR) — TOUCH: the mode explainer paragraph is missing, leaving a ~55 px empty band
Design `pause-tab-6-touch.png` has 3 lines of explainer between TOUCH MODE and the two Preview
buttons. The build leaves it blank. Evidence: `shots/touch-smart-bot.png`.

### Observations that are NOT defects (checked, look right)
- Toggle on/off is unambiguous everywhere (yellow track + knob right = on, dark track + white
  knob left = off): ENHANCE `bottom_00087.png`, LINK `bottom_00110.png`.
- Focus ring (yellow outline) is clearly visible on the focused row on every tab.
- Volume bar fill tracks the stepper (`bottom_00070.png`: A reduced, B still full).
- Swatch selection ring moves to the tapped colour (`bottom_00174.png`, blue).
- SESSION tab matches `pause-tab-1-session.png` (only the small glyphs ▶/⟳/⏻ are absent).
- LINK tab rows, buttons and status line are clean, no clipping.
- Tapping the DISPLAY "Swap" toggle closes the pause menu — checked the source, that is
  deliberate (`menuSel == 6` sets `menuOpen = false`), so NOT reported as a defect. (It does
  mean the Swap toggle's on-state is never visible, but that is behaviour, not pixels.)

## Run 3 — Tier B (real ROM fixtures), pause TOP screen
Movie `m3_rom_pause.json` -> `m3_rom_pause.ctm`, `azctl boot --fresh-sd-fixtures`, capture
`rec3/`. `shots/tierb-pause-top.png` (= `rec3/top_00060.png`): the two real game names
("Pokemon FireRed ⇄ Pokemon Emerald") render centred and un-truncated — good. The pills are
plus/cross-shaped here too, in all three colours (`shots/z-tierb-pills.png`) — D5 is not a
ROM-less artefact. Fixtures cleaned afterwards (`azctl clean-fixtures`, originals verified).

### D8 (POLISH) — DISPLAY/TOUCH: the right-edge scrollbar is a full-height near-white strip
It is the brightest element on the screen and shows no thumb/track distinction, so it conveys
no scroll position (design `pause-tab-2-display.png` has a short light thumb on a dark track).
May be baked plate art rather than code. Evidence: `shots/z-display-rightedge.png`.

### D9 (POLISH) — overlay row labels ("TILT", "CO-OP", "EDGES") are a different, much smaller and dimmer type than the baked row labels on the same tab
e.g. LINK: "Link cable" / "Net link (loopback)" are large and bright, "CO-OP" is a tiny dim
all-caps mono label. Same on ENHANCE ("TILT") and TOUCH ("EDGES").
Evidence: `shots/link-coop-on-bot.png`, `shots/enhance-tilt-max-bot.png`.

## Repro (any agent)
```
cd projects/3DGBA
tools/emutest/run ctm make tools/emutest/runs/sweep-g3-pause-tabs/m1_tabtour.json /tmp/m1.ctm
tools/emutest/run azctl boot --gdb --movie /tmp/m1.ctm && tools/emutest/run gdbio resume
tools/emutest/run see rec --seconds 130 --fps 1 --screen both --format none --out /tmp/rec1
tools/emutest/run azctl stop
```
(the same with `m2_states.json` for the control-state frames, `m3_rom_pause.json`
+ `--fresh-sd-fixtures` for Tier B).

