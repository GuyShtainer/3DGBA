# PHASE 33 ROMGEN: survey of S2 (buildings.bin, VXB7) and S3 (relief.bin, VXL4)

Status: survey only. Nothing in here has been built. Upstream Python was **read, never executed**.
Upstream root: `projects/_reference/pokeemerald-3Ds-dualscreen/3ds_port/scripts/` (shortened to `scripts/`).
The consumer C is ours: `source/voxel/voxel_building.c` and `source/voxel/voxel_relief.{c,h}`.
S0 (`rg_world`, `rg_art`) is specified in `SPEC-S0-S1.md`, and every input below goes through it.

Conventions used throughout:
- **NAME->ID**: upstream names layouts by their pret symbol (`"LAYOUT_RUSTBORO_CITY"`) and tilesets by C symbol
  (`gTileset_Rustboro`). We have no pret checkout, so each name becomes a row in a hand-written C table of
  (layout id, width, height, primary ptr, secondary ptr, fingerprint). The fingerprint is a hash of the blockdata,
  checked at runtime. SPEC-S0-S1 §1.2 already gives the anchor ids, for example Rustboro = 4 and Route104 = 20.
- **ORDER**: Python's `dict` keeps insertion order, and that order is deterministic. `sorted` is stable. Iteration over
  a `set` of int tuples is deterministic, but it follows CPython's tuple-hash layout, which C does not reproduce.
  Every such site is listed per generator.
- **ROUND**: Python 3 `round()` rounds half to even. In C this is `nearbyint()` under FE_TONEAREST. It is **not**
  `roundf`/`lround`.

---

## S2: buildings.bin (VXB7)

### S2.1 Output format (consumer = `source/voxel/voxel_building.c:8-29`; writer = `gen_voxel_buildings.py:1293-1452`)

Everything is little endian, and the order below is the file order.

**1. Header** (`export`, ~l.1413). It is 24 bytes:

| Field | Type |
|---|---|
| `"VXB7"` | 4 bytes |
| pages | u16 |
| models | u16 |
| pageModels | u16 |
| placements | u16 |
| heightBytes | u16 |
| masks | u16 |
| vertices | u32 |
| variants | u16 |
| (zero) | u16 |

**2. Page table**, 8 bytes per page: u16 w, u16 h, u32 absolute offset of the RGBA5551 texels.

**3. Models**, 16 bytes each: u8 w, u8 h, u16 ground metatile, u32 firstVertex, u32 vertexCount, u32 heightsOffset
(`struct.pack("<BBHIII")`, l.1343).

**4. PageModels**, 8 bytes each: u16 model, u16 page, i16 ox, i16 oy (`"<HHhh"`). The ox/oy value is the art's
position on the page minus the getbbox crop origin (l.1390-1394).

**5. Placements**, 16 bytes each: u16 layout, u16 pageModel, u16 x, u16 y, u16 ground, u16 extraCount, u32 extraFirst
(`"<HHHHHHI"`). Ground `0xFFFF` = OWN_GROUND.

**6. Heights**, one byte per model cell, written in this order:
- `cell_heights` (l.978-1006) is the per-cell maximum Y of the clipped triangles.
- Each value is `min(255, int(round(t)))`. Here `round` is half-to-even (see ROUND).
- An unowned cell is stored as 255 (l.1346).

**7. Pad** to 2 bytes.

**8. Footprints**, one u16 per model cell. A box is `0xFFFF`; otherwise the value is an index into masks.

**9. Masks**, 32 bytes each: 16 u16 rows, where bit x of row z is set. Masks are deduplicated by first appearance in a
dict (l.1352-1356).

**10. Quarters**, one u8 per cell (`spec["quads"]`, default 0), then pad to 2.

**11. Variants**, 6 bytes each: u16 layout, u16 metatile, u8 q, u8 0 (`ground_variants`, l.1454). Then pad to 4.

**12. Vertices**: 6 floats each (x/16, y/16, z/16, u, v, shade), stored as IEEE f32 (`"<%df"`).

**13. Texels**, one page after another. Each page is laid out like this:
- One page per layout that places anything, in layout-id order: `sorted(by_layout)` sorts int ids (l.1364).
- `pack_atlas` uses a skyline packer, tallest first:
  - The sort key is `(-h, -w)`, and the sort is stable over the insertion order "models used, then patches".
  - Size candidates are powers of two from 64 to 1024, with aspect ≤ 8 (l.1209-1245).
- The page must be ≤ 512×512 (l.1374), and there may be at most 256 pages (l.1411).
- Texel order is PICA 8×8 Morton (`texel_offset`, l.1250).
- Each texel value is `(r>>3)<<11 | (g>>3)<<6 | (b>>3)<<1 | (a>=128)`.

Consumer limits: `MAX_VARIANTS 128`, texture ≤ 512×512, ≤ 256 pages (`voxel_building.c`). Model names are **not**
written to the file. They only key the dicts (`crops`, l.1330), so a C port can key by model index.

### S2.2 Inputs

Every input listed below comes from the ROM through S0. Upstream reads each one from pret files.

