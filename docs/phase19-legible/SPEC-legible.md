# SPEC-legible — make the UI text BIG ENOUGH TO READ (phase 19)

Status: **investigation complete, target sizes grounded in four independent anchors, ladder chosen,
every string re-measured against its real box — not implemented.**
Author pass: 2026-08-13. Scope: `source/typography.h` (the ladder), `source/assets.c` (role→face
map + the two composite widgets), `tools/build_assets.sh` (the bake), and the *vertical geometry*
of the screens the bigger line boxes no longer fit. **Out of scope and their diffs must stay
EMPTY:** emulation, threading, `celiolink.c` / `netlink.c` / gbacore SIO, the HD‑2D passes
(`tilt.c`, DoF/bloom/light/pop).

User complaint being answered (real New‑3DS hardware, phase‑18 build), verbatim:

> "Make the text bigger in the app, everything is simply too small to be written clearly"

**Verdict: the user is right, the deficit is systematic, and its cause is identifiable.** Phase 18
made every draw pixel‑exact by pinning `px == lineFeed(face)` — that part is correct and must not
regress. But it then *picked* the ladder's numbers by reading the design pack's type table as
`lineFeed`, when that table is in **em px**. `mkbcfnt -s N` takes **points at 96 dpi**
(em = N × 4/3) and emits `lineFeed ≈ 1.28 × em`. So a role the design specifies at "12 px" was
baked as `lineFeed = 12`, i.e. **em ≈ 9.3 px — 0.78× the intended size**, and the same 0.78×
applies to all seven rungs. The app is not "a bit small by taste"; it is *uniformly three quarters
of its own design*, and six of its seven rungs sit below the ISO/ANSI legibility floor.

---

## 0. Executive summary

| | Claim | Status |
|---|---|---|
| **C1** | 6 of 7 rungs are below the ~16 arcmin comfortable‑reading floor at 30 cm on the 133 ppi bottom screen; CHIP is at **8.7′**, half the floor | **MEASURED** (§L1.1) |
| **C2** | The Pokémon game text the user reads happily on the *same panel* is **cap 12.0 device px**; our body text is cap 7 and our chips cap 4 | **MEASURED from a real frame** (§L1.2) |
| **C3** | The design pack's OWN baked plate labels are cap 7–9 px — i.e. **the art that ships in `data/` is already bigger than the text we draw on top of it** | **MEASURED from the plate PNGs** (§L1.3) |
| **C4** | The px↔lineFeed unit error explains the whole deficit, uniformly (0.78×) | **PROVEN** (§L1.4) |
| **C5** | The horizontal re‑fit is nearly free: at the proposed ladder **exactly one** of 93 measured strings overflows, by 2 px | **MEASURED** (§L3.1) |
| **C6** | The real work is VERTICAL — line boxes grow 14→18 / 9→15 px and collide with the HUD bar, the status‑hint band, the presence card, the .sav picker and the touch explainer | **ENUMERATED with fixes** (§L3.2) |
| **C7** | The new ladder is **1.01 MB SMALLER** than the current one, because merging four roles onto two faces removes two 530 KB bakes | **MEASURED** (§L2.4) |
| **C8** | A user‑selectable Normal/Large setting is affordable in bytes but is **the wrong answer** and is not recommended | **REASONED** (§L2.2) |

The one‑line change: **every role moves up 1–2 rungs, and the seven faces become five.**

Evidence images banked in `docs/phase19-legible/evidence/`.

---

## L1 — GROUNDING THE TARGET SIZES

Three anchors, computed independently, then reconciled. Nothing here is "this feels bigger".

### L1.1 — Anchor (a): PHYSICS

**L1.1.1 — Panel geometry.** The 3DS family's panels, with the pixel pitch each implies:

| panel | px | diagonal | width | **ppi** | **pitch** |
|---|---|---|---|---|---|
| bottom, 3DS / New 3DS (non‑XL) | 320×240 (4:3) | 3.02″ / 3.33″ | 2.416″ / 2.664″ | **132.5** / 120.1 | **0.192 mm** / 0.211 |
| bottom, XL / New 3DS XL | 320×240 | 4.18″ | 3.344″ | 95.7 | 0.265 mm |
| top, 3DS / New 3DS (non‑XL) | 400×240 (5:3) | 3.53″ / 3.88″ | 3.027″ / 3.327″ | 132.2 / 120.2 | 0.192 / 0.211 mm |
| top, New 3DS XL | 400×240 | 4.88″ | 4.185″ | 95.6 | 0.266 mm |

**The binding case is the 132–133 ppi bottom screen** (a non‑XL console): it has the *smallest*
physical pixel, so a ladder that is legible there is legible on every model. Use **0.1905 mm/px**
(the brief's ~133 ppi) throughout. Note the consequence, which is counter‑intuitive and worth
stating in the code: on an XL the *same* px count is 1.39× physically larger, so **XL owners never
see this bug as badly as non‑XL owners do**.

**L1.1.2 — Angular size.** At a viewing distance *d*, a feature of height *h* subtends
`θ ≈ (h/d) × 3437.75` arcmin (small‑angle). At **d = 300 mm**:

```
1 device px = 0.1905 mm  →  (0.1905 / 300) × 3437.75 = 2.183 arcmin
```

**L1.1.3 — The floor being designed to.** ISO 9241‑303 and ANSI/HFES 100 both specify **character
height ≥ 16 arcmin as the minimum, 20–22′ preferred** for sustained reading of screen text.
(Legge & Bigelow's critical‑print‑size result — fluent reading needs ≳ 0.2° ≈ 12′ of *x‑height* —
is the same statement in a different metric and agrees within a rung.) Character height means
**cap height**, not em size and not lineFeed, so cap height is what this spec designs to.

```
16′ floor      → 16 / 2.183 = 7.33 device px of cap height   (design minimum: 8 px)
20′ preferred  → 20 / 2.183 = 9.16 px
22′ generous   → 22 / 2.183 = 10.08 px
```

**L1.1.4 — Where the shipped ladder sits.** Cap heights read out of the shipped `data/fnt_*.bin`
(ink bounding box of `H`, sheet decoded from the actual bytes — same reader `test_typography.c`
uses):

| role | px (=lineFeed) | face | **cap px** | mm | **arcmin @30 cm** | vs 16′ floor |
|---|---|---|---|---|---|---|
| TXT_TITLE | 17 | sg‑bold pt10 | 10 | 1.905 | **21.8′** | ✅ |
| TXT_BUTTON | 12 | sg‑bold pt7 | 7 | 1.334 | **15.3′** | ❌ |
| TXT_BODY | 12 | sg‑med pt7 | 7 | 1.334 | **15.3′** | ❌ |
| TXT_VALUE | 11 | jbm‑bold pt6 | 7 | 1.334 | **15.3′** | ❌ |
| TXT_SEG | 10 | sg‑med pt6 | 6 | 1.143 | **13.1′** | ❌ |
| TXT_SECTION | 9 | jbm‑med pt5 | 5 | 0.953 | **10.9′** | ❌❌ |
| TXT_CHIP | 7 | jbm‑med pt4 | 4 | 0.762 | **8.7′** | ❌❌❌ |

**L1.1.5** — *Six of seven rungs fail the floor.* `TXT_CHIP` at **8.7′ is 54 % of the minimum** and
roughly the angular size of 20/40 acuity's threshold letter — i.e. it is near the limit of what a
person with normal vision can resolve *at all*, let alone read comfortably. That single row is the
user's complaint, and `TXT_SECTION` (every section caption, every hint line, the HUD clock and fps)
is the second.

### L1.2 — Anchor (b): THE GAME'S OWN TEXT, ON THE SAME PANEL — the strongest anchor

The user reads Pokémon Emerald's text on this hardware every day without complaint. That text is a
**measurable** on‑panel size and it is the only reference that controls for panel, distance, eyesight
and lighting all at once.

**L1.2.1 — Method.** Loaded the user's own `gameA.gba` (copy staged in scratch; the originals in
`sdmc/dual-gba/` were read‑only, per harness rule) in mGBA 0.10.5 at 3×, captured the window,
reconstructed the exact 240×160 GBA framebuffer (nearest re‑expansion verified: 0.08 mean absolute
difference), and read the glyph ink rows directly.
Evidence: `evidence/anchor-gba-emerald-240x160.png`, `evidence/anchor-gba-cap-height-x8.png`.

