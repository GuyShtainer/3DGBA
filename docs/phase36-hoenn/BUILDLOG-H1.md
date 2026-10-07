# PHASE 36 H1 — Dewford, Mauville, Verdanturf, Fallarbor, Slateport (build log, 2026-10-07)

Base `2c30eff`. All 22 H1 census rows now have a recipe on Emerald and on Ruby / Sapphire. **Hardware: not run.**

## Recipes

Emerald rows live in `rg_specs` through the `RG_HSPECS_*_ROWS` macros of `source/romgen/rg_hspecs.h`, builders in
`rg_hspecs_<town>.c`, shared profile helpers (`rg_h_gable`, `rg_h_gable_t`, `rg_h_flat`, `rg_h_flat_t`) in
`rg_hspecs.c`. RS: `rg_rsspecs.c` `kRetarget` / `kReplace`, RS-own rows in `rg_rsspecs_h1.c`.

| row | layout | rect (cells) | placements | RS |
|---|---|---|---|---|
| dewford_house_w | 12 | 1,0,5,4 | 1 | same layout, applies as is |
| dewford_house | 12 | 16,11,4,4 | 2 | as is |
| gym_dewford (`rg_gym`) | 12 | 5,13,6,5 | 1 | as is |
| mauville_house | 3 | 18,11,4,4 | 1 | as is |
| mauville_bike | 3 | 34,2,5,4 | 1 | as is |
| mauville_house_e | 3 | 31,11,5,4 | 1 | as is |
| mauville_block (flat) | 3 | 36,11,4,5 | 1 | as is |
| game_corner (flat) | 3 | 5,10,7,4 | 1 | as is |
| verdanturf_house | 15 | 0,11,4,4 | 2 | retarget 15 / 92F55851 |
| verdanturf_house_w | 15 | 8,11,5,4 | 1 | retarget |
| battle_tent_verdanturf (dome) | 15 | 1,3,5,5 | 1 | replaced by rs_contest_verdanturf 2,3,5,5 |
| fallarbor_house_n | 14 | 0,3,4,4 | 1 | retarget 14 / 434DB33E |
| fallarbor_house_s | 14 | 5,14,4,4 | 1 | retarget |
| battle_tent_fallarbor (dome) | 14 | 6,3,5,5 | 1 | replaced by rs_contest_fallarbor 6,3,5,5 |
| slateport_house | 2 | 4,16,4,4 | 2 | retarget 2 / A8D336A7 |
| slateport_house_w | 2 | 24,41,6,4 | 1 | retarget |
| slateport_house_g | 2 | 2,22,5,5 | 1 | retarget (RS paints it yellow) |
| slateport_fan_club | 2 | 25,7,7,6 | 1 | retarget |
| oceanic_museum (flat) | 2 | 28,22,6,6 | 1 | retarget |
| slateport_shipyard | 2 | 24,32,8,7 | 1 | retarget |
| battle_tent_slateport (dome) | 2 | 8,8,5,5 | 1 | replaced by rs_contest_slateport 8,8,5,5 |

Lesson (flat roofs): when row 0 or the rounded corners of a roof show ground pixels, `rg_close_backs`' guard sees the
closure through them and chamfers the back. Fix: end the top edge one pixel short (a skipped step) and mark those rows
and corners as `behind` exact rects.

## Gates

- `check all`: Emerald 55 specs, 4 failed (pre-existing hedge, mart, lab, rustboro_gym); Ruby 42 specs, 1 failed
  (pre-existing hedge). Every exact rect of every spec is 0/0/0.
- Census: Emerald 60 -> 82 / 160, Ruby 58 -> 80 / 139.
- `budget 0`: 0 over on both. Worst chunk 8982 (Emerald) / 8496 (Ruby), worst model 2808: unchanged.
- Pins (romgen CLI): Emerald buildings 6d321c3a -> f3ce7c8e (7873564 -> 8475268 B, 286 -> 307 models, 2894 -> 2918
  placements, 79482 -> 87360 vertices, 56 -> 59 masks); Ruby = Sapphire buildings 40135582 -> 91257d8b (85 models,
  2105 placements, 39252 vertices, 60 masks). Unchanged: Emerald regions 007a370f, signposts 38515605, relief
  21a837f0, ledges eb25a383; FR = LG buildings 5ba2cc16, regions 3716874d, signposts ba2fde45, relief 32c24146; RS
  regions 1a09cd5f, signposts 9b4d379c, relief 215a12d9.
- `make -C tools/romgen test` 30 suites and `vtest` 11 suites, every one 0 failures. Device `make -j8` links.

## Evidence (Azahar, 2026-10-07)

Private emutest instance h (New 3DS, `--keep-n3ds`, voxel on with the default instance's settings.bin), warp-save
copies of emerald-lavaridge.sav / ruby.sav (originals untouched). Same app build before and after; only the staged
`buildings.bin` changes (before = the base commit's CLI output). No "chunk scratch full" in any of the 12 runs. The
device log shows `307 models on 118 pages, 2918 placements, 87360 vertices` (Emerald) and `85 models on 58 pages,
2105 placements, 39252 vertices` (Ruby). Side-by-side shots (left before, right after) and the after voxel logs are in
`evidence/`; the Ruby shots are local only (ROM-derived art).

| spot (player) | before | after |
|---|---|---|
| Dewford 0.11 (8,20) | the gym is a low box | the gym stands in 3D, roof and facade read right |
| Mauville 0.2 (12,16) | game corner and the house to the right are low | both stand in 3D with their roofs |
| Verdanturf 0.14 (7,17) | the two houses are flat | both stand as gabled houses |
| Fallarbor 0.13 (8,10) | the tent and the plank house are flat | the tent is a dome, the house a gable (an NPC dialog was open in both runs) |
| Slateport 0.1 (30,30) | museum and shipyard are flat | the museum stands as a flat-roofed block; the shipyard's tall roof stands in front of the player, who shows as the occluded silhouette |
| Ruby Verdanturf 0.14 (6,10) | Contest Hall and houses flat | the Contest Hall and both houses stand in 3D |

Not seen in Azahar: Dewford's houses, Mauville's bike shop / east houses, Fallarbor's south house, Slateport's fan
club, purple and green houses and the tent. They pass `check` (exact 0/0/0, back closure, round trip).
