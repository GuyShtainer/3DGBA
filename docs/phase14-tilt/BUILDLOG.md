# Phase 14 (HD-2D tilt) — build log

Dated entry per slice (PHASE.md invariant 7). Newest first.

## 2026-08-04 — FINAL GATE: verification + docs (no source changes — nothing was broken)

Verification only. All six suites re-run from a clean shell using **each file's own header compile
line**, then `make clean && make -j8` and `make cia`.

| Suite | Compile line source | Checks | Result |
|---|---|---|---|
| `test_celiolink` | `test/test_celiolink.c:6` | **1259** | PASS, 0 failures |
| `test_netlink_reliability` | `test/host/test_netlink_reliability.c:5` | **66** | PASS, 0 failures |
| `test_diag` | `test/host/test_diag.c:9` | **369** | PASS, 0 failures |
| `test_control` | `test/host/test_control.c:12` | **6897** | PASS, 0 failures |
| `test_trace_replay` | `test/host/test_trace_replay.c:4` | **58** (+4 loud SKIPs) | PASS, 0 failures |
| `test_tilt` | `test/host/test_tilt.c:3` | **1694** | PASS, 0 failures |
| | | **10343 total** | **0 failures** |

**Build:** `make clean && make -j8` → `3DGBA.3dsx` **3,784,448 B**, exit 0. `make cia` →
`3DGBA.cia` **1,911,744 B**, exit 0 (makerom/bannertool auto-discovered from the toolkit
`tools/bin/`).

**Warning audit, mechanical.** 35 `warning:` lines total, by file:

| File | Warnings |
|---|---|
| `source/main.c` | 15 |
| `source/rompicker.c` | 7 |
| `source/touch.c` | 4 |
| `external/mgba/src/util/sha1.c` (vendored) | 3 |
| `source/wireless.c` | 1 |
| `source/ui.c` | 1 |
| `source/theme.c` | 1 |
| `source/netlink.c` | 1 |
| `source/celiolink.c` | 1 |
| **`source/tilt.c` / `source/tilt.h` / `source/tilt.v.pica` / `test/host/test_tilt.c`** | **0** |

**Every one of the 35 is pre-existing, proven the mechanical way rather than asserted:**
`git stash push` of the phase's five modified files (`main.c`, `theme.c`, `theme.h`,
`gamestate.c`, `gamestate.h`) → `make clean && make -j8` → the warning set is **identical
message-for-message and symbol-for-symbol**, differing only in line numbers (the phase's added
code shifts them, e.g. `theme.c` `lerp_rgb` 63 → 65, `main.c` `MENU_TAB_NAMES` 1459 → 1747) →
`git stash pop`. So the phase introduced **zero** warnings anywhere, not merely zero in its own
files. `test_celiolink`'s single host-compile warning (`cl_partner_party` unused) is likewise
pre-existing — `celiolink.c` has an empty diff for this phase (PHASE invariant 2).

**BUILDLOG coverage confirmed:** T1 (math + shader), T2 (the render pass), T3 (gate + tween +
touch), T4 (UI/persistence/tier/diagnostics), and the fix pass — five dated entries below.

**Docs updated:** `docs/HANDOFF.md` — a tilt-track paragraph in *Current status*, the six-suite
build/test block (SPEC-integration I7.7), the hardware checklist as **Next steps #4** (how to turn
it on, what to look for per rung, the stereo interaction, and an explicit FAILURE list), and a
dated newest-first session-log entry. `docs/kb/hd2d-octopath-3d.md` — a **SHIPPED** banner pointing
here, and a §3 correction note replacing its vertex-grid recommendation with round 2's simpler
finding (a ground-plane tilt is a planar homography, so any tessellation including 4 vertices is
exact, and the PICA200 reconstructs perspective-correct interpolation in fixed function once
clip-space `w` is real).

**Still hardware-unproven** — the phase's whole exit gate is PC-green + build-green (PHASE.md
invariant 8). Nothing in this entry says the effect works; it says the tree is clean.

## 2026-08-04 — FIX PASS: three adversarial reviews of the phase diff (vs `19375eb`)

Five findings (1 major, 4 minor). Each was re-verified against the code before acting; two were
fixed, one was fixed **differently from the way the reviewer proposed** (their fix is unsafe on
this hardware — argued below), and two are justified with the spec paragraph that already decided
them. Nothing in this pass changes the tilt-off path: the only executable edits are inside
`tilt_draw_image` (reached solely when `tilt_active()`), one gate input expression, and one
`TiltDraw` initializer field.

### Files changed

| File | What |
|---|---|
| `source/main.c` | `TiltDraw.clone` + `tilt_mesh_recolor()` + the build/clone/reuse branch in `tilt_draw_image`; the three `TiltDraw` initializers; **G9 now reads the live `touchMode`, not the frame-top `tmEff`**. |
| `source/tilt.c` | Comments only (no code): the G6 **KNOWN RESIDUAL** block (finding 4) and the "what *structural* does and does not cover" block under G9 (finding 5). |
| `test/host/test_tilt.c` | +TEST 18 (second-eye slab contract) and +TEST 19 (pause-menu → touch-on sequence). |
| `docs/phase14-tilt/SPEC-render.md` | +**R3.4.4 errata**: the mono pass-budget row undercounted the escapes by 2, and R3.1.2's prescale is now priced. |

Untouched, as PHASE invariant 2 requires: `celiolink.c`, `netlink.c`, `gbacore.c`, `touch.c`,
`wireless.c`, `warp.v.pica`, `tilt.v.pica`, `tilt.h`. `touch_to_gba` is still byte-identical.

### Per-finding outcome

**F1 — `sharpBilinear` forced at `SCALE_1X` (minor) → JUSTIFIED, priced, no code change.**
Confirmed real: `main.c:1575` is `!smooth && (mode != SCALE_1X || td) && preTgt`, so a 1:1 tilted
image does pay a `C2D_TargetClear` of the 512×512 VRAM target (`main.c:1949`) plus a 480×320 blit,
per image. But this is **SPEC-render R3.1.2 verbatim**, with its own stated reason (a tilt resamples
at non-integer rates everywhere, so `GPU_NEAREST` on the raw texture shimmers) and its own stated
price ("+1 pass in the 1:1-with-tilt case only"). It adds **no** render target and no VRAM, so
PHASE invariant 3's actual prohibition is untouched; every non-1:1 mode — including the shipped
default `SCALE_FIT` — already pays this pass today. The reviewer's real ask was that the cost be on
the sheet: done, in the new **R3.4.4**, with the A/B (`worstMs` at 1:1 vs Fit, tilt 20°) and the
cheap fallback (let 1:1 sample `e->tex` LINEAR) written down.

**F2 — the right eye rebuilt an identical vertex slab (minor) → FIXED.**
Confirmed: both eyes pass the *same* `TiltView*`, the same `scaleMode[0]` and the same 400×240
rect, so `calc_xform` and `tilt_project` could only produce the same 176 vertices; only the dim
tint could differ, and when `focScreen == 0` even that matched. `TiltDraw` gains `clone`:
`-1` = build, `>= 0` = "the geometry is already in that slab this frame". The right eye now clones
slab 0 and rewrites only the colour — and when the tint matches it points at **slab 0 itself**, so
the third game image costs **no vertex write and no `GSPGPU_FlushDataCache` IPC at all** (was: 176
float divides + a 6.3 KB write + a third GSP IPC every tilted frame). TEST 18 pins the substitution
bitwise, pins that two builds of the same view+mod are byte-identical (what licenses drawing one
slab twice), and pins that the premise is **not vacuous** (change the scale mode and the mesh really
does move, so cloning across modes would be a bug). SYNCDRAW is unaffected: a slab is still written
at most once per frame and never rewritten under a queued draw, which is the only thing R3.5.1
forbids. Bail-out safety: if the left eye returned early (`!e->core`), the right eye's `render_game`
takes the identical early return, so a stale slab can never be cloned.

