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
