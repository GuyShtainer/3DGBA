# PHASE 33 ROMGEN build log, S2 (buildings.bin)

## 2026-10-06 S2.1 S0 additions + images
- Built: `rg_art.{h,c}` gains `rg_cell_px` (G1), `rg_subtile_px` (G2), `rg_c5_to_8` (G4); `rg_world.{h,c}` gains
  `rg_metatile_entries` (G3); new `rg_bimg.{h,c}` (RgImage new/free/paste/bbox/crop/hash/equal, `rg_c5_rgba`,
  `rg_hex_to_c5`, `rg_cell_image`, `rg_building_art`); `test/host/test_romgen_bimg.c`.
- Tests: 3651 checks synthetic, 10481 with `ROMGEN_ROM`. Synthetic: `rg_cell_px` equals `rg_layer` wherever `rg_layer` draws (both
  layers, flips, secondary palettes), lower idx-0 pixels carry palette slot 0 of their own quarter's palette and are drawn,
  a tile past the tile data is (31,0,31) idx 0 (drawn on the lower layer, undrawn on the upper), an out-of-range metatile is
  all magenta (lower) / empty (upper); building_art: ground blocks transparent whole, a block with painted-in ground kept whole,
  `owned` limits cells, `upperOnly`, off-map fails; every hex colour literal in voxel_building_specs.py (33 of them) has a 555
  preimage and a one-bit-off literal is rejected. Real ROM: Littleroot (2,4,5,5) = 80x80, alpha only 0/255, every transparent
  pixel (0,0,0,0), 5983 opaque pixels, bbox (0,1)-(80,80); the east house (13,4) has the identical bbox.
- Decisions / deviations:
  - `rg_building_art` derives upstream's `ground_px` itself from the ground metatiles whenever `owned` is given
    (`build_models` passes `ground_px` exactly when `owned` is set), so the spec's signature needs no extra parameter.
  - `rg_cell_image` allocates its 16x16 image (spec signature `RgImage *out16`) and has no `layers` argument (only the
    two-layer form; the lower-only form is added when a caller needs it, S2.6 room_check).
  - Uncompressed tilesets decode `min(avail, 16384)` bytes (S0 rule), so "a tile past the data" in a synthetic fixture needs a
    compressed tileset (exact length). Noted for later fixtures.

## 2026-10-06 S2.2 geometry kernel + checks
- Built: `rg_geom.{h,c}` (mesh with interned tags and per-triangle cached tag flags, clip, triangulate, tile_pieces, Strip,
  strip_face, Prism incl. Proj cuts / Strip / Tile edges / caps, HipRoof, Frustum, Vault, Walls, Card, Facet, PlainWall,
  Decal, Cylinder, Lifted, chunked part list; `rg_pysum`, floor-div helpers, stable merge sort) and `rg_bcheck.{h,c}`
  (double-depth Raster, ortho check, density check); `test/host/test_romgen_geom.c`. Relief and Mound stay for S2.5.
- Tests: 585 checks, 0 failures (ASan/UBSan). pysum compensated vs naive vectors (0.1x10 = 1.0, 1e100+1-1e100 = 1.0),
  nearbyint(2.5)==2, floor div/mod, stable sort, clip/triangulate (both windings, L shape area), tile_pieces order and flip
  and s0, Raster tie keeps first / nearer wins / alpha 127 vs 128 / colour int(255*0.72)==183, a projected wall prism = 0/0/0
  and empty density, a shifted uv gives wrong>0, doubled u is listed, clamp cuts, every part type emits with exact counts
  (vault 18, cylinder 46), projected parts = art, hip and frustum and every Strip variant pass the density check
  (which validates the port of strip_face / segments / wrap / tail).
