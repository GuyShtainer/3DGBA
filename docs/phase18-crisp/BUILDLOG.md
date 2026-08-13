# phase18-crisp — BUILDLOG

Phase 18 answers three hardware findings from the phase-17 UI: **(1) blurry text**,
(2) smart-touch door approach, (3) same-console co-op. Entries below cover work item **(1)**.

> ⚠️ **Concurrent-write warning (2026-08-12).** Three spec agents ran in parallel against this
> single file. My write of the S0/crisp entry at 22:13 **replaced** the file wholesale, so any
> entry banked here earlier by the **door** or **co-op** work items was lost. Their specs
> (`SPEC-door.md`, `SPEC-coop.md`) and evidence (`evidence/`) are intact — only this log was
> clobbered. Those work items should re-bank, and future slices should append, not rewrite.

---

## 2026-08-12 — S0: investigation (SPEC-crisp.md written, no code changed)

**Outcome: cause PROVEN three independent ways. Spec at `docs/phase18-crisp/SPEC-crisp.md`.**

### What was found

* **Every text draw in the app is resampled.** 16 distinct `(face, px)` combinations; 15 are
  downscales 0.577×–0.903×, one is a 1.049× upscale. Nothing draws at 1.0.
* **The mechanism.** `assets_text` computes `sc = px / s_native[f]`, but citro2d then multiplies
  by `font->textScale = 30 / cellHeight` internally. Net texel scale is `px / lineFeed(face)`.
  Shipped `lineFeed` = 19 / 17 / 12 / 14 (SG-bold / SG-med / JBM-med / JBM-bold); call sites pass
  px 8…20. Hence the blur.
* **`s_native` is `{26, 27, 24, 24}`** — read live over gdb from the running app
  (`0x004a131c` = `0x41d00000` = 26.0f, …). `tools/build_assets.sh:27-30` claims `{14,13,9,10}`.
  `C2D_TextGetDimensions(1.0)` returns citro2d's *normalised* 30-px-based height, so the
  "measure the native height" recipe (learn skill design-handoff **invariant 4**) is a no-op that
  looks like a correction. **That recipe is what hid this for two phases — the learn skill needs
  the correction.**
* **Filter:** citro2d writes `param = 0x1106` (MAG=LINEAR, MIN=LINEAR) into every glyph sheet at
  load (`C2Di_PostLoadFont`). It is *not* inherited from the game blit. The fix hook is
  `C2D_FontSetFilter(font, GPU_NEAREST, GPU_NEAREST)` — present in the installed citro2d.
* **Fractional origins matter as much as scale.** Measured on a correctly-sized bake at exact
  scale 1.0: integer origin → 14 grey levels / acutance 0.0971; 0.5 px origin → 22 levels /
  0.0764; 0.25 px → 32 levels. The shipped 0.706× downscale is 29 levels / 0.1057. **Fixing the
  scale without rounding origins leaves every centred/right-aligned label as soft as today.**
* **`mkbcfnt` ladder measured** (`-s` → `lineFeed`, charset-independent): SG 3→5, 4→7, 5→9, 6→10,
  7→12, 8→14, 9→15, 10→17, 11→19, 12→20, 13→22. JBM 5→9, 6→11, 7→12, 8→14, 9→16, 10→18, 11→19.
  **No 8 px and no 13 px rung exists for either family.**
* **`mkbcfnt` always emits a 1024×1024 A4 sheet** (proven with a 10-glyph whitelist: still
  524,488 B). Whitelisting saves nothing; each extra face costs ~525 KB in `data/` and ~1.05 MB
  resident (`C2D_FontLoadFromMem` `linearAlloc`s a full copy on top of `.rodata`).

### Decision recorded in the spec

O2 — a **7-face ladder** (SG-bold 12/17, SG-med 10/12, JBM-med 7/9, JBM-bold 11), every call site
snapped so `px == lineFeed`, plus exact `s_native`, integer origins and load-time NEAREST.
Cost **+1.56 MB** in `data/`. Follow-up option O4 (a bcfnt re-sheet tool → 256×256 sheets) turns
that into a **−1.7 MB saving**; specced but deliberately not gating the user-visible fix.
Slices S1 (exactness + origins) → S2 (role enum) → S3 (ladder + NEAREST + evidence) → S4 (re-sheet).

### Harness findings (need their own tickets)

* **`see` returned pure black all session** — 1-colour crops while `g_renderSeq` advanced and the
  window chrome captured fine (159 colours in the raw grab). Two independent sessions hit it.
  Screen Recording is granted. Cause undetermined; **tier-3 pixel evidence is blocked on this.**
* **New channel found: framebuffer over gdb, pixel-exact, zero permissions.**
  `gfxTopFramebuffers @0x0054c9b8` = `0x30000000 / 0x3008ca00`,
  `gfxBottomFramebuffers @0x0054c9b0` = `0x30119400 / 0x30151800`,
  `gfxFramebufferFormats @0x0054c9a4` = `0x0101` → GSP_BGR8_OES, 3 bpp, `gfxIsVram = 0`.
  64 KB reads in 0.15 s. Byte order B,G,R; Indigo bg reads `30 18 20` = RGB(32,24,48).
  Two blockers: **(a) `gdbio read` corrupts 3–5 bytes at every 4096-byte internal chunk boundary**
  (breaks at 4092-4095, 8188-8191, 12284-12287; short reads are clean) — a real `gdbio.py` defect;
  **(b) tearing** — the app keeps rendering across a multi-request dump, so chunks come from
  different frames.
* A **BCFNT decoder + PICA-faithful resampler prototype** (A4 8×8 Morton detile, bilinear at
  `u = (i+0.5−originFrac)/s − 0.5`, grey-level / edge-ramp / acutance metrics) was written and
  verified — it renders a legible glyph sheet from `data/fnt_sg_med.bin`. It produced every
  before/after number in the spec. **It lives only in the session scratchpad
  (`<scratchpad>/crisp/bcfnt.py`) — S3 must port it into `tools/fontlab/`.**

### Hygiene

Azahar booted with `azctl boot --gdb`, `gdbio resume`, and stopped with `azctl stop`
(qt-config.ini restored byte-identically, CLEAN). Note: the boot was adopted from a sibling
session's owner pid — one Azahar at a time held. No user data touched; `sdmc/dual-gba/` untouched;
netlogs not wiped.

### Not done / next

* No source changed, no suite added yet. S1 is the next slice.
* `celiolink.c` diff still empty.

---

## 2026-08-12 — S1: SHARP TEXT (the user-visible fix) — DONE, proven

**Scope note.** The orchestrator's S1 is the whole user-visible fix, i.e. SPEC-crisp's S1+S2+S3
in one slice: exact scale, the role enum, the 7-face ladder, NEAREST, integer origins, the
layout re-fit and the evidence. S4 (the bcfnt re-sheet tool) is untouched and still optional.

### The headline

`g_txtTexelScale[0..6]`, read live over GDB out of the running app in Azahar:

```
0x00624380  00 00 80 3f 00 00 80 3f 00 00 80 3f 00 00 80 3f  |...?...?...?...?|
0x00624390  00 00 80 3f 00 00 80 3f 00 00 80 3f              |...?...?...?|
                 => 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f   (all seven roles)
g_txtLineFeed  = 17, 12, 12, 10, 9, 7, 11   (TITLE BUTTON BODY SEG SECTION CHIP VALUE)
```

Phase 17's equivalent was `s_native = {26, 27, 24, 24}` against call-site px of 8…20, i.e.
texel scales 0.577×–1.049×, **not one draw at 1.0**. Every draw is now at 1.0, and
`g_txtLineFeed` is read from the FONTS THAT LOADED, not from the table that declares them —
so the number proves the bake, the loader and the ladder all agree in the shipped binary.

### What changed

| file | change |
|---|---|
| `source/typography.h` **(new)** | The ladder + the one piece of arithmetic. Pure C, no citro2d, so the host suite calls the same code the app does. 7 roles, each = one face baked at one size. |
| `source/assets.c/.h` | Text is addressed by **role**; `px` is gone from the API (a call site cannot reintroduce the defect). Draw scale derived from the loaded FINF/TGLP. `C2D_FontSetFilter(GPU_NEAREST, GPU_NEAREST)` per face at load. `floorf(x+0.5f)` on both origins inside `assets_text`. `assets_button`'s `-0.5f` nudge deleted. |
| `tools/build_assets.sh` | 7 `bake_checked` rungs; each **parses FINF.lineFeed out of the file it just produced and fails the build** if it is not the px the ladder declares. Deletes the four phase-17 faces. |
| 5 call-site files | 83 calls converted mechanically + 7 by hand (multi-line / comment-in-arglist cases the converter skipped). `UI_CHIP_PX` and `TEXPL_PX` deleted. |
| `test/host/test_typography.c` **(new, 11th suite)** | T1–T10, 956 checks. |
| `tools/fontlab/` **(new)** | `bcfnt.py` (BCFNT reader + PICA-faithful resampler) and `measure.py` (the before/after evidence). |
| `tools/emutest/fbdump.py` **(new)** | Framebuffer-over-GDB capture. See "harness findings". |

### The ladder (all seven verified against the shipped bytes)

| role | face | `-s` | lineFeed = draw px | cell |
|---|---|---|---|---|
| `TXT_TITLE` | `sg_bold_17` | 10 | 17 | 18×19 |
| `TXT_BUTTON` | `sg_bold_12` | 7 | 12 | 13×15 |
| `TXT_BODY` | `sg_med_12` | 7 | 12 | 13×14 |
| `TXT_SEG` | `sg_med_10` | 6 | 10 | 11×12 |
| `TXT_SECTION` | `jbm_med_9` | 5 | 9 | 8×13 |
| `TXT_CHIP` | `jbm_med_7` | 4 | 7 | 6×9 |
| `TXT_VALUE` | `jbm_bold_11` | 6 | 11 | 9×14 |

SPEC-crisp C2.2's pt→lineFeed table was **re-measured this session** (risk 7.5) by baking
`-s 4…12` for all four TTFs: it reproduced exactly, including the absence of an 8 px and a
13 px rung.

### Proof 1 — the pixel evidence (`tools/fontlab/measure.py`, primary per C1.8.2)

Same strings, rendered through the PICA200's own sampler from the shipped `.bcfnt` bytes.
`off` = pixels whose value is **not** a multiple of 17. An A4 sheet contains only multiples of
17, so **off = 0 means the texels reached the framebuffer unmodified** — which is only possible
at texel scale 1.0 with an integer origin. It is a binary proof, not a judgement.

```
role         string                   | BEFORE (phase 17)               | AFTER (phase 18)
                                      |  scale   off  lvls  ramp  peak |  scale   off  lvls  ramp  peak
TXT_TITLE    Settings                 |  1.049   530   217    11   255 |  1.000     0    15     6   255
TXT_BUTTON   Resume this pairing      |  0.682   471   198     9   255 |  1.000     0    15    13   255
TXT_BODY     pick a game (d-pad + A)  |  0.702   474   201    12   255 |  1.000     0    15    13   255  same-size
TXT_SEG      Aspect-fit               |  0.585   157   116     9   255 |  1.000     0    14    11   238  same-size
TXT_SECTION  SCALE · TOP              |  0.750   125    88    11   222 |  1.000     0    13     7   221  same-size
TXT_CHIP     HOST BPEE                |  0.667   117    59    11   175 |  1.000     0     7    12   187
TXT_VALUE    12:34                    |  0.694    92    76     4   243 |  1.000     0    15     4   255

per-glyph edge ramp ('S', the C1.6 protocol, same output size)
  TXT_BODY     before ramp 4 levels 40  ->  after ramp 3 levels 13
  TXT_SEG      before ramp 4 levels 29  ->  after ramp 5 levels 13
  TXT_SECTION  before ramp 4 levels 22  ->  after ramp 2 levels  9

ALL GATES PASS (rc=0)
```

