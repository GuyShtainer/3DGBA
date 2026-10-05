# BUILDLOG-P2 (phase 32 slice P2)

## 2026-10-05 slice A: vendor + own foundation + embedded art
- Vendored 31 upstream voxel files at c330c0a (not voxel_battle.*), header added to each, NOTICE.md verbatim.
- Own: gba_game.h, ctr_shims.{h,c}, ctr_shims_pure.c, vx_adapter, vx_snapshot, vx_lz77, vx_behavior, vx_battle_stub, vx_data.
- ctr_voxel.c: shader + tree art embedded (voxel_shbin, data/voxel_trees.bin via tools/voxel/pack_trees.py).
- Makefile: source/voxel in SOURCES, -DCTR_VOXEL_ENABLED=1 -DCTR_VOXEL_LIGHTING=1, -Wl,--wrap=GX_BindQueue.
- Build: `make -j8` links 3DGBA.3dsx, no warnings from source/voxel. Nothing calls the voxel code yet.
- Open: gba_game.h rows marked M and the weather offsets are unmeasured (P2a, next slice).
