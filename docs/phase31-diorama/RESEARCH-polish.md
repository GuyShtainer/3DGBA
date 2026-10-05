# Phase 31 research — polish/presentation techniques from the DramaticShape voxel mod

**Status: skeleton — sections are being filled incrementally as the reference is read.**

Idea-level distillation of the presentation layer of the archived **DramaticShapeVoxelMod**
(Lua/LÖVE2D mod for gen1recomp), read from the local study archives:

- `projects/_reference/DramaticShapeVoxelMod-latest/` (primary; "DSVM" in pointers below)
- `projects/_reference/Gen2Recomped-DramaticShapes/` (Gen-2 port; "G2DS", cited only where it evolved)

**Legal framing (binding).** The reference is unlicensed / redistribution-restricted. This
document is a clean-room distillation: everything below is written in our own words —
no copied code, no copied comments, no copied data-table values. Where a table's *values*
matter (e.g. `voxel_heights.lua`), only the schema and the roles are described, and the
values are explicitly flagged as unusable — we must derive our own from our own data.
Provenance pointers (`file:line`) sit beside each idea for later audit; they are pointers
into the private archive, never quotes. The implementer of phase 31 reads THIS document
only, never the reference.

Companion doc: `docs/kb/voxel-diorama-reference.md` (ecosystem map, licensing, the
C-side voxel renderer teardown). This doc covers what that one deferred: the polish.

---

## 1. Overall render pipeline (passes, targets, order)

Provenance: DSVM `main.lua:1-36,225-530` (pipeline registration), `lib/VoxelScene.lua:1-10,990-1414` (frame assembly), `lib/VoxelScene.lua:1128-1378` (in-scene pass order).

### 1.1 The two hook points

The mod owns exactly two stages of the host engine's frame:

1. **A world-draw stage** ("voxel") that replaces the flat tile blit with a full 3D
   render into its own canvas. If anything prevents the 3D pass (no depth buffer,
   headless, meshes not built yet), it returns nothing and the engine silently falls
   back to the ordinary 2D world for that frame — every frame independently. This
   "return nil = 2D fallback" contract is load-bearing: mode toggles, map warps and
   async mesh builds all lean on it instead of blocking. (`main.lua:369-492`)
2. **A world-present stage** ("tiltshift") that post-processes the *finished world
   canvas* BEFORE the UI is composited over it. This placement is the whole trick:
   the miniature blur lands on the diorama only, and dialog boxes/menus stay crisp.
   (`main.lua:508-530`)

Our citro3d mapping: (1) = the top-screen 3D scene render; (2) = a post pass on the
scene render-target before the 2D UI layer draws. The "blur world, not UI" ordering
is cheap to honor and visually essential.

### 1.2 Frame skeleton (one visible frame, in order)

From `VoxelScene.render` + its inner `drawScene` (`lib/VoxelScene.lua:990-1414`):

1. **Prefetch / mesh streaming** — request this map's terrain mesh and each
   connected neighbor's; draw whatever is already built; evict everything outside
   the current map + neighbor "live set" when it changes. Meshes build
   asynchronously in a few-ms-per-frame budget during the update tick; frames
   covered by a warp fade get a wider build slice, so door transitions hide the
   rebuild. Two mesh variants per map: BODY (map interior, fast, enough to leave
   the 2D fallback) and FULL (adds the off-map border ring, background-built,
   masked out where a neighbor's body overlaps it). (`lib/VoxelScene.lua:447-573`,
   `main.lua:264-366`)
2. **Per-frame lighting state** — point the shared sun rig at the day/night clock
   (or noon indoors), compute the hour tint the scene shader multiplies every
   surface by, bind the tileset's window-glass mask + night-window strength +
   movement-driven glint phase, and the map's fog/atmosphere record if it has one.
   (`lib/VoxelScene.lua:1015-1050`)
