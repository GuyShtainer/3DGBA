# PHASE 33 ROMGEN, SPEC for S2 (buildings.bin, VXB7)

Status: build-ready spec, 2026-10-06 (written incrementally; sections 1-7 plus Appendix A of citations).
Contract: `docs/phase33-romgen/PHASE.md`. Foundation: `SPEC-S0-S1.md` (the S0 API) and `SURVEY-S2-S3.md`.

Citation keys used throughout (all under `projects/_reference/pokeemerald-3Ds-dualscreen/3ds_port/scripts/`):
- `vb:N` = `voxel_building.py` line N (1817 lines: primitives, Raster, checks, LayoutArt)
- `gen:N` = `gen_voxel_buildings.py` line N (1647 lines: expanders, reuse, placements, export)
- `sp:N` = `voxel_building_specs.py` line N (1375 lines: the SPECS table and its part builders)
- `props:N` = `voxel_props.py` line N (266 lines: tile-found objects)
- `vbuild.c:N` = our vendored consumer `source/voxel/voxel_building.c`
- **M2** = a measurement made for this spec on the user's BPEE ROM (`roms/emerald.gba`) with a throwaway C
  probe of ours that links `rg_world.c` and `vx_lz77.c` (not Zallax code, not committed). Every M2 number is
  re-asserted by a host test in real-ROM mode (section 5.4).

All Python was read as specification only. None of it was run, by any route. No pret tree was read.

---

## 0. What S2 produces, and what it consumes

