# PHASE 19 / SPEC-legible — INDEPENDENT VERIFICATION

Written by the verification pass, not by the slices. Nothing here is copied from `BUILDLOG.md`:
every number below was re-derived from the shipped bytes (`data/fnt_*.bin`, `source/*`) or read
off a capture taken in this pass. Where I reproduce a slice claim I say so and give my own
measurement beside it.

Evidence root: `tools/emutest/runs/p19-verify/`.

---

## 0. Status

| Gate | Verdict |
|---|---|
| Crispness (R1: every role at texel scale exactly 1.0) | **PASS — no regression** (byte-side, §1) |
| Ladder actually got bigger | **PASS** (§1.2, cap heights re-measured) |
| Forbidden files untouched (emulation / threading / link / net / HD-2D) | **PASS** (§2) |
| Thirteen app host suites + celiolink | **PASS** (§3) |
| Horizontal fit — independent width re-measurement | **PASS** (§4) |
| Vertical fit — independent ink-box re-derivation | **2 DEFECTS** (§5) |
| Capture sweep across every reachable screen | **1 blocker + 1 major + 3 minor** (§6) |

**The phase does what it set out to do** — six of seven rungs are measurably bigger, the
crispness law is intact in the running binary, and nothing overflows horizontally. It ships one
defect that is worth blocking on: in Smart touch mode the bottom-screen HUD **loses the end of
the game name and the whole `●FOCUS` chip** underneath the mode chip (§6.3), which is new in this
phase and visible in the very first in-game capture I took.

---

## 1. The phase-18 property that must not regress

### 1.1 Texel scale, from the shipped font bytes

Re-derived with `tools/fontlab/bcfnt.py` against `source/typography.h`'s table — my own script,
not `test_typography`:

```
role         sym             px lineFd  cellH   drawSc  TEXELSC
TXT_TITLE    sg_bold_19    19.0     19     22 0.733333 1.000000 OK
TXT_BUTTON   sg_bold_15    15.0     15     18 0.600000 1.000000 OK
TXT_BODY     sg_med_15     15.0     15     18 0.600000 1.000000 OK
TXT_SEG      sg_med_15     15.0     15     18 0.600000 1.000000 OK
TXT_SECTION  jbm_med_12    12.0     12     15 0.500000 1.000000 OK
TXT_CHIP     jbm_med_12    12.0     12     15 0.500000 1.000000 OK
TXT_VALUE    jbm_bold_12   12.0     12     15 0.500000 1.000000 OK
```

Every rung is `lineFeed == px` and `texel_scale == 1.0f` **exactly** (equality, no tolerance).
`data/` holds exactly five `fnt_*.bin`, matching `TXT_DISTINCT_FACES 5` — no stale vintage left
behind for `bin2s` to embed.

`assets.c` still snaps the origin (`x = floorf(x + 0.5f)`) and still calls
`C2D_FontSetFilter(GPU_NEAREST, GPU_NEAREST)`, so the second half of R1 (integer origin) holds
and any future off-1.0 draw will look obviously wrong instead of quietly soft.

### 1.2 The ladder is genuinely bigger

Cap heights measured off the shipped bytes (glyph `H`), plus x-height:

```
face           lineFeed cellH cap  x-height   arcmin @30cm on the 133 ppi bottom panel
sg_bold_19       19      22    11     8        24.1'
sg_bold_15       15      18     9     6        19.7'
sg_med_15        15      18     9     6        19.7'
jbm_med_12       12      15     7     5        15.3'
jbm_bold_12      12      15     7     5        15.3'
```

To make "bigger" a measurement rather than a claim, I **re-baked the phase-18 ladder** from the
same TTFs in `tools/.fontcache/` with the same `mkbcfnt`, and measured its cap heights the same
way. Cap height, not lineFeed, is what the eye reads:

| role | p18 face | p18 cap | p19 cap | change | p19 arcmin |
|---|---|---|---|---|---|
| TXT_TITLE   | sg_bold_17  | 10 | 11 | **+10 %** | 24.1' |
| TXT_BUTTON  | sg_bold_12  |  7 |  9 | **+29 %** | 19.7' |
| TXT_BODY    | sg_med_12   |  7 |  9 | **+29 %** | 19.7' |
| TXT_SEG     | sg_med_10   |  6 |  9 | **+50 %** | 19.7' |
| TXT_SECTION | jbm_med_9   |  5 |  7 | **+40 %** | 15.3' |
| TXT_CHIP    | jbm_med_7   |  4 |  7 | **+75 %** | 15.3' |
| TXT_VALUE   | jbm_bold_11 |  7 |  7 | **±0 %**  | 15.3' |

