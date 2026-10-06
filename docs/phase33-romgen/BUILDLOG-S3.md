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

## 2026-10-06 — S3.1 lead verification (host suite, device parity, emulator visual)

- Host: `ROMGEN_ROM=$PWD/roms/emerald.gba make -C tools/romgen test` (the path must be ABSOLUTE; a relative one
  silently skips the real-ROM parts because the tests run from tools/romgen) gives all 12 suites 0 failures;
  relief_ledge has 55152 checks.
- CLI: `romgen roms/emerald.gba OUT --time` gives relief.bin 23292 B, 23 rows, 850 cells, 863 ledge cells, 6.1 ms.
- Device parity (Azahar, New-3DS mode, `ROMGEN_DEV_HOOK=1` auto-run): `relief ledges: 23292 bytes, 23 rows, 850
  cells, 1073 ms`. regions, signposts, buildings and **relief are all byte-identical to the host**. This is
  emulator evidence, not hardware evidence.
- Visual (O5 row 1): Route 101 (0/16) through the save warp, voxel mode on. The same spot with and without
  relief.bin (`evidence/s31-route101-ledges-relief.png` vs `-norelief.png`). The pixel difference
  (`s31-route101-diff.png`) sits exactly on the three ledge runs, plus sparkle-particle noise. With relief the lips
  stand up as a thicker raised band. It is subtle by design (LIP 6 px) from the default camera. **PASS.**

## 2026-10-06 S3.2 Tables, CPython order, S0 additions, alias

Built: `rg_pyset.{h,c}` (hash64 int/tuple, set_order, commonest + int/rgb wrappers; caller-provided scratch via
`rg_set_scratch_bytes` / `rg_commonest_scratch_bytes`), `rg_rtables` completed (A.1 ids, A.5 `RG_OUTDOOR_MAPS_BY_FOLDER`,
A.7 `rg_dir_sort_key`, `rg_relief_alt_base`, T1-T9 all in `rg_rtables_check`), `rg_behavior.h` A.6 sets, G7
`rg_cell_image_layers` (`rg_cell_image` now calls it), G8 `rg_props_cells_in` (+ `rg_props_open/_cells/_close` to hold the
connection table), `rg_ralias.{h,c}` (alias_of, AliasArt metatile/cell_image, own_id), `rg_rrock.h` (the rock id sets
and colours of rel:559-631, numeric, header only), `docs/PROVENANCE.md` (10 new rows, one per table/encoded rule),
`test/host/test_romgen_pyset.c`, `test/host/test_romgen_rtables.c`.

Results (ROM `roms/emerald.gba`, `ROMGEN_ROM` absolute, no suite printed "skipped"):
```
make -C tools/romgen test     # 14 suites, 0 failures
  art 33149, bimg 10481, buildings 4400, expand 1512, export 580568, geom 585, interior 851, pyset 28 (no ROM part),
  regions 474, relief_ledge 55152, roles 656013, rtables 9981 (3515 without the ROM), signs 1751, world 612
```
- A.8: every vector passes exactly (6 hashes, 4 set orders incl. the 30-tuple one); recomputed with CPython 3.14.0
  builtins before coding, they match the spec. Extra: 1000-key resize run, duplicates, commonest tie goes to first in set order.
- T1-T9 pass on the real ROM on the first run, including T7 (down 27 up 27 left 40 right 40 dive 7 emerge 7) and T8
  (Route 104: up->0/3, down->0/20 offset 0, right->0/0 offset 50). Refusals tested by tampering the opened world: T1
  (a width), T2/T3 (a map's layout id), T5, T6 each make `rg_rtables_check` fail; restored, it passes again.
- G7: of the 512 primary metatiles of Route 101's pair, 171 have an empty upper layer (lower-only == two-layer image,
  exactly), 335 differ; `rg_cell_image` equals `rg_cell_image_layers(false)` for all.
- G8: 56 layouts carry prop cells, 8194 cells (sea_rock 4440, sand_boulder 210, sea_stack 3544); every copy the S2 props
  models record (5 models) has its min cell marked; one-shot and held-context forms agree.
