# PHASE 19 / SPEC-legible — RESULTS

> **The request, verbatim:** *"Make the text bigger in the app, everything is simply too small to be
> written clearly."*

Every rung of the type ladder moved up one to three steps. Six of the seven roles gain **1–3 pixels of
cap height (+10 % to +75 %)**; the app's body text now matches the ISO "preferred" 20-arcmin line, and
the worst offender — the 4-pixel chip label — is gone. Sharpness did not move: every role still draws
at texel scale **exactly 1.0** on an integer origin, verified on the shipped binary.

---

## READ THIS FIRST — WHAT WAS **NOT** ACHIEVED

1. **Hardware sign-off is OPEN.** CLAUDE.md rule #6 is binding and this phase has not cleared it.
   Every measurement here comes from the shipped font bytes and from Azahar. Azahar renders the
   320×240 panel into ~720×540 (a 2.25× non-integer upscale), so it can prove **size** and
   **geometry** but never the final look on your panels. **You are the gate.** The checklist is in
   `HANDOFF.md`; the one question that matters is *"can you read the chips and the hint line without
   leaning in?"*
2. **Two rungs knowingly sit under the ISO floor.** `TXT_SECTION` and `TXT_CHIP` land at **15.3′**
   against a 16′ minimum. This is a deliberate, measured trade — see *What had to stay smaller*
   below. Nothing you **read** sits there; only captions, key legends, codes and readouts.
3. **`TXT_VALUE` did not get bigger.** Its 11 → 12 px move is a **line-height** change, not a size
   change: JetBrains Mono Bold rasterises to cap 7 at both. It is listed in the ladder for honesty,
   not as a win. Reason below.
4. **One string still nearly fills its box.** `"Preview Gamepad"` measures 103 px in a 106 px button
   (97 %). It fits, and the suite grades it every run, but it has no slack left.
5. **The "after" capture of the empty ROM picker is banked, not freshly re-shot.** Reaching that
   screen needs the ROM-less SD state, and a concurrent session had ROM fixtures staged; I would not
   disturb another session's harness state. It is covered instead by the identity proof below.
6. **The binary size delta is not purely this phase's.** The working tree is shared with a concurrent
   phase-20 "peersprite" slice whose code is also in the measured `.3dsx` / `.cia`. The **`data/`**
   figure *is* purely this phase (fonts only).

---

## 1. THE LADDER — OLD vs NEW

Cap height is what the ergonomics standards specify (not em size, not line height), so cap height is
what the table designs to. `arcmin @30 cm` is on the **binding** panel: the 132.5 ppi **non-XL bottom
screen**, where one device pixel = 0.1905 mm = 2.183 arcmin (SPEC-legible L1.1.1–L1.1.3). An XL owner
sees every one of these 1.39× larger.

| role | phase 18 face | phase 19 face | cap px | Δ | arcmin @30 cm | vs 16′ floor |
|---|---|---|---|---|---|---|
| `TXT_TITLE`   | sg-bold 17  | **sg-bold 19**  | 10 → **11** | +10 % | 21.8′ → **24.0′** | ✅ generous |
| `TXT_BUTTON`  | sg-bold 12  | **sg-bold 15**  |  7 → **9**  | +29 % | 15.3′ → **19.6′** | ✅ preferred |
| `TXT_BODY`    | sg-med 12   | **sg-med 15**   |  7 → **9**  | +29 % | 15.3′ → **19.6′** | ✅ preferred |
| `TXT_SEG`     | sg-med 10   | **= TXT_BODY**  |  6 → **9**  | +50 % | 13.1′ → **19.6′** | ✅ preferred |
| `TXT_SECTION` | jbm-med 9   | **jbm-med 12**  |  5 → **7**  | +40 % | 10.9′ → **15.3′** | ⚠️ knowing deviation |
| `TXT_CHIP`    | jbm-med 7   | **= TXT_SECTION**|  4 → **7**  | **+75 %** | 8.7′ → **15.3′** | ⚠️ knowing deviation |
| `TXT_VALUE`   | jbm-bold 11 | jbm-bold 12     |  7 → **7**  | **0 %** | 15.3′ → 15.3′ | ⚠️ not a size step |