- Decisions / deviations:
  - `RgExact` lives in `rg_bcheck.h` (the spec put it in rg_bspecs.h) so rg_bcheck needs no dependency on the specs table.
  - `rg_density_check` keeps the spec's `art` parameter but ignores it (upstream's density_check does not use it).
  - `rg_clip` takes `nc` (live coordinates) and copies whole `RgPt`s for kept vertices; only cut vertices are zero-filled
    beyond `nc`. Callers never read past `nc`.
  - `Card.art` is held inside the part (`RgCard.art`), so `rg_part_emit(part, mesh)` drops the spec's separate `art` argument.
  - Python `sum()` over mixed int/float items is modelled as double (ints are exact in double at these magnitudes).

## S2.3 - Littleroot house end to end

Added: `rg_bspecs.{h,c}` (RgSpec, `rg_littleroot_house`, spec rows 1-2 only, of 34), `rg_buildings.{h,c}` (build_models for
direct specs, cell_heights, cell_footprints, find_placements, placement_patches, pack_atlas, texel_offset, VXB7 writer),
`test_romgen_buildings.c`.

Results (real ROM): both houses 614 triangles, gate ortho 0/0/0 and empty density list; heights 31/49/61/49 per row, all in
1..255; placements (10,2,4) and (10,13,4); meshes identical except plaster-column u (shifted by exactly 56, to 1e-9, since the
fractional u sums are not bit-exact). 2-model buildings.bin = 154296 bytes (1 page, 3684 vertices, 1 mask); one house alone =
77184 bytes. Both round-trip through VoxelBuildings_Init (PageOf/CellAt).

Deviations / decisions:
- `RgSpec.rect` is int16 (not u8); `RgExact` lives in rg_bcheck.h.
- `rg_cell_footprints` returns maskOf[] plus an `RgMaskSet` (dedup, first-seen order).
- `RgErr` gained RG_ERR_TOO_BIG and RG_ERR_BUILDINGS (field-width overflow guard).
- The writer recomputes everything for the sizing pass (out == NULL), no caching.
- Variants table fixed at 0 (arrives S2.5-S2.6); `Card.art` is held inside the part; `rg_cell_image` allocates its output.
- Python int vs float `sum()` caveat: sums go through rg_pysum (compensated), area sums are float in upstream too.
- Synthetic coverage is numerics only (texel_offset, pack_atlas); the end-to-end proof is the real-ROM run. A synthetic
  fixture world with masks is not yet written (mask paths are exercised with 1 mask on the real ROM).

## 2026-10-06 — S2.3 visual check in the emulator (PASS)

- `buildings.bin` exported from the S2.3 spec table (2 Littleroot houses, 154296 B) next to the
  generated `regions.bin` + `signposts.bin` on the emulator SD (local only, ROM-derived, never committed).
- App log: `VOXEL buildings: 2 models on 1 pages, 2 placements, 3684 vertices`; page 0 loads in 112 ms.
- `evidence/s23-littleroot-houses.png`: both houses stand as real 3D models (roof, walls, windows,
  door, cast shadows) and the signposts are 3D; Birch's lab is still flat (S2.4). Matches the
  Littleroot panel of Zallax's sheet in shape and proportion.
- Getting there: no Fly in the party, so a COPY of a save was patched (sector checksums recomputed):
  `specialSaveWarpFlags |= 1` (CONTINUE_GAME_WARP) + `continueGameWarp = 0:9 (10,11)`. Patching
  `location`/`pos` alone does NOT move the player — Continue restores the saved map view.
- Seen but not chased: `VOXEL: no VRAM for a 512x256 atlas (free=164864)` / atlas cache capped at 1,
  `missing=6` chunks steady — VRAM pressure since the logical surface + bloom target landed. Movie mode
  pins Old-3DS in Azahar; recheck on New 3DS hardware (HW run) before acting.
- Side find, fixed in 438e86b: `TOUCH_NAMES[TOUCH_PANEL]` read past a 3-entry table every frame
  (single-game hint) — ~2–5k `unmapped Read8` per Azahar run since phase 32, 0 after the fix.

## 2026-10-06 S2.4 all direct specs

