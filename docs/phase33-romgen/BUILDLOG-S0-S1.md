# PHASE 33 ROMGEN build log, S0-S1

## 2026-10-06 S0.1 rg_world + map connections (commit below)
- Built: `source/romgen/rg_world.{h,c}` (ROM gate, layout table walk, map groups, events, tilesets, pairs, structural
  alternates, cell queries), `rg_behavior.h`, `test/host/rg_fixture.h` (synthetic mini-ROM), `test_romgen_world.c`.
- Added (recommended by SURVEY-S2-S3): `rg_map_connections(w, group, num, RgConn*, max)`; dirs 1..4 only (dive/emerge
  filtered), returns the true count so a result > max means truncated. `rg_world_map(w, group, num)` lookup.
- Test: 59 checks synthetic, 612 with `ROMGEN_ROM` (all M1 pins pass on the first real run: 442 layouts, 518 maps, group
  vector, 0 order mismatches, 36 unused ids, 82 outdoor maps, 533 sign / 1313 warp events, 73 tilesets + NULL / 76 pairs,
  the 15 alternates exactly, 0x8B only in 0x083DF83C twice, connection census 27/27/40/40 and the three verified links).
- Deviations / notes:
  - Arena is a chain of 64 KB calloc blocks (stable pointers), not literally one block.
  - Trailing NULL entries of the layout table are trimmed (a fixture/robustness rule; BPEE has none).
  - Duplicate group start addresses: only the first group keeps the count (BPEE has none; needed to define behaviour).
  - `RgWorld` also carries `maps[]`, `groupStart/Count`, `outdoorMaps`, `warpEvents`, `signEvents` (needed by tests and CLI).
  - Real-ROM alt pairs matched the spec table with a plain equal-u16-word share (collision/elevation bits included).

## 2026-10-06 S0.2 rg_art + S0.3 CLI skeleton
- S0.2: `source/romgen/rg_art.{h,c}` (port of voxel_art.Pair + the cells.py foliage/covers/treads questions; MIT header),
  `test/host/test_romgen_art.c`: 39 checks synthetic, 33 149 with `ROMGEN_ROM`.
  Synthetic: flips, palette split at 6 vs tile split at 512 independently, index 0 undrawn on both layers, tile past data
  skipped, NULL secondary, merged layer-1-wins, layer equality on pixels + colours, foliage (exactly half counts), covers,
  treads incl. the 240-pixel fill, threshold measures. Real ROM: all 76 pairs decode; **General flight of steps = metatile 175,
  across 1.983, down 28.341** (SPEC: about 2 / 28). 1 092 metatile-instances read as treads.
- Spec ambiguity checked with evidence (SURVEY: CPython >= 3.12 `sum()` of floats is compensated, <= 3.11 naive): over every
  metatile of every pair with >= 240 drawn pixels the nearest distance to a treads threshold is across 0.0032 / down 0.0017,
  vs ~1e-12 for any summation-order difference, so naive vs compensated cannot flip a single metatile. Naive order kept (spec).
  Pinned in the real-ROM test (margins > 1e-6).
- Deviation: the exact `across == 4.0` / `down == 15.0` boundary drawing of SPEC 7.2 is not constructible with 5-bit colours
  (luminance steps are ~8.2 per grey step, never exactly 4.0 or 15.0); tested instead as the nearest representable cases
  (down ~16.5 true, ~8.2 false, across ~8.2 false) plus the real-ROM margin pin.
- S0.3: `tools/romgen/{romgen_cli.c,Makefile}` (host CLI; `make -C tools/romgen`, output `tools/romgen/build/` git-ignored).
  Opens the world and prints the M1 counts: `world: 442 layouts, 518 maps, 73 tilesets, 76 pairs, 82 outdoor maps, 1313 warp
  events, 533 sign events`; `--time` prints 2.0 ms for the world open on the PC.
- Device: `source/romgen` added to SOURCES and `rg_%.o : CFLAGS += -ffp-contract=off` in the Makefile; `make -j8` green
  (rg_art.o, rg_world.o compile under devkitARM with no warnings). Not wired into the app (S4).

## 2026-10-06 S1.1 rg_roles
- Built: `source/romgen/rg_roles.{h,c}` (port of voxel_cells.Layout.role_at, houses/lamps/free_post/is_signpost, global
  post_metatiles; MIT header), `RgProgressFn` typedef added to rg_world.h, `test/host/test_romgen_roles.c`.
