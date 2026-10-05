# PHASE 33: ROMGEN. The full Zallax 3D look from a normal ROM

**Ask (Guy, 2026-10-05):** "Do whatever we can to use a normal ROM". There should be no Emerald3DS builder and no
`emerald3ds.pak`. The user has their own Emerald dump, and 3DGBA does the rest.

## Why this phase exists
Phase 32 vendored Zallax's voxel *renderer* (`source/voxel/`), which reads our live EWRAM/ROM. The renderer
also reads four precomputed data files, and his look depends on them:

| file | consumer (vendored) | upstream generator (MIT, `_reference/.../3ds_port/scripts/`) | lines |
|---|---|---|---|
| `regions.bin` (VXR5) | voxel_regions.c | gen_voxel_regions.py + voxel_cells.py | 95 + 434 |
| `signposts.bin` (VXS2) | voxel_sign.c | gen_voxel_sign_masks.py + voxel_sign_mask.py | 90 + 87 |
| `buildings.bin` | voxel_building.c | gen_voxel_buildings.py + voxel_building.py + voxel_building_specs.py | 1647 + 1817 + 1375 |
| `relief.bin` | voxel_relief.c | gen_voxel_relief.py + voxel_relief_lines.py + voxel_props.py (+ checks) | 3846 + 658 + 266 |

All four generators also share `voxel_art.py` (tileset pixels). They read a *decomp-layout tree*
(layouts.json, map.json events, tiles, palettes, metatiles, attributes, the behaviour enum), which Zallax's
builder reconstructs from the ROM. Without the files the world is flat ground with box houses.

## Goal
3DGBA generates all four files **from the loaded ROM**, written as loose files to `sdmc:/3DGBA/voxel/`.
Those are read through the existing `CtrData_Open` loose path, which has no CRC/ABI pin. Generation is
keyed by ROM SHA-1 and cached, so it runs once per ROM, either on first voxel use or from a settings action
with a progress screen.

## Rules
- Port = re-implement Zallax's MIT generators in **pure C** (CLAUDE.md rule 4: dual-compile on the PC).
  Every ported file gets the "Modified for 3DGBA (GPLv3), 2026" header plus the MIT notice
  (LEGAL-zallax-port.md option 2a).
- **Never run Zallax's builder or scripts** (a session permission denial; no route around it). We read them
  only as specification.
- Read inputs from the **ROM image** with our own offsets and accessors (vx_adapter, our sym docs), never
  from a pret tree.
- Generated data is ROM-derived. It is never committed, never in romfs/.cia/release assets, and it is
  .gitignored. release-legal-audit checks this.
- On-device cost has to be measured. If one generator is too slow on ARM11, that generator alone gets a PC
  fallback (a host build of the same C, writing to SD).

## Validation (no reference output is available to us)
1. Each format round-trips through the *vendored consumer's own parser* in a host test.
2. Each generator gets invariant tests drawn from the upstream docstrings: known layouts and cells. Examples:
   Littleroot's two houses are the same model; ROUTE104's signposts are at their event positions.
3. Emulator frames are compared against Zallax's README screenshots, for the same places where reachable.
4. Optional, only if Guy ever offers it: one pak from Guy for a byte comparison. It is never required.

## Slices
- S0: the ROM-world foundation. Layouts and map headers from the ROM, the tileset pair (tiles, palettes,
  metatiles, attributes), map events, the behaviour table. This is voxel_cells.py's `Layout`/`MapEvents`/
  `pair_for` plus voxel_art pixels, in C.
- S1: the roles (voxel_cells role_at) → regions.bin, then signposts.bin.
- S2: buildings.bin (the specs table + the geometry builder).
- S3: relief.bin.
- S4: on-device driver (first-run generation, SHA-1 cache, progress UI, timing on hardware) + ENHANCE status.
- S5: adversarial review + release-legal-audit gate.
