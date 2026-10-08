# PHASE 36 H3 — Lilycove (build log, 2026-10-08)

Base `701a229`. All 13 Lilycove census rows (12 on Emerald, 13 on Ruby / Sapphire) have a recipe. Eight Emerald rows on
layout 6, all exact 0/0/0 on both games; Ruby / Sapphire retarget the layout pin and need no RS-own row. **Hardware and
Azahar: not run.**

## Recipes

Rows in `rg_hspecs.h` (`RG_HSPECS_LILYCOVE_ROWS`), builders in `rg_hspecs_lilycove.c`. Layout 6 pin: Emerald 7A998B47,
Ruby / Sapphire C5BB1A9F (the layouts differ only in art and a few ids; every building cell below was compared by `art`).
The seed rects of `census` were wrong for most Lilycove buildings (they are door signatures); every rect below was fixed
from `art`.

| row | rect (cells) | px | placements | notes |
|---|---|---|---|---|
| lilycove_house | 54,12,4,4, matchRows 2-3 | 64x64 | 5 (35,3; 41,3; 11,11; 54,12; 10,19) | blue slatted-roof house = `rg_h_gable(w,64,37,14)`; rows 0-1 differ per house, rows 2-3 are shared |
| lilycove_house_w | 37,11,5,4 | 80x64 | 1 | the same kit, a cell wider |
| lilycove_store | 23,0,9,7 | 144x112 | 1 | Department Store: facade only, the map ends above it. Approximation: top 8 rows stand for the roof, wall 104 px tall (flat block) |
| lilycove_museum | 7,0,10,**6** | 160x96 | 1 | three flat blocks: wings (wall 31, roof rows 33-65) and a taller tower (wall 55, roof rows 9-41) |
| lilycove_hall | 20,18,7,7 | 112x112 | 1 | Contest Hall: flat top rows 8-86 (skylit roof + grey fascia, bevelled corners `behind`), red facade 86-112 |
| lilycove_pavilion | 9,27,7,6 | 112x96 | 1 | the orange-roofed hall (door 12,32): flat block, wall rows 62-96 |
| lilycove_wood | 36,20,6,5 | 96x80 | 1 | wooden house: three pitched profiles (centre roof 0-52, side roofs 16-52, facade 52-80) |
| lilycove_cave | 69,4,3,2 | 48x32 | 1 | the stone arch of the sea cave: a 24 px block over the cliff cells (both Ruby doors 70,5 and 71,5 sit in this one rect) |

Ruby / Sapphire: all eight are `kRetarget` rows (layout 6, pin C5BB1A9F) in `rg_rsspecs.c`. The wooden house's door
signature differs on RS because the roof tiles are painted differently (`art` on both): the silhouette is identical, so a
retarget serves and no RS-own `kReplace` row (`rg_rsspecs_h3.c`) was needed. Placements on Ruby are the same as Emerald's
(5 + 1 + 1 + 1 + 1 + 1 + 1 + 1 = 12).

**Museum rect is shortened to 6 cell rows.** The museum's 7th cell row (art rows 96-112: the balustrade and the entrance
steps) is left as the map's flat art. The first try was 7 rows with the entrance set back so the steps stayed open; the
round trip then failed (`CellAt` of the footprint's bottom-middle cell found no geometry, because the recess left cell 12,6
empty). With 6 rows the front is flush at row 96, the steps and balustrade stay as drawn, and a player on the steps is in
front of the wall.

**Approximations, said plainly.**
- Department Store: the roof is above the map's top row, so the model is a 104 px wall with a token 8 px roof. It is the
  tallest model of the slice. It stands against the top edge of the map: nothing north of it, so it hides nothing, but the
  camera's view of it is just the facade.
- Museum: the entrance cells (11,5), (12,5) are inside the footprint (the dark doorway is the wall's own art), as every
  house door is.
- Cave mouth: a rock block 24 px high stands on the cells of the arch (cliff art is part of its 3x2 rect, so the whole rect
  is opaque). Whether it sits well in the relief cliff is **not seen**: it is the riskiest row for Azahar.
- Pavilion, hall: the roofs are flat blocks (the Game Corner's pattern), with the scalloped / bevelled corners marked `behind`.
- Nothing here is in the sea: no water cells under any footprint, so variants stay at 75 on both games.

## Gates

- `check` exact rects 0/0/0 for all eight rows on Emerald and on Ruby; Sapphire the same. `check all`: Emerald 67 specs,
  4 failed (pre-existing hedge, mart, lab, rustboro_gym); Ruby 54 specs, 1 failed (pre-existing hedge).
- Census: Emerald 95 -> 107 / 160, Ruby 93 -> 106 / 139.
- `budget 0`: 0 over on both. Worst chunk 8982 (Emerald) / 8496 (Ruby), worst model 2808: unchanged. New models: store 906
  vertices, house 180, house_w 204, museum 90, hall 42, pavilion 90, wood 168, cave 240.
- Pins (each in its test with the reason): Emerald buildings 5b2711bb -> f127285a (8883292 B, 319 models, 118 pages, 663
  pageModels, 2943 placements, 91284 vertices, 69 masks, 75 variants, 29102 triangles; pinned in `expand`, `export`,
  `interior`). Ruby = Sapphire buildings a00bb5ae -> 2dc3a918 (3927268 B, 97 models, 58 pages, 223 pageModels, 2130
  placements, 43194 vertices, 70 masks, 75 variants; recipe rows pinned 41 -> 49, `49/49 pins match`). Unchanged: Emerald
  regions 007a370f, signposts 38515605, relief 21a837f0, ledges eb25a383; FR = LG buildings 8f2e72bf, regions 3716874d,
  signposts ba2fde45, relief 32c24146; RS regions 1a09cd5f, signposts 9b4d379c, relief 215a12d9; Ruby == Sapphire.
- Full gate (`gate-run.sh`): all six CLI outputs rc 0; `test` 30 and `vtest` 11, "suites ok: 41 (expect 41)"; device `make -j8` rc 0. Emerald buildings CLI SHA-1 f127285a (full and ledges runs), Ruby = Sapphire 2dc3a918.

## Next

H4 Mossdeep / Sootopolis. Azahar before / after for Lilycove on Emerald plus Ruby is still to do (the cave mouth and the
museum's entrance first).