The complaint is answered for six of the seven rungs, and most strongly exactly where it was
loudest (the 7 px chip label the brief calls out went cap 4 → cap 7).

**Observation O1 — `TXT_VALUE` did not actually get bigger.** `jbm-bold` at pt 11 and at pt 12
both rasterise to **cap 7**; only the line box moved (11 → 12). So the role that draws the
presence-card trainer ID, the two volume-level readouts and the wireless RTT/LOSS numbers is
the same optical size it was before this phase, still at 15.3'. That is defensible (they are
glanced-at numerals, and the next rung up would be `-s 9` / cap 8) but the ladder table in
`typography.h` presents `11 → 12` as a step, and in cap height it is not one.

The mono rungs sit at 15.3', knowingly under the 16' ISO floor; the mitigation the slices chose
(prose leaves the mono rung entirely) is present in the source — `rompicker.c` empty state, the
settings subtitle, the O3DS warning, the touch explainer and the LINK disabled-note all draw at
`TXT_BODY`.

---

## 2. Scope — the diffs that must stay empty

`git diff --stat` over `source/celiolink.c source/netlink.c source/gbacore.c source/celiolink.h
source/netlink.h source/hd2d.c source/tilt.c` → **empty**. Emulation, threading, the link/net
drivers and the HD-2D passes are untouched.

Caveat worth recording: the working tree is **shared with a concurrent phase-20 "peersprite"
slice**, so `source/main.c`, `gamestate.*`, `presence_*`, `peersprite.*` and two host suites carry
changes that are NOT phase 19's. I graded only the typography/layout hunks.

---

## 3. Suites

All fourteen built and run from the compile lines in their own headers:

```
control 6897 · diag 376 · fieldpath 1808 · netlink PASS · peersprite 62078 · presence 61371 ·
profiles 546 · theme 83444 · tilt 1723 · trace PASS · typography 1408 · uigeom 18332 ·
uihit 1834 · celiolink PASS
```

