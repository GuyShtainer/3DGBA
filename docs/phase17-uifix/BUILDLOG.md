# Phase-17 UI fix — BUILDLOG

Append-only. One entry per slice: what changed, the exact commands, the capture path, and **a
sentence describing what the PNG actually showed**.

---

## Slice F1 — the widget core (SPEC-widgets W1 + W2 + W3)

**Date:** 2026-08-09 · **Scope:** RC1 (the baked "Aspect-fit" overprint) + RC2 (the plus-sign
rounded rect) and their dependent symptoms. **Sweep defects closed:** D1, D5 (the corner-block
half), D6 (a, b, c), D21. **Suites:** 8/8 green. **celiolink.c diff:** empty.

### What changed

| File | Change |
|---|---|
| `source/uigeom.{c,h}` *(new)* | Pure-C rounded-rect geometry: `ui_round_clamp_r`, `ui_round_steps`, `ui_round_rect_quads` (midpoint-sampled corner staircase), `ui_round_outline_quads`, `ui_seg_radius`. Header-free (`<stdint.h>`/`<math.h>`) per CLAUDE.md #4 — it dual-compiles on the PC. |
| `test/host/test_uigeom.c` *(new)* | T1–T11, **18301 checks**. Keeps the pre-fix decomposition as `legacy_quads()` so T5 is a proven regression, not an assertion. |
| `source/ui.c` / `ui.h` | `ui_fill` now blits `uigeom`'s decomposition; new `ui_border_round`; **deleted** the call-site-less `ui_segmented` / `ui_toggle` / `ui_cart` (OQ4). `ui_seg_hit` kept (main.c:3201/4265 hit-test PK_SEG rows with it). |
| `source/assets.c` / `assets.h` | `assets_seg` drawn **procedurally** from `g_ui.panel` + `g_ui.acc` (W1.2 option b); **`draw_hslice` deleted** with it. |
| `source/main.c` | Splash button → `assets_button(...)` (W1.5, the depth-0.6 occlusion); pause feature pills → `ui_border_round` + role-coloured label (W3.5, 1:1 with the design). |
| `source/touch.c` | `pad_zone` was a **private copy** of the same broken three-rect decomposition → routed to `ui_fill`; the pad-key frames use `ui_border_round` so the outline follows the fill's silhouette. |

### The proof loop (every run: `azctl stop`, one Azahar, fixtures only)

```bash
export DEVKITPRO=/opt/devkitpro DEVKITARM=$DEVKITPRO/devkitARM && make -j8
tools/emutest/run ctm make tools/emutest/runs/sweep-verify/<movie>.json /tmp/m17_*.ctm
tools/emutest/run azctl boot --gdb [--fresh-sd-fixtures] --movie /tmp/m17_*.ctm
tools/emutest/run see rec --seconds N --fps F --screen both --out tools/emutest/runs/p17-f1-* &
tools/emutest/run gdbio resume
tools/emutest/run azctl stop
```

| Run | Config | Movie | Output |
|---|---|---|---|
| R1 | ROM-less (`clean-fixtures`) | `m1_tabtour.json` | `runs/p17-f1-tabs/` (285 f @ 3 fps) |
| R2 | `--fresh-sd-fixtures` | `movie_picker_sweep.json` | `runs/p17-f1-picker/` (splash + resume prompt) |
| R3 | fixtures, `recent.bin` removed | `movie_picker_sweep.json` | `runs/p17-f1-picker2/` (the game-select mode pill) |
| R4 | ROM-less, `setbin.py hud=3 touch=1 padEdge=0` | none (auto-session) | `runs/p17-f1-hud2/` (HUD + virtual gamepad) |
| R5 | ROM-less, `setbin.py theme=2 hud=3` | `m1_tabtour.json` | `runs/p17-f1-daylight/` (six-theme evidence) |

### What I SAW — before / after

**D1, the "Aspect-fit" overprint (W1.6 items 2–4).**
*Before* `runs/sweep-verify/rec-tabs/bottom_00135.png`: the selected MIX MODE chip is the **left**
pill carrying "Solo" superimposed on "Aspect-fit" — a dark double-struck blob between the clean
neighbours "Mixed" and "Split".
*After* `runs/p17-f1-tabs/bottom_00135.png` (+ 3× crop `/tmp/z17-audio-seg.png`): the selected chip
is a clean gold rounded pill reading **exactly "Split"**, single-struck, with "Solo"/"Mixed" dim and
clean beside it. Nothing else is inside the pill. Matches `screenshots/pause-tab-3-audio.png`
(dark track, gold pill, dark ink) apart from the missing numeric level (D15, SPEC-layout).
*The stretch case* `runs/p17-f1-picker2/bottom_00050.png`: the picker mode pill reads exactly
**"1 Game" / "2 Games"** — before (`zooms/z-seg.png`) it carried "Aspect-fit" stretched 2.46×.
*The squash case* (h=14) `runs/p17-f1-tabs/bottom_00210.png` + `/tmp/z17-edges.png`: the EDGES row
reads **"Round" / "Soft" / "Sharp"**, each single-struck, the selected "Sharp" dark-on-gold.
(The row still collides with the hint line — that is D9, SPEC-layout's, untouched here.)
*Whole tab* `runs/p17-f1-tabs/bottom_00110.png`: DISPLAY shows "1:1", "Aspect-fit", "Sharp", "off"
selected on four rows — four live labels, zero intruders.

**D6a, the pause chip row (W2.7 item 3).**
*Before* `runs/sweep-verify/zooms/z-chiprow.png`: eight literal crosses — a tall filled centre block
with a short bar protruding left and right at mid height, "Touch Off" and "Co-op" overflowing onto
the nubs.
*After* `runs/p17-f1-tabs/top_00080.png` + 5× `/tmp/z17-chiprow.png`: eight **outlined rounded
pills** — 3D blue, DoF dim, Bloom/Light/Tilt gold, Smart green, Co-op/Link dim — each a 1 px
rounded frame with the label in the same colour inside it, transparent interior, no nubs, nothing
overflowing. This is `screenshots/pause-tab-2-display.png`'s top screen 1:1 (W3.5 taken: OQ2 is
resolved by the handoff's "match the reference 1:1" hard rule).

**D6b, the in-game identity dot (W2.7 item 2).**
*Before* `runs/sweep-verify/zooms/z-hud-dot.png`: a thin green **plus** with long arms.
*After* `runs/p17-f1-hud2/top_00100.png` + 20× `/tmp/z17-dot3.png`: a solid green **disc**
(octagonal at 5 px, which is what a 5×5 r2 rounded rect can be on this panel) — no arms.

**D6c, the gamepad colour swatches (W2.7 item 4).**
*Before* `runs/sweep-verify/zooms/z-swatches.png`: five crosses with deep square corner bites.
*After* `runs/p17-f1-tabs/bottom_00210.png` + 5× `/tmp/z17-swatches.png`: five clean **rounded
squares** with a smooth corner staircase and no black bites; the white selection ring around the
active one is unchanged and still square (correct — it is a `ui_border` focus ring).

**The volume bar (W2.7 item 5).** 8× crop `/tmp/z17-audio-vol.png`: the gold fill and the track
both have **rounded ends** now. Before, `ui_fill`'s guard fired (h=6 ≤ 2r) and drew a square bar;
`ui_round_clamp_r` makes r=3 on h=6 a true stadium, as `pause-tab-3-audio.png` draws it.

**The translucent scrim, the alpha invariant (W2.7 item 6).** 7× crop `/tmp/z17-scrim2.png` of the
"TOUCH · GAMEPAD" chip (black at α=0x96 over the game): **uniformly dark, rounded, no darker corner
patches** — the non-overlap guarantee (host-tested T2) holds in pixels.

**D5's corner blocks (in-scope deviation, see below).**
*Before* `runs/sweep-verify/zooms/z-pad-dpad.png`: every pad key a square with four opaque black
corner blocks.
*After* `runs/p17-f1-hud2/bottom_00100.png` + 4× `/tmp/z17-dpad.png`: four clean rounded keys with
rounded frames and **no corner blocks at all**. The four D-pad keys still *overlap each other* so
their borders cross — that is the zone-geometry half of D5 plus D3/D4, which **SPEC-layout owns**;
untouched here.

**D21, the splash (W1.6 item 6).** `runs/p17-f1-picker/bottom_00018.png`: the button is a clean
gold rounded plate reading **"TAP TO START"** — one string, no baked "▶ START" over it. Matches
`screenshots/01-boot-splash.png`. The cause was depth order, not copy: the sprite was submitted at
z=0.6 and citro2d's depth test is GEQUAL, so it hid the correct label the code already drew.
**W1.5.1 depth audit:** `grep` over every `C2D_DrawImageAt` / `C2D_DrawRectSolid` / `C2D_DrawText`
/ `C2D_DrawTriangle` in `source/*.c` now returns **zero** call sites with a non-`0.0f` depth.

**All six themes.** `runs/p17-f1-daylight/bottom_00110.png` (theme=2): the segmented controls
**follow the theme** — a white `g_ui.panel` track with a bronze `g_ui.acc` pill and dark ink, every
selected label ("1:1", "Sharp", "both") readable. The baked sprite could not do this: only the
Indigo pack is built, so a sprite-based fix is wrong in five of six themes *by construction*. The
Daylight chip row (`top_00080.png`) is also correct — blue/amber/dim outlines on the grey pause
backdrop. **Honest caveat:** the white track sits on the *Indigo* baked plate — the one-art-pack
gap (D10/D12) is untouched and remains W4.3's (slice S5) job.

**W3.6, the `ui_panel` corner leak: not reproduced.** The picker's slot cards and the resume
prompt's A/B cards are drawn from the **plate art** (`select-dual-bot`, `resume-top`), not from
`ui_panel`; at 10× (`/tmp/z17-card-tl.png`) the card corner is a clean rounded arc with no
background showing at the frame's inside corner. No action needed. (The card's cart chip, r=2, is
now a rounded rect instead of a bitten square.)

### Host suites

| Suite | Result |
|---|---|
| celiolink | 1259 checks, 0 failures |
| netlink | 66, 0 |
| diag | 376, 0 |
| control | 6897, 0 |
| trace_replay | 58, 0 (4 skips) |
| tilt | 1723, 0 |
| presence | 49756, 0 |
| **uigeom (new)** | **18301, 0** |
| `tools/emutest/tests/run_host_tests.sh` | Ran 147 tests — OK |

T5 is the regression that names the bug and it is red-then-green by construction (printed each run):

```
ui_dot HUD identity mark     legacy   9.00 | new  22.04 | true  21.57
pause feature pill           legacy 350.00 | new 429.30 | true 428.54
gamepad colour swatch        legacy 376.00 | new 426.94 | true 426.27
toggle knob                  legacy  36.00 | new  86.94 | true  86.27
```

Build: `make -j8` clean — no new warnings in `ui.c`, `uigeom.c`, `assets.c`, `main.c`, `touch.c`
(the `-Wmisleading-indentation` / unused-variable warnings that remain are pre-existing lines I did
not touch).

### Quad cost (the perf argument — analytic, NOT hardware-proven)

Worst case per shape: **13 quads** (fill) / **30 quads** (outline). Per frame: pause top screen
9 outlined pills × 30 = 270 quads; the 60 Hz in-game HUD path is 1 dot (4) + the touch scrims
(≈11 each). `C2D_DrawRectSolid` is one batched quad with no texture bind, and `assets_seg` now
issues **no texture binds at all** where it previously issued two binds + six textured sub-rect
draws. Net: more quads, fewer state changes. Per CLAUDE.md #6 this is an argument, not a proof —
**flag it for the real New-3DS pass.**

### Deviations (and why)

1. **`UI_OUTLINE_MAX_QUADS` is 30, not the spec's 26.** The spec's strip list lets a cap band
   *straddle* the inner shape's edge (`y+t`), which emits a full-width quad **over the transparent
   interior** — measured as 136 px² of intrusion on a 208×30 r8 t1 ring before the fix. Adding the
   two boundaries `y+t` / `y+h-t` costs at most two extra strips ⇒ 30 quads.
