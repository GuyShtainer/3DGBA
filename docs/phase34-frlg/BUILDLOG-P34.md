# BUILDLOG-P34

Phase 34 FRLG build log. One entry per slice; commands and outputs verbatim.

## R0 Profile plumbing, Emerald only (2026-10-06)

Branch `worktree-agent-acd70da9d250c7ef3`, based on main `6f37a41`. Renderer track: touches no existing romgen file
(`rg_gameprof.{h,c}` are new). Emerald only: the FRLG rows have `game = GP_NONE`, so `gameprof_detect` refuses them.

Environment note: mid-session the Xcode license gate blocked `/usr/bin/git|clang`; every command here ran with
`DEVELOPER_DIR=/Library/Developer/CommandLineTools`. The worktree also needed the git-ignored generated inputs of the
main tree (`external` and `roms` symlinks, `source/assets_gen.h`, `data/*.bin`, `assets/*`), copied or linked, never
committed.

### What changed
- New `source/romgen/rg_gameprof.{h,c}`: `GameProfile`, `GpGame`, `GpBehSet`/`gp_beh`, `gameprof_detect`,
  `gameprof_emerald`. The Emerald row is built from the existing `gba_game.h` macros; its 11 behaviour sets are
  generated at first use from `rg_is_water/jump/house_door/sand` and the renderer's `MetatileBehavior_*` (tall grass
  has no predicate in the code: the four values 0x02, 0x03, 0x07, 0x09 are a pinned list). One field beyond SPEC 1.1:
  `backupMap` (Emerald: fixed 0x02032318; FRLG: 0 = derive from the live pointer in R2).
- `source/voxel/gba_game.h`: includes the profile; new macros `GBA_ADDR_TILESET_BUILDING`, `GBA_BEHAVIOR_MASK`,
  `GBA_ATTR_LAYER_MASK/SHIFT`; `vx_prof()` / `VXP(field)`; `gTileset_General` and `UNPACK_BEHAVIOR` read the profile.
  `gVxProf` is defined in `vx_adapter.c`; NULL means the Emerald row, so every pre-Phase-34 caller is unchanged.
- 56 `VXP()` sites: `vx_snapshot.c` 14, `vx_adapter.c` 17, `voxel_atlas.c` 13, `voxel_world.c` 4 (+ 2 in
  `gba_game.h`). Left on the Emerald macros on purpose: `gTileset_Fortree`, `gTileset_GenericBuilding` (Emerald-only id
  tables, guarded in R2), the compile-time array bounds, and all `GBA_OFF_*` struct offsets.
- `source/vx_host.c`: `sBpee` -> `sGame` (`GpGame`); Rebind detects through `gameprof_detect` and sets `gVxProf`;
  `VX_DEV_FORCE_OVERLAY` keeps its old meaning (falls back to the Emerald row). Status string unchanged.
- `tools/romgen/Makefile`: `vx_behavior.c` added to the CLI cores (the profile builder calls it); new
  `make -C tools/romgen vtest` target (all 8 `test_voxel_*` suites, run from the repo root so the real-ROM re-check of
  `test_voxel_entities` finds `roms/emerald.gba`).
- Test headers of `test_voxel_{entities,mesh,world,adapter}.c` list `source/romgen/rg_gameprof.c` in their command line.
- New `test/host/test_romgen_gameprof.c`.

### Bits 8-9 measurement (SPEC 1.2 hazard, lead decision 9.1 #2)
Real Emerald ROM, `ROMGEN_ROM=<abs>/roms/emerald.gba`, every attribute of every tileset romgen opens:

    MEASURE bits 8-9 (0x0300): 0 of 18258 attributes in 73 tilesets are set; bits 8-11 (0x0F00): 0; layer-type nonzero: 8569, layer values seen mask 0x0007

Bits 8-9 are zero everywhere, so widening Emerald's `behMask` to 0x1FF would be a no-op. R0 still keeps Emerald at
0xFF (no behaviour change in this slice); the 1.5 packing path is NOT needed. R2 may share the 0x1FF mask.

### Gate (SPEC 0.3, renderer half plus the whole Emerald half), before vs after

Before = `6f37a41` with nothing changed; after = this branch. `diff` of the two captured logs shows only the added
`test_romgen_gameprof` suite (the voxel suites in the "after" run were built from the updated header command lines).

| Suite | before | after |
|---|---|---|
| test_voxel_entities | 35 | 35 |
| test_voxel_mesh | 20 | 20 |
| test_voxel_world | 133 | 133 |
| test_voxel_adapter | 122 | 122 |
| test_voxel_gate | 37 | 37 |
| test_voxel_lz77 | 173 | 173 |
| test_voxel_overlay | 23 | 23 |
| test_voxel_shims | 69 | 69 |
| test_romgen_art | 33149 | 33149 |
| test_romgen_bimg | 10481 | 10481 |
| test_romgen_buildings | 4400 | 4400 |
| test_romgen_expand | 1512 | 1512 |
| test_romgen_export | 580568 | 580568 |
| test_romgen_geom | 585 | 585 |
| test_romgen_interior | 851 | 851 |
| test_romgen_pyset | 28 | 28 |
| test_romgen_regions | 474 | 474 |
| test_romgen_relief_canvas | 1579 | 1579 |
| test_romgen_relief_drawn | 1388 | 1388 |
| test_romgen_relief_ledge | 55152 | 55152 |
| test_romgen_roles | 656013 | 656013 |
| test_romgen_rtables | 9981 | 9981 |
| test_romgen_signs | 1751 | 1751 |
| test_romgen_world | 612 | 612 |
| **test_romgen_gameprof (new)** | n/a | 5681, 0 failures |

No suite printed "skipped" apart from the pre-existing "0 skipped" counters.

CLI, `tools/romgen/build/romgen roms/emerald.gba OUT --time` (default relief mode `ledges`; S3.7 `--relief full` does not
exist yet), SHA-1, identical before and after:

    2929c7642be7ef83aad7cbb1619e062900ca4c74  buildings.bin
    007a370f440fa3c36cf0056440f05c025a386c4f  regions.bin
    eb25a3835edf7ebbcc9d634dd199be955fb4d27e  relief.bin
    385156050629ee50724bf3f6504991b10e48c7c1  signposts.bin

### Device build
`export DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM; make -j8` links (`built ... 3DGBA.3dsx`), and also
with `make ROMGEN_DEV_HOOK=1`. Release defaults unchanged (`ROMGEN_DEV_HOOK` 0, `VX_DEV_ALLOW_O3DS` 0). No new warnings
in the touched files.

### Not done here (the lead owns it)
- The Azahar Littleroot pixel-diff after merge (Azahar was not run).
- `make cia` was not run.

## 2026-10-06 — R0 merged into main (26d8bb1), lead verification

- Azahar, New-3DS mode, emerald-littleroot save warp, voxel on. Capture before the merge (main at 7b311c8) and after
  it. The pixel difference sits only on the two wandering NPCs and their shadows, the sparkle particles and the fps
  digits. Buildings, terrain, signposts and the player are identical. **PASS** (R0 renderer gate).
