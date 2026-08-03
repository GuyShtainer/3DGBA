# SPEC-render — Phase 14 HD-2D tilt: the GPU + math half

Authored 2026-08-03 against commit `19375eb` (baseline green). Binding on the implementer of the
tilt render path. Subordinate to `docs/phase14-tilt/PHASE.md` — its **What bounds it** and
**Invariants** win any conflict with this document.

**Sources read for every claim below** (nothing here is from memory):

| What | Where |
|---|---|
| gen1recomp teardown (the homography, billboards, view growth, filtering) | `docs/kb/external/gen1-render.md` findings 1-8 |
| PICA200 caps (no fragment shader, 6 TEV stages, pass budget) | `docs/kb/hd2d-octopath-3d.md` §0, §3 |
| The raw-C3D escape/return sequence, `calc_xform`, `render_game`, every existing pass | `source/main.c` (line cites inline) |
| citro3d matrix/scissor/attribute API | `/opt/devkitpro/libctru/include/c3d/{maths.h,base.h,attribs.h}` |
| GPU enums (filters, formats, scissor modes, texture modes) | `/opt/devkitpro/libctru/include/3ds/gpu/enums.h` |
| Shader build wiring | `Makefile` (`PICAFILES`/`HFILES_BIN`, lines ~104-116) |

Every number in the tables was computed, not estimated; the arithmetic is reproduced inline so a
reviewer can re-derive it. Everything the PC cannot settle is tagged **verify-on-hw**.

---

## R1. THE PROJECTION

### R1.1 — The model, stated plainly

The GBA frame is a **ground plane**. Take frame-space pixel coordinates `(cx, cy)`,
`cx ∈ [0,240]`, `cy ∈ [0,160]` (`GBA_W`/`GBA_H`, `main.c` frame constants). Re-express them
around the viewport centre:

```
u = cx - vw/2          // horizontal offset from centre, vw = 240
w = cy - vh/2          // vertical offset from centre, vh = 160  (w > 0 = toward the viewer)
```

Rotate the plane about the **horizontal axis through the viewport centre** by angle `a`, and view
it through a pinhole camera sitting a distance `d` in front of that centre:

```
plane point   (u, w, 0)  --rotate X by a-->  (u,  w·cos a,  w·sin a)     // +z = toward the viewer
camera        at z = d, looking down -z,  focal length = d
depth from camera:        z_cam = d - w·sin a
projection:               sx = vw/2 + d·u        / z_cam
                          sy = vh/2 + d·w·cos a  / z_cam
```

Substituting `q ≡ d / (d - w·sin a)` gives **exactly** gen1recomp's closed form
(`Tilt.groundPoint`, `src/render/Tilt.lua:120-130`, cited in `gen1-render.md` finding 1):

```
q  = d / (d - w·sin a)
sx = vw/2 + u·q
sy = vh/2 + w·cos a · q
```

**R1.1 (requirement).** `d = FOCAL · vh` with `FOCAL = 1.0` and `vh = 160` → **d = 160**, matching
`Tilt.lua:33` exactly. Do not invent a different focal length; the ladder angles in R2 are
calibrated to this one.

So the "planar homography" and the "true-3D rotated quad under a perspective camera" are the *same
object*: a plane under a pinhole IS a homography. There is nothing to approximate.

### R1.2 — `q` is strictly positive (no clipping pathologies)

`z_cam = d - w·sin a ≥ d - (vh/2)·sin a = d·(1 - 0.5·sin a) > 0` for all `a < 90°` (since
`d = vh`). Therefore `q > 0` everywhere on the frame, for every angle we could ever ship. No
vertex ever crosses the camera plane, no `w ≤ 0` clip case, no need for a near-plane guard.

**R1.2 (requirement).** The implementation may assert `q > 0` in debug builds but must not add a
runtime clamp — a clamp would silently hide a units bug.

### R1.3 — Why real clip-space `w` makes the PICA200 do for free what their fragment shader faked

gen1recomp is stuck in LÖVE's 2D API: their mesh has no `w`, so they smuggle the projective
divide through a **fragment shader** — the vertex stage premultiplies UV by a per-vertex
`VertexScale` and the fragment stage divides it back (`Renderer.lua:456-471`, `gen1-render.md`
finding 1). We have **no fragment shader at all** (`hd2d-octopath-3d.md` §0.1: `3ds/gpu/shbin.h`
enumerates only `VERTEX_SHDR` and `GEOMETRY_SHDR`). We do not need one.

A rasterizer with perspective-correct interpolation interpolates `attr/w_clip` and `1/w_clip`
**linearly in screen space** and divides at each pixel. For a plane under a pinhole, the three
quantities `u·q`, `v·q`, `q` are *affine functions of screen position* — that is the defining
property of a homography. So if we emit a per-vertex clip `w` proportional to the camera depth,
the hardware's own interpolator reconstructs the exact same map their fragment shader computes.

**The `w` we must emit is the DEPTH, not the scale**: `w_clip ∝ z_cam ∝ 1/q`. (Sanity check
against their shader: they linearly interpolate `uv·scale` and `scale` and divide — i.e. the
linear quantity is `attr·q`, i.e. `attr/w` with `w = 1/q`. Same thing.) Getting this inverted is
the single easiest way to ship a subtly-wrong tilt; R1.9 checks it numerically.

**R1.3 (requirement).** Emit `w_clip = 1/q` (call it `iq`). Never `q`.

Numeric proof that the hardware interpolation is *exact* (not approximate) — worst error over 19
interior samples per edge, and over a 66-point barycentric grid inside a triangle, computed in
double precision:

| Test | a=15° | a=25° | a=35° |
|---|---|---|---|
| edge TL→BL, source-space error | 2.8e-14 px | 4.3e-14 px | 2.8e-14 px |
| edge TL→BR (diagonal) | 5.7e-14 px | 5.7e-14 px | 2.8e-14 px |
| edge TR→BL (diagonal) | 5.7e-14 px | 3.9e-14 px | 2.8e-14 px |
| interior barycentric grid (triangle TL/BL/TR) | 8.5e-14 px | — | — |

That is float round-off. **Consequence (R1.4).**

### R1.4 — One quad or a grid: mathematically identical, so use the grid

Because `u·q`, `v·q`, `q` are affine over the *whole* plane, **any** triangulation of the quad
reproduces the identical map — subdividing introduces no seam and no error (this is stronger than
`gen1-render.md` finding 1's "ONE quad is pixel-exact"; it says *any* tessellation is
pixel-exact). We therefore keep the **existing 16×11 grid** (`WARP_COLS 15`/`WARP_ROWS 10`/
`WARP_VERTS 176`, `main.c:988-991`) instead of a 4-vertex quad, because the grid is what carries
the per-vertex stereo displacement (R4.2). With zero displacement the grid is bit-equivalent to
the quad.

**R1.4 (requirement).** Tilt geometry = the 16×11 grid at 16-px frame-space spacing, indices
reused from `warpIbo` (`main.c:1011-1018`). A 4-vertex quad is permitted only for passes that
carry no per-vertex displacement (the bloom composite quad, R4.5).

### R1.5 — The matrix: we use NEITHER `Mtx_PerspTilt` NOR a hand-built perspective matrix

The perspective lives entirely in the per-vertex `w`. The matrix stays the **orthographic
screen-pixel projection the warp pass already uses**:

```c
C3D_Mtx proj;
Mtx_OrthoTilt(&proj, 0.0f, screenW, screenH, 0.0f, 1.0f, -1.0f, true);   // main.c:1073 verbatim
C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, tiltProjLoc, &proj);                 // main.c:1074 pattern
```

(`Mtx_OrthoTilt` — `c3d/maths.h:542`; "tilted to account for the 3DS screen rotation", i.e. it
folds in the quarter-turn of the physical framebuffer.)

Why this works: an ortho matrix `P` is linear, and its last row is `(0,0,0,1)`. Feeding the
homogeneous point `(X·iq, Y·iq, 0, iq)` yields

```
clip   = P · (X·iq, Y·iq, 0, iq) = iq · [ P · (X, Y, 0, 1) ]
ndc    = clip.xy / clip.w = the ordinary flat NDC of the screen pixel (X, Y)
clip.w = iq                                     ← the real, per-vertex perspective w
```

so the **position** is whatever we computed on the CPU (in screen pixels, the space every other
pass already lives in) while the **interpolation** is fully perspective-correct. Depth:
`Mtx_OrthoTilt`'s depth-range fix maps `z=0` to `z_ndc = -0.5` for every vertex (verified
numerically in R1.9, column `z_ndc`) — inside the PICA's `[-1,0]` range, constant, and harmless
with the depth test off.

**Why `Mtx_PerspTilt` (`maths.h:544-556`) is rejected** — four concrete reasons, not taste:

1. **Its parameters are expressed in the rotated screen frame.** The header's own notes on the
   `Persp*` family (`maths.h:508-528`, 544-556) exist because the 3DS screens are sideways; the
   `fovy`/`aspect` you pass are effectively the *horizontal* fov and the *inverse* aspect. To get
   "focal = frame height" we would have to back-solve a fov from `d` **and** an aspect that then
   interacts with the per-screen `calc_xform` rect (`main.c:709-715`) — two coupled unknowns
   where the ortho path has zero.
2. **It forces a world space we never otherwise use.** Every existing pass (`pop_eye`,
   `warp_grid_eye`, `dof_bands`, `bloom_add`, `light_pass`, `touch_to_gba`) computes in *screen
   pixels*. A camera matrix would make the tilt the only subsystem in metres-from-a-camera, and
   the pure-C golden values (R6) would no longer be directly comparable to the flat path.
3. **It buys nothing.** There is no second object in the scene, no depth sorting, no near/far
   content. The only thing a projection matrix contributes is the divide — which we get from `iq`.
4. **One shared uniform.** `Mtx_OrthoTilt(0, screenW, screenH, 0, 1, -1, true)` is already the
   projection used by `warp_grid_eye` (`main.c:1073`) and `bloom_add` (`main.c:1278`), so the
   merged raw block (R3.3) sets the uniform **once** for all six draws.

**Also rejected: `Mtx_PerspStereoTilt` (`maths.h:573`) for the stereo eyes.** Our stereo is a fake
per-element displacement on a flat composite (`gen1-render.md` finding 5 / `hd2d-octopath-3d.md`
§1 row 3); a real stereo camera matrix would shift the whole ground plane *rigidly*, producing
zero relative depth between the player and the terrain — i.e. it would delete the effect we ship.

**R1.5 (requirement).** Build the matrix with `Mtx_OrthoTilt(&proj, 0, screenW, screenH, 0, 1, -1,
true)` — screenW/H = 400/240 (top) or 320/240 (bottom). No perspective matrix anywhere in this
phase.

### R1.6 — The shipped projection, including the R2 cover/anchor terms

R2 adds a cover scale `k` and a vertical anchor `yc`. The full shipped frame-space projection is:

```
s = sin a,  c = cos a,  d = 160
k  = tilt_cover(a)                       // R2.4;  k = 1 exactly at a = 0
yc = vh - k·(vh/2)·c/(1 - 0.5·s)         // bottom-anchored: near edge pinned to cy = vh
u  = cx - vw/2 ;  w = cy - vh/2
q  = d / (d - w·s)
fx = vw/2 + k·u·q
fy = yc   + k·w·c·q
```

At `a = 0`: `s=0, c=1, k=1, yc=80, q=1` → `fx = cx`, `fy = cy`, `iq = 1` — **the identity map**.
That is the algebraic form of PHASE Invariant 1: the tilt math at angle 0 is a no-op, so the
tween can pass through zero without a discontinuity.

### R1.7 — Frame space → screen space

The frame-space point is then carried to the screen by the **unmodified** shared transform
(`calc_xform`, `main.c:709-715`):

```
X = ox + fx·sx        // sx, sy, ox, oy from calc_xform(mode, screenW, screenH, ...)
Y = oy + fy·sy
```

`calc_xform` is affine, and affine ∘ homography = homography, so the composition is still exactly
a homography — the perspective-correct interpolation argument (R1.3) survives the screen fit
untouched, for all three scale modes (1:1 / Aspect-fit / Stretch).

**R1.7 (requirement).** Tilt is computed in **frame space** and composed with `calc_xform`
afterwards. Never bake the screen rect into the tilt math — that would fork the math per screen
and break the pure-C module's testability.

### R1.8 — The exact 4 vertex positions and UVs (grid corners; the 16×11 interior follows)

Source rect: the GBA frame at `(0,0)-(240,160)` inside the 256×256 tiled texture (`upload_frame`,
`main.c:603-611`), UVs `u = gx/256`, `v = 1 - gy/256` — **identical to the warp grid's UV math**
(`main.c:1065-1066`), which is also correct for `preTex` because `PRESCALE/PRE_TEX = 2/512 =
1/256` (the comment at `main.c:1065` states this invariant; `PRESCALE 2`, `PRE_TEX 512`,
`main.c:636-639`).

Corner table, shipped model (R1.6), `k = tilt_cover_fit`, bottom-anchored:

| a | corner | source (cx,cy) | UV (u,v) | frame-space (fx, fy) | q | emitted `iq = 1/q` |
|---|---|---|---|---|---|---|
| 0° | TL | (0,0) | (0.0000, 1.0000) | (0.000000, 0.000000) | 1.00000000 | 1.000000 |
| 0° | TR | (240,0) | (0.9375, 1.0000) | (240.000000, 0.000000) | 1.00000000 | 1.000000 |
| 0° | BL | (0,160) | (0.0000, 0.3750) | (0.000000, 160.000000) | 1.00000000 | 1.000000 |
| 0° | BR | (240,160) | (0.9375, 0.3750) | (240.000000, 160.000000) | 1.00000000 | 1.000000 |
| 10° | TL | (0,0) | (0.0000, 1.0000) | (8.728425, 0.000000) | 0.92011210 | 1.086823 |
| 10° | TR | (240,0) | (0.9375, 1.0000) | (231.271575, 0.000000) | 0.92011210 | 1.086823 |
| 10° | BL | (0,160) | (0.0000, 0.3750) | (-12.430812, 160.000000) | 1.09507926 | 0.913176 |
| 10° | BR | (240,160) | (0.9375, 0.3750) | (252.430812, 160.000000) | 1.09507926 | 0.913176 |
| 15° | TL | (0,0) | (0.0000, 1.0000) | (11.843810, 0.000000) | 0.88541842 | 1.129409 |
| 15° | TR | (240,0) | (0.9375, 1.0000) | (228.156190, 0.000000) | 0.88541842 | 1.129409 |
| 15° | BL | (0,160) | (0.0000, 0.3750) | (-20.310093, 160.000000) | 1.14864569 | 0.870590 |
| 15° | BR | (240,160) | (0.9375, 0.3750) | (260.310093, 160.000000) | 1.14864569 | 0.870590 |
| 20° | TL | (0,0) | (0.0000, 1.0000) | (14.136881, 0.000000) | 0.85396362 | 1.171008 |
| 20° | TR | (240,0) | (0.9375, 1.0000) | (225.863119, 0.000000) | 0.85396362 | 1.171008 |
| 20° | BL | (0,160) | (0.0000, 0.3750) | (-29.539547, 160.000000) | 1.20628727 | 0.829030 |
| 20° | BR | (240,160) | (0.9375, 0.3750) | (269.539547, 160.000000) | 1.20628727 | 0.829030 |

Centre check (`cx=120, cy=80`): `fx = 120.000000` at every angle; `fy` = 80 / 73.054073 /
69.647238 / 66.319194 at 0/10/15/20° — i.e. the image rides **+6.95 / +10.35 / +13.68 frame px
upward** as the near edge stays pinned (R2.5 discusses why that is the correct behaviour).

Note `fy` is exactly 0.000000 at the top corners and exactly 160.000000 at the bottom corners for
every angle. That is not a coincidence — it is the defining property of `tilt_cover_fit` (R2.4),
and it is the cheapest possible regression test.

### R1.9 — NUMERIC EQUIVALENCE PROOF (the correctness argument for the whole phase)

Setup: gen1recomp's `Tilt.groundPoint(cx, cy, 240, 160)` with `FOCAL=1.0` (their centre-pinned
form, `k=1`, `yc=80` — deliberately *their* exact formula, not our R1.6 variant, so the comparison
is against the source of truth) versus our full GPU path:

```
(a) reference : sy from groundPoint, then screen Y = oy + sy·1.5
(b) our path  : emit v = (X·iq, Y·iq, 0, iq) with iq = 1/q
                clip = Mtx_OrthoTilt(0, 400, 240, 0, 1, -1, true) · v
                ndc  = clip.xyz / clip.w
                undo the quarter turn, invert the ortho viewport map → screen Y
```

Top screen, Aspect-fit (`calc_xform` gives `sx = sy = 1.5`, `ox = 20`, `oy = 0`):

| a | edge | cy | groundPoint sy | → screen Y (a) | matrix Y (b) | \|ΔY\| | w_clip | z_ndc |
|---|---|---|---|---|---|---|---|---|
| 0° | top | 0 | 0.000000 | 0.000000 | 0.000000 | 0.0e+00 | 1.000000 | −0.500 |
| 0° | mid | 80 | 80.000000 | 120.000000 | 120.000000 | 0.0e+00 | 1.000000 | −0.500 |
| 0° | bot | 160 | 160.000000 | 240.000000 | 240.000000 | 0.0e+00 | 1.000000 | −0.500 |
| 15° | top | 0 | 11.580118 | 17.370177 | 17.370177 | 7.1e−15 | 1.129410 | −0.500 |
| 15° | mid | 80 | 80.000000 | 120.000000 | 120.000000 | 0.0e+00 | 1.000000 | −0.500 |
| 15° | bot | 160 | 168.760523 | 253.140785 | 253.140785 | 0.0e+00 | 0.870590 | −0.500 |
| 25° | top | 0 | 20.143584 | 30.215376 | 30.215376 | 7.1e−15 | 1.211309 | −0.500 |
| 25° | mid | 80 | 80.000000 | 120.000000 | 120.000000 | 0.0e+00 | 1.000000 | −0.500 |
| 25° | bot | 160 | 171.930344 | 257.895516 | 257.895516 | 0.0e+00 | 0.788691 | −0.500 |
| 35° | top | 0 | 29.073078 | 43.609617 | 43.609617 | 7.1e−15 | 1.286788 | −0.500 |
| 35° | mid | 80 | 80.000000 | 120.000000 | 120.000000 | 0.0e+00 | 1.000000 | −0.500 |
| 35° | bot | 160 | 171.883176 | 257.824764 | 257.824764 | 0.0e+00 | 0.713212 | −0.500 |

Same comparison on the X axis, left edge (`cx = 0`), which exercises the `u·q` term:

| a | edge | groundPoint sx | → screen X (a) | matrix X (b) | \|ΔX\| |
|---|---|---|---|---|---|
| 0° | top/mid/bot | 0.000000 | 20.000000 | 20.000000 | 3.6e−15 |
| 15° | top | 13.749789 | 40.624684 | 40.624684 | 7.1e−15 |
| 15° | bot | −17.837483 | −6.756224 | −6.756224 | 2.1e−14 |
| 25° | top | 20.933629 | 51.400443 | 51.400443 | 7.1e−15 |
| 25° | bot | −32.150868 | −28.226301 | −28.226301 | 1.4e−14 |
| 35° | top | 26.744561 | 60.116842 | 60.116842 | 7.1e−15 |
| 35° | bot | −48.252969 | −52.379454 | −52.379454 | 0.0e+00 |

**Agreement to double-precision round-off (≤6e−14 px) at every sample.** The agreement is
*algebraic*, not numerical luck: `P · (X·iq, Y·iq, 0, iq) = iq · P · (X, Y, 0, 1)`, so the NDC is
identical to the flat point by construction and the whole projective content sits in `w_clip`.
The table's job is to catch a units/sign slip (e.g. emitting `q` instead of `1/q`, which was
caught exactly this way while writing this spec — with `w_clip = q` the interpolated source
coordinate is off by up to **31 px** at 15°, a visibly wrong but plausible-looking tilt).

**Caveat on the matrix reproduction.** citro3d ships as a binary here (`/opt/devkitpro/libctru/lib`;
only headers are on disk), so column (b) was computed against a *reproduction* of
`Mtx_OrthoTilt` = quarter-turn ∘ depth-range-fix ∘ `Mtx_Ortho`, per the header's own description
(`maths.h:530-542`). The equivalence argument does **not** depend on that reproduction being
byte-exact: it needs only that the matrix is linear and its last row is `(0,0,0,1)`, which is true
of every orthographic projection. Only the `z_ndc = −0.500` column depends on the depth-fix
detail, and it is inert here (depth test off, `main.c:1089`). **verify-on-hw / verify-in-Azahar**:
a flat frame at level 0 must be pixel-identical to today's blit — that single check validates the
whole matrix path.

**R1.9 (requirement).** The host test (R6.9) must reproduce the `fy`/`q` half of this table to
1e−4 px. The clip-space half is asserted by construction (the shader is 4 `dp4`s) and is
**verify-on-hw** for the actual rasterizer.

### R1.10 — Sign convention

`a > 0` recedes the **top** of the frame (rows above centre shrink toward the horizon) and brings
the **bottom** toward the viewer. Rows above centre have `w < 0` → `q < 1`; rows below have
`w > 0` → `q > 1`. This matches gen1recomp (`gen1-render.md` finding 1) and the HD-2D read
described in PHASE.md "What ships". Negative angles are not shipped and are not tested.

---

## R2. VIEW GROWTH / COVERAGE — the hard constraint

### R2.1 — The deficit, and the three candidate answers