- **Layouts** (`layouts.json`): id, w, h, blockdata, primary and secondary tileset.
  - Upstream: `voxel_building.py:91-114`.
  - Our source: `rg_world` layouts (SPEC-S0-S1 §1.2).
  - Upstream's `index+1` equals our 1-based layout id (`find_placements`, l.1172).
- **Metatiles** and **metatile attributes**.
  - Upstream: `dump_region_art.py:28-50`, regex over `metatiles.h` / `headers.h`.
  - Our source: `RgTileset.metatiles` / `.attrs`.
- **4bpp tiles** and **16×16 palettes**.
  - Upstream: `dump_region_art.py:76-122`, the files `tiles.4bpp` and `palettes/NN.gbapal`.
  - Our source: S0 `rg_art` (LZ77-decoded tiles plus the pal pointer).
  - The palette split must match exactly: indices 0-5 come from the primary tileset and 6-12 from the secondary,
    whichever tileset the tile comes from (`dump_region_art.py:110-114`). SPEC-S0-S1 §1.4 / `rg_layer` already does
    this.
  - Missing data falls back to magenta `(255,0,255)` (l.103-107). In the ROM a missing palette cannot happen; a tile
    index past the end of the tile data can.
- **RGB888 conversion**: `c5*255//31` (`dump_region_art.py:91-93`). This is the same conversion as S0's.
- **voxel_props.everywhere / cells_of**: used by `prop_specs` (gen l.93-140) to find boulders, signs and similar props
  by their tiles. That module (`scripts/voxel_props.py`, 266 lines) is itself table-driven by metatile ids per tileset.
- **The spec tables** (`voxel_building_specs.py`, 1375 lines; `SPECS` at l.1092-1375, 40 `"name"` entries). Each
  spec is one of:
  - a direct model
  - a `components` / `kit` / `props` / `interior` expander (gen l.38-876)

  Specs name 18 distinct layouts (counted with grep), for example LITTLEROOT_TOWN, PETALBURG_CITY, RUSTBORO_CITY,
  MART, POKEMON_CENTER_1F/2F, the Littleroot house floors, BIRCHS_LAB(+_WITH_TABLE) and RUSTBORO_CITY_GYM. They also
  name 2 tileset symbols (gTileset_Petalburg, gTileset_Rustboro).

### S2.3 Algorithm (`main`, l.1596-1647)

1. **Expand the specs** (`build_models`, l.877-940).
   - Each spec runs through its expander and becomes concrete `{name, layout, rect, ground, parts|relief, exact, owned?,
     match_rows?, at?, bare_at?, interior?}`.
   - `reuse_everywhere()` / `reuse_pieces` (l.414, `LOOSE_PIXELS 64`) find furniture repeated across rooms. Each one
     becomes a `bare` twin model.
2. **Cut the art** (`LayoutArt.building_art`, `voxel_building.py:160-199`). The image is RGBA, (w·16)×(h·16), over the
   spec rect. For each 8×8 quarter:
   - Upper-layer pixels with a nonzero index are opaque.
   - Otherwise, a lower-layer quarter is cleared when its 64 RGB values equal one of the ground metatiles' lower
     quarters (`ground_tiles`).
   - With `owned`, the test goes per pixel against `ground_pixels`.
   - With `upper`, only the upper layer is used.
3. **Build the geometry**.
   - `spec["parts"]()` returns parametric parts from `voxel_building.py:221-1520`: Prism, Strip, HipRoof, Frustum,
     Vault, Walls, Relief, Lifted, Mound, Card, Facet, PlainWall, Decal, Cylinder.
   - A `relief` spec instead takes `pick_side` / `seam_art` / `flank_band` (gen l.240-410) and makes one `vb.Relief`.
   - Every part emits triangles into `Mesh` as (x, y, z, u, v) plus a shade. The shade constants are:

     | Shade | Value |
     |---|---|
     | ART | 1.0 |
     | WEST | 0.80 |
     | EAST | 0.72 |
     | BACK | 0.66 |
     | WOUND | 0.90 |

   - The projection premise is u = X and v = Z − Y: the oblique GBA view.
   - Support routines are polygon clipping (`clip`, l.296), ear triangulation (`triangulate`, l.317), and texture
     splitting by tile (`tile_pieces`, l.357). EPS = 1e-6.
4. **Run the correctness gates**. All three must pass, or the build aborts (l.1626-1634):
   - `ortho_check` (`voxel_building.py:1538-1664`): rasterize the mesh orthographically back to the GBA view and require
     `wrong = missing = extra = 0` against the art inside `exact` rects. It also writes a debug PNG, which we do not
     need.
   - `density_check` (l.1665): the texel density / shear of every triangle must be within 1e-3.
   - `room_check` (gen): composite each modelled room with the terrain and require 0 differing pixels.

   These gates prove the model, not the file. **A C port must keep them in the host test**, because they are the only
   oracle we have (there is no reference `buildings.bin`). On device they can be skipped.
