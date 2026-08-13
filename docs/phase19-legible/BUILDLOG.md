# BUILDLOG — phase 19 / SPEC-legible ("make the text bigger")

One entry per slice, banked immediately after it. Numbers here are OBSERVED, pasted from the
tool that produced them — never "should be".

---

## G1 — THE LADDER AND THE BAKE (2026-08-13)

**Scope shipped:** SPEC-legible L1.5.2 (the new ladder) + L2.3 (seven roles onto five faces) +
the bake that guards it. No layout re-fit (G2), except the one coordinated constant set §L3.2.2
forced — see DEVIATIONS.

### What changed

| file | change |
|---|---|
| `source/typography.h` | ladder moved up 1–2 rungs; `TXT_SEG` aliases `TXT_BODY`, `TXT_CHIP` aliases `TXT_SECTION`; new `TXT_DISTINCT_FACES 5`; the em-vs-lineFeed cause written into the header |
| `tools/build_assets.sh` | five `bake_checked` lines (19/15/15/12/12), and the stale-face `rm` now clears **both** the phase-17 four and the phase-18 seven before `sharpen.py` globs `data/fnt_*.bin` |
| `source/assets.c` | `ASSET_FONTS` retargeted; **alias-aware load** — each distinct `_bin` symbol is `C2D_FontLoadFromMem`'d once and both role slots point at the one handle (a naive X-macro would `linearAlloc` ~527 KB twice) |
| `source/uihit.h` | `UIHIT_MENU_VIEW_H` 228 → 226, `SET_LINK_NOTE_Y` 215 → 211 (L3.2.2) |
| `source/main.c` | `PT_LINK` presence toggle y 196 → 192 (L3.2.2) |
| `test/host/test_typography.c` | T5 extended to the phase-18 vintage; T7 alias-aware; T10 +30 boxes and a new T10b pending-overflow row; **new T15** (alias contract) and **new T17** (cap height from the shipped bytes) |
| `test/host/test_uihit.c` | the scroll/thumb goldens now derive from `UIHIT_MENU_VIEW_H` instead of hard-coding 228 |
| `tools/fontlab/measure.py` | retargeted to phase-18 → phase-19 (the phase-17 pack can no longer be baked); before-scale and before-origin now chosen from the pack's own bytes, so a lawful BEFORE is not charged for a half-pixel origin it cannot produce |

### PROOF

**1. The bake produced exactly the declared rungs** (`tools/build_assets.sh`, verbatim):

```
  fnt_sg_bold_19   -s 11  lineFeed=19  cell=20x22   (drawn at 19 px, scale 1.000)
  fnt_sg_bold_15   -s 9   lineFeed=15  cell=16x18   (drawn at 15 px, scale 1.000)
  fnt_sg_med_15    -s 9   lineFeed=15  cell=16x18   (drawn at 15 px, scale 1.000)
  fnt_jbm_med_12   -s 7   lineFeed=12  cell=9x15    (drawn at 12 px, scale 1.000)
  fnt_jbm_bold_12  -s 7   lineFeed=12  cell=9x15    (drawn at 12 px, scale 1.000)
```

`bake_checked` asserts each lineFeed, so a future `mkbcfnt` bump fails the bake, not the app.
Stem-snap after the bake (all five, `sharpen.py`): opaque ink 54.7 / 43.2 / 41.3 / 26.6 / 34.7 %,
solid 68.1 / 53.8 / 57.7 / 55.0 / 56.3 % — every face clears T13's 30 % floor.

**2. R1 still holds ON THE SHIPPED BINARY** — Azahar 2125.1.2, `3DGBA.3dsx` booted with a CTM
movie, read over the GDB stub (`gdbio read g_txtTexelScale 28` / `g_txtLineFeed 28`):

```
g_txtTexelScale = [1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0]
g_txtLineFeed   = [19, 15, 15, 15, 12, 12, 12]
```

That is 1.0 for all seven roles (including both aliases) and the new ladder exactly.

**3. Crispness did not regress — the multiple-of-17 check on the new faces**
(`tools/fontlab/measure.py --before <rebaked phase-18 pack> --after data`):

```
role         string                   | BEFORE (phase 18)         | AFTER (phase 19)
                                      |  scale   off  lvls ramp peak |  scale   off  lvls ramp peak
TXT_TITLE    Settings                 |  1.000     0    11    5  255 |  1.000     0    11    4  255
TXT_BUTTON   Resume this pairing      |  1.000     0    11    5  255 |  1.000     0    11    4  255
TXT_BODY     pick a game (d-pad + A)  |  1.000     0    10    6  255 |  1.000     0    11    4  255
TXT_SEG      Aspect-fit               |  1.000     0     9    5  255 |  1.000     0    11    4  255
TXT_SECTION  SCALE · TOP              |  1.000     0     9    3  255 |  1.000     0     9    2  255
TXT_CHIP     HOST BPEE                |  1.000     0     6    2  255 |  1.000     0     8    4  255
TXT_VALUE    12:34                    |  1.000     0    11    2  255 |  1.000     0    11    4  255
  ALL GATES PASS: every AFTER run is bit-exact (0 off-grid pixels), NEAREST and LINEAR agree.
```

Sheet banked at `evidence/crispness-p18-vs-p19.html`.

**4. It IS bigger — measured, not eyeballed.** T17 reads the ink rows of `H` out of the shipped
`.bin`s:

```
     TXT_TITLE    sg_bold_19   cap 11 px (was 10)  ink top  6 of cell 22   24.0 arcmin@30cm
     TXT_BUTTON   sg_bold_15   cap  9 px (was  7)  ink top  5 of cell 18   19.6 arcmin@30cm
     TXT_BODY     sg_med_15    cap  9 px (was  7)  ink top  5 of cell 18   19.6 arcmin@30cm
     TXT_SEG      sg_med_15    cap  9 px (was  6)  ink top  5 of cell 18   19.6 arcmin@30cm
     TXT_SECTION  jbm_med_12   cap  7 px (was  5)  ink top  4 of cell 15   15.3 arcmin@30cm
     TXT_CHIP     jbm_med_12   cap  7 px (was  4)  ink top  4 of cell 15   15.3 arcmin@30cm
     TXT_VALUE    jbm_bold_12  cap  7 px (was  7)  ink top  4 of cell 15   15.3 arcmin@30cm
```

Every cap and every ink-top matches SPEC-legible L1.5.2 / L3.2 to the pixel.

And in the **real framebuffer**, not just the font file: the ROM-picker empty state captured
from the running app and reconstructed to native 320×240 (`see shot` + `native.py --screen
bottom`, nearest, byte-exact), then measured — the `R` of `Rescan` occupies rows **190…198 =
cap 9 device px** (phase 18: 7), and the mono empty-state line's ink is **9 rows**, which is the
`4…12` extent L3.2 predicts for the new SECTION cell. Captures: pause SESSION / pause TOUCH /
paused-summary top / picker empty, all visibly larger than the banked `before-*.png` set at the
same crop.

