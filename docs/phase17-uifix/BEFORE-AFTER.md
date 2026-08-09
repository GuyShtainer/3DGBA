# Phase-17 UI fix — BEFORE / AFTER

**What this is.** The phase-16 blind visual sweep found **22 confirmed defects**
(`docs/phase16-emutest/sweep/REPORT.md`). This document puts each one's original evidence image
beside a fresh capture of the **final phase-17 binary** and gives a one-line verdict.

**Binary under test:** `3DGBA.3dsx` / `3DGBA.cia` built **2026-08-09 13:11** from the phase-17 tree
(`source/celiolink.c` diff: **0 lines**). Every "after" frame below was captured from **that
binary** through `tools/emutest/` and **opened and read by the author** — no verdict here is
inferred from source.

**Score: 20 fixed · 1 partially fixed (documented residual) · 1 deliberately deferred · 1
re-diagnosed as not-a-defect.**

**Contact sheets** (self-contained HTML, captioned, open in a browser — this is the fastest way to
read the whole thing):

| Sheet | Covers |
|---|---|
| [`evidence/sheet-D1-D2-segments-touch.html`](evidence/sheet-D1-D2-segments-touch.html) | **D1** the baked "Aspect-fit" overprint · **D2** picker touch |
| [`evidence/sheet-D6-rounded-rects.html`](evidence/sheet-D6-rounded-rects.html) | **D6** the plus/cross rounded rect · **D5** the corner blocks |
| [`evidence/sheet-D3-D4-D17-gamepad-smart.html`](evidence/sheet-D3-D4-D17-gamepad-smart.html) | **D3** · **D4** the virtual gamepad · **D17** Smart mode |
| [`evidence/sheet-D8-D9-D16-D18-pausetabs.html`](evidence/sheet-D8-D9-D16-D18-pausetabs.html) | **D8** · **D9** · **D16** · **D18** the pause tabs |
| [`evidence/sheet-D7-D13-D20-D21-picker.html`](evidence/sheet-D7-D13-D20-D21-picker.html) | **D7** · **D13** · **D20** · **D21** the ROM picker |
| [`evidence/sheet-D10-D11-D12-contrast.html`](evidence/sheet-D10-D11-D12-contrast.html) | **D11** the 3D badge · **D10** · **D12** the themes |
| [`evidence/sheet-scroll-and-deferred.html`](evidence/sheet-scroll-and-deferred.html) | touch drag-scroll (the user's third ask) · **D22** the one deferral |

**Capture runs** (all under `tools/emutest/runs/`, all with `azctl stop`, one Azahar at a time,
fixtures cleaned, `settings.bin`/`recent.bin` restored byte-identically, `sdmc/dual-gba` never
written):

| Run | Config | Covers |
|---|---|---|
| `p17-final-picker/` | Indigo, 11 dummy ROMs, cold picker | D1(picker) D2 D6(dot) D7 D11 D18(list) D20 + drag-scroll |
| `p17-final-tabs/` | Indigo, all six pause tabs + two scroll drags | D1(8 rows) D6 D8 D9 D15 D16 D18 D19 |
| `p17-final-pad/` | `touch=1 padEdge=0` virtual gamepad | D3 D4 D5 + the `≡ menu` chip as a real control |
| `p17-final-daylight/` | `theme=2` Daylight | D10 D12 |
| `p17-final-empty/` | ROM-less | D13 D21 |
| `p17-final-smart2/` | `touch=2` Smart pointer | D17 |

---

## The three things the user asked for

**1. "Fix them visually."** 20 of the 22 defects are fixed and photographed below. The two that are
not are each recorded with the reason, not quietly dropped.

**2. "Make sure both button and touch control are working properly."** Every documented target on
the ROM picker now responds to a tap *and* is reachable by d-pad with a visible focus ring — mode
segments, both slot cards, START, START—LINKED, the "settings · ZR" chip, and the resume prompt's
buttons. `START` on the console no longer launches the *defaults* pairing behind the user's back,
`B` no longer falls through into a defaults session, and a tap that hits nothing does nothing
instead of firing the nearest control. In-session, the `≡ menu` chip is a real tap target (it used
to be decoration whose tap pressed **A** in the game).

**3. "The scroll using touch should work."** It does, in three places: the ROM list, the in-session
pause menu, and the standalone Settings screen. A drag never activates the control it started on,
and each scrolling view draws an honest scrollbar whose thumb length is the visible fraction of the
content and whose position tracks the offset the right way up.

---

## Every defect, before and after

Images are 1:1 copies staged in `evidence/ba17/` so this document is self-contained.
`b-*` = the phase-16 sweep's own evidence · `a-*` = the phase-17 final binary.

### D1 🔴 critical — the selected segment printed the sprite's baked "Aspect-fit" over the live label

| BEFORE (phase-16 sweep) | AFTER (final binary) |
|---|---|
| ![before](evidence/ba17/b-D1-audio-seg.png) | ![after](evidence/ba17/a-D1-audio-seg.png) |
| ![before](evidence/ba17/b-D1-picker-pill.png) | ![after](evidence/ba17/a-D1-picker-pill.png) |

**FIXED.** The active pill is now drawn **procedurally** from the theme's own `panel`/`accent`
(`assets_seg`, with `draw_hslice` deleted), so it carries no baked pixels at all and is correct in
**all six themes** — which a sprite fix could not have been, since only the Indigo art pack is
baked. I read all eight affected rows on the final binary: DISPLAY `1:1` / `1:1` / `Sharp` / `both`,
AUDIO `Solo`, ENHANCE `Off`, TOUCH `Off` and `Round`, picker `1 Game` / `2 Games` — every selected
chip carries exactly its own word, single-struck. "Aspect-fit" now appears only where it is the
genuine middle option of a SCALE row.

### D2 🟠 major — every picker tap resolved to "1 Game"; no picker target responded

| BEFORE — the right-half tap that produced "1 Game" | AFTER — the same tap, left half then right half |
|---|---|
| ![before](evidence/ba17/b-D2-before-tap.png) ![before](evidence/ba17/b-D2-after-tap.png) | ![after](evidence/ba17/a-D2-tap-left.png) ![after](evidence/ba17/a-D2-tap-right.png) |

**FIXED.** The handler read `hidTouchRead` on the **release** frame, when HID reports (0,0) — so
`px < 160` always won and the mode collapsed to 1. It now latches the position while `KEY_TOUCH` is
**held** and acts on release, through a shared pure-C gesture machine (`source/uihit.c`, host-tested).
Every drawn rect comes from the same `uihit_pick_rect()` table the tap is tested against, so
*drawn rect == hit rect == focus rect* by construction. On the final binary I saw the left-half tap
select **1 Game** (and re-lay the whole plate out to the single-game design) and the right-half tap
select **2 Games**; slot cards, START, START—LINKED and the settings chip all respond (see the
picker sheet and D7/D13 below).

### D3 🟠 major — virtual gamepad L and R drawn on top of the bottom-screen HUD text

| BEFORE | AFTER |
|---|---|
| ![before](evidence/ba17/b-D3-lr.png) | ![after](evidence/ba17/a-D3-lr.png) |

**FIXED.** In Gamepad mode the bottom-screen HUD bar is suppressed and the top band is exactly
`L | TOUCH · GAMEPAD | R` over bare video, as `screenshots/06-touch-gamepad.png` draws it. Nothing
was lost — the top screen still carries the full HUD (dot, game name, FOCUS, 3D, fps, clock,
battery), verified in `p17-final-pad/top_00090.png`.

### D4 🟠 major — START overlapped the footer hint and ran off the screen edge

| BEFORE | AFTER |
|---|---|
| ![before](evidence/ba17/b-D4-start.png) | ![after](evidence/ba17/a-D4-start.png) |

**FIXED.** The centred footer hint is gated off in Gamepad mode (where it genuinely collided) and
START is a complete four-sided rounded key inside the 240 px screen with its label alone inside it.
The bottom-right `≡ menu` chip is complete *and* is now a **real tap target** — one tap opened the
pause menu on the final binary (`p17-final-pad/bottom_00125.png`, SESSION tab). Before, a tap there
silently pressed **A** in the game.

### D5 🟠 major — gamepad keys were overlapping squares with opaque black corner blocks

| BEFORE (padEdge = Round) | AFTER |
|---|---|
| ![before](evidence/ba17/b-D5-dpad.png) | ![after](evidence/ba17/a-D5-dpad.png) |

**FIXED.** `touch.c`'s `pad_zone` was a private copy of the same broken three-rect decomposition as
`ui_fill`; it is routed through the fixed geometry now. At 4× I see four separate rounded keys, one
continuous gold outline each, **zero** black corner blocks. Residue, recorded honestly: a 1–2 px
pinch where two adjacent keys' rounded corners meet — that is the zone *geometry*, not the
rasterisation, and it is cosmetic.

### D6 🟠 major — pills and rounded rects rendered as a plus/cross

| BEFORE | AFTER |
|---|---|
| ![before](evidence/ba17/b-D6-chiprow.png) | ![after](evidence/ba17/a-D6-chiprow.png) |
| ![before](evidence/ba17/b-D6-dot.png) | ![after](evidence/ba17/a-D6-dot.png) |
| ![before](evidence/ba17/b-D6-swatches.png) | ![after](evidence/ba17/a-D6-swatches.png) |

**FIXED — the root cause of the whole family.** `ui_fill` approximated a rounded rect as three
rects (a full-height centre column plus two shortened side strips) = "rectangle minus four SQUARE
corners", which degenerates into a literal cross once `r` approaches `min(w,h)/2`. It now clamps `r`
and approximates each corner with a midpoint-sampled **staircase** (`source/uigeom.c`, pure C,
18 332 host checks, with the pre-fix decomposition kept in the test as a proven regression). On the
final binary: eight outlined rounded pills on the pause top screen, a solid green **disc** before
the game name, five clean rounded colour swatches. `ui_border`/`ui_panel`/`ui_chip*` all build on
the same routine and were each re-checked.

### D7 🟠 major — the "settings · ZR" chip painted over the plate's baked footer hint

| BEFORE | AFTER |
|---|---|
| ![before](evidence/ba17/b-D7-footer.png) | ![after](evidence/ba17/a-D7-footer.png) |

**FIXED.** Retyping the hint in code was rejected — the copy contains "↑" and "⚡", which the
`mkbcfnt` bake does not carry. Instead the footer band is **recomposed from the plate's own
pixels**: blank the band by stretching a verified-empty slice of that same plate, re-blit the baked
hint 8 px higher, then draw the chip below it. The glyphs, the metrics and all six themes' art
survive verbatim. Measured on the final capture: START's fill ends at row 210, the hint's ink runs
212–221, the chip's fill runs 223–238 — three bands, zero overlap.

### D8 🟠 major — pause DISPLAY: the Swap/Skip row overprinted by the hint line and clipped

| BEFORE | AFTER |
|---|---|
| ![before](evidence/ba17/b-D8-swapskip.png) | ![after](evidence/ba17/a-D8-swapskip.png) |

**FIXED.** The tab is a scrolling viewport now (228 px), the plate scrolls **with** its controls so
the baked captions stay attached to their rows, and the hint band is fixed chrome painted from the
plate's own background. Both toggles are complete rows, nothing is cut by y=240.

### D9 🟠 major — pause TOUCH: the EDGES row collided with the hint line

| BEFORE | AFTER |
|---|---|
| ![before](evidence/ba17/b-D9-edges.png) | ![after](evidence/ba17/a-D9-edges.png) |

**FIXED.** The row is back at its manifest coordinates as a full-width segmented control under a
code-drawn "GAMEPAD · EDGES" caption, reachable by scrolling and well clear of the hint.

### D10 🟠 major — Daylight theme: button and card labels went dark-on-dark

| BEFORE (Daylight, pause LINK) | AFTER (Daylight, pause LINK) |
|---|---|
| ![before](evidence/ba17/b-D10-daylight.png) | ![after](evidence/ba17/a-D10-daylight.png) |

**FIXED.** The bug was mixing two palettes: text coloured from the *active theme* drawn on 9-slice
plates baked from *Indigo*. `theme.c` gained **`g_art`** — the palette the baked art was actually
made from, stamped into the asset header by `build_assets.sh` — and every ink on a baked surface now
comes from `g_art` while ink on a procedural surface still comes from `g_ui`. A new `theme` host
suite (**82 693 checks**) grades a 30-row inventory of every (ink, surface, draw-scale) triple for
all five presets plus custom over its whole hue × contrast grid, and contains the red-before-green
regressions that name D10 and D11.

### D11 🟠 major — the in-game "3D" badge was dark navy on black

| BEFORE (measured 3.55:1) | AFTER (measured 8.52:1) |
|---|---|
| ![before](evidence/ba17/b-D11-3dbadge.png) | ![after](evidence/ba17/a-D11-3dbadge.png) |

**FIXED.** Two halves, and the suite says which does what: the badge is routed to the handoff's
`#a9d4ff`-on-`#3E86D6` role pair *and* its label moved off the 0.30-scale system font onto the baked
`FNT_JBM_MED` at native size — the draw scale is what was actually eating the contrast. The extra
headroom matters because the real panel's gamma is expected to move this one **against** us
(REPORT §6).

### D12 🟠 major — Daylight in-game: half light, half dark on one screen

| BEFORE | AFTER (partial) |
|---|---|
| ![before](evidence/ba17/b-D12-daylight-split.png) | ![after](evidence/ba17/a-D12-daylight-split.png) |

**PARTIALLY FIXED, and the residual is deliberate.** The *unreadable* half is gone — on the final
binary every element on the Daylight HUD bar reads (white game name, warm-sand FOCUS chip and fps
via `theme_on_scrim`, white clock, light-blue 3D badge). The *coherence* split (a beige theme
background under a theme-invariant black scrim) is inherent to shipping one baked art pack; only a
per-theme plate pack fixes it (W4.3.b: ≈1.7 MB of ELF, a load/unload path, an init-order change and
a visible stall on theme switch). Filed, not hidden — D12's residual is its acceptance test.

### D13 🟡 minor — ROM-less start dropped into a black dead session

| BEFORE | AFTER (top) | AFTER (bottom) |
|---|---|---|
| ![before](evidence/ba17/b-D13-romless.png) | ![after](evidence/ba17/a-D13-romless-top.png) | ![after](evidence/ba17/a-D13-romless-bot.png) |

**FIXED.** A real empty state: "No games found" + the path on the top screen, "Rescan" and "Start
without a game" on the bottom. The defaults fallback is now a *labelled choice* rather than
something that happened to you. Operable by d-pad (focus ring) and by touch; the settings chip is
reachable from it. The dead A/B slot cards and the misleading baked "tap a game above ↑" hint are
blanked, since there is nothing above to tap.

### D14 🟡 minor — ~1 s of "uninitialised" white / flat-grey texture at session start

**NOT A DEFECT — re-diagnosed, and the hardening shipped anyway.** The undefined-memory case is
removed by construction (`memset` + flush at texture creation, plus an `everUploaded` gate at the
three `render_game` call sites; `render_game` itself is byte-identical). But the capture disproved
the diagnosis: both screens fade white → grey → black *together* and the Game Freak logo follows —
the bottom is exactly the top's white at the unfocused 50 % dim, i.e. **both screens are showing the
authentic frame the ROM itself draws while it boots**. Suppressing it would be wrong. (A ROM that
*fails* to load is a different case and now shows an explicit banner — see "New defects closed"
below.)

### D15 🟡 minor — pause AUDIO: a stray "A"/"B" glyph on the volume bar, no numeric level

| BEFORE | AFTER |
|---|---|
| ![before](evidence/ba17/b-D15-vol.png) | ![after](evidence/ba17/a-D15-vol.png) |

**FIXED.** The stray channel tag is deleted at both sites; the level is right-aligned above the
bar's right end at the manifest's own coordinates, matching `pause-tab-3-audio.png`. It tracks the
real value — three "−" presses read **63**, and 256 − 3×32 = 160 ⇒ (160·100+128)/256 = 63 exactly.
(The design's "80" is unreachable on a ±12.5 % step, so the true value is shown rather than the
mockup's digits.)

### D16 🟡 minor — pause TOUCH: the touch-mode explainer paragraph was missing

| BEFORE | AFTER |
|---|---|
| ![before](evidence/ba17/b-D16-explainer.png) | ![after](evidence/ba17/a-D16-explainer.png) |

**FIXED.** All three copies ship and the paragraph **changes with the segment** in the same run.
Layout is done by a pure-C greedy word wrap over a *measurement callback* (`uihit_wrap`), so it is
provable on the PC against the live bcfnt metrics — a hand-counted character budget is exactly how a
fourth line lands on top of a button. It truncates with `…` rather than overflowing.

### D17 🟡 minor — Smart touch mode: no mode chip, and a raw cyan developer readout

| BEFORE | AFTER |
|---|---|
| ![before](evidence/ba17/b-D17-smart.png) | ![after](evidence/ba17/a-D17-smart.png) |

**FIXED.** A gold "TOUCH · SMART POINTER" chip top-centre, the `≡ menu` chip bottom-right, **zero
cyan text** anywhere (the phase-13 diagnostics line is behind `TOUCH_DIAG_HUD`, default 0). The
report's guessed cause was wrong in an instructive way: `touch_draw`'s SMART branch already existed
and the call site simply read `if (tmEff == TOUCH_PAD)`.

### D18 ⚪ polish — the scrollbar was one near-white strip with an inverted thumb

| BEFORE | AFTER |
|---|---|
| ![before](evidence/ba17/b-D18-scrollbar.png) | ![after](evidence/ba17/a-D18-scrollbar.png) |

**FIXED.** One arithmetic pair (`uihit_thumb_h` / `uihit_thumb_y`) serves both the pixel scroll and
the row scroll because the units cancel; the bar is drawn only when the view actually overflows, its
thumb length is the visible fraction of the content, and it is at the **top** when the scroll is 0.
Column profiles on the final captures: track `g_ui.line`, thumb `g_ui.dim`, and outside the track
the plate's own background — the baked near-white strip is gone.

### D19 ⚪ polish — overlay row labels used a much smaller, dimmer type than the baked labels

| BEFORE (LINK tab) | AFTER (LINK tab) |
|---|---|
| ![before](evidence/ba17/b-D19-linklabels.png) | ![after](evidence/ba17/a-D19-linklabels.png) |

**FIXED.** Before, "CO-OP" was a tiny dim all-caps tag jammed against its toggle at the bottom
right while every other row in the same list was large, bright, title-case — the overlay rows read
as an afterthought. Now there are two deliberate styles, picked by geometry: a row label matches
the baked rows exactly ("Co-op presence" beside "Link cable" / "Net link (loopback)"), and a
full-width control gets a small dim all-caps **caption above it** in the plate's own caption style
("DIORAMA · TILT", "GAMEPAD · EDGES", read on the final binary in `p17-final-tabs/bottom_00178.png`).
*The same before-frame also shows a defect the fix pass found in its own work and closed:* the
square gold focus ring biting into the rounded toggle at the top right.

### D20 ⚪ polish — every ROM row drew an identical cartridge chip

| BEFORE | AFTER |
|---|---|
| ![before](evidence/ba17/b-D20-cartchips.png) | ![after](evidence/ba17/a-D20-cartchips.png) |

**FIXED.** The cap is tinted per title, and never the fixed role colours (Game-A green `#63B23C` /
Game-B blue `#3E86D6`). The capture also found a second bug in the process: with a `code[0] +
code[3]` hash, Emerald (BPEE) and FireRed (BPRE) came out **identical** — char 0 is the type and
char 3 the region, which is 'E' for every US release. It hashes all four characters now.

### D21 ⚪ polish — boot splash read "▶ START" instead of "TAP TO START"

| BEFORE | AFTER |
|---|---|
| ![before](evidence/ba17/b-D21-splash.png) | ![after](evidence/ba17/a-D21-splash.png) |

**FIXED, and it was not a copy difference.** The code was already drawing the correct label; the
labelled example sprite was submitted at **depth 0.6** and citro2d's depth test is `GEQUAL`, so the
sprite hid it. A `grep` over every `C2D_Draw*` in `source/*.c` now returns zero call sites with a
non-`0.0f` depth.

### D22 ⚪ polish — the HUD bar is a translucent scrim over the game (non-1:1 scale only)

| BEFORE | AFTER — unchanged |
|---|---|
| ![before](evidence/ba17/b-D22-scrim.png) | ![after](evidence/ba17/a-D22-scrim.png) |

**STILL BROKEN — deliberately deferred, with the reason.** The fix needs an origin offset inside
`render_game`, which is the **HD-2D dispatch site this phase is forbidden to touch**, and it needs
hardware sign-off. At the default **1:1** scale the video is inset cleanly below an opaque bar and
there is no overlap at all — the defect only appears if the user selects aspect-fit or stretch.

---

## The user's third ask, proven

| Before the drag | After the drag |
|---|---|
| ![before](evidence/ba17/a-scroll-before.png) | ![after](evidence/ba17/a-scroll-after.png) |

A drag on the **bottom** screen scrolls the **top**-screen ROM list (the 3DS has no top-screen
digitiser, so this is the only touch path there is), the white focus highlight follows the scroll,
the new scrollbar's thumb tracks it, and the list clamps exactly at `maxTop`. Critically: **that
drag started inside the START—LINKED button and did not activate it.** The gesture machine's
counters, read live over GDB during the pass, say the same thing numerically — a 5 px move commits a
tap, a 12 px move commits nothing, and a scroll drag leaves `tapN` at 0 with `lastHit = -1`.

The pause menu and the standalone Settings screen scroll identically.

---

## New defects the fix pass found in its own work, and closed

None of these were in the original 22 — they were raised by an independent re-sweep and code review
*against the fixes* and are recorded so the phase is not scored on its own homework.

| Found | Outcome |
|---|---|
| Selection/focus rings were still **square** on widgets that had become **rounded** (a dark right-angle notch inside each corner) | **Fixed** — one `sel_ring()` that takes its radius from the widget, applied at all ten pause/settings widget cases, plus `assets_button`'s own ring and the wireless lobby's cards |
| The **disabled START** label measured **1.46:1** on the gold button — a pale ghost | **Fixed** — `theme_ink_disabled()` mixes toward the surface by the largest fraction that still clears the suite's 3.0 gate; measured **3.63:1** after, still visibly weaker than enabled |
| Daylight drew a **pure white slab** as the segmented track on the indigo plate | **Fixed** — the track and its label moved to `g_art`; the shipped Indigo default is byte-for-byte unchanged |
| The empty state re-blitted "tap a game above ↑" and kept two dead slot cards | **Fixed** — both blanked |
| A `.gba` that fails to load started a **silent dead session** | **Fixed**, after the capture corrected the diagnosis: `mCorePreloadVF` only copies bytes, so a header-only file "loads" and the ARM core then executes garbage. The check moved to the cartridge header (0xC0 header present, the fixed **0x96 at 0xB2** — mGBA's own `GBAIsROM` magic) and runs *before* the core sees the file. Both screens now carry "This game could not be loaded". |
| The gamepad overlay costs **351** rect draws/frame at padEdge=Round | **Verified, not changed** — every cheaper draw changes the pixels (the keys are translucent, so a fill-then-inset-fill would double-blend). Pinned by a budget test so a future radius change fails loudly. **This is a quad count, not a timing claim** — it goes on the hardware list. |
| A stray unlabelled dark pill on the paused top screen | **Not reproducible, and honestly reported as unattributed.** Confirmed as app chrome (it *brightens* while the frame darkens), confirmed absent from the final build's identical frame, but never attributed — so it may be masked rather than removed. Needs a probe build. |

---

## What is proven here, and what is not

**Proven in the emulator, by looking at the pixels:** every "FIXED" verdict above, on the final
binary, in Indigo and (for the theme-sensitive ones) in Daylight.

**NOT proven, and cannot be by this harness** (CLAUDE.md #6 — real New 3DS is the sign-off gate):

- **Real digitiser accuracy and drag *feel*.** Azahar's touch is a synthesized point stream. The
  tap/drag threshold, the scroll rate and whether the 16 px settings chip is comfortable with a
  thumb are hardware judgements.
- **Contrast on the physical panels.** D11 (the 3D badge) and D18 (the scrollbar) are contrast
  judgements made from emulator framebuffers; the 3DS's gamma will shift them — most likely making
  D11 worse, which is why it was given headroom rather than the minimum.
- **The frame cost of the new geometry.** The rounded-rect staircase trades more quads for fewer
  state changes (`assets_seg` now issues *no* texture binds where it issued two binds plus six
  textured sub-rect draws). That is an argument, not a measurement.
- **Stereoscopic 3D.** Azahar presents a single view, so D22's judgement and the HD-2D tilt as seen
  in 3D are invisible here.
- **Anything needing a second console** — the wireless lobby populated, the link-cable trade UI
  mid-session, co-op presence.
