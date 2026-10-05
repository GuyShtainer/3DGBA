# Phase-31 diorama research — the pokeemerald-3d "HD-2D" presenter, distilled

**Status: skeleton — sections being filled incrementally (2026-09-01).**

Clean-room idea-level distillation of `tripplyons/pokeemerald-3d` (archived at
`projects/_reference/pokeemerald-3d/`, study-only — unlicensed pret-derived fork; see
`docs/kb/voxel-diorama-reference.md` §Licensing). Everything below is re-expressed in our
own words: ideas, algorithms, buffer semantics, parameter roles, pipeline order.
**No code text, no comments, no data-table values are copied.** Provenance pointers
(`file:line`) mark where each idea lives in the reference so a later audit can trace it;
they are pointers, not quotes. This document is the sole input for the implementer — the
reference code is not to be opened.

Reference files read: `src/wasm_display.c` (~1.5k lines, game-side export layer),
`web/presenter.js` (~1.1k lines, WebGPU/WGSL presenter), `web/app.js` (only where it
configures the presenter). Our side: `projects/3DGBA/source/gbacore.h` (what the mGBA
wrapper exposes today).

## Contents

1. [The architecture in one paragraph](#1-the-architecture-in-one-paragraph)
2. [Side-channel buffer inventory (game side)](#2-side-channel-buffer-inventory-game-side)
3. [How the game side fills each buffer](#3-how-the-game-side-fills-each-buffer)
4. [The presenter pipeline, stage by stage](#4-the-presenter-pipeline-stage-by-stage)
5. [Parameter roles (sliders and tunables)](#5-parameter-roles-sliders-and-tunables)
6. [The shipped negative result: heights = 0](#6-the-shipped-negative-result-heights--0)
7. [BUFFER-BY-BUFFER: CAN WE PRODUCE IT?](#7-buffer-by-buffer-can-we-produce-it)
8. [Implications for our citro3d port](#8-implications-for-our-citro3d-port)

---

## 1. The architecture in one paragraph

The decomp'd game, compiled to WASM, contains a **software re-implementation of the GBA
PPU** plus an extra "HD-2D" renderer that, once per frame, fills a family of side-channel
buffers: a large **overscanned world image** (the map rendered well past the visible
viewport, sampled straight from map data rather than from the 512-px BG tilemaps), a
**per-pixel layer-ID map** and **per-pixel BG-priority map** over that same world image, a
**separate sprite pass** (sprites composited alone, never baked into the world, plus a
per-sprite texture atlas with descriptors and foot anchors), recovered **map-space camera
coordinates**, and a per-pixel **height buffer that is deliberately all zero** (§6). The
browser presenter (WebGPU/WGSL) consumes these buffers to build a 3D scene: the world image
becomes a textured terrain plane viewed by a real perspective camera, the layer/priority
maps drive which pixels stand up versus lie down and how sprites depth-sort against BG
detail, sprites become depth-tested upright billboards with projected blob/mask shadows,
and a post stack (bloom, color grading, fog, tilt-shift) produces the diorama look. A
**scene-kind gate** on the game side says each frame whether the HD-2D projection is
trustworthy; when it is not (menus, battles, scanline effects, semi-transparent objects),
the presenter falls back to showing the flat canonical frame.

Split of labor: everything that needs *game knowledge* (map layout, metatile attributes,
camera state, tileset animations) happens game-side in C; everything *visual* (mesh,
camera, lighting, post) happens presenter-side. That boundary is exactly where our 3DS
port boundary would sit (emulator-side extraction vs citro3d presenter).

## 2. Side-channel buffer inventory (game side)

All buffers are exported via getter functions returning pointer + size
(`wasm_display.c:1397-1560`). Dimensions and semantics:

Coordinate conventions used throughout: "screen" = the GBA's 240x160 viewport;
"world" = the overscanned atlas; "map" = metatile grid coordinates (16 px per metatile,
8 px per tile, each metatile = 2 vertical halves x 4 quadrant tiles). The world buffer is
**800x768** with the 240x160 viewport centered inside it — i.e. overscan margins of
(800-240)/2 = 280 px left/right and (768-160)/2 = 304 px top/bottom
(`wasm_display.c:24-31`). Layer IDs are one-hot bit codes: BG0=0x01, BG1=0x02, BG2=0x04,
BG3=0x08, OBJ=0x10, backdrop=0x20 (`wasm_display.c:37-43`).

| # | Buffer | Shape | Semantics |
|---|---|---|---|
| B1 | Canonical display RGBA | 240x160x4 | Full software-PPU render of the frame (all BGs incl. BG0 text, sprites, windows, blending). The flat fallback image and the source for the viewport patch (B2 center). `wasm_display.c:81,1552` |
| B2 | World RGBA (overscanned) | 800x768x4 | The overworld rendered from map data across the whole overscan; alpha 0 = "no map here" (e.g. beyond an indoor layout's edge). Central 240x160 is then overwritten with the canonical live-tilemap render so door animations / tile overrides / windows / scanline effects stay exact. `wasm_display.c:82,1256-1289,1292-1324` |
| B3 | World layer IDs | 800x768x1 | Per pixel: which conceptual GBA layer "won" that pixel (one-hot code above; backdrop where nothing drew). Outside the viewport it is derived from **metatile layer type**, not from real tilemaps — see §3.4. `wasm_display.c:83,1278` |
| B4 | World BG priorities | 800x768x1 | Per pixel: the winning BG's hardware priority (0..3) read from that BG's control register; 4 = backdrop; 0xff (inside the viewport patch only) = "OBJ excluded here by the GBA window" — a sentinel the presenter can use to keep sprites from drawing over window-masked UI. `wasm_display.c:84,1279-1286,1312-1321` |
| B5 | World height | 800x768x1 (s8) | Per-pixel terrain height. **Written as constant 0 by design** — the shipped negative result, §6. `wasm_display.c:85,1199-1204` |
| B6 | World pixel origin X/Y | 2 x s32 | Map-space pixel coordinate of the world buffer's top-left texel: player metatile pos x16 minus overscan offset plus recovered camera offset. Purpose: lets the presenter anchor procedural effects (shader noise) to **map space** so they don't swim as the overscan atlas slides with the camera. `wasm_display.c:86-87,1235-1238,1437-1445` |
| B7 | Grid offset X/Y | 2 x u32 | Sub-tile (mod 8) alignment between the world atlas and the BG1 scroll registers, so the presenter can snap tile-grid-aligned effects to real tile boundaries. `wasm_display.c:1427-1435` |
| B8 | Object pass RGBA | 240x160x4 | All sprites composited **alone** over transparency, priority-ordered, window-masked, with GBA blend effects applied — except semi-transparent (OBJ-mode-1) sprites which keep their raw color and carry an alpha channel scaled from the blend register's source coefficient, so the presenter can do the blend itself in 3D. `wasm_display.c:88,786-797,296-309` |
| B9 | Object-pass ID map | 240x160x1 | Per pixel of B8: the OAM index (0..127) that produced the pixel; 0xff = none. Lets the presenter cut the flat object pass apart per sprite. `wasm_display.c:89,795-796,313` |
| B10 | Object source atlas | 128 slots x 64x64x4 | Per-OAM-entry texture: each visible sprite's **untransformed** pixel block (max GBA sprite is 64x64), rendered from OBJ VRAM with palette applied, no affine transform baked, alpha as in B8. Slot order = descriptor order. `wasm_display.c:90,684-784` |
| B11 | Object descriptors | 128 x 16 s32 | Per atlas slot: OAM index; screen x; screen y; anchor x; anchor y (anchor = bottom-center "feet" point, doubled extents accounted for double-size affine); native w,h; drawn w,h; BG priority (0..3); logical sprite id (game engine's sprite table id, 0xff if unmanaged); affine flag; affine matrix a,b,c,d (identity encoded as 256/0/0/256, i.e. 8.8 fixed point). `wasm_display.c:91,735-760` |
| B12 | Object anchors | 128 x 2 s16 | Same bottom-center anchor per raw OAM index (also filled during the flat sprite pass, so it exists even for unmanaged entries). `wasm_display.c:95,616-617` |
| B13 | Object priorities | 128 x 1 | Per raw OAM index: its BG priority bits. `wasm_display.c:96,610` |
| B14 | Display layer IDs | 240x160x1 | B3's equivalent for the canonical display render (includes OBJ and BG0 codes). `wasm_display.c:93,1467` |
| B15 | Display object IDs | 240x160x1 | B9's equivalent for the canonical render. `wasm_display.c:94,1477` |
| B16 | Scene kind | u32 fn | Per-frame gate: 1 = HD-2D projection is trustworthy this frame, 0 = present the flat canonical frame. Conditions in §3.6. `wasm_display.c:1365-1395` |

## 3. How the game side fills each buffer

### 3.1 A software GBA PPU, register-faithful

The base layer (`wasm_display.c:141-818`) is a from-scratch per-pixel GBA compositor that
reads the real memory-mapped state: DISPCNT mode/enable bits, per-BG control registers
(char base, screen base, size, 4/8-bpp), scroll registers, window registers (WIN0/1 H/V
ranges with wraparound semantics, WININ/WINOUT masks), blend registers (all three effects:
alpha, brighten, darken, with coefficient clamping), text and affine BGs, bitmap modes
3/4, and full OAM sprite rendering (shapes/sizes table, flips, 1D/2D mapping, affine with
double-size mode, priority loops 3->0, per-sprite palettes). Two subtleties worth keeping:

- **Scanline effects via HBlank DMA capture** (`wasm_display.c:158-214`): pokeemerald does
  per-scanline register tricks (e.g. window sweeps) by pointing a repeating 1-unit HBlank
  DMA at an I/O register. The re-renderer snapshots all four DMA channels, detects that
  exact configuration, and — when asked for a register's value "at scanline y" — reads
  the DMA source array at index y-1 instead of the live register. This is how a whole-frame
  re-render can still honor per-scanline state.
- **Sprite coordinate recovery** (`wasm_display.c:540-567`): hardware OAM x/y are 9/8-bit
  wrapped values; the engine's managed sprites expose true signed logical coordinates via
  helper exports, and only entries beyond the managed prefix fall back to unwrap-by-range
  heuristics (x > 240 means negative, etc.).

### 3.2 The world render: sample the MAP, not the tilemaps

`RenderHd2dWorld` (`wasm_display.c:1207-1290`) fills B2/B3/B4/B5 by iterating every world
pixel and asking "what map content is here?" directly from map data structures — because
the GBA's BG tilemaps only cover ~2 metatiles beyond the screen, while the overscan needs
~35 metatiles. Per pixel:

1. Convert world pixel -> signed screen coordinate -> map metatile coordinate using the
   recovered camera offset (§3.3) and a floor-division-by-16 that is correct for negatives
   (`wasm_display.c:836-839,1268-1269`).
2. Resolve the metatile id at that map coordinate (`ResolveHdMapSample`,
   `wasm_display.c:974-1035`): inside the current map, prefer the game's live working copy
   of the map grid (so script-modified tiles are honored), falling back to the map-grid
   getter; outside it, walk the map-connection list (N/S/E/W with per-connection lateral
   offsets) and index the neighbor layout's metatile array. Diagonal gaps and no-connection
   areas resolve to the engine's out-of-bounds behavior. A per-frame coarse cache
   (metatile-granularity sample grid sized world/16 + 2 in each axis,
   `wasm_display.c:28-29,899-900,1246-1255`) avoids re-resolving per pixel.
3. Fetch the metatile's **attributes** word and unpack its **layer type**
   (`wasm_display.c:1120-1127,1158-1173`). Layer type maps each metatile's two draw halves
   onto conceptual BGs: SPLIT -> bottom=BG3, top=BG1; COVERED -> bottom=BG3, top=BG2;
   NORMAL -> bottom=BG2, top=BG1. This is the key trick behind B3: the per-pixel layer ID
   outside the viewport is *reconstructed from map semantics*, mirroring exactly how the
   game itself assigns metatile halves to hardware BGs when it does draw them.
4. Render the metatile pixel (`wasm_display.c:1038-1118`): pick the half (bottom first,
   top overdrawn), quadrant, and 8x8 tile entry from the tileset's metatile table; read
   tile pixels from VRAM when the tileset is the currently-loaded one, else from a
   **tileset cache** (§3.5); apply palette; color-0 = transparent. Apply the current blend
   effect (brighten/darken/alpha vs the layer beneath) so overscan pixels match the
   viewport's look during fades and weather (`wasm_display.c:856-890,1186-1197`).
5. Write RGBA (alpha 0 where no map exists — indoor maps get hard borders,
   `wasm_display.c:1149-1156,1277`), the winning layer code, the winning BG's priority
   bits from its control register, and height=0.

Then `CopyCanonicalWorld` (`wasm_display.c:1292-1324`) re-renders the *viewport* the
normal way (live tilemaps, no BG0, no sprites — `RenderTiled(dispcnt, FALSE, FALSE)` at
`wasm_display.c:1358`) and pastes it over the world buffer's center, so anything the map
data cannot know about (door-opening tile overrides, live tile animation state, window
effects, scanline tricks) is exact where the player is looking. Pixels whose world alpha
was 0 are skipped so the flat border filler around indoor maps is not promoted to terrain.
Inside the patch, the priority map also encodes the OBJ-window mask (0xff sentinel).

### 3.3 Camera/pan recovery to map space

The GBA scroll registers only carry scroll mod 512 and mix in scripted pans and shakes.
The renderer reconstructs a **full signed camera offset in map-pixel space**
(`wasm_display.c:1213-1234`) by combining: the engine's own camera-with-pan query (used
to place OAM), the field camera's intra-step movement accumulator, and the scripted pan
recovered as (total camera pixel offset - sprite coordinate offset). The mod-16 residue
of the GPU value is kept but rebased onto the signed components, preserving the standard
+32-style vertical pan and large camera shakes so entity feet stay on their world tiles.
One correction: the engine advances its saved metatile position at the *start* of a
16-px camera step, so while a step is in flight the offset is corrected by one metatile
in the direction of motion to avoid double-stepping (`wasm_display.c:1228-1234`).
Finally the world buffer's map-space pixel origin (B6) = saved player metatile position
x16 - overscan offset + camera offset (`wasm_display.c:1235-1238`).

### 3.4 The object pass: sprites as first-class citizens

Sprites are **never baked into the world image**. Two structures are produced per frame:

- **Atlas + descriptors** (B10/B11, `RenderObjectSources`, `wasm_display.c:684-784`):
  every visible OAM entry gets a 64x64 atlas slot holding its untransformed texel block
  and a 16-word descriptor (§2 B11). The affine transform is *not* baked into the texels;
  the matrix rides in the descriptor so the presenter can apply it on the billboard quad.
  OBJ-window sprites (mode 2) are skipped. Semi-transparent sprites get a real alpha
  channel derived from the blend source coefficient ((eva/16)*255,
  `wasm_display.c:667-682`) instead of being pre-blended against a background they will
  no longer sit on.
- **Flat object pass** (B8/B9, `RenderObjectPass`, `wasm_display.c:786-797`): the full
  sprite layer composited alone over transparency in correct priority order with windows
  and blending honored — a ready-made "all sprites" overlay plus a per-pixel OAM-index
  map to cut it apart. (The presenter mainly uses atlas+descriptors; the flat pass is the
  simpler alternative route and the fallback data.)
- **Anchors** (B12): each sprite's bottom-center point — the "feet" — computed from
  logical coordinates and size (double-size affine handled); this is the point the
  presenter plants on the terrain to give the billboard its world position and its
  shadow location.

### 3.5 Tileset cache for off-map content

Connected neighbor maps may use tilesets that are not loaded in VRAM. A small cache
(16 entries, `wasm_display.c:902-972`) holds each such tileset decompressed (LZ77 when
compressed), plus two palette sets: the authored palettes and a "display" copy that is
mutated by replaying the tileset's **animation callbacks** from the last cached frame
counter to the current one — so animated water/flowers in the overscan tick in sync with
the real map. A monotonic per-tileset animation frame counter exported by the engine
identifies how many animation steps to replay.

### 3.6 The scene-kind gate

`WasmDisplaySceneKind` (`wasm_display.c:1365-1395`) returns "HD-2D trustworthy" only when
ALL of: video mode 0; the main callback is the standard overworld callback (field
transitions with screen-space effects excluded); map type is a real map; **no HBlank-DMA
scanline effect is active** (those cannot be projected onto a plane); no blend effect
targets OBJ as source (billboards rendered from raw sources would get the wrong
treatment); and no OAM entry is in semi-transparent mode. Any of these -> present the
flat canonical frame instead this frame. The design stance: **fall back to exactness
rather than approximate the projection** — transitions, flashes and battle intros simply
play flat.

## 4. The presenter pipeline, stage by stage

Frame flow (`presenter.js:1051-1107`): if this frame is not "enhanced" (HD-2D mode on AND
scene-kind gate says yes, `app.js:913-916`), upload the canonical 240x160 frame and blit
it nearest-neighbor — done. Otherwise upload the side-channel buffers and run six passes
into a supersampled scene target (240x160 x renderScale, default scale 4,
`presenter.js:520-525`, `app.js` default render scale 4):

1. terrain pass (depth-tested, writes depth)
2. cast-shadow mask pass (offscreen mask)
3. shadow composite pass (mask onto scene)
4. billboard pass (depth-tested against terrain depth)
5. cinematic post pass (tilt-shift + bloom + grade + vignette)
6. flat UI overlay pass, then nearest-neighbor present.

### 4.1 Terrain: a tile-grid heightfield mesh (currently a flat plane)

CPU side (`presenter.js:728-793`): the world height buffer is max-pooled per 8-px tile
(grid aligned to real tile boundaries via the exported grid offsets, with one extra tile
of apron). For each tile it emits a top quad at its height plus **side skirt quads**
toward any lower N/S/E/W neighbor, each skirt textured by a half-texel-inset edge strip
of the world texture and carrying a fixed per-direction shade factor (roughly: west
darkest ~0.64, north ~0.68, east ~0.82, south lightest ~0.88 — a baked sun direction).
Mesh coordinates: x/z in world-atlas pixels centered on the atlas, y = up (height).
**Because the height buffer is all zero (§6) this degenerates to a flat textured plane —
but the extrusion machinery is fully built and waiting for a better height source.**
Rebuilt every frame (cheap at ~100x97 tiles).

The vertex shader (`presenter.js:146-170`) applies a hand-rolled perspective: a camera
placed at a fixed height (~480 world px) above the plane center, pitched by
(max tilt ~45.4 degrees) x perspective-slider; view depth = height - y*cos - z*sin;
focal length = height / (1 + zoomOut) where zoomOut is a smooth cubic-ish function of
the zoom slider (`presenter.js:152-154,804-818`); NDC x/y divide by depth (w=depth).
Depth buffer value is a near/far-normalized distance (near ~32, far ~1024,
`presenter.js:479-482`). A per-vertex **fog factor** ramps up with view depth beyond the
camera distance (reaching 1 over a couple hundred px, `presenter.js:168`).

The fragment shader (`presenter.js:201-245`) does the "shading" look, all anchored so
nothing shimmers frame-to-frame:

- **Pixel-art relief**: compare each texel's luminance to a 4-tap neighborhood 2 px away
  and a "sunward" tap up-left; darker-than-surroundings pixels get a small ambient +
  directional darkening (~10% max) — reads the authored art as shallow relief instead of
  inventing geometry (`presenter.js:209-221`).
- **World-anchored dapple**: 3-octave value noise sampled at map-space coordinates
  (world pixel origin B6 + texel position) — broad cloud/canopy-like luminance modulation
  (~7%) plus a tiny per-texel grain (~1%), both static in map space so camera motion
  never makes them swim (`presenter.js:176-195,226-230`).
- **Sun sweep + split toning**: a gentle diagonal brightness gradient across the atlas
  (up-left = sunward), and a warm/cool color split — cool blue-ish multiplier in shadowed
  areas, warm in sunlit ones (a few percent each, `presenter.js:229-236`).
- **Saturation lift** (~1.12x), **distance haze** (mix toward a pale blue at up to ~5%
  by the fog factor), and slight darkening near the atlas edge (`presenter.js:238-243`).
- Everything lerps from the raw texel by the shading slider (`presenter.js:244`).

### 4.2 Billboards: sprites stood upright in the scene

CPU side (`buildFrameLayers`, `presenter.js:933-1035`):

- **UI extraction first**: every pixel whose canonical layer ID has the BG0 bit becomes
  part of a flat UI texture (BG0 = textboxes/menus in this engine). If a UI plane exists,
  priority-0 sprites are checked for overlap with UI pixels; overlapping ones are copied
  (via the object-pass ID map) into the UI texture and *excluded* from billboarding — so
  an arrow cursor on a textbox stays flat with the textbox (`presenter.js:935-968,1009`).
- **Group anchor**: all OAM pieces sharing one engine sprite id take the group's lowest
  foot line as their common anchor row, so multi-piece actors stand on one line
  (`presenter.js:970-1000`).
- Sprites are sorted back-to-front by BG priority then OAM index and emitted as one quad
  each (`presenter.js:982-1024`): the anchor point (feet) is projected with the same
  camera math as the terrain (CPU mirror of the vertex shader, `presenter.js:804-818`),
  then the quad is laid out in screen space around that projected point scaled by the
  projected scale — i.e. **the sprite stays a crisp upright screen-space rectangle whose
  position and size come from its feet's place in the 3D scene**. Each vertex carries
  the atlas layer, source/draw sizes, the affine matrix, screen origin, OAM id and
  priority.

Fragment shader (`presenter.js:295-330`): samples the object atlas (applying the GBA
affine matrix in 8.8 fixed point per pixel, exactly as OAM hardware would), alpha-tests,
and resolves **occlusion by overhead map art via the BG-priority texture**, not the depth
buffer: it reads B4 at the pixel's world-atlas position; sentinel 0xff (OBJ window)
discards; a BG priority strictly lower (= drawn above on GBA) than the sprite's priority
marks the pixel occluded. Occluded pixels are **not discarded** — they draw as a
desaturated pale-tinted translucent silhouette (~60% alpha), a deliberate choice to keep
a readable whole entity behind overhead terrain instead of slicing it into fragments
(`presenter.js:303-327`). Depth output: while inside the priority atlas, the fragment
depth is forced to nearest (0.0) so terrain never z-fights the sprite — the GBA priority
model is the sole occlusion authority there; outside the atlas the projected depth is
used (`presenter.js:328`). Painter's order plus less-equal depth testing handles
sprite-vs-sprite layering.

### 4.3 Cast shadows: projected, receiver-masked silhouettes

Three cooperating pieces:

- **Foot rows** (`measureObjectFootRows`, `presenter.js:821-856`): for each sprite that
  is a real overworld actor (see §5 object-event flags), scan its atlas pieces bottom-up
  for the lowest row with any opaque pixel — the *visible* foot line (better than the
  OAM box bottom, which includes transparent padding).
- **Shadow geometry** (`buildProjectedShadows`, `presenter.js:858-931`): per actor
  sprite piece, emit a quad lying **on the ground plane**: each source-sprite point is
  dropped to the ground at an offset proportional to its height above the foot row —
  lateral skew ~0.16x and away-shear ~1.65x per pixel of height — producing an elongated,
  slanted silhouette like a low sun behind the camera; ground y comes from the terrain
  height lookup (flat today). The quad is padded by ~3 source px so the blur apron isn't
  clipped, and clamped at the foot row so the shadow starts exactly under the feet
  (`presenter.js:897-914`). Rendered with the same camera projection, depth-biased just
  above the terrain (`presenter.js:377-382`).
- **Mask + composite**: the shadow pass renders silhouette alpha into an offscreen mask
  using **max blending**, so overlapping shadows union instead of stacking darker
  (`presenter.js:590-598`). Alpha per pixel is a 13-tap blur of the source sprite's alpha
  (center + ring1 + ring2 weights, `presenter.js:422-441`). Receiver masking uses B4: no
  shadow on OBJ-window pixels or on map art drawn above the sprite's priority
  (`presenter.js:414-420`). The composite pass then multiplies the scene once with a
  dark cool blue at ~half strength, scaled by the shading slider (`presenter.js:456-463`).

### 4.4 Cinematic post: tilt-shift, bloom, grade, vignette

One fullscreen pass over the supersampled scene (`presenter.js:33-123`):

- **Tilt-shift**: blur amount = max of (a) distance of uv.y from a focus line at ~0.53
  (slightly below center = the playable plane), smoothstepped over a band roughly a
  quarter of the screen tall, and (b) a weaker radial term near the corners. Blur = 8-tap
  ring average mixed in by ~0.82 x blur amount; the ring radius scales with render scale
  so the blur describes world detail, not device pixels (`presenter.js:68-88`).
- **Bloom**: bright-pass (luminance smoothstep from ~0.57 to ~0.90) sampled on a wider
  ring, added at ~23% — glow for water/roofs/windows without lifting dark lines
  (`presenter.js:90-102`).
- **Grade**: saturation ~1.14, contrast ~1.10 around mid-gray, split toning (cool
  shadows, warm highlights), an elliptical vignette (~31% at corners), a warm key-light
  glow anchored upper-left, and a sub-1% dither to hide banding
  (`presenter.js:104-119`).
- The whole pass lerps from the ungraded scene by the shading slider
  (`presenter.js:121`).

### 4.5 UI overlay + present

The flat UI texture (BG0 + captured priority-0 sprites) is alpha-tested and drawn **after**
grading, so menus and textboxes stay pixel-crisp and un-cinematic (`presenter.js:466-476,
1097-1098`). Finally the graded scene is presented with nearest-neighbor sampling.

## 5. Parameter roles (sliders and tunables)

Three user sliders, each 0..1 (UI shows percent), persisted and URL-overridable
(`app.js:64-73,104-109,225-227,1173-1189`); defaults: shading 0.5, perspective 0.5,
zoom 1.0 (`app.js:71-73`).

| Parameter | Where it lands | Role |
|---|---|---|
| **shading** | camera uniform slot z (`presenter.js:1063`) | Master *look strength*: lerps terrain lighting/toning, shadow composite opacity, and the entire cinematic pass between "raw texels" (0) and full effect (1). Geometry (tilt, billboards) unaffected. `presenter.js:244,462,121` |
| **perspective** | uniform slot x | Scales the camera pitch: effective tilt = slider x max tilt (~45.4 deg). 0 = top-down flat (classic view geometry), 1 = full diorama tilt. Used identically in terrain/shadow vertex shaders and the CPU billboard projection. `presenter.js:148,365,805` |
| **zoom** | uniform slot y | Zoom-out amount: mapped through a smooth cubic to a focal-length divisor (focal = camera height / (1+zoomOut)), so higher = wider view of the overscan world. `presenter.js:153-154,808-809` |

Mode switching cross-fades: toggling HD-2D animates all three rendered values from 0 to
their set values (and back) over a fixed duration, so the plane tilts up smoothly
instead of snapping (`app.js:270-292`). When the scene-kind gate fails mid-play, the
presenter simply shows the flat frame that frame (no animation — instant exactness).

Fixed internal tunables worth knowing as *roles* (values are theirs, magnitudes rough):
camera height ~480 px above the plane, near ~32, far ~1024 (`presenter.js:479-482`);
supersample scale 4x; focus line at ~53% screen height; fog onset past the camera
distance over ~260 px; skirt shade factors per compass direction; shadow shear ~1.65
and skew ~0.16 per height pixel; shadow tint dark cool blue at ~52% max.

One more input: **object-event sprite flags** — the presenter reads the game's
object-event table directly from WASM memory (16 entries; an active bit and the engine
sprite id per entry) to mark which sprite slots are real overworld actors
(`app.js:902-908`). Only those get foot-row measurement and cast shadows; weather
particles and effect sprites do not.

## 6. The shipped negative result: heights = 0

The entire buffer/mesh/skirt machinery for a per-pixel heightfield exists — and the game
side fills the height buffer with a constant zero (`wasm_display.c:1199-1204`). The
author's stated rationale, paraphrased: the game's collision map, metatile behaviors,
and per-tile elevation values are **gameplay masks, not a visual height map**. Deriving
geometry heights from them produces two concrete failure classes:

1. **It tears authored art apart.** Buildings and decorations are painted *across* tiles
   whose gameplay attributes differ (a door tile is passable, the wall beside it is not;
   a roof edge and the sky behind it share no collision distinction that matches the
   drawn silhouette). Extruding by those masks cuts through the middle of coherent
   pixel-art objects.
2. **It mis-places actors.** Walkable cells on top of "elevated" terrain (mountain-top
   paths) would rise with the terrain while the actor logic still thinks in flat screen
   rows — actors visually sink into or float off the geometry.

So the shipped choice is: **keep all authored pixel-art depth on a single coherent
perspective plane** and get the 3D feeling from tilt + upright billboards + shadows +
DOF instead. The one allowed relief is *shading-only* (the luminance-neighborhood trick,
§4.1) which darkens pixels without moving them.

The boundary this draws — and it matches our own earlier findings
(`docs/kb/voxel-diorama-reference.md` §5, Addendum): per-pixel or per-tile height
*derived automatically from gameplay data* is a dead end. Real volume requires either
hand-tuned per-tile height data (DramaticShape's approach) or semantic per-tile
classification plus structure grouping so whole buildings extrude as units
(pokeemerald-multiplatform's approach). Anything in between produces torn art. For a
first citro3d phase, the tilted-plane-plus-billboards recipe is the proven,
zero-authoring-cost look; the heightfield mesh path stays available as machinery if we
later feed it *classified structure* heights rather than raw masks.

## 7. BUFFER-BY-BUFFER: CAN WE PRODUCE IT?

*(to be written — per buffer: reconstructable from VRAM/OAM? from EWRAM map semantics?
from the composited frame? or not at all; both routes spelled out where both exist)*

## 8. Implications for our citro3d port

*(to be written)*