Tilting recedes the top edge, so at `k = 1` the projected quad no longer covers the frame rect: at
15° the top edge sits at `fy = 11.58` (a 11.6-px void band) while the bottom edge overshoots to
`fy = 168.76`. gen1recomp answers this by **rendering more world** — `Tilt.viewGrowth`
(`Tilt.lua:132-138`, `gen1-render.md` finding 4) grows the world canvas by
`base = 1/(cos a · topScale)` plus a 35 % margin, and fills the ring with the map's border block
(`TileRenderer.lua:636-670`).

**We cannot do that.** Our source is a fixed 240×160 composited frame and a second emulation pass
is unaffordable (`hd2d-octopath-3d.md` §0.2). So the choices are:

- **(a) scale-to-cover** — zoom the projected image until it covers the rect; the excess falls
  outside and is cropped.
- **(b) letterbox / void-fill** — leave `k = 1` and fill the uncovered region with something.
- **(c) hybrid** — the minimum scale that covers *most* of the rect, plus a fill for the residue.

The decision is (c), and the reason is a geometric fact gen1recomp never had to notice (R2.3).

### R2.2 — The two reference growth factors

```
topScale = 1/(1 + 0.5·sin a)        // q at the far edge  (their Tilt.lua:133, verified identical)
botScale = 1/(1 - 0.5·sin a)        // q at the near edge  (their formula has no analogue)
base     = 1/(cos a · topScale)     // gen1's centre-pinned cover growth (Tilt.lua:135)
```

| a | gen1 `base` (centre-pinned) | `kfit` (bottom-anchored, R2.3) | `kfull` (no void) | topScale | botScale |
|---|---|---|---|---|---|
| 0° | 1.0000 | 1.00000 | 1.0000 | 1.00000 | 1.00000 |
| 5° | 1.0476 | 1.00191 | 1.0436 | 0.95824 | 1.04556 |
| 8° | 1.0801 | 1.00494 | 1.0696 | 0.93494 | 1.07479 |
| 10° | 1.1036 | 1.00777 | 1.0868 | 0.92011 | 1.09508 |
| 12° | 1.1286 | 1.01129 | 1.1040 | 0.90583 | 1.11602 |
| 15° | 1.1693 | 1.01794 | 1.1294 | 0.88542 | 1.14865 |
| 18° | 1.2139 | 1.02636 | 1.1545 | 0.86617 | 1.18274 |
| 20° | 1.2462 | 1.03306 | 1.1710 | 0.85396 | 1.20629 |
| 25° | 1.3365 | 1.05411 | 1.2113 | 0.82555 | 1.26792 |
| 35° | 1.5709 | 1.12037 | 1.2868 | 0.77713 | 1.40211 |
| 50° | 2.1516 | 1.32749 | 1.3830 | 0.72305 | 1.62080 |

The `base` column reproduces `gen1-render.md` finding 4's published numbers exactly (1.17 / 1.57 /
2.15 at 15/35/50°) — a cross-check that our formulas are their formulas.

### R2.3 — THE KEY OBSERVATION: the tilt is nearly height-preserving, if you stop pinning the centre

gen1's `base(a)` is large because it holds the **viewport centre fixed** and then scales until the
receded top edge reaches the top of the window — throwing away the bottom overshoot. But the
homography expands the near edge almost exactly as much as it shrinks the far edge:

```
projected height at k = 1 :  H_p = (vh/2)·cos a·(topScale + botScale)
                                 = vh·cos a / (1 - 0.25·sin²a)
required cover if we are free to translate vertically:
      kfit(a) = vh / H_p = (1 - 0.25·sin²a) / cos a
```

At 15°: `H_p = 157.18` px — only **1.8 % short of 160**. So `kfit(15°) = 1.01794` against gen1's
`base(15°) = 1.16926`. The whole "17 % overscan at 15°" cost in PHASE.md §1 is an artifact of
pinning the centre, and it evaporates if we pin the **near edge** instead.

With `k = kfit` and `yc = vh - k·(vh/2)·cos a·botScale` (R1.6), the projected quad spans **exactly
`fy ∈ [0, 160]`** — proof: `k·(vh/2)·cos a·(topScale + botScale) = k·vh·cos a/(1-0.25 s²) = vh`.
Zero rows are lost off the top *or* the bottom, at every angle. (Confirmed numerically in R2.4:
`proj top Y` = 0.000, `proj bot Y` = 160.000 to 1e−15 for every row of the table.)

### R2.4 — Coverage numbers (the decision evidence)

Bottom-anchored fit (`k = kfit`, the shipped default). "retained" = fraction of the 240×160 source
pixels that survive inside the frame rect; "void" = fraction of the frame rect not covered by the
quad; edge figures in **GBA pixels per side**:

| a | retained | void | void px/side @ far row | crop px/side @ near row | crop px/side @ mid row | proj top fy | proj bot fy |
|---|---|---|---|---|---|---|---|
| 0° | 100.00 % | 0.00 % | 0.000 | 0.000 | 0.000 | 0.000 | 160.000 |
| 5° | 98.82 % | 0.91 % | 4.791 | 5.449 | 0.229 | 0.000 | 160.000 |
| 8° | 98.01 % | 1.30 % | 7.253 | 8.899 | 0.590 | 0.000 | 160.000 |
| 10° | **97.44 %** | 1.50 % | 8.728 | 11.264 | 0.925 | 0.000 | 160.000 |
| 12° | 96.84 % | 1.66 % | 10.073 | 13.675 | 1.340 | 0.000 | 160.000 |
| 15° | **95.88 %** | 1.82 % | 11.844 | 17.370 | 2.115 | 0.000 | 160.000 |
| 18° | 94.84 % | 1.90 % | 13.320 | 21.147 | 3.082 | 0.000 | 160.000 |
| 20° | **94.11 %** | 1.91 % | 14.137 | 23.704 | 3.840 | 0.000 | 160.000 |
| 25° | 92.09 % | 1.81 % | 15.573 | 30.215 | 6.160 | 0.000 | 160.000 |
| 35° | 87.10 % | 1.19 % | 15.520 | 43.610 | 12.892 | 0.000 | 160.000 |

Option (a) done gen1's way — centre-pinned `base(a)` scale-to-cover, zero void:

| a | k | retained |
|---|---|---|
| 10° | 1.1036 | 84.45 % |
| 15° | 1.1693 | **77.75 %** |
| 20° | 1.2462 | 71.55 % |
| 25° | 1.3365 | 65.71 % |
| 35° | 1.5709 | 54.78 % |

Option (a) done our way — bottom-anchored but scaled to `kfull = 1 + 0.5 sin a` (the factor that
makes even the far corners reach the rect edge, i.e. zero void):

| a | retained | void | crop px/side @ near row | proj top fy |
|---|---|---|---|---|
| 10° | 83.53 % | 0.000 % | 19.17 | −12.551 |
| 15° | 76.28 % | 0.000 % | 27.50 | −17.521 |
| 20° | 69.89 % | 0.000 % | 35.05 | −21.366 |
| 25° | 64.53 % | 0.000 % | 41.87 | −23.861 |

**Buying the last 1.8 % of void costs ~20 points of retained frame.** That settles it.

### R2.5 — DECISION (binding)

**R2.5.1** Ship **(c) hybrid**: bottom-anchored fit-to-height cover, `k = tilt_cover(a) =
kfit(a) + mix·(kfull(a) − kfit(a))` with **`mix = 0` by default**, and `yc` pinning the near edge
to `cy = vh` (R1.6). Vertical crop is exactly zero at every angle; the residual deficit is
**two thin wedges at the top-left/top-right of the frame rect** totalling ≤1.91 % of the frame.

**R2.5.2** The wedges are **filled by the theme background** — no new geometry. `render_game`
already does `C2D_TargetClear(screen, clrBg)` before the blit (`main.c:1330`, `main.c:1340`), and
`clrBg` is the same colour that letterboxes the frame today, so the wedge reads as the surround
narrowing with perspective (a diorama "stage", which is the intended look). Cost: zero passes,
zero VRAM.

**R2.5.3** The widened **near edge is scissored to the frame rect** (R3.6), not allowed to spill.
Rationale is stereo-specific, not aesthetic: `draw_pop_tex` already clips every pop to the frame
box precisely because content present in one eye and absent in the other at the border causes
binocular rivalry (`main.c:718-719` states this). Letting the near edge spill would reintroduce
exactly that at the frame border, on the passes we most care about.
*Measured cost of the scissor*: on the top screen at Aspect-fit, spilling to the screen edge
instead would retain 100.00 / 99.56 / 98.40 % at 10/15/20° versus 97.44 / 95.88 / 94.11 %
scissored — i.e. the scissor costs ~2.5-4 points of retention to buy a rectangular,
rivalry-free frame. **Spill is the documented fallback** if the scissor coordinate mapping
(R3.6, verify-on-hw) proves troublesome: it needs no GPU state at all.

**R2.5.4** The vertical re-anchoring moves the image **up** by `80 − yc` frame px (6.95 / 10.35 /
13.68 at 10/15/20°). This is correct and intended: it is what a camera does when it pitches down
and re-aims, it keeps the near edge — where the player sprite, the ledge in front of them and any
textbox live — permanently uncropped, and it means a gate miss (Invariant 5) can never crop a
textbox vertically. The shift is constant per angle and rides the same tween, so it never pops.

**R2.5.5 — the shipped angle ladder: OFF / 10° / 15° / 20°**, default **15°** when enabled.
Justification, straight from R2.4: retained 97.4 / 95.9 / 94.1 %, near-row side loss 11.3 / 17.4 /
23.7 px. gen1's ladder (OFF/15/35/50, `gen1-render.md` finding 4) is not defensible for us above
its first rung: at 35° we lose **43.6 px per side** at the near row — a third of the frame width,
and precisely the rows the player occupies — because they can render 2.56× more world and we
cannot. PHASE.md §1's "ship ~15 degrees as the headline" is satisfied with 15° as the default and
20° as the enthusiast rung.

**R2.5.6** `mix` is a single compile-time float (`TILT_COVER_MIX`, default `0.0f`) plumbed through
`tilt_view_init`. If hardware review finds the wedges read as a bug rather than a stage, raising
`mix` toward 1 trades retention for zero void with no code change. Do **not** expose it in the
menu in this phase.

**R2.5.7 — deferred void-fill upgrade (do not build in slice 1).** If the wedges must go without
paying `mix`: draw one extra "horizon skirt" quad per image covering the wedge region, textured
with source row 0 stretched upward and multiplied by a dark gradient via the vertex colour (the
R5 shader already carries colour). It is 4 verts in the same VBO and the same draw block, i.e.
~zero budget — but it is unproven art direction, so it is not in the shipped slice.

### R2.6 — Where the wedges actually land (honest visual note)