**5. Size.** Fonts 7 faces → 5:

| | before | after | Δ |
|---|---|---|---|
| `data/` (all 71 files) | 3,870,842 B | **2,812,726 B** | −1,058,116 B (−1.01 MB) |
| `3DGBA.3dsx` | 5,417,868 B | **4,359,660 B** | −1,058,208 B (−1.01 MB) |
| `3DGBA.cia` | 2,128,832 B | **2,002,880 B** | **−125,952 B (−123 KB)** |

The `.cia` prediction in L2.2.1 was *+13 KB* (gzip proxy); the real packer gives **−123 KB**, so
the phase is cheaper than specified on every axis. Runtime linear heap drops by the same
~1.01 MB as `data/` (each face is `linearAlloc`'d in full at load) — the alias de-duplication in
`assets.c` is what makes that true, and it is **proven on the running app**, not argued:
`s_fonts[7]` read over GDB at `0x00523324` on a live boot —

```
TITLE 0x08009e50  BUTTON 0x0a00c130  BODY 0x0a00dd68  SEG 0x0a00dd68
SECTION 0x0a00ddb0  CHIP 0x0a00ddb0  VALUE 0x0a00ddf8      -> 5 distinct C2D_Font handles
```

`SEG` is byte-identical to `BODY` and `CHIP` to `SECTION`: seven roles, five allocations.

**6. Suites — all thirteen app host suites + the harness host tests, green:**

```
control 6897 · diag 376 · fieldpath 1808 · netlink 66 · presence 49773 · profiles 505 ·
theme 83444 · tilt 1723 · trace 58 · typography 1054 · uigeom 18332 · uihit 1834 · celiolink 1259
harness host tests: Ran 149 tests — OK
```

None shrank: typography 1016 → **1054** (+38: T15, T17, 30 new T10 boxes, T10b), uihit 1718 →
**1834** (the hint band is two rows taller, so T9's exhaustive chrome-band sweep covers more
rows). Every other count is identical to the phase-18 baseline.

Build: `make -j8` → `3DGBA.3dsx`, `make cia` → `3DGBA.cia`, **no new warnings** (the surviving
ones are the pre-existing `main.c` unused-symbol / misleading-indentation set; `assets.c`,
`typography.h` and `uihit.h` produce none).

### What is now VISIBLY BROKEN, on purpose (G2's list)

Confirmed in the captures, exactly as SPEC-legible predicted:

* **`Preview Gamepad` overflows its button by +2 px** — measured 103 px in a 101 px box, which
  is L3.1.3's number to the pixel; visible in the capture as ink touching both rounded ends.
  T10b prints it every run and already grades it against the 104 px box G2 will give it.
* **The pause status-hint line is clipped by the bottom of the screen** (`statusTxt` still at
  y=231 with a 15 px cell). L3.2.2 moves it to 226; G2 owns the draw sites.
* The HUD bar, presence card, `.sav` picker, touch explainer and the ink-centred labels
  (L3.2.1 / .3 / .4 / .5 / .7) are untouched and will be wrong until G2.

### DEVIATIONS

1. **No user-facing TEXT SIZE setting was built.** The slice said "if L2 chose one" — L2.2
   chose *not* to (option (b) rejected: a second ladder is +2.52 MB image, +2.52 MB linear heap,
   a doubled T10/T16 gate and a mid-session font-reload path, bought to offer the user *less*
   legible text after they asked for more). Recorded in SPEC Q2 as a costed later phase.
2. **Three layout constants landed early** (`UIHIT_MENU_VIEW_H` 228→226, `SET_LINK_NOTE_Y`
   215→211, `PT_LINK` presence toggle y 196→192). They are G2's §L3.2.2, taken now because
   T11 grades the *shipped* constants against the *shipped* font and goes red the moment the
   SECTION cell grows 13→15 — the binding "suites stay green" rule wins over the slice boundary.
   They are exactly the spec's table, so G2 inherits them unchanged; the rest of §L3.2 (the
   hint-line draw y's, the HUD bar, the explainer, the presence card, the picker) is untouched.
3. **T14 / T16 not written.** They grade geometry G2 has not created yet (the wrapped explainer
   rect, the vertical-fit table). T10 was extended instead, by 30 boxes that G2 does not move.
4. **`tools/fontlab/measure.py` was retargeted rather than left alone**: its BEFORE pack was the
   phase-17 bake, which `build_assets.sh` can no longer produce, so the tool could not run at
   all. It now takes any previous pack and models that pack's own draw rule (lawful pack ⇒
   scale px/lineFeed at an integer origin; phase-17 pack ⇒ the ceil'd `s_native` at the worse of
   two origins). Retargeting it *without* that second half would have manufactured a fake
   phase-18 regression (185 off-grid pixels charged to a pack that never drew on a half pixel).

### Harness hygiene

Two boots this slice (`runs/20260813-063907` movie-driven, `runs/20260813-064412` picker), both
`gdbio detach` + `azctl stop`; `qt-config.ini` restored byte-identically both times, no Azahar
left running, `sdmc/dual-gba/` never touched (the picker ran ROM-less).

---

## G2 — THE RE-FIT (2026-08-13)

SPEC-legible **L3 + L4**: every box, row offset and wrapped paragraph that the bigger ladder
broke, plus the horizontal/vertical gates that keep them honest. G1 grew the type; G2 makes the
app fit around it. Numbers below are OBSERVED — pasted from the tool or measured out of a
reconstructed 320×240/400×240 framebuffer captured from the running app.

### What changed

