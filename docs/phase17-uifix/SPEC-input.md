# SPEC-input — phase 17: touch + button correctness

**Scope:** the user's asks #2 and #3 — *"make sure both button and touch control are working
properly"* and *"the scroll using touch should work"*. Everything below is input: hit-testing,
edge semantics, drag-scroll, d-pad/button reachability, and how each of those is **proved** with
`tools/emutest`.

**Not in scope (owned by SPEC-visual):** how anything is *drawn*. This spec fixes only where a
touch lands and what a button does. Where the two meet — a hit box that must equal the drawn box —
this spec defines the **single shared rect table** and SPEC-visual must draw from it.

**Binding rules inherited:** no edits to emulation / threading / `celiolink.c` / `netlink.c` /
`gbacore` SIO / HD-2D passes. All six themes. Suites stay green and grow.

**Evidence base.** Every geometry number below was measured this session from the shipped art
(`design_handoff_3dgba_ui/assets_3ds/plates/indigo/*.png`, pixel-scanned with PIL) and from
`plates/manifests.json`, and cross-read against the drawing code. Line numbers are as of the
phase-17 starting tree.

---

## 0. The one structural change everything else hangs off

### 0.1 — `source/uihit.{c,h}`: a pure-C hit-test module (CLAUDE.md rule #4)

Today every screen hard-codes its hit rects as magic numbers in an `if` chain, *separately* from
the magic numbers it draws with. That duplication is the direct cause of D2 and of the five
further mismatches this spec documents (§I1.6, §I3.5, §I3.6). The fix is not "retype the numbers
more carefully" — it is to make the drawn rect and the hit rect **the same array**.

- **0.1.1** Create `source/uihit.h` / `source/uihit.c`. Header-free per CLAUDE.md #4: only
  `<stdint.h>`/`<string.h>`. **No `citro2d.h`, no `3ds.h`.** It must dual-compile on the PC.
- **0.1.2** Core types:

  ```c
  typedef struct { int16_t x, y, w, h; } UiRect;      // device px, origin top-left, y-down
  int  uihit_in   (UiRect r, int px, int py);          // 1 iff px,py inside [x,x+w) x [y,y+h)
  int  uihit_seg  (UiRect r, int nseg, int px);        // 0..nseg-1, -1 if outside r
  int  uihit_index(const UiRect* tbl, int n, int px, int py);  // first hit, else -1
  ```
  `uihit_seg` must reproduce `ui_seg_hit()` exactly (`i = (px - x) / (w / n)`, clamped) so the
  drawn segment boundary and the hit boundary cannot drift — with the one change that a point
  **outside** `r` returns -1 instead of being clamped into segment 0. (`ui_seg_hit` is currently
  called only after a rect test, so this is behaviour-preserving.)
- **0.1.3** Every rect table in this spec lives in `uihit.c` as `static const UiRect`, exposed
  through a named accessor (`uihit_pick_rects()`, `uihit_pad_zones()`, …). `rompicker.c`,
  `main.c`, `wireless.c` and `touch.c` **draw from the same accessor** they hit-test with.
  SPEC-visual: this is the contract — no literal rect may appear in a draw call for any control
  named in this document.
- **0.1.4** New host suite `test/host/test_uihit.c`, built the house way:
  `clang -std=c11 -Wall -Wextra -O0 -g -I source test/host/test_uihit.c -o /tmp/tu && /tmp/tu`
  It asserts the numbers in §I1.5, §I1.6, §I2, §I3.5 and §I3.6 as data, so a future coordinate
  edit fails on the PC in a second instead of on hardware in a week.
- **0.1.5** The gesture state machine (§I2.1) also lives here — pure C, no HID types — so
  tap-vs-drag, the threshold, the clamp and the end-of-drag rule are all unit-testable.

### 0.2 — Test seams: two diagnostic globals

The picker's entire state (`sel`, `topRow`, `idxA`, `idxB`, `dragging`) is **stack-local**, so the
harness cannot read it over GDB and cannot prove a tap landed. `nm 3DGBA.elf` resolves file-scope
statics (verified: `s_isN3DS`, `s_ctlOn`, `g_renderSeq` all appear), so a small mirror struct is
enough.

- **0.2.1** `rompicker.c`: file-scope `PickDiag g_pickDiag;` — all fields `int32_t`, written once
  per frame at the end of the input block. **Logging only**: nothing may read it back, and no
  branch may depend on it.

  | off | field | meaning |
  |----|--------|---------|
  | 0x00 | `magic` | `0x50494B31` ('PIK1') — proves the symbol/offsets are the ones you think |
  | 0x04 | `frame` | picker frame counter (liveness) |
  | 0x08 | `sel` | highlighted ROM row |
  | 0x0c | `topRow` | first visible row = **the scroll offset** |
  | 0x10 | `nRoms` | rows scanned |
  | 0x14 | `idxA` | slot A row, -1 = empty |
  | 0x18 | `idxB` | slot B row, -1 = empty |
  | 0x1c | `mode` | mirror of `g_prefs.gameMode` |
  | 0x20 | `lastHit` | `PickTarget` enum of the last resolved tap (§I1.5), -1 = none |
  | 0x24 | `lastTap` | packed `(x << 16) \| y` of the last **latched** touch point |
  | 0x28 | `tapN` | taps resolved (drag-rejected taps not counted) |
  | 0x2c | `dragN` | drags that crossed the threshold |
  | 0x30 | `dragRows` | rows moved by the current/last drag (signed) |
  | 0x34 | `startN` | START / START—LINKED activations |

- **0.2.2** `main.c`: file-scope `MenuDiag g_menuDiag;` with `magic 0x4D4E5531` ('MNU1'),
  `frame`, `tab`, `row`, `scroll`, `lastHit`, `lastTapX/Y`, `tapN`, `dragN`. Written in both the
  pause-menu and `run_settings` input blocks (one struct, a `screen` field distinguishes them).
- **0.2.3** `touch.c`: file-scope `PadDiag g_padDiag;` with `magic 0x50414431` ('PAD1'),
  `lastKeys` (the GBA mask `pad_keys` returned), `lastSx/lastSy`, `zoneN[10]` press counters per
  zone, `menuChipN`. Needed because the in-game pad has **no** other observable (the `s_tLog`
  touch log only records SMART mode).
- **0.2.4** Cost: three stores per frame on a menu path. Nothing on the emulation path. Guarded by
  nothing — these must be present in the shipping build, because the harness *is* the regression
  gate and a debug-only symbol would be untestable in the artifact the user installs.