**L1.2.2 — Result (GBA pixels).** Pokémon Gen‑3 body font ("CONTINUE", "PLAYER", "POKéDEX",
"NEW GAME"): capital glyph face occupies rows 6–14 inclusive = **cap height 9 GBA px**, plus a 1 px
drop shadow (10 px of ink), on a **16 px line pitch**. Confirmed on three separate runs of text in
the same frame.

**L1.2.3 — On the 3DS panel.** `render_game` aspect‑fits the 240×160 frame
(`main.c:2100`, default `SCALE_FIT` both screens):

| screen | fit scale | **game cap height, device px** | mm | **arcmin @30 cm** |
|---|---|---|---|---|
| bottom 320×240 | `min(320/240, 240/160)` = **1.3333** | **12.0** | 2.286 | **26.2′** |
| top 400×240 | `min(400/240, 240/160)` = **1.5** | **13.5** | 2.571 | **29.5′** |
| either, `SCALE_1X` | 1.0 | 9.0 | 1.715 | 19.6′ |
| bottom, `SCALE_STRETCH` | 1.5 vertically | 13.5 | 2.571 | 29.5′ |

**L1.2.4 — The comparison that settles the argument.** On the *default* bottom screen the user
comfortably reads **cap 12 px** game text. Our UI, on the same pixels, in the same session, draws
its body text at **cap 7** (58 %) and its chips at **cap 4 (33 %)**. There is no defensible reading
of "the UI chrome may be smaller than the content" that survives a 3× ratio.

**L1.2.5 — The target this anchor sets.** UI body text must not be meaningfully smaller than the
game text: **cap ≥ 9 px** on the bottom screen (75 % of the game's 12 px, 19.6′, i.e. at the ISO
"preferred" 20′ line). Chrome/labels may be one rung under body, never two.

### L1.3 — Anchor (c): FIRST‑PARTY / DESIGN‑PACK REFERENCE

The 3DS system UI's own metrics are not documented in a citable form, so this anchor is served by
something better and *local*: **the design pack's own baked art**, which is the reference the app
is supposed to look like and which ships inside `data/`.

**L1.3.1 — Baked plate labels, measured off the PNGs** (`design_handoff_3dgba_ui/assets_3ds/plates/indigo/`,
ink rows against the flat panel colour `#201830` / rail `#2A2042`):

| baked label | plate | ink rows | **cap px** | arcmin |
|---|---|---|---|---|
| tab rail "Session" / "Link" / "Touch" | `pause-bot-display` | 18–25 / 141–148 / 172–179 | **8** | 17.5′ |
| tab rail "Display" (with descenders) | same | 49–58 | 8 (+2 desc) | 17.5′ |
| section captions "SCALE · TOP", "SCALE · BOTTOM", "FILTER · FOCUSED", "HUD" | same | 12–18, 67–73, 122–128, 177–183 | **7** | 15.3′ |
| row label "Link cable" | `pause-bot-link` | 20–28 | **8–9** | 17.5–19.6′ |

**L1.3.2 — The finding.** *The immutable baked art is already bigger than the text we draw on top
of it.* A baked cap‑8 rail label sits 20 px from a code‑drawn cap‑6 segment label; a baked cap‑7
caption sits directly above a code‑drawn cap‑5 caption of the same kind. See
`evidence/before-pause-audio.png` and `evidence/before-pause-touch.png` — the size break between
baked and code‑drawn text is visible without measurement. Matching the art is therefore not a
fidelity nicety: it is the same requirement as legibility, pointing the same way.

**L1.3.3 — The design pack's stated type table** (`assets_3ds/fonts/FONTS.md` §3, "Exact type roles
(device px — match these)"), which is the *intended* ladder:

| design role | family/weight | **design px (em)** | implied cap (≈0.72 em) |
|---|---|---|---|
| Screen title | Space Grotesk 600 | 14–16 | 10–11.5 |
| Button label | Space Grotesk 700 | **12–14** | 8.6–10 |
| List row / body | Space Grotesk 500–600 | **11–13** | 7.9–9.4 |
| HUD game name | Space Grotesk 600 | 10–11 | 7.2–7.9 |
| Section label (SCALE·TOP…) | JetBrains Mono 600 | **9** | 6.5 → measured 7 ✔ |
| Game code / stat line | JetBrains Mono 500 | 8–9 | 5.8–6.5 |
| HUD chips | JetBrains Mono 600 | 7–7.5 | 5–5.4 |
| PAUSED / divider caps | JetBrains Mono 700 | 9–10 | 6.5–7.2 |

### L1.4 — WHY the ladder came out uniformly small (the cause, not a guess)

**L1.4.1 — The unit error.** `mkbcfnt -s N` is documented in FONTS.md itself as "points→px at
96 dpi, so `-s 14` ≈ 14 px" — the second half of that sentence is wrong, and it is what was
believed. Measured on this toolchain (13 sizes × 4 faces, this session):

```
em  = pt × 96/72 = pt × 1.3333
cap ≈ 0.72 × em  = pt × 0.96          (measured: sg-bold pt10 → cap 10, pt11 → cap 11, …)
lineFeed ≈ 1.28 × em = pt × 1.706     (measured: sg pt10→17, pt11→19; jbm pt7→12, pt8→14)
```

Phase 18 set `lineFeed := the design's px`. The design's px is **em**. So every rung shipped at
`em = designPx / 1.28 = 0.78 × designPx`, and hence `cap = 0.78 × intended`. Check it against
L1.1.4 and L1.3.3: BUTTON wanted em 12–14 and got em 9.33; BODY wanted 11–13 and got 9.33; SECTION
wanted 9 and got 6.67; CHIP wanted 7–7.5 and got 5.33. **Uniform 0.78×, every row.** This is the
whole defect, and it is why "everything" is too small rather than "some things".

