# BUILDLOG-P2 (phase 32 slice P2)

## 2026-10-05 slice A: vendor + own foundation + embedded art
- Vendored 31 upstream voxel files at c330c0a (not voxel_battle.*), header added to each, NOTICE.md verbatim.
- Own: gba_game.h, ctr_shims.{h,c}, ctr_shims_pure.c, vx_adapter, vx_snapshot, vx_lz77, vx_behavior, vx_battle_stub, vx_data.
- ctr_voxel.c: shader + tree art embedded (voxel_shbin, data/voxel_trees.bin via tools/voxel/pack_trees.py).
- Makefile: source/voxel in SOURCES, -DCTR_VOXEL_ENABLED=1 -DCTR_VOXEL_LIGHTING=1, -Wl,--wrap=GX_BindQueue.
- Build: `make -j8` links 3DGBA.3dsx, no warnings from source/voxel. Nothing calls the voxel code yet.
- Open: gba_game.h rows marked M and the weather offsets are unmeasured (P2a, next slice).

## 2026-10-05 slice B: P2a offset measurement + first host suites
- Upstream offsetof probe (§2.10) is impossible here: no pret `include/` exists in any reference checkout. Replaced by
  (1) a host-libmgba RAM probe (tools/voxel/probe_ram.c) run on five saves, asserted by tools/voxel/probe_check.py
  (0 fails over 5 dumps), and (2) reading immediates out of the ROM's own accessor functions with arm-none-eabi-objdump.
- Measured: weather block curr 0x6D0 / palState 0x6C6 / blendEVA 0x730 (u16) / fogH 0x6FB / fogD 0x724 (my placeholders
  were wrong); gMain.inBattle = byte 0x439 bit 1; PaletteFade active = bit 15 of u16@6, y = (u16@4>>6)&31;
  ObjectEvent hasReflection mask 0x20001 (bit 17), held/single movement bits 6/1; Sprite template @0x14, data @0x2E,
  subpriority @0x43; GraphicsInfo images @0x1C (my probe read 0x18 first: anims), size@6 w@8 h@0xA; MapEvents
  objectEvents@4 bgEvents@0x10, template graphicsId@1, BgEvent x@0 y@2 elev@4 kind@5; connections direction@0 offset@4 group@8 num@9.
- Observed: ObjectEvent mapNum/mapGroup of the player are NOT kept current (stale after a warp) -> not decoded.
- test_voxel_lz77 173 checks, test_voxel_shims 69 checks, both green (ASan+UBSan).