Zero failures. `test_profiles` needs `source/peersprite.c` on its link line (the header's own
compile comment is stale — that is the phase-20 slice's, noted for whoever fixes it).

---

## 4. Horizontal fit — my own re-measurement

Widths summed from the shipped `CWDH` `charWidth` values, per role, against the box each string
is drawn into. Spot-check of the riskiest strings (the ones the slices flagged, plus every
segmented-control label, which is where a 10 px → 15 px rung is most likely to burst):

```
BUTTON   Preview Gamepad        103 / 106   97.2%   (the phase's one near-miss; confirmed)
BODY     Needs a running game   128 / 151   84.8%
BODY     Tilt is set, ... flat. 263 / 298   88.3%
BODY     put .gba ..., then Rescan 254 / 320  79.4%
BUTTON   Continue without a game 142 / 164   86.6%
TITLE    TAP TO START            99 / 175
CHIP     TOUCH · SMART POINTER  105 / 320
```

Segmented controls, worst label per control against its cell width:

```
SCALE  "Aspect-fit"  61 / 69.3  88.0%      HUD    "bottom"  44 / 52.0  84.6%
TOUCH  "Gamepad"     55 / 69.3  79.3%      AUDIO  "Mixed"   35 / 72.0  48.6%
EDGES  "Round"       37 / 69.3  53.4%      TILT   "Low"     24 / 42.5  56.5%
```

Nine-pill pause row, worst case (`Touch Off` + `Wireless` + `Vivid` all present), recomputed from
the font: **356 px on a 400 px screen, 22 px margins** — matches the slice's claim.
Note the row is wider than the plate's own baked pill band (`x56..344`, 288 px); it is centred on
the *screen*, so it runs x22..x378. That is pre-existing (it was 383 px before this phase) but it
is a thing to look at in the capture, not to assume.

Presence card (grew 80 → 108 px): longest row `"trade & battle use the game's own"` = 165 px in a
194 px text column; all eight rows' ink lands inside the card (bottom ink row 104 of 107).

**No horizontal overflow found.**

---

## 5. Vertical fit — two defects the slices missed

Both are the *same class* the phase's own `typo_center_y` helper exists to remove: a hand-typed
vertical offset that was tuned for the old, shorter cell and never converted.

`grep -rn "typo_role_px" source/*.c` finds exactly one surviving line-box centring site.

### DEFECT V1 — the settings "Done" chip's label overhangs its own pill

`source/main.c:5434`

```c
ui_fill((float)DONE.x, (float)DONE.y, (float)DONE.w, (float)DONE.h, g_ui.acc, 5.0f);
assets_text_c(txtBuf, TXT_BUTTON, "Done", DONE.x + DONE.w / 2.0f, DONE.y + 1.0f, g_ui.ink);
```

`DONE = {244, 224, 72, 14}` → the pill occupies rows **224..237**.
`TXT_BUTTON` is cell 18 with `inkTop 5`; at `y = 225` the cap ink of `Done` lands on rows
**230..238**. Row 238 is **one row below the pill's last row**, and there are **six** empty rows
above the glyphs — the label reads as sitting on, and slightly through, the bottom edge.

`typo_center_y(TXT_BUTTON, 224, 14)` returns **220**, which puts the ink at 225..236, inside the
pill with 1 row top / 1 row bottom. The site was simply not converted.

This is the *only* chip in the app whose label is still placed by a literal, and it is on fixed
chrome that is drawn last, so nothing else can hide it.

### DEFECT V2 — the ROM picker's `settings · ZR` chip is 2 px low and ungraded

`source/rompicker.c:328`

```c
assets_text(buf, TXT_CHIP, "settings · ZR", (float)rSet.x + 6.0f,
            (float)rSet.y + ((float)rSet.h - typo_role_px(TXT_CHIP)) / 2.0f, g_ui.dim);
```

This is the phase-18 **line-box** centring formula, the one L3.2.7 replaced everywhere else.
`rSet = {6, 223, 88, 16}` → pill rows 223..238; the formula gives `y = 225`, ink rows **229..237**:
6 rows of padding above, **1** below. `typo_center_y(TXT_CHIP, 223, 16)` gives 223 → ink 227..235,
4 above / 3 below.

It is also **not covered by `test_typography`** — the string `"settings · ZR"` appears nowhere in
the suite, so neither T10 (width) nor T16 (ink-in-box) grades it. Width itself is fine (65 px in
an 82 px column).

### Everything else re-derived and clean

| Row | Re-derived ink | Box | Verdict |
|---|---|---|---|
| HUD name (`TXT_BODY` @ y2) | 7..18 | bar 0..19 | fits |
| HUD clock/fps (`TXT_SECTION` @ y4) | 8..16 | bar 0..19 | fits |
| HUD chip (`UI_CHIP_H` 16 @ y2) | box 2..17, ink 6..14 | bar 0..19 | fits |
| pause pill row (`PILL_Y` 129 `PILL_H` 17) | 133..141 | 129..145 | 4 above / 4 below |
| `≡ menu` chip (218,20) | ink 222..230 | 218..237 | fits, clears row 240 |
| touch mode chip (y4, h17) | ink 8..16 | 4..20 | fits |
| menu status hint (`y=226`) | 230..238 | screen ..239 | 1 row spare |
| settings hints (206 / 222) | 210..218 / 226..234 | screen ..239 | 7 rows apart |
| `.sav` picker last row (y190) | 195..206 | help ink 218..226 | 11 rows clear |
| LINK note (208) + presence toggle (190) | note 213..224 | viewport 226 | fits |
| wireless fpLine (206) / status (224) | 210..218 / 228..236 | screen ..239 | 9 rows apart |
| presence card rows | last ink 104 | card 0..107 | fits |
| stepper `-`/`+` (y+2, h24) | 1 px below optical centre | — | cosmetic, below noticing |

The menu status line (`assets_text_r ... 310, 226`) and the centred touch hint
(`assets_text_c ... 160, 226`) share a row but are in an `if/else` — they can never both draw.

---

## 6. Capture sweep — my own boots

Five Azahar boots, all `azctl boot` → `gdbio resume` → `see rec` → `gdbio detach` → `azctl stop`
("profile: restored qt-config.ini byte-identically … CLEAN" on every one; `fixtures: originals
re-hashed, all untouched`). Frames at 2 fps, both screens, in
`tools/emutest/runs/p19-verify/frames/{pause,tabs,pick,wl,sav}`; the shots cited below are
banked in `tools/emutest/runs/p19-verify/shots/`.

**Harness note.** The working tree and the emulator were shared with a concurrent phase-20
session that held Azahar continuously; these boots were taken in the gaps between its runs.
Because *its* ROM fixtures + `recent.bin` were staged on the emulated SD, my boots came up on
the **resume prompt** with two real Pokémon Emerald ROMs rather than the ROM-less empty picker —
which is the more useful state (real game names, a live dual-core session) but means the
**ROM-less empty picker was not re-shot in this pass**; its strings are covered by §4 only.

### 6.1 The phase-18 property, read out of the RUNNING app

`gdbio` against boot 3, after `poll g_renderSeq --changed` proved the render loop live:

```
g_txtLineFeed[0..6]   = 19 15 15 15 12 12 12          <- the phase-19 ladder, in the shipped binary
g_txtTexelScale[0..6] = 0x3f800000 x7  ==  1.0f exactly, all seven roles
```

**No regression.** This is the strongest form of the claim: not the bake, not the header — the
value `assets_text` actually hands the sampler, read live. Combined with §1.1 (byte side) and the
unchanged `floorf(x + 0.5f)` origin snap, R1 holds.

### 6.2 Screens that are clean

| Screen | Shot | Verdict |
|---|---|---|
| splash, both screens | `splash-top.png`, `splash-bot.png` | "TAP TO START" centred in its 175 px button; the two-line Old-3DS warning and the baked legal lines all clear |
| resume prompt | `resume-prompt-top.png`, `-bot.png` | title, two game cards, three buttons, key legends — no collisions |
| picker, populated | `picker-rows-top.png`, `picker-dual-bot.png` | rows at 23.5 px pitch hold an 18 px cell; `BPEE` chip right-aligned; mode seg, both slot cards, `START` / `START — LINKED` all fit |
| in-game HUD, **top** | `hud-top.png` | name + `●FOCUS` + `3D` + `22fps` + clock + battery on one 400 px bar, wide clearances |
| pause summary (top) | `pause-summary-top.png` | `PAUSED`, both game names, 8 pills, hint — nothing clipped |
| pause SESSION / DISPLAY / AUDIO / LINK / TOUCH | `pause-*.png` | every seg label inside its cell; the 3-line touch explainer wraps exactly as §4 predicts and clears the Preview buttons; `Preview Gamepad` fits (tight, 97 %) |
| virtual gamepad | `gamepad-overlay.png` | `L R A B START` all centred in their keys, `TOUCH · GAMEPAD` and `≡ menu` chips clear of everything |
| standalone settings, top | `settings-top.png` | title / subtitle / the two hint lines, correctly spaced |
| settings LINK (disabled) | `settings-link-note.png` | "Needs a running game" fits its 151 px column and stops clear of the Done chip |
| wireless lobby, both | `wireless-top.png`, `wireless-bot.png` | 4 seat cards, RTT/LOSS tiles, transport line, 3 action buttons — clean |
| `.sav` picker (Daylight) | `sav-picker-top-daylight.png`, `-bot-` | 20 px row pitch, title clear of row 0, help line on the bottom screen |
| Daylight theme, ENHANCE tab | `theme2-daylight-enhance.png` | geometry identical, ink retinted, nothing collides (set by patching `settings.bin` byte 64, restored byte-identically — sha1 `16e8b5f7bc98f3dd13801f03c48997425b373329` before and after) |

### 6.3 DEFECT C1 (blocker) — the bottom-screen HUD name is eaten by the touch-mode chip

`shots/DEFECT-hud-name-vs-touchchip.png` + `-zoom.png` (boot 1, frame 120).

The bottom bar renders `Pokemon Emeral` and then the `TOUCH · SMART POINTER` chip covers the
rest. Both are drawn every frame in Smart mode: `main.c:4968` draws the HUD bar (name at x=15)
whenever `(hudMode & 2) && tmEff != TOUCH_PAD`, and `touch.c:861` draws the chip centred on
x=160 in the same band. The Gamepad case was thought through — the bar is explicitly suppressed
for `TOUCH_PAD`, with a comment explaining that the pad owns the top band — but **Smart draws a
chip in that band too and no one suppressed the bar for it.**

Measured on the shipped faces (and against the re-baked phase-18 pack, so the regression is
attributable):

```
              name 15..           FOCUS chip        SMART chip        result
p18           15..94              100..130          127.5..192.5      2.5 px touch
p19           15..117             123..165          106.5..213.5      name cut, FOCUS chip FULLY COVERED
```

Two distinct losses, both new this phase:
1. the game name is truncated mid-word for `Pokemon Emerald` (10 px) and `Pokemon LeafGreen`
   (24 px) — and in **Gamepad** mode `LeafGreen` collides too (8 px), which the pad suppression
   does not help because the pad path draws no bar at all, so this bites the `Off` mode;
2. the `●FOCUS` chip — the only thing on the bottom screen that says which game your buttons
   drive — is drawn **entirely underneath** the mode chip whenever the bottom screen has focus.

Neither T10 nor T16 can see this: both grade a string against *its own* box, and these two
strings are drawn by different modules into the same 20 px band with no shared rect.

### 6.4 DEFECT C2 (major) — the settings "Done" label hangs off its own pill

`shots/DEFECT-done-chip-settings.png`, `-zoom.png` (boot 3, frame 60). The static prediction in
§5/V1 is exactly what the pixels show: a wide empty gold band above the word and the glyph
bottoms flush with / over the pill's last row. `main.c:5434` is the one call site left that
places a label with a literal (`DONE.y + 1.0f`) instead of `typo_center_y`, which would return
220 instead of 225.

### 6.5 DEFECT C3 (minor) — `settings · ZR` sits low in its pill

`shots/DEFECT-settings-chip-zoom.png` (boot 3, frame 35). Same cause, the other survivor:
`rompicker.c:328` still uses the phase-18 line-box formula. 6 rows of padding above the ink, 1
below. Not clipped, but visibly bottom-heavy next to every other chip in the app, and ungraded
by the suite.

### 6.6 DEFECT C4 (minor) — `DIORAMA · TILT` still reads as a sub-line of "Vivid mode"

`shots/DEFECT-tilt-caption-zoom.png` (boot 2, frame 105). The BUILDLOG records this as DEFECT 4,
found and fixed by moving the tilt row 198 → 200. The capture says the fix did not achieve its
purpose: the caption's ink sits **4 rows** under "Vivid mode"'s baked ink and **4 rows** above
its own control — i.e. equidistant — while the plate's own established pattern for a dim mono
line directly under a bold label is *"top screen"* under *"Stereoscopic 3D"*, a **description of
the row above**. So the caption is read as belonging to the wrong row. Two more pixels cannot
fix a tie; the row needs an asymmetric gap (or the caption needs to move to the tilt seg's left).

### 6.7 DEFECT C5 (minor) — the pause pill row no longer fits the plate's dim band

`shots/pause-summary-top.png`. The `pause-top` plate dims a 240×160 game band at native
x 80..320; the feature pills are centred on the **400 px screen**, so at phase-19 sizes they
overhang it:

```
                    p18 row width / extent        p19 row width / extent
8 pills (captured)   235 px  x 82.5..317.5   ->    277 px  x 61.5..338.5
9 pills (worst case) 291 px  x 54.5..345.5   ->    356 px  x 22.0..378.0
```

In phase 18 the 8-pill row fit the dim band exactly; now the outer pills sit on the plain black
letterbox outside it. Nothing is unreadable — it is a composition regression, and it is the price
L3.1.4 paid knowingly (it optimised the row against the 400 px screen, not against the plate).

### 6.8 Observation O2 — the smallest text in the app was not touched

`main.c:4861` and `main.c:4885` draw two dense diagnostic lines with `ui_text(..., 0.32f, ...)`
— the 3DS **system** font at ~1/3 scale, the smallest and blurriest text the app can produce.
They are gated on `presenceOn && (hudMode & 1)`, i.e. on a **user-facing feature toggle**, not on
a dev flag, so a player who turns Co-op presence on gets them on the top screen at y=216/226.
T12 budgets them deliberately (they cram ~73 characters into 400 px, which no 12 px face holds),
and the second one belongs to the concurrent phase-20 slice — but "everything is too small" is
the complaint this phase exists to answer, and these two lines are the smallest thing left.

### 6.9 Not covered in this pass

* the **ROM-less empty picker** (the emulated SD carried another session's ROM fixtures for the
  whole window, and cleaning them would have destroyed that session's run);
* the **`Touch = Off` centred hint** state and the **co-op toast**, both of which need a state I
  could not reach inside the emulator time I had;
* **four of the six themes** — Indigo (default) and Daylight were shot; OLED / Duo / Retro /
  Custom were not. `test_theme` (83 444 checks) passes and the phase changed no colour token, so
  the risk there is ink contrast, not geometry;
* **hardware**. CLAUDE.md #6 is unchanged: Azahar renders the 320×240 panel into ~720×540, a
  2.25× non-integer upscale, so every capture above is softer than the device. Size judgements
  are safe from these; sharpness judgements come from §1.1/§6.1, not from the pixels.
