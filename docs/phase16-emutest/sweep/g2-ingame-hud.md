# Sweep g2 — in-game HUD / dual-core session presentation

Harness `tools/emutest` (Azahar 2125.1.2). Evidence: `tools/emutest/runs/sweep-g2-ingame-hud/`.
Every claim below is read off a PNG I captured. Source is cited only as a *suspected* cause.
Emulator stopped, fixtures cleaned (`sdmc:/3DGBA` ROM-less again), `sdmc/dual-gba` originals
re-hashed untouched, `qt-config.ini` restored byte-identically.

## Runs (all `azctl boot --gdb --fresh-sd-fixtures --movie <ctm>` then `gdbio resume`)

| run | movie (in the evidence dir) | evidence |
|---|---|---|
| R1 dual session + X swap | `movie_g2_dual.json` → `g2_dual.ctm` | `rec-dual/` (2 fps, both screens) |
| R2 dual (resume path) + 3×Y | `movie_g2_focus.json` | `rec-focus/` |
| R3 **1-Game** session (`settings.bin` deleted → gameMode defaults to 1) | `movie_g2_focusx.json` | `rec-focusx/` |
| R4 dual, X / Y / X / Y | `movie_g2_yx.json` | `rec-yx/` |
| R5 dual soak | `movie_g2_enh2.json` | `rec-enh2/`, `enh2-now.*.png` |
| R6 dual → **pause menu** (START+SELECT) | `movie_g2_menu.json` | `rec-menu/` |

Harness note (not a defect, but it costs runs): this app burns a CTM's *poll* budget ~2.4×
faster than `ctm.py` models (≈9.4 polls per scripted frame), and citra_qt **pauses emulation
when playback ends**. Rule: an event scripted at frame S needs a movie of ≥ 2.4·S scripted
frames — pad the tail, or the run freezes before your input. (The freeze is also useful: it
parks a screen for leisurely capture.)

---

## F1 — MAJOR — every HUD "chip"/pill renders as a filled PLUS SIGN (broken 3-slice), not a pill

**Pixels:** the pause overlay's top-screen chip row (`3D · DoF · Bloom · Light · Tilt ·
Touch Off · Co-op · Link`) draws each chip as a **cross**: a tall centre block with a thin
horizontal bar sticking out to the left and right at mid height, and each piece carries its
own dark border, so the borders visibly cross *through* the shape. At 16× the `3D` chip is a
literal plus sign. The design (`05-pause-menu.png`) is 8 small, equal-height, **rounded
outline** pills. The same broken sprite is used for the per-game dot in the in-game HUD: the
marker before "Pokemon Emerald" / "Pokemon FireRed" is a green/blue **plus**, not the design's
round dot (`04-in-game-dual.png`).

- Evidence: `rec-menu/top_00200.png` → zooms `zoom-chiprow.png` (all 8 chips, 6×),
  `zoom-chiprow-left.png` (10×), `zoom-chip-3d-single.png` (16×, one chip);
  in-game dot `rec-enh2/top_00148.png` → `zoom-hud-dot.png` (12×).
- Repro: R6 (`movie_g2_menu.json`), any frame ≥ 140; the dot is on every in-game frame of
  every run.
- Suspected source: the 3-slice/9-slice pill draw (left cap / centre / right cap) — the caps
  are emitted at a different height and offset than the centre instead of being butted flush,
  so the two rects form a cross. Same helper feeds both the chip row and the HUD dot.

## F2 — MAJOR — the in-game "3D" badge is dark navy text on black: unreadable

**Pixels:** top-screen HUD, right of centre. The badge is a 1 px blue outlined box, but the
"3D" glyphs inside are a very dark blue against the black HUD bar. At 1× it is a smudge; even
at 10× the two glyphs are barely resolvable. Every other element on the same bar (name white,
fps yellow, clock white, battery) is high contrast. The `3D` chip in the pause row has the same
problem inverted (dark navy text on a medium-blue fill).

- Evidence: `rec-dual/top_00100.png` → `zoom-3dchip.png` (rect 590,0,140,45 @10×), context
  `zoom-top-hud-right.png`; pause-row twin in `zoom-chip-3d-single.png`.
- Repro: R1, any in-game frame from `rec-dual/top_00080` on (present in every run).
- Design ref: the handoff fixes the 3D badge role colours as `#a9d4ff` on `#3E86D6`
  (`04-in-game-dual.png` shows light-blue-on-blue).

## F3 — MAJOR — the 1-Game session's bottom "CONTROLLER" screen is an empty black screen

