# PHASE 36 H4 — Mossdeep, Sootopolis (build log, 2026-10-08)

Base `1c78cb9`. All 10 Mossdeep and all 12 Sootopolis census doors on Emerald (10 and 11 on Ruby / Sapphire) are now covered
by a recipe; no door of either town is left without one. Every recipe is exact 0/0/0 on Emerald and Ruby. **Hardware and
Azahar: not run.**

## Recipes

Rows in `rg_hspecs.h` (`RG_HSPECS_MOSSDEEP_ROWS`, `_SOOTOPOLIS_ROWS`), builders in `rg_hspecs_mossdeep.c` and
`rg_hspecs_sootopolis.c`; the RS-own row in `rg_rsspecs_h4.c`. Layout pins: Mossdeep (7) Emerald AD4D637F, Ruby / Sapphire
9CB79195; Sootopolis (8) Emerald A3CC5D0B, Ruby / Sapphire 0ECDD398. The census seed rects were wrong for nearly every
building here (they are door signatures; the Sootopolis ones are clamped at 16 cells): every rect below was fixed from `art`.

| row | layout | rect (cells) | placements (Emerald / RS) | RS |
|---|---|---|---|---|
| mossdeep_house | 7 | 17,13,4,4, matchRows 1-4 | 5 / 5 | retarget |
| mossdeep_wide | 7 | 35,20,5,5, ground {0x001, 0x124} | 1 / 1 | retarget (RS draws the facade rows differently; rows 0-2 and the silhouette are the same) |
| mossdeep_space | 7 | 60,8,9,8 | 1 / - | replaced on RS by **rs_mossdeep_space**, 60,6,9,8 (another building: a green block two cell rows higher, no gantry) |
| sootopolis_tower | 8 | 43,14,3,4, matchRows 1-4, ground = 4 stone floors | 3 (+3 on twin layout 357) / 3 | retarget |
| sootopolis_box | 8 | 44,3,3,4, matchRows 2-4 | 6 (+6 on twin 357) / 5 | retarget |
| gym_sootopolis (`rg_gym`, `kGymExact`) | 8 | 28,28,6,5 | 1 / 1 | retarget |

The Petalburg gym kit fits Sootopolis's gym as it did Fortree's and Lavaridge's (the signature 3119FED0 was a hint, not a proof:
the `check` is). Layout 357 is the unused Sootopolis twin; the dwellings and the gym's neighbours are placed there too (9
placements), which is why Emerald gains 26 placements for 17 doors.

