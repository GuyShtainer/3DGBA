# PHASE 33 ROMGEN build log, S3 (relief.bin)

## 2026-10-06 S3.1 Ledges on the device (S3a)

Built: `rg_rlat.h` (lattice types, header only), `rg_ledge.{h,c}` (ledge_cells + junctions, ledge_layouts, ledge_berms),
`rg_relief_write.{h,c}` (VXL4 serialiser, two-pass sizing), `rg_relief.{h,c}` (LEDGES mode of `rg_relief_build`,
`rg_relief_cells`), `rg_rtables.{h,c}` (only A.1 ENABLED, A.3 outdoor list, A.4 alts, T1/T2/T4 so far; S3.2 extends),
`docs/PROVENANCE.md` (new: one row per table), `rg_is_ledge_junction` (G11) in `rg_roles.{h,c}`, `RG_ERR_TABLES` /
`RG_ERR_RELIEF` in `RgErr`, `RgRunOpts.relief` + `RgOutput.relief/reliefSize/rst/msRelief` in `rg_run`, CLI
(`--only relief`, `--relief ledges|off`, `--relief-layout ID`, relief timing), device hook (`RG_RELIEF_LEDGES`, writes
`relief.bin`, a relief line in `romgen_timings.txt`), `test/host/test_romgen_relief_ledge.c`, and a `test` target in
`tools/romgen/Makefile` (the spec's `make -C tools/romgen test` did not exist; it builds every `test_romgen_*.c` with
ASan+UBSan and runs it; `T=relief_ledge` runs one).

Commands and results (ROM `roms/emerald.gba`, exported as `ROMGEN_ROM`):

```
make -C tools/romgen test            # all 12 suites, 0 failures (about 2 min under ASan)
  art 33149, bimg 10481, buildings 4400, expand 1512, export 580568, geom 585, interior 851, regions 474,
  relief_ledge 55152 (132 without the ROM), roles 656013, signs 1751, world 612 checks
tools/romgen/build/romgen roms/emerald.gba OUT --only relief --time
  relief.bin: 23292 bytes, 23 rows, 850 cells, 23 ledge layouts, 863 ledge cells (mode ledges)
  time: relief 5.0 ms (host clang -O2)
```

relief.bin: 23,292 bytes, 23 rows, 850 lifted cells, 863 ledge cells (13 of them lift nothing above 0.25, so they are not written), cut table empty
(`0000 0000`), trailer `CUTS`. Two CLI runs are byte-identical (`cmp`); two `rg_run` runs and a direct `rg_relief_build`
are byte-identical in the test.

Route 101 (layout 17): 14 cells, SHA-1 of the cell bytes `ec2cfdfe845f9e96f97884814164089fa33280c5` (pinned in the
test; recomputed independently with Python `hashlib` over the file's row bytes: same). Cells: five at y=6 (x 6..10),
five at y=7 (x 2..6), four at y=13 (x 8..11).

Oracles (SPEC-S3 section 5):
- O1 I1-I4 (independent decode in the test): magic/side/trailer, `offset + 4 + 36V + 14C + 8 == size`, ids strictly
  ascending and `rg_relief_outdoor`, w/h equal the ROM layout's, cells ascending (y, x) and inside the map, flags clear,
  base 0, cut table empty, every height 0..6, every written cell is a ledge cell (with junctions), every lattice point
  shared with an in-map non-ledge cell is 0, row set == candidates whose berm lifts some cell (each recomputed and
  compared byte for byte).
- Consumer round trip: the file is written to `voxel/relief.bin` in a temp dir and `VoxelRelief_Init()` loads it;
  `VoxelRelief_Cell` returns every written cell's 25 heights equal to the independent decode, `IsDrawn` false, base 0,
  `CutCount` 0.
- Hand-computed synthetic: a 3x3 layout, a row of three south-jump cells, lip = metatile rows 8..13. Expected column
  [0, 2.25, 4.5, 1.5, 0] derived by hand from the upstream rules (point at py 16+4j reads f/b along the jump) and
  matched exactly; stored [0, 2, 4, 2, 0] (2.25 -> 2, 4.5 -> 4, 1.5 -> 2: half-even). A junction layout (south ledge
  above, east ledge left, blocked cell between): junction takes both directions in join order.
- H9 pins: `nearbyint(0.5)==0`, `(1.5)==2`, `(-2.5)==-2`, `(2.5)==2`; clamp 200 -> 127, -200 -> -128; 0.25 is not a
  lifted point, 0.2501 is; `rg_floordiv(-1..-5, 4)`.
- Writer: byte layout checked against known offsets, signed-foot cut order, `out == NULL` size == written size, a cap
  one byte short -> `RG_ERR_TOO_BIG`, unsorted/duplicate ids, >65535 cells/cuts, width 256 -> `RG_ERR_RELIEF`, empty file.
- `rg_rtables_check` passes on the real ROM (T1 dims of 20, 4, 17; T2 outdoor-type map layouts + A.4 alts == A.3;
  T4 alt/base same size and tileset pair) and refuses the synthetic fixture.

Device builds: `make -j8` and `make clean && make -j8 ROMGEN_DEV_HOOK=1` compile with no warning in any file this slice
touched (the only warnings left are older ones in main.c, netlink.c, celiolink.c and mGBA's sha1.c). Release rebuilt
afterwards with `make clean && make -j8 && make cia` (hook 0, `VX_DEV_ALLOW_O3DS` 0): `3DGBA.3dsx` is 4,582,344 bytes,
identical to the S2.8 hook-off size; `3DGBA.cia` 2,156,992 bytes. The hook build is 4,801,324 bytes.
Not run: Azahar (the lead does the visual and device-parity checks; O4 S3a parity needs the device SHA-1).

Deviations and decisions:
1. **`rg_rtables` arrives in a reduced form in S3.1.** `rg_relief_outdoor` (A.3/A.4) and the ENABLED ids are needed by
   S3a, S3.2 was to add them; this slice carries only those tables plus T1/T2/T4 and their PROVENANCE rows. S3.2 keeps
   extending the same files.
2. **`make -C tools/romgen test` is new** (it was named by the brief but absent).
3. `rg_relief_build` in LEDGES mode runs `rg_rtables_check` first and returns `RG_ERR_TABLES` on a mismatch; FULL and
   OFF return `RG_ERR_RELIEF` until S3.7.
4. The CLI now includes relief in its default output (`--relief ledges`), like buildings; `--only` restricts. The
   upstream `ledges_on_ground` is skipped in S3a (it is a no-op on a flat lattice, so the bytes are the same).
5. Row layouts 20 and 4 (ENABLED), if they have ledge cells, are written flat + berms in S3a (their solved relief is
   S3.6/S3.7); they take no junctions (rel:2353).
6. The relief builder keeps its per-layout lip bitmap and its raised-point array as plain arrays (the spec's "bitmap
   not dict"); the ground-colour set is a 4 KB bitset per ledge cell.
Open: the Azahar parity (device SHA-1 vs host) and the Route 101 visual check (lead). Functions in rg_ledge.c stay
under about 60 lines; `ledge_row` carries an unused `e` that is harmless.