**L1.4.2** — The fix does not touch the crispness law. R1 (`px == lineFeed`, integer origins) is
about the *relationship* between the bake and the draw; it is satisfied at any rung. Phase 19
changes only *which* rung, and `bake_checked` keeps proving the relationship.

### L1.5 — RECONCILIATION AND THE PROPOSED LADDER

**L1.5.1 — Reconciling the anchors.** Four independent numbers for "how big should body text be",
in cap px on the bottom screen:

| anchor | says | cap px |
|---|---|---|
| (a) ISO/ANSI floor @30 cm, 133 ppi | ≥ 16′ | **≥ 8** |
| (a) ISO/ANSI preferred | 20–22′ | **9–10** |
| (b) the game's own text, same panel | ≥ 75 % of cap 12 | **≥ 9** |
| (c) design pack's baked row labels | match the art | **8–9** |
| (c) design pack's stated table (body 11–13 em) | | **8–9.4** |

They converge on **cap 9 for body**, which is `mkbcfnt` pt 9 in both Space Grotesk cuts → lineFeed
**15**. Every other rung is then placed relative to it and re‑checked against the same anchors.

**L1.5.2 — THE PROPOSED LADDER.** Five faces, seven roles (two aliases — see §L2.3):

| role | now (px/cap) | **new px** | face | `-s` | lineFeed | cellH | **cap** | arcmin@30cm | Δcap | justification |
|---|---|---|---|---|---|---|---|---|---|---|
| `TXT_TITLE` | 17 / 10 | **19** | sg‑bold | 11 | 19 | 22 | **11** | 24.0′ | +10 % | design "title 14–16 em" → em 14.7 ✔; stays one clear step above BUTTON |
| `TXT_BUTTON` | 12 / 7 | **15** | sg‑bold | 9 | 15 | 18 | **9** | 19.6′ | **+29 %** | design "button 12–14 em" → em 12.0 ✔; 20′ preferred line |
| `TXT_BODY` | 12 / 7 | **15** | sg‑med | 9 | 15 | 18 | **9** | 19.6′ | **+29 %** | design "body 11–13 em" → em 12.0 ✔; = 75 % of game text (L1.2.5) |
| `TXT_SEG` | 10 / 6 | **15** | sg‑med | 9 | 15 | 18 | **9** | 19.6′ | **+50 %** | *merged into BODY* — a segment label is a body word in a pill; measured to fit every cell (§L3.1) |
| `TXT_SECTION` | 9 / 5 | **12** | jbm‑med | 7 | 12 | 15 | **7** | 15.3′ | **+40 %** | design "section 9 em" → em 9.33 ✔; **exactly matches the baked plate captions' cap 7** (L1.3.1) |
| `TXT_CHIP` | 7 / 4 | **12** | jbm‑med | 7 | 12 | 15 | **7** | 15.3′ | **+75 %** | *merged into SECTION* — the biggest single move in the phase; see the deviation at L1.5.5 |
| `TXT_VALUE` | 11 / 7 | **12** | jbm‑bold | 7 | 12 | 15 | **7** | 15.3′ | 0 | design "caps/values 9–10 em" → em 9.33 ✔; weight, not size, is what distinguishes it |

Advance‑width consequences (the input to §L3): `sg‑bold` `n` 6→7 (+17 %), `sg‑med` `n` 6→7 (+17 %)
and SEG 5→7 (+40 %), `jbm‑med` 4→5 (SECTION +25 %) and 3→5 (**CHIP +67 %**), `jbm‑bold` 5→5 (0 %).

**L1.5.3 — Every rung is checked at the bake.** Each new face goes through `bake_checked` with its
declared lineFeed (`sg-bold 11→19`, `sg-bold 9→15`, `sg-med 9→15`, `jbm-med 7→12`, `jbm-bold 7→12`);
all five were baked and parsed this session and produced exactly those lineFeeds, so the guard will
pass and the crispness law is preserved by construction.

**L1.5.4 — TITLE deliberately moves least.** It is the only rung that already cleared the floor
(21.8′). Growing it to 24′ keeps the hierarchy intact without spending layout budget where there is
no complaint. This is the one place the phase is *not* aggressive, on purpose.

**L1.5.5 — DEVIATION, stated explicitly: the mono rung lands at 15.3′, under the 16′ floor.**
The obvious fix is `jbm-med -s 8` (lineFeed 14, cap 8, 17.5′). It was measured and **rejected**:
JetBrains Mono is monospaced, so cap 7→8 costs advance 5→7 (+40 % on *every* mono string), and that
breaks four things at once —
(i) the pause‑top feature‑pill row goes 356 px → **475 px on a 400 px screen** (unfixable without
deleting pills);
(ii) `settings · ZR` 91 px in an 82 px chip, `Link actions need a running game` 224 px in a 219 px
column, `Tilt is set, but this is an Old 3DS…` 364 px in 300;
(iii) the touch explainer goes to 5 wrapped lines and lands on the Preview buttons;
(iv) it desyncs from the **baked** plate captions, which are cap 7 and cannot be re‑rendered.
The mitigations actually taken instead:
* **prose leaves the mono rung entirely** (§L3.3) — every full sentence moves to `TXT_BODY`
  (cap 9, 19.6′), so nothing the user *reads* is at 15.3′; what stays at cap 7 is captions, key
  legends, codes, tags and readouts, which are *glanced at*, and which the design explicitly assigns
  to mono;
* the deficit is 0.7′, i.e. **cap 7 clears 16′ at any viewing distance ≤ 28.6 cm**, which is inside
  the normal handheld range — it is marginal, not absent.
Alternatives with their measured costs are carried to Open Questions (Q1).

---

## L2 — THE SHAPE OF THE CHANGE

### L2.1 — Option (a): one bigger ladder for everyone

Simplest: no new state, no new setting, no second bake, `g_txtTexelScale[]`/`g_txtLineFeed[]` keep
their shape, `assets_text()` keeps its signature, no call site changes meaning. The whole change is
seven numbers in `typography.h`, five lines in `build_assets.sh`, and the geometry in §L3.2.

### L2.2 — Option (b): a user‑selectable TEXT SIZE (Normal / Large) — **rejected**

**L2.2.1 — Byte cost, measured.** A face's `.bcfnt` is a fixed **1024×1024 A4 sheet = 524,288 B**
plus header/CWDH/CMAP, and *every* size from `-s 4` to `-s 16` fits in ONE sheet — so a bake's size
is independent of its point size (527,740 B for either Space Grotesk cut, 530,376 B for either
JetBrains cut, verified across 13 sizes). Therefore:

| | faces | raw in `data/` + ELF | gzip (proxy for the `.cia`) |
|---|---|---|---|
| current ladder | 7 | 3,702,088 B (**3.53 MB**) | 116,767 B (114 KB) |
| proposed ladder | 5 | 2,643,972 B (**2.52 MB**) | 130,416 B (127 KB) |
| **Δ** | −2 | **−1,058,116 B (−1.01 MB)** | **+13,649 B (+13 KB)** |
| a *second* (Large) ladder on top | +5 | **+2.52 MB** | +~130 KB |

`data/` goes 3.69 MB → **2.68 MB**. The `.cia` (2,128,832 B today) would move by roughly +13 KB for
the new ladder alone, or +145 KB with a second ladder — both trivially affordable.

**L2.2.2 — RAM cost, which is the one that actually matters.** Disassembled from the shipped
library: `C2D_FontLoadFromMem` does `linearAlloc(size)` + `memcpy` of the **entire** `.bcfnt`
(`libcitro2d.a font.o`, verified). So each face costs its full ~530 KB of **linear heap at runtime**
*in addition to* its copy in the executable image. Today: 3.53 MB linear + 3.53 MB image. Proposed:
2.52 MB + 2.52 MB (**−2.0 MB of resident memory**). A second ladder loaded eagerly would put it back
to 5.04 MB + 5.04 MB in an app that already carries two GBA cores, two 256×256 frame textures, the
prescale target, the DoF/bloom targets and 66 plate/widget sheets.

**L2.2.3 — The decisive argument is not bytes.** Every box in the app has to fit the **Large**
ladder, or Large is broken. If Large fits, Large is shippable as the only ladder — and then
"Normal" exists solely to offer *less legible* text to a user who has just told us the text is too
small. It doubles the T10 gate surface (93 strings × 2 ladders), doubles the bake, adds a preference
to persist/migrate in `settings.bin`, and adds a font‑reload path (`C2D_FontFree` + re‑`linearAlloc`
mid‑session) that can fail. That is a lot of new failure surface bought with no legibility.

**L2.2.4 — Recommendation: do not build the setting.** The honest handheld affordance already
exists and is free: **`SCALE_1X` / aspect‑fit** for the game, and `hudMode` off/top/bottom/both for
the HUD. If the user still wants control after phase 19, the setting is a *later* phase whose cost
is now documented (Q2).

### L2.3 — Option (c): fewer distinct rungs — **adopted, and it pays for the growth**

**L2.3.1 — The merges.** Once everything is bigger, two distinctions stop earning their keep:

* `TXT_SEG` (was sg‑med 10) **→ `TXT_BODY`** (sg‑med 15). A segment label was only smaller because
  the cells were "tight"; measured, every cell in the app takes the body face (§L3.1). One face
  removed.
* `TXT_CHIP` (was jbm‑med 7) **→ `TXT_SECTION`** (jbm‑med 12). Both are "small mono label"; at
  cap 7 they are the same size, and the design pack's own art draws captions and chips at the same
  cap. One face removed.

**L2.3.2 — The roles STAY in the enum.** `TxtRole` keeps all seven names and `TXT_COUNT` stays 7,
so: every call site is untouched, `g_txtTexelScale[TXT_COUNT]` / `g_txtLineFeed[TXT_COUNT]` keep the
shape the harness reads over GDB, and a future re‑split of a merged role is a one‑line change in
`typo_face()` plus a bake line. Only `typo_face()` and `ASSET_FONTS` learn that two roles resolve to
one baked symbol.

**L2.3.3 — Runtime de‑duplication is REQUIRED, not optional.** `assets.c`'s `ASSET_FONTS` X‑macro
currently does one `C2D_FontLoadFromMem` per role. With aliases that would `linearAlloc` the same
530 KB face twice (L2.2.2). `assets_init` must load each **distinct symbol** once and point both
roles' `s_fonts[]` slots at the same `C2D_Font`, and `assets_exit`/teardown (if any) must free each
handle once. A host test asserts the distinct‑symbol count (§L5.2.4).

### L2.4 — DECISION

> **Ship (a) + (c): one ladder, bigger, on five faces instead of seven. Do not build the setting.**

It answers the user's sentence literally ("everything… bigger"), it is the smallest diff that does
so, it makes the app **1.01 MB smaller on disk and 2.0 MB lighter in RAM**, and it moves the UI
*toward* the design pack rather than away from it. Gold‑plating (the setting) is deferred with its
costs written down.

---

## L3 — THE RE‑FIT

### L3.1 — HORIZONTAL: every string against its real box

**L3.1.1 — Method.** Every string in the app that goes through `assets_text*` / `assets_button` /
`assets_seg` was extracted from `source/*.c` (93 strings incl. every literal in every label table:
`S_SCALE`, `S_FILT`, `S_HUD`, `S_AUD`, `S_TCH`, `S_EDG`, `TILT_NAMES`, `MENU_TAB_NAMES`, the `PK_BTN`
label switch, the 9 pause‑top pills, the wireless/picker/presence/HUD/pad strings) and measured with
the **same arithmetic `test_typography.c` T10 uses** — at texel scale 1.0 a run's width is exactly
`Σ charWidth`, read from the real `.bcfnt` bytes. Boxes are the shipped rects from `uihit.c`, the
`PCtl` tables and the draw sites.

**L3.1.2 — Result: 92 of 93 fit unchanged.** The full table lives in the T10 extension (§L5.2.1);
the notable headroom checks:

| where | role | string | now | **new** | box |
|---|---|---|---|---|---|
| `PICK_SETTINGS` chip | CHIP | `settings · ZR` | 39 | **65** | 82 |
| wireless lobby | CHIP | `× different game` | 48 | **80** | 272 |
| presence card | CHIP | `MAP 3-12   TILE 14,9` | 60 | **100** | 194 |
| pause TOUCH | SEG | `Gamepad` | 37 | **55** | 69 |
| DISPLAY | SEG | `Aspect-fit` | 42 | **61** | 69 |
| co‑op toast | BODY | `Co-op: Hoenn RS vs Hoenn RS — no peer possible` | 218 | **280** | 384 |
| HUD | BODY | `Pokemon LeafGreen` | 89 | **115** | 150 |
| settings | SECTION | `Tilt is set, but this is an Old 3DS - it stays flat.` | 208 | **260** | 300 |
| pause LINK | BUTTON | `Wireless lobby...` | 73 | **96** | 216 |

**L3.1.3 — THE ONE OVERFLOW.**

| where | role | string | new width | box | over |
|---|---|---|---|---|---|
| pause TOUCH `ACT_PREVIEW_PAD` | BUTTON | `Preview Gamepad` | **103** | 101 | **+2** |

**Remedy — level 1 (the box grows).** `PT_TOUCH`'s two preview buttons are `{93,109,101,44}` and
`{201,109,101,44}` with a 7 px gutter and a right edge at 302, while the tab's other full‑width rows
end at 301 and the scrollbar track starts at x=312. Change to **`{93,109,104,44}` and
`{204,109,104,44}`** — 7 px gutter preserved, right edge 308, still 4 px clear of the track. Label
then has 1 px of slack each side. Because `pctl_rects()` feeds both the draw and the hit test from
the same table, the touch target follows automatically.
*No copy is shortened and no role is demoted anywhere in this phase's horizontal pass.*