Added: builders `rg_littleroot_lab`, `rg_pokemon_center` / `rg_poke_mart` (shared `center_or_mart` core), `rg_oldale_house`,
`rg_briney_house`, `rg_flower_shop`, `rg_kit_house(width)`, `rg_gym`, `rg_devon`, `rg_fountain`, plus `rg_flat_part` and
`rg_flat_block` (the latter incl. the roof-unit branch, exercised only synthetically until S2.5 uses it for stone/olive).
`rg_geom` gained two part kinds for the fountain's private classes (`Relief_top`, `Jet`: `RG_P_FOUNTAIN_TOP`, `RG_P_JET`).
`rg_specs[]` now holds 14 direct rows (sp:1092 order, rows 12-14, 16-19 and 22-34 still to come) with the layout ids and
FNV-1a-32 pins measured on the user's BPEE ROM: L1 Petalburg CA6DFAA0, L3 Mauville 6FFC5818, L4 Rustboro A55404CF,
L10 Littleroot EFE99674, L11 Oldale 52C922B6, L20 Route 104 157E3492.

Real-ROM results (all 14 models: ortho 0/0/0, density list empty, host build, both `RG_PYSUM_COMPENSATED` 1 and 0):
triangles: house_w 614, house_e 614, lab 364, pokemon_center 286, poke_mart 160, oldale_house 264, briney_house 270,
flower_shop 310, kit_house_4 264, kit_house_5 260, gym 244, gym_rustboro 244, devon_corporation 870, rustboro_fountain 70.
Cell heights: 1..255 everywhere inside each spec's match rows; 0 (nothing over the cell) only where the model does not reach:
Center/Mart 4 of 16 (back row, match_rows (1,4)), flower shop 6 of 24 (back row), Devon 6 of 90 (the wings stand one row
north of the tower).
Placements (63 in all): Center 18 incl. Petalburg L1 (19,13) and Oldale L11 (5,13); Mart 14; gym 4 = L1 (12,4), L3, L7, L442;
gym_rustboro 1 = L4 (24,15); kit_house_4 13; kit_house_5 4; Oldale houses 2 (L11 (4,4) and (14,13)); the rest only at their own
reference. The gym is therefore placed in layout 1 (by `gym`) and layout 4 (by `gym_rustboro`); neither spec matches the
other's layout (Rustboro's copy has its own roof and flanks), which is why upstream keeps two specs.
File (14 models): 1,247,008 bytes, 30 pages, 14,928 vertices, 63 placements, 17 masks; every model's reference cell resolves
through `VoxelBuildings_PageOf` / `CellAt` after `VoxelBuildings_Init`; a far cell does not.
Tests: test_romgen_buildings 4379 checks with the ROM (1117 synthetic only), all 8 romgen suites green under ASan/UBSan;
device `make` links (3DGBA.3dsx).

Deviations / decisions:
- `center_or_mart` is two entry points (`rg_pokemon_center` crown, `rg_poke_mart` none), not (arg0, arg1): the rib repeat is
  (9,16) / (12,16) and the crown is a flag, which two int arguments would not carry cleanly.
- A strip put on a prism edge goes through `rg_strip_fin` (`edge_strip`), as upstream's `Strip.__init__` resolves `start`.
  Forgetting it (start = 0) made Devon fail with 25 wrong pixels (repeat rows started 7 texels early); caught by the gate.
- Fountain `Relief_top` tags are the literal strings "fountain.top" / "fountain.side~proj" (sp:561, 568), not name-derived;
  the `Jet` tag is name + "~proj". Both are projected, so the density check exempts them.
- The test's old "heights in 1..255 everywhere" became: <= 255 everywhere, >= 1 over the houses and inside match rows, and
  at least one covered cell per model.
- Not run: upstream's lambda `exact` for kits (`kit_house_exact`) is stored as the two literal tables for 64 and 80.
Open: none for S2.4. Next: S2.5 (components, kit, props); `rg_flat_block` unit branch gets its real-ROM gate there.

