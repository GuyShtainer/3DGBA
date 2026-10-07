# Voxel look backlog (Guy's notes from Emerald screenshots, 2026-10-06)

Guy, on the Emerald 3D look across several Hoenn places: "gorgeous but not perfect, but it will do for now". The open
points, with the cause found in the code:

| # | Guy's note | Cause (checked 2026-10-06) | Fix direction | Size |
|---|---|---|---|---|
| L1 (DONE, see below) | "the trees near the entrance to the cave in Dewford are flat on top" | `voxel_tree.c` `VoxelTree_Part` knows only the General-tileset tree metatiles (0x1D4-0x1E7, 0x1EC/0x1ED, the small trees 0x016/0x017/0x0C6/0x0C7/0x1F4/0x1F5). The Route 106 / Dewford shrubs on sand are metatiles 0x124, 0x239, 0x242, 0x243 (`romgen author emerald.gba art 22 42 17 12 3`). They are not in the table, so they render as flat ground art. Seen in `docs/phase33-romgen/evidence/s37-full-r106.png` (foreground). | Scan all outdoor layouts for foliage metatiles missing from the table (the T1 foliage rule: at least 50 % foliage pixels), then add them as VOXEL_TREE_SMALL or as a new "shrub" part. Emerald table only; the FRLG table comes from T1. | small |
| L2 | "the grass is still flat" | Upstream renders tall grass as ground texture. Only the rustle field effect is a sprite. There is no grass geometry. | Tall-grass cells (behaviour set) get low crossed or billboard blade cards from the cell's own art, swaying optionally. Budget: many cells per screen, so instance it cheaply and test the frame time on hardware. | medium |
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
