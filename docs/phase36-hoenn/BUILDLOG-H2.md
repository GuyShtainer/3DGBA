# PHASE 36 H2 — Fortree, Lavaridge, Pacifidlog (build log, 2026-10-07 / 08)

Base `11d90c9`. All 13 H2 census rows have a recipe on Emerald and Ruby / Sapphire (commit `2d7eba2`). Pins measured and
committed (`b95b8f9`, `6628a2b`). Guy's look at Pacifidlog found sea under the hut bases and the Pokemon Center. Two
fixes followed, for every game: the footprint ground of a building in the sea is the planks (`550ff69`), and a
building's cells that are sea draw the sea, not the planks (`140f6a7`). Gate: 41 / 41 suites, `vtest`, device
`make -j8` (detail in Gates). **Hardware: not run.**

## Recipes

Emerald rows in `rg_hspecs.h` (`RG_HSPECS_FORTREE_ROWS`, `_LAVARIDGE_ROWS`, `_PACIFIDLOG_ROWS`), builders in
`rg_hspecs_fortree.c` and `rg_hspecs_pacifidlog.c`. Layouts 5, 13 and 16 have the same fingerprint on Emerald and Ruby
(C5D353C6, 306B069F, 0CDAE2A1), so every row applies on RS unchanged: no `kRetarget`, no RS-own rows.

| row | layout | rect (cells) | placements | notes |
|---|---|---|---|---|
| gym_fortree (`rg_gym`, `kGymExact`) | 5 | 19,7,6,5 | 1 | the Petalburg gym; its top row is the canopy's edge |
| gym_lavaridge (`rg_gym`, `kGymExact`) | 13 | 2,11,6,5 | 1 | the Petalburg gym with the gym sign drawn into the east wall (metatiles 671/679); the sign is painted on the wall |
| fortree_hut | 5 | 8,1,5,3, matchRows 1-3 | 6 | all six treehouses share rows 1-2; ground forest 0x0C6 |
| pacifidlog_hut | 16 | 15,10,3,4 | 5 | all five huts are the same 3x4 metatiles; ground (not hut art) = the plank deck 0x221 and the sea 0x170; footprint fill: see below |

**fortree_hut is an approximation, on purpose.** The treehouses stand on log decks up in the trees, but the player walks
on the deck at their own height (the drawing shows the height only through the ladders). A raised deck would bury the
player. So the model is the hut alone, on the ground: the plank hut front with the doorway, the palm fronds as a low
15-degree canopy, and the log stacks either side as thin cards. The deck, the ladder and the forest stay flat map art.
The hut is three spans (west panel, doorway, east panel), so L5 dresses the ends from the plank panels and not from the
dark doorway (one span gave navy ends).

**pacifidlog_hut** is an octagonal `RG_P_FRUSTUM` (x 3-45, z 35-63, wall 21, 45-degree thatch band 13) with a projected
finial slab on top. The first try was 42 px deep (round). That showed the band's rear faces above the roof, because the
art implies a shallow (about 26 px) hut, so the depth is 28. The thatch's 2 px overhang past the walls is left out.