5. **Find placements** (`find_placements`, l.1063-1180). The template is the spec rect's metatiles; core = the owned
   cells in `match_rows`. Then, for every layout:
   - Skip the layout unless its primary tileset is the same as the reference's.
   - The secondary tileset must also match, unless every core metatile is < 512.
   - Slide the template over every (px, py). A core cell matches when it has the same metatile, or when
     `same_building_pixels` holds: the candidate metatile's full 16×16 image equals the art on every owned pixel
     (l.1191-1206).
   - Interior pieces match only at their own (x, y), and only in an identical room (same dims and same blockdata).
   - The placement's ground is the commonest metatile with collision 0 (`cell & 0xC00 == 0`) in the 1-cell ring. Ties
     go to the first seen in scan order (`max(ring, key=ring.get)`, insertion-ordered dict).
   - `odd` = the owned cells whose metatile differs from the template.
   - Finally, append the `reused_at` placements.
6. **Make the patches** (`placement_patches` / `ground_patch`, l.1258-1291). For each odd cell where the model's cell
   height is 0:
   - Take the map's metatile image with the model-owned pixels cleared.
   - Keep the patch if it is not empty (`getbbox`).
   - Patches are deduplicated per page by their raw bytes (`img.tobytes()`).
7. **Export**: see S2.1. `ground_variants` (l.1454) adds the (layout, metatile, quarter) ground variants that
   placements need.

### S2.4 Hand-authored data to port as data (not code)

- **SPECS + expanders + part factories** (`voxel_building_specs.py`, all 1375 lines).
  - This is art-coordinate geometry. Examples: `facet("corner_w", (0,15,47), (16,-1,31), 32, ...)` at l.638, the
    `rect` tuples, and `HOUSE_EXACT` / `LAB_EXACT` / `CROWN_EXACT`.
  - It also holds a handful of RGB keys (`"ring": [(222,230,238)]`, l.1262/1281).
  - It has to become C. The functions take parameters (`littleroot_house(8)`, `center_or_mart((9,16), crown=True)`),
    so this is code, not a flat table. Estimate 1.3-1.6× the Python lines.
- **Layout/tileset names** become the NAME->ID table (18 layouts + 2 tilesets), each fingerprinted.
- **voxel_props OBJECTS**: a metatile-id table per tileset, ported as data.
- **The GROUND constants and metatile ids in specs**, for example `GRASS`. These are raw metatile numbers, so they port
  verbatim.

All of this data is **game-specific art coordinates for Emerald (BPEE) rev 0**. That ROM is the only one we can
fingerprint (SPEC-S0-S1 §1.1). The data says nothing outside the 18 named layouts, plus wherever `find_placements`
finds the same templates.

### S2.5 Python-only dependencies and C replacements

| Python | Where | C replacement |
|---|---|---|
| `PIL.Image` new / load / paste / crop / getbbox / tobytes / copy / convert | gen + vb (art, patches, atlas) | `RgImage {w,h,uint32 *rgba}` plus 6 helpers. Crop and bbox are trivial; `tobytes` dedup becomes a hash of (w, h, bytes) followed by memcmp |
| `PIL.ImageDraw`, `Image.resize`, PNG save | `ortho_check` debug sheet (vb l.1654-1662), `preview`, `town_preview`, `Camera` (vb l.1719+) | drop (debug/preview only) |
| `json` (layouts.json) | vb l.91 | S0 `rg_world` |
| regex over pret headers (`incbin_map`) | dump_region_art l.28-50 | S0 tileset pointers |
| `struct.pack` | export | explicit LE writers (S1 already has them) |
| `argparse`, `types.SimpleNamespace` | main | CLI flags / plain struct |
| `math.floor`/`ceil`, float math | geometry | `<math.h>` double; build with `-ffp-contract=off` |
| `round()` | `cell_heights` l.1006 | `nearbyint` (half-even) |
| frozenset/tuple dict keys | expanders, masks dedup | sorted arrays plus a small open-addressing hash |

**Set-iteration risk.** These sites build sets: gen l.297 (`grids`), l.476 (`out`), l.525, l.611, l.626-633 (the
`leave`/`left`/`seen` flood), and `voxel_building.py` (not swept). If any of them is iterated in a way that reaches
the output (vertex order, mask order, placement order), C must emulate CPython's set order. The relief generator
already does that (`_hash64`/`set_order`, see S3). I have **not proven** that the buildings sets are order-neutral; an
audit is needed before S2.2. Vertex *order* matters only for byte-identity, not for the picture. The host gate in
S2.7 is therefore the decision point: byte-equal, or "gates pass + render-equal".

### S2.6 Size and cost estimate

- **Python to port**:

  | File | Lines |
  |---|---|
  | `gen_voxel_buildings.py` | 1647 |
  | `voxel_building.py` | 1817 (minus ~250 of preview/Camera/render) |
  | `voxel_building_specs.py` | 1375 |
  | `dump_region_art.py` | 146 (mostly replaced by S0) |
  | `voxel_props.py` | 266 |
  | **Total** | **~4,900 lines of real logic** |

- **C estimate: 9,000-11,000 lines** (geometry about 2×, specs about 1.4×, the image helpers plus the pack/export
  about 800). It does not reuse S0/S1 code beyond `rg_world`/`rg_art`. This revises the earlier rough "4-5k", which
  undercounted the geometry library.
