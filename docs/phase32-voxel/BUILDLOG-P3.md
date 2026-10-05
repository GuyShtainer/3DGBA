# BUILDLOG-P3 (phase 32 slice P3)

## 2026-10-05 slice A: mGBA backdrop-key patch, gbacore additions, overlay + gate (pure C)
- patches/mgba-backdrop-key.patch (MPL-2.0, 2 mGBA files) applied in external/mgba and libmgba rebuilt.
  The old build-3ds dir was unusable (cmake cache pointed at the pre-rename projects/dual-gba path) and a newer
  Homebrew json-c broke configure: recreated build-3ds with -DUSE_JSON_C=OFF. flags.make C_DEFINES diffed identical
  to the old build's, so Makefile MGBA_DEFS stays matched. Re-apply recipe in docs/kb/mgba-integration.md.
- gbacore_mem_block (core->getMemoryBlock, read-only host pointer; region = address top nibble) and
  gbacore_set_overlay_mode (BG1-3 + OBJ off, backdrop key on; calls the patch's setter, which also dirties all
  scanlines, otherwise mGBA's per-scanline cache would keep stale rows on static screens).
- vx_overlay.{c,h}: key-to-RGBA5551 pass, VX_OVERLAY_KEY_PATCH switch (fallback C keys on the backdrop colour),
  the 6.6 state machine. test_voxel_overlay 23 checks incl. exhaustive proof (all 32768 colours x BLDY 0..16 via the
  renderer's brighten/darken masks) that 0x0020 is never a real pixel.
- voxel_gate() in tilt.c/h (pure C) + test_voxel_gate (37 checks, 1024x9x3 sweep vs an independent reference).

## Slice B/C proof (emulator, Old-3DS movie harness)
- BLOCKER: no voxel data pak exists here and building one is forbidden, so the voxel WORLD was never rendered.
- Forced-overlay dev build (-DVX_DEV_FORCE_OVERLAY=1, default 0): voxel pref ON -> keyed BG0 overlay over black
  (OVERLAY_BLACK path), full 400x240 top: evidence/forced-overlay-textbox.png, forced-overlay-startmenu.png. HUD 14-31 fps (Old 3DS emu).
- Voxel OFF (normal build): evidence/voxel-off-startmenu.png, normal flat frame (HUD 23-24 fps).
- Unverified: world render, depth buffer on top target, y-flip, door-fade key on real content, HW perf.