**F3 — tilt-on costs 3 raw escapes, not the budgeted 1 (minor) → accounting FIXED; the proposed
code fix REJECTED with evidence.** The count is right: `gfxSet3D(true)` is session-wide
(`main.c:2049`) and the right-eye `render_game` (`main.c:3138`) has no slider guard, so three game
images are rendered every frame — that is also true today, it just costs citro2d draws instead of
escapes. **But `s3dOn` is the wrong key for skipping it.** `s3dOn = slider > 0.03 && !menuOpen &&
s3dEnabled` (`main.c:2955`) while the physical parallax barrier follows the **slider alone**, so
`s3dOn == false` with the barrier ON is reachable persistently (ENHANCE 3D master off, slider up)
and transiently (the 250 ms menu tween-down with the slider up). In both, a tilted left eye against
a flat right eye is genuine binocular rivalry on hardware — the exact failure `draw_pop_tex`'s clip
exists to prevent (`main.c:718-719`). Gating the whole right-eye *render* is worse: with tilt off
that changes the flat path, i.e. PHASE invariant 1. The only sound key is the raw slider, and "the
right framebuffer is not scanned out at slider 0" is inferred here, not documented — so it is
recorded as a **hardware-run lever**, not a PC-time change. What shipped instead is F2, which makes
the third escape carry no mesh work. Corrected table: **R3.4.4**.

**F4 — G6 is a negative test, so undetected full-screen menus tilt (MAJOR) → CONFIRMED as
behaviour, but it is a DISCLOSED design decision, not an oversight; no blind detector added.**
The mechanism is exactly as reported: `GCTX_OVERWORLD` is `game_read`'s fall-through
(`gamestate.c:163-164`), and `sb1Valid` / `px >= 0` / `!textDlg` cannot discriminate a pokédex, a
party summary, a mart list, a trainer card, the naming keyboard, the title screen, or the
cable-club trade menus under the in-process link. What the review did not cite is
**SPEC-integration I1.7**, which states this residual in terms, names those same screens, calls it
"a presentation-only, tweened, reversible wrongness on a screen we cannot currently name", gives
the promotion path, and instructs: *"Do not invent a second detector for phase 14."* The two fixes
the review proposes were both re-examined here and both rejected on evidence:
- *A positive `cb2 == CB2_Overworld` profile entry* is the right long-term answer, but **no pret
  symbol map is vendored anywhere in this tree** (checked) and a guessed address fails **silently
  in the worst direction**: `cb2` never matches ⇒ the whole feature is dead on hardware with no
  error message. The house rule is verify-on-hw, never ship a blind address — the same rule that
  keeps `mapObjects` for FR/LG flagged rather than "corrected".
- *The BG0 ≥55 %-coverage backstop* is I1.7's own optional, and it is **top-screen only**
  (`bg0_scan` never runs for the bottom game), so it would make the two screens behave differently,
  and it is a heuristic that can only be falsified on hardware.
Severity in practice is bounded by `g_prefs.tiltLevel` shipping at **0** (I5.8): no existing user's
frame changes until they opt in, and the wrongness tweens away the moment a *detected* context
appears. **Action taken:** the residual is now written at the gate itself (`tilt.c` G6 block) with
the named screens, both rejected fixes and the promotion path, and the hardware checklist below
carries the data-collection step that closes it for real. The phase-13 gs logger already records
the `cb2` of every screen visited with `resolved=0` marking exactly these fall-throughs, beside the
`d_tiltLvl` / `d_tiltAngT` columns slice T4 added — one hardware session with tilt on yields the
constants to promote.

**F5 — touch suppression is not structural across the tween (minor) → the amplifier FIXED, the
residual justified.** Both halves verified. (a) is a real one-frame bug: `tmEff` is snapshotted at
`main.c:2100` but `ACT_PREVIEW_PAD`/`ACT_PREVIEW_SMART` set `touchMode` **and** clear `menuOpen`
later in the same frame (`main.c:2668-2670`), so on that frame the gate saw `menuOpen == 0` with a
stale `touchActive == 0` — G3 released, G9 not yet armed — the target jumped back to the user level
and `tilt_tween_step` **retargeted upward**, restarting the 250 ms descent clock with smart touch
already live. Fixed by reading the live `touchMode` (`tmEff != TOUCH_OFF` ⟺ `touchMode !=
TOUCH_OFF` — the `single`-mode SMART→PAD substitution can neither produce nor consume `TOUCH_OFF`,
so the meaning is unchanged and only the staleness is gone). TEST 19 replays the whole sequence in
both models and demands the live feed never re-open the gate and never let the angle rise.
(b), the tween residual itself, is **not** fixed, deliberately: the reviewer's "0.3-0.5 s" assumed
the descent restarts, which was true only because of (a); the honest bound is *(250 ms − however
long the pause menu was open)*, because touch can only change from inside that menu and **G3 has
already been pulling the bottom screen down since it opened**. Removing the rest would mean either
snapping (forbidden by PHASE invariant 5) or suppressing the first taps (a change to a
hardware-validated input path, to buy a sub-quarter-second cosmetic). The sanctioned lever, if
hardware ever shows a real mis-tap, is **SPEC-render R6.3's asymmetric ease** (slow in / ~3-frame
out) which SPEC-integration I2.4's symmetric 250 ms overrode — it would cut the window to ~50 ms
without touching the gate or the touch path. The over-claim in the G9 comment ("bit-identical BY
CONSTRUCTION") is now qualified in place: `touch_to_gba` *is* byte-identical and the gate can never
hand a touched screen a nonzero **target**; the **angle** still arrives there through the tween.

### Counts

| Suite | Before | After |
|---|---|---|
| `test_celiolink` | 1259 | **1259** |
| `test_netlink_reliability` | 66 | **66** |
| `test_diag` | 369 | **369** |
| `test_control` | 6897 | **6897** |
| `test_trace_replay` | 58 (+4 skips) | **58 (+4 skips)** |
| `test_tilt` | 1579 | **1694** (+115: TEST 18/19) |

All PASS. `make -j8` → `3DGBA.3dsx`, zero warnings in `tilt.c` / `tilt.h` / `tilt.v.pica` /
`test_tilt.c`; `main.c`'s warnings are the pre-existing UI-redesign leftovers (unused
`menu_layout`/`clrTxt`/`MENU_TAB_NAMES`…), none on a line this pass touched.

### Added to the hardware checklist

1. **F4 data collection (the one that buys a real gate):** with tilt at Med, visit pokédex, party
   SUMMARY, trainer card, town map, a mart/PC list and the cable-club trade menu; then read the
   `cb2` column of `sdmc:/cias/netlogs/3DGBA_gs_*.txt` for the rows with `resolved=0` and a nonzero
   `d_tiltAngT`. Those constants are the deny-list (or the `CB2_Overworld` positive) that closes
   the residual.
2. **F1/F3 budget A/B:** `worstMs` with two games at tilt 20° — `SCALE_1X` vs `SCALE_FIT`, and 3D
   slider at 0 vs up. The first prices R3.1.2's prescale; the second prices the third escape and
   decides whether the slider-keyed right-eye skip (R3.4.4 note 1) is worth proving.
3. **F5:** open the pause menu on the TOUCH tab and tap Smart within ~2 frames, then immediately
   tap a bag/menu row; a mis-tap there would be the residual tween, and the fix is R6.3's
   asymmetric ease.

## 2026-08-03 — SLICE T4: UI, persistence, tiering, diagnostics