### Why these numbers, and not "a bit bigger"

Three independent anchors were computed and reconciled (SPEC-legible L1); two of them decide it:

* **Physics (L1.1).** ISO 9241-303 and ANSI/HFES 100 both put the floor for screen text at **cap ≥ 16
  arcmin**, with **20–22′ preferred**. On the binding panel that is **7.33 px minimum, 9.16 px
  preferred**. Under the phase-18 ladder **six of seven rungs failed the floor**, and `TXT_CHIP` at
  **8.7′ was 54 % of the minimum** — roughly the angular size of the threshold letter on a 20/40
  acuity line, i.e. near the limit of what normal vision can resolve *at all*. That row was the
  complaint.
* **The game's own text (L1.2) — the anchor that settles it.** You read Pokémon Emerald on this exact
  panel without complaint. Measured from the real 240×160 framebuffer, the Gen-3 body font is **cap 9
  GBA px**; aspect-fitted to the bottom screen that is **cap 12 device px (26.2′)**. The phase-18 UI
  drew its body at cap 7 (**58 %** of the game text) and its chips at cap 4 (**33 %**). No reading of
  "chrome may be smaller than content" survives a 3× ratio. The target this sets — **body cap ≥ 9 px,
  chrome at most one rung under body, never two** — is exactly what the new ladder delivers.

### The root cause phase 18 got wrong

Phase 18 was right that `px == lineFeed` makes text sharp, and wrong about *which* px. It read the
design pack's type table (which is in **em px**) as lineFeed. `mkbcfnt -s N` takes **points at 96 dpi**,
so `em = N×4/3` and `lineFeed ≈ 1.28 × em`. Every phase-18 rung therefore shipped at
**em = designPx / 1.28 = 0.78× its own design**, uniformly. This phase bakes the sizes the design
always specified.

---

## 2. BEFORE / AFTER — SAME SCREEN, SAME CROP, NATIVE DEVICE PIXELS

**Sheet: [`evidence/sheet-before-after.html`](evidence/sheet-before-after.html)** — ten cells, five
matched pairs, captioned.
**Sheet: [`evidence/sheet-shipped-screens.html`](evidence/sheet-shipped-screens.html)** — six screens
of the shipped binary.

| screen | before (phase 18) | after (shipped phase 19) | what moved |
|---|---|---|---|
| paused summary, top | `before-pause-top.png` | `final-pause-top.png` | pill chips cap 4 → **7**; pill box 15 → 17 px so the taller cell stops clipping |
| pause SESSION | `before-pause-session.png` | `final-pause-session.png` | button labels cap 7 → **9**, and now centred on the **ink** box, not the line box |
| pause AUDIO | `before-pause-audio.png` | `final-pause-audio.png` | segment labels cap 6 → **9** |
| pause TOUCH | `before-pause-touch.png` | `final-pause-touch.png` | explainer cap 7 → **9**; the same sentence now needs 3 lines and still clears the buttons |
| empty ROM picker | `before-picker-empty.png` | `g2-picker-empty-bot.png` | the empty-state line left the mono rung for `TXT_BODY`: cap 5 → **9** (+80 %), the biggest single jump in the app |

All captures are reconstructed to exact native resolution (`see shot` → `native.py`, nearest,
byte-exact 320×240 / 400×240), never judged from the emulator's 2.25× upscale.

### Why the banked "after" captures are valid for the shipped binary

The shipped build's pause SESSION capture is **byte-identical — 0 differing pixels** — to the
capture banked during the earlier G2 pass:

```
run compare evidence/g2-pause-session.png evidence/final-pause-session.png
  IDENTICAL — 320x240, 0 differing pixels
```

The screens changed after G2 were re-captured fresh from the shipped binary (`final-pause-*.png`,
six screens including the two the fix pass touched).

---