**L3.1.4 — The one row that needs padding relief, not a box.** The pause‑top feature‑pill row
(`draw_paused_summary`, up to 9 pills) is measured as `Σ(labelW + pad) + gap` and is centred on the
400 px top screen:

| | label pad | inter‑pill gap | total |
|---|---|---|---|
| now (cap 4) | 12 | 5 | 291 px |
| new (cap 7), unchanged padding | 12 | 5 | 383 px (fits, 8 px margins) |
| **new, specified** | **10** | **4** | **356 px** (22 px margins) |

Take pad 12→10 and gap 5→4. Pill height 15→**17**, label y 132→**131** (the box stays 129‑anchored).
Note the row already exceeded the manifest's `x56 w288` band at 291 px; it still does at 356 — an
inherited, accepted deviation, now recorded.

### L3.2 — VERTICAL: this is the real work

Bigger glyphs are *taller* boxes. At texel scale 1.0 a run occupies a quad of `cellHeight`, with ink
inside it at a known offset. Measured for the proposed faces (ink extent over `AHgpy1:9`):

| role | cellH now → new | ink rows within the cell (new) | ink height |
|---|---|---|---|
| TITLE | 19 → **22** | 6 … 19 | 14 |
| BUTTON | 15 → **18** | 5 … 16 | 12 |
| BODY / SEG | 14/12 → **18** | 5 … 16 | 12 |
| SECTION / CHIP | 13/9 → **15** | 4 … 12 | 9 |
| VALUE | 14 → **15** | 4 … 12 | 9 |

**L3.2.1 — HUD bar (both screens) — the only place growth costs gameplay area.**
Today: a 14 px `THEME_HUD_BAR` rect drawn *over* the game image, holding `TXT_BODY` (cellH 14) at
y=0, `TXT_SECTION` (cellH 13) at y=2 and 13 px chips at y=0.5. At the new sizes nothing fits.
**Note the design's own intent:** `plates/manifests.json` → `play-top` places the game‑name text rect
at `y=6 h=14`, fps/clock at `y=7 h=12`, and the **game video rect at `y=27`** — i.e. the design
reserves the top **27 px** for the HUD and letterboxes the game beneath it. We overlay instead of
letterboxing, so we take the middle:

> **HUD bar height 14 → 20 px.** Focus underline moves 14 → 20. Game name `TXT_BODY` at **y=2**
> (ink rows 7…18). Clock/fps `TXT_SECTION` at **y=4** (ink 8…16). `UI_CHIP_H` 13 → **16**, chip y
> 0.5 → **2**. Bottom bar identical.

Cost: **+6 device px of the 240**, i.e. 2.5 % of screen height, covering GBA rows 0–13 instead of
0–9 at the top screen's 1.5× fit. **Accepted**, because (i) it is *less* than the design's own 27 px
band, (ii) the bar is already user‑suppressible per screen (`hudMode` off/top/bottom/both), and
(iii) the alternative is keeping the clock, fps and the game name at 10.9′/15.3′, which is the
complaint. Right‑hand cluster re‑measured: leftmost x moves 230 → **196** (top) and the left cluster
(name + FOCUS + LINK chips) ends at **215** on the 320 px bottom bar against a cluster start of 257 —
both clear.

**L3.2.2 — Status‑hint band + the LINK note (a genuine three‑way squeeze).**
`TXT_SECTION` at y=231 (`statusTxt`) / y=229 (`hintBuf`) with cellH 15 would put ink at 235…243 —
**clipped by the 240 px screen**. And `UIHIT_MENU_VIEW_H = 228` (the fixed‑chrome repaint line) plus
`SET_LINK_NOTE_Y = 215` no longer coexist: `215 + 15 = 230 > 228`, while the LINK tab's last control
(`ACT_PRESENCE` toggle, `y196 h18`) ends at 214 and the note must sit *below* it.
**Remedy — level 1 (geometry), one coordinated edit:**

| constant | now | **new** | why |
|---|---|---|---|
| `UIHIT_MENU_VIEW_H` | 228 | **226** | the hint band grows with the face |
| pause `statusTxt` y | 231 | **226** | ink 230…238, clears 240 |
| `TOUCH_OFF` `hintBuf` y | 229 | **226** | same band |
| `PT_LINK` `ACT_PRESENCE` y | 196 | **192** | frees 4 px; bottom edge 210 |
| `SET_LINK_NOTE_Y` | 215 | **211** | ink 215…223 ≤ 226 ✓, and ≥ 210 ✓ |

T11 must be re‑pointed at the new pair (`211 + 15 ≤ 226` and `211 ≥ 210`).

**L3.2.3 — Touch‑mode explainer (`menu_touch_explainer`).** With prose moved to `TXT_BODY` (§L3.3)
the Smart string wraps to **5** lines at the current 208 px column and lands on the Preview buttons
at y=109. The Preview/swatch/edge rows **must not move**: the `pause-bot-touch` plate *bakes*
"GAMEPAD · COLOR" above the swatch row, so shifting the row desyncs it from its own caption.
**Remedy — level 1 + level 2 combined:**