**Footprint ground of a building in the sea (`550ff69`, all games).** The renderer fills every model cell with the
placement's ground, and `try_place` picked it as the commonest walkable metatile of the ring round the rect. Round a hut
on a deck, or a centre on a pier, that ring is mostly sea, so the cells the model leaves open (the octagon's corners, the
centre's front) showed water where the map draws planks. Now, when the ring's winner is water, the ground is the
commonest walkable metatile of the rect and ring that is neither water nor a house door. Moved placements: Emerald and
Ruby, the five Pacifidlog huts and the Pacifidlog centre (-> 0x221, the deck) and two route houses (kit_house_4 on 0/39,
kit_house_5 on 0/29); FireRed = LeafGreen, 7 (Vermilion fan club and green house, Cinnabar lab and mansion, Five Island
harbour, the Route 12 cottage and gate). Sizes, vertices and counts unchanged. The earlier attempt (`5fc4471`, the deck
first in the spec's ground list) changed bytes but not the look, because the spec's ground list does not set the fill.

**Cells of a building that are sea (`140f6a7`, all games).** The Azahar shots of `550ff69` showed the opposite fault: a
plank slab over open sea north of every hut. One placement has one ground, but a hut's top two rows (and the centre's
top row) are water cells: the roof drawn on the upper layer over sea on the lower. So romgen now marks, per placement,
each owned cell that is water at that placement with quarters 0xF (the whole upper layer hidden) and registers that
variant; the renderer draws such a cell as its own metatile less the upper layer (the sea under the roof) and every
other cell with the placement's ground (the deck). Props models and interiors keep their rules. A variant names its
layout's tileset: FireRed's Sevii harbours give the same water metatile id two drawings, so `CellAt` takes a variant
only when the cell's own tileset is the variant's (found in the variant list, not on screen: the Birth Island and Five
Island harbours register the same water ids from different tilesets). Variants: Emerald and RS 66 -> 75, FireRed = LeafGreen 0 -> 103 (of 128: little headroom left there). New check
in `test_romgen_buildings`: the Pacifidlog centre's top row carries quarters 0xF and its variants exist, its bottom
row 0; it fails (8 checks) on the single-ground code.

## Gates

- `check`: every exact rect of the four new rows is 0/0/0 on Emerald and on Ruby. `check all`: Emerald 59 specs, 4
  failed (pre-existing hedge, mart, lab, rustboro_gym); Ruby 46 specs, 1 failed (pre-existing hedge).
- Census: Emerald 82 -> 95 / 160, Ruby 80 -> 93 / 139 (+13 each, every H2 row).
- `budget 0`: 0 over on both. Worst chunk 8982 (Emerald) / 8496 (Ruby), worst model 2808: unchanged. New models:
  gym 732 vertices, pacifidlog_hut 324 (x5), fortree_hut 216 (x6); Pacifidlog chunk 0,1 is at 2568 verts (27%). The two
  ground fixes change no model, placement or vertex count, so `check` and `budget` stand.
- Gate on `550ff69`: romgen `test` 30 / 30 and `vtest` 11 / 11, every suite 0 failures; device `make -j8` links. Gate on
  `140f6a7`'s code before the repins: 36 / 41, the five red suites being the expected pins (`expand`, `export`,
  `interior`, `rs_world`, `frlg_buildings`); after the repins each of the five and `test_romgen_buildings` reran alone
  with 0 failures (1546, 643866, 885, 8435, 4283, 4093 checks); `vtest` 11 / 11 and device `make -j8` green in that run.
  The full gate runs again on main at the merge.
- Buildings pins, each recorded in its test with the reason: Emerald f3ce7c8e -> 48b231f4 (recipes) -> 1970b9aa
  (`550ff69`) -> 5b2711bb (`140f6a7`; 311 models, 118 pages, 655 pageModels, 2931 placements, 89364 vertices, 68 masks,
  75 variants, 8606276 bytes, pinned in `expand`, `export`, `interior`); Ruby = Sapphire 91257d8b -> 3eafee01 -> 7257e9b6
  (`5fc4471`) -> aad730ec (`550ff69`) -> a00bb5ae (`140f6a7`; 89 models, 58 pages, 2118 placements, 41274 vertices, 69
  masks, 75 variants, 3650252 bytes; `pinned == 41`); FR = LG 5ba2cc16 -> 23ed0dd9 (`550ff69`) -> 8f2e72bf (`140f6a7`; 85
  models, 103 variants). Every other output byte-identical: Emerald regions 007a370f, signposts 38515605, relief
  21a837f0, ledges eb25a383; FR = LG regions 3716874d, signposts ba2fde45, relief 32c24146; RS regions 1a09cd5f,
  signposts 9b4d379c, relief 215a12d9.
- New checks (`test_romgen_buildings`): Pacifidlog's Pokemon Center (layout 16) stands on 0x221 (fails on the old
  vote); its top row carries quarters 0xF and those variants exist, its bottom row 0 (fails, 8 checks, on `550ff69`).
- Variant headroom: FireRed = LeafGreen now uses 103 of the 128 variant slots (`RG_MAX_VARIANTS`). A later Kanto change
  that adds water cells may need the cap raised.

## Evidence (Azahar, 2026-10-08)