- **CPU (estimate; nothing was run, by rule)**:
  - Dominant cost: `find_placements`. 40-60 models (after expansion) × ~325k candidate positions over the layouts with
    a matching primary. There is an early reject on the first core metatile, so the full compare is rare.
    `same_building_pixels` compares ≤ 256 pixels per call. Estimate ≤ 20 M simple ops: well under 1 s on PC C, and
    about 1-3 s on an 804 MHz ARM11.
  - Geometry and gates: each model is ≤ ~(10×8 cells)·256 px rasterized once, which is negligible.
  - Atlas: ≤ 256 pages of ≤ 512 columns of skyline scan. That is < 0.5 s.
  - Upstream CPython is likely to take minutes, because the per-pixel loops run in the interpreter. None of that carries
    over.
- **Memory**:
  - one page buffer of 512×512×2 = 512 KiB
  - the model art (RGBA, 40-60 × ≤ 100 KiB, ≈ 2-4 MiB as uint32). This can be cut by keeping art 8-bit plus a palette,
    or by holding one layout's models at a time.
  - the vertex list (estimate < 1 MiB)
- **Verdict: on-device generation is plausible** at the first boot with a ROM. Host remains the place for the gates.
  Keep a `--host-only` PC CLI regardless.

### S2.7 Risks

1. **No oracle file.** No reference `buildings.bin` exists that we may use: generating one means running Zallax's
   Python, which is forbidden. Correctness rests on porting `ortho_check`/`density_check`/`room_check` faithfully and
   requiring 0/0/0 per model.
2. **Specs are code, not data.** 1375 lines of parametric geometry with art coordinates. A typo produces a model that
   passes no gate. The gates catch it, but only if the gates are ported first.
3. **Name-based placement.** `find_placements` matches by tileset identity. The NAME->ID table must carry ROM
   fingerprints, or a wrong id silently places nothing.
4. **Float and order fidelity.** Vertex values come from double math stored as f32. Clip/triangulate order and set
   iteration decide vertex order. Byte-identity with upstream is not reachable without auditing every set; the
   acceptance bar must be stated (proposed: gates pass + per-model triangle count equal to the count the spec author
   documented, if any. That count is **unknown**, because upstream prints it only at run time).
5. **The upstream file is built from pret data.** pret data equals the ROM only where the decomp matches. Matching
   builds of pokeemerald are byte-exact, so this should hold, but it is unverified for the
   `metatiles.h`/`headers.h` order assumptions.

---

## S3: relief.bin (VXL4)

Upstream code involved:

| Module | Lines | Role here |
|---|---|---|
| `gen_voxel_relief.py` (abbreviated `rel:` below) | 3846 | the generator |
| `voxel_cells.py` | 434 | roles; already ported by S1 as `rg_roles` |
| `voxel_props.py` | 266 | shared with S2 |
| `voxel_art.py` | 176 | replaced by S0 |
| `voxel_building.LayoutArt` | | replaced by S0 `rg_art` |

### S3.1 Output format (consumer = `source/voxel/voxel_relief.c:8-29, 110-200, 245-275`; writer = `rel:3683-3808`)

Everything is little endian.

**Header**: `"VXL4"`, then u16 layoutCount, then u16 side. side must be 5 (`relief.c:121`).

**Layout rows**, 14 bytes each (`"<HHHHIh"`, `rel:3784`):

| Field | Type | Notes |
|---|---|---|
| layout id | u16 | |
| cells | u16 | |
| width | u16 | |
| height | u16 | bit 15 = drawn, bit 14 = unit of 2 px |
| offset | u32 | absolute |
| base | s16 | pixels |

- Rows are written in `sorted(tables)` order, which is ascending layout id.
- The consumer's id index takes the **first** row for a duplicated id (`relief.c:190-194`).

**Cells**, `2 + 25` bytes each: u8 x, u8 y, then 25 × int8 heights, row major.
- Each height is `max(-128, min(127, int(round(v / unit))))`. Here `round` is half-even (see ROUND). `unit` is 2 when
  bit 14 is set.
- Cells are sorted (y, x), and the consumer **rejects** any other order (`relief.c:159-164`).

**Cut table.** The cell bodies are followed by:
- u16 variants, u16 cuts.
- **Variants**, 36 bytes each (`"<HH16H"`): u16 layout (the first layout that drew it), u16 metatile, 16 u16 mask rows
  (bit set = background). Index order = first-encounter order while processing layouts (`rel:3722-3724`).
- **Cut cells**, 14 bytes each (`"<HBBHhHHBB"`):

  | Field | Type | Notes |
  |---|---|---|
  | layout | u16 | |
  | x | u8 | |
  | y | u8 | |
  | variant | u16 | `0xFFFF` = no cut |
  | foot | s16 | pixels; the consumer converts with `/16` |
  | behind | u16 | `0xFFFF` = none |
  | wall | u16 | `0xFFFF` = none, `0xFFFE` = NO_FACE |
  | sides | u8 | |
  | flags | u8 | 1 = plain, 2 = GOES_ON |

  - The table is `sorted(cuts)`: by layout, then **x, then y**.
  - The consumer binary-searches with key `layout<<16 | x<<8 | y` (`relief.c:257-275`), which matches that sort.
  - The consumer reads `sides | flags<<8` as one value, so `GOES_ON = 2<<8` (`relief.h:98`).

