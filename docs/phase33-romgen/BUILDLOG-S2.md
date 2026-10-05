# PHASE 33 ROMGEN build log, S2 (buildings.bin)

## 2026-10-06 S2.1 S0 additions + images
- Built: `rg_art.{h,c}` gains `rg_cell_px` (G1), `rg_subtile_px` (G2), `rg_c5_to_8` (G4); `rg_world.{h,c}` gains
  `rg_metatile_entries` (G3); new `rg_bimg.{h,c}` (RgImage new/free/paste/bbox/crop/hash/equal, `rg_c5_rgba`,
  `rg_hex_to_c5`, `rg_cell_image`, `rg_building_art`); `test/host/test_romgen_bimg.c`.
- Tests: 3651 checks synthetic, 10481 with `ROMGEN_ROM`. Synthetic: `rg_cell_px` equals `rg_layer` wherever `rg_layer` draws (both
  layers, flips, secondary palettes), lower idx-0 pixels carry palette slot 0 of their own quarter's palette and are drawn,
  a tile past the tile data is (31,0,31) idx 0 (drawn on the lower layer, undrawn on the upper), an out-of-range metatile is
  all magenta (lower) / empty (upper); building_art: ground blocks transparent whole, a block with painted-in ground kept whole,
  `owned` limits cells, `upperOnly`, off-map fails; every hex colour literal in voxel_building_specs.py (33 of them) has a 555
  preimage and a one-bit-off literal is rejected. Real ROM: Littleroot (2,4,5,5) = 80x80, alpha only 0/255, every transparent
  pixel (0,0,0,0), 5983 opaque pixels, bbox (0,1)-(80,80); the east house (13,4) has the identical bbox.
- Decisions / deviations:
  - `rg_building_art` derives upstream's `ground_px` itself from the ground metatiles whenever `owned` is given
    (`build_models` passes `ground_px` exactly when `owned` is set), so the spec's signature needs no extra parameter.
  - `rg_cell_image` allocates its 16x16 image (spec signature `RgImage *out16`) and has no `layers` argument (only the
    two-layer form; the lower-only form is added when a caller needs it, S2.6 room_check).
  - Uncompressed tilesets decode `min(avail, 16384)` bytes (S0 rule), so "a tile past the data" in a synthetic fixture needs a
    compressed tileset (exact length). Noted for later fixtures.
