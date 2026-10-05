# Phase 31 — SPEC-render: the diorama render pass, shader, buffers and control

Binding parent: `docs/phase31-diorama/PHASE.md`. Every claim cites `file:line` (this repo or the
headers under `/opt/devkitpro`) or a URL; every number was computed, and the arithmetic is
reproduced inline so a reviewer can re-derive it. Anything the PC cannot settle is tagged
**verify-in-Azahar** or **verify-on-hw**.

**Revision 2026-09-10b** — PHASE.md moved **stereoscopic 3D into v1** (*What ships #6*,
invariant 5). That added **§3S THE PER-EYE PASS**, rewrote **§1.4**, and amended **§1.2**,
**§1.5**, **§3.3 R-M1**. Where an earlier subsection says "same image both eyes", §3S wins.

**Sources read for every claim below** (nothing here is from memory):

| What | Where |
|---|---|
| The phase contract, bounds and invariants | `docs/phase31-diorama/PHASE.md` |
| Mesh recipes, coordinates, camera, frame order | `RESEARCH-classification.md` §2, §5, §7 |
| Presenter pipeline, billboards | `RESEARCH-presenter.md` §4 |
| Cross-cutting ideas, PICA post-FX mapping | `RESEARCH-polish.md` §1.3, §2.4 |
| The house style for a render spec: computed budgets, the raw-C3D pass | `docs/phase14-tilt/SPEC-render.md` |
| PICA200 caps (no fragment shader, 6 TEV stages) | `docs/kb/hd2d-octopath-3d.md` §0 |
| The frame loop, gate, tilt block, allocations, control tables | `source/main.c` (line cites inline) |
| Gate/tween policy, the level ladder | `source/tilt.{c,h}` |
| The tiled-texture encoder contract | `source/peersprite.{c,h}` |
| citro3d matrix/state/draw API | `/opt/devkitpro/libctru/include/c3d/*.h` + citro3d `source/` on GitHub |
| GPU enums (formats, test funcs, cull modes, write masks) | `/opt/devkitpro/libctru/include/3ds/gpu/enums.h` |
| citro2d's target creation, assumed state and text metrics | citro2d `source/{base,text}.c` on GitHub |
| Emerald layout sizes | `https://raw.githubusercontent.com/pret/pokeemerald/master/data/layouts/layouts.json` |
| Shipped font metrics for the control fit | `data/fnt_sg_med_15.bin` via `tools/fontlab/bcfnt.py` |

## 1. THE PASS

### 1.0 Where the draw goes — one line, two call sites already exist

`render_game()` (`source/main.c:2324-2380`) is the only function that puts a game image on a
screen. Phase 14 already carved the "replace the blit" hole in it: a non-NULL `const TiltDraw* td`
makes it clear the target and hand off to `tilt_draw_image`, returning before the flat
`C2D_DrawImageAt` (`main.c:2355` in the sharp-bilinear branch, `main.c:2375` in the direct
branch). The diorama takes the *same* hole, one level earlier.

**R-P1 (requirement).** `render_game` gains ONE new parameter, `const DioDraw* dd`, and ONE new
line, placed immediately after the existing `!e->core` early-return at `main.c:2328`:

```c
if (!e->core) { C2D_TargetClear(screen, clrBg); C2D_SceneBegin(screen); return; }
if (dd) { C2D_TargetClear(screen, clrBg); dio_draw_scene(screen, dd, screenW, screenH); return; }   // phase 31
```

It goes **above** the `sharpBilinear` decision (`main.c:2341`) and not next to the two `td`
branches, for a measured reason: the diorama samples the **metatile atlas**, never `e->tex` and
never `preTex`, so the NEAREST 2x prescale pass 0 (`main.c:2347-2352`, a 480x320 fill into the
512x512 VRAM target) is pure waste under the diorama. Entering above it skips a full-screen
GPU fill per engaged eye — 3 of them per frame across topL/topR/bottom in the worst case.
`mode`, `smooth`, `tint`, `preTgt` and `preTex` are all unread on this path.

`dd == NULL` restores today's function character for character, which is PHASE.md invariant 1.

### 1.1 The engage rule — level 4 is a HARD switch, not a tween

`tilt_active()` is `t->level > 0 || t->ang > TILT_EPS_DEG` (`source/tilt.c:200`), so a naive
level 4 would report "tilt active" forever. The ladder is extended so the two are mutually
exclusive by arithmetic, with no second gate and no change to `tilt_active`:

| | value |
|---|---|
| `TILT_LEVELS` | 4 -> **5** (`source/tilt.h:40`) |
| `TILT_ANGLE_DEG` | `{ 0.0f, 10.0f, 15.0f, 20.0f, 0.0f }` (`source/tilt.c:18`) |
| new symbol | `#define TILT_LEVEL_DIO (TILT_LEVELS - 1)` in `tilt.h` |

**R-P2 (requirement).** In the render block, immediately after `main.c:4771-4772`:

```c
bool dioTop  = dioOk && tiltTw[0].level == TILT_LEVEL_DIO && tiltTw[0].ang <= TILT_EPS_DEG
               && dio_scene_ready(&dioW, /*screen*/0);
bool tiltTop = tilt_active(&tiltTw[0]) && !dioTop;      // main.c:4771, amended
bool tiltBot = tilt_active(&tiltTw[1]);                 // main.c:4772, UNCHANGED
```

Consequences, each deliberate:

- **Level 4 settled** -> `ang == 0` -> `dioTop` true, `tiltTop` false. Exactly one of the two
  paths can run.
- **3 -> 4** -> the angle tweens 20 deg -> 0 over `TILT_TWEEN_MS` (250 ms, `tilt.h:91`) with
  `tiltTop` still true, then the diorama appears. The user sees "the tilt flattens, then the world
  stands up". There is no continuous parameter between a flat frame and a 3D scene, so a tween is
  not available; flatten-then-switch is the only transition that never shows a half-built state.
- **4 -> 3** -> `level` is 3 on the first frame, so `dioTop` goes false immediately and the tilt
  tweens *up* from 0. Also the correct behaviour when the gate shuts (G1-G11 all return 0):
  `level == 0` -> `dioTop` false **that same frame** -> flat. PHASE.md invariant 5 / "fallback =
  exactness" is satisfied by the existing gate with no new code.
- `dio_scene_ready()` is the third term and it is what makes a mid-warp / mid-load frame fall back
  rather than draw an empty world: no built mesh for the current layout -> flat, silently.

**R-P3 (requirement, PHASE bound 3).** The BOTTOM screen at level 4 behaves as level 3. That is a
one-function change in the pure-C module, not a render-block special case:

```c
/* tilt.h/tilt.c — screen-aware ladder read; host-testable, no libctru */
float tilt_angle_deg_for_level_screen(int level, int screen) {
    if (level == TILT_LEVEL_DIO && screen != 0) return TILT_ANGLE_DEG[TILT_LEVEL_DIO - 1];  /* 20 deg */
    return tilt_angle_deg_for_level(level);
}
```

and `main.c:4571` becomes
`tilt_tween_step(&tiltTw[sc], tiltLvl[sc], tilt_angle_deg_for_level_screen(tiltLvl[sc], sc), dtTilt);`.
`tiltTw[1].level` stays 4, so `tilt_active` is true on the bottom and the bottom screen tilts at
20 deg while the top is a diorama. G9 (`screen == 1 && touchActive`, `tilt.c:154`) still pins the
bottom to 0 whenever any touch mode is live — the touch mapping is untouched, invariant 2.

**Free tier clamp.** G2 (`!isN3DS -> 0`, `tilt.c:146`) already forbids the whole ladder on an Old
3DS, so the diorama inherits the New-3DS-only rule from CLAUDE.md convention 1 with zero new code.

### 1.2 Everything phase 14 excludes under tilt is excluded here too

Each of these lines currently ends in `&& !tiltTop`. Each gains `&& !dioTop`. They are listed with
the line the condition lives on so the implementer can do them in one pass and a reviewer can
`grep -c dioTop` and expect **six**.

| Pass | Condition lives at | Amended to |
|---|---|---|
| stereo scenery pop + `pop_eye` | `main.c:4784` `bool popPass = pop3d && !tiltTop;` | `... && !tiltTop && !dioTop` |
| BG0 UI pop | `main.c:4785` `bool uipop = s3dOn && depth3d.nui > 0 && topG->core && !tiltTop;` | `... && !dioTop` |
| tilt-shift DoF bands | `main.c:4794` `bool dofPass = ... && !tiltTop;` | `... && !dioTop` |
| LDR bloom | `main.c:4795-4796` `bool bloomPass = ... && !tiltTop;` | `... && !dioTop` |
| time-of-day light grade | `main.c:4797` `bool litPass = ... && !tiltTop;` | `... && !dioTop` |
| co-op presence avatar | `main.c:4841-4843` / `main.c:5076-5078` (drawn unconditionally; the tilt is passed as `tiltTop ? &tiltVw : NULL`) | wrap both calls in `if (!dioTop)` |

The reasoning is phase 14's R4.0.1 verbatim (`main.c:4776-4783`): *a pass that is not yet
diorama-aware is EXCLUDED while the diorama is up, never drawn flat over it.* All five effect
passes composite sub-rects of the **flat frame-space** image (`calc_xform`, `main.c:709-715`);
over a perspective scene they are not merely wrong, they re-paint the scene with 2D game pixels.

The presence avatar is the one that needs saying out loud, because it is the only one that is
*world* content and could in principle be re-homed: phase 15 places it by frame-space anchor and
phase 20 draws it from a citro2d sheet quad. Putting the peer into the diorama means turning it
into a **billboard in the world**, which is exactly what §4(b)/§5 already build for object events —
so it is a natural v1.5 item (one extra billboard whose transform comes from `presOut[].wx/wy`),
and it is explicitly NOT v1. In v1 the co-op chip therefore reads dim on a diorama frame; §7 makes
the HUD say so. (Stereo moving into v1 does not change this: the avatar is excluded on BOTH eyes,
§3S.5.)

`dof_prepare` / `bloom_bright` at `main.c:4800-4801` are already guarded by `dofPass || bloomPass`
and `bloomPass`, so they need no edit — they go silent for free.

`sharpTop` (`main.c:4802`) needs no edit either: its consumers (`warp_grid_eye`, `ui_pop_eye`) are
now excluded, and `render_game` never reaches the prescale decision under `dd`.

### 1.3 The fallback path — same frame, no latch

There is no diorama-side fallback code. `dioTop` is recomputed from scratch every frame from
(a) `tiltTw[0]` which the existing gate drives, (b) `dioOk` which is a permanent init verdict,
(c) `dio_scene_ready()`. Any of them false -> `dd = NULL` -> `render_game` runs today's blit on
**that same frame**, one frame after the game left the overworld. This is the phase-14 shape
(`tiltOk` folded into `gi.userLevel` at `main.c:4534`) and it is why there is no second ladder:

**R-P4 (requirement).** `dioOk` is folded in exactly where `tiltOk` is, so a missing shader, a
failed `linearAlloc` or a failed atlas allocation presents the SAVED level to the gate as 0 and
the tween parks at 0.0:

```c
gi.userLevel = tiltOk ? g_prefs.tiltLevel : 0;                                 // main.c:4534 today
gi.userLevel = tiltOk ? ((!dioOk && g_prefs.tiltLevel == TILT_LEVEL_DIO)       // phase 31
                          ? TILT_LEVEL_DIO - 1 : g_prefs.tiltLevel) : 0;
```

i.e. a console where the diorama failed to initialise degrades level 4 to level 3 (Max tilt), it
does not go flat. That is the friendlier failure and it keeps `tilt_target_level` untouched.

### 1.4 The right eye under level 4 — a real second camera (AMENDED 2026-09-10)

**This subsection was rewritten when PHASE.md moved stereoscopic 3D from "After v1" into
*What ships #6*. The full treatment is §3S; what follows is only the frame-loop half.**

The right-eye block already re-enters `render_game` with the same game (`main.c:5060-5062`). Under
the diorama it receives its own `DioDraw` differing in exactly one field, `eye`:

```c
DioDraw dioL = { &dioW, 0 };   /* left  */
DioDraw dioR = { &dioW, 1 };   /* right */
render_game(topG, top,  ..., dioTop ? &dioL : NULL);   // main.c:4818
render_game(topG, topR, ..., dioTop ? &dioR : NULL);   // main.c:5060
```

Everything else is shared. The second eye re-issues the identical `C3D_DrawElements` calls against
the **same** terrain and billboard buffers, so it costs **zero CPU vertex writes and zero
`GSPGPU_FlushDataCache` calls** — the strongest form of phase 14's `slab == clone` case
(`main.c:1338-1345`). The only per-eye state is the MVP uniform, whose projection carries a signed
interaxial and a shared convergence plane (§3S.2/§3S.3).

Two gate consequences, both settled in §3S.6:

- **G10 no longer shuts level 4.** `tilt_target_level`'s stereo rule (`tilt.c:155`) gains one term,
  `&& in->userLevel < TILT_LEVEL_DIO`, so levels 1-3 keep today's truth table exactly (their tests
  are unmodified) while level 4 passes through with the slider up.
- **The six per-eye 2D passes stay excluded for BOTH eyes** — §1.2's `&& !dioTop` clauses are on
  the shared booleans, so the right-eye call sites at `main.c:5063-5079` are covered by the same
  six edits. §3S.5 checks each one off by line.

### 1.5 Frame-order summary (top screen, level 4 engaged)

