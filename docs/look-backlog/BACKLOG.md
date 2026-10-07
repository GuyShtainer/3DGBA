# Voxel look backlog (Guy's notes from Emerald screenshots, 2026-10-06)

Guy, on the Emerald 3D look across several Hoenn places: "gorgeous but not perfect, but it will do for now". The open
points, with the cause found in the code:

| # | Guy's note | Cause (checked 2026-10-06) | Fix direction | Size |
|---|---|---|---|---|
| L1 (DONE, see below) | "the trees near the entrance to the cave in Dewford are flat on top" | `voxel_tree.c` `VoxelTree_Part` knows only the General-tileset tree metatiles (0x1D4-0x1E7, 0x1EC/0x1ED, the small trees 0x016/0x017/0x0C6/0x0C7/0x1F4/0x1F5). The Route 106 / Dewford shrubs on sand are metatiles 0x124, 0x239, 0x242, 0x243 (`romgen author emerald.gba art 22 42 17 12 3`). They are not in the table, so they render as flat ground art. Seen in `docs/phase33-romgen/evidence/s37-full-r106.png` (foreground). | Scan all outdoor layouts for foliage metatiles missing from the table (the T1 foliage rule: at least 50 % foliage pixels), then add them as VOXEL_TREE_SMALL or as a new "shrub" part. Emerald table only; the FRLG table comes from T1. | small |
| L2 (DONE, see below) | "the grass is still flat" | Upstream renders tall grass as ground texture. Only the rustle field effect is a sprite. There is no grass geometry. | Tall-grass cells (behaviour set) get low crossed or billboard blade cards from the cell's own art, swaying optionally. Budget: many cells per screen, so instance it cheaply and test the frame time on hardware. | medium |
| L3 (DONE, see below) | "the lighting is somewhat off. The shadow has a single direction which doesn't add up: the NPCs are lit from the front" | `voxel_lighting.h`: a fixed sun in the northwest (`VOXEL_SUN_DX 0.85`, `DZ 0.55`), so shadows fall southeast, towards the camera side. The GBA sprite art is shaded as if lit from the front and above. The baked terrain shadows and the sprite shading disagree. | Move the sun in front of the scene (south, high), so cast shadows fall away from the camera. Check that `voxel_lighting.c`'s ray march does not assume a northwest sun (its reach box is built from DX/DZ signs). | small-medium |
| L4 | "maybe for future: live lighting that changes with the day (use the 3DS clock or the RTC for RSE)" | The lighting is baked once into the chunk colours. | Sun angle and colour from the time of day. The source is the 3DS clock (osGetTime), which is also what mGBA's RTC reads for RSE, so all games agree. FRLG have no RTC, so they use the 3DS clock. Re-bake chunks incrementally when the sun has moved enough (minutes, not frames), plus a global tint (dawn, day, dusk, night). Future, after L3. | large |

L1 finding (lead, 2026-10-06, `art 22 42 17 12 3`): the Route 106 / Dewford shrubs are ONE-CELL bushes (16x16) over
sand or grass, not two-tile trees. The tree pass draws crowns from the fixed embedded 64x64 `voxel_trees.bin` (the
General tree only), so adding 0x124/0x239/0x242/0x243 to the table would draw the wrong art. Needed: a new shrub part
(`VOXEL_TREE_SHRUB`) that keys the cell's own atlas art (foliage pixels kept, the ground colour made transparent) into
a rounded card standing on the cell, with the cell's ground drawn under it. The same part covers the FRLG round bush
(0x005) and any later one-cell foliage. Emerald table rows only through the profile; FRLG rows added once checked.

**L1 DONE (2026-10-06, branch of agent-af36f586).** One-cell bushes stand as a rounded card of their own art.
- **Design.** A new shrub part (`VOXEL_TREE_SHRUB`, `voxel_tree.c`). In every chosen metatile the bush is the metatile's
  UPPER layer (palette index 0 transparent) and the ground its LOWER layer, so the keying is the GBA layer split itself:
  the atlas gets 32 shrub slot pairs after the cut tiles (`VOXEL_SHRUB_GROUND(k)` = lower layer only,
  `VOXEL_SHRUB_LEAVES(k)` = upper layer only on a transparent background), composed once per atlas build and refreshed
  by the existing animation path. Drawn in the terrain pass (atlas texture), not the tree pass (fixed tree texture).
  `VoxelMesh_TileUV` gives a shrub cell its ground slot, so the flat, relief and wall paths all draw the ground; the
  bush is one card leaning back 60° from the cell's south edge (top 0.87 tiles), lit as a rounded crown, lifted by the
  cell's relief. Border shrubs (Route 106's south wood) are drawn like border trees: flat ground plus the card, no
  two-course box.
