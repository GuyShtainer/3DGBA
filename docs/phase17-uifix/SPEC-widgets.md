# SPEC-widgets — phase 17 UI fix, the widget/art core

**Owns:** RC1 (the baked "Aspect-fit" overprint) and RC2 (the plus-sign rounded rect), every
defect that is a *symptom* of them, and the theme/contrast class that makes a screen unreadable.
**Covers report defects:** D1, D6, D11, D21 directly; D10 / D12 root-caused with a two-part fix;
D19 partially (the font-path half). **Does not own:** D2 (picker touch — SPEC-input), D3/D4/D5
(gamepad zones), D7/D8/D9/D16/D18 (layout/scroll), D13/D14/D15/D17/D20/D22.

**Binding (repeated so a slice author cannot miss it):** no edits to emulation, threading,
`celiolink.c` / `netlink.c` / gbacore SIO, or the HD-2D passes. `celiolink.c` keeps its empty diff.
All six themes must work. Nothing is "done" until re-captured with `tools/emutest/` and the PNG has
been *read*.

---

## 0. What I opened, and what the pixels say

Everything below is first-hand: I read the source, decoded the art PNGs numerically, and looked at
the sweep captures myself. Numbers here are measurements, not restatements of the report.

| Evidence | What I did | Result |
|---|---|---|
| `design_handoff_3dgba_ui/assets_3ds/widgets/*/seg-active.png` | decoded all 6 themes | 74×25, **"Aspect-fit" baked into the pixels**, dominant fill = the theme accent, corner radius ≈ 6 |
| the whole 42-sprite widget set (contact sheet, 3× nearest) | visual audit, indigo | 20 sprites carry baked text (list in W1.4) |
| `source/assets.c` `assets_seg` / `draw_hslice` | read | active pill = `draw_hslice(seg-active)`, live label centred on top |
| `runs/sweep-verify/rec-tabs/bottom_00135.png` @ (90,22)-(312,60), 3× | cropped + read | "**Solo**" superimposed on "**Aspect-fit**"; neighbours "Mixed"/"Split" clean; track + pill corners *are* correctly rounded (that part is the art) |
| `runs/sweep-verify/zooms/z-seg.png` | read | picker pill: "Aspect-fit" **stretched 2.46×** under "2 Games" |
| `runs/sweep-verify/zooms/z-chiprow.png` | read | 8 pause pills are **literal crosses**: full-height centre block + short side nubs |
| `runs/sweep-verify/zooms/z-swatches.png` | read | 5 swatches = squares with **opaque corner bites**, same shape family |
| `runs/sweep-verify/zooms/z-hud-dot.png` | read | identity dot is a green **plus**; FOCUS chip is a **square-cornered** outline |
| `runs/sweep-verify/rec-tabs/top_00040.png` @ (330,0)-(400,16) | pixel histogram | brightest "3D" glyph pixels (26,40,65)…(51,83,130) on a bar at (6,5,10) → **1.4 : 1 to 2.8 : 1** contrast |
| `runs/sweep-verify/rec-theme/bottom_00135.png` @ (88,105)-(316,200), 2× | cropped + read | Daylight "Save state"/"Load state"/"Load .sav": **indigo `fill-secondary-r8` art (#33265A) under Daylight ink `g_ui.text` #241C33** |
| `runs/sweep-verify/rec-theme/bottom_00030.png` @ (20,143)-(300,178), autocontrast | cropped + read | "Use defaults" is a **single clean render** — `btn-ghost` is *not* blitted (`assets_button` treats it as transparent). No defect. Recorded so nobody chases it. |
| `runs/sweep-verify/rec-tabs/bottom_00185.png` @ (255,30)-(316,120), 4× | cropped + read | LINK toggles are the **art** sprites — perfect stadium + round knob. `ui_toggle()` is dead code. |
| `assets_3ds/plates/indigo/pause-bot-display.png` | sampled | content background is flat **#201830 = `g_ui.bg`** — so a procedural `g_ui.panel` track *is* visible, as the design intends |
| `design_handoff_3dgba_ui/assets_3ds/plates/*` | listed | **all six theme packs exist** (24 plates + 42 widgets each, ≈412 KB PNG per theme) |
| `data/` | `du` | 2.4 MB total; **plates 176 KB + widgets 168 KB** compressed `.t3x`; the rest is fonts |
| `tools/build_assets.sh` | read | builds **one** theme (`THEME="${1:-indigo}"`) — this is the D10/D12 root |

### 0.1 Measured design radii (the target the fix must hit)

Decoded from the indigo art by walking the top-left alpha profile:

| sprite | size | radius |
|---|---|---|
| `seg-active` | 74×25 | **6** |
| `seg-track` | 168×30 | **8** |
| `fill-primary-r8` / `fill-card-r8` | 45×34 | **8–9** |
| `fill-panel-r9` | 45×34 | **10** |
| `toggle-on/off` | 34×19 | **9** (= h/2, a true stadium) |
| `dot-green` | 9×9 | **4** (a circle) |
| `pill-3d` / `pill-dim` | 28/56×18 | **5**, drawn as a **1 px outline, transparent interior** |
| `badge-a/b` | 20×15 | 5 |
| `chip-focus` | 36×14 | 3 |
| `cart-tinted` | 15×20 | 3 |

**Consequence for W2:** the runtime radii already in the code (`r=5` on h=15 pills, `r=3` on h=13
chips, `r=4` on the swatches, `r=2` on the 5×5 dot) are *correct* — they match the design ratio to
within a pixel. **Only the rasterisation is wrong.** The RC2 fix therefore restores fidelity with no
radius re-tuning, except where the code deliberately wants a stadium (dot, toggle knob).

---

## W1 — RC1: the baked example label under every live label

### W1.1 The mechanism, exactly

`source/assets.c:195-204`:

```c
draw_hslice(assets_wgt("seg-track"),  x, y, w, h, h/2);                       // track
draw_hslice(assets_wgt("seg-active"), x+active*ow+2, y+2, ow-4, h-4, (h-4)/2); // pill  <-- baked "Aspect-fit"
for (i…) assets_text_c(buf, FNT_SG_MED, opts[i], …);                          // live label ON TOP
```

`seg-active.png` is the design's *example* pill: 74×25 with the word **Aspect-fit** rendered into
the pixels. `draw_hslice` resamples it to the runtime pill box, so the baked word is geometrically
mangled in a call-site-specific way:

| call site | pill box (w×h) | middle scale | what you see |
|---|---|---|---|
| pause DISPLAY/AUDIO/TOUCH `PK_SEG` w=208–216 h=30, n=3 | ≈65×26 | **0.82× squeeze** | tight double-strike (`z-seg-audio`: "Solo" over "Aspect-fit") |
| pause `PK_SEG` HUD n=4 | ≈48×26 | 0.46× | illegible blob |
| pause EDGES row h=**14**, n=3 | ≈53×10 | 0.68× x, **0.40× y** | squashed smear (compounds D9) |
| picker mode pill w=296 h=30 n=2 | ≈144×26 | **2.46× stretch** | the wide smear in `z-seg.png` |

Only the two SCALE rows escape by coincidence — their correct word *is* "Aspect-fit" — and even
those read as double-struck because the baked glyphs and the live glyphs are at different scales.

### W1.2 Decision: draw the segmented control **procedurally** from theme tokens (option b)

I decoded every theme's art and compared it to `g_themePresets[]` in `source/theme.c`:

| theme | `seg-active` dominant | `g_ui.acc` | `seg-track` dominant | `g_ui.panel` |
|---|---|---|---|---|
| indigo | `#F5D042` | `#F5D042` ✅ | `#2A2042` | `#2A2042` ✅ |
| oled | `#F5D042` | `#F5D042` ✅ | `#14171F` | `#14171F` ✅ |
| daylight | `#BE7A16` | `#BE7A16` ✅ | `#FFFFFF` | `#FFFFFF` ✅ |
| duo | `#63B23C` | `#63B23C` ✅ | `#1B222C` | `#1B222C` ✅ |
| retro | `#E7B84A` | `#E7B84A` ✅ | `#341C5E` | `#341C5E` ✅ |
| custom | `#35D0BA` (the pack's teal seed) | generated from the user's hues | `#243B33` | generated |

**The art is a flat fill of the theme token, plus a rounded outline, plus the example word.** So a
procedural rounded rect in `g_ui.acc` on a procedural rounded rect in `g_ui.panel` is *pixel-equal
to the shipped art in all five fixed themes*, and is **more** correct for `custom` (where the baked
teal cannot follow the user's hues at all).

Three further facts make procedural the only workable choice:

1. **Only one theme's art is built.** `tools/build_assets.sh` bakes `$THEME` (default `indigo`)
   into `data/`. Any sprite-based fix is wrong in 5 of the 6 themes *today* — it would violate the
   phase's "all six themes must work" rule by construction. (W4 addresses the art side separately;
   the segmented control must not wait on it.)
2. **No build-pipeline change is needed** — which is the point. Options (a) "emit a label-free
   `seg-active` in `build_assets.sh`" and (c) "3-slice from text-free columns" both require a new
   image-processing step in a shell script, must be redone per theme, and still leave the pill
   locked to the baked palette. Rejected.
3. It removes 2 texture binds + 6 sub-rect draws per segmented control, replacing them with 22
   solid quads (W2.6) — cheaper, and it makes the EDGES row (h=14) correct instead of squashed.

**Trade-off, stated honestly:** we lose the art's 1 px inner highlight ring on the active pill
(`#8A7438`-family pixels, 11 px of 1850 in the indigo sprite ≈ 0.6 % of the pill). At 1× on a 240 p
panel that ring is a sub-pixel AA artefact of the design tool, not a design element — it is absent
from `screenshots/pause-tab-2-display.png` at 1:1. If a later pass wants it back, it is a
`ui_round_outline` in a 25 %-lightened accent, not a reason to keep the baked word.

### W1.3 The implementation

`source/assets.c` — replace the body of `assets_seg` (keep the signature; three call sites,
`main.c:4091`, `main.c:4330`, `rompicker.c:278`, stay untouched):

```c
// Pill radius from the design art: seg-track 168x30 -> r 8, seg-active 74x25 -> r 6  (ratio ~0.26).
static float seg_r(float h) { float r = 0.26f * h; return r < 3.0f ? 3.0f : (r > 8.0f ? 8.0f : r); }

void assets_seg(C2D_TextBuf buf, float x, float y, float w, float h,
                const char* const* opts, int n, int active, u32 inkA, u32 dim) {
	ui_fill(x, y, w, h, g_ui.panel, seg_r(h));                       // track  (was seg-track sprite)
	float ow = w / (float)n;
	if (active >= 0 && active < n)                                    // active (was seg-active sprite)
		ui_fill(x + active * ow + 2.0f, y + 2.0f, ow - 4.0f, h - 4.0f, g_ui.acc, seg_r(h - 4.0f));
	for (int i = 0; i < n; i++)
		assets_text_c(buf, FNT_SG_MED, opts[i], x + i * ow + ow / 2.0f, y + h / 2.0f - 5.0f,
		              10.0f, i == active ? inkA : dim);
}
```

- `assets.c` already `#include "ui.h"` (for `ui_border`), so no new dependency.
- `ui_fill` must be the **W2-corrected** one; ship W2 first or in the same slice.
- Radii produced: h=30 → track 7.8 / pill 6.8 (art: 8 / 6 ✅); h=26 → 6.8 / 5.7; h=14 →
  3.6 / 3.0 (clamped, and further clamped to h/2 by `ui_round_clamp_r`).
- `draw_hslice` becomes **unreferenced** once this lands (it was `assets_seg`-only). **Delete it**
  along with the now-dead `seg-active` / `seg-track` handling — do not leave a dead sprite path
  that a future author can re-wire. (`assets_fill9` / `draw_9slice` stay: they back the buttons,
  the volume steppers, the selected list row and the wireless seat cards, and are correct.)

### W1.4 Baked-text audit of the whole widget set (required by the task)

I read all 42 indigo sprites at 3×. **20 carry baked text:**

`btn-primary` "▶ START" · `btn-primary-focus` "▶ START" · `btn-secondary` "Change games" ·
`btn-secondary-focus` "Change games" · `btn-ghost` "Use defaults" · `btn-ghost-focus`
"Use defaults" · `btn-destructive` "⏻ Quit" · `btn-destructive-focus` "⏻ Quit" ·
`btn-accent-outline` "⚡ LINKED" · `chip-3d` "3D" · `chip-3dtop` "3D on top" · `chip-focus`
"●FOCUS" · `chip-link` "⚡LINK" · `pill-3d` "3D" · `pill-dof` "DoF" · `pill-green` "Gamepad" ·
`pill-dim` "Link Off" · `seg-active` "Aspect-fit" · `seg-example` "1:1 | Aspect-fit | Stretch" ·
plus `logo` / `wordmark` (art, legitimate) and `badge-a` / `badge-b` ("A"/"B" — a **fixed** label,
legitimate).

**Text-free (the pack's intended runtime set):** `fill-primary-r8`, `fill-secondary-r8`,
`fill-card-r8`, `fill-panel-r9`, `seg-track`, `toggle-on/off`, `dot-*`, `cart-*`, `battery-*`.
The design pack's own convention is therefore: *`fill-*-r8` are the label-free 9-slice bodies; the
`btn-*` / `chip-*` / `pill-*` / `seg-active` sprites are example renders for the gallery.*
`seg-example` (188×31) is the **gallery composite** of a whole 3-option control — reference only,
never a runtime asset.

**W1.4.1 — what the app actually blits.** I grepped every `assets_draw_wgt` / `assets_wgt` /
`assets_button` call site. The runtime set is: `dot-*` (wireless), `cart-tinted`, `badge-a/b`,
`peer-walk-*`, the four `fill-*-r8` 9-slices, `seg-track` + `seg-active` (`assets_seg`), and
`btn-primary` in `run_splash`. `assets_button` already routes `btn-primary`/`btn-secondary` to the
label-free fills, treats `btn-ghost` as transparent, and gives `btn-accent-outline`/`btn-destructive`
a `fill-card-r8` base + `ui_border` — **so its `else assets_draw_wgt_fit(sprite,…)` fallback is
unreachable.** Verified in pixels (§0, "Use defaults" is a single clean render).
⇒ **Exactly two baked labels reach the screen: `seg-active` (D1) and `btn-primary` in the splash
(D21).** Nothing else needs changing, and W1.5 is the complete remainder.

### W1.5 D21 is not a copy difference — it is a depth-order bug (learn-skill invariant 1)

`source/main.c:4370-4376`:

```c
C2D_Image b = assets_wgt("btn-primary");
if (b.tex) C2D_DrawImageAt(b, 72.0f, 101.0f, 0.6f, NULL, 175/121.f, 42/38.f);   // depth 0.6
…
assets_text_c(txtBuf, FNT_SG_BOLD, "TAP TO START", 159.0f, 113.0f, 15.0f, g_ui.ink);  // depth 0.0
```

citro2d's depth test is `GEQUAL` — **larger depth wins**. The sprite is submitted at **z = 0.6**
and the label at z = 0.0, so the baked "▶ START" *hides* the correct "TAP TO START" that the code
already draws. The report's "the label is baked and the code-drawn label is not visible" is right
about the symptom and wrong about the cause: the label is not missing, it is occluded. This is
verbatim the invariant recorded in the learn skill's `design-handoff-implementation.md` §1 ("the
single biggest bug"), reappearing.

**Fix (one line, kills both halves):**

```c
assets_button(txtBuf, "btn-primary", 72.0f, 101.0f, 175.0f, 42.0f,
              "TAP TO START", FNT_SG_BOLD, 15.0f, g_ui.ink, 0);
```

`assets_button` 9-slices the label-free `fill-primary-r8` at depth 0.0 and centres the label after
it — correct order, correct copy, no baked word, and it 9-slices instead of inflating a 121 px
sprite to 175 px (learn invariant 2). The `pulse` local becomes genuinely unused; either wire it
(`assets_button` has no tint parameter — a `ui_fill` overlay at `pulse` alpha would do) or delete it
and its `(void)pulse;`. **Prefer deleting**: the design's splash button does not pulse.

**W1.5.1 — sweep the whole file for the same z-order class.** `C2D_DrawImageAt(..., 0.6f, ...)` at
`main.c:4372` is the only non-zero UI depth I found, but the slice must `grep -n "C2D_DrawImageAt\|
C2D_DrawRectSolid\|C2D_DrawText" source/*.c` and assert every UI draw passes depth `0.0f`. Record
the grep output in the BUILDLOG. (The HD-2D passes legitimately use depth — do not touch them.)

### W1.6 Acceptance (W1)

1. `seg-active.png` and `seg-track.png` are no longer referenced anywhere in `source/`
   (`grep -c 'seg-active\|seg-track' source/` → 0).
2. **Capture proof, Indigo:** pause AUDIO tab — the selected chip reads exactly `Solo` (or the live
   value), single-struck, on an accent pill; neighbours `Mixed`/`Split` unchanged. Compare the crop
   against `design_handoff_3dgba_ui/screenshots/pause-tab-3-audio.png`.
3. **Capture proof, the stretch case:** picker mode pill reads exactly `1 Game` / `2 Games`.
4. **Capture proof, the squash case:** pause TOUCH tab EDGES row (h=14) reads `Round`/`Soft`/`Sharp`
   with the selected one legible.
5. **Capture proof, per theme:** the same pause DISPLAY frame in all 6 themes; in each, the selected
   option's word is readable (W4.6 measures this numerically).
6. **Capture proof, splash:** the boot splash button reads `TAP TO START`, matching
   `screenshots/01-boot-splash.png`.

---

## W2 — RC2: `ui_fill` is a plus sign, not a rounded rect

### W2.1 The mechanism, exactly

`source/ui.c:5-10`:

```c
void ui_fill(float x, float y, float w, float h, u32 col, float r) {
	if (r <= 0.0f || w <= 2.0f*r || h <= 2.0f*r) { C2D_DrawRectSolid(x,y,0,w,h,col); return; }
	C2D_DrawRectSolid(x + r,       y,     0, w - 2*r, h,       col);   // full-height centre
	C2D_DrawRectSolid(x,           y + r, 0, r,       h - 2*r, col);   // left strip
	C2D_DrawRectSolid(x + w - r,   y + r, 0, r,       h - 2*r, col);   // right strip
}
```

That is **"rectangle minus four square corners of side r"**, not a rounded rect. A true rounded
corner removes only `r²(1 − π/4) ≈ 0.215 r²`; this removes `r²` — **4.65× too much**, and it removes
it as a hard square bite rather than an arc. The error is invisible while `r ≪ min(w,h)/2` and
becomes a literal cross as `r → min(w,h)/2`. The existing guard fires only at the exact degenerate
`w ≤ 2r || h ≤ 2r`, which is one step *past* the worst case — so the worst shapes are precisely the
ones it lets through.

Worked, against my own captures:

| call site | w×h, r | centre column | side nubs | shape |
|---|---|---|---|---|
| `ui_dot` (HUD identity) `main.c:3830,4016` | 5×5, r=2 | 1×5 | 2×1 | **a plus** (`z-hud-dot.png`) |
| pause feature pills `main.c:2343` | ~22–46 × 15, r=5 | (w−10)×15 | 5×5 | **a cross** (`z-chiprow.png`) |
| gamepad swatches `main.c:4125,4339` | 22×20, r=4 | 14×20 | 4×12 | square with 4×4 corner bites (`z-swatches.png`) |
| `ui_toggle` knob (dead code) | 10×10, r=4 | 2×10 | 4×2 | a plus |
| `ui_chip_fill_w` | w×13, r=3 | (w−6)×13 | 3×7 | mild bites |
| "Done" chip `main.c:4343` | 72×14, r=5 | 62×14 | 5×4 | visible bites |
| settings affordance `rompicker.c:290` | 84×16, r=5 | 74×16 | 5×6 | visible bites |
| volume bar `main.c:4101` | bw×6, r=3 | — | — | guard fires → **square bar** (design wants a stadium) |

### W2.2 The corrected geometry — a pure-C module, `source/uigeom.{c,h}`

`ui.c` includes `<citro2d.h>`, so it cannot be host-tested. Per CLAUDE.md rule 4 (and the pattern
`tilt.{c,h}` / `control.{c,h}` already follow), the **decision** — which quads, for a given
`w/h/r` — moves into a header-free module; `ui.c` keeps only the blitting.

```c
// uigeom.h — PURE C: <stdint.h>/<math.h> only. No libctru, no citro2d, no globals.
// Dual-compiles on the PC host harness (test/host/test_uigeom.c).
#pragma once
#include <stdint.h>

typedef struct { float x, y, w, h; } UiQuad;

#define UI_ROUND_STEPS_MAX 6                               /* staircase bands per cap        */
#define UI_ROUND_MAX_QUADS (1 + 2 * UI_ROUND_STEPS_MAX)    /* = 13, the fill worst case      */
#define UI_OUTLINE_MAX_QUADS (2 * (2 * UI_ROUND_STEPS_MAX + 1))  /* = 26, the outline case   */

float ui_round_clamp_r(float w, float h, float r);   /* clamp to [0, min(w,h)/2]             */
int   ui_round_steps(float r);                       /* bands per cap: clamp(ceil(r),1,MAX)  */

/* Emit a non-overlapping quad decomposition of the rounded rect. Returns the count
   (0 if degenerate). `cap` must be >= UI_ROUND_MAX_QUADS. */
int ui_round_rect_quads(float x, float y, float w, float h, float r, UiQuad* out, int cap);

/* Same shape as a `t`-thick INSIDE outline (transparent interior). Run-length merged.
   `cap` must be >= UI_OUTLINE_MAX_QUADS. */
int ui_round_outline_quads(float x, float y, float w, float h, float r, float t,
                           UiQuad* out, int cap);
```

**Algorithm (fill).** With `R = ui_round_clamp_r(w,h,r)`, `N = ui_round_steps(R)`, `s = R/N`:

```
band k (0..N-1):  dy      = R - (k + 0.5f) * s            // midpoint sampling of the corner circle
                  inset_k = R - sqrtf(R*R - dy*dy)
  top    quad:  ( x + inset_k, y + k*s,             w - 2*inset_k, s )
  bottom quad:  ( x + inset_k, y + h - (k+1)*s,     w - 2*inset_k, s )
body (iff h - 2R > 0):  ( x, y + R, w, h - 2R )
```

Why this shape:

- **Non-overlapping by construction.** Bands occupy disjoint y-ranges `[y+ks, y+(k+1)s)`, the body
  occupies `[y+R, y+h−R)`. This is *load-bearing*: `touch.c:84/87/607` fill with black at α=0x96,
  and overlapping quads would double-darken. `ui.h`'s current comment already promises
  "non-overlapping … safe for alpha"; the new implementation must keep that promise and a host test
  must enforce it.
- **Vertically exact.** `2·N·s + (h − 2R) = 2R + h − 2R = h`. No hairline seam at the cap/body join.
- **`inset_k < R` always** (`dy < R ⇒ sqrt(R²−dy²) > 0`), so `w − 2·inset_k > w − 2R ≥ 0`: no
  negative-width quad is ever emitted, no clamp needed.
- **`N = ceil(R)` capped at 6** makes every band ≈1 device pixel — the finest step a 240 p panel can
  show. Every real radius in the app is 2…8, so `s = R/ceil(R)` is exactly 1.0 for integral `r`
  (all current call sites), i.e. the staircase lands on pixel boundaries.

**Degenerate inputs:** `w ≤ 0 || h ≤ 0` → 0 quads. `R < 0.5` → 1 quad = the whole rect (the old
fast path, kept). `2R == min(w,h)` (a circle or a stadium) → body is skipped on the short axis and
the formula still holds — this is what turns `ui_dot(5,5,r=2)` from a plus into a disc and lets
the volume bar (h=6, r=3) become the stadium the design draws.

**Algorithm (outline).** Walk the same band list for the outer shape `(x,y,w,h,R)` and the inner
shape `(x+t, y+t, w−2t, h−2t, max(R−t,0))` at the same y-steps; per band emit the left piece
`(x+o_k, y_k, i_k − o_k, s)` and the right piece `(x+w−i_k′, y_k, i_k′ − o_k′, s)`, then run-length
merge adjacent bands with equal insets (the straight middle collapses to one pair). Bands where the
inner shape is empty emit one full-width quad (the extreme cap rows). Bound: 26 quads.

### W2.3 `ui.c` after the change

```c
#include "uigeom.h"

void ui_fill(float x, float y, float w, float h, u32 col, float r) {
	UiQuad q[UI_ROUND_MAX_QUADS];
	int n = ui_round_rect_quads(x, y, w, h, r, q, UI_ROUND_MAX_QUADS);
	for (int i = 0; i < n; i++) C2D_DrawRectSolid(q[i].x, q[i].y, 0.0f, q[i].w, q[i].h, col);
}

void ui_border_round(float x, float y, float w, float h, u32 col, float t, float r) {
	UiQuad q[UI_OUTLINE_MAX_QUADS];
	int n = ui_round_outline_quads(x, y, w, h, r, t, q, UI_OUTLINE_MAX_QUADS);
	for (int i = 0; i < n; i++) C2D_DrawRectSolid(q[i].x, q[i].y, 0.0f, q[i].w, q[i].h, col);
}
```

`ui_border` (square, 4 strips) **stays** — `assets_button`'s focus ring, the `PK_*` selection rings
and the `ui_panel` frame all want a square frame around a 9-sliced art body, and changing those is
out of scope. `ui_border_round` is the new sibling W3 needs.

### W2.4 Quad cost per call (state this in the BUILDLOG; it is the perf argument)

| shape | R | N | quads (fill) | quads (outline) |
|---|---|---|---|---|
| `ui_dot` 5×5 | 2 | 2 | **4** (2+2, body skipped: h−2R=1 → 5) | — |
| feature pill 22–46×15 | 5 | 5 | **11** | 22 |
| swatch 22×20 | 4 | 4 | **9** | — |
| chip fill w×13 | 3 | 3 | **7** | 14 |
| seg track 208×30 | 7.8 | 6 (capped) | **13** | — |
| seg active 65×26 | 6.8 | 6 | **13** | — |
| worst case | any | 6 | **13** | **26** |

Per-frame worst cases: **pause top screen** 9 feature pills × 11 = 99 quads (+ the outline variant
if W3.5 is taken: 198). **In-game HUD** (the only 60 Hz hot path): 1 dot (4) + up to 4 chips ×
26 outline = **108 quads**. `C2D_DrawRectSolid` is one batched quad with no texture bind and no
state change between same-colour draws; 100–200 quads is far below citro2d's per-frame envelope and
replaces 6 *textured* sub-rect draws in `assets_seg`. **Net: fewer state changes, more quads, no
expected regression.** Per CLAUDE.md #6 the frame-budget claim is only *proven* on real New 3DS —
record it as an analytic argument in the BUILDLOG and flag it for the hardware pass.

### W2.5 Callers, and what each must look like after

| caller | file:line | shape now | shape after |
|---|---|---|---|
| `ui_dot` — HUD identity mark | `ui.c:99` ← `main.c:3830, 4016` | green **plus** | a 5 px **disc**, matching `04-in-game-dual.png` |
| feature pills | `main.c:2343` | **crosses** | rounded pills, r5 on h15 (art ratio ✓) — and see W3.5 |
| gamepad swatches | `main.c:4125, 4339` | corner-bitten squares | rounded squares, r4 on 22×20 |
| `ui_chip_fill_w` | `ui.c:54` ← `main.c:1606` (presence name chip) | mild bites | rounded chip r3 (art `chip-focus` = 3 ✓) |
| "Done" chip | `main.c:4343` | bites | rounded r5 |
| settings affordance | `rompicker.c:290` | bites | rounded r5 |
| slot-card cart chip | `rompicker.c:136` | bites | rounded r2 |
| `ui_panel` fill (slot cards, presence card) | `ui.c:20` ← `rompicker.c:134`, `main.c:1642` | bites at r4/r6 | rounded fill inside the square `ui_border` frame **(unchanged frame — verify the fill does not now show through at the corners; if it does, W3.6)** |
| volume bar track/fill | `main.c:4101-4102, 4333-4334` | guard → square bar | **stadium** (r=3 = h/2), matching `pause-tab-3-audio.png` |
| touch HUD scrims | `touch.c:84, 87, 607` | bites | rounded, α=0x96 **must not darken at the corners** — the non-overlap invariant |
| `ui_toggle`, `ui_segmented`, `ui_cart` | `ui.c:84, 67, 90` | — | **dead code, no call sites.** Fix them anyway (public API) or delete them; deleting is cleaner. Decide in the slice and record it. |

### W2.6 Host test plan — `test/host/test_uigeom.c`

Compile line goes in the file header, matching the house style:

```
clang -std=c11 -Wall -Wextra -O2 -I source test/host/test_uigeom.c source/uigeom.c -lm -o /tmp/tug && /tmp/tug
```

| TEST | Requirement | Assertion |
|---|---|---|
| **T1** | W2.2 clamp | `ui_round_clamp_r` never exceeds `min(w,h)/2`; negative/NaN-free; `r=0 → 0` |
| **T2** | W2.2 non-overlap | over a matrix `w,h ∈ {5,10,13,14,15,16,20,22,26,30,46,84,208,296}`, `r ∈ {0,1,2,3,4,5,6,8,h/2}`: **no two emitted quads intersect** (area > 0) |
| **T3** | W2.2 exact vertical cover | `Σ band heights ×2 + body height == h` to 1e-4; the union's y-extent is exactly `[y, y+h]` |
| **T4** | W2.2 bounds | every quad lies inside `[x,x+w]×[y,y+h]`; no quad has `w ≤ 0` or `h ≤ 0`; count ≤ `UI_ROUND_MAX_QUADS` |
| **T5** | **the anti-regression that names the bug** | for the four crossing shapes — `(5,5,2)` dot, `(30,15,5)` pill, `(22,20,4)` swatch, `(10,10,4)` knob — assert the emitted **area ≥ 0.90 × (w·h − 4r²(1−π/4))**, i.e. within 10 % of a true rounded rect. The *old* three-rect decomposition fails this by construction (it is short by `4r²·π/4`): T5 is red before the fix and green after |
| **T6** | W2.2 monotone silhouette | per-band `inset_k` is strictly decreasing in `k` (the staircase never widens going inward) and `inset_0 < R` |
| **T7** | W2.2 degenerate | `w≤0`/`h≤0` → 0 quads; `r<0.5` → exactly 1 quad equal to the input rect; `2r == w == h` (a circle) → no body quad, `2N` quads, area within 10 % of `πr²` |
| **T8** | W2.2 pixel-grid | for integral `r` the band boundaries are integral (`s == 1.0`), so no sub-pixel seam |
| **T9** | outline | `ui_round_outline_quads` quads are non-overlapping, lie inside the outer shape and outside the inner shape, and `area(outline) + area(fill of the inner shape) == area(fill of the outer shape)` to 1 % |
| **T10** | outline bound | count ≤ `UI_OUTLINE_MAX_QUADS`; `t ≥ min(w,h)/2` degrades to the solid fill |
| **T11** | W1.3 radius ladder | `seg_r()` (exposed from `uigeom.h` as `ui_seg_radius(h)` so it is testable): h=30→7.8, h=26→6.8, h=25→6.5, h=14→3.6, h=10→3.0; each within ±1 px of the measured art radius in §0.1 |

Register the suite in the phase's green list: **7 app host suites** after this phase
(celiolink 1259, netlink 66, diag 369, control 6897, trace_replay 58, tilt 1694, **uigeom N**),
plus `bash tools/emutest/tests/run_host_tests.sh`.

### W2.7 Acceptance (W2)

1. T1–T11 green; **T5 demonstrated red on the pre-fix decomposition** (keep the old function in the
   test file as `legacy_quads()` so the regression is proven, not asserted).
2. **Capture proof:** `zooms/z-hud-dot.png` re-shot — the mark is a disc, not a plus.
3. **Capture proof:** `zooms/z-chiprow.png` re-shot — 8 pills with rounded ends, no protruding nubs,
   no label overflowing onto a nub.
4. **Capture proof:** `zooms/z-swatches.png` re-shot — 5 rounded squares, no black corner bites.
5. **Capture proof:** pause AUDIO volume rows — the bar has rounded ends.
6. **Capture proof, alpha:** in-game bottom screen with touch = Gamepad, the `touch.c` header scrim
   — uniform darkness, **no darker corner patches** (the non-overlap invariant, visually).

---

## W3 — the dependent symptoms: which are RC2, which are not

| symptom (report) | verdict | evidence |
|---|---|---|
| **W3.1** pause chip row renders as crosses (D6a) | **RC2, confirmed.** `main.c:2343` `ui_fill(x,129,pw,15,bg,5)`; with pw≈22 the centre column is 12×15 and the nubs 5×5 — the exact cross in `z-chiprow.png`. The report's "each piece carrying its own border" is a perceptual reading of the AA edge of three same-coloured quads; there is no border in this code path. No separate bug. | `z-chiprow.png`, `main.c:2343` |
| **W3.2** in-game identity dot is a plus (D6b) | **RC2, confirmed.** `ui_dot` = `ui_fill(x,y,5,5,col,2)`; centre 1×5 + two 2×1 nubs = a plus, exactly as captured. | `z-hud-dot.png`, `ui.c:98-100` |
| **W3.3** gamepad colour swatches are crosses with corner blocks (D6c) | **RC2, confirmed.** `main.c:4125/4339` `ui_fill(sx,y+3,22,h−6,pc,4)` with `PK_SWATCH h=26` → 22×20 r4 → 4×4 corner bites. The "opaque black corner blocks" are the *plate background* showing through the bites, not a sprite. The white selection ring is a separate, correct `ui_border`. | `z-swatches.png`, `main.c:4121-4127` |
| **W3.4** "feature pills" | **RC2 for the geometry** — same call, same shape. | as W3.1 |
| **W3.5** …but the pills' **style** is a separate 1:1 gap | **NOT RC2.** The design draws these as **outlined** pills — `pill-3d` = blue 1 px outline + blue text, `pill-dof`/`pill-green`/`pill-dim` likewise, transparent interior (measured in §0.1; visible in `screenshots/pause-tab-2-display.png`, top screen). The code draws a **solid** `g_ui.acc` fill with `g_ui.ink` text. Fixing RC2 gives correctly-rounded *solid* pills — better, but still not the reference. | `screenshots/pause-tab-2-display.png`, `pill-*.png` alpha profiles |
| **W3.6** `ui_panel` corner leak | **watch item, not yet a defect.** `ui_panel` = rounded `ui_fill` inside a **square** `ui_border`. Today the square-bite fill hides inside the square frame; a *rounded* fill may reveal 4 background pixels at the frame's inside corners. Re-capture the picker slot cards (`rompicker.c:134`, r=4) and the presence card (`main.c:1642`, r=6) and look. If it shows: route `ui_panel` to `ui_border_round` with the same `r`. | to be captured |

**W3.5 implementation (recommended, but severable — it is a style change, not a defect fix):**

```c
// main.c:2343 — outlined pill, matching pill-3d/pill-dof/pill-dim
ui_border_round(x, 129.0f, pw[i], 15.0f, on ? C : g_ui.dim, 1.0f, 5.0f);
assets_text_c(buf, FNT_JBM_MED, labs[i], x + pw[i]/2.0f, 132.0f, 8.0f, on ? C : g_ui.dim);
```

with the per-pill role colour `C` used for **both** outline and text (that is what the art does).
Keep the `bg[]` array only if the user prefers the current filled look — **ask before landing**
(Open Question OQ2). Whatever is chosen, W3.1's geometry fix applies either way.

**W3 acceptance:** the three re-shot zooms in W2.7 plus, if W3.5 lands, a side-by-side of the pause
top screen against `screenshots/pause-tab-2-display.png` at 1:1.

---

## W4 — contrast and theme correctness

Two distinct defects with two distinct roots. Do not merge them.

### W4.1 The principle (this is the rule the check enforces)

> **An element's ink token must come from the same theme as the surface it is drawn on.**
>
> - procedural surface (`ui_fill`, `ui_border_round`) → the **active** theme, `g_ui.*`
> - baked-art surface (a plate, a `fill-*-r8` 9-slice, a widget sprite) → the **art** theme, `g_art.*`
> - constant scrim (`THEME_HUD_BAR`, `THEME_MENU_DIM`) or raw game pixels → the theme-invariant
>   `THEME_ON_DARK` / `THEME_ON_DARK_DIM` (already in `theme.h:95-96`)

D10/D12 are exactly a violation of line 2; D11 is a violation of line 3 *plus* a font-path bug.

### W4.2 D11 — the "3D" badge. Two causes, both must be fixed.

**Cause A — the role pair is not used.** `main.c:3854`:
`ui_chip(txtBuf, "3D", rx - cw, 0.5f, THEME_GAME_B)` passes **one** colour to
`ui_chip_w`, which uses it for the border *and* the text. The handoff's fixed role pair is
`#a9d4ff` **on** `#3E86D6` (`CLAUDE.md` "Hard rules"; `theme.h:68` already defines
`THEME_3D_TEXT` — and `grep` shows **it is never used**). So the constant exists and the design
says what to do with it; the call site simply does not.

**Cause B — the glyphs are drawn at 0.32 scale with the *system* font.** `ui_chip_w` calls
`ui_text(..., 0.32f, col)`, i.e. `C2D_TextParse` + a 0.32× downscale. At that scale a stroke covers
~⅓ of a device pixel, so the rasteriser blends the ink toward the background. Measured on
`rec-tabs/top_00040.png`: the brightest "3D" pixels are (26,40,65)…(51,83,130) against a bar at
(6,5,10) → **1.4 : 1 to 2.8 : 1**. Nominally `#3E86D6` on black is 5.6 : 1 — *a naive colour check
passes while the screen is unreadable.* The FOCUS chip survives the same treatment only because
gold `#F5D042` starts 3× brighter. This is the learn skill's invariant 4 ("draw near scale 1.0")
and is also the real content of D19.

**Fix (both causes):**

```c
// ui.h / ui.c — a chip whose frame and ink are separate roles.
float ui_chip_2w(C2D_TextBuf buf, const char* s, float x, float y, float w, u32 frame, u32 ink);
float ui_chip_2 (C2D_TextBuf buf, const char* s, float x, float y,          u32 frame, u32 ink);
// ui_chip(…, col) becomes ui_chip_2(…, col, col) — every existing caller keeps working.
```

- `ui_chip_w`/`ui_chip_2w` draw the frame with **`ui_border_round(..., t=1.0f, r=3.0f)`** (art
  `chip-focus` r=3, §0.1) instead of the square `ui_border`.
- Draw the label with the **baked** font at its native size — `assets_text(buf, FNT_JBM_MED, s,
  x+6, y+2, 8.0f, ink)` — not `ui_text(..., 0.32f, ...)`. `FNT_JBM_MED` is baked at 7 pt ≈ 9 px
  native, so the draw scale is ≈0.9 and coverage is near 1.0. Width measurement moves to
  `assets_text_w(buf, FNT_JBM_MED, s, 8.0f) + 12.0f` so the chip still auto-sizes.
  *(`ui.c` does not currently include `assets.h`; add the include, or move `ui_chip*` into
  `assets.c` next to the other baked-font helpers. Either is fine — record the choice.)*
- Route the badge: `ui_chip_2(txtBuf, "3D", rx - cw, 0.5f, THEME_GAME_B, THEME_3D_TEXT)`.
- **The same two fixes apply to every HUD chip** — `●FOCUS`, `⚡LINK`, `TILT*`, `CO-OP`
  (`main.c:3834, 3875, 3889, 4021, 4022`). They keep their single accent colour (correct per the
  design) but gain the baked font and the rounded frame. That also closes D19's HUD half.

### W4.3 D10 / D12 — Daylight (and OLED / Duo / Retro / Custom) go dark-on-dark

**Root, measured:** `tools/build_assets.sh` bakes **one** theme (`THEME="${1:-indigo}"`) into
`data/`, while every label reads the **active** theme. In the Daylight capture the button body is
indigo `fill-secondary-r8` `#33265A` and the label is Daylight `g_ui.text` `#241C33` — contrast
**≈ 1.2 : 1**. It is not a palette mismatch, it is two different themes on one pixel.

Two fixes. **Ship both**; they are complementary, not alternatives.

**W4.3.a — the readability guarantee (small, must ship).**
Teach the code which theme the art was baked from, and take ink from *that* theme wherever the
surface is baked art.

1. `tools/build_assets.sh` writes the theme it built into the generated header:
   ```sh
   case "$THEME" in indigo) TID=0;; oled) TID=1;; daylight) TID=2;; duo) TID=3;;
                    retro) TID=4;;  custom) TID=5;; *) TID=0;; esac
   echo "#define ASSET_THEME_ID $TID" >> "$GEN"
   ```
2. `theme.c` exposes the derived palette (a *derivation*, not a hardcode — it follows whatever the
   build produced):
   ```c
   Theme g_art;                         // the palette the embedded art was baked from
   void theme_init_art(int artThemeId); // called once from assets_init(): g_art = presets[id]
                                        // (ASSET_THEME_ID == THEME_CUSTOM -> the pack's own
                                        //  custom seed 205/168/14, which is what the art shows)
   ```
3. Every label drawn on a baked surface switches token family: `assets_button`'s `col` arguments
   at `rompicker.c:173-175, 282-283, 287-288`, `main.c:4107-4118, 4336-4337`,
   `wireless.c:233-235, 250, 257-258`; and the plate-relative body text in the pause tabs and the
   picker. Mechanically: `g_ui.text → g_art.text`, `g_ui.dim → g_art.dim`, `g_ui.ink → g_art.ink`.
   **`g_ui.acc` stays `g_ui.acc`** — the accent is drawn procedurally (focus rings, the segmented
   pill after W1) so it correctly follows the active theme, and it is the one visible sign that a
   theme was chosen while only one art pack ships.
4. `run_settings`'s theme list marks the un-baked entries, e.g. `Daylight  (art: Indigo)`, so the
   UI is honest about what the user is getting. Cheap, and it stops the next bug report.

**W4.3.b — the real fix: build all six packs, load one (recommended, sized below).**
All six theme packs already exist in the handoff (§0: 24 plates + 42 widgets each). Compressed
`.t3x` is **176 KB plates + 168 KB widgets = 344 KB per theme**, so six packs add **≈1.7 MB** to a
2.4 MB `data/` — trivial for a `.cia`.

The constraint is **RAM, not disk**: `assets_init` loads *every* sheet up front, and a 400×240
RGBA8 plate becomes a 512×256 POT texture ≈ 512 KB, so one pack is already ≈13 MB of linear RAM.
Preloading six would be ≈78 MB — **not viable** beside two GBA cores. Therefore:

- `build_assets.sh` loops all six themes, emitting per-theme symbol stems
  (`pl_<theme>_<name>` / `wg_<theme>_<name>`) and six X-macro lists in `assets_gen.h`.
- `assets.c` gains `assets_load_theme(int id)` / `assets_unload_theme(void)` and loads **only the
  active pack**; steady-state RAM is unchanged.
- **Init order:** `main.c` currently calls `assets_init()` at 4414 and `settings_load()` (which
  calls `theme_apply`) at 4440. **Hoist the `settings_load` block above `assets_init()`** — it only
  reads a file into `g_prefs` + locals and does no GPU work — so the first pack loaded is the right
  one and the splash is already themed.
- Theme changes in `run_settings` call `assets_unload_theme(); assets_load_theme(new)` **between
  frames on the main/render thread** (never inside `C3D_FrameBegin/End`, never from a worker —
  CLAUDE.md rule 2). Budget ≈24+42 `.t3x` decompressions; measure it and, if it is visible, show a
  one-frame "applying theme…" and re-enter the settings loop.
- `THEME_CUSTOM` has no correct pack by definition (the art's teal seed cannot follow user hues).
  Load the `custom` pack and let W4.3.a's `g_art` be the pack's seed palette — which is exactly why
  W4.3.a must ship even after W4.3.b.

**Sequencing:** W4.3.a is a small, safe, testable change that makes every theme *readable*.
W4.3.b is the change that makes them *coherent*. If the phase runs short, ship (a) and file (b).

### W4.4 The check that catches this class for all six themes — `test/host/test_theme.c`

A pure-C host suite over an explicit **token inventory**: every (ink token, surface token, text size
class) triple the app actually draws. Two ≤3-line enablers:

- `theme.h`: `#ifndef THEME_HOST_SHIM` around `#include <citro2d.h>`.
- `test/host/ctr_shim.h`: `typedef uint32_t u32;` + an inline `C2D_Color32`.
  Compile: `clang -std=c11 -Wall -Wextra -O2 -I source -DTHEME_HOST_SHIM -include test/host/ctr_shim.h test/host/test_theme.c source/theme.c -lm -o /tmp/tth && /tmp/tth`

**The contrast model** (this is the part that matters — a naive WCAG check *passes* the unreadable
3D badge, §W4.2):

```
L(c)            = WCAG relative luminance of an sRGB triple
alpha(px)       = glyph coverage estimate: 1.0 for px >= 12, else 0.35 + 0.65*(px-6)/6, floored 0.35
                  (calibrated against the measured 3D badge: 0.32-scale system font -> ~0.35)
eff(fg, bg, px) = L( lerp(bg, fg, alpha(px)) )
contrast        = (max(eff, L(bg)) + 0.05) / (min(eff, L(bg)) + 0.05)
```

| TEST | Assertion |
|---|---|
| **TH1** | For every theme in `{indigo, oled, daylight, duo, retro}` **and** custom sampled at `baseHue ∈ {0,30,…,330} × contrast ∈ {6,14,24}`: every inventory pair has `contrast ≥ 4.5` for `px < 13`, `≥ 3.0` otherwise. |
| **TH2** | The **fixed role colours** are unchanged by any theme: `THEME_GAME_A`, `THEME_GAME_B`, `THEME_3D_TEXT`, `THEME_QUIT`, `THEME_QUIT_TEXT` and the five `PAD_COLOR_*` are compile-time constants and appear in the inventory as such. |
| **TH3** | The 3D badge pair specifically: `THEME_3D_TEXT` on `THEME_HUD_BAR`-over-black at 8 px passes; `THEME_GAME_B` on the same at 0.32-scale coverage **fails** — the red-before-green regression that names D11. |
| **TH4** | Ink-on-baked-art pairs are evaluated against **`g_art`**, not `g_ui` (W4.1). Before W4.3.a this test is **red for daylight/oled/duo/retro/custom** on the `assets_button` rows — that redness *is* D10. |
| **TH5** | `THEME_ON_DARK` / `THEME_ON_DARK_DIM` on `THEME_HUD_BAR` and on `THEME_MENU_DIM` pass in all themes (they are theme-invariant by construction — this guards a future "helpful" refactor). |
| **TH6** | `theme_make_custom` produces a `Theme` whose every field is opaque (alpha 0xFF) and whose `ink`-on-`acc` pair passes, for the whole hue×contrast grid. |

**Visual counterpart (the proof, not the gate).** Add a harness recipe
`docs/phase17-uifix/recipes/theme-sweep.md`: for `theme ∈ 0..5`, `setbin.py theme=<n>` → boot →
`m1_tabtour` → capture pause SESSION + DISPLAY + LINK, the picker, and an in-game HUD frame →
`run sheet` into one captioned contact sheet. **The model reads that sheet** and pastes what it saw
into the BUILDLOG. Six themes × 5 screens = 30 frames; that is the "all six themes work" evidence.

### W4.5 Acceptance (W4)

1. TH1–TH6 green, with TH3 and TH4 demonstrated **red before** the fix.
2. **Capture proof:** in-game top screen — the "3D" badge is light blue on a blue-framed rounded
   chip, legible at 1×; compare to `screenshots/04-in-game-dual.png`.
3. **Capture proof:** the Daylight LINK tab re-shot at the same crop as §0 — "Save state" /
   "Load state" / "Load .sav" legible.
4. **Capture proof:** the 6-theme contact sheet, read and described in the BUILDLOG.

---

## W5 — slice order, build, and the proof loop

| slice | content | proves |
|---|---|---|
| **S1** | `source/uigeom.{c,h}` + `test/host/test_uigeom.c` (T1–T11). No renderer change yet. | RC2's geometry, on the PC, T5 red→green |
| **S2** | `ui.c` → `ui_fill` via `uigeom`; add `ui_border_round`. Build, capture: HUD dot, chip row, swatches, volume bar, touch scrim. | W2.7 items 2–6 |
| **S3** | `assets_seg` procedural (W1.3); delete `draw_hslice`; splash → `assets_button` (W1.5) + the depth grep (W1.5.1). Capture: pause AUDIO/DISPLAY/TOUCH-EDGES, picker pill, splash. | W1.6 |
| **S4** | `ui_chip_2*` + baked-font chip labels + the `THEME_3D_TEXT` routing (W4.2); W3.5 if approved; W3.6 check. | W4.5 item 2, W3 |
| **S5** | W4.3.a (`ASSET_THEME_ID` / `g_art`) + `test/host/test_theme.c`. Capture: the 6-theme sheet. | W4.5 items 1, 3, 4 |
| **S6** *(stretch)* | W4.3.b (six packs, lazy load, init-order hoist). | theme coherence |

**Build:** `export DEVKITPRO=/opt/devkitpro DEVKITARM=$DEVKITPRO/devkitARM && make -j8`.
New `.c` files need no Makefile edit (it auto-globs `source/`). If `data/` is regenerated, run
`tools/build_assets.sh` first.

**Harness hygiene (every slice, no exceptions):** one Azahar at a time; `azctl stop` always;
`gdbio resume` after `azctl boot --gdb` or nothing moves; never write to `sdmc/dual-gba/`.

**Suites that must be green at the end of every slice:**
celiolink 1259 · netlink 66 · diag 369 · control 6897 · trace_replay 58 · tilt 1694 ·
**uigeom (new)** · **theme (new)** · `bash tools/emutest/tests/run_host_tests.sh`.

**BUILDLOG:** append to `docs/phase17-uifix/BUILDLOG.md` after every slice — the command run, the
capture path, and **a sentence describing what the PNG actually showed**. "It should render
correctly now" is not an entry.

---

## Open Questions

**OQ1 — `seg-active`'s 1 px inner highlight.** Going procedural drops the sprite's faint inner ring
(0.6 % of the pill's pixels, invisible at 1×). Confirm that is acceptable, or accept a
`ui_round_outline` in a lightened accent as a follow-up. *My recommendation: drop it.*

**OQ2 — filled vs outlined feature pills (W3.5).** The design draws the pause top-screen pills as
outlines with coloured text; the app draws solid accent fills with ink text. RC2 fixes the geometry
either way. Do we go 1:1 (outline) or keep the current filled look? This is a taste call on a screen
the user sees constantly. *My recommendation: go 1:1 — the report's "'Touch Off' and 'Co-op'
overflow" symptom also disappears, since an outline has no nub to overflow onto.*

**OQ3 — W4.3.b scope.** Six art packs + lazy loading is ≈1.7 MB of ELF, a new load/unload path in
`assets.c`, an init-order change in `main()`, and a visible stall on theme switch. It is the only
thing that makes Daylight actually *look* like Daylight. In this phase, or filed as phase 18?
*My recommendation: ship W4.3.a now (readability is a defect), file W4.3.b — it deserves its own
capture pass across 30 frames.*

**OQ4 — delete or fix the dead widgets?** `ui_toggle`, `ui_segmented`, `ui_cart` have **no call
sites** (the toggles on screen are the art sprites). Deleting removes 40 lines and three ways to
reintroduce RC2; keeping them means they must also be corrected and tested. *My recommendation:
delete, and note it in the BUILDLOG so the API loss is deliberate.*

**OQ5 — `ui_chip*`'s home.** Switching chip labels to the baked font makes `ui.c` depend on
`assets.h`. Move `ui_chip*` into `assets.c` (where the font helpers live) or add the include?
*My recommendation: add the include — moving them churns five call sites for no benefit.*

**OQ6 — the coverage constant in the contrast model.** `alpha(px)` is calibrated from a single
measurement (the 3D badge at 0.32 scale ⇒ ≈0.35). If TH1 turns out to be over- or under-strict on
real captures, re-derive it from a purpose-made frame that draws a known colour at 6/8/10/13/16 px
and measures the realised pixels. Worth doing once; not a blocker.

**OQ7 — out of scope but adjacent, flagged so it is not lost.** D20 (`cart_tint` computed but the
row blits the untinted `cart-tinted` sprite, `rompicker.c:266`) is a widget-layer defect that no
other phase-17 spec owns. Assign it, or add it here as W6.