## 2026-10-06 — S2.4 visual check in the emulator (PASS)

- 14-model `buildings.bin` (1,247,008 B, same as the test) on the emulator SD; app log: `14 models on 30 pages,
  63 placements, 14928 vertices`.
- `evidence/s24-littleroot-lab.png`: Birch's lab is now a 3D model (raised roof + rooftop dome, front wall).
- `evidence/s24-oldale-center.png`: Oldale Pokémon Center as a 3D model (curved roof, Poké Ball crest, P.C sign)
  next to an Oldale house. Both match Zallax's sheet.

## 2026-10-06 S2.5 components, kit, props

Added: `rg_grelief.c` (Relief incl. `_subtract`, Mound incl. with_ring BFS and `_spans`), `rg_bexpand.c/.h` (component, kit and
props expanders; seam_art, pick_side, flank_band, piece_spec, props find / cells_of / beyond), stone_block / olive_block and the
21-row hedge/railing/kit table in `rg_bspecs.c`, and the two-phase `rg_build_models` plus owned / repeat_at / props placements,
ground variants (<= 128) and the VXB7 variants table in `rg_buildings.c`. `rg_geom` gained `c[7]` points and the RELIEF/MOUND parts.

Real ROM (`ROMGEN_ROM=roms/emerald.gba`): 67 models = hedge 6, railing 35, kit 7, props 5 (+14 direct), skipped 0. Gate
(ortho 0/0/0, empty density list, props judged against `drawing`): 0 failures over 67 models, 12868 triangles. seam_art column
assert (`seamClash`) 0, connections assert (`connAmbiguous`) 0. buildings.bin: 3479644 B, 68 pages, 219 page-models, 40254
vertices, 2362 placements, 56 masks, 66 variants (<= 128); round-trips through `VoxelBuildings_Init` (variants enumerate, every
owned layout has a page and a top). New suite `test_romgen_expand.c`: 417 checks (synthetic with_ring / pick_side / flank / Relief
and Mound gate vectors, real-ROM pins, consumer round trip). All 9 romgen suites green under ASan/UBSan, geom/buildings/expand
also with `RG_PYSUM_COMPENSATED=0`. `make -C tools/romgen` and the device `make -j8` build; fixed one maybe-uninitialized warning in rg_geom.c.

Deviations: (A2) layouts iterate by id where upstream sorts by name; affects only model order / `_n` suffix; `propTies` = 0 on this
ROM so the tie-break divergence never fires. Committed as one commit (the pieces share files, so per-sub-step green splits were not practical).
Open: none. Not yet checked visually in Azahar, and on-device memory for 219 page-models is unmeasured.

## 2026-10-06 — S2.5 visual check in the emulator (PASS)

- Full S2.5 `buildings.bin` (3,479,644 B, = the test) on the emulator SD; app log: `67 models on 68 pages,
  2362 placements, 40254 vertices`, no VRAM/page errors logged.
- `evidence/s25-rustboro.png`: Rustboro Center + Mart, street lamps (props, with cast shadows) and a stone kit
  building all stand as 3D models. Verified by the lead: expand 417/0, buildings 4387/0 with the ROM.

## 2026-10-06 S2.6 interiors + reuse

Added: `rg_brooms.c/.h` (the 13 room piece tables, their helpers, open polygons, shade lists, colour table), `rg_binterior.c/.h`
(`_inside` / `_inside_grid`, the room expander `interior_specs`, KeyTab cell keys, `register_piece`, `reuse_pieces` exact + loose
passes, `place_reused`, `reuse_everywhere`, bare twins, `room_check`), 13 interior rows at the end of `rg_specs[]`, and in
`rg_buildings.c` the interior branch of `find_placements` (position == rect, same-room only, `reused_at` entries with whole-room
`patch_all` for unmodelled rooms), the twin's `own` mask in `ground_patch` and a masked-art gate for twins. New suite
`test_romgen_interior.c`: 851 checks (synthetic shapes / colour preimages / table sanity; real ROM: whole-table build, gate, 13x
room_check, a negative control, write + consumer round trip).