**Trailer**: u32 offset of the cut table, then `"CUTS"`. The consumer checks both (`relief.c:121-134`).

Which layouts get a row (`rel:3696-3698, 3773-3775`), in this processing order:
1. `ENABLED`
2. every member of every drawn group (`DRAWN` order)
3. every outdoor layout with a ledge (`ledge_layouts`, in layouts.json = id order)
4. every non-drawn layout whose world `base` is nonzero (sorted by **name**)

A layout is skipped when it has no lifted cells and its lift is 0.

### S3.2 Inputs (all from the ROM via S0, plus one S0 extension)

**From S0 / S1:**
- Layout dims, blockdata, metatiles, attributes and behaviours, tiles and palettes: S0.
- Roles (`vc.Layout`, `role_at`, `blocked`, `off_map`, `is_ledge_junction`, `outdoor`): S1 `rg_roles`. SPEC-S0-S1
  §3.3 already lists `is_ledge_junction` (cells:333-340).
- Alternate layouts (`vc.alternate_layouts`): S0 §1.5.
- `vc.MB` **by name**:
  - `FLAT_BEHAVIOURS` = every MB whose name contains "BRIDGE", "_LOG_", or ends in "_DOOR", plus MB_NO_RUNNING, minus
    MB_REFLECTION_UNDER_BRIDGE (`rel:612-614`).
  - `SANDS` = every MB with "SAND" in its name (`rel:619`).
  - `MB_WATERFALL` (`rel:605`).
  - **Name-substring sets cannot be derived from the ROM.** They must be written out as explicit value lists in
    `rg_behavior.h`. Each value needs a cited source (our `gba_game.h`/`vx_behavior.c` or a ROM-observed census). Flagged:
    our behaviour table may not yet name every BRIDGE/LOG/DOOR/SAND value. That must be checked before S3.

**S0 extension needed: map connections.** S0 (SPEC-S0-S1 §1.3) reads events but not connections. Relief needs them in
three places: `find_drawn` (`rel:661-713`), `map_links` (`rel:801-831`), and `connected_sides` (`rel:2725-2752`, used at
`rel:261`).
- Offsets, measured in P2a (`docs/phase32-voxel/BUILDLOG-P2.md:19`): MapHeader `connections @0xC` points to
  `{s32 count; ptr}`. Each entry is 12 bytes: direction u8 @0, s32 offset @4, group u8 @8, num u8 @9.
- **Direction values, verified now on `roms/emerald.gba`:**
  - 1 = down: Route101 0/16 to Littleroot 0/9
  - 2 = up: Littleroot 0/9 to Route101 0/16
  - 3 = left
  - 4 = right: Route104 0/19 to Petalburg 0/0, offset 50
  - 5 / 6 = dive / emerge, which are filtered out
- Census: 1×27, 2×27, 3×40, 4×40, 5×7, 6×7.
- Proposed API: `rg_map_connections(w, group, num, RgConn *out, max)`.

**Name-keyed constants that must become id tables.** Each was resolved from the ROM by structure (our throwaway probe):

| Upstream name | Use | Our id | How resolved / confidence |
|---|---|---|---|
| `LAYOUT_ROUTE104`, `LAYOUT_RUSTBORO_CITY` | ENABLED (`rel:174`) | 20, 4 | anchors in SPEC-S0-S1 §1.2 |
| `LAYOUT_LITTLEROOT_TOWN` | WORLD_ROOT (`rel:798`) | 10 | anchor |
| `gTileset_General` | find_drawn primary (`rel:643`) | ptr `0x083DF704` | primary of layouts 4/10/20 |
| `gTileset_Lavaridge` | ALIAS_TILESETS (`rel:408`) | ptr `0x083DF794` | secondary of Lavaridge 0/12 (layout 13) |
| `LAYOUT_LAVARIDGE_TOWN`, `LAYOUT_JAGGED_PASS`, `LAYOUT_MT_CHIMNEY` | ALIAS_LAYOUTS (`rel:411`) | 13, {136, 292} | 13 = map 0/12. 136 and 292 are the only **outdoor (type 3)** maps (24/12, 24/13) on the Lavaridge secondary, apart from 0/27 = Route112 (layout 28, which upstream says is "not yet" aliased). Which of 136/292 is which does not matter (set membership). **Medium confidence**: map numbering is from memory of the game's order, not a checkout |
| `LAYOUT_ROUTE116` | ALIAS_REFERENCE (`rel:412`) | 32 (map 0/31, 100×20) | medium; verify the General rock tiles render identically |
| group `route122` | DRAWN_EXCLUDED (`rel:771-774`) | group whose first-named seed is Route122 = layout 38 (0/37, 40×40) | medium |
| groups `route104,route105,route106` | WRAP_GROUPS default (`rel:1329`, env-overridable) | groups seeded by 20 / 21 / 22 | medium. Group *names* are `lid[7:].lower()` of the first seed in **sorted-name** order (`rel:715-723`) |