**Pixels:** in a single-game session the bottom screen shows only a header
"＋CONTROLLER" (no game name, no SOLO chip) and a footer line "3D on top · Off ·
START+SELECT = menu". Everything between is pure black: no Off/Gamepad/Smart segmented
control, no D-pad, no A/B, no L/R, no SELECT/START, no `menu` button. The design
(`in-game-single-controller.png`) fills that area with the touch-mode segmented control, the
whole virtual gamepad, a "3D on top" chip and a "menu" button, and its header reads
"CONTROLLER · ● Pokémon Emerald · SOLO".
Caveat I can see in my own shot: the footer says the touch mode is **Off**, so part of the
emptiness may be state-dependent — but with the segmented control also missing there is
nothing on the screen to turn it on, and the header/SOLO identity is missing regardless.

- Evidence: `rec-focusx/bottom_00120.png` (and every bottom frame from 93 on), top screen for
  context `rec-focusx/top_00120.png`.
- Repro: R3 — delete `sdmc:/3DGBA/settings.bin` (so gameMode defaults to 1 Game), then
  `movie_g2_focusx.json`.

## F4 — MINOR — for ~1 s after a session starts, the screens show an uninitialised white / flat-grey texture

**Pixels:** at the moment a session appears, the top screen's video area is a **solid white**
rectangle and the bottom screen is a **flat mid-grey** rectangle that covers the *entire*
320×240 (no aspect-fit margins, unlike the running game), while the HUD already reads
"16fps"/"0fps". It clears within ~1 s. It reads as a broken/missing texture rather than a
deliberate loading state (there is no spinner or "Loading" text).

- Evidence: `rec-menu/top_00082.png` + `rec-menu/bottom_00082.png` (dual);
  `rec-focus/top_00030.png` + `zoom-load-grey.png` (grey variant, HUD reads "0fps").
- Repro: any run — the two rec frames straddling the session start (R6 frame 82, R2 frame 30).

## F5 — POLISH — the HUD bar is a translucent scrim laid *over* the top of the game image

**Pixels:** the game video is aspect-fit to the full 400×240, so the HUD bar sits on top of the
first ~20 device px of it: inside the video's x-range the bar is translucent and you can read
the game's sky through it (a visible vertical seam at the video's left edge x≈20, black to the
left of it, dark blue-grey to the right). The design plate has the video area *below* an opaque
bar.

- Evidence: `zoom-hud-dot.png` (12×; the seam and the sky through the bar), full frame
  `rec-enh2/top_00148.png`.

---

## What looked CORRECT (checked with my own captures, no defect found)

- **HUD element set, order and fit** — dot, game name, FOCUS chip, 3D badge, fps, clock,
  battery on one baseline; nothing clipped at either screen edge (the battery nub ends inside
  400 px), nothing overlapping, no truncated strings. `zoom-top-hud-left.png`,
  `zoom-top-hud-right.png`.
- **Bottom (second game) HUD** — reduced to dot + name + fps + clock with the centred footer
  hint "tap screen · pause menu", matching `04-in-game-dual.png`'s reduced bar.
- **Focused vs unfocused treatment** — works and is unmistakable. Same game, same scene:
  FireRed focused (`rec-dual/top_00100.png`) is vivid; FireRed unfocused
  (`enh2-now.bottom.png`) is clearly darkened. The focused screen additionally gets the yellow
  rule under the HUD + the FOCUS chip; the unfocused screen has neither.
- **Y = switch focus and X = swap layout both work** — R4: the X press toasts
  "Layout: A top / B bottom" and the FOCUS chip + rule move to the other screen
  (`rec-yx/top_00108.png` / `rec-yx/bottom_00108.png`), then Y moves focus back to the top
  game without a layout toast (`rec-yx/top_00136.png`).
- **Video framing** — aspect-fit and centred (20 px side margins for a 240×160 frame in
  400×240), identical on title screens and in game, no stretching, no off-centre drift
  (`zoom-t300-left.png`, `zoom-t100-left.png`).
- **Chip-row content and layout** (apart from F1's shape) — 8 chips, centred, no clipping at
  the screen edge, no overlap; the on/off distinction is clear (yellow fill = on, muted purple
  = off) and "Touch Off" spells out its state.
- **Pause overlay top screen** — "❚❚ PAUSED", "Pokemon Emerald ⇄ Pokemon FireRed",
  "settings on the touch screen ↓": centred, legible, correctly ordered
  (`rec-menu/top_00200.png`).
- **Pause Session tab (bottom)** — Resume / Change games / Quit: 9-slice plates clean, labels
  centred, the destructive Quit correctly in red (`rec-menu/bottom_00200.png`).

## Known-already, seen but not re-litigated

The in-game HUD text is a code-drawn/system-style font, not the baked `.bcfnt` (visible in
every zoom above); the pause overlay's top screen shows a black background where the design
keeps the dimmed game image behind it.

## Not covered

Enabling the Enhance/Touch toggles (to see the chip row in other states, and to see whether
the single-game gamepad appears when touch mode ≠ Off) — reaching those tabs needs more CTM
runs than the poll budget allowed here.
