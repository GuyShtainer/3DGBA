# SPEC-layout — phase 17 UI fix: layout collisions and the remaining visual defects

**Scope.** Everything in the phase-16 sweep report that is *not* RC1 (the `seg-active` baked-label
overprint), RC2 (`ui_fill`'s plus-sign rounded rect) or RC3 (picker touch). Those three have their
own specs. This one owns **L1–L8**, covering sweep defects **D3, D4, D5 (geometry half), D7, D8,
D9, D13, D14, D15, D16, D17, D18, D19, D20, D22**.

**Method.** Every claim below was re-checked from the pixels in this session: the verifier captures
under `tools/emutest/runs/sweep-verify/`, the design references under
`design_handoff_3dgba_ui/screenshots/`, the plate PNGs under
`design_handoff_3dgba_ui/assets_3ds/plates/indigo/` (sampled per-pixel with PIL), the dynamic-rect
map `design_handoff_3dgba_ui/assets_3ds/plates/manifests.json`, and the source. Where I contradict
the sweep report I say so explicitly.

**Binding constraints carried in from the phase brief.**
Do not touch emulation, threading, the link/net drivers, or the HD-2D render passes.
`source/celiolink.c` keeps an empty diff. All six themes must work. Suites stay green
(celiolink 1259 · netlink 66 · diag 369 · control 6897 · trace_replay 58 · tilt 1694, plus
`bash tools/emutest/tests/run_host_tests.sh`). **No requirement here is done until it has been
re-captured with `tools/emutest/` and the PNG read.** Per-requirement capture recipes are in §10.

---

## 0. Three findings that reframe the whole layout problem

These came out of reading the art and the manifest, and they decide L1, L2, L3 and L8.2.

**F1 — the pause content panel is a SCROLL SURFACE, by design, and the code never implemented it.**
`manifests.json` `_meta.notes["pause-bot-*"]` says verbatim:

> "The content panel (right of the 74px tab rail) scrolls vertically; rects with y+h > 240 sit
> below the fold. Tab rail x:0..74."

and `pause-bot-display`, `pause-bot-audio` and `pause-bot-touch` each carry `"scroll": true`.
The manifest places **DISPLAY**'s two toggles at y=234 and y=263 and **TOUCH**'s edges segment at
y=253 and a "See gamepad" button at y=294 — *below the 240 px fold*. `source/main.c` instead
squashed them onto y=224 (`PT_DISPLAY` line 2175, `PT_TOUCH` line 2211), which is precisely why
they collide with the status hint at y=231. **D8 and D9 are not two bugs; they are one missing
feature.** `main.c:2534` already declares `int menuScroll = 0;` with the comment "content scroll
offset (tall tabs scroll; see menu_layout)" — and nothing ever reads it. The scaffolding was
started and abandoned.

**F2 — the plate PNGs are 240 px tall, so the below-fold art does not exist.**
I measured every plate: all bottom plates are exactly 320×240. `pause-bot-display.png` bakes the
first ~3 pixel rows of its "Swap screens" label at y≈237 and then the canvas ends;
`pause-bot-touch.png` bakes "GAMEPAD · COLOR" at y=178..185 and the first rows of
"GAMEPAD · EDGES" at y≈237. So scrolling cannot be "translate the plate and reveal more art" —
the revealed band must be **painted with the panel background and its labels drawn in code**.
That is cheap: the below-fold band is flat `g_ui.bg` (sampled `(32,24,48)` = `RGB(0x20,0x18,0x30)`
= Indigo `g_ui.bg` exactly, `source/theme.c:12`) and only four labels are missing.

**F3 — both DISPLAY and TOUCH plates bake a scrollbar; AUDIO/SESSION/ENHANCE/LINK do not.**
Column x=315 profile: DISPLAY = track RGB 193 from y=2..191, thumb RGB 250 from y=193..239;
TOUCH = track 193 from y=2..169, thumb 250 from y=171..239; the other four are flat bg (32) for
all 240 rows. So the "near-white strip" (D18) is *baked art on exactly the two scrolling tabs*, and
its thumb is baked at the **bottom** of the track while the tab opens at scroll 0 — the art's own
error. A baked thumb can never track a live scroll position, so the scrollbar must become a
code-drawn element (L8.2), and the baked strip must be covered.

---

## L1 — Pause DISPLAY and TOUCH tabs: rows overprinted by the status hint (D8, D9)

### What I saw

`runs/sweep-verify/rec-tabs/bottom_00110.png` (DISPLAY): the four segment rows are on grid, then at
the bottom edge the "Swap"/"Skip" overlay labels, both toggle knobs and the right-aligned hint
"L/R tab  A select  B resume  (or tap)" are all stacked on the same ~9 px band and the toggles are
cut by the screen edge.
`runs/sweep-verify/rec-tabs/bottom_00210.png` (TOUCH): "Round / Soft / Sharp" and the "EDGES"
overlay label have the hint running through them letter-for-letter, and below them the top two
pixel rows of a baked all-caps caption appear before the screen ends. That baked caption is
`pause-bot-touch.png`'s "GAMEPAD · EDGES" at y≈237 (F2).
References: `screenshots/pause-tab-2-display.png`, `screenshots/pause-tab-6-touch.png`.

### Resolution: **scrolling, not re-flowing**

Re-flowing was considered and rejected: DISPLAY's four 30 px segments plus their four baked
section labels already consume y=12..221, and the design's two toggle rows need 58 px more. There
is nowhere to put them without shrinking rows away from the baked label positions, which breaks the
1:1 contract. The manifest (F1) says scroll. Scroll it is.

### L1.1 — Restore the design's row coordinates

In `source/main.c`, replace the squashed tails with the manifest rects.

`PT_DISPLAY` (main.c:2172-2175) becomes 6 controls:

| # | kind | act | x | y | w | h | overlay label |
|---|------|-----|---|---|---|---|---|
| 0 | PK_SEG(3) | ACT_SCALE_TOP | 93 | 26 | 208 | 30 | — (baked "SCALE · TOP") |
| 1 | PK_SEG(3) | ACT_SCALE_BOT | 93 | 81 | 208 | 30 | — (baked) |
| 2 | PK_SEG(2) | ACT_FILTER | 93 | 136 | 208 | 30 | — (baked) |
| 3 | PK_SEG(4) | ACT_HUD | 93 | 191 | 208 | 30 | — (baked) |
| 4 | PK_TOG | ACT_SWAP | **268** | **234** | 34 | 18 | **"Swap screens"** (below-fold, code-drawn) |
| 5 | PK_TOG | ACT_FS | **268** | **263** | 34 | 18 | **"Frameskip"** (below-fold, code-drawn) |

`PT_TOUCH` (main.c:2208-2211) becomes **6** controls (was 5):

| # | kind | act | x | y | w | h | overlay label |
|---|------|-----|---|---|---|---|---|
| 0 | PK_SEG(3) | ACT_TOUCHMODE | 93 | 26 | 208 | 30 | — (baked "TOUCH MODE") |
| 1 | PK_BTN | ACT_PREVIEW_PAD | 93 | 109 | 101 | 44 | — |
| 2 | PK_BTN | ACT_PREVIEW_SMART | 201 | 109 | 101 | 44 | — |
| 3 | PK_SWATCH | ACT_PADCOL | 93 | **195** | 208 | 26 | — (baked "GAMEPAD · COLOR" at y=178) |
| 4 | PK_SEG(3) | ACT_PADEDGE | **93** | **253** | **208** | **30** | **"GAMEPAD · EDGES"** (below-fold) |
| 5 | PK_BTN | **ACT_SEE_PAD** (new) | 93 | 294 | 208 | 31 | — |

`PT_AUDIO`, `PT_SESSION`, `PT_ENHANCE`, `PT_LINK` are unchanged — their tallest rect ends at
y=224 (ENHANCE's tilt row), so they never scroll.

**L1.1.3 (swatch height).** The manifest gives the swatch row `h=31`; the code uses `h=26` and the
swatches themselves are `y+3 .. y+h-3` = 22 px tall (main.c:4125). Keep **26** and only move `y`
192→195: at h=31 the selection ring (`ui_border(x-3, y-2, 156, h+4)`, main.c:4126) would reach
y=228 and cross into the below-fold divider added in L1.2.3. Note the deviation in the BUILDLOG.

**L1.1.1 (the silent trap).** `PTABN` (main.c:2216) must go `{3, 6, 4, 6, 7, 5}` → `{3, 6, 4, 6, 7, **6**}`.
The comment already at main.c:2213-2215 documents exactly this failure mode: a forgotten count
means the row never draws *and* is never hit-tested, with no error. Same for the swatch's `h`
26→26 (unchanged) and DISPLAY staying at 6.

**L1.1.2.** `ACT_SEE_PAD` is a new action id (append to the enum at main.c:2149-2157, do not
renumber). Its behaviour: `touchMode = TOUCH_PAD; menuOpen = false;` — i.e. "switch to the gamepad
and resume so you can see it", the design's intent for a primary button under the pad settings. In
`run_settings` (which has no session to resume into) it sets `touchMode = TOUCH_PAD` only. Label
"See gamepad", sprite `btn-primary`, colour `g_ui.ink` (see main.c:4107-4119 and 4336-4337 — both
draw sites need the case).

### L1.2 — The scroll model (render side)

Add to `run_session`'s pause block and to `run_settings`:

```
static const short PT_CONTENT_H[6] = { 240, 290, 240, 240, 240, 334 };   // per tab
#define PANEL_X 82            // content column left edge — MEASURED, not the manifest's 74
#define PANEL_W (320 - PANEL_X)
scrollMax = PT_CONTENT_H[tab] - 240;        // 0 for four tabs, 50 for DISPLAY, 94 for TOUCH
```

Content heights derive from the last rect: DISPLAY 281 + 9 pad = 290; TOUCH 325 + 9 = 334.

**`PANEL_X = 82` is measured, and the manifest is wrong here.** `_meta` says "Tab rail x:0..74",
but scanning row y=100 of `pause-bot-display.png`: the rail's panel fill `(42,32,66)` runs x=0..80,
a 1 px edge `(50,39,81)` sits at x=81, and the content background `(32,24,48)` starts at x=82. 74 is
the mockup's CSS value, not the baked art. Cutting at 74 or 86 would either scroll a slice of the
rail or leave an 4 px unscrolled column of content. (The existing touch test `mtp.px < 86` at
main.c:3195/4260 is 4 px generous but harmless — leave it, or tighten to 82 for consistency.)

**L1.2.1 — draw the plate in two pieces so the rail does not scroll.** `assets_draw_plate`
(assets.c:76-80) always blits at (0,0). Add `assets_draw_plate_region(const char* id, float sx,
float sy, float sw, float sh, float dx, float dy)` built on the **existing public helper**
`assets_img_cell` (assets.c:154-161, assets.h:52-54) — no new subtexture arithmetic, no scissor
state to leak (contrast main.c:1278/1289, where citro2d's failure to reset the scissor is already
a documented hazard). Then per frame:

1. rail: `assets_draw_plate_region(plate, 0, 0, PANEL_X, 240, 0, 0)`
2. content: `assets_draw_plate_region(plate, PANEL_X, scroll, PANEL_W, 240 - scroll, PANEL_X, 0)`
3. below-fold band, only when `scroll > 0`:
   `C2D_DrawRectSolid(PANEL_X, 240 - scroll, 0, PANEL_W, scroll, g_ui.bg)`

`g_ui.bg` is pixel-exact against the plate here: I sampled `pause-bot-display.png` at (160,180)
and (200,225) and both are `(32,24,48)`, identical to `g_themePresets[THEME_INDIGO].bg`
(theme.c:30). Geometry is theme-independent per `_meta.geometry_is_theme_independent`, and each
theme's plate is generated from that theme's palette, so this holds by construction for all six —
**but it must be re-verified in capture for whichever second theme gets its plates built** (see
Open Question Q3).

**L1.2.2 — every widget draws at `y - scroll`.** In the draw loops (main.c:4065-4130 pause,
main.c:4315-4342 settings) use `float y = c->y - scroll;`. Skip the control entirely when
`y + h < 0 || y > 240` so nothing paints over the rail row area at the top.

**L1.2.3 — the four below-fold labels are code-drawn** (F2), in the baked labels' own style,
measured off the plates:

| Label | Style measured from the art | Draw at |
|---|---|---|
| "Swap screens" | `pause-bot-link.png` "Link cable": x=93, glyphs y=19..30, white — Space Grotesk ~12 px, `g_ui.text` | `assets_text(FNT_SG_MED, "Swap screens", 93, 237 - scroll, 12.0f, g_ui.text)` |
| "Frameskip" | same | `93, 266 - scroll, 12.0f, g_ui.text` |
| "GAMEPAD · EDGES" | `pause-bot-touch.png` "GAMEPAD · COLOR": x=93, glyphs y=178..185 (8 px caps), RGB (176,168,200) = `#B0A8C8` = `g_ui.dim` exactly | `assets_text(FNT_JBM_MED, "GAMEPAD · EDGES", 93, 239 - scroll, 9.0f, g_ui.dim)` |
| (divider above it) | TOUCH plate bakes a 1 px rule at y=165, x=93..311 above its section label | `ui_fill(93, 226 - scroll, 218, 1, g_ui.line, 0)` |

These replace the `PCtl.ov` overlay for these rows — see L8.3, which makes `ov` render this way
everywhere.

**L1.2.4 — scroll follows the focused row (d-pad path).** After `menuRow` changes, clamp:
if `PT[menuRow].y < scroll + 8` → `scroll = max(0, PT[menuRow].y - 8)`;
if `PT[menuRow].y + PT[menuRow].h > scroll + 232` → `scroll = min(scrollMax, PT[menuRow].y + PT[menuRow].h - 232)`.
Reset `scroll = 0` on every tab change (main.c:3183-3184, 4247-4249) — the existing `menuRow = 0`
lines are the place.

**L1.2.5 — touch drag-scroll and hit-testing are SPEC-input's (I2).** This spec owns the render
transform and guarantees exactly one shared variable: the hit-test at main.c:3197-3207 and
main.c:4262-4269 must compare against `c2->y - scroll`, and the drag handler must write the same
`scroll` this section reads. Do not let the two paths keep separate offsets.

### L1.3 — Move the status/hint line off the bottom screen

The collision only exists because a screen-fixed footer shares space with content. **Move
`tStatus` to the pause TOP screen.**

- Delete the bottom-screen draw at main.c:4131-4132.
- Draw instead on the top screen, inside the `menuOpen` branch, centred:
  `assets_text_c(txtBuf, FNT_JBM_MED, statusOrHint, 200.0f, 214.0f, 9.0f, g_ui.dim)`.
- I verified the space is free: `pause-top.png`'s last baked element is
  "settings on the touch screen ↓" at y≈161..173; y=180..240 is flat scrim.
  `runs/sweep-verify/rec-tabs/top_00080.png` confirms it on the real app.
- The design's own pause-tab references carry **no** bottom-screen footer, so this moves us
  *towards* 1:1, not away.

**L1.3.1** In `run_settings` the same line lives at main.c:4309-4310 on the top screen already
("L / R switch tab" / "B done"); leave it, and drop `run_settings`' bottom-screen "Done" chip
(main.c:4343-4344) *only if* it collides after L1.1 — on TOUCH it now sits at y=224 over the
scrolled content. Move it to the top screen line as "B / tap Done" or keep it and give the
content viewport a 226 px height in `run_settings` only. **Decide from the capture**, do not guess.

### L1.4 — Acceptance

DISPLAY at scroll 0 is pixel-comparable to `pause-tab-2-display.png` (four segments, "Swap
screens" label + toggle just entering at the bottom edge). Scrolled to max, both toggle rows are
fully visible with their labels and nothing is clipped. TOUCH at scroll 0 matches
`pause-tab-6-touch.png` (mode segment, explainer, two Preview buttons, GAMEPAD · COLOR swatches);
scrolled to max shows "GAMEPAD · EDGES" + its 3-way segment + "See gamepad". No text overlaps any
other text on either tab at any scroll position.

---

## L2 — game-select: the "settings · ZR" chip chops the plate's baked footer hint (D7)

### What I saw

`runs/sweep-verify/rec-picker/bottom_00050.png`: an opaque rounded chip reading "settings · ZR"
sits at the bottom-left directly on the baked hint, which then reads "…ove ↑ · ⚡ linked = trade /
battle-ready". Source: `source/rompicker.c:290-291` — `ui_fill(6, 216, 84, 16, g_ui.panel2, 5)`
plus text at (12, 219). Reference `screenshots/03-game-select.png` shows the hint unobstructed and
**no chip at all**.

I measured the plate to find out whether the chip could simply move. `select-dual-bot.png` is
flat background (32,24,48) on every row from y=200 except **y=219..229, x=19..300** — the baked
hint. The START/LINKED buttons are drawn by code at y=174..211. So the free bands are y=212..218
(7 px) and y=230..239 (10 px). Neither holds a 16 px chip. **The chip cannot just move; the footer
has to be recomposed.**

### L2.1 — Decision: suppress the baked hint at runtime and recompose the footer in code

The plate art stays untouched (it is handoff art, and `tools/build_assets.sh:44-50` copies plates
verbatim through `tex3ds`; editing them forks the pack and must be redone per theme). Instead, in
`rompicker_run`, after `assets_draw_plate` (rompicker.c:275) and before the widgets:

```
C2D_DrawRectSolid(0, 210, 0, 320, 30, g_ui.bg);      // cover the baked hint band
```

`g_ui.bg` is exactly the plate's colour there (measured above), so this is invisible.

### L2.2 — Redraw the footer as two lines in the freed 30 px

- **Line 1 (y=212), the design's copy verbatim, centred:**
  `assets_text_c(txtBuf, FNT_JBM_MED, hint, 160.0f, 212.0f, 8.0f, g_ui.dim)` where
  `hint` = `"tap a game above ↑ · ⚡ linked = trade / battle-ready"` in 2-game mode and the
  single-mode plate's own copy `"tap a game above ↑ · full 3D + touch · link locally or online"`
  in 1-game mode (read the exact strings off `select-single-bot.png` before typing them — the
  1-game string above is quoted from the sweep report, not measured by me).
  Baked metrics to match: 8 px JetBrains Mono, x 19..300 → ~281 px wide, colour `g_ui.dim`.
- **Line 2 (y=225), the affordance, left:**
  `ui_fill(6, 224, 88, 14, g_ui.panel2, 5.0f)` + `assets_text(FNT_JBM_MED, "⚙ settings · ZR", 12, 226, 8.0f, g_ui.dim)`.

Bottom edge 238, 2 px margin. The hint moves up 7 px from its baked position; everything else is
identical to the reference.

### L2.3 — The tap target must equal the drawn chip

`rompicker.c:229` currently accepts `tp.py >= 214 && tp.px < 96`, which is the *old* chip. Change
it to the drawn rect `px ∈ [6,94), py ∈ [224,238)`. RC3's spec owns the press/release edge; this
requirement owns only the rectangle, and the two must agree. An affordance whose hitbox is not its
pixels is what produced half of D2.

### L2.4 — Alternative, recorded and rejected

Blank the hint band at **build time** in `tools/build_assets.sh` (it already runs `python3` for
fontTools, so a Pillow pass is not a new class of dependency) and then draw the whole footer in
code. Rejected as the primary: it adds a hard Pillow requirement to a build that currently only
needs fontTools + tex3ds, and it has to run for all six themes' plate sets. Revisit if the runtime
cover rect turns out to be visible on any theme (Q3).

### L2.5 — Acceptance

Bottom screen of the picker at 1×: the hint reads end-to-end with no chopped word, the settings
chip is fully drawn below it and inside the screen, and tapping the chip opens `run_settings`.
Compare against `screenshots/03-game-select.png`.

---

## L3 — Virtual gamepad geometry (D3, D4, D5-geometry)

### What I saw

`runs/sweep-verify/rec-pad2/bottom_00120.png`, and my own crop of its y=200..240 band:

- **L/R sit on top of the HUD text.** The L box (4,4,52,22) covers "+ Pokemon FireRed" drawn at
  (15,1); the R box (264,4,52,22) swallows "17fps 02:00" drawn right-aligned to x=314.
- **START is crossed by the footer hint.** START (128,214,64,22) and the centred
  "START+SELECT · pause menu" at y=229 (main.c:4134-4135) share the same band; the hint's glyphs
  run through the button's lower half.
- **Correction to the report:** the "≡ menu" chip at bottom-right is **not** half off-screen. In
  my crop it is complete and legible at (269..314, 222..236). D4's second sentence overstates.
  START's own bottom border at y=236 is present too; what makes it look cut is the hint crossing
  it. Nothing on the pad is actually clipped by the 240 px edge.
- **The opaque corner blocks are RC2's bug family, but NOT RC2's function.** They come from
  `pad_zone()` at `source/touch.c:41-48`, a *second, independent* copy of the three-rect
  fake-rounded-rect — not `ui_fill`. Fixing `ui.c` alone leaves the gamepad broken. Confirming the
  brief's suspicion: yes, same family; no, not the same call site.

Reference: `screenshots/06-touch-gamepad.png`, manifest unit `pad-bot`.

### L3.1 — In Gamepad mode, the bottom HUD bar does not draw

The design's gamepad bottom screen has **no** game-name/fps row: its top band is exactly
`L | TOUCH · GAMEPAD | R`. The manifest agrees — `pad-bot` lists no name/fps/clock rects, while
`ingame-dual-bot` does.

In `source/main.c:4013`, change `if (hudMode & 2)` to `if ((hudMode & 2) && tmEff != TOUCH_PAD)`.
The pad's own chip (touch.c:82-85) becomes the header. The top screen keeps its full HUD.
This is the whole of D3 — the pad coordinates are already the manifest's and must **not** move.

### L3.2 — In Gamepad and Smart modes, the centred footer hint does not draw

`main.c:4134-4135` draws `tHint` centred at y=229 whenever the menu is closed. In
`06-touch-gamepad.png` and `07-touch-smart.png` the footer hint is absent and the "≡ menu" chip is
the affordance; in `04-in-game-dual.png` (touch = Off) the hint **is** present at y≈230, which is
what we draw today. So gate it: draw `tHint` only when `tmEff == TOUCH_OFF`. That resolves D4
without moving START.

### L3.3 — `pad_zone` gets the same corner fix as `ui_fill`, with one extra constraint

`touch.c:41-48` must stop leaving the four r×r corners unpainted. The constraint the comment at
lines 43-44 records is real and must survive: the zone fill is **translucent** (alpha 0x36 idle /
0x92 pressed, touch.c:63), so the replacement corner geometry must remain **non-overlapping** —
overlapping translucent quads double-blend and the interior goes visibly more opaque than the
edges. A staircase of 2-3 non-overlapping quads per corner satisfies both. RC2's spec owns the
shared routine; this requirement owns the alpha-safety constraint and the second call site.

**L3.3.1** With `padEdge = Sharp` (r = 1.0) the current path already looks right
(`runs/sweep-verify/rec-edge/bottom_00120.png`) — keep that as the control image when verifying
Round.

### L3.4 — Geometry confirmed correct (do not "fix")

All pad rects already match `pad-bot` exactly: L (4,4,52,22), R (264,4,52,22), D-up (33,138,44,30),
D-down (33,192,44,30), D-left (7,162,32,36), D-right (71,162,32,36), A (252,150,60,60),
B (198,176,50,44), START (128,214,64,22), menu chip (269,216,45,18). The D-pad arms *do* share
edges by design (up's bottom 168 vs left/right's top 162) — the "lattice of crossing lines" the
report describes is the corner blocks plus `ui_border` on abutting rects, not wrong coordinates.
After L3.3 lands, re-look before touching any number.

### L3.5 — Acceptance

Compare to `screenshots/06-touch-gamepad.png`: top band shows only L, the gold TOUCH · GAMEPAD
chip and R over the game image with no text under them; no centred footer text anywhere; START and
"≡ menu" both complete; every key a solid rounded shape with no black corner squares at
`padEdge = Round`, indistinguishable in corner treatment from `padEdge = Sharp` except for radius.

---

## L4 — Pause AUDIO: the stray "A"/"B" glyph and the missing numeric level (D15)

### What I saw

`runs/sweep-verify/rec-tabs/bottom_00135.png` and `evidence/zooms/z-audio-vol.png`: a tiny "A"
(resp. "B") floats just above the left end of each volume bar, jammed against the round "−"
button, half on the bar's top edge. There is no number anywhere on the row. The baked plate
already says "VOLUME · A" / "VOLUME · B" as the section labels, so the glyph is a duplicate of
information the art carries.

**Source, exactly.** `source/main.c:4103` (pause) and `source/main.c:4335` (`run_settings`):

```
assets_text_r(txtBuf, FNT_JBM_MED, (c->act==ACT_VOLA)?"A":"B", x+26, y-1, 8.0f, g_ui.dim);
```

Right-aligned to x+26 = 119, at y-1 = 81 for VOL A — i.e. immediately left of the bar origin
(`bx = x+28 = 121`) and 1 px above the row. It is a leftover channel tag from the pre-plate UI.

### L4.1 — Delete both occurrences of the channel-letter draw

Lines main.c:4103 and main.c:4335. Nothing replaces it in place; the baked label is the label.

### L4.2 — Draw the level, at the manifest's rect

`manifests.json` `pause-bot-audio` lists `"vol A value"` at **(298, 66, 11, 12)** and
`"vol B value"` at **(298, 117, 11, 12)** — right-aligned above the right end of each bar.
`screenshots/pause-tab-3-audio.png` shows "80" and "70" there.

In both draw sites, for `PK_STEP`:

```
char lv[8]; snprintf(lv, sizeof lv, "%d", (v * 100 + 128) / 256);   // 0..100
assets_text_r(txtBuf, FNT_JBM_BOLD, lv, 309.0f, (c->act==ACT_VOLA) ? 66.0f : 117.0f, 10.0f, g_ui.dim);
```

`FNT_JBM_BOLD` is the handoff's "caps / values" role (`tools/build_assets.sh:32`), which is what
this is. Right edge 309 = the stepper's right edge (x+w = 93+216). Under L1.2.2 the y is
`- scroll`, but AUDIO never scrolls, so it is a constant here.

**L4.2.1** The step is `±32/256` (main.c:4285) = 12.5 %, so the readout goes
0/13/25/38/50/63/75/88/100. The design's "80"/"70" are not reachable values; that is a mockup
number, not a spec. Show the true value — do not fake the design's digits.

### L4.3 — Acceptance

`pause-tab-3-audio.png` side by side: two volume rows each with "− bar +", a right-aligned number
above the bar's right end, no stray glyph anywhere near the "−" button. Press "+" and read the
number change in the next capture.

---

## L5 — Pause TOUCH: the missing touch-mode explainer (D16)

### What I saw

`runs/sweep-verify/rec-tabs/bottom_00210.png`: between the TOUCH MODE segment (ends y=56) and the
Preview buttons (start y=109) there is a ~53 px band of bare plate.
`screenshots/pause-tab-6-touch.png` puts a 3-line explainer there, and `manifests.json`
`pause-bot-touch` lists it explicitly: `{"label": "touch-mode explainer text", "type": "text",
"x": 93, "y": 66, "w": 208, "h": 32}`. `PT_TOUCH` has no text item — the v3 rewrite dropped it.

### L5.1 — Recover the copy from the design source

The three strings are in `design_handoff_3dgba_ui/prototypes/3DGBA Prototype.dc.html:721-723`
(`touchExplain`), verbatim:

| touchMode | Copy |
|---|---|
| Off (0) | `Off — a touch opens the pause menu. No game input from the touch screen.` |
| Gamepad (1) | `Gamepad — a translucent virtual controller (D-pad, A/B, L/R, START) over game B.` |
| Smart (2) | `Smart — the touch screen is a pointer on the real Gen-3 UI: tap-to-walk, tap menus/party/targets, double-tap = START.` |

The Smart string is the one visible in `pause-tab-6-touch.png`, wrapped to 3 lines.

### L5.2 — Draw it, wrapped, in the manifest rect

Not a `PCtl` (it is not focusable and must not consume a `menuRow`). Draw it in the tab draw block,
after the plate and before/among the widgets, only when `menuTab == 5` (and the equivalent tab in
`run_settings`):

- rect (93, 66, 208, 32), 3 lines max, line height 11 px, baselines y = 66, 77, 88 (`- scroll`).
- `FNT_JBM_MED` at 9.0 px, colour `g_ui.dim` — matching the reference's small mono body copy.
- Word-wrap at 208 px measured with `assets_text_w(buf, FNT_JBM_MED, word, 9.0f)`; truncate with
  "…" if a mode's copy exceeds 3 lines rather than overflowing into the Preview buttons.

**L5.2.1** The explainer must update live when the segment changes — it is the only thing on the
tab that names the active mode in words while RC1 is unfixed, and it stays the *description* after
RC1 lands. Read `touchMode` each frame, no caching.

### L5.3 — Acceptance

`pause-tab-6-touch.png` comparison with Smart selected: three lines of explainer copy filling the
band, no empty gap, no collision with the Preview buttons at y=109. Then flip the segment to
Gamepad and to Off and capture each — the paragraph must change.

---

## L6 — Smart touch mode: no indicator, and a raw debug readout shipped (D17)

### What I saw

`runs/sweep-verify/rec-rom/bottom_00215.png` and `evidence/zooms/z-smart-footer.png`: a cyan
`field p=9,4 key=-` bottom-left in a different font and colour, sharing a baseline with the centred
"START+SELECT · pause menu". No mode chip. No guidance labels.

**Two separate causes, both located.**

1. The debug line is `source/main.c:4042-4056`, drawn unconditionally whenever
   `tmEff == TOUCH_SMART && sm.valid`, in `C2D_Color32(0x42,0xF5,0xD0,0xFF)` at (4, 224). Its own
   comment says "TEMP debug: confirm RAM reads on device".
2. **The mode chip already exists and is simply never called.** `touch_draw()`
   (`source/touch.c:602-609`) draws a gold "TOUCH · SMART POINTER" chip for `TOUCH_SMART` — but the
   call site at `main.c:4039-4041` is `if (tmEff == TOUCH_PAD) touch_draw(...)`. Smart never
   reaches it. This corrects the report's guess ("the mode chip is emitted for PAD only") into a
   one-line fix.

Reference: `screenshots/07-touch-smart.png`.

### L6.1 — Call `touch_draw` for Smart

`main.c:4039` → `if (tmEff == TOUCH_PAD || tmEff == TOUCH_SMART) touch_draw(tmEff, tk, &sm, txtBuf);`
`touch_draw` already branches internally and draws nothing for `TOUCH_OFF`. This alone restores the
"TOUCH · SMART POINTER" chip at the design's top-centre position.

### L6.2 — Put the debug line behind a build flag

In `source/touch.h`, following the `CTL_D5_ENABLE` convention (`source/control.h:51`):

```
#ifndef TOUCH_DIAG_HUD
#define TOUCH_DIAG_HUD 0        /* 1 = draw the raw smart-touch ctx/px/key readout on-screen */
#endif
```

Wrap main.c:4042-4056 in `#if TOUCH_DIAG_HUD ... #endif`. Default **0** for shipping. When enabled,
move it off the footer baseline to (4, 208) so it never shares a line with anything user-facing.
The SD-side logging (`touch_log_sample`, touch.c:598) is untouched — that is the real diagnostic
channel and it stays on.

### L6.3 — Draw the design's Smart guidance labels

`smart-bot`'s manifest entry is deliberately open — `"pointer + transient hints (drawn in code)"`,
(0,0,320,240) — so the geometry below is read off `07-touch-smart.png` (mockup screen frame
x 525..820 / y 47..271, i.e. ÷0.925 horizontally and ÷0.9333 vertically) and is accurate to about
±2 px. Verify in capture and adjust; do not treat these as manifest-exact.

Extend `touch_draw`'s `TOUCH_SMART` branch (touch.c:605-608). All chips: `ui_fill(...,
C2D_Color32(0,0,0,0x96), 4.0f)` background + `FNT_JBM_MED` 8 px label, matching the existing
"≡ menu" chip style (touch.c:86-88).

| Chip | Copy | Rect (device px) | Condition |
|---|---|---|---|
| L6.3.1 mode chip | `TOUCH · SMART POINTER` | centred, y=4, h=14 | always (exists, L6.1) |
| L6.3.2 | `tap menu row` | x=17, y=40, h=12 | `sm.ctx` ∈ {field menu, party, bag} |
| L6.3.3 | `double-tap = START` | x=17, y=196, h=12 | `sm.ctx == field` |
| L6.3.4 | `tap target / party` | right-aligned to x=305, y=196, h=12 | `sm.ctx` ∈ {battle action, battle move, battle target} |
| L6.3.5 | `≡ menu` | — | x=269..314, y=216..234 (manifest `smart-bot`) — add it; PAD has it, Smart does not |

**L6.3.6 — the tap cursor.** `07-touch-smart.png` also shows a gold ring at the last tap point with
a `tap → walk here` chip beside it. That is transient state (`sm.px/sm.py` already carry it). Draw a
2 px `g_ui.acc` ring of radius 7 at the last tap's *screen* coordinates for ~30 frames after a tap,
with the label chip offset +14 px in x, clamped to stay inside 320×240. Mark this **optional for
this phase** — it is the one item here that needs new transient state in `touch.c` and it must not
delay L6.1/L6.2, which are the actual defect.

### L6.4 — Acceptance

Smart mode, in a session, at 1×: a gold "TOUCH · SMART POINTER" chip top-centre, at least the
"≡ menu" chip bottom-right, **no cyan text anywhere**, and no centred footer hint (L3.2).
Compare to `07-touch-smart.png`.

---

## L7 — Session start: uninitialised texture, and the ROM-less dead session (D14, D13)

### L7.1 — The white/grey first frames

**What I saw.** `runs/sweep-verify/rec-rom/top_00133.png`: the HUD bar is fully drawn and reads
"0fps", and the 1:1 video rect — measured at device (80,39)–(320,200), i.e. exactly 240×160 centred
— is solid **white**. Letterboxing is intact around it, confirming the report's correction: the
defect is the *video rect*, not the whole screen.

**Cause, verified.** `setup_core` (`source/main.c:582-596`) does
`memset(e->fb, 0, ...)` for the CPU-side framebuffer, then `C3D_TexInit(&e->tex, 256, 256,
GPU_RGB565)` — and `C3D_TexInit` does **not** clear the allocation. The texture holds whatever was
in linear memory until the first per-frame `GX_DisplayTransfer` uploads a real GBA frame. `render_game`
(main.c:1991-2003) draws `e->tex` from frame 0.

**L7.1.1 — Clear the texture at creation.** In `setup_core`, immediately after `C3D_TexInit`
succeeds and before `e->has_tex = true`:

```
if (e->tex.data) { memset(e->tex.data, 0, e->tex.size); C3D_TexFlush(&e->tex); }
```

`C3D_TexInit` allocates in linear memory (not VRAM), so a `memset` is valid; `C3D_TexFlush`
publishes it to the GPU. `GX_MemoryFill` with `GX_FILL_16BIT_DEPTH` + `gspWaitForPSC0()` is the
alternative if the flush proves insufficient — pick whichever the capture shows working, and say
which in the BUILDLOG. This runs once per session on the main thread before the render loop, so it
costs nothing per frame and touches no worker.

**L7.1.2 — Belt and braces: do not present before the first upload.** Add
`bool everUploaded;` to `EmuInstance`, set it where the per-frame texture upload happens, and in
the session render path draw `clrBg` instead of calling `render_game` while it is false. Black
(theme bg) with the HUD reading 0fps is an honest loading frame; white is a broken one.
**This must not touch `render_game` itself** — gate at the call site, so the HD-2D/tilt path inside
is byte-identical.

**L7.1.3 — Acceptance.** `see rec` from before the session starts through frame ~200: no frame
shows a white or flat-grey video rect. Cite the first frame that shows real game pixels and the
frame immediately before it.

### L7.2 — ROM-less start has no empty state

**What I saw.** `runs/sweep-verify/rec-tabs/bottom_00040.png` (captured after
`azctl clean-fixtures`): the app is in a session — "+ gameB", "59fps 02:00", "tap screen · pause
menu" — over pure black, with no ROM anywhere on the SD card. Nothing tells the user why.

**Cause, verified.** `rompicker.c:194` — `int n = scan_roms(...); if (n == 0) return false;` — and
`main.c:4447-4452`, where a `false` return falls through to hard-coded
`sdmc:/3DGBA/gameA.gba` / `gameB.gba`. `setup_core` then fails to load, `e->core == NULL`, and
`render_game` (main.c:1954) clears to background while the HUD keeps drawing.

**L7.2.1 — Give the picker a real empty state.** When `n == 0`, do **not** return: enter an
empty-state loop inside `rompicker_run` that draws the normal picker plates
(`select-dual-top` / `select-dual-bot`) and, on the top screen, a centred card in place of the
list:

- Title: `assets_text_c(FNT_SG_BOLD, "No games found", 200, 96, 16.0f, g_ui.text)`
- Body:  `assets_text_c(FNT_JBM_MED, "put .gba files in sdmc:/3DGBA/", 200, 122, 9.0f, g_ui.dim)`
- Bottom screen, reusing the picker's own button rects so the layout does not invent geometry:
  - `btn-secondary` (12, 174, 148, 37) "Rescan" → A or tap, re-runs `scan_roms`
  - `btn-ghost` (168, 174, 140, 37) "Start without a game" → returns the current
    defaults-fallback behaviour (see L7.2.3)
  - the L2 footer: hint line "no .gba files in sdmc:/3DGBA · press A to rescan" + the
    "⚙ settings · ZR" chip, unchanged from L2.2 so there is one footer implementation.

**L7.2.2 — Distinguish "no games" from "user cancelled".** Today both return `false` and both boot
phantom default ROMs. Change `rompicker_run`'s contract to a tri-state (enum or an out-param):
`RP_STARTED` / `RP_CANCELLED` / `RP_NO_GAMES`. In `main.c:4446-4452`, `RP_CANCELLED` (START on the
picker) should **exit the session loop**, not silently launch two games the user never chose. This
is a behaviour change beyond the pixels — flag it in the BUILDLOG and get it re-captured.

**L7.2.3 — The harness dependency (do not skip this).** `.claude/skills/emutest/SKILL.md` §2 states
that **Tier A of `smoke.sh` depends on the current behaviour**: "boot ROM-less (the app falls
through the empty picker into a dead-core session), play a synthesized movie that drives the pause
menu". Landing L7.2.1 as written breaks Tier A. That is why "Start without a game" exists as an
explicit ghost button: Tier A's movie gains one button press and keeps working, and the behaviour
becomes labelled instead of accidental. **`bash tools/emutest/tests/run_host_tests.sh` and
`tools/emutest/smoke.sh` must both be re-run after this requirement**, and the Tier-A movie updated
in the same commit. See Q1.

**L7.2.4 — Acceptance.** With `azctl clean-fixtures` applied: the boot lands on a screen that says
"No games found" and names the directory; no session starts by itself; "Start without a game"
still reaches the pause menu so Tier A passes.

---

## L8 — Polish, in descending value-for-effort

### L8.1 — Per-title cartridge chips on the ROM list (D20) — **cheap, do it**

`runs/sweep-verify/rec-picker/top_00050.png`: eight rows, eight identical dark-purple carts with a
green cap. `screenshots/03-game-select.png` colours the cap per title.

`source/rompicker.c:99-102` computes `cart_tint(code)` and **nothing calls it** — the compiler does
not complain because it is `static` and used nowhere, so this shipped silently.

I inspected the sprites: `cart-tinted.png` is 15×20 — body `(51,38,90)` with a green cap band at
**rows 3..5, columns 3..12** — and `cart-blank.png` is the same 15×20 body with **no cap**. So the
fix needs no tint API:

Replace `rompicker.c:266`

```
assets_draw_wgt("cart-tinted", 18.0f, y + 1.0f);
```

with

```
assets_draw_wgt("cart-blank", 18.0f, y + 1.0f);
ui_fill(21.0f, y + 4.0f, 10.0f, 3.0f, cart_tint(codes[gi]), 1.0f);
```

(+3,+3 from the sprite origin, 10×3 — the exact band the art uses.)

**L8.1.1** `cart_tint` (rompicker.c:100) currently maps onto `{PAD_COLOR_1, PAD_COLOR_2,
PAD_COLOR_0, PAD_COLOR_3, PAD_COLOR_4}`. `PAD_COLOR_1` is the fixed Game-A green `#63B23C` — the
design's rule is that green and blue are *role* colours for slots A and B. Drop `PAD_COLOR_1` and
`PAD_COLOR_2` from the cart palette (or shift the hash to a 3-colour set of non-role tints) so a
row's cart can never impersonate the A/B badges drawn at x=360 on the same row
(rompicker.c:268-269). The report flags this collision explicitly.

### L8.2 — The scrollbar (D18) — **falls out of L1, do it there**

The baked strip (F3) scrolls away with the plate under L1.2.1, and its thumb can never track a live
position. So:

- **L8.2.1** After the plate pieces, cover the baked strip: `C2D_DrawRectSolid(310, 0, 0, 10, 240, g_ui.bg)`.
- **L8.2.2** Draw a live scrollbar, only when `scrollMax > 0`:
  - track: `ui_fill(313, 6, 4, 228, g_ui.line, 2.0f)`
  - thumb: height `h = max(24, 228 * 240 / contentH)`, top `y = 6 + (228 - h) * scroll / scrollMax`,
    `ui_fill(313, y, 4, h, g_ui.dim, 2.0f)`
- **L8.2.3** This also fixes the two things D18 actually reports: the track stops being the
  brightest element on the screen (the art's 193/250 greys become `g_ui.line`/`g_ui.dim`, which are
  the design's own tokens, `#3B2E60` / `#B0A8C8` on Indigo), and the thumb sits at the **top** at
  scroll 0 instead of the bottom.

### L8.3 — Overlay row labels vs baked labels (D19) — **cheap, do it**

`runs/sweep-verify/rec-tabs/bottom_00185.png`: on LINK, "Link cable" and "Net link (loopback)" are
large bright title-case (baked) while "CO-OP" is tiny, dim, all-caps. Same on ENHANCE ("TILT") and
TOUCH ("EDGES").

Cause: `main.c:4068` and `main.c:4317` draw `PCtl.ov` as
`assets_text_r(FNT_JBM_MED, ov, x - 6, y + h/2 - 4, 8.0f, g_ui.dim)` — right-aligned to the *left*
of the widget, 8 px mono, dim. The baked labels are, measured off the plates: **x = 93** (content
left edge, not widget-relative), vertically centred on the widget, **Space Grotesk ~12 px**,
`g_ui.text` for toggle-row labels ("Link cable" glyph rows y=19..30 with its toggle at y=16..34);
section headers above segments are the *other* style (JBM 8 px caps at `g_ui.dim`, e.g.
"SCALE · TOP" glyph rows y=12..19 above a widget at y=26).

**L8.3.1** Give `PCtl` a label *style* rather than one hard-coded draw. Two styles suffice:
- `OV_ROW` (toggle rows: CO-OP, Swap screens, Frameskip): `assets_text(FNT_SG_MED, ov, 93, y + (h - 12)/2, 12.0f, g_ui.text)`
- `OV_SECTION` (headers above segments: TILT, EDGES): `assets_text(FNT_JBM_MED, ov, 93, y - 14, 9.0f, g_ui.dim)`

**L8.3.2** Reclassify the existing labels: `"CO-OP"` → `OV_ROW` with copy **"Co-op presence"**
(title case, matching "Link cable"); `"TILT"` → `OV_SECTION` with copy **"DIORAMA · TILT"** (caps,
matching "SCALE · TOP"); `"EDGES"` → `OV_SECTION` with copy **"GAMEPAD · EDGES"** (which is also
exactly what the plate bakes below the fold — L1.2.3 — so use one string constant for both);
`"Swap"`/`"Skip"` → `OV_ROW` with copy "Swap screens" / "Frameskip".

### L8.4 — HUD scrim over the game image (D22) — **DEFER, with the reason**

**What the design says.** `manifests.json` `play-top` puts "game A video (aspect-fit)" at
**(20, 27, 360, 213)** — the video is *inset below* the 27 px HUD bar, which is baked into the
plate. `ingame-dual-bot` does the same on the bottom: (9, 24, 302, 194). Our `render_game`
(main.c:1956-1961) fits into the full (screenW, screenH) from origin (0,0), so at aspect-fit the
video runs under the bar and the bar becomes a translucent scrim over live game pixels — exactly
the seam the report describes. At 1:1 the 240×160 inset falls below the bar by luck, which is the
precondition that qualifies D22.

**Why defer.** Implementing the inset means giving `render_game` an origin offset, and
`render_game` is the function that dispatches to `tilt_draw_image` (main.c:1981, 2001) — the HD-2D
path this phase is forbidden to touch. Any change there also invalidates the tilt suite's 1694
assertions and, per CLAUDE.md's "Done" gate, cannot be signed off without real New-3DS hardware.
That is a bad trade against a cosmetic seam.

**If it is taken anyway**, the minimal shape is: add `float ox, float oy, float vw, float vh` to
`render_game`, pass `(0,0,screenW,screenH)` at every existing call site except the in-game top
screen (and bottom), where the HUD is on and the scale is not 1:1 — then pass the manifest rect.
`tilt_draw_image`'s own arguments must be re-derived from the same rect, and the tilt suite re-run.
**Raise this with the user before doing it** (Q4).

### L8.5 — Not in this spec's scope, recorded so it is not lost

D10 / D12 (Daylight theme dark-on-dark; half-light/half-dark in-game) and D11 (the "3D" badge's
navy-on-black) are real and unassigned by the phase brief's L-list. D10/D12 are the shipping
consequence of `tools/build_assets.sh` baking **one** theme's plates (line 13: `THEME="${1:-indigo}"`)
while text colours come from the live theme; D11 is a role-colour miss (the handoff fixes the 3D
badge at `#a9d4ff` on `#3E86D6`). D21 (splash reads "▶ START" not "▶ TAP TO START") is RC1's baked-
label family. See Q2.

---

## 9. File / function index

| Requirement | File | Function / line |
|---|---|---|
| L1.1, L1.1.1 | `source/main.c` | `PT_DISPLAY` 2172-2175 · `PT_TOUCH` 2208-2211 · `PTABN` 2216 · act enum 2149-2157 |
| L1.2 | `source/main.c` | pause draw block 4058-4132 · `run_settings` draw 4312-4344 |
| L1.2.1 | `source/assets.c` / `.h` | new `assets_draw_plate_region` on `assets_img_cell` (assets.c:154-161) |
| L1.2.4 | `source/main.c` | 3183-3188 (pause nav) · 4247-4252 (settings nav) |
| L1.2.5 | `source/main.c` | hit-tests 3197-3207 · 4262-4269 (**SPEC-input I2 owns the input**) |
| L1.3 | `source/main.c` | delete 4131-4132; draw on top screen in the `menuOpen` branch |
| L2 | `source/rompicker.c` | `rompicker_run` 274-292 (plate 275, chip 290-291, tap 229) |
| L3.1 | `source/main.c` | 4013 (`hudMode & 2`) |
| L3.2 | `source/main.c` | 4133-4136 (`tHint`) |
| L3.3 | `source/touch.c` | `pad_zone` 41-48 (2nd copy of RC2's bug) |
| L4 | `source/main.c` | `PK_STEP` draw 4095-4106 and 4331-4335 |
| L5 | `source/main.c` | TOUCH tab draw block; copy from `prototypes/3DGBA Prototype.dc.html:721-723` |
| L6.1 | `source/main.c` | 4039-4041 (`touch_draw` gate) |
| L6.2 | `source/main.c` / `source/touch.h` | 4042-4056 · new `TOUCH_DIAG_HUD` |
| L6.3 | `source/touch.c` | `touch_draw` 602-609 |
| L7.1 | `source/main.c` | `setup_core` 582-596 |
| L7.2 | `source/rompicker.c` / `source/main.c` | 194 (`n == 0`) · 4446-4452 (defaults fallback) |
| L8.1 | `source/rompicker.c` | 99-102 (`cart_tint`) · 266 (row blit) |
| L8.2 | `source/main.c` | pause draw block (with L1.2) |
| L8.3 | `source/main.c` | 4068 and 4317 (`PCtl.ov` draw) |
| L8.4 | `source/main.c` | `render_game` 1950-2005 — **deferred** |

---

## 10. Verification (the only definition of "done")

One capture per requirement group, each read by the implementer and quoted in
`docs/phase17-uifix/BUILDLOG.md`. Reuse the sweep's own recipes (`sweep/REPORT.md` §7) so the
before/after images are directly comparable, and always finish with `azctl stop`.

| Group | Config / movie | Frames to read | The sentence you must be able to write |
|---|---|---|---|
| L1 | `azctl clean-fixtures`, `m1_tabtour.json` + a drag on DISPLAY/TOUCH | DISPLAY at scroll 0 and scroll max; TOUCH at scroll 0 and scroll max | "Both toggle rows / the EDGES segment + See gamepad are fully visible, labelled, and no text overlaps." |
| L2 | fixtures + `mkroms.py`, `movie_picker_sweep.json` | picker bottom, both modes | "The footer hint reads end-to-end; the settings chip is below it and fully on-screen." |
| L3 | `setbin.py sTop=1 sBot=1 touch=1 padColor=0 padEdge=0`, `m_resume.json` | in-game bottom; plus `padEdge=2` as control | "No HUD text under L/R, no footer text through START, no black corner blocks." |
| L4, L5 | `m1_tabtour.json` | AUDIO tab; TOUCH tab in each of the 3 modes | "A number above each bar's right end, no stray letter; three lines of explainer that change with the mode." |
| L6 | real fixture ROMs, `m_rom.json`, touch = Smart | in-game bottom mid-session | "Gold SMART POINTER chip present, ≡ menu present, zero cyan text." |
| L7.1 | real fixture ROMs, `see rec` started **before** `gdbio resume` | every frame from session start to +200 | "No frame shows a white or grey video rect; first game pixels at frame N." |
| L7.2 | `azctl clean-fixtures` | boot +10 s, both screens | "'No games found' is on screen and no session started." |
| L8.1 | `movie_picker_sweep.json` | picker top | "Each row's cart cap is a different colour and none is the A/B role green/blue." |
| L8.2 | with L1 | DISPLAY at scroll 0 and max | "The thumb is at the top at scroll 0 and at the bottom at scroll max, and is no longer the brightest thing on screen." |
| L8.3 | `m1_tabtour.json` | LINK, ENHANCE, TOUCH | "The overlay label is the same size, weight and colour as the baked labels beside it." |

Use `run compare` against the design screenshot for the framed claims and `run zoom` for anything
under 10 px. One `run sheet` per group is the deliverable evidence.

---

## 11. Open questions

**Q1 — Tier-A smoke depends on the ROM-less dead session (L7.2.3).** The empty state as specified
breaks `smoke.sh` Tier A unless the Tier-A movie gains a "Start without a game" press. Confirm the
approach: (a) ghost button + one-line movie change, as specified; (b) retarget Tier A at
`run_settings` (reachable by ZR from the empty state and it already drives `g_prefs.tiltLevel`,
which is the global Tier A reads); (c) leave the ROM-less fallthrough and ship no empty state.
I recommend (a) with (b) as a follow-up, but this changes harness code, which is outside this
spec's remit.

**Q2 — Who owns D10, D11, D12, D21?** They are confirmed defects and no L-number covers them. D11
is a two-line role-colour fix and could ride along here. D10/D12 need `build_assets.sh` to bake
more than one theme's plates (or the themes to be reduced to the ones whose plates exist) — that is
a phase of its own, and until it lands "all six themes must work" is only satisfiable for *text*
colours, not plates. Confirm the intended reading of the binding rule.

**Q3 — The runtime cover rects (L1.2.1 step 3, L2.1, L8.2.1) assume `g_ui.bg` matches the plate's
background exactly.** Verified pixel-exact for Indigo `(32,24,48)`. It should hold for every theme
because the plates were generated from the same palettes, but only Indigo's plates are baked today,
so it is unverifiable in capture right now. If a second theme's plates are ever built and the
colour is off by even 1/255, these become visible rectangles. Should the cover instead sample the
plate texture, or should we accept the coupling and add a host-side assertion that
`g_themePresets[i].bg` equals the theme's plate pixel at (200,225)?

**Q4 — Is L8.4 (the aspect-fit HUD scrim) in or out?** The design's answer is unambiguous (the
video rect is inset at (20,27,360,213)), but implementing it means adding an origin offset to
`render_game`, which is the HD-2D dispatch site the phase brief puts off-limits, and it invalidates
the tilt suite plus needs hardware sign-off. My recommendation is **out**, deferred to a phase that
can run on hardware. Confirm.

**Q5 — `run_settings`' "Done" chip (L1.3.1).** After the TOUCH tab starts scrolling, the chip at
(244,224,72,14) sits over scrolled content. Move it to the top screen (consistent with L1.3) or
give `run_settings` a 226 px viewport? I lean toward the top screen so both menus have one footer
model, but it is a small 1:1 deviation and worth a decision rather than a guess.

**Q6 — "See gamepad" (L1.1.2) is a new action.** The design puts a `btn-primary` "See gamepad" at
(93,294,208,31) on the TOUCH tab, but the tab already has "Preview Gamepad"/"Preview Smart". Is
"See gamepad" meant to be *set mode + resume the session*, as I specified, or is it redundant with
Preview and should be dropped (leaving `PTABN[5] = 5` and a 304 px content height)? Dropping it is
the smaller change; keeping it is what the manifest lists.