3. **Pose capture, exactly once per entity per frame** — walking phase, hop lift,
   facing, mirror flag are sampled into a `posed` list that BOTH the sun pass and
   the camera pass then read, so the shadow and the visible sprite can never
   disagree by a tick. (`lib/VoxelScene.lua:588-620`)
4. **Camera resolution** — first-person/third-person rig blend if one of those
   rungs is active, else the fixed orbit; then the render-distance trapezoid is
   fitted around whichever camera won (section 7). (`lib/VoxelScene.lua:1075-1099`)
5. **Sun shadow pass** — render the scene from the light into a shadow map,
   but ONLY when a change-signature says something moved (section 3). Runs before
   the main scene because render-target passes don't nest. (`lib/VoxelScene.lua:883-982,1120-1122`)
6. **Main scene pass**, cleared to the sky color (section 8), then in order:
   a. terrain mesh of the current map, then each in-frustum neighbor at its
      connection offset (same world-pixel origin as the 2D game, so no transform
      beyond the neighbor's 2D offset);
   b. fallback blob shadows (flattened sprite quads) only when no shadow map exists;
   c. **water** (section 4) — after terrain, before characters, because a mirror
      can only reflect what is already drawn;
   d. the player's **occlusion silhouette**: the player's flat quad drawn with the
      depth test inverted, so it paints only where the world hides them — drawn
      BEFORE the solid characters so the only depth it can lose to is the world
      itself (`lib/VoxelScene.lua:1194-1209`);
   e. **the cast** — every character as a leaning billboard (section 9), then
      "authored figures" (people the tileset drew into furniture, cut out and
      given the same billboard treatment);
   f. **tall grass** then **flower cutouts**, drawn last with a camera-ward depth
      bias so a tuft row still overdraws a walker's feet (the 3D re-creation of
      the GB grass-over-feet trick) while losing honestly to buildings
      (`lib/VoxelScene.lua:1281-1336`);
   g. additive **atmosphere** (god rays / motes) over the finished depth buffer —
      occluded by trees, writes no depth (`lib/VoxelScene.lua:1338-1344`);
7. **2D FX overlay** — the game's ordinary field-effect draw closures run on top,
   anchored by projecting their world coordinates through the 3D camera
   (`project(wx, 0, wy)` → screen), so emotes/animations land on the right heads
   without being ported to 3D. (`main.lua:474-487`)
8. **Supersample resolve** — the whole pass above secretly rendered into a larger
   canvas; fold it down to window size (section 6). (`main.lua:397-403,491`)
9. **Tilt-shift** on the resolved world canvas (section 2), then the engine
   composites UI on top.

### 1.3 Cross-cutting ideas worth stealing

- **No y-sort anywhere.** Occlusion is the depth buffer; walk behind a building
  and the building simply wins. The entire 2D painter's-order machinery is
  replaced by three *targeted* depth biases (billboard pull, grass pull, flower
  pull), each justified per case. (`lib/VoxelScene.lua:6-10`)
- **Geometry invalidation piggybacks on the game's own block writes.** Any tile
  edit (cut tree, door stamp, script) funnels through one map-write choke point;
  wrap it once and refresh the affected map's mesh in the background while the
  stale mesh keeps drawing — no scene blink. Read-back-after-write so a rewrite
  of an identical value costs nothing. (`main.lua:1097-1152`)
  For us: hook our EWRAM map-change detection (mapId change already rebuilds; a
  metatile hash over the visible window can catch in-map edits).
- **Palette unification.** The 3D pass has no screen-space palette shader, so the
  active display mode's color transform is baked into the terrain atlas and
  sprite textures ahead of the draw, and even the sky ramp is stored "shaped like
  a world palette" so the same transform applies. Idea for us: define every
  polish color (sky, fog, shadow tint) as a ramp derived from game palette data,
  not hardcoded RGB, so day/night and screen-fade states stay coherent.
  (`lib/VoxelScene.lua:52-70,93-111`)
- **Render at display pixel resolution, not game resolution** — the 3D pass is
  crisp while 2D FX closures keep drawing in world-pixel units at a scale factor.
  (`main.lua:157-183,391-397`)