| file | change |
|---|---|
| `source/typography.h` | `TxtFace` gains `cellH` / `inkTop` / `inkH` (measured over `AHgpy1:9`), plus `typo_cell_h` / `typo_ink_top` / `typo_ink_h` / `typo_ink_bottom` and **`typo_center_y(role, boxY, boxH)`** — the one vertical-centring rule (L3.2.7) |
| `source/assets.c` | `assets_button` + `assets_seg` centre the **ink** box, not the line box |
| `source/ui.c` | `UI_CHIP_H` 13 → `UIHIT_CHIP_H` 16; `chip_label` takes the box top and centres by ink |
| `source/touch.c` | `touch_chip` box 14→17; menu chip 220/18 → **218/20**; `pad_label` + both chips ink-centred |
| `source/uihit.h` | new pure-C homes for the geometry a PC test has to read: `UIHIT_HUD_*` (bar 20, name y2, readout y4, chip y2), `UIHIT_CHIP_H/TOUCH_CHIP_H/TOUCH_CHIP_Y/MCHIP_*`, `UIHIT_PILL_*`; `SET_LINK_NOTE_Y` 211→**208**; `SET_LINK_DISABLED_NOTE` shortened (see DEFECT 3) |
| `source/main.c` | HUD bar 14→20 both screens; `PT_TOUCH` previews 101→**106** (gutter 4); `PT_LINK` presence toggle 192→**190**; `PT_ENHANCE` tilt seg 198→**200**; `OV_SECTION` −14→**−17**, `OV_ROW` ink-centred; explainer → `TXT_BODY`, `TEXPL_W/Y/LEAD/LINES` 216/62/15/3 + the one copy rewrite; presence card 80→**108** with L3.2.4's row table and a WRAPPED union note; pill row pad 12→10 / gap 5→4 / h 15→17; status + touch-off hints 231/229→**226**; `PK_STEP` value y−16→**y−18**; `run_settings` hints 210/224→**206/222**, O3DS warning 62→**66**; prose → `TXT_BODY` (L3.3.3) at 6 sites |
| `source/rompicker.c` | `.sav` picker `ROW_H` 16→**20**, `VIS_ROWS` 13→**9**; empty-state prose → `TXT_BODY` |
| `source/wireless.c` | `fpLine` 209→**206**, `status` 226→**224**, scan-card rows +4/+20→**+2/+21** |
| `tools/build_assets.sh` | the bake now PRINTS each face's ink box next to its lineFeed (L3.2.7's "guarded like lineFeed"; T18 is the assertion) |
| `test/host/test_typography.c` | T10 28→**123 boxes** + a hard fail at >98 % of box; T10b retired; T11 re-pointed at the note's new role/rows; **new T14** (the wrapped paragraph), **new T16** (vertical fit, 40 rows + 5 derived checks), **new T18** (the ink table vs the bytes + the centring rule). Links `source/uihit.c` so T14 drives the SHIPPED `uihit_wrap` |

### THREE DEFECTS THE SPEC'S OWN CORPUS DID NOT CONTAIN

L5.2.1 asked for ≥93 boxes; the extension found real breakage the 93-string extraction missed.

1. **`PRES_CARD_UNION_NOTE` overflows by 106 px.** "trade & battle use the game's own Union Room
   - not from here" is **300 px at TXT_CHIP cap 7** in a **194 px** card row (it fit at cap 4).
   The card cannot grow to 320 (it floats over the game image) and shortening it to one row costs
   either "Union Room" or "not from here" — the two halves `test_presence` pins by name. Remedy:
   **it wraps**, `_1` (165 px) + `_2` (130 px), card 104 → **108** so the second row's ink ends at
   104. `PRES_CARD_UNION_NOTE` stays defined as the concatenation, so the contract test is untouched.
2. **`SET_LINK_NOTE_Y` 211 is unreachable once L3.3.3 applies.** L3.2.2 solved `211 + 15 ≤ 226` for
   the note on the mono rung; L3.3.3 moves that same note to `TXT_BODY`, whose line box is **18**.
   `211 + 18 = 229 > 226`, and 208 (the only y that fits) was below the presence toggle's bottom
   edge of 210. Both legs moved: toggle **192→190** (bottom 208), note **211→208**.
   `208 + 18 == 226` is the same equality, re-solved for the real face.
3. **The LINK note runs under the "Done" chip.** *Found only in a capture.* At `TXT_BODY` the note
   was 194 px from x=93, i.e. it reached x=287, and `run_settings` draws `DONE = {244,224,72,14}`
   as fixed chrome AFTERWARDS — so the pill ate the descender of the final "g" in "running game".
   That is the phase-18 fix pass's defect ("the chrome band takes its baseline") reappearing on the
   other axis. The note's real box is `DONE.x − 93 = 151 px`, not the 219 px content column, so the
   copy is now **"Needs a running game" (128/151)** and T10 grades it against 151.