```
C3D_FrameBegin(C3D_FRAME_SYNCDRAW)                       main.c:4507
  ... parked-window snapshots already taken ...
  gate + tween (unchanged)                               main.c:4526-4572
  dio_frame_update(&dioW, ...)  <-- CPU: camera follow, billboard build   [§9]
  render_game(topG, top, ..., dd)                        main.c:4818
      C2D_TargetClear(top, clrBg)      colour + DEPTH -> 0 (far)  [§3.6]
      dio_draw_scene(top, dd, 400, 240)
          C2D_Flush; C3D_FrameDrawOn(top); C3D_BindProgram(&dioProg)
          [terrain draw] [billboard draw]                          [§5, §6]
          restore state; C2D_Prepare()                             [§6.3]
  (popPass/uipop/dofPass/bloomPass/litPass/presence all skipped)   [§1.2]
  HUD bar + chips (citro2d)                              main.c:4846+
  render_game(topG, topR, ..., &dioR) -> SAME buffers, own target,
                                       own MVP (signed interaxial)  [§1.4, §3S]
  bottom screen: tilt at level 3 or flat                 main.c:5095-5098
C3D_FrameEnd
```

Every texture and VBO write listed above happens **inside** `C3D_FrameBegin/End`, which is
PHASE.md invariant 4 and the phase-20 rule quoted at `main.c:1550`.


## 2. THE SHADER

### 2.1 Full proposed source — `source/diorama.v.pica`

This is a genuine 3D vertex shader, not phase 14's screen-space one. `tilt.v.pica` smuggles the
projective divide into a hand-built per-vertex `iq` under an ORTHO matrix (`source/tilt.v.pica`
lines 18-24, SPEC-render R1.5); here the perspective is real and lives in the matrix, so the
shader is the textbook `dp4` block plus two pass-throughs.

```
; diorama.v.pica — phase 31 DIORAMA: world-space vertex shader (SPEC-render §2, §3).
;
; A SIBLING of tilt.v.pica and warp.v.pica, never an edit to either. warp.v.pica emits z=0,w=1
; (affine screen space, main.c:1071/2224/2276); tilt.v.pica emits a HAND-BUILT w = iq under an
; ortho matrix (tilt.v.pica:18-24) because its "3D" is a planar homography. This one is the real
; thing: positions arrive in WORLD units (1 tile = 1.0 in X/Z, 1 storey = 1.0 in Y — clean-room
; RESEARCH-classification.md §2) and the single uniform is a full model-view-projection built by
; Mtx_PerspTilt * Mtx_LookAt (§3). w_clip therefore comes out of the matrix's last row and the
; fixed-function rasterizer does perspective-correct interpolation with nothing to fake.
;
; The PICA200 has NO fragment shader at all (3ds/gpu/shbin.h:11-12 lists only VERTEX_SHDR and
; GEOMETRY_SHDR — docs/kb/hd2d-octopath-3d.md §0.1), so every per-pixel decision this pass makes
; is a TEV stage + the alpha test (§6), and every per-face decision is baked into the vertex
; colour by the pure-C mesh emitter (source/diorama.c).

; Uniforms
.fvec mvp[4]                        ; Mtx_PerspTilt(fovx, 240/400, near, far, true) * Mtx_LookAt(...)

; Constants
.constf myconst(0.0, 1.0, 0.0, 0.0)
.alias  ones  myconst.yyyy

; Outputs
.out outpos position
.out outtc0 texcoord0
.out outclr color

; Inputs: v0 = (x, y, z)     WORLD units: x east, y up, z south (§3.1)
;         v1 = (u, v)        atlas texcoord, already half-texel inset by the emitter (§4.1.5)
;         v2 = (r, g, b, a)  per-face directional shade (§6.1) x alpha (1.0 opaque / cutout src)

.proc main
	; r0 = (x, y, z, 1)
	mov r0.xyz, v0.xyz
	mov r0.w,   ones

	; outpos = mvp * r0   — w_clip = dot(mvp[3], r0) = the true view depth (§3.4)
	dp4 outpos.x, mvp[0], r0
	dp4 outpos.y, mvp[1], r0
	dp4 outpos.z, mvp[2], r0
	dp4 outpos.w, mvp[3], r0

	; pass through
	mov outtc0, v1
	mov outclr, v2

	end
.end
```

Register budget: 3 input registers of 12, 3 output registers of 7, 1 temporary, 4 float uniform
vectors. Nothing here is near a limit — the PICA's constraint on this phase is fill rate and CPU
(§9), never shader complexity.

### 2.2 The attribute layout, and why the colour is four FLOATS

```c
typedef struct { float x, y, z; float u, v; float r, g, b, a; } DioVert;   /* 36 B */
```

```c
C3D_AttrInfo* ai = C3D_GetAttrInfo();
AttrInfo_Init(ai);
AttrInfo_AddLoader(ai, 0, GPU_FLOAT, 3);   // v0 = (x, y, z)     world units
AttrInfo_AddLoader(ai, 1, GPU_FLOAT, 2);   // v1 = (u, v)
AttrInfo_AddLoader(ai, 2, GPU_FLOAT, 4);   // v2 = (r, g, b, a)
C3D_BufInfo* bi = C3D_GetBufInfo();
BufInfo_Init(bi);
BufInfo_Add(bi, vbo, sizeof(DioVert), 3, 0x210);   // 3 attrs, permutation nibbles 0/1/2
```

byte-for-byte the `TiltVert` loader block at `main.c:1349-1353`, with `(x, y, iq)` becoming
`(x, y, z)`.

**R-S1 (requirement). The colour attribute is `GPU_FLOAT, 4` — never `GPU_UNSIGNED_BYTE, 4`,
even though that would save 12 B/vertex.** This is phase 14's R5.3 note, restated because the
diorama is the first pass where the saving would be tempting (§5 budgets 60-120 k vertices, so
12 B/vertex is 0.7-1.4 MB): *"whether the PICA normalises integer attributes to 0..1 or hands over
the raw 0..255 is not documented in any header here, and guessing wrong yields a silently 255x
over-bright frame"* (`main.c:1171-1174`). `GPU_UNSIGNED_BYTE = 1` exists in
`3ds/gpu/enums.h:297` and `AttrInfo_AddLoader` accepts it (`c3d/attribs.h:12`), but neither the
header nor citro3d documents the normalisation rule, and phase 14 declined to guess with 176
vertices at stake. Nothing has changed that; the fix is a **measurement**, not a decision, and it
belongs in §11 as an open question with a named experiment (O-R3).

The vertex colour is load-bearing here in a way it was not in phase 14: it carries the **baked
directional shade** that makes a box read as a volume with no lighting unit at all — top 1.00,
north 0.90, south 0.85, east 0.82, west 0.75 in the reference's model
(`RESEARCH-classification.md` §5.3), and the untextured grey sides of the small extruded boxes
(§5.4 of the same). Per-face, so it is constant across each quad's four vertices and costs the
emitter nothing.

### 2.3 Makefile wiring — there is none to do

`PICAFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.v.pica)))` (`Makefile:103`)
globs the directory; `OFILES_BIN` (`Makefile:113`) and `HFILES_BIN` (`Makefile:115`) derive
`diorama.shbin.o` and `diorama_shbin.h` from it, and the pattern rule at `Makefile:183-184`
assembles `%.v.pica -> %.shbin` with picasso and embeds it as a data blob.

**Dropping `source/diorama.v.pica` into the tree is the entire build change.** `#include
"diorama_shbin.h"` in `main.c` next to `tilt_shbin.h`, and the symbols `diorama_shbin` /
`diorama_shbin_size` exist. (Confirming this is worth the sentence: phase 14 added `tilt.v.pica`
the same way and touched no build file.)

### 2.4 Init and teardown — `dio_gpu_init()`, modelled on `tilt_init`

```c
static DVLB_s*         dioDvlb;
static shaderProgram_s dioProg;
static int             dioMvpLoc = -1;
static DioVert*        dioTerrainVbo;   // linearAlloc, rebuilt on map change  (§5.2)
static DioVert*        dioSpriteVbo;    // linearAlloc, double-buffered        (§5.4)
static u16*            dioIbo;          // linearAlloc, static quad indices    (§5.3)
static C3D_Tex         dioAtlas, dioSheet;
static bool            dioOk;           // false => level 4 degrades to level 3, permanently

static void dio_gpu_init(void) {
    dioDvlb = DVLB_ParseFile((u32*)diorama_shbin, diorama_shbin_size);
    if (!dioDvlb) return;
    shaderProgramInit(&dioProg);
    shaderProgramSetVsh(&dioProg, &dioDvlb->DVLE[0]);
    dioMvpLoc = shaderInstanceGetUniformLocation(dioProg.vertexShader, "mvp");
    /* ... linearAlloc the three buffers, C3D_TexInit the two textures (§4) ... */
    if (dioMvpLoc < 0 || !dioTerrainVbo || !dioSpriteVbo || !dioIbo) return;   // dioOk stays false
    dio_build_indices(dioIbo);                                   // pure-C, §5.3
    GSPGPU_FlushDataCache(dioIbo, DIO_IBO_BYTES);
    dioOk = true;
}
```

Called from `main()` immediately after `tilt_init()` (`main.c:5701`), and `dio_gpu_fini()` next to
`tilt_fini()`. Unlike `tilt_init` it does **not** depend on `warpOk` — the diorama builds its own
index buffer (§5.3) rather than reusing `warpIbo`, because `warpIbo`'s 176-vertex 16x11 topology
(`main.c:1113-1120`) has nothing to do with a quad soup.

The uniform is set once per draw block with the same call phase 14 uses:
`C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, dioMvpLoc, &mvp);` (`main.c:1348` pattern).


## 3. MATRICES + COORDINATES

Everything in this section was derived from citro3d's own matrix **source**, not from the header
doc comments — the header comments for `Mtx_PerspTilt` are misleading and the implementation says
so in its own words. Sources: `source/maths/mtx_persptilt.c`, `mtx_orthotilt.c`, `mtx_lookat.c`,
`mtx_multiply.c` at `https://raw.githubusercontent.com/devkitPro/citro3d/master/`, plus
`/opt/devkitpro/libctru/include/c3d/maths.h`.

### 3.1 World axes

Clean-room from `RESEARCH-classification.md` §2 ("Render space"), unchanged:

| axis | direction | unit |
|---|---|---|
| **x** | east (map x, growing right) | 1 tile |
| **y** | **up** | 1 storey (== the 16-px height of one row of metatile art) |
| **z** | **south** (map y, growing down-screen) | 1 tile |

Tile `(mx, my)` of map instance with origin `(ox, oy)` occupies
`[ox+mx, ox+mx+1] x [oy+my, oy+my+1]` in x/z, ground at `y = 0`. `(x east, y up, z south)` is a
**right-handed** basis: with the camera looking north (-z), +z is toward the viewer, which is the
textbook RH arrangement. That fact selects `isLeftHanded = false` everywhere below (§3.3).

Sub-tile positions (the billboard lerp, `RESEARCH-classification.md` §5.5) are plain floats in the
same units; a sprite standing on tile `(mx, my)` is anchored at `(mx + 0.5, 0, my + 0.5)`.

### 3.2 The camera

`RESEARCH-classification.md` §7.1, transcribed with our symbol names. All of it lives in
`source/diorama.c` (pure C, host-tested); `main.c` receives a finished `DioCam` of six floats.

```
pitch   = 40 deg  (down)                     research: pleasing range 35-50
yaw     = 0       (looking north, -z)        LOAD-BEARING: billboards face south (§4b)
fovy    = 35 deg  (vertical)                 tight/telephoto -> the diorama flattening
D       = 8 + 0.2 * max(mapW, mapH), adaptive term clamped to +5   ->  D in [8, 13]
H       = tan(pitch) * D                                            ->  H in [6.713, 10.91]
eye     = (tx + sin(yaw)*D, H, tz + cos(yaw)*D) = (tx, H, tz + D)   (south of, and above, target)
at      = (tx, 0, tz)
up      = (0, 1, 0)
target: tx += (playerX - tx) * 0.15 per frame, same for tz; snapped on warp, rebased on connection
```

`D` is the **horizontal** distance; the eye-to-target distance is `D / cos(pitch) = 1.3054 D`, i.e.
10.44 .. 16.97 world units. That number drives §3.4.

Everything is precomputed once per frame: `sin/cos(yaw)`, `tan(pitch)` are compile-time constants
while yaw and pitch are fixed, so the per-frame camera cost is two lerps and one `Mtx_LookAt`.

### 3.3 The citro3d calls — exact argument order, and the trap

**`Mtx_PerspTilt` does NOT take what its header says it takes.** The header
(`c3d/maths.h:544-556`) names the parameters `fovy` and `aspect`; the implementation renames them
`fovx` and `invaspect` and explains: *"the 3DS screens are sideways, and so are these parameters
-- in fact, they are actually the fovx and the inverse of the aspect ratio"*
(`mtx_persptilt.c:3-13`). That comment describes the matrix's **internals**, not the caller
contract, and reading it as a caller contract is how you ship a 2.78x-squashed diorama. The
caller contract is settled two ways:

1. **The constant.** `#define C3D_AspectRatioTop (400.0f / 240.0f)` (`c3d/maths.h:37`) — the
   ORDINARY aspect w/h, not h/w.
2. **The official example.** `devkitPro/3ds-examples` `graphics/gpu/textured_cube/source/main.c:137`:
   `Mtx_PerspTilt(&projection, C3D_AngleFromDegrees(80.0f), C3D_AspectRatioTop, 0.01f, 1000.0f, false);`

and it is confirmed by deriving the NDC extents from the matrix itself (§3.5 below).

**R-M1 (requirement).** The two matrices, in this exact form. *(Amended 2026-09-10: the
shipped call is `Mtx_PerspStereoTilt` with the same first five arguments plus `iod`/`screen` —
see §3S.2 R-E3. At `iod == 0` it is bit-identical to the `Mtx_PerspTilt` below, so every argument
role, the `isLeftHanded = false` derivation and the near/far analysis in §3.4 are unchanged.)*