- Tests: 39 checks synthetic (one hand-built layout per branch: water/ledge/floor/stair, lone foliage is no post, horizontal and
  vertical fences, cliff vs shelf, fence needs in-map neighbours, lone drawing = signpost, cover is no post, sign event,
  wall-backed post carried by post_metatiles incl. across layouts of the same tileset ADDRESS and NOT a twin tileset, house
  flood limits 5 columns / 7 rows / foliage stop, indoor layouts have no signposts, lamp branch vs plain wall, reversed pair
  order = identical bytes). 656 013 checks with the real ROM: invariants 1-9 of SPEC 7.2 all pass.
- Real-ROM role census (325 479 cells): floor 91 520, water 67 644, ledge 1 054, stair 2 182, wall 4 639, tree 22 401,
  prop 0, shelf 86 733, fence 7 197, cliff 41 746, signpost 363. Rustboro: 26 signposts, 20 without a sign event, 2 via the
  lamp branch. Route 104: 3 of its 5 sign events stand open on three sides and all 3 are SIGNPOST. Littleroot's four signs,
  the wall-backed (7,8) and (12,8) included, are SIGNPOST.
- Role array shape for S2/S3: `RgRoles{data, off[id-1], total}`, `rg_roles_of(r, layoutId)`; one byte per cell, row major.
- Deviation: "lamp branch" invariant 6 is asserted by structure (signpost, no event, one blocked side whose cell is WALL,
  south open) since the test cannot see which branch fired; that holds for 2 Rustboro cells.

## 2026-10-06 S1.2 rg_regions (VXR5) + round trip
- Built: `source/romgen/rg_regions.{h,c}` (`rg_regions_write(w, r, out, cap)`; out=NULL sizes it), `test_romgen_regions.c`.
- Test: 23 checks synthetic (random roles 0..10 forced into every cell, a NULL layout entry absent from the file, size
  query, too-small buffer, absent id / out-of-range cell => FLOOR, and four negatives the vendored `voxel_regions.c` must reject:
  bad magic, descending ids, role byte 11, truncated file). 474 checks with the real ROM: regions.bin = 330 791 bytes
  (= 8 + 12 x 442 + 325 479), every one of the 325 479 cells agrees through the VENDORED parser.
- Test files are written to mkdtemp dirs under /tmp and removed; nothing ROM-derived lands in the repo.

## 2026-10-06 S1.3 rg_signs (VXS2) + round trip
- Built: `source/romgen/rg_signs.{h,c}` (cutout_mask, metatile_mask, head_mask, head_ground, record list, VXS2 writer; MIT
  header), `test/host/test_romgen_signs.c`.
- Tests: 51 checks synthetic (cutout ring/hole/open ring/border pixels/empty; head_mask: popcount 16 rejected, 14 allowed, width
  15 rejected, 208 pixels rejected, 104 accepted, no 8-connected seed rejected, empty upper layer; head_ground: exact match,
  tie keeps the first inserted, most frequent wins, y+2 fallback, off-map -> 0; end to end: post columns 3/4 and 11/12 give mask
  0x1818 on every row, a lamp's lantern gives head rows 8..15 = 0x00FF with headGround = the floor metatile; indoor copy gives no
  record; records sorted (layout, y, x); round trip through the VENDORED voxel_sign.c + voxel_regions.c: IsCell, HeadGround).
  1751 checks with the real ROM: 363 records, signposts.bin = 26 144 bytes, strictly sorted, all in outdoor layouts, 20 with a
  lantern (17 in Rustboro), every record round-trips through the vendored consumers.
- **SPEC ERRORS found (recorded, not silently changed):**
  1. SPEC 7.4 says on the real ROM "every record's `rows` is non-zero". Measured: 4 of 363 records have an all-zero mask:
     layout 2 (39,44) m=824, layout 36 (23,76) m=578, layout 30 (16,0) m=791, layout 46 (52,9) m=268, all signposts with no
     sign event whose lower layer draws only colours the walkable neighbours also draw and with an empty upper layer.
     The upstream algorithm (`metatile_mask`, smask:47-52) produces exactly that, so the faithful port keeps them. The consumer
     then holds a sign cell whose mask draws nothing (IsCell true, emits no voxels). Pinned as `zeroRows == 4` in the test.
     Decision for Guy / S5 review: drop them from the file or keep upstream-faithful behaviour.
  2. SPEC 5.5 says "35 u16, no padding" for the consumer's record: it is 36 u16 = 72 bytes (3 + 16 + 16 + 1), which is what the
     same spec's "72*count" and the upstream `struct.pack("<HHH16H16HH")` say. The writer uses 72.
