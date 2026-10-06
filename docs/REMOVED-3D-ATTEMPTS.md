# Removed 3D attempts (2026-10-06)

Guy, 2026-10-06: the voxel world (phases 32-34: `source/voxel/`, `source/vx_host.c`,
`source/romgen/`) "completely replaces all our attempts for a 3D world… they can be deleted".
He chose "Everything old". This page records what went, where to find it in history, and what
was deliberately kept.

## What was removed

| Commit | What | Size |
|---|---|---|
| `8edd20c` | **Phase 31 diorama** (halted 2026-09-12): `docs/phase31-diorama/`, `tools/diorama/`, `test/host/dio_fixture.h` | -7371 lines |
| `ab7184b` | **Phase 14 HD-2D tilt**: `source/tilt.{c,h}`, `source/tilt.v.pica`, `test/host/test_tilt.c`, `docs/phase14-tilt/`, plus the tilt mesh, gate/tween, menu row, pill, HUD chip and settings UI in `main.c` | +188 / -6006 |
| `60fbe21` | **2D depth-pop, grid warp, 2D DoF, bloom, time-of-day light and Vivid passes** in `main.c`, plus `source/warp.v.pica`, the per-frame `DepthSnap`, and the Light/Vivid menu rows | +68 / -900 |

Total: about 14,300 lines deleted.

## What was kept

- **The voxel gate.** `voxel_gate` and `VoxGateIn` moved verbatim from `tilt.h` to
  `source/vx_gate.{c,h}`. `test_voxel_gate.c` is unchanged apart from its include.
- **The Zallax compositor**, which does the voxel world's DoF and bloom. The **DoF** and
  **Bloom** pause-menu toggles (`dofOn`/`bloomOn`) now drive only the voxel world.
- **Phase 15 presence** and `gamestate.c`. `calc_xform` stays, because presence uses it.
- **The 2D path.** Outside the voxel world the top screen is plain 2D, and both eyes are
  identical: the 3D slider does nothing there.

## Where to find the old code

Look these commits up with `git show <sha>`. The deletion commits above also hold the last
working version of every removed file.

- Phase 31 diorama: `f1c09c7`, the bank of specs, research and fixture tooling made when it
  was halted.
- Phase 14 tilt: `ae35079`.
- 2D stereo and depth lineage:
  - `1275649` added per-eye stereo;
  - `7bf7088` fixed the wobble.
- HD-2D passes:
  - M1 DoF bands: `99ed9fa`;
  - M2 grid warp and text-aware DoF: `ae8b60c`;
  - M3 LDR bloom: `1755458`;
  - M4 time-of-day light: `35a6ba4`.

## settings.bin and g_prefs compatibility

The settings file layout did not change. It is still 29 × s32 = 116 bytes, with magic
`'DGB3'`, and the length ladder is unchanged.

- **Reserved settings words:**
  - `Settings.rsvTilt` (was `tilt`);
  - `rsvLight` (index 14, was `lightOn`);
  - `rsvVivid` (index 15, was `vividOn`).

  These words are written as 0 and ignored on load. `_Static_assert`s pin their offsets, and
  `lenLight`/`lenVivid` now use `offsetof` on the reserved fields. An old `settings.bin` still
  loads, and every later field keeps its offset.
- **Reserved `UiPrefs` field:** `UiPrefs.rsvTilt` (was `tiltLevel`). The emutest harness reads
  `g_prefs` by offset over GDB, so `g_prefs+0x1c` stays put.
- **Reserved action ids:** `ACT_RSV_TILT`, `ACT_RSV_LIGHT = 14` and `ACT_RSV_VIVID = 15`, so
  the later ids do not move.
- **`GsDepth` columns:** they stay in the game-state log, but only `overworld`, `textTop`,
  `textBot`, `s3d` and the presence/peer-sprite blocks are still filled; every depth, disparity
  and tilt column is written as 0. This keeps the log format stable.

## How the removal was checked

- **Pixel diffs.** Before and after captures in Azahar, using the same movie, pixel-diffed:
  - `firered-house`: identical, and plain 2D.
  - `firered-pallet`: still voxel 3D with trees and K1 models. Only NPCs, water sparkle and
    the fps digits differ.
  - `emerald-r104`: only NPCs, water sparkle and the fps digits differ.
  - `emerald-littleroot`: with the original movie, the old build left the START menu open
    while the new build closed it. A short tap was sampled at a different game frame,
    because the main loop's timing changed. Each build is deterministic: the new build
    matched its own repeat exactly (0 px). Re-shot with a movie that has no menu, the scene
    matches. Only NPCs, flower sparkle, the fps digits and the slowly drifting voxel sun
    dapples (`VOXEL_DAPPLE_DRIFT`) differ; most of those dapple pixels are off by 1-3 levels.
- **Tests.**
  - Romgen `test` (25 suites) and `vtest` (all 9 groups) give the same counts as before.
  - Host tests:
    - `test_presence`: 61376 → 56722 checks. TEST 29 and the tilt half of TEST 7 were removed.
    - `test_typography`: 1419 → 1400 checks.
    - `test_tilt`: deleted.
- **Device builds.** `make -j8 ROMGEN_DEV_HOOK=1`, then `make -j8`, then `make cia`, all with no
  new warnings. There are 25 warnings, a subset of the 26 the baseline had.

## Known follow-ups (left as they are)

- **Orphaned baked labels.** The baked `pause-bot-enhance` plate art still shows the labels
  "Tilt-shift DoF", "LDR Bloom", "Time-of-day light" and "Vivid mode", and the ENHANCE tab
  now has an empty band at y141-226. This needs re-baked plate art.
- **Broken smoke-test pieces.** The TILT channel in `tools/emutest/smoke.sh` (press-ctm asserts
  `tiltLevel == 3`) and `tools/emutest/tests/fixtures/movie_menu_tilt_quit.json` target the deleted row. They need retargeting.
- **Historical comments.** Comments in `presence.{c,h}`, `presence_art.c`, `fieldgate.h`,
  `touchgeom.h`, `touch.c`, `gamestate.h` and `main.c` still mention `tilt_project` or the
  depth-pop. They describe how that code was designed and were left as they are.