- Alias: 13 has 7 mapped ids, 136 has 9, 292 has 13 (colours 6, 7, 12). `own_id(general(m)) == m` holds on every mapped id
  (no General id has two own ids, so the more-used-twin rule never fires on this ROM); the colour map is a function; the
  recoloured own image of each aliased id equals the reference layout's image of its General id exactly (29 of 29).
- relief.bin in LEDGES mode unchanged: SHA-1 of the CLI's relief.bin (`--only relief`) before `eb25a3835edf7ebbcc9d634dd199be955fb4d27e`,
  after `eb25a3835edf7ebbcc9d634dd199be955fb4d27e`.
- Device builds: `make clean && make -j8 ROMGEN_DEV_HOOK=1` compiles (no warning in any romgen file); release rebuilt with
  `make clean && make -j8 && make cia`: `3DGBA.3dsx` 4,582,344 bytes and `3DGBA.cia` 2,156,992 bytes, both identical to the
  S3.1 release sizes (the new modules are not linked in until S3.3+ calls them). Hook 0, `VX_DEV_ALLOW_O3DS` 0.
  Not run: Azahar.

Deviations and decisions:
1. **`rg_map_connections_all`** added to rg_world (the dive/emerge-including reader) because T7 needs the raw census and
   `rg_map_connections` filters those entries. `rg_map_connections` itself is unchanged in behaviour.
2. **G8 has two forms.** The spec's one-shot `rg_props_cells_in(w, id, flags)` exists, and `RgProps` (open/cells/close) holds
   the connection table so S3.4 does not rebuild it for each of the 87 layouts. Flags are one bit per object (sea_rock 1,
   sand_boulder 2, sea_stack 4), nonzero = any; the spec's "names filter = all three" holds.
3. **Rock sets live in `rg_rrock.h`, not `rg_rtables`**: they are numeric upstream (no decomp), so they carry the Zallax
   header, not the pret one. The colours are there too (verbatim 8-bit triples) for S3.4.
4. **Sets of ids are predicates** (`rg_is_rock_tile` etc.); alias_of's sorted(roles) is "ids 0..511 for which `rg_is_alias_role`".
   Colour votes are kept in upstream's insertion order so `most_common(1)` ties break to the first inserted pair.
5. `rg_alias_of` also keeps dense lookup tables (`toGen`, `toOwn`, 1024 ids) so the per-cell calls of S3.3/S3.4 are O(1).
6. The T9 jump check is on Route 101 only and "every FLAT value < 0xF0" is the spec's; the extra test that FLAT and S0's
   water set do not overlap passes.
Open: nothing unresolved. The group-existence assertions for seeds 20/21/22/38 wait for find_drawn (S3.3, O3).

## 2026-10-06 S3.3 find_drawn, links, groups, seams

Built: `rg_rdrawn.{h,c}` (find_drawn: seeds, A.5-folder-order links, rock_near, FIFO BFS groups, alternate groups;
`rg_drawn_group` / `rg_drawn_excluded`; `rg_map_links_sorted`; `rg_seam_cells`), `--relief-log` in the CLI (prints the
groups), `test/host/test_romgen_relief_drawn.c`, two PROVENANCE rows updated (group-existence assertion now made).
No new decomp table; nothing from pret added.