| constant | now | **new** |
|---|---|---|
| `TEXPL_W` | 208 | **216** (content column is x93…309) |
| `TEXPL_Y` | 66 | **62** |
| `TEXPL_LEAD` | 10 | **15** (= the face's own lineFeed) |
| `TEXPL_LINES` | 4 | **3** |

and **one copy change** (level 2), because "menus/party/targets," is an unbreakable 19‑char token
that forces a 4th line by itself:

> `TOUCH_EXPLAIN[2]` : ~~"Smart — the touch screen is a pointer on the real Gen-3 UI: tap-to-walk,
> tap menus/party/targets, double-tap = START."~~
> → **"Smart — point at the real game UI: tap to walk, tap menus, party and targets. Double-tap =
> START."**

Measured at the new face: Off 3 lines, Gamepad 3, Smart 3. Lines land at y 62/77/92, last ink row
108, Preview buttons at 109 — **1 px clear, nothing below moves**. The host suite pins this
(§L5.2.3).

**L3.2.4 — Presence card (`presence_draw_card`, 214×80).** Rows are hard‑offset at +8/+10/+27/+29/
+44/+56/+67 for a face whose cellH was 9. At cellH 15 the last three rows overlap each other.
**Remedy — level 1 (the box grows; it floats over the game image at y=32 with 100+ px below it):**

| row | role | y now | **y new** |
|---|---|---|---|
| card `H` | — | 80 | **104** |
| name | BUTTON | +8 | **+6** |
| gender | VALUE | +10 | **+8** |
| `ID` / id | CHIP / VALUE | +29 / +27 | **+30 / +30** |
| location | CHIP | +44 | **+48** |
| read‑only note | CHIP | +56 | **+64** |
| union note | CHIP | +67 | **+80** |

Bottom ink 92 ≤ 104. Card spans y 32…136 on the top screen; it draws only while `!menuOpen`, so it
cannot collide with the pause pill row at y129.

**L3.2.5 — `.sav` picker (`rompicker.c` savpicker_run).** `ROW_H 16` with `TXT_BODY` cellH 18 →
rows overlap. Title `TXT_TITLE` cellH 22 at y=6, help `TXT_SECTION` at y=214.
**Remedy — level 1:** `ROW_H` 16 → **20**, `VIS_ROWS` 13 → **9**, rows from y=30 (last row top 190,
ink 195…206, clear of the help line's ink at 218…226). The list already scrolls (`topRow` follow
logic at `rompicker.c:727`), so fewer visible rows costs nothing but a scroll — remedy 3 in
support of remedy 1.

**L3.2.6 — Section captions drawn in code (`menu_ov_label`, `OV_SECTION`).** Drawn at `y − 14`;
with cellH 15 the cell would touch the control. **`y − 17`** (ink at y−13 … y−5). *This also fixes a
pre-existing mismatch*: the baked plate captions are cap 7 and the code‑drawn ones were cap 5 — after
this phase both are cap 7 and the two finally agree (L1.3.2).

**L3.2.7 — `OV_ROW` labels and every "centre a label in a box" site.** `assets_button`,
`assets_seg`, `pad_label`, `menu_ov_label(OV_ROW)` and `touch_chip` all centre on
`(h − typo_role_px(role)) / 2`. With `BODY` px 15 in an 18 px toggle row that yields y+1.5 and ink
that touches the bottom edge. **Add `typo_ink_top(role)` / `typo_ink_h(role)` to `typography.h`**
(constants measured at bake time, guarded exactly like lineFeed) and centre the **ink** box, not the
line box: `y + (h − typo_ink_h(r)) / 2 − typo_ink_top(r)`, rounded to an integer (the integer‑origin
half of R1 is non‑negotiable). One helper, five call sites, and vertical centring becomes correct
for every rung instead of accidentally‑correct for the old ones.

**L3.2.8 — Chips and the menu affordance (`ui.c`, `touch.c`).** `UI_CHIP_H` 13 → **16**;
`touch_chip` box 14 → **17**, label `y+3` → `y+2`; `MCHIP_H` 18 → **20**, label `MCHIP_Y+5` →
`MCHIP_Y+3`, and the hamburger bars re‑centred on the new box. `MCHIP_W` 48 holds: bars 8 + gap 4 +
`menu` 20 = 32 ≤ 48.

**L3.2.9 — Standalone settings screen (`run_settings`).** Hint lines `L / R switch tab` (y210) and
`B done` (y224) at cellH 15 → **y206 / y222** (ink 210…218 and 226…234). Subtitle y48 and the
Old‑3DS warning y62 keep their y (ink 52…60 / 66…74; both clear the first control at y≥26 of the
tab body, which starts below). `Done` pill unchanged (label 29 px in 72).

**L3.2.10 — Wireless (`wireless.c`).** Seat cards 181×52 hold as‑is (name ink sy+11…sy+22, code ink
sy+33…sy+41 ≤ 52). Only the two bottom readouts need air: `fpLine` y209 → **206**, `status` y226 →
**224** (ink 228…236).

**L3.2.11 — Nothing else moves.** ROM‑picker list rows (`ROWH 23.5`, text at y+3 → ink y+8…y+19
inside a 21.5 px card), the picker's slot cards, the splash, the dead‑core panel, the toasts and the
virtual‑gamepad key rects were all re‑checked and have vertical slack at the new cellH. They are in
the capture sweep (§L5.4) but need no edit.

### L3.3 — READABILITY vs 1:1 DESIGN FIDELITY: the one real conflict, and the call

**L3.3.1 — The conflict.** FONTS.md is explicit: *"anything that looks like a readout, code, tag, or
label = JetBrains Mono … Never mix them the other way — that inversion is what breaks the look."*
But the mono rung cannot exceed cap 7 without breaking the layout (L1.5.5), and cap 7 is 15.3′ —
under the floor. Several **full sentences** are currently on that mono rung.

**L3.3.2 — The call: readability wins, and it turns out fidelity does too.** A full sentence is not
"a readout, code, tag or label" — the design's own rule never assigned prose to mono; the app did.
So **prose moves from `TXT_SECTION` to `TXT_BODY`** (cap 9, 19.6′, Space Grotesk — the design's
own body face), and the mono rung keeps exactly what FONTS.md assigns it. Both requirements are
satisfied by the same edit.

**L3.3.3 — The strings that move to `TXT_BODY`** (all re‑measured, all fit):

| site | string | new width | box |
|---|---|---|---|
| `menu_touch_explainer` | all three `TOUCH_EXPLAIN[]` | wraps to 3 lines | 216 |
| `run_settings` | `configure before you pick a game` | 195 | 300 |
| `run_settings` | `Tilt is set, but this is an Old 3DS - it stays flat.` | 263 | 300 |
| `run_settings` / `uihit.h` | `SET_LINK_DISABLED_NOTE` | 194 | 219 |
| `rompicker` empty state | `put .gba files in <ROM_DIR>/` (+`, then Rescan`) | 176 / 254 | 400 / 320 |
| dead‑core panel | `not a valid .gba - pause menu, Change games` (was CHIP) | 261 | 320 |

**L3.3.4 — The strings that STAY mono** (`TXT_SECTION`): section captions (`SCALE · TOP`,
`DIORAMA · TILT`, `GAMEPAD · EDGES`, `NEARBY SESSIONS`), key legends (`L/R tab  A select  B resume
(or tap)` 185 px, `Up/Down: move   A: load   B: cancel` 175 px, `L / R switch tab`, `B done`), the
HUD clock/fps, the picker's `READY`/`A · TOP`/`SAME FILE IN BOTH SLOTS` state tag, ROM codes,
seat tags, the feature pills and every chip. **This is the design's own division, restored.**

**L3.3.5 — Accepted deviations from the design pack, listed so nobody rediscovers them.**
(1) the mono rung is 9 em where the design says 7–7.5 em for *chips* (readability wins; §L1.5.5);
(2) the pause‑top pill row is 356 px against a 288 px manifest band (inherited);
(3) the HUD bar is 20 px overlaid where the design letterboxes at 27 px (§L3.2.1);
(4) `TXT_VALUE` and `TXT_SECTION` are now the same size, differing only in weight — the design
lists them a rung apart, but at cap 7 the quantisation has no rung between them.

---

## L4 — THE DENSE SCREENS, EXPLICITLY

**L4.1 — The six pause tabs.** The plates are **immutable bitmaps** and they bake the tab rail
(cap 8), the section captions (cap 7) and most row labels (cap 8–9). Only the *widgets and the
overlay labels* are code‑drawn. So the plan is: **grow the code‑drawn text to meet the art**, never
the reverse.
* SESSION — three 40–43 px buttons carrying cap‑7 labels today (`evidence/before-pause-session.png`
  shows the mismatch plainly); `Resume`/`Change games`/`Quit` go to BUTTON 15. No geometry change.
* DISPLAY — four segs (`208×30`), two `OV_ROW` toggles. Seg cells at BODY 15: worst case
  `Aspect-fit` 61/69, `bottom` 44/52. `OV_SECTION` offset −14→−17. Content height unchanged, so the
  existing 53 px of scroll is unchanged.
* AUDIO — mode seg + two `PK_STEP` rows; `−`/`+` are BUTTON 15 (`assets_text_c` centred, glyph
  widths 7/8 in a 20 px pad); the `%` value is VALUE 12 right‑aligned at `x+w`, y−16 → **y−18**
  (cellH 15 above a row that starts at y).
* ENHANCE — five toggles + the tilt seg (`170×26`, 4 cells of 42): `Off`/`Low`/`Mid`/`Max` at BODY
  15 are 17–24 px. Caption `DIORAMA · TILT` 70/170.
* LINK — the three‑way squeeze of §L3.2.2 (toggle 196→192, note 215→211, band 228→226).
* TOUCH — the explainer rewrite of §L3.2.3 and the +3 px preview buttons of §L3.1.3.

**L4.2 — The standalone settings screen (`run_settings`).** Same control tables as the pause tabs
(so §L4.1 covers the rows) plus its own chrome: title, subtitle, Old‑3DS warning, two hint lines,
`Done` pill. Only the hint lines move (§L3.2.9); subtitle and warning change *role* (§L3.3.3).

**L4.3 — The ROM picker rows.** `LIST_ROWS 8 × ROWH 23.5` from y=41 — a 23.5 px row for a 12 px‑ink
name. **No change**: name ink lands y+8…y+19 inside the 21.5 px card, the code chip (now cap 7,
`BPEE` 20 px) at y+4 lands y+7…y+16, and the A/B badge sprites are untouched. The bottom screen's
slot cards take `pick a game (d-pad + A)` at 140/250. The `settings · ZR` chip is the tightest
horizontal item in the app at **65/82** and is called out for the capture sweep.

**L4.4 — The in‑game HUD bar.** §L3.2.1. **Decision on how much gameplay area is acceptable:
+6 px (14→20), 2.5 % of screen height, and not one pixel more.** Rationale: the design itself
reserves 27 px; the bar is already suppressible per screen; and a 20 px bar is the minimum that
holds a cap‑9 name and a cap‑7 readout with 2 px of padding. If a future phase wants the bar back at
14 px it must move the name/clock to a *smaller* rung, which is a legibility regression and needs
its own justification.

**L4.5 — The wireless lobby.** Four 181×52 seat cards (2×2, 61 px pitch), two RTT/LOSS value tiles,
the fingerprint line, the status line, and up to four full‑width buttons on the bottom screen.
Widths: `Connect online (soon)` 126/293, `Wireless unavailable — install + run the .CIA` 249/300,
`× different game` 80/272, seat name `GUYA-PLAYER` 80/150. Verticals: cards unchanged, the two
bottom readouts move up 3 and 2 px (§L3.2.10). Scan cards (`by+4` name, `by+20` chip) get
`by+2` / `by+22` so the two ink boxes do not touch.

**L4.6 — The virtual gamepad key glyphs.** `pad_label` draws `A`/`B`/`L`/`R`/`START` with
`TXT_BUTTON`. At 15 px: `START` 37 px in the 64 px key at (128,214,64,22) → centred label ink at
y 222…234, inside the key. `A` 8 px, `L` 7 px in 52–60 px keys. **No geometry change**, and the
glyphs go from cap 7 to cap 9 — the chrome the user stares at for a whole session in Gamepad mode.
The `≡ menu` chip is §L3.2.8.

---

## L5 — PROOF PLAN

Four claims, four independent instruments. A phase‑19 build is not done until all four pass.

### L5.1 — (i) THE CRISPNESS LAW STILL HOLDS

**L5.1.1** — `tools/build_assets.sh` bakes all five faces through `bake_checked` with the declared
lineFeeds (19/15/15/12/12). A wrong `mkbcfnt` fails the *bake*, not the app.
**L5.1.2** — `test_typography` T2 (`FINF.lineFeed == role px`, exact) and T3
(`typo_texel_scale(...) == 1.0f`, no tolerance) extended to the five faces and seven roles including
the two aliases.
**L5.1.3** — On the running app over GDB: `g_txtTexelScale[0..6]` must read **1.0f** for all seven
roles and `g_txtLineFeed[]` must read **{19,15,15,15,12,12,12}**. This is the shipped‑binary proof a
screenshot cannot give.
**L5.1.4** — `tools/fontlab/measure.py`'s objective metric on the NEW faces: **zero off‑grid
pixels** (every rendered pixel a multiple of 17, because an A4 sheet's texels are), `peak == 255`,
edge ramp 2–3 px. Run over the §L3.1 corpus, before/after, into an HTML sheet.
**L5.1.5** — T13 (stem‑snap solidity floor) re‑run: `sharpen.py` is size‑independent, but the LUT
renormalises per face, so all five NEW bakes must clear the floor. A face that does not is a bake
failure, not a tuning note.

### L5.2 — (ii) NO STRING OVERFLOWS — the extended T10 IS the gate

**L5.2.1** — T10 grows from 28 boxes to **≥ 93**: every literal that reaches `assets_text*`,
`assets_button` or `assets_seg`, each against its shipped box. The extraction is mechanical (§L3.1.1)
and the list is reproducible.
**L5.2.2** — T10 keeps its `w <= box` assertion and its >90 % note, and gains a **hard fail at
>98 %** so a string that "just fits" is surfaced before the next copy edit breaks it.
**L5.2.3** — **New T14 — the wrapped paragraph fits its rect.** `uihit_wrap` over `TOUCH_EXPLAIN[]`
at the shipped `TXT_BODY` face, `TEXPL_W`: assert `lines <= TEXPL_LINES` for all three copies **and**
`TEXPL_Y + (TEXPL_LINES-1)*TEXPL_LEAD + inkBottom(BODY) < 109` (the Preview row). This is the guard
that made a copy edit safe; without it the explainer silently grows a 4th line onto a button.
**L5.2.4** — **New T15 — the alias contract.** Assert `TXT_SEG` resolves to the same `sym` as
`TXT_BODY` and `TXT_CHIP` to the same as `TXT_SECTION`; assert `typo_face()` yields exactly **five**
distinct syms; assert `data/` contains exactly those five `fnt_*.bin` and no stale sixth (the T5
idiom, which caught a stale bake before).
**L5.2.5** — **New T16 — vertical fit.** For every fixed‑offset text row enumerated in §L3.2
(HUD bar, hint band, LINK note, presence card, sav picker, explainer, chips), assert
`y + inkTop(role) >= boxTop` and `y + inkBottom(role) <= boxBottom` using ink offsets read from the
shipped bytes. §L3.2 is a table of numbers precisely so this test can be a table of numbers.
**L5.2.6** — T11 re‑pointed at `SET_LINK_NOTE_Y 211` / `UIHIT_MENU_VIEW_H 226` / last‑control
bottom 210. T12's system‑font budget must not increase: the role→BODY moves of §L3.3.3 are
role changes, not new `ui_text` bypasses.
**L5.2.7** — All thirteen app host suites plus the harness host tests stay green:
`clang -std=c11 -Wall -Wextra -O2 -I source test/host/test_*.c -lm` for each, plus
`export DEVKITPRO=/opt/devkitpro; export DEVKITARM=/opt/devkitpro/devkitARM; make -j8`.

### L5.3 — (iii) IT IS VISIBLY BIGGER — before/after captures, same crop

**L5.3.1 — The "before" set is already banked** (`docs/phase19-legible/evidence/`, captured this
session from `3DGBA.3dsx` in Azahar 2125.1.2, reconstructed to native 320×240 / 400×240 with
`native.py`, `g_renderSeq` confirmed advancing):
`before-picker-empty.png`, `before-pause-session.png`, `before-pause-audio.png`,
`before-pause-touch.png`, `before-pause-top.png`.
**L5.3.2** — Re‑capture the identical five screens from the phase‑19 build via the same movie
(`scratchpad/movie_p19b.ctm` recipe: boot → `Start without a game` at (226,192) → START+SELECT →
tab taps at x=40, y=52/83/114/145/176) and publish them side by side with `run sheet`, one caption
per claim.
**L5.3.3 — The objective size check, not an eyeball one.** For each after‑capture, measure the ink
row extent of a named string in the reconstructed native framebuffer and assert the cap height
equals the ladder's (e.g. `Resume` cap 7 → **9**; the status hint's caps 5 → **7**; a pause‑top pill
label 4 → **7**). Judge SIZE from captures, SHARPNESS only from §L5.1.4 — Azahar's 2.25× non‑integer
window upscale adds softness that is not in the app.
**L5.3.4 — The anchor comparison, published.** One sheet putting `evidence/anchor-gba-cap-height-x8.png`
(the game's cap‑9 GBA px = 12 device px) next to the new `TXT_BODY` at cap 9 device px, at the same
magnification. This is the picture that answers the user's sentence.

### L5.4 — (iv) NOTHING CLIPS OR COLLIDES — a capture sweep of every screen touched

**L5.4.1** — Screens in the sweep: splash; ROM picker (dual + single + empty state + the
`settings · ZR` chip); `.sav` picker; the pre‑game settings screen (all five tabs); in‑game top HUD;
in‑game bottom HUD; Gamepad overlay (all three `padEdge` values); Smart chip; the `≡ menu` chip;
paused summary (top) with **all nine pills lit**; all six pause tabs at scroll‑top **and**
scroll‑bottom; presence card; wireless idle / scan / connected; the dead‑core panel.
**L5.4.2** — Worst‑case content, not typical content: longest ROM name, `Wireless lobby...`,
`Co-op: Hoenn RS vs Hoenn RS — no peer possible`, `Tilt is set, but this is an Old 3DS…`,
`SAME FILE IN BOTH SLOTS`, a 100 % volume value, `TILT3` + `3D` + `CO-OP` chips simultaneously.
**L5.4.3** — **All six themes.** Only ink colours differ, but the sweep must include at least one
dense screen per theme (the pause AUDIO tab) to prove no theme's ink lands on a baked label the
bigger glyph now overlaps.
**L5.4.4** — Verdicts are `run compare` diffs against the before‑set with an explicit tolerance, plus
a `run zoom` crop at ≥4× for every string flagged >90 % of its box in T10.
**L5.4.5** — Harness hygiene, binding: `azctl stop` always; one Azahar at a time; `sdmc/dual-gba/`
stays read‑only; bank a BUILDLOG entry to `docs/phase19-legible/BUILDLOG.md` after every slice.
**L5.4.6** — **CLAUDE.md #6 still applies.** Azahar proves geometry, not perception. The phase is
"done" when the user reads the text on the real New 3DS — and the specific question to ask is not
"is it better" but "**can you read the chips and the hint line without leaning in**", because those
are the two rungs this spec knowingly leaves at 15.3′.

---

## Open Questions

**Q1 — Should the mono rung take cap 8 (17.5′) at the cost of a redesign?**
`jbm-med -s 8` clears the ISO floor but costs +40 % on every mono advance and breaks four things
(L1.5.5), the worst being the pause‑top pill row at 475 px on a 400 px screen. Two escapes were
measured and not taken: (a) drop to six pills or two rows; (b) put the small‑label role in
**Space Grotesk Medium pt8** (cap 8, proportional) — which *does* fit everything (pill row 392 px,
`settings · ZR` 66/82, `Tilt is set…` 246/300) but inverts FONTS.md's family rule and would visibly
disagree with the **baked** cap‑7 mono captions on every pause plate. Recommendation: ship cap 7 now,
revisit only if the user reports the chips are still hard to read.

**Q2 — Does the user actually want a TEXT SIZE setting?**
Rejected here as gold‑plating (L2.2), but the cost is now known: +5 faces = +2.52 MB image
+2.52 MB linear heap +~130 KB `.cia`, a `settings.bin` field with migration, a mid‑session font
reload path, and a doubled T10/T16 gate. If the answer to L5.4.6's question is "still a bit small",
the cheapest next step is *another rung up on one ladder*, not a second ladder.

**Q3 — Can the plates be re‑rendered at all?**
Every baked label in `plates/<theme>/*.png` is frozen art with no generator in the repo, which is
what pins `TXT_SECTION` to cap 7 and the rail to cap 8. If the design source can be re‑exported at
the corrected em sizes (or a small tool can re‑typeset the labels, which is *mechanically* plausible
— the label backgrounds are flat `#201830` / `#2A2042`), the mono ceiling of Q1 disappears and the
whole UI could move another rung. Worth asking the design owner before assuming it is impossible.

**Q4 — Which console does the user actually hold?**
The whole arithmetic is pinned to the 132–133 ppi non‑XL panel. On a New 3DS **XL** every number in
L1.1.4 is 1.39× more generous (CHIP would be 12.2′ not 8.7′, BODY 21.3′ not 15.3′), which changes
nothing about the direction but does change how much of the complaint is size versus contrast. One
question to the user ("New 3DS or New 3DS XL?") makes the record exact.

**Q5 — Should `hudMode` gain a "compact" state now that the bar is 20 px?**
The bar can already be turned off per screen, but a user who wants the clock without losing 6 px of
game has no middle option. Out of scope for phase 19; noted because §L3.2.1 is the phase's only
deliberate cost to gameplay area.

**Q6 — Does `typo_ink_top` / `typo_ink_h` (L3.2.7) belong in the bake guard?**
They are measured properties of the shipped bytes, like `lineFeed`. Making `bake_checked` assert
them too would catch a font‑version bump that moves ink inside an unchanged cell — the exact class
of silent rot phase 18 was written to stop. Cheap; proposed but not specified as mandatory.