- **Tables** (`GpShrub`, per game profile, keyed by secondary-tileset ROM address because a secondary id means nothing
  without its tileset): Emerald 11 entries, FireRed/LeafGreen 3 each. Measured with `romgen author ROM shrubs` and checked
  by eye; provenance in `docs/PROVENANCE.md`.
- **Cost.** 6 extra vertices per shrub cell (ground 6 as before + card 6 = 12); no new texture, no per-frame keying.
- **Evidence.** `evidence/l1-em-r106-before.png` / `l1-em-r106-after.png` (Azahar N3DS, save `emerald-r106`, the Granite
  Cave mouth: the bush wood south of the route was a flat green carpet, now rows of standing bushes);
  `evidence/l1-fr-pallet-before.png` / `l1-fr-pallet-after.png` (FireRed, save `firered-pallet`: identical, Pallet has no
  one-cell bushes in view; a no-regression check on flowers, fences and signs). The FR round bush 0x005 is in Viridian
  (layout 79, column x=19, y=7..15), off screen at the `firered-viridian` save spawn; not shot.
- **First-cut lesson.** The first build changed nothing on screen at the cave: the bush cells there carry relief (the
  shrub path bailed to flat) and the field the camera sees is the map BORDER, not route cells. Both were fixed before
  the evidence above; an emulator shot is the only check that caught it.
- **Out of scope, still flat:** big trees and pines drawn in SECONDARY tilesets (Fortree/Rustboro stacked pines, the
  large-tree quadrants of several secondary tilesets) - they need 2x2/1x2 parts from atlas art, not the one-cell card;
  multi-cell hedges (FR 0x13D/0x13E/0x13F); Emerald 0x2C0 in tileset 0x083DF734 (an ambiguous spiky plant). Tall grass,
  flowers and fences are excluded by design (L2 owns grass).

Order: L1 → L3 → L2 → L4, after the FRLG milestones M0/M1 that are running now. Each one is emulator-checked, then
hardware-checked (frame time).

## Future: camera freedom (Guy 2026-10-06, after the Gen1Recomp voxel mod)

Reference: Gen1Recomp (a native Lua/LÖVE rewrite of Red/Blue/Yellow, not an emulator) with DramaticShapeVoxelMod:
pitch presets, third-person follow, free-fly, experimental first person (v1.5.1), VR, day/night. Inspiration only,
no code from it (licence unknown; the original repo was taken down, a fork exists).

| # | Step | What it needs here | Size |
|---|---|---|---|
| C1 | Pitch presets (gentle to steep diorama) | `voxel_camera.c` already has pitch/yaw/distance; the pitch is a setting today. Bind a button to cycle presets. | small |
| C2 | Yaw in 90° steps (look east/west/south) | The world mesh is fully 3D, but: sprites are 4-direction billboards (remap the facing relative to the camera), building backs/sides were authored from the front only, the tilt-shift focus and the entity AABB assume a north view, and D-pad input must be rotated to match the camera. Lighting ties into L3/L4. | medium-large |
| C3 | Free yaw / third-person follow | C2 plus smooth sprite-facing selection and an occlusion fallback (fade walls between the camera and the player). | large |
| C4 | First person (experimental) | Flat sprite cards seen edge-on, tile-step movement from the GBA game, longer draw distance against the New-3DS frame budget (Azahar shows ~21-25 fps today). Prototype only, after hardware frame-time numbers exist. | large, risky |

Order: after L1-L4. Each step is checked in Azahar, then on hardware (frame time).

L1 lead check (2026-10-06, after merge 7c9beec): FireRed Viridian, warp-save copy `roms/firered-viridian2.sav` (3/1 at
21,17): the round bushes (0x005) along the fence west of the Pokémon Center path stand as rounded cards
(`evidence/l1-fr-viridian-after.png`). Still flat there: fences, hedges, and the Viridian houses (town slice K3).

## L5: plain side walls on Kanto buildings (lead, 2026-10-07)