## 3. COPY THAT CHANGED — object to any of this

Only **two** user-visible strings changed in this phase. Both changed because a **rect** stopped
fitting, not because of taste. The full literal sets of both trees were diffed to produce this list,
so it is exhaustive.

| # | before | after | why |
|---|---|---|---|
| 1 | `"Link actions need a running game"` | **`"Needs a running game"`** | At `TXT_BODY` the old sentence measures 194 px from x=93, running to x=287 — straight under the **"Done"** chip, which is fixed chrome at `{244,224,72,14}` drawn *after* the content, so the pill ate the final `g`'s descender. The note's real box is `DONE.x − 93 = 151 px`, not the 219 px of the content column. |
| 2 | `"trade & battle use the game's own Union Room - not from here"` (one line) | same words, **split across two lines** | Not a wording change — the identical sentence, re-wrapped to two lines because the taller cell no longer fits one. |

**Nothing else was reworded, abbreviated or dropped.**

### Boxes that grew (no copy cost)

| constant | phase 18 | phase 19 | note |
|---|---|---|---|
| `UIHIT_HUD_BAR_H` | 14 | **20** | +6 device px = 2.5 % of the 240-row screen |
| `UIHIT_CHIP_H` | 13 | **16** | the outlined/filled pill |
| `UIHIT_TOUCH_CHIP_H` | 14 | **17** | the touch-mode label |
| `UIHIT_MCHIP_H` / `_Y` | 18 / 220 | **20 / 218** | the "menu" affordance grew upward (218+20 = 238, clear of 240) |
| `UIHIT_PILL_H` | 15 | **17** | the 15 px `TXT_CHIP` cell was clipping at 15 |
| `UIHIT_PILL_PAD` / `_GAP` | 12 / 5 | **10 / 4** | tightened *to pay for* the taller pills, so the row still fits |
| `UIHIT_MENU_VIEW_H` | 228 | **226** | the hint band needs two more rows for the taller cell |
| `SET_LINK_NOTE_Y` | 215 | **208** | `208 + 18 == 226` — flush with the band, nothing clipped |
| presence toggle `y` | 196 | **190** | keeps its bottom edge at 208 |
| tilt seg `y` | 198 | **200** | so `DIORAMA · TILT` groups downward to the control it heads |
| `.sav` picker row pitch | 16 | **20** | 16 would have overlapped the taller line boxes |

A structural fix rides along: five call sites centred the **line** box (`boxY + (boxH − px)/2`),
which is correct only for a face with symmetric padding — none of ours has one. The error was 3–4 px,
enough to push a button label onto its own bottom edge. `typo_center_y()` now centres the measured
**ink** box, once, for everybody.

---

## 4. WHAT HAD TO STAY SMALLER, AND WHY

**The mono rung — `TXT_SECTION` / `TXT_CHIP` at cap 7 (15.3′), under the 16′ floor.**
The next rung up, `jbm-med -s 8`, was **baked and measured**, not estimated: it gives cap 8 (17.5′)
and clears the floor, and it was **rejected** because the mono **advance goes 5 → 7 px (+40 %)**:

* the pause-top pill row blows out to **475 px on a 400 px screen**;
* it desynchronises from the **baked cap-7 plate captions**, which are frozen art (`GAMEPAD · COLOR`,
  `SCALE · TOP`, …) — a live label one size off its baked neighbour looks broken;
* the fixed-width readout boxes cannot absorb it.

The mitigation is architectural rather than typographic (SPEC-legible L3.3): **prose left the mono
rung entirely**. Sentences now draw at `TXT_BODY` (cap 9, 19.6′). What remains at 15.3′ is captions,
key legends, ROM codes and numeric readouts — things that are *glanced at*, not read. This is why the
empty-picker line and the LINK note moved rungs rather than merely growing.