---

## I1 — RC3: the ROM picker's touch is dead

### I1.1 — Diagnosis, confirmed

`rompicker.c:220-236`:

```c
touchPosition tp; hidTouchRead(&tp);                       // 220  read EVERY frame
if (k & KEY_TOUCH) { dragY0 = tp.py; dragTop0 = topRow; dragging = false; }
if (kHeld & KEY_TOUCH) { ...drag... }
if ((kUp & KEY_TOUCH) && !dragging) {                      // 228  act on RELEASE
    if (tp.py >= 214 && tp.px < 96) { ...settings... }
    else if (tp.py < 42) { g_prefs.gameMode = (tp.px < 160) ? 1 : 0; ... }
    else if (tp.py >= 174) { ...START / LINKED... }
}
```

On the frame `KEY_TOUCH` goes **up**, the HID touch entry is no longer valid and libctru reports
`(0,0)`. The handler nevertheless evaluates its whole `if` chain against that `(0,0)`:

- `tp.py >= 214` → false (0 is not ≥ 214) → the settings chip never fires;
- `tp.py < 42` → **true** → `gameMode = (0 < 160) ? 1 : 0` → **always 1**;
- the `>= 174` START branch is unreachable because the `< 42` branch already claimed the event.

That is exactly what the sweep photographed (REPORT D2): the first tap at (240,20) — the *right*
half of the mode pill, i.e. "2 Games", the mode already active — flipped the app to **1 Game**,
and the following four taps at (80,20), (240,20), (40,225) and (160,100) produced **zero** pixel
change. Only a `px == 0` read can turn a tap at x=240 into "1 Game". Diagnosis confirmed from the
pixels, not inferred.

`recent_prompt` (`rompicker.c:154`) reads on `k & KEY_TOUCH` — the **press** edge, when the entry
is still valid — which is why the resume prompt's touch works and isolated the fault to the picker.
`main.c:3193` (pause menu) and `main.c:4257` (`run_settings`) use the same correct press-edge
idiom. `wireless.c:63` likewise. **The picker is the only release-edge reader in the app.**

### I1.2 — The corrected edge idiom

Do **not** simply move the picker to press-edge: that loses tap-vs-drag discrimination, which the
user explicitly asked for ("the scroll using touch should work" implies a drag must not activate
whatever is under the finger). The correct 3DS idiom, and the one this spec mandates everywhere:

- **I1.2.1** Read the position **while the touch is valid** — i.e. on the press edge and on every
  held frame — and **latch** it. Never read on the release frame.
- **I1.2.2** Concretely, once per frame, before any handler:

  ```c
  touchPosition raw; hidTouchRead(&raw);
  bool down   = (kDown & KEY_TOUCH) != 0;
  bool held   = (kHeld & KEY_TOUCH) != 0;
  bool up     = (kUp   & KEY_TOUCH) != 0;
  if (held) { lastX = raw.px; lastY = raw.py; }     // the ONLY place raw is consumed
  ```
  `lastX/lastY` on the release frame therefore hold the **last valid** sample, which is what a
  release-edge handler must act on.
- **I1.2.3** Feed `down/held/up` + `lastX/lastY` into `uihit_gesture_step()` (§I2.1), which returns
  one of `GEST_NONE / GEST_DRAG / GEST_TAP(x,y)`. Handlers only ever see `GEST_TAP` with a valid
  coordinate. This makes the release-edge bug structurally unrepresentable.
- **I1.2.4** `g_pickDiag.lastTapX/Y` records the latched point, so a harness run can prove the app
  saw the coordinate the movie sent — the single most useful diagnostic when a tap "does nothing".
- **I1.2.5** Apply the same latch to `recent_prompt`, `wireless.c`, the pause menu and
  `run_settings`. They are correct today only because they act on press; once a drag exists on any
  of them (§I2.4) they need the latch too. One idiom, five call sites, zero exceptions.

### I1.3 — What the picker's touch surface actually is

A 3DS **cannot touch the top screen.** The task brief lists "the ROM list rows" as a tap target;
that is physically impossible — the manifest puts the list on `select-*-top`
(`x16 y37 w369 h197`). So:

- **I1.3.1** The **bottom** screen is the picker's whole input surface: the mode pill, the slot
  card(s), START, START—LINKED and the settings affordance.
- **I1.3.2** Row *selection* is therefore d-pad-only today, which makes the app impossible to
  drive by touch alone. The closure is: **a drag on the bottom screen scrolls the top list, and
  the highlight follows the scroll** (§I2.2.6), then a tap on a slot card commits the highlighted
  row (§I1.5, `PICK_SLOT_A/B`). That is the complete touch-only path: *drag until you see it → tap
  the card → tap START.*

### I1.4 — Measured plate geometry (the source of truth for the rects)

Pixel-scanned from `plates/indigo/select-dual-bot.png` and `select-single-bot.png`
(320×240, plate background `#201830`):

| element | dual mode | single mode | how measured |
|---|---|---|---|
| slot A card | rows **51–102**, cols **12–307** | rows **51–109**, cols 12–307 | first/last non-bg pixel per row; card border is the A-green `#654E36`→`#446C36` ramp |
| slot B card | rows **111–162**, cols 12–307 | — | B-blue `#2F4F83` border |
| info panel (non-interactive) | — | rows **118–163** | flat panel, no border colour |
| baked footer hint | rows **219–229**, cols **19–300** | rows **220–229**, cols 25–294 | the text D7 says the settings chip chops |
| everything else | transparent to the plate bg | | |

Manifest `dynamic` rects (what the code draws into, and what SPEC-visual must keep):

| control | dual | single |
|---|---|---|
| mode seg | `12,12,296,30` | `12,12,296,30` |
| slot A name text | `47,77,248,17` | `49,80,245,18` |
| slot B name text | `47,138,248,17` | — |
| START | `12,174,124,37` | `12,175,130,37` |
| START—LINKED / LINK A FRIEND | `144,174,164,37` | `150,175,158,37` |

The code at `rompicker.c:278-288` already draws exactly these. **Good news: the art and the draw
calls agree. Only the hit test disagrees with both.**

### I1.5 — The corrected picker hit table

`uihit.c`, one table per mode, in this order (first match wins):

```c
typedef enum { PICK_NONE=-1, PICK_MODE=0, PICK_SLOT_A, PICK_SLOT_B,
               PICK_START, PICK_LINKED, PICK_SETTINGS } PickTarget;
```