After the side-wall fix (55ddb82) every Kanto building has closed west/east ends, but the end faces are flat colour
patches cut from the model's own art (the ROM has no side-view drawing). Guy's Day Care view now shows a solid wall,
yet a plain one. Future: dress the end walls from the front art (repeat the facade's wall band, base stripe and a
window from the same building) so a side reads like the front. Size: small-medium, Kanto table only.

## Pause menu ENHANCE tidy (2026-10-07)

The orphaned "Time-of-day light" and "Vivid mode" labels are erased from all 6 `pause-bot-enhance` plates
(`tools/plate_erase_rows.py`), and VOXEL 3D / 3D ANGLE / 3D ZOOM now sit directly under LDR Bloom (y141 / y181 / y227).
The tab scrolls 27 px (was 136); ZOOM's seg is the only thing below the fold. The smoke test's `press-ctm` channel
drives 3D ANGLE (`voxPitch` 2 -> 4) instead of the deleted tilt row. Screens: `evidence/pm-enhance-{indigo,daylight}[-scrolled].png`.

## L3 DONE (2026-10-07, branch worktree-agent-af1fec61451660776)

**Sun moved in front of the scene.** `VOXEL_SUN_DX/DZ` 0.85/0.55 (north-west, shadows toward the camera) became
`0.45 / -0.65` in `voxel_lighting.h`: the sun lies along (-DX, 1, -DZ) from a point (+Z is south), so it is south of the
scene on the camera's side, 52 degrees up, and a little to the west. Shadows travel (+DX, +DZ), north and a touch east:
away from the camera, up the screen. Why this vector: the GBA sprites are drawn lit from the front and above, so the sun
must be camera-side; a south wall gets facing 0.65 (light 0.90), a west wall 0.45 (0.84), a roof 1.0, north/east walls
ambient 0.70 - so building fronts stay lit but not white, and the side component keeps west and east walls apart. A
steeper sun (0.30/-0.70, tried first) put the shadows almost wholly behind their casters, hidden; the 0.45 side
component lets them peek out to the right.

**One source of truth.** Every sun consumer reads the two macros: the ray march (`Lit`, `RayPoint`), the reach box
(`RayCeiling`), the chunk hash margins (`VoxelLighting_Hash`), the face term (`VoxelLighting_Face`, which also serves
modelled buildings via `ModelTri`, the relief lattice in `voxel_mesh_builder.c`, signs and crown cards), the contact
blob offset (`VoxelLighting_Contact`), the NPC/player cast shadow (`EmitCastShadow` in `voxel_entities.c`, which lays the
sprite along the sun, so it now agrees with the terrain shadows by construction), the leaf dapple's orientation and drift
(`DappleUniforms`), and the mote wind (`DrawMotes`, "the way the shadows fall"). Not sun-driven: the sun rays (screen-
space shafts from the top-left corner, a decorative overlay; a high sun slightly to the west still sits upper-left, so
kept).

**Code changes beyond the constants.** The march assumed a north-west sun in three places, now sign-agnostic: the cell
skip (`StepsToLeave`/`LeftTile` replace the "rx and rz only fall" shortcut), the ray-ceiling box, and the chunk hash
(`VoxelLighting_Hash` now widens south/east/west/north by the signs of DX/DZ). The contact blob offset follows the sun.
A day cycle (L4) only has to drive `VOXEL_SUN_DX/DZ` (make them variables, or a function of time of day), then
`VoxelLighting_Reset()` and re-bake the chunks: nothing else holds the vector. `tools/romgen/Makefile` vtest now builds
with `-DVOXEL_LIGHTING_TESTS` so `test_voxel_world` can compare the cell-skipping march with the point-by-point
reference (2184 points) and pin the face terms.

**L4 interaction.** The chunk hash margins are compile-time today; a moving sun must use the sun's worst-case run on each
side (for a full day: all four). The sun direction also drives the dapple drift and mote wind, which will rotate with it
(wanted). At night the vector can stay a dim moon from the same side; the march and the face term are sign-agnostic, so any direction is valid.

Evidence (Azahar N3DS, same saves, before = old sun build, after = new): `evidence/l3-em-oldale-{before,after}.png`,
`l3-em-rustboro-*`, `l3-fr-pallet-*`, `l3-fr-viridian-*`. Before: big dark wedges fall toward the camera from every
building and tree, NPCs get a long shadow toward the viewer. After: shadows fall behind and beside, building fronts and
NPCs read evenly lit, roofs keep their shading. Hardware frame time (the ray count is about the same: a 3.6 x 5.2 tile
run instead of 6.8 x 4.4) still to be read on the device.

