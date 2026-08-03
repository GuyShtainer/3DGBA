# Phase 14 — HD-2D tilt (the diorama pass)

Decided 2026-08-03 after the external-projects teardown. Round 2 of that teardown
(`docs/kb/external-projects-teardown.md` §R2.6, detail in `docs/kb/external/gen1-render.md`)
**corrected the old plan and made this smaller than budgeted**: gen1recomp's tilted world is ONE
4-vertex quad through a planar homography, and their per-pixel perspective-correct divide exists
only because LÖVE is a 2D API. **The PICA200 does that in fixed function when clip-space w is
real** — so a genuinely 3D-rotated quad needs no fragment shader (we have none), no vertex grid,
and no projective texcoords.

## What ships

A cycleable, presentation-only perspective tilt of the GBA frame: the frame is treated as a
ground plane, rotated about the horizontal axis through the viewport centre, viewed through a
perspective camera. Rows above centre recede, rows below come toward the viewer — the HD-2D
diorama read.

## What bounds it (decided, do not re-litigate)

1. **Mild angles only.** gen1recomp renders up to 2.56x more world to fill the tilted frustum;
   we have a fixed 240x160 composited frame and no more world to render. Overscan-crop cost is
   ~17% at 15 degrees, ~57% at 35, 2.15x at 50. **Ship ~15 degrees as the headline**; anything
   past ~20 must either crop hard or show void, and the spec decides which (fill vs crop) with
   numbers.
2. **Billboard NOTHING from the game.** Their billboard pass requires layer separation (entities
   removed from the ground pass, so holes never exist). We have one composited framebuffer:
   cutting an OAM rect out leaves a hole with no data behind it, HUD/glued-OAM sprites would
   float (their heal-machine rule), and classifying standing-vs-glued is the per-tileset special-
   case tail they abandoned *with* full tile data. Tilt the whole frame; billboard only our own
   overlays if ever. This is settled — see gen1-render.md finding 3.
3. **Presentation only.** Zero effect on emulation, input, collision, touch mapping, or the link.
   Touch coordinate mapping (`touch_to_gba`) must keep working exactly as today (see Invariant 4).

## Invariants (binding on every implementer)

1. **Flat path byte-identical when tilt is off.** Tilt is strictly additive behind ONE
   `tilt_active()` check that includes the tween (level > 0 OR angle > 0), exactly as
   `Tilt.active()` does. Tilt-off must cost zero extra GPU passes and take the existing
   `render_game` blit unchanged. A no-shader / no-VRAM fallback returns to the flat path.
2. **The trade path is untouched.** No edits to celiolink.c, netlink round/ack logic, or the
   gbacore SIO fill. This is a render-thread-only phase.
3. **Frame budget is the real risk, not pixel fill.** The render thread also orchestrates the
   per-frame `LightEvent` handshake with two saturated GBA workers at 804 MHz; extra target
   binds / shader swaps / `C2D_Flush` churn eat into the 16.7 ms those workers depend on. The
   spec must budget passes explicitly and the implementation must keep tilt to ONE extra draw
   per already-rendered game image (no new render targets unless justified with numbers).
4. **Touch stays correct.** The bottom screen's `touch_to_gba` inverse transform must remain
   valid. Either tilt is excluded from the touch-mapped path, or the inverse is updated and
   host-tested — the spec decides and says which, explicitly. A silently-wrong touch mapping
   would regress a hardware-validated feature.
5. **Gate it like gen1recomp does**: honored only in free-roam overworld, ignored while a
   script/menu/battle is up. We have `GsSnap.ctx == GCTX_OVERWORLD` (gamestate.c) — a signal
   they had to approximate. Tween between levels (~0.25 s) so it never snaps.
6. **Pure-C math, host-tested** (CLAUDE.md #4): the projection/corner/view-growth math lives in
   a header-free module that dual-compiles on the PC, with golden-value checks.
7. **Suites stay green and grow**; `make` must produce `3DGBA.3dsx` cleanly after every slice,
   with zero warnings in new files. Bank a dated `BUILDLOG.md` entry per slice.
8. **Hardware-final** (CLAUDE.md #6): Azahar cannot prove the frame budget, the stereo fusion,
   or how the tilt actually reads on the parallax barrier. PC-green + build-green is this
   phase's exit gate; the HANDOFF test checklist gains the tilt items.

## Existing machinery to build on (verified in-tree, 2026-08-03)

- `source/warp.v.pica` + `warpProg`/`warpProjLoc` (main.c:994-1005, 1071) — a passthrough vertex
  shader taking a projection matrix uniform, with the **full raw-C3D escape/return sequence
  already proven** at main.c:1069-1090 (`C2D_Flush` -> `C3D_FrameDrawOn` -> `C3D_BindProgram` ->
  AttrInfo/BufInfo -> TexEnv -> restore). NOTE: it sets z=0,w=1 (affine); a true-perspective
  tilt needs real w, so expect a sibling `tilt.v.pica` rather than a change to this one (which
  the stereo warp depends on).
- `calc_xform(mode, W, H, &ox,&oy,&sx,&sy)` — the shared screen-rect transform every pass uses.
- `render_game()` (main.c:1300) — the flat/sharp-bilinear blit, and `preTgt`/`preTex` (PRE_TEX
  256, VRAM) — an existing offscreen target whose UVs already coincide at 1/256.
- ENHANCE pause tab: `PT_ENHANCE` rows (main.c:1473) with `ACT_3D/ACT_DOF/ACT_BLOOM/ACT_LIGHT/
  ACT_VIVID`, the `PILL(...)` renderer (main.c:1593), and `Settings`/`settings_load/save`
  persistence — tilt joins these, it does not invent a parallel settings path.
- Prior HD-2D passes to compose with (or exclude): `pop_eye` (stereo sprite pop),
  `warp_scenery_eye` (per-tile stereo depth warp), `light_pass` (time-of-day MULTIPLY mesh),
  DoF/bloom. The spec must state tilt's interaction with EACH, including per-eye stereo.
- `DepthSnap.tdepth[10][15]` + `build_depth_grid` — available if the tilt wants terrain
  awareness, though the shipped design does not require it.