**The houses.** Mossdeep has six red-brick houses on the map but they are two buildings. Five are the 4x4 house (the census
seeds 20,7,11,4 / 66,22,4,4 / 48,3,5,4 / 18,7,9,4 / 17,14,4,3 all misread its rows: the real rect is 4x4 and its top row
varies with the neighbours, so it is excluded from the match). The sixth, door 36,24 (seed 35,21,5,4), is a different, wider
5x5 house with two arched windows whose top 8 rows are sand: the sand is not a ground tile of the usual list, so the recipe
adds sand 0x124 to the ground set. Ruby draws its facade differently (a single window and the regular door tile), but the
silhouette and every roof row are identical, so a retarget serves (the same call as Lilycove's wooden house).

**Space Center, Emerald.** A flat block, 144x128: deck (domes and dish) rows 0-46, the glazed pylon facade 46-128, the top
corners `behind` (ground). **It is the tallest model in Mossdeep and the rocket and gantries stand just behind it** (the
gantry cells, rows 2-7 above the rect, are flat upper-layer art): the block's wall is 82 px, so from the front camera it hides
the rocket's base. Not seen on screen. **Space Center, Ruby:** a different building, an RS-own `kReplace` row.

**Sootopolis dwellings.**
- *Pointed dwelling (tower).* A dark pyramid roof on a cream octagon with the door at the bottom. Modelled as a 4-cell-row
  gable (48x64): roof 0-46 with the corners cut by the texture's alpha, wall 46-64. The model starts below the apex row: the
  apex cell and a cliff piece either side of it are opaque upper-layer art that no roof covers (the first try, a 5-row rect, left
  480 art pixels missing), so **the top 6 px of the apex stay flat map art** just behind the roof's top edge.
- *Box dwelling.* A pale box with a panelled roof and a door porch: flat block 48x64, wall rows 36-64.
- Two decorated twins of each shape exist (diamond window, no door: tower rows 3-4 and box rows 0-1 differ). They are not
  census rows, so they are **left as flat art**; beside a modelled dwelling of the same shape they will look different. A
  follow-up recipe with the same builders would cover them (tower seed 53,34,3,4).
- The `exact` rects of the roof bands are `behind` (the pyramid's corners and the cliff pieces are ground-coloured floor), the
  wall rows are strict: wrong 0, missing 0, extra 0.

**Relief interplay: none.** The renderer lifts a building to the relief of its door cell. `romgen --relief-layout 8` shows
layout 8 has only 176 cells with a non-zero height (the central pool, the south-west rim and the cells round them, such as
(29,17), (26,23), (22,34), (13,38) and columns 4-6 / 50-55 below row 38); **none is a dwelling or gym cell, nor a door cell**.
The crater's terraces and walls are flat map art (cliff lines in the white stone), so no model redraws a raised wall and none
doubles it: every dwelling stands at lift 0. This was derived from the relief dump, not seen on screen.

## Gates

- `check`: every new row exact 0/0/0 on Emerald and Ruby (Ruby: rs_mossdeep_space too); Sapphire the same. `check all`:
  Emerald 73 specs, 4 failed (pre-existing hedge, mart, lab, rustboro_gym); Ruby 60 specs, 1 failed (pre-existing hedge).
- Census: Emerald 107 -> 124 / 160 (+17), Ruby 106 -> 122 / 139 (+16).
- `budget 0`: 0 over on both. Worst chunk 8982 (Emerald) / 8496 (Ruby), worst model 2808: unchanged. New models, in vertices:
  mossdeep_house 162, mossdeep_wide 114 (Ruby 42), mossdeep_space 402 (rs_mossdeep_space 894), sootopolis_tower 114,
  sootopolis_box 42, gym 732.
- Variants: Emerald 75 -> 78 and Ruby / Sapphire 75 -> 78 (of 128): cause not isolated (the test comments say water cells under the new placements; unverified).
- Pins (each in its test with the reason): Emerald buildings 8883292 B -> ec319d0e (CLI SHA-1 prefix; 9020476 B, 325 models, 671
  pageModels, 2969 placements, 92850 vertices, 70 masks, 78 variants, 29624 triangles; pinned in `expand`, `export`,
  `interior`). Ruby = Sapphire buildings 2dc3a918 -> b7432279 (4074356 B, 103 models, 58 pages, 229 pageModels, 2146 placements,
  45180 vertices, 71 masks, 78 variants; recipe rows pinned 49 -> 55, `55/55 pins match`). Unchanged: Emerald regions
  007a370f, signposts 38515605, relief 21a837f0, ledges eb25a383; FR = LG buildings 8f2e72bf, regions 3716874d, signposts
  ba2fde45, relief 32c24146; RS regions 1a09cd5f, signposts 9b4d379c, relief 215a12d9; Ruby == Sapphire.
- Full gate: `suites ok: 41 (expect 41)`, test rc=0, vtest rc=0, build rc=0. All six CLI runs rc=0; hashes above confirmed (Emerald EL relief eb25a383).

## Next

1. Merge into main; Azahar before / after in Mossdeep and Sootopolis on Emerald, plus an RS town (Ruby Mossdeep for the
   RS-own Space Center). Look at: the Emerald Space Center wall against the rocket; the Sootopolis pointed dwelling's apex stub
   and corner alpha; the dwellings against the terrace lines; the gym on its islet.
2. Optional follow-up: the door-less decorated twins of the two Sootopolis dwellings.
3. Hardware: pending, as for every slice.