**Output.** `buildings.bin` (magic `VXB7`), one file of: geometry for every building model (stored
once, uv in the model's own art pixels), one RGBA5551 texture page per layout that places a model,
the page-model and placement tables, the per-cell heights, sun footprints and upper-layer quarters,
and the ground variants. Byte layout in section 4. It is written to `sdmc:/3DGBA/voxel/` by S4,
never committed, never in romfs.

**Inputs (all from the ROM via S0).** Upstream reads, besides `layouts.json` / blockdata / tilesets:
1. layout **names** (`LAYOUT_LITTLEROOT_TOWN`, ...) in SPECS (sp:1092-1375) and in the props sort
   (gen:102). We resolve each name to a 1-based layout id once, at spec time, and pin it with a
   FNV-1a fingerprint of the blockdata (section 3.1). No names exist in the ROM.
2. tileset **names** (`gTileset_Petalburg`, `gTileset_Rustboro`, `gTileset_General`) in the component
   and props specs. We resolve them to tileset ROM addresses (section 3.1).
3. map `connections` (props:93-121), for objects a map seam cuts. Available as `rg_map_connections`.
4. Pixel colours of metatiles, including palette-index-0 pixels and missing tiles. **The S0 API does
   not expose these yet** (section 1.1, gap G1).

The output is ROM-derived. Nothing of the ROM and nothing generated is committed.

---

## 1. C module layout and APIs

All modules live in `source/romgen/`, are pure C11 like the S1 files (only `<stdint.h> <stdbool.h> <stddef.h> <string.h>
<stdlib.h> <math.h>`; no stdio, no libctru), compile with `-ffp-contract=off` (already set for `rg_%.o`
in the device Makefile and in `tools/romgen/Makefile`), and write output into caller buffers or
`malloc`'d blobs. Every file that ports upstream code carries the header convention of the existing
files (`rg_roles.c:1-4`), with the source script named:

```c
/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/<file>.py (<what>),
 * MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
```

Doubles everywhere (Python floats are IEEE double). Floats appear only at the f32 pack in the writer.

### 1.1 S0 additions (`rg_art.{h,c}`, `rg_world.{h,c}`), slice S2.1

The S1 accessors are not enough for S2. `build_layer` (rg_art.c) skips palette-index-0 pixels and
skips tiles past the tile data, because S1 only needed "drawn" masks. Upstream's `building_art`
(vb:161-196) and `cell_image` (vb:198-216) need the full picture:
- a lower-layer pixel of palette index 0 is drawn with **palette slot 0's colour** of that subtile's
  palette (opaque);
- a tile number past the tile data draws **magenta (255,0,255)** with index 0, which is BGR555
  (31,0,31) exactly;
- an upper-layer pixel of index 0 is transparent (unchanged).

New API (G1-G4 in section 7.2):

```c
/* G1: one metatile layer as the GBA draws it, every pixel. c = BGR555, idx = palette index (0..15),
 * drawn bit = the pixel is opaque for this layer (lower: always 1; upper: idx != 0). */
typedef struct RgCellPx { uint16_t c[16][16]; uint8_t idx[16][16]; uint16_t drawn[16]; } RgCellPx;
void rg_cell_px(RgPair *p, uint16_t metatile, int layer, RgCellPx *out);

/* G2: one 8x8 subtile by (tile, palette), no flips (props, flank_band). Out of range -> magenta, idx 0. */
void rg_subtile_px(RgPair *p, uint16_t tile, uint8_t pal, uint16_t c[64], uint8_t idx[64]);

/* G3: the 8 raw u16 entries of a metatile (props upper_subtiles, kit scans). false when out of range
 * (upstream's `pair.entries` returns None, props:80). */
bool rg_metatile_entries(const RgWorld *w, const RgLayout *L, uint16_t metatile, uint16_t out[8]);

/* G4: BGR555 -> RGB888 the way voxel_art does (c5*255//31), for the specs' hex colour constants. */
static inline uint8_t rg_c5_to_8(unsigned c5) { return (uint8_t)(c5 * 255u / 31u); }
```

Colour constants in specs (`WALL_COLOURS = ("f6b473", ...)`, sp:590) are RGB888 strings that came from
`c5*255//31`; the C table stores them as BGR555 (computed once from the hex by the reverse map, which is
exact because `c5 -> c5*255//31` is injective) and every comparison runs in BGR555. A hex value that has
no 555 preimage is a spec error, asserted at test time.

### 1.2 `rg_bimg.{h,c}`: RGBA images (port of the PIL parts of vb:161-216, gen:1191-1290)

```c
typedef struct RgImage { int w, h; uint8_t *px; } RgImage;          /* RGBA8, row-major, malloc'd */
bool rg_img_new(RgImage *im, int w, int h);                          /* zeroed = (0,0,0,0) */
void rg_img_free(RgImage *im);
void rg_img_paste(RgImage *dst, const RgImage *src, int x, int y);   /* PIL paste without mask: copy RGBA */
bool rg_img_bbox(const RgImage *im, int bb[4]);                      /* alpha != 0, half-open; false = None */
bool rg_img_crop(const RgImage *im, const int bb[4], RgImage *out);
uint64_t rg_img_hash(const RgImage *im);                             /* FNV-1a of w,h,px; equality = hash + memcmp */
/* vb:198-216: a metatile drawn opaque over black, alpha 255 everywhere */
void rg_cell_image(RgPair *p, uint16_t metatile, RgImage *out16);
/* vb:161-196: the art of a rect of cells; ground tiles drawn as the ground pixels, cells not owned blank */
bool rg_building_art(const RgWorld *w, RgPair *p, const RgLayout *L, int x, int y, int cw, int ch,
                     const uint16_t *groundTiles, unsigned nGround, const uint8_t *owned /* cw*ch or NULL */,
                     bool upperOnly, RgImage *out);
```

`getbbox` rule: every alpha in these images is 0 or 255 and every transparent pixel is (0,0,0,0)
(building_art clears to zero, ground_patch writes (0,0,0,0), gen:1272), so old Pillow (any channel
non-zero) and new Pillow (alpha only) agree; C uses alpha != 0. Asserted by a debug check in the writer.

### 1.3 `rg_geom.{h,c}`: the mesh and every part type (port of vb:68-1530)

```c
enum { RG_TAG_CLAMP = 1, RG_TAG_PROJ = 2, RG_TAG_DEPTH = 4, RG_TAG_BEHIND = 8 };  /* suffix flags */
typedef struct RgVtx { double x, y, z, u, v; } RgVtx;
typedef struct RgTri { RgVtx p[3]; double shade; uint16_t tag; uint8_t flags; } RgTri;
typedef struct RgMesh { RgTri *t; unsigned n, cap; const char *const *tagNames; } RgMesh;
bool rg_mesh_add(RgMesh *m, const RgVtx *a, const RgVtx *b, const RgVtx *c, double shade,
                 uint16_t tag, uint8_t flags);       /* append; order = upstream emission order */

/* vb:296-314 Sutherland-Hodgman against axis = val, keep >= (keep) or <= (!keep), EPS inclusive */
unsigned rg_clip(const RgVtx *in, unsigned n, int axis, double val, bool keep, RgVtx *out);
/* vb:317-354 ear clipping; returns triangle index triples; guard 10000 iterations */
unsigned rg_triangulate(const double (*xy)[2], unsigned n, unsigned (*tri)[3]);
/* vb:357-394 cut a face into whole-tile pieces (floor/ceil with +-EPS) */
unsigned rg_tile_pieces(...);

/* One emitter per upstream part class; each appends to the mesh in upstream order. */
typedef enum { RG_P_PRISM, RG_P_STRIP, RG_P_HIPROOF, RG_P_FRUSTUM, RG_P_VAULT, RG_P_WALLS, RG_P_RELIEF,
               RG_P_LIFTED, RG_P_MOUND, RG_P_CARD, RG_P_FACET, RG_P_PLAINWALL, RG_P_DECAL,
               RG_P_CYLINDER } RgPartKind;
typedef struct RgPart { RgPartKind kind; const char *name; union { ... } u; } RgPart;  /* data only */
bool rg_part_emit(const RgPart *p, const RgImage *art, RgMesh *m);
```

`Band`, `Proj`, `Tile` are value types (structs) used by Prism/Strip/HipRoof, not parts. Each union
member mirrors the constructor arguments of its class one for one (`vb:400-1530`), so a spec in C is
the upstream call with the same numbers. Tag strings are kept (for check reports and the `~` suffix
logic) as a per-model string table; the `flags` byte caches the suffix tests so the checks never
parse strings: `CLAMP` = name ends `~clamp`, `PROJ` = ends `~proj`, `DEPTH` = contains `~depth`,
`BEHIND` = contains `~behind`.

### 1.4 `rg_bcheck.{h,c}`: Raster and the three checks (vb:1538-1716, gen:1518-1593)

```c
typedef struct RgRaster { int w, h; uint8_t *rgb; float *depth; int16_t *owner; } RgRaster;
void rg_raster_draw(RgRaster *r, const double vs[3][6] /* sx, sy, depth, w, u, v */,
                    const RgImage *tex, double shade, int16_t owner);
typedef struct RgOrthoResult { unsigned wrong, missing, extra; } RgOrthoResult;
bool rg_ortho_check(const RgMesh *m, const RgImage *art, const RgExact *exact, unsigned nExact,
                    const RgImage *reference /* NULL = art */, RgOrthoResult *out);
unsigned rg_density_check(const RgMesh *m, const RgImage *art, RgDensityBad *bad, unsigned maxBad);
unsigned rg_room_check(const RgWorld *w, RgPair *p, uint16_t layoutId, const RgBuildModels *models,
                       const RgOpen *open, unsigned nOpen);   /* differing pixels */
```

The raster depth buffer is **double**, not float (Python compares doubles; a float buffer would merge
near-ties that Python keeps apart). Memory: the largest raster is a room (`13*16+64` x `20*16+32`
for the gym, about 272 x 352 = 96k pixels x 12 B = 1.1 MB); freed after each check.

### 1.5 `rg_bspecs.{h,c}`: the specs table and the part builders (sp:1-1375)

```c
typedef enum { RG_SPEC_DIRECT, RG_SPEC_COMPONENTS, RG_SPEC_KIT, RG_SPEC_PROPS, RG_SPEC_INTERIOR } RgSpecKind;
typedef struct RgExact { int16_t x0, y0, x1, y1; bool behind; } RgExact;
typedef struct RgSpec {
    const char *name;
    RgSpecKind kind;
    uint16_t layoutId;           /* resolved 1-based id (section 3.1); 0 for components/props */
    uint32_t layoutFnv;          /* FNV-1a of the layout's blockdata: the name pin */
    int8_t rect[4];              /* x, y, w, h in cells (direct) */
    int8_t matchRows[2];         /* (r0, r1); default (0, h) */
    uint16_t ground[4]; uint8_t nGround;
    const RgExact *exact; uint8_t nExact;
    bool (*parts)(const struct RgSpec *s, int arg0, int arg1, RgPartList *out);  /* builder */
    int16_t arg0, arg1;          /* the lambda's arguments: plaster_x, rib_repeat, width, ... */
    const void *ext;             /* kind-specific: RgComponentsSpec, RgKitSpec, RgPropsSpec, RgInteriorSpec */
} RgSpec;
extern const RgSpec rg_specs[];  extern const unsigned rg_spec_count;   /* 34, in sp:1092 order */
```

### 1.6 `rg_bexpand.c`: component, kit, props, interior expanders and reuse (gen:38-874)

```c
/* Expands rg_specs[] into the flat model-spec list in upstream order (gen:877-893). */
bool rg_expand_specs(const RgWorld *w, RgModelSpecList *out, const volatile int *cancel);
```
Covers component_specs (gen:38-90), prop_specs (gen:93-142) with the props object table
(props:33-57) as const data, piece_spec / seam_art / pick_side / flank_band (gen:145-288), kit_specs
(gen:291-324), _inside / _inside_grid (gen:327-378), reuse / reuse_everywhere (gen:390-556),
interior_specs (gen:559-874).

### 1.7 `rg_buildings.{h,c}`: models, placements, atlas, writer (gen:877-1482)

```c
typedef struct RgBuildModel { const RgModelSpec *spec; RgImage art; RgMesh mesh; uint8_t w, h;
                              uint16_t groundMetatile; uint8_t *owned; uint8_t *own /* bare twin */; } RgBuildModel;
bool rg_build_models(const RgWorld *w, const RgModelSpecList *specs, RgBuildModels *out);  /* gen:877-939 */
void rg_cell_heights(const RgBuildModel *m, uint8_t *out);                                /* gen:978-1006 */
void rg_cell_footprints(const RgBuildModel *m, int32_t *maskOf, RgMaskSet *masks);        /* gen:1014-1051 */
bool rg_find_placements(const RgWorld *w, const RgBuildModel *m, RgPlacementList *out);   /* gen:1063-1188 */
bool rg_pack_atlas(const RgImage *const *imgs, unsigned n, int *tw, int *th, int (*spots)[2]); /* gen:1209-1244 */
/* gen:1293-1451: out == NULL sizes the file; returns bytes written or 0 on an error */
size_t rg_buildings_write(const RgWorld *w, const RgBuildModels *models, uint8_t *out, size_t cap,
                          RgBuildStats *stats);
```

### 1.8 Driver and CLI (`rg_run.{h,c}`, `tools/romgen/romgen_cli.c`)

- `RgRunOpts` gains `bool wantBuildings`; `RgOutput` gains `uint8_t *buildings; size_t buildingsSize;`,
  counts (`models, pages, placements, vertices, masks, variants`), and `msBuildModels, msChecks,
  msPlacements, msWriteBuildings`.
- A gate result per model: `RgOutput.buildingsFailed` (count) plus a fixed-size list of failing model
  names, so a gate failure is visible on device (section 5.5).
- CLI: `--only buildings` (alongside `regions,signposts`), `--dump-model NAME` (prints tri count, the
  three check results, cell heights), and `--time` reports the four new timings.
- `tools/romgen/Makefile` and the device Makefile glob `rg_*.c`, so the new files need no build edit;
  the host CLI build links `-lm`.

---

## 2. Port plan: constants, thresholds, float order and determinism

### 2.1 Constants (all must appear verbatim, one `#define` each, with the citation)

| C name | value | source | use |
|---|---|---|---|
| `RG_NUM_PRIMARY` | 512 | vb:68 | tile < 512 and palette < 6 = primary tileset; metatile < 512 = primary |
| `RG_EPS` | 1e-6 | vb:69 | clip inclusivity, tile_pieces floor/ceil, Strip segment tests |
| `RG_SHADE_ART/WEST/EAST/BACK/WOUND` | 1.0 / 0.80 / 0.72 / 0.66 / 0.90 | vb:73-79 | face shades (fountain sides 0.7, set in sp:534-588) |
| `RG_OWN_GROUND` | 0xFFFF | gen:34 | placement ground of a tile-found prop |
| `RG_MAX_VARIANTS` | 128 | gen:35, voxel_atlas.h:69 | variant cap (gate: > 128 fails the file) |
| `RG_LOOSE_PIXELS` | 64 | gen:464 | loose-reuse threshold |
| `RG_FOOTPRINT_FULL` | 240 | gen:1011 | a cell with 0 < n < 240 footprint pixels gets a mask |
| `RG_MAX_TEXTURE` | 512 x 512 | gen:1247 | page cap (area test `tw*th > 512*512` fails the file) |
| `RG_MAX_PAGES` | 256 | gen:1410, ctr_voxel.c:935 | page cap |
| atlas sizes | {64..1024}^2, `w <= 8h && h <= 8w`, sorted by (area, h) | gen:1216-1218 | candidate pages |
| `PITCH` | 22.0 deg | sp:42 | house roofs (`tan(radians(22))`) |
| `ROOF_COLUMNS` | (12, 68) | sp:43 | house roof course sampling |
| `GRASS` | 0x001 | sp:40 | ground metatile |
| ortho margin | 32 (raster (W+64) x (H+32)) | vb:1595-1662 | ortho_check |
| barycentric tolerance | -1e-7 | vb:1538-1587 | Raster inside test |
| opaque | alpha >= 128 | vb:1538-1587, gen:1199, gen:1385 | raster, same_building_pixels, texels |
| density tolerance | 1e-3; normal tolerance 1e-9 | vb:1665-1716 | density_check |
| cell-height area / clip pad | `_area_xz > 1e-6`; bounds +-2*EPS | gen:991-1004 | cell_heights |
| footprint skip | `max y < 1.0`; `abs(area) < 1e-6` | gen:1028-1032 | cell_footprints |
| patch lift | y = 0.01 | gen:1400-1403 | ground patch quads |
| Mound degenerate | `< 1e-12` | vb:1369 | dropped triangles |
| Frustum | y0 = -1; drawn if nz > 0.1; diagonal if abs(nx) > 1e-6; W if nx < -0.3, E if nx > 0.3 else BACK | vb:774-827 | |
| Cylinder | 16 sides; side drawn if nz > 0.2; `ua = u0 + (k*3) % max(1, int(u1-u0-w))` | vb:1474-1517 | |
| Decal lift | 0.5 (under-decal 0.25) | vb:1453, gen:559-874 | |
| pick_side widths | 8, 6, 4 (strict >, first best); fallback 4 x height | gen:231-267 | relief side column |
| props `rise`/`step` | sea_rock 1.0/2, sand_boulder 1.0/2, sea_stack 1.6/4 | sp:1254-1281 | Mound |
| props rings | sea_rock (222,230,238), sea_stack (131,131,139) | sp:1262, sp:1281 | Mound ring colour |
| Mound `_spans` straightness | 0.5 | vb:1168-1383 | |

### 2.2 Float-order and determinism hazards, with the C rule for each

H1. **`sum()` of floats.** CPython 3.12+ `sum()` of floats is compensated (Neumaier); 3.11 is naive
left to right. Upstream supports both (builder/pyproject.toml:10 `requires-python >=3.11`), and its CI
runs 3.12 (.github/workflows/ci.yml:14). So upstream output is itself interpreter-dependent.
Value-carrying call sites: strip_face `s` and `t` (vb:625-626, `s` becomes the vertex u),
`_unit` (vb:649), density_check (vb:1687, 1701-1702), `polygon_ccw` (vb:405, sign only),
triangulate area (vb:320, sign only), `_area_xz` (gen:1056, compared to 1e-6).
**C rule:** one helper `double rg_pysum(const double *v, unsigned n)` in `rg_geom.c` implementing
CPython 3.12's algorithm exactly (Neumaier with the final `+ c` and the special-value rules,
`Objects/bltinmodule.c` builtin_sum: start 0.0 as int 0 then float; summands converted in order),
selected by `RG_PYSUM_COMPENSATED` (default 1 = CI's 3.12). The naive variant is the same helper with
the macro at 0. Every site above calls it; no other summation of more than two terms is allowed.
Host test: a table of hand-made vectors where the modes differ (e.g. {1e16, 1.0, -1e16}) proves which mode
is compiled; the real-ROM test runs both modes and records how many output bytes differ (expected 0:
the sums are 3-term dot products against mostly axis-aligned unit vectors, which are exact).

H2. **`round()` is half to even.** cell_heights (gen:1006) `int(round(t))`. **C rule:** `nearbyint(t)`
under the default rounding mode (`FE_TONEAREST`), never `round()`/`lround()` (half away from zero).
`int()` elsewhere truncates toward zero: C cast `(int)`. `math.floor/ceil`: `floor/ceil`.

H3. **libm.** `cos sin tan atan2 asin sqrt hypot` appear in Mound (vb:1301-1318), Cylinder
(vb:1489-1510), HipRoof (vb:675, 682: `tan(radians(22.0))`), edge_normal (vb:400), Strip
(vb:474, 621), PlainWall (vb:1443), _plan_normal (vb:753), and density_check. `sqrt` is correctly
rounded everywhere (IEEE). The others may differ by 1 ulp between macOS libm, glibc, and newlib (device).
Python's `math.hypot` (3.10+) is its own near-correctly-rounded algorithm, not libm's.
`math.radians(x)` is `x * (pi/180)` with `pi/180` as one double constant: C uses
`x * (M_PI / 180.0)` written as a precomputed constant `0.017453292519943295`, not `x*M_PI/180`.
**C rule:** call libm (`hypot` included). Byte identity of vertices against upstream cannot be proven
across platforms, so the tested invariants are: (a) ortho_check exact = 0/0/0 and density_check empty
for every model on the host **and** on the device build (S2.8), (b) the integer outputs (cell heights,
footprint masks, quarters, placements, page sizes, texels) are identical between the host build and
the device build (the device writes `buildings.bin`; the host test compares everything except the
vertex block byte for byte, and the vertex block with |delta| <= 1e-5 per float).

H4. **No FMA.** `-ffp-contract=off` for all `rg_*.c` (already in both Makefiles). ARM11 VFPv2 has no
FMA; x86-64/arm64 hosts would contract `a*b+c` without the flag.

H5. **f32 pack.** `struct.pack("<f")` and a C `(float)` cast both round to nearest even. Vertices are
`x/16.0` (exact scale by a power of two) then the cast. Same in C.

H6. **Set and dict order.** Audited one by one (section 2.3). Dicts iterate in insertion order (a
language guarantee since 3.7), so a dict loop is reproduced by an insertion-ordered array. Sets of
tuples of small ints iterate in CPython hash order, which is not a language guarantee; the rule is to
prove each set loop order-neutral or to replace it with a defined order and record the divergence.

H7. **Stable sorts.** Python's `sorted`/`list.sort` are stable. C uses a stable merge sort helper
`rg_stable_sort` (never `qsort`, which is not stable) wherever keys can tie: pack_atlas order
(gen:1215), prop group order (gen:102-110), and the placement sort (gen:1406, full 7-tuple).

H8. **Integer semantics.** Python `//` and `%` floor toward minus infinity; C truncates. Every
`//`/`%` that can see a negative operand uses `rg_floordiv/rg_floormod`: props `cells_of` with a seam
offset (`(sx + i) // 2` where `sx` can be negative, props:217-222), `beyond` (`sx // 2`, `sx % 2`,
props:140-147, 186-192), Cylinder `% max(1, ...)` (non-negative, plain `%` allowed).

### 2.3 Set/dict order audit (every container loop that reaches the output)

| site | container | verdict | C rule |
|---|---|---|---|
| Mound.ring_pixels (vb:1168+) | set | membership only | bitset |
| Mound.with_ring BFS (vb:1168+) | dict, FIFO by level, neighbours +x,-x,+y,-y, first writer wins | order-dependent but deterministic | reproduce exactly: queue + per-pixel "set" flag, same neighbour order |
| component_specs (gen:38-90) | DFS stack `todo.pop()`; block cut by dict insertion order | deterministic | explicit stack (LIFO), same push order |
| interior floods (gen:559-874) | DFS sets | reachability only | bitset flood, any order |
| register_piece anchor `max(...)` over a set (gen:390-462) | set | proven neutral: the set of loose starts it yields does not depend on the anchor, because `_drawn_at` re-checks every cell | use row-major max; note in a comment |
| reuse `taken` pixels (gen:464-539) | set | first wins over a deterministic loop | bitset |
| kit grids (gen:291-324) | set | membership | bitset |
| find_placements ring (gen:1143-1153) | dict counts, `max(ring, key=ring.get)` | first maximum in insertion (scan) order | array of (metatile, count) in first-seen order; strict `>` |
| find_placements `reused_at` floor count (gen:1169-1173) | dict counts | same | same |
| **seam_art** (gen:174-228) | iterates the `south` **frozenset**, writes the rows below each cell's column | order matters only if two south cells share a column | **assert** in the real-ROM test that no two south cells of any hedge/railing share a column; if one ever does, port the CPython set order with S3's `_hash64`/set-order helper |
| **prop_specs group order** (gen:93-142) | layouts sorted by **name** string; groups sorted by `-len`, stable | ties broken by layout name, which the ROM lacks | **divergence:** tie-break by 1-based layout id; reference layout `where[0]` = lowest id (its art is identical either way: every group shares the General primary). Recorded in section 7 |
| props `find` (props:158-214) | `subs.items()` dict (row-major insertion), then `sorted(found)` | deterministic; frozensets never compared because (name, x0, y0) is unique | sort by (name, x0, y0) |
| props `connections()` (props:93-121) | built from `os.listdir` order | upstream nondeterminism, order-dependent only if two neighbours contain the same out-of-layout cell | `rg_map_connections` order; real-ROM test asserts no cell is claimed by two neighbours |
| ground_variants (gen:1454-1482) | dict `(ts, mt, q) -> first lid`, then sorted | first lid depends on model order x `at` order | same loops in the same order |
| export `patches` (gen:1366-1370) | dict keyed by image bytes, insertion order | deterministic | hash + memcmp list in insertion order |
| export `mask_index` (gen:1350-1358) | dict, insertion order | deterministic | linear/hashed list in insertion order |
| export `by_layout` (gen:1360-1364) | dict, iterated `sorted(by_layout)` | deterministic | sort by lid |

### 2.4 Order of the work, made explicit

Upstream's output order is fixed by these loops; the C port keeps each one:
1. model order = `rg_specs[]` order, each spec expanded in place (gen:881-892); within a direct spec
   the model, then its `_bare` twin when `bare_at` is set (gen:925-937).
2. placements of a model: layouts in id order, `py` then `px`, then the `reused_at` list (gen:1090-1180);
   `found` = models in order x their placements (gen:1320-1326). The C port may visit layouts grouped by
   tileset pair (to keep one `RgPair` open), but it stores each placement with the sort key
   (model index, layout id, py, px, reuse seq) and sorts before use, so `found` is upstream's order.
3. vertex order = all model meshes in model order, then per page (lid ascending) the patch quads of each
   placement in `found` order (gen:1395-1405). `extraFirst` is assigned there, before the final sort.
4. `placements.sort()` is a lexicographic sort of the full 7-tuple
   (layout, pageModel, x, y, ground, extraCount, extraFirst) (gen:1406). `extraFirst` makes ties impossible.

---

## 3. The specs table in C

### 3.1 Name resolution (layout and tileset names have no ROM counterpart)

Each name is resolved once, here, to a 1-based layout id (the same id space as `RgWorld.layouts[id-1]`,
upstream `index + 1` at gen:1141, 1161, 1178, and the consumer's `Port_GetMapLayoutById`). The table stores the id
**and** the FNV-1a-32 of the layout's blockdata (its `w*h` u16 entries as ROM bytes), the pin a host test
re-checks. A ROM whose fingerprint differs (a hack, another revision) skips that spec with a log line
instead of modelling the wrong building (section 5.5).

Evidence for every id (M2): the map that uses the layout (group/num from `gMapGroups`), its size against
the spec's rect, the metatiles inside the rect, and, for alternates, the cell diff against the base.

| upstream name | id | evidence | confidence |
|---|---|---|---|
| `LAYOUT_PETALBURG_CITY` | 1 | map 0/0, 30x30; Center rect holds 0x048-0x063 (primary, 4x4 block); gym rect 0x1aa-0x1cd | high |
| `LAYOUT_MAUVILLE_CITY` | 3 | map 0/2, 40x20; Mart rect holds 0x028-0x043 | high |
| `LAYOUT_RUSTBORO_CITY` | 4 | map 0/3, 40x60; gym rect shares 0x1b2-0x1cd with layout 1's gym; kit corners 0x224 x4, 0x220 x4; fountain 0x338-0x34a | high |
| `LAYOUT_LITTLEROOT_TOWN` | 10 | map 0/9, 20x20; the two house rects share rows 0-2 (0x208-0x21a) and differ only in rows 3-4 (the facade) | high |
| `LAYOUT_OLDALE_TOWN` | 11 | map 0/10; house rect 0x26c-0x28f | high |
| `LAYOUT_ROUTE104` | 20 | map 0/19, 40x80; Briney 0x268-0x284, flower shop 0x248-0x265 | high |
| `..._BRENDANS_HOUSE_1F` / `2F` | 54 / 55 | maps 1/0, 1/1 (11x9, 9x8), one map each | high |
| `..._MAYS_HOUSE_1F` / `2F` | 56 / 57 | maps 1/2, 1/3, one map each | high |
| `..._PROFESSOR_BIRCHS_LAB` | 58 | map 1/4, 13x13 | high |
| `..._PROFESSOR_BIRCHS_LAB_WITH_TABLE` | 432 | used by no map header; S0 `altOf = 58`; 13x13; same tilesets; differs from 58 **only** in cells x 8-10, y 3-5, which is the spec's table piece `(128, 64, 176, 89)` px (sp:1349) | high |
| `LAYOUT_HOUSE1` | 59 | map 2/0, 10x9; **used by 9 maps** = sp:1359 "Oldale's first house, and eight more maps'" | high |
| `LAYOUT_HOUSE2` | 60 | map 2/1, 11x8; **used by 12 maps** = sp:1364 "and eleven more maps'" | high |
| `LAYOUT_POKEMON_CENTER_1F` / `2F` | 61 / 62 | maps 2/2, 2/3; used by 15 / 17 maps | high |
| `LAYOUT_MART` | 63 | map 2/4, 11x8; used by 13 maps | high |
| `LAYOUT_LAVARIDGE_TOWN_POKEMON_CENTER_1F` | 71 | map 4/5, 14x9, one map; differs from 61 only in cells x 1-3, y 0-4 (its own corner) | medium-high (no name; map order + the diff) |
| `LAYOUT_RUSTBORO_CITY_GYM` | 94 | map 11/3, 11x20, one map | high |
| `gTileset_General` | 0x083DF704 | primary of 239 layouts incl. 1, 3, 4, 10, 11, 20 | high |
| `gTileset_Petalburg` | 0x083DF71C | secondary of 7 layouts (1, 10, 11, 17-19, 442) | high |
| `gTileset_Rustboro` | 0x083DF734 | secondary of 8 layouts (incl. 4, 20) | high |

Fingerprints (FNV-1a-32 of blockdata bytes): L1 CA6DFAA0, L3 6FFC5818, L4 A55404CF, L10 EFE99674,
L11 52C922B6, L20 157E3492, L54 74435B94, L55 EB9E8528, L56 933D5B5E, L57 EC70A737, L58 B02DC393,
L432 E9140827, L59 0E2EFCEE, L60 1DF7BAB3, L61 BBF5FE0D, L62 2C4488F9, L63 13A673F9, L71 7A6744BC,
L94 C053E45E.

Layout 442 is a byte-identical copy of layout 1 (0 differing cells, S0 `altOf = 1`). Upstream places
models there too (find_placements scans every layout, gen:1090), so the C port does the same; it adds
one page (section 7).

### 3.2 Data table or per-spec functions: **per-spec C builder functions behind a const descriptor table**

Why not pure data: the part lists are parametric code, not records. `littleroot_house(plaster_x)`
(sp:46-96) derives the upper storey from `lower.slope_point(7)` (a HipRoof method), computes
`zb_hi = (lower.zf + lower.zb) - zf_hi`, and feeds those into the next parts; `center_or_mart`,
`kit_house(width)`, `flat_block(...)`, `stone_block/olive_block(w, h, meta)` and `gym()` do the same.
A data table would have to precompute those doubles (losing the derivation and the float order) or
grow an expression language. Why not one big generated table either: the numbers must stay readable
next to the upstream lines for review.

Why not all code: the descriptor fields (name, layout, rect, ground, match_rows, exact, kind) are
plain records that the expanders, find_placements and the tests iterate. So:
- `rg_specs[]` (const, 34 entries in sp:1092-1375 order) holds the record fields and a builder pointer
  plus up to two int arguments (the lambda's arguments).
- One C function per upstream builder, in `rg_bspecs.c`, written statement for statement against the
  Python (same operations in the same order, so H1-H4 hold): `littleroot_house(px)`,
  `littleroot_lab()`, `center_or_mart(r0, r1, crown)`, `oldale_house()`, `briney_house()`,
  `flower_shop()`, `kit_house(width)`, `gym()`, `flat_block(...)`, `stone_block(w,h,meta)`,
  `olive_block(w,h,meta)`, `flat_part(...)`, `devon()`, `fountain()`.
- Interior rooms are **const data**: each room's piece list (sp:593-1090) is a const array of
  `RgPieceDef` built with C99 designated initialisers that mirror `piece()` / `facet()` keyword
  arguments one for one (sp:593-626); the four helper families that compute pieces
  (`center_walls(front)`, `_replace`, `house_chair`, `stairwell`, `potted_plant`, `gym_block`,
  `gym_statue`) are small C functions that append to the room's list, called in the same order.
- Colour constants are BGR555 (section 1.1). Every hex string is converted by a host test from the
  upstream literal and compared, so a transcription slip fails a test.

### 3.3 The 34 specs (sp:1092-1375), with layout and art rectangles

Art is the rect's cells x 16 px (`building_art`, vb:161-196). Exact rects are art pixels
(x0, y0, x1, y1[, behind]).

| # | name | kind | layout | rect (cells) | art px | ground | match_rows | builder | exact |
|---|---|---|---|---|---|---|---|---|---|
| 1 | littleroot_house_w | direct | 10 | (2,4,5,5) | 80x80 | 0x001 | - | littleroot_house(8) | HOUSE_EXACT (2,54,80,80) (12,45,68,54) (10,38,70,45) (12,16,68,38) (sp:99) |
| 2 | littleroot_house_e | direct | 10 | (13,4,5,5) | 80x80 | 0x001 | - | littleroot_house(64) | HOUSE_EXACT |
| 3 | littleroot_lab | direct | 10 | (3,12,7,5) | 112x80 | 0x001 | - | littleroot_lab() | LAB_EXACT (2,53,112,80) (8,32,104,53) (48,16,104,32) (sp:152) |
| 4 | pokemon_center | direct | 1 | (19,13,4,4) | 64x64 | 0x001 | (1,4) | center_or_mart((9,16), crown) | CROWN_EXACT = CENTER_EXACT + (16,0,48,24,behind) (sp:202) |
| 5 | poke_mart | direct | 3 | (22,11,4,4) | 64x64 | 0x001 | (1,4) | center_or_mart((12,16)) | CENTER_EXACT (8,30,56,64) (0,38,64,64) (12,9,52,30) (sp:200) |
| 6 | oldale_house | direct | 11 | (4,4,4,4) | 64x64 | 0x001 | - | oldale_house() | (2,36,64,64) (8,14,56,36) (sp:240) |
| 7 | briney_house | direct | 20 | (15,47,5,4) | 80x64 | 0x001 | - | briney_house() | (1,39,80,64) (0,39,1,48) (8,16,72,39) (sp:289) |
| 8 | flower_shop | direct | 20 | (3,15,6,4) | 96x64 | 0x001, 0x206, 0x207 | - | flower_shop() | (0,4,96,38) (1,38,95,64) (sp:325) |
| 9 | kit_house_4 | direct | 1 | (9,16,4,4) | 64x64 | 0x001 | - | kit_house(64) | (2,38,62,64) (8,16,56,38) (sp:364) |
| 10 | kit_house_5 | direct | 1 | (5,2,5,4) | 80x64 | 0x001 | - | kit_house(80) | (2,38,78,64) (8,16,72,38) |
| 11 | gym | direct | 1 | (12,4,6,5) | 96x80 | 0x001 | (0,4) | gym() | GYM_EXACT (2,45,40,72) (72,45,94,72) (3,5,93,45) (40,41,72,80) (sp:406) |
| 12 | hedge | components | secondary Petalburg (7 layouts) | per run | per run | 0x001 | - | Relief (height 11) | (0,0,w*16,h*16) set by build_models (gen:920) |
| 13 | rustboro_stone | kit | 4: corner 0x224, top {0x225}, end {0x226}, foot 0x21c | per building | w*16 x h*16 | 0x2BB, 0x2C3, 0x001 | - | stone_block(w,h,meta) | (0,7,w,h) (sp:479) |
| 14 | rustboro_olive | kit | 4: corner 0x220, top {0x221,0x222}, end {0x223,0x23F}, foot 0x240 | per building | idem | idem | - | olive_block(w,h,meta) | (0,8,w,h) |
| 15 | gym_rustboro | direct | 4 | (24,15,6,5) | 96x80 | 0x2BB, 0x2C3, 0x001 | - | gym() | GYM_EXACT |
| 16 | railing | components | secondary Rustboro (8 layouts) | per 4x4 block | per block | 0x2BB, 0x2C3, 0x001 | - | Relief (height 12, hull 12, bridge 3, block 4, upper, flank 0x2A7) | (0,0,w*16,h*16) |
| 17 | sea_rock | props | every General-primary layout | per variant | per variant | 0x170 | - | Mound(rise 1.0, step 2, ring (222,230,238)) | (0,0,w*16,h*16), judged against `drawing` |
| 18 | sand_boulder | props | idem | idem | idem | 0x124 | - | Mound(1.0, 2) | idem |
| 19 | sea_stack | props | idem | idem | idem | 0x170 | - | Mound(1.6, 4, ring (131,131,139)) | idem |
| 20 | devon_corporation | direct | 4 | (7,7,10,9) | 160x144 | 0x2BB, 0x2C3, 0x001 | - | devon() | (0,8,160,128) (48,128,112,144) (sp:531) |
| 21 | rustboro_fountain | direct | 4 | (27,38,3,3) | 48x48 | 0x2BB, 0x2C3, 0x001 | - | fountain() | (0,0,48,48) |
| 22 | pc1f | interior | 61 | room | 224x144 | 0x202 | - | POKEMON_CENTER_1F, open CENTER_1F_OPEN | per piece (gen:559-874) |
| 23 | pc2f | interior | 62 | room | 224x160 | 0x202 | - | POKEMON_CENTER_2F, open CENTER_2F_OPEN | per piece |
| 24 | mart | interior | 63 | room | 176x128 | 0x201, shade 0x202 0x204 0x206 0x208 0x20A | - | MART, open MART_OPEN (sp:755) | per piece |
| 25 | brendan_1f | interior | 54 | room | 176x144 | 0x201 | - | brendan_1f() | per piece |
| 26 | brendan_2f | interior | 55 | room | 144x128 | 0x201 | - | brendan_2f() | per piece |
| 27 | may_1f | interior | 56 | room | 176x144 | 0x201 | - | may_1f() | per piece |
| 28 | may_2f | interior | 57 | room | 144x128 | 0x201 | - | may_2f() | per piece |
| 29 | lab | interior | 58 | room | 208x208 | 0x202 | - | lab() | per piece |
| 30 | lab_table | interior | 432 | room | 208x208 | 0x202 | - | table piece (128,64,176,89) h 8 leave LAB_SHADOW solid + lab()'s side_w, side_e | per piece |
| 31 | lavaridge_pc1f | interior | 71 | room | 224x144 | 0x202 | - | LAVARIDGE_CENTER_1F, open CENTER_1F_OPEN | per piece |
| 32 | house1 | interior | 59 | room | 160x144 | 0x223 | - | house1() | per piece |
| 33 | house2 | interior | 60 | room | 176x128 | 0x223 | - | house2() | per piece |
| 34 | rustboro_gym | interior | 94 | room | 176x320 | 0x201, shade 0x202 0x203 0x204 0x216 0x22f 0x237 | - | rustboro_gym() | per piece |

Notes on the table:
- Props objects (props:33-57) are const data: `sea_rock` General tiles ((141,142),(157,158)) pal 1;
  `sand_boulder` ((88,89),(104,105)) pal 3, `land`; `sea_stack` 4x4 grid with a None at (0,0) and the
  optional `(69, True, 0)` at (3,0), pal 3. One model per `(sx%2, sy%2, present)` group (gen:93-142).
- The props models carry `drawing` (the art before `with_ring`); the ortho check judges against it
  (gen:1621, `reference=`).
- `kit_house_exact(w)` = [(2,38,w-2,64), (8,16,w-8,38)] and `flat_block_exact(w,h,r)` = [(0,r,w,h)]
  (sp:364, 479), with w and h in pixels.
- Expanded model names keep upstream's formats: kit `"%s_%d_%d" % (name, x, y)` (gen:319); props
  `name`, then `name_<n>` (gen:132); interior pieces and `_bare` twins (gen:931) as upstream. Names
  are never written to the file. They are only used for reports and `--dump-model`.

---

## 4. `buildings.bin` byte layout (writer gen:1293-1451, cross-checked against `vbuild.c`)

All integers little-endian. The writer produces upstream's layout exactly; every field is listed with
the consumer line that reads and validates it. Offsets are file offsets; the file starts at 0, so the
consumer's `ftell`-based pad (vbuild.c:226-228) and the writer's `(-fixed) % 4` (gen:1434-1436) agree.

| # | block | size | fields | writer | consumer read / validation |
|---|---|---|---|---|---|
| 1 | header | 24 | `"VXB7"`, u16 pages, u16 models, u16 pageModels, u16 placements, **u16 heightBytes**, u16 masks, u32 vertices, u16 variants, u16 0 | gen:1413-1415 (`"<HHHHHHIHH"`) | vbuild.c:182-193: magic memcmp; counts read; the trailing u16 is ignored |
| 2 | page table | 8 x pages | u16 w, u16 h, u32 texel offset | gen:1439-1444 | vbuild.c:122-128 TakePage: no validation at Init; ReadPage (vbuild.c:301-319) checks `first + count <= w*h`, seeks `offset + first*2` |
| 3 | models | 16 x models | u8 w, u8 h, u16 ground, u32 firstVertex, u32 vertexCount, u32 heights | gen:1343 (`"<BBHIII"`) | vbuild.c:130-141 TakeModel: `firstVertex + vertexCount <= vertices` and `heights + w*h <= heightBytes` |
| 4 | page models | 8 x pageModels | u16 model, u16 page, i16 ox, i16 oy | gen:1390-1395, 1420 (`"<HHhh"`) | vbuild.c:143-151: `model < models && page < pages` |
| 5 | placements | 16 x placements | u16 layout, u16 pageModel, u16 x, u16 y, u16 ground, u16 extraCount, u32 extraFirst | gen:1405, 1406, 1422 (`"<HHHHHHI"`), sorted | vbuild.c:153-165: `pageModel < pageModels && extraFirst + extraCount <= vertices`; binary-searched by layout (vbuild.c:322-348), so **sorted by layout is required** |
| 6 | heights | heightBytes | u8 per model cell (pixels, 255 = cell not owned) | gen:1346-1347, 1423 | vbuild.c:212; `sMaxTop` from bytes != 0xFF (vbuild.c:232-235) |
| 7 | pad | heightBytes & 1 | 0 | gen:1424-1425 | vbuild.c:214 |
| 8 | footprints | 2 x heightBytes | u16 mask index or 0xFFFF | gen:1426 | vbuild.c:217; each must be 0xFFFF or `< masks` (vbuild.c:223-225), else the whole file is rejected |
| 9 | masks | 32 x masks | 16 u16 rows, bit x of row z | gen:1427-1428 | vbuild.c:218 |
| 10 | quarters | heightBytes | u8 per model cell (bit 2*row+col) | gen:1348-1349, 1429 | vbuild.c:219 |
| 11 | pad | heightBytes & 1 | 0 | gen:1430-1431 (`len(quarters) % 2`, the same value) | vbuild.c:220 |
| 12 | variants | 6 x variants | u16 layout, u16 metatile, u8 quarters, u8 0 | gen:1432-1433 (`"<HHBB"`) | vbuild.c:221; used by CellAt OWN_GROUND (vbuild.c:422-450), cap VOXEL_VARIANTS 128 (voxel_atlas.h:69) |
| 13 | pad | to a multiple of 4 | 0 | gen:1434-1437 | vbuild.c:226-228 |
| 14 | vertices | 24 x vertices | f32 x/16, y/16, z/16, u, v, shade | gen:1338-1341, 1398-1404, 1438 | vbuild.c:231 (VoxelVertex, 6 f32); model tris in steps of 3 vertices, patches in steps of 6 (`a,b,c,a,c,d`) (vbuild.c:471-553) |
| 15 | texels | sum of 2*w*h per page | RGBA5551 u16, PICA 8x8 Morton tiled | gen:1378-1386, 1446 | ReadPage (vbuild.c:301-319) + VoxelGrade_Texels |

Note: row 1 widths: `heightBytes` and `masks` are **u16**. The writer must fail cleanly (not truncate)
when `heightBytes > 65535`, `masks > 65535`, `models`, `pageModels`, `placements > 65535`, a model
`w` or `h > 255`, any `ox/oy` outside i16, or `pages > 256` (gen:1410). The C writer checks each
before writing a byte and returns `RG_ERR_TOO_BIG` with the field named.

The **texel offset** in the page table is absolute (head + table + body + earlier pages,
gen:1439-1444). The C writer computes the total size first (two passes: `out == NULL` sizes, as the S1
writers do) so offsets are known before the table is written.

### 4.1 Field semantics (what the writer must compute)

- **Model `ground`** = `spec["ground"][0]` (gen:923). **Placement `ground`** = the commonest
  non-blocked (`cell & 0xC00 == 0`, the raw u16 including collision bits) metatile in the 1-cell ring
  around the placement inside the layout, first maximum in scan order (y outer, x inner), else the
  model's ground (gen:1143-1153); `RG_OWN_GROUND` 0xFFFF for props (gen:1094-1097); the model ground for
  interior pieces (gen:1141); for `reused_at` entries with `ground None` the commonest non-blocked
  metatile of the whole room, with every cell patched (gen:1166-1176).
- **Heights**: `cell_heights` (gen:978-1006): per triangle, cells within its xz bounds +-2*EPS; clip the
  triangle to the cell (x >= cx*16, x <= cx*16+16, z >= cy*16, z <= cy*16+16, in that order); if >= 3
  points and `_area_xz > 1e-6`, top = max(top, max y). Byte = `min(255, nearbyint(top))`, and 255 for a
  cell not in `owned` (gen:1346-1347).
- **Footprints** (gen:1014-1051): none for interiors; triangles with max y < 1.0 or |area| < 1e-6 skipped;
  pixel centres (px+0.5, pz+0.5) inside either winding (all three edge functions >= 0 or all <= 0);
  per cell `0 < n < 240` gives a mask, deduplicated by value in first-seen order (gen:1350-1358).
- **Quarters**: `spec["quads"]` per cell (props only), else 0 (gen:1348-1349).
- **Page per layout** (gen:1360-1405): layouts in ascending id; `used` = sorted distinct model indices
  placed there; images = each used model's art cropped to its bbox, then every distinct patch image
  (dedup by bytes, insertion order); packed by `pack_atlas` (stable sort by (-h, -w); first size in
  (area, h) order whose skyline packing fits; best = lowest y, then leftmost x; strict `<`);
  page-model `ox, oy` = spot minus the crop's top-left (so uv in model pixels map onto the page).
- **Texel** = `(r>>3)<<11 | (g>>3)<<6 | (b>>3)<<1 | (a >= 128)` (gen:1385) written at
  `texel_offset(x, y, tw)` (gen:1250-1255). Since `(c5*255//31) >> 3 == c5` for every c5, the C writer
  packs straight from BGR555: `r5<<11 | g5<<6 | b5<<1 | a`. Unused texels stay 0.
- **Patch quads** (gen:1398-1404): for each placement in `found` order, each patch cell (i, j) emits
  `a,b,c,a,c,d` with a = (i, 0.01, j, ox, oy, 1.0), b = (i+1, 0.01, j, ox+16, oy, 1.0),
  c = (i+1, 0.01, j+1, ox+16, oy+16, 1.0), d = (i, 0.01, j+1, ox, oy+16, 1.0). These are **not** /16
  (cell units already) and uv are **page** pixels; the consumer applies offset 0 for patches
  (vbuild.c:471-553).
- **Model vertices** (gen:1338-1343): `x/16, y/16, z/16, u, v, shade` per triangle corner, in mesh
  order; uv are model-art pixels, mapped by the consumer as `(ox + u)/pageW`, `1 - (oy + v)/pageH`.
- **Variants** (gen:1454-1482): for each model in order, for each `(lid, px, py)` in `spec["at"]`, for
  each `(i, j), q` in `quads` (dict insertion order) with the cell inside the layout: metatile `mt`,
  tileset = primary if `mt < 512` else secondary; key `(tileset, mt, q)` keeps the first lid; output
  sorted by `(lid, mt, q)`; more than 128 fails the file.

### 4.2 Consumer validation summary (what a malformed file does on device)

`VoxelBuildings_Init` (vbuild.c:167-251) rejects the whole file (logs `truncated or malformed`, falls
back to extruded houses) on: wrong magic, any short read, a model or placement row that indexes past
its table, a footprint index >= masks. It does **not** validate: page sizes or texel offsets (only at
`ReadPage`), placement sort order (a mis-sorted file silently loses placements in the binary search),
variant layout ids, vertex values. So the host round-trip test (section 5.4) must check exactly those:
sorted placements, `offset + 2*w*h <= fileSize` per page, POT page sizes within 512x512, and finite
vertex floats.

---

## 5. The model checks, and how they run

There is no reference `buildings.bin` (producing one means running upstream, which is forbidden). The
three upstream gates (gen:1596-1638) are the oracle: they prove each model reproduces its drawing. They
are ported first (slice S2.2) and every later slice is accepted through them.

### 5.1 Raster (vb:1538-1587), shared by all three

- Buffers: colour RGB (init `bg`), depth **double** (init -1e30), owner (init "none", int16 tag id).
- `draw(verts, tex, shade, owner)`: verts are (sx, sy, depth, invw, u, v); `area` from the screen
  triangle; skip if `|area| < 1e-9`; bbox `max(0, floor(min))` .. `min(w-1, ceil(max))` inclusive;
  pixel centre (px+0.5, py+0.5); barycentrics `a`, `b` exactly as vb:1566-1567 (`* inv`, `inv = 1/area`),
  `c = 1 - a - b`; reject if any < -1e-7; `d = a*d0 + b*d1 + c*d2` (left to right); **skip if
  `d <= depth`** (so on a tie the first triangle drawn wins: triangle order is part of the contract);
  u, v interpolated and divided by `iw`; texel `floor(u), floor(v)`, skipped outside the texture or
  alpha < 128; write depth, owner, colour `(int)(r*shade)` per channel (truncation, after the double
  multiply).
- The projection everywhere: screen x = X, screen y = Z - Y, depth = Y + Z (vb:1617-1619).

### 5.2 `ortho_check(model, exact, reference)` (vb:1595-1662)

- Raster `(W + 64) x (H + 32)`, bg (0,0,0), margin M = 32 on left, right and top.
- Draw every triangle in mesh order except those whose tag contains `~depth` or `~behind`, at
  `(x + M, z - y + M, y + z, 1, u, v)`, textured with the model's own `art`.
- Judge against `reference` if given (props: the art before `with_ring`), else `art`. Regions = `exact`
  (default the whole art). For each art pixel (x, y) of each region (overlapping regions count twice,
  as upstream): if reference alpha >= 128, then `missing` if no owner at (x+M, y+M), `wrong` if the
  colour differs from the reference RGB; else, if something was drawn and the region is not `behind`,
  `extra`.
- Bare twins are judged with art whose pixels outside `own` are cleared (gen:1609-1617).
- **Asserts: wrong = missing = extra = 0 for every model.**

### 5.3 `density_check(model)` (vb:1665-1716)

- For every triangle not tagged with a suffix `~clamp` or `~proj`: normal via cross product,
  `nl = sqrt(pysum(n*n))`, skip if < 1e-9; in-plane horizontal `h` and fall line `f` (horizontal if
  `|n.y| > 1 - 1e-9`); `beta = asin(min(1, sqrt(nx^2 + nz^2)))`, `expect = cos(beta) + sin(beta)`;
  solve the uv Jacobian; `along = hypot(dh)`, `down = hypot(df)`, `shear = |dh . df|`; skip if
  `|det| < 1e-9`.
- **Asserts: no triangle with `|along - 1| > 1e-3`, `|down - expect| > 1e-3` or `shear > 1e-3`.**
  (One texel per world pixel along the eave; down the slope the GBA's `cos + sin` foreshortening.)

### 5.4 `room_check(models, layout)` (gen:1518-1593)

- Raster `W x H` = the room's pixels, bg (0,0,0). Collect every placement of every model in this layout
  (via find_placements, in model order): `covered[(px+i, py+j)] = ground` over the whole rect, the
  placement's patches, and the model.
- Terrain: every cell drawn as two triangles `(q0,q1,q2), (q0,q2,q3)` with `cell_image(covered or own
  metatile)`, depth `Y + Z`; then the patches, depth `+0.01`; then each model's triangles except `~depth`
  ones (note: `~behind` **is** drawn here), offset by (px*16, py*16).
- Compared with the room's own drawing (`cell_image` of every cell, RGB), skipping pixels inside the
  spec's `open` polygons (`_inside`, gen:327-351: even-odd polygon test at pixel centres).
- **Asserts: 0 differing pixels for each of the 13 interior rooms** (layouts 54-63, 71, 94, 432).

### 5.5 Where the checks run

- **Host tests (always).** `test/host/test_romgen_buildings.c`, built with the line of
  `test/host/test_romgen_signs.c:5-15` (the full vendored voxel set including `voxel_building.c`,
  `-DVOXEL_HOST_FILES -DCTR_VOXEL_LIGHTING=1`, ASan/UBSan, `-ffp-contract=off`, `-lm`). Synthetic tests
  run without a ROM; the real-ROM tests run when `ROMGEN_ROM` points at `roms/emerald.gba` and skip
  (counted, not failed) otherwise, as the S1 tests do.
  - Synthetic: clip/triangulate/tile_pieces vectors; a Prism of one known tile must give 0/0/0 and an empty
    density list; a deliberately shifted uv must give `wrong > 0`; a triangle with a doubled texel
    density must be listed; a two-triangle depth tie keeps the first; Raster colour truncation
    (`int(255*0.72) == 183`); `rg_pysum` in both modes against hand vectors; `nearbyint(2.5) == 2`;
    texel_offset against the Morton formula for every (x, y) of an 8x8 tile; pack_atlas on a hand set
    (stable ties, chosen size, spots).
  - Real ROM, per model: 0/0/0 ortho, empty density list, triangle count printed and pinned in the test
    once S2.7 passes (a later change that moves a count must update the pin knowingly).
  - Real ROM, per room: room_check == 0.
  - Real ROM, invariants from the upstream docstrings: `littleroot_house_w` is found in layout 10 at (2,4) and `_e` at
    (13,4) (any further placements are listed and pinned, not assumed absent); their meshes have equal triangle counts and identical geometry
    except the u of faces textured from the plaster column; the Pokemon Center model (#4) is placed in
    every layout whose rows 1-3 of the rect match (at least Petalburg and Oldale, the docstring's
    "canonical copy" and "Oldale paints its path"); `house1` is placed in 9 layouts' worth of maps
    (one layout, 59) and `house2` likewise (60); `lab_table` is placed only in 432; hedges only in
    Petalburg-secondary layouts; seam_art's "no two south cells share a column" (section 2.3);
    "no out-of-layout cell claimed by two connections" (section 2.3); every RGB888 colour constant
    has a BGR555 preimage.
  - Round trip through the consumer (PHASE.md validation 1): write `buildings.bin` into a `mkdtemp`
    dir with a `voxel/` subdir and `chdir` (as test_romgen_signs.c does), then `VoxelBuildings_Init()`
    must succeed; counts equal the writer's stats; for every page `VoxelBuildings_ReadPage(p, 0, w*h)`
    returns the writer's texels; for every placement, `VoxelBuildings_CellAt` / footprint / variant
    accessors return the writer's values at a sample of cells; `EmitSome` over a placement's rect emits
    `vertexCount/3` model triangles plus `extraCount/6` patch quads with uv inside [0,1]. Plus the
    checks the consumer does not do (section 4.2): placements sorted by layout, pages within the file,
    finite floats.
  - Device parity (H3): the device build's `buildings.bin` (S2.8) copied back is compared with the host
    one: all bytes outside the vertex block equal; vertex floats within 1e-5.
- **On device.** The gates cost one raster per model and per room (section 6.2). Default: run them, and
  **drop** any model that fails (log its name and numbers into `RgOutput`), instead of upstream's
  abort-the-file (gen:1636-1638). Rationale: a dropped model falls back to the extruded box for that
  building only; an aborted file loses every building. For the tested dump (SHA-1
  `f3ae088181bf583e55daf962a92bb46f4f1d07b7`) the gates are proven on the host, so S4 may skip them on
  device when the cache key matches, if S2.8's timing says they are expensive. A spec whose layout
  fingerprint does not match (section 3.1) is skipped before any work.

---

## 6. Slices, exit criteria, and the on-device budget

Each slice ends green: host tests pass under ASan/UBSan, `make -C tools/romgen` builds, the device build
(`make`) links, and the slice is committed on its own. "Gate" below = ortho 0/0/0 + density empty
(+ room 0 for rooms).

### 6.1 Slices

| slice | content | exit criteria |
|---|---|---|
| **S2.1** S0 additions + images | G1-G4 (`rg_cell_px`, `rg_subtile_px`, `rg_metatile_entries`, `rg_c5_to_8`) in rg_art/rg_world; `rg_bimg.{h,c}` (RgImage, bbox, crop, paste, hash, `rg_cell_image`, `rg_building_art`) | `rg_cell_px` equals `rg_layer` wherever `rg_layer` has a drawn pixel, and fills idx-0 lower pixels with palette slot 0 (synthetic tileset fixture); out-of-range tile gives (31,0,31); real ROM: `rg_building_art` of Littleroot (2,4,5,5) is 80x80 with alpha only 0/255 and transparent pixels all (0,0,0,0); colour-constant table converts |
| **S2.2** primitives + checks | `rg_geom` (Mesh, clip, triangulate, tile_pieces, Proj/Tile/Band value types, Prism, Strip, strip_face, HipRoof, Frustum, Vault, Walls, Card, Facet, PlainWall, Decal, Cylinder, Lifted; `rg_pysum`, `rg_stable_sort`, floor-div helpers) and `rg_bcheck` (Raster, ortho, density) | every synthetic test of section 5.5; each part type has one synthetic model that passes the gate and one perturbed copy that fails it. Relief and Mound are deferred to S2.5 (they only serve components and props) |
| **S2.3** Littleroot house end to end | `littleroot_house(px)` builder; descriptor rows 1-2; `rg_build_models` for direct specs; `cell_heights`, `cell_footprints`; `find_placements` for direct specs (ring ground, same_building_pixels); `pack_atlas`, `texel_offset`; `rg_buildings_write` for this subset | both houses pass the gate; a two-model VXB7 parses through `VoxelBuildings_Init` and passes the round-trip test (section 5.5); placements at (2,4) and (13,4) in layout 10; heights are in 1..255 inside the rect, 0/255 rules hold; the two meshes differ only in plaster-column u |
| **S2.4** all direct specs | builders for lab, center_or_mart (both), oldale, briney, flower_shop, kit_house (both), gym (both copies), devon, fountain, flat_block/flat_part | all 14 direct models pass the gate; Center placed in Petalburg and Oldale; gym placed in layouts 1 and 4 (`gym` and `gym_rustboro` are separate specs with separate refs); file round-trips |
| **S2.5** components, kit, props | Relief (+ `_subtract`), seam_art, pick_side, flank_band, piece_spec, component_specs; kit_specs + stone/olive_block; Mound (with_ring BFS, _spans) + props `find` / `cells_of` / `beyond` + prop_specs | every hedge, railing block, kit building and props variant passes the gate (props judged against `drawing`); seam_art column assert; connections assert; props tie-break divergence logged once; variants <= 128 |
| **S2.6** interiors + reuse | `_inside` / `_inside_grid`, interior_specs (all piece kinds), the 13 room data tables, reuse_pieces / register_piece / reuse_everywhere, bare twins, room_check | every piece and bare twin passes the gate; room_check == 0 for all 13 rooms; reused pieces found in unmodelled rooms are placed with whole-room patches |
| **S2.7** full export | `rg_buildings_write` complete (pages per layout incl. 442, patches, masks, quarters, variants, size guards, two-pass sizing); `rg_run` `wantBuildings`; stats | full real-ROM file round-trips through the consumer; all section 4.2 extra checks pass; counts printed and pinned (models, pages, placements, vertices, masks, variants); both `RG_PYSUM_COMPENSATED` modes built and compared (differing bytes reported) |
| **S2.8** CLI, timing, device | `--only buildings`, `--dump-model`, `--time`; host timing; a device test hook (S4 owns the UI) that runs the generator on the New 3DS and writes the file + timings to `sdmc:/3DGBA/voxel/` | host: `romgen ROM OUT --only buildings --time` prints per-phase ms; device: file produced, timings recorded in the buildlog, device-vs-host parity (H3); the in-game check: Littleroot shows two modelled houses (PHASE.md validation 3) |

### 6.2 On-device memory and time budget

Memory (peak, target < 8 MB; New 3DS app heap is far larger, but S4 runs inside the emulator process):

| item | size | rule |
|---|---|---|
| ROM | 0 (mGBA's buffer) | never copied (S0) |
| RgWorld arena | ~60 KB (SPEC-S0-S1.md 1.7) | shared with S1 |
| one RgPair | ~1.15 MB worst case (SPEC-S0-S1.md 1.7) | **one open at a time** (S0 rule). Three pair-grouped passes: (1) build every model with its reference layout's pair; (2) find_placements + patches + same_building_pixels per candidate layout's pair, storing upstream sort keys (section 2.4); (3) room_check per room. Order restored by sort |
| model art | <= 92 KB per direct model (160x144 RGBA8 for Devon), interiors larger per piece | after a model's gates and placements, keep it **cropped to its bbox as RGBA5551** (lossless, section 4.1; same_building_pixels and ground_patch need only RGB555 equality and alpha >= 128); estimate 2-4 MB in RGBA8, about 1-2 MB as 5551 |
| meshes | 136 B per double triangle | convert each model's mesh to its final f32 vertex block once its heights, footprints and gates are done, then free the doubles; peak = one model's doubles + the f32 list |
| f32 vertices | 24 B x vertices | unknown until S2.7; upstream's consumer mallocs the whole block on device, so the count is bounded by what the game already loads |
| one page | <= 512 KB (512x512x2) | built and appended one at a time |
| raster | <= 1.1 MB (largest room) | freed after each check |
| output blob | file size | the writer sizes first, then fills one malloc |

Time (estimate; nothing measured yet): host C is expected to be well under 1 s (SURVEY-S2-S3.md:228-236:
find_placements <= 20 M simple ops; geometry negligible; atlas < 0.5 s). On the 804 MHz ARM11 with VFPv2
doubles the SURVEY estimate is 1-3 s without gates. Gates add one raster per model and per room:
roughly (sum of model art areas + 13 room areas) x per-pixel cost x triangle overlap, which is a few
million pixel tests, so seconds, not minutes. **Budget: buildings <= 10 s on device including gates.**
If S2.8 measures more, the order of retreat is: skip gates for the tested SHA-1 (section 5.5), then the
PHASE.md PC fallback (the same C, host CLI, writing to SD).

---

## 7. Open points, recorded rather than guessed

### 7.1 Ambiguities and divergences

| # | point | what we do | how it is caught |
|---|---|---|---|
| A1 | `sum()` mode: upstream is interpreter-dependent (>= 3.11 supported, CI on 3.12) | Neumaier (3.12) by default, naive behind `RG_PYSUM_COMPENSATED=0` | S2.7 builds both and reports differing bytes; the gates must pass in both |
| A2 | props group tie order uses layout **names** (gen:102, 107) | tie-break by layout id; `where[0]` = lowest id | logged once; affects model order and the `_n` suffixes, never geometry (same art, General primary) |
| A3 | `seam_art` iterates a frozenset (gen:174-228) | treated as order-neutral | real-ROM assert: no two south cells share a column; if it fires, port CPython set order (S3's helper) |
| A4 | libm ulp differences host vs device (H3) | call libm | gates on both; host-device parity test (floats within 1e-5, all other bytes equal) |
| A5 | `LAYOUT_..._LAB_WITH_TABLE` = 432 by structure (no name in the ROM) | 432 | fingerprint + the cell diff (only the table cells differ from 58) |
| A6 | `LAYOUT_LAVARIDGE_TOWN_POKEMON_CENTER_1F` = 71 by map order | 71 | fingerprint + the cell diff vs 61; room_check == 0 is a strong confirmation (a wrong room would not compose) |
| A7 | layout 442 (byte copy of Petalburg) gets its own page and placements, as upstream would | keep | page count pinned in S2.7 |
| A8 | props `connections()` order comes from `os.listdir` (props:99-101) | `rg_map_connections` order | assert no out-of-layout cell is claimed by two neighbours |
| A9 | upstream aborts the whole file on any failed gate (gen:1636-1638) | device drops the failing model only | `RgOutput.buildingsFailed`; host tests require 0 failures |
| A10 | a spec's fingerprint mismatch (hack / other revision) | skip that spec, log it | S4's cache key is the SHA-1, so a skipped spec is visible in the log, never silent |
| A11 | upstream `entries()` returns None for an out-of-range metatile (props:80), but `cell_image` on such a metatile would use whatever the arrays hold | G3 returns false; `rg_cell_px` of an out-of-range metatile is all magenta idx 0 | synthetic test |
| A12 | SURVEY said 18 layouts named; the specs name **19** distinct layouts (432 was missed) | 19 | section 3.1 table |
| A13 | `MAX_VARIANTS` 128 is a hard cap shared with the atlas (voxel_atlas.h:69) | fail the file when exceeded, as upstream | the count is pinned in S2.7 |
| A14 | the trailing u16 of the header is written 0 and never read (vbuild.c:182-193 reads the header up to offset 22) | write 0 | round trip |
| A15 | `heightBytes` is u16: the total model cells must stay <= 65535 | `RG_ERR_TOO_BIG` | S2.7 prints the total |
| A16 | `town_preview`, `preview`, `Camera`, `render_scene` (gen:952-975, 1485-1515; vb:1719-1817) are debug renderers | not ported | n/a |
| A17 | upstream's `--only` filters model names by prefix (gen:894); ours selects output files | `--dump-model NAME` covers the debug use | n/a |

### 7.2 What the S0 API lacks (to add in S2.1)

- **G1** full-pixel metatile layers: `rg_layer`/`build_layer` drop palette-index-0 pixels and missing
  tiles, but `building_art` and `cell_image` draw lower-layer index 0 as palette slot 0's colour and a
  missing tile as magenta. Needed: `rg_cell_px(pair, metatile, layer, RgCellPx*)`.
- **G2** raw subtile by (tile, palette), no flips, for props art and flank_band:
  `rg_subtile_px(pair, tile, pal, c[64], idx[64])`.
- **G3** raw metatile entries (8 u16) for props `upper_subtiles` and kit scans:
  `rg_metatile_entries(world, layout, metatile, out[8])`. The bytes are in `RgTileset.metatiles`; there is
  no helper that applies the primary/secondary split.
- **G4** BGR555 -> RGB888 (`c5*255//31`) for the specs' hex colour constants.
- **G5** layout and tileset **names** do not exist in the ROM: replaced by the pinned id + fingerprint
  table (section 3.1). Not an API addition, a data table in `rg_bspecs.c`.
- **G6** connections exist (`rg_map_connections`), but upstream works per layout; a small helper
  `rg_layout_neighbours(world, layoutId, out[])` that maps the map-level connections to
  (neighbour layout, dx, dy) as props:93-121 does (dedup per map, `b != a`) avoids re-deriving it in S2
  and S3 (S3's relief uses the same props code).

### 7.3 Top risks

1. **Spec transcription volume.** 1375 lines of parametric geometry (and ~500 lines of interior piece
   data) transcribed by hand. The gates catch a wrong number only where an `exact` rect or a room covers
   it; deep geometry outside `exact` (roof courses behind the drawing) can be wrong and still pass.
   Mitigation: per-statement review against sp:N, pinned triangle counts, the plaster-column invariant.
2. **Relief and interior expanders** (gen:145-288, 390-874) are the least specified parts (order-sensitive
   floods, reuse passes, art composition with offsets `uo = 2*h*16 + sh + th + ch`). They carry most of the
   set-order hazards (section 2.3). They are slices S2.5-S2.6 so the simple path ships first.
3. **No oracle file.** Byte identity with upstream cannot be shown; acceptance is gates + invariants +
   consumer round trip + host-device parity. A behaviour difference that keeps every gate green (e.g. a
   placement ground chosen differently) would only show in-game (PHASE.md validation 3).
4. **Floating-point fidelity** (H1, H3): ulp-level vertex differences are harmless to the renderer but
   could flip an EPS test in clip/tile_pieces on device; the device-gate run in S2.8 is the check.
5. **Device time** of the gates and find_placements over 239 General-primary layouts is an estimate
   only; the retreat order is in section 6.2.

---

## Appendix A. Working citation notes (from the reading pass)

- vb = scripts/voxel_building.py, gen = scripts/gen_voxel_buildings.py, sp = scripts/voxel_building_specs.py.
- vb:68-79 constants: NUM_PRIMARY 512, EPS 1e-6, shades ART 1.0 WEST .80 EAST .72 BACK .66 WOUND .90.
- vb:161-196 building_art; vb:198-216 cell_image (opaque black bg, alpha 255 everywhere).
- vb:296-314 clip (Sutherland-Hodgman, EPS-inclusive), vb:317-354 triangulate (ear clip, k-1 wraps to last,
  guard 10000; area via sum() -> only sign used), vb:357-394 tile_pieces (floor/ceil with +-EPS).
- consumer: voxel_building.c:167-251 parse; heightBytes u16 (<= 65535 model cells total); padding after
  heights only if odd; second pad after quarters only if odd; pad to 4 via ftell.
- vb:410-501 emit_prism (CCW required else raise; edge facing = nz+ny; skip ny<-0.5 or facing<=EPS when no material;
  Proj cuts at lo/hi, clamp v = lo+0.5 / hi-0.5; caps via triangulate + clip per band; W shade .80 E .72).
- vb:504-530 emit_cap_piece (s_back = band.length - bw, length = z0-z1 or 1e9; flip xor side=="w").
- vb:535-611 Strip.segments/u_pieces; vb:614-645 strip_face (dens = |t.y| + hypot(t.x,t.z); s,t via sum() of 3
  products -> CPython>=3.12 compensated-sum hazard; "~clamp" tag when const); vb:648 _unit (sqrt(sum)).
- vb:653-746 HipRoof (z_rf = (zf+zb)/2 + cd/2; rise = tan(radians(pitch))*(zf-z_rf); 4 fascia, 4 slopes, ridge
  teeth/cap halves, ridge ends). vb:749-771 _plan_normal / inset_plan. vb:774-827 Frustum (y0=-1; drawn nz>0.1;
  diag |nx|>1e-6; shade W if nx<-0.3, E if nx>0.3 else BACK). vb:830-855 Vault. vb:858-874 Walls.
- vb:877-1125 Relief (runs/hull/bridge/foot/seam; emit top/riser/top~depth/front/back/flanks; _subtract vb:1128).
- vb:1145-1165 Lifted (+base on y and z). vb:1168-1383 Mound (ring_pixels set = membership only; with_ring BFS
  dict-order flood: FIFO by level, neighbour order +x,-x,+y,-y, first writer wins -> C must reproduce exactly;
  _spans straightness 0.5; emit uses sqrt/cos/sin/atan2 -> libm ulp hazard; _tri degenerate <1e-12 dropped, winding
  flip). vb:1386 Card, 1415 Facet, 1432 PlainWall (SHADE_WEST), 1453 Decal (lift .5), 1474 Cylinder (sides 16,
  nz>0.2 projected, ua = u0 + (k*3) % max(1,int(u1-u0-w))).
- vb:1520 Model emits parts in list order. vb:1538-1587 Raster (barycentric -1e-7, depth y+z larger nearer, ties keep
  first, colour int(c*shade), alpha>=128). vb:1595-1662 ortho_check (margin 32, raster (W+64)x(H+32), skip tags with
  "~depth"/"~behind", regions rect[,behind]). vb:1665-1716 density_check (tol 1e-3, skip tags ending ~clamp/~proj).
- gen:38-90 component_specs (DFS todo.pop, block cut via dict insertion order, merge identical (layout,pattern) ->
  repeat_at). gen:93-142 prop_specs (groups sorted by -len stable; Mound.with_ring; quads OR). gen:145-171 piece_spec
  (owned set; south/north/east/west frozensets, sorted where they become pattern). gen:174-228 seam_art: ITERATES the
  `south` frozenset and writes rows below per column -> order matters only if two south cells share a column i.
  gen:231-267 pick_side (widths 8,6,4; strict > so first best; fallback 4 x height block max count, strict >).
  gen:270-288 flank_band (rows = height+2). gen:291-324 kit_specs (corner/top/end/foot scan; dup grids skipped; meta
  unit = 0x222 in top row and last == 0x223). gen:327-378 _inside/_inside_grid (pixel centre; ellipse <=1; rect
  half-open; even-odd polygon; bounds +-2).
- gen:390-539 reuse (cell_keys = cell_image bytes; reuse_pieces exact pass then loose pass; LOOSE_PIXELS 64; taken
  pixels first wins; register_piece cells dict built by iterating a SET -> anchor = max(...) tie order is set-order,
  but the set of loose starts it yields is anchor-independent (proof: _drawn_at re-checks every cell) -> neutral).
  gen:542-556 reuse_everywhere (indoor = not outdoor, not modelled, both tilesets non-NULL).
- gen:559-874 interior_specs (match/shaped/ground/open; ground flood DFS = reachability; shade colours; owner -1 for
  reused; leave flood -> `left`; claims PNG = drop; walls/fill period; rects; mark_under; decal_of setdefault; per
  piece art layout: [obj h*16][marks h*16][side sh][top th][card ch][under h*16] uo = 2*h*16+sh+th+ch; facet/card/
  relief/Lifted; PlainWall sides "~behind" when no mine; Decal _floor (v_offset h*16), _under~behind (uo, lift .25)).
- gen:877-939 build_models (expansion in SPECS order, then reuse_everywhere, then per spec; bare twin drops
  Decal *_floor parts; twin.own).
- gen:978-1006 cell_heights (clip to cell, _area_xz > 1e-6, max y, int(round) half-even, min 255).
- gen:1011 FOOTPRINT_FULL 240; gen:1014-1051 cell_footprints (skip tri with max y < 1.0; |area|<1e-6 skip; pixel
  centre inside either winding; interior models -> all None; 0<n<240 -> mask).
- gen:1063-1179 find_placements. gen:1191-1206 same_building_pixels. gen:1209-1244 pack_atlas/_skyline (sizes sorted
  by (area, h); first lowest y then leftmost x). gen:1247 MAX_TEXTURE 512x512. gen:1250 texel_offset.
  gen:1258-1290 ground_patch/placement_patches. gen:1293-1451 export. gen:1454-1482 ground_variants (key (tileset,
  mt, q) first lid; sorted (lid, mt, q); MAX_VARIANTS 128). gen:1518-1593 room_check. gen:1596-1638 main gates.
- getbbox: all alphas are 0 or 255 and transparent pixels are (0,0,0,0) -> old (any channel) and new (alpha_only)
  Pillow semantics agree: C = bbox of alpha != 0.
- (c5*255//31)>>3 == c5 for c5 in 0..31, so texel = BGR555 channels repacked, no precision loss.
