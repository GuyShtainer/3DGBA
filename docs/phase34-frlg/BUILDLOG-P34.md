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