**`TXT_VALUE` — cap 7 both before and after.**
`jbm-bold` rasterises to cap 7 / x-height 5 at **both** pt 6 (lineFeed 11) and pt 7 (lineFeed 12);
both were baked from the same TTF and read off the glyph sheets to confirm. The rung moved anyway for
a real reason: **12 makes the mono line box identical to `TXT_SECTION`/`TXT_CHIP`**, and the app puts
them on a shared grid (the AUDIO tab draws its volume readout at `rowY − 18`, on the caption's own
baseline; the wireless RTT/LOSS tiles sit beside cap-7 mono captions). A rung 1 px shorter than its
neighbours would have shown. The honest way to make it bigger is `jbm-bold -s 8` (cap 8, 17.5′) and
it is rejected for the same advance reason, plus it would break the "**weight, not size**" contract
the role exists to express.

**The pause-top pill row overhangs the plate's design rect.** At 8 pills with the longest dynamic
labels the row is 313 px (x 41…354); at 9 it is 352 px (x 22…374), against a manifest rect of
x 56 w 288. It also exceeded 288 at the phase-18 sizes (291 px for 9), so this phase did not
introduce it, and shrinking the type back is the one thing this phase exists not to do. The gates
that do mean something are asserted: the row is centred on the manifest rect's own centre (200) and
keeps ≥ 20 px of screen margin.

---

## 5. THE FINAL GATE

Run in a clean shell (`env -i`), from the project root.

### Crispness did not regress — read out of the SHIPPED binary

`3DGBA.3dsx` (clean rebuild) booted in Azahar, read live over the GDB stub:

```
gdbio read g_txtTexelScale 28
  0x00525398  00 00 80 3f 00 00 80 3f 00 00 80 3f 00 00 80 3f
  0x005253a8  00 00 80 3f 00 00 80 3f 00 00 80 3f
  -> 0x3f800000 = 1.0f for ALL SEVEN roles, both aliases included

gdbio read g_txtLineFeed 28
  0x0052537c  13 00 00 00 0f 00 00 00 0f 00 00 00 0f 00 00 00
  0x0052538c  0c 00 00 00 0c 00 00 00 0c 00 00 00
  -> 19, 15, 15, 15, 12, 12, 12  = the new ladder exactly
```

These are populated at runtime by `assets_init` from the bytes of the fonts that actually loaded, so
this is a measurement of the shipping artifact, not of a table. Origins stay integer by construction:
`typo_center_y` rounds, and `test_typography` T18 asserts `y == (float)(int)y`.

Two independent guards keep this true for the future: `tools/build_assets.sh bake_checked()` parses
`FINF.lineFeed` out of every `.bin` it produces and **fails the bake** if it is not the px the ladder
declares, and `test_typography` re-parses the shipped bytes and asserts the same thing. The `px`
argument is still absent from the drawing API, so a numeric literal at a call site is still a compile
error.

### Suites — thirteen app host suites + the harness host tests

```
control    6897    diag        376    fieldpath  1808    netlink      66
presence  61376    profiles    546    theme     83444    tilt       1723
trace        58    typography 1419    uigeom   18332    uihit      1834
celiolink  1259
                                            TOTAL  179,138 checks · 0 failures
harness host tests: Ran 149 tests — OK
```

Grown by this phase: **typography 1408 → 1419**, **presence 61371 → 61376**. No suite shrank.
New coverage includes T15 (the alias contract: 7 roles, 5 faces, and `data/` holds exactly those),
T16 (vertical fit — every fixed-offset row keeps its ink inside its box), T17 (cap height per rung,
read from the bytes — the "it is actually bigger" claim), T18 (the declared ink box matches the
shipped bytes) and T19 (one module per band).

### Build + warning audit

```
make -j8   clean            make cia   clean
```

The warning audit was done by building **HEAD in a detached `git worktree`** and diffing the
normalised warning multisets — *not* by `git stash`, because a concurrent phase-20 session shares this
working tree and a stash would have swept up its uncommitted work.

```
HEAD (phase 18): 28 warnings      phase 19: 28 warnings
diff of the two multisets: IDENTICAL — no new warnings, none removed
```

