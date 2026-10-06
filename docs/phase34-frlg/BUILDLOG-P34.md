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