2. **T9's area identity is 1 % only for aligned inputs, 3 % otherwise; the interior-intrusion bound
   is 0 for aligned inputs, 2 % of the ring's area otherwise.** "Aligned" = integral `r ≤
   UI_ROUND_STEPS_MAX` and integral `t`, which is **every real call site**. When the radius is
   capped (r=8 → 6 bands of 1.33 px) the outer and inner staircases sample different grids and
   leave sub-pixel slivers in the four corners (measured: 6 px² on 208×30 r8 t1). Recorded rather
   than hidden.
3. **T8 (pixel grid) is asserted for integral `r ≤ 6`, not `r ≤ 8`.** With `UI_ROUND_STEPS_MAX = 6`
   the band height is 1.0 px exactly only up to r=6; the spec's "every real radius is 2…8" is true
   of `assets_fill9`'s 9-slice radius, not of any `ui_fill` call site (those are 1.5…6).
4. **`seg-active` / `seg-track` still appear in `source/assets_gen.h`.** That file is *generated* by
   `tools/build_assets.sh`, which **globs** `widgets/<theme>/*.png` — hand-removing the entries
   would be undone by the next asset build. No code references them any more (the W1.6-1 grep is
   clean apart from comments), so the sprites are simply never blitted; they cost only atlas space.
5. **`touch.c`'s `pad_zone` fixed here although SPEC-layout lists "D5 (geometry half)".** It was a
   verbatim private copy of the RC2 decomposition — the same bug this slice exists to kill, and
   leaving it would contradict deleting `draw_hslice` for exactly that reason. Only the
   *rasterisation* changed; the zone **positions/overlap** (D3, D4, D5's overlap half) are
   untouched and remain SPEC-layout's.
6. **W3.5 (outlined feature pills) landed** — OQ2 asked to ask first. The handoff's hard rule
   "Match the reference 1:1" and `screenshots/pause-tab-2-display.png` (outlined pills, coloured
   text, transparent interior) decide it; it also removes the "label overflows the nub" symptom
   outright. Easy to revert to filled (`ui_fill` + `g_ui.ink`) if the user prefers.
7. **OQ1 accepted as recommended:** the sprite's 1 px inner highlight ring (0.6 % of the pill,
   invisible at 1×) is gone with the procedural pill.
8. **OQ4 taken:** `ui_segmented` / `ui_toggle` / `ui_cart` deleted (no call sites) rather than
   fixed — three fewer ways to reintroduce the plus/cross. `ui_seg_hit` and `ui_tri` are live and
   kept.

### Not in this slice (so nobody thinks it was missed)

D2 (picker touch — SPEC-input), D3/D4 + D5's zone overlap, D7/D8/D9/D16/D18 (layout + scroll),
D10/D11/D12 (theme/contrast, W4 → slice S4/S5 — the "3D" badge is still dark-blue-on-black in
`runs/p17-f1-hud2/top_00100.png`), D13/D14/D15/D17/D19/D20/D22.
Everything timing-sensitive stays unproven until real New-3DS hardware (CLAUDE.md #6).

---

## Slice F2 — input: picker touch + button nav (SPEC-input I1 + the picker half of I3)

**Date:** 2026-08-09 · **Scope:** RC3 / REPORT **D2** (every picker tap resolving to "1 Game", no
picker target responding) plus the picker's button-nav defects. **Sweep defects closed:** D2; the
§5 "the picker is close to unstartable / console START launches the defaults" note.
**Suites:** 10/10 green (uihit is new, 371 checks). **celiolink.c diff:** empty (`git diff --stat`
= 0 lines).

### What changed

| File | Change |
|---|---|
| `source/uihit.{c,h}` *(new)* | Pure-C hit-test + gesture core (SPEC-input §0.1): `UiRect`, `uihit_in/seg/index`, the `UiGesture` state machine, the picker rect tables (`uihit_pick_rect/pick/pick_mode_seg`), and the scroll arithmetic (`uihit_scroll_clamp`, `uihit_clamp_sel`, `uihit_follow_sel`). Header-free (`<stdint.h>` only) per CLAUDE.md #4 — it dual-compiles on the PC. |
| `test/host/test_uihit.c` *(new)* | T1–T7, **371 checks**: the golden rect table, the D2 regression + its anti-test, four-corner coverage of every target in both modes, the dead gutter, the single-mode absence of slot B, the gesture rules, the scroll clamp/rate and the highlight-follow. |
| `source/rompicker.h` | `PickDiag g_pickDiag` — the harness's GDB window into the picker (§0.2.1), offsets `magic 0x00 … startN 0x34`. Logging only; nothing reads it back. |
| `source/rompicker.c` | The picker's input block rewritten on the gesture machine; **every drawn rect now comes from `uihit_pick_rect()`** — the same array the tap is hit-tested against; the d-pad focus chain + focus rings; `KEY_START` activates START instead of launching the defaults; `KEY_B` no longer falls through to a defaults session; a "PICK A GAME FIRST" status hint. `recent_prompt` got the same latch **and** the focus it never had (I3.1). |

### The proof loop

```bash
export DEVKITPRO=/opt/devkitpro DEVKITARM=$DEVKITPRO/devkitARM && make -j8
tools/emutest/run ctm make docs/phase17-uifix/movies/<M>.json /tmp/m17f2_*.ctm
tools/emutest/run azctl boot --gdb [--fresh-sd-fixtures] --movie /tmp/m17f2_*.ctm
tools/emutest/run see rec --seconds N --fps 3 --screen both --out tools/emutest/runs/p17-f2-* &
tools/emutest/run gdbio resume
tools/emutest/run gdbio read-u32 g_pickDiag+0xNN        # the numeric proof
tools/emutest/run azctl stop
```

| Run | Movie | Fixtures | Output |
|---|---|---|---|
| A | `movies/m17_picker_targets.json` | `--fresh-sd-fixtures` + `mkroms.py` (13 ROMs) | `runs/p17-f2-targets/` (300 f @ 3 fps) |
| B | `movies/m17_picker_notready.json` | same + a hand-written `recent.bin` | `runs/p17-f2-notready/` (345 f) |
| C | `movies/m17_picker_dpad.json` | same | `runs/p17-f2-dpad/` (270 f) |

Hygiene: one Azahar at a time, `azctl stop` after every run, `mkroms.py clean` + `azctl
clean-fixtures` at the end (`originals re-hashed, all untouched`; `sdmc:/3DGBA` is ROM-less again).
The runs were on the **Daylight** theme that `settings.bin` happened to hold — so the whole slice
was proven on a non-Indigo theme (I4.2.4), which is how the focus-ring defect below was caught.

### What I SAW

**D2, the release-edge read — the regression that had to be disproved.**
Run A, `runs/p17-f2-targets/bottom_00074.png`: a tap on the **left** half (80,20) → the plate flips
to the single-game layout, "1 Game" gold on the left, the tall A card, "LINK A FRIEND".
`bottom_00080.png`: a tap on the **right** half (240,20) → back to the dual plate with "2 Games"
gold on the right. **The right-half tap selects 2 Games** — the sweep's evidence frame showed the
identical tap producing "1 Game", because the handler read (0,0) on the release frame.
The numeric half, run B: `g_pickDiag+0x24` (`lastTap`) = **0x008C00BE = (140 << 16) | 190** — the
app latched *exactly* the coordinate the movie sent. Never (0,0) again.

**Every documented target.**
- *mode segments* — above (both halves, `runs/p17-f2-targets/bottom_00074/00080.png`).
- *slot card A* — `top_00088.png`: the tap committed row 0, a green **A** badge appears on
  "Pokemon Emerald" and the status flips to "B · BOTTOM".
- *slot card B* — `top_00106.png`: a blue **B** badge on "GOLF" and the status reads **READY**.
- *START* — run A ends with the console START button on a ready picker: `top_00290.png` /
  `bottom_00290.png` show a live session, **"Pokemon Emerald" on top and "GOLF" on the bottom** —
  the *picked* pair. The defaults would have been gameA/gameB (Emerald + FireRed). That is I1.7.1
  closed: the button labelled START no longer launches two games the user did not pick.
- *START not ready* — run B, `top_00088.png` zoomed (`/tmp/z_f2_hint.png`): the top-right status
  reads **"PICK A GAME FIRST"** in the destructive salmon, and `g_pickDiag+0x34` (`startN`) = **0**.
  It used to be silent.
- *the dead gutter* between START and START—LINKED (140,190): `lastHit` = **-1**, `startN` = 0,
  and `bottom_00110.png` shows the picker unchanged with focus still on the B card. A miss does
  nothing instead of firing the scarier button.
- *the settings chip* — run B, `bottom_00265.png`: the tap at (40,222) opened the **Settings**
  screen (Display tab). Before the fix the chip was unreachable: its `py >= 214` test could never
  be satisfied by a (0,0) read.

**Touch drag-scroll (the user's third ask), and "a drag never activates".**
Run A `top_00098.png`: a 120 px drag that **starts on the START—LINKED button** and ends over the
slot A card scrolled the list from row 0 to **row 5** — GOLF/CHARLIEQUEST/HOTEL/JULIETT/BRAVOGAME/
INDIA/ECHO/DELTA, i.e. `topRow == maxTop == 13-8 == 5`, clamped exactly — with the **highlight
following the scroll** (the white focus ring is on GOLF). No session started, no slot changed: the
gesture never emitted a tap. Run B's counters confirm the converse: five clean taps produced
`dragN = 0`, and `tapN = 2` for the two taps in that movie.

**D-pad + A reachability with a visible focus (the second ask).**
Run A frames 110→139 walk the chain by DOWN: `bottom_00110.png` ring on the **B card** →
`bottom_00120.png` ring on **START** → RIGHT → `bottom_00125.png` ring on **START—LINKED** →
`bottom_00134.png` ring on the **settings chip** → `top_00139.png` ring back on the **list row**.
Run C walks it the other way with UP and exercises the adjusts: the mode pill flips
**2 Games → 1 Game** on d-pad LEFT (`bottom_00167.png`), back on RIGHT (`bottom_00177.png`), and
again on **A** (`bottom_00188.png`); then DOWN×3 + **A on the settings chip** opens Settings
(`bottom_00235.png`). Every bottom-screen target is now operable without the touchscreen, and the
prompt has focus too: run B `bottom_00060.png` (ring on "Resume this pairing") →
`bottom_00068.png` (DOWN moves it to "Pick new games") → A → the picker (I3.1 / T20).

**A defect the capture caught, and the fix.** In run A the focus ring on the settings chip was
drawn *straddling* the chip's edge — and in Daylight `g_ui.panel2` is near-white, so a white ring
on it was almost invisible (`/tmp/z_f2_setring.png` vs the unfocused `/tmp/z_f2_setring_off.png`:
the difference is a few grey pixels). `focus_ring` now insets by **-2** so the ring lands on the
plate outside the control. Run C `/tmp/z_c_set3.png` (4× crop of `bottom_00115.png`): a crisp white
rounded outline clear of the chip. This is exactly the class of bug the "read the PNG yourself"
rule exists for — the ring *was* being drawn, and a state read would have called it a pass.

### The GDB numbers (run B, read live while the picker was up)

```
g_pickDiag+0x00 magic    = 0x50494B31 'PIK1'   (the offsets are the ones I think)
            +0x04 frame  = 1748                 (liveness)
            +0x08 sel    = 0      +0x0c topRow = 0      +0x10 nRoms = 13
            +0x14 idxA   = -1     +0x18 idxB   = -1     +0x1c mode  = 0 (dual)
            +0x20 lastHit= -1                    <- the dead-gutter tap resolved to NOTHING
            +0x24 lastTap= 0x008C00BE = (140,190) <- the exact point the movie sent
            +0x28 tapN   = 2      +0x2c dragN  = 0      +0x30 dragRows = 0
            +0x34 startN = 0                     <- START on a not-ready picker did nothing
```

**Harness limit found (worth recording).** The gdb stub stops answering once the app leaves the
picker for a **session** (three emulated threads) or for `run_settings` — every read after that
returns `timeout (5.0s) waiting for stub data` even though the app keeps rendering. So state reads
must be taken *while the screen under test is up*; that is why run A's numbers come from run B's
long picker hold instead. Not a defect in this slice, but the next slice should not lose an hour
to it.

### Host suites

| Suite | Result |
|---|---|
| celiolink | 1259, 0 |
| netlink | 66, 0 |
| diag | 376, 0 |
| control | 6897, 0 |
| trace_replay | 58, 0 (4 skips) |
| tilt | 1723, 0 |
| presence | 49756, 0 |
| uigeom | 18301, 0 |
| **uihit (new)** | **371, 0** |
| `tools/emutest/tests/run_host_tests.sh` | Ran 147 tests — OK |

Build clean: `rompicker.c` emits **4** warning lines where the pre-slice tree emitted **7** — all
of them pre-existing (`slot_card`/`cart_tint` unused, `savpicker_run`'s `kHeld`/`kUp`); `uihit.c`
emits none.

### Deviations (and why)

1. **"the ROM list rows" are not a tap target — they physically cannot be.** They live on the
   *top* screen (`select-*-top`, manifest `x16 y37 w369 h197`) and the 3DS has no top-screen
   digitiser. The touch-only path is the one SPEC-input §I1.3.2 specifies and this slice proves:
   *drag on the bottom screen until the row is visible → tap the slot card → tap START.*
2. **The picker's drag-scroll (I2.2) landed here even though I2 is a separate section.** Tap-vs-drag
   is inseparable from the gesture machine that fixes D2 — without it every scroll would activate
   whatever the finger lifted off. The **pause-menu / settings content scroll (I2.4)**, the `.sav`
   picker (I2.5.1) and the wireless list (I2.5.2) are untouched and remain another slice's.
3. **Q2 answered "focus zone", not "key hints".** The task asked for d-pad reachability *with a
   visible focus*, which hints cannot provide. Consequence, recorded: inside the list, UP/DOWN no
   longer **wrap** — at the ends they step out to the bottom-screen controls (and the chain wraps
   as a whole). The old wrap made the bottom controls unreachable by definition.
4. **Only the picker's half of I3.** I3.3 (swatch d-pad), I3.4 (volume bar), I3.5 (wireless stale
   rects), I3.6/I3.7 (pad zones + menu chip), I3.8 (tab rail arithmetic) and I3.9 (dead v2 layout
   engine) are in `main.c` / `wireless.c` / `touch.c` and are *not* in this slice. `uihit.c` is
   built to take their tables next (`uihit_index` + a `static const UiRect[]` each).
5. **`PICK_SLOT_B` is an EMPTY rect in single mode, not an absent 5th entry** (§I1.5.3 says five
   entries). One table shape keeps `index == PickTarget`, and "cannot be hit" is expressed as
   `w == 0`, which `uihit_in` rejects first. Asserted both ways in T1/T3.
6. **The settings chip's interim rect `6,214,88,22` (§I1.6.3) still overprints the plate's baked
   footer hint** — that is REPORT **D7** and it is SPEC-visual's call (open question Q1: the footer
   band has to be re-composited or the plate regenerated). This slice only guarantees *drawn rect
   == hit rect*; it made the overprint slightly wider (84×16 → 88×22) because a touch target needs
   ≥ 20 px in the short axis. Visible in `runs/p17-f2-targets/bottom_00060.png`.
7. **`tapN` counts every resolved gesture, including one that hits nothing** (the spec's wording is
   ambiguous). A miss is then distinguishable by `lastHit == -1`, which is what T10 asserts — and
   "the app saw a tap and decided it hit nothing" is more useful than a silent counter.
8. **I1.7.3's caller-side half is NOT done here.** `rompicker_run` now returns false only for
   *no ROMs* / *the prompt's "Use defaults"*, but `main.c:4456` still turns false into a hard-coded
   `gameA.gba`/`gameB.gba` session. That is REPORT **D13** (the missing empty state) and belongs to
   SPEC-visual.

### Not in this slice

D1/D5/D6/D21 (closed by F1), D3/D4 (gamepad zone geometry), D7/D8/D9/D16/D18 (layout + the pause
scroll), D10/D11/D12 (theme/contrast), D13/D14/D15/D17/D19/D20/D22. Nothing here is hardware-proven
— Azahar cannot speak for the real digitiser's accuracy or the 804 MHz frame budget (CLAUDE.md #6).

---

## Slice F3 — touch drag-scroll (SPEC-input I2: the picker list, the pause tabs, the scrollbars)

**Date:** 2026-08-09 · **Scope:** the user's third ask — *"the scroll using touch should work"* —
i.e. SPEC-input **§I2** end to end: the ROM-list drag proven (I2.2), the pause/settings **content
scroll** wired for real (I2.4), and an **honest scrollbar** on both (I2.4.6).
**Sweep defects closed:** **D8** (DISPLAY's Swap/Skip row overprinted by the hint line and clipped
by the screen edge), **D9** (TOUCH's EDGES row colliding with the hint line), **D18** (the
near-white scrollbar strip with an inverted thumb). **Suites:** 10/10 green; `uihit` **371 → 1489**
checks. **`celiolink.c` diff: 0 lines** (`git diff source/celiolink.c | wc -l` = 0).

### What changed

| File | Change |
|---|---|
| `source/uihit.{c,h}` | **+11 functions, all pure C.** Content scroll: `uihit_content_h` (derived from the control table, so it cannot drift the way `PTABN[]` historically did), `uihit_max_scroll`, `uihit_scroll_px` (1:1, clamped), `uihit_follow_rect` (d-pad focus follow, with a `UIHIT_MENU_LEAD` caption band). Scrolled hit test: `uihit_in_scrolled` / `uihit_index_scrolled`. Scrollbar: `uihit_thumb_h` / `uihit_thumb_y` — units cancel, so ONE pair serves the pixel scroll (menuScroll/maxScroll) and the row scroll (topRow/maxTop). New constants `UIHIT_MENU_VIEW_H 228`, `RAIL_W 82`, `CONTENT_X 93`, `THUMB_MIN 24`, `LEAD 14`. |
| `test/host/test_uihit.c` | **T8 / T8b / T9 / T10** — golden manifest tables for the two overflowing tabs, the "a tab that fits must not scroll" rule, the clamp at both ends, focus-follow for every row, the scroll transform (incl. an exhaustive sweep proving **no** point with `py ≥ 228` ever resolves to a control at **any** offset), and the thumb: monotone, inside the track, **at the top when scroll is 0**. |
| `source/main.c` | `PT_DISPLAY` / `PT_TOUCH` back at their **manifest** coordinates (I2.4.2). New `MenuDiag g_menuDiag` (§0.2.2). New `pctl_rects` (one table for draw + hit), `menu_ov_label`, `menu_plate_bg`, `menu_draw_plate` (the **plate scrolls with its controls**), `menu_draw_chrome` (hint band + honest scrollbar). Pause input rewritten on `UiGesture`; `run_settings` given the same scroll, gesture and a **Done chip fixed to its drawn rect** (I2.4.7). |
| `source/rompicker.c` | An honest scrollbar for the ROM list, drawn only when the list overflows. |

**The design problem this slice had to solve, and the decision.** The manifest says the content
panel scrolls, but the plate art is only 240 rows tall and it **bakes the section captions**
("SCALE · TOP", "GAMEPAD · COLOR"). Scrolling the controls alone would tear every label off its
row. So `menu_draw_plate` draws the **whole plate shifted up**, then repaints the tab rail from the
unscrolled art (the rail is fixed chrome). Two consequences, both handled:
1. below the art there is nothing, and "nothing" in a pause menu is the **dimmed game** showing
   through — seen as an olive tear in the first run (`p17-f3-menu/bottom_00070.png`). Fixed by
   `menu_plate_bg`, which paints from a **verified-empty region of the plate itself** (x82..309,
   rows 226..233) instead of `g_ui.bg`. That matters because only the **indigo** art pack is built
   (REPORT D10/D12), so on any other theme a `g_ui.bg` fill is a different colour from the plate
   and would be the most visible mismatch on the screen. Same source now backs the hint-band blank
   and the scrollbar-column overpaint.
2. the plate's last **two** rows are the clipped top of a below-fold caption whose body the art
   does not contain — `MENU_PLATE_KEEP 238` drops them when scrolled, so a 2 px glyph sliver is
   never stranded mid-row.

### The proof loop

```bash
export DEVKITPRO=/opt/devkitpro DEVKITARM=$DEVKITPRO/devkitARM && make -j8
tools/emutest/run ctm make docs/phase17-uifix/movies/<M>.json /tmp/m17f3_*.ctm
tools/emutest/run azctl boot --gdb --movie /tmp/m17f3_*.ctm
tools/emutest/run gdbio resume                       # BEFORE see rec — see the harness note below
tools/emutest/run see rec --seconds N --fps 3 --screen both --with-state <sym> --out runs/p17-f3-*
tools/emutest/run azctl stop
```

| Run | Movie | Theme | Output |
|---|---|---|---|
| A | `movies/m17_menu_scroll.json` | Daylight (whatever settings.bin held) | `runs/p17-f3-menu/` (198 f, `--with-state g_menuDiag+0x14`) |
| B | same movie | **Indigo** (`setbin.py theme=0`) | `runs/p17-f3-menu-indigo/` (198 f) |
| C | `movies/m17_list_drag.json` | Indigo, 11 dummy ROMs | `runs/p17-f3-list/` (174 f, `--with-state g_pickDiag+0x0c`) |
| D | `/tmp/m17f3_short.json` | Indigo, 11 ROMs | `runs/p17-f3-nums/` (72 f, 4 state symbols — the counter proof) |
| E | `movies/m17_settings_scroll.json` | Indigo | `runs/p17-f3-settings/` (156 f) |

Hygiene: one Azahar at a time, `azctl stop` after every run, `mkroms.py clean` at the end
(`removed 11`, `sdmc:/3DGBA` holds only `settings.bin`), `settings.bin` restored, `qt-config.ini`
restored byte-identically five times, `sdmc/dual-gba` never touched (no `--fresh-sd-fixtures` was
needed — 11 dummies already overflow the 8-row list).

### The numbers, read live over GDB

**Run A — `g_menuDiag+0x14` (`menuScroll`) per captured frame:**

```
frames   0- 46  scroll=0     picker/session, then the pause menu on DISPLAY
frames  47- 47  scroll=20    the drag is moving
frames  48- 49  scroll=40
frames  50- 81  scroll=53    <- CLAMPED: the finger asked for 80, maxScroll(281) = 53
frames  82- 83  scroll=3     the drag back
frames  84-118  scroll=0     <- CLAMPED at the other end
frames 119-120  scroll=30    TOUCH tab
frames 121-151  scroll=55    <- CLAMPED at maxScroll(283) = 55
```

**Run D — the tap-vs-drag counters, `g_pickDiag` `topRow / tapN / dragN / lastHit`:**

```
frames   7- 43   0 | 0 | 0 | -1     picker idle
frames  44- 44   1 | 0 | 1 | -1     the drag crossed the threshold
frames  45- 45   2 | 0 | 1 | -1
frames  46- 53   3 | 0 | 1 | -1     <- topRow clamped at maxTop (11-8); tapN STILL 0
frames  54- 67   3 | 1 | 1 |  1     a short tap: tapN 0->1, lastHit = PICK_SLOT_A
```

The drag began at **(160,205) — inside the START—LINKED button** — and `tapN` never moved and
`lastHit` stayed −1. That is "a drag must never activate a row", measured.

### What I SAW — before / after

**D8, the DISPLAY tab (the toggles that lived on the hint line).**
*Before* `runs/p17-f1-tabs/bottom_00110.png` (post-F1, pre-F3): the bottom row is a gold toggle and
the word "Swap" **printed on top of** "L/R tab  A select  B resume  (or tap)", with a second toggle
("Skip") half off the right edge — three things fighting for one 14 px band. The right edge carries
the baked near-white strip.
*After* `runs/p17-f3-menu-indigo/bottom_00071.png` (scroll 53): "**Swap screens**" + its toggle and
"**Frameskip**" + its toggle are two clean, fully-visible rows at y≈181 and y≈210, the hint line
below them is untouched and legible, and the panel background runs seamlessly to the screen edge —
no tear where the plate art ends. `bottom_00046.png` (scroll 0) is the same tab at rest: four seg
rows, a clean hint line, **no** toggle debris.

**T16, the scrolled hit test.** `runs/p17-f3-menu-indigo/bottom_00090.png`: the tap at **(284,219)**
— the frameskip toggle's *scrolled* position, 53 px above where the control "is" — flipped it
**on** (gold, with its gold focus ring) and the status line reads "**Frameskip on**". At scroll 0
that same tap resolves to nothing, and `uihit_index_scrolled` is asserted both ways in T9.

**D9, the TOUCH tab.**
*Before* `runs/p17-f1-tabs/bottom_00210.png`: "Round / Soft / Sharp" squashed into a 172×14 sliver
at the very bottom with the hint text running through it letter-for-letter, and a baked caption
sliced by the screen edge below it.
*After* `runs/p17-f3-menu-indigo/bottom_00150.png` (scroll 55): a **full-width segmented control**
at the manifest's `93,253,208,30`, reading "Round / Soft / Sharp" with "Round" selected as a clean
gold pill — well clear of the hint line — under a code-drawn "PAD EDGES" caption in the plate's own
caption style. The swatch row above it is at its manifest `195,h31`.

**D18, the scrollbar.** Column profiles at device x=315 (`runs/p17-f3-menu-indigo`):

| offset | track | thumb | verdict |
|---|---|---|---|
| scroll 0 | y182–221 `(55,45,91)` = `g_ui.line` | **y7–180** `(177,170,199)` = `g_ui.dim` | thumb at the **TOP** |
| scroll 53 (max) | y6–46 | **y48–220** | thumb at the **BOTTOM** |

Thumb length 174 px of a 216 px track = `228 × 216 / 281` — the visible fraction of the content,
exactly. Outside the track the column reads `(31,24,46)` — the plate's own background, i.e. the
baked near-white strip is gone. On the **non-scrolling** SESSION tab
(`bottom_00040.png`) **no bar is drawn at all** and the tab is 1:1 with
`screenshots/pause-tab-1-session.png`.

**The ROM list drag (the user's ask, in the app).**
`runs/p17-f3-list/top_00050.png` → `top_00070.png`: the visible rows go
`FOXTROT / ALPHA / zz-…long / GOLF / CHARLIEQUEST / HOTEL / JULIETT / BRAVOGAME` →
`GOLF / CHARLIEQUEST / HOTEL / JULIETT / BRAVOGAME / INDIA / ECHO / DELTA`. **The list visibly
moved** by exactly 3 rows (= maxTop for 11 ROMs; the finger asked for 6), the white focus ring
followed the scroll onto the new first row, and the new list scrollbar's thumb went from
**y42–173 (top)** to **y93–224 (bottom)** — 132 px of a 184 px track = 8/11, the visible fraction.

**A drag never activates; a tap does.** `runs/p17-f3-list/bottom_00088.png`: after that drag — which
*started inside START—LINKED* — the picker is still the picker (no session launched) and a
subsequent tap on card A committed **GOLF**, the row the drag had brought into view. The threshold
pair, measured on slot card B: a **5 px** move committed it (`bottom_00120.png`: "B · BOTTOM SCREEN
/ GOLF", brightness step 515→665 at frame 120) and a **12 px** move immediately after left it
**unchanged** through frame 173. The second drag also *began on the mode pill* at (160,40) and the
header stayed "**2 Games**" the whole time — the control D2 used to flip by accident.

**Run_settings (I2.4.7).** `runs/p17-f3-settings/bottom_00120.png`: the standalone Settings screen
scrolls identically (thumb top→bottom at x315 across frames 75→95), the below-fold toggles appear,
and the gold "**Done**" chip is drawn last as fixed chrome. `bottom_00145.png`: the tap at
(280,230) — inside the *drawn* rect `244,224,72,14`, which is now also the *hit* rect — returned to
a clean picker.

**Six themes.** Run A was Daylight (white seg tracks, bronze accent `(190,122,22)`, dim
`(110,103,132)`), run B Indigo. Both scroll identically and both scrollbars track the offset in the
theme's own colours — the input arithmetic is theme-independent by construction (it never reads a
colour) and the bar is drawn from `g_ui.line`/`g_ui.dim`.

### Host suites

| Suite | Result |
|---|---|
| celiolink | 1259, 0 |
| netlink | 66, 0 |
| diag | 376, 0 |
| control | 6897, 0 |
| trace_replay | 58, 0 (4 skips) |
| tilt | 1723, 0 |
| presence | 49756, 0 |
| uigeom | 18301, 0 |
| **uihit** | **1489, 0** (was 371) |
| `tools/emutest/tests/run_host_tests.sh` | Ran 147 tests — OK |

Build clean: `make -j8` emits **no new warnings** in `main.c`, `uihit.c` or `rompicker.c` — the
remaining lines are the same pre-existing set F2 recorded (`clrTxt`/`clrPanel`/`clrSelTxt`,
`ease_out`, `splash_panel`, the dead v2 `menu_layout` engine, `slot_card`/`cart_tint`, and four
`-Wmisleading-indentation` sites I did not touch).

### Harness note worth keeping

**`see rec --with-state` must run AFTER `gdbio resume`, not before.** Started first, its broker
claims the boot's one gdb client, `gdbio resume` then times out, and the emulator stays **parked**
— the capture is 150 frames of Azahar's "Launching…" splash, which looks like a rendering bug and
is not one. (First run of this slice, discarded.) SKILL.md's "start rec before resume" is right
only for a plain, stateless `rec`.

Also re-confirmed, and now bounded: the gdb stub **stops answering after ~60–150 halt/cont
blinks**, regardless of screen — run C lost state at frame 149 while still sitting in the picker,
and every post-run `read-u32` failed. So take numeric proof **early** (run D is 36 s and got
everything) and treat late reads as unavailable, not as zero.

### Deviations (and why)

1. **The plate scrolls with the content.** SPEC-input I2.4 only specifies the *input* side; it does
   not say what happens to the baked captions. Drawing the shifted plate is the only option that
   keeps a label attached to its row, so it is here rather than deferred to SPEC-visual — the
   scroll would be actively wrong without it.
2. **`maxScroll = contentH - VIEW_H` only when `contentH > 240`, else 0.** The spec's literal
   `CONTENT_H[tab] - VIEW_H` floored at 240 gives every non-overflowing tab **12 px** of pointless
   travel and would draw a scrollbar on four tabs whose plates bake no track — contradicting
   §I2.4.6's own sentence. Asserted as T8's "a tab that FITS must not scroll at all".
3. **Content height is DERIVED (`uihit_content_h`) instead of the spec's `CONTENT_H[6]` literal.**
   A hand-kept table is the exact shape of the `PTABN[]` trap main.c:2213 documents. Derivation
   gives 281 for DISPLAY (matching the spec) and 283 for TOUCH (the spec guessed 284).
4. **Q5 answered "no".** The design's "See gamepad" button (`93,294,208,31`) is NOT added: it
   duplicates the two Preview buttons already on the tab, and adding a control is a visual decision
   this slice has no mandate for. `PTABN[5]` stays 5; TOUCH's content is 283, not 325.
5. **`UIHIT_MENU_LEAD = 14`, not in the spec.** Focusing a below-fold control by d-pad with a bare
   "bring the rect into view" rule parks its top edge at the viewport top and scrolls its own baked
   caption out of sight — a focus ring on an unlabelled widget. The lead band pulls the caption in
   and snaps to 0 near the top.
6. **The `ov` caption can now sit ABOVE its control.** Restoring PAD EDGES to the manifest's
   full-width `x=93` leaves no room to its left — the old right-aligned caption would have printed
   **on the tab rail**. `menu_ov_label` picks by geometry, so TILT/CO-OP/Swap/Frameskip keep the
   existing right-aligned idiom and only the full-width row moves. The design's own caption for
   that row is baked art that does not exist below y=240; regenerating the plate is SPEC-visual's.
7. **At scroll 0 the below-fold row no longer peeks.** `screenshots/pause-tab-2-display.png` shows
   "Swap screens" half-visible at the bottom edge; our app puts the status hint in that band, and a
   row peeking into it **is** REPORT D8. The blanked band wins; the honest thumb (81 % of the
   track) is what now says "there is more below".
8. **`run_settings`: the Done chip overlaps the frameskip toggle by ~4 px at max scroll**
   (`bottom_00120.png`) — both are at the manifest's own coordinates and they genuinely collide in
   that band. Fixed chrome is drawn last, so Done wins. The clean fix is a settings-specific
   `VIEW_H` (222), which changes geometry shared with the pause menu — deferred, not hidden.
9. **I2.5 is NOT in this slice.** `savpicker_run` (I2.5.1, open question **Q4**) and the wireless
   scan list (I2.5.2) are untouched. Reaching `savpicker` needs a live session with a `.sav`
   present, and `wireless.c`'s rects are the **stale-v1** mess §I3.5 owns — shipping either
   unproven would break this phase's own rule. Recorded as the next slice's, with the reason.
10. **`g_menuDiag.lastHit/tapN/dragN` are written but were only read in the picker's case.** The
    stub-longevity limit above meant the pause-menu counters could not be sampled late in a run;
    the pause-menu proof is therefore the per-frame `scroll` curve plus the pixels. The fields are
    live and correct by inspection, not by measurement — flagged rather than claimed.

### Not in this slice

D1/D5/D6/D21 (F1), D2 + picker button-nav (F2), D3/D4 (gamepad zone geometry), D7 (the picker's
settings chip over the baked footer), D10/D11/D12 (the one-art-pack gap — the runs above show it:
the Daylight run draws Daylight widgets on Indigo plates), D13/D14/D15/D16/D17/D19/D20/D22, and all
of I3 except the picker half F2 did. Nothing here is hardware-proven — Azahar cannot speak for the
real digitiser's accuracy, drag feel, or the 804 MHz frame budget (CLAUDE.md #6).

---

## Slice F4 — layout collisions (SPEC-layout L1 · L2 · L3 · L4 · L5)

**Date:** 2026-08-09 · **Scope:** the pile-ups the earlier slices did not own — the picker's
settings chip over the plate's baked footer, the virtual gamepad fighting the HUD and the footer
hint, the AUDIO row's stray channel glyph + missing level, the TOUCH tab's missing explainer, and a
re-verification that F3's DISPLAY/TOUCH scroll still holds.
**Sweep defects closed:** **D3**, **D4**, **D7**, **D15**, **D16**; **D8 / D9 re-verified** (F3's,
unregressed). **Suites:** 10/10 green; `uihit` **1489 → 1718** checks. **`celiolink.c` diff: 0 lines.**

### What changed

| File | Change |
|---|---|
| `source/uihit.{c,h}` | New pure-C **`uihit_wrap`** — greedy word wrap over a *measurement callback*, UTF-8-safe hard break, `...` truncation instead of overflow. `PICK_SETTINGS` moved `6,214,88,22` → **`6,223,88,16`** (L2.3). |
| `test/host/test_uihit.c` | **T11** (+229 checks): all three explainer copies at ten font widths, no line over 208 px, no split UTF-8 sequence, monotone line count, ellipsis on truncation, degenerate inputs. Plus the L2 footer rows — the lifted hint band 212..221 must stay **dead**, the chip owns 223..238 and nothing above or below it. |
| `source/rompicker.c` | `pick_footer_band()` + `pick_footer_chip()` — the footer is **recomposed from the plate's own pixels** (L2). |
| `source/main.c` | L3.1 bottom HUD bar suppressed in Gamepad mode; L3.2 centred footer hint suppressed in Gamepad mode; the `≡ menu` chip wired as a real tap target; L4 the channel-tag draw deleted at **both** sites and replaced by the manifest-placed level; L5 `TOUCH_EXPLAIN[3]` + `menu_touch_explainer()` called from the pause menu **and** `run_settings`. |
| `source/touch.{c,h}` | `touch_menu_chip()` — ONE rect for the `≡ menu` chip's draw and its hit test; `pad_keys` no longer resolves a tap there to **A**. |

**L2, the decision and why.** The plate BAKES its footer hint (measured per-pixel on
`select-dual-bot`: rows 198..218 and 230..239 are pure background, **219..229** carry
"tap a game above ↑ · ⚡ linked = trade / battle-ready"; `select-single-bot` is the same at 220..229)
and the chip was drawn straight on top of it. There is no free 18 px band to move the chip into —
START/LINKED end at y=211. Retyping the hint in code (SPEC-layout L2.2's letter) was **rejected**:
the copy contains "↑" and "⚡", which the `mkbcfnt` bake does not carry, and it would have to be
retyped per plate variant. Instead the band is rebuilt from the art itself: blank rows 212..239 by
stretching a **verified-empty slice of that same plate** (rows 232..239 — not `g_ui.bg`, because
only the indigo pack is baked today and a token fill would be a visibly different colour on any
other theme; this is F3's `menu_plate_bg` idiom), then re-blit the baked hint 8 px higher. The
glyphs, the colours and all six themes' art survive verbatim.

### The proof loop

```bash
export DEVKITPRO=/opt/devkitpro DEVKITARM=$DEVKITPRO/devkitARM && make -j8
tools/emutest/run ctm make docs/phase17-uifix/movies/<M>.json /tmp/m17f4_*.ctm
tools/emutest/run azctl boot --gdb --movie /tmp/m17f4_*.ctm
tools/emutest/run gdbio resume                       # ALWAYS after boot
tools/emutest/run see rec --seconds N --fps 3 --screen both --out tools/emutest/runs/p17-f4-*
tools/emutest/run azctl stop
```

| Run | Movie | SD state | Output |
|---|---|---|---|
| A | `movies/m17_f4_picker_footer.json` | 11 dummy ROMs | `runs/p17-f4-picker/` (165 f) — the **lift-10** build, kept as the evidence for the mid-slice defect below |
| B | same | same | `runs/p17-f4-picker2/` (150 f) — the final build |
| C | `movies/m17_f4_tabs.json` | ROM-less | `runs/p17-f4-tabs/` (225 f) — AUDIO, TOUCH ×3 modes, DISPLAY + scroll |
| D | `movies/m17_f4_pad.json` | ROM-less, `setbin touch=1 hud=3 padEdge=0` | `runs/p17-f4-pad/` (210 f) — gamepad, the `≡ menu` tap, TOUCH at max scroll, volume `−` |
| E | `movies/m17_picker_dpad.json` | 11 dummy ROMs | `runs/p17-f4-dpad/` (165 f) — the focus ring on the moved chip |
| F | `/tmp/m17f4_set.json` | 11 dummy ROMs | `runs/p17-f4-set/` (105 f) — d-pad UP + A opens Settings |
| G | `/tmp/m17f4_settouch.json` | 11 dummy ROMs | `runs/p17-f4-settouch/` (150 f) — `run_settings`' TOUCH tab |

Hygiene: one Azahar at a time, `azctl stop` after every run, `mkroms.py clean` + `setbin --restore`
at the end (`settings.bin` byte-restored, `sdmc:/3DGBA` holds only `settings.bin`), `qt-config.ini`
restored byte-identically seven times, `sdmc/dual-gba` never touched. Theme = **Indigo** for every
run (the only baked plate pack).

### What I SAW — before / after

**D7, the settings chip over the baked footer hint.**
*Before* `runs/p17-f2-targets/bottom_00060.png`: an opaque near-white pill reading "settings · ZR"
sits ON the hint, which then reads "**…ve ↑ · ⚡ linked = trade / battle-ready**" — the first three
words are under the pill.
*After* `runs/p17-f4-picker2/bottom_00060.png` + 4× crop `/tmp/z_f4_dual.png`: the hint reads
**"tap a game above ↑ · ⚡ linked = trade / battle-ready"** end to end, arrow and lightning bolt
intact, and the chip sits **below** it, complete and fully on-screen.
Single mode, `bottom_00090.png` + `/tmp/z_f4_sing.png`: **"one game · full 3D + touch · link
locally or online"**, same result.
Measured on the capture (rescaled to device px), dual: the START button's gold fill at x=40 ends at
row **210**, the hint's ink runs **212..221**, the chip's `g_ui.panel2` fill `(49,39,87)` runs
**223..238** and row 239 is background again — three bands, zero overlap, and 223..238 is exactly
the `uihit` rect the tap is tested against.
Compare `screenshots/03-game-select.png`: hint unobstructed, which is now true of ours too (the
design carries no chip at all; ours is 11 px lower than the baked position and otherwise identical).

**A defect the capture caught mid-slice, and the fix.** The first build lifted the hint by 10 px and
drew the band *before* the START buttons. `runs/p17-f4-picker/bottom_00090.png` (single mode) shows
the result: the hint's **glyph tops are shaved off** — "one game · full 3D…" with the ascenders
clipped — because rows 210..211 of the lifted band were painted over by the button fills drawn
afterwards. Fixed by lifting **8** px and drawing the band **after** the widgets, so it starts at
y=212 where the buttons have already ended. `runs/p17-f4-picker2/bottom_00090.png` is the same
frame with complete glyphs. A state read would have called the first build a pass.

**D3, HUD text under the gamepad's L/R keys.**
*Before* `runs/p17-f1-hud2/bottom_00100.png`: "gameB" is printed inside the **L** key's box and
"53fps 02:00" inside the **R** key's box — the HUD bar and the pad both own y=0..26.
*After* `runs/p17-f4-pad/bottom_00050.png` + 4× `/tmp/z_p4_top.png`: the top band is exactly
**L | TOUCH · GAMEPAD | R** over bare video. No name, no fps, no clock, no bar. The TOP screen
(`top_00050.png`) still carries the full HUD — "● gameA · ●FOCUS … 3D 53fps 02:00 🔋" — so nothing
was lost, it moved to the screen the design puts it on. 1:1 with `screenshots/06-touch-gamepad.png`.

**D4, the footer hint through START.**
*Before* `runs/p17-f1-hud2/bottom_00100.png`: "START+SELECT · pause menu" runs letter-for-letter
through the START key's lower half.
*After* `runs/p17-f4-pad/bottom_00050.png` + 5× `/tmp/z_p4_bot.png`: **START** is a complete
rounded key with its label alone inside it, and the **"≡ menu"** chip is complete at bottom-right.
No centred text anywhere on the screen. (Confirming the spec's correction to the sweep: the chip
was never half off-screen — the hint crossing START is what made the row look cut.)

**The `≡ menu` chip was decoration; now it is a control.** With the hint gone from Gamepad mode the
chip is the only touch route to the menu — and nothing hit-tested it. Worse, `pad_keys`' A zone
(`px > 252 && py > 150`, unbounded downwards) meant a tap there **pressed A in the game**.
`runs/p17-f4-pad`: frames 15..80 are the gamepad; the movie taps **(290,229)**; `bottom_00090.png`
is the **pause menu, SESSION tab**. One tap, menu open, no A press (opening the menu takes that
frame's game-input branch away). `touch.h`'s `touch_menu_chip()` is the single rect the draw and the
hit both read.

**D15, the AUDIO row.**
*Before* `runs/p17-f1-tabs/bottom_00135.png`: a lone tiny "A" (resp. "B") floats above the left end
of each bar, jammed against the round "−" and half on the bar's top edge; no number anywhere.
*After* `runs/p17-f4-tabs/bottom_00110.png` + 5× `/tmp/z_f4_aud.png`: **"VOLUME · A"** (the plate's
own baked caption) on the left and **"100"** right-aligned above the bar's right end on the same
baseline; the stray glyph is gone from both rows. That is `manifests.json`'s "vol A value"
(298,66,11,12) / "vol B value" (298,117,11,12) and matches `screenshots/pause-tab-3-audio.png`.
**It tracks the real value:** `runs/p17-f4-pad/bottom_00180.png` + 6× `/tmp/z_p4_aud.png` — after
three "−" presses VOL A reads **63** and the bar's fill is 63 % of the track. 256 − 3×32 = 160,
and (160·100+128)/256 = 63 exactly. Per L4.2.1 the design's "80" is unreachable on a ±12.5 % step,
so the true value is shown rather than the mockup's digits.

**D16, the missing touch-mode explainer.**
*Before* `runs/p17-f3-menu-indigo/bottom_00120.png` region y=66..105: bare plate — a ~53 px hole
between the TOUCH MODE segment and the Preview buttons (the plate bakes no ink at all on rows
20..164 of the content column, measured).
*After* `runs/p17-f4-tabs/`, one frame per mode, 4-5× crops:
- Off (`bottom_00120.png`, `/tmp/z_f4_expl_off.png`) — *"Off — a touch opens the pause menu. No game input from the touch screen."* (2 lines)
- Gamepad (`bottom_00150.png`, `/tmp/z_pad.png`) — *"Gamepad — a translucent virtual controller (D-pad, A/B, L/R, START) over game B."* (2 lines)
- Smart (`bottom_00170.png`, `/tmp/z_smart.png`) — *"Smart — the touch screen is a pointer on the real Gen-3 UI: tap-to-walk, tap menus/party/targets, double-tap = START."* (3 lines)
The paragraph **changes with the segment** in the same run, no reload. Nothing reaches the Preview
buttons at y=109 (the longest copy ends at y≈95). The em dash survived the bcfnt bake — checked in
the zoom, it renders as a dash, not a tofu box. Same on the standalone Settings screen
(`runs/p17-f4-settouch/bottom_00106.png`), the second call site.

**D8 / D9 re-verified (F3's, unregressed).** `runs/p17-f4-tabs/bottom_00215.png`: DISPLAY at max
scroll shows **"Swap screens"** and **"Frameskip"** as two complete rows with their toggles, the
hint line clean below them. `runs/p17-f4-pad/bottom_00135.png`: TOUCH at max scroll shows the
**PAD EDGES** caption + the full-width "Round / Soft / Sharp" segment clear of the hint — and the
new explainer scrolls **with** the content (it is drawn at `TEXPL_Y - scroll`), so it never detaches
from its tab.

**Button nav to the moved chip.** `runs/p17-f4-dpad/bottom_00092.png` + 6× `/tmp/z_d4_ring.png`:
the white focus ring is crisp on all four sides of the chip at its new rect, on the plate rather
than on the chip's fill, and clear of the hint above it. `runs/p17-f4-set/`: d-pad **UP** from the
list focuses the chip (frames 55..70) and **A** opens the Settings screen (`bottom_00090.png`,
Display tab). Touch does the same: `runs/p17-f4-picker2/bottom_00125.png` — the tap at (40,230)
opened Settings. Draw rect, hit rect and focus rect are one rect.

### Host suites

| Suite | Result |
|---|---|
| celiolink | 1259, 0 |
| netlink | 66, 0 |
| diag | 376, 0 |
| control | 6897, 0 |
| trace_replay | 58, 0 (4 skips) |
| tilt | 1723, 0 |
| presence | 49756, 0 |
| uigeom | 18301, 0 |
| **uihit** | **1718, 0** (was 1489) |
| `tools/emutest/tests/run_host_tests.sh` | Ran 147 tests — OK |

Build clean: `make -j8` emits **no new warnings** in `main.c`, `rompicker.c`, `touch.c` or
`uihit.c` — the remaining lines are byte-for-byte the pre-existing set F3 recorded (`clrTxt` /
`clrPanel` / `clrSelTxt`, `ease_out`, `splash_panel`, the dead v2 `menu_layout` engine,
`slot_card` / `cart_tint`, `savpicker_run`'s `kHeld`/`kUp`, and the `-Wmisleading-indentation`
sites in `main.c` and `touch.c`'s party hit-tests, which only changed line numbers).

### Deviations (and why)

1. **L2.2 is implemented by RE-BLITTING the baked hint, not by retyping it in code.** The spec's
   `assets_text_c(..., "tap a game above ↑ · ⚡ linked = …")` cannot work: `mkbcfnt` does not bake
   "⚡" (U+26A1), the string differs per plate variant, and it would have to be re-derived if the
   art pack is ever regenerated. Lifting the plate's own pixels keeps the copy, the metrics and all
   six themes' colours correct **by construction**. `tools/build_assets.sh` is untouched, so L2.4's
   rejected Pillow alternative stays rejected.
2. **The chip is 16 px tall, not the 22 px F2 gave it.** 212..239 is all the space below the START
   buttons, and it has to hold a 10-row hint plus the chip plus a gap. 16 px with a 2 px bottom
   margin is what fits; the affordance is also reachable by **ZR** and by d-pad, and this is a
   stylus-era digitiser. Recorded rather than hidden.
3. **L3.2 gates the footer hint on `tmEff != TOUCH_PAD`, not on `tmEff == TOUCH_OFF`.** The spec
   removes it in Smart mode too — but Smart's replacement chips are **L6's**, which is not this
   slice, so obeying the letter would have shipped a mode with *no* menu affordance at all. In
   Gamepad mode the hint genuinely collides with START (that IS D4) and the `≡ menu` chip replaces
   it; in Smart mode nothing collides and the hint still reads "START+SELECT → pause menu", which is
   true. **Flip this to `== TOUCH_OFF` in the slice that lands L6.1/L6.3.5.**
4. **The `≡ menu` chip becoming a real tap target is beyond L3's letter.** It is the direct
   consequence of requirement L3.2 — removing the only affordance text from a mode whose one
   remaining affordance was decorative — and of the phase brief's "make sure both button and touch
   control are working properly". It also fixes a silent input bug (a tap on the chip used to press
   **A** in the game). `pad_keys` gained one guard line; no zone coordinate moved.
5. **The explainer allows 4 lines at a 10 px lead, not the spec's 3 at 11 px.** 4 × 10 from y=66
   ends at 105 and the Preview buttons start at 109; the 3-line budget would have truncated the
   Smart copy if the bake had come out wider than it did. As shipped the Smart copy needs 3 lines,
   so the 4th is headroom, and `uihit_wrap` ellipsises rather than overflowing if a future string
   exceeds even that (T11 asserts both).
6. **Wrapping is a `uihit` function with a measurement callback, not inline `main.c` code.** A
   hand-counted character budget is exactly how a 4th line lands on a button; deriving the layout
   from the real measured width makes it provable on the PC (T11 sweeps three copies × ten font
   widths) while the metrics still come from the live bcfnt.
7. **L1.1.2 / L1.1.3 / the "See gamepad" button are NOT here.** F3 already restored the manifest
   coordinates and answered Q5 "no" (it duplicates the two Preview buttons); `PTABN[5]` stays 5.
   This slice only re-verified that F3's scroll still holds after its own edits.
8. **L1.3 (moving the status hint to the pause TOP screen) NOT taken.** F3 solved the same collision
   by blanking the hint band as fixed chrome and giving the content a 228 px viewport, and the
   captures show no overlap at any scroll position on any tab. Moving the line now would be churn on
   a working screen for no pixel gain. Recorded so the requirement is not silently dropped.

### Not in this slice

L6 (the Smart-mode chips + the cyan `field p=9,4 key=-` debug line, **D17**), L7 (the white first
frame **D14** and the ROM-less empty state **D13**), L8.1 (per-title cart tints, **D20**), L8.3
(overlay label styles, **D19**), L8.4 (**D22**, deferred by the spec), and D10 / D11 / D12 (the
one-art-pack gap — every run above draws Indigo widgets on Indigo plates because that is the only
pack `build_assets.sh` bakes). Nothing here is hardware-proven: Azahar cannot speak for the real
digitiser, drag feel or the 804 MHz frame budget (CLAUDE.md #6).

## Slice F5 — contrast, themes, startup, polish (SPEC-widgets W4 · SPEC-layout L6 · L7 · L8)

**Date:** 2026-08-09 · **Scope:** the class of defect that makes a screen *unreadable* rather than
*ugly* — the "3D" badge, every non-Indigo theme's dark-on-dark labels, the smart-touch mode's
missing indicator + shipped developer readout, the session's first frames, the ROM-less dead
session, and the three cheap polish items.
**Sweep defects closed:** **D11**, **D10**, **D17**, **D13**, **D19**, **D20**; **D14** *corrected
and hardened* (see below); **D12** partially (its unreadable half; the residual is the one-art-pack
gap). **Suites:** 11/11 green — `theme` is new, **82047 checks**. **`celiolink.c` diff: 0 lines.**
**`smoke.sh`: run/read-state/press-ctm/see/see-rec all PASS** (Tier B SKIP without `--rom`).

### What changed

| File | Change |
|---|---|
| `source/theme.{c,h}` | **`g_art`** + `theme_init_art()` (W4.3.a) — the palette the *baked art* was made from. `rel_lum()` and **`theme_on_scrim()`** made public. `theme_make_custom` fixed in two places found by the new suite (`ink` picked from the accent's luminance; `dim` derived from `panel2`). A `THEME_HOST_SHIM` guard so the file dual-compiles on the PC (CLAUDE.md #4). |
| `test/host/test_theme.c` + `test/host/ctr_shim.h` *(new)* | TH1–TH6, **82047 checks**: a 30-row inventory of every (ink, surface, draw-scale) triple the app draws, graded for 5 presets **and** custom over its whole hue × contrast grid; the role-colour lock; **TH3/TH4 are the red-before-green regressions that name D11 and D10.** |
| `tools/build_assets.sh` / `source/assets_gen.h` | stamps **`ASSET_THEME_ID`**; `assets_init()` feeds it to `theme_init_art()` before anything can draw. |
| `source/ui.{c,h}` | **`ui_chip_2` / `ui_chip_2w`** (separate frame + ink roles) and **`ui_chip_measure`**; all four chip helpers now draw the label with the **baked `FNT_JBM_MED` at native size** and a **rounded** frame. `g_renderSeq` moved here (see the harness note). |
| `source/main.c` | the 3D badge routed to `THEME_3D_TEXT`-on-`THEME_GAME_B`; HUD clock/fps + the pause status line + the in-game footer hint moved off the 0.30–0.38-scale system font; `hudAcc = theme_on_scrim(g_ui.acc)`; **L6.1** `touch_draw` called for SMART; **L6.2** the cyan readout behind `TOUCH_DIAG_HUD` (default 0); **L3.2** the footer-hint gate finally flipped to `== TOUCH_OFF`; **L7.1** texture memset + `everUploaded` + `render_game_gate`; **L8.3** `PCtl.ovs` (OV_ROW / OV_SECTION) + `menu_ov_label` rewritten; ink on baked surfaces → `g_art`. |
| `source/rompicker.c` | **L7.2** `empty_state_run()` — the ROM-less empty state; **L8.1** per-title cart caps (`cart-blank` + a tinted band). |
| `source/touch.c` / `.h` | the SMART branch now draws its mode chip **and** `≡ menu` (which SMART also hit-tests); `TOUCH_DIAG_HUD`; the chips use the baked font; the hamburger is drawn as quads. |
| `tools/emutest/tests/fixtures/movie_menu_tilt_quit.json` | Tier A gains one tap — **"Start without a game"** (L7.2.3). |

### The proof loop

```bash
export DEVKITPRO=/opt/devkitpro DEVKITARM=$DEVKITPRO/devkitARM && make -j8
tools/emutest/run ctm make docs/phase17-uifix/movies/m17_f5_theme.json /tmp/m17f5_theme.ctm
tools/emutest/run azctl boot --gdb [--fresh-sd-fixtures] --movie /tmp/m17f5_*.ctm
tools/emutest/run gdbio resume                       # ALWAYS after boot
tools/emutest/run see rec --seconds N --fps 3 --screen both --out tools/emutest/runs/p17-f5-*
tools/emutest/run azctl stop
```

| Run | Config | Output |
|---|---|---|
| A | ROM-less, Indigo | `runs/p17-f5-empty/` (243 f) — the empty state, its d-pad focus, its settings chip |
| B | `--fresh-sd-fixtures`, touch=Smart | `runs/p17-f5-rom/` (600 f) — the picker with real ROMs |
| C | 13 dummy ROMs (`mkroms.py`) | `runs/p17-f5-pick/` — the cart-cap colours |
| D | fixtures, touch=Smart | `runs/p17-f5-sess/` (600 f @ 6 fps) — session start, the HUD, Smart mode |
| E | `theme=0..5`, ROM-less | `runs/p17-f5-th{0..5}/` (276 f each) — the six-theme sweep |
| F | `theme=2`, final build | `runs/p17-f5-th2b/` — the accent-lift re-shot |
| G | `theme=4`, final build | `runs/p17-f5-fin4/` — a final-binary regression pass |

Hygiene: one Azahar at a time, `azctl stop` after every run, `mkroms.py clean` + `azctl
clean-fixtures` (`originals re-hashed, all untouched`), `setbin.py --restore`, `qt-config.ini`
restored byte-identically every time, `sdmc/dual-gba` never written.

### What I SAW — before / after

**D11, the "3D" badge (W4.2).**
*Before* `runs/sweep-verify/rec-tabs/top_00040.png`, 5× crop: a **square** blue box whose two
glyphs are barely-there dark-navy smudges on the black bar.
*After* `runs/p17-f5-sess/top_00260.png`, 6× crop: a **rounded** blue frame with clearly readable
**light-blue "3D"**, beside a crisp gold "15fps" and near-white "02:00".
Measured on the real pixels inside each badge's frame: brightest glyph pixel **(63,103,162) on
(6,5,10) = 3.55:1** before → **(144,170,204) = 8.52:1** after. Both halves were needed and the
suite says which does what: the baked font is what realises the contrast, the role pair is what
makes it the handoff's `#a9d4ff`-on-`#3E86D6` badge and adds the headroom that has to survive the
real panel's gamma (REPORT §6 warns D11 gets *worse* on hardware).

**D10, Daylight dark-on-dark (W4.3.a).**
*Before* `runs/sweep-verify/rec-theme/bottom_00135.png`: on LINK, "Save state" / "Load state" /
"Load .sav" are dark navy ink on the dark navy indigo button bodies — I can barely find the words;
"Wireless lobby..." is *white* on gold; "CO-OP" is a tiny dim all-caps tag left of its toggle.
*After* `runs/p17-f5-th2/bottom_00085.png` (same tab, same theme): all three read **crisp white**,
"Wireless lobby…" is **dark ink on gold**, and the third row reads "**Co-op presence**" in the same
size, weight and colour as the baked "Link cable" above it.
**All six themes, same frame:** `p17-f5-th{0,1,2,3,4,5}/bottom_00085.png` — every label is readable
in every theme and the **only** thing that differs is the accent (gold on Indigo/OLED/Retro,
**bronze** on Daylight, **green** on Duo, **teal** on Custom), which is exactly the intent: while
one art pack ships, the accent is the visible proof a theme was chosen.

**The rule, visible in one frame.** `runs/p17-f5-th2b/bottom_00030.png` (Daylight empty state):
"Rescan" is dark ink on the *indigo-baked* gold button and "Start without a game" is white on the
*indigo-baked* body (both `g_art`), while the "settings · ZR" chip beside them is Daylight's own
near-white panel with Daylight dim ink (`g_ui`, because that chip's fill is procedural). Two
palettes on one screen, each on its own surface.

**D12, the split screen.** `runs/p17-f5-th2b/top_00058.png`: the unreadable half is gone — every
element on the HUD bar is legible on Daylight. The *residual* split (a beige theme background under
a constant black scrim) is inherent to a theme-invariant scrim and only a per-theme plate pack
fixes it — W4.3.b, deferred (OQ3). Recorded, not claimed.

**A defect my own capture found, and the fix (W4.1's third line, refined).**
`runs/p17-f5-th2/top_00058.png`: Daylight's focused "59fps" was dark bronze **on** the grey bar —
measured **1.54:1**, the least readable thing on the HUD. The accent is deliberately still drawn on
the scrim (it is the theme's only in-game signal), so `theme_on_scrim()` lifts any accent below the
scrim floor halfway to white. *After* `runs/p17-f5-th2b/top_00058.png`: the same glyphs measure
**2.62:1** and read as warm sand — see the two 2×/4× crops. The suite prints the per-theme numbers:
`Daylight accent BE7A16 -> DEBC8A on the bar: 6.61:1 (nominal) / 2.83:1 (white game)`.

**D17, Smart touch.** `runs/p17-f5-sess/bottom_00260.png`: a gold "**TOUCH · SMART POINTER**" chip
top-centre, the "**≡ menu**" chip bottom-right, **zero cyan text** anywhere, and no centred footer
hint. The cause was not what the report guessed: `touch_draw`'s SMART branch already existed and
the call site simply read `if (tmEff == TOUCH_PAD)`.

**A tofu the capture caught.** With the chip labels on the baked font, the `≡` came out as a solid
block (8× crop, `runs/p17-f5-sess`): **U+2261 is not in JetBrains Mono**, so `mkbcfnt` cannot bake
it — it had survived only because the system font has it, at the blurry scale this slice removes.
The hamburger is three quads now: `runs/p17-f5-smart2/s.bottom.png` 6× shows **three crisp gold
bars + "menu"**. The same reasoning retired "⚡LINK" → "LINK" (U+26A1 is an emoji codepoint, absent
from both faces) *before* it could ship as a box. "●FOCUS" was checked and **does** bake
(`runs/p17-f5-sess/top_00169.png`, 7×).

**D13, the ROM-less empty state.** `runs/p17-f5-empty/top_00040.png`: "**No games found**" centred
in bold, with "**put .gba files in sdmc:/3DGBA/**" under it. `bottom_00040.png`: a gold "**Rescan**"
(focused, white ring) and "**Start without a game**", the plate's baked footer hint intact beneath
them and the settings chip below that. It is operable both ways: `bottom_00066.png` shows the
d-pad ring moved to the second button, and the tap at (40,230) opened the **Settings** screen
(`bottom_00090.png`, Display tab). Before this, a first-ever boot with no ROMs dropped straight
into a black dead session with no explanation.

**D20, per-title cart caps.** `runs/p17-f5-pick/s.top.png` with 13 ROMs: the caps alternate
**coral (238,129,98)** and **gold (239,209,94)** down the list — sampled per row, none is the
Game-A green `#63B23C` or the Game-B blue `#3E86D6` (L8.1.1). *The capture also found a second
bug:* with the spec's `code[0] + code[3]` hash, Emerald (BPEE) and FireRed (BPRE) came out
**identical** (`runs/p17-f5-rom/top_00150.png`) — char 0 is the type and char 3 the region, which
is 'E' for every US release. It hashes all four chars now.

**D19, overlay labels.** Best seen on LINK above: "Co-op presence" is now the same face, size and
ink as the baked rows. `runs/p17-f5-th2/bottom_00100.png` shows the other style — "**DIORAMA ·
TILT**" as a small dim all-caps caption *above* its segmented row, matching the plate's own
"SCALE · TOP" captions.

**D14, corrected — and this one is a downgrade of the original claim, not a fix.**
The spec said the white/grey first frames were an uninitialised `C3D_TexInit` allocation. I shipped
the hardening it asks for (a `memset` + flush at creation, plus an `everUploaded` gate at the three
`render_game` call sites, leaving `render_game` itself byte-identical) — but the capture says the
diagnosis was wrong. `runs/p17-f5-sess`: the picker is on screen at frame 166; frame 167 is the
session with a **white** 240×160 rect on top and a **flat (128,126,128)** rect on the bottom; both
fade white → grey → black together at frames 174/175/176 and the Game Freak logo follows at 210.
The bottom is exactly the top's white at the unfocused 50 % dim — i.e. **both screens are showing
the same authentic frame the ROM itself draws while it boots**, which is why the white survives a
memset that provably zeroes the texture. The undefined-memory case is removed by construction; the
remaining second of white is the game, and suppressing it would be wrong.

**Nothing regressed.** Final-binary pass, `runs/p17-f5-fin4/`: the pause top screen's eight
outlined pills are intact and their labels now crisp (`top_00070.png`), and the TOUCH tab still
shows the explainer, the Preview buttons and the five rounded swatches (`bottom_00105.png`).

### Host suites

| Suite | Result |
|---|---|
| celiolink | PASS |
| netlink | PASS |
| diag | 376, 0 |
| control | 6897, 0 |
| trace_replay | PASS |
| tilt | 1723, 0 |
| presence | 49756, 0 |
| uigeom | 18301, 0 |
| uihit | 1718, 0 |
| **theme (new)** | **82047, 0** |
| `tools/emutest/tests/run_host_tests.sh` | Ran 147 tests — OK |
| `tools/emutest/smoke.sh` | run/read-state/press-ctm/see/see-rec **PASS** |

Build clean: `make -j8` emits **no new warnings** in the files this slice touched — the remaining
lines are byte-for-byte F4's pre-existing set, minus one (`cart_tint` is no longer unused).

### The harness dependency this slice had to pay for (L7.2.3)

The empty state breaks two things that quietly depended on the ROM-less fall-through, and both are
fixed in the same commit:

1. **Tier A's movie** drove the pause menu of the accidental dead-core session. It now taps
   "Start without a game" first — one line, and the behaviour is *labelled* instead of accidental.
2. **`g_renderSeq` never moved at boot**, so `smoke.sh`'s `read-state` and `see` channels both went
   red: the counter lived in `main.c` and was bumped only by `run_session`. SKILL.md documents it as
   "~60 Hz liveness", which was only true by accident. It lives in `ui.c` now and **every** UI frame
   loop bumps it (splash, prompt, picker, empty state, settings, `.sav` picker, wireless). The D1
   watchdog's uses are phase-modulo or pass-through and only run inside a session, so a non-zero
   starting value changes nothing there. `read-state` now reports `renderSeq changed 0 -> 15`.

### Deviations (and why)

1. **W4.3.b (six baked art packs) NOT taken** — filed, per OQ3's own recommendation. It is ≈1.7 MB
   of ELF, a load/unload path in `assets.c`, an init-order change and a visible stall on theme
   switch, and it needs its own 30-frame capture pass. W4.3.a makes every theme *readable*; (b) is
   what makes them *coherent*, and D12's residual split is its acceptance test.
2. **W4.3.a step 4 (mark un-baked themes in the theme list) has nowhere to go.** There is no theme
   picker in the shipped v3 menus — `ACT_THEME` exists only in the dead v2 `menu_layout` engine, so
   the theme is selectable only through `settings.bin`. `theme_art_is()` is implemented and tested,
   ready for whoever restores the control. Recorded rather than silently dropped.
3. **The contrast gate is 3.0:1, not the spec's 4.5:1** (OQ6). The handoff's own presets do not
   clear 4.5 on ink-on-accent (Daylight's white on `#BE7A16` is 3.5:1), so a 4.5 gate would measure
   the designer's palette rather than this phase's defect. Body-text pairs are held to 4.5
   separately in TH1b and do clear it. 3.0 still catches D11 (1.56) and D10 (1.21) red.
4. **The coverage model keys off DRAW SCALE, not `px`.** The spec's `alpha(px)` proxies the real
   cause; with the baked fonts, 8 px at a 9 px native size is scale 0.89 (coverage ~1.0) while the
   system font at 8 px was scale 0.32. Keying on px would have graded the fix as no change.
5. **Two `theme_make_custom` fixes that are not in any spec line**, both found by TH6 and neither
   visible by eye: `ink` was a hardcoded near-black regardless of accent hue (3.2:1 on a blue
   accent), and `dim` was a fixed 54 % white while `panel2` climbed to 45.6 % — dim-on-panel2 at the
   top of the contrast slider measured **1.30:1**, i.e. the picker's settings chip and the presence
   card's note were invisible in the user's own theme. `ink` now follows the accent's luminance
   across the exact 0.179 black/white crossover; `dim` is derived *from* `panel2`.
6. **`theme_on_scrim()` is new API, not in W4.** W4.1 line 3 says ink on a constant scrim must be
   theme-invariant — but the accent is *deliberately* drawn there, and the capture showed Daylight's
   at 1.54:1. Lifting the accent keeps the hue (and the signal) while removing the dependence on the
   game frame. Applied ONLY on the HUD/footer; every other `g_ui.acc` is on a procedural surface and
   is untouched.
7. **L6.3.2–L6.3.4 (the Smart guidance chips) NOT shipped, deliberately.** They are gated on
   `sm.ctx ∈ {field menu, party, bag, battle …}`, which needs a real Gen-3 game to reach a menu; no
   harness session gets there, so shipping them would mean shipping code this phase cannot look at.
   L6.3.6 (the tap-cursor ring) is marked optional by the spec and is out for the same reason.
   L6.1/L6.3.5 — the actual defect — are in and photographed.
8. **The empty state reuses the picker's plate, so the plate's baked (empty) A/B slot cards are
   still visible behind it.** Drawing a bespoke plate is an art change; the spec explicitly says to
   reuse the picker's own geometry rather than invent a layout. Visible in
   `runs/p17-f5-empty/bottom_00040.png` and recorded rather than hidden.
9. **`rompicker_run` keeps its `bool` return; L7.2.2's tri-state is not needed.** "No games" is no
   longer a return value at all — it is a screen that loops until the user chooses. Only the two old
   meanings survive, and "Start without a game" is now the *only* way to reach the defaults
   fallback, which is exactly the labelling L7.2.2 asked for.
10. **The status line and footer hint became plain strings** (they were pre-parsed `C2D_Text`), and
    the hint's "→" became "·" — the arrow is not in the bake and would have been the next tofu.
11. **The picker's list scrollbar keeps `g_ui.line`/`g_ui.dim`** although it sits on baked art. It
    is a shape, not ink; the theme-coloured bar is legible on the indigo plate in all six runs.
12. **`assets_gen.h` was hand-edited to add `ASSET_THEME_ID 0`.** It is generated, and
    `build_assets.sh` now emits exactly that line, so the next asset build reproduces it byte for
    byte; `assets.c` also carries an `#ifndef` fallback so an older generated header still compiles.

### Not in this slice

D22 (the aspect-fit HUD scrim — deferred by SPEC-layout L8.4 with the reason: it needs an origin
offset inside `render_game`, the HD-2D dispatch site this phase may not touch, and hardware
sign-off). D12's residual coherence split and the whole of W4.3.b. Everything here is
Azahar-only: real-panel gamma will move the contrast judgements — most likely against D11 — and
CLAUDE.md #6 keeps the frame budget and anything timing-sensitive unproven until a New 3DS runs it.

---

## Slice F6 — the FIX PASS over the re-sweep / code-review findings (10 items)

**Date:** 2026-08-09 · **Scope:** the 10 findings the re-sweep + code review raised against F1–F5.
**Outcome:** 1 major and 6 minors **fixed and photographed**, 1 minor **verified and pinned by a
test instead of changed** (with the reason), 1 minor **fixed after its diagnosis was corrected by
the capture**, 1 minor **no longer reproducible and honestly reported as unattributed**.
**Suites:** 10/10 green — `theme` **82047 → 82693** (new TEST 7), `uigeom` **18301 → 18332** (new
TEST 12). **`celiolink.c` diff: 0 lines** (`git diff source/celiolink.c | wc -l` = 0).

### Per-finding outcome

| # | File:line | Sev | Verdict | What happened |
|---|---|---|---|---|
| 5 | rompicker.c:572 | **major** | **FIXED** | disabled START ink → `theme_ink_disabled`; measured **1.46:1 → 3.63:1** on the real pixels |
| 1 | main.c:4414 | minor | **FIXED** | every pause/settings selection ring is `sel_ring` (rounded, radius taken from the widget) |
| 8 | assets.c:195 | minor | **FIXED** | `assets_button`'s focus ring + the accent/destructive frame → `ui_border_round(ASSETS_BTN_R)` |
| 9 | assets.c:206 | minor | **FIXED** | seg TRACK `g_ui.panel → g_art.panel`, its label `g_ui.dim → g_art.dim`; Daylight white slab gone |
| 6 | main.c:1623 | minor | **FIXED** | presence nameplate + prompt now `theme_on_scrim(g_ui.acc)` (code-proven, see caveat) |
| 7 | rompicker.c:347 | minor | **FIXED** | the empty state no longer re-blits "tap a game above ↑" |
| 3 | rompicker.c:369 | minor | **FIXED** | the empty state blanks the plate's baked A/B slot cards |
| 4 | rompicker.c:482 | minor | **FIXED**, diagnosis corrected | the check had to move to a ROM-HEADER test — see below |
| 10 | touch.c:114 | minor | **VERIFIED, not changed** | counts reproduced exactly (351/189/81) and pinned by `uigeom` TEST 12 |
| 2 | main.c:2534 | minor | **NOT REPRODUCIBLE**, unattributed | see "the one I could not close" |

### What changed

| File | Change |
|---|---|
| `source/theme.{c,h}` | **`theme_ink_disabled(ink, surface)`** — the ink for a disabled control on a SOLID surface: `ink` mixed toward that surface by the largest fraction ≤ 38 % that still clears 3.0:1 (a fixed 38 % would have re-broken the Daylight art pack, whose own enabled ink is only 3.5:1). |
| `source/assets.{c,h}` | `assets_button`'s focus ring and the accent-outline/destructive frame → `ui_border_round(..., ASSETS_BTN_R)`; new public `ASSETS_BTN_R 8.0f` so a ring cannot guess its button's radius. `assets_seg`'s track → `g_art.panel`. |
| `source/main.c` | new `sel_ring()` (rounded selection ring, radius from the widget) applied at all 5 pause + 5 `run_settings` widget cases and the swatch marks; presence chrome → `theme_on_scrim`; `presence_draw_card`'s frame → rounded; **`rom_header_check()`** + `EmuInstance.loadFailed` + **`draw_load_error()`** on both screens; seg call sites pass `g_art.dim`. |
| `source/rompicker.c` | START's not-ready ink → `theme_ink_disabled`; `pick_footer_band(plate, withHint)`; new `pick_blank_cards()`; the empty state gains one dim line and drops the two dead cards + the hint. |
| `source/wireless.c` | the lobby seat cards' and the scan rows' rings → `ui_border_round(ASSETS_BTN_R)` (same silhouette mismatch, same `fill-card-r8` bodies). |
| `test/host/test_theme.c` | **TEST 7** (the disabled-ink regression, red-before-green in all five art packs) + `SRC_DIS` inventory row; the seg-label row re-graded as an ART pair. |
| `test/host/test_uigeom.c` | **TEST 12** — the gamepad overlay's per-frame quad budget, with ceilings. |

### The proof loop

```bash
export DEVKITPRO=/opt/devkitpro DEVKITARM=$DEVKITPRO/devkitARM && make -j8
tools/emutest/run ctm make docs/phase17-uifix/movies/<M>.json /tmp/fp*.ctm
tools/emutest/run azctl boot --gdb [--fresh-sd-fixtures] --movie /tmp/fp*.ctm
tools/emutest/run gdbio resume                       # ALWAYS after boot
tools/emutest/run see rec --seconds N --fps 3 --screen both --out tools/emutest/runs/fixpass/*
tools/emutest/run azctl stop
```

| Run | Movie | SD / theme | Output |
|---|---|---|---|
| A | `movies/fp_picker.json` | 12 dummy ROMs, Indigo | `runs/fixpass/a-picker/` (390 f) |
| B | `movies/fp_empty.json` | ROM-less, Indigo | `runs/fixpass/b-empty/` (168 f) |
| C | `movies/fp_theme2.json` | 12 dummy ROMs, **Daylight** | `runs/fixpass/c-daylight/` (297 f) |
| D | `movies/fp_sess.json` | `--fresh-sd-fixtures` (real Emerald + FireRed) | `runs/fixpass/d-sess2/` (285 f) |
| E | `resweep/movies/r4_smart.json` replay | fixtures | `runs/fixpass/e-r4repeat/` (discarded: the movie needs a resume prompt that was not there) |

Hygiene: one Azahar at a time, `azctl stop` after every run (`qt-config.ini` restored byte-identically
five times), `mkroms.py clean` + `azctl clean-fixtures` (`originals re-hashed, all untouched`),
`setbin.py --restore`; `sdmc/dual-gba` never written.

### What I SAW

**Finding 5 — the disabled START (the major).**
*Before* `runs/resweep/r1-picker/bottom_00035.png`, 6× crop `evidence/fixpass/fp_start_before.png`:
the word "START" is a **pale lavender ghost** on the gold — I can only find it because I know it is
there. *After* `runs/fixpass/a-picker/bottom_00035.png`, same crop
(`evidence/fixpass/fp_start_after.png`): a solid **dark olive-brown "START"**, plainly readable and
still obviously weaker than the enabled dark-navy. Measured inside the button body (x24..124,
y184..202, ink = the darkest 25 px, fill = the brightest 400):
**(180,171,187) on (239,209,94) = 1.46:1 → (120,103,61) on (239,209,94) = 3.63:1**
(3.63 is the antialiased peak; the nominal pair is 4.23:1, which TEST 7 prints per art pack).

**Findings 1 + 8 — the rings.**
*Before* `runs/resweep/zooms/z-squarering-rounded-btn.png`: a square gold ring with a dark
right-angle gap inside each corner. *After* `runs/fixpass/a-picker/bottom_00375.png`, 9× crop
`evidence/fixpass/fp_prev_ring.png`: the focused "Preview Gamepad" ring is a smooth rounded
staircase with **no notch and nothing protruding**.
*Before* `runs/resweep/zooms/z-seg-pill-corner.png`: the selected "1:1" pill's LEFT corners filled
square by the row's ring while its right corners were round. *After*
`runs/fixpass/a-picker/bottom_00230.png` (`evidence/fixpass/fp_seg_after2.png`): the pill is
rounded on **all four** corners with the ring's own rounded corner outside it.
*Before* `runs/resweep/r6-empty-retro/bottom_00035.png`, 9× (`fp_rescan_before.png`): the white
focus ring on "Rescan" is a square with a dark right angle biting into the button. *After*
`runs/fixpass/b-empty/bottom_00060.png` (`fp_rescan_after.png`): the ring follows the button's
rounded silhouette. The swatch marks too — `evidence/fixpass/fp_swatch.png`, 7×: a **rounded** white
outline around the rounded gold swatch. `run_settings` carries the identical ring
(`runs/fixpass/b-empty/bottom_00120.png`, SCALE·TOP selected).

**Finding 9 — the white slab, same tab, same theme, before and after.**
*Before* `runs/p17-f5-th2/bottom_00105.png` (Daylight, pause TOUCH): the TOUCH MODE track is a
**pure #FFFFFF slab** on the indigo plate. *After* `runs/fixpass/c-daylight/bottom_00280.png`: the
track is the plate's own indigo, the bronze accent pill still says "Daylight is active", and the
inactive labels read better, not worse. Sampled: track **(255,255,255) → (40,32,64)**; the
"Gamepad" cell's label/track contrast **3.97:1 → 5.33:1**. On Indigo, `g_art.panel == g_ui.panel`,
so the shipped default theme is byte-for-byte unchanged (`runs/fixpass/a-picker/bottom_00035.png`).

**Findings 3 + 7 — the empty state.**
*Before* `runs/resweep/r6-empty-retro/bottom_00035.png`: two empty labelled cards
("A · TOP SCREEN" / "B · BOTTOM SCREEN", swatches, no names) above Rescan / Start, and the baked
footer telling the user to "tap a game above ↑" when there is nothing above.
*After* `runs/fixpass/b-empty/bottom_00060.png`: **no cards, no hint** — a clean plate with one dim
line, "put .gba files in sdmc:/3DGBA/, then Rescan", the two buttons, and the settings chip. The
top screen is unchanged ("No games found" + the path).

**Finding 4 — and the capture corrected the diagnosis.** My first fix probed the file after
`gbacore_load_rom` failed. It never fired: `runs/fixpass/a-picker/bottom_00175.png` (that build,
running) is the **same silent grey rect with a live "35fps 02:01" HUD** the finding photographed —
because `gbacore_load_rom` hands the file to `mCorePreloadVF`, which only COPIES BYTES, so a
256-byte header-only dummy "loads" and the ARM core then executes garbage. The check moved to the
cartridge header (`rom_header_check`: the full 0xC0 header present and the fixed **0x96 at 0xB2**,
which is mGBA's own `GBAIsROM` magic), and it runs **before** the core sees the file. *After*
`runs/fixpass/c-daylight/top_00185.png` and `bottom_00185.png`: both screens carry
**"This game could not be loaded / not a valid .gba - pause menu, Change games"** in the
destructive red on a dark rounded panel. A **missing** file is still silent — that is the labelled
"Start without a game" path the harness's Tier A is built on, and `rom_header_check` returns −1
there. (Deliberately not the 156-byte Nintendo logo: trained/patched ROMs differ there.)

**Finding 10 — verified, then pinned.** `uigeom` TEST 12 reproduces the reviewer's arithmetic on the
real call sites: **padEdge Round = 351, Soft = 189, Sharp = 81** rect draws per frame for the 9 keys
(was 63 with the pre-W2 decomposition). Not changed, for a stated reason: every cheaper draw
changes the pixels — the keys are TRANSLUCENT, so the obvious "rounded fill in `line` + inset fill
on top" would double-blend the interior, which is the exact invariant `uigeom`'s non-overlap
contract exists to protect. The budget test makes a future radius/step change fail loudly instead
of silently costing 5×. Per CLAUDE.md #6 this is a quad COUNT, not a timing claim — Azahar cannot
price a frame, so it goes on the real-hardware list.

**Finding 6 — fixed, and honestly only code-proven.** `presence_draw_chrome`'s two accent draws now
go through `theme_on_scrim`, the same lift `hudAcc` already had at every other accent-on-scrim site.
I could **not** photograph it: the nameplate and the "A - CARD" prompt need co-op presence ON *and*
two real Gen-3 saves standing on the same map, which no harness movie can reach (the same limit
F5 recorded for the Smart guidance chips). The colour maths is covered by `theme` TEST 5, which
prints Daylight's `BE7A16 -> DEBC8A` and its 6.61:1 on the bar.

### The one I could not close — finding 2, the stray pill

I reproduced the reviewer's frame exactly and then could not make it come back.

- **It is app chrome, not the game.** At `runs/resweep/r4-smart/top_00265.png` (running) the region
  x120..137 y148..156 reads max **24**; at `top_00266.png` (paused) it reads max **64** while the
  whole frame gets *darker* (mean 25.9 → 15.9). A frozen game frame cannot brighten under a scrim.
- **It is not the baked plate.** `plates/indigo/pause-top.png` rows 144..161 are a flat
  `(9,7,13,189)` scrim, checked per pixel.
- **It is gone.** `runs/fixpass/d-sess2/top_00220.png` is the *same paused screen* — same ROMs,
  same theme, same 8 feature pills, same frozen grass frame — and a pixel diff against
  `runs/resweep/r4-smart/top_00267.png` returns **273 differing pixels, all of them inside
  x120..137 y148..156**. Everything else on the two frames is identical.
- **I could not attribute it.** Nothing in this slice's diff draws on the paused top screen, and it
  appears in exactly one recorded run of the pre-fix build (absent from every paused frame of
  `p17-f5-sess`, `p17-f5-fin4`, `p17-f1-tabs`, `p17-f4-tabs`, `resweep/r2-tabs`, `resweep/r3-pad`).

So I am NOT claiming a fix. The most likely remaining explanation is a state- or stack-dependent
draw (this slice added a field to the stack-allocated `EmuInstance`, which moves `run_session`'s
frame), i.e. it may be **masked rather than removed**. It needs a build with a probe or the phase-13
gamestate logger to name, and that is the follow-up.

### Host suites

| Suite | Result |
|---|---|
| celiolink | 1259, 0 |
| netlink | 66, 0 |
| diag | 376, 0 |
| control | 6897, 0 |
| trace_replay | 58, 0 (4 skips) |
| tilt | 1723, 0 |
| presence | 49756, 0 |
| **uigeom** | **18332, 0** (was 18301 — TEST 12) |
| uihit | 1718, 0 |
| **theme** | **82693, 0** (was 82047 — TEST 7 + SRC_DIS) |
| `tools/emutest/tests/run_host_tests.sh` | Ran 147 tests — OK |

Build clean: `make -j8` emits **no new warnings** in `main.c`, `rompicker.c`, `assets.c`, `theme.c`,
`wireless.c` — the remaining lines are byte-for-byte F5's pre-existing set.

### Deviations (and why)

1. **`theme_ink_disabled` is a search, not a constant.** A flat 38 % mix measured 2.29:1 on the
   Daylight art pack — i.e. the fix would have reintroduced the defect on the pack W4.3.b adds next.
   Backing off in 6 % steps until the pair clears the suite's own 3.0 gate makes the rule
   "as dim as this palette can afford", and TEST 7 asserts both ends in all five packs.
2. **The disabled-label inventory row is `body = 0`.** Held to TH1b's 4.5 it would demand that a
   deliberately-weakened affordance read as strongly as an active one. TEST 7 separately asserts it
   stays strictly UNDER the enabled ink, which is the property that actually matters.
3. **Finding 9 also moved the seg's `dim` argument to `g_art.dim`.** Changing only the track would
   have left Daylight's `g_ui.dim` (#6E6784) on the new indigo track at **2.83:1** — under the gate.
   Track and label are one surface decision, so they move together.
4. **Finding 4 refuses on the HEADER, which means the harness's own `mkroms.py` dummies now show the
   banner.** That is correct — they are not GBA ROMs — and it is what made the fix provable. Any
   future movie that wants a *live* dummy session must set byte 0xB2 to 0x96.
5. **The picker does not grey out invalid ROMs in the list.** It would be the better affordance, but
   it needs a header read per row in `scan_roms` and a list-row style this phase has no reference
   for. The banner is the honest minimum: the failure now says what happened. Recorded, not hidden.
6. **The dead v2 `menu_layout` engine keeps its square `ui_border` rings.** It has no call sites
   (the compiler says so). F1 deleted three dead widget helpers rather than fix them; deleting this
   one is a bigger edit than a fix pass should carry, and fixing dead code would be unprovable by
   capture. Flagged so a future revival does not inherit the defect.
7. **`wireless.c`'s two rings are changed but NOT photographed.** Reaching the lobby needs a live
   session plus a radio; both card bodies are `fill-card-r8` at r=8, the identical call shape proven
   on every other button in this slice. Called out rather than claimed.
8. **Finding 10 is answered with a test, not a change** — see above.

---

## FINAL GATE — the clean-shell re-run, the warning audit, and the proof pass on the shipped binary

**Date:** 2026-08-09 · **Scope:** no new features. This entry is the phase's exit gate: every suite
re-run from a fresh shell, the build re-done from clean, the warnings audited against HEAD rather
than against memory, and **every headline defect re-photographed on the binary that actually ships**
— because F1–F6 each proved themselves against their *own* build, and a phase is only as good as its
last binary.

### The one code change in this entry

`make -j8` on the final tree emitted **one warning that HEAD did not**: `main.c:4760`
`-Wmisleading-indentation`, on `run_settings`' `PK_STEP` case, where `if(sel)sel_ring(...); break; }`
puts an unguarded `break` at the guarded statement's indentation. Braced (`if(sel){...} break;`).
Semantics unchanged; the warning is gone.

It is worth naming **how** it was found, because "no new warnings" is the kind of claim that is
usually taken on faith. The phase brief said *stash-and-compare, do not take "pre-existing" on
faith*, so:

```bash
git worktree add --detach /tmp/g_head HEAD
ln -s <repo>/external /tmp/g_head/external      # gitignored, real dir
cp source/assets_gen.h /tmp/g_head/source/ ; cp -R data /tmp/g_head/   # generated, gitignored
cd /tmp/g_head && make -j8                       # HEAD, same toolchain, same flags
# then diff the per-file, per-message warning SETS (not the counts, and not the line numbers,
# which move with every edit):
grep -E "/source/[a-z_]+\.(c|h):[0-9]+:[0-9]+: warning:" build.log \
  | sed -E 's|.*/source/||; s/:[0-9]+:[0-9]+: warning: /|/' | sort | uniq -c