**Hidden name ordering.** Upstream sorts by pret *names* in several places:
- `sorted(seeds)` (`rel:715`), which fixes group naming and the BFS root
- `sorted(os.listdir(maps_dir))` (`rel:661`, `rel:807`), map folder names
- `sorted(links)` in `map_links` (`rel:831`), the link tuples of names
- `sorted(base.items())` (`rel:1079`), print only
- the export's extra-layout list (`rel:3698`)

These orders decide:
- (a) the group name (it matters only for WRAP_GROUPS and DRAWN_EXCLUDED)
- (b) the node numbering of the world solve, and therefore the Gauss-Seidel **update order**, which changes the float
  result before rounding to LEVEL
- (c) the variant numbering in the cut table

(a) is solved by the id table above. (c) changes bytes but not meaning. (b) is the real fidelity risk: see S3.6 R2.
Reproducing it exactly would need the pret names of all 82 outdoor layouts and 518 maps as a C table. That is a data
table of identifiers; it needs an **ip-legal** check before adoption. The alternative is to accept non-identical
ordering and verify by tolerance.

**Hand-authored pixel and metatile data to port verbatim as data:**
- Rock colours, compared as RGB888 after `c5*255//31`: `ROCK_TOP`, `ROCK_RIM`, `ROCK_FACE`, `ROCK_FLECK` (`rel:559-563`).
- General metatile-id sets: `DIRT`, `BOULDER`, `SIDE_WEST`, `SIDE_EAST`, `SEA_CAPS`, `FACE_SOUTH`, `ROCK_TILES`
  (`rel:569-589, 629-631`).
- `JUMPS` (`rel:181-183`).
- `ROLE_REFERENCE` (`rel:1270`).
- `voxel_props.OBJECTS`: tile ids and palettes (`props:33-56`).
- Numeric constants:
  - lattice: LEVEL, STEP, MOUND, LIP*
  - region tests: FOOTPRINT, REGION_MIN, THIN, MAJORITY, DRAWN_*
  - world solve: SEAM/HARD/LOOSE_WEIGHT, GROUND_SPREAD, MASSIF
  - wrap splitting: WRAP_*
  - cuts and spread: CUT_LIFT, CUT_PIXELS, SPREAD
  - other: RIDGE_TOP, SHORE_STRIP, ROCKY_WATER, PIER_REACH

All of this data is BPEE-specific: General-tileset metatile ids and palette colours.

### S3.3 Algorithm (`main`, `rel:3811-3846`, then `export`)

1. **Choose layouts.** `ENABLED` + DRAWN members + `ledge_layouts()` (outdoor, with any JUMPS behaviour 0x38-0x3F),
   de-duplicated keeping the first occurrence (`rel:3822-3827`).
2. **Find drawn groups** (`find_drawn`, `rel:633-733`; runs at import time).
   - A seed is a non-alternate outdoor layout on the General primary with ≥ `DRAWN_MIN` = 5 cells in `ROCK_TILES`.
     Before counting, the cells are aliased through `alias_of`, which matches Lavaridge-secondary metatiles to General
     rock by palette-free drawing equality (`rel:422-456`).
   - Two such layouts link when a map connection has rock within 2 cells on **both** sides of the seam.
   - BFS gives the canvas offsets, then normalisation to min (0, 0).
   - Each alternate is added as its own group in its base's place (`rel:725-731`).
3. **Per drawn group: canvas and prepare** (`drawn_canvas` `rel:1089-1268`, `drawn_prepare` `rel:1564-1849`). On a
   per-pixel canvas of (CW·16)×(CH·16):
   - Classify each pixel as GROUND/TOP/FACE/FLECK/RIM/VOID/FREE from rock colours and roles. FLAT_ROLES, FLAT_BEHAVIOURS,
     piers and props count as ground.
   - Majority vote in a 5×5 window (`MAJORITY` 2), using running sums.
   - Region flood fill. Regions are split at wrap necks (`split_wrapped`, `rel:1500`) for WRAP_GROUPS only.
   - Drops between regions ("runs"), per-cell stats and edges per seam.
   - Optional pickle cache (`rel:1571-1590, 1845-1847`): developer-only, so **drop it**.
4. **World levels** (`world_levels`, `rel:959-1086`), for all candidate groups at once.
   - `_blocks` connects terraces in each group and solves them with `_robust`, which is two `_gauss_seidel` passes with
     median samples and outlier rejection (≤ 8 px).
   - Then one global graph: nodes = group blocks + plain maps, edges = seam cells (`SEAM_WEIGHT` 16 per cell) plus loose
     ties (0.01). Littleroot is fixed at 0, plus one fixed node per connected component.
   - `_give_up_seams` re-solves the graph, dropping the worst disagreeing seam each time, until none is off by more than
     8 px (or 8 px after rounding to whole levels).
   - Groups whose ground spread exceeds `GROUND_SPREAD` 5% beyond `MASSIF` 160 px are dropped, and the whole loop
     repeats (`rel:1066-1073`).
   - Output: an absolute level per terrace, and `base` per plain map, both rounded to LEVEL·round(v/LEVEL).