Results (ROM `roms/emerald.gba`, `ROMGEN_ROM` absolute, no suite printed a skip):
```
make -C tools/romgen test     # 15 suites, 0 failures
  relief_drawn 1388 checks (the 14 earlier suites unchanged: art 33149, bimg 10481, buildings 4400, expand 1512,
  export 580568, geom 585, interior 851, pyset 28, regions 474, relief_ledge 55152, roles 656013, rtables 9981,
  signs 1751, world 612)
romgen ROM OUT --relief-log   # relief.bin SHA-1 eb25a3835edf7ebbcc9d634dd199be955fb4d27e (unchanged); other three files unchanged
```
find_drawn on the user's ROM: **54 seeds, 96 recorded links (48 connections, both sides), 35 groups = 31 seed groups + 4
alternate groups, 66 pool members.** Largest group: seed 7, members {7, 40-45} (7 layouts), canvas **200 x 240 cells**
(= 3200 x 3840 px = 12.3 M px). Others: 6 {6,37,36} 200x110; 27 {27,29,28} 140x140; 16 {16,48,47,263} 240x40;
239 {239,238,241,394,240,395} 120x80; 345 {345,265} 128x72; 34 {34,35} 80x160; 19 {19,26} 120x100; 12 {12,23}
80x20; 32 {32,15} 100x40; every other seed group is a single layout. Alternate groups (392, 46, 319, 357) copy their base's
group with the base swapped for the alternate. The full list is pinned in the test (`kGroups`), and the ordered link list
by an FNV hash (0xbca01a91), because they fix variant numbering.

Against the survey's approximate probe (51 seeds / 28 groups / largest {7, 40-45} 200x240): the survey did not alias, and
the three Lavaridge-art layouts 13, 136, 292 have **no** General rock tile raw (0) but 56 / 745 / 316 after `alias_of`, so
they become seeds and form 3 single-layout groups: 51 + 3 = 54 seeds, 28 + 3 = 31 groups. The largest group and its canvas are
identical. (Raw count with alternates 55 = 51 + the 4 alternates, which upstream skips as seeds.)

Spec points that did not hold on this ROM (recorded, not "fixed"):
1. **SPEC-S3 O3 "the group whose seed is 20 contains 4, 21, 22" is false.** Layouts 20, 21, 22 are each a single-layout
   group (no link at all: the seams between them meet no rock within DRAWN_SEAM). Layout 4 (Rustboro) has **no** rock tile
   (not a block layout, in no group). So the WRAP groups are three singletons and the ENABLED fallback `solve()` for 4
   will be the only path for Rustboro. The group-existence assertion for 20/21/22/38 holds (all four exist; 38 is
   excluded). The test asserts the real facts above instead of the spec sentence.
2. The link list of 20 is empty; T8's order is therefore checked on `rg_map_links_sorted` (20 -> 4 up o0, 20 -> 21 down o0,
   20 -> 1 right o50 all present; the list is sorted by rank, not ROM order).

Oracles: the links are cross-checked by an **independent formulation** (b placed in a's frame, global cells within 2 of the
edge, over every ROM connection between block layouts): the recorded link set equals the independent seam set exactly.
map_links: 148 sorted, unique, every outdoor-to-outdoor connection of every ROM map (all maps, not the A.5 table) present,
alternates mirrored. Determinism: two `rg_drawn_find` runs are memcmp-equal.

Device builds: `make clean && make -j8 ROMGEN_DEV_HOOK=1` compiles (no warning in rg_rdrawn.c); release rebuilt with
`make clean && make -j8 && make cia`: `3DGBA.3dsx` 4,582,344 B, `3DGBA.cia` 2,156,992 B, same as S3.2 (find_drawn is not called
by the device path until S3.7). Hook 0, `VX_DEV_ALLOW_O3DS` 0. Not run: Azahar.

Deviations and decisions:
1. **`drawn_ok`** needs `world_levels()["regions"]` (S3.5). `rg_drawn_group(d, id, checked, regionOk)` takes that membership as an
   optional per-group array (NULL = exclusion only); S3.5 supplies it.
2. Groups are named by the seed's layout id; DRAWN_EXCLUDED is "key == 38 and not an alternate group" (an alternate group's
   Python name is the alternate's own, never "route122").
3. Rock tiles are kept as one w*h bitmap per kept layout (alias applied) instead of the metatile list; `rock_near` reads it.
4. `rg_seam_cells` takes ROM direction codes and layout sizes directly (no dict); edge codes are ROM codes.
Open: nothing unresolved. Lead review of member order requested by the spec: the pinned list is in the test.