| # | target | dual rect | single rect | rule |
|---|--------|-----------|-------------|------|
| **I1.5.1** | `PICK_MODE` | `12,12,296,30` | `12,12,296,30` | segment = `uihit_seg(r,2,px)` → boundary at **x=160**; seg 0 = "1 Game" (`gameMode=1`), seg 1 = "2 Games" (`gameMode=0`). Setting mode 1 clears `idxB`. |
| **I1.5.2** | `PICK_SLOT_A` | `12,51,296,52` | `12,51,296,59` | commits `idxA = sel`; if `idxA == sel` already, clears it (`idxA = -1`) so a card is its own undo. |
| **I1.5.3** | `PICK_SLOT_B` | `12,111,296,52` | *absent* | same, on `idxB`. Never present in single mode — the table for single mode has 5 entries, not 6. |
| **I1.5.4** | `PICK_START` | `12,174,124,37` | `12,175,130,37` | starts iff `ready`; otherwise sets the status hint "pick a game first" and does nothing. |
| **I1.5.5** | `PICK_LINKED` | `144,174,164,37` | `150,175,158,37` | as above, with `*startLinked = true`. |
| **I1.5.6** | `PICK_SETTINGS` | see §I1.7 | see §I1.7 | returns `"__SETTINGS__"`. |

- **I1.5.7** The **gap between START and START—LINKED is dead** (dual x136–143, single x142–149).
  Today `tp.px < 136` sends the whole gap to LINKED. A dead gutter that matches the art is correct;
  a tap that misses both buttons must do nothing, not silently pick the scarier one.
- **I1.5.8** All other bottom-screen pixels are dead. In particular the baked footer band and the
  single-mode info panel must not be tappable.
- **I1.5.9** Every rect above is `y ∈ [y, y+h)`, `x ∈ [x, x+w)` — half-open, matching
  `uihit_in`, matching how the art is rasterised. No `<=`.

### I1.6 — The settings affordance: rect, and why it has to move

`rompicker.c:290-291` draws `ui_fill(6, 216, 84, 16)` + the label — i.e. **x6–89, y216–231** — and
hit-tests `tp.py >= 214 && tp.px < 96` — i.e. **x0–95, y214–239**. Two different rects, neither
matching the other, and the drawn one sits **on top of the plate's baked footer hint** (measured
rows 219–229, cols 19–300) which is D7.

- **I1.6.1** Input requirement: the settings hit rect **must equal** whatever SPEC-visual decides
  to draw, and must not overlap any other target. Nothing else about the placement is an input
  decision.
- **I1.6.2** Input constraint SPEC-visual must satisfy: a touch target on this hardware needs
  ≥ 20 px in the short axis to be reliably hit with a thumb. The plate's genuinely empty bands are
  dual `y103–110` (8 px), `y163–173` (11 px), `y212–218` (7 px) — **none of them is big enough.**
  So the chip cannot simply be nudged; the footer band has to be re-composited (overpaint
  `0,214,320,26` with the plate bg and redraw hint + chip together as one dynamic row) or the plate
  regenerated. Recorded here as the reason the D7 fix is not a 2-pixel move. Open question Q1.
- **I1.6.3** Until that lands, the interim rect is `6,214,88,22` for **both** draw and hit — the
  drawn box grown to a thumb-sized 22 px and the hit box shrunk to match it exactly.

### I1.7 — Button/d-pad bindings on the picker (corrections)

Current bindings (`rompicker.c:207-216`): `START`→`return false`, `ZR`→settings, `Y`→toggle mode,
`d-pad`→`sel`, `A`→assign slot, `X`→start, `SELECT`→start linked, `B`→unassign / `return false`.