**Scope shipped:** SPEC-integration **I4 (settings + UI)**, **I5 (performance tier)** and
**I6 (HUD + diagnostics)**. The effect now has a **real control**: an ENHANCE-tab 4-way `PK_SEG`
row, persisted in `g_prefs.tiltLevel` via an appended `Settings.tilt` word, readable from both the
pause menu and the standalone pre-game settings screen. Slice T2's compile-time `TILT_DEV_LEVEL`
is **gone** — `gi.userLevel` reads `g_prefs.tiltLevel`, exactly as T2's decision D8 and T3's D19
promised. **It ships at level 0** (I5.8), so the default binary is still inert and the render path
with tilt off is untouched.

### Files changed

| File | What |
|---|---|
| `source/theme.h` / `source/theme.c` | `UiPrefs.tiltLevel` (+ the "clamp live, never rewrite" note) and the `g_prefs` initializer's trailing `0`. |
| `source/main.c` | `Settings.tilt` + 3 `_Static_assert`s + the `lenPad` length rung; `ACT_TILT`; the `TILT_NAMES` table; the `PT_ENHANCE` row + `PTABN[3] 5→6` + the `menu_layout` push; the four `PK_SEG` switch cases + two label tables; the status line; the pill arrays `[7]→[8]` + the Tilt pill; the HUD TILT chip; `s_isN3DS`/`s_speedupActive` + the Old-3DS notice in `run_settings`; the `GsDepth` tilt fill; `gs_log_set_env`. |
| `source/gamestate.h` / `source/gamestate.c` | `GsDepth`/`GsLogEntry` gain `tiltLvl`/`tiltAngTop`/`tiltAngBot`; the dump header gains the three columns **and the empty-field count goes 15 → 18**; `gs_log_set_env` stamps model + 804 MHz probe into the header. |
| `test/host/test_tilt.c` | +TEST 6 (settings round-trip + tier/default logic). |