All 28 are pre-existing (unused statics in `main.c`, misleading-indentation in
`main.c`/`touch.c`/`ui.c`/`theme.c`/`netlink.c`, `sha1.c` stringop-overread, one
`wireless.c` format-truncation, one unused static in the frozen `celiolink.c`).

### Frozen paths — diffs empty, as required

`git diff` over `celiolink.c`, `netlink.c`, `gbacore.*`, the HD-2D passes and the emulation/threading
files returns **nothing**. No emulation, threading, link/net driver or HD-2D code was touched.

### All six themes

Unchanged from the G2 verification: each theme was set by patching `Settings.theme` and capturing the
AUDIO tab. Bright-pixel counts over the five non-default captures are **3232 / 3195 / 3179 / 3195 /
3195** — within 1.7 %, i.e. identical geometry with different ink, which is what "only ink colours
differ" predicts. No theme puts ink on a baked label the bigger glyph overlaps.

### Size — the app got SMALLER

Merging two rungs turns seven baked faces into five, and a `.bcfnt` is a fixed 1024×1024 A4 sheet
(~527 KB) whatever its point size:

| | phase 18 (HEAD) | phase 19 (shipped) | Δ |
|---|---|---|---|
| fonts in `data/` | 3,702,088 B (7 faces) | **2,643,972 B (5 faces)** | **−1,058,116 B (−1.01 MiB)** |
| `data/` total | 3,870,842 B (73 files) | **2,812,726 B (71 files)** | −1,058,116 B |
| `3DGBA.3dsx` | 5,417,868 B | **4,365,648 B** | **−1,052,220 B (−19.4 %)** |
| `3DGBA.cia` | 2,128,832 B | **2,008,000 B** | **−120,832 B (−5.7 %)** |

It also drops **~2.0 MB of runtime linear heap**, because `C2D_FontLoadFromMem` `linearAlloc`s a full
copy of every face. (Caveat from the top of this document: the `.3dsx`/`.cia` rows also contain a
concurrent phase-20 slice's code; the `data/` row is purely this phase.)

The merged roles stay in the enum and `TXT_COUNT` stays 7, so no call site changed and the GDB
harness still reads arrays of the shape it expects. Re-splitting a merged role later is one line in
`typography.h` plus one bake line.

### Harness hygiene

Six boots this pass. Every one ended in `azctl stop` reporting *"fixtures: originals re-hashed, all
untouched"* and *"restored qt-config.ini byte-identically (CLEAN)"*; never two Azahars at once. The
emulated `sdmc:/3DGBA/settings.bin` was backed up, patched (`touchMode` → Off, for capture parity with
the before-set) and **restored byte-identically** — sha1
`16e8b5f7bc98f3dd13801f03c48997425b373329` before and after. `sdmc/dual-gba/` was never written.
New CTM recipes are banked at `docs/phase19-legible/movies/m_final_*.json`.

---

## 6. WHAT YOU WILL SEE, AND WHAT TO CHECK

Turn the console on and the whole interface should read one step larger. The clearest places to look:

* the **empty ROM picker** line ("put .gba files in …") — cap 5 → 9, the biggest jump in the app;
* the **pause menu buttons** (Resume / Change games / Quit) — bigger, and no longer sitting low in
  their own pills;
* the **paused summary pill row** on the top screen — the 3D / DoF / Bloom / Tilt chips were the 4-px
  offenders and are now 7 px;
* the **segmented controls** (Solo / Mixed / Split, Off / Low / Mid / Max) — +50 %.

Then the two questions this phase cannot answer without you:

1. **Can you read the chips and the hint lines without leaning in?** Those sit knowingly at 15.3′,
   under the standards floor, for the advance-width reasons in §4. If the answer is no, the fix is
   *not* a bigger mono rung — it is moving those specific strings to `TXT_BODY`, the same move that
   rescued the picker line.
2. **Does anything overlap, clip or look crowded on the real panels?** Every box was re-measured and
   the host suite grades 28 boxes against the shipped font bytes, but baked plate art plus live text
   is exactly the combination an emulator's upscale can flatter.
