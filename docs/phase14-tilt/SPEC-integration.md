# SPEC-integration — phase 14 tilt: gating, tween, touch, settings, tiers, HUD, tests

Companion to `PHASE.md` (binding contract) and `SPEC-render.md` (the projection/quad half).
Everything here is **presentation + policy**: when tilt is allowed to engage, how it moves, how
the user sets it, what it costs per tier, and how a hardware photo is made interpretable.
This spec owns **no GPU code**. Where the two specs meet, the seam is named explicitly (§0.3).

Sources read for every claim below: `source/main.c` (@19375eb), `source/gamestate.{c,h}`,
`source/touch.c`, `source/theme.h`, `source/diag.h`, `test/host/test_diag.c`,
`design_handoff_3dgba_ui/assets_3ds/plates/manifests.json`,
`docs/kb/external/gen1-render.md`, `docs/kb/hd2d-octopath-3d.md`.

---

## 0. The seam (read before writing any code)

**0.1** All policy in this spec lives in the **pure-C, header-free module `source/tilt.{c,h}`**
(CLAUDE.md rule #4; PHASE.md invariant 6) — the same module `SPEC-render.md` puts the projection
math in. To keep two authors out of one merge conflict, the file is split by banner comment:

```
// ---- gate + tween policy (SPEC-integration §1-§2, §5) ----      <- this spec owns
// ---- projection / corners / view growth (SPEC-render) ----       <- the render spec owns
```

`tilt.c` includes **only** `<stdint.h>`/`<math.h>`/`"tilt.h"` — no libctru, no citro, no
`gamestate.h` (which pulls `gbacore.h`). Enum values it needs cross the boundary as plain ints
guarded by `_Static_assert` at the call site (§1.3).

**0.2** Host test file: **`test/host/test_tilt.c`**, one `main()`, TEST 1–6 = this spec, TEST 7+ =
`SPEC-render`. Build line (same shape as the other four suites):

```
clang -std=c11 -Wall -Wextra -O2 -I source test/host/test_tilt.c -o /tmp/tt && /tmp/tt
```

It `#include "../../source/tilt.c"` directly, exactly as `test_diag.c:17` includes `diag.c`.

**0.3** Interfaces this spec **hands to** `SPEC-render`:
`tilt_active(&tw)` (the one check PHASE invariant 1 hangs on), `tw.ang` (degrees, ≥ 0), and the
screen id. Interfaces it **needs from** `SPEC-render`: `TILT_ANGLE_DEG[TILT_LEVELS]` (the
level→angle table) and a single entry point of the shape
`tilt_draw_frame(target, EmuInstance*, screenW, screenH, mode, smooth, angDeg, tint)`.

---

## 1. I1 — THE GATE

### 1.1 Where the signal already is (no new reads, no new race class)

`run_session`'s **parked-window block** (`main.c:2031-2042`) already calls `game_read` for **both**
games every non-menu frame and keeps the results screen-mapped:

```c
GbaCore* gsTop = swapped ? emuB.core : emuA.core;      // main.c:2034
GbaCore* gsBot = swapped ? emuA.core : emuB.core;      // main.c:2035
bool gsTopOk = game_read(gsTop, gpTop, &gst);          // main.c:2041
bool gsBotOk = game_read(gsBot, gpBot, &gsb);          // main.c:2042
```

This runs with the workers **parked** (`main.c:1897` "Workers are parked here -> touch RAM access
safe") — the same window the touch path, the 3D depth path and the D4 control tick already use.
`gst`/`gsb` are then consumed by the gs logger and dropped.

**I1.1** Tilt gating MUST read those two existing structs. It MUST NOT add a `game_read`, a
`profile_for`, or any `gbacore_read*`. Cost of the whole gate: two struct copies.

**I1.2** The snapshot crosses into the render block exactly as `DepthSnap depth3d` does — a
function-scope struct declared next to it (`main.c:1683`), filled in the parked window, read in
the render block (`main.c:2592`):

```c
// phase 14: per-SCREEN tilt gate inputs, snapshotted in the parked window (the depth3d pattern,
// main.c:1683/1972/2592). Index = SCREEN (0 = top, 1 = bottom), never game slot.
typedef struct { uint8_t ok, fieldCtx, sb1Valid, textDlg; int16_t px; } TiltSnap;
TiltSnap tiltSnap[2] = { {0,0,0,0,-1}, {0,0,0,0,-1} };
```

Filled immediately after `main.c:2042`, from `gsTopOk/gst` → `tiltSnap[0]`, `gsBotOk/gsb` →
`tiltSnap[1]`. Zero-init means **a frame that never fills it gates tilt OFF** (fail-safe).

### 1.2 The predicate

**I1.3** Exact ctx rule: tilt is honored **iff `GameState.ctx == GCTX_OVERWORLD`** (value 1,
`gamestate.h:12`). Every other value — `GCTX_NONE`, both battle menus, `GCTX_BATTLE_TARGET`,
`GCTX_PARTY`, `GCTX_FIELDMENU`, `GCTX_BAG`, `GCTX_BATTLE_OTHER` — gates tilt **off**. This is
strictly stronger than gen1recomp's own gate (free-roam overworld, no transition, no script
runner — `Tilt.lua:116-118`, `Zoom.lua:103-111`): they had to approximate; `gamestate.c`'s
task-based menu detection is a positive read of the game's own live task table.

**I1.4** `ctx` crosses into `tilt.c` as a plain int, with the enum pinned at the call site:

```c
_Static_assert(GCTX_OVERWORLD == TILT_CTX_FIELD, "tilt.h TILT_CTX_FIELD drifted from GameCtx");
```

so a future insert into the `GameCtx` enum is a **compile error**, not a silent mis-gate.

**I1.5 — validity flags (this is the part that must not be skipped).** `ctx == GCTX_OVERWORLD` is
the **fall-through** branch of `game_read` (`gamestate.c:161-163`), and it unconditionally sets
`out->ctxResolved = false` there. So:

- `ctxResolved` is **useless as a positive signal** — it is `false` for the true overworld *and*
  for every undetected full-screen menu (pokédex / town map / summary / trainer card / naming
  keyboard / title). Do not gate on it.
- The gate MUST therefore also require **`sb1Valid && px >= 0`** — the save is loaded and the
  player's tile is real. This is not invented here: it is the project's existing "really in the
  field" predicate, `CtlIn.fieldValid` at `main.c:2076-2077` (SPEC-control-replay D4.9), which has
  been exercised on hardware by the D4 walk scripts.
- The gate MUST additionally require **`!textDlg`** (`GameState.textDlg`, `gamestate.h:108`,
  from `sFieldMessageBoxMode`/`sMessageBoxType`, `gamestate.c:168`). A field textbox up = a script
  is talking = precisely gen1recomp's "no script runner" clause. `textBanner` (the map-name popup)
  is **not** a gate — it is a 2-second transient and killing tilt for it would flap.

**I1.6 — unmapped game ⇒ OFF, never garbage.** `profile_for` returns `NULL` for any game code not
in `PROFILES` (`gamestate.c:55-61`); `game_read` then **memsets `*out`, sets the sentinels, and
returns false** with `ctx = GCTX_NONE` (`gamestate.c:87-92`). So a non-Pokémon ROM, or a Pokémon
revision we have not mapped, produces `ok=0, fieldCtx=0, sb1Valid=0, px=-1` → target level 0 →
tilt tweens to flat and stays there. **Requirement: the gate must test `ok` explicitly** (not just
`ctx`), because `ctx==GCTX_NONE` already implies off but a future refactor of the sentinels must
not be able to open the gate. Belt and braces on the one path that would otherwise tilt a frame we
know nothing about.

**I1.7 — residual, stated honestly.** An *undetected* full-screen menu (the cb2-column TODO the
phase-13 gs logger exists to close) still reads as `ctx=GCTX_OVERWORLD, sb1Valid=1, px>=0,
textDlg=0` and **will tilt**. That is a presentation-only, tweened, reversible wrongness on a
screen we cannot currently name, and the promotion path is already built: read the `cb2` column of
`3DGBA_gs_*.txt` for the screen, add it to the profile, and it becomes a real gate. Do **not**
invent a second detector for phase 14.
*Optional slice-2 backstop (top screen only, zero new reads):* `bg0_scan` already fills
`depth3d.uiRect[6]`/`nui` for the top game (`main.c:1980`). If the BG0 panels cover **≥ 55 % of the
150 (15×10) visible tiles**, treat it as a menu and gate down. It is top-only (`bg0_scan` never
runs for the bottom game, `main.c:1980`), so it would make the two screens behave differently —
which is why it is **not** in slice 1. If it ships, disclose the asymmetry in the BUILDLOG.

### 1.3 Both games, independently — decided

**I1.8** Tilt is gated **per screen, by the game on that screen** (`tiltSnap[0]` = whatever game
`swapped` puts on top). Not "the focused game", not "one gate for both".

Why:
1. The data for both games is already in hand in the same parked window (§1.1) — per-screen costs
   nothing extra; a focused-only rule would cost a *decision* and buy nothing.
2. The screens are already independent everywhere else in this codebase: `scaleMode[2]`,
   `smooth[2]`, `hudMode` bitmask, per-screen `calc_xform(mode, 400/320, 240, …)`. A single shared
   gate would be the odd one out.
3. The failure mode of a shared gate is *visible and wrong*: game A opens a battle, and game B's
   quietly-in-the-overworld screen snaps flat for no reason the player can see.
4. Focus (`focused`) is an **input** concept (which game the pad drives); tilt is a **presentation**
   concept. Coupling them would make X/Y (swap/focus) silently restyle a screen.

**I1.9** Exception: the frameskip tier rule (§5.4) *does* consult `focScreen`. That is a budget
clamp, not the gate, and it is applied after the gate in `tilt_target_level` so the truth table
stays one function.

### 1.4 The predicate, as code

**I1.10**

```c
// tilt.h — gate inputs. One flat struct so the whole policy is a pure function of observable
// state and the host suite can enumerate it (PHASE.md invariant 6).
#define TILT_LEVELS     4      // 0 = Off, 1 = Soft, 2 = Med, 3 = Deep (SPEC-render owns the angles)
#define TILT_CTX_FIELD  1      // == GCTX_OVERWORLD (gamestate.h:12); _Static_assert'd in main.c

typedef struct {
	int userLevel;     // g_prefs.tiltLevel, 0..TILT_LEVELS-1 (the SAVED preference)
	int screen;        // 0 = top, 1 = bottom
	int ok;            // game_read returned true for that screen's game (profile exists)
	int ctx;           // GameState.ctx as read
	int sb1Valid;      // GameState.sb1Valid
	int px;            // GameState.px (-1 = SaveBlock not ready)
	int textDlg;       // GameState.textDlg (field textbox up = script talking)
	int menuOpen;      // pause menu up
	int wlOn, netOn;   // wireless / loopback net link live
	int touchActive;   // tmEff != TOUCH_OFF   (bottom screen only; see §3)
	int stereoEngaged; // top screen only: the per-eye pop/warp passes will run this frame
	int isN3DS;        // APT_CheckNew3DS
	int fsOn, focScreen;
} TiltGateIn;

int tilt_target_level(const TiltGateIn* in);   // -> 0..TILT_LEVELS-1
```

Evaluation order (first hit wins, all return 0):

| # | Rule | Reason |
|---|---|---|
| G1 | `userLevel <= 0` | user says off |
| G2 | `!isN3DS` | tier clamp, §5.2 |
| G3 | `menuOpen` | §2.4 |
| G4 | `wlOn \|\| netOn` | tier clamp, §5.5 |
| G5 | `!ok` | no profile → never garbage (I1.6) |
| G6 | `ctx != TILT_CTX_FIELD` | I1.3 |
| G7 | `!sb1Valid \|\| px < 0` | I1.5 |
| G8 | `textDlg` | I1.5 (script/dialog) |
| G9 | `screen == 1 && touchActive` | §3 (Invariant 4) |
| G10 | `screen == 0 && stereoEngaged` | §5.6 |
| G11 | `fsOn && screen != focScreen` | §5.4 |
| — | else | `min(userLevel, TILT_LEVELS-1)` |

**I1.11** `tilt_target_level` is the **only** place any of these rules exists. `main.c` fills the
struct and calls it once per screen per frame — no gate logic in the render block.

---

## 2. I2 — THE TWEEN AND `tilt_active()`

### 2.1 Timing source

**I2.1** The render loop already computes a wall-clock stamp at the top of **every** iteration,
before the menu/no-menu split: `u64 nowMs = osGetTime();` (`main.c:1777`). The tween MUST be
driven from it:

```c
float dtMs = (float)(nowMs - tiltLastMs); tiltLastMs = nowMs;   // clamped inside tilt_tween_step
```

`tiltLastMs` is a `u64` initialized to `osGetTime()` next to the other per-session state
(`main.c:1737` `fpsT0`). Because the read is above the `if (!menuOpen)` split, the tween keeps
running with the pause menu open (required by I2.5) and while the game is gated off.

**I2.2** Divergence from the existing DoF/bloom easing, and why. `dofLvlTop/bloomLvl`
(`main.c:2596-2601`) ease by **fixed per-frame increments** (+0.08 / −0.34). That is fine for a
band alpha but wrong here: the loop is capped to 60 fps but not floored (`main.c:2805` only waits
when *under* budget), so under exactly the load tilt adds, a frame-counted tween would visibly run
slow. Wall-clock also makes the tween a pure function of `dtMs` — host-testable without a fake
clock. Note the divergence in the BUILDLOG so the next reader does not "fix" it to match DoF.

**I2.3** `dtMs` is clamped to **[0, 100] ms** inside `tilt_tween_step`. Rationale: `apt_hook`
suspends the app (`main.c:1764` `svcSleepThread(16ms); continue;` — which skips the `nowMs` read),
so a HOME-menu excursion produces one enormous delta on resume. Clamping makes the resume look
like a 100 ms step instead of teleporting the angle. (Teleporting would be *correct* but reads as
a snap — and PHASE invariant 5 says it never snaps.)

### 2.2 The tween

**I2.4**

```c
#define TILT_TWEEN_MS  250.0f   // gen1recomp tweens ~0.25 s (PHASE.md invariant 5)
#define TILT_EPS_DEG   0.001f

typedef struct {
	float angFrom, angTo, ang;   // degrees; ang is what the renderer consumes
	float tMs;                   // elapsed ms inside the current segment, clamped to TILT_TWEEN_MS
	int   level;                 // last target level fed in (Tilt.active()'s "level > 0" half)
} TiltTween;

void tilt_tween_reset(TiltTween* t);                                  // all zero, tMs = TILT_TWEEN_MS
void tilt_tween_step (TiltTween* t, int level, float angTo, float dtMs);
int  tilt_active     (const TiltTween* t);
```

`tilt_tween_step` semantics, in order:

1. clamp `dtMs` to `[0, 100]`;
2. **retarget**: `if (angTo != t->angTo) { t->angFrom = t->ang; t->angTo = angTo; t->tMs = 0.0f; }`
   — a mid-tween change restarts from the *current* angle, so the value is C0-continuous (never
   jumps). It is not C1 (velocity kinks on retarget); accepted, and cheaper than a spring;
3. `t->level = level;`
4. `t->tMs += dtMs;`
5. if `t->tMs >= TILT_TWEEN_MS` → `t->tMs = TILT_TWEEN_MS; t->ang = t->angTo;` (**assignment**, not
   a lerp — see I2.6);
   else `u = t->tMs / TILT_TWEEN_MS; s = u*u*(3-2*u); t->ang = t->angFrom + (t->angTo - t->angFrom)*s;`

Smoothstep `3u²−2u³` is gen1recomp's easing and is monotone on [0,1], so each segment is monotone.

**I2.5 — the gate transition rule.** The **target level** is recomputed every frame from
`tilt_target_level` (§1.4) and fed to `tilt_tween_step` every frame — including frames where the
gate is closed, the menu is open, or the app just resumed. Entering a menu mid-tilt therefore
**tweens down over 250 ms**; it never snaps. Concretely: `menuOpen` flips true → G3 returns 0 →
`angTo` becomes 0 → the tween runs *while the menu is open* because `nowMs` and the step call live
above the menu split (I2.1). Closing the menu tweens back up. Same for a battle starting, a
textbox opening, a wireless link starting.

**I2.6 — the exact contract PHASE invariant 1 hangs on.**

```c
int tilt_active(const TiltTween* t) { return t->level > 0 || t->ang > TILT_EPS_DEG; }
```

This is `Tilt.active()` (`Tilt.lua:110-114`) verbatim: *level > 0 OR angle > 0*. The `level > 0`
half matters on the very first frame after the user turns tilt on (target set, angle still 0); the
`ang` half matters on every frame of the tween back down.

**Requirement I2.7:** when a down-tween completes, `t->ang` MUST be **exactly `0.0f`** — that is
why step 5 assigns `angTo` rather than evaluating the lerp at `u == 1` (which is exact in IEEE for
this expression but not obviously so to a reader). The host suite asserts `t->ang == 0.0f` with
`==`, not an epsilon. Only then does `tilt_active()` return 0 and the flat path become
**byte-identical** to today's `render_game` blit (PHASE invariant 1).

**I2.8** One `TiltTween` per **screen**: `TiltTween tiltTw[2];` declared next to `depth3d`
(`main.c:1683`), `tilt_tween_reset`'d there. Per-screen because the gates are per-screen (I1.8).

---

## 3. I3 — TOUCH (PHASE invariant 4)

### 3.1 What the code actually does

- `touch_to_gba(px, py, mode, &gx, &gy)` (`main.c:619-629`) inverts **exactly** `render_game`'s
  bottom-screen transform: same three scale modes, same centring `ox/oy`, integer divide, and an
  in-frame test. Its header comment says so: *"Mirrors render_game's transform for the bottom
  screen (mode = scaleMode[1])."*
- It is called from **one** site, `main.c:1951`, and **only** under `tmEff == TOUCH_SMART`.
  `TOUCH_PAD` never uses `gx/gy`: `touch_update` returns `pad_keys(sx, sy)` from raw screen
  coordinates (`touch.c:534`) and `TOUCH_OFF` returns 0 (`touch.c:535`).
- With `touchMode == TOUCH_OFF`, a bottom-screen tap does not reach the game at all — it opens the
  pause menu (`main.c:1885`).
- Smart touch consumes `gx/gy` as **game-UI pixel coordinates**: action/move/party/battler
  hit-tests (`touch.c:112-151`), field-menu row hit-test (`touch.c:318`), bag rows + drag scroll
  (`touch.c:349-367`) and tap-to-walk/BFS (`touch.c:215-274`).

### 3.2 Decision: **(b) — suppress tilt on the bottom screen while any touch mode is active**

**I3.1** Rule G9 (§1.4): `screen == 1 && tmEff != TOUCH_OFF` → target level 0. The bottom screen
therefore tilts **only** when touch is off, which is the default (`touchMode = TOUCH_OFF`,
`main.c:1670`) and the normal dual-game configuration.

**I3.2** Why (b) and not (a) "exclude the bottom screen entirely": (a) throws away the effect on
half the device for a conflict that only exists in one mode. (b) costs the effect exactly where
the conflict is, and nowhere else. In the common two-games-no-touch session the player still gets
the dual-screen diorama.

**I3.3** Why not (c) "implement the inverse":

1. **`touch_to_gba` would have to become time-varying.** Under (c) the correct inverse depends on
   `tw.ang`, which changes every frame during a 250 ms tween. Touch is read in the parked window
   (`main.c:1946-1970`), the tween is stepped in the render phase (§2.1) — so a tap during a tween
   is inverted against an angle that is one frame stale *relative to the pixels the user aimed at*.
   That is a new correctness class, introduced into a feature that is hardware-validated.
2. **Error amplification at the receded edge.** The homography's `scale` compresses the top rows;
   a 1-px touch error there becomes 2–3 GBA px at 15° and worse above. Tap-to-walk (16-px tiles)
   would survive it; the bag/menu row hit-tests (8-px rows, `touch.c:349`) would not, reliably.
3. **The standing instruction.** Memory `touch-reliability-over-piling`: *finish/verify smart-touch
   on hardware one feature at a time; stop blind batches.* A silently-wrong mapping is the exact
   regression PHASE invariant 4 names.

**I3.4** Invariant 4 is then satisfied **by construction, not by arithmetic**: `touch_to_gba` is
consulted only when `tmEff == TOUCH_SMART`, and when `tmEff != TOUCH_OFF` the bottom screen's
target level is 0. The host suite proves the implication as a truth table (§7, TEST 3), so the
guarantee is checked, not asserted.

**I3.5** Transition behaviour: switching touch on mid-session tweens the bottom screen down over
250 ms (it is just another gate change, I2.5). The Touch tab's `Preview Gamepad` / `Preview Smart`
buttons close the menu (`main.c:2306`) straight into a tweening-flat bottom screen — correct and
self-explanatory.

**I3.6** `TOUCH_PAD` is suppressed too, even though it never calls `touch_to_gba`. One rule beats
two: the pad overlay (`touch_draw`, `main.c:2704`) is drawn in flat screen space over the game
image, and a tilted game under a flat D-pad reads as a bug. Same suppression, different reason —
document both so nobody "optimizes" PAD back in.

**I3.7 (not shipped — recorded so slice 2 does not re-derive it).** The closed-form inverse of the
`Tilt.groundPoint` homography (`Tilt.lua:120-130`, cited via gen1-render.md §1), for frame space
`vw=240, vh=160, d=FOCAL*vh`:

```
forward:  u = cx - vw/2 ; w = cy - vh/2 ; scale = d / (d - w*sin a)
          sx = vw/2 + u*scale ; sy = vh/2 + w*cos(a)*scale
inverse:  W = sy - vh/2 ; U = sx - vw/2
          w = W*d / (d*cos a + W*sin a)          <- solve sy for w
          scale = d / (d - w*sin a) ; u = U/scale
          cx = u + vw/2 ; cy = w + vh/2
```

If (c) ever ships, the test must assert a **round trip** `forward(inverse(p)) == p` within 0.01 px
over a grid (corners, centre, and the top edge where `scale` is smallest) at each shipped angle,
**and** the composition with the existing `calc_xform` screen transform — not the homography
alone. Do not trust hand-computed goldens; generate them in the test from the forward map.

---

## 4. I4 — SETTINGS + UI

### 4.1 Control kind: **PK_SEG, 4 segments**

**I4.1** `{PK_SEG, ACT_TILT, 4, 140, 198, 170, 26, "TILT"}`. Not `PK_TOG`, because the feature is a
ladder (PHASE invariant 5 "tween between levels"; gen1recomp's `Tilt.level`), and the codebase
already models 3–4-way ladders as `PK_SEG` with `nseg`: `ACT_TOUCHMODE` uses
`{PK_SEG,ACT_TOUCHMODE,3, 93,26,208,30,0}` (`main.c:1481`) and `ACT_HUD` uses `nseg=4`
(`main.c:1468`). Segment labels: **`{"Off","Soft","Med","Deep"}`** — four labels in a 170 px
segmented control is ~42 px each, which fits the 8–9 px JBM the `assets_seg` renderer uses.

### 4.2 Exact coordinates (they fit; nothing moves)

`PT_ENHANCE` today (`main.c:1473-1475`) is five toggles in one right-hand column:

| act | x | y | w | h | plate label (baked art) |
|---|---|---|---|---|---|
| ACT_3D | 276 | 51 | 34 | 18 | "Stereoscopic 3D" + "top screen" |
| ACT_DOF | 276 | 83 | 34 | 18 | "Tilt-shift DoF" |
| ACT_BLOOM | 276 | 112 | 34 | 18 | "LDR Bloom" |
| ACT_LIGHT | 276 | 141 | 34 | 18 | "Time-of-day light" |
| ACT_VIVID | 276 | 170 | 34 | 18 | "Vivid mode" |

The `pause-bot-enhance` plate (`plates/indigo/pause-bot-enhance.png`, manifest entry
`manifests.json:88`) bakes those five labels and **nothing below y ≈ 190**; the tab rail occupies
`x < 86` (`main.c:2262` hit-test) and the status hint sits at `y = 231` (`main.c:2794-2795`).

**I4.2** The new row lands in that empty band: **x = 140, y = 198, w = 170, h = 26** →
right edge 310 (flush with the toggle column's 276+34), bottom edge 224 (7 px above the hint).
No existing row moves; the plate art does **not** need regenerating.

**I4.3** The label uses the `PCtl.ov` overlay-label mechanism, which exists for exactly this —
controls the baked plate does not label. Precedent: `{PK_TOG,ACT_SWAP,0, 150,224,34,14,"Swap"}` and
`{PK_TOG,ACT_FS,0, 284,224,34,14,"Skip"}` on DISPLAY (`main.c:1469`), and
`{PK_SEG,ACT_PADEDGE,3, 140,224,172,14,"EDGES"}` on TOUCH (`main.c:1483`) — same x, same idea.
`ov` renders right-aligned at `x-6` (`main.c:2732`), i.e. `"TILT"` spans roughly x 115→134 at 8 px
JBM: clear of the rail at 86. **Slice 2 (optional):** regenerate the plate with a real baked
"Diorama tilt" label + "New 3DS · overworld only" sublabel and drop the `ov`.

**I4.4** `PTABN[3]` must go **5 → 6** (`main.c:1485`). Forgetting this is silent: the row draws
never, and the touch hit-test loop `for (i2 < nP)` never reaches it.

**I4.5** `menu_layout()`'s ENHANCE case (`main.c:1546-1553`) should gain
`PUSH(W_SEG, ACT_TILT, 0, 0);` for consistency. **Note for the implementer:** `menu_layout` /
`MenuW` / `menu_w_h` are **currently dead code** in the v3 plate path — the only references are the
definition and a comment (`main.c:1719`). Adding the row there is harmless bookkeeping; do not
spend time wiring it, and do not assume it drives anything.

### 4.3 The four switch statements (the trap)

**I4.6** `ACT_TILT` MUST get an explicit `case` in **all four** `PK_SEG` switches:

| site | file:line | what a missing case does |
|---|---|---|
| pause, read current | `main.c:2280-2283` | `default:` returns `g_prefs.padEdge` |
| pause, write back | `main.c:2285-2288` | `default:` **writes `g_prefs.padEdge`** |
| settings, read | `main.c:2930-2932` | same |
| settings, write | `main.c:2934-2936` | same |

Both write switches end in `default: g_prefs.padEdge = cur;`. A new `PK_SEG` without its case does
not fail loudly — it **silently re-skins the virtual gamepad**. Add the cases first, then the row.
Two more label tables need the option list: `main.c:2745-2753` (pause) and `main.c:2968-2973`
(standalone settings).

**I4.7** New action id: `ACT_TILT` joins the `>= 100` redesign-only block (`main.c:1455-1457`),
after `ACT_PREVIEW_SMART`. Ids `< 100` are the legacy `menuSel` dispatch (`main.c:1449` "legacy ids
(0..19) feed the original menuSel dispatch untouched") — a seg control must **not** land there.
`PCtl.act` is `unsigned char`, so any value ≤ 255 is fine.

### 4.4 Persistence

**I4.8** The level lives in **`g_prefs`**, not in the `settings_load/save` parameter list:

```c
// theme.h UiPrefs (append; theme.h:37-45)
int tiltLevel;   // phase 14: HD-2D diorama tilt, 0 = off .. TILT_LEVELS-1 (SAVED value; the
                 // live level is clamped per tier — gen1recomp Game.lua:841-857 rule)
```
Default `0` — append to the `g_prefs` initializer in `theme.c:19`.

This is the documented house pattern, verbatim from `theme.h:35-37`: *"Kept in a global so main.c,
touch.c, rompicker.c and wireless.c all read the same values without threading params through the
(already long) settings_load/save signatures."* It also means **zero call-site churn**: the ~12
`settings_save(...)` calls and the 3 `settings_load(...)` calls (`main.c:1747`, `main.c:2894`,
`main.c:3067`) are untouched.

**I4.9** `Settings` (`main.c:1349-1371`) gains `s32 tilt;` **appended after `padEdge`**, and
`settings_save` (`main.c:1418-1421`) adds `g_prefs.tiltLevel` as the last initializer element.

**I4.10 — no magic bump.** `SETTINGS_MAGIC` (`main.c:1348`) identifies the *family*; **length**
does the versioning, via the `offsetof` ladder at `main.c:1380-1388`. Add one rung:

```c
size_t lenPad = offsetof(Settings, tilt);   // pre-tilt = the current full struct
size_t lenNew = sizeof s;                   // includes tiltLevel
if ((n != lenNew && n != lenPad && n != lenOld && … ) || s.magic != SETTINGS_MAGIC) return;
…
if (n >= lenNew) g_prefs.tiltLevel = (unsigned)s.tilt % TILT_LEVELS;   // modulo, like padEdge
```

Existing files (current `sizeof`) now match `lenPad`, load everything they had, and leave
`tiltLevel` at its default 0. **Backward-compatible; no magic bump.**

**I4.11 — known one-way property, disclosed not fixed.** The loader accepts an *enumerated set* of
lengths, so a file written by the new build (larger) is rejected wholesale by an **older** build —
which silently reverts every preference to defaults. That is pre-existing behaviour for every
field added since `dof`; it is called out here so a downgrade during hardware bisection is not
mistaken for a settings bug.

### 4.5 The standalone settings screen

**I4.12** `run_settings` (`main.c:2889`) exposes tabs `{1,2,3,5}` = Display/Audio/**Enhance**/Touch
(`main.c:2896`). Because it renders straight from `PTABS[tab]`/`PTABN[tab]`, the tilt row appears
there **automatically** once I4.2/I4.4 land — provided I4.6's two `run_settings` switch cases and
label table exist. Verify by eye: ZR on the game picker → Enhance tab → the TILT row is present and
adjustable, and `SETSAVE()` persists it (`main.c:2895`).

**I4.13** `run_settings` has **no model flag** (no `isN3DS` parameter). Since §5 clamps by model,
add a file-scope `static bool s_isN3DS;` set in `main()` right after `APT_CheckNew3DS(&isN3DS)`
(`main.c:3030`) — matching the existing file-scope `s_hasPtm` (`main.c:68`) pattern — and read it
from both `run_session` and `run_settings`. Do **not** widen `run_settings`' signature.

**I4.14** Pause-menu status line on change (`status[48]`, drawn at `main.c:2794`):
`"Tilt: Deep (overworld only)"`, or on an Old 3DS `"Tilt: Deep — New 3DS only"`.

### 4.6 The pause summary pill — a real overflow

**I4.15** `draw_paused_summary` (`main.c:1589-1605`) declares `const char* labs[7]; u32 fg[7],
bg[7];` and `float pw[7]` and currently pushes **up to exactly 7** pills (3D, DoF, Bloom, Light,
[Vivid], touch, link). Adding `PILL("Tilt", g_prefs.tiltLevel > 0, g_ui.acc);` makes **8** →
buffer overrun. **All four arrays MUST be widened to `[8]`** in the same edit. Insert the Tilt pill
right after `Light` so the enhance pills stay grouped.

---

## 5. I5 — PERFORMANCE TIER

gen1recomp drops tilt **first**: `Performance.lua:44-46` — `high` = tilt+gbcfx+survey,
`balanced` = **no tilt**, `low` = nothing. And it **clamps live state without rewriting saved
options** (`Game.lua:841-857`). Both rules are adopted verbatim.

**I5.1 — clamp live, never rewrite.** Every rule below changes the *target level* inside
`tilt_target_level`. None of them writes `g_prefs.tiltLevel` or calls `settings_save`. A user who
sets Deep on a New 3DS and then boots the same SD card in an Old 3DS still has Deep saved.

**I5.2 — model detection and the Old-3DS rule.** The model is read once in `main()`:
`APT_CheckNew3DS(&isN3DS)` (`main.c:3030`), already threaded into `run_session`
(`main.c:1615`), plus a self-calibrating 804 MHz probe `speedupActive` (`main.c:3034-3039`,
`busy_ticks()` at `main.c:2879`).

- **New 3DS:** tilt offered, gate as specified.
- **Old 3DS:** the row stays **visible and adjustable** (so the setting round-trips and a user who
  moves the card to a New 3DS gets what they picked), but the **live level is clamped to 0** (rule
  G2). The Old-3DS path is already the "expect slowdown" path — `main.c:3041-3043` boots with
  *"Old 3DS detected. Two GBA cores need a New 3DS - expect slowdown."* — and CLAUDE.md convention
  #1 makes Old 3DS best-effort only. Spending render budget on a taste effect there is the wrong
  trade. Surface it: the `ov` label reads `"TILT"` and the status line says
  `"Tilt: <level> — New 3DS only"` (I4.14).

**I5.3 — do NOT clamp on `speedupActive`.** A `.3dsx` from the Homebrew Launcher cannot claim
804 MHz/L2, so `speedupActive` is false on the whole `make`-and-3dslink dev loop — clamping there
would make tilt un-iterable until a `.cia` install. Instead: when `isN3DS && !speedupActive`, allow
tilt but **stamp the fact** in the gs log's header/HUD path so a slow hardware photo is never
mistaken for a tilt cost. (The existing `perfWarn` splash already tells the user.)

**I5.4 — frameskip.** `fsOn` exists to starve the *unfocused* game to free budget for the focused
one (`main.c:1760-1761`, `gbacore_set_frameskip(core, 2)`). Tilting a screen we are deliberately
running at ⅓ rate spends the budget we just freed, on the screen the player is not looking at, and
the tilt of a 20 fps image is where warp shimmer will read worst. **Rule G11: when `fsOn`, only the
focused screen may tilt.** `focScreen` is already computed at `main.c:2579`.

**I5.5 — the wireless / net link: tilt is FORCED OFF (`wlOn || netOn`).** Argued, not assumed:

1. **It buys nothing.** Under `wlOn` the peer core is **paused** (`other->paused`, `main.c:2232`) —
   its screen is a frozen still. Tilting a frozen frame is pure cost.
2. **The participant game is latency-bound at ~4–5 fps** during a Gen-3 trade (MEMORY,
   `wireless-strategy.md` tier D/F). Tilt at 5 fps is a slideshow of a warped image.
3. **The render thread is the link's clock.** It orchestrates the per-frame `LightEvent` handshake
   (PHASE invariant 3) and, under `wlOn`, also drives the D1/D2/D3 samplers and the per-frame CSV
   writer (`main.c:2131`). Wall-clock added here is wall-clock removed from a round-paced protocol
   that took **thirteen hardware runs** to get working. PHASE invariant 2 says the trade path is
   untouched; the cheapest way to *keep* it untouched is to not run new GPU work beside it.
4. It also makes the D3 CSV honest: no tilt column can confound a link post-mortem (§6.3).

`netOn` (M2.5 loopback) is included for the same round-paced reason. **`linkOn` (the local
in-process cable, both cores free-running at full speed) is NOT suppressed** — say so explicitly in
the code comment so a future reader does not "fix" the asymmetry.

**I5.6 — stereo arbitration (rule G10), and the budget argument that makes this phase cheap.**
The top screen already spends **three game renders per frame** (top-left, top-right, bottom) when
3D is engaged, plus per-eye `warp_grid_eye` + `pop_eye` + optional DoF/bloom/light
(`main.c:2612-2671`). Those passes are **screen-space overdraws computed from `calc_xform`'s flat
rect** — geometrically they contradict a tilted plane, and running both would double-count depth
and misregister every popped sprite.

Decision for slice 1: **on the top screen, stereo wins.** `stereoEngaged` = the same expression the
render block already computes, `pop3d = s3dOn && depth3d.overworld && topG->core`
(`main.c:2592`). When it is true, the top screen's tilt target is 0.

Consequences, all good:
- The 3D slider becomes a **live A/B switch**: slider up = stereo diorama, slider down (or the
  ENHANCE 3D master toggle off) = tilt diorama. Self-explanatory without a single word of UI copy.
- **Tilt never adds cost to the 3-target frame.** It can only engage when the top screen is a
  single flat render. Extra cost is then ≤ 2 tilted quads per frame (top + bottom), each one
  textured 2-triangle draw plus one raw-C3D escape/return (the already-proven sequence at
  `main.c:1069-1090`). That is **no more state churn than the shipped `warp_grid_eye` already
  spends twice per frame under 3D** — and by construction they never both run. This is the
  numbers-backed answer to PHASE invariant 3's "budget the passes explicitly".
  *verify-on-hw: the claim is a pass count, not a measurement.*
- DoF / bloom / light are also flat-rect passes; when tilt is active on the top screen, whether
  they are skipped or composed is **`SPEC-render`'s call** — this spec only requires that the
  choice is stated there and that whatever is skipped is disclosed in the pause pill row.

**I5.7 — slice-2 upgrade path (not this phase).** The geometrically correct way to have both is a
per-eye **asymmetric frustum offset** on the tilt quad, which would supersede `pop_eye` entirely
rather than fight it. Record it; do not build it in phase 14.

**I5.8 — defaults.** `g_prefs.tiltLevel` ships **0 (Off)**, on every model. Rationale: the other
HD-2D effects (`dofOn/bloomOn/lightOn`, `main.c:1672-1675`) default on because they are
hardware-proven; this one is not (PHASE invariant 8 — PC-green is only the phase exit gate).
Shipping off guarantees no existing user's frame budget changes on upgrade. **Flip condition,**
recorded so it actually happens: after one hardware run confirms (a) no fps regression on the
non-tilt path and (b) ≥ 55 fps sustained with tilt at level 3 on a New 3DS with two cores running,
change the default to **2 (Med)** and note it in the BUILDLOG.

---

## 6. I6 — HUD + DIAGNOSTICS

### 6.1 The chip

**I6.1** Follow the existing chip pattern exactly. `main.c:2649-2650` draws the 3D chip inside the
`hudMode & 1` top bar, right-to-left along the `rx` cursor:

```c
if (s3dEnabled) { float cw = ui_text_w(txtBuf, "3D", 0.32f) + 12.0f;
                  ui_chip(txtBuf, "3D", rx - cw, 0.5f, THEME_GAME_B); }
```

Add, immediately after it (so it sits to the **left** of the 3D chip), advancing `rx` the same way:

```c
// phase 14: tilt state, so a hardware PHOTO is interpretable — accent = engaged on this screen,
// dim = the setting is on but the gate is closed (menu/battle/dialog/touch/link/tier).
if (g_prefs.tiltLevel > 0) {
    char tc[8]; snprintf(tc, sizeof tc, "TILT%d", g_prefs.tiltLevel);
    float cw = ui_text_w(txtBuf, tc, 0.32f) + 12.0f;
    rx -= cw + 6.0f;
    ui_chip(txtBuf, tc, rx, 0.5f, tilt_active(&tiltTw[0]) ? g_ui.acc : THEME_ON_DARK_DIM);
}
```

**I6.2** Differences from the 3D chip, deliberate: the 3D chip shows *enabled*; this one shows
*enabled + engaged* via the colour. A photo of a tilted screen with a dim TILT chip is
self-contradictory and would immediately localise a gate bug. It lives in the `else` branch of the
`netOn || wlOn` split (`main.c:2632/2635`), i.e. it never competes with the net-diag readout — and
under a link tilt is off anyway (I5.5).

**I6.3** Bottom screen: **no chip**. The bottom HUD bar (`main.c:2691-2699`) only carries clock +
fps and is 320 px wide; the pause pill (I4.15) already covers "is tilt on". Keep the in-game chrome
minimal.

### 6.2 Logging — reuse the gs seam, add no file

**I6.4** Tilt telemetry rides the **phase-13 game-state logger** (`gs_log_sample`, `gamestate.h:163`
→ `3DGBA_gs_*.txt`), via the existing size-tolerant `GsDepth` block whose header comment already
licenses this: *"Newer per-sprite disparity-detail fields are APPENDED (size-tolerant, like the
settings loader)"* (`gamestate.h:131-134`). Append **three** fields:

```c
// --- phase 14 tilt (LOGGING ONLY): the level/angle actually in force this frame, so a hardware
// photo of a tilted screen can be read against the gate. Both screens ride the TOP row because
// gs_log_sample is called with depth != NULL only for screen 0 (main.c:2120/2123). ---
uint8_t tiltLvl;              // effective (clamped) level on the TOP screen, 0..TILT_LEVELS-1
float   tiltAngTop, tiltAngBot;   // tweened angle in DEGREES per screen (0 = flat)
```

`GsLogEntry` (`gamestate.c:212-231`) gains the same three; `gs_log_sample` copies them
(`gamestate.c:287-293`); the dump header gains `,d_tiltLvl,d_tiltAngT,d_tiltAngB`
(`gamestate.c:~322`) and the `dValid` branch gains `",%u,%.2f,%.2f"`.

**I6.5** The **empty-field count for non-depth rows must go 15 → 18** — `gamestate.c` currently
emits `else fprintf(f, ",,,,,,,,,,,,,,,");` with the comment *"7 d_\* + 8 detail = 15 empty
fields"*. Getting this wrong shifts every subsequent column on bottom-screen rows and silently
corrupts the link columns. Update the comment arithmetic in the same edit.

**I6.6 — do NOT add a tilt edge trigger.** The gs ring is edge-triggered on ctx / cb2 / link state
plus a 600-frame and a 2 s wall-clock heartbeat (`gamestate.c:264-273`). Tilt changes are already
correlated with the ctx edges that cause them, and the 2 s heartbeat samples any hold. Adding a
tween edge would emit ~15 rows per transition and flood a 1024-entry ring.

### 6.3 Not the CSV

**I6.7** The D3 per-frame CSV (`DiagCsvRow`, `diag.h:263-307`) gets **no tilt column**. Reasons:
it is armed only under `wlOn` with a participant core (`main.c:2131`) and tilt is forced off there
(I5.5) — the column would be a constant 0; and its 62-column header is pinned by golden bytes plus
a header/row field-count parity assert in `test_diag.c` TEST 6 (`test/host/test_diag.c:478-538`),
so a decorative column would cost a golden rewrite for zero information. Say this in the BUILDLOG
so "why is tilt missing from the CSV" is answered once.

**I6.8** No new log file, no new SD path, no change to `diag_log_path` (`main.c:184`).

---

## 7. I7 — TEST PLAN

### 7.1 What the host suite proves (`test/host/test_tilt.c`, TEST 1–6)

**I7.1 — TEST 1: gate truth table.** Enumerate `tilt_target_level` over the full cross product of
the boolean inputs × all 9 `GameCtx` values × screen ∈ {0,1} × userLevel ∈ {0..3}. Assert:
- exactly one output is non-zero for the all-clear row, and it equals `userLevel`;
- every G1–G11 rule returns 0 in isolation (one row per rule, all other inputs clear);
- **rule precedence**: with two rules firing, the result is still 0 (order-independence of a
  0-returning ladder — proves no rule can "unblock" another);
- `ok == 0` ⇒ 0 for **every** ctx value including `TILT_CTX_FIELD` (I1.6, the unmapped-game case);
- `sb1Valid == 0` or `px < 0` ⇒ 0 even with `ctx == TILT_CTX_FIELD` (I1.5).

**I7.2 — TEST 2: per-screen independence.** Same input struct except `screen` and the two
screen-specific rules: assert `screen 0` is unaffected by `touchActive`, `screen 1` is unaffected
by `stereoEngaged`, and that a battle on one screen does not zero the other (I1.8) — i.e. two calls
with different snapshots yield independent answers.

**I7.3 — TEST 3: the touch invariant (PHASE invariant 4).** Assert the implication
`touchActive != 0 ⇒ tilt_target_level(screen=1, …) == 0` over the entire remaining cross product,
and separately that `touchActive` never affects `screen=0`. This is Invariant 4 as a machine-checked
statement (I3.4).

**I7.4 — TEST 4: tween shape.**
- **Monotonicity:** step 0→15° in 5 ms slices; assert `ang` non-decreasing at every step and
  `0 ≤ ang ≤ 15` throughout.
- **Golden values:** from rest at 0 with target 15°, one step of 125 ms ⇒ `u = 0.5`,
  `s = 0.5`, `ang == 7.5f` exactly; a step of 62.5 ms ⇒ `u = 0.25`, `s = 0.15625`,
  `ang == 2.34375f` exactly (both are exact binary fractions — assert with `==`).
- **Termination:** 250 ms of accumulated `dtMs` in any slicing lands `tMs == TILT_TWEEN_MS`.
- **Exact zero (I2.7):** after a full down-tween, `t->ang == 0.0f` compared with `==`, and
  `tilt_active(t) == 0`.
- **dt clamp:** a single `dtMs = 100000.0f` step advances at most 100 ms of tween.
- **Retarget continuity:** retarget mid-tween; assert the first post-retarget `ang` differs from the
  pre-retarget `ang` by ≤ the per-step delta (no jump), and that the new segment still terminates.

**I7.5 — TEST 5: `tilt_active` contract.** `level > 0 && ang == 0` ⇒ active (the first frame after
enabling); `level == 0 && ang > EPS` ⇒ active (the down-tween); `level == 0 && ang == 0` ⇒ **not**
active. This is the exact predicate the flat-path guarantee hangs on.

**I7.6 — TEST 6: settings round-trip.** Pure-C mirror of the `Settings` length ladder (the loader
itself is in `main.c` and not host-compilable): assert the `offsetof` rungs are strictly increasing,
that `offsetof(Settings, tilt)` equals the **old** `sizeof`, and that
`(unsigned)v % TILT_LEVELS` maps every `s32` (incl. negatives and `INT32_MIN`) into `0..3`. If the
ladder is not host-visible, replicate the struct in the test with a comment pinning it to
`main.c:1349`, and add a `_Static_assert` in `main.c` that the two agree.

**I7.7** Suite budget: ~250–400 checks. Add the new build line to `docs/HANDOFF.md`'s "How to
build / test" block and to the phase BUILDLOG, and keep the other five suites green
(1259 / 66 / 369 / 6897 / 58).

### 7.2 What ONLY hardware can prove

**I7.8** Honest list — none of this is provable on the PC or in Azahar:
1. **Stereo fusion under tilt.** Even with G10 forbidding the overlap, the *transition* (slider
   moving while tilted) crosses a state Azahar does not render; the parallax barrier is the only
   judge of whether the cross-fade is comfortable.
2. **The frame budget.** Azahar models neither core-2 contention nor the 804 MHz/L2 claim
   (CLAUDE.md convention #6). The extra quad's real cost, and whether it steals from the two GBA
   workers, is measurable only on a New 3DS.
3. **Readability.** Whether a 15° tilt of a 240×160 frame on a 400×240 LCD reads as "diorama" or as
   "blurry" — including how `GPU_LINEAR` filtering of the warped frame reads against the shipped
   sharp-bilinear look (gen1-render.md §8 step 3).
4. **Shimmer.** Sub-pixel crawl at the receded top edge while walking, which is a function of the
   real panel and real motion.
5. **The gate against real screens.** Which undetected full-screen menus (I1.7) tilt in practice.

### 7.3 Hardware checklist (HANDOFF style — paste into `docs/HANDOFF.md` Next steps)

```
PHASE-14 TILT — hardware checklist (New 3DS, .cia, one console; no link needed)
(a) BASELINE UNCHANGED: boot with Tilt=Off. Play 2 min dual-game. fps HUD must match the
    pre-phase-14 build; no TILT chip visible. (PHASE invariant 1 — flat path byte-identical.)
(b) ENGAGE: pause -> ENHANCE -> TILT = Deep. Resume in the overworld with the 3D slider at 0.
    EXPECT: the top screen tilts over ~0.25 s (no snap); TILT3 chip in accent. Photograph it.
(c) GATE — MENU: open the pause menu while tilted. EXPECT: tween DOWN, not a snap. Close it:
    tween back up.
(d) GATE — SCRIPT/BATTLE: talk to an NPC (textbox) and start a battle. EXPECT: flat both times,
    tweened; TILT chip goes dim (setting on, gate closed). Back to the overworld: tilts again.
(e) GATE — STEREO ARBITRATION: raise the 3D slider while tilted. EXPECT: tilt tweens out and the
    stereo pop takes over; lower it: tilt returns. No frame where both are visible.
(f) TOUCH INVARIANT: set Touch = Smart. EXPECT: the BOTTOM screen tweens flat and stays flat.
    Then tap a menu row and tap-to-walk. EXPECT: smart touch behaves EXACTLY as before this phase
    (this is the regression that matters — PHASE invariant 4).
(g) LINK SUPPRESSION: start a wireless link with Tilt=Deep. EXPECT: both screens flat for the whole
    session; the trade still completes and saves (run-#10 parity).
(h) BUDGET: Tilt=Deep, 3D slider 0, two games running, no frameskip. Read the fps HUD and the
    worst-frame ms over 60 s. RECORD the number; compare with (a). A drop below ~55 fps is a FAIL
    and the default in I5.8 does not flip.
(i) FRAMESKIP: enable Skip. EXPECT: only the focused screen tilts.
(j) OLD 3DS (if available): the TILT row is present and adjustable, and the screens stay flat.
(k) LOGS: copy sdmc:/cias/netlogs/ after the run. The gs log must carry d_tiltLvl / d_tiltAngT /
    d_tiltAngB, and the bottom-screen rows must still parse (18 empty d_* fields — I6.5).
```

---

## 8. Implementation order (smallest green steps)

1. `source/tilt.{c,h}` gate + tween sections; `test/host/test_tilt.c` TEST 1–6 green on PC.
2. `main.c`: `TiltSnap tiltSnap[2]` + `TiltTween tiltTw[2]` + the parked-window fill + the
   per-frame `tilt_target_level`/`tilt_tween_step` calls + the `_Static_assert`. **No rendering
   yet** — `tilt_active()` is computed and logged, nothing draws. Build green, zero warnings,
   behaviour identical.
3. `g_prefs.tiltLevel` + `Settings.tilt` + the length rung + the ENHANCE row + `PTABN[3]=6` + the
   four switch cases + the two label tables + the pill array widening. Still nothing draws.
4. HUD chip + gs-log columns (I6).
5. Hand to `SPEC-render`: the draw, behind `tilt_active()`.

Steps 2–4 are shippable and inert: they satisfy PHASE invariant 1 trivially, which makes step 5 the
only risky diff.

---

## Open questions

**Q1 (for the user / render spec).** Segment labels: `Off / Soft / Med / Deep`. If `SPEC-render`'s
angle table ends up at, say, 6/10/15°, "Deep" over-promises for a deliberately mild effect —
`Off / 1 / 2 / 3` or `Off / Low / Mid / Max` may read more honestly. Cosmetic, decide at
implementation.

**Q2 (render spec).** With tilt active on the top screen, are DoF / bloom / light **skipped** or
**composed**? This spec only requires the answer be stated and that anything skipped is reflected
in the pause pill row. Composing them means bouncing the flat stack through an offscreen target
first (they all draw in 400×240 screen space via `calc_xform`), which is more than "one extra draw
per game image" (PHASE invariant 3) — so skipping is the expected answer.

**Q3.** Default flip (I5.8): should the post-hardware default be **2 (Med)** or stay **0 (Off)**
because tilt is a taste effect? Recommendation: Med, matching how DoF/bloom/light ship on. User's
call after seeing it on the panel.

**Q4.** Should the top-screen tilt gate also require `focScreen == 0` when a *single* game is
loaded (`single` mode, bottom screen is the touch controller)? In `single` the bottom has no game,
so `tiltSnap[1].ok == 0` gates it off for free — believed to need no extra rule, but worth one
hardware glance in checklist item (b).

**Q5 (slice 2, deferred).** The I1.7 BG0-coverage backstop for undetected full-screen menus is
top-screen-only because `bg0_scan` only runs for the top game. Is it worth running `bg0_scan` for
the bottom game too (cost: one more OAM/BG0 scan per frame in the parked window), or is the cb2
promotion path enough? Recommendation: cb2 promotion — it is the phase-13 logger's whole purpose.
