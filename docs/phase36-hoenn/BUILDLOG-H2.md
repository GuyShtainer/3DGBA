# PHASE 36 H2 — Fortree, Lavaridge, Pacifidlog (build log, 2026-10-07, WIP)

Base `11d90c9`. All 13 H2 census rows have a recipe on Emerald and Ruby / Sapphire (commit `2d7eba2`). **The test pins
are not updated yet, so the romgen test run is red on four suites (expected pin changes only, see Gates). Azahar
evidence was not captured. Hardware: not run.** The slice was stopped early for a machine restart.

## Recipes

Emerald rows in `rg_hspecs.h` (`RG_HSPECS_FORTREE_ROWS`, `_LAVARIDGE_ROWS`, `_PACIFIDLOG_ROWS`), builders in
`rg_hspecs_fortree.c` and `rg_hspecs_pacifidlog.c`. Layouts 5, 13 and 16 have the same fingerprint on Emerald and Ruby
(C5D353C6, 306B069F, 0CDAE2A1), so every row applies on RS unchanged: no `kRetarget`, no RS-own rows.

| row | layout | rect (cells) | placements | notes |
|---|---|---|---|---|
| gym_fortree (`rg_gym`, `kGymExact`) | 5 | 19,7,6,5 | 1 | the Petalburg gym; its top row is the canopy's edge |
| gym_lavaridge (`rg_gym`, `kGymExact`) | 13 | 2,11,6,5 | 1 | the Petalburg gym with the gym sign drawn into the east wall (metatiles 671/679); the sign is painted on the wall |
| fortree_hut | 5 | 8,1,5,3, matchRows 1-3 | 6 | all six treehouses share rows 1-2; ground forest 0x0C6 |
| pacifidlog_hut | 16 | 15,10,3,4 | 5 | all five huts are the same 3x4 metatiles; ground water 0x170 |

**fortree_hut is an approximation, on purpose.** The treehouses stand on log decks up in the trees, but the player walks
on the deck at their own height (the drawing shows the height only through the ladders). A raised deck would bury the
player. So the model is the hut alone, on the ground: the plank hut front with the doorway, the palm fronds as a low
15-degree canopy, and the log stacks either side as thin cards. The deck, the ladder and the forest stay flat map art.
The hut is three spans (west panel, doorway, east panel), so L5 dresses the ends from the plank panels and not from the
dark doorway (one span gave navy ends).

**pacifidlog_hut** is an octagonal `RG_P_FRUSTUM` (x 3-45, z 35-63, wall 21, 45-degree thatch band 13) with a projected
finial slab on top. The first try was 42 px deep (round). That showed the band's rear faces above the roof, because the
art implies a shallow (about 26 px) hut, so the depth is 28. The thatch's 2 px overhang past the walls is left out.

## Gates

- `check`: every exact rect of the four new rows is 0/0/0 on Emerald and on Ruby. `check all`: Emerald 59 specs, 4
  failed (pre-existing hedge, mart, lab, rustboro_gym); Ruby 46 specs, 1 failed (pre-existing hedge).
- Census: Emerald 82 -> 95 / 160, Ruby 80 -> 93 / 139 (+13 each, every H2 row).
- `budget 0`: 0 over on both. Worst chunk 8982 (Emerald) / 8496 (Ruby), worst model 2808: unchanged. New models:
  gym 732 vertices, pacifidlog_hut 324 (x5), fortree_hut 216 (x6); Pacifidlog chunk 0,1 is at 2568 verts (27%).
- **romgen `test`: red, pins only.** The run was stopped after 26 suites. The failures are the expected buildings pins:
  `test_romgen_expand` (4: models 307, size/vertices/placements/masks), `test_romgen_export` (1),
  `test_romgen_interior` (2) and `test_romgen_rs_world` (8: buildings SHA-1 91257d8b plus the counts and the
  `pinned == 37` row count, which becomes 41). `test_romgen_buildings` was not touched; its H1 stop at the first Hoenn row
  still holds. Not run yet: the last suites of `test`, `vtest`, and device `make -j8`.
- Pins still to be re-read and recorded: Emerald buildings (f3ce7c8e) and Ruby = Sapphire buildings (91257d8b). Every
  other pin must stay byte-identical (list in PHASE.md).

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

## Next

1. Export the ROM paths (as in H1), run `make -C tools/romgen test`, and read the new values. Update the four test files
   the way H1's commit `d5f89ca` did: one reason line per pin, "Phase 36 H2: 3 Fortree/Lavaridge models + 1 Pacifidlog
   model, 13 placements". Then `make -C tools/romgen vtest` and device `make -j8`.
2. Azahar before and after: Fortree 0.4, Lavaridge 0.12 and Pacifidlog 0.15 on Emerald, plus one Ruby town.
3. Look for occlusion in Fortree: the canopy over the hut door, and the logs next to the ladder.