## 2. Tilt-shift depth of field ("T-SHIFT")

Provenance: DSVM `lib/TiltShift.lua:1-179` (the world pass), `lib/BattleDOF.lua:1-100` (the battle variant and its shipped negative result).

### 2.1 The model

Not a real DOF — a screen-space fake that is *exactly correct* for this camera:
with a fixed high camera looking down at a ground plane, distance from the lens
increases monotonically up the screen, so **screen height IS depth** and "a band
of rows" = "a slab of world". The effect is therefore purely 1D in screen Y:

- A horizontal **focus band** stays perfectly sharp. Its center sits at the
  vertical middle of the frame — because that is where the camera parks the
  player. Focus follows the subject for free; no depth buffer is read at all.
- Blur strength ramps from 0 at the band edge to full at a configured distance,
  with a **quadratic ease-in** (`s = s*s`) so the band edge shows no seam.
- Implementation: a **separable gaussian**, two fullscreen passes (horizontal
  then vertical), 9 taps each (center + 4 mirrored pairs at standard gaussian
  weights) where the per-pixel tap spacing is scaled by the band-distance factor
  `s`. Blurring by widening tap spacing (not by more taps) keeps cost constant
  per pixel regardless of strength.
- The final pass also applies a **saturation lift**: mix each pixel toward its
  own chroma away from luma. This is half the "miniature model photo" read —
  toy photos are contrasty and rich. Cheap (one dot product + mix).

### 2.2 Parameter roles (the ladder)

Three strength presets, each defined by four knobs (roles, with rough magnitudes;
derive our own values on hardware):

- `spacing` — gaussian tap gap at full blur, expressed as a **fraction of canvas
  height** (order of 0.002–0.004) so the look is resolution-independent; clamped
  to a sane texel range at runtime. Effective blur reach = 4 taps × spacing,
  compounded by the two passes.
- `band` — half-height of the fully sharp zone in canvas-UV (order 0.07–0.14;
  stronger preset = narrower band).
- `range` — UV distance from band edge to full blur (order 0.3–0.4; stronger
  preset = shorter ramp).
- `saturation` — the color-pop factor (order 1.1–1.3, rising with strength).

Stronger rung = narrower band + steeper ramp + wider blur + more saturation, all
moving together.

### 2.3 The battle variant, and its shipped negative result

`BattleDOF` is the same shader with the band **measured from the scene** instead
of fixed: the sharp slab is fitted around the two combatants' ground marks (with
a margin factor, ~half the gap between them, on both sides) so the slab in focus
is provably the slab they stand in. Two extra ideas there:

- If the subjects are drawn as 2D pics composited AFTER the blur, they are never
  blurred at all — perfect subject separation with zero masking work.
- **The negative result:** the pass is shipped disabled, with the reason stated
  in the header — once the combatants became in-scene geometry (rather than pics
  over the top), a band blur that softens their tile softens THEM, and that must
  be solved before re-enabling. Lesson for us: screen-space band DOF composes
  cleanly only with subjects that are composited after it, or that provably sit
  inside the band.

### 2.4 3DS mapping

Cost on PICA200: two fullscreen textured passes over a 400×240 (or 240×400
rotated) target. No dependent reads beyond 9 taps; the fragment math is trivial.
The saturation mix can ride the second pass. This is one of the cheapest,
highest-yield items in the whole list (and phase-14's HD-2D work already wanted
it). Caveat: PICA200 has no user fragment shaders — the 9-tap gaussian must be
built from multi-pass fixed-function combiners or a multi-texture trick; the
proven 3DS approach is a few passes of hardware bilinear downscale/upscale
blended back over the sharp image with a vertical alpha gradient mask
(gradient texture = the band/range ramp). Same model, different mechanism.

## 3. Cast shadows

