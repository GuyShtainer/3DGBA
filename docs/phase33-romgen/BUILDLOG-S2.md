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

## 2026-10-06 S2.2 geometry kernel + checks
- Built: `rg_geom.{h,c}` (mesh with interned tags and per-triangle cached tag flags, clip, triangulate, tile_pieces, Strip,
  strip_face, Prism incl. Proj cuts / Strip / Tile edges / caps, HipRoof, Frustum, Vault, Walls, Card, Facet, PlainWall,
  Decal, Cylinder, Lifted, chunked part list; `rg_pysum`, floor-div helpers, stable merge sort) and `rg_bcheck.{h,c}`
  (double-depth Raster, ortho check, density check); `test/host/test_romgen_geom.c`. Relief and Mound stay for S2.5.
- Tests: 585 checks, 0 failures (ASan/UBSan). pysum compensated vs naive vectors (0.1x10 = 1.0, 1e100+1-1e100 = 1.0),
  nearbyint(2.5)==2, floor div/mod, stable sort, clip/triangulate (both windings, L shape area), tile_pieces order and flip
  and s0, Raster tie keeps first / nearer wins / alpha 127 vs 128 / colour int(255*0.72)==183, a projected wall prism = 0/0/0
  and empty density, a shifted uv gives wrong>0, doubled u is listed, clamp cuts, every part type emits with exact counts
  (vault 18, cylinder 46), projected parts = art, hip and frustum and every Strip variant pass the density check
  (which validates the port of strip_face / segments / wrap / tail).
- Decisions / deviations:
  - `RgExact` lives in `rg_bcheck.h` (the spec put it in rg_bspecs.h) so rg_bcheck needs no dependency on the specs table.
  - `rg_density_check` keeps the spec's `art` parameter but ignores it (upstream's density_check does not use it).
  - `rg_clip` takes `nc` (live coordinates) and copies whole `RgPt`s for kept vertices; only cut vertices are zero-filled
    beyond `nc`. Callers never read past `nc`.
  - `Card.art` is held inside the part (`RgCard.art`), so `rg_part_emit(part, mesh)` drops the spec's separate `art` argument.
  - Python `sum()` over mixed int/float items is modelled as double (ints are exact in double at these magnitudes).
