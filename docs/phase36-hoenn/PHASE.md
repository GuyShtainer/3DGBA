# PHASE 36 — Hoenn census follow-up (romgen building recipes)

Status: **plan** (2026-10-07). H1 in progress. Nothing hardware-run.

## Goal

Close the Hoenn building gap that Phase 35 S4 measured. After S4, Emerald covers 60 / 160 census rows and Ruby /
Sapphire 58 / 139. Every one of the 81 rows still open on Ruby / Sapphire is open on Emerald as well. So this is new
authoring, not RS drift. Each row needs a recipe, or the building stays an extruded box or flat ground art.

Recipes are authored once on **Emerald** (the `rg_specs` table) and reach Ruby / Sapphire through Phase 35's copy of
that table (`rg_rsspecs.c`). A row applies on RS without change when the layout fingerprint and the art match. When
the RS layout differs only outside the building, the pin is moved (`kRetarget`). When RS draws a different building
(the Emerald Battle Tents are Contest Halls on RS), an RS-own row takes the Emerald row's place (`kReplace`).

## The 81 rows, by slice

Counts are RS census rows (`romgen author ROM census`, map G/N). Each one is uncovered on Emerald too.

| Slice | Maps | Rows | Notes |
|---|---|---|---|
| **H1** | Slateport 0/1 (7), Mauville 0/2 (4), Verdanturf 0/14 (4), Fallarbor 0/13 (3), Dewford 0/11 (4) | 22 | Mauville and Dewford layouts are byte-identical on RS. Slateport, Verdanturf and Fallarbor differ (repins). The Emerald Battle Tents are RS Contest Halls. |
| H2 | Fortree 0/4 (7), Lavaridge 0/12 (1), Pacifidlog 0/15 (5) | 13 | The gyms of Fortree and Lavaridge carry the Petalburg gym's door signature (try the `rg_gym` builder per layout, like `gym_rustboro`). Pacifidlog's five huts share one signature: one model, 5 placements. |
| H3 | Lilycove 0/5 (13 on RS, 12 on Emerald) | 13 | Department store, contest hall / museum, harbour; the largest single town. |
| H4 | Mossdeep 0/6 (7), Sootopolis 0/7 (9) | 16 | Sootopolis is cliff dwellings inside a crater (large rects up to 16x16; relief interplay). |
| H5 | Routes and landmarks: 0/8, 0/24, 0/25 (3), 0/26..0/29, 0/34, 0/36, 0/37, 24/12, 24/21, 26/3, 26/4, 27/0 | 17 | Mostly singles: League, Weather Institute, Trick House, Cycling Road gates, the Safari Zone, and so on. |
| H6 (Emerald only, not in the 81) | Battle Frontier 26/4 (+7), 26/14 (12), 0/26 (+1) | 20 | Emerald-only buildings, no RS counterpart. |

## Method (per slice)

The Kanto K-slice loop of Phase 34 (`docs/phase34-frlg/BUILDLOG-P34.md`), run on Emerald first:

1. `census --map G/N` gives the door, the destination and a seed rect. `art LAYOUT X Y W H` on both Emerald and Ruby
   shows the building. Note where RS differs.
2. Fix the rect from the art. Write a builder in `source/romgen/rg_hspecs_<town>.c`. These are the Kanto profile
   prisms: every face the front camera sees is a PROJ edge, so the ortho gate holds by construction. Gables use
   15-degree slopes so L6b does not lay them down again. Add an `RgSideCfg` so L5 dresses the ends. The row goes into
   `rg_specs` through the `RG_HSPECS_*` macros of `rg_hspecs.h`, ahead of the interior rows.
3. `check SPEC` on Emerald: exact rects 0/0/0, density 0, back closure, placements as the census says, round trip ok.
   `preview SPEC` views are an aid only.
4. Run the same row on Ruby. If the layout pin differs, compare the building's cells on both carts and repin
   (`kRetarget`). If the art differs, write an RS-own row (`kReplace`, `rg_rsspecs_<town>.c`). `check` on Ruby must be
   0/0/0 too. Sapphire must stay byte-identical to Ruby.
5. Run `budget 0` on both games: 0 models over 3300 vertices, 0 chunks over 9344.

## Gates (every slice)

- Every new recipe: `check` exact rects 0/0/0 on Emerald, and on Ruby wherever it applies. `check all` on both games
  shows only the pre-existing failures: Emerald `hedge`, `mart`, `lab`, `rustboro_gym` (round trip); Ruby `hedge`.
- `budget 0`: 0 over on both games.
- Pins: Emerald buildings and Ruby = Sapphire buildings change. Each change is recorded in its test with the reason.
  Every other output stays byte-identical: Emerald regions 007a370f, signposts 38515605, relief 21a837f0, ledges
  eb25a383; FR = LG buildings 5ba2cc16, regions 3716874d, signposts ba2fde45, relief 32c24146; RS regions 1a09cd5f,
  signposts 9b4d379c, relief 215a12d9.
- `make -C tools/romgen test` (30 suites) and `vtest` (11): read every suite's "N checks, 0 failures" line. Device
  `make -j8` links.
- Census before and after, on Emerald and Ruby.
- Azahar: before and after on Emerald in each town of the slice, plus one RS town. No "chunk scratch full" in
  voxel.log.
- Legal: numbers only from `projects/_reference`. Nothing there is executed. No ROM-derived image of Ruby / Sapphire is
  committed. Emerald evidence follows phases 33 and 34.
- **Hardware: pending** for every slice until a New 3DS run.