Real ROM (`ROMGEN_ROM=roms/emerald.gba`): 200 interior pieces + 19 bare twins (286 models in all with the 67 of S2.5), skipped 0.
Pieces per room: 54:20 55:13 56:11 57:12 58:40 432:3 59:14 60:15 61:20 62:18 63:10 71:5 94:19. Gate (ortho 0/0/0, empty density
list; twins judged on their own pixels): 0 failures. room_check == 0 for all 13 rooms (layouts 54-63, 71, 94, 432). The negative
control (the same placements with every triangle removed) leaves 16144 pixels wrong in room 61, so the 0s are not vacuous.
Reuse: 173 exact placements, 159 own-pixel (bare) placements, 37 unmodelled rooms received at least one piece; 265 placements carry
a whole-room patch (all of them nOdd == w*h). buildings.bin: 7,898,476 B, 118 pages, 630 page-models, 80520 vertices, 2894
placements, 56 masks, 66 variants (<= 128, pages <= 256); round-trips through `VoxelBuildings_Init`.
Updated pins in `test_romgen_expand.c` (reason: the table now carries the 13 interior rows): models 67 -> 286, triangles 12868 ->
25514, file 3,479,644 -> 7,898,476 B, pages 68 -> 118, placements 2362 -> 2894, vertices 40254 -> 80520; masks (56) and variants (66)
unchanged (interior pieces are boxes for the footprint masks). The S2.3 suite needed only its build line.
All 9 romgen suites green under ASan/UBSan with the ROM; geom / buildings / expand / interior in both `RG_PYSUM_COMPENSATED` modes
(1 and 0, identical pins). `make -C tools/romgen` and the device `make -j8` build.

Deviations / decisions: (1) pieces are built (art + mesh) inside the room expander, not in phase 2, because relief / card / decal
parts borrow the piece's art and cell arrays; `RgBuildModel.built` makes phase 2 skip them. (2) A bare twin's mesh is the piece's
mesh with the `*_floor.decal` triangles dropped (same result as re-emitting the parts without the `_floor` decal). (3) Twins are
inserted right after their model once `reuse_everywhere` is done (bare_at is complete only then). (4) Upstream's "blocked cell
%d,%d is in no piece" and "reuse:" prints are not ported; counters (`nReuseExact`, `nReuseBare`, `nReuseRooms`) replace them.
(5) A piece that claims no pixel and has no walls returns `RG_ERR_BUILDINGS` with its name in `errPiece` (upstream `SystemExit`).
(6) `place_reused`'s loose pass checks the anchor cell first, then the other cells; same set of placements as upstream's start-pixel
lookup. Anchor and floor-count ties take first-seen / row-major (SPEC neutral rule). (7) `patch_all` is applied to every placement of
that model in that layout. (8) An interior row whose layout fingerprint does not match is skipped, as the other kinds.
Dropped: none. Open: functions `room_setup`, `room_owner`, `pb_body` and the table functions in `rg_brooms.c` exceed the ~60-line
guide (straight transcriptions; not split here); the file doubled to 7.9 MB and 118 pages, on-device memory and load time are
unmeasured and not yet seen in Azahar; the room tables were checked against the ROM only through the gate and room_check (they
cannot cross-check each other).

## 2026-10-06 — S2.6 visual check in the emulator (PASS)

- Full S2.6 `buildings.bin` (7,898,476 B, = the test) on the emulator SD; app log: `286 models on 118 pages,
  2894 placements, 80520 vertices`, no errors logged.
- `evidence/s26-player-house.png`: the Littleroot player house 1F as a 3D room — walls, fridge, sink counter,
  shelf, TV, table + chairs as modelled pieces. Lead-verified: interior 851/0, expand 1512/0 with the ROM.