- **I1.7.1 (defect).** `KEY_START` returns false, and `main.c:4448-4452` turns a false return into
  **"start a session on the hard-coded default ROM paths"**. So on a screen whose primary button is
  labelled *START*, pressing the console START button launches two games the user did not pick.
  The sweep saw exactly this ("the console START button does start a session but ignores the picked
  games and the mode"). Fix: `KEY_START` = activate `PICK_START` (identical to `X`). When not
  `ready`, set the status hint and consume the press.
- **I1.7.2 (defect).** `KEY_B` with nothing assigned also `return false` → the same defaults
  session. Fix: `B` unassigns B, then A, then does **nothing** (the picker is the app's root
  screen; there is nowhere to go back to).
- **I1.7.3 (contract).** After I1.7.1/I1.7.2, `rompicker_run()` returns false only for *no ROMs
  found* / *settings round-trip*. The caller must then show an empty state, **not** launch
  `gameA.gba`/`gameB.gba`. That caller-side change is REPORT D13 and belongs to SPEC-visual; this
  spec only fixes the picker's side of the contract and states the dependency.
- **I1.7.4** `X` (start) and `SELECT` (start linked) stay, and `START` joins `X`. The picker draws
  no key hints today, so none of this is discoverable — see I3.2.
- **I1.7.5** The mode pill's keyboard equivalent is `Y`. Keep it, and make the pill's *touch*
  behaviour identical (set, not toggle: tapping the already-active segment is a no-op, unlike `Y`
  which flips). A segmented control that flips when you tap the lit half is the bug D2's evidence
  frame actually shows.

---

## I2 — Touch drag-scroll

### I2.1 — The shared gesture state machine (`uihit.c`)

```c
typedef struct {
    int  active;        // a touch is down
    int  x0, y0;        // press point (latched, always valid)
    int  x,  y;         // latest valid point
    int  base;          // scroll offset at press time
    int  dragged;       // threshold crossed this gesture
} UiGesture;

typedef enum { GEST_NONE=0, GEST_DOWN, GEST_DRAG, GEST_TAP } UiGestEv;

UiGestEv uihit_gesture_step(UiGesture* g, int down, int held, int up, int px, int py);
```

- **I2.1.1 Threshold.** `dragged` latches when `max(|x-x0|, |y-y0|) > UIHIT_DRAG_PX`, with
  **`UIHIT_DRAG_PX = 6`**. Six is what the codebase already uses for the same judgement
  (`touch.c:458`, the smart-touch gesture drag flag) — one number, one meaning, app-wide. Today
  the picker tests `abs(tp.py - dragY0) > 6` on **py only**, so a horizontal swipe across a slot
  card is still classified as a tap; using the max of both axes fixes that.
- **I2.1.2 Latching is one-way.** Once `dragged` is set it stays set for the rest of the gesture,
  even if the finger returns to within 6 px of the origin. Otherwise a finger that drifts out and
  back activates a control on release.
- **I2.1.3 `GEST_TAP` is emitted on the release frame iff `!dragged`**, carrying `(x, y)` = the
  latched last-valid point (§I1.2.2). `GEST_DRAG` is emitted on every held frame after the
  threshold. `GEST_DOWN` is emitted on the press frame and is used only by controls that want
  press-feedback; no control may *act* on it.
- **I2.1.4 The gesture resets completely on release** — `active=0, dragged=0`. The current picker
  never clears `dragging` (it is only reset on the next press), which silently disables the
  d-pad's "keep `sel` visible" follow (`rompicker.c:248`) for the rest of the picker's life after
  the first drag. That is a real, reachable bug: drag once, then the d-pad can move the highlight
  off-screen with no scrolling.

### I2.2 — Picker list drag-scroll

The existing code (`rompicker.c:221-227`) is close but not right:

- **I2.2.1 Absolute, not incremental.** Keep the current shape — `topRow = base + (y0 - y)/PX` —
  recomputed from the press origin every frame. It is drift-free and it is already what the code
  does. Do **not** switch to per-frame deltas.
- **I2.2.2 Pixels per row.** Change `12` → **`UIHIT_PICK_ROW_PX = 24`**. The top list's row pitch
  is `ROWH = 23.5f` (`rompicker.c:198`), so 24 px of finger travel = one row = a 1:1 feel between
  what the finger does and what the list does. The shipped 12 scrolls twice as fast as the art
  implies, which reads as "flicky". (Taste knob; it is a named constant precisely so it can be
  tuned without hunting.)
- **I2.2.3 Clamp.** `maxTop = (n > LIST_ROWS) ? n - LIST_ROWS : 0;` then
  `topRow = clamp(base + delta, 0, maxTop)`. The current three-line expression happens to compute
  the same thing but is unreadable and its `nr > n - LIST_ROWS` test is meaningless when
  `n <= LIST_ROWS`. Replace with the plain clamp; assert both ends in `test_uihit.c`.
- **I2.2.4 Over-scroll does nothing.** No rubber-band, no bounce: at either end the list simply
  stops. (Rubber-band needs per-frame animation state on a screen that has none; out of scope.)
- **I2.2.5 A drag never activates.** Guaranteed by §I2.1.3 — the picker's tap handler only ever
  runs on `GEST_TAP`.
- **I2.2.6 The highlight follows the scroll.** After the scroll offset settles each frame, clamp
  `sel` into `[topRow, topRow + LIST_ROWS - 1]`. This is what makes touch-only operation possible
  (§I1.3.2): what you dragged into view is what a slot-card tap will commit. Conversely, when the
  d-pad moves `sel`, scroll `topRow` to keep `sel` visible — that is the existing line 248 logic,
  which must now run unconditionally (its `if (!dragging)` guard is the I2.1.4 bug).
- **I2.2.7 The whole bottom screen is the scroll surface**, including on top of the slot cards and
  buttons — a drag that begins on START scrolls the list and does not start a game. Excluding the
  cards would create dead bands the user has to learn.
- **I2.2.8 Vertical only.** `|dx|` participates in the tap/drag decision (I2.1.1) but never moves
  the list. There is nothing to scroll horizontally.
- **I2.2.9** Observability: `g_pickDiag.topRow`, `.dragN`, `.dragRows`, `.tapN`.

### I2.3 — Does drag-scroll work today?

The HANDOFF's "drag-scroll shipped in the 0708 build" refers to `rompicker.c:221-227`, which is
present and structurally plausible. **The sweep saw no scrolling because it never dragged** — the
only touch movie, `m_tapprobe.json`, is five `["touch", x, y]` taps and no `touch_hold`. So the
honest status is *unproven*, not *broken*, with four known defects in it: the py-only threshold
(I2.1.1), the never-cleared `dragging` (I2.1.4), the 12 px/row rate (I2.2.2), and the fact that
the highlight does not follow (I2.2.6). **T2 (§I4) is the first test that will ever have exercised
it.**

### I2.4 — Pause-menu tab scrolling

The manifest marks **every** `pause-bot-*` unit `"scroll": true`, and its `_meta` says *"the
content panel (right of the tab rail) scrolls vertically; rects with y+h > 240 sit below the
fold."* The design's own rects prove it:

| tab | control below the fold (manifest) | where the code put it |
|---|---|---|
| DISPLAY | `toggle: swap` `268,234,33,18`; `toggle: frameskip` `268,**263**,33,18` | `PT_DISPLAY` squeezes both to `y=224` |
| TOUCH | swatches `93,195,208,31`; `seg: pad edges` `93,**253**` ; **`btn: See gamepad` `93,294,208,31`** | swatch `y=192`, edges seg `y=224`, and the See-gamepad button **does not exist** |

So the DISPLAY toggles and the TOUCH edges row were dragged ~30–40 px upward to fit a screen the
design never intended them to fit on — which is precisely why they now collide with the status
hint line at y231 and get clipped by the screen edge (REPORT D8, D9), and why the TOUCH tab has a
55 px hole where the explainer belongs (D16).

I also confirmed the scrollbar is **baked into the plate art**, not drawn by code: in
`pause-bot-display.png` columns **312–319** are a near-white strip for all 240 rows, with the
brighter thumb baked at rows **193–239**; `pause-bot-touch.png` has its thumb at rows **171–239**.
Both tabs render at scroll-top while their baked thumb sits at the **bottom** of the track — REPORT
D18's "inverted thumb" is unfixable in code without overpainting that strip.

Requirements:

- **I2.4.1** Introduce a real content scroll. `menuScroll` already exists (`main.c:2534`) and is
  only ever reset to 0 — wire it up. Per-tab content height:
  `CONTENT_H[6] = { 240, 281, 240, 240, 240, 325 }` (max of `y+h` over that tab's controls, taken
  from the manifest, floored at 240). `maxScroll[tab] = CONTENT_H[tab] - VIEW_H` with
  `VIEW_H = 228` (the plate's content viewport above the status-hint line at y231).
- **I2.4.2** Restore the manifest y-coordinates in `PT_DISPLAY` and `PT_TOUCH` (swap `y=234`,
  frameskip `y=263`, swatch `y=195,h=31`, edges seg `y=253`), and add the missing
  `PK_BTN ACT_PREVIEW_PAD`-style "See gamepad" row at `93,294,208,31` **only if** SPEC-visual wants
  it; if not, leave a 6th control absent and set `CONTENT_H[5] = 284`. Either way `PTABN[]` must be
  updated in the same commit — `main.c:2213-2216` documents that forgetting it fails *silently*
  (the row neither draws nor hit-tests).
- **I2.4.3** Hit-testing under scroll: test against `(c->x, c->y - menuScroll, c->w, c->h)`, and
  reject any touch with `py >= VIEW_H` (the status-hint band is not part of the scrollable
  content). One line, and it keeps the hit box glued to the art at every scroll position.
- **I2.4.4** Drag to scroll, same gesture machine, on the **content area only** (`px >= 93`): a
  drag on the tab rail must not scroll content. Pixels-per-unit here is **1:1** (`menuScroll +=
  y0 - y`) — unlike the picker, the finger and the content are on the same screen, so anything
  other than 1:1 feels broken. Clamp to `[0, maxScroll]`.
- **I2.4.5** D-pad completeness: whenever `menuRow` changes, scroll so the focused control's rect
  is fully inside `[menuScroll, menuScroll + VIEW_H)`. Without this, a control below the fold is
  focusable but invisible — worse than unreachable.
- **I2.4.6** Make the bar honest. Input side: expose `menuScroll` / `maxScroll` (and
  `g_menuDiag.scroll`). Visual side (SPEC-visual): overpaint the baked strip `312,0,8,240` and draw
  track + thumb from those numbers —
  `thumbH = max(24, VIEW_H * trackH / CONTENT_H)`,
  `thumbY = trackY + (maxScroll ? menuScroll * (trackH - thumbH) / maxScroll : 0)`.
  On a non-scrolling tab (SESSION/AUDIO/ENHANCE/LINK) the plate bakes no strip at all — verified —
  so nothing is drawn and nothing scrolls.
- **I2.4.7** `run_settings` reuses `PTABS`/`PT_PLATE`, so it inherits the same scroll state and the
  same rules. Its "Done" chip (`244,224,72,14` drawn, `px>=240 && py>=224` hit) is **fixed
  chrome** — it must be excluded from the scroll transform and from `VIEW_H`, and its hit rect must
  be corrected to the drawn `244,224,72,14`.

### I2.5 — Everything else that scrolls

- **I2.5.1** `savpicker_run` (`rompicker.c:317-366`) has a `VIS_ROWS = 13` list and **no touch code
  at all**. It is reachable from LINK → "Load .sav". Add the same gesture + drag-scroll, plus tap
  a row = select (this list *is* on the top screen too, so the same "drag on the bottom, tap a
  bottom-screen confirm" shape applies — or, cheaper, accept d-pad-only and say so on screen).
  Open question Q4.
- **I2.5.2** The wireless scan list (`wireless.c:236-250`) shows `vis = min(nLob, 4)` of up to 8
  scanned lobbies — **lobbies 5–8 are unreachable by any input**. Either clamp the scan to 4 or
  give the list the same drag-scroll. Its rows are on the *bottom* screen, so a tap already works
  (and its hit rects already match its draw — the one place in `wireless.c` that does).

---

## I3 — Button / d-pad navigation audit

Rule applied: **(a)** every touch target must be reachable without the touchscreen, and **(b)** the
thing that will act on `A` must be visibly focused.

| screen | (a) keyboard-complete? | (b) focus visible? | defects |
|---|---|---|---|
| Boot splash | ✅ A/B/START/touch all skip | n/a | — |
| Resume prompt | ✅ A / X·B / START | ❌ **no focus at all** | I3.1 |
| ROM picker — top list | ✅ d-pad + A | ✅ `fill-card-r8` row fill | — |
| ROM picker — bottom controls | ⚠️ via Y / X / SELECT / ZR only | ❌ no focus, no key hints | I3.2, I1.7.1 |
| Settings (`run_settings`) | ✅ L/R, d-pad, A, B | ✅ accent `ui_border` | I3.3, I3.4 |
| Pause menu | ✅ L/R, d-pad, A, B | ✅ accent `ui_border` | I3.3, I3.4, I2.4.5 |
| Wireless lobby | ✅ d-pad + A + B + X | ⚠️ phase 0/2 only | I3.5 |
| `.sav` picker | ✅ d-pad + A + B | ✅ gold row | no touch at all (I2.5.1) |
| In-game, touch = Off | ✅ START+SELECT or tap | n/a | — |
| In-game, touch = Gamepad | ⚠️ menu = combo only | n/a | I3.6, I3.7 |
| In-game, touch = Smart | ⚠️ menu = combo only | n/a | I3.7 |

- **I3.1** `recent_prompt` draws all three buttons with `assets_button(..., focus = 0)` — the
  `focus` parameter exists (`assets.c:191` draws a white ring) and is never used anywhere in the
  app. Three actions on three different physical buttons with nothing on screen naming them.
  Fix: add `sel` (0..2) driven by d-pad up/down, `A` activates the focused one, pass
  `focus = (i == sel)`; keep `X`/`B`/`START` as the existing shortcuts.
- **I3.2** The picker's bottom controls carry no key hints. Minimum fix: draw the binding next to
  each — `Y` on the mode pill, `X` on START, `SELECT` on START—LINKED, `ZR` on settings — in
  `FNT_JBM_MED` at `g_ui.dim`, in the dead margins identified in I1.6.2. (A second focus zone that
  d-pad-DOWN moves into is the richer answer; open question Q2.)
- **I3.3 (defect).** `PK_SWATCH` — the gamepad colour row — has **no keyboard path**.
  `main.c:3211-3242` handles `PK_SEG` with `adj`, `PK_STEP` with `adj`, then a chain of
  `activate &&` cases; `PK_SWATCH` appears in **none** of them, so left/right does nothing and `A`
  falls through to the legacy `menuSel` dispatch, which only knows ids 0–19 while `ACT_PADCOL` is
  107 → a silent no-op. `padColor` is **touch-only**, in both the pause menu and `run_settings`
  (`main.c:4267` is the only writer in each). Fix: handle `PK_SWATCH` with `adj`
  (`padColor = clamp(padColor + adj, 0, 4)`), save, consume `activate`.
- **I3.4 (defect).** `PK_STEP` (volume) tapped in the middle of the bar: the pause menu computes
  `adj = (px < x+24) ? -1 : (px > x+w-24 ? 1 : 0)` → **0**, i.e. a tap on the bar does nothing;
  `run_settings` computes `adj = (px < x+24) ? -1 : 1` → **+1**, i.e. a tap anywhere right of the
  minus button increments. Two behaviours for one widget. Fix both to the pause-menu semantics
  (dead middle) *or* implement drag-to-set on the bar (`vol = 256 * (px - bx) / bw`, `bx = x+28`,
  `bw = w-56` — the same numbers the fill uses at `main.c:4101`), which is what the design's
  bar-with-a-value implies. Recommend drag-to-set + the two stepper ends; open question Q3.
- **I3.5 (defect, new — not in the sweep).** `wireless.c` phase 0/1/3 hit rects are **stale v1
  coordinates** that no longer match the plate-composited buttons. Measured:

  | phase | drawn (code, matches manifest) | hit-tested (code) | consequence |
  |---|---|---|---|
  | 0 | Host `13,33,293,40` | `x 12–307, y 22–55` | only y33–55 of a 40 px button works |
  | 0 | Scan `13,83,293,42` | `y 64–97` | only y83–97 works |
  | 0 | Connect online `13,160,293,42` | `y 126–155` (status text only) | **completely disjoint** |
  | 0 | baked "‹ back to menu" text at rows **217–224** | `y 188–209` | back is dead; tapping the *Online* button's lower half **exits the lobby** |
  | 1/3 | Start linked trade `13,73,293,42` | `x 24–295, y 96–131` | y73–95 dead; y116–131 fires over empty plate |
  | 1/3 | Leave `13,124,293,32` | `y 140–161` | **y124–131 of the Leave button fires START-LINK** |

  Firing "start the linked session" from a tap on "Leave" is the worst input bug in the app after
  D2. Fix: replace all of it with `uihit_index()` over a table built from the same rects the
  `assets_button` calls use. Phase 2 (the scan list) already matches and needs no change.
- **I3.6 (defect).** `touch.c` `pad_keys()` (lines 23-38) and `pad_overlay()` (lines 75-89) encode
  the virtual gamepad **twice**, and they disagree:

  | key | drawn | hit-tested | delta |
  |---|---|---|---|
  | UP | `33,138,44,30` | `px 32–77`, `py 119–163` | hit starts 19 px above the art, dead 164–167 |
  | DOWN | `33,192,44,30` | `py 197–239` | dead 192–196, hit runs to the screen bottom |
  | LEFT | `7,162,32,36` | `px 0–38`, `py 157–203` | hit covers the screen edge the art leaves clear |
  | RIGHT | `71,162,32,36` | `px 72–111`, `py 157–203` | ditto |
  | A | `252,150,60,60` | `px 253–319`, `py 151–239` | **hit is ~2× the drawn button** |
  | B | `198,176,50,44` | `px 199–252`, `py 177–239` | overruns the bottom edge |
  | START | `128,214,64,22` | `px 129–191`, `py 215–239` | |
  | L | `4,4,52,22` | `px 0–55`, `py 0–27` | |
  | R | `264,4,52,22` | `px 265–319`, `py 0–27` | |

  The d-pad's overlapping hit boxes are also why it reads as a lattice rather than four keys. Fix:
  one `static const UiRect PAD_ZONES[9]` in `uihit.c` (the drawn values, which are the manifest's
  `pad-bot` values verbatim), `pad_keys` = `uihit_index(PAD_ZONES, 9, px, py)` → key mask,
  `pad_overlay` draws from the same array. Draw == hit by construction, and D5's corner blocks
  become a pure drawing question.
- **I3.7 (defect).** In Gamepad and Smart modes the pause menu is reachable **only** by the
  START+SELECT combo (`main.c:2703`), yet `pad_overlay` draws a "≡ menu" chip bottom-right
  (`touch.c:86-88`) and the manifest gives both `pad-bot` and `smart-bot` a `btn: menu` at
  `269,216,45,18`. The chip is **not a hit target** — and worse, in PAD mode a tap on it satisfies
  `px > 252 && py > 150` and therefore presses **A** into the game. Fix: add the menu chip to
  `PAD_ZONES` as a non-key zone tested **first**, and have `touch_update` report it so the session
  opens the menu; draw the same chip in SMART and honour it there too.
- **I3.8** `main.c:4260` (`run_settings`) and `main.c:3195` (pause) both map the tab rail with
  `t2 = (py - 8) / 30`. Measured active-tab bands in the plate art: **7–36, 37–67, 68–98, 99–128,
  129–159, 160–190** (pitch 30.67, not 30). The formula mis-assigns at every boundary — y=37 →
  SESSION, y=98 → ENHANCE, y=160 → TOUCH — and any tap at y ≥ 192, on empty rail below the last
  tab, selects TOUCH. Fix: a 7-entry boundary table `{7,37,68,99,129,160,191}` + linear search,
  returning -1 outside. Also `px < 86` vs a rail that measures **x0–81** with content starting at
  x93 — use `px < 88` (the midpoint) and record it as intentional.
- **I3.9** `ACT_PAUSEG` ("pause the other game") is in the v2 `menu_layout` list but in **no**
  `PCtl` table, so it is unreachable by any input. Either give it a row or delete the action.
  (`menu_layout` / `MenuW` / `menu_w_h` / `menu_w_sel` — `main.c:2220-2320` — are the whole dead v2
  layout engine, called from nowhere. Deleting them removes ~100 lines of misleading code.)

---

## I4 — Regression-proof test plan

Two tiers, both required. **The PC tier is where a coordinate regression must fail**; the emulator
tier proves the wiring reached the real app.

### I4.1 — PC tier: `test/host/test_uihit.c`

Runs in ~50 ms, no emulator, no permissions. Build line in the file header per house style
(§0.1.4). Assertions:

- **I4.1.1** *Draw==hit, table-driven.* For every control named in this spec, assert the hit rect
  equals the rect the drawing code uses. Implemented by having both read `uihit_*_rects()` and
  asserting the returned table against a **golden copy of the manifest numbers** literal in the
  test. A future edit to either side fails here.
- **I4.1.2** *The D2 regression.* `pick_hit(DUAL, 240, 20) == PICK_MODE, seg 1`;
  `pick_hit(DUAL, 80, 20) == PICK_MODE, seg 0`; boundary `pick_hit(DUAL,159,20)→seg 0`,
  `pick_hit(DUAL,160,20)→seg 1`. And the anti-test: `pick_hit(DUAL, 0, 0) == PICK_NONE` — the
  coordinate the release-edge bug produced must resolve to **nothing**, so even if the edge
  handling regressed the mode could not silently flip.
- **I4.1.3** *Every target, four corners each.* For each rect: the four inside corners hit, the
  four just-outside points miss. Includes the dead gutter (I1.5.7) and the single-mode absence of
  `PICK_SLOT_B` (I1.5.3).
- **I4.1.4** *Gesture.* Down at (100,100); move to (100,105) → still `GEST_NONE`, release →
  `GEST_TAP(100,105)`. Down at (100,100); move to (100,107) → `GEST_DRAG`; return to (100,100) →
  still `GEST_DRAG` (one-way latch, I2.1.2); release → **no** `GEST_TAP`. Horizontal-only:
  (100,100)→(110,100) → `GEST_DRAG`, no tap (the py-only-threshold regression).
- **I4.1.5** *Scroll clamp.* n=11 ROMs, LIST_ROWS=8 → maxTop=3. Drag +1000 px → `topRow == 3`;
  drag −1000 px → `topRow == 0`; drag exactly 24 px → 1 row; 23 px → 0 rows. n=4 → maxTop=0, any
  drag leaves `topRow == 0`.
- **I4.1.6** *Highlight follow.* After a drag to `topRow=3`, `sel` clamps into `[3,10]`; a d-pad
  move to `sel=0` pulls `topRow` back to 0.
- **I4.1.7** *Pause rail.* `rail_hit(y)` for y ∈ {7,36,37,67,68,98,99,128,129,159,160,190} returns
  {0,0,1,1,2,2,3,3,4,4,5,5}; `rail_hit(6)` and `rail_hit(191)` return -1.
- **I4.1.8** *Pad zones.* All nine zones + the menu chip: no two rects overlap (assert pairwise),
  every drawn pixel of every zone maps back to its own key, and `(285,225)` — the menu chip —
  returns the chip, **not** A (the I3.7 regression).
- **I4.1.9** *Wireless.* Host/Scan/Online/back and Start/Leave/back resolve from their drawn rects;
  `(160,128)` (inside the drawn Leave button) must **not** return "start link" (the I3.5
  regression).
- **I4.1.10** *Scroll transform.* With `menuScroll = 40`, a control at manifest `y=263` is hit at
  `py=223`, and not at `py=263`; `py >= 228` never hits any control (I2.4.3).

### I4.2 — Emulator tier: harness recipes

Standard preamble/postamble for every run (SKILL.md §1); `azctl stop` is not optional.

```bash
T=tools/emutest
$T/run ctm make docs/phase17-uifix/movies/<M>.json /tmp/m.ctm
$T/run azctl boot --gdb --fresh-sd-fixtures --movie /tmp/m.ctm
$T/run see rec --seconds N --fps 4 --screen both --out rec-<M> &   # start BEFORE resume
$T/run gdbio resume
$T/run gdbio read-u32 <symbol>            # the proof
$T/run azctl stop
```

Fixtures: `runs/sweep-verify/mkroms.py` stages **11** dummy `.gba` files, which is exactly what the
scroll tests need (11 > `LIST_ROWS` 8 → `maxTop = 3`). Movie boots are Old-3DS-pinned (SKILL.md
§2) — fine, none of this is model-sensitive.

`g_prefs` field offsets (from `theme.h` `UiPrefs` order, corroborated by SKILL.md's own
`g_prefs+0x1c` = `tiltLevel` example): `theme +0x00`, `customBaseHue +0x04`, `customAccentHue
+0x08`, `customContrast +0x0c`, **`gameMode +0x10`**, **`padColor +0x14`**, **`padEdge +0x18`**,
`tiltLevel +0x1c`.

| # | test | movie ops (after the boot wait) | assertion (gdbio) |
|---|------|--------------------------------|-------------------|
| **T1** | mode pill, both segments | `["touch",240,20]`, wait, `["touch",80,20]`, wait | `g_prefs+0x10` == 0 after the first, == 1 after the second; `g_pickDiag+0x20` (`lastHit`) == `PICK_MODE`; `g_pickDiag+0x28` (`tapN`) increments by 1 each time |
| **T2** | **drag-scroll** | `["touch_hold",160,200,4]`, `["touch_hold",160,176,4]`, `["touch_hold",160,152,4]`, `["touch_hold",160,128,4]`, `["touch_release"]` | `g_pickDiag+0x0c` (`topRow`) goes 0→3 (72 px ÷ 24 = 3, clamped at maxTop=3); `dragN` +1; **`tapN` unchanged** (the drag-never-activates proof); `lastHit` still -1 |
| **T3** | scroll clamp, both ends | T2, then `["touch_hold",160,40,4]`…`["touch_hold",160,220,4]`, release | `topRow` == 3 at the top of the drag and **== 0** at the bottom; never negative, never > 3 |
| **T4** | tap-vs-drag, 6 px | `["touch_hold",60,80,2]`, `["touch_hold",60,85,2]`, `["touch_release"]` → then the same ending at `60,92` | first: `tapN` +1 and `lastHit == PICK_SLOT_A`; second: `dragN` +1, `tapN` unchanged |
| **T5** | slot cards commit | `["touch_hold",160,200,4]`…(scroll)… then `["touch",160,80]`, `["touch",160,135]` | `g_pickDiag+0x14` (`idxA`) and `+0x18` (`idxB`) both ≥ 0 and equal to the rows visible at that scroll offset; tapping card A again returns `idxA` to -1 |
| **T6** | START | T5, then `["touch",60,190]` | `g_pickDiag+0x34` (`startN`) == 1 and `g_renderSeq` keeps advancing into a session (`poll g_renderSeq --changed`); a capture at +3 s shows the in-game HUD, not the picker |
| **T7** | START—LINKED | T5, then `["touch",220,190]` | `startN` == 1 **and** the session starts linked — observable as the `⚡LINK` HUD chip in the capture |
| **T8** | START not-ready is inert | fresh picker, `["touch",60,190]` | `startN` == 0, `g_renderSeq` still in the picker, capture unchanged |
| **T9** | settings chip | `["touch",40,222]` | the capture at +2 s is the Settings screen (top screen reads "Settings"); `g_menuDiag` magic reads `0x4D4E5531` (the settings screen writes it) |
| **T10** | dead gutter | `["touch",140,190]` (dual) | `startN` == 0, `lastHit` == -1 |
| **T11** | console START | `["tap","START"]` on a ready picker | `startN` == 1 with the **picked** paths — assert `g_pickDiag.idxA/idxB` unchanged and the session's HUD names match the picked titles (the I1.7.1 regression: today this launches the defaults) |
| **T12** | pause rail, all six | six `["touch",40,y]` at y = 20, 50, 80, 110, 145, 175 | `g_menuDiag` `tab` reads 0,1,2,3,4,5 in order |
| **T13** | pause segment | on DISPLAY, `["touch",120,40]` then `["touch",270,40]` | `scaleMode` is a stack local → assert instead through `settings.bin` (the seg calls `settings_save`) with `run sdmc cat`, plus `g_menuDiag.lastHit`; for a pure-gdb assertion use the TILT seg on ENHANCE → `g_prefs+0x1c` (0→3) |
| **T14** | **swatch by d-pad** (I3.3) | pause → TOUCH tab → d-pad to the swatch row → `["tap","DRIGHT"]` ×2 | `g_prefs+0x14` (`padColor`) 0→2. **Fails today** — this is the regression test for the fix |
| **T15** | pause tab scroll | DISPLAY tab, `["touch_hold",200,200,4]` … `["touch_hold",200,120,4]`, release | `g_menuDiag.scroll` 0→~53 then clamped at `maxScroll = 281-228 = 53`; a capture shows the frameskip toggle fully on-screen and **not** overlapping the hint line |
| **T16** | scrolled hit test | after T15, `["touch",284,181]` (the frameskip toggle at `y=263-53=210`… recompute for the final scroll) | `fsOn` flips — observable via `settings.bin`; and the same tap at scroll 0 must hit nothing |
| **T17** | gamepad zones | in-session, touch=Gamepad, one `["touch",x,y]` at the centre of each of the nine drawn zones | `g_padDiag.zoneN[i]` == 1 for each i; `lastKeys` matches the expected GBA mask |
| **T18** | gamepad menu chip (I3.7) | `["touch",291,225]` | `g_padDiag.menuChipN` == 1 **and** `lastKeys` == 0 (no stray A); capture shows the pause menu |
| **T19** | wireless lobby (I3.5) | LINK → Wireless lobby, then `["touch",160,53]` (centre of the drawn Host button) | the capture shows the hosting state; and `["touch",160,180]` (drawn Online button) must **not** exit the lobby |
| **T20** | resume-prompt focus (I3.1) | `["tap","DDOWN"]`, `["tap","A"]` | the second button ("Pick new games") activates — capture shows the picker |

- **I4.2.1** Every test's capture must be **read by the agent**, and what was seen pasted into
  `docs/phase17-uifix/BUILDLOG.md`. A green gdb read with an unlooked-at PNG is not a pass — the
  whole point of D2 is that the *state* was changing (to the wrong value) while the screen looked
  frozen.
- **I4.2.2** Record `--with-state g_pickDiag+0x0c` on T2/T3 so the manifest carries `topRow` **per
  frame** — the scroll curve is then visible without polling, at the documented ~0.4 s/frame cost.
- **I4.2.3** All movies live in `docs/phase17-uifix/movies/*.json` (versioned), not in `runs/`
  (which is evidence, not source).
- **I4.2.4** Theme sweep: re-run T1, T12 and T15 with `setbin.py theme=2` (Daylight). Input must be
  theme-independent by construction; this catches a hit table that accidentally reads a theme
  colour or a focus ring that vanishes.
- **I4.2.5** Harness hygiene per the brief: one Azahar at a time, `azctl stop` always,
  `clean-fixtures` after any `--fresh-sd-fixtures` run, never touch `sdmc/dual-gba`.

### I4.3 — Suite budget

`test_uihit.c` should land around **150–200 checks** (9 pad zones × 8 points, 6 picker targets × 8,
12 rail boundaries, 10 gesture cases, 8 clamp cases, 6 wireless rects × 4). Add it to whatever
runs the existing six suites, and record the new count in the file header the way
`test_celiolink.c` does. The six existing suites (celiolink 1259, netlink 66, diag 369, control
6897, trace_replay 58, tilt 1694) plus `test_presence.c` must stay green — none of them include
`ui.c`, `rompicker.c`, `touch.c` or `main.c`, so `uihit.c` is additive and cannot disturb them.

---

## Open questions

- **Q1 — Where does the picker's settings affordance go?** No plate-empty band on
  `select-*-bot` is ≥ 20 px tall (measured: 8, 11 and 7 px), so the chip cannot be moved out of the
  baked footer hint's way; it needs either a re-composited footer row (overpaint `0,214,320,26`,
  redraw hint + chip together) or regenerated plate art. Input can live with any answer as long as
  drawn rect == hit rect. **Owner: SPEC-visual / design.**
- **Q2 — Does the picker get a second focus zone?** Today the bottom-screen controls are
  button-reachable but not d-pad-*focusable*. A "d-pad DOWN off the last row moves focus to the
  button row" model is more consistent with the rest of the app, but it changes list navigation
  (DOWN currently wraps). I3.2's key hints are the cheap alternative. Recommend hints now, focus
  zone only if the user asks.
- **Q3 — Volume bar: dead middle or drag-to-set?** Drag-to-set matches the design (a bar with a
  numeric value) and reuses the gesture machine, but it is the only *continuous* control in the app
  and needs its own clamp/step rules. Dead middle is one line and unifies the two screens today.
- **Q4 — `savpicker_run`: touch, or an honest "d-pad only" label?** It is a rarely-used developer-
  ish path (LINK → Load .sav). Adding drag-scroll is ~15 lines once `uihit` exists; doing nothing
  is defensible if the screen says so.
- **Q5 — Does the TOUCH tab get the design's "See gamepad" button** (`93,294,208,31`)? It only
  becomes reachable once tab scrolling exists (I2.4.2), and it duplicates the two Preview buttons
  already on the tab. Affects `CONTENT_H[5]` (325 vs 284) and `PTABN[5]` (6 vs 5).
- **Q6 — Should a drag that starts on a slot card scroll the list (I2.2.7) or be rejected?**
  Specified as "scroll", because dead bands are worse; but on a screen where the cards are the
  primary targets, a user who drags off a card may expect nothing to happen. Cheap to flip.
- **Q7 — Does `pad_keys` keep multi-touch-free semantics?** The 3DS reports one point, so exactly
  one zone can be pressed per frame; the current `if` chain and the proposed `uihit_index` both
  return the first match. Worth confirming nobody relies on diagonal d-pad input (the current
  overlapping boxes make some diagonals *impossible*, so the fix may change how the pad feels —
  flag it for the hardware pass).
- **Q8 — In-game Smart mode: should a tap on the menu chip be swallowed** before
  `touch_to_gba()` maps it into the game? Yes per I3.7, but it carves a 45×18 hole out of the
  playfield that Smart mode currently pipes to the game. Alternative: keep the chip PAD-only and
  drop it from the Smart draw.