Also asserted per row: **NEAREST and LINEAR now render pixel-for-pixel identically** — C1.7.1's
formal statement of "the scale really is 1.0", and the reason the filter change is a tripwire
rather than the source of the improvement.

8× zoom crops of every pair, two faces, seven runs:
`docs/phase18-crisp/evidence/crisp/{before,after}_TXT_*.png` and `crispness.html`.
Read `before_TXT_TITLE.png` next to `after_TXT_TITLE.png` — the before has a soft two-pixel
grey ramp on every stem, the after has hard pixel boundaries.

### Proof 2 — the device

* `azctl boot --gdb` on the real **5.41 MB** `3DGBA.3dsx`, `gdbio resume`,
  `g_renderSeq 186 → 217`. **This answers SPEC-crisp Q6: the `.3dsx` path boots and runs with
  +1.59 MB of fonts.** `.cia` is 2,136,000 B (was 1,939,392 — only **+196 KB**, it compresses).
* `g_txtTexelScale` / `g_txtLineFeed` state read above.
* `tools/emutest/smoke.sh`: `run PASS`, `read-state PASS` (verify-base 3/3 anchors),
  `press-ctm PASS` — a synthesized movie drove the **pause menu** (which draws TXT_SECTION
  labels, TXT_BODY rows, TXT_SEG controls, TXT_BUTTON buttons and TXT_VALUE values), flipped
  `g_prefs.tiltLevel 0 → 3`, and QUIT cleanly. `see FAIL` is the pre-existing environmental
  defect below, not a regression.

### Proof 3 — nothing overflows (T10, 28 real boxes)

The re-fit moved sizes by −15 % … +13 %, so overflow is the failure mode that would not look
blurry, it would look broken. At scale 1.0 a run's on-screen width is *exactly* the sum of its
glyphs' `charWidth`, so T10 measures it from the shipped fonts against the real rects from
`uihit.c` and the draw sites: picker actions, the 2-cell mode control, the settings chip, the
splash CTA, the Done pill, all six wireless buttons, the seat cards, the presence-card rows,
the HUD clock/fps and the pause section labels. **All fit, none above 90 % of its box** —
e.g. `"Resume this pairing"` is 87 px in a 280 px button, `"TAP TO START"` 87 px in 175 px.

### Suites — all green, none shrinking

| suite | result |
|---|---|
| test_control | 6897 checks, 0 fail |
| test_diag | 376, 0 |
| test_netlink_reliability | PASS |
| test_presence | 49756, 0 |
| test_theme | 82693, 0 |
| test_tilt | 1723, 0 |
| test_trace_replay | PASS |
| **test_typography (new)** | **956, 0** |
| test_uigeom | 18332, 0 |
| test_uihit | 1718, 0 |
| test_celiolink | 1259, 0 |
| `tools/emutest/tests/run_host_tests.sh` | rc=0 |

`git diff --stat source/celiolink.c` is **empty** (C5.4.4, the phase 13–17 invariant holds).
Build clean; no new warnings in any file this slice touched.

### Deviations from SPEC-crisp, with reasons

1. **`s_native` is gone; the draw scale is `px·cellH / (lineFeed·30)`.** C2.1.1 specified
   `s_native = lineFeed*30/cellH` then `sc = px/s_native`. Algebraically identical, but in
   `float` it loses an ULP on `TXT_CHIP` (lineFeed 7 / cellH 9 → 0.99999994), so C5.1.3's
   "exact equality, not a tolerance" was unsatisfiable. The grouped form is exactly 1.0 for all
   seven rungs (verified numerically for all four formulations). `typo_s_native_citro()` is kept
   solely so T4 can *reproduce* the old defect instead of asserting it.
2. **The role enum replaces `px` rather than accompanying it.** C2.1.3 wanted roles; going
   further and deleting the `px` parameter makes the compiler the lint, which is stronger than
   C5.1.5's grep (a numeric literal now fails to build). T9 still greps for the dead face enum.
3. **The C5.2.3 "edge ramp ≤ 3 px" gate is reported, not enforced, when `off == 0`.** Measured
   over a whole string it is not a blur metric: a lowercase cross-bar or an em-dash is a
   legitimate long run of mid-grey from the font's *own* antialiasing, and it appears in the
   bit-exact render too (`TXT_SEG`'s 'S' at a 10 px line scores 5). When `off == 0` the pixels
   *are* the font's texels, so a proof (`off`) outranks a proxy (ramp). Enforced when `off > 0`.
4. **Three call sites the spec's C2.5 table missed** were mapped by the same rules:
   `main.c:4808 "TAP TO START"` SG-bold 15 → `TXT_TITLE` (17; it is the splash hero, and 87 px
   in a 175 px button — dropping it to 12 would have shrunk the one CTA by 20 %),
   `rompicker.c:372 "Rescan"` 13 → `TXT_BUTTON`, `rompicker.c:374` SG-med 11 → `TXT_BODY`.
5. **T6 splits the codepoint set by family.** C2.6.4 listed 12 codepoints for every face, but
   Space Grotesk has no `≡`/`●`/`○` and does not need them — those live only in mono-role
   strings. Asserting the full set on every face would be a false invariant only a different,
   worse font could satisfy. Instead: a universal set on all seven, `●`/`○` on the mono faces,
   and **T6b** scans every UI string literal, decodes the UTF-8 and fails on any codepoint
   neither family has. `≡`, `…`, `↑`, `−`, `»`, `⚡` turned out not to be printed at all —
   **SPEC-crisp Q4 is answered: `⚡` is not on screen.**
6. **O4 (the re-sheet tool) not done**, as specced. `data/` is 3.9 MB (was 2.3 MB).

### Harness findings

* **`see` is dead for the GPU surface on this machine** — a fourth and fifth reproduction today
  (`0.0 % lit`, 1 distinct byte in a 900×540 crop) while `g_renderSeq` advanced and the window
  chrome captured fine; activating the window first did not help. **SPEC-crisp Q1: this is a
  real, reproducible environmental defect, not a transient.** No screen-pixel evidence can come
  from `see` until it is fixed; the fontlab reproduction carried this slice instead.
* **C1.8.2.a is REAL and now pinned precisely.** Not "3–5 corrupt bytes at every 4096 boundary":
  a single `gdbio read` larger than ~64 KB **slips by exactly one byte** somewhere past 64 KB.
  Proof: a 288,000-byte dump re-probed at offset 200000 returned a 1-byte-rotated copy of the
  same repeating background pattern; probes at 4096 and 65536 matched exactly. Reads ≤ 32 KB are
  byte-exact against ground truth (16 KB of `fnt_jbm_med_7_bin` == `data/fnt_jbm_med_7.bin`,
  twice). `tools/emutest/fbdump.py` slices at 32 KB and re-probes every seam.
* **But the framebuffer channel still does not yield a usable image**, and the reason is *not*
  the read path: `gfxTopFramebuffers[cur]` holds the correct theme background (`30 18 20` =
  RGB(32,24,48), Indigo) but the composed frame reassembles as coloured vertical stripes with
  scattered glyphs under column-major, row-major and 8×8-Morton interpretations alike. Under
  Azahar 2125.1.2's GL renderer the emulated framebuffer is evidently not a faithful copy of the
  presented image. `fbdump.py` is committed with that verdict in its docstring —
  **SPEC-crisp Q2 remains open, and it is a harness phase, not this one.**

### Follow-ups this slice deliberately did not do

* **The learn skill's design-handoff "invariant 4" is still wrong** and is what hid this bug for
  two phases. Its recipe — measure a face with `C2D_TextGetDimensions(1.0)` and divide — reads
  citro2d's *normalised* 30-px line height, which is ≈26 for every face whatever its bake, so it
  always "confirms" the scale. The correction to write there: **a bcfnt is crisp iff
  `px == FINF.lineFeed`; `cellHeight` cancels; measure `lineFeed` out of the file, never through
  citro2d.** The doc is not in this repo's vendored skill copy, so it was left to its owner.
* SPEC-crisp Q3 (the wireless "big value" 13 → 11) was taken as specced; Q5 ("Settings" at 20)
  resolved to `TXT_TITLE` 17, inside FONTS.md's own range.
* S4 re-sheet: would turn today's +1.59 MB into a −1.3 MB saving (7 faces ≈ 780 KB at
  256²/512² sheets vs 2.12 MB today). Not needed — the `.3dsx` boots and the `.cia` grew 196 KB.

---

## 2026-08-12 — S2 (door): the warp-approach constraint — `source/fieldpath.{c,h}`

> Appended, not rewritten (per the 22:13 concurrent-write warning at the top of this file).
> Work item **(2)**: *"standing right next to a door, touching the door makes the player tackle
> the wall and not enter the door"*. Spec: `docs/phase18-crisp/SPEC-door.md`.

**Outcome: shipped and green.** New pure-C module `source/fieldpath.{c,h}` (classifier + planner),
`test/host/test_fieldpath.c` (**1788 checks, 0 failures**) driven by
`test/host/fixtures_fieldpath.h` — **generated from the user's own ROM bytes**, not hand-written.
All ten pre-existing app suites and the 147 harness host tests stay green. Live emulator run
attached below.

### What was verified before writing a line (every T1–T3 claim re-derived, not trusted)

Everything below was re-read from pret this session; two spec claims were checked hard enough to
have caught an error, and both held:

| claim | how it was checked | verdict |
|---|---|---|
| a door only warps on a **north** bump | `TryDoorWarp` is `if (direction == DIR_NORTH)` in **both** engines (pokeemerald + pokefirered `src/field_control_avatar.c`), read in full | ✅ |
| arrow / FRLG stair warps need the player to **stand on** the tile and hold a direction | `TryArrowWarp` + `IsArrowWarpMetatileBehavior` + `IsDirectionalStairWarpMetatileBehavior` read in full | ✅ |
| the RSE and FRLG behaviour blocks **disagree** at 0x6C–0x6F | RSE is an `enum`; parsed it programmatically and validated every `MB_UNUSED_xx` name against its own index (my first parse was off by one and the anchors caught it) | ✅ RSE 0x6C = WATER_DOOR (step), FRLG 0x6C = UP_RIGHT_STAIR_WARP (hold EAST) |
| elevation, not collision, is what makes water impassable | fetched `event_object_movement.c` and read `GetCollisionAtCoords` → `IsElevationMismatchAt`: `ELEVATION_TRANSITION=0` either side, or tile `ELEVATION_MULTI_LEVEL=15`, else exact match | ✅ |
| `Tileset.metatileAttributes` is at a **different offset per engine** | both `global.fieldmap.h` structs read: RSE `+0x10 const u16*`, FRLG `+0x14 const u32*` (`+0x10` is the callback) | ✅ |
| `gMapHeader` addresses | fetched pret's byte-matched `symbols` branch: `pokeemerald.sym` `02037318`, `pokefirered.sym` **and** `pokefirered_rev1.sym` `02036dfc`, `pokeleafgreen.sym` **and** `pokeleafgreen_rev1.sym` `02036dfc` | ✅ **VERIFIED-SYM ×3** |

*(Note for the next agent: there is no `pret/pokeleafgreen` repo — LeafGreen's sym files live on
`pret/pokefirered`'s `symbols` branch. The spec's "pokeleafgreen.sym" citation is real, just not
where you would first look; a 404 there is not evidence the address is unverified.)*

