# PHASE 36 H5 — Routes and landmarks (build log, 2026-10-08)

Base `3fe6613`. Twelve Emerald rows cover the route and landmark doors of the slice; Ruby / Sapphire retarget the same rows and
gain one RS-only row (the League facade of layout 266). Every recipe is exact 0/0/0 on Emerald and on Ruby / Sapphire where it
applies. **Hardware and Azahar: not run.**

## Recipes

Rows in `rg_hspecs.h` (`RG_HSPECS_ROUTES_ROWS`, registered in `rg_bspecs.c`), builders in `rg_hspecs_routes.c`; the RS-own row in
`rg_rsspecs_h5.c`, added through the new `kExtra` table of `rg_rsspecs.c` (RS-only rows that replace no Emerald row). Every rect
was fixed from `art` (the census seeds are door signatures). Emerald layout ids are the census "L" numbers.

| row | Emerald layout / rect (cells) | art size (px) | placements | Ruby / Sapphire |
|---|---|---|---|---|
| league_gate | 9 / 13,0,11,6 | 176x96 | 1 | same layout bytes, no retarget |
| seashore_house | 25 / 10,2,5,4 | 80x64 | 1 | retarget 25 |
| trick_house | 26 / 9,61,5,6 | 80x96 | 1 | retarget 26 |
| cycling_gate | 26 / 14,12,6,5, matchRows 2-5 | 96x80 | 2 (14,12 and 15,84) | retarget 26 |
| route111_gate | 27 / 11,109,5,5 | 80x80 | 1 (+ the twin layout 392) | retarget 27 |
| cable_car_station | 28 / 27,23,6,5 | 96x80 | 1 | retarget 28 |
| cable_car_chimney | 136 / 14,32,6,5 | 96x80 | 1 | retarget 137 |
| glass_workshop | 29 / 32,2,4,4 | 64x64 | 1 | retarget 29 |
| route114_house | 30 / 28,2,4,4 | 64x64 | 1 | retarget 30 |
| weather_institute | 35 / 1,26,11,7 | 176x112 | 1 | retarget 35 |
| entrance_gate | 37 / 35,0,5,6 | 80x96 | 1 | retarget 37 |
| entrance_gate_wide | 241 / 30,29,10,5 | 160x80 | 1 | retarget 242 |
| rs_league (RS only) | - | 176x144 | - | 266 / 9,0,11,9, pin 21EB0971 |

Layout pins (Emerald / Ruby = Sapphire): 25 A525346D / 74E34955, 26 4A7EB534 / 7A89EA0E, 27 31657B1A / 15DFA917, 28 BE8B08F3 /
5F21044A, 29 A32A722C / 9D7E921C, 30 5DAFC653 / 46526A3F, 35 B691459D / ECAD8849, 37 CDAAAC93 / FA51621C, 136 737C0DC4 / 137
479CDF78, 241 B13E9480 / 242 EBF19174, 9 5476D808 (the same on RS). Every building's cells were compared with `art` between the
games: identical on all rects, so the RS rows are plain retargets. The 0/27 and 0/28 signatures (Route 111 gate, Route 112
cable car station) do not differ between the games; each pair of rows exists on both.

Shapes. All are Kanto-style profile prisms (`rg_h_flat`, `rg_h_gable`, and a new local `h_pitched_t` for the Trick House: a
pitched pink centre with two lower wing roofs); the front faces are PROJ edges, so wrong / missing / extra are 0 by
construction. Look L5 side dressing uses the facade rows with a fallback patch.

## Census 27/0 (negative seed rect "4,22,-3,-21") — a bug, fixed

Ruby 27/0 is the door (5,24) of layout 288, a **1x1 dummy layout** (a Route 104 prototype leftover); the door is off the layout. The
census clamped the rect's x1/y1 to the layout size (1) but left x0/y0 beyond it, so width/height came out negative. Fix in
`tools/romgen/rg_author.c` (clamp x0 <= x1, y0 <= y1): the row now prints `1,1,0,0` and stays in the census (its doors are
counted). Test: `TestCensusRs` in `test_romgen_frlg_buildings.c` (every Ruby census rect is non-negative and inside its layout,
and at least one is empty). The door is left without a model: there is nothing on the layout to model.