Provenance: DSVM `lib/ShadowMap.lua:1-547` (the whole mechanism), `lib/VoxelScene.lua:825-982` (what casts, and the redraw signature), `lib/VoxelScene.lua:1141-1159` (the decal fallback).

### 3.1 Mechanism: a real shadow map (second scene pass from the sun)

Yes — it is a genuine shadow map, not projected geometry. An **orthographic camera
pointed down the sun line** renders every caster once and stores per-texel "how far
did the light travel before hitting something". The main pass transforms each
fragment into that light space and compares. That one mechanism gives, with zero
special cases: shadows climbing walls, draping over roofs, falling on passing NPCs,
and buildings/trees/signs casting at all (the predecessor system — each sprite
frame squashed flat onto its ground plane — could only ever paint the floor and
only characters cast).

Portability decision worth copying: depth is stored **packed into two 8-bit color
channels of an ordinary color target** (~16-bit precision over the frustum) rather
than a sampled depth texture, because depth sampling is the least portable corner
of their API and their contract is fall-back-not-crash. A third channel stores a
**caster-type flag** (world vs. character) so one surface class can *decline* a
caster class — water declines character shadows, because a hard sprite cutout on
a reflective surface reads as a sticker, while ground/roofs/NPCs still take them.

### 3.2 The sun as a shear (the parameter model)

The sun is expressed as two shear coefficients: a point `h` world-pixels above
ground drops its shadow `(KX*h, KZ*h)` away from the point below it.

- **Magnitude** of the pair = sun elevation. Tuned near 1.0 combined (≈45° sun) so
  a character throws a shadow about as long as it is tall — their original steeper
  noon sun read as "a smudge under everybody's feet".
- **Ratio** = compass bearing, and it is deliberately biased west-of-northwest:
  a billboard sprite leans back over the ground directly north of its feet, so a
  due-north shadow would land entirely under the slab and never be seen. The
  bearing is chosen so the shadow clears the sprite's own half-width. This
  "shadow must escape the billboard's footprint" rule is specific to leaning-card
  worlds and directly applies to us.
- The day/night system swings these coefficients with the clock (golden-hour long
  shadows), which is what forced the anti-peter-panning work below.

### 3.3 Precision engineering (the part that took them iterations)

- **Resolution ladder:** the light frustum is sized to the *visible ground*, which
  varies ~3x with zoom/window, so the map edge is picked per frame from a small
  ladder (1–2K) as the smallest size resolving a target of roughly a third to a
  half world-pixel per texel; capped rather than chased.
- **Texel snapping:** the light frustum's corner is snapped to whole texel
  multiples; otherwise every shadow edge reprojects by a sub-texel amount each
  frame the camera slides and the whole world's shadows crawl and shimmer.
- **Asymmetric frustum fit:** the camera sits south looking north, so visible
  ground runs far north/barely south; the sun sits southeast, so off-screen
  casters only matter on the south and east sides. Fitting margins only where
  each is needed (instead of a symmetric box) roughly tripled effective
  resolution at the shallow camera angle. Ground reach is computed from the
  camera pitch (where the top view ray hits the ground plane), capped at a few
  view-heights once the ray nears the horizon.
- **Slope-scaled depth bias:** the compare's forgiveness is a constant floor
  (quantization + matrix disagreement) **plus** a term proportional to the
  texel's world size times the worst-case lit-surface depth ramp (~3 world px
  per texel for the steepest sun-facing roof). A constant-only bias is generous
  at one zoom and shows diagonal acne bands at another. They validated it with a
  probe test that *counts isolated shadowed pixels on lit surfaces*
  (`tests/voxel_acne_probe.lua`) — automate the artifact, don't eyeball it.
- **"Snugging" thin casters (anti-peter-panning):** the bias forgiveness makes
  the first `slack` world-pixels of every shadow disappear, so shadows detach
  from feet. Fix: translate each thin caster (sprite cards, flowers, figures)
  **toward the sun along its own light ray** by ~0.9× the slack before storing
  it — the shadow's landing spot is unchanged (every point stays on its ray) but
  it is stored shallower, so ground at the caster's feet now fails the lit test
  and the shadow roots at the feet again. The one obligation: the caster's lit
  draw must use the *same* snugged transform for its own shadow lookup, or the
  card moirés against a mis-registered record of itself. (`lib/ShadowMap.lua:418-455`)