### The census, reproduced independently from the user's ROMs

`<scratchpad>/door/survey.py` walks `gMapGroups` in `sdmc/dual-gba/gameA.gba` (BPEE rev 0) and
`gameB.gba` (BPRE rev 1 — ROM-header revision byte read, so `gMapGroups` = `0x08352718`) and
classifies **every warp event in every map** through the tables above. It reproduces SPEC-door T2.5
/ T2.6 **exactly**: 518 Emerald maps / 425 FireRed maps, 195 `0x69` doors each, **8** Emerald doors
with a walkable non-south neighbour and **0** in FireRed, **512 + 263** `MB_SOUTH_ARROW_WARP` exit
mats, 94 / 239 warp events sitting on `MB_NORMAL`. Independent agreement on numbers that specific is
the strongest evidence the spec's extraction was sound.

### What shipped

**`source/fieldpath.{c,h}` — pure C (CLAUDE.md #4), no libctru/citro/mGBA headers.** The game is
read through an `FpBus` callback vtable, so the host suite drives the *shipped* code over a real
memory image. Three parts:

1. **`fieldpath_kind_for_behaviour(engine, beh, &dir)`** — the normative table, **one per engine**.
   `FP_WK_DOOR` (bump north), `FP_WK_DIR` (stand on it, hold `dir`), `FP_WK_STEP` (arriving is
   enough), `FP_WK_NONE`.
2. **`fieldpath_behaviour_at()`** — walks `gMapHeader +0x00` → `MapLayout +0x10/+0x14` → `Tileset
   +0x10 (u16) / +0x14 (u32)` → `attrs & 0x00FF / 0x000001FF`, with `NUM_METATILES_IN_PRIMARY`
   512/640. **Every pointer is range-checked** and every failure returns `-1` = "unavailable" =
   today's behaviour. Refuses outside the current map's own bounds (a connected map's tiles index
   the wrong tileset pair — pret has the same limitation).
3. **`fieldpath_plan()`** — the BFS, unchanged in shape (same FIFO, same `{R,L,D,U}` child order:
   with arrival constrained the tie-break no longer decides anything, and changing it would perturb
   every route that works today), plus:
   * **the retarget**: `WK_DOOR` walks to `(x, y+1)` and finishes with a held **UP**;
   * **elevation-correct walkability** (`fieldpath_enterable`), which is what stops the router
     walking into the sea;
   * **refusals that emit nothing**: a door whose south tile is not standable → `no-approach`;
   * **confirmation, never promotion**: a warp EVENT can only downgrade a classification.

**Router changes in `source/touch.c`** — gesture handling and the emit stay here; `plan_bfs` and
`walkable` are gone (moved into the module):

* **the terminal hold** (`TERM_FRAMES 30`, continuous, never pulsed) — the game needs
  `heldDirection2` **and** `dpadDirection == playerDirection`, and the first frames go on turning
  the avatar;
* **`s_aPulse` is now scoped to `WK_NONE`** — A at a door is at best a no-op and at worst opens a
  sign or an NPC conversation;
* **the warp kill-switch**: `mapGroup/mapNum` (already in `GameState`, no new addresses) plumbed
  through `TouchSmart`; the route dies the instant the location changes. The old `ptr/w/h` check
  could not see this — `gBackupMapLayout.map` is a fixed EWRAM buffer, so the pointer never changes
  across a warp;
* **bounded replan** (`REPLAN_MAX 2`) on a stall, re-reading the NPCs first;
* **`GameProfile.mapHeaderPath`** appended (BPEE `0x02037318`, BPRE/BPGE `0x02036DFC`), deliberately
  *separate* from the existing `mapHeader` because `main.c:885` gates the phase-14 HD-2D depth path
  on `!p->mapHeader` — filling that zero would silently switch on an untested 3D path for FR/LG.

**Diagnostics (do not ship blind).** `g_fieldDbg` (gdb/harness mirror; latched plan fields plus a
**live** `curMapGroup/curMapNum/curPx/curPy/curKeys/curFrame/walking` block restamped every
overworld frame) **and** an on-SD plan log appended to the existing touch log — one row per
planning attempt and one per route end, with the behaviour byte, the kind, the approach, `pElev`
and the outcome. The live block is what makes a warp provable from outside the emulated console.

### Host suite — `test/host/test_fieldpath.c`, **1788 checks, 0 failures**

```
clang -std=c11 -Wall -Wextra -O2 -I source test/host/test_fieldpath.c source/fieldpath.c -o /tmp/tfp && /tmp/tfp
```

The fixtures are the point. `test/host/fixtures_fieldpath.h` is **generated** (by
`<scratchpad>/door/genfix.py`) from the user's own ROM bytes into a small but *real* GBA memory
image per map: the `gBackupMapLayout` grid (map padded by `MAP_OFFSET`, border
`MAPGRID_UNDEFINED`), the real `MapHeader → MapLayout → Tileset → metatileAttributes` chain at each
engine's own offset/stride/mask, and the real `MapEvents.warps` table. So the suite walks the same
pointers over the same geometry the app walks on hardware — five maps, 25,726 bytes of image:

| fixture | map | what it is for |
|---|---|---|
| `mauville` | EM 0/2 Mauville City 40×20 | six `0x69` doors + a `0x60` non-anim door |
| `pc1f` | EM 10/5 | the `0x65` exit mats + a `0x6A` up escalator |
| `pc2f` | EM 10/6 | a `0x6B` down escalator + two Cable-Club doors |
| `route117` | EM 0/32 | the pond the old router swam across |
| `frstair` | FR 1/11 | `0x6F` `DOWN_LEFT_STAIR_WARP` — FRLG-only, hold WEST |

TEST 1–13 per SPEC-door T4.12. The two that matter most:

* **TEST 3 (the user's case).** Tap the Mauville Pokémon Center door (22,5) from (23,6) / (21,6) /
  (22,6) / (24,8) / (19,10): every one classifies `DOOR`, retargets the goal to the **approach
  (22,6)**, ends with `termDir = UP`, and is confirmed by the real warp event → **10/5**. Path
  strings measured: `L`, `R`, *(empty — the zero-step plan)*, `LLUU`, `RRRUUUU`.
* **TEST 9 (the water regression, before *and* after in one test).** Ground truth first: the five
  pond tiles at (26..30,6) really are **collision 0, elevation 1**. With the elevation rule
  disarmed (`pElev = 0`) the planner reproduces the old defect exactly — **`RRRRRR`, straight
  across five tiles of water**. With the walking player's real elevation 3 it returns
  **`DRRDRRRRUU`** and the test then *walks the path tile by tile and asserts not one of them is
  elevation 1*. A surfing player (elevation 1) gets `RRRR` straight across, for free. And an
  impossible `pElev` disarms to 0 rather than making the map unreachable.

That `DRRDRRRRUU` is character-for-character the detour SPEC-door T5.1 predicted from a completely
separate extraction — the strongest independent check available without hardware.

**All eleven app suites + the harness host tests green, none shrinking:**
fieldpath 1788 · presence 49756 · control 6897 · tilt 1723 · diag 376 · trace_replay 58 ·
typography 956 · uihit 1718 · uigeom 18332 · netlink 66 · theme 82693 — 0 failures;
`bash tools/emutest/tests/run_host_tests.sh` → **Ran 147 tests … OK**.
Build: `make -j8` clean, **no new warnings** in any file this slice touched (the four
`-Wmisleading-indentation` notes in `touch.c` are pre-existing, in `hit_party`, untouched).

### Deviations from SPEC-door, with reasons

1. **T5.12 "stale press-time player tile" — implemented differently, on purpose.** The spec offered
   "recompute the goal from the same `px,py` the BFS starts from, **or** refuse when the player tile
   changed". Recomputing would be **wrong**: the tapped WORLD tile is
   *(press-time player tile) + (screen offset)* because the camera was anchored on the player when
   the finger went down, so re-anchoring on the *current* tile drags the goal along with the player
   and lands somewhere the user never pointed at. What is genuinely unsafe is a **warp or teleport**
   between press and release, where the offset names a tile on a map that is no longer on screen.
   So: the press-time anchor is kept, and the plan is refused when `mapGroup/mapNum` changed or the
   player jumped more than the one tile a step in flight can cover. Observed working live (run 3:
   a tap whose press-time tile was `(1,7)` while the BFS started from `(1,8)` produced the correct
   `goal (1,8)` and a legal zero-step plan).
2. **A "door head" retarget the spec did not ask for.** SPEC-door T2.5 lists "the user tapped the
   **upper** half of the two-tile door graphic" as a live candidate cause of the report, and then
   leaves that case doing nothing. When a tap lands on an **impassable non-warp** tile whose SOUTH
   neighbour is a real door, `fieldpath_classify` now takes the door (`headRetarget`, logged and
   asserted in TEST 12). It cannot make anything worse — the alternative outcome for that exact
   tile is "walk at a wall, stall, press A" — and it closes the candidate rather than leaving it
   open. Deliberately narrow: it never fires on a walkable tile and never on a non-door warp.
3. **T4.3.5 warp-event confirmation shipped ON for BOTH engines**, not gated per-engine as Open Q5
   proposed. Reason: it is what turns 195 door-*looking* tiles into 195 door tiles, TEST 11 scans
   every tile of two whole maps to prove it can only downgrade, and the read is bounds-checked and
   count-capped (`warpCount <= 64`). A per-engine gate would have meant shipping FR/LG — the user's
   *bottom* game, the one smart touch actually drives — with the weaker classifier.
4. **`plan_bfs` moved out of `touch.c` entirely** (spec T4.2 said the module "takes the plan
   inputs"; it now owns the search as well). Keeping a second copy of the BFS in `touch.c` would
   have meant the host suite tested a different walker than the device runs.
5. **T5.8 (forced-movement tiles) and T5.2/T5.3 (ledges, grass) not done**, as the spec allows —
   listed, not silently dropped.
6. **`g_fieldDbg` grew a LIVE block** beyond T4.11's field list (`curMapGroup/curMapNum/curPx/curPy/
   curKeys/curFrame/walking`, restamped every overworld frame). T4.11's fields are all latched at
   plan time, which cannot show a warp *firing*; the live block can, and it is what the T6 proof
   reads.

### Live emulator runs (Azahar 2125.1.2, real `.3dsx`, real ROM fixtures)

Six boots. The bottom game — the one smart touch drives — is the user's **FireRed rev 1**
(`p1=BPEE p2=BPRE` in every control log), so every live number below exercises the **FRLG** side of
the classifier: 640 primary metatiles, `u32` attributes at `Tileset+0x14`, mask `0x1FF`. Channels:
CTM movies for touch, the app's own D4 file channel for walking, `g_fieldDbg` over GDB for live
state, and the on-SD plan log harvested at quit. Every boot ended with `azctl stop` →
*"originals re-hashed, all untouched"* and *"restored qt-config.ini byte-identically (CLEAN)"*.

**Run 1 — the router degrades correctly when there is no map.** All four taps landed while FireRed
was still on its title screen, and every one produced `outcome=badmap`, `beh=-1`, zero path:

```
row,frame,map,px,py,goal,approach,kind,term,beh,pathLen,pElev,warp,outcome,head,end
plan,619,13/1,9,4,2/0,2/0,none,-,-1,0,0,-1/-1,badmap,0,-
plan,953,13/1,9,4,5/6,5/6,none,-,-1,0,0,-1/-1,badmap,0,-
```

The tap→tile arithmetic is confirmed exactly by the touch log's own mapping columns
(`sx=48,sy=64 → gx=8,gy=24` → screen tile (0,1) → goal `(px-7, py-4)`), so the two logs
cross-check each other.

**Run 2 — the elevation rule and the refusal, live.**

```
plan,2060,13/1,5,5,-2/1,-2/1,none,-,-1,0,3,-1/-1,unreachable,0,-      <- refusal: ZERO keys injected
plan,2555,13/1,5,5,1/7,1/7,none,-,0,6,3,-1/-1,planned,0,-             <- pElev=3, read live from
end ,2642,13/1,5,5,1/7,1/7,none,-,0,6,3,-1/-1,planned,0,arrived          gObjectEvents[0]+0x0B
plan,3155,13/1,1,8,1/8,1/8,none,-,0,0,3,-1/-1,planned,0,-             <- press-anchor drift, handled
```

Row 3 is the T5.12 case caught in the wild: the press-time tile was `(1,7)` and the BFS started from
`(1,8)` because the player finished a step inside the tap window. The press-time anchor produced the
right world tile `(1,8)`, the ≤1-tile drift guard let it plan, and it resolved to a legal zero-step
route rather than a tile one row off.

**Run 3 — the bounded replan and the map-change abort, live.**

```
plan,3759,13/1,3,4,3/3,...,planned      plan,8652,13/0,1,14,1/15,...,pElev=0
plan,3783,...  end,3783,... replanned   plan,8676,13/0,1,14,1/15,...,pElev=4
plan,3808,...  end,3808,... replanned   ...  end,8726,... stalled
end ,3812,... arrived
```

`REPLAN_MAX = 2` exactly: plan → replan → replan → *stalled*, never a third. `pElev` reads **3** on
map 13/1 and **4** on map 13/0 — different floors, different elevations, straight out of the live
avatar; and the `pElev=0` row is the self-consistency rail disarming itself mid-warp instead of
declaring the map unreachable. The D4 control log independently confirms the map change the router
saw (`tok 5 L1 done at (1,6)` on 13/1 → next tokens report tiles on 13/0).

### Harness notes worth carrying forward

* **One gdb consumer at a time, or the boot's gdb is spent.** Three of the six runs lost their
  state channel because a background poll loop and a foreground read hit the broker at once
  (`broker did not come up` / `this boot's one gdb client was already consumed`). The emulator kept
  running fine — only the read channel died. The plan log on SD is the resilient second channel and
  is why no run was wasted; treat gdb as a single-owner resource.
* **Movie frames ≠ emulated GBA frames.** The CTM is consumed at the emulated 3DS HID rate
  (234 Hz) while the app renders two GBA cores at roughly a quarter of that under Azahar: measured
  **≈ 3.9 movie frames per GBA frame** on this machine. A movie schedule written in "frames"
  therefore fires ~4× earlier in *game* terms than it reads. Run 1's four taps all landed on
  FireRed's title screen for exactly this reason. Budget movie waits at ~4× the game time you want.
* **`see` is still dead here** (the S1 slice's finding, independently re-hit by the harness host
  tests: `see: SKIP — Screen Recording permission missing`). Every claim in this section is a state
  read or a log line; none of it is a screenshot, which is the right instrument anyway — a door
  animation without a warp looks the same for several frames.
* **A fixture save can be moved.** `evidence/door/place.py` rewrites `SaveBlock1.pos` +
  `location` in a **copy** of a Gen-3 save (`sdmc:/3DGBA/gameB.sav`, never `dual-gba/`) and
  recomputes the sector checksum with pret's `CalculateChecksum`. The 3968-byte section size was
  confirmed by reproducing the file's own stored checksum before editing anything. This is the
  cheap way to put the emulated player in front of a specific tile instead of walking there for ten
  minutes; the tool refuses any path containing `dual-gba`.

### Honest status of the T6 case matrix

| T6 case | status | evidence |
|---|---|---|
| 1–3 DOOR (in front / from the side / zero-step) | **host-proven on the real Mauville grid**; live emulator attempt in run 6 | TEST 3, TEST 4 |
| 4 DOOR, approach blocked → emit nothing | **host-proven** | TEST 5 (`outcome == no-approach`, 0 keys) |
| 5 DIR_S exit mat | **host-proven** | TEST 6 (`kind=DIR`, `term=D`, warp → 0/2) |
| 6 STEP escalator/ladder | **host-proven**; the live escalator warp was also driven and observed (13/1 → 13/0), by D4 rather than by a tap | TEST 7 + run-3/5 control logs |
| 7 FRLG directional stair | **host-proven** on a real FR map | TEST 8 |
| 8 water refusal | **host-proven, before and after** | TEST 9 |
| 9 NPC replan | **host-proven** (TEST 5 NPC case) **and live** (run 3: plan → replan → replan → stalled, never a third) | run 3 plan log |
| 10 map-change abort | **live**: `pElev` and the plan rows follow the map across 13/1 → 13/0 and the route does not survive it | run 3 plan log |
| 11 `WK_NONE` unchanged | **host-proven** (TEST 13, incl. the tie-break) **and live** (every `kind=none` row) | run 2/3 plan logs |
| **the DOOR warp firing, live** | **NOT YET SHOWN** — see below | — |

**What is genuinely not proven yet, stated plainly.** No emulator run has yet caught a `kind=door`
row followed by `end … mapchange`. That is not for want of the code working — it is that the
FireRed save the fixtures carry starts inside the **Indigo Plateau Cable Club (13/1)**, whose two
`0x69` doors sit behind an impassable counter row and are script-only (`D4` confirms: every
`U1` into that alcove **times out**, so the tiles are unreachable by walking and the router
correctly returns `unreachable` rather than inventing a route). Getting to a normal building door
means walking two maps, and each attempt costs a ~10-minute boot. Run 6 removes the walk entirely
by moving the fixture save to Indigo Plateau (3/9) at (11,7) — standing **directly below** the
Pokémon Center door at (11,6), i.e. the user's exact sentence — and taps north five times.

**Hardware is still the gate regardless** (CLAUDE.md #6): `TERM_FRAMES = 30` is an emulated-frame
count and has never been counted at the app's real frame rate, let alone at the ~4–5 emu-fps of a
wireless session (SPEC-door T4.5.4 / Open Q6). The on-SD plan log exists precisely so the next
hardware run answers it: a door tap must read `kind=door, term=U`, an approach one tile **south**
of the goal, and end **`mapchange`** — never `timeout`.

### Files

| file | |
|---|---|
| `source/fieldpath.h` / `source/fieldpath.c` | **new** — the classifier + the elevation-correct planner (pure C) |
| `source/touch.c` | `plan_bfs`/`walkable` removed; terminal hold, warp kill-switch, bounded replan, `g_fieldDbg`, the SD plan log |
| `source/touch.h` | `TouchSmart.mapGroup/mapNum`; the `FieldDbg` mirror + `FDBG_END_*` |
| `source/gamestate.h` / `.c` | `GameProfile.mapHeaderPath` appended, three VERIFIED-SYM values |
| `source/main.c` | one line: `sm.mapGroup/sm.mapNum` from the state read already being done |
| `test/host/test_fieldpath.c` | **new** — TEST 1–13, 1788 checks |
| `test/host/fixtures_fieldpath.h` | **new, generated** — five real maps out of the user's ROMs |
| `docs/phase18-crisp/evidence/door/` | the three live plan logs + `survey.py`, `genfix.py`, `place.py`, the run-6 movie |

`celiolink.c`, `netlink.c`, `gbacore.c` — **empty diff**, as required. No render path, no theme, no
draw call was touched: this slice draws nothing, so all six themes are unaffected by construction.

### Run 6 — the fixture-save placement works, and the tap resolves to the door

Runs 1–5 never reached a normal building door because the FireRed save starts inside the Cable
Club. Run 6 removed the walk: `place.py` moved the **fixture** save (a copy) to
`IndigoPlateau (3/9)` at **(11,7)** — standing directly below the Pokémon Center door at **(11,6)**,
which is the user's sentence exactly — and the movie taps north five times.

```
row,frame,map,px,py,goal,approach,kind,term,beh,pathLen,pElev,warp,outcome,head,end
plan,2217,3/9,11,7,11/6,11/6,none,-,-1,0,0,-1/-1,badmap,0,-
plan,2931,3/9,11,7,11/6,11/6,none,-,-1,0,0,-1/-1,badmap,0,-
plan,3958,3/9,11,7,11/6,11/6,none,-,-1,0,0,-1/-1,badmap,0,-
plan,4940,3/9,11,7,11/6,11/6,none,-,-1,0,0,-1/-1,badmap,0,-
plan,5655,3/9,11,7,11/6,11/6,none,-,-1,0,0,-1/-1,badmap,0,-
```

Two things are proven and one is not:

* **The save surgery works** — the game loaded on map **3/9 at (11,7)**, the tile chosen.
* **The tap → world-tile arithmetic is exactly right** — every tap resolved to goal **(11,6)**, the
  door tile, with zero ambiguity.
* **But the game was still on its main menu**, not in the overworld: `VMap` was empty, so
  `map_read` failed and the router did the correct thing — `badmap`, `beh=-1`, **zero keys**. (The
  live `3/9 (11,7)` reading comes from `SaveBlock1`, which FRLG populates while the CONTINUE panel
  is up — the same trap run 1 hit.) The D4 intro script `s W150 s W150 a W600` spent its A on
  opening the main menu and had none left to select CONTINUE.

That is a *test-harness* miss, not a code miss, and the log says so unambiguously — which is the
whole point of shipping the plan row.

### One post-proof hardening edit

`walk_plan`'s `FpPlan` was a **stack** local carrying the 4225-entry path array (~4.3 KB), sitting
deep inside the per-frame render-thread call chain. It survived seven Azahar boots, but Azahar is
more forgiving about stack headroom than a real 3DS thread, and CLAUDE.md #6 says hardware is the
gate — so it is now `static`, which is safe by construction (`touch_update` runs only on the
main/render thread; workers never touch this path, CLAUDE.md #2). Storage class only, no behaviour
change; suites and build re-run clean afterwards.

### Run 7 — the same wall, and the honest conclusion

Same placement, a longer intro script (`s W120 s W120 a W120 a W120 a W120 a W400` — two STARTs and
**four** A presses). Identical result:

```
plan,3081,3/9,11,7,11/6,11/6,none,-,-1,0,0,-1/-1,badmap,0,-
plan,4299,3/9,11,7,11/6,11/6,none,-,-1,0,0,-1/-1,badmap,0,-
plan,5416,3/9,11,7,11/6,11/6,none,-,-1,0,0,-1/-1,badmap,0,-
plan,6534,3/9,11,7,11/6,11/6,none,-,-1,0,0,-1/-1,badmap,0,-
plan,7658,3/9,11,7,11/6,11/6,none,-,-1,0,0,-1/-1,badmap,0,-
```

`VMap` never became valid across 7,658 emulated GBA frames, so FireRed never entered a loaded map.
Two candidates remain and the run cannot tell them apart from outside: the D4 A presses are not
selecting CONTINUE on FRLG's main menu (they demonstrably *do* work — runs 3–5 reached the
overworld with the same token grammar), or the relocated `location` makes the continue-time map
load fail (the patch sets `location` + `pos` but leaves `warp1`/`warp2`/`lastHealLocation` pointing
at the Cable Club, and pokefirered's continue path may consult those). **Stopping here rather than
guessing**: the next attempt should patch the whole warp block, or — cheaper — walk out of the club
once by hand and let the game write its own save.

**So: the live door warp is the one T6 case still open.** Everything else in this slice is proven,
most of it twice. Calling it done would be the exact "validated only in Indigo" failure the phase
rules forbid, so it is written down as open, with the instrument (`kind=door`, `term=U`, approach
one tile **south**, `end … mapchange`) already in the build for whoever gets there first — on
hardware, that is the same row the user's own run will print.

### Cleanup

`azctl stop` (profile restored byte-identically, CLEAN), `azctl clean-fixtures`
(*"originals re-hashed, all untouched"*, `sdmc:/3DGBA` ROM-less again, the user's `recent.bin`
restored), and the app's `settings.bin` restored from the backup taken before the first boot —
`touchMode` is back to **0 (Off)**, the user's own value. `sdmc/dual-gba/` was never written; the
only save ever edited was the staged **copy**, which `clean-fixtures` has now deleted.

---

## 2026-08-13 — S3 (coop): same-console co-op — the RS profiles, the explanation, the `.sav` guard

**Scope:** the user's ask #3 — *"the co-op on same console doesnt work, lets start by making same
game work (emerald&emerald, ruby&sapphire and firered&leafgreen)"* — per `SPEC-coop.md`.

### The headline

Two of the user's three pairs are **proven working on the emulator this session**, with state reads:

| pair | verdict | evidence |
|---|---|---|
| **Emerald + Emerald** | ✅ draws | `reason 0/0, drawn 1/1, live 2/2, gameId 1/1, artOk 1`, **108 avatar quads blitted** at screen (152,96) |
| **FireRed + FireRed** (the FR+LG proxy) | ✅ draws | `reason 0/0, drawn 1/1, gameId 2/2`, **504 quads** |
| **Ruby + Sapphire** | 🟡 **built, VERIFY-ON-HW** | 30 addresses VERIFIED-SYM ×4 maps + host-suite proof; **no RS ROM exists on this machine** |
| Emerald + FireRed (the user's actual carts) | ✅ refuses, **and now says why** | `reason 5/5, pairReason 2, toastTimer 179 at session frame 856` |

**The spec's verdict was right and this slice confirmed it: presence was never broken.** The user
could not reach a working state, for four separate reasons, and all four are now fixed.

### Every RS address was re-derived, not trusted

SPEC-coop §P3.3 is a table of 30 addresses. The house rule forbids shipping any of them on someone
else's word, so all four of pret's byte-matched RS symbol maps were downloaded again and diffed:

```
RAM diffs ruby vs sapphire:      0 (of 727 syms)
RAM diffs ruby vs ruby_rev1:     0
RAM diffs ruby vs sapphire_rev1: 0
AGREE gSaveBlock1 02025734 x4   AGREE gMain        03001770 x4   AGREE gObjectEvents 030048a0 x4
AGREE gSaveBlock2 02024ea4 x4   AGREE gTasks       03004b20 x4   AGREE gFieldCamera  03004880 x4
AGREE gMapHeader  0202e828 x4   AGREE BattleMainCB2 0800f808 x4  ... (26 symbols, all AGREE)
```

Every spec value verified. The struct **layouts** were read from `pret/pokeruby master` too
(`global.h`, `main.h`, `global.fieldmap.h`, `field_camera.h`) and are byte-identical to Emerald's
for every field this app touches — including `Tileset.metatileAttributes` at `+0x10` as a `const
u16*`, which is the RSE shape `fieldpath.c` assumes, and `pokeruby`'s
`metatile_behaviors.h`, whose numbering matches the RSE table **row for row** (0x69
`MB_ANIMATED_DOOR`, the whole 0x60–0x6D block, 0x0E/0x0F/0x1B/0x1C/0x29). `fp_engine()` routes
`AXVE`/`AXPE` to `FP_ENG_RSE` by its `code[2]` test with no edit, so **S2's door fix works on
Ruby/Sapphire by construction.**

**One spec number was wrong and is corrected here.** P3.1.2 claims "344 of the 394 common-prefix
`(group, num)` slots name a different map". Re-derived per group (which is what a map id actually
is): Ruby has **394** maps in 34 groups, Emerald **518** in 34; they share **393** `(group, num)`
slots and **76** of those name a different map, with the divergence starting at **group 0 index 50**
(`Underwater1` vs `Underwater_Route124`) and shifting everything after it. Group 5 index 1 is
`FallarborTown_ContestLobby` vs `FallarborTown_BattleTentLobby`, as the spec says. The headline
number differs; **the verdict is unchanged and, if anything, better founded** — 19 % of the shared
slots disagree, so `PRES_GAME_HOENN_RS` must be its own id. This is recorded in `presence.h`.

### What shipped

| file | change |
|---|---|
| `source/gamestate.h` | `uint8_t sbDirect` **appended** after `mapHeaderPath` (P3.2.3; the spec said "after `hbCtr`", but S2 had since appended `mapHeaderPath` — appending at the end is the only safe edit) |
| `source/gamestate.c` | **`AXVE` + `AXPE` rows**, one `RS_PROFILE_BODY` macro so the two are literally pinned; explicit `sbDirect = 0` written into all three existing rows; the `sbDirect` branch in `game_read` |
| `source/presence.h` | `PRES_GAME_HOENN_RS = 3`, with the map-table diff as its argument |
| `source/presence.c` | `presence_game_id`: `AXVE`/`AXPE` → `PRES_GAME_HOENN_RS` |
| `source/presence_read.c` | the `sbDirect` branch in `ident_refresh` + **the all-zero-name rejection** (P3.2.5) |
| `source/main.c` | `g_presDiag` + 14 `_Static_assert`s (P1.3); `pres_pair_verdict`/`pres_universe_name`/`pres_pair_toast` (one shared implementation for three surfaces); the **session-start toast**; the **`CO-OP x`** chip suffix; the **LINK tab on the pre-game settings screen** with its six live-session rows drawn disabled; the **blit witness** |
| `source/rompicker.c` | the **same-file-in-both-slots refusal**, guarding all three assignment sites, compared **by resolved file** |
| `README.md` | the two-copies workflow + which pairs can co-op (P2.4.5) |
| `test/host/test_presence.c` | TEST 6 grown: the RS universe through the **real ladder**, both directions |
| `test/host/test_profiles.c` | **new suite, 505 checks** |

`presence_art.c`, `presence_ui.c`, `fieldgate.h` — **untouched** (P2.6.1 freeze).
`celiolink.c`, `netlink.c`, `gbacore.c` — **empty diff**, as every phase requires.

### `test_profiles.c` — the real table, not a copy of it

SPEC-coop P4.6.2 offered two shapes and asked which was done. **Neither, exactly**: the spec's
fallback was "a 20-line test that `#include`s `gamestate.h` and a **copy** of the table". A copy is
a second source of truth that drifts the first time someone edits one of them — the exact bug class
the test exists to catch. `gamestate.c` turned out to host-compile with only **five** undefined
emulator symbols, so the suite **links the shipping `source/gamestate.c`** and drives it through a
fake byte-addressed GBA bus. Every assertion is about the table that ships.

```
TEST 1  profile_for resolves all 5 codes; "AXV"/"AXVX"/"BPEX" match NOTHING (the prefix-match trap)
TEST 2  sbDirect as a table: BPEE/BPRE/BPGE = 0, AXVE/AXPE = 1
TEST 3  sbDirect BEHAVIOUR through the real game_read — both directions
TEST 4  no ROM address in the RS rows except the licensed battleMainCb; every named 0 asserted
TEST 5  AXVE and AXPE differ ONLY in their 4-char code (memcmp of the whole row)
TEST 6  all 30 RS addresses == pret's symbol maps
TEST 7  the all-zero RS name is refused; a real name latches; BPEE unaffected
TEST 8  every column is 0 or a real GBA address; the row spans exactly 49 uint32_t
=== 505 checks, 0 failures ===
```

**Mutation-tested, because a green suite proves nothing until it can go red:**

| mutation | failures |
|---|---|
| `sbDirect` lost on the RS rows | **13** |
| one RS address off by 8 (`0x020239F8` → `0x020239F0`) | **1** |
| `AXPE` unpinned from `AXVE` | **1** |

### The live runs (Azahar 2125.1.2, the real `.3dsx`, the user's real ROMs as staged copies)

`g_presDiag` (P1.3) is what made all of this a one-line read instead of hand-decoding a 148-byte
`GsLogEntry` ring. `magic` read back `0x50525331` = `'PRS1'` on every run, so the offsets are the
ones the harness thinks they are.

**Run 1 — CO-3, the user's ACTUAL carts (BPEE + BPRE), pref on from `settings.bin`:**

```
magic = 0x50525331 ('PRS1' OK)   frame = 856   enabled = 1
  game0: reason=5 (universe) drawn=0 gameId=1(HOENN) selfMap=none
  game1: reason=5 (universe) drawn=0 gameId=2(KANTO) selfMap=none
artOk = 1   pairReason = 2 (PAIR_UNIVERSE)   pairGame = 1(HOENN) / 2(KANTO)
toastTimer = 179          <-- CAUGHT IN ITS WINDOW
blit = N=0                <-- and nothing is ever drawn for a cross-universe pair
```

`toastTimer = 179` on the **first frame of the session**, before either game had even loaded a save
(`selfMap = none`): the returning-user path P2.2.1 exists for, where the pref is already on and the
pause-menu status line can never fire. This is the run that answers the user's "it doesn't work" —
**the app now says why, at the moment it matters, without opening a menu.**

**Run 2 — CO-1, Emerald + Emerald** (staged `gameB` ← a copy of `gameA`, ROM and `.sav`):

```
frame = 1077   enabled = 1
  game0: reason=0 (ok) drawn=1 live=2 gameId=1(HOENN) selfMap=10-6 peerMap=10-6 dTile=+0,+0
  game1: reason=0 (ok) drawn=1 live=2 gameId=1(HOENN) selfMap=10-6 peerMap=10-6 dTile=+0,+0
artOk = 1   pairReason = 0 (PAIR_OK)   toastTimer = 0   <-- correctly NO toast for a working pair
anchor = GBA-frame (120,88)          blit = N=108 at screen (152,96) size 16x32
```

Every P4.2.2 clause passes, including `selfMap[0] == peerMap[1]` and `selfMap[1] == peerMap[0]`.

**Run 3 — the FR+LG proxy (P4.1.4), BPRE + BPRE:** `reason 0/0, drawn 1/1, gameId 2/2 (KANTO),
artOk 1, blit N=504 at (152,96)`. FireRed + LeafGreen itself is **proved by construction plus this
run**, not directly — the user's card has no LeafGreen. The construction half is a host assertion
(`presence_game_id("BPGE") == presence_game_id("BPRE") == PRES_GAME_KANTO`, TEST 6) and this run
exercises the identical `gameId`-equality path plus the FR/LG profile addresses.

**Run 4 — CO-2, position** (both saves patched with the door slice's `place.py`: seat A at map 10-6
(9,4), seat B at 10-6 (12,6) — a known **+3,+2** delta):

```
  game0: gameId=1(HOENN) selfMap=10-6 peerMap=10-6  dTile=+3,+2     <-- exactly the fixture delta
  game1: gameId=1(HOENN) selfMap=10-6 peerMap=10-6  dTile=-3,-2     <-- and its symmetric inverse
blit = N=129 at screen (240,128) size 16x32
```

Steady state then settles on `reason 8 (obj)` / `12 (selfobj)` — the P-G7 object-agreement
discriminator, and it is **correct**: `place.py` moves the *stored* `SaveBlock1` position, but the
running field engine has not put `gObjectEvents[0]` there, so the gate refuses to draw a peer at a
position the world does not back. P-G7 is holdable, which is why 129 quads went out during the
window when they did agree. This is also the cleanest demonstration of what P1.3 bought: **"which
rule refused?" is now one `read-u32`.**

### The blit witness, and the honest limit on visual proof

**Both pixel channels on this machine are dead, and this slice reproduced both.** `see shot`
returned a uniform black 900×540 rectangle while `g_renderSeq` advanced 1301 → 1310 across the
capture (a sixth reproduction of SPEC-crisp Q1); `fbdump` produced coloured vertical stripes even
with a **held** halt across the whole dump and `verify — 0/4096 bytes changed`, confirming S1's
verdict that Azahar 2125.1.2's emulated framebuffer is not a faithful copy of the presented image
(Q2). So **no screen-pixel evidence was available**, through no fault of this slice, and the slice
rule "the avatar VISIBLE in a capture" **cannot be honoured as written today.**

*(Harness finding worth carrying: the broker halts and resumes around **each** read, which is why
`fbdump` tears. Issuing one `{"op":"halt"}` to the broker socket first makes the whole dump a single
frozen snapshot — `verify` then reports 0/4096 changed. It did not rescue the image, but it removes
tearing as a confound for whoever fixes Q2. A long `--timeout` on the broker matters too: the
client's `--timeout` does not propagate to it.)*

So the gap was closed the only honest way: `drawn == 1` says the **gate** opened, which cannot
distinguish a real sprite from one culled, clipped to nothing, or submitted off-screen. The **blit
witness** is incremented on the far side of the actual `C2D_DrawImageAt`, with the screen-space
pixel rect the quad was submitted at. Both runs check out **arithmetically, exactly**:

| run | `dTile` | foot anchor | sprite top-left | screen (SCALE_1X, verified `scaleMode = [0,0]`) | blit reported |
|---|---|---|---|---|---|
| CO-1 | +0,+0 | (120, 88) = `PRES_ANCHOR_X/Y` | (112, 56) | bottom 320×240, ox 40 oy 40 → **(152, 96)** | **(152, 96)** 16×32 |
| CO-2 | +3,+2 | (168, 120) | (160, 88) | top 400×240, ox 80 oy 40 → **(240, 128)** | **(240, 128)** 16×32 |

`16x32` is the full `PRES_CELL_W/H` — nothing was clipped. And CO-1's `blitN = 108` over 54 frames
is exactly **2 per frame = both screens**. That is the strongest available statement that the
avatar is on screen at the expected tile; it is **not** a photograph, and it is not claimed as one.

### Before / after, measured — the `.sav` hazard (P2.4)

Not argued from source: the pre-fix picker was **built and booted** (`/tmp/3DGBA_before.3dsx`) and
run against the identical movie (three `A` presses on the same list row) and the identical fixtures.

```
BEFORE:  nRoms=2 sel=0 mode=0  idxA=0  idxB=0   <-- ONE FILE IN BOTH SLOTS
AFTER:   nRoms=2 sel=0 mode=0  idxA=0  idxB=-1  <-- refused, held across all three presses
         startN=0 in both                            (P4.5.1's exact acceptance)
```

`idxB = 0` is two live mGBA cores holding a writable `VFile` on one `.sav` and flushing whole save
images over each other. The user's "make emerald&emerald work" leads straight into it, so the
picker refuses the assignment and says `SAME FILE IN BOTH SLOTS` / `copy it: game1.gba / game2.gba`
for ~2 s. The comparison is by **resolved file**, not by list row (P2.4.4).

### Suites — all green, none shrinking, one new

| suite | before | after |
|---|---|---|
| test_control | 6897 | 6897 |
| test_diag | 376 | 376 |
| test_fieldpath | 1788 | 1788 |
| test_netlink_reliability | 66 | 66 |
| **test_presence** | 49756 | **49773** |
| test_theme | 82693 | 82693 |
| test_tilt | 1723 | 1723 |
| test_trace_replay | 58 | 58 |
| test_typography | 956 | 956 |
| test_uigeom | 18332 | 18332 |
| test_uihit | 1718 | 1718 |
| **test_profiles** | — | **505 (new)** |
| `tools/emutest/tests/run_host_tests.sh` | 147 | **147 OK** |

Build clean; `main.c` warning count unchanged at 14, all pre-existing (unused statics, three
`-Wmisleading-indentation` on untouched lines). No new warning in any file this slice touched.

### Deviations from SPEC-coop, with reasons

1. **`sbDirect` appended at the END of `GameProfile`, not after `hbCtr`** (P3.2.3). S2 appended
   `mapHeaderPath` in the meantime; inserting mid-struct would silently reassign every positional
   initialiser after it. Appending is the rule the spec itself states, applied to the current tail.
2. **RS `sMenuBase` = 0, not `0x020388B8`** (P3.3.3 listed the address). The RS `sMenu` symbol's
   **size is 4 bytes** (`pokeruby.sym:312`) while `touch.c:510-524` reads `+1/+2/+4/+5/+8` — i.e.
   Emerald's `struct Menu` layout is **not proven** for RS, and a wrong-layout read is what the
   house rule bans. It costs nothing: `gWindowsBase` is 0 and no menu task resolves, so
   `GCTX_FIELDMENU` is unreachable on RS regardless. Degradation named in the source.
3. **RS `mapHeaderPath` = `0x0202E828` is shipped** (the spec's table predates S2's field). Licensed
   because RS's `MapHeader`/`Tileset`/`MAPGRID_*`/behaviour numbering were all verified identical to
   the RSE shape `fieldpath.c` assumes. RS `mapHeader` (the HD-2D depth gate) stays **0** — filling
   it would switch an untested render path on for a new game, which this phase forbids.
4. **The chip suffix is ASCII `x`, not `×`** (P2.2.2). `×` (U+00D7) is not proven present in the
   baked `TXT_CHIP` face, and a missing glyph in a two-character chip is a worse defect than a
   plainer one. Trivially revertible once the bake's coverage is measured.
5. **`test_profiles.c` links the real `gamestate.c`** instead of copying the table (P4.6.2 asked
   which was done). Reason above. It is also numbered TEST 1–8 in its own file rather than "TEST 38"
   — that number is already taken in `test_presence.c`.
6. **`toast[48]` → `toast[64]`.** The longest pair sentence is 45 bytes and 48 would have truncated
   it. Every other toast is far shorter, so nothing else changes.
7. **`g_presDiag` carries five fields the spec did not list** (`pairReason`, `pairGame[2]`,
   `toastTimer`, the blit witness, `anchorX/Y`). `toastTimer` is P4.4.3's own suggestion; the rest
   exist because the capture channel is dead and they are the only way left to assert what the spec
   asked for. All are appended after the specced ladder, so the specced offsets are exactly as
   written.
8. **P2.3's choice (Open Q2) resolved as "grow `TABS[]`"**, not "move the row to ENHANCE". ENHANCE is
   measurably full and moving the row needs plate art that does not exist; growing the tab list needs
   only the disable pass, which is implemented (`set_row_live_only`, one predicate shared by the draw
   pass, the activation switch and the tap handler, so a row cannot look disabled and still fire).
   The disabled scrim is the **theme's own background at ~70 % alpha**, so it fades correctly in all
   six themes by construction rather than by a hard-coded colour.
9. **Open Q1 taken as the spec recommends**: the toast ships now; the picker's `⚡ co-op` marker is
   logged as the follow-up.

### Honest status — what is NOT proven

* **Ruby + Sapphire has never been EXECUTED.** No RS ROM exists on this machine
  (`sdmc:/dual-gba/` is BPEE + BPRE only), so all 30 addresses are **VERIFIED-SYM ×4 maps +
  VERIFIED-SRC layouts, VERIFY-ON-HW**. The cheap gate before anyone calls RS "supported" is P3.6:
  boot Ruby in one core and Sapphire in the other and confirm smart touch's tap-to-walk works on
  both — that exercises `sb1ptr` + `mapObjects` + `mapLayout`, the exact three columns presence
  needs. Under the harness the same proof is `g_presDiag.gameId[0..1] == 3, 3` with `px/py` that
  **change as the player walks**; a static `px/py` is the signature of a wrong `sb1ptr`, and
  `fieldValid = 0` forever is the signature of a missed `sbDirect`. Two RS columns are
  additionally **verify-on-hw-pending** even then: `battleMainCb` (the one shipped ROM address,
  and the only thing keeping a peer off an RS battle screen) and `partyCount`.
* **FireRed + LeafGreen itself** — proved by construction plus a BPRE+BPRE run, per P4.1.4.
* **No screen-pixel evidence exists for anything this slice did.** See the blit-witness section.
  The `CO-OP x` chip, the toast text, the disabled LINK rows and the picker's refusal band were
  **not photographed**; the toast was proven to *fire* (`toastTimer = 179`) and its content is
  determined by two measured values (`pairReason = 2`, `pairGame = HOENN/KANTO`) through a six-line
  pure function, but that last step is read from source, not measured.
* **Everything remains Azahar-only** (CLAUDE.md #6). P5's hardware list is untouched by this slice:
  the frame budget with co-op on, `PRES_ANCHOR_Y`'s 8-px calibration, and `facingDirection` all
  still need a New 3DS.
* **The intro-demo false positive (P1.1.7 / P2.7) was NOT fixed** — it needs a captured `cb2` per
  game, which no run this session produced. Documented behaviour stands: *a peer may be drawn over
  the other game's attract-mode demo.* Both CO-1 and CO-3 ran in exactly that state.

### Hygiene

`azctl stop` after every one of the seven boots (profile restored byte-identically, **CLEAN**);
`azctl clean-fixtures` (*"originals re-hashed, all untouched"*, `sdmc:/3DGBA` ROM-less again, the
user's `recent.bin` restored); `settings.bin` restored **byte-identically** from the snapshot taken
before the first boot (`presence` back to **0**, the user's own value). `sdmc/dual-gba/` was never
written — only the staged copies were, including the two `place.py` save patches, and
`place.py` refuses any path containing `dual-gba` by construction.

---

## 2026-08-13 — FIX PASS: the four review findings, plus the system-font draws phase 18 missed

> Appended, not rewritten (per the 22:13 concurrent-write warning at the top of this file).

**Every finding was re-verified before it was fixed; all four are REAL. Two of them (1 and 2) are
majors and both are fixed. Verifying finding 4 turned up something bigger than the finding: the
S1 slice's headline claim — "every text draw in the app is at texel scale 1.0" — was FALSE for the
HUD game names, the whole `.sav` picker screen, the virtual gamepad's key glyphs and the co-op
toast. Those are the user's complaint #1 in the surfaces they look at most, so they are fixed here
and pinned by a new source-scan test.**

### Per-finding outcomes

| # | file:line | severity | verdict | outcome |
|---|---|---|---|---|
| 1 | `source/touch.c:468` | major | **CONFIRMED** | fixed + host-tested (TEST 14) + mutation-killed |
| 2 | `source/rompicker.c:388` | major | **CONFIRMED** | fixed both sides (read + write) + **live before/after over GDB** |
| 3 | `source/main.c:5019` | minor | **CONFIRMED, and worse than reported** | fixed + host-tested (T11) |
| 4 | `test/host/test_theme.c:103` | minor | **CONFIRMED** | model rebuilt on the ladder (TEST 0), +751 checks |

---

### Finding 1 — the stall replan tripled tap-a-sign latency. CONFIRMED.

**Re-derived, not trusted.** `fieldpath_plan` sets `goalOpen = (out->kind == FP_WK_NONE)` and, for
`FP_WK_NONE`, leaves `approach == goal == the tapped tile` (fieldpath.c:325/333). The BFS therefore
terminates ON a tile the avatar can never stand on, `px == s_appX` never becomes true, `s_pathPos`
sticks at `s_pathLen - 1`, and the stall counter always trips **at the terminal**. The replan then
always succeeds — the same blocked-goal-allowed BFS re-finds a 1-step path from where we already
stand. Frame arithmetic, counted from `walk_update_inner`: trip at 25 → replan (1) → 25 → replan
(2) → 25 → `s_replans < REPLAN_MAX` is now false → `s_aPulse`. **75 frames instead of 25**, with
the direction key emitted every one of them, i.e. a full extra second of the avatar bumping the
sign, plus two extra 4225-node BFS floods on the render thread.

**Fix.** A new pure predicate in `source/fieldpath.h`:

```c
static inline int fieldpath_should_replan(FpKind kind, int pathPos, int pathLen) {
	return !(kind == FP_WK_NONE && pathPos >= pathLen - 1);
}
```

`touch.c` gates the replan on it. Narrower than the reviewer's suggestion on purpose: a **mid-route**
stall on a `WK_NONE` route is still a real obstruction (an NPC that walked into the path) and still
replans — only the terminal is excluded. `DOOR`/`DIR`/`STEP` are untouched at every position,
because their approach is a tile the player genuinely can stand on.

**Proof.** `test_fieldpath` **TEST 14** (+20 checks, 1788 → 1808) drives the real Mauville grid: it
first *measures* the premise (`fieldpath_enterable(approach) == false` for the WK_NONE plan to
(20,4), `pathLen == 4`), then asserts no replan at `pathPos == pathLen-1` or beyond, a replan at
every earlier step, no replan for a zero-length plan, and a replan for all three warp kinds at all
positions. **Mutation-tested:** reverting the predicate to `return 1;` produces 3 failures.

**Not proven live, stated plainly.** The live instrument needs a Gen-3 **overworld** with smart
touch armed; the door slice burned seven boots on exactly that (its runs 6/7 never got past
FireRed's main menu) and hardware is the gate regardless. The row to look for in the on-SD plan log
is a `kind=none` tap followed by `end … stalled` with **no `end … replanned` rows in between** —
the same log the user's own hardware run prints. Runs 1–5 of the door slice show the old shape
(`plan → replanned → replanned → stalled`) for comparison.

---

### Finding 2 — the resume prompt bypassed the same-file save-corruption guard. CONFIRMED, and proven live.

**Re-derived.** `rompicker_run`'s three guarded assignment sites are all **downstream** of
`if (choice == 1) { …; return true; }`. `load_recent` applied no such rule, and `main.c` never
compares `pathA` to `pathB` before `run_session`. A `recent.bin` written by any pre-fix build could
therefore put one file in both slots, one tap into the boot — and `gbacore_load_rom` derives the
`.sav` from the ROM path, so that is two live mGBA cores holding a writable `VFile` on one save
image. The user's own words are "emerald & emerald", which is the path straight into it.

**Fix.** One predicate, `recent_same_file()`, used by **both** ends: `load_recent` degrades the
loaded pairing to a single game (and counts it), and `rompicker_save_recent` refuses to persist the
shape at all, so the file cannot be written that way again. Degrade rather than refuse the prompt:
the resume stays one button, and the prompt already renders slot B as "(single mode)" in the dim
ink. Two appended `PickDiag` fields (`recentDup` 0x38, `recentB` 0x3c — every existing offset
unchanged) make it readable from outside; they are carried across `rompicker_run`'s `memset` so
"the guard fired, then the user picked new games" cannot read as "it never fired".

**Proof — live, both directions, same instrumented build.** A hostile `recent.bin` was staged
(both slots = `sdmc:/3DGBA/gameA.gba`, 512 B) against harness fixtures, and the app booted to the
resume prompt in Azahar 2125.1.2 twice. Only `load_recent`'s guard differs between the two boots.

```
                        magic        recentDup(0x38)  recentB(0x3c)  g_renderSeq
BEFORE (guard disabled) 0x50494b31   0                1              676
AFTER  (guard on)       0x50494b31   1                0             1049
```

`recentB = 1` in the BEFORE run is the defect itself, read out of the running app: the prompt is
holding a two-slot pairing whose slots name the same file. `recentDup = 1, recentB = 0` in the
AFTER run is the guard firing and the pairing degrading. `magic` reads `'PIK1'` in both, so the
offsets are the ones claimed.

---

### Finding 3 — the LINK explanation had its glyphs eaten. CONFIRMED — and it was worse than reported.

The reviewer said the descenders were clipped. **Measured** from the shipped `data/fnt_jbm_med_9.bin`
through `tools/fontlab`, rendering the actual string at texel scale 1.0:

```
lineFeed 9  cellW 8  cellH 13  baseline 9
"Link actions need a running game"  ->  ink on line-box rows 3..9  (width 128 px)
row 8 = the BASELINE row, inked across the whole string
row 9 = the 'g' tails of "running" and "game"
```

Drawn at `y = 220`, that is absolute rows **223..229**. `menu_draw_chrome` repaints the plate
background over `y >= UIHIT_MENU_VIEW_H (228)` one call later, so it took **the baseline row of
every glyph** plus the descenders — not just the tails. And `PT_LINK`'s content is 214 px tall, so
`uihit_max_scroll` is 0 and the line could never scroll clear.

**Fix.** `y = 215` (ink 218..224: one px below the last row's bottom edge of 214, three clear of
the repaint). The y and the string moved into `uihit.h` as `SET_LINK_NOTE_Y` /
`SET_LINK_DISABLED_NOTE` so the host suite grades the **shipped** values.

**Proof.** `test_typography` **T11** re-reads `cellHeight` out of the font file and asserts
`y + cellH <= UIHIT_MENU_VIEW_H` **and** `y >= 214`. **Mutation-tested:** restoring 220 gives
`T11 … line box y 220..233 overlaps the chrome repaint at y >= 228`. T10 additionally holds the
string (128 px) inside the 219 px content column.

---

### Finding 4 — test_theme's draw-scale model was stale. CONFIRMED, and rebuilt on the ladder.

The `SC_*` macros divided by 14/13/9/10 — phase-17's *wrong* "native px" (the real lineFeeds were
19/17/12/14) — and the `INV[]` rows still carried phase-17 draw sizes for roles that had moved.
The reviewer's error-direction analysis is right (the stale scales grade *more strictly*, so no
theme was ever passed that would fail), but the header's claim was false and the D11 guard was
decoupled.

**Fix.** Every inventory row now names the **role** it draws through (`#include typography.h`);
`sysScale > 0` marks the rows that genuinely still bypass the ladder. Baked rows grade at
`SC_BAKED = 1.0`, cited to `test_typography` T1/T2/T3 rather than re-derived from the same table
(re-deriving is exactly how a column can be wrong AND green). One new inventory row — the dev
net-diag stat line at 0.40 — keeps the coverage term, and therefore the D11 mechanism, modelled on
the only draws that can still exhibit it.

**New TEST 0** grades the column itself: a system row must not name a rung and must still be
coverage-penalised; a baked row must name a real face and grade at 1.0; and **every one of the
seven rungs must be exercised by at least one row**. `test_theme` 82693 → **83444 checks**.
**Mutation-tested:** moving the one `TXT_VALUE` row to `TXT_SECTION` → *"no inventory row draws
through ladder rung 6"*; giving the system row a rung → *"a system-font row must not name a ladder
rung"*.

**Deliberately NOT added to the inventory:** the phase-15 CO-OP readout (system font at 0.32 over
raw game pixels). It has no scrim of its own, so there is no honest base colour to grade it
against; inventing a mid-grey would produce a number that means nothing. Named in the header
instead of hidden.

---

### Beyond the findings — the system-font draws phase 18's S1 slice missed

Enumerating every call site for finding 4 turned up that S1 converted `assets_text` call sites but
not **raw** `ui_text` / `C2D_DrawText` draws. Four of them were live user-facing chrome:

| site | was | now |
|---|---|---|
| the HUD bar's two **game names** (`main.c`, top + bottom) | system font @ 0.40, always on screen while playing | `assets_text(TXT_BODY)` at y=0 |
| the whole **`.sav` picker screen** (`rompicker.c savpicker_run`) | system font @ 0.6 / 0.5 / 0.45 | `TXT_TITLE` / `TXT_BODY` / `TXT_SECTION` |
| the **virtual gamepad's key glyphs** A/B/L/R/START (`touch.c pad_label`) | system font @ 0.42 | `TXT_BUTTON`, centred on the role px |
| the **toast**, including phase 18's own co-op explanation (`main.c`) | system font @ 0.5 | `TXT_BODY` |

Widths were **measured from the shipped font bytes** before the change, not eyeballed, and every one
is now a T10 row: `"Pokemon LeafGreen"` 89 px (HUD name, before the right-hand cluster at ~230),
`"START"` 28 px in the 64 px key, `"Pick a .sav to load"` 117 px of 392, the longest toast
`"Co-op: Hoenn RS vs Hoenn RS — no peer possible"` **218 px of 384** (against ~330 px at the old
0.5 system size — the baked face is *narrower*, so `pres_pair_toast`'s length constraint went from
estimated to exact, and its stale comment was corrected).

Also deleted: `slot_card` in `rompicker.c` — dead since phase 17 (the plate art draws the slot
cards), warned as unused on every build, and holder of the last two non-fallback `ui_text` draws.
**Build warnings 25 → 24; no new warning anywhere.**

**New TEST 12 — the R1 bypass budget.** R1 is enforced *inside* `assets_text` (the role carries the
size), so the only way to put blurry text back is to bypass it. T12 parses each file (skipping
comments and string literals, so the app's own explanatory comments are not counted, and
`ui_text_w` — a measurement, not a draw — is correctly excluded) and pins the count:

```
source/main.c        13   assets_ready() fallbacks + the dev diag readouts
source/rompicker.c    0   picker, resume prompt, empty state and .sav picker are all baked
source/wireless.c     0   the whole lobby is baked
source/touch.c        0   pad key glyphs and both chips are baked
source/presence_ui.c  0   presence chrome is baked
```

main.c's 13 are all justified in the test's own comment: seven `ui_text` + one `ui_text_r` are the
presence card's `!assets_ready()` fallback (there is no baked font to fall back TO in that build),
two `ui_text_c` are the load-error and PAUSED fallbacks, three `C2D_DrawText` are the two net-diag
stat lines and the `#if`'d gamestate probe — dev readouts cramming ~72 characters into 400 px,
which no 12 px face holds. **Mutation-tested:** adding one `ui_text` to `touch.c` →
*"source/touch.c has 1 system-font text draws, budget 0"*.

**Live confirmation the ladder survived all of it** (Azahar, real `.3dsx`, over GDB):

```
g_txtTexelScale[0..6] = 3f800000 x7   -> 1.0f, all seven roles
g_txtLineFeed[0..6]   = 17 12 12 10 9 7 11
g_renderSeq            = 1049 (advancing)
```

### Suites — all twelve green, none shrinking, three grew

| suite | before | after |
|---|---|---|
| test_control | 6897 | 6897 |
| test_diag | 376 | 376 |
| **test_fieldpath** | 1788 | **1808** (TEST 14) |
| test_netlink_reliability | PASS | PASS |
| test_presence | 49773 | 49773 |
| test_profiles | 505 | 505 |
| **test_theme** | 82693 | **83444** (TEST 0) |
| test_tilt | 1723 | 1723 |
| test_trace_replay | PASS | PASS |
| **test_typography** | 956 | **981** (T11, T12, 12 new T10 rows) |
| test_uigeom | 18332 | 18332 |
| test_uihit | 1718 | 1718 |
| `tools/emutest/tests/run_host_tests.sh` | 147 OK | **147 OK** |

`git diff --stat source/celiolink.c source/netlink.c source/gbacore.c` is **empty** — the phase
13–18 invariant holds. `make -j8` clean. No render pass, no theme token and no HD-2D code was
touched, so all six themes are unaffected by construction; the only colour-carrying change is
`test_theme`'s model, which now grades **more** of the app (all seven rungs) than before.

### Honest status

* **Finding 1 has no live emulator proof** — it needs a Gen-3 overworld with smart touch armed;
  host-proven and mutation-killed instead, with the exact plan-log signature written down above.
* **No pixel evidence exists for anything here.** `see shot` returned a uniform-black 900x540 and
  720x540 crop (1 distinct colour each) while `g_renderSeq` advanced — the **seventh** reproduction
  of SPEC-crisp Q1 on this machine. The black PNGs were deleted rather than filed as evidence.
  Every claim above is a state read, a font-byte measurement or a host assertion.
* **Hardware is still the gate** (CLAUDE.md #6). Nothing here changes timing, but the HUD/pad/
  toast font conversions are a *look* change that only a photograph can finally judge.

### Hygiene

Three boots, each ended with `azctl stop` → *"fixtures: originals re-hashed, all untouched"* and
*"restored qt-config.ini byte-identically (CLEAN)"*. `azctl clean-fixtures` afterwards: 4 fixture
files removed, `sdmc:/3DGBA` ROM-less again, and **the user's own `recent.bin` restored** (verified
by hand: `A=sdmc:/3DGBA/gameA.gba`, `B=sdmc:/3DGBA/gameB.gba` — two different files). The hostile
`recent.bin` used for the before/after existed only between the staging and the clean.
`sdmc/dual-gba/` was never written. The BEFORE binary was a one-line mutant of the shipping tree,
built to `/tmp/3DGBA_before.3dsx`; the tree was restored and rebuilt afterwards.

---

## 2026-08-13 — S4: THE SECOND CAUSE OF THE BLUR (the bake), + live pixel evidence at last

> Appended, not rewritten.

**Headline: the user's complaint #1 was still only half fixed, and the remaining half was
measurable. `VERIFY.md` V1.4 found it (the bake is soft); this slice fixes it, and — because
the same session unblocked screen capture — it is the first phase-18 slice with BEFORE/AFTER
evidence taken off a real framebuffer instead of a Python re-implementation.**

### Re-verified before touching anything

`VERIFY.md`'s V1.4 table was reproduced independently (my own reader over `data/fnt_*.bin`,
letters+digits): `jbm_med_7` **0.0 %** fully-opaque texels / 87.7 % stranded 25–75 %,
`jbm_med_9` **0.0 %** / 75.6 %, `sg_med_10` 0.4 % / 66.1 %, `sg_med_12` 2.0 % / 63.9 %.
Numbers agree exactly. `jbm_med_7`'s modal alpha is nibble **8 of 15** — the face is 50 % grey
by construction, and phase 18's scale-1.0 work made the app reproduce that *perfectly*.

`mkbcfnt --help` (v2.3.0) confirms the bake tool has **no** hinting / gamma / contrast switch:
only `-o -s -b -w`. So the lever is post-bake.

### What shipped

| file | change |
|---|---|
| `tools/fontlab/sharpen.py` **(new)** | The stem-snap pass. Per face: a **16-entry monotone alpha LUT** (a bcfnt sheet is A4, so 16 entries is the COMPLETE space of order-preserving alpha edits). Two stages — per-face gain to a `--ceil-pct` (95) percentile of the face's own lit alphas, then a symmetric power S-curve about `--pivot` 0.5 with `--gamma` 2.2. `apply` / `report` / `check` subcommands. |
| `tools/build_assets.sh` | Runs `sharpen.py apply --force` after the seven `bake_checked` rungs, and fails the bake if a face stays under the solidity floor. |
| `test/host/test_typography.c` | **T13** — decodes the shipped sheets (8×8 Morton detile added) and pins the outcome in the bytes: every face >5 % fully-opaque, ≥30 % of its ink at ≥75 % alpha, <50 % stranded. 981 → **1016** checks. |
| `tools/emutest/azctl.py` | Pins `Layout/filter_mode=false` (NEAREST window filter) — this is what makes the framebuffer recoverable from a `see shot`. |
| `tools/emutest/see.py` | `display_asleep()` preflight → **SKIP 75**, plus `--wake`. |
| `tools/emutest/{native,ramp}.py` | Promoted out of `runs/p18-verify/` into the harness proper. |
| `tools/emutest/tests/test_see_rec.py` | The uniform-verdict tests now stub BOTH environment probes (they read the real display state before, so they failed whenever the developer's display slept), + 2 new tests: the sleeping-display SKIP, and that the check runs BEFORE `find_window()`. 147 → **149**. |

Chosen by grid search over (ceil-pct, gamma) with rendered strips read by eye at 7× as well as
by metric; `g2.2 / c95` was the knee — `c85` began to look blocky and close counters, `g1.6`
left mid-tones behind. **Not idempotent by construction** (the gain renormalises against the
face's own distribution), so `apply` refuses a face that already clears the floor unless
`--force`; the bake forces because mkbcfnt rasterises fresh every time.

### Result in the shipped bytes

```
face          opaque       solid(>=75%)   stranded      ink mass
jbm_med_7     0.0 -> 35.3   1.3 -> 70.4   87.7 -> 22.0    x1.80
jbm_med_9     0.0 -> 26.7   3.4 -> 44.9   75.6 -> 41.4    x1.37
sg_med_10     0.4 -> 16.3  15.2 -> 38.4   66.1 -> 42.0    x1.16
sg_med_12     2.0 -> 21.8  19.3 -> 39.1   63.9 -> 44.1    x1.07
sg_bold_12   11.1 -> 29.5  30.2 -> 45.9   54.8 -> 34.7    x1.01
sg_bold_17   30.6 -> 45.3  42.2 -> 59.6   38.8 -> 21.3    x0.98
jbm_bold_11  11.3 -> 28.1  32.1 -> 48.9   49.8 -> 30.5    x1.01
```

The bold cuts harden at ~×1.0 ink = pure edge gain, no bolding. Only the 7 px face gains real
weight, which is what stem snapping at that size means. **T13 mutation-killed**: restoring the
pre-sharpen `fnt_jbm_med_7.bin` produces 3 failures naming the exact defect.

### THE HARNESS FIX THAT UNBLOCKED EVERYTHING — and a correction to VERIFY

`VERIFY.md` V-H1 is right that the black captures were a **sleeping display**, and wrong that
`nohup caffeinate -u -t <secs> &` is "the whole fix". It is not reliable: this session had
**four live `caffeinate` processes and the display asleep anyway**, and `see` correctly SKIPped
(my new guard caught its own bug class in the wild, twice, within minutes of being written).
What works is a **short SYNCHRONOUS `caffeinate -u -t 3` immediately before the grab** — now
`see shot --wake` / `see rec --wake`.

Also: with `filter_mode=false` pinned, `native.py --verify` reports ~0.8 % of device pixels
differing from a nearest re-expansion, not the 0 that V-H2 claims. Good enough for visual
comparison and for band-level metrics; **do not** restate "byte-exact" without re-deriving it.

### Live A/B — the first real pixel evidence of this phase

The phase-17 binary was rebuilt from HEAD in a `git worktree` (3,822,776 B) and both builds were
driven by the **same synthesized movie** to the same pause screen, captured with the same pins.
Diffing the two 320×240 / 400×240 reconstructions isolates exactly which pixels are font-drawn
(the rest is baked plate art — V1.5 confirmed on live pixels, and it is why the first crops I
took looked identical: I had cropped plate art).

`ramp.py`, same region, both builds:

```
region                                    solid>=75%        mean ramp
top-screen status pills                  54.3 -> 80.2 %   2.54 -> 1.45 px
pause footer hint (TXT_CHIP)             12.2 -> 50.5 %   2.61 -> 1.45 px
DIORAMA-TILT sub-label (TXT_CHIP)        10.1 -> 48.0 %   2.34 -> 1.57 px
Off/Low/Mid/Max segments                 67.1 -> 72.9 %   2.39 -> 2.21 px
```

Sheets: `evidence/sheet-crisp.html` (on-screen) and `evidence/sheet-stemsnap.html` (font bytes).
Captures + crops in `evidence/live/`.

### Independent re-verification of the other two slices (nothing taken on trust)

* **DOOR.** Fetched `pret/pokeemerald` `src/field_control_avatar.c` and read it: `TryDoorWarp`
  really is gated `if (direction == DIR_NORTH)`, and `TryArrowWarp` really does require
  `IsArrowWarpMetatileBehavior(behaviour, direction)` — i.e. stand on it and hold. The shipped
  tables agree row for row (`0x69` → `FP_WK_DOOR` + `FP_U`; `0x65` `MB_SOUTH_ARROW_WARP` →
  `FP_WK_DIR` + `FP_D`) and `classify` sets `approachY = gy + 1`. **S2's model is correct.**
* **CO-OP.** Re-downloaded `pokeruby.sym` / `pokesapphire.sym` / `pokeruby_rev1.sym` and
  reverse-looked-up **every** address in the RS row: all 18 checked resolve to exactly the
  symbol their comment claims (`gSaveBlock1` `0x02025734`, `gSaveBlock2` `0x02024EA4`,
  `gMapHeader` `0x0202E828`, `gObjectEvents` `0x030048A0`, `gTasks` `0x03004B20`,
  `gPlayerPartyCount` `0x03004350`, `gMenuCallback` `0x03004AE8`, `BattleMainCB2` `0x0800F808`,
  `gBackupMapLayout` `0x03004870`, `sMessageBoxMode` `0x030005A8`, `gLinkStatus`, …), and the
  RAM space of the three maps is **identical** (729 symbols, zero diff). The `gMain+off`
  columns check out as offsets of `gMain 0x03001770`. **VERIFIED-SYM stands. Still never
  executed** — no RS ROM on this machine.

### Final gate

* Twelve app suites green: control 6897, diag 376, fieldpath 1808, netlink 66, presence 49773,
  profiles 505, theme 83444, tilt 1723, trace 58, **typography 1016**, uigeom 18332, uihit 1718.
* Harness host tests **149 OK** (was 147).
* `make -j8` + `make cia` clean. `3DGBA.3dsx` 5,417,868 B, **`3DGBA.cia` 2,128,832 B**.
* **Warning audit done properly**: built this tree and the phase-17 commit side by side and
  diffed the warning sets (ours only, mgba excluded). Identical except one *removed*
  (`slot_card`). **Zero new warnings** — not taken on faith.
* `git diff` on `celiolink.c`, `netlink.c`, `gbacore.c/.h`, `tilt.c` — **all empty**.
* Hygiene: three boots, every one ended `azctl stop` → "restored qt-config.ini byte-identically
  (CLEAN)". ROM-less Tier-A only; `sdmc/dual-gba/` never touched; netlogs not wiped. The
  phase-17 worktree is scratch-only (`external/` symlinked, never written).

### Not done

* No live overworld door run (see RESULTS.md for why, and for what to check on device).
* SPEC-crisp S4 (the bcfnt re-sheet tool, −1.7 MB) still untouched and still optional.
* `VERIFY.md` stops mid-V2 — it was cut off before reaching co-op. Its V1 findings are all
  addressed here; its V2/V3 sections were re-derived independently above instead.
