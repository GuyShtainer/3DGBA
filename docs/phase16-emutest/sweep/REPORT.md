# Phase-16 blind UI sweep — final report

**Date:** 2026-08-09 · **Build under test:** the `3DGBA.cia` staged by `azctl` (harness commit `fce78ba`)
**Method:** four sweep agents (g1 boot/picker, g2 in-game HUD, g3 pause tabs, g4 features/modes)
drove the emulator through `tools/emutest/`; a fifth **adversarial verifier** then re-drove every
claim from its own repro and judged it from its own pixels. Only claims the verifier could SEE are
reported as confirmed.
**Evidence root:** `tools/emutest/runs/sweep-verify/` (authoritative — the verifier's own captures),
with `tools/emutest/runs/sweep-g{1,2,3,4}-*/` as the originals.
**Contact sheets:** `docs/phase16-emutest/sweep/evidence/*.html` (self-contained, open in a browser).

---

## 1. Headline

The sweep covered the whole cold-start path (splash → resume prompt → ROM picker), the in-game HUD
on both screens, all six pause tabs in default and toggled states, the virtual gamepad and Smart
touch modes, the wireless lobby, the Daylight theme, and the ROM-less boot — 13 emulator runs and
roughly 5,500 captured frames in the verification pass alone.

**The one thing that must be fixed: every segmented control in the app is unreadable.** The
`seg-active` widget PNG ships with the design mockup's example label **"Aspect-fit"** baked into
the art, and the live option label is then drawn on top of it — so the *selected* chip, the one
chip that has to be readable, is a black smear of two superimposed strings while its unselected
neighbours are perfectly clean. It hits the game-select mode pill ("1 Game / 2 Games") and eight
rows across the pause menu (SCALE·TOP, SCALE·BOTTOM, FILTER·FOCUSED, HUD, MIX MODE, TILT, TOUCH
MODE, EDGES). In practice **the pause menu never tells you by name which option is selected.**
Everything else found is a step down in severity from that.

Two further clusters deserve a human's attention: **the ROM picker's touch input is effectively
dead** — every tap resolves as a tap at (0,0), which silently collapses the mode to "1 Game", and
no picker control (settings chip, slot cards, START) responds at all — and **a family of code-drawn
rounded rects renders as a literal plus/cross**: the pause chip row, the in-game game-identity dot,
and the gamepad colour swatches.

Known-already items (the in-game HUD not using the baked `.bcfnt`; Display/Touch tabs not pixel-1:1
with the design; only the Indigo plate set built) were **not** counted as findings, except where the
Indigo-only plates produce an actually unreadable screen (D10/D12) — that is a shipping consequence
of the known gap, not a restatement of it.

---

## 2. Confirmed defects, ranked

Paths are relative to `projects/3DGBA/`. `V*` = a verifier run (recipes in §7).
"Suspected source" is the sweep agents' code hypothesis — **not verified from the pixels**; treat it
as a lead, not a diagnosis.

| # | Sev | Defect | Screen | What it looks like | Repro | Suspected source |
|---|-----|--------|--------|--------------------|-------|------------------|
| **D1** | 🔴 critical | Selected segment prints the sprite's baked **"Aspect-fit"** on top of the live label | Pause DISPLAY (SCALE·TOP, SCALE·BOTTOM, FILTER, HUD), AUDIO (MIX MODE), ENHANCE (TILT), TOUCH (TOUCH MODE, EDGES) **and** the game-select mode pill | The highlighted yellow chip carries two dark strings superimposed at a small offset — always the same intruder, "Aspect-fit". AUDIO's MIX MODE selected chip reads "Aspect-fit" beside clean neighbours "Mixed"/"Split"; DISPLAY's FILTER reads "Aspect-fit" beside "Smooth"; the HUD row's is an illegible blob. Unselected chips are clean. Only the two SCALE rows survive by coincidence (their correct word *is* "Aspect-fit") and even those look double-struck. | V3 `rec-tabs/bottom_00110.png` (DISPLAY), `bottom_00135.png` (AUDIO), `bottom_00160.png` (ENHANCE), `bottom_00210.png` (TOUCH); V1 `rec-picker/bottom_00050.png` + `bottom_00090.png` (picker, both modes) | `assets_seg()` (`source/assets.c:195-204`) draws `design_handoff_3dgba_ui/assets_3ds/widgets/indigo/seg-active.png` — the design's *example* pill with the label baked in — then centres the live text over it. Every `assets_seg` caller inherits it. |
| **D2** | 🟠 major | Every tap on the picker resolves to "1 Game"; **no picker touch target responds** | game-select, bottom screen | Touch-only movie: before any touch the pill is on the RIGHT (2 Games). The first touch lands on the RIGHT half — the mode already active — and the screen nevertheless flips to **1 Game**. The next four taps (left half, right half, the "settings · ZR" chip, the slot-A card) produce **zero** pixel change for the rest of the recording: still 1 Game, no settings screen, no slot filled. A tap on the resume prompt works correctly, so it is picker-specific. | V2 `rec-tap/bottom_00140.png` (before) → `bottom_00180.png` (after tap 1) → `bottom_00276.png` (after all taps); movie `m_tapprobe.json` | `rompicker_run` (`source/rompicker.c:228-236`) acts on the touch **release** edge (`kUp & KEY_TOUCH`) but calls `hidTouchRead` on that same frame, when HID reports (0,0) → `px<160` → `gameMode = 1`. `recent_prompt` reads on the press edge and behaves correctly. |
| **D3** | 🟠 major | Virtual gamepad **L and R are drawn on top of the bottom-screen HUD text** | in-game bottom screen, touch = Gamepad | The L button's box and translucent fill cut "+ Pokemon FireRed" in half at mid-glyph height; the R button swallows the right-hand readout so the "17fps 02:00" digits sit *inside* the button. Design `06-touch-gamepad.png` keeps L/R clear of the header row. | V5 `rec-pad2/bottom_00120.png` (any in-game frame) | `touch.c` `touch_draw()` PAD zone rects are placed in raw bottom-screen space with no offset for the HUD bar the session draws at the top. |
| **D4** | 🟠 major | Virtual gamepad **START overlaps the footer hint and is clipped by the screen edge** | in-game bottom screen, touch = Gamepad | The centred footer "START+SELECT · pause menu" runs through the button's lower half — the border crosses the letters and "SELECT" is behind the fill. The button has a top border but no bottom border: it runs off the 240 px screen and is cut. The "menu" chip at bottom-right is also half off-screen. | V5 `rec-pad2/bottom_00120.png` | the START zone's `y+h` exceeds 240, and the session's footer hint shares its baseline. |
| **D5** | 🟠 major | Gamepad keys are **overlapping squares with opaque black corner blocks** (padEdge = "Round") | in-game bottom screen, touch = Gamepad | At 4× every key is a plain square with a 1 px border plus four small opaque black squares stuck in its corners; the four D-pad squares overlap so each border is drawn across its neighbours — the pad reads as a lattice of crossing lines, not a D-pad. A/B/L/R/START carry the same blocks. **Specific to padEdge=0 "Round"** — with "Sharp" the blocks are absent. | V5 `rec-pad2/bottom_00120.png` vs V6 `rec-edge/bottom_00120.png` (compare.py: 4.67 % of pixels) | the rounded-corner sprite for pad keys is blitted as opaque squares at the corner offsets. |
| **D6** | 🟠 major | **Pills and rounded rects render as a plus/cross** — pause chip row, in-game identity dot, colour swatches | pause top screen; in-game HUD (both screens); pause TOUCH tab | All eight pause chips (3D · DoF · Bloom · Light · Tilt · Touch Off · Co-op · Link) are crosses: a taller centre block with a thin bar protruding left and right at mid height, each piece carrying its own border so the borders visibly cross. "Touch Off" and "Co-op" overflow onto the protruding bar. The in-game mark before the game name is a green **plus**, not a round dot. The five GAMEPAD·COLOR swatches are unmistakable crosses, made worse by the clean square white selection ring around the active one. *Scope correction:* the FOCUS chip on the same HUD bar is a clean rectangle — this is the pill/dot helper, not "every chip". | V3 `rec-tabs/top_00080.png` (chip row), `top_00040.png` (in-game dot), `bottom_00210.png` (swatches) | the 3-slice pill helper emits the caps at a different height/offset than the centre, so the two rects form a cross; the code-drawn rounded-rect fill has the same corner geometry bug. Possibly two call sites, one bug family. |
| **D7** | 🟠 major | **"settings · ZR" chip is painted on top of the plate's baked footer hint**, chopping it mid-word | game-select, bottom screen, both modes | An opaque rounded chip sits bottom-left directly on the baked hint line, which is cut mid-word: 2-game reads "…ove ↑ · ⚡ linked = trade / battle-ready", 1-game "…ull 3D + touch · link locally or online". Same baseline, so the two texts read as one broken sentence. Design `03-game-select.png` has the hint unobstructed and no chip at all. | V1 `rec-picker/bottom_00050.png` (2-game) / `bottom_00090.png` (1-game) | `source/rompicker.c:290-291` draws `ui_fill` + the chip last, over the plate's baked footer text. |
| **D8** | 🟠 major | Pause **DISPLAY**: the Swap/Skip toggle row is overprinted by the status hint line and clipped by the screen edge | pause DISPLAY tab, bottom of the plate | The hint "L/R tab  A select  B resume  (or tap)" runs straight through the last row: "Skip" is a smear on top of the hint's glyphs and the toggle's white knob sits on the text. Both toggles are also flush against — and cut by — the bottom screen edge. | V3 `rec-tabs/bottom_00110.png` | the DISPLAY tab has more rows than the 240 px plate holds; the last row's baseline coincides with the hint line's. |
| **D9** | 🟠 major | Pause **TOUCH**: the EDGES row collides with the hint line and a baked caption is sliced by the screen edge | pause TOUCH tab, bottom of the plate | "Soft" and "Sharp" have "…A select  B resume (or tap)" running through them letter-for-letter; the selected chip is a double-drawn yellow blob (D1 compounding); the baked caption below shows only the top few pixel rows of its glyphs before the screen ends. | V3 `rec-tabs/bottom_00210.png` | `PT_TOUCH`'s last `PK_SEG` overlaps the hint line; the plate art's last caption falls below y=240. |
| **D10** | 🟠 major | **Daylight theme: button and card labels go dark-on-dark** | resume prompt, pause SESSION/LINK tabs, wireless lobby | On LINK, "Save state", "Load state" and "Load .sav" are dark navy ink on the dark navy Indigo plate — barely legible at 1×; the identical labels are crisp white in the Indigo run. Same for "Change games" (SESSION), the game names inside the resume prompt's A/B cards, "Pick new games"/"Use defaults", and the lobby's "Scan for lobbies"/"Connect online (soon)". | V8 `rec-theme/bottom_00135.png` (LINK), `bottom_00110.png` (SESSION), `top_00030.png` + `bottom_00030.png` (resume), `bottom_00160.png` (lobby); baseline `rec-tabs/bottom_00185.png` | text colours come from the active theme while the 9-slice plates are the baked **Indigo** art. This is the shipping consequence of the known "only Indigo plates are built" gap: a non-Indigo theme is currently *unusable*, not merely off-palette. |
| **D11** | 🟠 major | The in-game **"3D" badge is dark navy text on black** — unreadable | in-game HUD, top screen | A 1 px blue outlined box whose two glyphs are almost the colour of the black bar; at 1× it is a smudge and even at 8× the glyphs are barely resolvable, while "59fps" beside it is crisp yellow and the game name crisp white. Same problem inverted on the pause row's 3D chip, and still wrong in Daylight (navy on grey). | V3 `rec-tabs/top_00040.png`; V8 `rec-theme/top_00090.png` | the badge's text colour token is not the handoff's `#a9d4ff`-on-`#3E86D6` role pair. |
| **D12** | 🟠 major | **Daylight theme in-game: half light, half dark on one screen** | in-game top screen, theme = Daylight | The letterbox around the GBA frame is near-white/beige while the HUD bar directly above it stays dark grey with dim orange text, and the pause menu two button-presses away is still dark navy. One screen, two colour worlds. | V8 `rec-theme/top_00090.png` | screens drawn from theme tokens vs screens/plates drawn from the baked Indigo art. Same root as D10. |
| **D13** | 🟡 minor | **ROM-less start has no empty state** — it drops into a black dead session | post-boot with no `.gba` files, both screens | With `sdmc:/3DGBA` ROM-less, no picker and no "no games found" copy is ever drawn. ~10 s after boot the app is *in a session*: top shows "+ gameA", FOCUS, 3D, 59fps, 02:00, battery over pure black; bottom "+ gameB", "tap screen · pause menu" over pure black. Nothing tells the user why both screens are empty. | V3 `rec-tabs/top_00040.png` + `bottom_00040.png` (after `azctl clean-fixtures`) | `main()` (`source/main.c:4448-4452`): `rompicker_run` returning false on a zero-ROM scan falls through to hard-coded default paths and starts a session. |
| **D14** | 🟡 minor | ~1 s of **uninitialised white / flat-grey texture** when a session starts | both screens, first frames of a session | At the instant the session appears the top screen's video rect is solid **white** and the bottom's flat **mid-grey**, HUD already drawn and reading 0fps, no spinner and no loading copy — it reads as a missing texture, not a deliberate loading state. *Correction to the original claim:* the grey is confined to the aspect-fit video rect (letterboxing intact), not the whole 320×240. | V4 `rec-rom/top_00133.png` + `bottom_00133.png` | the GBA framebuffer texture is presented before the first emulated frame has been uploaded (initial contents undefined). |
| **D15** | 🟡 minor | Pause **AUDIO**: a stray tiny "A"/"B" glyph clipped onto the volume bar; **no numeric level** | pause AUDIO tab, VOLUME·A / VOLUME·B | Each volume row has a tiny "A" (resp. "B") drawn just above the left end of the bar, half sitting on the bar's top edge and jammed against the round "−" button — it reads as a stray glyph, not a label. There is no percentage on the row; design `pause-tab-3-audio.png` puts the value (80 / 70) right-aligned above each bar. The bar fill itself does track the stepper. | V3 `rec-tabs/bottom_00135.png` | the `PK_STEP` row draws the channel letter at the bar origin instead of the level at the row's right edge. |
| **D16** | 🟡 minor | Pause **TOUCH**: the touch-mode explainer paragraph is missing, leaving a large empty band | pause TOUCH tab, between TOUCH MODE and the Preview buttons | ~55 px of empty plate where `pause-tab-6-touch.png` puts a 3-line explainer of the selected mode. Compounded by D1: since the selected chip is unreadable, *nothing* on the tab says what the active mode is or does. | V3 `rec-tabs/bottom_00210.png` | the v3 `PT_TOUCH` control list has no explainer text item (the v2 layout still listed one). |
| **D17** | 🟡 minor | **Smart touch mode**: no mode chip, no guidance labels, and a raw cyan **developer readout** on the footer line | in-game bottom screen, touch = Smart | The header carries only "+ Pokemon FireRed" and fps/clock — no touch-mode chip (Gamepad mode *does* get a "TOUCH · GAMEPAD" chip), none of `07-touch-smart.png`'s guidance labels, and a cyan `field p=9,4 key=-` is drawn bottom-left in a different font and colour, on the same baseline as the centred "START+SELECT · pause menu" hint. (Smart drawing no button overlay is by design; the debug string and the missing mode indicator are not.) | V4 `rec-rom/bottom_00215.png`, `bottom_00230.png` | the phase-13 touch diagnostics line is drawn unconditionally in SMART; the mode chip is emitted for PAD only. |
| **D18** | ⚪ polish | The right-edge **scrollbar is one near-white strip**, and its thumb position is **inverted** | pause DISPLAY and TOUCH tabs, right edge | A uniform light strip runs the full height of the right edge and is the brightest element on the screen. There *is* a thumb — a pixel profile gives track RGB≈183, thumb ≈211, Δ28/255 — invisible at 1×. And on the DISPLAY tab scrolled to the **top** the thumb sits at the **bottom** of the track (y 440-505 of 540), so the little position it does communicate is wrong. | V3 `rec-tabs/bottom_00110.png` | scroll-indicator track/thumb colours plus the thumb's position mapping. |
| **D19** | ⚪ polish | Overlay row labels (TILT, CO-OP, EDGES) use a **much smaller, dimmer type** than the baked labels on the same list | pause ENHANCE / LINK / TOUCH | On LINK, "Link cable" and "Net link (loopback)" are large bright title-case while the third row's label is a tiny dim all-caps "CO-OP" — different size, weight and colour in one list; the rows read as an afterthought. Same for TILT and EDGES. | V3 `rec-tabs/bottom_00185.png`, `bottom_00160.png`, `bottom_00210.png` | the overlay label path uses the small mono font while the plate labels are baked art. |
| **D20** | ⚪ polish | Every ROM row draws an **identical cartridge chip** — the per-title colour never appears | game-select, top screen list | Eight visible rows for eight different titles all carry the same dark-purple cart body with a green cap. Design `03-game-select.png` colours the chip per title, which is what makes the list scannable; the uniform green cap also collides with the fixed role colour for Game A. | V1 `rec-picker/top_00050.png` | the per-title tint is computed (`cart_tint`) but the row blits the untinted sprite via `assets_draw_wgt`. |
| **D21** | ⚪ polish | Boot splash button reads **"▶ START"** where the design says "▶ TAP TO START" | boot splash, bottom screen | The pill is crisp and perfectly legible with no overlapping second string — unlike D1, nothing is smeared. The only difference from `01-boot-splash.png` is the wording, because the label is baked into `btn-primary.png` and the code-drawn label is not visible. A copy difference from a mockup, not a rendering fault. | V1 `rec-picker/bottom_00018.png` | `run_splash` (`source/main.c:4370-4376`) draws the labelled example sprite; same baked-label family as D1, benign here. |
| **D22** | ⚪ polish | The HUD bar is a **translucent scrim over the top of the game image**, not a bar above it | in-game top screen, **non-1:1 scale only** | With SCALE = aspect-fit the video fills 400×240 and the bar sits on its first ~20 device px: a hard vertical seam at the video's left edge, black to the left, dimmed game pixels showing through to the right, the game's foliage readable behind the game name. **Precondition the original claim omitted:** at the default 1:1 scale the video is a 240×160 inset well below the bar and nothing overlaps. | V5 `rec-pad2/top_00120.png`; contrast V4 `rec-rom/top_00165.png` | the bar is a translucent overlay over a full-screen video rect rather than the video being inset below an opaque plate. |

---

## 3. Evidence per defect

Full-frame capture, then the zoom crop that makes the defect obvious. Paths relative to
`projects/3DGBA/`; `runs/` = `tools/emutest/runs/sweep-verify/`; report-side `zooms/` =
`docs/phase16-emutest/sweep/evidence/zooms/`.

| # | Full frame | Zoom crop |
|---|-----------|-----------|
| D1 (pause) | `runs/rec-tabs/bottom_00135.png` | `zooms/z-pause-seg-audio.png` (3×, MIX MODE) · `zooms/z-display-seg.png` (3×, SCALE·TOP) |
| D1 (picker) | `runs/rec-picker/bottom_00050.png` | `runs/zooms/z-seg.png` |
| D2 | `runs/rec-tap/bottom_00180.png` | before/after triple: `runs/rec-tap/bottom_00140.png` → `bottom_00180.png` → `bottom_00276.png` |
| D3 | `runs/rec-pad2/bottom_00120.png` | `runs/zooms/z-pad-lr.png` |
| D4 | `runs/rec-pad2/bottom_00120.png` | `runs/zooms/z-pad-start.png` |
| D5 | `runs/rec-pad2/bottom_00120.png` | `runs/zooms/z-pad-dpad.png` (Round, broken) vs `runs/zooms/z-pad-dpad-sharp.png` (Sharp, clean) |
| D6 | `runs/rec-tabs/top_00080.png` · `runs/rec-tabs/bottom_00210.png` | `runs/zooms/z-chiprow.png` · `runs/zooms/z-hud-dot.png` · `runs/zooms/z-swatches.png` |
| D7 | `runs/rec-picker/bottom_00050.png` | `runs/zooms/z-footer.png` |
| D8 | `runs/rec-tabs/bottom_00110.png` | `runs/zooms/z-display-bottom.png` |
| D9 | `runs/rec-tabs/bottom_00210.png` | `runs/zooms/z-touch-bottom.png` |
| D10 | `runs/rec-theme/bottom_00135.png` | `zooms/z-theme-buttons.png` (2×) |
| D11 | `runs/rec-tabs/top_00040.png` | `runs/zooms/z-hud-3d.png` |
| D12 | `runs/rec-theme/top_00090.png` | (full frame is the evidence — the split is screen-wide) |
| D13 | `runs/rec-tabs/top_00040.png` + `bottom_00040.png` | `zooms/z-romless-hud.png` (2×) |
| D14 | `runs/rec-rom/top_00133.png` + `bottom_00133.png` | (full frames — the whole video rect is the defect) |
| D15 | `runs/rec-tabs/bottom_00135.png` | `zooms/z-audio-vol.png` (3×) |
| D16 | `runs/rec-tabs/bottom_00210.png` | `zooms/z-touch-emptyband.png` (2×) |
| D17 | `runs/rec-rom/bottom_00215.png` | `zooms/z-smart-footer.png` (3×) |
| D18 | `runs/rec-tabs/bottom_00110.png` | `runs/zooms/z-rightedge.png` |
| D19 | `runs/rec-tabs/bottom_00185.png` | (readable at 1× in the full frame) |
| D20 | `runs/rec-picker/top_00050.png` | `zooms/z-cartchips.png` |
| D21 | `runs/rec-picker/bottom_00018.png` | `runs/zooms/z-splash-btn.png` |
| D22 | `runs/rec-pad2/top_00120.png` | `runs/zooms/z-hud-scrim.png` |

**Contact sheets** — `docs/phase16-emutest/sweep/evidence/`, each one captioned with the defect
boxed in red and (where useful) the design reference or a clean control shown in green:

| Sheet | Covers |
|---|---|
| `sheet-D1-segmented.html` | D1 — pause AUDIO + DISPLAY + the picker pill, with the design reference |
| `sheet-D2-picker-touch.html` | D2 — the before / after-first-tap / after-all-taps triple |
| `sheet-D3-D5-gamepad.html` | D3, D4, D5 — plus the Sharp control shot and the design reference |
| `sheet-D6-cross-pills.html` | D6 — chip row, in-game dot, colour swatches, design reference |
| `sheet-pause-tabs.html` | D8, D9, D15, D16, D18 |
| `sheet-D10-D12-theme.html` | D10, D12 — with the Indigo baseline and the *rejected* lobby claim |
| `sheet-misc.html` | D7, D11, D13, D14, D17, D20, D22 |

---

## 4. Checked and looked correct

Swept, captured, examined — nothing found. Evidence that the sweep was not selective.

- **Boot splash layout** — plate art, logo and button geometry all clean; the only note is the copy
  (D21). No clipping, no z-order fault.
- **Resume prompt (Indigo)** — A/B cards, game names, "Pick new games" / "Use defaults" crisp and
  correctly placed; its touch handling *works* (a tap at 160,110 advances to the picker), which is
  exactly what isolated D2 to the picker.
- **ROM list rows (top screen)** — 8 rows on a clean grid, no ragged columns; long titles
  (`zz-emutest-Some Homebrew Demo`) truncate at the plate edge rather than overflowing. Only the
  chip colour is wrong (D20).
- **Pause tab rail** — the six tab labels, the active-tab highlight and the left accent are
  consistent and unambiguous on every tab.
- **Pause SESSION tab (Indigo)** — "Change games" / "Resume" buttons and the game-pair row: correct.
- **Pause LINK tab (Indigo)** — the two toggles show a clear on/off state; "Wireless lobby…" is a
  legible gold primary button.
- **Pause ENHANCE tab** — rows on grid, toggles distinguishable; only D1 and D19 apply.
- **Toggle widgets generally** — on/off states are visually distinct everywhere they appear.
- **Wireless lobby, both screens** — the bottom screen renders the full design entry screen
  ("LOCAL · SAME ROOM (UDS)", gold "Host a session", "Scan for lobbies", the "OR · OVER THE
  INTERNET" divider, "Connect online (soon)", "‹ back to menu") and the top screen matches
  `08-wireless-lobby.png` (seat cards + RTT/LOSS).
- **Pause overlay top screen (Indigo)** — the game-name row is white and legible over the dimmed
  video; no z-order fault (see §5).
- **Intro / title screens with tilt = Max** — plain axis-aligned rectangles; no warp, no corner
  wedges (see §5).
- **In-game HUD elements other than the 3D badge** — game name, FOCUS chip, fps, clock and battery
  are high-contrast and correctly placed on both screens.
- **padColor** — changing the gamepad colour visibly re-tints the pad (gold → salmon); prefs do
  reach the renderer.
- **1:1 scale in-game composition** — video inset cleanly below the HUD bar, no overlap (the
  precondition that qualifies D22).

---

## 5. Rejected and misjudged claims

Six sweep claims did not survive verification, plus one that could not be reached. Recorded so
nobody chases them.

| Claim (source) | Verdict | Why |
|---|---|---|
| Pause TOP screen composites the live game **over** the pause panel, obscuring the game-name row (g4) | **Rejected — not reproduced** | Pausing over the same GAME FREAK screen: the game image is clearly *dimmed* (compare live `rec-rom/top_00165.png` with paused `top_00190.png`) and sits *behind* the "Pokemon Emerald ⇄ Pokemon FireRed" row, which is white and legible. There is no grey PAUSED panel at all for it to be drawn over. The real difference from `05-pause-menu.png` is the *missing* dimmed backdrop, not draw order. |
| Diorama tilt engages outside the overworld — intro/title screens perspective-warped (g4) | **Rejected — not reproduced** | Re-run at the original's exact `setbin.py tilt=3`: the HUD chip does read TILT3, but the Game Freak logo and the grass intro pan are plain axis-aligned rectangles — no trapezoid, no black corner wedges. Same at aspect-fit scale. **Honest caveat:** the verifier's sessions never reached the overworld, so this proves the intro screens are *not* warped; it does not prove the tilt works where it should. |
| Wireless lobby bottom screen is a dead "scanning…" end with no Host/Scan affordances (g4) | **Rejected — not reproduced** | Entered from the LINK tab at the same coordinate (201,91) in two independent runs: the full design entry screen renders and stays for ~45 s. The original run appears to have already been past the idle phase. |
| The gamepad EDGES option (Round/Soft/Sharp) makes no visible difference (g4) | **Rejected — inverted** | Isolating the variable (two runs identical except `padEdge`, same `padColor=0`, same frame index): with "Sharp" the four opaque corner blocks are **gone**; with "Round" they are present — `compare.py` reports 4.67 % of pixels changed. The original compared `padEdge=0/padColor=0` against `padEdge=2/padColor=3` and attributed the whole delta to colour. The setting works; "Round" is the broken corner path (now D5). |
| Daylight theme makes the **lobby** a half-dark/half-light screen pair (g4) | **Partly rejected** | The in-game half is real and is reported as D12. The lobby half is not: at the same theme both lobby screens are the dark Indigo plate — no white bottom screen, no split pair. |
| Boot splash label, severity "minor" (g1) | **Downgraded to polish (D21)** | The observation is right but there is no visible rendering fault — the pill is crisp with no overlapping second string. It is a copy difference from a mockup. |
| 1-Game session's bottom "CONTROLLER" screen is empty black (g2) | **Unverified — could not reach the screen** | Four attempts with `gameMode=1` and with `settings.bin` deleted: the picker fills the slot and lights START, but no d-pad sequence ever started the session, and the console START button starts a 2-game *defaults* session instead. **Not disproven** — the single highest-value thing to re-test, ideally on hardware, or after D2 is fixed. |

**Also new from the verification pass** (medium confidence — no single frame proves it, so it is not
in the confirmed table): **the ROM picker is close to unstartable in the harness.** Taps never
actuate any picker control (D2), and the d-pad + A path started a session in only one of six runs;
`rec-single4/bottom_00200.png` shows both slots filled and START lit gold after A,A,DOWN,A,A — and
no session for the remaining 30 s. The console START button does start a session but ignores the
picked games and the mode, launching the 2-game defaults pairing. On a real unit a user who taps
START would see nothing happen. This may be an emulator input-timing artefact; it deserves a
deliberate hardware check.

---

## 6. What the harness could NOT check

- **Anything needing a second console.** The wireless lobby's populated state, seat cards with a
  real peer, RTT/LOSS under real traffic, the link-cable trade UI mid-session, co-op presence.
  Every lobby observation here is the solo/idle state.
- **Real stereoscopic 3D.** Azahar presents a single view; parallax, the depth-slider response and
  the HD-2D tilt *as seen in 3D* are invisible to this harness. D22 and the tilt rejection both
  carry that caveat.
- **The overworld-gated features.** No verification session reached the overworld, so the tilt gate
  (does it engage where it *should*?), Smart-touch pathfinding overlays and the NPC/co-op presence
  layer were never seen in their intended context.
- **True colour / contrast on the physical panels.** D11 (3D badge) and D18 (scrollbar) are
  contrast judgements made from emulator framebuffers; the 3DS's actual gamma will shift them —
  most likely making D11 worse, not better.
- **Timing-dependent appearance.** Frame-budget artefacts, tearing, frameskip's visual cost and
  audio-sync feedback need the 804 MHz two-core budget on real hardware (CLAUDE.md "Done" gate).
  The `0fps`/`17fps` readouts in these captures are emulator artefacts, not defects.
- **The 1-Game session's bottom screen** (see §5) — blocked by the picker, not by the harness.
- **Themes beyond Indigo and Daylight**, and the Daylight *plates* — only the Indigo plate set is
  built, so D10/D12 describe the current state of a partially-built feature rather than a regression.

---

## 7. Reproducing any of this

```bash
cd /Users/guyshtainer/VSCodeProjects/3ds-toolkit/projects/3DGBA
# movies + helper scripts live in tools/emutest/runs/sweep-verify/
tools/emutest/run ctm  make tools/emutest/runs/sweep-verify/<movie>.json /tmp/m.ctm
tools/emutest/run azctl boot --gdb [--fresh-sd-fixtures] --movie /tmp/m.ctm
tools/emutest/run see  rec --seconds N --fps F --screen both --out rec-X &   # start BEFORE resume
tools/emutest/run gdbio resume
tools/emutest/run azctl stop                                                 # always
```

| Run | Config | Movie | Output | Covers |
|-----|--------|-------|--------|--------|
| V1 | fixtures + `mkroms.py` dummies, settings reset | `movie_picker_sweep.json` | `rec-picker/` | D1 (picker), D7, D20, D21 |
| V2 | same, taps only (no Y presses) | `m_tapprobe.json` | `rec-tap/` | D2 |
| V3 | `azctl clean-fixtures` (ROM-less) | `m1_tabtour.json` | `rec-tabs/` | D1, D6, D8, D9, D11, D13, D15, D16, D18, D19 |
| V4 | real fixture ROMs | `m_rom.json` | `rec-rom/` | D14, D17 + lobby / pause-top checks |
| V5 | `setbin.py sTop=1 sBot=1 touch=1 padColor=0 padEdge=0` | `m_resume.json` | `rec-pad2/` | D3, D4, D5, D22 |
| V6 | as V5 but `padEdge=2` | `m_resume.json` | `rec-edge/` | D5 control |
| V7 | `setbin.py tilt=3` | `m_resume.json` | `rec-tilt/` | tilt rejection |
| V8 | `setbin.py theme=2` | `m_theme.json` | `rec-theme/` | D10, D12 |

Group-agent originals: `docs/phase16-emutest/sweep/g1-boot-picker.md`, `g2-ingame-hud.md`,
`g3-pause-tabs.md`, `g4-features-modes.md`. Verifier pass: `verify.md`.
No azahar instance was left running; all captures used staged fixture copies, never the sdmc
originals.