- Still open: device memory/load time for the 7.9 MB / 118-page file (hardware run).

## 2026-10-06 S2.7 full export

Audit: `rg_buildings_write` was already complete from S2.3-S2.6 (pages per layout incl. 442, patches, masks, quarters, variants,
the size guards with the field named in `RgBuildStats.errField`, two-pass sizing via `out == NULL`). Nothing to rewrite there.
Missing, now added: (1) `rg_run` `wantBuildings`: after the pair loop it runs `rg_build_models` -> `rg_model_gate` on every model ->
`rg_buildings_write` (size, malloc, fill); `RgOutput` gains `buildings/buildingsSize`, `bModels bPages bPageModels bPlacements
bVertices bMasks bVariants`, `buildingsFailed` + the first 8 failing names, `msBuildModels/msChecks/msWriteBuildings` (placements
run inside the write). A gate failure is counted, not fatal (the file is still written). (2) CLI: `--only` now also accepts
`buildings` (default on) and prints the counts, the gate result and the three timings: needed to exercise `wantBuildings`;
`--dump-model` is NOT added (S2.8). (3) `test/host/test_romgen_export.c`: the section 4.2 checks as asserts over an independent
parse of the bytes + the consumer round trip.

Section 4.2 checks (580,568 checks, 0 failures, ASan/UBSan, both pysum modes): magic, size % 4, header trailing u16 == 0, pads zero;
page table (POT, area <= 512x512, offsets contiguous from the texel start, every page inside the file, texels end exactly at EOF);
model rows (`firstVertex+vertexCount <= vertices`, `heights+w*h <= heightBytes`, vertexCount % 3, cells sum == heightBytes); page-model
indices; placements sorted by layout (1..442), `pageModel < pageModels`, `extraFirst+extraCount <= vertices`, extraCount % 6, every
patch vertex y == 0.01 and shade 1; footprints 0xFFFF or `< masks`; quarters <= 0x0F; variants sorted unique by (layout, metatile,
quarters), <= 128; every vertex float finite, shade > 0. Consumer: `VoxelBuildings_Init`, variants enumerate == 66, every layout that
has a page answers `PageOf`/`PageSize`/`ReadPage` (first and last texels in, one past the end refused): 118 layouts. Also: second
full `rg_run` is byte-identical; `wantBuildings` off leaves regions identical and buildings NULL; `out == NULL` size == written size;
a cap one byte short returns 0 / `RG_ERR_TOO_BIG` ("output buffer").

Pinned counts (real ROM, test + CLI agree): buildings.bin 7,898,476 B; 286 models; 118 pages; 630 page-models; 2894 placements
(9 in layout 442); 80,520 vertices (3,978 patch vertices); 2169 height bytes; 56 masks; 66 variants; 0 gate failures.
regions.bin 330,791 B and signposts.bin 26,144 B unchanged.

Pysum diff: CLI built with `RG_PYSUM_COMPENSATED=1` and `=0`, run on the same ROM: buildings.bin 0 differing bytes (`cmp -l | wc -l`),
regions.bin 0, signposts.bin 0. (Identical, not hidden.)

`rg_run` timing, PC (host clang -O2, one run, all three outputs): total about 190-220 ms; buildings: models 83 ms, gates 9 ms,
placements + write 70 ms. (Under ASan the test sees ~370 / 30 / 325 ms.)

Deviations / decisions: (1) SPEC 4.2 says "POT page sizes within 512x512"; upstream's atlas allows {64..1024}^2 and the writer's real
guard is area <= 512x512 (gen:1216-1218, rg_buildings.c). Page 3 is 1024x256, so the test asserts POT + dims <= 1024 + area <=
512*512. (2) The CLI default now includes buildings (`--only` restricts). (3) A model gate failure does not fail `rg_run`.
Open: device memory and load time for the 7.9 MB file and the on-device `rg_run` time (S2.8, hardware); `--dump-model`/`--time`
polish is S2.8.