At 15° on the top screen, Aspect-fit: the wedges are ~17.8 screen px wide at the very top,
tapering to zero at screen Y ≈ 88 (source row ≈ 69). The HUD bar (`400×14`, `main.c:2624`) covers
only the top 14 screen px, so it hides the widest part but not the taper. **verify-on-hw**: this
is the single most likely "looks wrong" candidate in the phase and must be on the HANDOFF
checklist with a photo.

---

## R3. THE PASS + BUDGET

### R3.1 — Where tilt hooks

Tilt **replaces the final blit of `render_game`** (`main.c:1300-1344`); it is not an extra
composite on top of it. Concretely, inside `render_game`:

- `sharpBilinear == true` (`!smooth && mode != SCALE_1X && preTgt`, `main.c:1313`): pass 0 (the
  NEAREST 2× prescale into `preTgt`, `main.c:1315-1323`) runs **unchanged**; pass 1 (the LINEAR
  fit-to-screen `C2D_DrawImageAt`, `main.c:1326-1332`) is **replaced** by the tilt draw sourcing
  `preTex`. This is exactly how `warp_grid_eye` already chooses its source —
  `C3D_TexBind(0, sharpPre ? pre : &g->tex)` (`main.c:1082`) — and the UVs need no change because
  `PRESCALE/PRE_TEX == 1/256` (`main.c:1065`).
- otherwise: the direct blit (`main.c:1334-1342`) is replaced by the tilt draw sourcing `e->tex`.

**R3.1.1** The `C2D_TargetClear(screen, clrBg)` stays in both branches (it paints the letterbox
*and* the R2.5.2 wedges).

**R3.1.2** When tilt is active and `!smooth`, **force the prescale path on even for `SCALE_1X`**.
Reason: the tilt resamples at non-integer rates everywhere, so `GPU_NEAREST` on the raw texture
shimmers; a 2× NEAREST prescale followed by `GPU_LINEAR` is the project's existing answer to
exactly that (`main.c:631-635`). When `smooth`, sample `e->tex` with `GPU_LINEAR` directly (the
user asked for linear). Cost: +1 pass in the 1:1-with-tilt case only.

**R3.1.3** `render_game` gains one parameter, `const TiltView* tv` (`NULL` = flat). Its body
becomes `if (tv) { tilt_draw(...); } else { <today's code, character for character> }` — PHASE
Invariant 1.

**R3.1.4** `render_game` currently leaves the screen bound so callers can draw the HUD on it
(`main.c:1297-1299`). The tilt path must preserve that contract: it ends with `C2D_Prepare()`
followed by `C2D_SceneBegin(screen)` so every existing HUD/menu/toast draw is untouched.

### R3.2 — Filtering