### 3.4 Cost structure and caching

- **The sun pass redraws only on change.** A signature string captures everything
  the pass depends on: camera center quantized to quarter-pixels, view size,
  camera pitch, sun shear (quantized so a running day cycle redraws a few times
  a minute, not per frame), the view-trapezoid state, mesh identities, and every
  entity's pose tuple. Same signature → reuse last frame's map. Standing still,
  dialogs, menus: shadow cost is zero. (`lib/VoxelScene.lua:825-870`)
- **What casts:** terrain mesh (buildings/trees/ledges/props ride in it), the
  water surface (so shorelines shadow lakes), flower cutouts, authored figures,
  and one **upright** card per character (the lean is a camera trick; the sun
  sees the unleaned card). **Tall grass deliberately does not cast** — thousands
  of tufts would speckle at pixel scale for a doubled mesh draw. Explicit
  cost/benefit line-drawing per caster class.
- **Fallback ladder:** no shadow map (driver/headless) → flattened per-sprite
  ground decals; player set shadows OFF → nothing (a player's "no" is not a
  capability failure). Both live in the scene pass, not the shadow module.

### 3.5 3DS mapping

The PICA200 has *native* shadow-mapping hardware (shadow texture formats +
per-fragment compare in the texture unit; exposed by citro3d), so the packed-color
portability trick is unnecessary for us — but everything else transfers directly:
the shear-based sun model, texel snapping, asymmetric fit, slope-scaled bias,
snugging, the signature cache (huge on our frame budget: redrawing the sun pass
only on movement), and the what-casts cost lines. Realistic budget: one extra
geometry pass over a few thousand quads, into a 512–1024px shadow target —
plausible on PICA but hardware-gated (convention #6). The blob-decal fallback is
also our natural first rung: flattened sprite quads need no second pass at all.

## 4. Water: waves + reflections

Provenance: DSVM `lib/Water.lua:1-380` (design constants and their rationale), `lib/Water.lua:410-1124` (the shader's algorithms), `lib/VoxelScene.lua:719-823` (pass placement + the cast-drawn-twice trick).

### 4.1 Placement and structure

Water is cut out of the terrain mesh at build time and drawn as its own pass —
**after the terrain, before the characters** — because a mirror can only reflect
what is already drawn. Every other surface is opaque and drawn once by the scene
shader; water is the one translucent/reflective special case. The pass reads the
frame-so-far as a texture plus its depth, with the depth buffer detached for the
duration (can't sample a bound target), the shader doing the depth test itself;
water never writes depth (it's flat, never self-overlaps, and everything after
it stands on it by construction).

Three cost rungs, priced honestly: OFF = the flat animated water tile through the
ordinary shader; SKY = sky/sun/moon reflection only (a handful of ALU per pixel,
no extra buffer reads); FULL = adds the screen-space world march (~two dozen
depth-texture samples per water pixel). Every failure (no readable depth, shader
refused) falls down this ladder silently.

### 4.2 What it reflects, in resolve order

1. **The sky.** The reflected direction is projected through *the frame's own
   view-projection matrix as a point at infinity*, and the screen row that lands
   on indexes the same banded sky-gradient ramp the painted sky uses (same
   texture, same dither, same palette transform). Because both skies answer the
   same question with the same arithmetic, the lake meets the painted sky at the
   waterline with no seam, at any pitch/FOV/zoom. Indoors there is no sky and the
   water reflects its own color (a pond in a cave). (`Water.lua:636-673`)
2. **The sun/moon.** Hung by **angle**, not screen position — the reflected body
   usually projects off the top of the frame where screen distances are
   meaningless. The disc's angular radius is the painted disc's pixel radius
   converted through the camera FOV, so the disc on the water is exactly the disc
   in the sky (craters, dithered rim, twilight glow included). This doubles as
   the specular term: a low sun lays a broken gold path across the water out of
   the reflection itself, with no separate highlight model. (`Water.lua:20-28,686-711`)
3. **The world, screen-space.** The reflected ray is walked forward in world
   space, each sample projected through the frame's matrix, until it passes
   behind the depth buffer; then binary-refined onto the contact and the color
   read from a **copy of the frame as it stood before the water went down**.
   Steps grow geometrically (first step a few world px, ratio ~1.2, ~24 steps →
   reach of a few view-heights); a **thickness test** (crossing depth must not
   exceed a multiple of the step's own depth span) rejects rays that shot behind
   thin things — the classic screen-space smear of a foreground tree painted
   across the pond; hits fade at the frame rim and with distance travelled,
   handing over to the sky (true to life: distant water reflects haze, which is
   what the sky bands are). What's off-screen simply can't be reflected —
   accepted limitation of the technique. (`Water.lua:360-377,737-784`)
4. **The cast (people), drawn twice.** The game composites people OVER the
   world, water included (a surfing player sits on top), so people can't be in
   the frame before water draws — yet must appear in reflections. Fix: paint the
   whole cast into the *reflection copy only* before the water pass, then again
   normally after it. Both go through the same draw function so they can never
   diverge. The march finds them "honestly": sprites aren't in the depth buffer,
   so a ray aimed at one passes to the terrain behind and reads the copy there —
   where the sprite is already painted. (`VoxelScene.lua:719-745`)

Also: water **declines character shadows** via the shadow map's caster-type
channel — a hard sprite silhouette on a reflective surface reads as a sticker —
while still taking tree/building/cliff shadows. (`Water.lua:554-569`)

### 4.3 The waves: a voxel heightfield marched in the shader

The surface is a field of **one-world-pixel-wide columns at whole-pixel
heights** — waves quantized to the same unit as everything else in the voxel
world, "what water made of pixels should look like". No extra geometry: the mesh
stays one flat quad per tile, and the fragment shader walks the view ray down
through the wave slab (relief mapping, ~16 samples) taking the first column top
it falls below. Whether the ray *entered* a column (side face) or *fell through*
in place (top face) decides which face shade it wears — crests get lit and
shaded flanks like real solids, tall bars occlude short ones for free, the field
parallaxes against the plane. (`Water.lua:184-213,844-926`)

Design decisions with reasons, all reusable:

- **Wave model = 3 crossing sine trains with one dominant** (weights roughly
  60/30/10). Equal weights read as "soup" — patchy interference blobs with no
  travel; a dominant train gives crests a direction and a line. Dominant
  wavelength ~5 tiles; near pixel-scale wavelengths read as static.
  (`Water.lua:216-238`)
- **Two slow modulators on the dominant train only**: an amplitude *swell*
  (wave sets — a few tall, then a lull; travels slower than the crests, as real
  wave groups do) and a phase *bend* (crest lines bow off-line instead of ruling
  across the whole lake). Wavelengths 4–5x the carrier so neither reads as a
  wave itself; modulating the minor trains too is soup again. (`Water.lua:240-275`)
- **Stepped time.** The field advances in discrete steps (~12 steps/sec, a clean
  divisor of 60, slightly under classic pixel-art animation rate), each step
  moving the dominant train exactly one world pixel; the phase rate is *derived*
  from the train constants so changing a wavelength can't desync the speed. A
  quantized surface that slides smoothly betrays that the quantization is
  cosmetic. (`Water.lua:277-307`)
- **Normals from the smooth field, not the stepped one.** Integer column heights
  give integer differences → the reflected ray can only take ~5 directions →
  the moon (a 2°-wide disc) is stepped over entirely; the lake goes dark with
  odd flaring columns ("confetti"). So the *shape* stays quantized (it's what
  you see) while the *normal* is read off the underlying smooth field at the
  column's integer coordinate — quantized in space, continuous in value; the
  glitter path returns without softening one edge. (`Water.lua:813-841`)
- **Texture lookup per column, not per fragment** — a column shows the texel of
  art standing at its own world position (one world px = one atlas texel), so
  the art can't swim with camera motion and neighboring fragments can't
  disagree; offsetting the fragment's own UV by the parallax instead peppered
  the surface with noise. (`Water.lua:928-955`)
- **March stride = one screen pixel of surface, floored at one world pixel.**
  Finer buys nothing the screen resolves; coarser skips columns
  fragment-dependently (pepper noise). Early-out when the whole slab projects
  under a pixel (steep camera) — the cost only lands where it shows.
  (`Water.lua:308-346,887-901`)

### 4.4 Two stylizations argued from first principles

- **Fresnel, floored and softened.** Honest Schlick gives a near-overhead camera
  ~2% reflectivity — invisible. The floor is lifted far above the physical value
  and the exponent softened (roughly: floor a third, ceiling near one, power 2),
  keeping the *shape* (grazing camera → mirror) while making the bottom of the
  curve "a pond rather than a painted tile". (`Water.lua:113-129`)
- **The horizon lean.** A steep camera's reflection points nearly straight up:
  darkest sky bands, no sun, and a screen-space ray that exits the frame in two
  steps — all *correct*, and together "a lake with nothing in it". So as the
  camera steepens past the pitch where the horizon leaves frame, the reflected
  ray's **elevation** is eased toward the elevation the shallowest camera
  reflects at (bearing preserved; the per-column wave deflection re-added on top
  of the leaned *level* reflection, otherwise every column reflects one
  elevation and the waves flatten out of the reflection). Zero lean when the
  horizon is in frame, so the one visible join — the waterline — stays exact.
  Aim at a target elevation, not a fixed blend weight: a fixed weight lands each
  camera pitch somewhere different. (`Water.lua:131-182,1052-1085`)

### 4.5 3DS mapping

The screen-space march and per-pixel relief march are fragment-shader loops —
not expressible on PICA200. What transfers:

- The **tiered contract** (flat animated water → sky-only reflection → full) is
  exactly how we should structure it; our top tier is different tech.
- Sky-only tier: per-vertex reflected-direction → sky-ramp color, or an env/
  gradient texture via texture combiners + a fresnel factor per vertex. Cheap,
  and it is "most of what makes the model read as outdoors" (their FULL preset
  argues this — `main.lua:575-578`).
- Scene reflections, if ever: the classic **planar mirror pass** (render the
  scene mirrored about the water plane into a texture, sample with projective
  UVs) — a second geometry pass, honest cost, PICA-friendly; the cast-into-
  reflection-only compositing trick and the "water declines sprite shadows"
  taste rule both carry over unchanged.
- The wave-surface look (stepped voxel waves, dominant-train + swell/bend,
  stepped time) can be done as an actual low-res vertex-displaced grid over
  water areas (CPU-animated heights at ~12 Hz over a coarse grid, integer
  heights) — same aesthetic, vertex-side cost, no shader march. Wave normals
  from the smooth field still apply.

## 5. World curvature ("V-CURVE")

*(pending)*

## 6. Supersampled anti-aliasing

*(pending)*

## 7. Render-distance culling to the camera trapezoid ("FIT")

*(pending)*

## 8. Day/night atmosphere model + `map_atmosphere.lua` schema

*(pending)*

## 9. Billboard sprites: facing-frame selection vs camera angle

*(pending)*

## 10. 3D-staged battles + the over-the-shoulder camera

*(pending)*

## 11. Free-roam 1st/3rd person over the game's own movement machinery

*(pending)*

## 12. `voxel_heights.lua`: schema and role (values OFF LIMITS)

*(pending)*

## 13. Sprite-to-voxel methodology (band classification)

*(pending)*

## 14. Cheap-vs-expensive ranking of the polish items

*(pending)*