## Guy's notes on the Kanto/Sevii shots (2026-10-07)

Guy, after the FRLG census reached 151/152: "Great job. Im very pleased. And yea the side walls are not perfect, some
roofs are not there. The fences and rocks are also flat, not only flowers and grass". His circled shots are in
`evidence/guy-1007/` (Trainer Tower, Four Island, Five Island, Resort Gorgeous).

| # | What Guy circled | Cause (lead's reading) | Fix direction | Size |
|---|---|---|---|---|
| L6 | Roofs that stop at the ridge: Five Island lilac houses, Four Island Center + orange/lilac houses, Resort Gorgeous house | The GBA art shows only the FRONT roof slope; the recipes extrude what is visible, so the back half (ridge + rear slope, behind the map's top row of the building) is missing or ends in a flat slab. | Per recipe family (`sv_gable`, `k_center`, the K-house builders): mirror the front slope to a rear slope about the ridge, colour/texture from the front slope rows; add a `check` rule + preview angle that looks from behind/above so a missing rear slope fails. Audit all 85 Kanto models + the Emerald set. | medium |
| L5+ | Side walls "not perfect" (Five Island right house: plain purple end) | Ends are closed (side-closure check) but plain colour. | Already L5: dress ends from facade art (gable triangle, wall + window texture). | medium |
| L7 (DONE, see below) | Trainer Tower: no entrance, foot not seated (Guy: "there is no enterence to the battle tower") | The model HAS a plinth + porch with a door (`ks3-preview-k_trainer_tower-flfr.png`), but in-game (`guy-1007/88c9ed28-image.jpg`, KS3's `ks3-fr-trainertower.png`) the tower face runs straight into flat grass: the plinth/porch rows are not drawn or sit below ground. Suspect: placement clip / ground offset of the lower exact rects, or rect rows below the placement. | Reproduce at 3/62 (59,9), compare the placement rect vs the model's lower parts, fix so the porch + door stand on the plinth in front of the tower; add a test that the porch prism is emitted at placement. | small |
| L8 | Flat fences (Resort Gorgeous, Pallet), rocks (sea rocks, boulders), flowers | No prop geometry for these metatiles: they render as ground art. | A prop part like the L1 shrub card: fence = thin upright card along the fence line (posts/rails from the upper layer), rocks = low rounded card/box, flowers = small upright card (check layer split; L2 reports it). Per-game tables like GpShrub. | medium |

Order (lead): L2 (DONE) → L7 Trainer Tower entrance (bug) → L6 roofs → L5 side-wall dressing → L8 fences/rocks/flowers → L4 day cycle.
## L2 DONE (2026-10-07, branch worktree-agent-a51e8b9b69c45d499)

Tall grass stands up as two low, lit blade cards over its own flat cell. Evidence: `evidence/l2-em-r101-*`, `l2-em-grass-*`
(player standing in grass), `l2-fr-r1-*`, `l2-fr-grass-*` (each `-before` / `-after`, Azahar N3DS).

**Finding that changed the design.** The blades are on the LOWER layer (upper layer 0 px on Emerald 0x00D and FR 0x00D, measured with
`romgen author ROM grass`), not the upper one as the L1 shrubs are. The cell's lower layer is blades over plain-grass colour, so the blade
slot is the lower layer with the plain-grass metatile's colours keyed to transparent (`grassGround` = 0x001; 3 key colours per game).

**Which cells.** By behaviour, not a hand list: profile `bladeGrass` = {0x02} on both Emerald and FRLG (`romgen author ROM grass`
lists every (tileset, id, behaviour, uses, upper-layer px)). At most `VOXEL_GRASSES` (12) per tileset pair.
Excluded: Emerald 0x03 long grass (Route 119), 0x07 short grass, 0x09 ash grass (different art, not tufts); Emerald (0x083DF794, 0x206),
blades over sandy-brown ground (`grassSkip`); tree-owned metatiles; border cells; building cells; sea-edge cases unverified.

**Design.** `VoxelTree_EmitGrassCard` (`voxel_tree.c`): two quads, 12 vertices. Back card foot z = y+0.5, rise 0.40, run 0.12, u mirrored;
front card foot z = y+1.0, rise 0.55, run 0.14; sunk 0.04; `rounded` so `VoxelLighting_Face` lights it as crown. The blade slot reuses the shrub
atlas machinery (`VOXEL_GRASS_BLADES(k)`, 12 ids after the shrub ids, same 4 atlas pages, no new texture page, no per-frame keying; keying
happens once at atlas compose). The flat cell keeps its slot, so a cell with no ready blade slot is just flat. Tried and rejected: one card
(too subtle), two equal tall cards (solid dark carpet), one tall card (hedge bands with bare stripes). No sway: it needs a per-material
vertex-shader uniform or CPU re-meshing, neither is nearly free.

**Cost.** 12 added vertices per grass cell (18 total with the flat quad). A full 8x8 chunk: 1152 against the 9344 scratch limit.
Densest 26x16 window over every General-primary layout (`romgen author ROM grass`): Emerald 116 cells = 1392 added vertices; FR/LG
305 cells (L317 at 28,9) = 3660 added vertices, against the 96K-vertex arena (Rustboro draws 54K). No LOD or degradation was needed.
Azahar HUD fps (25 cap): Emerald Route 101 25 -> 26; Emerald player in grass 25 -> 25; FR Route 1 25 -> 23; FR dense grass 25 -> 23.
Hardware fps is unmeasured.

**Sprites in grass.** The player and NPCs in grass are drawn normally: the cards are depth-tested geometry, no z-fighting, no sprite hidden;
at the steep pitch the lowest legs are partly behind the front card, which reads as standing in grass. The rustle effect sprite is untouched.

**Flowers (not implemented).** Measured: FR 0x008 / 0x009 (Route 1 flowers, ~3000 uses each) are lower-layer only (upper 0 px), behaviour 0,
so the same mechanism (key the plain-grass colours out of the lower layer) would cover them with a tileset-id list instead of a behaviour
(flowers have no behaviour). Emerald 0x004 (631 uses) is the opposite: fully opaque on the UPPER layer (256 px), so it would need the source
layer switched to the upper/composite. Cost per flower cell would be the same 6-12 vertices.

**Tests.** `test_voxel_frlg.c` GrassAround (classification only 0x02, GrassSource round trip and bound, blade slot present and distinct,
a grass cell emits 6 + 6*VOXEL_GRASS_CARDS = 18 vertices); `test_voxel_world.c` TestGrassProfile (bladeGrass == {0x02}, ground, skip list,
id layout).

## L7 DONE (2026-10-07, branch of agent-a67c5a8d1fa0113ab)

Root cause: not placement, not ground level. The model was too big for a map chunk. `k_trainer_tower` was 4148 triangles
(12444 vertices), and a chunk's vertex scratch (`VOXEL_CHUNK_SCRATCH`, 9344 vertices in `ctr_voxel.c`) refuses the rest
(`VOXEL: chunk scratch full, 1222 triangles refused at 6,0 of 3:62 (9342 of 9344 vertices)` in voxel.log). The model's
parts are emitted in recipe order, so what was refused was the tail: the platform and the porch with the door. 4036 of the
4148 triangles were end caps: a cap is one quad per repeat of its flat patch, and the tower's patches were 4x4 px. Rows
vs art were fine (the porch is rows 6-7 of the 9x8 rect, door (58,7), inside the census rect; `buildings.bin` held the
porch at z 6.9-8.0, y 0-1.1 cells). Fix: the five side patches are the largest pixel-flat rects of the art (wall x 55-61
rows 0-89, pillars 2x34, grey edge 28x2), 4148 -> 744 triangles, chunk 6,0 now 2796 vertices (and 18.7 ms -> 8.5 ms).
The same cause hit `k_saffron_silph` (9342 of 9344, 824 triangles refused: the entrance canopy and glass door were never
drawn); fixed the same way, 3488 -> 968 triangles. Still affected, not fixed (no flat patch large enough: the cap needs a
stretched patch or a coarser tessellation, a generator change): `k_pokemon_tower` (20464 triangles), `k_power_plant`
(9292); with the best flat rects they only drop to 5488 and 5050 triangles, still over the scratch. Evidence:
`evidence/l7-before-a|b.png`, `l7-after-a|b.png` (3/62 at (59,9) and (59,14)), `l7-silph-before|after.png` (3/10 at (33,33)).

## L7 follow-up: the chunk vertex budget, measured and gated (2026-10-07)

The class of bug behind L7: a chunk whose geometry overflows the device's per-chunk vertex scratch
(`VOXEL_CHUNK_SCRATCH`, 9344 vertices with lighting) silently refuses the TAIL of what it emits, and models are
emitted last. New tool: `romgen author ROM budget [PCT]` measures every model and every chunk of every map (outdoor,
plus Emerald's 3D interiors) with the device's own emitters in the device's order (ground rows, trees/shrubs/grass,
then the models whose placement origin is in the chunk). Calibration: FR 3/62 chunk 6,0 measures 2796, exactly what
the device logged after L7.

Before (relief FULL):
- FR = LG: 2116 chunks in 76 layouts, **3 over the scratch**: 3/4 L82 chunk 1,0 = 62874 vertices (Pokemon Tower,
  20464 triangles, 672 %); 3/28 L98 chunk 0,4 = 28512 (Power Plant, 9292 triangles); and a new one, 3/4 L82 chunk 1,1 =
  10044 (two `k_lavender_house`, 1382 triangles each, 107 %). Azahar confirms all three in voxel.log (17844, 6390 and 234
  triangles refused).
- Emerald: 5563 chunks in 406 layouts, 0 over, 5 at >= 80 % (worst 0/47 L48 chunk 1,0 = 8982, 96 %: sea_rock /
  sea_stack props). Worst model rustboro_stone_3_43, 936 triangles.

Cause: 53399 of FRLG's 56151 model triangles were end caps, one fan per repeat of a small patch rect, and 52424 of
those sampled a single-colour patch. Fix (generator, not per recipe; `rg_geom.c emit_cap_piece`, enabled for outdoor
models in `rg_buildings.c`): a cap piece whose patch is one solid colour (`art_rect_flat`: every pixel of the patch
identical) is ONE polygon per profile piece, its uv pinned to the patch centre, tagged `~clamp`. Same pixels by
construction; all 455 author preview images (all 85 FR models + the 6 changed Emerald ones, 5 views each) are
pixel-identical before and after. Side closure kept (`check all` on FR and LG: 85 PASS each, all 170 side lines 0 open or n/a).

After:
- FR = LG: 0 over, 0 at >= 80 %, worst chunk 2712 (29 %); all model triangles 56151 -> 5186. Pokemon Tower 20464 ->
  176, Power Plant 9292 -> 282, Lavender house 1382 -> 14 (chunk 1,1 now 1836), Trainer Tower 744 -> 156, Silph 968 ->
  32. Largest model k_pallet_lab 332 triangles.
- Emerald: six models 64 triangles fewer each (littleroot_house_e/w 614 -> 550, littleroot_lab, kit_house_4/5,
  oldale_house); chunks unchanged. **Still 96 % on 0/47 chunk 1,0** (sea_stack 330 triangles x 562 placements, its
  mound parts are `~proj`, not caps): not over, but the next prop or relief change there will tip it. Gated now.

The gate: `romgen author ROM check` FAILs a model over 3300 vertices (the heaviest terrain measured under a model is
5892, Emerald 0/8 chunk 1,8; 9344 - 5892 = 3452); `test_romgen_budget` (new, 28th romgen suite) runs the budget over
all three games and fails on any chunk over the scratch, any model over the limit, or RG_BUDGET_SCRATCH drifting from
ctr_voxel.c. It fails on the pre-fix generator (verified). Pins: FR = LG buildings.bin b0f63cdc -> e9f54cdd, Emerald
2929c764 -> ec2f3292 (7898476 -> 7870828 B); regions, signposts, relief (both modes) unchanged.

Evidence (Azahar, private instance, New 3DS): `evidence/budget-tower-{before,after}.png` (Lavender at 18,9: the tower
body above the base and the Lavender house's gable now drawn), `budget-plant-{before,after}.png` (Route 10 at 7,43:
before only the two end caps stood, after the whole Power Plant), `budget-littleroot-{before,after}.png` (Emerald
Littleroot: identical buildings, only NPCs moved). After: no "chunk scratch full" line in voxel.log in any run.

Device fallback (recommended, not implemented): JobFinish logs an overflow only when it beats the session's worst, so
a second, smaller overflow is silent. Log every refusing chunk once (map + chunk + refused count). And reserve the tail
of the scratch for models (stop terrain decoration, trees/grass, at scratch minus the chunk's model vertices, which
`VoxelBuildings` knows before JOB_TREES) so an overflow costs grass, not a building.