Private emutest instance h (New 3DS, `--keep-n3ds`, voxel on with the default instance's settings.bin), warp-save copies
of emerald-lavaridge.sav / ruby.sav (originals untouched; the copies were deleted afterwards). Same app build before and
after; only the staged `buildings.bin` and its three sibling .bin files change (before = the CLI output of the base
recipes, SHA-1 Emerald f3ce7c8e, Ruby 91257d8b; after = the H2 outputs, 48b231f4 and 3eafee01). No "chunk scratch full"
in any of the 5 after runs. The device log shows `311 models on 118 pages, 2931 placements, 89364 vertices` (Emerald, all
four Emerald runs) and `89 models on 58 pages, 2118 placements, 41274 vertices` (Ruby). Side-by-side shots (left before,
right after) and the after voxel logs are in `evidence/`; the Ruby shot is local only (ROM-derived art). The Fortree huts
shot and the Ruby after shot were taken at the window's 1x scale (the window had moved to another display) and are
upscaled 2x (nearest) in the side-by-side; the others are native. Night lighting in every shot (the 3DS clock was 02:01).

| spot (player) | before | after |
|---|---|---|
| Fortree 0.4 (21,13) | the gym is a low thatch-roofed block | the gym stands in 3D with a tall roof, its roof top runs off the top of the screen; the hut at the left edge stands up |
| Fortree huts 0.4 (10,7) | the huts are flat map art (canopy, dark doorway, deck) | the upper huts stand with a low palm canopy and the dark doorway still visible under it; the near hut at the bottom shows a large green canopy over its front, so its doorway is not visible from this camera; the log stacks beside the ladder read as thin wooden strips, nothing wrong seen; a small brown wedge shows left of the upper right hut's canopy |
| Lavaridge 0.12 (5,17) | the gym is a low block with its sign | the gym stands in 3D, roof and facade read right; a large house roof in the foreground covers the lower half in both |
| Pacifidlog 0.15 (16,15) | the two huts are flat | both huts stand as octagonal thatched domes with a finial on top; no rear faces above the roof; a darker blue rectangle shows in the water around the north hut's base, where the deck art would be |
| Ruby Fortree 0.4 (21,13) | the gym is a low block | the gym stands in 3D, same as Emerald |

Not seen in Azahar: Lavaridge gym's painted sign on the east wall (the camera looks at the south face), the Fortree
huts' other four treehouses, Pacifidlog's other three huts, Ruby Lavaridge and Pacifidlog, Sapphire. The Fortree
huts' canopy hides the near hut's doorway at this camera angle; whether that matters in play is for Guy to judge.

### Fix evidence (Azahar, 2026-10-08)

Same instance and method. Before = the H2 recipe bins (Emerald 48b231f4; FR 5ba2cc16), after = `550ff69` (1970b9aa;
23ed0dd9). No "chunk scratch full" in any run; counts as above. The FR shots and some Emerald shots are at the window's
1x scale, upscaled 2x (nearest).

| spot (player) | before | after `550ff69` |
|---|---|---|
| Pacifidlog 0.15 (16,15), `h2-pacifidlog-fix-before-after.png` | a dark-blue patch round the north hut's base | the base is planked and the patch is gone; small plank tabs at the hut's top corners; **new fault: a plank slab over open sea above and left of the south hut** (the hut's top row is sea in the art) |
| Pacifidlog centre 0.15 (9,16), `h2-pacifidlog-center-before-after.png` | the centre and its deck | the centre and its deck look the same; a dark-blue band along the deck's south edge in both; hut bases planked, but plank slabs spill over the sea at the huts' top rows |
| FR Vermilion 3/5 (10,9), `h2-fr-vermilion-before-after.png` | fan club area, centre, green roof | identical; no wrong-colour patch |
| FR Cinnabar, `h2-fr-cinnabar-before-after.png` | the lab and the centre on the island | identical |

The plank slabs are what `140f6a7` fixes.

Second round, the same way: before = `550ff69` (Emerald 1970b9aa, FR 23ed0dd9), after = `140f6a7` (5b2711bb, 8f2e72bf);
all shots at the window's 1x scale, upscaled 2x (nearest); no "chunk scratch full" in any of the 6 runs; Emerald 311
models, 2931 placements, 89364 vertices; FR 85 models, 164 placements, 19878 vertices, before and after.

| spot (player) | before `550ff69` | after `140f6a7` |
|---|---|---|
| Pacifidlog 0.15 (16,15), `h2-wq-pacifidlog-before-after.png` | plank slabs over the sea north of the huts | the slabs are gone: sea north of each hut, the deck planked under both huts, no dark patch |
| Pacifidlog centre 0.15 (9,16), `h2-wq-pacifidlog-center-before-after.png` | plank slabs over the sea at four huts | the slabs are gone round all four huts, their bases planked; the centre and its deck unchanged, the dark-blue band along the deck's south edge still there |
| FR Two Island 3/13 (10,11), `h2-wq-fr-twoisland-before-after.png` | a green ground slab round the harbour building, out over the sea (older than `550ff69`: Two Island is not among its 7 moved placements) | sea round the building; no tile from another island's tileset. A faint lighter-blue rectangle of sea round the building matches the lighter water outside the slab in the before shot, so it reads as the map's own art. The pale oval on the roof is the player behind it (the x-ray silhouette) |

## Next

1. Merge into main; H3 Lilycove.
2. Look backlog, not blocking: the Fortree near hut's canopy hides its doorway at the steep near-camera angle; a small
   brown wedge shows left of the upper right hut's canopy (`h2-fortree-huts-before-after.png`); a dark-blue band along
   the Pacifidlog deck's south edge (in the before as well, so not H2's).
3. Hardware: pending, as for every slice.