Untouched, as invariant 2 requires: `celiolink.c`, `netlink.c`, `gbacore.c`, **`touch.c`**,
`wireless.c`, `warp.v.pica`, `tilt.v.pica`, **`tilt.c`/`tilt.h`** (the gate predicate needed no
change — I5's rules were already G2/G4/G10/G11). `touch_to_gba` is still byte-identical.

### Counts (all six suites)

| Suite | Before | After |
|---|---|---|
| **tilt** | 1510 | **1579 checks, 0 failures** (+69, TEST 6) |
| celiolink | 1259 | 1259, 0 failures — PASS |
| netlink reliability | 66 | 66, 0 failures — PASS |
| diag | 369 | 369, 0 failures — PASS |
| control | 6897 | 6897, 0 failures — PASS |
| trace replay | 58 (+4 skips) | 58, 0 failures, 4 skips — PASS |

`make -j8` → **`3DGBA.3dsx`** (3,784,260 B, +1,748 B over slice T3), exit 0.

**Warnings: the pre-existing set, unchanged.** `main.c` emits the same **15** phase-13 legacy
warnings by message (`menu_layout`/`menu_w_h`/`menu_w_sel`/`splash_panel`/`ease_out`/
`MENU_TAB_NAMES`/`PAD_EDGE_NAMES`/`clrTxt`/`clrPanel`/`clrSelTxt`/`menuScroll`/4×
misleading-indentation). `gamestate.c` emits **zero**. `theme.c`'s single
`-Wmisleading-indentation` in `lerp_rgb` was proven pre-existing the mechanical way
(`git stash push source/theme.c` → rebuild → same message at the un-shifted line 63 → `git stash
pop`); the two comment lines this slice adds move it to 65. One warning **was** introduced and
then removed rather than tolerated: `snprintf(tc, sizeof tc, "TILT%d", …)` for the HUD chip tripped
`-Wformat-truncation` (the compiler cannot see the loader's modulo), so the chip label is a
4-entry `static const char*` table instead — cheaper too, since it runs every HUD frame.

### HOW INVARIANT 1 WAS VERIFIED (read the diff)

The **render path is not touched at all by this slice** — no edit lands between `C3D_FrameBegin`
and the game blits. The one line that reaches the renderer is the level source:

| Edit | Neutral because |
|---|---|
| `gi.userLevel = tiltOk ? TILT_DEV_LEVEL : 0` → `… g_prefs.tiltLevel : 0` | `g_prefs` ships `tiltLevel = 0` (`theme.c`) and a pre-tilt settings file cannot raise it (the length rung leaves it at the default — TEST 6b). ⇒ **G1** ⇒ target 0 on both screens ⇒ `tilt_tween_step(t,0,0.0f,dt)` takes no retarget, saturates `tMs` and **assigns** `ang = angTo` ⇒ `ang` stays *bitwise* `0.0f` ⇒ `tilt_active()` = 0 ⇒ all three `render_game` calls get `NULL` ⇒ today's flat blit, character for character. TEST 6e asserts the whole chain, including that a second of stepping leaves `ang == 0.0f` under `==`. |
| HUD `rx -= cw + 6.0f;` added inside the `if (s3dEnabled)` chip block | `rx` is a function-local that is **never read after** that block; the tilt chip that would consume it is behind `if (g_prefs.tiltLevel > 0)`, false by default. Zero pixels move. |
| the `GsDepth` tilt fill + the 3 log columns | logging only, in the parked window; nothing reads them back. |
| `Settings` grows one word | old files still load (the `lenPad` rung) and a **longer** file is rejected — the pre-existing, disclosed one-way property (I4.11). |

What *does* change with tilt off, deliberately and per spec, is **chrome**: the ENHANCE tab has a
6th row (I4.2) and the paused-summary pill row has an 8th, greyed, "Tilt" pill (I4.15). Both are
menu surfaces, not the game image.

### Decisions taken (this slice's open questions, resolved)

**D22 — SPEC-integration Q1 RESOLVED: the segment labels are `Off / Low / Mid / Max`, not
`Off / Soft / Med / Deep`.** Q1 anticipated exactly this: with SPEC-render R2.5.5's ladder landing
at OFF/10/15/20°, "Deep" over-promises a deliberately mild effect — gen1's own "deep" rung is 50°,
which R2.5.5 rejects outright for us. Low/Mid/Max describes a position on **our** ladder without
claiming an absolute. One `TILT_NAMES[TILT_LEVELS]` table feeds all three consumers (pause seg,
standalone-settings seg, status line) so they cannot drift.

**D23 — Q3 (the default flip) is NOT taken now; the default stays 0 (Off).** I5.8's flip condition
is explicitly *post-hardware* ("after one hardware run confirms (a) no fps regression on the
non-tilt path and (b) ≥55 fps sustained at level 3 with two cores"). This slice is PC-green only,
so flipping would be shipping an unmeasured frame-budget change to every existing user. The
condition is recorded in the hardware checklist item (h) below so it actually happens.

**D24 — the HUD chip label is a table, and it encodes ENGAGEMENT, not just the setting.** `TILT1/2/3`
in `g_ui.acc` when `tilt_active(&tiltTw[0])`, in `THEME_ON_DARK_DIM` otherwise. That difference is
the point: a photograph of a tilted screen carrying a **dim** chip is self-contradictory and
localises a gate bug in one glance, which is the only way §7.2's "the gate against real screens"
item can be worked. Top screen only (I6.3).

**D25 — I5.3's "stamp the speedup fact" ships as a gs-log HEADER line, not a clamp and not a new
file.** `gs_log_set_env(isN3DS, speedupActive)` (called once from `main()`) prints
`# env: model=… speedup804=…` with an explicit "a low fps here is NOT a tilt cost" note when the
804 MHz/L2 claim did not engage. Tilt is deliberately **not** clamped on `speedupActive`: a `.3dsx`
from the Homebrew Launcher can never claim it, so clamping would make the effect un-iterable on
the entire `make`-and-`3dslink` dev loop. The env stamp is deliberately **not** reset by
`gs_log_reset` — the model is a property of the boot, not of a play session.

**D26 — the D3 per-frame CSV gets NO tilt column** (I6.7 asks for this to be said here once, so:
**why is tilt missing from the CSV?**). Three reasons: the CSV is armed only under `wlOn` with a
participant core, and rule G4 forces tilt **off** under any link — the column would be a constant
0; its 62-column header is pinned by golden bytes *and* a header/row field-count parity assert
(`test_diag.c` TEST 6), so a decorative column costs a golden rewrite for zero information; and
the gs log already carries the level and both angles on every top row.

**D27 — the gs log's tilt values are the PREVIOUS frame's, and that is the right choice.** The gs
sample runs in the parked window; `tilt_tween_step` runs later, in the render phase. Rather than
move the tween (which would put a gate rule outside `tilt_target_level`, forbidden by I1.11) or
duplicate it, the row logs `tiltTw[*].level/.ang` as they stand — i.e. the state of the frame the
player has just seen, which is exactly what a photograph shows. Stated in the header comment so
nobody reads a one-frame lag as a bug.

**D28 — `I6.5`'s 15 → 18 empty-field bump was done in the same edit as the columns**, and the
comment's arithmetic was rewritten (`7 d_* + 8 detail + 3 tilt = 18`). This is the single most
dangerous line in the slice: an off-by-one there shifts every later column on **bottom-screen**
rows and silently corrupts the link diagnostics, which is the one thing that log exists to make
readable. Verified by counting the commas in the emitted literal (18) and by re-deriving the
header/row field counts by hand: 26 leading + 18 depth + 5 link = **49** on both branches.

**D29 — `run_settings` did not grow a parameter (I4.13).** The model landed in file-scope
`s_isN3DS`, matching the existing `s_hasPtm` pattern, and it earns its keep: when the saved level
is > 0 on an Old 3DS the settings screen says so in orange
(*"Tilt is set, but this is an Old 3DS - it stays flat."*). Without that, a user who sets Max on an
O3DS sees a control that appears to do nothing. The pause-menu equivalent is I4.14's status line
(`"Tilt: Max — New 3DS only"` vs `"Tilt: Max (overworld only)"`).

**D30 — the `Settings` mirror in the test is pinned by `_Static_assert`s in main.c, not by hope.**
I7.6 allows replicating the struct "with a comment pinning it to main.c"; a comment cannot fail.
`main.c` now asserts `sizeof(Settings) == 24*sizeof(s32)`, `offsetof(tilt) == 23*sizeof(s32)` and
`offsetof(padEdge) == 22*sizeof(s32)`, and `test_tilt.c`'s `RefSettings` asserts the same three
numbers. Inserting a field anywhere above `tilt` is now a **compile error in main.c**, not a test
that quietly validates a fiction.

### What is now proven, and what is not

Proven on the PC (TEST 6, +69 checks): the length ladder is strictly increasing and the pre-tilt
rung is exactly one `s32` short of the new struct (so **no magic bump** is needed and every
existing settings file still loads, keeping its chrome prefs and taking the tilt default);
off-ladder, longer and foreign-magic files are rejected; `(unsigned)v % TILT_LEVELS` maps every
`s32` including `INT32_MIN` into a legal ladder index; all four levels round-trip file → gate →
angle; the shipped default is inert (`ang` bitwise `0.0f` after a second of stepping,
`tilt_active() == 0`); the **Old-3DS tier clamp over 576 rows** (4 levels × 2 screens × 9 contexts
× 8 flag combinations) is 0 everywhere; and I5.1's *clamp live, never rewrite* as two machine-
checked properties over 128 rows — the answer is never **greater** than the saved level, and the
input struct is **byte-identical** after the call (`memcmp`), so no tier rule can ever write the
preference back.

**Unproven and unprovable here (verify-on-hw):** everything in SPEC-integration §7.2 and the
phase's O1–O8, plus this slice's own additions: that the 6th ENHANCE row lands in the plate's
empty band without colliding with baked art (I4.2's coordinates are read off the manifest, not
measured on a panel — **this is the most likely cosmetic surprise in the slice**); that four
segment labels are legible at ~42 px each in the 8–9 px JBM the `assets_seg` renderer uses; that
the 8-pill paused summary still centres inside the plate's 288 px pill region when *Vivid* is also
on; and that the TILT chip does not collide with the fps/clock/battery cluster at the right of the
top HUD bar.

### Hardware checklist — paste into `docs/HANDOFF.md` Next steps

SPEC-integration §7.3's list is now **actionable for the first time** (until this slice there was
no way to raise the level without a custom build). Additions from T4:

```
(l) SETTINGS ROUND-TRIP: pause -> ENHANCE -> TILT = Max. Quit to the game picker, ZR -> Settings
    -> Enhance: the TILT row must read Max. Power off, boot again, re-open: still Max.
(m) OLD-FILE COMPAT: keep an sdmc:/3DGBA/settings.bin written by the PREVIOUS build. Boot this
    build: every other preference (theme, pad colour, scale, HUD, touch) must survive, and TILT
    must read Off. (I4.10 — no magic bump.)
(n) THE 6TH ROW: photograph the ENHANCE tab. The TILT segment must sit clear of the five baked
    toggle labels above it and of the status hint at y=231, with the "TILT" overlay label clear of
    the tab rail. If it collides, the fix is new plate art (open question O6), not new coordinates.
(o) PILL ROW: pause with Vivid ON and a link up -> 8 pills. They must stay inside the plate's pill
    strip and stay centred.
(p) HUD CHIP: with TILT=Max in the overworld the chip reads TILT3 in the accent colour; open a
    battle and it must go DIM (setting on, gate shut) without moving the fps/clock/battery cluster.
(q) OLD 3DS: the row is adjustable, the settings screen shows the orange "Old 3DS - it stays flat"
    line, and both screens stay flat.
(r) THE gs LOG: after the run, sdmc:/cias/netlogs/3DGBA_gs_*.txt must carry the "# env:" line and
    the d_tiltLvl / d_tiltAngT / d_tiltAngB columns; bottom-screen rows must still parse (18 empty
    d_* fields — I6.5) with the lerr/lstat columns landing in the right place.
(h') DEFAULT FLIP (I5.8/D23): with TILT=Max, 3D slider 0, two games, no frameskip — if fps holds
    >=55 over 60 s AND the tilt-off path shows no regression, change the default to 2 (Mid) and
    log it here. Otherwise the default STAYS 0.
```

## 2026-08-03 — SLICE T3: the gate, the tween wiring, and the touch invariant

**Scope shipped:** SPEC-integration **I1 (the gate)**, **I2 (the tween + `tilt_active()` contract)**
and **I3 (touch)**. `tilt_target_level()` — the full G1-G11 predicate — now exists in the pure-C
module and is the *only* place any gate rule lives; `main.c` snapshots its game-RAM inputs in the
existing parked window and calls it once per **screen** per frame, then steps both tweens from the
render loop's wall-clock delta. The temporary slice-T2 gate expression is gone. **No UI still**:
the level is the compile-time `TILT_DEV_LEVEL`, shipping at 0 (I4/§4 is the settings slice), so G1
fires on every frame of the built `.3dsx` and the binary behaves exactly as it did before.

### Files changed

| File | What |
|---|---|
| `source/tilt.h` | `TiltGateIn` + `tilt_target_level()` under the reserved gate banner (the T1 placeholder note is gone). Still header-free pure C. |
| `source/tilt.c` | `tilt_target_level()` — the G1-G11 ladder with the argument for each rule, including the three reasons SPEC-integration §3.2 rejected the touch inverse. |
| `source/main.c` | **net +75 lines over slice T2** (whole-phase `git diff --numstat` vs the 19375eb baseline: **+353 / −12**, of which T2 accounted for +278 / −12 — i.e. this slice deleted no pre-phase-14 line and replaced only T2's own temporary gate expression). `TiltSnap` + the `_Static_assert(GCTX_OVERWORLD == TILT_CTX_FIELD)`, `TiltSnap tiltSnap[2]`, the parked-window fill, the per-screen gate + tween block, and the bottom screen's tilt draw (slab 2). |
| `test/host/test_tilt.c` | +TEST 1 / 2 / 3 (gate truth table, per-screen independence, the touch invariant). |

Untouched, as invariant 2 requires: `celiolink.c`, `netlink.c`, `gbacore.c`, **`touch.c`**,
`gamestate.c`, `wireless.c`, `warp.v.pica`, `tilt.v.pica`. `touch_to_gba` is byte-identical.

### Counts (all six suites)

| Suite | Before | After |
|---|---|---|
| **tilt** | 1400 | **1510 checks, 0 failures** (+110, TEST 1-3) |
| celiolink | 1259 | 1259, 0 failures — PASS |
| netlink reliability | 66 | 66, 0 failures — PASS |
| diag | 369 | 369, 0 failures — PASS |
| control | 6897 | 6897, 0 failures — PASS |
| trace replay | 58 (+4 skips) | 58, 0 failures, 4 skips — PASS |

`make -j8` → **`3DGBA.3dsx`** (3,782,512 B, +1,524 B over slice T2), exit 0.

**Warnings: byte-for-byte the pre-existing set**, verified the same mechanical way T2 used —
`git stash push source/main.c` → build → collect the `main.c` warning *messages* (line numbers
shift, so the text is compared, not the location) → `git stash pop` → build → `diff`: **identical,
15 warnings, all the phase-13 legacy set**. Repeated with `TILT_DEV_LEVEL` forced to **2**, so the
*enabled* path is not merely dead-code-eliminated warning-free — also identical, and it links.
`tilt.c` emits zero warnings under both `clang -Wall -Wextra` and devkitARM gcc.

### HOW INVARIANT 1 WAS VERIFIED (read the diff)

Five edits, each neutral with the tilt off — argued from the code, not from a hope:

| Edit | Neutral because |
|---|---|
| `TiltSnap` typedef + `_Static_assert` | types only; the assert is compile-time |
| `TiltSnap tiltSnap[2]` + the parked-window fill | 2 struct copies off `gst`/`gsb`, which `game_read` had **already** filled two lines above (I1.1). No new `game_read`, no `profile_for`, no `gbacore_read*`, no new race class — and it touches nothing the renderer reads |
| the gate + tween block | `TILT_DEV_LEVEL == 0` ⇒ `gi.userLevel == 0` ⇒ **G1** ⇒ target 0 on both screens ⇒ `tilt_tween_step(t, 0, 0.0f, dt)` takes no retarget (`0.0f != 0.0f` is false), leaves `tMs` saturated and **assigns** `ang = angTo`, so `ang` stays *bitwise* `0.0f` ⇒ `tilt_active()` = `0 > 0 \|\| 0.0f > 0.001f` = 0 on every frame |
| `tiltTop`/`tiltBot` ⇒ the three `render_game` calls | all three receive `NULL`, i.e. today's flat branch character for character |
| the bottom screen's `botMod` + `TiltDraw` | one `C2D_Color32` and a 3-field struct init; the `tiltBot ? &tiltBL : NULL` argument evaluates to the literal `NULL` the call had before |

The **stronger** statement is that this is not merely "off because the constant is 0": the gate is a
pure function that the host suite enumerates over **147,456 rows** (every boolean input × all 9
`GameCtx` values × both screens × all 4 levels) against an independently-written reference, so what
the shipped binary does when the level *is* raised is machine-checked too.

### Decisions taken (this slice's open questions, resolved)

**D16 — the two specs disagree about the bottom screen; SPEC-integration's rule ships, so the
bottom screen CAN tilt.** `SPEC-render` R4.8.1 says "tilt ships top-screen only by default";
`SPEC-integration` §3.2 explicitly considers and **rejects** that as option (a) — *"(a) throws away
the effect on half the device for a conflict that only exists in one mode"* — and chooses **(b)
suppression**: rule G9 zeroes the bottom screen whenever `tmEff != TOUCH_OFF`, and only then.
SPEC-integration §0.1 owns gate policy, so it wins. Consequences, all deliberate:
- `touch_to_gba` is still **bit-identical** and PHASE invariant 4 still holds *by construction*:
  it is consulted only under `TOUCH_SMART`, and under any touch mode the bottom target is 0. TEST 3
  proves the implication over 13,824 rows rather than asserting it in a comment.
- `TOUCH_PAD` is suppressed too (I3.6) even though it never calls `touch_to_gba` — its overlay is
  drawn in flat screen space and a tilted game under a flat D-pad reads as a bug.
- Slice T2's **D14 ("`tiltTw[1]` is stepped but pinned to level 0") is hereby superseded**; T2
  predicted this would be "a gate change, not a re-derivation", and it was: the bottom draw is one
  `tiltBot ? &tiltBL : NULL` argument plus its own `TiltView` and slab 2 (which R3.5.1 had already
  reserved).
- In the default configuration (`touchMode = TOUCH_OFF`, `main.c:1893`) the player therefore gets
  the dual-screen diorama, which is what §3.2 bought by choosing (b).

**D17 — `TiltSnap` stores the RAW `ctx`, not I1.2's sketched `fieldCtx` flag.** I1.2's struct
sketch names the field `fieldCtx` (a pre-digested "is this the overworld" bool), but I1.10's gate
input is `int ctx` and G6 is `ctx != TILT_CTX_FIELD`. Digesting it in `main.c` would move a gate
rule *out* of `tilt_target_level`, which I1.11 forbids ("the only place any of these rules
exists"), and would make the truth table's 9-context sweep untestable. The raw value is safe
because I1.4's `_Static_assert` pins the enum. Recorded as a deviation from the sketch, not from
the requirement.

**D18 — `tiltOk` (shader + VRAM arena present) is folded into `gi.userLevel`, not bolted on as a
twelfth rule or a second check at the draw site.** Presenting the saved level as 0 when the shader
is missing makes the tween park at bitwise 0.0f, so `tilt_active()` — the single check invariant 1
hangs on — is already false and nothing downstream needs to know. It also keeps `tilt.c` free of a
GPU-shaped concept, which R6.1 requires.

**D19 — the level source stays `TILT_DEV_LEVEL` for one more slice.** §8's implementation order
puts the gate (step 2) before the settings row (step 3) precisely so the risky diff ships inert.
`gi.userLevel` is the one line the settings slice replaces with `g_prefs.tiltLevel`.

**D20 — I1.7's optional BG0-coverage backstop is NOT shipped**, per its own "which is why it is not
in slice 1". `bg0_scan` only runs for the *top* game (`main.c:2217`), so the backstop would make the
two screens behave differently — and D16 just made the bottom screen a first-class tilt target,
which sharpens that asymmetry rather than softening it. The residual is stated honestly: an
*undetected* full-screen menu still reads as `ctx=GCTX_OVERWORLD, sb1Valid=1, px>=0, textDlg=0` and
**will tilt**. The promotion path is the phase-13 `cb2` column, exactly as I1.7 says.

**D21 — the exhaustive sweeps are asserted as AGGREGATES.** TEST 1(f) walks 147,456 gate rows and
TEST 3 walks 13,824, but each contributes ~3 checks (row count, mismatch count, non-vacuity)
instead of one per row. Total suite growth is +110 checks for total coverage of the predicate;
inflating the count to 160k would make the suite's number meaningless as a signal.

**Answers banked for the specs' own open questions:** **Q4** (does `single` mode need an extra
rule for the empty bottom screen?) — **no**: `game_read` returns false with no core, so
`tiltSnap[1].ok == 0` and G5 gates it off for free. Asserted in TEST 2. **Q1** (segment labels) and
**Q3** (the default-flip) belong to the settings slice and are untouched here.

**Housekeeping note for the next author:** every `main.c:NNNN` cite written *in this slice* was
re-verified against the post-slice file. Cites inside T1/T2's own comment blocks (e.g. `tilt.h`'s
"osGetTime, main.c:1777", `tilt.c`'s "main.c:1764") drifted when T2 inserted +278 lines and drifted
again here; they were deliberately **not** rewritten, to keep this slice's diff to the gate. If a
later slice is already touching those files, refresh them then.

### What is now proven, and what is not

Proven on the PC: the gate predicate over its **entire** input space against an independent
reference; every G1-G11 rule in isolation; two-rule precedence (110 combinations — no rule can
unblock another); `ok == 0` ⇒ off for all 9 contexts (the unmapped-game case); `sb1Valid`/`px`
required even in the field; per-screen independence including "a battle on one screen leaves the
other alone"; the touch implication over 13,824 rows plus the mirror property that `touchActive`
never changes the top screen's answer; and the *composition* `tilt_target_level → tilt_tween_step →
tilt_active` as `main.c` wires it — an open gate rises monotonically to the ladder angle, a closing
gate descends and lands on **exactly `0.0f`**, and only there does `tilt_active()` report the flat
path.

**Unproven and unprovable here (verify-on-hw):** everything in SPEC-integration §7.2 and the phase's
O1-O8. The items this slice specifically adds to the hardware checklist (§7.3 (c), (d), (f), (i),
(j)) are: the menu / textbox / battle gate transitions really tween rather than snap; the bottom
screen goes flat *and stays flat* the moment Touch is set to Gamepad or Smart, with smart touch
behaving exactly as before; only the focused screen tilts under Skip; and the row is present but
inert on an Old 3DS. Paste §7.3 into `docs/HANDOFF.md` with the settings slice — until the ENHANCE
row exists there is no way for a tester to raise the level without a custom build.

## 2026-08-03 — SLICE T2: the render pass (the tilted draw, wired behind `tilt_active()`)

**Scope shipped:** SPEC-render R3 + R4 step 5 — the raw-C3D block, the source-texture choice, the
filtering, the state save/restore, R2's coverage decision, and the composition rules for every
existing pass. `render_game`'s final blit is now *replaceable* by a perspective mesh. **No UI**:
the level comes from a compile-time constant that ships at **0**, so the built `.3dsx` behaves
exactly as it did before this slice.

### Files changed

| File | What |
|---|---|
| `source/main.c` | +278 / −12. The tilt render module (`TiltVert`/`TiltDraw`, `tilt_init`/`tilt_fini`, `tilt_mesh_base`, `tilt_draw_image`), one new `render_game` parameter, the temporary gate + tween wiring, the R4.0.1 exclusions, `tilt_init()`/`tilt_fini()` in `main()`. |
| `test/host/test_tilt.c` | +TEST 17 — the *emitted vertex*, which is the only host-checkable half of a GPU slice. |

Untouched, as invariant 2 requires: `celiolink.c`, `netlink.c`, `gbacore.c`, `touch.c`,
`gamestate.c`, `wireless.c`, `warp.v.pica`, `tilt.c`, `tilt.h`.

### Counts (all six suites)

| Suite | Before | After |
|---|---|---|
| **tilt** | 1243 | **1400 checks, 0 failures** (+157, TEST 17) |
| celiolink | 1259 | 1259, 0 failures — PASS |
| netlink reliability | 66 | 66, 0 failures — PASS |
| diag | 369 | 369, 0 failures — PASS |
| control | 6897 | 6897, 0 failures — PASS |
| trace replay | 58 (+4 skips) | 58, 0 failures, 4 skips — PASS |

`make -j8` → **`3DGBA.3dsx`** (3,780,988 B, +2,804 B over slice T1), exit 0.

**Warnings: byte-for-byte the pre-existing set.** Verified mechanically, not by eye: the 15
`source/main.c` warnings were captured from a build of the *pristine* `main.c`
(`git checkout -- source/main.c`) into `/tmp/warn_base.txt`, then compared with `diff` against a
build of the edited file — identical, and identical **again** with `TILT_DEV_LEVEL` forced to 2 (so
the enabled path is not merely dead-code-eliminated warning-free). All 15 are the phase-13 legacy
set (`menu_layout`/`menu_w_h`/`menu_w_sel`/`splash_panel`/`ease_out`/`MENU_TAB_NAMES`/
`PAD_EDGE_NAMES`/`clrTxt`/`clrPanel`/`clrSelTxt`/`menuScroll`/4× misleading-indentation).

### HOW INVARIANT 1 WAS VERIFIED (read the diff, do not hope)

`git diff` deletes exactly **12 lines**. Every one is a *pure conjunction or parameter addition*
that is provably the original expression when the tilt is off:

| Deleted line | Became | Neutral because |
|---|---|---|
| `render_game(...)` signature | `+ const TiltDraw* td` | the three call sites pass `NULL` / `tiltTop ? &td : NULL` |
| `sharpBilinear = !smooth && mode != SCALE_1X && preTgt` | `!smooth && (mode != SCALE_1X \|\| td) && preTgt` | `td == NULL` ⇒ `(X \|\| 0) == X` |
| `uipop = …` | `… && !tiltTop` | `tiltTop == false` ⇒ `X && 1` |
| `dofPass = …`, `bloomPass = …`, `litPass = …` | `… && !tiltTop` | same |
| `if (pop3d)` ×2 | `if (popPass)`, `popPass = pop3d && !tiltTop` | same |

and the **two inserted lines** in `render_game`'s two branches are both `if (td) { …; return; }`
guards placed *after* the code they gate on, so with `td == NULL` control flow is unchanged.

The remaining question is therefore only "is `tiltTop` false in the shipped build?", and it is,
for a reason stronger than the constant: `tilt_tween_reset` parks the tween at `ang == 0.0f,
angTo == 0.0f, level == 0`; `tilt_tween_step(t, 0, 0.0f, dt)` takes no retarget (`0.0f != 0.0f` is
false), leaves `tMs` saturated, and **assigns** `ang = angTo` (T1 decision D6/I2.7), so `ang` stays
*bitwise* `0.0f`; `tilt_active` is then `0 > 0 || 0.0f > 0.001f` = 0 on every frame of every
session. No GPU state is touched, no extra pass is emitted, no `TiltView` is even built.

The **geometric** half of the invariant is now machine-checked too (TEST 17a): at angle 0 the
mesh's 176 vertices land on `ox + gx*sx, oy + gy*sy` — the *same expression* `render_game`'s flat
blit uses (`main.c:1306-1310` vs `calc_xform` at `main.c:709-715`) — **bitwise**, for all three
scale modes on both screens, with `iq == 1.0f` and the flat path's exact UVs.

### Decisions taken (this slice's open questions, resolved)

**D8 — the level source: a compile-time `TILT_DEV_LEVEL`, defaulting to 0.** The task allowed a
constant or a debug hook; a constant wins because it makes the neutrality argument above a
*compile-time* fact as well as a runtime one, and because every existing debug hook in this file
(KEY_Y, the `sdmc:/cias/control` opt-in) is already load-bearing for the link work that invariant 2
forbids touching. `#ifndef`-guarded, so `-DTILT_DEV_LEVEL=2` overrides without editing the file.
Both builds (0 and 2) were made and compared. It disappears when the integration slice lands
`g_prefs.tiltLevel` + the ENHANCE `PK_SEG` row.

**D9 — the coverage decision (R2): bottom-anchored fit + background-filled wedges + SPILL (no
scissor).** The cover/anchor half is already in `tilt_view_init` (`k = kfit`, `TILT_COVER_MIX 0`),
so vertical crop is exactly zero and the residue is the two top-corner wedges — filled for free by
the `C2D_TargetClear(screen, clrBg)` that R3.1.1 keeps in both branches. The horizontal decision is
**R3.6.2 spill**, and the reason is *not* "the mapping was hard":
> R2.5.3 justifies the scissor by **binocular rivalry** — content in one eye and not the other at
> the frame border (`main.c:718-719`). Under rule G10 tilt and stereo are mutually exclusive, and
> when the tilt is up **both eyes receive the identical tilted image**. The rivalry the scissor buys
> off cannot occur in the shipped configuration, so its only remaining cost — R2.5.3's own measured
> 2.5-4 points of retained frame — is unpaid-for.

R3.6.1 independently forbids shipping the inferred quarter-turn mapping unproven, and slice T2 is
PC-green only. The mapping is written down under `#if TILT_SCISSOR` (compiled out, so zero
warnings) for whoever proves O4 in Azahar. TEST 17c pins what spill actually costs in screen
pixels: at 10/15/20° the near row's left edge lands at **+1.35 / −10.47 / −24.31** screen px on the
top screen at Aspect-fit — i.e. at 10° it eats the 20 px letterbox pillar exactly, and above that
the viewport clips it. **verify-on-hw: this is the one visible consequence of D9.**

**D10 — composition (R4): every not-yet-tilt-aware pass is EXCLUDED while `tilt_active()`, and
the exclusion is written explicitly even though it is already implied.** `pop_eye`, `ui_pop_eye`,
`dof_bands`, `bloom_add`, `light_pass` all require `s3dOn`, and G10 makes `tiltTop ⇒ !s3dOn`, so a
*settled* tilt can never co-occur with them. But the **250 ms tween-out** after the 3D slider comes
up is exactly a window where `pop3d && tilt_active()` are both true, and drawing a flat-rect
overdraw over a tilted plane there is precisely the mis-registered "looks like a bug" frame R4.0.1
forbids. Hence `popPass`/`&& !tiltTop` rather than relying on the implication.
`warp_scenery_eye` needs nothing: it is the *no-shader* fallback, and no shader ⇒ `tiltOk == false`
⇒ flat (R4.2). This also answers SPEC-integration **Q2** for the record: with tilt up, DoF / bloom /
light are **skipped**, not composed — the expected answer, and here it costs nothing because they
were already off (the slider is down).

**D11 — one raw-C3D block, and it sets NO blend state.** The block is the proven
`C2D_Flush → C3D_FrameDrawOn → C3D_BindProgram → uniform → AttrInfo/BufInfo → TexBind/TexEnv →
DepthTest/CullFace → draw → C2D_Prepare` sequence (`main.c:1069-1092`), plus `C2D_SceneBegin(tgt)`
at the end for R3.1.4's "leave the screen bound" contract. Slice T2 deliberately does **not**
touch `C3D_AlphaBlend`: citro2d's standard blend is live (every pass that changes it restores it by
hand, because `C2D_Prepare` does not — R3.3.1), and with vertex alpha 1.0 that blend is the opaque
overwrite the base mesh wants. Set-and-restore would be pure churn on the thread that also drives
the worker handshake. **It becomes mandatory in slice 3** when the additive bloom and MULTIPLY
light draws join this block — flagged here so it is not discovered as a bug.

**D12 — the right eye takes the same projection.** `topR` is rendered unconditionally
(`main.c:2662`), so under tilt it must carry the *identical* tilted image or the two halves of the
top screen disagree. It gets its own slab (R3.5.1 forbids sharing) and, mirroring today's flat and
warp calls, no dim tint. Cost: 2 tilted images per frame, exactly as R3.4's "tilt ON, 3D off" row
budgets.

**D13 — `render_game` gained ONE parameter, but it is a 3-field `TiltDraw*`, not R3.1.3's bare
`const TiltView*`.** The draw needs the view **and** the dim tint (which R5.4 moves from a TEV
constant to the vertex colour) **and** the slab index (R3.5.1's per-image arena). Three parameters
at three call sites, two of which are `NULL`, would be worse to read than one struct whose NULL-ness
*is* the flat/tilt switch. Recorded as a deviation; the spirit of R3.1.3 (one parameter, NULL =
flat, `if (td) … else <today's code>`) is preserved exactly.

**D14 — `tiltTw[1]` (the bottom screen) is stepped but pinned to level 0.** R4.8.1/I3.1 keep the
bottom screen flat so `touch_to_gba` is bit-identical; keeping its tween alive costs ~10
instructions a frame and means enabling it later is a gate change, not a re-derivation.

**D15 — the vertex arena is allocated at R3.5's full 908-vert-per-image budget** (~98 KB
`linearAlloc`, 3 slabs) even though slice T2 writes only the first 176 of each. The alternative —
size it to the base mesh and realloc in slice 2 — would re-plumb the one thing that must not churn.
Only `nVerts * sizeof(TiltVert)` = 6,336 B per image is cache-flushed (R3.5.2), not the arena.

### What is now proven, and what is not

Proven on the PC: the vertex the renderer emits (TEST 17 — bitwise flat-path identity in *screen*
space across 3 modes × 2 screens, exact vertical span at every shipped angle, the spill goldens,
`iq` strictly decreasing with `iq(far) > 1 > iq(near)` and exactly 1 on the centre row, and
angle-independent texcoords). Proven by the toolchain: both builds link, zero new warnings, the
`.3dsx` is produced, and `tilt_shbin` is now actually *referenced* rather than merely linked.

**Unproven and unprovable here (verify-on-hw / in Azahar):** that the PICA200's rasterizer really
reconstructs the divide from `w_clip = iq` (R1.9's clip-space half — the whole phase rests on it and
one flat-at-level-0 frame validates it); the R3.4 pass budget and `worstMs` (O2); how the wedges and
the D9 spill read (O3); the scissor mapping (O4); `GPU_LINEAR` on a 2×-prescaled tilted source (O5);
and everything in SPEC-integration §7.2. Add to the checklist: **with `-DTILT_DEV_LEVEL=2`, a
level-0 (flat) frame must be pixel-identical to today's blit** — that single check validates the
matrix, the attribute layout, the TEV and the `iq` sign at once.

## 2026-08-03 — SLICE T1: the tilt math module + shader (no render wiring)

**Scope shipped:** the pure-C projection/cover/tween module, its sibling vertex shader, and the
host suite that pins both to the spec's published tables. **Nothing is wired into the render path
and no tracked file was modified** — `git status` after the slice shows four `??` entries and
`git diff` is empty, which is PHASE.md invariant 1 satisfied *by construction* rather than by
inspection.

### Files created

| File | What |
|---|---|
| `source/tilt.h` | Types + API + constants. Header-free (no libctru/citro3d/`u32`/`C3D_*`), `#pragma once`, banner-split per SPEC-integration §0.1 so the gate half drops in without a merge conflict. |
| `source/tilt.c` | `tilt_view_init` / `tilt_project` / `tilt_unproject` / the four growth factors + `tilt_cover` / `tilt_disp_scale` / `tilt_coverage` / the angle ladder / `tilt_tween_reset` / `tilt_tween_step` / `tilt_active`. `<math.h>` + `"tilt.h"` only. |
| `source/tilt.v.pica` | The perspective vertex shader (SPEC-render R5.2 verbatim + a cite-dense header). A **sibling**; `warp.v.pica` is byte-untouched. |
| `test/host/test_tilt.c` | TEST 4/5 (tween, SPEC-integration) + TEST 7-16 (projection, SPEC-render R6.9). Compile line in the file header. |

### Counts (all six suites, every pre-existing count met exactly)

| Suite | Result |
|---|---|
| **tilt (new)** — `clang -std=c11 -Wall -Wextra -O2 -I source test/host/test_tilt.c -o /tmp/tt` | **1243 checks, 0 failures** |
| celiolink | 1259 checks, 0 failures — PASS |
| netlink reliability | 66 checks, 0 failures — PASS |
| diag (D1/D2/D3) | 369 checks, 0 failures — PASS |
| control (D4/D5) | 6897 checks, 0 failures — PASS |
| trace replay (D7c) | 58 checks, 0 failures, 4 loud SKIPs — PASS |

`make -j8` → **`3DGBA.3dsx`** (3,778,184 B), exit 0. **Zero warnings in the two files this slice
adds to the devkitARM build** (`tilt.c`, `tilt.v.pica` — verified by touching both and re-making:
the only lines emitted are the three "compiling" echoes). Every remaining warning is the
pre-existing `main.c`/`wireless.c`/mGBA set logged by phase 13.

### Build wiring confirmed (SPEC-render R5.5 — no Makefile edit)

`PICAFILES` (`Makefile:103`) globbed `source/tilt.v.pica`; picasso assembled it; `build/` now holds
`tilt.shbin.o` (992 B) and **`build/tilt_shbin.h`** declaring `tilt_shbin[]` / `tilt_shbin_size`
(**296 B of shader**) — exactly the symbol shape `warp_shbin` has at `main.c:1002`. The `.shbin.o`
is in `OFILES_BIN` so it links whether or not `main.c` references it, which is why **`main.c` was
not touched at all**: the link is proven without spending any of invariant 1's budget.

### Decisions taken (open questions this slice had to resolve)

**D1 — the two specs define different tweens; SPEC-integration's ships.** `SPEC-render` R6.4/R6.3
list `tilt_tween(cur, target, easeIn, easeOut)` with frame-counted asymmetric easing
(`TILT_EASE_IN 0.0667` / `TILT_EASE_OUT 0.34`) and `tilt_active(int level, float angleCur)`;
`SPEC-integration` §2 specifies wall-clock `TiltTween` + `tilt_tween_reset/step` + smoothstep +
`tilt_active(const TiltTween*)`. SPEC-integration §0.1 explicitly assigns "gate + tween policy" to
that document, and its version is strictly more specified (dt clamp, retarget rule, exact-zero
termination, host-testable without a fake clock — I2.2's reasoning about the un-floored 60 fps loop
is sound). **Shipped: the SPEC-integration tween. `TILT_EASE_IN`/`TILT_EASE_OUT` are not defined
and `tilt_tween(cur,target,…)` does not exist.**
*Consequence the next author must know:* the retract is now symmetric 250 ms, so SPEC-render R6.8's
"a script/menu/battle retracts the tilt almost immediately (~3 frames)" no longer holds — it
retracts over a quarter second, per I2.5. R2.5.4's promise ("a textbox is never distorted for
long") is weakened from ~50 ms to ~250 ms. If hardware review dislikes it, the fix is a second
duration constant in `tilt_tween_step`, not a second tween.

**D2 — SPEC-integration I7.4's "one step of 125 ms" golden is unreachable** under its own I2.3
100 ms dt clamp. The **clamp wins** (it is the property that stops a HOME-menu resume from
teleporting the angle); the identical golden is reached in two legal 62.5 ms steps —
`ang == 7.5f` exactly, asserted with `==`. Both goldens (2.34375f at u=0.25, 7.5f at u=0.5) are
exact binary fractions and pass bitwise. Recorded in the test beside the assertion.

**D3 — R2.4's three coverage columns: definitions pinned.** The spec labels them tersely and the
obvious mis-reading is wrong by 17 %, so `tilt_coverage` implements:

```
void @ far row = DESTINATION px/side of the frame rect left uncovered = (vw/2)(1 - k*q_far)
crop @ row     = SOURCE columns/side falling outside the rect         = (vw/2)(1 - 1/(k*q))
```

All 30 published figures (10 angle rows × 3 columns) reproduce to <0.01 px under this reading.
The plausible mis-implementation — crop = the projected overhang `|fx(0,160)|` = 20.310 px at 15° —
does **not** reproduce the spec's 17.370. Identity that explains the table:
`k·q_near = kfit·botScale = (1 + 0.5 sin a)/cos a = ` gen1's own `base(a)`, so
`cropNear = (vw/2)(1 − 1/base)`; since `vw/2 = (vh/2)·1.5`, that is numerically the same series as
R1.9's centre-pinned screen-Y column — which is why the two tables share digits. Asserted both ways
in TEST 13 (`void == fx(0,0)` cross-checks R2.4 against R1.8).

**D4 — the gate is NOT in this slice.** `TiltGateIn` / `tilt_target_level` (SPEC-integration §1.4)
belong to the integration slice; `tilt.h` and `tilt.c` carry the reserved banner comment and the
test file reserves **TEST 1/2/3/6** for it (per SPEC-integration §0.2), so the two authors do not
collide. Slice T1 uses TEST 4/5 (tween/active, needed by the renderer) and TEST 7-16.

**D5 — two small API additions beyond R6.4**, both forced by the D1 unit split: `TILT_ANGLE_DEG[]`
(the level→degree table SPEC-integration §0.3 asks SPEC-render to hand over) and
`tilt_angle_deg_for_level()` next to R6.4's radian `tilt_angle_for_level()` — the tween runs in
degrees, `TiltView` in radians, and TEST 16 asserts the pair agrees. Both clamp an out-of-range
level instead of indexing out of bounds.

**D6 — `tilt_cover(a, mix)` short-circuits `mix == 0.0f` to `tilt_cover_fit(a)`** so the shipped
path (`TILT_COVER_MIX 0.0f`) carries no extra rounding, and `k == 1.0f` **bitwise** at angle 0.
Same reason `tilt_tween_step` terminates by assignment (I2.7): the flat path's exactness is the
whole invariant.

**D7 — defensive, not in either spec:** `tilt_tween_step` swallows negative *and NaN* `dtMs` via
`!(dtMs > 0.0f)`. A NaN would otherwise poison `ang` permanently and strand `tilt_active()` at
true, i.e. a stuck tilt with no way back to the flat path. Asserted in TEST 4.

### What the numbers actually proved

- **The identity at angle 0 is bitwise**, not approximate: `sinA == 0`, `cosA == 1`, `k == 1.0f`,
  `yc == 80.0f`, and all 176 mesh vertices satisfy `fx == cx`, `fy == cy`, `q == 1.0f` under `==`
  (TEST 7). Also `tilt_disp_scale == 1.0f` exactly, so the stereo comfort clamp is bit-identical to
  today's, and `tilt_coverage` returns three exact zeros.
- **Every published golden reproduced in float32 at the spec's own tolerance** — R1.8/T2 corners at
  1e-4 px, gen1's `Tilt.groundPoint` reference (T4, the external source of truth, tested in its
  centre-pinned form) at 1e-4 px, the R2.2 growth table at 1e-5, R2.4 coverage at 0.01 px. No
  tolerance had to be relaxed.
- **Round trip** `tilt_unproject∘tilt_project` worst error over 5×5 × {0,10,15,20}°: **1.53e-05 px**
  (spec budget 0.01 px). The inverse is therefore ready if bottom-screen tilt is ever enabled — it
  is *not* enabled (R4.8.1 / I3.1: `touch_to_gba` untouched).
- Structural sanity that a wrong sign or a wrong `k` would break: `q` strictly increasing with `cy`
  and `fy` strictly increasing (no fold-over) at every integer angle 1..60°; `q > 0` throughout
  (R1.2, so no near-plane guard is needed); `qTop < 1 < qBot` with `qMid == 1` exactly; the far edge
  measurably narrower than the near edge.

### Still verify-on-hw after this slice

Everything in R1.9's clip-space half (the CPU side is now pinned by TEST 8/10, but that the
PICA200's rasterizer reconstructs the divide from `w_clip = iq` is asserted *by construction* and is
unproven until a frame is drawn), plus the phase's whole Open Questions list (O1-O8) and
SPEC-integration §7.2. Nothing in this slice can move `worstMs`: it draws nothing.