- After the merge: `make -C tools/romgen test` + `vtest` give 27 suites, 0 failures. Device `make -j8` links, both
  with `ROMGEN_DEV_HOOK=1` and without (this also covers S3.6's skipped hook compile). Release `.cia` rebuilt.
- Xcode license gotcha: once Xcode updated, /usr/bin/git and clang refuse to run until `sudo xcodebuild -license
  accept`. The workaround is `export DEVELOPER_DIR=/Library/Developer/CommandLineTools`.

## G1 romgen profile + FRLG world open (2026-10-06)

Worktree branch based on main `3e09081`. Romgen track. Commits: `9ebf3d3` (code + suite), then the docs commit.
Env: `DEVELOPER_DIR=/Library/Developer/CommandLineTools`; `ROMGEN_ROM` absolute. The new suite finds `firered.gba` /
`leafgreen.gba` beside `ROMGEN_ROM` (or via `ROMGEN_ROM_FR` / `ROMGEN_ROM_LG`, defined in `rg_fixture.h`).

### What changed
- `rg_gameprof.{h,c}`: real FR/LG rev 1 rows (ROM layer: 43 groups + sizes, gMapGroups, gMapLayouts, 384 slots, 640/640/7,
  attrs u32 at +0x14, mask 0x1FF, MapLayout 26 B, General/Building primaries) and the 11 romgen behaviour sets of SPEC
  section 2. RAM anchors stay 0 (R1's). New `gameprof_detect_romgen()` returns the FRLG rows; `gameprof_detect()` (the
  renderer's entry) still refuses a non-Emerald row until `gMain != 0`, so R0's test and the renderer are unchanged.
- `rg_world.{h,c}`: `RgWorld.prof`, `RgLayout.prof` (`rg_lprof()` falls back to Emerald for hand-built layouts),
  `groupStart/Count[RG_MAX_GROUPS 64]`, `RG_ERR_GAME` (`RG_ERR_NOT_BPEE` kept as alias), `layout_plausible`/tileset
  reads/caps/attr bytes from the profile, `rg_attr` -> u32, `rg_behaviour` -> u16 masked by `behMask`,
  `rg_metatile_entries` / `rg_tileset_addr_of` split at `nPrimMetatiles`, new `rg_find_map_layouts(w)` (search for header
  (3,0)'s layout pointer, accept the first hit consistent with every header). `rg_world_open` uses the stored value and
  falls back to the search only when a row stores 0.
- `rg_art.c` (palette/tile/metatile splits from `RgPair.prof`), `rg_buildings.c` (3 sites), `rg_roles.c` (six behaviour
  tests via `gp_beh`, house window from the profile), `rg_behavior.h` (pointer comment only).
- `rg_run.c`: non-Emerald games open the world, fill counts and return (no roles/regions/signs/buildings/relief). Specs: if
  `prof->specs` is set use it, else `rg_specs` (the Emerald row leaves `specs` NULL on purpose: pointing it at `rg_specs` would
  make `rg_gameprof.c` link `rg_bspecs.c`, which the vtest/renderer builds do not have).
- `romgen_cli.c`: include + one added line (prints `game: ...`; for non-Emerald sets `wantRegions = false` so no empty file
  is written).
- `test/host/rg_fixture.h` (`fxr_load_rom`, `FXR_ENV_FR/LG`), `test/host/test_romgen_frlg_world.c`: 14685 checks, 0 failures,
  0 skipped.

### Census (identical on FR and LG)
43 groups with the 43 sizes; 425 maps; 384 layout slots, 18 NULL, 309 referenced; 63 tilesets; 62 tileset pairs; 76 outdoor
maps; 1294 warps; gMapLayouts search = 0x0834EBFC (FR) / 0x0834EBDC (LG) and every header consistent; General primary
0x082D4B04 / 0x082D4AE4 used by 181 layouts, Building 0x082D4C24 / 0x082D4C04 used by 184; General has 640 metatiles; all
tileset counts within 640 / 384; Pallet (6,7),(15,7),(16,13) behaviour 0x69, collision-blocked, attribute is a u32 (> 0xFFFF);
pond metatiles carry 0x15; behaviour census over present layouts: 0x15 x40507, 0x1B x751, 0x02 x5701, 0x21 x2878, 0x38 x41,
0x39 x46, 0x3A x0, 0x3B x1022, no 0x3C-0x3F; largest value used 0xE0. FR and LG blockdata byte-identical in all 366 present
layouts (384 - 18 NULL).

### SPEC / SURVEY mismatches (true values pinned)
- Sign events: SURVEY says 506; the true count with the `kind <= 4` rule is **519** (= bg kinds 0 x422 + 1 x73 + 3 x14 +
  4 x10; the SURVEY's own kind histogram sums to 519). Pinned 519.
- "Identical blockdata in all 384 layouts": 18 slots are NULL, so it is 366 present layouts (pinned).
- Added pins: 62 tileset pairs, 184 Building-primary layouts (logged, not pinned).

### Gate (SPEC 0.3), before vs after
Before = `3e09081` untouched; the suite list now has 28 romgen+vtest suites (relief_solve, relief_world exist since R0 merge).
`diff` of the count lines: the only difference is the added `test_romgen_frlg_world`.

    2929c7642be7ef83aad7cbb1619e062900ca4c74  buildings.bin   (before and after)
    007a370f440fa3c36cf0056440f05c025a386c4f  regions.bin
    eb25a3835edf7ebbcc9d634dd199be955fb4d27e  relief.bin
    385156050629ee50724bf3f6504991b10e48c7c1  signposts.bin

Suites (before = after): art 33149, bimg 10481, buildings 4400, expand 1512, export 580568, gameprof 5681, geom 585,
interior 851, pyset 28, regions 474, relief_canvas 1579, relief_drawn 1388, relief_ledge 55159, relief_solve 379,
relief_world 691, roles 656013, rtables 9981, signs 1751, world 612; vtest: entities 35, mesh 20, world 133, adapter 122,
gate 37, lz77 173, overlay 23, shims 69. New: frlg_world 14685.

`romgen roms/firered.gba OUT` -> `game: FireRed rev 1`, `world: 384 layouts, 425 maps, 63 tilesets, 62 pairs, 76 outdoor maps`,
exit 0, OUT empty. Same for leafgreen (`LeafGreen rev 1`).

Device: `make -j8 ROMGEN_DEV_HOOK=1` then `make -j8` both link (3DGBA.3dsx); only pre-existing warnings.

### Notes for the lead
- Other test files' header command lines that list `rg_world.c` alone now also need `rg_gameprof.c` + `vx_behavior.c`
  (make targets already do).
- `romgen --dump-roles` on a non-Emerald ROM would dereference NULL regions (not guarded; G2 enables regions).

## B0 authoring toolchain + FRLG buildings plumbing (2026-10-06)

Worktree branch on main `dd248f4`. Romgen track. Commits: Kanto table + FRLG buildings pass, the authoring tool, the suite, docs.
Env: `DEVELOPER_DIR=/Library/Developer/CommandLineTools`; ROM env vars absolute (`ROMGEN_ROM`, `_FR`, `_LG`).

### What changed
- `tools/romgen/rg_png.{h,c}`: PNG writer (RGBA8, filter 0, stored deflate blocks, CRC-32 + Adler-32), no dependency.
- `tools/romgen/rg_author.{h,c}`: `romgen author ROM <census [--map G/N] | art LAYOUT X Y W H | preview SPEC | check [SPEC|TOWN|all] [--expect N] | placements SPEC> [--out DIR]`.
  Images go to `tools/romgen/out/author/<BPRE|BPGE|BPEE>/` (next to the build dir; git-ignored, checked with `git check-ignore -v`).
  `check` = ortho wrong/missing/extra per exact rect, density list, placements (vs `--expect`), consumer round trip of a one-model file; exit 1 on a failure.
  `preview` = ortho, diff (red wrong / blue missing / yellow extra), fl, fr, top (textured z-buffer, pitch 40 deg). Works on Emerald specs too (precedents).
- `Makefile`: one hunk, links `rg_author.c rg_png.c` plus the voxel consumer sources (for the round trip) into `build/romgen` and `build/romgen_mem`.
- `romgen_cli.c`: `author` dispatch. `source/romgen/rg_kspecs.{h,c}`: `rg_kspecs_table(prof, &n)`, the empty Kanto table.
- `rg_run.c`: FRLG runs the buildings pass over the Kanto table (no roles needed); regions, signposts, relief stay off.
- `test/host/test_romgen_frlg_buildings.c` (2390 checks, 0 skipped).

### Census (FR rev 1; LG identical row for row), counts only
76 outdoor maps; 277 warps on them = 191 doors (destination type 8) + 55 map-to-map (type 3) + 31 cave (type 4); 152 placements = 108 mainland + 44 Sevii.
Per map section (hex: count): Pallet 58:3, Viridian 59:5, Pewter 5A:6, Cerulean 5B:9, Lavender 5C:6, Vermilion 5D:8, Celadon 5E:9, Fuchsia 5F:9,
Cinnabar 60:5, Indigo 61:1, Saffron 62:13 (towns 74); Route 2 4, R4 1, R5 3, R6 2, R7 2, R8 2, R10 2, R11 1, R12 2, R15 1, R16 2, R18 1, R22 1,
R23 1, R25 1, Forest 2, Safari 6 (34); Sevii sections 8F:4, 90:4, 91:7, 92:7, 93:4, 94:4, 95:4, 98:1, 9A:1, 9F:1, A1:1, A5:2, A7:1, A9:1, AE:1, BB:1 (44).
All of SPEC section 6's per-town numbers hold.

### Discrepancies (true values pinned)
- SPEC 5.1 "277 door warps": 277 is every warp on an outdoor map. By the SPEC's own rule (destination type 8) there are **191** doors; the 152 / 108 / 44 are unaffected.
- `rg_buildings.c` needed **no change**: the direct-spec path has no CPython-order site (found order, id order, stable sorts by (layout, x, y), first-seen ties). The CPython order lives in the props / name-order expanders, which a Kanto table does not use.
- `rg_gameprof.c` is **untouched**: setting `specs` there would make every renderer-only build (vtest, VADAPT) link `rg_kspecs.c` and the builders. `rg_run.c` selects the table by game instead (`rg_kspecs_table`), so the profile hunk R1 owns cannot conflict. `GameProfile.specs` stays NULL for all rows.
- Seed rects in `census` are a starting heuristic (blocked secondary-tileset cells touching the door); the author fixes the rect from `art`.

### Gate (SPEC 0.3), before vs after: identical
    2929c7642be7ef83aad7cbb1619e062900ca4c74  buildings.bin
    007a370f440fa3c36cf0056440f05c025a386c4f  regions.bin
    21a837f091c6b4764ad284e02438f7113c829cbf  relief.bin (FULL)
    eb25a3835edf7ebbcc9d634dd199be955fb4d27e  relief.bin (--relief ledges)
    385156050629ee50724bf3f6504991b10e48c7c1  signposts.bin
All 30 suite counts (22 romgen incl. pyset 28 and relief_faults 221, 8 vtest) identical; only addition: test_romgen_frlg_buildings 2390, 0 failures, 0 skipped.
`romgen firered.gba OUT` -> `buildings.bin: 24 bytes, 0 models`, exit 0, loaded by the vendored consumer (test). Device `make -j8 ROMGEN_DEV_HOOK=1` and `make -j8` link.
## R1 - FR/LG anchors, the self-check, the voxel.log line (branch worktree-agent-a9388853c0d669c13, based on 1a879a3)

Added: FR and LG rows in `rg_gameprof.c` (`rendererOn = false`, so `gameprof_detect()` still refuses them and the app renders 2D),
`source/romgen/rg_anchor.{h,c}` (pure C, 20 numbered checks), `vx_host.c` probe (ROM checks once per bind, RAM checks once per
settled map, one `voxel.log` line each, no retry), tests in `test_romgen_gameprof.c`, PROVENANCE rows (one per value).

### Harvested anchors (value, method, live status)
Live = the anchor's RAM/ROM check passed on the running game in Azahar (New-3DS mode), Pallet Town save copies
`roms/firered-pallet.sav`, `roms/leafgreen-pallet.sav` (3/0, 12,12; continue-game warp; checksum lengths self-calibrated).
I did not dump raw RAM values through gdbio: gdb reads the 3DS process, not GBA address space, so "live" means the in-app check.

| anchor | FR | LG | method | live FR | live LG |
|---|---|---|---|---|---|
| gMain / sb1Ptr / backupLayout | 0x030030F0 / 0x03005008 / 0x03005040 | same | gamestate.c (V-GS) | checks 11-13 | checks 11-13 |
| mapHeader / objEvents / playerAvatar / sprites | 0x02036DFC / 0x02036E38 / 0x02037078 / 0x0202063C | same | gamestate.c (V-GS) | checks 15-17 | checks 15-17 |
| plttUnfaded / paletteFade | 0x020371F8 / 0x02037AB8 | same | ROM literal in CB2_Overworld; gamestate.c | check 18 | check 18 |
| weatherPtr | 0x083C2C2C | 0x083C2A68 | literal pool + Thumb disassembly (LG not a constant shift: -0x1C4) | check 19 | check 19 |
| weatherOff | {0x6D0,0x6C6,0x730,0x6FB,0x724} | same | struct arithmetic, ROM immediates | check 19 | check 19 |
| gfxInfoPtrs (152) | 0x0839FE20 | 0x0839FE00 | ROM pointer-run scan, +0x1C ROM pointer | ROM check 7 (real ROM) | same |
| fldeffTemplates (36) | 0x083A0080 | 0x083A0060 | ROM scan, template pointers after the gfx table | ROM check 8 (real ROM) | same |
| cb2Overworld / cb2OverworldBasic | 0x080565C9 / 0x080565BD | same | ROM literal scan | checks 10, 20 | checks 10, 20 |
| backupMap | 0 | 0 | derived (read from backupLayout.map) | check 13 | check 13 |

### Check numbering (rg_anchor.c; each returns the first failing number, 0 = ok)
ROM: 1 header code/rev, 2 extents in ROM, 3 mapGroups, 4 header layout == mapLayouts[id-1], 5 General primary, 6 Building primary,
7 gfx table, 8 fldeff table, 9 weatherPtr + weatherOff, 10 both cb2 + the &gPaletteFade literal.
RAM: 11 gMain+4 ROM pointer, 12 sb1Ptr, 13 backupLayout map, 14 w/h == layout+15/+14, 15 live layoutId == ROM, 16 player object
(id, isPlayer, coords == pos+7), 17 player sprite anim table is a ROM pointer, 18 paletteFade y <= 16, 19 weather curr/palState,
20 callback2 is cb2 and not in battle.
voxel.log, FR: `vx: rom anchors ok (BPRE rev 1)` then `vx: anchors ok (BPRE rev 1) map 3.0`; LG: `(BPGE rev 1)` ... `map 3.0`;
the game stays 2D (screenshot: Pallet Town, no voxel).

### Gate
- Romgen tests: every count identical except gameprof 5681 -> 5798. vtest: all lines identical.
- Emerald Littleroot (emerald-littleroot, voxel on, pre-change build 1a879a3 vs this branch): the pixel diff is ~780 pixels
  in small specks (NPCs, sparkles, fps digits); no terrain or model difference.
- Device: `make -j8 ROMGEN_DEV_HOOK=1`, touch main.c + romgen_dev.c, `make -j8`: all link.

### Deviations from the SPEC
- `vx_anchor_check_ram(prof, rom, size, ram)` takes the ROM too (checks 14, 15, 19 need it); the ROM check takes `size`.
- New field `rendererOn` in GameProfile (the gate that keeps FRLG 2D until R2); lives in `source/romgen/` with the rest.
- Check 17 tests the sprite's anim table (+0x08), not the template (+0x14): live, the player's template pointer is
  0x03007DAC, a stack copy in IWRAM, so SPEC 3.3's "template is a ROM pointer" is wrong for FRLG.
- The RAM checks run once the map has been stable for 45 candidate frames: a map load updates the saved location a few frames
  before the backup layout, and the first live run logged a false check-14 failure (w/h still the previous map's).

### Notes for the lead
- Test setup: `roms/` in a worktree is a symlink to the main tree's, so the Pallet saves land there (gitignored).
- The emutest state dir path is too long for the AF_UNIX socket in a worktree: set `EMUTEST_STATE_DIR=/tmp/<short>`.
- A pre-existing `sdmc:/3DGBA/gameA.gba` (an Emerald copy) and `gameA.sav` blocked `--stage-roms`; I moved them to
  the session scratchpad (`sd-backup/`) and put back after the runs.
- Not live-verified: the raw weather/palette values (only through the checks), the fldeff and gfx tables (ROM-only).

## K1 Pallet Town recipes (2026-10-06), in progress
- k_pallet_house banked: 80x64, hip-roof house (rg_hiproof + one Prism, kit_house shape), check PASS ortho 0/0/0 on both exact rects, density empty, round trip ok; placements (5,4) and (14,4) on layout 78.
- k_pallet_lab banked: 112x64, flat roof + vent unit over a yellow-brick front (rg_flat_block shape written out locally for plain-brick end walls), check PASS ortho 0/0/0, density empty, round trip ok; placement (13,10).

### K1 final (2026-10-06)
Worktree branch on main `35d6a32`. Files: `source/romgen/rg_kspecs_pallet.c` (new), `rg_kspecs.c` (extern + one `sTowns` line), `test/host/test_romgen_frlg_buildings.c` (empty-table test replaced by TestPallet; census now expects `covered 3 / 152`). Layout 78 FNV pin `843369BB` (FR = LG).
Census: `covered 3 / 152` (Pallet 0x58 all three placements).

Per-model checklist (5.5):
- k_pallet_house, rect (5,4,5,4) 80x64, Hip roof + one Prism (kit_house shape; arg-free builder). 1 covered (2 placements: (5,4), (14,4)). 2 exact rects written as numbers: facade (2,32,78,64), roof+eave (14,0,66,32). 3 check: ortho 0/0/0 both rects, density 0 bad, round trip ok. 4 placements = exactly (5,4) and (14,4), map 3/0, none elsewhere. 5 viewed: gabled/hipped roof with overhanging eave over a plain two-window facade reads right from fl, fr and top. 6 gate identical. 7 pending R2/M1.
- k_pallet_lab, rect (13,10,7,4) 112x64, flat-block shape written out locally (3 roof slabs around a vent Prism + front Prism, plain-brick end walls). Exact rects: facade (0,32,112,64), roof+vent+cornice (0,0,112,32). check ortho 0/0/0, density 0, round trip ok. placement (13,10) only. 5 viewed: flat front with cornice, grid roof and the red vent unit read right from all three views. 6 identical. 7 pending R2/M1.

Tests: frlg_buildings 2467 checks, 0 failures (was 2390). FR buildings.bin 76032 bytes, 2 models, 3 placements, SHA-1 `66b63ede1e7eb54828654bf4a86f7a899b6c1aa7`; LG file byte-identical.

### Gate (SPEC 0.3), Emerald, identical to the B0 entry
    2929c7642be7ef83aad7cbb1619e062900ca4c74  buildings.bin
    007a370f440fa3c36cf0056440f05c025a386c4f  regions.bin
    21a837f091c6b4764ad284e02438f7113c829cbf  relief.bin (FULL)
    eb25a3835edf7ebbcc9d634dd199be955fb4d27e  relief.bin (--relief ledges)
    385156050629ee50724bf3f6504991b10e48c7c1  signposts.bin
`make -C tools/romgen test` + `vtest`: 31 suites, every count equal to the B0 entry (art 33149, bimg 10481, buildings 4400, expand 1512, export 580568, gameprof 5681, geom 585, interior 851, regions 474, relief_canvas 1579, relief_drawn 1388, relief_faults 221, relief_ledge 55159, relief_solve 379, relief_world 691, roles 656013, rtables 9981, signs 1751, world 612, frlg_world 14685, pyset 28; relief_full 1538098; vtest entities 35, mesh 20, world 133, adapter 122, gate 37, lz77 173, overlay 23, shims 69); only frlg_buildings grew. 0 skipped, 0 failures. Device `make -j8` and `make -j8 ROMGEN_DEV_HOOK=1` link.
## R2 - FRLG renderer path, M0 (branch of c9edf92 + ceb83c1 + this entry, based on bab1e69)

Pallet Town renders in voxel 3D on FireRed and LeafGreen rev 1 with NO data files (no buildings.bin / regions.bin / relief.bin in
`sdmc:/3ds/3DGBA/voxel/BPRE` or `BPGE`; the dirs were never created). Emerald is unchanged.

### What changed
- `gba_game.h`, `vx_adapter.{h,c}`: u32 attributes interned to u16 (`vx_intern_attrs32`: behaviour 9 bits, layer type bits 29-30 into
  bits 12-13; the Emerald table stays a zero-copy ROM pointer); `MapLayout.borderWidth/Height` read from layout +0x18/+0x19 (2x2, 3x2,
  0x0 stored as one zero cell); `vx_border_cells/vx_border_cell`; per-profile data dir and pak path; `weatherPtr` dereferenced once per
  bind; backup-map base taken from the live pointer (`backupMap == 0`).
- `vx_snapshot.{h,c}`: `backupMapBase`, FRLG weather base, bounds checks. `voxel_atlas.c`: tiles per primary tileset from the profile (640).
- `voxel_world.c`, `voxel_tree.c`, `vx_behavior.c`: Emerald-only id tables guarded by `emeraldIdTables`; the behaviour predicates
  read the active profile's sets (the raw Emerald tables are renamed `MetatileBehavior_Emerald*` and feed the Emerald profile builders).
- `rg_gameprof.c`: `rendererOn = true` for FR/LG. `vx_host.c`: ROM anchors gate detection, RAM anchors gate FRLG candidacy, callback
  normalised to the Emerald CB2 values for `voxel_gate`, non-outdoor map types (anything but 1,2,3,5,6) hand the frame back to 2D,
  status string "Voxel 3D: Emerald, FireRed, LeafGreen (rev 1)".
- Tests: `test_voxel_adapter` (intern known answers, border helpers, real-ROM layout border census 330/7/28 + 19 unreadable slots),
  new `test_voxel_frlg` (synthetic live state over the real ROM: snapshot -> adapter -> world for Pallet, 84 checks), `test_romgen_gameprof`
  (rev 1 detected, rev 0 refused), and one line of `test_romgen_frlg_world.c` flipped (see deviations).

### Emulator M0 (Azahar, New-3DS mode, voxel pref on, harness state dir /tmp/r2emu)
| check | FR | LG |
|---|---|---|
| Pallet in voxel 3D, no data | `evidence/m0-fr-pallet.png` | `evidence/m0-lg-pallet.png` |
| voxel.log | `vx: rom anchors ok (BPRE rev 1)`, `vx: anchors ok (BPRE rev 1) map 3.0` | same with BPGE |
| Route 1 connection (warp save 3/19 at 12,40, Pallet's houses visible at the south edge) | `evidence/m0-fr-route1-connection.png` | crossing from Route 1 into Pallet: `evidence/m0-lg-route1-crossing.png`, log `map 3.19` then `map 3.0` |
| Indoor (player's house 1F, map 4.0) | plain 2D: `evidence/m0-fr-house-2d-indoor.png` | not run |
| Leave the house | 2D, black fade, then 3D Pallet with no garbage frame in the sampled frames (about one shot per 2.5 s): `m0-fr-house-exit-fade.png`, `m0-fr-house-exit-3d.png`; log `map 4.0` then `map 3.0` | not run |
| Emerald Littleroot before (bab1e69 build) vs after | `m0-emerald-littleroot-before.png` / `-after.png`: 0.106 % of pixels differ above a delta of 24, in small specks (NPCs, sparkles, fps digits); `vx:` log lines identical | |

Not verified: the status string is code only (no menu screenshot); the LG house exit; a fade captured at frame granularity (sampling was
about every 2.5 s, so a one-frame glitch could slip between shots; the hand-back path is the same one used for Emerald interiors with
interiors3d off).

### Gate
- Emerald SHA-1 (`romgen emerald.gba`, all outputs) identical before/after (`sha-before.txt` == `sha-after.txt`).
- `make -C tools/romgen test`: 23 suites, every count identical except `test_romgen_gameprof` 5798 -> 5799; 0 failures, 0 skipped.
- `make -C tools/romgen vtest`: every count identical except `test_voxel_adapter` 122 -> 562; new `test_voxel_frlg` 84 checks, 0 failures, 0 skipped.
- Device: `make -j8 ROMGEN_DEV_HOOK=1` then `make -j8` both link.

### Deviations
- `test_romgen_frlg_world.c` line 55 asserted the R1 behaviour (`gameprof_detect` refuses FRLG, "R1/R2 own it"); R2 turns the renderer on, so
  it now asserts `gameprof_detect(...) == p` for FR rev 1. Same check count (14685).
- Player (6,6)-style walking in the harness was unreliable (CTM timing), so the connection and the house were reached with warp-save copies
  (`roms/firered-r1.sav`, `firered-house.sav`, LG likewise; gitignored, made from the Pallet saves with the continue-game patch). Route 1 at
  y=70 is off the map (water-like border garbage in the first try); y=40 is inside it.
- FRLG gets a non-existent pak path so the Emerald-pinned `emerald3ds.pak` is never opened for them.
- `romgen_dev.c` still writes to `OUT_DIR "sdmc:/3ds/3DGBA/voxel"` regardless of game (G2).

### Notes for the lead
- The SD was restored byte-for-byte (gameA.gba/.sav, settings.bin, recent.bin); Emerald's `.bin` files untouched. `tools/emutest/.venv` is an
  untracked harness artefact, not committed.
- `--stage-roms` refuses while `sdmc:/3DGBA/gameA.gba` exists; I moved it to the scratchpad and put it back.
- The movie `ML.ctm` takes about 5000 emulated frames to reach the overworld; shots need 150-300 s of wall time per run in the voxel renderer.

## 2026-10-06: R2 merged (83274f7) + first in-game K1 models (lead)

- The host CLI made `buildings.bin` from each ROM: FR → `sdmc:/3ds/3DGBA/voxel/BPRE/`, LG → `.../BPGE/`. Both are
  76032 B, SHA-1 66b63ede… (= the K1 pin), 2 models, 3 placements.
- Azahar, New-3DS mode, release build at 83274f7, Pallet saves:
  - `evidence/m1-fr-pallet-models.png` and `evidence/m1-lg-pallet-models.png`. Both Pallet houses and Oak's Lab are
    real 3D models, with walls, depth, a roof and the lab's vent and side wall. They cast shadows on the ground.
  - The flowers, fences and signs are still ground art (no regions/signposts for FRLG until G2). The tree walls are
    flat (T1).
- M1 is not closed yet. Still needed: G2 (terrain/water/signs), T1 (trees), the 4-angle and door checks, and then the
  hardware run.

## G2 - FRLG art, regions.bin and signposts.bin (branch of e3a7c32; commits 72733f0, b4c2ceb + this entry)

FireRed and LeafGreen rev 1 now make `regions.bin` (VXR5, 246995 B, 366 layouts) and `signposts.bin` (VXS2, 10376 B, 144 records) with the host
CLI and with the device hook; relief stays off until L1. FR and LG produce byte-identical files (asserted).

### What changed
- `rg_run.c`: the FRLG early-return is gone. Roles, regions, signposts and buildings run for every game; only the relief call is gated on
  `GP_EMERALD` (the Emerald-only relief modules are never reached).
- `rg_art.c` and the `is_house` window were already profile-driven since G1 (7 primary palettes, tile split 640, `houseHalfWidth/Height` read from the
  profile). G2 only had to prove them: `test_romgen_frlg_regions` re-derives every Pallet metatile colour (98 distinct metatiles, 37922 pixels) straight
  from the ROM tables (pal < 7 from the primary palette block, pal >= 7 from the secondary; 14036 of those pixels use palettes 7-12 and 18553 would come out
  different from the wrong tileset, so the check can fail).
- `rg_roles.c` (is_signpost): on FRLG a sign is **a blocked outdoor cell with behaviour 0x84 and a walkable cell south**. The Emerald lantern / open-post
  heuristics are not used there: they missed Pallet's two mailboxes (metatile 0x2AD touches the house wall, role came out WALL) and produced 12 cells that are not
  signs. Emerald branch unchanged (its output is pinned). The door rule needed no change: `build_houses` never required a walkable door, and the three Pallet doors
  (collision-blocked, 0x69) make WALL roles (tested).
- `rg_gameprof.c`: `houseHalfWidth` 5 -> **8**, `houseHeight` stays 7, both measured: door-to-building extents over the 191 census doors (SPEC 5.1 seeds,
  door to the far edge / rows above the door): 8 columns cover 98 %, 7 rows cover 97 % (the Emerald placeholders 5 / 7 cover 83 % / 97 %). The census seeds merge
  touching buildings, so no window is tight; the value only decides how far a house mass may spread and changes no pinned Emerald byte.
- `romgen_cli.c`: FRLG no longer forces `wantRegions = false`; `--dump-roles` returns with a message when there is no regions buffer instead of dereferencing NULL.
- `romgen_dev.c/.h`, `main.c`: the device hook writes to `vx_profile_data_dir(prof)` of the ROM (Emerald `sdmc:/3ds/3DGBA/voxel`, FR `.../BPRE`, LG `.../BPGE`),
  runs once per game per session (bit per `GpGame`), and `main.c` offers BPRE / BPGE ROMs to it (rev 0 is refused inside via `gameprof_detect_romgen`).
  S3.8 is merged, so this hunk may land.
- `test_romgen_frlg_regions.c` (new, 1639412 checks); `test_romgen_frlg_buildings.c` one assertion updated (FRLG now has regions; 2467 checks unchanged).

### Pinned numbers (FR rev 1 = LG)
    regions.bin   246995 B  3716874d6ba477acbdaaecc079fd7dc77526cd1b
    signposts.bin  10376 B  ba2fde451aa9612c10f7a7a80cf0d9b06f007e06   (144 records, 6 with a head, 0 empty masks)
    buildings.bin  76032 B  66b63ede1e7eb54828654bf4a86f7a899b6c1aa7   (the K1 pin still holds)
    Pallet (layout 78) roles slab  f438cc30f0033f119e7bc4731722794311ba2739   floor 260 water 12 stair 10 wall 68 tree 116 fence 9 sign 5
    Route 24 (layout 112, map 3/43) slab  2d5f4d6cefd72d858956364f23e2ade5c7ca7229
    Pallet house door metatile 0x2A3 as rg_cell_image (16x16 RGBA)  f0e58e8686e7e54af622e5bfe3bb38953ed16430
Kanto roles: floor 78167 water 46563 ledge 1109 stair 3372 wall 4541 tree 23994 fence 5673 cliff 25042 signpost 144.
Signs: 151 outdoor sign BgEvents, 138 on a 0x84 cell, 135 land on a record (89.4 %); the 3 events on 0x84 that miss have no walkable cell south, the 13 that are
not on 0x84 are not signs. 147 outdoor 0x84 cells (138 with an event, 9 without), 144 have a south cell = the 144 records. Pallet: exactly the 5 census cells
(4,7) (13,7) mailboxes, (9,11) (5,14) (16,16) fence/post signs. Route 24: the Nugget Bridge deck is x 10-12, y 17-39; row 17 is behaviour 0x2A (never water on
FRLG), rows 18-39 behaviour 0, none of the 69 cells is water; the sea (0x15) is water everywhere and collision-walkable; the role is water iff the behaviour is in
the water set, asserted for every cell of all 366 layouts. SURVEY's "506 signs" / G1's 519 are all-layout event counts; the test counts outdoor events (151).

### Gate (SPEC 0.3), before vs after: identical
    2929c7642be7ef83aad7cbb1619e062900ca4c74  buildings.bin
    007a370f440fa3c36cf0056440f05c025a386c4f  regions.bin
    21a837f091c6b4764ad284e02438f7113c829cbf  relief.bin (default run = FULL)
    eb25a3835edf7ebbcc9d634dd199be955fb4d27e  relief.bin (--relief ledges)
    385156050629ee50724bf3f6504991b10e48c7c1  signposts.bin
`make -C tools/romgen test` + `vtest`: 31 suites before, 32 after; every count equal (art 33149, bimg 10481, buildings 4400, expand 1512, export 580568,
frlg_buildings 2467, frlg_world 14685, gameprof 5799, geom 585, interior 851, regions 474, relief_canvas 1579, relief_drawn 1388, relief_faults 221,
relief_full 1538098, relief_ledge 55159, relief_solve 379, relief_world 691, roles 656013, rtables 9981, signs 1751, world 612; vtest adapter 562, entities 35,
frlg 84, gate 37, lz77 173, mesh 20, overlay 23, shims 69, world 133); the only addition is `frlg_regions` 1639412. 0 failures, 0 skipped.
Device: `make -j8 ROMGEN_DEV_HOOK=1` then `make -j8` (release last) both link (3DGBA.3dsx).

### Azahar (New-3DS mode, release build, harness state dir /tmp/g2emu, host-CLI files in sdmc `3ds/3DGBA/voxel/BPRE` and `BPGE`)
- `evidence/g2-fr-pallet-signs.png`, `evidence/g2-lg-pallet-signs.png`: Pallet with the K1 house and lab models, both mailboxes and the three fence/post signs as 3D
  cut-outs, the pond as water. The ground itself is flat (no relief on FRLG until L1; the "terrain" the spec mentions is the ground art, the pond and the models).
- `evidence/g2-fr-route24-bridge.png` (warp save `roms/firered-r24.sav`, 3/43 at 11,27): the Nugget Bridge deck is solid planking over the water, the trainers stand on
  it, the river and the sea are water on both sides.
- SD restored: gameA.gba/.sav, recent.bin, settings.bin re-hashed identical to the pre-run backup; `azctl clean-fixtures` ran; Emerald's `voxel/*.bin` untouched
  (regions.bin still 007a370f...). The BPRE/BPGE dirs now hold the full G2 output (overwriting the K1 buildings.bin with an identical file).

### Deviations / notes for the lead
- `rg_art.c` needed no edit (G1 did the palette/tile splits); G2 added the proof. SPEC's "FRLG art" for G2 is therefore a test-only change.
- The FRLG sign rule replaces, not extends, the Emerald heuristics (see above). It is a `game != GP_EMERALD` branch in `is_signpost`, not a profile flag.
- `houseHalfWidth` 5 -> 8 is the one profile value that changed; nothing in the renderer reads the WALL role on FRLG, so it only moves regions.bin bytes.
- A new warp save `roms/firered-r24.sav` (+ symlink `firered-r24.gba`) was made in the gitignored `roms/`; Guy's originals untouched.
- Merge: `rg_gameprof.c` has two tiny hunks (a 2-line comment above `#define GP_FRLG_COMMON` and `.houseHalfWidth = 5` -> `8` on one line of that macro); T1's
  `treePart/treeGround` edits live in the FRLG row initialisers below it, so the hunks should not overlap.
## T1 - Kanto trees (branch of e3a7c32)

FireRed and LeafGreen rev 1 tree walls are now 3D trees: `voxel_tree.c` reads its tables from the game profile, and the
FR/LG rows carry the Kanto table. Emerald is unchanged (every metatile id 0..1023 gives the same part and ground as the old
switch statements; test_voxel_world compares them).

### What changed
- `tools/romgen/rg_author_trees.c` (+ `author ... trees [LO HI]` in rg_author.c, header, Makefile): lists the General-tileset
  metatiles that are foliage by Gummygamer's rule (re-implemented from the description, credited in the file header; the
  gen1recomp-voxel-frlg reference was read, never executed), with their counts, the 2x2 blocks they form, corner votes, and
  the foliage metatiles that never sit in a block. `trees LO HI` writes a contact-sheet PNG of a metatile range (review aid).
- `rg_gameprof.{h,c}`: `treePart` / `treePartCount`, `treeGround` / `treeGroundCount` as flat int16 pairs. Emerald row = the
  old tables as data; FR and LG rows = `kFrlgTreePart`. PROVENANCE: "ROM-measured, no decomp" (two rows added).
- `voxel_tree.c`: expands the profile's pairs into per-profile lookup tables (1024 ids), rebuilt when the profile pointer
  changes. Part and ground are O(1) per cell. Out-of-range ids behave as before (-1 / identity).
- Tests: `test_voxel_world.c` TestTreeTables (+6214 checks: every id 0..1023 against the old switches, FR/LG table rows via a
  header-only fake image, the 12 ids primary and < 640, Pallet block quadrants); `test_voxel_frlg.c` (Route 1 3/19 real
  layout: 278 tree-part cells, 138 top row + 140 bottom row, nothing outside the 12 ids; the old `trees == 0` on Pallet became
  `trees > 0`); new suite `test_romgen_frlg_trees.c` (the tool on the real ROMs); `test_romgen_gameprof.c` one assertion flipped
  (Emerald `treePart` is now non-NULL; same count). `test_romgen_frlg_buildings.c` gained one `#include` (it includes rg_author.c,
  which now references the trees function).

### The FRLG tree table (FR and LG rev 1 identical; metatile id -> part, 0 tl 1 tr 2 bl 3 br)
| part | ids |
|---|---|
| 0 (top row, left) | 0x1C, 0x1E |
| 1 (top row, right) | 0x1D, 0x1F |
| 2 (bottom row, left) | 0x14, 0x16, 0x24, 0x26 |
| 3 (bottom row, right) | 0x15, 0x17, 0x25, 0x27 |

No `treeGround` entries (no Kanto tree metatile paints canopy over other ground that a replacement would clean up). Pallet's
border block (SPEC T1) 1C 1D / 14 15 is quadrants 0 1 / 2 3 and is the most common block in the ROM. Reading the art: 0x14/0x15
is the canopy apex, 0x1C/0x1D the canopy body, 0x24/0x25 the trunk row; in walls the tiles stack as 1C over 14 (or 1C over 24 at
the map edge), so the table pairs each 1C-row tile with the tile below it.

### Tool output summary (`romgen author firered.gba trees`; leafgreen.gba is identical apart from the header line)
- Ground colour key BGR555 0x532E; 181 layouts use the General tileset; 1346 distinct 2x2 foliage blocks over them (blocks need all
  four cells foliage AND collision-blocked, as in Gummygamer's rule, which only looks at wall cells; tall grass is green too but walkable).
- 66 primary metatiles are foliage by the rule (119 with the older Zallax rule, which also flags plain grass); 14559 blocked-foliage cells
  in all. The 12 table ids carry 13 879 of them (e.g. 0x1C 2489, 0x14 2454, 0x1D 2432, 0x15 2397, 0x1F 1023, 0x1E 963).
- Most common blocks: 1C 1D 14 15 x1027, 14 15 1C 1D x572 (the same wall, other phase), 1C 1F 14 17 x245, 1E 1D 16 15 x232, 1E 1F 14 15 x219.

### One-tile foliage and the rest: decisions
Cut trees are object events, not metatiles (nothing to do). Every other foliage metatile the tool finds is left as flat ground art, by decision:
| id(s) | what it is (contact sheets) | blocked uses | decision |
|---|---|---|---|
| 0x005 | one round bush | 193 of 193 | flat. The only heavily used leftover; a one-cell bush is not a small tree (the small-tree crown is two tiles tall). Candidate for the L1 "shrub" part, same family as the Emerald shrubs 0x124/0x239/0x242/0x243 |
| 0x13D-0x13F, 0x145-0x147, 0x14D-0x14F | rectangular hedges (Gym / gate) | 0x13E 97, rest under 6 | flat (a hedge is not a tree) |
| 0x0FA/0x0FB | round hedge cap over a tree's bottom row | 33 / 34 | flat |
| 0x0D6/0x0D7, 0x0B4/0x0B5 (seen on the sheet), 0x10A/0x10B (not viewed) | a tree apex over a fence or a cliff | 62-65, 40, 5 | flat (their ground under the apex is not known without a replacement table; rare) |
| 0x1A/0x1B, 0x22/0x23, 0x012/0x013, 0x00A | narrow one-column pines | 1-6 each | flat |
| 0x255, 0x25D | secondary-tileset tree tops (ids >= 640) | 10, 11 | out of scope: the table is General-only (`VoxelWorld_UsesTreeSprites`) |
The table is data, so L1 (Emerald shrubs) and a later Kanto shrub part only add pairs (`part 4` = VOXEL_TREE_SMALL exists already).

### Gate (SPEC 0.3), before vs after
Baseline = main e3a7c32 built from `git archive` in a scratch directory; ROMGEN_ROM / ROMGEN_ROM_FR / ROMGEN_ROM_LG absolute.
- Emerald CLI SHA-1s unchanged: default run buildings 2929c764..., regions 007a370f..., signposts 38515605..., relief (FULL)
  21a837f0...; `--relief ledges` relief eb25a383...
- `make -C tools/romgen test`: 22 suites with identical counts (0 failures, 0 skipped), plus the new `test_romgen_frlg_trees` 61.
- `make -C tools/romgen vtest`, before -> after: entities 35 -> 35, mesh 20 -> 20, **world 133 -> 6347** (TestTreeTables), **frlg 84 -> 94**
  (Route 1 block; the Pallet `trees == 0` check became `trees > 0`, same count), adapter 562, gate 37, lz77 173, overlay 23, shims 69
  unchanged; 0 failures, 0 skipped. (The SPEC gate said world only; the frlg growth is the Route 1 check, which needs the real-ROM
  fixture that lives in that suite.)
- Device: `make -j8` links (3DGBA.3dsx); no new warnings in the files touched.

### Notes for the lead
- Emulator check (yours): Pallet's border/edge trees and Route 1's two side walls and north end should be 3D trees. Things to look
  at: the painted flat apex under the 3D crown (the floor keeps the tile's own art, as on Emerald), the trunk row 0x24/0x25 mapped as
  the bottom part, and the round bushes 0x005 (still flat) near Route 1/2 tree edges.
- Merge: `rg_gameprof.c` hunks are three: the two `kEmeraldTree*` / `kFrlgTreePart` arrays + `GP_FRLG_TREES` macro just above the Emerald row,
  four fields in the Emerald row, and `, GP_FRLG_TREES` appended after `.cb2OverworldBasic = ...` at the end of `GP_FRLG_COMMON` (not near
  the house-size fields). `rg_gameprof.h` gains `treeGroundCount` and a comment.

## 2026-10-06: T1 merged (457efb2), lead emulator check

- The host CLI regenerated FR/LG `regions.bin`, `signposts.bin` and `buildings.bin` into the emulated SD (`BPRE`/`BPGE`).
- `evidence/t1-fr-route1-trees.png`: Route 1's tree walls are 3D trees.
- `evidence/t1-fr-pallet.png`: Pallet's border trees are 3D (top corners); the signs, mailboxes and K1 models are as
  in G2.
- Still flat: tall grass (look backlog L2), flowers, fences and the round bush 0x005.

## K2 landmark recipes (2026-10-06), in progress
- k_center banked: 80x64 (rect (24,23,5,4) on layout 79 = rows 23-26 of the Viridian Center; the roof's top lip, 6 art rows in row 22, is outside the rect because Seven Island's Center stands at y=0 and a 5-row rect cannot place there). Emerald's center_or_mart shape: chamfered Frustum (plan front 65 / back 28, wallTop 28, bandRise 4), no crown vault. check PASS ortho 0/0/0 on 3 exact rects (the hip-side strips x<8 and x>=72 above row 28 are excluded: the frustum inset is 4 px, the art side slope is 7), density empty, round trip ok. placements 18 = the 16 census ones + Saffron (L207) and One Island (L88), whose Center is pixel-identical on every owned pixel. viewed: red roof, chamfered body, Poke Ball plate over the entrance read right from fl, fr and top.
- k_mart banked: 64x64, rect (34,16,4,4) on layout 79, matchRows (1,4) (row 0 = the roof's top lip is art only, rows 1-3 are matched). Same chamfered Frustum as k_center in blue (front 65, back 22, wallTop 20, bandRise 4); the side-wall strips wrap at columns 50-56 (plain panels; columns 8-16 carry the MART lettering). check PASS ortho 0/0/0 on 3 exact rects (hip-side strips x<8 and x>=56 above row 36 excluded), density empty, round trip ok. placements 13 = the 11 census ones + Saffron (L207) and One Island (L88), pixel-identical. viewed: blue roof over a white chamfered body, MART lettering and the sign plate read right from fl, fr, top.
- k_gym (+ k_gym_7, k_gym_8 width variants, one builder, arg0 = width px): check PASS on all three (ortho 0/0/0 on every exact rect, density 0, round trip ok); placements 2 + 4 + 1 = 7. Viewed previews (6-wide, 8-wide): slatted gold roof slab, GYM plate, porch with Poke Ball sign and doors read correctly from both oblique views and top.
- K2 done (2026-10-06): 5 recipes (k_center, k_mart, k_gym, k_gym_7, k_gym_8), 41 placements, census covered 39/152 (was 3). New FR = LG buildings.bin SHA-1 pin dec0c711992d4b9186249f298e4f10bac9d7393f (7 models, 41 placements, 906280 bytes). Emerald gate identical (buildings 2929c764, regions 007a370f, signposts 38515605, relief 21a837f0 / ledges eb25a383; FRLG regions 3716874d, signposts ba2fde45). `make test` and `vtest` 0 failures. Not done: Azahar Viridian screenshot (SPEC 5.5 item 7), skipped as too costly for this slice. Variants not mine: Saffron Center/Gym, One Island Center, Indigo Plateau.
- Evidence: evidence/k2-k_center.png, k2-k_mart.png, k2-k_gym.png, k2-k_gym_7.png, k2-k_gym_8.png (fl | fr | top views).

## 2026-10-06: K2 merged (f6679d2), lead in-game check

- Gate after the merge: Emerald SHA-1s unchanged (buildings 2929c764, regions 007a370f, signposts 38515605, FULL relief
  21a837f0, ledges relief eb25a383); FR buildings.bin `dec0c711992d4b…` (7 models, 41 placements); FRLG regions 3716874d,
  signposts ba2fde45.
- Azahar, FireRed, warp-save copy `roms/firered-viridian.sav` (3/1 at 30,28; gitignored, Guy's save untouched):
  `evidence/k2-fr-viridian.png`. The Viridian Pokémon Center stands as the K2 model with its sign and door; the Mart roof
  shows at the top edge. Flowers and the fence row are still flat (known, later slices). LG check pending.

## L1 Kanto ledges, `relief.bin` LEDGES mode for FireRed / LeafGreen (2026-10-06)

Branch `worktree-agent-a842df80d0dc18db5` on main `415119d`. Commit `ac29a11` (code + tests) plus this entry.

### What changed
- `rg_rtables.{h,c}`: new `rg_relief_outdoor_layout(w, id)`. Emerald returns `rg_relief_outdoor(id)` (test: equal for every id of the fixture); FRLG returns `RgLayout.outdoor`, which `rg_world.c` already sets from map types 1/2/3/5/6 (plus alternate inheritance) = SPEC 7.1's rule.
- `rg_ledge.{h,c}`: jump cells and `rg_ledge_layouts` read the profile's `jump` set (`gp_beh(&rg_lprof(L)->jump, b)`; FRLG 0x38-0x3B); `rg_ledge_enabled` now takes the layout and is false unless the game is Emerald (the ENABLED list is Emerald ids 20 and 4); `rg_ledge_layouts` calls `rg_relief_outdoor_layout`. Ascending id order unchanged.
- `rg_relief.c`: the T1-T9 tables check runs on Emerald only; on any other game FULL is built as LEDGES (Kanto has no drawn relief, so FULL = LEDGES there; `off` still writes nothing).
- `rg_run.c`: the `GP_EMERALD` gate on relief is gone. The device dev hook (`romgen_dev.c`, `opts.relief = RG_RELIEF_LEDGES`, writes to the per-game dir `voxel/BPRE|BPGE`) therefore writes `relief.bin` for FRLG too with no change to it. The renderer loads `relief.bin` through `VoxelFile_Open` like regions.bin, so the per-game dir needed no change (voxel.log: `VOXEL relief: 31 layouts`).
- `romgen_cli.c`: the status line says `ledges` for a FULL request that built no drawn rows.
- No profile fields `ledgeLip/ledgeWidth/ledgeBack` (see measurement).

### Lip measurement (Route 1 = layout 89, `romgen author ROM art 89 0 4 14 4`; an east/west ledge on layout 95, `art 95 8 2 6 6`)
Kanto's ledge strip is the same drawing geometry as Hoenn's: 8 px deep (a light top edge row and a dark shadow row inside the 8), half a cell, south ledge in rows 8-15 of the cell and east/west in columns 8-15. Numbers read from the stored heights (cells of all 31 rows, 27 distinct 25-height patterns): the south ledge cell is `0 | 3 2 3 2 3 | 6 5 6 5 6 | 3 3 3 3 3 | 0` (rows j=0..4), peak 6 = RG_LIP, ramp 3 at 4 px = 6*(1 - 4/RG_LIP_BACK); the east/west column is `0 3 6 3 0`. RG_LIP 6, RG_LIP_WIDTH 8, RG_LIP_BACK 8 all fit, so the Emerald constants stay and the profile carries nothing. The 5/6 alternation is the drawing's 1-px top-edge jitter, as on Emerald. (The 6 itself is a rendering choice, not a measurement of the art.)

### Numbers
- Route 1 (layout 89, 24x40): 61 ledge cells (61 jump cells, no junction), relief row of 61 cells.
- Whole file, FR = LG: 26320 bytes, 31 rows, 958 cells, 31 ledge layouts, 960 ledge cells; SHA-1 `32c24146974e720620e97906024d8a60d2f6e09c` (FR and LG byte-identical; the cart differs only in code/header, not in blockdata or attributes).
- Pins in `test/host/test_romgen_frlg_relief.c` (51714 checks): synthetic 3x3 (the S3.1 hand computation, column 0, 2.25, 4.5, 1.5, 0 -> stored 0 2 4 2 0) under an Emerald row recoloured as FireRed; 0x3C is not a jump in Kanto; indoor / underground layouts get no row; ENABLED false for FRLG even at id 20; every row an outdoor layout and every outdoor layout with a jump cell has a row; FULL == LEDGES == pinned bytes, OFF writes nothing; vendored `VoxelRelief_Init` reads every cell back; Route 1 first cell (2,5) heights.
- `test_romgen_frlg_regions.c`: its G2 assertion `relief == NULL` for FULL on FRLG became `relief != NULL && size 26320` (the expected L1 change, count unchanged).

### Gate (actual output)
    2929c7642be7ef83aad7cbb1619e062900ca4c74  buildings.bin   (Emerald, default run)
    007a370f440fa3c36cf0056440f05c025a386c4f  regions.bin
    385156050629ee50724bf3f6504991b10e48c7c1  signposts.bin
    21a837f091c6b4764ad284e02438f7113c829cbf  relief.bin (FULL)
    eb25a3835edf7ebbcc9d634dd199be955fb4d27e  relief.bin (--relief ledges)
    FR = LG: dec0c711992d4b9186249f298e4f10bac9d7393f buildings, 3716874d6ba477acbdaaecc079fd7dc77526cd1b regions, ba2fde451aa9612c10f7a7a80cf0d9b06f007e06 signposts, 32c24146974e720620e97906024d8a60d2f6e09c relief
`make -C tools/romgen test` (ROMGEN_ROM, 0 skipped): every suite count identical to K2 (art 33149, bimg 10481, buildings 4400, export 580568, frlg_buildings 2543, frlg_regions 1639412, frlg_trees 61, frlg_world 14685, gameprof 5799, geom 585, interior 851, regions 474, relief_canvas 1579, relief_drawn 1388, relief_faults 221, relief_full 1538098, relief_ledge 55159, relief_solve 379, relief_world 691, roles 656013, rtables 9981, signs 1751, world 612, pyset 28); new frlg_relief 51714; 0 failures. `vtest` (with ROMGEN_ROM_FR/_LG): entities 35, mesh 20, world 6347, frlg 94, adapter 562, gate 37, lz77 173, overlay 23, shims 69, 0 failures, 0 skipped. Device `make -j8 ROMGEN_DEV_HOOK=1` then `make -j8` both link (3DGBA.3dsx).

### Azahar (New 3DS, voxel on, FireRed + LeafGreen, warp-save copies `roms/firered-ledge.sav`, `roms/leafgreen-ledge.sav` = Pallet saves warped to 3/19 at 13,28; gitignored)
Ran on a private harness instance (`--instance l`, own bundle clone and sdmc under the git-ignored `tools/emutest/az-l/`, state dir /tmp/l1) because `--stage-roms` on the default instance refuses while the user's `sdmc:/3DGBA/gameA.*` exist and I must not remove them. The files `relief.bin` were copied into the default instance's `voxel/BPRE` and `voxel/BPGE` too (neither held one before; `BPRE/buildings.bin` etc. untouched); the instance-l tree has its own copies.
- `evidence/l1-fr-route1-ledges.png`: Route 1 FR, the ledge rows run across the path with the sandy gap; the ledge strips read as raised ridges with a smooth top edge, no floating or sunken cells, signs/fence at its end sit on the ground.
- `evidence/l1-fr-route1-ledge-with-vs-without-relief.png`: same view with relief.bin (top) and without it (bottom): without, the ledge is a flat jagged band; with, it has a soft raised ridge profile and the neighbouring ground does not move.
- `evidence/l1-lg-route1-ledges.png`: LG, same place, same look (`VOXEL relief: 31 layouts` in voxel.log).
- `evidence/l1-fr-route1-south.png`: Route 1's south end by the Pallet houses (the first, non-ledge view): trees and grass unchanged by the relief file.
- Not done: the hop. A DOWN-hold movie (`HOP.ctm`) over 5 minutes never moved the player (CTM timing, as the R2 entry noted), so no frame of the player crossing a ledge. The ledge cells keep their `0x3B` behaviour, so the game's own hop is unchanged; only the visual ridge was verified.
- Cleanup: `azctl stop` ran, instance l idle. The default instance's sdmc (gameA.gba/.sav, settings.bin, recent.bin) was not modified; hashes before: gameA.gba dd5945db, gameA.sav a40e4025, settings.bin 762ee064, recent.bin dcf15e18 (backup in the session scratchpad `l1bak/`).

## K3 Viridian City, Route 2, Viridian Forest gates (2026-10-06), done
- Census owned (before: covered 39 / 152): Viridian 3/1 L79: (25,11)->5/0 seed 23,9,6,4 and (25,18)->5/2 seed 23,16,6,3 (two houses; the Center/Gym/Mart are K2's). Route 2 3/20 L90: (5,13;6,13)->15/3, (5,51;6,51)->15/0 (the two Forest gate halves), (17,22)->15/1, (18,46;18,41;19,41;19,46)->15/2 seed 17,39,4,8. Viridian Forest 1/0 L117: (29,62;28,62;30,62)->15/0 and (5,9;6,9;4,9)->15/3 (the other halves of the same gates).
- k_viridian_house banked: rect (24,8,5,4) 80x64 on L79. Gabled shingle roof as a 5-point Prism (fascia + front slope PROJ rows, back slope a Strip), body Prism with a PROJ facade, chimney Prism (front rows 22-40, top rows 8-22). The roof is cut in three x-slices so the slice under the chimney takes a clean wrapped Strip (otherwise the painted chimney ghosts onto the slope). check PASS ortho 0/0/0 on 4 exact rects (back slope rows 8-19 not pinned: the back slope is a Strip, PROJ on a back-facing slope fails the density gate), density empty, round trip ok. placement (24,8) only. viewed: gable roof, ridge, chimney and facade read right from fl, fr and top.
- k_viridian_house2 (same builder, arg0 = 1 adds two thin PROJ planes for the flower boxes) banked: rect (24,15,5,5) 80x80. check PASS ortho 0/0/0 on 7 exact rects, density empty, round trip ok. placement (24,15) only. viewed: as the house, plus the flower boxes read as flat cards at the front.
- k_route2_house banked: rect (14,20,5,3) 80x48 on L90 (FNV 5E505C50; ground list {0x010, 0x011} = the two grass metatiles of this layout; `0x001` there is not grass and broke the first attempt). Hip roof (rg_hip, K1 shape) with a very steep modelled pitch (59.6 deg): the hip builder centres the ridge and the art's back eave row (6) forces a shallow depth; ortho exactness does not depend on pitch. Facade a PROJ prism (the facade rows must be rect rows 32-48, i.e. front z 48 not 64: first run failed 1142 wrong until fixed). check PASS ortho 0/0/0 on 2 exact rects (facade (1,32,80,48); slope + eave (14,1,62,32); the hip-end faces and the 1-2 px stripe ends at columns 12-13 / 62-65 are not pinned, the K1 precedent), density empty, round trip ok. placement (14,20) only. viewed: blue hip roof with stripes, window, door and pilasters read right from fl, fr and top.
- k_route2_gate (the Route 2 east building, dest 15/2: 4 doors, a walk-through gatehouse) banked: rect (16,41,6,7) 96x112 on L90. A flat block as a body Prism (front z 96: cornice+facade rows 59-96, roof top rows 16-59 on a level face), a canopy Prism (front z 107, y 20-30), two pillar planes, level mat/porch-floor planes at y=0 (rows 0-16 and 96-112) and 8 thin PROJ planes for the log posts; all faces PROJ with depth z = art row + y so the ortho is exact. check PASS ortho 0/0/0 on 5 exact rects (everything incl. posts), density empty, round trip ok. placement (16,41) only. viewed: grey slatted roof, brick facade, canopy over the door, yellow mats and posts read right from fl, fr, top (posts and mats are flat cards).
- k_route2_gate_s (the Route 2 south gate half, dest 3/20 at (4,49)) banked: rect (2,45,8,7) 128x112 on L90. Body Prism (front z 104, y 0-45, back 45: cornice+facade rows 59-104, roof top rows 0-59), canopy Prism (front z 124, y 20-32) between two pillar planes (rows 84-112), side column planes rows 104-112 and a level apron face at y=0. check PASS ortho 0/0/0 on 1 exact rect (0,0,128,112), density empty, round trip ok. placement (2,45) only. viewed: grey slatted roof, window band, brick wall, canopy over the dark doorway between pillars, reads right from fl, fr, top.
- k_route2_gate_n (the Route 2 north gate half, dest 3/20 at (4,11)) banked: rect (2,13,8,6) 128x96 on L90, stopping above the tree row that hides the lower brick wall. Body Prism (front z 96, y 0-21, back 37: cornice/window/brick rows 75-96, roof top rows 16-75) plus a thin lip plane over the north door (rows 12-16). check PASS ortho 0/0/0 on 2 exact rects, density empty, round trip ok. placement (2,13) only. Limitation: the log posts and path stay flat ground; the brick under the windows is cut by the rect. viewed: flat slatted roof with the door lip and window band reads right from fl, fr, top.
- k_forest_gate_n (Viridian Forest north gate half, dest 1/0 at (3,7)) banked: layout 117 (FNV pin 1DED0623, FR = LG), rect (0,4,11,6) 176x96. Body Prism (front z 88, y 0-44, back 44: cornice/window/brick rows 44-88, roof top rows 0-44), canopy Prism (front z 116, y 28-41, rows 67-88) and two pillar planes. check PASS ortho 0/0/0 on 1 exact rect (0,0,176,88), density empty, round trip ok. placement (0,4) only. Limitation: the bushes on rows 88-96 stay flat ground. viewed: flat slatted roof, window band, brick wall, canopy over the dark doorway between pillars.
- k_forest_gate_s (Viridian Forest south gate half, dest 1/0 at (28,60)) banked: layout 117, rect (24,62,10,7) 160x112, only the roof is visible (the building continues off the map). A level slab (y 45, rows 16-112) plus the raised lip plane over the door (rows 12-16). check PASS ortho 0/0/0 on 2 exact rects, density empty, round trip ok. placement (24,62) only. viewed: a flat slatted roof slab with the door lip along its back edge, reads right from fl, fr, top.
- K3 close: 8 models, 8 placements (one each); census covered 39/152 -> 47/152. New FR = LG buildings.bin SHA-1 27844bb42020d5fe5510da1e120641946b004ca0 (replaces dec0c711992d4b9186249f298e4f10bac9d7393f; 15 models, 49 placements, 1255472 bytes). Emerald gate identical (buildings 2929c764, regions 007a370f, signposts 38515605, relief 21a837f0, ledges relief eb25a383); FRLG regions 3716874d, signposts ba2fde45 unchanged. romgen test + vtest 0 failures 0 skipped; device make -j8 builds. Azahar (private instance k, FR, warp-save copies): k3-viridian-houses.png (houses at (24,8)/(24,15) with chimneys, flower boxes), k3-viridian-south.png (Center and Mart still right), k3-route2.png (Route 2 house and the gatehouse). Limitations: gate halves leave the neighbouring tree/bush cells and log posts flat; the south Forest half is a roof slab only.


## K4 Pewter City (2026-10-06), in progress
- Census owned (before: covered 47 / 152): Pewter 3/2 L80 (FNV AAB0C96C, FR = LG): (17,6;25,4)->6/0 seed 12,0,16,7 (the Museum, two doors), (33,11)->6/4 seed 32,9,5,3 and (9,30)->6/7 seed 8,28,5,3 (the same signature 957ACFDF: one house model, two placements). The Gym (6/2), Mart (6/3) and Center (6/5) are K2's and already covered (`k_gym_7`, `k_mart`, `k_center`).
- k_pewter_house banked: rect (32,8,5,4) 80x64 on L80 (the census seed is 5x3; the ridge cap starts 8 px into the cell row above it, so the rect is taken one row higher; both placements have identical metatiles in all four rows). Body Prism (front z 64, y 0-16: facade rows 48-63) and a 5-point roof Prism (fascia rows 42-47, front slope rows 22-41 on a 33.7 deg face, a level ridge cap rows 9-21 as a PROJ top face; no back slope PROJ, so the density gate is clean). check PASS ortho 0/0/0 on 2 exact rects ((0,9,80,48) roof, (0,48,80,64) facade), density empty, round trip ok. placements (32,8) and (8,27), both on L80 / map 3/2. viewed: slate gable roof with the white ridge cap, door and window read right from fl, fr and top.
- k_pewter_museum banked: rect (12,0,16,7) 256x112 on L80. Three stepped-profile Prisms, every edge PROJ: the hall (x 0-175: facade rows 72-95, awning 55-71, cornice 48-54, slope 24-47, level cap 9-23), the right wing (x 176-255: facade 62-79, fascia 58-61, slope 32-57, cap 17-31) and the entrance porch (x 64-111, a low ramp roof rows 76-95 at z 98-112 so the hall wall does not hide it, arches and door rows 96-111). The two hedges (8 bush cells) are 8 one-pixel-deep cards (rows 96-111) with a 1-px PROJ top face: without a top face the consumer round trip failed ("the placement's cell does not resolve", rg_cell_heights ignores vertical faces and the bottom-middle cell (20,6) was a bush). check PASS ortho 0/0/0 on 5 exact rects, density empty, round trip ok. placement (12,0) only. viewed: pink roof with cornice windows and awning, the porch with its arched door and the lower wing with the red door read right from fl, fr and top; the hedge reads as flat bush cards.
- K4 close (2026-10-06): 2 models (k_pewter_house, k_pewter_museum), 3 placements, covered 47 -> 50 / 152. buildings.bin FR = LG pin beca820c9f1d5d7ae9d4fb5770e69ccf270a3183. Gate: Emerald buildings 2929c764, regions 007a370f, signposts 38515605, relief 21a837f0 (ledges eb25a383) unchanged; FRLG regions 3716874d, signposts ba2fde45, relief 32c24146 unchanged. `make test` 27 suites 0 failures (frlg_buildings 2689 checks); `vtest` 0 failures, 0 skipped; device make builds. Evidence: k4-pewter-museum.png (Azahar, museum visible; gym menu overlay open), k4-preview-pewter-house.png, k4-preview-pewter-museum.png. Limitations: hedge bushes are cards, the museum top is cropped by the camera in the shot, gate/rock corners not applicable.

## K5 Cerulean City, Route 4, Route 25 (2026-10-06)
- Census before: 50 / 152 (3/3 L81, 3/22 Route 4, 3/44 L113). Route 4 holds only the Center (K2); Cerulean's Center, Gym, Mart are K2's.
- k_cerulean_house_a..e (L81 pin B952CCF4, rects (8,8,7,4) (15,8,6,4) (28,8,7,4) (13,14,7,4) (21,25,6,4)): one blue-roof profile builder (body y 0-24, slate slope to a level ridge at y 40; rows facade 43-64, fascia 40-43, roof 8-40). check: ortho 0/0/0 on all three exact rects, density empty, 1 placement, round trip ok. Previews viewed: facade, door, green planters, window and slate roof read correctly; small silhouette artefacts at the left/right roof ends accepted.
- k_cerulean_bike (rect (12,23,4,6), 64x96): flat glass roof block (level top y 38, wall rows 58-96). check PASS (ortho 0, density empty, round trip ok). Preview viewed: checkered glass roof, three windows and striped awning read correctly.
- k_route25_cottage (L113 pin BBAC050A, rect (49,1,5,4), 80x64): hip roof cut into nine x-slices so the diagonal hip ends follow the art; chimney painted flat on the roof. check PASS. Preview viewed: green hip roof, eave band, pillars and door correct; hip ends are a slight staircase.
- Covered 50 -> 57 / 152 (7 models, 7 placements). buildings.bin FR = LG SHA-1 0b7fb62f971284d253557b16a776db90e29e0ab9. Gate: Emerald buildings 2929c764, regions 007a370f, signposts 38515605, relief 21a837f0 (ledges eb25a383) unchanged; FRLG regions 3716874d, signposts ba2fde45, relief 32c24146 unchanged.
- Evidence: k5-cerulean.png (Azahar, instance k: blue house, Center, Bike Shop glass roof visible), k5-preview-cerulean-{house_a..e,bike}.png, k5-preview-route25-cottage.png.

## K6 Vermilion City, Routes 5-8 (2026-10-06), in progress
- Census owned (before: covered 57 / 152): Vermilion 3/5 L83: (9,6)->9/0 seed 8,3,5,4 (Fan Club), (12,17)->9/3 seed 11,15,5,3, (19,17)->9/4 and (28,24)->9/7 (same signature 05C0CF11 = one house model, two placements). (22-24,34)->1/4 is the S.S. Anne gangway, excluded. Center, Mart, Gym are K2's (covered). Routes 3/23-3/26 (L93-L96): each has the Underground Path hut (->1/30, 1/32, 1/33, 1/35, signature E41CF127 on all four), plus Route 5 (23,25)->17/0 Day Care seed 21,22,5,4 and (24,32;25,32)->17/1 seed 23,30,4,3, Route 6 (12,5;13,5)->18/0 seed 11,3,4,3, Route 7 (15,10)->19/0 seed 14,8,3,3, Route 8 (7,10)->20/0 seed 6,8,3,3. Own placements: 4 + 4 + 5 = 13.
- k_vermilion_fanclub banked: rect (8,3,5,4) 80x64 on L83 (FNV 82FF5FAD, FR = LG). Profile prism (facade rows 43-64, fascia 40-43, scalloped roof slope 8-40). check PASS ortho 0/0/0 on 1 exact rect (0,12,80,64) (rows 8-11 are the roof edge against the rock, not pinned), density empty, round trip ok. placement (8,3) only. viewed: corrugated yellow roof over the facade with door, window and planters reads right from fl, fr and top.
- k_vermilion_house banked (same builder as the Fan Club, arg0 = width 64): rect (18,14,4,4) 64x64 on L83 with matchRows (1,4) (the census seed is 18,15,4,3; the roof edge starts 10 px into the row above, and that top row differs between the two placements, so only rows 1-4 are matched). check PASS ortho 0/0/0 on 1 exact rect (0,12,64,64), density empty, round trip ok. placements (18,14) and (27,21), both L83 / map 3/5. viewed: yellow corrugated roof over the facade with door, window and planter reads right from fl, fr and top.
- k_vermilion_green banked: rect (11,14,5,4) 80x64 on L83 (census seed 11,15,5,3; the roof is the row above). Profile prism: green slope rows 0-34, fascia with rivets 34-40, facade with arched emblem, door, window and planters 40-64. check PASS ortho 0/0/0 on 1 exact rect (0,0,80,64), density empty, round trip ok. placement (11,14) only. viewed: green striped roof, rivet fascia, arched emblem over the door and the planters read right from fl, fr and top.
- k_path_hut (the Underground Path hut) banked: rect (30,28,3,4) 48x64 on Route 5 L93 (FNV A0C68725, FR = LG), matchRows (1,4) because the roof starts in the row above the census seed (30,29,3,3). Flat block as a profile prism (facade rows 42-64, cornice 35-42, level roof face rows 0-35). check PASS ortho 0/0/0 on 1 exact rect (0,0,48,64), density empty, round trip ok. placements 4 = the whole reuse: L93 (30,28) map 3/23, L94 (18,10) map 3/24, L95 (6,11) map 3/25, L96 (12,1) map 3/26 (exact-match reuse across Routes 5-8, all four owned by this slice). viewed: grey slatted flat roof, cornice, brick facade with pillars and the wooden door read right from fl, fr and top.
- k_daycare (Route 5, dest 17/0) banked: rect (21,21,5,5) 80x80 on L93 (census seed 21,22,5,4; the roof top row is the row above). Orange hip roof cut into 13 x-slices (2-px diagonal steps, top row 14 at the corners to row 8 along the ridge), eave band 40-43, cream facade 43-80; the same x-slice technique as the Route 25 cottage. check PASS ortho 0/0/0 on 13 exact rects, density empty, round trip ok. placement (21,21) only. viewed: orange slatted hip roof, eave, two windows and the door read right from fl, fr and top; hip ends are a slight staircase.
- k_route5_gate (Route 5 south gate half, dest 17/1, doors (24,32),(25,32)) banked: rect (22,32,6,7) 96x112 on L93 (census seed 23,30,4,3 is only the door approach; the building is the 6x7 block below it). The Route 2 south-half shape at 96 px: body Prism (front z 104, y 0-45, back 61: cornice/windows/brick rows 59-104, level roof top 16-59), canopy Prism (front z 124, top 84-91, front 92-103) between two white pillar planes, side columns and a level apron (rows 104-111), the raised lip over the door as a plane (rows 12-16). check PASS ortho 0/0/0 on 2 exact rects ((0,16,96,112) and the lip (22,12,74,16)), density empty, round trip ok. placement (22,32) only. Limitation: the log posts, the sand path and the porch floor of the bottom map row (rows 112-128, cell row 39) stay flat ground. viewed: flat slatted roof, window band, brick wall, canopy between pillars and the apron read right from fl, fr and top.
- k_route6_gate (Route 6 north gate half, dest 18/0, doors (12,5),(13,5)) banked: the same builder as k_route5_gate (`k_gate_half`, arg0 = row shift 16 because the map top cuts the roof, no lip; arg1 = x shift 16). L94 (FNV D03FD324, FR = LG), rect (9,0,8,6) 128x96: one tree column each side. Reason: the plain 6x6 gate cells (rect (10,0,6,6)) also match Route 5's gate (L93 (22,33), overlapping k_route5_gate, and no overlap handling exists) and two Saffron gate cells (L207 (32,0), (32,47), map 3/10, K10's); a 6x8 rect (adding the sand path rows) matched only Route 6 but failed the round trip (the unmodelled path cells do not resolve). The tree columns make the cell contents unique, and the trees are not modelled (T1 owns them). check PASS ortho 0/0/0 on 1 exact rect (16,0,112,96), density empty, round trip ok. placement (9,0) only (L94 / map 3/24). viewed: flat slatted roof, window band, brick wall, canopy between pillars and the apron read right from fl, fr and top; Route 5 spec unchanged (PASS, 1 placement).
- k_route7_gate (Route 7 gatehouse, dest 19/0, west door (15,10)) banked: rect (15,7,8,5) 128x80 on L95 (FNV 4C1E067E, FR = LG; census seed 14,8,3,3 is the west approach only). A body Prism (x 16-112, front z 80, y 0-37, back 37: cornice/windows/brick rows 43-80, level roof top rows 0-43). check PASS ortho 0/0/0 on 1 exact rect (16,0,112,80), density empty, round trip ok. placements 2: L95 (15,7) map 3/25 and L207 (1,24) map 3/10 (Saffron: the same gatehouse from its east end; a deliberate reuse on a layout this slice does not own, K10 gets it for free, the census will count that Saffron row as covered). Limitation: the two side porches (x 0-16 and 112-128: awnings and the arrow-mat doors, drawn side-on) stay flat ground. viewed: flat slatted roof, cornice, window band, brick wall and the white end pillars read right from fl, fr and top.
- k_route8_gate (Route 8 gatehouse, dest 20/0, door (7,10)) banked: the Route 7 builder (`k_route7_gate`), rect (0,7,8,5) 128x80 on L96 (FNV 87638ADF, FR = LG). A separate row because the top corner cell of the rect (fence posts here, grass on Route 7) differs, so the Route 7 rect does not match. check PASS ortho 0/0/0 on 1 exact rect (16,0,112,80), density empty, round trip ok. placements 3: L96 (0,7) map 3/26, L103 (9,8) map 3/33 (Route 15's west gatehouse) and L207 (58,24) map 3/10 (Saffron's west-facing gate); the last two are deliberate reuse on layouts this slice does not own (the same cells, so the same art; K9 and K10 get them for free). Same limitation as Route 7 (side porches stay ground). viewed: identical read to Route 7 (flat slatted roof, cornice, window band, brick, white end pillars).
- k_path_hut corrected: the hut is 4 cells wide (x 30-33), not the census seed's 3, so the first banked rect (30,28,3,4) lost the east pillar. Now rect (30,28,4,4) 64x64, exact (0,0,64,64), still matchRows (1,4); check PASS ortho 0/0/0, density empty, round trip ok, placements still the same 4. viewed again: both white end pillars present, door, brick and slatted roof read right.
- K6 closing: census covered 57 -> 73 / 152 (13 own placements + 3 reuse placements on Saffron L207 x2 and Route 15 L103). FR = LG buildings.bin SHA-1 90a94764a477945f9c6ef47611bcf42ebc43a142 (old pin 0b7fb62f). Gate: FRLG regions 3716874d, signposts ba2fde45, relief 32c24146 unchanged; Emerald buildings 2929c764, regions 007a370f, signposts 38515605, relief 21a837f0, ledges relief eb25a383 identical. `make -C tools/romgen test`: 27 suites, 0 failures, 0 skipped (test_romgen_frlg_buildings 2951 checks); `vtest`: 9 suites, 0 failures, 0 skipped; device `make -j8` builds. Azahar (private instance k, warp-save copies, voxel on, new movie MK.ctm without the trailing START/B so no menu overlay): `evidence/k6-vermilion.png` shows Vermilion at (12,9) in 3D, an orange-roofed house with flower boxes beside the Pokemon Center, no menu open (it does not isolate which K6 model is which; the previews do that); `evidence/k6-route.png` shows Route 5 at (31,26) in 3D, the Underground Path hut (slatted grey roof, brick, door) in front and the Day Care at the left. Not passed: Route 5 at (31,33) rendered plain 2D (letterboxed GBA frame, same at 90 s and 140 s) while (31,20) and (31,26) render 3D; cause not investigated (possibly a gate hand-back next to the hut), nothing in K6 data changes at that cell.

## VRAM atlas fallback (2026-10-06)
Resolves K6's "Route 5 at (31,33) rendered plain 2D". Renderer only: no romgen data changes.

- Cause: VRAM held room for exactly one 512x256 RGBA5551 atlas (256 KiB). The 6 MiB (6291456 B) is taken up by:
  screen targets 1612800 (top L and top R 400x240 RGBA8 + DEPTH16 at 576000 each, bottom 320x240 at 460800);
  sharp-bilinear prescale target `preTex` 512x512 RGBA8 1048576 (main.c:2251); the logical surface 512x256
  RGBA8 + DEPTH16 786432 and bloom 128x64 RGB565 16384 (vx_host.c:255-261); the mesh arena 1572864
  (`VOXEL_CHUNK_VRAM_BUDGET`) and the building-page arena 786432 (`VOXEL_PAGE_VRAM_BUDGET`). Sum 5823488, which
  leaves exactly the logged `VRAM free=467968`. One atlas leaves 205824. At (31,33) Route 5 (3:23) and the Saffron
  connection map (3:11, a different tileset pair) are both on screen; the second never got an atlas, its chunks
  stayed missing and the gate fell back to 2D. The LRU did the right thing: both atlases were in view, so neither
  could be evicted. `CtrVideo_RequestPlaneRelease` is a no-op in this build, so nothing ever freed VRAM.
- Fix: `AllocateAtlasPage` (ctr_voxel.c) tries VRAM first, then the linear heap (`C3D_TexInit`, which PICA200 can
  sample, with less bandwidth). The upload stays the same GPU TextureCopy from the linear staging buffer, on the
  render thread inside the frame, and it still allocates only when an atlas is built. The policy is the pure
  `vx_atlas_mem_allowed` (ctr_shims_pure.c): it keeps the 6-texture cap (at most 1.5 MiB of linear) and a 2 MiB
  linear reserve. The atlas log line now says `mem=` with V or L per page, and each linear placement is logged.
  The mesh budget was not reduced: "Rustboro's view alone draws 54K" vertices of the 96K, so shrinking it trades this
  bug for missing chunks in cities.
- Before (instance v, FR warp save firered-r5s, Route 5 (31,33)):
  ```
  VOXEL atlas for 3:23: 341/512 slots (rebuilds=1, linear free=14043136)
  VOXEL: atlas cache capped at 1 (VRAM free=205824)
  VOX: atlas VRAM blocked; no compositor planes to release
  VOXEL: no VRAM for a 512x256 atlas (free=205824); asking for the depth planes back
  VOXEL stream frame=120 missing=2 ... (every 120 frames through frame=720, missing=2)
  ```
- After (same save, same movie):
  ```
  VOXEL atlas for 3:23: 341/512 slots mem=V (rebuilds=1, VRAM free=205824, linear free=14043136)
  VOXEL atlas page 0 in linear memory (VRAM free=205824, linear free=13780992)
  VOXEL atlas for 3:11: 267/512 slots mem=L (rebuilds=2, VRAM free=205824, linear free=13780992)
  VOXEL atlas page 0 in linear memory (VRAM free=205824, linear free=13518848)
  VOXEL atlas for 3:26: 93/512 slots mem=L (rebuilds=3, VRAM free=205824, linear free=13518848)
  ```
  No `VOXEL stream frame=` line at all. It is printed only while `missing != 0` (ctr_voxel.c, the
  `lastMissingLog` block), so missing was 0 at every check.
- Emerald Route 104 (emerald-r104) after the fix: `atlas for 0:19 ... mem=V`, `atlas for 0:0 ... mem=L` (Petalburg,
  the neighbour, which before this fix could not have had an atlas either), no stream lines.
- Evidence: `evidence/vram-before-r5s.png` shows Route 5 (31,33) as a letterboxed 2D GBA frame (before);
  `evidence/vram-after-r5s.png` shows the same spot in 3D, with the Route 5 gate house and trees and the textured
  Saffron-side building at the lower left; `evidence/vram-after-emerald-r104.png` shows Emerald Route 104 (Briney's
  cottage, the pier, the sea) in 3D.
- Gate: `make -C tools/romgen test` 27 suites, 0 failures (romgen data untouched by this change); `vtest` 9 suites,
  0 failures (test_voxel_shims 69 -> 79 checks: `TestAtlasMem`); device `make -j8` builds with no warnings in
  source/voxel/.
- UNPROVEN until a New 3DS run: frame time with an atlas in linear (FCRAM) memory. Azahar does not model PICA
  texture fetch bandwidth. The current map's atlas usually lands in VRAM (it is built first), and the neighbour's
  pair is the one in linear. However, an atlas does not move back to VRAM when the player crosses into the neighbour,
  so after a crossing the current map can be the one sampled from FCRAM. On hardware, check the fps at a town edge
  and after walking into the neighbour. If it is slow, the next step is migrating the current map's atlas into VRAM
  (evict or swap the off-view one). Shrinking `preTex` (1 MiB) is not free: it is the render target of the
  sharp-bilinear pass for every 2D GBA frame, including the bottom screen while the top screen is in voxel.
## K7 Lavender Town, Route 10 (2026-10-06), in progress
- Census owned (before: covered 73 / 152): Lavender 3/4 L82 (mapsec 0x5C, 6 placements): (18,6)->1/88 the Pokemon Tower (seed 15,0,7,7), (10,11)->8/2 a house (seed 8,9,5,3), (5,16)->8/3 and (10,16)->8/4 two houses side by side (same seed 3,14,10,3, same signature B1641368), Center (6,5) and Mart (20,15) are K2's (covered). Route 10 3/28 L98 (mapsec 0x6E, 2 placements): (7,40;2,37)->1/95 the Power Plant (seed 2,34,11,7), Center (13,20) K2's. Own placements: Tower 1 + house 3 + Power Plant 1 = 5.
- k_lavender_house banked: rect (8,8,5,5) 80x80 on L82 (FNV B6187344, FR = LG), matchRows (1,4) = cell rows 9-11 (the census seed; the roof starts 10 px into row 8, the facade ends 4 px into row 12). One purple-roofed house shape: all three Lavender houses are the same cells, so one row, 3 placements: (8,8) and the side-by-side pair (3,13), (8,13). Profile prism (facade rows 47-68, eave 45-47, lilac roof slope 10-45). check PASS ortho 0/0/0 on 1 exact rect (0,10,80,68), density empty, round trip ok. placements 3 (all map 3/4). viewed: lilac roof over the facade with two windows, the door and yellow trim reads right from fl, fr and top.
- k_power_plant banked: rect (2,34,11,8) 176x128 on Route 10 L98 (FNV EB232F5A, FR = LG); the 8th cell row holds the bottom 8 px of the facade (census seed is 11x7). Flat-roofed hall x 16-160 with chamfered corners: per x-range one profile prism (wall 37 rows, 12-row bevelled rim, pink roof rows 26-.., parapet face rows 16-26 behind it; the front depth is the wall's bottom row, so the centre block stands 8 rows forward of the wings), the four chamfers as 2-px slices following the diagonal, four turbine hoods as boxes on the roof (front rows 26-54, the dark opening on top rows 12-26). check PASS ortho 0/0/0 on 11 exact rects (wings, centre, four hood openings, the four chamfers above their diagonals), density empty, round trip ok. placement (2,34) only (L98, map 3/28). The two doors of the census row, (7,40) and (2,37), both fall inside the rect (census now shows k_power_plant on that row). Limitation: the chamfers' diagonal bottom edges and the two pylons beside the hall stay flat ground; the slices make the chamfers a slight staircase. viewed: the pink roof with four red-striped turbine hoods, the yellow rim, louvred wings and the glass centre block with its entrance read right from fl, fr and top.
- k_pokemon_tower banked (the showpiece): rect (14,0,9,7) 144x112 on Lavender L82 (FNV B6187344, FR = LG; the 7 cell rows, x 14-22, take the rock on both sides; the map top cuts the tower). A stone platform (front wall rows 81-112, 31 high; top face rows 55-81 in front, two rim strips beside it) carries a tall block with a chamfered plan: the front face x 39-105 (window bays, pilasters, plinth, rows 0-71) is one vertical plane at z 102, the two chamfers x 23-39 and 105-121 are 2-px slices whose bottom edge is the 45-degree diagonal (row = x + 31, mirror about x 72), the door is a shallow box (flat top rows 90-95 over the front face 95-111). Above the art (the art stops at the map top) the tower is invented to read tall: the block is capped at y 102 and two setback tiers (x 48-96 to y 128, x 58-86 to y 150) are textured with the olive spandrel band (art rows 8-9, PROJ clamped), so they show as fluted stripes. check PASS ortho 0/0/0 on 7 exact rects (front face, both chamfers above their diagonals, platform front wall, platform top, both rim strips), density empty, round trip ok. placement (14,0) only (map 3/4). viewed: from fl, fr and top it reads as a stepped, tiered tower with four window columns on a stone platform with a door, not a box; the chamfer slices are a slight staircase and the back is open (never seen from the camera).
- Side walls (lead note from Guy: the K5/K6 x-slice models had no side walls in-game because every slice used ends=false): every K7 model now closes its outermost west and east faces. The Prism end faces are drawn only through a cap band, so `lv_profile` takes LV_W / LV_E flags and adds a cap tile (a plain patch of the model's own wall art) whenever a flag is set; sliced parts pass LV_W on the leftmost slice and LV_E on the rightmost, inner slices 0. House (one prism, LV_WE), Power Plant (outer chamfer slice pair W/E, each hood LV_WE, the centre block and inner slices open), Tower (platform LV_WE, rim_w LV_W, rim_e LV_E, tier2/tier3 LV_WE, outer chamfer pair W/E). The ortho check cannot see this (the faces are edge-on from the front), so it was checked by eye per model: house fl/fr, Power Plant fl/fr and Tower fl/fr show a solid wall on both ends, and in the Azahar shots the purple houses left of the camera show their side wall as a solid lilac face.
- K7 closing: census covered 73 -> 78 / 152 (5 own placements: Tower 1, Power Plant 1, purple-roofed house 3 on L82). FR = LG buildings.bin SHA-1 d4038f346a9692569279f695d9e6b14a6e432ec7 (old pin 90a94764; 36 models, 80 placements). Gate: FRLG regions 3716874d, signposts ba2fde45, relief 32c24146 unchanged; Emerald buildings 2929c764, regions 007a370f, signposts 38515605, relief 21a837f0, ledges relief eb25a383 identical (re-hashed with the CLI). `make -C tools/romgen test`: 27 suites, 0 failures, 0 skipped (test_romgen_frlg_buildings 3001 checks); `vtest`: 9 suites, 0 failures, 0 skipped; device `make -j8` builds. Azahar (private instance k, EMUTEST_STATE_DIR=/tmp/k7, warp-save copy firered-k7lav at 3/4 (18,9), voxel on via settings.bin offset 0x68, FR voxel files in the instance's voxel/BPRE): `evidence/k7-lavender.png` shows the Tower from its door in 3D: the stone platform with the dark door and rim strips and the chamfered body rising out of the top of the frame, with the purple house at left showing a solid lilac side wall. `evidence/k7-lavender-tower.png` (warp 17,12) shows the Poke Mart, two purple houses on the left with solid side walls and the Tower's platform at the top edge. Limitation: the camera does not frame the Tower's full height in 3D at ground level, the setback tiers are cropped by the top edge; the previews (`k7-preview-*-fl|fr.png`) show the whole shape. The Tower's back and the pylons beside the Power Plant stay open or flat (never seen from the camera).
## Side walls (2026-10-07)
- Report (Guy): the K6 Day Care showed no east wall in Azahar; the K5 Route 25 cottage was built the same way. Cause: both are x-slice models (`vm_profile` / the K5 equivalent, `ends=false`), and a Prism draws its end faces (x0 / x1) ONLY through a cap band. **The cap rule** (rg_geom.c `emit_prism`): an end face is emitted when `hasCaps` is true AND `west` (x0) / `east` (x1) is set for that side; each `RgBand` [y0,y1] clips the triangulated (z,y) polygon and `emit_cap_piece` tiles `band.tile` on the plane x = const (flipped on the west side), `band.z0` must be >= the polygon's max z. `west/east = true` alone draws nothing; with caps and no flag nothing is drawn either. K7's `lv_profile` already added a cap tile whenever an end flag is set. Cap triangles are edge-on in the ortho view, so the ortho check and the density gate can never see a missing or present side wall.
- New gate, `romgen author ... check` (FireRed / LeafGreen only; Emerald is pinned bytes and never runs it): per model two lines, `side west: N of M wall cells open (tolerance T)` and the same for east, `FAIL: open side` when N > T. Implementation `rg_side_check` (rg_bcheck.c): project every emitted triangle along x onto the (z,y) plane (a unit-cell coverage grid, edge-on triangles skipped, depth/behind-tagged triangles ignored); the expected cells are the section of the model that is visible from the side, from `rg_prism_exposed` (rg_geom.c: a prism cell is exposed on a side when no prism that reaches further out on that side contains the cell; cells within 0.75 px of the outline are eroded; sheets, z-span or y-span under 4, are exempt) plus, for HIPROOF / FRUSTUM parts within 2.5 px of the model's edge, everything under the part's upper outline. A cell is open when no triangle covers it. Tolerance `max(8, expected / 50)` cells (2 percent). `n/a` is printed when no prism is at the edge. Inner slices are NOT required to be closed: the exposure rule only counts the steps of a slice that nothing further out hides.
- Fix: `rg_close_sides` (rg_bspecs.c), run after `s->parts()` and before emit (rg_buildings.c), driven by `RgSideCfg` rows carried in `RgSpec.ext` (unused by DIRECT specs). Per entry: a part-name prefix (NULL = any prism), a uniform wall rect and roof rect (art coordinates, verified to be one flat colour by script), and the eave y. Every cap-less prism that is exposed on a side gets a cap band: the wall tile below the eave, the roof tile above. Builders are unchanged; the end tile is a plain patch of the model's own art (wall colour, roof colour), the same precedent as K7's `sCap`. Orthographic exact rects stay 0/0/0 on every model.
- Audit, before -> after (36 Kanto models; `check all`): 27 failed before, 0 after. K1 / K2 (pallet house, lab, Center, Mart, gym, gym_7, gym_8): PASS before (existing caps or hip / frustum), unchanged. K3: viridian_house, viridian_house2, route2_house, route2_gate, route2_gate_s, route2_gate_n, forest_gate_n FAIL -> fixed. K4: pewter_house, pewter_museum FAIL -> fixed. K5: cerulean_house_a..e, cerulean_bike, route25_cottage FAIL -> fixed. K6: vermilion_fanclub, vermilion_house, vermilion_green, path_hut, daycare, route5/6/7/8_gate FAIL -> fixed. K7: pokemon_tower (552 of 7403 open: the chamfer and body steps), power_plant (82 of 2017) FAIL -> fixed; lavender_house PASS. forest_gate_s: `n/a` (a roof slab only, no wall is modelled; viewed, left as is, nothing to close).
- Pin: FR = LG buildings.bin SHA-1 d4038f346a9692569279f695d9e6b14a6e432ec7 -> 8b452134afdba2244129a5035939cca51e5754d5 (FR and LG byte-identical; test_romgen_frlg_buildings pin updated). Host tests (3001 -> 3157 checks): a synthetic open-sided prism FAILS the side check on both sides and the same box with caps passes (`TestSideCheck`); every Kanto spec's `check` text must contain both side lines and no `FAIL: open side`.
- Gate: Emerald buildings 2929c764, regions 007a370f, signposts 38515605, relief 21a837f0, `--relief ledges` eb25a383 unchanged; FRLG regions 3716874d, signposts ba2fde45, relief 32c24146 unchanged. `make -C tools/romgen test`: 27 suites, 0 failures, 0 skipped; `vtest`: 9 suites, 0 failures; device `make -j8` builds (the only warnings are old ones in celiolink.c / netlink.c / main.c).
- Evidence (fl + fr side by side, `evidence/sides-<spec>-before.png` and `-after.png` for all 24 changed spec previews): viewed. k_daycare: before, a roof and a wall with no end faces so you see into the gap; after, solid cream walls and a gold roof edge on both ends. k_route25_cottage: both ends closed, wall tan and the green roof tile above the eave. k_pokemon_tower and k_power_plant: the slab at each end of the chamfers is a flat green / blue-grey face, the steps are gone. k_pewter_museum, route gates: the ends are plain stone / grey faces. Azahar (private instance s, warp save at map 3/23 (31,26), voxel on via the default instance's settings.bin): `evidence/sides-daycare-azahar-before.png` (old buildings.bin d4038f34) shows the Day Care's east end open; `evidence/sides-daycare-azahar-after.png` (new 8b452134) shows the same view with the tan east wall.
- Left open: k_forest_gate_s (roof slab only, n/a). The wall tiles are flat colour patches, not the real side art (the ROM has no side view, so this is the same limit as K7's `sCap`). Hardware not run.
## K8 Celadon City (2026-10-07), in progress
- Census owned (before: covered 78 / 152), Celadon 3/6 L84 (FNV 6B8BA7E4, FR = LG), 9 placements: (34,21)->10/14 seed 33,19,3,3 (Game Corner), (39,20)->10/15 seed 38,18,3,3 (Prize Room), (11,14;15,14)->10/0 seed 4,5,19,10 (Dept. Store; the seed is a flood fill that also takes the two wings beside it), (30,11;29,5;30,4;31,5)->10/7 seed 27,4,12,8 (the Celadon Mansion = Condominiums, door row at the foot plus three roof doors), (37,29)->10/17, (41,29)->10/18, (49,29)->10/19 (same signature 7495FB70 = one house model, three placements), Center (48,11) and Gym (11,30) are K2's (covered).
- k_celadon_house banked: rect (36,25,4,5) 64x80, matchRows (1,5) (the roof-top hood in row 25 differs at the third placement). Flat block (`cl_block`: facade rows 42-80, level roof top 8-42). check PASS ortho 0/0/0 on 1 exact rect (0,8,64,80), density empty, side west 0 of 1152 open, side east 0 of 1152 open, round trip ok. placements 3: (36,25), (40,25), (48,25), all L84 / map 3/6. viewed: olive flat roof with its vent hood over the green two-storey facade with windows and the wooden door reads right from fl and fr, solid green end walls on both sides.
- k_celadon_game_corner banked: rect (31,17,7,6) 112x96 on L84 (census seed 33,19,3,3 is only the door approach; the building is the 7x6 block). One slab (facade rows 8-79, 71 high, 40 deep, the top a smear of the clear cream rows 26-27: the art has no roof) plus a porch box for the purple arched entrance (front face rows 56-86, dome top rows 44-56, x 39-73). check PASS ortho 0/0/0 on 2 exact rects ((0,8,112,79) and the porch (39,44,73,86)), density empty, side west 0 of 2790 open, side east 0 of 2790 open, round trip ok. placement (31,17) only. Side tiles: cream (2,24,110,32) for the hall, purple (40,61,48,64) for the porch. Limitation: the chamfered upper corners of the art stay as painted diagonals on a straight slab. viewed: a tall cream block with two rows of windows, orange diamond-relief base and the purple arch with its door and grey pillars in front reads right from fl and fr, solid cream end walls.
- k_celadon_prize banked: rect (38,17,3,4) 48x64 on L84 (census seed 38,18,3,3). Same slab builder (facade rows 8-63, 55 high, 30 deep). check PASS ortho 0/0/0 on 1 exact rect (0,8,48,63), density empty, side west 0 of 1484 open, side east 0 of 1484 open, round trip ok. placement (38,17) only. viewed: the small cream block with a window pair, tan cornice and the orange base with the double door reads right from fl and fr, solid cream end walls.
- k_celadon_dept banked (Dept. Store plus the two wings): rect (4,4,21,12) 336x192 on L84 (the census seed 4,5,19,10 is a flood fill of the three; this rect is 21 wide so it takes the whole east wing). The store is one flat block (`cl_block`, x 81-223, facade rows 78-181 = 103 high, roof rows 16-78) with the roof penthouse as a second prism (x 112-192, front rows 56-70, top rows 17-56, 14 above the roof, front depth 173); the wings are flat blocks 69 high (roof rows 8-59, facade 59-128) at x 0-80 and 224-336. check PASS ortho 0/0/0 on 4 exact rects ((81,16,223,181), (112,17,192,71), (0,8,80,128), (224,8,336,128)), density empty, side west 0 of 9280 open, side east 0 of 9280 open, round trip ok. placement (4,4) only; both store doors (11,14) and (15,14) fall inside. Side tiles: purple (128,82,176,87) store, grey (113,59,191,61) penthouse, green (3,60,5,121) wings. Limitation: the corner bevels and the pilaster recesses are painted, not modelled; the wing hoods are painted on the roof. viewed: a clear six-storey block with the grey louvred roof and the yellow-panelled penthouse between two lower green blocks reads right from fl and fr; solid purple / green end walls on the outer ends.
- k_celadon_mansion banked (Celadon Mansion = the Condominiums): rect (27,3,7,9) 112x144 on L84 (census seed 27,4,12,8 is a flood fill that also takes the plain blocks east of it; those have no door and are not modelled here). One flat block (`cl_block`, facade rows 75-144 = 69 high, olive roof rows 24-75, x 0-112). check PASS ortho 0/0/0 on 1 exact rect (0,24,112,144), density empty, side west 0 of 3283 open, side east 0 of 3283 open, round trip ok. placement (27,3) only; the door (30,11) and the three roof doors (29,5), (30,4), (31,5) are in the census row, which now shows the model. Side tile: pale green (3,76,5,136). viewed: a four-storey green block with the olive tiled roof and its vent hood, two purple pilaster strips and the wooden door reads right from fl and fr, solid green end walls on both sides.
- K8 closing: census covered 78 -> 85 / 152 (7 own placements: house 3, Game Corner, Prize Room, Dept. Store, Mansion; Center and Gym were K2's). FR = LG buildings.bin SHA-1 8b452134afdba2244129a5035939cca51e5754d5 -> ef4ad20a393edb33fc515b8967ce07ce8f362a3b (41 models, 87 placements, 5227864 bytes). Gate: Emerald buildings 2929c764, regions 007a370f, signposts 38515605, relief 21a837f0, `--relief ledges` eb25a383 identical; FRLG regions 3716874d, signposts ba2fde45, relief 32c24146 unchanged (FR and LG). `make -C tools/romgen test`: 27 suites, 0 failures, 0 skipped (test_romgen_frlg_buildings 3157 -> 3259 checks); `vtest`: 9 suites, 0 failures, 0 skipped; device `make -j8` builds. Note: the test file gates its Kanto block on `nk == 41`; a stale count silently skips ~700 checks (it printed 2449 checks, 0 failures until fixed), so bump both sites per slice. Azahar (private instance k, EMUTEST_STATE_DIR=/tmp/k8, warp-save copies at 3/6 (13,17) and (34,24), voxel on via the default settings.bin, FR voxel files in the instance's voxel/BPRE): `evidence/k8-celadon-dept.png` shows the Dept. Store as a tall multi-storey block in 3D with the two wings left and right showing solid sides; `evidence/k8-celadon-gamecorner.png` shows the Game Corner with its purple arch and the Prize Room beside it, and the green houses below in the foreground with solid side walls. Per-model previews: `evidence/k8-preview-k_celadon_*-flfr.png`. Limitations: decorative door-less blocks (east of the Mansion, between the houses) are not census rows and stay flat art; the Game Corner / Prize Room roofs are invented smears; LG screenshot not taken this slice (blockdata identical, buildings.bin byte-identical). Hardware not run.
## K9 Fuchsia City, Safari Zone, Routes 11 / 12 / 16 / 18 (2026-10-07), in progress
- Census owned (before: covered 85 / 152). Fuchsia 3/7 L85 (FNV E2428371, FR = LG), 9 placements: Mart (11,15), Gym (9,32), Center (25,31) are K2's; (14,31)->11/4, (19,31)->11/9, (33,31)->11/7, (38,31;39,28;39,29)->11/8 are four identical grey-roofed halls; (28,16)->11/2 a house; (24,5)->11/0 the Safari Zone entrance. Safari Zone 1/63..1/66 (L147-150, mapsec 0x88), 6 placements: centre (25-27,30)->11/0 (the entrance building seen from inside) and (29,25)->1/67, east (40,14)->1/68, north (43,8)->1/69, west (12,7)->1/71 and (19,18)->1/70. Routes: 11 = 3/29 (L99, gate (58,10;65,10)->22/0), 12 = 3/30 (L100, house (12,86)->23/2, gate (14,15;15,15;14,21)->23/0), 15 = 3/33 (gate already covered by k_route8_gate, same cell contents), 16 = 3/34 (L104, house (10,5)->25/0, two-storey gate (20,6;27,6;20,13;27,13)->25/1), 18 = 3/36 (L106, gate (41,9;48,9)->26/0). K9 owns 18 uncovered rows (6 + 6 + 1 + 2 + 2 + 1); Route 15 is covered already.
- k_fuchsia_hall banked: rect (13,28,5,4) 80x64 on L85, matchRows (1,4) (the rect's top row is rock, which differs between the placements; roof top edge at art row 8). Flat block (`fz_block`: facade rows 43-64, rim row 42, level roof top 8-43). check PASS ortho 0/0/0 on 1 exact rect (0,8,80,64), density empty, side west 0 of 627 open, side east 0 of 627 open, round trip ok. placements 4: (13,28), (18,28), (32,28), (37,28), all L85 / map 3/7. viewed: the light-grey flat roof with its glass panel over the brick facade with the yellow door and two windows reads right from fl and fr, solid brick-coloured end walls on both sides.
- k_fuchsia_house banked: rect (26,12,6,5) 96x80 on L85 (census seed 26,13,6,4; the roof top edge starts 8 px into row 12). Flat block (`fz_block`: facade rows 56-80 = 24 high, gold tiled roof rows 8-56). check PASS ortho 0/0/0 on 1 exact rect (0,8,96,80), density empty, side west 0 of 1012 open, side east 0 of 1012 open, round trip ok. placement (26,12) only (map 3/7; door (28,16) inside). Side tiles: grey awning band (8,57,88,59), gold roof (8,10,40,12). viewed: the gold tiled roof with its relief over the cream-and-green facade with the yellow door and two arched windows reads right from fl and fr, plain grey end walls on both sides (the facade's own awning grey, since the art has no flat cream patch).
- k_safari_rest banked (the four Safari rest houses, `k_fuchsia_house` builder with width 80 / roof rows 32): rect (28,22,5,4) 80x64 on Safari centre L147 (FNV 1A2757E4, FR = LG), matchRows (1,3): the top row differs on L150 (cliff above) and the bottom row on L147 (path cells), the middle two rows are identical on all four. Roof rows 8-40, facade 40-64 (24 high). check PASS ortho 0/0/0 on 1 exact rect (0,8,80,64), density empty, side west 0 of 660 open, side east 0 of 660 open, round trip ok. placements 4: L147 (28,22) map 1/63, L148 (39,11) map 1/64, L149 (42,5) map 1/65, L150 (18,15) map 1/66; all four census doors ((29,25), (40,14), (43,8), (19,18)) are inside. Side tiles: grey awning (8,41,72,43), gold roof (8,10,40,12). viewed: the gold roof over the cream-and-green facade with door and two windows reads right from fl and fr, grey end walls both sides. The fourth placement's bottom row (path cells) is drawn with L147's bottom row art.
- k_fuchsia_safari banked (the Safari Zone entrance, Fuchsia side): rect (22,0,6,6) 96x96 on L85 (census seed 16,0,16,6 is a flood fill that also takes the tree rows and fences beside it). The map top cuts the roof. One flat block (`fz_block`: facade rows 64-96 = 32 high, gold tiled roof rows 0-64, so the roof is 64 deep). check PASS ortho 0/0/0 on 1 exact rect (0,0,96,96), density empty, side west 0 of 1860 open, side east 0 of 1860 open, round trip ok. placement (22,0) only (door (24,5) inside). Side tiles: grey band (8,65,88,67), gold roof (8,57,40,59). viewed: a gold-roofed block with its cream-striped facade, the pale canopy over the red Poke Ball door and two small windows reads right from fl and fr, plain grey end walls. Limitation: the canopy and its pillars are painted on the facade (the art has them no more than 1 px deeper), the roof is cut by the map top so its depth is a guess.
- k_safari_west banked (the big Poke-Ball-door building on Safari West, door (12,7)): rect (10,2,6,6) 96x96 on L150 (FNV FEAD1C27, FR = LG; the census seed 4,0,16,13 is a flood fill with the trees). The same art as the Fuchsia entrance with the roof top at row 8 (trees above), so the `k_fuchsia_safari` builder with arg0 = 8: facade rows 64-96 (32 high), roof rows 8-64. check PASS ortho 0/0/0 on 1 exact rect (0,8,96,96), density empty, side west 0 of 1620 open, side east 0 of 1620 open, round trip ok. placement (10,2) only (map 1/66). viewed: the same gold-roofed block with the canopy and red door reads right from fl and fr, plain grey end walls.
- k_safari_hall banked (the roof of the Safari Zone entrance building on the centre map, doors (25,30)-(27,30)): rect (22,30,8,6) 128x96 on L147 (the census seed 22,30,8,6 is the rect itself). The map bottom cuts the roof, so the art is roof only. One low block (`fz_block`: roof top rows 8-72, a front face that reuses the last 24 roof rows 72-96, 24 high; invented height). check PASS ortho 0/0/0 on 1 exact rect (0,8,128,96), density empty, side west 0 of 1364 open, side east 0 of 1364 open, round trip ok. placement (22,30) only (map 1/63). Side tile: flat gold roof (40,10,70,12) for wall and roof. viewed: a low gold-tiled roof slab with solid end faces from fl and fr; the grey awning above it stays ground art.
- k_route11_gate and k_route18_gate banked (the Route 11 gatehouse (58,10;65,10)->22/0 and the Route 18 gatehouse (41,9;48,9)->26/0; one builder `k_route_gate`, the same body as the Route 7 / 8 gate): rects (58,7,8,5) on Route 11 L99 (FNV EB2A5FB3, FR = LG) and (41,6,8,5) on Route 18 L106 (FNV 45402064), both 128x80. The art of the body (x 16-112) is identical to `k_route7_gate`; only the side porches' ground cells differ (so neither matched the existing gate rows). Body = one flat block x 16-112 (facade rows 43-80 = 37 high, flat grey roof top rows 0-43); the porches stay flat ground. check PASS for both: ortho 0/0/0 on 1 exact rect (16,0,112,80), density empty, side west 0 of 1435 open, side east 0 of 1435 open, round trip ok. placements 1 each ((58,7) map 3/29, (41,6) map 3/36). Side tile: grey stripe (24,73,104,76). viewed (both): the grey corrugated roof over the window band and yellow brick between two white pillars reads right from fl and fr, plain blue-grey end walls on both sides. Route 15's gate (3/33) was already covered by `k_route8_gate`.
- k_route12_cottage and k_route16_cottage banked (the blue-roofed hip cottages: Route 12 (12,86)->23/2 and Route 16 (10,5)->25/0; one builder `k_cottage`, built the way the Day Care is): rects (11,84,5,3) on Route 12 L100 (FNV C0CE28C3) and (9,3,5,3) on Route 16 L104 (FNV B570D855), both 80x48. Same art, different metatile ids (another secondary tileset), so two rows. 13 x-slices whose first roof row follows the 2-px diagonal of the hip ends (row 6 at the corners down to row 0 along the ridge), each one a profile prism (facade rows 27-48 = 21 high, eave band 24-27, roof slope rows r0-24 at 45 degrees). check PASS for both: ortho 0/0/0 on 13 exact rects (the slices), density empty, side west 0 of 646 open, side east 0 of 646 open, round trip ok. placements 1 each ((11,84) map 3/30, (9,3) map 3/34). Side tiles: pale facade (34,42,62,44), blue slope (14,13,66,18), eave 24. viewed (both): a blue slate hip roof over the pale facade with the wooden door and window, with the stepped hip ends and the gable-end triangle behind them (same look as the accepted Day Care), solid end walls on both sides.
- k_route12_gate banked (the Route 12 gatehouse on its pier, doors (14,15), (15,15), (14,21)->23/0): rect (12,15,5,7) 80x112 on Route 12 L100. Rows: the wooden north deck 0-16 (left as flat ground; the two north doors sit on it), the grey ribbed roof top 16-59, a dark rim 59-64, the window band, yellow brick and the dark arch with its canopy 64-112. One flat block over the full 80 px (`fz_block`: facade rows 59-112 = 53 high, roof top rows 16-59). check PASS ortho 0/0/0 on 1 exact rect (0,16,80,112), density empty, side west 0 of 2091 open, side east 0 of 2091 open, round trip ok. placement (12,15) only (map 3/30; door (14,21) is inside the model's rect, so the census row gets covered). Side tile: grey roof plank (48,20,54,56). viewed: the ribbed grey roof over the window band, the yellow brick facade and the dark arch with its canopy reads right from fl and fr, plain grey end walls. Limitation: the arch is painted on the facade (the passage itself is not modelled).
- k_route16_gate banked (the Cycling Road gatehouse, two storeys, doors (20,6), (27,6), (20,13), (27,13)->25/1): rect (20,3,8,12) 128x192 on Route 16 L104. A two-step profile prism over x 16-112 (the side porches stay flat ground): the facade and the lower rim are one face at z 192 (rows 139-192, 53 high), the lower roof top is level at y 53 back to z 133 (rows 80-139), the upper rim is a 5-high face at z 133 (rows 75-80), the upper roof top is level at y 58 (rows 0-75). check PASS ortho 0/0/0 on 1 exact rect (16,0,112,192), density empty, side west 0 of 7097 open, side east 0 of 7097 open, round trip ok. placement (20,3) only (map 3/34; all four doors inside). Side tile: grey roof plank (48,20,54,56). viewed: two stepped grey ribbed roofs over the window band and yellow brick facade reads as a two-level building from fl and fr, solid end walls on both sides including both steps.
- K9 closing: census covered 85 -> 103 / 152 (18 own placements: Fuchsia halls 4, house, Safari entrance; Safari West building, Safari entrance-hall roof, four rest houses; Route 11 and 18 gates; Route 12 and 16 cottages; Route 12 and 16 gates; Route 15's gate was already covered by k_route8_gate). 12 new models, 53 Kanto models total, 105 placements. FR = LG buildings.bin SHA-1 ef4ad20a393edb33fc515b8967ce07ce8f362a3b -> dfe19b37d8bfec04575a253e17d9c09097f5242e. Every model: check PASS, ortho 0/0/0 on every exact rect, density empty, round trip ok, side west/east both 0 open of N (hall 627, house 1012, safari 1860, safari_west 1620, safari_hall 1364, safari_rest 660, route11/18 gates 1435, cottages 646, route12 gate 2091, route16 gate 7097). Gate: Emerald buildings 2929c764, regions 007a370f, signposts 38515605, relief 21a837f0, `--relief ledges` eb25a383 identical; FRLG regions 3716874d, signposts ba2fde45, relief 32c24146 unchanged (FR and LG). `make -C tools/romgen test` (ROMGEN_ROM, _FR, _LG set): 27 suites, 0 failures, 0 skipped (test_romgen_frlg_buildings 3259 -> 3479 checks, nk bumped to 53 at both sites); `vtest`: 9 suites, 0 failures; device `make -j8` builds. Azahar (private instance k, EMUTEST_STATE_DIR=/tmp/k9, warp-save copies, FR voxel files in the instance's voxel/BPRE): `evidence/k9-fuchsia-safari.png` shows the Safari Zone entrance building on Fuchsia 3/7 with its gold roof and facade in 3D and the grey hall roof at the bottom right; `evidence/k9-route11-gate.png` shows the Route 11 gatehouse (3/29) as a solid block with a closed east side wall. Per-model previews: `evidence/k9-preview-*-flfr.png`. Limitations: the Safari centre-hall roof is cut by the map bottom, so it is an invented low slab; the cottage hips show a gable triangle behind the stepped ends (as the Day Care); Route 11/18 gate side porches stay ground; LG screenshot not taken (blockdata identical, buildings.bin byte-identical). Hardware not run.