```

**Result after the fix — the phase's warning set is a strict SUBSET of HEAD's:**

| Change | Warning |
|---|---|
| **gone** | `main.c` `variable 'menuScroll' set but not used` |
| **gone** | `rompicker.c` `'cart_tint' defined but not used` |
| **gone** | `rompicker.c` one `-Wmisleading-indentation` |
| **gone** | `rompicker.c` `unused variable 'kHeld'` / `'kUp'` ×1 each (2 → 1) |
| added | *(none)* |

Everything else — `celiolink.c`'s `cl_partner_party`, `main.c`'s dead v2 `menu_layout` engine and
its `clrTxt`/`clrPanel`/`clrSelTxt`, `netlink.c` / `theme.c` / `touch.c` / `ui.c`'s
`-Wmisleading-indentation` sites, `wireless.c`'s `-Wformat-truncation` — is byte-for-byte HEAD's.

### The gate

| Suite | Result |
|---|---|
| celiolink | **1259**, 0 |
| netlink | **66**, 0 |
| diag | **376**, 0 |
| control | **6897**, 0 |
| trace_replay | **58**, 0 (4 loud skips) |
| tilt | **1723**, 0 |
| presence | **49756**, 0 |
| uigeom | **18332**, 0 |
| uihit | **1718**, 0 |
| theme | **82693**, 0 |
| **total** | **164 379 checks, 0 failures** |
| `tools/emutest/tests/run_host_tests.sh` | **Ran 147 tests — OK** |

`make clean && make -j8` → `3DGBA.3dsx` **3 822 776 B** · `make cia` → `3DGBA.cia` **1 939 392 B**,
both clean. `git diff source/celiolink.c source/netlink.c source/gbacore.c` = **0 lines**.

### The proof pass — six runs on the shipped binary

Every run: one Azahar, `azctl stop` after, `qt-config.ini` restored byte-identically,
`sdmc/dual-gba` never written. `settings.bin` and `recent.bin` were snapshotted before the pass and
**md5-verified identical** after it; the dummy ROMs were removed (`mkroms.py clean`).

| Run | Config | Output |
|---|---|---|
| P1 | Indigo, 11 dummy ROMs, no `recent.bin` (cold picker) | `runs/p17-final-picker/` (270 f @ 3 fps) |
| P2 | Indigo, resume → session → all six pause tabs + two scroll drags | `runs/p17-final-tabs/` (300 f) |
| P3 | `touch=1 padEdge=0 hud=3` virtual gamepad | `runs/p17-final-pad/` (210 f) |
| P4 | `theme=2` Daylight, same movie as P2 | `runs/p17-final-daylight/` (270 f) |
| P5 | ROM-less | `runs/p17-final-empty/` (180 f) |
| P6 | `touch=2` Smart pointer | `runs/p17-final-smart2/` (285 f) |

**Harness note (cost me one run, worth recording).** `mkroms.py`'s dummies are header-only, so since
F6's `rom_header_check` they correctly trip the new "could not be loaded" banner. Any movie that
wants a *live* dummy session must set **byte 0xB2 = 0x96** first (F6 deviation 4 predicted exactly
this). Also: `setbin.py --help` is not a flag — it is parsed as a field name, and `mkroms.py --help`
**creates the ROMs**.

### What I SAW on the shipped binary

- **D1** — `p17-final-tabs/bottom_00120.png` (DISPLAY): four selected gold pills reading exactly
  `1:1`, `1:1`, `Sharp`, `both`; `bottom_00160.png` (AUDIO): `Solo`; `bottom_00178.png` (ENHANCE):
  `Off`; `bottom_00215.png` / `bottom_00240.png` (TOUCH): `Off` and `Round`;
  `p17-final-picker/bottom_00087.png` / `00104.png` (picker): `1 Game` / `2 Games`. Eight rows plus
  the pill, every one single-struck. "Aspect-fit" survives only as the genuine middle option of the
  two SCALE rows.
- **D2** — `p17-final-picker/bottom_00087.png`: a tap on the **left** half selects `1 Game` **and**
  re-lays the whole plate out to the single-game design; `bottom_00104.png`: the **right**-half tap
  — the exact tap the sweep photographed producing "1 Game" — selects `2 Games`. `top_00144.png`:
  the slot-A card tap put a green **A** badge on GOLF and flipped the header to `B · BOTTOM`;
  `bottom_00200.png`: both slots filled, START's ink crisp dark-on-gold (it was a pale
  `theme_ink_disabled` olive at `bottom_00012.png` while nothing was picked); `top_00240.png`: the
  session that started is the **picked** pair.
- **Drag-scroll** — `top_00121.png` (list at ALPHA TEST, thumb at the top) → `top_00144.png` (list
  at GOLF, thumb moved down, focus ring followed). The drag started inside START—LINKED and did not
  launch anything.
- **D6** — `p17-final-tabs/top_00120.png` 4×: eight outlined **rounded pills**, labels inside, no
  nubs, nothing overflowing. `p17-final-picker/top_00240.png` 5×: a solid green **disc** before the
  game name and a rounded FOCUS chip. `bottom_00215.png` 3×: five clean rounded swatches with a
  **rounded** white ring on the active one.
- **D5** — `p17-final-pad/bottom_00090.png` 4×: four separate rounded keys, one continuous gold
  outline each, **zero** black corner blocks. Residue recorded: a 1–2 px pinch where two adjacent
  keys' rounded corners meet.
- **D3 / D4** — same frame: the top band is exactly `L | TOUCH · GAMEPAD | R` over bare video (no
  name, no fps, no bar — `top_00090.png` shows the full HUD still on the top screen), START is a
  complete four-sided rounded key inside the screen with no hint through it, and the `≡ menu` chip
  is complete. `bottom_00125.png`: the tap at (290,229) **opened the pause menu** — the chip is a
  control now.
- **D7** — `p17-final-picker/bottom_00012.png` 3×: `tap a game above ↑ · ⚡ linked = trade /
  battle-ready` end to end, arrow and bolt intact, chip on its own row below.
- **D8 / D9** — `p17-final-tabs/bottom_00136.png`: `Swap screens` + `Frameskip` as two complete rows
  with the hint clean below; `bottom_00240.png`: the `GAMEPAD · EDGES` caption + a full-width
  `Round / Soft / Sharp` clear of the hint.
- **D11** — `top_00240.png` 5×: a rounded blue frame with readable light-blue `3D` beside crisp gold
  `35fps` and near-white `02:01`.
- **D15 / D16 / D18 / D19** — `bottom_00160.png` (`VOLUME · A` + right-aligned `100`, no stray
  glyph), `bottom_00215.png` (the two-line Off explainer), the right-edge column (dark track,
  lighter thumb, **at the top** while the tab is at the top), `bottom_00195.png` (`Co-op presence`
  in the baked rows' own face and size).
- **D10 / D12** — `p17-final-daylight/bottom_00195.png`: `Save state` / `Load state` / `Load .sav`
  crisp **white**, `Wireless lobby…` dark-on-gold, bronze accent throughout — the theme is visibly
  chosen and every label reads. `top_00075.png`: the coherence split is still there (beige letterbox
  under a dark scrim) and every element on that bar is legible. Partial, exactly as claimed.
- **D13 / D21** — `p17-final-empty/top_00040.png` + `bottom_00040.png`: `No games found` + the path,
  `Rescan` (focused, rounded white ring) + `Start without a game`, no dead cards, no misleading
  baked hint. `bottom_00008.png`: the splash pill reads `TAP TO START`.
- **D17** — `p17-final-smart2/bottom_00250.png`: a gold `TOUCH · SMART POINTER` chip, the `≡ menu`
  chip with three crisp bars, **zero** cyan text.

### The deliverable

`docs/phase17-uifix/BEFORE-AFTER.md` — every one of the sweep's 22 defects, its original evidence
image beside a final-binary capture, with a one-line verdict; 51 images staged in
`evidence/ba17/` so the document is self-contained; seven captioned, self-contained HTML contact
sheets in `evidence/` built with `tools/emutest/sheet.py`. `docs/HANDOFF.md` gained the phase-17
paragraph in **Current status**, a seven-item human-eyeball checklist in **WHAT'S LEFT TO TEST**
(touch accuracy with a thumb on the 16 px settings chip, drag feel and the 8 px threshold, panel
gamma on the 3D badge and the scrollbar, the 351-rect/frame gamepad budget, the six themes, the two
screens no harness can reach, and the one unattributed stray pill), and a dated session entry.

### Deviations

1. **The proof pass used dummy ROMs, not the user's real Pokémon ROMs**, for every run except none.
   Consequence, stated: the video rect is white/grey because the dummy cores execute nothing, so the
   HUD-over-video composition is proven against a *flat* frame. The one thing that actually depends
   on real game pixels — **D22**'s translucent scrim at non-1:1 scale — is the phase's declared
   deferral and is shown from the re-sweep's real-ROM capture instead.
2. **D14 and D22 have no new final-binary capture.** D14 is re-diagnosed as not-a-defect (the white
   is the ROM's own boot output) and D22 is unchanged by construction — no code in this phase
   touches either path, so re-shooting them would produce the same frames the re-sweep already
   published. Both are shown from the earlier captures, labelled as such.
3. **`wireless.c`'s two focus rings and the presence chrome are still un-photographed** — reaching
   them needs a live session plus a radio (lobby) or two real Gen-3 saves on the same map
   (presence). Carried forward from F6 deviation 7 into the hardware checklist rather than quietly
   dropped.