```c
C3D_Mtx proj, view, mvp;
Mtx_PerspTilt(&proj, C3D_AngleFromDegrees(DIO_FOVY_DEG),   /* 35.0f — VERTICAL fov       */
              C3D_AspectRatioTop,                          /* 400/240 = 1.6667 (c3d/maths.h:37) */
              DIO_NEAR, DIO_FAR,                           /* 2.0f, 48.0f  — §3.4        */
              false);                                      /* RIGHT-handed — §3.3.1      */
Mtx_LookAt(&view, FVec3_New(eyeX, eyeY, eyeZ),
                  FVec3_New(atX,  0.0f, atZ),
                  FVec3_New(0.0f, 1.0f, 0.0f),
                  false);                                  /* RIGHT-handed — §3.3.1      */
Mtx_Multiply(&mvp, &proj, &view);                          /* mvp = P * V  — §3.3.2      */
C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, dioMvpLoc, &mvp);
```

There is no model matrix: the pure-C emitter writes world coordinates directly, so M is the
identity and `mvp = P*V`. That removes a whole class of instancing bug at the map-connection
seams (§3.1's `(ox, oy)` is baked into the vertices by the emitter, once per map build).

**3.3.1 — `isLeftHanded = false` in BOTH calls.** Derived, not copied. `Mtx_LookAt`'s LH branch
sets `zaxis = normalize(target - position)` then `xaxis = normalize(cross(up, zaxis))`
(`mtx_lookat.c:11-15`). Feed it our RH world with `eye = (tx, H, tz+D)`, `at = (tx, 0, tz)`,
`up = +y`:

```
LH : zaxis = norm(0, -H, -D)          xaxis = cross((0,1,0), zaxis) = (-D,0,0)/|..| = WEST   -> mirrored
RH : zaxis = norm(0, +H, +D)          xaxis = cross((0,1,0), zaxis) = (+D,0,0)/|..| = EAST   -> correct
```

The LH branch would put world-east on the **left** of the screen — a mirrored world that still
looks entirely plausible in a screenshot, which is the worst possible failure mode. With
`isLeftHanded = false` the view basis is `x = east`, `y = up-and-north`, `z = south-and-up`
(backward), `cross(x,y) = z` (right-handed), and `Mtx_PerspTilt(false)` sets
`r[3].z = -1` so `w_clip = -z_view > 0` for everything in front of the camera
(`mtx_persptilt.c:34`). The example at `textured_cube/source/main.c:137,168`
(`Mtx_Translate(..., 0, 0, -2.0, ...)`, i.e. content at negative z) uses exactly this pair.

**3.3.2 — multiplication order.** `Mtx_Multiply(out, a, b)` computes
`out.r[j].c[i] = SUM_k a.r[j].c[k] * b.r[k].c[i]` (`mtx_multiply.c:17`) — plain row-major with the
vector treated as a **column**: `Mtx_MultiplyFVec4` dots `mtx->r[i]` with `v`. So `out = a x b`
and applying it gives `a*(b*v)`. **`Mtx_Multiply(&mvp, &proj, &view)`**, projection first. The
reverse order compiles, runs, and produces garbage.

### 3.4 Near, far, and DEPTH16 precision — the arithmetic

With `isLeftHanded = false`, `mtx_persptilt.c:33-35` gives, for a point at distance `t` in front of
the camera (`t = -z_view > 0`):

```
z_ndc = near*(t - far) / ((far - near) * t)              near -> -1 ,  far -> 0
```

which is the PICA's required `[-1, 0]` range. citro3d's default depth map is
`C3D_DepthMap(true, -1.0f, 0.0f)` (citro3d `source/base.c:102`, set in `C3D_Init`), so the value
actually stored is

```
depth(t) = -z_ndc = k * (far/t - 1)      with  k = near / (far - near)      depth in [0, 1]
           near -> 1.0 (NEAREST) ,  far -> 0.0 (FARTHEST)
```

**R-M2 (requirement).** `DIO_NEAR = 2.0f`, `DIO_FAR = 48.0f` (`k = 2/46 = 0.043478`).

Why those two numbers, from §3.2's geometry (D in [8,13], pitch 40 deg, fovy 35 deg):

| ray | angle below horizontal | ground hit, horizontal from eye | eye distance |
|---|---|---|---|
| frustum bottom | 40 + 17.5 = 57.5 deg | 0.535 D | **0.995 D** = 7.96 .. 12.9 |
| frustum top | 40 - 17.5 = 22.5 deg | 2.026 D | **2.193 D** = 17.5 .. 28.5 |

so the ground working range is ~8 .. 28.5, and a 4-storey wall at the very bottom edge of the
frame is still ~5.5 units out (a wall *nearer* than that sits below the frustum's bottom plane and
is not drawn). `near = 2.0` therefore has ~2.7x headroom over the closest drawable vertex, and
`far = 48` covers the deepest visible ground (28.5) plus the tallest structure behind it.

Depth resolution, one DEPTH16 LSB = `1/65536 = 1.526e-5`:

```
d(depth)/dt = -k*far/t^2 = -2.08696 / t^2       ->    dt_per_LSB = 1.526e-5 * t^2 / 2.08696
```

| t (world units) | dt per LSB |
|---|---|
| 8 | 0.00047 |
| 20 | 0.0029 |
| 30 | 0.0066 |
| 40 | 0.0117 |

**The tightest depth delta in the whole mesh recipe set is the DECAL lift of ~0.02 world units**
(`RESEARCH-classification.md` §5.4: a rug/mat quad lifted a hair above the floor to stop
z-fighting). It survives at every distance in the working range with a >1.7x margin even at t=40.
The WATER recess (~-0.1) and the LOW/LEDGE box tops (0.4) are an order of magnitude safer. **A
16-bit depth buffer is sufficient for this scene; nothing here needs `GPU_RB_DEPTH24`** — which
matters, because the screen targets are citro2d's and re-creating them at DEPTH24 would cost
another 96 KB of VRAM per top target and re-plumb `C2D_CreateScreenTarget` (§4.3).

If `near` is ever lowered (e.g. because §11/O-R2's clipping check fails on hardware), every number
in that table scales by `near/2.0` — halving `near` doubles `dt_per_LSB`, and at `near = 1.0` the
t=40 row becomes 0.0240, i.e. **worse than the 0.02 decal lift**. So `near` is not a free knob;
raising the camera or clamping structure heights is the correct response to a clipping report.

### 3.5 Depth test direction — GPU_GREATER, and the proof

The single most common porting bug on this platform, so it is derived here rather than recalled:

1. `depth(near) = 1.0`, `depth(far) = 0.0` (§3.4) — the depth axis is **inverted** relative to
   desktop OpenGL. Nearer = **larger**.
2. `C2D_TargetClear` clears with `C3D_RenderTargetClear(target, C3D_CLEAR_ALL, colour, 0)`
   (citro2d `source/base.c:299`) — clear depth **0 = farthest**, which is the correct "empty"
   value for an inverted axis.
3. citro2d's own state is `C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL)` (citro2d
   `source/base.c:152`), i.e. it too treats larger as nearer.

**R-M3 (requirement).** The diorama pass sets
`C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);` (`GPU_GREATER = 6`, `3ds/gpu/enums.h:174`;
`GPU_WRITE_ALL = 0x1F` includes `GPU_WRITE_DEPTH`, `enums.h:228-231`). `GPU_LESS` would keep the
**farthest** fragment and render the world inside-out. `GPU_GEQUAL` is also acceptable and is what
citro2d uses; `GREATER` is preferred for the opaque terrain/massing pass because coplanar
same-depth fragments then keep the FIRST one drawn, which makes the pass order-independent for
the shared edges the mesh emitter produces between adjacent quads.

**Depth clear value: 0**, and it comes for free — `C2D_TargetClear(screen, clrBg)` at the new
`render_game` line (§1.0) already clears `C3D_CLEAR_ALL`. No extra clear, no extra `C3D_FrameSplit`
beyond the one that call already performs today (`main.c:2354`, `main.c:2367`).

**The HUD hazard, and its fix.** Because our pass now **writes** depth (phase 14's tilt does not —
`main.c:1354` uses `GPU_WRITE_COLOR`, which excludes `GPU_WRITE_DEPTH`), the citro2d HUD drawn
afterwards is depth-tested against our scene. citro2d's quads sit at `z = 0` under
`Mtx_OrthoTilt(0, w, h, 0, 1, -1, true)` -> `z_ndc = -0.5` -> stored depth **0.5**. With
`GPU_GEQUAL` restored by `C2D_Prepare()`, any world pixel whose depth exceeded 0.5 would **hide the
HUD**. Depth 0.5 corresponds to `t = far/(0.5/k + 1) = 48/12.5 = 3.84` world units, which §3.4
shows nothing reaches — but that is an accident of the current near/far and camera, not an
invariant. See §6.3 for the one-line guard that makes it an invariant.

**Aspect: 400/240, always.** The diorama is top-screen only (PHASE.md bound 2), so
`C3D_AspectRatioBot` never appears. Both eyes use the same matrix (§1.4). Note this is the
LOGICAL screen size, not the framebuffer's: `C2D_CreateScreenTarget` builds the top target as
`C3D_RenderTargetCreate(GSP_SCREEN_WIDTH /*240*/, GSP_SCREEN_HEIGHT_TOP /*400*/, ...)` (citro2d
`source/base.c:286`) and `C2D_SceneSize` swaps them back when `tilt` is set (`base.c:172-177`).
The `*Tilt` matrices exist precisely to absorb that quarter turn; you never pass 240/400.

### 3.6 Winding and culling — a real invariant, shipped OFF until photographed

`RESEARCH-classification.md` §7.2 step 5 records that the reference **disables backface culling
because its quad winding is inconsistent**. That is a property of the reference's emitter, not of
the problem, and we can do better: our emitter is pure C and host-tested, so a winding invariant is
enforceable and gradeable.

**R-M4 (requirement, emitter side).** Every quad `dio_emit_quad()` writes is wound so that its four
vertices run **counter-clockwise when viewed from OUTSIDE the solid**, in the right-handed world
basis of §3.1 — i.e. `cross(v1-v0, v2-v1)` points along the face's outward normal. For a ground
quad on tile `(mx, mz)` with outward normal `+y` that is
`(mx,0,mz) -> (mx,0,mz+1) -> (mx+1,0,mz+1) -> (mx+1,0,mz)`, split into
`(0,1,2)` and `(0,2,3)`. This is the same convention the citro3d example uses (its `PZ` face
`(-.5,-.5,+.5) -> (+.5,-.5,+.5) -> (+.5,+.5,+.5)` has `cross = (0,0,+1) =` its stated normal,
`textured_cube/source/main.c:21-23`). **`test_diorama.c` must grade it**: for every emitted quad,
recompute the cross product and assert it agrees with the face's recorded outward direction. That
test is free, runs on the PC, and catches the class of bug that made the reference give up.

**R-M5 (requirement, render side).** Ship `C3D_CullFace(GPU_CULL_NONE)` and put the real mode
behind one constant:

```c
#ifndef DIO_CULL
#define DIO_CULL GPU_CULL_NONE      /* -> GPU_CULL_BACK_CCW once §10's screenshot proves it */
#endif
```

Reason, and it is phase 14's `TILT_SCISSOR` precedent verbatim (`main.c:1154-1168`: *"Do not ship
it unproven"*): whether "CCW from outside in the RH world" arrives at the rasterizer as
`GPU_CULL_BACK_CCW = 2` or `GPU_CULL_FRONT_CCW = 1` (`3ds/gpu/enums.h:305-308`) depends on a sign
chain that **no header on this machine documents end to end** — the RH view basis, `PerspTilt`'s
quarter turn, `C3D_SetViewport`'s NDC->window mapping (`citro3d source/base.c:136-146`), and the
PICA's own front-face convention in window coordinates. The orientation analysis says it should be
`GPU_CULL_BACK_CCW` (the world->view map is orientation-preserving because the view basis is RH;
the view->NDC Jacobian is `[[0,a],[-b,0]]`, `det = +ab > 0`, also preserving; the viewport scales
are both positive), but "should be" is not evidence.

The prediction is cheap to settle: §10's Littleroot screenshot with `DIO_CULL=GPU_CULL_BACK_CCW`
either looks identical to the `NONE` shot (correct) or has the south faces of every building
missing (inverted) — a one-build, one-photo experiment. **Expected saving when it lands: the
north faces of every wall column and structure, which §9 measures at ~12-18% of the vertical
fill.** Nothing about correctness depends on it, which is exactly why it must not gate the phase.


## 3S. THE PER-EYE PASS (stereoscopic 3D — v1)

*Added 2026-09-10 after the PHASE.md revision that moved stereo from "After v1" into
**What ships #6** and **invariant 5**. This section SUPERSEDES §1.4's original "same image both
eyes" rule; §1.4 is amended in place and now points here.*

The whole argument for stereo being cheap is that **the geometry already exists**. The second eye
adds: one `Mtx_PerspStereoTilt` call, one uniform upload, and the same two `C3D_DrawElements`
calls against the **same** vertex and index buffers. **Zero CPU vertex writes, zero
`GSPGPU_FlushDataCache`, zero extra texture memory.** That is the strongest possible form of phase
14's clone rule (`main.c:1338-1345`), and it is why this is a render change and not a phase.

### 3S.1 Where it goes

The right-eye block already exists and already re-enters `render_game` with the same game
(`main.c:5060-5062`):

```c
if (render_game_gate(topG, topR, clrBg))
    render_game(topG, topR, preTgt, &preTex, 400.0f, 240.0f, scaleMode[0], smooth[0], NULL, clrBg,
                tiltTop ? &tiltTR : NULL);            // <- gains the diorama's `dd` (§1.0)
```

**R-E1 (requirement).** `DioDraw` gains ONE field, `int eye` (0 = left, 1 = right), and the render
block builds two of them off the same scene:

```c
DioDraw dioL = { &dioW, /*eye*/ 0 };
DioDraw dioR = { &dioW, /*eye*/ 1 };
...
render_game(topG, top,  ..., dioTop ? &dioL : NULL);   // main.c:4818
render_game(topG, topR, ..., dioTop ? &dioR : NULL);   // main.c:5060
```

`dio_draw_scene` reads `dd->eye` to pick the sign of the interaxial and does nothing else
differently. The bottom screen never has an eye (PHASE.md bound 2).

`slider3d` and `s3dOn` are already computed above the gate (`main.c:4510-4511`:
`float slider3d = osGet3DSliderState(); bool s3dOn = slider3d > 0.03f && !menuOpen && s3dEnabled;`)
and are in scope at both call sites — no new reads, no new state.

### 3S.2 Parallel cameras + asymmetric frustum, NOT toe-in

Two constructions produce a stereo pair:

- **Toe-in**: keep both eyes' `Mtx_LookAt` aimed at the same target point and translate the eye
  positions. Simple, and **wrong on a parallax-barrier panel**: rotating each camera inward
  keystones the two images, which introduces **vertical disparity** that grows toward the frame
  corners. Vertical disparity is the single most fatiguing stereo defect and the 3DS's barrier
  makes it worse, not better.
- **Parallel cameras with a shifted (asymmetric) frustum**: translate the eyes sideways along the
  camera-right vector and shear the projection so both frustums share a convergence plane. No
  rotation, therefore **exactly zero vertical disparity anywhere in the frame**, and the
  convergence plane is an explicit parameter instead of an emergent one.

**R-E2 (requirement). Use `Mtx_PerspStereoTilt` — it IS the asymmetric-frustum construction.**
Signature (`c3d/maths.h:573`, implementation `citro3d source/maths/mtx_perspstereotilt.c`):

```c
void Mtx_PerspStereoTilt(C3D_Mtx* mtx, float fovy, float aspect, float near, float far,
                         float iod, float screen, bool isLeftHanded);
```

It differs from `Mtx_PerspTilt` in exactly two matrix entries:

```
r[1].w = iod / 2                                  <- the lateral EYE TRANSLATION (constant in clip)
r[1].z = -r[3].z * (iod / (2*screen)) / (tan(fovy/2)*aspect)   <- the FRUSTUM SHEAR
```

everything else (`r[0].y`, `r[1].x`, `r[2].z`, `r[2].w`, `r[3].z`) is byte-identical to
`Mtx_PerspTilt`. **At `iod == 0` both new entries are zero and the matrix is bit-identical to
`Mtx_PerspTilt`** — so there is no branch anywhere:

**R-E3 (requirement).** The diorama ALWAYS calls `Mtx_PerspStereoTilt`; the mono case is
`iod = 0`. `Mtx_PerspTilt` never appears in the shipped diorama path (§3.3's R-M1 is amended
accordingly — its argument roles and the `isLeftHanded = false` derivation are unchanged, they are
the first five parameters of this call).

```c
const float K   = tanf(DIO_FOVY_RAD * 0.5f) * (400.0f/240.0f);   /* = 0.52550 at fovy 35 deg */
const float Tc  = 1.3054f * cam.D;                  /* eye->player distance, §3.2            */
const float sgn = (dd->eye == 0) ? -1.0f : +1.0f;   /* c3d/maths.h:510-511: -iod = LEFT eye  */
float iod = sgn * DIO_IOD * slider3d;               /* 0 when the slider is down             */

C3D_Mtx proj, view, mvp;
Mtx_PerspStereoTilt(&proj, C3D_AngleFromDegrees(DIO_FOVY_DEG), C3D_AspectRatioTop,
                    DIO_NEAR, DIO_FAR, iod, Tc / K, false);
Mtx_LookAt(&view, eye, at, up, false);              /* IDENTICAL for both eyes — §3S.3       */
Mtx_Multiply(&mvp, &proj, &view);
C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, dioMvpLoc, &mvp);
```

### 3S.3 The convergence math, derived from the matrix

Both eyes share **one** `Mtx_LookAt` (the mono camera of §3.2, target = the player's ground tile).
The eye separation lives entirely in the projection, which is what makes the two frustums parallel.
Working through `mtx_perspstereotilt.c` with `isLeftHanded = false` (`r[3].z = -1`, `w_clip = t`,
`t` = distance in front of the camera), writing `FT = tan(fovy/2)`, `ia = aspect`, `K = FT*ia`:

```
ndc.y(I) = -(x_view/t)/K  +  I * [ 1/(2t) - 1/(2*screen*K) ]

total inter-eye disparity, passing I = -iod (left) and I = +iod (right):
    d_ndc(t) = iod * [ 1/t - 1/(screen*K) ]
    ZERO at   t = screen * K            <- THE CONVERGENCE DISTANCE
```

Two consequences the implementer must not skip:

1. **`screen` is NOT the convergence distance in world units — `screen * K` is.** To converge on
   the player's ground tile at `Tc`, pass **`screen = Tc / K`**. Likewise the world-space eye
   separation the matrix actually realises is **`B = iod * K`**, not `iod`. This was derived from
   the matrix source and then checked numerically (table below); the header's prose
   (`c3d/maths.h:512-515`) reads as if both were plain world units, and the official example
   (`3ds-examples graphics/gpu/lenny/source/main.c:83,85`: `screen = 2.0f`, object at `z = -3.0f`)
   does not discriminate between the two readings because it puts the object behind the screen
   plane either way. **This is open question O-R5** — it is settled by one screenshot measurement
   (§3S.6), and getting it wrong changes only the STRENGTH of the effect, never its correctness,
   because both parameters carry the same `K`.
2. NDC y spans the screen's 400-px axis (§3.3/§3.5), so **1 NDC unit = 200 device px** and

```
d_px(t) = 200 * iod * [ 1/t - 1/Tc ]        (negative = INTO the screen, positive = out of it)
```

Numeric check of the derivation, computed directly from the matrix entries
(`fovy 35 deg`, `aspect 400/240`, `near 2`, `far 48`, `iod = +/-0.05`, `screen = 10` -> predicted
convergence `10*0.52550 = 5.255`):

| t | 4.0 | 8.0 | 10.0 | 13.0 | 20.0 | 40.0 |
|---|---|---|---|---|---|---|
| inter-eye px | **+0.60** | -0.65 | -0.90 | -1.13 | -1.40 | -1.65 |

the sign flips between t = 4 and t = 8, bracketing the predicted 5.255. A `screen = 13` run flips
between 4 and 8 as well, bracketing its predicted 6.83 — the disparity is exactly `1/t`-shaped
about `screen*K`, as derived.

### 3S.4 The interaxial — a starting value, from this app's own comfort budget

The budget is not invented: `main.c:781-797` already carries a measured, shipped one.

| constant | value | meaning |
|---|---|---|
| `POP_DISP_MAX` | **6.5f** | *"hard comfort ceiling on any element's forward disparity (px @ full slider)"* (`main.c:795`) |
| `UIPOP3D_PX` | 5.0f | BG0 dialog/menu panels — the strongest pop shipped (`main.c:796`) |
| `POP3D_RAMP_PX` | 3.6f | ground ramp at the bottom edge (`main.c:788`) |
| `POP3D_STANDUP` | 3.0f | how far a sprite's head pops beyond its feet (`main.c:789`) |

**Units: device pixels on the 400x240 top panel, per eye.** Proven at `draw_pop_tex`
(`main.c:...`, the body quoted): the shift is applied as
`C2D_DrawImageAt(img, ox + gx*sx + xoff, ...)` — added **after** the `sx` scale — and the function
itself says so (`float xs = xoff / sx;  // shift expressed in GBA pixels`). Each eye is drawn with
`+slider3d` / `-slider3d` (`main.c:4823`, `main.c:5065`), so the shipped **total inter-eye** ceiling
is `2 x 6.5 = 13 device px = 3.25% of screen width` at full slider.

**R-E4 (requirement). `DIO_IOD = 0.8f`, targeting a total inter-eye disparity of ~8 px (2.0% of
screen width) at the far edge of the visible ground, at full slider.**

Derivation, at a mid-size town (`D = 10`, so `Tc = 13.05`, §3.2/§3.4):

| what | t | `d_px = 200*0.8*[1/t - 1/13.05]` |
|---|---|---|
| near edge of visible ground (`0.995 D`) | 9.95 | **+3.8 px** out of the screen |
| the player's own tile (convergence) | 13.05 | **0.0** |
| far edge of visible ground (`2.193 D`) | 21.9 | **-5.0 px** into the screen |
| a tall structure at the far working limit | 40.0 | **-8.3 px** into the screen |

Why **below** the shipped 6.5-per-eye ceiling rather than at it: the flat path pops a handful of
sprite rectangles out of an otherwise zero-parallax image, so 6.5 px appears on a few small
objects. The diorama gives **every pixel** a real depth, so the same number would be applied
everywhere at once — and continuous full-frame parallax at 3.25% of screen width on a parallax
barrier is where crosstalk (ghosting) starts to read. 2.0% is the conservative first ship;
`DIO_IOD` is one constant and §10's screenshot pair is how it gets tuned.

Sanity check on the realised rig: world interaxial `B = DIO_IOD * K = 0.8 * 0.5255 = 0.42 tiles`
= 6.7 GBA pixels. A **miniature** interaxial for a scene whose "people" are 1 tile tall — which is
precisely the toy-diorama read this phase is chasing, and a useful smell test if a future tuner is
tempted to push `DIO_IOD` past ~1.5.

**Slider scaling is linear and total**: `iod = sgn * DIO_IOD * slider3d`. At `slider3d == 0` the
matrix is bit-identical to the mono one (R-E3), so the transition through zero is continuous and
needs no tween — unlike the flat path's pop passes, which are gated on `s3dOn`
(`slider3d > 0.03f`, `main.c:4511`). The diorama deliberately does **not** use that threshold: it
has nothing to switch on, so it just scales.

### 3S.5 Exclusions — both eyes, all six passes

Under `dioTop`, for **each** eye, the diorama replaces every per-eye 2D effect. §1.2 lists the
left-eye conditions; the right-eye sites are the same booleans, so **the `&& !dioTop` clauses
in §1.2 already cover both eyes** — but the right-eye call sites must be checked off explicitly
because they are 240 lines away:

| Pass | left eye | right eye | disabled by |
|---|---|---|---|
| grid warp + `pop_eye` | `main.c:4821-4826` | `main.c:5063-5067` | `popPass` (`main.c:4784`) |
| DoF bands | `main.c:4827` | `main.c:5069` | `dofPass` (`main.c:4794`) |
| bloom | `main.c:4828` | `main.c:5070` | `bloomPass` (`main.c:4795`) |
| UI pop | `main.c:4829` | `main.c:5071` | `uipop` (`main.c:4785`) |
| presence avatar | `main.c:4841-4843` | `main.c:5076-5078` | wrap in `if (!dioTop)` |
| time-of-day grade | `main.c:4844` | `main.c:5079` | `litPass` (`main.c:4797`) |

The geometry replaces them because it does their job **correctly**: `pop_eye` fakes per-sprite
depth by sliding rectangles, `warp_grid_eye` fakes scenery depth by stretching a grid, and both
exist only because the flat path has no z. With a depth-tested 3D scene the disparity comes out of
the projection, per pixel, with correct occlusion — a building in front of an NPC actually hides it
instead of being drawn at a guessed disparity.

### 3S.6 G10 — the exact minimal gate change

Today (`source/tilt.c:155`):

```c
if (in->screen == 0 && in->stereoEngaged)                 return 0;   // G10
```

`stereoEngaged` is fed `pop3d` (`main.c:4552`), i.e. *"the per-eye pop/warp passes run this
frame"*. G10 exists because those passes and the tilt cannot coexist — **not** because stereo and
3D geometry cannot. Under level 4 they not only coexist, the geometry IS the stereo.

**R-E5 (requirement). One term, one line:**

```c
if (in->screen == 0 && in->stereoEngaged && in->userLevel < TILT_LEVEL_DIO) return 0;   // G10
```

Why this is the minimal change, and why `test_tilt` TESTs 1-3 keep passing:

- The added term reads an input the ladder **already has** (`TiltGateIn.userLevel`,
  `source/tilt.h:68`). No new field, no new `main.c` fill, no new way for the render block and the
  ladder to disagree — which is invariant 5's whole point ("no second gate ladder").
- `TILT_LEVEL_DIO == TILT_LEVELS - 1 == 4`. Every case TESTs 1-3 enumerate uses
  `userLevel` in `0..3`, for which `userLevel < 4` is **always true**, so G10's contribution to
  the truth table is unchanged for levels 1-3. The tests are literally unmodified.
- The rule is still ordered *after* G5-G9, so a level-4 user in a battle, in a dialog, on an Old
  3DS, on a link or under frameskip is still gated off by the rule that owns that case. Only the
  stereo rule is exempted, and only for the one level that provides stereo itself.

**One knock-on the implementer must handle in the same edit** (it is not a truth-table change and
it is not optional): the final line of `tilt_target_level` is
`return (in->userLevel > TILT_LEVELS - 1) ? TILT_LEVELS - 1 : in->userLevel;` (`tilt.c:157`).
Bumping `TILT_LEVELS` 4 -> 5 changes what an **out-of-range** `userLevel` clamps to, from 3 to 4.
Any assertion that spells that constant as a literal `3` must be respelled `TILT_LEVELS - 1`, which
is what it always meant. `settings_load`'s `((unsigned)s.tilt) % TILT_LEVELS` (`main.c:2483`) is
already written that way and needs no edit — an old settings file storing 0..3 still loads to
0..3, which is PHASE.md invariant 1 for every existing user.

**What does NOT change:** `gi.stereoEngaged = pop3d ? 1 : 0` (`main.c:4552`) stays as it is, G9
(bottom screen + touch) stays as it is, and levels 1-3 continue to lose to stereo exactly as
today — a tilted flat frame plus per-eye rectangle pops really is incoherent.

**Verification (§10 exit gate, added):** boot to the overworld at level 4, set the 3D slider to
maximum, and take **both** eye crops. Three things must hold, and each localises a different bug:
(i) the two images differ; (ii) a far building sits **further RIGHT in the right eye** than in the
left (positive parallax = into the screen — if it is further LEFT, negate `DIO_IOD`, one
character); (iii) the player sprite's ground contact point is at the **same** horizontal position
in both (convergence on the player — if not, `screen` is mis-scaled and O-R5's `K` factor is the
suspect). A vertical offset anywhere is a construction error, not a tuning error: the parallel
frustum cannot produce one.

## 4. TEXTURES

### 4.1 (a) The metatile atlas

**4.1.1 What it holds.** One 16x16 px cell per *used* metatile of the current map layout, composed
from the game's **live BG character VRAM + palette RAM** — not from ROM. The tileset graphics the
map needs are already decompressed and DMA'd into BG VRAM by the game itself, which is the same
insight phase 20 turned into `peersprite` (`source/peersprite.h:16-21`: *"we read the 512 bytes
their game is displaying right now"*). Consequence, and it is exactly PHASE.md bound 5: a
**neighbour map with a different tileset has no art in VRAM**, so it is drawn as untextured dark
ground in v1; ROM decompression is an "After v1" item. Nothing in the render path needs to know
that — the emitter simply emits those quads with a flat vertex colour and a degenerate UV.

Gen-3 metatile ids are 10 bits: 0..511 resolve in the primary tileset, 512..1023 in the secondary.
That split is already in this codebase at `main.c:930-933` (`attrP = (id < 512) ? cPri : cSec`), so
the atlas composer inherits a verified rule rather than inventing one.

**4.1.2 Format — RGBA5551.**

| format | enum | bytes/texel | 512x512 | 512x256 | 256x256 |
|---|---|---|---|---|---|
| `GPU_RGBA8` | `0x0` (`3ds/gpu/enums.h:65`) | 4 | 1,048,576 (1024 KiB) | 524,288 | 262,144 |
| `GPU_RGBA5551` | `0x2` (`enums.h:67`) | 2 | **524,288 (512 KiB)** | 262,144 | 131,072 |
| `GPU_RGB565` | `0x3` (`enums.h:68`) | 2 | 524,288 | 262,144 | 131,072 |

**R-T1 (requirement). `GPU_RGBA5551`.** RGBA8 is a straight 2x waste: the source is GBA BGR555, so
three quarters of an RGBA8 texel is zero-information. Between the two 16-bit formats, RGB565 is the
one this app already uses for the game frame (`main.c:670`) — but that is because
`GX_TRANSFER_FMT_RGB565` is what mGBA's own output buffer is. Here the source is a *palette entry*,
5 bits per channel, and RGB565 forces us to synthesize a sixth green bit (round? replicate the
MSB? drop it?) — a decision with no right answer that shows up as a green cast on large flat
areas. RGBA5551 is **bit-exact** with BGR555 (R5->R5, G5->G5, B5->B5), and the spare alpha bit is
free: set to 1 everywhere in v1, and available with no format change if v1.5 wants layer-0
colour-0 to be genuinely transparent for the neighbour-map case. `GX_TRANSFER_FMT_RGB5A1 = 3`
exists (`3ds/gpu/gx.h:23`), so the §4.1.6 verification probe is available too.

**4.1.3 Geometry and packing — compact slots, full-space capacity.**

`RESEARCH-classification.md` §5.1 describes the reference's atlas: 512x512, 32 cells per row,
**slot = metatile id**, so the atlas is the whole 1024-id space and composition touches every id
the map uses (the reference already marks used ids and composes only those). Our recommendation
keeps the *geometry* and changes the *indexing*:

**R-T2 (requirement).** One permanent **512x512 RGBA5551** atlas (512 KiB), 32 cells per row, 32
rows = **1024 slots** — and `slot` is a **compact index assigned in used-id order**, not the raw
metatile id. The emitter's used-id scan (which it must run anyway to size the mesh) fills a
`uint16_t dioSlotOf[1024]` table (2 KiB, `0xFFFF` = unused).

Why compact rather than id-indexed, given the memory is identical:

- **Composition cost is the dominant per-map CPU cost** (§9.4) and it is proportional to the number
  of composed cells. A 20x20 town (Littleroot, Oldale, Route 101 — `data/layouts/layouts.json`)
  has at most 400 tiles and so at most 400 distinct ids, usually far fewer; id-indexing does not
  change that (the reference also only composes used ids) but compaction additionally makes the
  composed cells **contiguous**, which halves the number of 8x8 tiled blocks touched and keeps the
  written region inside a small number of cache lines.
- **Capacity is unchanged.** 1024 slots is the entire 10-bit id space, so **no map can overflow**,
  including the 80x80 monsters (`LAYOUT_ROUTE124/126/127` and their `UNDERWATER_*` twins, 6400
  tiles each). A smaller 256x256 (256 slots, 128 KiB) atlas would fit a town and fail a route; a
  runtime size switch would fragment the linear heap. One fixed allocation, never resized.
- The UV formula is unchanged (below), because the geometry is unchanged.

**Slot -> UV.** With `S = 512`, cell 16 px, `e` the inset of §4.1.5, and remembering that this
project's texture V axis has **v = 0 at the BOTTOM** (citro2d's own subtexture at `main.c:2340`
puts the image top at `1.0f` and the bottom at `1.0f - h/dim`):

```
c  = slot & 31 ;  r = slot >> 5              /* 32 cells per row  */
x0 = 16*c      ;  y0 = 16*r                  /* texel origin      */
u0 = (x0 +      e) / 512.0f    u1 = (x0 + 16 - e) / 512.0f
v0 = 1.0f - (y0 +      e) / 512.0f           /* v0 = the cell's TOP edge    */
v1 = 1.0f - (y0 + 16 - e) / 512.0f           /* v1 = the cell's BOTTOM edge */
```

**4.1.4 Filtering.** `C3D_TexSetFilter(&dioAtlas, GPU_NEAREST, GPU_NEAREST)` and
`C3D_TexSetWrap(&dioAtlas, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE)`
(`GPU_NEAREST = 0x0`, `GPU_CLAMP_TO_EDGE = 0x0`, `3ds/gpu/enums.h:30,37`). This is pixel art at
roughly 1:1 to 3:1 magnification; `GPU_LINEAR` would both mush the art and blend **across cell
boundaries**, which in an atlas means blending one metatile into an unrelated one. Note the
deliberate divergence from phase 14, which forces `GPU_LINEAR` on the tilted frame
(`main.c:1358`, R3.2.1): that pass resamples one continuous image at non-integer rates, this one
samples a discontinuous atlas.

**4.1.5 The inset — 1/16 texel, not a half texel.**

**R-T3 (requirement).** `e = 1.0f/16.0f` texel (= `1/8192` of the 512-wide atlas).

A **half**-texel inset is the correct rule for `GPU_LINEAR`, where the filter kernel reaches
+/-0.5 texel and a cell edge would pull in the neighbour. Under `GPU_NEAREST` the sampler takes
`floor(u * 512)` and reaches nothing, so the only failure is the interpolator's float rounding
letting a boundary pixel land on `u == u1` exactly and read texel `16c+16` — a one-pixel column of
a foreign metatile along a seam. A 1/16-texel inset removes that with **zero visible distortion**
(`floor((16c + 1/16))` is still `16c`, and every one of the 16 texels still maps to the same span
of the quad), whereas a half-texel inset would compress the 16 texels of art into 15 texels of UV
span — a 6.7% stretch, applied to every tile in the world.

If anyone ever switches this atlas to `GPU_LINEAR` (they should not), the inset must become a full
half texel **and** every cell must gain a 1-texel duplicated border, which costs 18x18 cells and
drops capacity to 28x28 = 784 slots. That is the trade being declined, written down so it is not
re-discovered.

**4.1.6 Upload path — CPU-side tiled encode, not `GX_DisplayTransfer`.**

The two candidates in-tree are `upload_frame` (`main.c:703-711`: a linear staging buffer +
`C3D_SyncDisplayTransfer` with `GX_TRANSFER_OUT_TILED(1)`) and `peersprite`'s CPU encoder
(`pspr_blit_tiled` / `pspr_tex_offset`, `source/peersprite.c:436-461`).

**R-T4 (requirement). CPU-side.** Three reasons, in order of weight:

1. **Memory.** The GX path needs a full 512 KiB **linear staging buffer in addition to** the 512 KiB
   texture, because a display transfer reads linear and writes tiled — 1 MiB for a 512 KiB atlas.
   The CPU path writes the final tiled bytes directly and needs no staging at all.
2. **The composer is already pure C and already writing texel by texel.** `diorama.c` walks
   8 sub-tile entries per metatile, resolving palette + flips per 8x8 tile
   (`RESEARCH-classification.md` §5.1). Emitting each texel at `dio_tex_offset(x,y)` instead of
   `y*W+x` costs one extra shift/mask chain per texel and *removes* a whole-atlas copy. It also
   keeps every byte-level decision inside the host-testable module (PHASE.md invariant 3).
3. **`C3D_SyncDisplayTransfer` blocks** (`main.c:705`), and a map change is already the frame where
   we are spending the CPU budget (§9.4). Stalling on a 512 KiB GPU transfer on top of that is the
   opposite of what the budget wants.

**The swizzle rule** (generalised from `pspr_tex_offset`, `source/peersprite.c:436-438`, which is
hard-wired to 4 bytes/texel):

```c
/* 3DS textures: 8x8 blocks in ROW-MAJOR order; Morton (z-order) within each block. */
static int dio_morton8(int x, int y) {           /* == pspr_morton8, peersprite.c */
    return  (x & 1)       | ((y & 1) << 1)
         | ((x & 2) << 1) | ((y & 2) << 2)
         | ((x & 4) << 2) | ((y & 4) << 3);
}
/* bpp = BYTES per texel (2 for RGBA5551). W = texture width in texels. */
static long dio_tex_offset(int x, int y, int W, int bpp) {
    return (long)(((y >> 3) * (W >> 3) + (x >> 3)) * 64 + dio_morton8(x & 7, y & 7)) * bpp;
}
```

Setting `W = dim`, `bpp = 4` reproduces `pspr_tex_offset` exactly (`*256 + morton8*4`), which is
the regression that pins the two together in `test_diorama.c`.

**R-T5 (requirement). Prove the encoder on the device, once, exactly as phase 20 does.**
`pspr_verify_tiling` (`source/peersprite.h:333`) exists because *"getting it wrong yields an
upside-down or channel-swapped or scrambled sprite whose failure is only visible in a photo"*
(`peersprite.h:323-330`). The same argument applies at 16 bpp with a different element size, and
the same probe answers it: compose a small (64x64) test pattern linearly, `C3D_SyncDisplayTransfer`
it with `GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB5A1) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB5A1)
| GX_TRANSFER_OUT_TILED(1)`, re-encode the same linear buffer with `dio_tex_offset`, `memcmp`.
Mismatch -> `dioOk = false` -> level 4 degrades to level 3 (§1.3) and the run says why. This costs
8 KiB of scratch and runs once.

**Where the writes happen.** Atlas composition is a **CPU store into a texture the GPU may be
sampling**, so it obeys the phase-20 rule (PHASE.md invariant 4, `main.c:1550`): it runs **inside**
`C3D_FrameBegin/End`, where `C3D_FRAME_SYNCDRAW` has already waited for the previous frame to
finish. The bus **reads** that feed it run in the parked window. §9.4 covers spreading the cost.

### 4.2 (b) The billboard sprite sheet

**4.2.1 Cells.** `gObjectEvents` is 16 entries of 0x24 (PHASE.md "Verified BPEE symbols"), and the
player is one of them, so **16 cells** cover every object event a map can have active at once.
Each cell is 32x32 — `peersprite`'s ceiling (`PSPR_MAX_W/H = 32`, `peersprite.h:66-67`), which is
also the real ceiling for overworld object events (16-colour, non-affine, <= 32x32).

**R-T6 (requirement).** One **128x128 `GPU_RGBA5551`** sheet = **32,768 B**, laid out 4x4 cells of
32x32; cell `i` at `(32*(i & 3), 32*(i >> 2))`. Same format argument as §4.1.2, plus the alpha bit
is now load-bearing rather than spare: OBJ palette index 0 is transparent, so
`a = (index != 0)` and the alpha test (§6.2) does the cutout. `GPU_RGBA4` (`enums.h:69`) is
rejected — 4 bits per channel visibly banding a trainer sprite.

This is a **new, separate** allocation, not the phase-15/20 presence sheet
(`s_peerTex`, 128x128 `GPU_RGBA8`, `main.c:1478-1481`). Sharing it was considered and declined:
that sheet's rows 0..95 are the placeholder art and rows 96..127 the four live co-op cells
(`peersprite.h:88-92`), it is `RGBA8` because the presence draw path is citro2d, and it is
allocated lazily only when the co-op pref is on. Sixteen diorama cells do not fit in the four free
ones, and coupling the diorama's lifetime to a co-op preference is a bug waiting to happen.

**4.2.2 Decode + change key.** The decoder is `peersprite`'s, generalised from "the player" to
"any active object event" — PHASE.md's own words for slice S3. Per object per frame: read the
header fields (`gObjectEvents[i].active/spriteId`, then `gSprites[id]` oam + `animNum` +
`animCmdIndex` + the inUse/invisible bits, strides and offsets at `peersprite.h:96-108`), form the
change key, and **re-decode pixels only when the key changed** (`peersprite.h:335+`: *"Every field
comes from a HEADER read; no pixels are involved in deciding whether to read pixels"*). Steady
state is header reads only; a walking NPC bursts a ~90-read tile gather roughly seven times a
second. §9.3 multiplies that by 16.

The cell write is `pspr_blit_tiled`'s shape at 2 bytes/texel — i.e. `dio_tex_offset(x, y, 128, 2)`
— and it happens **inside the frame**, same rule as §4.1.6.

**4.2.3 Placement.** `RESEARCH-classification.md` §5.5: one upright quad per active object, base on
the ground at the tile centre `(mx + 0.5, 0, mz + 0.5)`, **16 sprite pixels = 1.0 world unit**,
quad centred horizontally, sub-tile position lerped by the step timer, alpha cutout so transparent
texels write no depth. §5.4 sizes the buffer. The quad is **axis-aligned facing south** in v1
(`RESEARCH` §5.5's own note that the reference does not yaw the quad), which is sound only because
`yaw == 0` — restated here because it is the reason §3.2 calls yaw 0 load-bearing.

### 4.3 (c) VRAM budget — every allocation, measured

`OS_VRAM_SIZE = 0x600000` = **6 MiB = 6,291,456 B** (`/opt/devkitpro/libctru/include/3ds/os.h:61`).
`C3D_RenderTargetCreate` allocates colour AND depth from VRAM (`citro3d source/renderqueue.c:282,
289-290`), and `C3D_TexInitVRAM` allocates the texture from VRAM.

| # | Allocation | Where | Dimensions / format | Bytes |
|---|---|---|---|---|
| 1 | `top` colour | `main.c:5712` -> `C2D_CreateScreenTarget` -> `C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH16)` (citro2d `base.c:286`) | 240x400 RGBA8 | 384,000 |
| 2 | `top` depth | same call | 240x400 DEPTH16 | 192,000 |
| 3 | `topR` colour | `main.c:5713` | 240x400 RGBA8 | 384,000 |
| 4 | `topR` depth | same | 240x400 DEPTH16 | 192,000 |
| 5 | `bot` colour | `main.c:5714` | 240x320 RGBA8 | 307,200 |
| 6 | `bot` depth | same | 240x320 DEPTH16 | 153,600 |
| 7 | `preTex` (sharp-bilinear prescale) | `main.c:3135` `C3D_TexInitVRAM(&preTex, 512, 512, GPU_RGBA8)` | 512x512 RGBA8 | 1,048,576 |
| 8 | `dofTexA` (HD-2D DoF bounce) | `main.c:3143` `C3D_TexInitVRAM(&dofTexA, 128, 128, GPU_RGB565)` | 128x128 RGB565 | 32,768 |
| 9 | `bloomTex` (HD-2D glow map) | `main.c:3152` `C3D_TexInitVRAM(&bloomTex, 64, 64, GPU_RGB565)` | 64x64 RGB565 | 8,192 |
| | **TODAY, total** | | | **2,702,336 (2.577 MiB)** |
| | **free headroom today** | | 6,291,456 - 2,702,336 | **3,589,120 (3.42 MiB)** |
| A | diorama metatile atlas | §4.1 | 512x512 RGBA5551 | 524,288 |
| B | diorama billboard sheet | §4.2 | 128x128 RGBA5551 | 32,768 |
| | **diorama addition** | | | **557,056 (0.53 MiB)** |
| | **free headroom after** | | | **3,032,064 (2.89 MiB)** |

Note the three render targets created *from* textures (`preTgt`, `dofTgtA`, `bloomTgt`,
`main.c:3137/3146/3155`) pass `depthFmt = -1`, so they allocate **no** depth buffer — only the
three screen targets do.

**R-T7 (requirement). Try VRAM, fall back to linear, never fail the app.**

```c
if (!C3D_TexInitVRAM(&dioAtlas, 512, 512, GPU_RGBA5551))
    if (!C3D_TexInit    (&dioAtlas, 512, 512, GPU_RGBA5551)) return;   /* dioOk stays false */
```

VRAM is preferred (the PICA's texture unit reads it at higher bandwidth than FCRAM, and §9.2's
fill estimate assumes it), and 0.53 MiB against 3.42 MiB free is comfortable — but "comfortable"
is a calculation, and `preTex`'s 1 MiB shows how fast that changes. `C3D_TexInit` places the
texture on the **linear heap** instead, which is what `e->tex` (`main.c:670`) and the presence
sheet (`main.c:1480`) already do, and the only consequence is sampling bandwidth. Log
`vramSpaceFree()` (`3ds/allocator/vram.h:71`) at init into the diagnostics so a future phase that
adds a target can see what it took.

**Linear-heap additions** (not VRAM; sized in §5): the terrain VBO, the double-buffered billboard
VBO and the shared index buffer. For scale, the existing linear residents are the two 256x256
RGB565 game textures (131,072 each), the two GBA framebuffers, the 98,064-byte tilt vertex arena
(`main.c:1246`), the 65,536-byte presence sheet and five ~527 KB baked fonts.


## 5. VERTEX BUFFERS

### 5.1 How big can a map be — measured, not guessed

Fetched this session:
`https://raw.githubusercontent.com/pret/pokeemerald/master/data/layouts/layouts.json`
(168,542 B, **441 layouts**).

| | layout | w x h | tiles |
|---|---|---|---|
| **largest** (6-way tie) | `LAYOUT_ROUTE124`, `ROUTE126`, `ROUTE127` + their `UNDERWATER_*` twins | 80 x 80 | **6,400** |
| next | `LAYOUT_ROUTE119`, `ROUTE111`, `ROUTE111_NO_MIRAGE_TOWER` | 40 x 140 | 5,600 |
| next | `LAYOUT_BATTLE_FRONTIER_OUTSIDE_EAST` | 72 x 72 | 5,184 |
| next | `LAYOUT_ROUTE128` + `UNDERWATER_ROUTE128` | 120 x 40 | 4,800 |

Typical, and the maps this phase's exit gate names (PHASE.md invariant 8):

| layout | w x h | tiles |
|---|---|---|
| `LAYOUT_LITTLEROOT_TOWN` | 20 x 20 | 400 |
| `LAYOUT_OLDALE_TOWN` | 20 x 20 | 400 |
| `LAYOUT_ROUTE101` | 20 x 20 | 400 |
| `LAYOUT_PETALBURG_CITY` | 30 x 30 | 900 |
| `LAYOUT_ROUTE103` | 80 x 22 | 1,760 |
| `LAYOUT_RUSTBORO_CITY` / `SLATEPORT_CITY` | 40 x 60 | 2,400 |
| `LAYOUT_LITTLEROOT_TOWN_BRENDANS_HOUSE_1F` (an interior) | 11 x 9 | 99 |

Cumulative distribution over all 441 layouts — this is the number that sets the cap:

| tiles <= | layouts | share |
|---|---|---|
| 1,024 | 352 | **79.8%** |
| 2,304 | 397 | **90.0%** |
| 4,096 | 429 | **97.3%** |
| 6,400 | 441 | 100% |

Median layout area is **216** tiles; **only 12 layouts exceed 4,096 tiles**, and they are named
exhaustively in the first table.

### 5.2 Per-tile worst case, and why a naive whole-map cap is impossible

From `RESEARCH-classification.md` §5.2-§5.4, the theoretical maximum for one tile is
`ground quad + 4 sides x H levels + roof`:

```
verts/tile(H) = 4 + 16*H + 4         quads/tile(H) = 1 + 4*H + 1
H = 4 storeys  ->  72 verts/tile,  18 quads/tile
```

At 36 B/vertex (§2.2) that is **2,592 B per tile**, so the theoretical worst case for an 80x80
route is `6400 * 2592 = 16.6 MB`. That number is not a budget, it is a proof that **the cap cannot
be the theoretical worst case** — it has to be a real cap with a defined behaviour on overflow.

The theoretical maximum is also unreachable by construction, and the emitter is what makes it so:
walls are **exterior-only** (a face is skipped when its neighbour is WALL/ROOF/VOID,
§5.2.3/§5.3), structures are flood-filled so interior members contribute no walls at all (§4),
runs of the same shape fuse (§5.4's same-shape side cull), and every passable tile is exactly one
ground quad. In practice the dominant term is `1 quad/tile` of ground plus a data-dependent
massing tail.

**R-V1 (requirement). The honest number must be MEASURED, not modelled.** Slice S2 dumps a real
BPEE map to `test/fixtures/`; `test_diorama.c` must record, as graded expected values, the emitted
**quad count broken down by pass** (ground / wall-column / structure / per-tile prop / billboard)
for at least: an interior (`BRENDANS_HOUSE_1F`, 99 tiles), a town (`LITTLEROOT`, 400), and one of
the 80x80 routes. Those three numbers replace the estimate in §9 and, if they contradict the cap
below, the cap moves in the same slice.

### 5.3 The buffers — one arena, three regions

**R-V2 (requirement).** ONE `linearAlloc`, carved into three regions in this order:

| region | capacity | stride | bytes |
|---|---|---|---|
| terrain + massing vertices | `DIO_TERRAIN_QUADS = 12,288` quads = **49,152 verts** | 36 B | **1,769,472** |
| billboard vertices | `DIO_SPRITE_QUADS = 32` quads = 128 verts | 36 B | 4,608 |
| shared quad index buffer (`u16`, static) | 12,288 quads x 6 | 2 B | 147,456 |
| | | **total** | **1,921,536 B = 1.83 MiB** linear |

`12,288` quads is chosen so that at the ~3 quads/tile the recipes suggest (1 ground + a massing
tail) it covers **~4,096 tiles = 97.3% of all Emerald layouts as a whole-map build** (§5.1) — i.e.
PHASE.md bound 7's "built once per map" is literally true for all but twelve maps. Max vertex index
is 49,151, comfortably inside `u16`.

**One `linearAlloc`, not three, and the order is load-bearing.** `C3D_DrawElements` begins with

```c
u32 pa = osConvertVirtToPhys(indices);
u32 base = ctx->bufInfo.base_paddr;
if (pa < base) return;                    // citro3d source/base.c -> drawElements.c:6-8
```

— if the index buffer's physical address is **below** the vertex buffer's, the draw is **silently
skipped** and the screen is black with no error anywhere. Carving all three regions out of one
allocation with the indices last makes `phys(ibo) > phys(vbo)` true by construction, for both the
terrain and the billboard draw. (The existing code gets away with three separate `linearAlloc`s at
`main.c:1108-1110` because that allocator happens to return increasing addresses, and
`C3D_DrawElements(GPU_TRIANGLES, WARP_IDX, C3D_UNSIGNED_SHORT, warpIbo)` at `main.c:1372` has
worked ever since — but "happens to" is not a contract.) A stride of 36 B is a multiple of 4, so
the index region stays 4-byte aligned after the two vertex regions.

**The index buffer is static and built once**, at `dio_gpu_init` (§2.4), then cache-flushed once
— exactly `warp_grid_init`'s shape (`main.c:1113-1122`):

```c
for (int q = 0; q < DIO_TERRAIN_QUADS; q++) {
    u16 b = (u16)(4*q);
    *p++ = b; *p++ = b+1; *p++ = b+2;
    *p++ = b; *p++ = b+2; *p++ = b+3;      /* the §3.6 CCW-from-outside order */
}
GSPGPU_FlushDataCache(dioIbo, DIO_TERRAIN_QUADS * 6 * sizeof(u16));
```

The same index buffer serves the billboard draw: indices are relative to whatever `BufInfo_Add`
set as the buffer base, and each draw re-runs `BufInfo_Init`/`BufInfo_Add` with its own vertex
region (§2.2). Only the first `DIO_SPRITE_QUADS*6 = 192` indices are used there.

### 5.4 `C3D_DrawElements`, not `C3D_DrawArrays` — and the count limit

**R-V3 (requirement).** Indexed draws. Four vertices per quad instead of six is a **33% saving in
both vertex memory and vertex-shader invocations**, and the PICA has a post-transform vertex cache
(citro3d clears it explicitly after every element draw: `GPUCMD_AddWrite(GPUREG_VTX_FUNC, 1)`,
`drawElements.c:39`), so the two shared vertices of the second triangle are transformed once.
`C3D_DrawArrays` remains correct for anything without shared vertices; nothing here qualifies.

**R-V4 (requirement). Chunk every draw at 65,532 indices (10,922 quads).** citro3d issues the
count as a single register write, `GPUCMD_AddWrite(GPUREG_NUMVERTICES, count)`
(`drawElements.c:19`), and applies **no clamp of its own**. Whether that register is 32-bit or
narrower is not documented in any header on this machine, and a 12,288-quad terrain draw needs
73,728 indices — past 16 bits. The mitigation is four lines and costs one extra command per chunk:

```c
static void dio_draw_quads(int firstQuad, int quadCount) {
    const int MAXQ = 10922;                       /* 65,532 indices = 6 * 10,922 */
    while (quadCount > 0) {
        int n = quadCount > MAXQ ? MAXQ : quadCount;
        C3D_DrawElements(GPU_TRIANGLES, n * 6, C3D_UNSIGNED_SHORT, dioIbo + (size_t)firstQuad * 6);
        firstQuad += n; quadCount -= n;
    }
}
```

The vertex indices in the buffer are absolute, so advancing the `indices` pointer alone is correct.
This is **open question O-R4** — one Azahar screenshot with `MAXQ` raised to 20,000 on a large map
either renders identically (the register is wide) or shows a wrapped/truncated mesh.

### 5.5 When each buffer is written, and the SYNCDRAW rule

`C3D_FrameBegin(C3D_FRAME_SYNCDRAW)` (`main.c:4507`) calls `C3D_FrameSync()` and then
`C3Di_WaitAndClearQueue(-1)` (citro3d `source/renderqueue.c:163-177`), so **when `C3D_FrameBegin`
returns, the previous frame's GPU work is finished**. That is the guarantee the phase-20 rule at
`main.c:1550` rests on, and it is what makes CPU writes into GPU-read buffers legal *inside* the
frame. The remaining hazard is entirely **intra-frame**: never rewrite a buffer that a draw already
queued **this frame** may still read.

| buffer | written when | rule |
|---|---|---|
| index buffer | once, at init | static; flushed once |
| terrain VBO | **only** on map change / live-grid change (§5.6) | written **before** the first diorama draw of that frame; never again that frame |
| billboard VBO | once per frame, in `dio_frame_update` | written **before** the left-eye draw; **not** touched between the eyes |

**R-V5 (requirement).** `dio_frame_update()` (the one function that writes vertices) runs **before
`render_game(topG, top, ...)`** — i.e. between the gate/tween block and `main.c:4818` — and is
called exactly once per frame. After it returns, both eye draws read read-only.

**On double-buffering the billboard VBO: not needed, and here is the argument rather than the
habit.** Cross-frame double-buffering buys nothing, because `C3D_FRAME_SYNCDRAW` has already waited
(above). Intra-frame double-buffering would buy something only if the buffer had to change
**between** the two eye draws — and under §3S it does not: the stereo pair differs only in the MVP
uniform, not in a single vertex. Phase 14 needed three slabs (`TILT_IMAGES 3`, `main.c:1181`) for
the opposite reason: its three game images have genuinely different vertex data (different tint,
different screen rect), all queued inside one frame.

**Double-buffering becomes mandatory the moment either of these lands**, and this is the trigger
list, not a maybe: (i) `C3D_FRAME_SYNCDRAW` is dropped for a non-blocking frame loop, or (ii) a
future phase yaws the billboards **per eye** (a real camera-facing billboard would differ between
the two eyes by the interaxial angle, §3S). Cost when it does: one more 4,608-byte region and an
`eye`-indexed base pointer.

`GSPGPU_FlushDataCache` is called **once per write, over only the bytes written** — the phase-14
rule at `main.c:1334` (*"R3.5.2: ONLY the bytes written"*):

```c
GSPGPU_FlushDataCache(dioTerrainVbo, (size_t)usedTerrainQuads * 4 * sizeof(DioVert));  /* map change only */
GSPGPU_FlushDataCache(dioSpriteVbo,  (size_t)usedSpriteQuads  * 4 * sizeof(DioVert));  /* once per frame  */
```

So a steady walking frame issues **one** `GSPGPU_FlushDataCache` IPC (§9.3), and a map-change frame
issues two.

### 5.6 Rebuild policy, overflow, and the window fallback

**R-V6 (requirement). The terrain VBO is rebuilt only when the world it describes changes**, which
is PHASE.md bound 7 verbatim:

1. the map layout pointer changed (`MapHeader+0`, the value `metatile_layer` already caches at
   `main.c:920-926`) — a warp or a walk-through connection;
2. the live grid changed inside the map (a door animation, a cut tree) — detection cadence is
   PHASE.md's open question O3 and belongs to SPEC-data, not here. The render side needs only a
   boolean and must tolerate it firing on any frame.

Never per frame, never on camera motion.

**R-V7 (requirement). Overflow is a defined state, not a crash.** The builder:

1. computes `groundQuads = (W*H) + neighbour ring` **before emitting anything**;
2. if `groundQuads * DIO_QUAD_HEADROOM > DIO_TERRAIN_QUADS` (`DIO_QUAD_HEADROOM = 3`), switches to
   **window mode** — a `48 x 48`-tile region centred on the camera target (2,304 tiles), rebuilt
   when the player leaves the inner `32 x 32`. A 48x48 window comfortably contains the visible
   trapezoid, which §3.4's frustum arithmetic puts at roughly 13.6 tiles wide at the near edge and
   30 at the far edge over a 19.4-tile depth — about **423 tiles** of ground, plus margin for the
   camera's 0.15-per-frame follow lag;
3. emits in the order **ground -> structures -> wall columns -> per-tile props**, checking the
   remaining capacity at every quad and stopping cleanly (quad-atomic — never half a primitive) if
   it somehow still fills;
4. records `truncated`, `usedQuads` and `mode` in the diorama's diagnostic block, which §7 puts in
   the gs-log column and §8 can surface on screen.

Emit order is the degradation order on purpose: losing props is invisible, losing ground is not.

The twelve maps that take the window (§5.1) are exactly the ones where a whole-map build would be
mostly invisible anyway — an 80x80 route is 4x the visible trapezoid in each axis.

### 5.7 Billboards

`DIO_SPRITE_QUADS = 32` against **16 possible object events** (`gObjectEvents`, 16 x 0x24,
PHASE.md "Verified BPEE symbols") is 2x headroom, for two named reasons: a multi-piece OAM actor
may need a second quad, and the 3-4 player co-op the user has already flagged as a future ask would
raise the object count. 32 quads = 128 verts = **4,608 B**, i.e. the headroom is free.

Geometry per billboard (`RESEARCH-classification.md` §5.5, §4.2.3 here): one upright quad, base on
the ground plane at the lerped sub-tile position, **16 sprite px = 1.0 world unit**, centred
horizontally, axis-aligned facing south (valid only because yaw is 0, §3.2). Depth-tested against
the terrain like everything else, with the alpha test doing the cutout so transparent texels write
no depth (§6.2).


## 6. TEV / STATE

The PICA200 has **no fragment shader** and **six TEV combiner stages** (`GPUREG_TEXENV0..5`;
`3ds/gpu/shbin.h:11-12` lists only `VERTEX_SHDR` and `GEOMETRY_SHDR` —
`docs/kb/hd2d-octopath-3d.md` §0.1). The diorama uses **one** stage. The other five are the budget
that pays for every "After v1" polish item (shadow composite, water tint, day/night grade), which
is worth stating so nobody spends them here.

### 6.1 Stage 0 — texture x vertex colour, both channels

```c
C3D_TexEnv* env = C3D_GetTexEnv(0);
C3D_TexEnvInit(env);
C3D_TexEnvSrc (env, C3D_RGB,   GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);
C3D_TexEnvFunc(env, C3D_RGB,   GPU_MODULATE);      /* art x baked directional shade */
C3D_TexEnvSrc (env, C3D_Alpha, GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);
C3D_TexEnvFunc(env, C3D_Alpha, GPU_MODULATE);      /* atlas/sheet alpha x vertex alpha */
C3D_TexEnvInit(C3D_GetTexEnv(1));                  /* R5.4.1: a stale stage 1 poisons the result */
```

The RGB half is phase 14's stage verbatim (`main.c:1360-1363`). The **alpha** half differs and that
difference is the whole point:

- **Terrain / massing.** The atlas is opaque (alpha bit 1 everywhere, §4.1.2) and the vertex alpha
  is 1.0, so `1 x 1 = 1` and the fragment is opaque. Phase 14 uses `GPU_REPLACE` from
  `GPU_PRIMARY_COLOR` here (`main.c:1363`) because it has no texture alpha to respect; we do.
- **Billboards.** The sheet's alpha bit is `(palette index != 0)` (§4.2.1), so `MODULATE` carries
  the cutout mask straight into the alpha test below — one stage for both passes, no state change
  between the two draws except the alpha test itself.

**The vertex colour is the lighting model.** There is no light unit in this pass and no per-pixel
shading: the "fake sun" is baked per FACE by the pure-C emitter as a constant RGB —
top 1.00, north 0.90, south 0.85, east 0.82, west 0.75
(`RESEARCH-classification.md` §5.3), and the untextured grey sides of the small extruded boxes
(front 0.60, back 0.50, left 0.65, right 0.55, §5.4). A face's four vertices carry the same
colour, so this costs the emitter one float copy and the GPU nothing. It is also why R-S1 (§2.2)
refuses to guess at integer attribute normalisation: an unnoticed 255x here is a white world.

### 6.2 Alpha test — on for billboards, off for terrain

```c
/* terrain / massing draw */
C3D_AlphaTest(false, GPU_ALWAYS, 0);
C3D_DepthTest(true,  GPU_GREATER, GPU_WRITE_ALL);          /* §3.5 */

/* billboard draw — same program, same TexEnv, two state writes */
C3D_AlphaTest(true,  GPU_GREATER, 128);
```

`C3D_AlphaTest(bool, GPU_TESTFUNC, int ref)` — `c3d/effect.h:11`; `GPU_GREATER = 6`,
`3ds/gpu/enums.h:174`.

**Why ref 128.** The sheet is `GPU_RGBA5551`, so a decoded texel's alpha is exactly 0 or 255
(§4.2.1) and the vertex alpha is 1.0 — the modulated result is 0 or 255 and **128 is the midpoint
with maximum margin either way**. It is the same 0.5 cutout the reference uses
(`RESEARCH-classification.md` §5.5: *"Alpha-cutout at 0.5 so transparent texels don't write
depth"*), expressed in the 0..255 units `C3D_AlphaTest` takes.

**Why the alpha test and not blending.** A blended billboard would write depth for its transparent
texels (the depth write happens regardless of the blend result), so a sprite would punch a
sprite-shaped hole in everything behind it. The alpha test **discards** the fragment before the
depth stage, which is exactly what a cutout billboard needs. This is also why the billboards are
drawn in the same opaque pass rather than sorted: with a hard cutout there is nothing to sort.

**Draw order: terrain first, billboards second.** Not for correctness — `GPU_GREATER` makes both
orders produce the same image — but because the terrain establishes depth, so the billboard pass
gets the maximum benefit from any early-depth rejection the hardware does.

### 6.3 What must be restored for citro2d — the complete list

The HUD, the pause menu, the toast and the bottom screen are all citro2d, drawn **after** us in the
same frame (`main.c:4846+`). `C2D_Prepare()` is the restore primitive, and reading what it actually
does (citro2d `source/base.c:119-156`) is the only way to know what it does **not**:

| citro2d assumption | restored by `C2D_Prepare`? | citation |
|---|---|---|
| its own vertex shader bound | **yes** — `C3D_BindProgram(&ctx->program)` | `base.c:126` |
| its own `AttrInfo` / `BufInfo` | **yes** — `C3D_SetAttrInfo` / `C3D_SetBufInfo` | `base.c:127-128` |
| TexEnv 0..3 (its switchable modes) | **yes, lazily** — `ctx->flags \|= C2DiF_DirtyAny` re-emits them on the next draw | `base.c:125`, `base.c:597-706` |
| TexEnv 4 (reserved no-op) | **yes** | `base.c:138-139` |
| TexEnv 5 (the fade) | **yes** | `base.c:144-149` |
| bound texture | **yes** — `ctx->curTex = NULL` forces a re-bind | `base.c:126` |
| projection / modelview | **yes** — via `C2DiF_DirtyAny` | `base.c:125` |
| `C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL)` | **yes** | `base.c:152` |
| `C3D_CullFace(GPU_CULL_NONE)` | **yes** | `base.c:155` |
| **alpha test off** | **NO** | absent from `base.c:119-156` |
| **alpha blend** | **NO** | absent — `main.c:1368` already records this ("`C2D_Prepare` does NOT") |
| **scissor** | **NO** | `main.c:1374` already records this and resets it by hand |
| **depth map** (`C3D_DepthMap`) | **NO** | set once in `C3D_Init`, citro3d `source/base.c:102` |
| **per-texture filter** (`C3D_TexSetFilter`) | **NO** — it is texture state, not context state | `main.c:1356-1360` |

**R-X1 (requirement). The tail of `dio_draw_scene`, in this exact order:**

```c
C3D_AlphaTest(false, GPU_ALWAYS, 0);                 /* MANDATORY — C2D_Prepare will not */
C3D_CullFace(GPU_CULL_NONE);                         /* belt-and-braces; C2D_Prepare also does it */
C2D_Prepare();                                       /* hand the GPU back (main.c:1376 pattern) */
C3D_DepthTest(true, GPU_ALWAYS, GPU_WRITE_ALL);      /* THE HUD GUARD — must come AFTER C2D_Prepare */
```

Three notes, each earned:

1. **The alpha test is the one that bites.** Leave it on and every citro2d fragment whose alpha is
   `<= 128` is discarded — every anti-aliased glyph edge, every soft plate shadow, every dim chip.
   The HUD would render, but *thin and crunchy*, in a way that looks like a font bug and would send
   a reviewer to `typography.h`. It is absent from `C2D_Prepare` and it is absent from every
   existing pass's restore block **because no existing pass turns it on** — the diorama is the
   first.

2. **The depth guard, and why it comes after.** Our pass **writes depth** (`GPU_WRITE_ALL`), unlike
   phase 14's tilt which uses `GPU_WRITE_COLOR` (`main.c:1354`) and therefore leaves the buffer at
   the clear value. `C2D_Prepare` restores `GPU_GEQUAL`, and citro2d's quads sit at stored depth
   **0.5** (§3.5), so any world pixel nearer than `t = 3.84` world units would hide the HUD behind
   it. §3.4's frustum arithmetic says nothing gets that close — **but that is a consequence of the
   current `near`/`far`/pitch, not an invariant**, and the failure mode (a HUD chip vanishing over
   a tall foreground building) is intermittent and map-dependent, which is the worst kind of bug to
   chase. `GPU_ALWAYS` costs one register write, is strictly more permissive than `GPU_GEQUAL`, and
   preserves the property citro2d actually wants from its depth test — *"overwrite pixels with the
   same depth (needed to draw overlapping sprites)"* (`base.c:151`) — because a later draw always
   wins under `ALWAYS` too. It **must** be set after `C2D_Prepare`, which would otherwise overwrite
   it.
   *Scope:* the override lasts until the next `C2D_Prepare`. Under the diorama the five raw passes
   that call it are all excluded (§1.2), so it survives the top-screen HUD, and the bottom screen's
   own `C2D_TargetClear` gives it a fresh cleared depth buffer either way (targets do not share
   depth attachments — citro2d `base.c:286` allocates one per target).

3. **Blend: do not touch it.** citro2d's standard
   `GPU_SRC_ALPHA / GPU_ONE_MINUS_SRC_ALPHA` blend is live (restored by hand at `main.c:1081` and
   `main.c:2300` by the passes that change it) and with our alpha always 1.0 after the cutout it is
   an opaque overwrite — exactly what an opaque pass wants. Phase 14 reaches the same conclusion for
   the same reason (`main.c:1366-1371`). Changing and restoring it would be two register writes
   that buy nothing.

**Not needed here** (listed so a reviewer can confirm the absence is deliberate): `C3D_SetScissor`
(we draw the full viewport — phase 14's `TILT_SCISSOR` argument at `main.c:1154-1168` is
stereo-specific and moot now that the diorama IS the stereo, §3S), `C3D_DepthMap` (§3.4 keeps the
default), `C3D_FragOpMode`, `C3D_EarlyDepthTest`, `C3D_ColorLogicOp`, `C3D_StencilTest`,
`C3D_TexSetFilter` on any texture citro2d owns (the atlas and sheet are ours alone, §4).


## 7. THE CONTROL

### 7.1 The row as it ships today

```c
static const char* const TILT_NAMES[TILT_LEVELS] = { "Off", "Low", "Mid", "Max" };   // main.c:2567
...
{PK_SEG, ACT_TILT, 4,  140, 200, 170, 26, "DIORAMA · TILT", OV_SECTION_TIGHT}         // main.c:2644
```

so `x = 140`, `y = 200`, **`w = 170`**, `h = 26`, `nseg = 4`, on the ENHANCE tab (`PT_ENHANCE`).
The renderer is `assets_seg` (`source/assets.c:292-313`), reached from the pause menu
(`main.c:5231-5244`) and from the standalone ZR settings screen (`main.c:5560`):

```c
float ow = w / (float)n;                                                   /* cell width          */
ui_fill(x, y, w, h, g_art.panel, ui_seg_radius(h));                        /* the track           */
ui_fill(x + active*ow + 2, y + 2, ow - 4, h - 4, g_ui.acc, ...);           /* the ACTIVE PILL     */
for (i) assets_text_c(buf, TXT_SEG, opts[i], x + i*ow + ow/2,
                      typo_center_y(TXT_SEG, y, h), i == active ? inkA : dim);
```

Touch hit-testing is `ui_seg_hit(c->x, c->w, c->nseg, px)` (`source/ui.c:132`, called at
`main.c:4101` and `main.c:5467`) — a pure `w / nseg` division, so it follows `nseg` with no edit.

### 7.2 Does a fifth rung fit? — measured against the shipped `.bcfnt`

The UI-redesign rule is *measure with the real metrics*, so these are real metrics, not estimates.
`TXT_SEG` is an alias of `TXT_BODY` since phase 19 (`source/typography.h:110`) and resolves to the
baked face **`sg_med_15`**: `px = 15.0`, `cellH = 18`, `lineFeed = 15`, `inkTop = 5`, `inkH = 12`
(`typography.h:143-144`). On-screen advance per glyph is
`charWidth x textScale x drawScale` where `textScale = 30/cellH` (citro2d `source/text.c:74,195`,
`C2D_TextGetDimensions` at `text.c:237-240`) and `drawScale = px*cellH/(lineFeed*30)`
(`typo_draw_scale`, `typography.h:186-188`) — the two collapse to `px/lineFeed = 1.0`, which is
phase 18's "texel scale 1.0" property. Measured through `tools/fontlab/bcfnt.py` against
**`data/fnt_sg_med_15.bin`**, the shipped file:

| label | width (device px) |
|---|---|
| `Off` | 18.0 |
| `Low` | **24.0** |
| `Mid` | 21.0 |
| `Max` | **24.0** |
| `3D` | **15.0** |

| | today (`nseg 4`) | proposed (`nseg 5`) |
|---|---|---|
| cell width `ow = 170/n` | 42.50 | **34.00** |
| active pill `ow - 4` | 38.50 | **30.00** |
| widest label | 24.0 (`Low`/`Max`) | 24.0 (`Low`/`Max`) |
| **clearance inside the pill, per side** | 7.25 px | **3.00 px** |
| worst adjacent-label gap `ow - (wA+wB)/2` | 20.0 px | **11.50 px** (`Low`\|`Mid` and `Mid`\|`Max`) |
| narrowest touch cell | 42.5 px | **34.0 px** |

**Verdict: five rungs FIT. PHASE.md's default choice stands and open question O2 is answered
"yes".** The three numbers that decide it:

- **3.00 px of pill clearance per side.** Tight, but positive and symmetric, and the pill is a
  filled accent rectangle behind an inked label — there is no border to crowd. For scale, the
  design's own chips ship with 6 px of padding per side at a *smaller* cap height
  (`ui_chip_measure` adds 12 px total, `source/ui.c:86-90`).
- **11.5 px between adjacent labels.** Nearly the width of a whole glyph; the rungs read as
  separate words, which is the failure the measurement was for.
- **34 px touch cells.** The narrowest segmented cell in the app (the next narrowest is `ACT_HUD`
  at `208/4 = 52`), but comfortably above the smallest interactive target already shipping — the
  volume `-`/`+` buttons are **20 px** wide (`main.c:5249-5252`). D-pad adjustment (`adj`) works on
  the row regardless.

**Vertical geometry is untouched**: `typo_center_y(TXT_SEG, 200, 26) = 200 + (26-12)/2 - 5 = 202`,
ink rows 207..219 inside a row spanning 200..226 — 7 px clear above and below, and it does not
depend on `n`. The row does not move, no other row moves, `contentH` stays 226 and `maxScroll`
stays 0 (`main.c:2635-2643`). **No plate art changes** (phase 14's open question O6 stays open).

**Label choice: `"3D"`.** Shortest of the candidates measured (`3D` 15.0, `Dio` 18.0, `Full` 19.0,
`3-D` 20.0), and it is PHASE.md's own word for the rung. Now that stereo ships with it (§3S) the
name is doubly accurate.

### 7.3 The exact edit list — ten sites, no new files, no settings field

| # | File:line | Change |
|---|---|---|
| 1 | `source/tilt.h:40` | `TILT_LEVELS 4 -> 5`; add `#define TILT_LEVEL_DIO (TILT_LEVELS - 1)` |
| 2 | `source/tilt.c:18` | `TILT_ANGLE_DEG[] = { 0.0f, 10.0f, 15.0f, 20.0f, 0.0f }` |
| 3 | `source/tilt.c:155` | G10 gains `&& in->userLevel < TILT_LEVEL_DIO` (§3S.6 R-E5) |
| 4 | `source/tilt.{c,h}` | new `tilt_angle_deg_for_level_screen(level, screen)` (§1.1 R-P3) |
| 5 | `source/main.c:2567` | `TILT_NAMES[] = { "Off", "Low", "Mid", "Max", "3D" }` |
| 6 | `source/main.c:2644` | `PK_SEG, ACT_TILT, **5**, 140,200,170,26, ...` |
| 7 | `source/main.c:4913` | `TILT_CHIP[] = { "TILT","TILT1","TILT2","TILT3","DIO3D" }` (§7.5) |
| 8 | `source/main.c:4571` | tween call uses the screen-aware angle (#4) |
| 9 | `source/main.c:4534` | `gi.userLevel` degrades level 4 -> 3 when `!dioOk` (§1.3 R-P4) |
| 10 | `source/gamestate.h:712` + `gamestate.c:1338` | the `0..3` comments become `0..4` (§7.6) |

**Nothing else.** In particular:

- **`Settings` does not grow.** `Settings.tilt` (`main.c:2407`) already stores the level, and the
  four `_Static_assert`s pinning the struct layout (`main.c:2412-2417`) are untouched because no
  field is added or moved.
- **The clamp already handles it.** `settings_load` does
  `g_prefs.tiltLevel = ((unsigned)s.tilt) % TILT_LEVELS;` (`main.c:2483`) — written against the
  constant, not a literal. An **old settings file** storing 0..3 loads to 0..3 unchanged, which is
  PHASE.md invariant 1 for every existing user. **Downgrade** is safe too, and worth writing down:
  a file written by a level-4 build and read by an older `TILT_LEVELS == 4` binary yields
  `4 % 4 = 0` = Off — flat, never corrupt, never out of range.
- **`run_settings` needs no edit at all** (parity is automatic). Its three `ACT_TILT` sites read
  and write `g_prefs.tiltLevel` (`main.c:5489`, `main.c:5495`), cycle with
  `(cur + adj + ns) % ns` where `ns = PT[row].nseg` (`main.c:5485,5491`), and label with
  `TILT_NAMES` (`main.c:5560`) — all three follow #5 and #6 automatically. The ENHANCE tab is
  already in its tab list (`TABS[SET_TABS] = { 1, 2, 3, 4, 5 }`, `main.c:5416`).
- The pause menu's own `ACT_TILT` cases (`main.c:4124`, `main.c:4131`, `main.c:4137-4139`) follow
  for the same reason, including the confirmation toast that prints `TILT_NAMES[cur]`.

### 7.4 Level 4 through the gate and into the render block

`tilt_target_level` returns 4 when the user is at 4 and every rule passes — the final line
`return (in->userLevel > TILT_LEVELS - 1) ? TILT_LEVELS - 1 : in->userLevel;` (`tilt.c:157`) now
clamps to 4 instead of 3, which is the one knock-on §3S.6 flags for the tests.

The tween is then fed a **screen-dependent** angle (#4, §1.1 R-P3):

| screen | level 4 target angle | resulting state |
|---|---|---|
| top (0) | `TILT_ANGLE_DEG[4] = 0.0` | `ang -> 0` -> `dioTop` true, `tiltTop` false -> **the diorama** |
| bottom (1) | `TILT_ANGLE_DEG[3] = 20.0` | `tilt_active` true -> **level-3 behaviour**, PHASE.md bound 3 |

and G9 (`screen == 1 && touchActive`, `tilt.c:154`) still pins the bottom flat whenever any touch
mode is live, so the hardware-validated touch mapping stays bit-identical (PHASE.md invariant 2).

### 7.5 The HUD chip

```c
static const char* const TILT_CHIP[TILT_LEVELS] = { "TILT", "TILT1", "TILT2", "TILT3", "DIO3D" };
```

Measured against the shipped chip face `data/fnt_jbm_med_12.bin` (`TXT_CHIP` = `TXT_SECTION` =
`jbm_med_12`, `typography.h:146`): **`"DIO3D"` is 25.00 px, exactly the same as `"TILT1"`,
`"TILT2"` and `"TILT3"`.** The chip is laid out right-to-left off its own
`ui_chip_measure` (`main.c:4915-4918`, which adds 12 px of padding), so a same-width string
**shifts nothing** — the co-op chip to its left and the `3D` chip to its right keep their positions
to the pixel. (`"DIO"` at 15.0 and `"DIORAMA"` at 35.0 were the alternatives; the first is
ambiguous next to the existing stereoscopic `3D` chip, the second moves the row.)

**The colour rule is phase 14's, unchanged in form** (`main.c:4917`): accent when the diorama is
actually on screen this frame, dim when the setting is on but the gate is shut. Concretely
`ui_chip(txtBuf, tc, rx, HUD_CHIP_Y, (g_prefs.tiltLevel == TILT_LEVEL_DIO ? dioTop : tilt_active(&tiltTw[0])) ? hudAcc : THEME_ON_DARK_DIM)`.
This preserves the property phase 14 built the chip for (`main.c:4903-4906`): *"A photo of a tilted
screen with a DIM chip is self-contradictory and localises a gate bug immediately."* For level 4 the
same photo test reads: a diorama on screen with a dim `DIO3D` chip means `dioTop` and the draw
disagree — i.e. `dio_scene_ready()` (§1.1) or the degrade path (§1.3).

Note the existing stereoscopic `3D` chip (`main.c:4894-4898`, drawn when `s3dEnabled`) stays exactly
as it is. With stereo now part of v1 (§3S) the two chips together read correctly: `3D` = the slider
is enabled, `DIO3D` = the world is real geometry.

### 7.6 The gs-log column

`d_tiltLvl` already exists end to end: `DepthSnap.tiltLvl` (`source/gamestate.h:712`), stamped at
`main.c:3858` (`gd.tiltLvl = (uint8_t)tiltTw[0].level;`), emitted in the CSV header at
`gamestate.c:1348` and explained at `gamestate.c:1338`.

**R-C1 (requirement).** Two comment edits and no code change: `gamestate.h:712`'s
*"0..3"* becomes *"0..4"*, and `gamestate.c:1338`'s legend gains the fifth rung —

> `d_tiltLvl` = effective CLAMPED level 0..**4** on the top screen (0 = flat; **4 = DIORAMA**, a
> real 3D scene with no tilt angle, so `d_tiltAngT` is 0 **by design** at level 4 and a
> level-4 row with a non-zero angle means the tween has not finished).

That last clause matters: today the reader is told *"A level>0 with angle 0 = the gate is shut"*,
which is **exactly inverted for level 4** and would make every correct diorama row look like a
gate bug.

**R-C2 (requirement, diagnostics).** The render side publishes three more values for the log and
for §8's overlay — `dioQuads` (emitted terrain quads), `dioSprites` (billboards drawn) and
`dioFlags` (bit 0 `truncated`, bit 1 `windowMode`, bit 2 `atlasFallbackLinear`, bit 3
`neighbourUntextured`), all from §5.6/§5.7's builder. Where they land in the CSV is
`SPEC-data`/`gamestate.c` territory; what the render side owes is that they exist, are stamped in
the same parked window as `tiltLvl`, and cost nothing when the diorama is off.


## 8. DEBUG OVERLAY
(pending)

## 9. BUDGET
(pending)

## 10. VERIFY IN AZAHAR
(pending)

## 11. OPEN QUESTIONS
(pending)