Two more were found by the new gates rather than by eye: `run_settings`' subtitle and the O3DS
warning were 3 rows apart at `TXT_BODY` (warning 62→**66**, leading now the face's own 18 px), and
the wireless scan card's match chip at `by+22` put its last ink row one past the 34 px card
(→ **by+21**; L4.5's own number was one too far).

### PROOF — R1 STILL HOLDS ON THE SHIPPED BINARY

`3DGBA.3dsx` booted in Azahar 2125.1.2, read over the GDB stub (`runs/20260813-071644`):

```
gdbio read g_txtTexelScale 28 -> 00 00 80 3f  x7   = 1.0f, all seven roles
gdbio read g_txtLineFeed  28 -> 13 0f 0f 0f 0c 0c 0c = 19,15,15,15,12,12,12
```

Every role still draws at texel scale **exactly 1.0**; `assets_text` still floors the origin, and
`typo_center_y` returns an integer row by construction (T18 asserts `y == (float)(int)y`).
The bake's own report, next to the ladder it feeds:

```
  jbm_bold_12   cell 15  ink rows  4..12 (inkTop 4 inkH 9)   cap 7
  jbm_med_12    cell 15  ink rows  4..12 (inkTop 4 inkH 9)   cap 7
  sg_bold_15    cell 18  ink rows  5..16 (inkTop 5 inkH 12)  cap 9
  sg_bold_19    cell 22  ink rows  6..19 (inkTop 6 inkH 14)  cap 11
  sg_med_15     cell 18  ink rows  5..16 (inkTop 5 inkH 12)  cap 9
```

— identical to `typography.h`'s declared table, which is what T18 asserts.

### PROOF — CAPTURES, AND WHAT I READ IN THEM

Six boots (`runs/20260813-071644 / -072435 / -072923 / -073158 / -073319 / -073452` plus five
theme boots), `see rec`/`see shot` → `native.py` (nearest, byte-exact 320×240 / 400×240), then
row-profiled with PIL. Predicted values are from §L3.2; **observed** are ink rows in the real
framebuffer.

| screen | what I read | verdict |
|---|---|---|
| **in-game HUD, top** (`g2-hud-top.png`) | bar rows 0–19, focus underline **20–21**; "gameA" ink **8..17**; ●FOCUS chip box **2..17** (= 16 px at y2); clock ink **7..14**; right cluster starts x**192**, left ends x**178** | bar = 20 px exactly, 14 px of clear air between clusters |
| **in-game HUD, bottom** (`g2-hud-bot.png`) | name ink **8..17**; touch-off hint ink **231..238** | the hint is ON the screen (at 229 with a 15 px cell it ran to 241) |
| **paused summary** (`g2-paused-summary-top.png`) | pill row y **129..145** (= PILL_Y 129 + PILL_H 17); 8 lit pills span x**52..344**, margins 52/55 | worst case (9 pills, longest variants) is 356/400 per T16 |
| **pause SESSION** (`g2-pause-session.png`) | Resume/Change games/Quit at cap 9, ink-centred in 40/40/43 px buttons | labels sit on the button's optical centre, not 4 px low |
| **pause DISPLAY / AUDIO** | seg cells at cap 9; "100" values at y−18 on the caption baseline | no cell overflows (worst "Aspect-fit" 61/69) |
| **pause LINK** (`g2-pause-link.png`) | "Load .sav" ends row **186**; "Co-op presence" ink **193..204** (= typo_center_y(BODY,190,18)=188 → 193..204); status hint ink **229..237** | 4 clear rows between the button and the label |
| **pause TOUCH** (`g2-pause-touch.png`) | explainer ink **67..78 / 82..93 / 100..105**; buttons' top edge row **109**; fills run x**96..196** and x**206..306** (r8 inset of the 93/203 + 106 boxes) | 3 lines, ink clear of 109, right edge 309 vs the scrollbar's 312 |
| **gamepad overlay** (`g2-gamepad-overlay.png`) | menu-chip box rows **218..237** (on-screen, corners intact), label ink **225..230**; START key box **214..235**, label ink **219..227** | every key glyph is cap 9 and inside its key |
| **Smart chip** (`g2-smart-chip.png`) | "TOUCH · SMART POINTER" chip at y4 h17, x100..219 — between the game name (ends 59) and the clock cluster | no collision |
| **ROM picker rows** (`g2-picker-rows-top.png`) | row 0 card **40..61**, name ink **49..57**, "BPEE" chip ink **49..57**; row 1 name ink **73..81** | 23.5 px pitch still holds an 18 px cell |
| **`.sav` picker** (`g2-sav-picker-top.png`) | title ink **13..22**; rows at y30/y50 (pitch 20), ink **35..46** and **55..66**; help ink **217..226** | ROW_H 16 would have overlapped the line boxes; 20 gives 8 clear rows |
| **standalone settings** (`g2-settings-top.png`) | subtitle ink **53..64**; hints ink **209..217** and **226..232** | both hints on-screen; subtitle at cap 9, was cap 5 |
| **settings LINK** (`g2-settings-touch/-enhance`, `g2-zoom-link-note-vs-done.png`) | note ink **213..224**, clears the 226 band; DONE pill at 224 no longer overlaps the copy | DEFECT 3, fixed and re-captured |
| **settings ENHANCE** (`g2-settings-enhance.png`) | baked "Vivid mode" ink **175..183**, "DIORAMA · TILT" ink **187..193**, tilt seg **200..225** | 3 clear rows above, 8 below (it was **1** before the y198→200 move — see DEFECT 4) |
| **wireless lobby** (`g2-wireless-top/-bot.png`) | seat cards, RTT/LOSS tiles, transport line, three action buttons | nothing clipped or overlapping |
| **empty picker** (`g2-picker-empty-*.png`) | "put .gba files in sdmc:/3DGBA/" now TXT_BODY cap 9 (was mono cap 5) | the single biggest readability jump in the app |

**DEFECT 4 — found in a capture, not in a table.** L3.2.6's −17 caption offset is right by the ink
rule (4 clear rows above its own control), but on ENHANCE those 17 px land under the plate's baked
"Vivid mode", whose ink ends on row 183. At the manifest y=198 the caption's ink ran **185..193** —
ONE blank row under a bright bold label, so the dim mono caption read as a sub-line of "Vivid mode"
instead of as the tilt row's heading. The row drops two pixels (y198→**200**); `contentH` is 226
either way so `maxScroll` stays 0 and nothing else on the tab moves. Re-measured after the fix:
**187..193, three rows clear**. T16 now grades the caption against the plate's own baked ink.

### PROOF — ALL SIX THEMES

The theme has no shipped UI (`ACT_THEME` survives only in the dead `menu_layout`), so each was set
by patching `Settings.theme` at byte 64 of the emulated `sdmc:/3DGBA/settings.bin`, booting, and
capturing the AUDIO tab — L5.4.3's named dense screen. The file was backed up first and restored
byte-identically afterwards (`sha1 50ac10b51d5d380b96596ca9adb879cdfab9527f` before and after).

```
Indigo(0)   g2-pause-audio.png            Duo(3)     g2-theme3-duo-audio.png
OLED(1)     g2-theme1-oled-audio.png      Retro(4)   g2-theme4-retro-audio.png
Daylight(2) g2-theme2-daylight-audio.png  Custom(5)  g2-theme5-custom-audio.png
```

Bright-pixel counts over the five non-default captures: **3232 / 3195 / 3179 / 3195 / 3195** —
within 1.7 %, i.e. identical geometry with different ink, which is what "only ink colours differ"
predicts. No theme puts ink on a baked label the bigger glyph overlaps. (Only the Indigo art pack
is baked — the pre-existing REPORT D10/D12 limitation, unchanged by this phase.)

### SUITES — all thirteen app host suites + the harness host tests, green

```
control 6897 · diag 376 · fieldpath 1808 · netlink 66 · presence 61371 · profiles 546 ·
theme 83444 · tilt 1723 · trace 58 · typography 1408 · uigeom 18332 · uihit 1834 · celiolink 1259
harness host tests: Ran 149 tests — OK
```

None shrank. **typography 1054 → 1408 (+354)**: T10 28→123 boxes with a >98 % ceiling, T14 (3
copies × 5 assertions), T16 (40 fixed rows + the pill row, the HUD clusters, the pad key, the menu
chip and the .sav pitch), T18 (7 rungs × 5 metrics + 12 centring boxes). `uihit` 1834 unchanged.
`presence` 49773→61371 and `profiles` 505→546 grew from the **concurrent phase-20 peersprite
slice** working in the same tree, not from this one. Build: `make -j8` clean, **no new warnings**
(the survivors are the pre-existing `main.c`/`touch.c`/`ui.c` unused-symbol and
misleading-indentation set — verified by compiling each touched file).

T16's own headline numbers, from the shipped font:

```
HUD 400 px bar: left cluster ends 178, right cluster starts 192 (14 px clear)
HUD 320 px bar: left cluster ends 215, right cluster starts 249 (34 px clear)
pause-top pills: 9 worst-case pills = 356 px on 400 (22 px margins)
T14 copy 0/1/2: 3 lines each, last ink row 108 (Preview buttons at 109)
T10: 123 boxes measured, one >90 % note ("Preview Gamepad" 103/106)
```

### DEVIATIONS from SPEC-legible

1. **Preview buttons are 106 px wide with a 4 px gutter, not L3.1.3's 104 with 7.** L5.2.2's own
   >98 %-of-box ceiling rejects 103-in-104 (99.0 %). 93+106+4+106 → right edge 309; the scrollbar
   column is repainted from x=312, so the content still ends clear.
2. **`SET_LINK_NOTE_Y` is 208, not L3.2.2's 211, and the presence toggle is y190, not 192** — the
   arithmetic L3.2.2 solved for a 15 px cell, re-solved for the 18 px cell L3.3.3 gives the note.
3. **`SET_LINK_DISABLED_NOTE` copy changed** ("Link actions need a running game" → "Needs a running
   game"). Not in the spec; forced by DEFECT 3, which only a capture shows.
4. **`PRES_CARD_UNION_NOTE` wraps and the card is 108, not L3.2.4's 104.** DEFECT 1.
5. **`PT_ENHANCE`'s tilt seg moved 198→200**, not in the spec. DEFECT 4.
6. **`run_settings`' O3DS warning moved 62→66**; L3.2.9 kept 62 for the mono rung.
7. **The wireless scan chip is `by+21`, not L4.5's `by+22`** — one row over the card.
8. **`touch_chip` / `MCHIP` labels are placed by `typo_center_y`, not by L3.2.8's literal offsets**
   (`y+2`, `MCHIP_Y+3`). The helper puts the ink box dead centre; the literals are 1–2 px low.
   L3.2.7 names both call sites, so the helper wins where the two sections disagree.
9. **`MCHIP_Y` 220→218 as well as `MCHIP_H` 18→20.** L3.2.8 grew only the height, which would have
   put the box's last row on 240 and clipped its own rounded bottom corners.
10. **The ink metrics are guarded by the host suite (T18), not by `bake_checked`.** The bake now
    PRINTS the ink box beside the lineFeed so drift is visible at bake time, but the failing
    assertion lives in T18 — where it can also re-grade every dependent row (T16) in the same run.
11. **The `.sav` picker was captured with 2 files, so the 9-row limit is not exercised in pixels**;
    the row pitch, the last-row/help-line clearance and the title are (T16 grades the 9-row case).
12. **No `run compare` diffs against the before-set (L5.4.4).** The before-set is a different
    *layout*, not a different rendering of the same one — a pixel diff of two different layouts has
    no threshold that means anything. The claim is carried by measured ink rows instead.

The CTM recipes that produced every capture are banked at `docs/phase19-legible/movies/*.json`
(`ctm make <script>.json <out>.ctm`), so any screen in the table above is one boot away from being
re-shot: `m_g2_pause` (HUD → all six pause tabs → gamepad), `m_g2_settings` (settings screen, five
tabs), `m_g2_wl` (Smart chip → wireless lobby), `m_g2_rom`/`m_g2_rom2` (resume prompt / picker
rows, Tier B), `m_g2_sav` (the `.sav` picker), `m_g2_theme` (the AUDIO tab, one per theme).

### Harness hygiene

Eleven boots this slice; every one `gdbio detach` + `azctl stop`, `qt-config.ini` restored
byte-identically each time ("CLEAN"), never two Azahars at once (verified with `pgrep` after the
one run that timed out mid-loop). One Tier-B boot used `--fresh-sd-fixtures`; `azctl
clean-fixtures` afterwards reported *"originals re-hashed, all untouched … sdmc:/3DGBA is ROM-less
again; restored the user's pre-existing recent.bin"*. `sdmc/dual-gba/` never written. The emulated
`settings.bin` was backed up and restored byte-identically around the six-theme sweep.

### Collaboration hazard, recorded

A **phase-20 "peersprite" slice was editing the same working tree concurrently** (`source/
peersprite.c`, `gamestate.c`, `presence_*`, `test_presence.c`, `test_profiles.c`, and one new
`ui_text` dev readout in `main.c` with its matching T12 budget bump 13→14). Two consequences worth
knowing: (a) `test_profiles` needs `source/peersprite.c` on its link line now, which its own header
comment does not yet say; (b) I ran `git stash` once to check whether a link failure pre-dated my
work — it stashed **their** uncommitted work too. `git stash pop` restored it intact and I did not
do it again. Do not use `git stash` while another slice shares the tree.

### NOT done in G2

`L5.3.4`'s published anchor sheet (the GBA cap-9 comparison) and `L5.4.4`'s zoom-crop-per-flagged-
string gallery. T10 flags exactly one string above 90 % now ("Preview Gamepad", 103/106) and it has
its own zoom in `evidence/after-preview-overflow-zoom4x.png` from G1. **Hardware sign-off is still
open** (CLAUDE.md #6): the question to ask the user is L5.4.6's — *"can you read the chips and the
hint line without leaning in"* — because those two rungs sit knowingly at 15.3′.

---

## VERIFY PASS (independent) — 2026-08-13

An independent verification pass ran the thirteen app suites + celiolink (all green), re-derived
every ladder number from the shipped `data/fnt_*.bin` (texel scale **exactly 1.0** on all seven
roles), **re-baked the phase-18 ladder** from the same TTFs to put a number on "bigger"
(cap +29 %/+40 %/+50 %/+75 % on BUTTON/SECTION/SEG/CHIP; TXT_VALUE unchanged at cap 7), read
`g_txtLineFeed` = `19 15 15 15 12 12 12` and `g_txtTexelScale` = `0x3f800000` ×7 **live over GDB
out of the running app**, and took its own five-boot capture sweep.

Findings, full write-up in **`docs/phase19-legible/VERIFY.md`**, evidence in
`tools/emutest/runs/p19-verify/`:

| # | Sev | What |
|---|---|---|
| C1 | **blocker** | Smart mode: the bottom HUD game name is truncated and the `●FOCUS` chip is fully covered by the `TOUCH · SMART POINTER` chip (`main.c:4968` + `touch.c:861` share the 20 px band; the bar is suppressed for `TOUCH_PAD` only) |
| V1/C2 | major | `main.c:5434` — the settings `Done` label is placed by `DONE.y + 1.0f`, not `typo_center_y`; its ink lands on rows 230..238 in a pill of 224..237 |
| V2/C3 | minor | `rompicker.c:328` — the one surviving phase-18 line-box centring; `settings · ZR` sits 2 px low, and the string is ungraded by the suite |
| C4 | minor | `DIORAMA · TILT` still reads as a sub-line of "Vivid mode" (equidistant, 4 rows either side) — the y198→200 fix did not change the reading |
| C5 | minor | the pause pill row (277 px / 356 px worst case) now overhangs the `pause-top` plate's own dim band (x80..320); at p18 sizes the 8-pill row fit it exactly |
| O1 | note | `TXT_VALUE` 11→12 pt is not a size step: `jbm-bold` rasterises cap 7 at both |
| O2 | note | the two `ui_text(0.32f)` co-op diagnostic lines are gated on a **user** toggle, not a dev flag, and are now the smallest text in the app |

Not covered: the ROM-less empty picker, the `Touch = Off` hint, the co-op toast, and four of the
six themes — the emulator was shared with a concurrent phase-20 session for the whole window (its
ROM fixtures were staged, so every boot came up on the resume prompt). Hardware sign-off remains
open per CLAUDE.md #6.

---

## FIX PASS — the verify pass's findings, verified then fixed (2026-08-13)

Input: the eleven-item findings list from `docs/phase19-legible/VERIFY.md` (C1–C5, V1/V2, O1/O2,
plus two `presence_ui.h` stale-constant findings). **Every finding was re-derived from the shipped
bytes or the shipped art before it was touched** — two of them did not survive that (see REFUTED).
Nothing in `celiolink.c` / `netlink.c` / gbacore SIO / the HD-2D passes was edited: `git diff
--stat` on those paths is empty.

### Outcomes, one row per finding

| # | Sev | Verdict | What was done |
|---|---|---|---|
| **C1** | **blocker** | **CONFIRMED** | bottom HUD bar now gated `tmEff == TOUCH_OFF` (was `!= TOUCH_PAD`) |
| V1/C2 | major | CONFIRMED | `run_settings` "Done" placed by `typo_center_y`, not `DONE.y + 1.0f` |
| C6 (`PRES_PILL_TOP`) | major | CONFIRMED | derived from `UIHIT_HUD_BAR_H` — 18 → **24** |
| C7 (`PRES_PILL_H`) | minor | CONFIRMED | derived from `UIHIT_CHIP_H` — 15 → **18** |
| V2/C3 | minor | CONFIRMED (evidence partly wrong) | picker chip placed by `typo_center_y`; the claim "the string is ungraded" is FALSE — T10 already measured it horizontally, it was the VERTICAL grade that was missing |
| C4 | minor | CONFIRMED (numbers wrong) | new `OV_SECTION_TIGHT` (13 px) for the tilt caption |
| O2 | minor | CONFIRMED | the two `ui_text(0.32f)` co-op dev lines are behind `PRES_DIAG_HUD` (default 0) |
| O1 | note | CONFIRMED by re-bake | `typography.h`'s ladder now carries a before→after CAP column; TXT_VALUE marked "NOT a size step" with the rejection of pt 8 recorded |
| **C5** | minor | **REFUTED (framing)** | the "plate dims x 80..320" premise is false; the real number is banked in T16 instead |

### C1 — the blocker, measured then fixed

The claim, re-derived from `data/fnt_*.bin` rather than taken on trust (`tools/fontlab/bcfnt.py`):

```
name='Pokemon Emerald'   name ink x 15..117   ●FOCUS chip x 123..165
"TOUCH · SMART POINTER"  chip  x 100.5..219.5   (ui_chip_measure + 2, centred on 160)
vertical: bar rows 0..19 + focus rule 20..21   vs   chip rows 4..21
```

so the mode chip covered the last 17 px of the game name **and the whole FOCUS chip**. Confirmed by
eye in the verify pass's own capture (`runs/p19-verify/shots/DEFECT-hud-name-vs-touchchip-zoom.png`
shows "Pokemon Emeral" then the chip).

The fix is the design's own rule, applied where it always belonged. `manifests.json`'s **`smart-bot`
lists exactly what `pad-bot` lists** — video, the mode's widgets, and `btn: menu` at (269,216,45,18)
— and **no** name / fps / clock rects (only `ingame-dual-bot` has those). Screenshot `07-touch-smart.png`
shows the bottom band as the mode chip **alone** over bare video. `main.c` was suppressing the bar for
`TOUCH_PAD` only; it now suppresses it for **either** touch mode. The top screen keeps its full HUD in
every mode, so nothing the bar carries becomes unreachable.

**OBSERVED, on the rebuilt `.3dsx` in Azahar** (`runs/20260813-093901`, settings patched to
`touchMode=2`, `presence=1`; `shots/smart2.png` + `shots/smart2-top.png`):

* bottom screen — `TOUCH · SMART POINTER` alone in the top band, `≡ menu` bottom-right, peer
  nameplate `GUYA` over the game. **No bar, no truncated name, no covered chip.** 1:1 with
  screenshot 07.
* top screen — `● Pokemon Emerald  ●FOCUS   CO-OP  3D  15fps 02:00 [batt]`, complete and uncut.

### O2 — proven in the same capture

The top-screen shot above was taken with **`presence = 1`** and `hudMode & 1` on, i.e. the exact
state that used to print the 73-character `CO-OP me 3-12@14,9 | …` line at y=226 and the phase-20
`spr:` line at y=216 in the 3DS system font at scale 0.32. **Neither line is on the screen.**
`PRES_DIAG_HUD` (presence_ui.h, default 0) follows `TOUCH_DIAG_HUD`/`CTL_D5_ENABLE` verbatim;
`g_presDiag` (magic `PRS1`) is still filled every frame, so the structured channel a gdb read or a
photo-substitute actually uses is untouched. **A hardware calibration run rebuilds with
`-DPRES_DIAG_HUD=1`** — the phase-20 slice needs to know that. Promotion to a baked role is not
available: at `TXT_CHIP` (jbm_med_12, 5 px/char mono) the typical CO-OP line is 365 px and its worst
expansion ~116 chars = 580 px on a 400 px screen.

### V2/C3 and V1/C2 — measured in the framebuffer, before and after

Both were converted to `typo_center_y`. Captures native-ised to 320×240 (nearest) and row-profiled:

```
picker "settings · ZR"   pill rows 223..238
   before (VERIFY capture)  ink 229..237   6 above / 1 below, the "g" flush with the bottom edge
   after  (OBSERVED)        ink 227..235   4 above / 3 below      <- shots/FIXED-settings-chip-zoom.png

run_settings "Done"      pill rows 224..237
   before (VERIFY capture)  ink 230..238   6 above, last row OFF the pill
   after  (OBSERVED)        ink 225..233   inside, and the declared ink box (225..236) is centred
```

`grep -rn typo_role_px source/` now returns **no layout call site** — only `assets.c`'s three scale
computations. The line-box-centring idiom is gone from the tree.

Residual, stated rather than hidden: "Done" has no descender, so in a 14 px pill against a 12 px ink
box (2 px of total slack) the word reads 1 row high — 1 above / 4 below. That is the app-wide rule
doing the best the box allows, identical to every other button; growing the pill would push it over
the scrolled content column, so it stays.

### C4 — the finding was right, its numbers were not

VERIFY said "4 rows either side, equidistant". Row-profiling the real framebuffer says otherwise,
because `DIORAMA · TILT` is **caps-only**: its real ink is 7 rows, not the 9-row descender box
`typography.h` declares.

```
                        "Vivid mode" baked ink   caption ink   seg track   gaps (above/below)
before (17 px offset)   175..183                 187..193      200..225    3 / 6   -> grouped UPWARD
15 px (first attempt)   175..183                 189..195      200..225    5 / 4   -> still a tie
13 px (SHIPPED)         175..183                 191..197      200..225    7 / 2   -> 3.5x, grouped DOWN
```

The two distances the plates themselves use were measured off the shipped indigo art and settle what
"grouped" means here: **2 clear rows = "this dim line describes the row above"**
(`pause-bot-enhance.png`: "Stereoscopic 3D" ink 50..60, "top screen" ink 63..70) and **6 clear rows =
"this caption heads the control below"** (`pause-bot-display.png`: "SCALE · TOP" ink 12..19, its seg at
y=26). 6 below is unaffordable — the row has 42 rows between the baked label and the chrome band and
needs 9+26 for itself — so the split goes the other way. 13 is also the FLOOR
(`typo_ink_bottom(TXT_SECTION) == 13`), so a descender-bearing caption would still clear the control.
The offset is a **new style (`OV_SECTION_TIGHT`) used by that one row**; `OV_SECTION` stays at 17 so
the TOUCH tab's `GAMEPAD · EDGES` does not move.

OBSERVED after the fix: `shots/enhance-fixed-native.png`, caption ink **191..197**, 7 clear rows under
"Vivid mode", 2 over the seg. Zoom: `shots/FIXED-tilt-caption-zoom.png`.

### C6 / C7 — two constants that silently rotted, now derived

Both literals carried their own derivation in a comment and phase 19 moved the inputs underneath them:

```
PRES_PILL_H   15  "the 13 px chip + 2 px of air"          but UIHIT_CHIP_H    is 16 -> pills OVERLAP by 1 px
PRES_PILL_TOP 18  "bar 14 + a 2 px focus rule = 16; +2"   but UIHIT_HUD_BAR_H is 20 -> the clamp parks
                                                             the stack 4 rows INSIDE the chrome it exists to clear
```

They are now `((float)(UIHIT_CHIP_H + 2))` and `((float)(UIHIT_HUD_BAR_H + 2 + 2))`, with
`presence_ui.h` including `uihit.h` (pure C, `<stdint.h>` only — CLAUDE.md rule #4 intact).

**Why TEST 38 could not see it, and the new gate that can.** Every assertion in TEST 38 is written in
terms of `PRES_PILL_H` / `PRES_PILL_TOP`, so it graded the stack against the stale constants rather
than against the chrome they track. **TEST 38b** asserts the two relations against `UIHIT_*`. Proof it
is a real gate and not decoration — the same suite compiled against the OLD literals:

```
[FAIL] TEST 38b: pill pitch 15.0 must be >= the chip it paces (16) + 2 px of air
[FAIL] TEST 38b: the clamp parks the stack at y=18.0, inside the 20 px HUD bar + its 2 px focus rule (chrome owns rows 0..21)
[FAIL] TEST 38b: the clamped nameplate starts on row 18.0; the bar + rule own 0..21
[FAIL] TEST 38b: the clamped prompt starts on row 33.0 but the nameplate's 16 px chip runs to 34.0
=== 61376 checks, 4 failures ===
```

### C5 — REFUTED as framed, banked as a number

The finding's premise ("the plate dims native x 80..320") is **false**. `pause-top.png` was pixel-scanned:
every row of it is a **uniform full-screen scrim** — row 137 is RGBA `(9,7,13,189)` for all 400 columns,
and the only baked ink is the wordmark around row 105. The 240 px band visible in the capture is the
**game image at SCALE_1X** (240×160 centred on 400×240), i.e. a user setting, not art; the outlined pills
read at least as well over plain letterbox as over dimmed game pixels.

What is true and is now in T16: the design's own widget rect for this row is `pause-top` →
`active-feature pills` = **x 56, w 288**, and at phase-19 sizes the row exceeds it — 8 pills with the
longest dynamic labels = **313 px at x 41..354**, 9 = **352 px at x 22..374**. It also exceeded 288 at
the phase-18 rungs (291 px for 9), so it is not something this phase introduced, and shrinking the type
back is the one thing the phase exists not to do. T16 asserts the gates that mean something — the row is
centred on the manifest rect's own centre (200) and keeps ≥20 px of screen margin — and prints both
numbers every run.

### O1 — re-baked to check, then written down

`mkbcfnt jbm-bold.ttf -s {6,7,8}` baked fresh and measured through `tools/fontlab/bcfnt.py`:

```
pt 6  lineFeed 11  cellH 14  cap 7  advance 5      <- the PHASE-18 TXT_VALUE rung
pt 7  lineFeed 12  cellH 15  cap 7  advance 5      <- the PHASE-19 rung: same cap, same advance
pt 8  lineFeed 14  cellH 18  cap 8  advance 7      <- +40% advance, rejected
```

So the finding is right: 11 → 12 pt is a **line-height** move, not a size move. The ladder table in
`typography.h` now shows cap as a `before -> after` pair for all seven rungs (`10->11 7->9 7->9 6->9
5->7 4->7 7->7`), marks TXT_VALUE `<-- NOT a size step`, records why it moved anyway (12 puts the mono
line box on the same grid as TXT_SECTION/TXT_CHIP — the AUDIO tab draws its volume readout at
`rowY - 18` on the caption's own baseline) and why pt 8 is rejected. `T17` already printed the same
`(was N)` comparison every run; nothing in the bake changed.

### PROOF — the phase-18 crispness law did NOT move

Read out of the **rebuilt, running** app over the GDB stub (`runs/20260813-093901`):

```
gdbio read g_txtTexelScale 28
  0x00525398  00 00 80 3f 00 00 80 3f 00 00 80 3f 00 00 80 3f
  0x005253a8  00 00 80 3f 00 00 80 3f 00 00 80 3f          -> 1.0f x7, every role
gdbio read g_txtLineFeed 28
  0x0052537c  13 00 00 00 0f 00 00 00 0f 00 00 00 0f 00 00 00
  0x0052538c  0c 00 00 00 0c 00 00 00 0c 00 00 00          -> 19,15,15,15,12,12,12
```

No face was re-baked, no `px` moved, and every y this pass touched is a whole integer
(`typo_center_y` rounds by construction — T18 asserts `y == (float)(int)y`; `OV_SECTION_TIGHT_DY` is
`13.0f`). **No draw changed scale.**

### SUITES — thirteen app host suites + the harness host tests, green, none shrinking

```
control 6897 · diag 376 · fieldpath 1808 · netlink ok · presence 61376 · profiles 546 ·
theme 83444 · tilt 1723 · trace ok · typography 1419 · uigeom 18332 · uihit 1834 · celiolink ok
harness host tests: Ran 149 tests — OK
```

Grew: **typography 1408 → 1419** (T19; the two new T18 centring boxes; the T16 pill-row rect check;
the tilt caption row re-pointed) and **presence 61371 → 61376** (TEST 38b). Everything else identical.
Build: `make -j8` clean, **no new warnings** (the survivors are the pre-existing `main.c` /
`rompicker.c` unused-symbol + misleading-indentation set).

New coverage, and what each one would have caught:

* **T19 — one module per band.** The C1 blocker was invisible to a per-string test because it was two
  MODULES sharing a band. T19 proves they are geometrically incompatible and then pins the gate:
  ```
  TOUCH · SMART POINTER   chip x 101..220 vs name  15..117 / FOCUS 123..165 -> COLLIDES (so only one may draw)
  TOUCH · GAMEPAD         chip x 116..205 vs name  15..117 / FOCUS 123..165 -> COLLIDES (so only one may draw)
  ```
* **TEST 38b** (test_presence) — the two chrome-tracking constants, asserted against `UIHIT_*`.
* **T18** — `run_settings Done` (14 px) and `ROM picker settings-ZR chip` (16 px) added to the centring
  table, so the two hand-rolled sites are graded like the widget-backed ones.
* **T16** — the tilt caption at its new offset, and the pill row against the design rect.

### DEVIATIONS from the findings list

1. **C5 is not fixed, it is refuted and measured** — the plate has no 80..320 band (pixel-scanned).
2. **C4's fix is 13 px, not "move it beside the seg"** — `DIORAMA · TILT` is 70 px at TXT_CHIP and the
   space left of the seg is 47 px, so beside is impossible; and the finding's "4 rows either side" was
   wrong (it was 3/6, grouped upward).
3. **V2/C3's "the string is ungraded by test_typography" is false** — `{ TXT_CHIP, "settings · ZR", 82,
   "PICK_SETTINGS chip" }` has been in T10 all along. Only the vertical grade was missing; that is what
   was added.
4. **O1 is answered with documentation, not a bake** — pt 8 costs +40 % mono advance and breaks the
   "weight, not size" contract; the dishonest part was the table, and the table is fixed.
5. **O2 disables a surface the concurrent phase-20 slice uses.** Default `PRES_DIAG_HUD 0`; that slice's
   hardware calibration must build `-DPRES_DIAG_HUD=1`. Flagged here because it is the one change in
   this pass another slice needs to know about.

### Harness hygiene

Five boots (`runs/20260813-093637 / -093901 / -094047 / -094150 / -094448 / -094605`), every one
`azctl stop` → *"originals re-hashed, all untouched"* + *"restored qt-config.ini byte-identically
(CLEAN)"*; never two Azahars at once. The emulated `sdmc:/3DGBA/settings.bin` was backed up, patched
(`touchMode`, `presence`) to stage Smart mode and the co-op readout, and **restored byte-identically**
— sha1 `16e8b5f7bc98f3dd13801f03c48997425b373329` before and after. `sdmc/dual-gba/` never written.
New CTM recipes banked at `docs/phase19-legible/movies/m_fix_{smart,enhance,picker}.json`.

### Still open

Hardware sign-off (CLAUDE.md #6). The question for the user is unchanged from G2 — *can you read the
chips and the hint line without leaning in* — plus one new one this pass creates: **in Smart and
Gamepad touch modes the bottom screen no longer shows the game name, the LINK chip or the clock**
(by design, per `smart-bot`/`pad-bot` and screenshot 07); the top screen keeps all of it.

---

## FINAL GATE (2026-08-13)

The user-facing deliverable is **`RESULTS.md`**, written from this pass. Everything below is the gate
itself, run from a clean shell (`env -i`) at the project root.

### Suites — thirteen app host suites + the harness host tests

```
control    6897    diag        376    fieldpath  1808    netlink      66
presence  61376    profiles    546    theme     83444    tilt       1723
trace        58    typography 1419    uigeom   18332    uihit      1834
celiolink  1259
                                            TOTAL  179,138 checks · 0 failures
harness host tests (tools/emutest/tests/run_host_tests.sh): Ran 149 tests — OK
```

`test_profiles` now needs `source/peersprite.c` and `source/presence_art.c` on its link line (the
concurrent phase-20 slice added a `PROFILES[]` column and TEST 9); its header comment still names the
old line. Recorded here rather than edited, because that file belongs to the other slice.

### Build

`make clean && make -j8` clean, `make cia` clean:

```
3DGBA.3dsx  4,365,648 B      3DGBA.cia  2,008,000 B      data/  2,812,726 B (71 files, 5 faces)
```

### Warning audit — by WORKTREE, not by stash

`git stash` is unsafe here: a concurrent phase-20 slice shares this working tree (see *Collaboration
hazard* above). The baseline was therefore built as a **detached `git worktree` at HEAD**, with
`external/mgba` symlinked in and `tools/.fontcache` copied so the bake needed no network, then
`make clean && make -j8` in both trees and the normalised warning multisets diffed:

```
HEAD (phase 18): 28 warnings        phase 19: 28 warnings
diff of the two multisets: IDENTICAL — no new warnings, none removed
```

All 28 are pre-existing: unused statics/const-variables in `main.c` (7) and `rompicker.c` (2),
`-Wmisleading-indentation` in `main.c` (4), `touch.c` (4), `ui.c`, `theme.c`, `netlink.c`,
`sha1.c` stringop-overread (3), one `wireless.c` format-truncation, and one unused static in the
frozen `celiolink.c`.

That same worktree supplied the honest phase-18 baseline for the size table:
`data/` 3,870,842 B (73 files, 7 faces) · `3DGBA.3dsx` 5,417,868 B · `3DGBA.cia` 2,128,832 B.

### Frozen-path diffs — EMPTY

`git diff` over `source/celiolink.c`, `source/netlink.c`, `source/gbacore.*`, the HD-2D passes and
the emulation/threading files returns **nothing**. Confirmed by explicit path-scoped `git diff --stat`.

### R1 on the SHIPPED binary

`3DGBA.3dsx` from the clean rebuild, booted in Azahar, read live over the GDB stub
(`runs/20260813-100439`):

```
gdbio read g_txtTexelScale 28
  0x00525398  00 00 80 3f 00 00 80 3f 00 00 80 3f 00 00 80 3f
  0x005253a8  00 00 80 3f 00 00 80 3f 00 00 80 3f      -> 1.0f, all seven roles
gdbio read g_txtLineFeed 28
  0x0052537c  13 00 00 00 0f 00 00 00 0f 00 00 00 0f 00 00 00
  0x0052538c  0c 00 00 00 0c 00 00 00 0c 00 00 00      -> 19,15,15,15,12,12,12
```

### Captures — and why the banked ones are valid

Six screens re-captured from the shipped binary (`evidence/final-pause-{top,session,audio,enhance,
link,touch}.png`), all reconstructed to exact native resolution. The control that licenses the rest
of the banked G2 sweep:

```
run compare evidence/g2-pause-session.png evidence/final-pause-session.png
  IDENTICAL — 320x240, 0 differing pixels
```

Sheets: `evidence/sheet-before-after.html` (five matched before/after pairs, captioned) and
`evidence/sheet-shipped-screens.html` (the six fresh shipped-binary screens).

### Harness hygiene

Six boots this pass; every `azctl stop` reported *"fixtures: originals re-hashed, all untouched"* and
*"restored qt-config.ini byte-identically (CLEAN)"*; never two Azahars at once. The emulated
`sdmc:/3DGBA/settings.bin` was backed up, patched (`touchMode` → Off, for capture parity with the
before-set) and restored byte-identically — sha1 `16e8b5f7bc98f3dd13801f03c48997425b373329` before
and after. `sdmc/dual-gba/` never written. The concurrent session's staged ROM fixtures were left in
place, which is why the empty-picker "after" is banked rather than re-shot (stated in RESULTS.md).

### Still open

**Hardware sign-off (CLAUDE.md #6).** Unchanged and owed. The question that decides the phase is
*"can you read the chips and the hint line without leaning in?"* — those two rungs sit knowingly at
15.3′. Checklist: `docs/HANDOFF.md` → WHAT'S LEFT TO TEST item 6.