5. **Per drawn group: solve the shape** (`solve_drawn`, `rel:1851-2336`).
   - Footprint cells take their region's level.
   - Rock cells become ramps from the footprint above them, with elliptical corners.
   - `lay()` relaxation runs up to 64 sweeps in a sorted order (`rel:2294-2297`).
   - Per-map `base` = the commonest soil level.
   - Unset points take the map's base. Output: a lattice per layout, relative to its base. `_BASE` and `_SHIFT` are
     recorded.
6. **Per layout heights** (`layout_heights`, `rel:2544-2562`):
   - **drawn**: the group lattice, then `ledges_on_ground` (`rel:2466`), then `pier_ends` (`rel:2508`)
   - **ENABLED but not drawn** (fallback): `solve()` (`rel:189-398`). This is a per-cell region graph and a harmonic
     Gauss-Seidel on the 4-px lattice, ≤ 400 sweeps with tolerance 0.01, warm-started at 0. Then mounds are raised by
     BFS distance from the water (MOUND 8 per step, max 24).
   - **otherwise**: a flat lattice
   - Then `ledge_berms` for every layout (`rel:2368-2464`). The lip rises from its foot to LIP 6 px over LIP_WIDTH 8;
     the ground behind rises over LIP_BACK 8; corners take the nearer foot. Berms are read off the layout's own (not
     aliased) art.
7. **Export per layout** (`rel:3700-3772`). For drawn layouts:
   - `cut_cells` (`rel:2870`): rock tiles drawn over the ground behind them get a background mask where lifted >
     CUT_LIFT 2 px, with ≥ CUT_PIXELS 4.
   - `rim_cells` (`rel:3002`): the ground runs on under a terrace's north rim.
   - `spread` (`rel:3092`): even out falls within SPREAD 1.25.
   - `cell_shapes` (`rel:3148-3682`): per-cell own grids, plus cliff walls with the south-face metatile or NO_FACE.
   - Every blocked relief-role cell, and every cell with |shift| > 0.25, is written.
   - Non-drawn layouts write only the cells with |h| > 0.25 (`relief_cells`, `rel:2605`).
   - `alias_of` layouts write their **own** metatile ids back (`own_id`, `rel:499`).

### S3.4 Python-only dependencies and C replacements

| Python | Where | C replacement |
|---|---|---|
| `PIL` | `art.cell_image(...).getdata()/convert` in `awash`, `_drawing`, `alias_of`, `plain_ground`, `_plain_tile`; the canvas reads pixels via LayoutArt | S0 `rg_layer`/16×16 RGB cell images (`RgImage`, shared with S2). `preview`/`proof`/`check_lines` (`rel:2618-2808`, PIL ImageDraw): drop |
| `pickle`, `hashlib`, `inspect` | dev cache `rel:1571-1590` | drop |
| `json` + `os.listdir` over `data/maps/*/map.json` | `find_drawn`, `map_links`, `_map_sides` | S0 maps + the connections extension |
| `collections.Counter` / `defaultdict` | throughout | count arrays / small hash maps. **`most_common(1)` ties go to first insertion**, so C must keep insertion order |
| `commonest()` = `max(set(seq), key=count)` with **CPython 64-bit set order** | `rel:266, 2915, 2930, 2967, 2988` (keys = ints or RGB tuples) | port `_hash64` (tuple hash, an xxHash-style mix, `rel:70-93`) + the `set_order` open-addressing table (`rel:96-151`) verbatim: ~120 C lines, u64 arithmetic only. Upstream wrote this so that Pyodide (32-bit hash) and CPython agree, which is strong evidence that the **other** set iterations in this file are already order-neutral (a 32-bit hash would have broken them) |
| `round()` | ~30 sites | `nearbyint` (half-even), FE_TONEAREST |
| `sum()` of floats | `_gauss_seidel` (`rel:864`), `spread` and others | **Ambiguity:** CPython ≥ 3.12 `sum()` of floats is compensated (Neumaier); ≤ 3.11 is naive left-to-right. Which interpreter produced upstream's data is unknown. The C port must pick one; propose matching Neumaier (3.12+) behind one helper `rg_fsum()` |
| `float` math | solvers | double, `-ffp-contract=off` (the Gauss-Seidel stop tests `delta < 0.001` / `< 0.01` are sensitive to the last bit) |
| `struct` | export | LE writers |

### S3.5 Size and cost estimate

- **Python to port**: about 3,500 of `rel`'s 3846 lines (preview, proof and check are ~330 lines and are dropped), plus
  `voxel_props` (shared with S2). Roles come from S1.
