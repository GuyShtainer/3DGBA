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