## Doors left open

- **0/37 door 22,29 (layout 38) and 24/21 door 10,42 (layout 302 / Ruby 303)**: cave mouths with cliff relief at the mouth; a
  flat prism would sit on the cliff face with no art to match. Left open.
- Emerald 0/26 door 31,113 and 26/4 (Battle Frontier): slice H6, as the brief says.
- Ruby / Sapphire 27/0: see above.

## Relief

`romgen --relief-layout N`: the door cell of every Emerald rect is flat (centre 0) except the mountain cable car station (136):
its whole footprint and door cell are lifted by 16, so the model stands on the raised plateau (the lift is read at the bottom-left
cell). The Weather Institute (35) has cliff relief on its top row (cells (1..3,26), (9..11,26), (11,27): lifts to 16-21, door
cell flat); its model carries a strip of rock at the top corners (the art row it belongs to), which is why the preview shows rock
at the roof corners. Cable car station 28, as previewed, reads as a flat-top block: the oblique roof is a flat roof with art on
top, an approximation. No Ruby / Sapphire relief exists for any of these cells. Nothing here came near the vertex budget.

## Occlusion

Not exhaustively verified. I read the `art` collision column for the cells above each rect: the Seashore House, Trick House, the
cycling gates, Route 111 gate, cable car station 28, Route 114 house, Weather Institute and the wide gate have cells north of the
footprint that the metatile data marks as collision 0, so a path or a ledge may stand behind a wall; the League, entrance gate,
Glass Workshop and mountain station have none. The column also reads 0 on some roof cells of the footprint itself, so it is a hint, not
proof of walkability, and this was not followed into the rendered camera views. Treat the walls as potentially hiding a walkable
strip until Azahar / hardware looks.

## Gates

- `check all`: Emerald 85 specs, 4 failed (hedge, mart, lab, rustboro_gym: the pre-existing four); Ruby and Sapphire 73 specs, 1
  failed (hedge).
- `budget 0`: 0 models over the limit and 0 chunks over the scratch on all three carts. Emerald: worst chunk 8982 verts, worst
  model 2808 verts, worst terrain under a model 5892; Ruby = Sapphire: worst chunk 8496, worst model 2808, terrain 4764.
- Census: Emerald 124/160 -> 137/160 covered; Ruby 122/139 -> 136/139 (27/0 and the two cave mouths remain).
- Variants: 78 -> 83 of 128 (both games; the H2 sea rule for the water cells under the new placements).
- Pins (each in its test with the reason): Emerald buildings ec319d0e -> 3d7cb8cd (9020476 B -> 9579896 B, 325 -> 337 models, 2969 ->
  2983 placements, 70 -> 71 masks, 118 -> 119 pages, 92850 -> 94902 vertices, 29624 -> 30308 triangles); Ruby = Sapphire buildings
  b7432279 -> 850ab774 (4074356 B -> 4763460 B, 103 -> 116 models, 2146 -> 2160 placements, 58 -> 60 pages, 45180 -> 48522
  vertices, 71 -> 72 masks); recipe-row pins on RS 55 -> 68. Unchanged: Emerald regions 007a370f, signposts 38515605, relief
  21a837f0, ledges eb25a383; FR = LG buildings 8f2e72bf, regions 3716874d, signposts ba2fde45, relief 32c24146; RS regions
  1a09cd5f, signposts 9b4d379c, relief 215a12d9. Ruby == Sapphire byte-identical.
- Full gate (`gate-run.sh`): test rc=0, vtest rc=0, build rc=0, `suites ok: 41 (expect 41)`; the six CLI outputs: Emerald
  buildings 3d7cb8cd (ledges run the same), FR = LG 8f2e72bf, Ruby = Sapphire 850ab774, all other files as listed above.