- **C estimate: 7,000-9,000 lines.** Dense list-comprehension Python expands about 2-2.5×.
- **Measured canvas sizes.** These come from our own ROM probe, replicating the `find_drawn` rule. It is
  **approximate**: no alias step, and alternates not excluded.
  - 51 seed layouts form 28 drawn groups.
  - The largest group is layouts {7, 40-45}, a canvas of 200×240 cells = **12.3 M pixels**.
  - Next largest: 5.6 M, 5.0 M, 3.3 M, 3.1 M.
  - Total ≈ **49 M canvas pixels**, processed one group at a time.
- **Memory** (the binding constraint):
  - Upstream holds the per-pixel `kind`, `voted`, two running-sum tables of (W+1)(H+1) ints (`rel:1237-1241`), and a
    per-pixel `region` int (`rel:1607`).
  - A careful C port keeps `kind` u8 + `voted` u8 + `region` u32 + sliding-window sums ≈ 6 B/px. That is ≈ **74 MB** for
    the largest group; a naive port is about 170 MB.
  - On a 3DS this does not fit beside the emulator. Even standalone it needs New-3DS extended memory, and redesigning the
    region labelling (u16 labels, or tiling) is out of a faithful port's scope.
- **CPU (estimate, nothing was run)**:
  - World solve: a node graph of a few hundred nodes, ≤ 4000 Gauss-Seidel sweeps × tens of give-up rounds. About
    10^8 flops: ~0.1 s on PC, and a few seconds on an ARM11 (VFP double is about 1/10 the PC rate).
  - `solve()` fallback: ≤ 400 sweeps over ≤ 52k lattice points. Negligible.
  - Per-pixel passes: about 49 M px × ~100-300 ops ≈ 5-15 G simple ops. That is ~5-20 s on PC C, and **minutes** on an
    804 MHz ARM11.
  - Upstream CPython likely took tens of minutes; that is why the pickle cache exists.
- **Verdict: on-device relief generation is NOT plausible as a faithful first port.** Ship S3 as a **PC/host CLI**
  (`romgen_cli relief`) that writes `relief.bin` from the user's own ROM, with the user copying it to the SD card.
  Revisit on-device only if (a) a group-at-a-time, tiled region labelling bounds memory to ≤ 16 MB, and (b) a
  hardware run shows ≤ ~2 min.
  - A **cheap on-device subset** exists and is self-contained: ledges only (step 6 "otherwise" + `ledge_berms`, written
    as non-drawn rows). It needs no canvas, no world solve and no connections. It could ship first as "S3a".

### S3.6 Risks

- **R1. No oracle.** There is no reference `relief.bin` we may produce: running upstream is forbidden. The PHASE 32
  drop-in data, if it was ever copied from Zallax's build output, would be ROM-derived third-party output and is
  presumably not in the repo. Verification falls back to the consumer's own invariants:
  - sorted cells
  - CUTS trailer
  - side 5
  - variants < count
  - plus a visual/emulator check

  The upstream verification tools (`voxel_relief_lines.py`, `voxel_relief_check.py`) need a compiled mesh dumper,
  multiprocessing and PIL. They can be **re-implemented** as host tests (line check: rock outlines stay where drawn),
  but not run.
- **R2. Float and order fidelity of the world solve.** Node numbering comes from name-sorted orders. Gauss-Seidel
  results depend on update order and summation semantics (the 3.12 `sum`). Everything is then rounded to 16-px levels,
  so a terrace near a .5-level boundary can land one level off. That is a whole map lifted 16 px wrong.
  - Mitigation: log every pre-rounding value within 1 px of a .5 boundary in the host CLI, and review those by hand.
- **R3. Name-substring behaviour sets** (`FLAT_BEHAVIOURS`, `SANDS`). The pret names are not in the ROM. Writing the
  value lists needs a cited source per value; a miss turns bridges into rock.
- **R4. Memory and CPU on device** (S3.5): this forces a PC-fallback architecture for the drawn path.
- **R5. Scope size.** 7-9k lines on top of S2's 9-11k, with no oracle. S3a (ledges) is the only part whose correctness
  can be eyeballed quickly in game (berms on Route 101).

---

## Summary table

| | S2 buildings (VXB7) | S3 relief (VXL4) |
|---|---|---|
| Upstream Python (logic) | ~4.9k lines (gen 1647 + vb 1817 + specs 1375 + props 266) | ~3.5k (`rel`) + roles (S1) + props |
| C estimate | 9-11k lines | 7-9k lines |
| Correctness oracle | port `ortho_check` / `density_check` / `room_check` (0/0/0 per model) | consumer invariants + re-implemented line check; none exact |
| Name tables | 18 layouts + 2 tilesets (fingerprinted) | ENABLED, WORLD_ROOT, ALIAS_*, General/Lavaridge ptrs, DRAWN_EXCLUDED, WRAP_GROUPS; name ordering (R2) |
| Python-only deps | PIL (image struct), json/regex (S0) | PIL (cell images), pickle (drop), CPython set order (port `_hash64`/`set_order`), `sum` semantics |
| PC CPU (C) | < 1 s | ~5-20 s |
| 804 MHz ARM11 | ~1-5 s: **on-device plausible** | minutes + ~74 MB: **PC fallback**; S3a ledges on device |
| New S0 needs | none beyond SPEC-S0-S1 | map connections API (offsets known, directions verified) |
