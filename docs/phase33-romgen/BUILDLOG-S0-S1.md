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