**R3.2.1** The tilted draw always uses `C3D_TexSetFilter(src, GPU_LINEAR, GPU_LINEAR)` — gen1's
exact split (`Renderer.lua:514-521`, `gen1-render.md` findings 1 & 5: "warped texture = GPU_LINEAR,
flat path = GPU_NEAREST"). The flat path's filter selection (`main.c:1320`, `main.c:1338`) is not
touched.

**R3.2.2** `C3D_TexSetWrap` on `e->tex`/`preTex` is left as-is; the tilt never samples outside
`[0,240]×[0,160]` because the geometry is the frame, not a grown view. (`preTex` is already
`GPU_CLAMP_TO_EDGE`, `main.c:1637`.)

**R3.2.3** 2× supersampling of the tilted result (gen1's TODO, `gen1-render.md` finding 1) is out
of scope — it would need a new render target, which Invariant 3 forbids without numbers.

### R3.3 — ONE raw-C3D block per game image

The single most important budget decision: **all tilt-aware draws for one game image are emitted
inside one raw-C3D escape/return**, not one per effect. Today the code escapes twice per eye
(`warp_grid_eye` at `main.c:1069-1092` and `bloom_add` at `main.c:1274-1294`). Merged, tilt-on
costs **one**.

The block, in order, following the proven sequence at `main.c:1069-1092`:

```c
// ---- escape (mirrors main.c:1069-1074) ----
C2D_Flush();                                            // submit citro2d's pending batch  (1069)
C3D_FrameDrawOn(tgt);                                   // bind the target                 (1070)
C3D_BindProgram(&tiltProg);                             // our shader                      (1071)
C3D_Mtx proj; Mtx_OrthoTilt(&proj,0,W,H,0,1,-1,true);   //                                 (1073)
C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, tiltProjLoc, &proj);//                                 (1074)
C3D_AttrInfo* ai = C3D_GetAttrInfo(); AttrInfo_Init(ai);//                                 (1075-1078)
AttrInfo_AddLoader(ai, 0, GPU_FLOAT, 3);                //   v0 = (x, y, iq)
AttrInfo_AddLoader(ai, 1, GPU_FLOAT, 2);                //   v1 = (u, v)
AttrInfo_AddLoader(ai, 2, GPU_FLOAT, 4);                //   v2 = (r, g, b, a)
C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);      //                                 (1089)
C3D_CullFace(GPU_CULL_NONE);                            // trapezoid winding varies        (1090)
C3D_SetScissor(GPU_SCISSOR_NORMAL, ...frame rect...);   // R3.6
// ---- N draws, each: BufInfo_Add(slab) + TexBind + TexEnv (+ AlphaBlend) + Draw ----
// ---- return ----
C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);        // MUST: citro2d never resets it
C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,
               GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA);  // restore by hand    (main.c:1292-1293)
C2D_Prepare();                                          // hand the GPU back  (1092)
C2D_SceneBegin(tgt);                                    // restore citro2d's scene for the HUD
```

**R3.3.1 — the state that must be saved/restored, enumerated from the warp + bloom precedent:**

| State | Set by us | How it gets back to a healthy citro2d |
|---|---|---|
| Pending C2D batch | — | `C2D_Flush()` **before** we touch anything (`main.c:1069`, `1251`, `1274`) |
| Bound target | `C3D_FrameDrawOn` (1070) | `C2D_SceneBegin(tgt)` after `C2D_Prepare` |
| Shader program | `C3D_BindProgram` (1071) | `C2D_Prepare()` rebinds citro2d's (1092) |
| Vertex uniform (projection) | `C3D_FVUnifMtx4x4` (1074) | `C2D_Prepare()` / `C2D_SceneBegin` re-upload citro2d's |
| `C3D_AttrInfo` | `AttrInfo_Init` + loaders (1075-1078) | `C2D_Prepare()` |
| `C3D_BufInfo` | `BufInfo_Init/Add` (1079-1081) | `C2D_Prepare()` |
| TexEnv stage 0 | `C3D_TexEnvInit/Src/Func/Color` (1083-1087) | `C2D_Prepare()` |
| TexEnv stage 1 | `C3D_TexEnvInit(C3D_GetTexEnv(1))` (1088) — **required**, a stale stage 1 poisons the result | `C2D_Prepare()` |
| Depth test | `C3D_DepthTest(false, …)` (1089) | `C2D_Prepare()` |
| Cull face | `C3D_CullFace(GPU_CULL_NONE)` (1090) | `C2D_Prepare()` |
| Texture filter on the *shared* texture | `C3D_TexSetFilter` | `render_game` re-sets it each frame (`main.c:1102` comment states this) |
| **Alpha blend** | changed by the light/bloom draws | **NOT restored by `C2D_Prepare`** — restore by hand, exactly as `bloom_add` (`main.c:1292-1293`) and `light_pass` (`main.c:980`) already do |
| **Scissor** | `C3D_SetScissor` | **NOT touched by citro2d** — must be explicitly `GPU_SCISSOR_DISABLE`d, or every later citro2d draw on that target is clipped. **verify-on-hw** |

**R3.3.2** `GSPGPU_FlushDataCache` on **exactly the bytes written** for each slab before the first
draw (`main.c:1068` pattern). Do not flush the whole slab — see R3.5.

### R3.4 — Per-frame pass budget

Counts are static analysis of `main.c:2587-2802` (draw batches / render-target binds / raw-C3D
escapes). They are **reasoned, not measured** — `worstMs` (`main.c:907`) on real New 3DS hardware
is the only gate that counts (CLAUDE.md #6, PHASE Invariant 8).

Baseline today, overworld, 3D on, DoF+bloom+light on (the worst existing case):

| Stage | draws | target binds | raw escapes |
|---|---|---|---|
| `dof_prepare` (`main.c:2608`) | 1 | 1 | 0 |
| `bloom_bright` (`main.c:2609`) | 1 | 1 | 1 |
| top-L: `render_game` ×2 + warp grid + pop + DoF bands + bloom add + ui pop + light mesh | 8 | 2 | 2 |
| top-R: same (`main.c:2662-2671`) | 8 | 2 | 2 |
| bottom: `render_game` (`main.c:2675`) | 2 | 2 | 0 |
| HUD / menu | 1-2 | 0 | 0 |
| **total** | **~22** | **~9** | **5** |

Tilt-on, same feature set, with the R3.3 merged block:

| Configuration | game images | draws | target binds | raw escapes | Δ vs today |
|---|---|---|---|---|---|
| **tilt OFF** (any config) | — | 22 | 9 | 5 | 0 by construction (Invariant 1) |
| **tilt ON, 3D off** (mono) | top-L, bottom | 6 | 5 | 1 | +1 escape, +0 draws (all of DoF/bloom/light/pop are already gated on `s3dOn`, `main.c:2602-2605`) |
| **tilt ON, 3D on** (stereo, 3 images, bottom flat) | top-L, top-R, bottom | ~20 | 9 | **3** | **−2 draws, −2 escapes** |
| tilt ON, 3D on, bottom also tilted (non-default) | 3 | ~21 | 9 | 4 | −1 draw, −1 escape |

The stereo row is the important one and it is not a typo: merging warp+pop+ui+DoF+bloom+light
into one raw block per eye **removes** the two escapes per eye that today's split costs, and the
tilt draw *replaces* `render_game`'s blit rather than adding to it. Per
`hd2d-octopath-3d.md` §3 the canary is "per-pass `GX_DisplayTransfer` + render-target rebind +
texenv/shader reconfig overhead on the main thread — NOT pixel fill at 240×160", so fewer escapes
and identical target binds is the right shape.

**R3.4.1** Zero new render targets, zero new VRAM (Invariant 3). The only new memory is one
`linearAlloc` vertex arena (R3.5).

**R3.4.2** Fill cost does rise: the tilted trapezoid covers the same ~360×240 screen area, but
`GPU_LINEAR` sampling of a rotated plane has worse texture-cache locality than an axis-aligned
blit. On 240×160-class fill this is expected to be noise; **verify-on-hw** via `worstMs`.

**R3.4.3** Hard budget gate for this phase: `worstMs` must stay ≤16.7 ms with two games running,
tilt at 20°, 3D at full slider, DoF+bloom+light on. If it breaches, the drop order is
`light → bloom → DoF → tilt` (gen1 independently drops tilt first in its perf tiers,
`Performance.lua:44-46`, `gen1-render.md` finding 5 — we drop it last only because it is this
phase's deliverable; if the whole stack cannot fit, tilt is a luxury and goes).

### R3.4.4 — ERRATA (2026-08-04 fix pass, review findings 1 + 3): the mono row undercounts by 2

The **"tilt ON, 3D off (mono)"** row above says *game images: top-L, bottom* and *raw escapes: 1*.
Both are wrong against the shipped code, and the error is structural rather than an implementation
slip: `gfxSet3D(true)` is set **once for the whole session** (`main.c:2049`) and the right-eye
`render_game` call (`main.c:3138`) has no slider guard, so **three game images are rendered every
frame regardless of the 3D slider** — that is today's behaviour too, it simply costs 2 citro2d
draws instead of 2 raw-C3D escapes when the tilt is off. Corrected counts, tilt ON:

| Configuration | game images | raw escapes (tilt) | note |
|---|---|---|---|
| tilt ON, 3D off (mono) | top-L, **top-R**, bottom | **3** | the R-eye buffer is rendered but not scanned out at slider 0 |
| tilt ON, bottom flat (touch on) | top-L, top-R | 2 | |

Two consequences, both recorded rather than "fixed":

1. **The obvious optimisation — skip the right eye when `s3dOn` is false — is REJECTED as unsafe.**
   `s3dOn = slider > 0.03 && !menuOpen && s3dEnabled` (`main.c:2955`), and the physical parallax
   barrier follows the **slider alone**. So `s3dOn == false` with the barrier ON is reachable two
   ways: the ENHANCE master 3D toggle off with the slider up (persistent), and the 250 ms
   menu-open tween-down with the slider up (transient). In both, a tilted left eye against a flat
   right eye is real binocular rivalry on hardware. Gating the whole right-eye *render* is worse
   still: with tilt off that is a change to the flat path, i.e. a PHASE invariant 1 violation. The
   only sound key would be the raw slider (`slider3d <= 0`), and "the right framebuffer is not
   scanned out at slider 0" is inferred, not documented on this machine — so it stays a **lever for
   the hardware run**, not a PC-time change.
2. **What was done instead** (review finding 2): the third image no longer *costs* a third mesh.
   The right eye shares the left eye's `TiltView`, mode and 400×240 rect, so its 176 vertices are
   identical by construction; `TiltDraw.clone` makes it copy the left slab and rewrite only the
   colour, and when the dim tint matches (focused top screen) it draws **out of the left eye's slab
   directly** — no vertex write and no `GSPGPU_FlushDataCache` at all. Pinned bitwise by
   `test_tilt` TEST 18. Remaining per-frame cost of the third image: one raw escape (+ one 6.3 KB
   copy and one flush only while the top screen is the *unfocused* one).

**R3.1.2's prescale, priced (review finding 1).** R3.1.2 forces the sharp-bilinear path on at
`SCALE_1X` while tilting, and states the cost as "+1 pass in the 1:1-with-tilt case only". Spelled
out so it is on the hardware sheet: that pass is a `C2D_TargetClear` of the **512×512** RGBA8 VRAM
target (`PRE_TEX`, `main.c:1949`) plus a 480×320 `C2D_DrawImageAt`, **per game image** — so up to
3× per frame — and it is paid only when `scaleMode == SCALE_1X && !smooth && tilt on`. Every other
scale mode already pays it today (the default is `SCALE_FIT`, `main.c:2053`), so nothing changes
for the default configuration. It uses an existing target and no new VRAM, so PHASE invariant 3's
"no new render targets" holds; the honest reading of its "ONE extra draw per game image" is that
the *tilt* adds one draw and the *anti-shimmer prescale* is a separate, spec-mandated pass.
**verify-on-hw:** A/B `worstMs` at `SCALE_1X` vs Aspect-fit with tilt at 20°; if the 1:1 case is
materially worse, the cheap answer is to let 1:1 sample `e->tex` with `GPU_LINEAR` (accepting the
shimmer R3.1.2 bought off) rather than to add a target.

### R3.5 — The vertex arena

```
per image, worst case:  base grid          176 verts
                        pop strips         32 sprites × 4 strips × 4 = 512
                        UI panel pops      6 × 4 = 24
                        DoF bands          4 × 4 = 16
                        bloom composite    4
                        light mesh         176
                                          ---- ~908 verts
vertex = 3+2+4 floats = 36 B     →  ~33 KB per image
3 images (top-L, top-R, bottom)  →  ~100 KB linearAlloc
```

**R3.5.1** One slab per image (top-L / top-R / bottom), never shared, exactly as `warpVbo`
allocates per eye and for the stated reason — the GPU reads the buffer any time before
`C3D_FrameEnd`, so a rewritten slab would corrupt an already-queued draw ("SYNCDRAW-safe",
`main.c:996`).

**R3.5.2** Flush only `nVerts * sizeof(TiltVert)` bytes, not the arena. 100 KB of
`GSPGPU_FlushDataCache` per frame on the ARM11 is a real cost on the thread that also runs the
`LightEvent` handshake (Invariant 3); today's warp flushes 2.8 KB/eye.

**R3.5.3** Allocation failure (`linearAlloc` returns NULL) → `tiltOk = false` → the flat path,
permanently, exactly as `warpOk` gates the warp (`main.c:1010`, `main.c:2613-2615`).

### R3.6 — The scissor

The near edge is wider than the frame rect (R2.4), so the block scissors to the on-screen frame
rect: `(ox, oy)-(ox + GBA_W·sx, oy + GBA_H·sy)` from `calc_xform`, clamped to the screen.

`C3D_SetScissor(GPU_SCISSORMODE mode, u32 left, u32 top, u32 right, u32 bottom)`
(`c3d/base.h:21`; modes at `enums.h:199-206`, `GPU_SCISSOR_NORMAL = 3` "exclude pixels outside of
the scissor box"). **The rect is in physical framebuffer coordinates, and the 3DS framebuffer is
the screen rotated a quarter turn** (that is exactly why `Mtx_OrthoTilt` exists, `maths.h:530-542`).

**R3.6.1** Expected mapping for a logical `W×H` screen rect `(x0,y0)-(x1,y1)`:
`left = y0, top = W − x1, right = y1, bottom = W − x0`. **verify-on-hw / verify-in-Azahar**: prove
it in one iteration by scissoring to a known quadrant (e.g. the left half of the top screen) and
observing which half survives, *before* wiring it to the tilt. Do not ship it unproven.

**R3.6.2** If the mapping proves troublesome, the sanctioned fallback is **no scissor at all**
(the R2.5.3 "spill" mode). It retains *more* pixels (99.56 % vs 95.88 % at 15° on the top screen)
and needs zero state; the only cost is that the near edge covers the letterbox pillars, breaking
the rectangular frame and reintroducing per-eye border rivalry. Record the choice in BUILDLOG.

**R3.6.3** The scissor must be re-derived per image (400×240 vs 320×240, and per scale mode) and
**must** be disabled before `C2D_Prepare` (R3.3.1).

---

## R4. COMPOSITION WITH EVERY EXISTING PASS

### R4.0 — The ordered per-eye pipeline, tilt ON (binding)

```
── once per frame, shared by both eyes (frame/source space — tilt-agnostic) ──
 1. dof_prepare(&topG->tex, dofTgtA)          main.c:2608   unchanged
 2. bloom_bright(&dofTexA, bloomTgt)          main.c:2609   unchanged

── per game image (top-L, top-R, [bottom]) ──
 3. render_game pass 0: NEAREST 2x prescale -> preTgt        main.c:1315-1323  unchanged
                        (forced on when tilt && !smooth, R3.1.2)
 4. C2D_TargetClear(screen, clrBg)                            main.c:1330      unchanged
    ── enter the ONE raw-C3D block (R3.3) ──
 5. TILT BASE MESH        16x11 grid, source = preTex|e->tex, GPU_LINEAR,
                          per-vertex stereo displacement folded in           (R4.2)
 6. TILT POP STRIPS       per-sprite standee strips, tilted                  (R4.1)
 7. TILT UI POPS          BG0 window-panel rects, tilted                     (R4.3)
 8. TILT DoF BANDS        source = dofTexA, alpha ramp via vertex colour     (R4.4)
 9. TILT BLOOM ADD        source = bloomTex, additive blend, tilted quad     (R4.5)
10. TILT LIGHT MESH       gouraud MULTIPLY grid, untextured, tilted          (R4.6)
    ── restore blend, disable scissor, C2D_Prepare, C2D_SceneBegin ──
11. HUD / focus bar / toast / menu overlay    main.c:2622-2658  unchanged, UNTILTED
```

The ordering inside the block preserves today's semantics exactly: pops over the base, DoF bands
over the pops ("bands OVER the pops", `main.c:2618`), bloom over the blur (`main.c:2619`), UI pops
"hardest of all", light **last** so it tints the UI panels too (`main.c:2621`).

**R4.0.1** Steps 6-10 are *slices*. Until a pass is tilt-aware it is **excluded while tilt is
active** (`if (tiltOn) skip;`), never drawn flat over a tilted base — a flat overlay on a tilted
ground is visibly mis-registered and would look like a bug. Ship order: slice 1 = steps 3-5
(+ excluded 6-10); slice 2 = 6, 7; slice 3 = 8, 9, 10.

**R4.0.2** The HUD, focus bar, toast, pause menu, gamepad overlay and every `ui.c`/`theme.c` draw
stay **flat and untilted**. They are our chrome, not the game world (gen1 tilts only the world
canvas and composites the rest on top, `Renderer.lua:921-927`).

### R4.1 — `pop_eye` (per-sprite stereo standees, `main.c:754-777`) → **COMPOSE, tilted**

Mechanically: each strip is an axis-aligned frame-space rect drawn with a horizontal offset
`xoff` in *screen* px (`draw_pop_tex`, `main.c:720-736`). Under tilt each becomes a trapezoid.
Convert the offset to frame space first — `draw_pop_tex` already does this conversion for its
clipping (`float xs = xoff / sx;`, `main.c:726`), so the units precedent exists — then displace
the frame-space `cx` **before** `tilt_project`. Emit 4 verts per strip into the arena; the whole
sprite set becomes **one** draw instead of a citro2d batch.

**R4.1.1 — the comfort ceiling must be re-clamped.** Displacing before projection means the
on-screen disparity is multiplied by `k·q(row)`: at 15° that is 0.901 at the far row and 1.169 at
the near row. That is *geometrically correct* (near things deserve more disparity), but it
silently raises the hardware-validated comfort ceiling `POP_DISP_MAX = 6.5 px` (`main.c:694`) by
17 % at the near edge. **Requirement:** apply `clamp_disp()` to the *projected* disparity, i.e.
clamp `disp · tilt_disp_scale(v, cy)` where `tilt_disp_scale = k·q(cy)` (R6.6), so the shipped
ceiling is preserved exactly. Stereo comfort is a hardware-validated property; it does not get
quietly relaxed by a rendering change.

**R4.1.2** `pop_eye`'s per-strip source clipping (`main.c:722-729`) still runs in frame space and
is unchanged; the scissor (R3.6) handles the screen-space side.

### R4.2 — `warp_scenery_eye` / `warp_grid_eye` (per-tile & grid stereo warp, `main.c:903-910`, `1054-1093`) → **REPLACED (absorbed into the tilt mesh)**

This is the load-bearing item (i) from the task brief, and the answer is **before**: the stereo
displacement is computed in frame space and then **carried by the tilt transform**.

- `warp_grid_eye` already builds a 16×11 grid whose X is displaced per vertex
  (`w->x = ox + gx*sx + dispUnit * clamp_disp(RAMP_AT(gy) + dep)`, `main.c:1063`). The tilt mesh
  is the same grid; the only change is that the displacement is applied in **frame** space
  (`gx + disp/sx`) and the vertex then goes through `tilt_project` + `calc_xform` + `iq`.
- Doing it the other way (tilt first, then displace the projected X in screen space) would be
  **wrong**: the disparity would no longer be foreshortened with the terrain, a receding tile
  would carry as much pop as the near edge, and the standee ramp (`RAMP_AT`, `main.c:696`, which
  is explicitly "screen-anchored") would fight the perspective ramp. Before-tilt is both cheaper
  and correct.
- `warp_scenery_eye` is the no-shader fallback (`main.c:2615`). With tilt active there is no
  no-shader path (no shader ⇒ `tiltOk == false` ⇒ flat), so **`warp_scenery_eye` is never
  reached while tilt is on** and needs no changes.

**R4.2.1** The sub-tile scroll alignment (`warp_depth_at` with `camX/camY`, `main.c:1046-1052`,
`1062`) is unaffected — it samples the depth field in frame space, which is where we still are.

**R4.2.2** With 3D off (`s3dOn == false`) the displacement is zero and the grid degenerates to the
exact quad of R1.4.

### R4.3 — `ui_pop_eye` (BG0 window panels, `main.c:1209-1220`) → **COMPOSE, tilted**

Same treatment as R4.1 (rects → trapezoids, one draw). Note the panels are *chrome inside the
game image* — they tilt with the frame because they are part of the composited GBA output; we
cannot separate them (PHASE bound #2, `gen1-render.md` finding 5: billboard nothing). Their
disparity gets the same R4.1.1 re-clamp.

**R4.3.1** `ui_pop_eye` fires in **any** context, not just the overworld (`main.c:2593`), while
tilt is gated to the overworld (Invariant 5). During the tween-out after a menu opens, both are
briefly live — which is fine, because they share the projection.

### R4.4 — DoF / tilt-shift bands (`dof_prepare` `main.c:1098-1106`, `dof_bands` `main.c:1129-1144`) → **`dof_prepare` UNCHANGED; `dof_bands` COMPOSE, tilted**

`dof_prepare` operates entirely in source space (a half-res LINEAR bounce of `e->tex` into
`dofTexA`), so it is tilt-agnostic — no change, and it stays shared by both eyes.

`dof_bands` draws 4 alpha-ramped horizontal strips over the frame box in screen space; under tilt
they must ride the same trapezoid or the "sharp focal band" will not line up with the terrain it
is supposed to keep sharp. Each band = 4 verts through `tilt_project`, alpha ramp carried by the
**vertex colour** attribute (R5), source `dofTexA`.

**R4.4.1** The bands' own stereo offsets `dT`/`dB` (`main.c:1133-1134`, "bands ride the floor ramp
at their centers → no depth rivalry with it") are frame-space displacements → same treatment as
R4.1/R4.2, applied before projection, re-clamped per R4.1.1.

**R4.4.2** Aesthetic note, **verify-on-hw**: a perspective tilt and a tilt-shift blur are the same
photographic metaphor, so they should reinforce each other — but the blur band boundaries now
follow projected rows, i.e. the far band gets thinner on screen. If it reads badly, the cheap fix
is to define the band boundaries in *screen* rows and unproject them (`tilt_unproject`, R6.5)
rather than to exclude the pass.

### R4.5 — Bloom (`bloom_bright` `main.c:1249-1269`, `bloom_add` `main.c:1272-1295`) → **`bloom_bright` UNCHANGED; `bloom_add` COMPOSE, tilted**

`bloom_bright` is source-space (half-res → quarter-res glow map) — unchanged. `bloom_add`
composites the glow over the frame box additively; it must be tilted so the glow sits on the
pixels that produced it. It is a 4-vertex quad (no per-vertex displacement needed, R1.4) with
`GPU_BLEND` additive set inside the block and restored at the end (R3.3.1).

**R4.5.1** `bloom_add` already runs as a raw-C3D draw reusing `warpProg` and the ortho projection
(`main.c:1276-1279`). Moving it inside the merged block deletes one escape per eye — this is where
half of R3.4's savings come from.

### R4.6 — `light_pass` (time-of-day gouraud MULTIPLY mesh, `main.c:969-981`) → **COMPOSE, tilted — MANDATORY**

This is load-bearing item (ii). `light_pass` draws a 15×10 gouraud MULTIPLY mesh over the frame
box "in screen space". Its vertex colours come from the **terrain** (`light_vert` reads the
`tdepth` elevation field, `main.c:951-966`). If the mesh is not tilted, the shading slides off the
terrain it is shading — a hill's highlight would sit on the grass in front of it. **It must be
projected through the same homography.** Its grid is the *same* 16×11 vertex grid as the tilt
mesh, so the vertex loop is shared.

Mechanics inside the block: no texture bind needed — TEV stage 0 = `GPU_PRIMARY_COLOR` with
`GPU_REPLACE`, blend set to `dst*src` MULTIPLY exactly as today
(`C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_DST_COLOR, GPU_ZERO, GPU_DST_COLOR, GPU_ZERO)`,
`main.c:975`), restored at the end of the block (`main.c:980` precedent).

**R4.6.1** Bonus: as a raw mesh this is **one draw of 176 verts / 900 indices** instead of 150
`C2D_DrawRectangle` calls plus two blend switches and two `C2D_Flush`es (`main.c:974-980`). Expect
this pass to get *cheaper* under tilt. **verify-on-hw.**

**R4.6.2** `light_pass` must stay **last** in the block (`main.c:2621`: "lit LAST → tints the UI
panels too").

### R4.7 — Sharp-bilinear prescale (`render_game` pass 0, `main.c:1315-1323`) → **COMPOSE (it is the tilt's preferred source)**

Covered by R3.1/R3.1.2. It is a pure source-space pass; tilt consumes its output rather than
`e->tex` whenever it ran, which is exactly what `warp_grid_eye` does today (`main.c:1082`, "sharp-
bilinear keeps its crisp prescale as the source").

### R4.8 — Touch (`touch_to_gba`, `main.c:619-629`) → **EXCLUDED by default; exact inverse provided and host-tested**

PHASE Invariant 4 demands an explicit decision. It is:

**R4.8.1** Tilt ships **top-screen only** by default. The bottom screen is the touch controller and
the smart-touch path is hardware-validated (memory: "Touch: reliability over piling"); it stays
geometrically flat, so `touch_to_gba` is **bit-identical to today** and needs no edit for the
shipped configuration.

**R4.8.2** Two further reasons this is the right default, not laziness: (a) the tilt gate reads
`GsSnap.ctx == GCTX_OVERWORLD` for the **top** game only (`main.c:1975`); a bottom-screen tilt
would need its own gate read. (b) Every other HD-2D pass is already focused-top-only by budget
rule (`main.c:2604`, `hd2d-octopath-3d.md` §3).

**R4.8.3** Nonetheless the exact inverse **is specified and host-tested** (`tilt_unproject`, R6.5),
so enabling bottom tilt later is a one-line change and not a re-derivation. When it is enabled,
`touch_to_gba` becomes: existing screen→frame inverse, then `tilt_unproject` (frame→source),
then the existing bounds check. Round-trip error over a 5×5 grid × 4 angles: **2.8e−14 px**
(R6.9). If a caller ever gets this wrong the failure is silent mis-taps, so the host test asserts
the *composed* mapping, not just `tilt_unproject` in isolation.

### R4.9 — Summary table

| Pass | Verdict | Why (mechanically) |
|---|---|---|
| `render_game` flat/sharp blit | **REPLACED** | the tilt draw *is* the final blit; prescale pass 0 survives |
| `warp_grid_eye` | **REPLACED (absorbed)** | same 16×11 grid; displacement applied in frame space **before** projection so tilt carries it |
| `warp_scenery_eye` | **UNREACHABLE** | no-shader fallback; no shader ⇒ no tilt ⇒ flat path |
| `pop_eye` | **COMPOSE, tilted** | rects→trapezoids in the block; disparity re-clamped after projection (R4.1.1) |
| `ui_pop_eye` | **COMPOSE, tilted** | ditto; fires in any context, shares the projection |
| `dof_prepare` | **UNCHANGED** | source-space half-res bounce |
| `dof_bands` | **COMPOSE, tilted** | bands must follow projected rows or the focal band mis-registers |
| `bloom_bright` | **UNCHANGED** | source-space bright pass |
| `bloom_add` | **COMPOSE, tilted** | glow must sit on the pixels that made it; moving it into the block saves an escape |
| `light_pass` | **COMPOSE, tilted — mandatory** | terrain-derived shading; untilted it slides off the terrain |
| HUD / menu / gamepad / toast | **EXCLUDED (flat)** | chrome, not world |
| `touch_to_gba` | **EXCLUDED by default**, exact inverse specified | bottom screen stays flat; Invariant 4 |

---

## R5. THE SHADER

### R5.1 — New file `source/tilt.v.pica`; `warp.v.pica` is NOT touched

PHASE "Existing machinery" is explicit: `warp.v.pica` sets `z=0, w=1` (affine) and the stereo warp
depends on it. A true-perspective tilt needs real `w`, so this is a sibling file. `bloom_bright`/
`bloom_add` also bind `warpProg` (`main.c:1224`) — another reason not to change it.

### R5.2 — The source

```pica
; tilt.v.pica — perspective-correct tilt/composite vertex shader (3DGBA phase 14).
; Positions arrive in SCREEN pixels (CPU-projected: tilt_project -> calc_xform), carrying the
; homogeneous scale iq = 1/q with q = d/(d - w*sin a) (docs/kb/external/gen1-render.md finding 1,
; Tilt.lua:120-130). Emitting w_clip = iq is what makes the PICA200's fixed-function rasterizer
; reconstruct their TILT_SHADER's projective divide for free (SPEC-render R1.3) — there is no
; fragment shader on this GPU (hd2d-octopath-3d.md §0.1, 3ds/gpu/shbin.h:11-12).

; Uniforms
.fvec projection[4]                 ; Mtx_OrthoTilt(0, screenW, screenH, 0, 1, -1, true)

; Constants
.constf myconst(0.0, 1.0, 0.0, 0.0)
.alias  zeros myconst.xxxx
.alias  ones  myconst.yyyy

; Outputs
.out outpos position
.out outtc0 texcoord0
.out outclr color

; Inputs: v0 = (x, y, iq)   screen px + homogeneous scale
;         v1 = (u, v)       texcoord, 0..1 of the 256/512 POT source
;         v2 = (r, g, b, a) vertex colour: dim tint / DoF alpha ramp / light MULTIPLY colour

.proc main
	; r0 = (x, y, 0, 1)
	mov r0.xy, v0.xy
	mov r0.z,  zeros
	mov r0.w,  ones

	; homogeneous scale: r0 = iq * (x, y, 0, 1) = (x*iq, y*iq, 0, iq)
	; -> after the linear ortho matrix, ndc is unchanged and w_clip = iq  (SPEC-render R1.5)
	mul r0, v0.zzzz, r0

	; outpos = projection * r0
	dp4 outpos.x, projection[0], r0
	dp4 outpos.y, projection[1], r0
	dp4 outpos.z, projection[2], r0
	dp4 outpos.w, projection[3], r0

	; pass through
	mov outtc0, v1
	mov outclr, v2

	end
.end
```

**R5.2.1** 8 instructions, no branches, no loops — vertex cost is irrelevant at ≤908 verts/image.

**R5.2.2** The `mul` could be folded into the CPU (emit `x*iq, y*iq, iq` directly). It is kept in
the shader deliberately so the VBO stays in **screen pixels**, which is what the pure-C module
outputs and what the host test asserts (R6.9) — a VBO you can read is a VBO you can debug.

### R5.3 — Uniforms, attributes, buffer

| Item | Value |
|---|---|
| Uniform | `projection` (fvec[4]) — `shaderInstanceGetUniformLocation(tiltProg.vertexShader, "projection")`, mirroring `main.c:1006` |
| `v0` | `AttrInfo_AddLoader(ai, 0, GPU_FLOAT, 3)` — `(x, y, iq)` |
| `v1` | `AttrInfo_AddLoader(ai, 1, GPU_FLOAT, 2)` — `(u, v)` |
| `v2` | `AttrInfo_AddLoader(ai, 2, GPU_FLOAT, 4)` — `(r, g, b, a)` |
| Buffer | `BufInfo_Add(bi, slab, sizeof(TiltVert), 3, 0x210)` — 3 attrs, permutation nibbles 0/1/2 (today's 2-attr case is `0x10`, `main.c:1081`) |
| Vertex | `typedef struct { float x, y, iq; float u, v; float r, g, b, a; } TiltVert;` — 36 B |

**R5.3.1** Colour is **4 floats, not 4 `GPU_UNSIGNED_BYTE`s.** `GPU_UNSIGNED_BYTE` exists
(`enums.h:297`) and would save 12 B/vertex, but whether the PICA normalises integer attributes to
0..1 or delivers the raw 0..255 is not documented in any header on this machine, and getting it
wrong yields a silently 255× over-bright frame. 36 B × 908 × 3 ≈ 100 KB is affordable (R3.5).
Revisiting this is a **verify-on-hw** micro-optimisation, not a slice-1 concern.

### R5.4 — TEV configuration per draw (all fixed-function; 2 of 6 stages used)

| Draw | Stage 0 RGB | Stage 0 Alpha | Blend |
|---|---|---|---|
| base mesh | `MODULATE(TEXTURE0, PRIMARY_COLOR)` — the vertex colour carries the unfocused dim tint, replacing the `topMod` constant (`main.c:1086-1087`) | `REPLACE(PRIMARY_COLOR)` | citro2d standard (unchanged) |
| pop strips / UI pops | `MODULATE(TEXTURE0, PRIMARY_COLOR)` | `REPLACE(PRIMARY_COLOR)` | citro2d standard |
| DoF bands | `REPLACE(TEXTURE0)` | `REPLACE(PRIMARY_COLOR)` — the per-corner ramp (`main.c:1119-1122`) | citro2d standard |
| bloom add | `MODULATE(TEXTURE0, PRIMARY_COLOR)` (gain via vertex colour) | `REPLACE(PRIMARY_COLOR)` | additive `(GPU_ONE, GPU_ONE, GPU_ZERO, GPU_ONE)` (`main.c:1288`) |
| light mesh | `REPLACE(PRIMARY_COLOR)` (no texture) | `REPLACE(PRIMARY_COLOR)` | MULTIPLY `(GPU_DST_COLOR, GPU_ZERO, …)` (`main.c:975`) |

**R5.4.1** `C3D_TexEnvInit(C3D_GetTexEnv(1))` before the first draw (`main.c:1088`) — a stale
stage 1 from a previous pass silently corrupts the output.

### R5.5 — Build wiring (no Makefile edit needed)

`PICAFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.v.pica)))` and
`HFILES_BIN := $(PICAFILES:.v.pica=_shbin.h)` (`Makefile:103` / `Makefile:115`, consumed at
`Makefile:113`/`176`) auto-glob `source/`.
Dropping in `source/tilt.v.pica` therefore yields **`tilt_shbin.h`** with symbols `tilt_shbin` /
`tilt_shbin_size`, consumed exactly like `warp_shbin` (`main.c:1002`). **No Makefile change.**

**R5.5.1** Init/fini mirror `warp_grid_init`/`warp_grid_fini` (`main.c:1001-1027`): parse DVLB,
`shaderProgramInit`/`SetVsh`, fetch the uniform location, `linearAlloc` the arena; any failure
leaves `tiltOk = false` → flat path (Invariant 1's "no-shader / no-VRAM fallback returns to the
flat path").

### R5.6 — Geometry shader: considered, NOT used

The PICA200's geometry stage **is** programmable (`shbin.h:11-12` lists `GEOMETRY_SHDR`;
`hd2d-octopath-3d.md` §0.1 flags it as "a cheap lever for tessellating a vertex grid on-GPU"). We
are not using it, for three reasons:

1. **Nothing to amplify.** The mesh is ≤908 verts built by a CPU loop that already exists
   (`warp_grid_eye`, `main.c:1059-1067`). A GS would move that loop onto the GPU and save maybe
   tens of microseconds of ARM11 time — while the per-vertex `q` has to come from the pure-C tilt
   module anyway (Invariant 6), so the CPU touches every vertex regardless.
2. **It would fork the math.** Duplicating `tilt_project` in `.pica` means two implementations of
   the phase's correctness argument, and only one of them is host-testable.
3. **Cost/risk.** A GS needs `shaderProgramSetGsh` + `C3D_SetGeoShaderStride` + a different draw
   setup, and GS output buffering on PICA is a known performance trap. Wrong trade for a pass
   whose measured risk is *state churn*, not vertex throughput (`hd2d-octopath-3d.md` §3).

---

## R6. THE PURE-C MATH MODULE — `source/tilt.c` / `source/tilt.h`

### R6.1 — Rules

Header-free pure C per CLAUDE.md #4 and PHASE Invariant 6: only `<math.h>`/`<stdint.h>`; **no**
`3ds.h`, `citro2d.h`, `citro3d.h`, no `C3D_*`/`u32`/`float24` types, no globals. It compiles on the
PC with `clang -std=c11 -Wall -Wextra` and produces zero warnings. Everything GPU-shaped
(matrices, VBOs, TEV) is built in `main.c` **from** this module's outputs.

### R6.2 — Types

```c
// tilt.h — pure-C HD-2D tilt math (phase 14). No libctru/citro3d types.
// Model + notation: SPEC-render R1; source of truth gen1recomp Tilt.lua:120-153
// via docs/kb/external/gen1-render.md finding 1.

typedef struct {
	float angle;      // tween angle, RADIANS (0 = flat)
	float sinA, cosA;
	float vw, vh;     // frame extent (240, 160)
	float d;          // focal = TILT_FOCAL * vh
	float k;          // cover scale (R2.4)
	float yc;         // frame-space y that the source centre row projects to (R1.6)
} TiltView;
```

### R6.3 — Constants

```c
#define TILT_FOCAL      1.0f    // gen1 Tilt.lua:33 — d = FOCAL * vh
#define TILT_COVER_MIX  0.0f    // R2.5.6: 0 = fit (default), 1 = kfull (zero void)
#define TILT_LEVELS     4       // OFF / 10 / 15 / 20  (R2.5.5)
#define TILT_EASE_IN    0.0667f // ~15 frames = 0.25 s at 60 fps (PHASE Invariant 5)
#define TILT_EASE_OUT   0.34f   // ~3 frames — mirrors dofLvl's fast-out (main.c:2596-2599)
```

### R6.4 — API

```c
void  tilt_view_init(TiltView* v, float angleRad, float vw, float vh, float coverMix);
void  tilt_project (const TiltView* v, float cx, float cy, float* fx, float* fy, float* q);
void  tilt_unproject(const TiltView* v, float fx, float fy, float* cx, float* cy);

float tilt_top_scale (float angleRad);   // 1/(1 + 0.5 sin a)      — gen1 Tilt.lua:133
float tilt_bot_scale (float angleRad);   // 1/(1 - 0.5 sin a)
float tilt_cover_fit (float angleRad);   // (1 - 0.25 sin^2 a)/cos a   (R2.3)
float tilt_cover_full(float angleRad);   // 1 + 0.5 sin a              (R2.4)
float tilt_cover     (float angleRad, float mix);

float tilt_disp_scale(const TiltView* v, float cy);   // k*q(cy): stereo foreshortening (R4.1.1)

// coverage diagnostics (HUD / BUILDLOG / tests) — all in GBA px per side
void  tilt_coverage(const TiltView* v, float* voidFarPerSide,
                    float* cropMidPerSide, float* cropNearPerSide);

float tilt_angle_for_level(int level);                // 0/10/15/20 deg -> radians
float tilt_tween(float cur, float target, float easeIn, float easeOut);
int   tilt_active(int level, float angleCur);         // level>0 || angleCur>1e-4  (Tilt.lua:110-114)
```

**R6.4.1** `tilt_project` writes `q`, not `iq`; `main.c` emits `1.0f/q` (R1.3). Keeping the module
in `q` matches gen1's published formulas so a reader can diff them line by line.

**R6.4.2** No trig inside `tilt_project` — `sinA`/`cosA`/`k`/`yc` are precomputed by
`tilt_view_init` once per image per frame. Per-vertex cost is ~4 mul + 2 add + 1 div.

### R6.5 — The inverse (touch, R4.8.3)

```
Yp = (fy - yc) / (k·cosA)          // == w·q
w  = Yp·d / (d + Yp·sinA)
q  = d / (d - w·sinA)
cx = (fx - vw/2)/(k·q) + vw/2
cy = w + vh/2
```

Derivation: `fy - yc = k·cosA·w·q` and `q = d/(d - w·sinA)` ⇒ `Yp·(d - w·sinA) = w·d` ⇒
`w = Yp·d/(d + Yp·sinA)`. Denominator `d + Yp·sinA` is bounded away from zero for every shipped
angle: at 20° the frame maps to `Yp = (fy − 66.319)/(1.03306·0.93969) ∈ [−68.3, +96.5]`, so with
`d = 160, sinA = 0.342` the denominator stays in `[136.6, 193.0]`.

### R6.6 — Disparity foreshortening helper

`tilt_disp_scale(v, cy) = v->k · d/(d - (cy - vh/2)·sinA)`. Used by every stereo pass to re-clamp
against `POP_DISP_MAX` after projection (R4.1.1). At `angle == 0` it returns exactly `1.0f`, so
the flat path's clamp is bit-identical.

### R6.7 — How `main.c` builds the GPU state from this module

```c
TiltView tv; tilt_view_init(&tv, tiltAngleCur, (float)GBA_W, (float)GBA_H, TILT_COVER_MIX);
// per vertex:
float fx, fy, q;
tilt_project(&tv, cxDisplaced, cy, &fx, &fy, &q);     // cxDisplaced = cx + stereoDisp/sx (R4.2)
vert->x  = ox + fx * sx;                              // calc_xform, main.c:709-715 (unchanged)
vert->y  = oy + fy * sy;
vert->iq = 1.0f / q;                                  // R1.3 — the DEPTH, not the scale
vert->u  = gx / 256.0f;  vert->v = 1.0f - gy / 256.0f;   // main.c:1065-1066 (unchanged)
// once per block:
C3D_Mtx proj; Mtx_OrthoTilt(&proj, 0.0f, screenW, screenH, 0.0f, 1.0f, -1.0f, true);   // R1.5
```

**No `C3D_Mtx` ever enters `tilt.c`.** The module's entire contract with the GPU is
`(x, y, iq, u, v, rgba)` floats.

### R6.8 — Gate + tween wiring (in `main.c`, using the module)

```c
float target = (tiltLevel > 0 && depth3d.overworld && !menuOpen) ? tilt_angle_for_level(tiltLevel) : 0.0f;
tiltAngleCur = tilt_tween(tiltAngleCur, target, TILT_EASE_IN, TILT_EASE_OUT);
bool tiltOn  = tiltOk && tilt_active(tiltLevel, tiltAngleCur);
```

`depth3d.overworld` is already computed as `game_read(...) && ts.ctx == GCTX_OVERWORLD`
(`main.c:1975`) — the exact signal PHASE Invariant 5 calls for and that gen1 had to approximate
(`Tilt.gateOK`, `Tilt.lua:116-118`). The asymmetric ease (slow in / 3-frame out) means a script,
menu or battle retracts the tilt almost immediately, which is what keeps R2.5.4's promise that a
textbox is never distorted for long.

**R6.8.1** Settings: append `s32 tilt;` to `Settings` (`main.c:1349-1372`) and add its
`offsetof`-based accepted length to `settings_load` exactly as `dof`/`bloom`/`light`/`vivid` did
(`main.c:1381-1405`) — older files must keep loading.

**R6.8.2** Menu: a 4-way `PK_SEG` on the ENHANCE tab (`PT_ENHANCE`, `main.c:1473-1475`;
widget list `main.c:1546-1553`), new action id in the `>= 100` redesign-only group
(`main.c:1455-1457`). **Open question O6** — the ENHANCE plate art has 5 rows today.

### R6.9 — Golden values the host test must assert

`test/host/test_tilt.c`, built and run like every sibling:
`clang -std=c11 -Wall -Wextra -O2 -I source test/host/test_tilt.c -o /tmp/tt && /tmp/tt`.

**T1 — identity at zero.** For 25 sample points, `tilt_view_init(&v, 0, 240, 160, 0)` ⇒
`fx == cx`, `fy == cy`, `q == 1.0f` **exactly** (bitwise), and `tilt_disp_scale == 1.0f`.
`tilt_active(0, 0.0f) == 0`. This is the algebraic form of Invariant 1.

**T2 — the R1.8 corner table**, tolerance 1e−4:

| a | (cx,cy) | fx | fy | q |
|---|---|---|---|---|
| 10° | (0,0) | 8.728425 | 0.000000 | 0.92011210 |
| 10° | (240,160) | 252.430812 | 160.000000 | 1.09507926 |
| 10° | (120,80) | 120.000000 | 73.054073 | 1.00000000 |
| 15° | (0,0) | 11.843810 | 0.000000 | 0.88541842 |
| 15° | (240,0) | 228.156190 | 0.000000 | 0.88541842 |
| 15° | (0,160) | −20.310093 | 160.000000 | 1.14864569 |
| 15° | (240,160) | 260.310093 | 160.000000 | 1.14864569 |
| 15° | (120,80) | 120.000000 | 69.647238 | 1.00000000 |
| 20° | (0,0) | 14.136881 | 0.000000 | 0.85396362 |
| 20° | (240,160) | 269.539547 | 160.000000 | 1.20628727 |
| 20° | (120,80) | 120.000000 | 66.319194 | 1.00000000 |
| 25° | (120,80) | 120.000000 | 63.095270 | 1.00000000 |
| 35° | (0,160) | −68.505403 | 160.000000 | 1.40210808 |

**T3 — vertical fit is exact.** For every angle in {0,5,10,15,20,25,35}: `fy(cy=0) == 0 ± 1e−4`
and `fy(cy=160) == 160 ± 1e−4`. (The cheapest possible guard on `k`/`yc`.)

**T4 — the R1.9 reference table.** For a ∈ {0,15,25,35} the *centre-pinned* variant
(`k = 1, yc = vh/2`, i.e. gen1's own formula) must reproduce `sy` = 0 / 11.580118 / 168.760523
(15°), 20.143584 / 171.930344 (25°), 29.073078 / 171.883176 (35°) to 1e−4. This pins us to the
external source of truth, independent of our cover/anchor choices.

**T5 — growth factors** against the R2.2 table to 1e−5: `tilt_cover_fit` = 1.00777 / 1.01794 /
1.03306 / 1.05411 / 1.12037 at 10/15/20/25/35°; `tilt_cover_full` = 1.0868 / 1.1294 / 1.1710;
`tilt_top_scale` = 0.92011 / 0.88542 / 0.85396; and the gen1 cross-check
`1/(cos a · topScale)` = 1.1693 / 1.5709 / 2.1516 at 15/35/50°.

**T6 — round trip.** `tilt_unproject(tilt_project(p)) == p` within **0.01 px** over a 5×5 grid ×
{0,10,15,20}° (measured 2.8e−14 in double; float32 will be looser but nowhere near 0.01).

**T7 — coverage diagnostics** against R2.4 to 0.01 px: at 15°, void-far = 11.844, crop-mid =
2.115, crop-near = 17.370; at 20°, 14.137 / 3.840 / 23.704.

**T8 — monotonicity / sanity.** `q` strictly increases with `cy` for `a > 0`; `q > 0` for every
angle in [0°, 60°] over the whole frame (R1.2); `tilt_cover_fit(a) ≤ tilt_cover_full(a)` for
a ∈ [0°, 45°].

**T9 — tween.** From 0, `tilt_tween` reaches the 15° target in ≤15 frames and never overshoots;
from 15° with target 0 it reaches 0 in ≤4 frames; `tilt_active` stays true for the entire
descent and false exactly one frame after arrival (mirrors `Tilt.active()`, `Tilt.lua:110-114`).

**T10 — disparity clamp.** `tilt_disp_scale(v, 160)` = 1.169 at 15° / 1.246 at 20°; a disparity of
`POP_DISP_MAX` at the near row, multiplied and re-clamped per R4.1.1, must land at exactly
`POP_DISP_MAX`.

**R6.9.1** PHASE Invariant 7: the suite grows, `make` still produces `3DGBA.3dsx` with zero
warnings in new files, and a dated `BUILDLOG.md` entry is banked per slice.

---

## Open Questions — only hardware can settle these

**O1 — How does a tilted plane read through the parallax barrier?** Our stereo is *fake*
(screen-space displacement of a flat composite). Under tilt the disparity is now foreshortened by
`k·q` (R4.1.1) — geometrically right, but the 3DS's barrier has a narrow comfort window and the
near edge now carries 17-25 % more disparity before clamping. Does the combination read as depth
or as eye strain? Azahar cannot answer this at all. **Test:** 15° and 20°, full slider, 10 minutes
of overworld walking, both eyes.

**O2 — The real frame cost.** R3.4 argues tilt-on is *cheaper* than today in escapes and equal in
draws, but `GX_DisplayTransfer`/target-rebind/texenv churn on the main thread is the canary
(`hd2d-octopath-3d.md` §3) and that thread also drives the per-frame `LightEvent` handshake with
two saturated 804 MHz workers. **Test:** `worstMs` ≤16.7 ms, two games, tilt 20°, 3D full,
DoF+bloom+light on. Also confirm the merged block really deletes the two escapes rather than
trading them for a stall.

**O3 — Do the top-corner wedges read as a diorama stage or as a rendering bug?** (R2.6) 1.8 % of
the frame, ~17.8 screen px at their widest, only partly hidden by the HUD bar. The knob is
`TILT_COVER_MIX` (R2.5.6) and the designed alternative is the horizon skirt (R2.5.7). **Test:**
photograph the top-left corner at 10/15/20° with HUD on and off.

**O4 — The scissor coordinate mapping** (R3.6.1). The logical→physical quarter-turn for
`C3D_SetScissor` is inferred, not documented in any header on this machine. Prove it in Azahar
with a half-screen scissor before wiring it to tilt; the fallback (no scissor, spill) is specified
and costs nothing.

**O5 — Does `GPU_LINEAR` on a tilted 2×-prescaled source actually stay crisp?** gen1 uses LINEAR
for the warped canvas and NEAREST for flat (`Renderer.lua:514-521`), but they never combined it
with a 2× prescale. If it reads soft, the escalation is their deferred 2× supersample TODO, which
costs a render target and must be re-justified against Invariant 3.

**O6 — ENHANCE tab room.** The tab has 5 rows of art today (`PT_ENHANCE`, `main.c:1473-1475`;
plate `pause-bot-enhance`, `main.c:1486`). A 4-way tilt segment needs a 6th row, i.e. new plate
art. Belongs to the UX/settings spec, not this one; flagged here so it is not discovered at
integration time.

**O7 — Does the +10 px upward re-anchoring (R2.5.4) feel like a camera pitch or like the image
slipping?** It is constant per angle and tweened, so it should read as intent, but it moves the
player sprite off the exact screen centre. If it reads badly the fallback is a `recentre` blend
between centre-pinned and bottom-anchored — at the documented cost of ~20 points of retained
frame (R2.4).

**O8 — Interaction with the tilt-shift DoF** (R4.4.2). Two photographic metaphors stacked; they
should reinforce, but the far band now compresses on screen. Judgeable only by eye, on hardware.
