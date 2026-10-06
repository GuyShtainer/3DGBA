# PHASE 33 ROMGEN, SPEC for S3 (relief.bin, VXL4)

Status: COMPLETE draft for lead review (2026-10-06). No C written; nothing committed.
Contract: `docs/phase33-romgen/PHASE.md`. Foundation: `SPEC-S0-S1.md` (S0 API), `SPEC-S2.md` (S2 helpers reused here),
`SURVEY-S2-S3.md` §S3 (lines 264-549: the first reading of the generator; this spec supersedes it where they differ,
section 8.1 lists every place).

Citation keys (all upstream files under `projects/_reference/pokeemerald-3Ds-dualscreen/3ds_port/scripts/`):
- `rel:N` = `gen_voxel_relief.py` line N (3846 lines)
- `cells:N` = `voxel_cells.py`, `props:N` = `voxel_props.py`, `vb:N` = `voxel_building.py`, `smask:N` = `voxel_sign_mask.py`
- `lines:N` = `voxel_relief_lines.py`, `rchk:N` = `voxel_relief_check.py` (verification tools, read for the oracles only)
- `relief.c:N` / `relief.h:N` = our vendored consumer `source/voxel/voxel_relief.{c,h}`
- `pret:<path>` = the pret decompilation, **pokeemerald@731ad5b**, at `projects/_reference/pokeemerald` (Guy's
  authorization of 2026-10-06). Used only to resolve names to numbers; see the legal rules in section 3.0.

All upstream Python was read as specification only. None of it was run, by any route. The derivations in Appendix A
used only `jq`, `awk`, `sort` (with `LC_ALL=C`, which is Python's code-point string order) over the decomp's JSON and
header files, and the CPython interpreter's own builtin `hash()`/`set` for known-answer vectors (no upstream code).

---

## 0. What S3 produces, and what it consumes

**Output.** `relief.bin` (magic `VXL4`): per layout a row (id, cell count, size + drawn/unit flags, offset, base lift),
per lifted cell 25 signed heights on a 5x5 lattice (one point every 4 px), and a trailing **cut table** (variants =
per-pixel background masks of rock tiles, cut cells = foot / ground-behind / cliff-wall / flags). Byte layout in
section 4. Written by `rg_run` into a caller buffer; the device hook / S4 writes it next to the other three files;
never committed, never in romfs (PHASE.md rules).

**Two products, one format.**
- **S3a, ledges** (on device, first): every outdoor layout with a ledge gets its berms (rel:2368-2464) on a flat
  lattice; no drawn groups, no world solve, no cuts. A valid VXL4 that the consumer loads as-is. Cost: milliseconds.
- **S3 full** (host CLI first): the drawn mountains (per-pixel canvas per drawn group), the world levels, the
  per-layout shapes, cuts and cliff walls, plus the ledges. Memory ~74 MB for the largest group in a direct port
  (SURVEY §S3.5), so it runs in `tools/romgen/build/romgen` on the PC and the user copies `relief.bin` to the SD card.
  Slice S3.9 (optional) specifies the memory-bounded variant that could later run on the console.

**Inputs.** All from the ROM through S0/S1/S2 APIs, plus the pinned number tables of Appendix A:
1. layouts, blockdata, behaviours, blocked bits, tilesets, cell images (S0, S2.1 `rg_cell_image`);
2. roles (`rg_roles`, S1: `water/ledge/stair/floor/signpost/wall/tree/fence/cliff/shelf`), `covers`, `treads`;
3. map headers and connections (`rg_map_connections`, already in S0: dir 1 down, 2 up, 3 left, 4 right);
4. the props cells of S2 (`voxel_props.cells_in`, ported statically inside `rg_bexpand.c`; S3 needs it public);
5. **name-derived numbers** that the ROM does not carry: the named layout ids, the sorted-name orders, the
   behaviour sets. These are resolved through pokeemerald@731ad5b into the numeric tables of Appendix A, each one
   re-asserted against the user's ROM by a host test wherever the ROM can witness it (section 3).

---

## 1. C module layout and APIs

All modules live in `source/romgen/`, pure C11 like S1/S2 (only `<stdint.h> <stdbool.h> <stddef.h> <string.h>
<stdlib.h> <math.h>`; no stdio, no libctru), `-ffp-contract=off` (already set for `rg_%.o` in the device Makefile and in
`tools/romgen/Makefile`), output into `malloc`'d blobs owned by the caller. Every file that ports upstream code carries
the existing header convention, naming its script(s):

```c
/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/gen_voxel_relief.py (<what>),
 * MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
```

The number tables (`rg_rtables.c`) carry no Zallax header; they carry the pret citation header of section 3.0.

C golden rules (`~/.claude/c-golden-rules.md`) apply: no recursion (every flood fill is an explicit stack/queue, in the
**same pop order as upstream**: `stack.pop()` = LIFO, `queue.pop(0)` / `deque.popleft()` = FIFO), every loop bounded
(the solver loops have upstream's own caps: 400, 4000, 64; the fix-point loops `while changed` / `while thin` /
`while level_from_neighbours` / `while grow` get a hard cap of cells+1 iterations with an assert), functions <= ~60
lines (the long upstream functions are split at their comment paragraphs), >= 2 asserts per function on average,
every allocation checked (`RG_ERR_NOMEM`), `-Wall -Wextra` clean.

### 1.1 Shared types (`rg_rlat.h`, header-only)

```c
#define RG_P 4                   /* PER_CELL: lattice steps per cell (rel:156) */
#define RG_SIDE 5                /* RG_P + 1: points per cell side, the file's `side` */
typedef struct RgLat { int w, h; double *v; } RgLat;   /* (W*4+1) x (H*4+1) points, row major, malloc'd */
#define RG_UNSET NAN             /* upstream None in solve_drawn's lattice; never reaches arithmetic (asserted) */
typedef struct RgGrid { double g[RG_SIDE][RG_SIDE]; } RgGrid;   /* cell_grid (rel:2600-2602) */
static inline double *rg_lat_at(RgLat *L, int i, int j);        /* asserts bounds */
void rg_cell_grid(const RgLat *L, int x, int y, RgGrid *out);
bool rg_grid_equal(const RgGrid *a, const RgGrid *b);           /* exact ==, as Python list equality */
```

### 1.2 `rg_pyset.{h,c}`: CPython hash, set order, commonest (rel:61-150)

```c
int64_t  rg_hash64_int(int64_t v);                       /* rel:72-93 int branch: |m| mod (2^61-1), sign, -1 -> -2 */
int64_t  rg_hash64_tuple(const int64_t *items, unsigned n);  /* rel:72-82: the xxHash-style tuple mix, u64 math */
/* rel:96-145 set_order: the iteration order of set(seq) in 64-bit CPython, for a sequence of keys given with
 * their hashes. Keys are opaque 64-bit ids compared by value (ints, or packed RGB / pairs). out gets the unique
 * keys in table order; returns the count. Scratch is caller-provided (2 * next pow2 >= 5/3 n). */
unsigned rg_set_order(const int64_t *keys, const int64_t *hashes, unsigned n, int64_t *out, void *scratch);
int64_t  rg_commonest(const int64_t *keys, const int64_t *hashes, unsigned n, void *scratch);  /* rel:148-150 */
/* convenience wrappers for the three key kinds S3 uses */
int      rg_commonest_int(const int *v, unsigned n, void *scratch);            /* levels (rel:266) */
uint16_t rg_commonest_rgb(const uint16_t *c555, unsigned n, void *scratch);    /* colours, hashed as (r8,g8,b8) */
```

`rg_commonest_rgb` hashes each BGR555 colour as the tuple `(c5*255//31, ...)` of its three 8-bit channels (that is
what PIL's `getdata()` yields upstream) and compares keys in 555 (injective, SPEC-S2 1.1). Port `_hash64`,
`_insert_clean`, `set_order` statement for statement (~120 lines). Known-answer vectors: Appendix A.8.

### 1.3 `rg_rtables.{h,c}`: the pret-resolved numbers (Appendix A) and their ROM assertions

```c
extern const uint16_t RG_ENABLED[2];            /* {20, 4}: LAYOUT_ROUTE104, LAYOUT_RUSTBORO_CITY, in that order */
#define RG_WORLD_ROOT 10u                        /* LAYOUT_LITTLEROOT_TOWN */
extern const uint16_t RG_ALIAS_LAYOUTS[3];      /* {13, 136, 292} */
#define RG_ALIAS_REFERENCE 32u                   /* LAYOUT_ROUTE116 */
#define RG_EXCLUDED_GROUP_SEED 38u               /* group "route122": its name-giving seed */
extern const uint16_t RG_WRAP_GROUP_SEEDS[3];   /* {20, 21, 22}: groups "route104/105/106" */
extern const uint16_t RG_OUTDOOR_BY_NAME[87];   /* A.3: every upstream-outdoor layout, in LAYOUT_* name order */
extern const struct { uint16_t alt, base; } RG_OUTDOOR_ALTS[5];   /* A.4 */
extern const struct { uint8_t group, num; uint16_t layout; } RG_OUTDOOR_MAPS_BY_FOLDER[82];  /* A.5 */
uint16_t rg_name_rank(uint16_t layoutId);       /* index in RG_OUTDOOR_BY_NAME, 0xFFFF when absent */
bool rg_relief_outdoor(uint16_t layoutId);      /* used by an outdoor map, or an RG_OUTDOOR_ALTS alt */
uint16_t rg_relief_alt_base(uint16_t layoutId); /* 0 when not an alternate (S3's own table, not S0's rule) */
static inline unsigned rg_dir_sort_key(unsigned dir);   /* A.7: down 0, left 1, right 2, up 3 */
/* Every assertion of section 3.3 against the opened world. False + a reason string on the first mismatch: the
 * generator then refuses (RG_ERR_TABLES), never writes a relief from a ROM the tables do not describe. */
bool rg_rtables_check(const RgWorld *w, const char **why);
```

Behaviour sets go into the existing `rg_behavior.h` as inline predicates (A.6): `rg_is_flat_behaviour`,
`rg_is_sand`, `RG_MB_WATERFALL`, `RG_MB_BERRY_TREE_SOIL`; `rg_is_jump` already exists.

### 1.4 S0/S1/S2 additions (slice S3.2), gaps G7-G11

- **G7** `rg_cell_image_layers(RgPair*, uint16_t m, bool lowerOnly, RgImage *out16)`: vb:198-216 with `layers=(0,)`
  (lower layer alone, idx-0 pixels in palette slot 0's colour). S2's `rg_cell_image` is the two-layer form only
  (BUILDLOG-S2 S2.1). Used by drawn_canvas for props cells and crowned faces (rel:1149-1159) and drawn_role (rel:1295).
- **G8** `rg_props_cells_in(const RgWorld*, uint16_t layoutId, uint8_t *cellFlags /* w*h */)`: props:225-242 made
  public from `rg_bexpand.c` (the find/cells_of/beyond/connections code is already ported there; only a wrapper and
  the header line are new). Names filter = all three objects.
- **G9** `rg_layout_links(...)`: the map-connection list per layout in map-folder order (A.5), used by find_drawn
  (rel:661-713); and `rg_map_links_sorted(...)` for map_links (rel:787-831). Both live in `rg_rdrawn.c` (S3.3) but
  depend only on `rg_map_connections`.
- **G10** `rg_layer_pixels_upper(RgPair*, m)`: the drawn-pixel bitmap of the upper layer, = `rg_layer(p, m, 1)->drawn`
  (exists; noted so pier_ends, rel:2524, uses it rather than a new function).
- **G11** S1's `is_ledge_junction` (cells:333-340) is computed inside `rg_roles.c`; expose
  `bool rg_is_ledge_junction(const RgLayout*, int x, int y)` (pure function of blocked + behaviour).

### 1.5 The generator modules

| file | port of | content | slice |
|---|---|---|---|
| `rg_ledge.{h,c}` | rel:2333-2507 | `ledge_cells` (+junctions), `ledge_layouts`, `ledge_berms`, `ledges_on_ground`, flat lattice | S3.1 |
| `rg_relief_write.{h,c}` | rel:3683-3808 | VXL4 serialiser: rows, cells, variants, cuts, trailer; two-pass sizing (`out == NULL`) | S3.1 |
| `rg_relief.{h,c}` | rel:2544-2615, 3683-3846 | orchestration: layout selection, per-layout records, variant numbering, `rg_relief_build` | S3.1 (ledges) / S3.7 (full) |
| `rg_pyset.{h,c}` | rel:61-150 | section 1.2 | S3.2 |
| `rg_rtables.{h,c}` | Appendix A | section 1.3 | S3.2 |
| `rg_ralias.{h,c}` | rel:408-502 | `alias_of` (own->General id map, General->own, colour map), aliased metatile + recoloured image, `own_id` | S3.2 |
| `rg_rdrawn.{h,c}` | rel:633-733, 787-851 | `find_drawn` (seeds, rock_near, links, BFS groups, alternate groups), `map_links`, `_seam_cells` | S3.3 |
| `rg_rcanvas.{h,c}` | rel:1089-1325 | `drawn_canvas` (kind, blocked, side, flat, face_low, pier), `drawn_role`, rocky water, majority vote | S3.4 |
| `rg_rprep.{h,c}` | rel:1335-1849 | `drawn_prepare`: region flood, `split_wrapped` (+`_wrapped`, `_clusters`, `_rim_gaps`, `_neck`), big, runs, ties, stats, edges | S3.4 |
| `rg_rworld.{h,c}` | rel:834-1086 | `_gauss_seidel`, `_robust`, `_give_up_seams`, `_blocks`, `world_levels` (+ ground spread loop) | S3.5 |
| `rg_rsolve.{h,c}` | rel:189-398, 1851-2336, 2508-2562 | `solve` (ENABLED fallback), `awash`, `solve_drawn`, `pier_ends`, `layout_heights` | S3.6 |
| `rg_rcut.{h,c}` | rel:2826-3000 | `_cut_mask`, `_lifted`, `cut_cells`, `plain_ground`, `_plain_tile`, `_plain_background`, `_behind` | S3.7 |
| `rg_rshape.{h,c}` | rel:3002-3682 | `rim_cells`, `spread`, `cell_shapes` (follow, settle, shared points, flanks, cut clearing, fills, walls) | S3.7 |

Dropped (never ported): `preview`, `check_lines`, `proof` (rel:2618-2808, PIL renderers), the pickle cache
(rel:1571-1590, 1845-1847), the `print` diagnostics (replaced by counters in `RgOutput`), argparse `main`'s
`--preview/--proof/--layouts` (the CLI's own flags replace them, section 1.7).

### 1.6 Public API (`rg_relief.h`)

```c
typedef enum { RG_RELIEF_OFF = 0, RG_RELIEF_LEDGES = 1, RG_RELIEF_FULL = 2 } RgReliefMode;
typedef struct RgReliefStats {
    unsigned rows, cells, ledgeLayouts, ledgeCells, drawnRows;     /* drawnRows = bit-15 rows */
    unsigned seeds, groups, groupsOk, excluded, spreadDropped;     /* find_drawn / world_levels */
    unsigned variants, cuts, seamCellsGivenUp, nearHalfLevel;       /* nearHalfLevel: R2 log count (8.3) */
    uint32_t maxCanvasPx;                                           /* largest group canvas, pixels */
    double msLedges, msDrawnFind, msCanvas, msWorld, msSolve, msExport;
} RgReliefStats;
/* Builds relief.bin into a malloc'd blob. roles from S1 (rg_roles_all already run). LEDGES needs no pair beyond one
 * at a time; FULL opens pairs per group member. Progress = layouts done of total. */
RgErr rg_relief_build(const RgWorld *w, const RgRoles *r, RgReliefMode mode, RgProgressFn progress, void *ctx,
                      const volatile int *cancel, uint8_t **out, size_t *outSize, RgReliefStats *st);
```

`RgErr` gains `RG_ERR_TABLES` (Appendix A assertions failed: not the ROM the tables describe) and `RG_ERR_RELIEF`
(a field would overflow the format: >65535 cells/variants/cuts, a layout > 255 cells wide, a cut table offset > 2^32).

### 1.7 Driver, CLI, device hook

- `RgRunOpts` gains `RgReliefMode relief`; `RgOutput` gains `uint8_t *relief; size_t reliefSize; RgReliefStats rst;`
  and `double msRelief`. `rg_run` calls `rg_relief_build` after the roles (it needs them) and after buildings.
- CLI (`tools/romgen/romgen_cli.c`): `--only relief` joins `regions,signposts,buildings`; `--relief ledges|full`
  (default `full` once S3.7 is green, `ledges` before); `--relief-layout ID` dumps one layout's row and every cell's
  25 heights as text (replaces upstream `--layouts` for debugging); `--relief-log` prints the R2 near-boundary list
  (section 8.3) and the given-up seams; `--time` prints the six `ms*` phases.
- Device hook (`source/romgen_dev.c`, BUILDLOG-S2 S2.8): passes `RG_RELIEF_LEDGES` and writes `relief.bin` with the
  other files (`.tmp` + rename). FULL stays host-only until S3.9 measures inside budget on hardware.
- Builds: both Makefiles glob `rg_*.c`; no edit. Host links `-lm`.

---

## 2. Port plan: constants, float order, determinism

### 2.1 Constants (verbatim, with their upstream lines)

| constant | value | where | constant | value | where |
|---|---|---|---|---|---|
| LEVEL | 16 | rel:166 | STEP / PER_CELL | 4 / 4 | rel:167-168 |
| MOUND | 8 | rel:169 | JUMPS | 0x38..0x3F (with directions) | rel:181-182 |
| LIP / LIP_WIDTH / LIP_BACK | 6 / 8 / 8 | rel:184-186 | solve sweeps / tol | 400 / 0.01 | rel:189-398 |
| DRAWN_MIN / DRAWN_SEAM | 5 / 2 | rel:627-628 | GROUND_SPREAD | 0.05 | rel:762 |
| SEAM_WEIGHT / HARD / LOOSE | 16 / 1e6 / 0.01 | rel:795-800 | Gauss-Seidel cap / delta | 4000 / 0.001 | rel:851-870 |
| ROLE_MATCH | 0.9 | rel:1272 | WRAP DROP/COLUMNS/CUT/POCKET/REACH/DEEP | 6/3/24/64/64/5 | rel:1330-1335 |
| WALKED | 1000 | rel:1564-1849 | lay() sweeps | 64 | rel:1851-2336 |
| PIER_REACH | 2 | rel:2505 | plain_ground search | dx, dy in ±24 | ~rel:2900-3000 |
| SPREAD | 1.25 | rel:3088 | FLANK | 40 | rel:3148-3682 |
| GOES_ON / NO_FACE | 2 / 0xFFFE | rel:567, rel:3089 | file side | 5 | rel:3683-3808 |

The code records the number with the citation in a comment. The rock colour list and tile sets of rel:559-631 are numeric
in upstream (RGB triples and tile indices) and port verbatim.

### 2.2 Float hazards (H-numbers continue SPEC-S2's H1-H8)

- **H9 round is half-even.** Every Python `round(x)` / `int(round(x))` becomes `nearbyint` under the default
  FE_TONEAREST (asserted once at start-up: `fegetround() == FE_TONEAREST` is not available everywhere, so the host
  test pins `nearbyint(0.5)==0`, `nearbyint(1.5)==2`, `nearbyint(-2.5)==-2`). `int(x)` (truncation) stays a C cast.
- **H10 `sum()` is Neumaier in CPython >= 3.12.** Every `sum(floats)` goes through the existing `rg_pysum` with
  `RG_PYSUM_COMPENSATED` (SPEC-S2 H3). Explicit `+=` loops (solve() rel:189-398) stay naive. Sums of ints are exact
  and use integer accumulators. A two-term sum is identical in both modes (no special case needed). The Gauss-Seidel
  update sums (rel:851-870) are the most sensitive site: see R2 in 8.3.
- **H11 `math.hypot`** in the lay() sweeps: every argument is an integer pixel/lattice offset, so the C port computes
  `sqrt((double)(dx*dx + dy*dy))` with an exact integer square sum. IEEE sqrt is correctly rounded; CPython's hypot is
  correctly rounded for these inputs too, so the two agree bit for bit (deviation D3, recorded in 8.1).
- **H12 division and mean.** `statistics.mean`/`median` are not used (checked); means are `sum(...)/len(...)`, ported
  as `rg_pysum(...) / (double)n`. `median` in `_robust` is `sorted(...)[n//2]` (index form), ported on a qsort of
  doubles with a total-order comparator (no NaN reaches it: asserted).
- **H13 levels are Python ints.** World levels, drawn levels and bases are integers; every comparison and difference
  on them is exact (`int`). Floats appear only in the solver lattices, the robust fit and the shapes.
- **H14 float to int8.** Heights go out as `max(-128, min(127, int(round(v))))` (rel export): H9 then a clamp.
- **H15 -ffp-contract=off** and no `-ffast-math`, both builds; `double` everywhere upstream uses Python floats.

### 2.3 Set and dict order audit (where Python order leaks into bytes)

| site | container | decision |
|---|---|---|
| `commonest` (rel:148-150) on ints / RGB tuples | set over list | **rg_set_order** via `rg_commonest_int/_rgb` (needed: ties broken by first-in-set) |
| `_robust` (`tie = {k: ... for k in hard}`) over the `ties` set | set of pair tuples | **rg_set_order** over the ties' insertion sequence; the pair hash is `rg_hash64_tuple` of (a, b) |
| `set(block) - {-1, 0}` | small non-negative ints | ascending (contiguous small ints hash to themselves; set order = ascending when all < table size; asserted) |
| `thin`, `level_from_neighbours` passes | set iteration | **neutral**: results applied after the full pass (proved by reading); C iterates ascending |
| `_neck` BFS from sets | set seeds | **neutral**: the residual-reachable set after max-flow is unique; the capped `far` path is used only when side >= 64, which is never capped |
| `inside` / frontier BFS distances | set seeds | neutral (BFS distances do not depend on seed order) |
| `shared` point updates | dict keyed by point | neutral (each key updated independently) |
| `content` / `masks` loops | set/dict | neutral (results sorted before use) |
| `pier_ends` (rel:2508-2541) | dict of water cells | **hazard**: two pier cells may write the same water cell, last write wins. Port in upstream iteration order (the dict insertion order is deterministic: row-major over the pier set built row-major) and **assert** no double write on the user's ROM (host test). If it fires: use rg_set_order on the pier set, noted in 8.2 |
| dict iteration elsewhere | dicts | insertion order (Python >= 3.7) = the order C fills its arrays; ported as arrays in fill order |

### 2.4 Name-dependent order (resolved, section 3)

`sorted(seeds)`, `sorted(alternates)`, the export's layout ordering, and `sorted(links)` all compare layout *names*.
`sorted(os.listdir(maps))` in find_drawn (rel:661) fixes the order in which each layout's link list is filled; that
sets BFS member order, hence group member order, processing order and **variant numbering in the cut table**. All
four are replaced by the numeric ranks of Appendix A.3/A.5/A.7. Node numbering in world_levels: the `("b", g, k)`
nodes per group in drawn order, then nodes in sorted-link order; the `samples` dict order is loose ties, then seams.

### 2.5 Order of work

S3.1 lands first and alone gives a working, valid `relief.bin` on the device (ledges + berms). S3.2-S3.7 build the
full path strictly bottom-up (tables, then find_drawn, canvas, world, solve, export), each with its own host tests;
S3.8 adds the oracles and the CLI completion; S3.9 is optional.

---

## 3. Name-keyed tables (resolved through pokeemerald@731ad5b)

### 3.0 Legal rules for these tables (binding, from the lead's brief)

1. Committed code and docs carry **numbers only**. A short identifier name (e.g. `LAYOUT_ROUTE104`) may appear in a
   comment next to its number; nothing else from the decomp does: no decomp text, comments, code, or file excerpts,
   in code, tests or this spec.
2. Every citation pins **pokeemerald@731ad5b** and names the decomp file the number came from (path only).
3. Each table gets a **`docs/PROVENANCE.md` entry** (required deliverable of S3.2): table name, the C symbol, the
   decomp path(s) consulted at 731ad5b, the derivation method (one line: which tool, which ordering rule), and
   which ROM assertion(s) re-check it. Template:
   `| RG_OUTDOOR_BY_NAME | rg_rtables.c | pret:data/layouts/layouts.json @731ad5b | names sorted by code point (LC_ALL=C) | A.3 asserts 1-3 |`
4. Prefer numbers the ROM can witness. Each table below lists its ROM assertion; `rg_rtables_check` runs all of
   them on every generation (cheap: integer compares over the S0 world) and the host test runs them on the user's ROM.
5. Keep tables to what the algorithm reads. No names table is shipped; ranks are shipped as id lists.

### 3.1 What each upstream name turns into

| upstream name use | where | number form | appendix |
|---|---|---|---|
| `ENABLED` (two LAYOUT_ names) (solve() fallback layouts) | rel:174 (a list: order kept) | ids 20, 4 | A.1 |
| `WORLD_ROOT` | rel:798 | id 10 | A.1 |
| `ALIAS_LAYOUTS` (Lavaridge-art layouts) / `ALIAS_REFERENCE` (route116) | rel:411-412 | ids {13, 136, 292} / 32 | A.1 |
| `DRAWN_EXCLUDED` group (route122) | rel:771 | group whose name-giving seed is 38 | A.1 |
| `WRAP_GROUPS` default (route104/105/106; the env override is not ported) | rel:1329 | groups whose name-giving seed is 20, 21, 22 | A.1 |
| tileset checks (`gTileset_General`, Lavaridge secondary) | rel:408, 559-631 | "primary of the root" / "secondary of 13" | A.2 |
| `sorted(seeds)`, `sorted(alternates)`, export layout order, link sort key a/b | rel:633-733, 801-831, 3683 | rank in A.3 | A.3 |
| alternate layouts (`*_alt` names) | rel:633-733 | 5 outdoor pairs | A.4 |
| `sorted(os.listdir(maps))` | rel:661 | map (group, num) order | A.5 |
| behaviour names (`MB_*`) | rel:604-629, 1122, 3184 | numeric sets | A.6 |
| direction strings sorted (`down < left < right < up`) | rel:801-831 | ROM dir -> key | A.7 |

**Group naming.** Upstream names a drawn group after the first seed in sorted-name order (rel:700-733). The C port
names a group by that seed's **id** and compares groups by `rg_name_rank(seedId)`; `DRAWN_EXCLUDED` and the WRAP set
are then id compares. Assert (host): on the user's ROM, the groups seeded by 20, 21, 22 and 38 exist (else the
tables and the ROM disagree on what is drawn: `RG_ERR_TABLES` in FULL mode only; LEDGES does not read them).

### 3.2 The relief outdoor predicate (deviation D1, layout 442)

Upstream's outdoor set = layouts used by a map of an outdoor type, plus their `*_alt` names. S0's structural
alternate rule (byte-equal blockdata) finds 15 pairs where upstream's name rule has 13: the extras are 312->162
(indoor, irrelevant) and **442->1**. 442 is a byte copy of layout 1 that the decomp's layout list (441 entries at
731ad5b) does not name; upstream never saw it. S3 therefore does **not** use S0's alternate rule: `rg_relief_outdoor`
= used by an outdoor map (A.5 set, also derivable from the ROM's map headers: types ROUTE/TOWN/CITY/UNDERWATER/
OCEAN_ROUTE) or an alt in A.4. 442 is excluded, which matches upstream (open question Q3 for Guy).

### 3.3 ROM assertions (run by `rg_rtables_check` and the host test)

| # | assertion | witnesses |
|---|---|---|
| T1 | layout dims: 20 = 40x80, 4 = 40x60, 10 = 20x20, 13 = 20x20, 292 = 30x46, 136 = 40x47, 32 = 100x20, 38 = 40x40, 21 = 40x80, 22 = 80x20, 17 = 20x20 | A.1 ids |
| T2 | the set of layouts used by maps of outdoor type in the ROM's map headers == A.5's layout column (82 maps, as a set; the A.3 list = that set + A.4 alts) | A.3, A.5 |
| T3 | for each (group, num) in A.5, the ROM header's layout id == the table's | A.5 |
| T4 | A.4: each alt has the same w, h as its base and the same tileset pair | A.4 |
| T5 | every A.3 layout's primary tileset == layout 10's primary (the General set) | A.2 |
| T6 | the layouts whose secondary tileset == layout 13's secondary are exactly {13, 28, 136, 292, 293, 336, 337, 338, 339, 340, 341, 379, 380} | A.2 |
| T7 | connection census over outdoor maps: down 27, up 27, left 40, right 40, dive 7, emerge 7 | A.7 |
| T8 | layout 20's map (0/19) connections in ROM order are [up->4 offset 0, down->21 offset 0, right->(Petalburg) offset 50] | A.5/A.7 order |
| T9 | behaviour spot checks: layout 17 (Route 101) has cells with behaviour in 0x38..0x3F; every A.6 FLAT value < 0xF0 | A.6 |

Assertions that **cannot** be made against the ROM (taken on the decomp's word, recorded in PROVENANCE.md): the
sort order of names (A.3/A.5 ranks: the ROM has no names), and which behaviour value carries which name (A.6: the
ROM stores only numbers; spot checks T9 cover the jumps, and the water set is cross-checked against S0's
independently sourced `rg_is_water`).

---

## 4. relief.bin byte layout (export, rel:3683-3808; consumer relief.c:100-200)

All little-endian. Offsets are from the start of the file.

| part | layout | notes |
|---|---|---|
| header | `"VXL4"`, u16 rowCount, u16 side = 5 | 8 bytes |
| rows | rowCount x 14 B `<HHHHIh>`: u16 layoutId, u16 cellCount, u16 w, u16 h \| flags, u32 cellOffset, s16 base | rows in ascending layoutId (`sorted(tables)`, ids unique after the `dict.fromkeys` dedup); h bit 15 = drawn, bit 14 = unit 2 (`HEIGHT_UNIT`, rel:2811) |
| cells | per row, cellCount x 27 B: u8 x, u8 y, 25 x s8 height (row major, j then i) | sorted (y, x) within a row; height = clamp(-128, 127, nearbyint(v / unit)) (H9, H14) |
| cut table | u16 variantCount, u16 cutCount; variants x 36 B `<HH16H>`: u16 firstLayout, u16 metatile, 16 x u16 mask rows (bit set where background); cuts x 14 B `<HBBHhHHBB>`: u16 layout, u8 x, u8 y, u16 variant (0xFFFF = none), s16 foot, u16 behind (0xFFFF = none), u16 wall (0xFFFF none, 0xFFFE NO_FACE), u8 sides (0xFF = none), u8 flags (bit 0 plain background, bit 1 GOES_ON) | starts right after the last cell |
| trailer | u32 cutTableOffset, `"CUTS"` | the offset = length of header+rows+cells |

**Rules the writer must reproduce exactly.**
- `cellOffset` of row k = 8 + 14*rowCount + bytes of rows 0..k-1 (computed in sorted order).
- `base` = `int(lift)`: for a drawn row `_BASE[lid]` (solve_drawn), else the world level `world[lid]` (world_levels).
  **S3a writes 0** (no world solve): deviation D2.
- A row is written only if it has cells or a nonzero base (rel export `if not cells and not lift: continue`).
- **Variants** are keyed by (tileset of the metatile's half: primary if m < 512 else secondary, *the layout's own*
  metatile id, the 16 mask rows) and numbered in **first-encounter order** across the export's layout sequence;
  `firstLayout` = the layout that first produced it. The layout sequence is: `ENABLED` (20, 4) in that order, then
  every drawn group's members in group order then member order (find_drawn order: section 2.4), then the ledge
  layouts in ascending id (`ledge_layouts`, layouts.json order = id order), deduplicated keeping the first, then the
  world-lifted non-drawn layouts not yet listed **in name order** (A.3 rank). Only drawn layouts emit variants, so
  only the first two parts matter for numbering; the rest fixes nothing in the bytes but is kept for fidelity.
- **Cuts** are sorted as full 9-tuples (layout, x, y, variant, foot, behind, wall, sides, flags), ascending, signed
  compare for foot. Cut entries shorter than 9 fields are padded with (0xFFFF, 0xFF, 0) before the sort.
- `foot` = `unit * int(round(foot / unit))` (H9).
- A layout id field is the ROM layout id. Upstream uses the 1-based layouts.json position, which is the same number
  for ids 1..441 (A.3 assert T2 covers the outdoor ones; 442 never appears, D1).

**S3a file.** Rows for the ledge layouts only, flags clear, base 0, an empty cut table (`0000 0000`), trailer. The
consumer accepts it unchanged (relief.c:100-200 validates magic, side, trailer offset, row bounds, cell sort).

**Writer API (`rg_relief_write.h`).**

```c
typedef struct RgReliefRow { uint16_t id, w, hFlags; int16_t base; uint32_t nCells; const uint8_t *cells; /* 27 B each */ } RgReliefRow;
typedef struct RgCut { uint16_t layout; uint8_t x, y; uint16_t variant; int16_t foot; uint16_t behind, wall; uint8_t sides, flags; } RgCut;
typedef struct RgVariant { uint16_t firstLayout, metatile, mask[16]; } RgVariant;
/* rows must already be in ascending id; cuts are sorted here (qsort, full-tuple comparator); out==NULL: size only */
RgErr rg_relief_write(const RgReliefRow *rows, unsigned nRows, const RgVariant *v, unsigned nV,
                      RgCut *cuts, unsigned nCuts, uint8_t *out, size_t cap, size_t *size);
```

---

## 5. Correctness oracles (no upstream output exists)

There is no reference `relief.bin` and none may be produced (upstream is never run). Correctness rests on five
independent oracles, all host tests in `test/host/test_romgen_relief*.c` driven by the user's ROM (skipped with a
message when `ROMGEN_ROM` is unset, as the S1/S2 tests do), plus one emulator check.

### O1. Consumer round trip and invariants (every slice that writes bytes)

Compile `source/voxel/voxel_relief.c` with `-DVOXEL_HOST_FILES` and `VOXEL_RELIEF_PATH` pointed at the generated
file (same pattern as `test_romgen_export.c` with `VoxelBuildings_Init`), call `VoxelRelief_Init()`, require
success, then check through the consumer's public queries (`VoxelRelief_CutCount()` etc., relief.h) that every row
and cut is visible. Then decode the file independently (test-side reader, ~80 lines) and assert:

| # | invariant | applies |
|---|---|---|
| I1 | magic/side/trailer as section 4; trailer offset == end of cells; file size == offset + 4 + 36V + 14C + 8 | all |
| I2 | row ids strictly ascending; every id satisfies `rg_relief_outdoor` | all |
| I3 | w, h (low 14 bits) == the ROM layout's; every cell x < w, y < h; cells strictly ascending (y, x) | all |
| I4 | **S3a**: flags clear, base 0, cut table empty; every written cell is a ledge cell (`ledge_cells` with junctions); every height in 0..6 (LIP); every lattice point with an in-map touching cell that is not a ledge cell is 0 (off-map neighbours do not count, rel:2458-2462); the set of rows == the outdoor layouts with a jump behaviour (no-junction rule) and at least one cell above 0.25 | S3.1 |
| I5 | **full**: bit 14 set only with bit 15; non-drawn rows have no cuts; every cut (layout, x, y) is a written cell; cuts sorted by full tuple; variant indices < V or 0xFFFF; every variant referenced by at least one cut; variant `firstLayout` == the first layout (in export order, section 4) that references it | S3.7 |
| I6 | **seam continuity**: for two connected drawn members of one group, the shared lattice edge agrees in absolute height (value*unit + base) within 1 px on both sides wherever both sides wrote a cell | S3.7 |
| I7 | ledge rows of non-drawn, non-ENABLED layouts are **byte-identical** between S3a and full, except `base` (full may lift) | S3.7 |

I7 is the strongest cross-check available: the ledge path is computed twice, once alone and once inside the full
export, and must agree.

### O2. Line/fault check, re-implemented over the decoded file (rchk:118-170)

`test_romgen_relief_faults.c` ports `faults()` and `score()` (rchk:118-170; constants STEP_PX 4, SPREAD 1.25,
SEVERE 16, rchk:59-62) to run on **decoded** drawn rows: each cell's 5x5 grid = stored value * unit (the per-row base
cancels inside a cell), the cut variant's mask (from the cut table) skips fully-clear 4x4 quads, cells = every
written cell of a drawn row. Per drawn group (rows linked by `rg_rdrawn`) it reports flagged cells (total > 0.01) and
severe cells (total >= 16) per fault kind.

Pins (honest about what they prove): upstream scores floats before rounding; ours scores the stored int8s, so counts
are not comparable to anything upstream printed. The test therefore (a) **pins our own counts** for the reference
group (seed 32, `REFERENCE`, rchk:63) and for every group, as a regression baseline written at S3.8 and reviewed by
Guy against the emulator check; (b) fails if any group has severe cells > 5% of its written cells (a sanity bound,
Q5 lets Guy move it); (c) fails on any "north" fault > 2 px in a cell that is not a cut cell (a back face shown to
the camera is the visible-defect class this check exists for). `voxel_relief_lines.py` (the rendered line check)
needs a mesh dumper and a rasteriser; it is **not ported** (visual check O5 replaces it).

### O3. Re-derivation checks on intermediate products (unit tests per slice)

- find_drawn (S3.3): the group whose seed is 20 contains 4, 21, 22 (the WRAP and ENABLED members it is named for);
  a group seeded by 38 exists and is dropped; no member is indoor; every member has >= DRAWN_MIN rock-tile cells or
  is reached through links; member order is pinned as a regression list (it fixes variant numbering).
- canvas/prepare (S3.4): region labels are first-pixel row-major (assert on a synthetic canvas, and that relabelling
  a random permutation gives the same labels); `split_wrapped` only ever runs on WRAP groups.
- world_levels (S3.5): layout 10 (root) has base 0; every level is an integer multiple of 1 px (H13); seam
  differences between linked layouts equal the solved offsets; the run is identical with `RG_PYSUM_COMPENSATED` on
  and off **or** the test lists every level whose Gauss-Seidel value lies within 0.001 of a .5 boundary (R2).
- solve_drawn (S3.6): all lattice points of non-water, non-UNSET cells are finite; tops are flat (all 25 equal) where
  `kind == top`; ENABLED fallback `solve()` (rel:189-398) runs only for 20 or 4 when its drawn group fails `drawn_ok` (rel:746-753, 776);
  the stat `groupsOk` reports whether that happens on the user's ROM; it is unit-tested standalone on 20 and 4 either
  way (converges within its 400 sweeps, tolerance 0.01).
- pier_ends: the double-write assert of 2.3.

### O4. Determinism and PC-vs-device parity

- Same ROM, two runs in one process and two processes: byte-identical `relief.bin` (both modes).
- `RG_PYSUM_COMPENSATED` on and off: S3a bytes identical (S3a has no long float sums); full mode: reported diff
  count, expected 0 (R2).
- **S3a parity**: the device hook's `relief.bin` (Azahar New-3DS mode, then hardware in S4) has the same SHA-1 as
  the host CLI's for the same ROM. Recorded in the BUILDLOG with both hashes.
- Full mode has no device run before S3.9; S3.9's gate is byte identity with the host's faithful path.

### O5. Emulator visual check (Azahar, voxel mode on, Guy or the lead drives)

| place | layout | what must be seen | slice |
|---|---|---|---|
| Route 101 ledges | 17 | low berms (<= 6 px lips) along every ledge, ground behind ramping up over half a cell, ledge ends tapering to the ground, no spikes at corners | S3.1 |
| Route 104 / Rustboro / Route 105-106 | 20, 4, 21, 22 | drawn cliffs standing as terraces, cut tiles showing background through rock silhouettes, cliff walls textured, no back faces, seams continuous across map edges | S3.7 |
| Lavaridge alias | 13 (and 136, 292) | relief read through the Route 116 reference roles but drawn with Lavaridge's own tiles: no General-tileset metatiles visible, berms on its own ledges | S3.7 |
| Littleroot (root) | 10 | base 0: the map does not float or sink relative to the player | S3.7 |

Screenshots go in `docs/phase33-romgen/shots-S3/` (not committed if ROM-derived pixels are judged risky: the lead
decides per release-legal-audit), compared side by side with Zallax's README screenshots where the place matches.

---

## 6. Slices

Nine slices; S3.1 ships alone and first; S3.9 is optional. Line counts are C + tests, rough (the S2 estimates came
in within ~25%). Every slice: `make -C tools/romgen test` green, device `make` green, BUILDLOG-S3 entry with
commands and outputs, no upstream execution, PROVENANCE.md updated if a table moved.

### S3.1 Ledges on the device (S3a): ~900 lines

- **Files**: `rg_rlat.h`, `rg_ledge.{h,c}`, `rg_relief_write.{h,c}`, `rg_relief.{h,c}` (LEDGES mode only),
  `rg_roles.c` (G11 `rg_is_ledge_junction` exposed), `rg_run.{h,c}`, `tools/romgen/romgen_cli.c`, `source/romgen_dev.c`,
  `test/host/test_romgen_relief_ledge.c`.
- **Functions**: `rg_ledge_cells(layout, junctions, isEnabled, out)` (rel:2333-2364; JUMPS directions rel:181-182);
  `rg_ledge_layouts` (rel:2565-2577, over `rg_relief_outdoor`, no junctions); `rg_ledge_berms` (rel:2367-2464:
  ground-colour rings, lip pixel map, `run` limit 64, `first`, `lip_height`, the per-point reads, the touching rule);
  `rg_flat_lattice` (rel:2540); `rg_relief_cells` (rel:2605-2615, |v| > 0.25); `rg_cell_grid` (rel:2600-2602); writer.
- **Details that bite**: the lip pixel map is a per-layout bitmap of (16w x 16h) bits, not a dict (the lookups are
  bounded, off-map reads are false); ground colours compare in BGR555 (the full two-layer `rg_cell_image`, which is
  what `art.cell_image` returns, vb:198-216); for alias layouts the berm art is the layout's **own** art (rel:2560),
  which in S3a is always the case since S3a has no alias; ENABLED (20, 4) skip junctions (rel:2353) even in S3a;
  Python `//` floors (`rg_floordiv`, H16 in 8.3); `raised` keeps the max per point across cells.
- **Tests**: O1 I1-I4; Route 101 (17) pinned: row present, cell count and the SHA-1 of its cell bytes pinned as a
  regression after the visual check; a synthetic 3x3 layout with one south ledge whose 25 heights are computed by
  hand in the test (LIP 6, WIDTH 8, BACK 8: e.g. a point 2 px into the lip is 1.5 -> stored 2, 6 px is 4.5 -> 4,
  half-even); determinism; the half-even pins of H9.
- **Done-gate**: host suite green; `romgen --only relief --relief ledges` writes a file the consumer loads; device
  hook writes the same SHA-1 in Azahar (O4); Route 101 berms seen in the emulator (O5 row 1).

### S3.2 Tables, CPython order, S0 additions, alias: ~700 lines

- **Files**: `rg_pyset.{h,c}`, `rg_rtables.{h,c}`, `rg_behavior.h` (A.6), `rg_bimg.{h,c}` (G7 lower-only image),
  `rg_bexpand.c` + header (G8 `rg_props_cells_in`), `rg_ralias.{h,c}`, `docs/PROVENANCE.md` (one entry per table),
  `test/host/test_romgen_pyset.c`, `test_romgen_rtables.c`.
- **Functions**: `_hash64`, `_insert_clean`, `set_order`, `commonest` verbatim (rel:61-150); all of section 1.3;
  `alias_of` (rel:422-458: the own<->General id maps by image equality against the reference's art), `AliasArt`
  (`metatile`, `own_metatile`, `cell_image` with the colour map, rel:459-490), `layout_art`, `own_id` (rel:491-502).
- **Tests**: A.8 known answers (every vector, exact); T1-T9 on the user's ROM; G7 lower-only image of a known
  metatile equals the two-layer image where the upper layer is empty; G8 cells equal the set S2 already used
  internally (regression); alias: for 13, `own_id(general(m)) == m` round trip on every mapped id, and the colour map
  is a function (no colour mapped two ways).
- **Done-gate**: all asserts pass on the user's ROM; PROVENANCE.md entries reviewed by the lead (legal rule 3).

### S3.3 find_drawn, links, groups, seams: ~600 lines

- **Files**: `rg_rdrawn.{h,c}`, `test_romgen_relief_drawn.c`.
- **Functions**: rock colour / tile classification (rel:559-631), `find_drawn` (rel:633-733: seeds = outdoor layouts
  with >= DRAWN_MIN rock-tile cells, links per layout filled in **A.5 folder order**, rock_near within DRAWN_SEAM,
  FIFO BFS (`queue.pop(0)`), group id = lowest A.3 rank seed, alternates join their base's group in A.3 order),
  `drawn_group` + `drawn_ok` (rel:746-753, 776), DRAWN_EXCLUDED (rel:771), `map_links` (rel:801-831, sorted by
  (rank a, rank b, A.7 dir key, offset)), `_seam_cells`.
- **Tests**: O3 first bullet; link list of 20 equals T8's order; group membership and member order pinned.
- **Done-gate**: groups printed by `romgen --relief-log` match the regression list; lead review of member order.

### S3.4 Canvas and prepare: ~2200 lines (the largest)

- **Files**: `rg_rcanvas.{h,c}`, `rg_rprep.{h,c}`, `test_romgen_relief_canvas.c`.
- **Functions**: `drawn_canvas` (rel:1089-1268: per-pixel kind over the group's placed members, props cells via G8,
  berry soil 0xA0 at rel:1122, crowned faces with the lower-only image G7, rocky water, `awash` rel:2580-2595),
  `drawn_role` + ROLE_REFERENCE (rel:1270-1324, ROLE_MATCH 0.9), the majority vote (`commonest`, set order);
  `drawn_prepare` (rel:1564-1849: region flood fill LIFO with first-pixel row-major labels, `split_wrapped`
  rel:1338-1561 with `_wrapped`, `_clusters`, `_rim_gaps`, `_neck` max-flow, big regions, runs with WALKED 1000,
  `ties` **in insertion order** (for set_order in S3.5), stats, edges).
- **Memory**: direct port (byte per pixel per array, int32 labels); peak measured with `rg_memcount` and recorded
  (expect ~74 MB on the largest group; host only).
- **Tests**: O3 second bullet; per-group pixel-kind histograms and region counts pinned; `split_wrapped` fires only
  on WRAP groups and leaves the others' labels untouched.
- **Done-gate**: pinned histograms reviewed; peak memory recorded.

### S3.5 World levels: ~600 lines

- **Files**: `rg_rworld.{h,c}`, `test_romgen_relief_world.c`.
- **Functions**: `_blocks`, `_robust` (median by index, `tie` dict built over `rg_set_order(ties)`), `_gauss_seidel`
  (rel:851-870: 4000 iterations cap, stop when max delta < 0.001, update sums via `rg_pysum`), `_give_up_seams`,
  `world_levels` (rel:959-1086: node numbering per 2.4, samples order = loose ties then seams, SEAM_WEIGHT 16, HARD
  1e6, LOOSE 0.01, the GROUND_SPREAD 0.05 loop, `round` half-even to integer levels).
- **Tests**: O3 third bullet; both pysum modes; R2 log.
- **Done-gate**: levels pinned; R2 log empty or reviewed by Guy (8.3).

### S3.6 Solve: ~900 lines

- **Files**: `rg_rsolve.{h,c}`, `test_romgen_relief_solve.c`.
- **Functions**: `solve` (rel:189-398, explicit naive `+=`, 400 sweeps, tolerance 0.01, mounds MOUND 8,
  `commonest(edge)`), `solve_drawn` (rel:1851-2336: footprints, `thin`, `level_from_neighbours`, water bodies, the
  counted ridge, crests, tile / hangs_from / height_of / landing, the 64 `lay()` sweeps in (-hi, y, x) order with H11
  distances), `ledges_on_ground` (rel:2466-2504), `pier_ends` (rel:2505-2539, PIER_REACH 2, double-write assert),
  `layout_heights` (rel:2544-2562).
- **Tests**: O3 fourth and fifth bullets; per-layout lattice SHA-1s pinned.
- **Done-gate**: lattices finite and pinned; pier assert silent on the user's ROM.

### S3.7 Cuts, shapes, full export: ~1800 lines

- **Files**: `rg_rcut.{h,c}`, `rg_rshape.{h,c}`, `rg_relief.c` (FULL mode), `test_romgen_relief_full.c`.
- **Functions**: export constants, `_cut_mask` (via S2's `rg_cutout_mask`), `_lifted`, FILL (rel:2826-2870),
  `cut_cells` (rel:2870-2900), `plain_ground` (dx, dy within +-24), `_plain_tile`, `_plain_background`, `_behind`
  (to rel:3000), `rim_cells` (rel:3002-3090), `spread` (SPREAD 1.25, rel:3088-3146; function at rel:3092), `cell_shapes` (rel:3148-3682,
  FLANK 40, berry soil at rel:3184), export (rel:3683-3808) with the variant numbering of section 4.
- **Tests**: O1 I5-I7; O4 determinism; O5 rows 2-4 by the lead/Guy in Azahar.
- **Done-gate**: consumer loads the full file; I7 holds; drawn cliffs visible in the emulator.

### S3.8 Fault oracle, CLI completion, PC fallback: ~500 lines

- **Files**: `test/host/test_romgen_relief_faults.c`, `romgen_cli.c` (`--relief full` default, `--relief-layout`,
  `--relief-log`), `tools/romgen/README` usage lines, BUILDLOG-S3.
- **Content**: O2 port and pins; the PC-fallback procedure (CLI writes `relief.bin`, the user copies it to
  `sdmc:/3ds/3DGBA/voxel/`; the device hook leaves an existing full-mode file alone: a header flag check, `drawnRows
  > 0` means "full, do not overwrite with ledges"); the emulator checklist of O5 as a table in the BUILDLOG.
- **Done-gate**: O2 green with pinned counts; Guy has seen O5 rows 1-4.

### S3.9 (optional) Memory-bounded full mode on the device: ~1500 lines

Section 7. Gate: byte-identical output to the S3.7 host path on the user's ROM, `rg_memcount` peak <= 16 MB on the
host build, then a timed Azahar and hardware run.

### Budget

| slice | lines | runs where | depends on |
|---|---|---|---|
| S3.1 | ~900 | device + host | S0, S1, S2.8 hook |
| S3.2 | ~700 | host (tables also device) | S0 |
| S3.3 | ~600 | host | S3.2 |
| S3.4 | ~2200 | host | S3.3 |
| S3.5 | ~600 | host | S3.4 |
| S3.6 | ~900 | host | S3.5 |
| S3.7 | ~1800 | host | S3.6 |
| S3.8 | ~500 | host | S3.7 |
| S3.9 | ~1500 (optional) | device | S3.8 |
| **total** | **~8200 required + 1500 optional** | | |

---

## 7. On-device memory: the bounded design (final optional slice S3.9)

**Why.** A direct port of the full path holds, for the largest drawn group (canvas ~200 x 240 cells = ~12.3 M px),
several per-pixel arrays (kind, voted, side, flat, face_low, pier, region labels as int32, run/tie scratch) plus the
lattice: SURVEY §S3.5 estimated ~74 MB, which S3.4 measures. The New 3DS application budget left after the emulator
and the S2 buildings run (~27 MB arena delta measured in Azahar New-3DS mode for S2) makes that impossible. The
target is a **peak <= 16 MB** with **byte-identical output**.

**Design (each item preserves upstream's numbering and order).**

| array | direct | bounded | how |
|---|---|---|---|
| kind (per px) | 12.3 MB | ~0 | not stored: recomputed per cell from a cache of classified 16x16 cell images keyed by (tileset half, metatile); a group uses a few hundred distinct metatiles (~64 KB per 256) |
| voted (per px) | 12.3 MB | 6.1 MB | nibbles (fewer than 16 vote classes; asserted) |
| region labels | 49 MB (int32) | ~2-6 MB | run-length per row + union-find over runs; final labels renumbered by **first pixel in row-major order**, which is exactly the order upstream's flood assigns (asserted on S3.4's host arrays: identical label per pixel) |
| window sums | per-px arrays | 5-row ring | the 5x5 lattice-point windows only ever read +-2 rows |
| side / flat / face_low / pier | 4 x 12.3 MB | per-cell bits or recomputed | derived from kind + voted on demand per cell |
| runs | lists of ints | (value, count) histograms | `WALKED` adds 1000 zero samples without 1000 entries; medians and sums read the histogram in the same order (sums: ints, exact) |
| lattice | doubles (W*4+1)(H*4+1) | 6.2 MB for the largest group | unchanged (needed whole by the lay() sweeps); freed per group |
| per-pixel prepare outputs | kept to export | freed after prepare | solve_drawn and world_levels read only region-level results (checked: no per-pixel region read after prepare) |

Export runs **per group**: each group is prepared, solved and exported in turn; variant numbering is replayed in the
upstream layout order (section 4) by processing groups in that order and keeping only the variant key table (a few
hundred x 40 B) across groups. world_levels needs every group's region results first: pass 1 runs prepare for all
groups keeping only their region stats/edges/ties (KB-scale), pass 2 re-runs canvas + solve + export per group (the
canvas is cheap to recompute; time, not memory).

**Peak estimate.** ~6.1 MB voted + ~4 MB labels + ~6.2 MB lattice is not simultaneous: labels are freed before
the lattice is allocated, so the peak is max(voted + labels + ring ~10-12 MB, voted + lattice + export ~13-16 MB).
Honest range **14-16 MB**; if the measurement exceeds 16 MB, tile the lattice by layout member (each member's
sub-lattice plus a 1-cell halo) at the cost of a third canvas pass.

**Time estimate (honest).** The host direct port's time is measured in S3.7 (`--time`). The ARM11 at 804 MHz runs
this class of code ~10-20x slower than a desktop core, and the bounded design adds roughly 2x (recompute + two
passes). Expected **1-5 minutes** on a New 3DS, in a progress screen, once per ROM (S4 caches by SHA-1). If above
5 minutes, S3.9 is not shipped and the PC fallback stays the route for full relief.

**Gate.** (1) host bounded build output SHA-1 == host direct build (S3.7) on the user's ROM; (2) `rg_memcount`
peak <= 16 MB; (3) Azahar New-3DS run completes with the same SHA-1; (4) hardware time recorded in BUILDLOG-S3.

---

## 8. Open points, divergences, risks

### 8.1 Deviations (from upstream, and from the SURVEY)

| # | deviation | why | effect |
|---|---|---|---|
| D1 | layout 442 excluded from S3 (own outdoor predicate, A.3/A.4 instead of S0's structural alternates) | 442 is not in the decomp's layout list, so upstream never sees it; S0 pairs it with 1 | matches upstream; S0 behaviour for regions/buildings unchanged |
| D2 | S3a writes base 0 for every row and omits base-only rows | S3a has no world solve | full mode may lift some ledge maps; I7 compares S3a vs full excluding base |
| D3 | `math.hypot(dx, dy)` -> `sqrt(dx*dx + dy*dy)` on exact integer sums | integer arguments; both correctly rounded | bit-identical (asserted on a 0..64 grid in a unit test) |
| D4 | `pier_ends` double write asserted | dict last-write wins is order-fragile | none if the assert holds |
| D5 | Python `print` diagnostics -> `RgReliefStats` + `--relief-log` | no stdio in the core | none on bytes |
| D6 | `WRAP_GROUPS` env override not ported | build-time knob | default only |
| S-1 | SURVEY assumed one extra `set_order` site at most; the `ties` set in `_robust` is a second one | found reading `_robust` | S3.5 uses `rg_set_order` there |
| S-2 | SURVEY listed the behaviour sets without BERRY_TREE_SOIL | used at rel:1122 and rel:3184 | 0xA0 added to A.6 |
| S-3 | SURVEY's R2 (Gauss-Seidel update order) and R3 (name order) flagged as unknowns | resolved: node order per 2.4, names via A.3/A.5 | closed; R2 float sensitivity remains as 8.3 |
| S-4 | SURVEY's medium-confidence layout id guesses | all confirmed against pokeemerald@731ad5b and T1 dims | none |

### 8.2 Open questions for Guy

- **Q1 (S3a base 0).** Ship S3a with every map at base 0 (flat world, berms only) until full relief exists, or hold
  S3a until S3.5 so ledge maps also get their world lift? Recommendation: ship base 0; the lift only matters next
  to drawn mountains, which S3a does not draw.
- **Q2 (fund S3.9?).** Full relief on the console costs ~1500 lines and an estimated 1-5 min first run; the PC CLI
  route works without it. Decide after S3.7's host timing.
- **Q3 (layout 442).** Exclude (matches upstream, D1) or treat as layout 1's alternate (S0's rule)? Recommendation:
  exclude.
- **Q4 (Python version).** Upstream's float sums depend on the interpreter: CPython >= 3.12 `sum()` is Neumaier.
  We assume a current CPython (`RG_PYSUM_COMPENSATED` on). If Zallax's builder was run under an older Python, the
  switch flips it; only full mode can differ, and only where R2 fires.
- **Q5 (fault bound).** O2's 5% severe-cell sanity bound is a guess; adjust after the first emulator look.

**Lead decisions, 2026-10-06** (Guy delegated: "work on Emerald now"; none of these blocks a slice):
Q1 ship S3a at base 0. Q2 decide after S3.7's host timing. Q3 exclude 442. Q4 Neumaier on (current CPython),
switch kept. Q5 keep 5% until the first emulator look. Spot-checked by the lead against pokeemerald@731ad5b:
named layout ids/dims (A.1) and all A.6 behaviour sets recomputed independently, equal.

### 8.3 Risks and hazards

- **R2 near-boundary levels.** world_levels rounds Gauss-Seidel results to integer levels; a value within ~1e-9 of
  a .5 boundary could round differently under a different sum order. The port uses the same order and the same
  Neumaier sums, so it should match; `--relief-log` lists every value within 0.001 of a boundary (stat
  `nearHalfLevel`) so Guy can see whether any exist on his ROM.
- **R4 the lay() sweep order** (sorted (-hi, y, x)) uses float `hi`; ties among equal floats fall back to (y, x),
  which is exact. Ported with a qsort comparator over the same triple.
- **H16 floor division.** Python `//` and `%` floor toward minus infinity; every port of a `//` or `%` with a
  possibly negative operand uses `rg_floordiv` / `rg_floormod` (rg_rlat.h), asserted in a unit test at -1..-9.
- **R5 tileset identity.** Variant keys and A.2 compare tilesets by ROM address (S0 pair pointers). Two distinct
  addresses with identical content would make two keys where upstream (by name) had two too: consistent.
- **R6 memory on host.** S3.4's direct port may need ~74 MB; fine on a PC, recorded.
- **R7 legal.** Appendix A carries numbers and short identifiers only; release-legal-audit (S5) re-checks
  `rg_rtables.c`, PROVENANCE.md and this spec.

---

## Appendix A. Derived numeric tables (citations: pokeemerald@731ad5b)

Every table below: numbers only; a short identifier may stand beside a number in code comments. Each needs its
`docs/PROVENANCE.md` row (section 3.0). "ROM" = the assertion of section 3.3 that re-checks it on the user's dump.

### A.1 Named layout ids (pret:data/layouts/layouts.json @731ad5b, id = 1-based list position)

| upstream use | id | dims (w x h) | ROM |
|---|---|---|---|
| ENABLED[0] (LAYOUT_ROUTE104) | 20 | 40 x 80 | T1 |
| ENABLED[1] (LAYOUT_RUSTBORO_CITY) | 4 | 40 x 60 | T1 |
| WORLD_ROOT (LAYOUT_LITTLEROOT_TOWN) | 10 | 20 x 20 | T1 |
| ALIAS_LAYOUTS (LAVARIDGE_TOWN, MT_CHIMNEY, JAGGED_PASS) | 13, 136, 292 | 20x20, 40x47, 30x46 | T1, T6 |
| ALIAS_REFERENCE (LAYOUT_ROUTE116) | 32 | 100 x 20 | T1 |
| DRAWN_EXCLUDED group seed (route122) | 38 | 40 x 40 | T1 |
| WRAP_GROUPS seeds (route104/105/106) | 20, 21, 22 | 40x80, 40x80, 80x20 | T1 |
| Route 101 (test place, not an upstream constant) | 17 | 20 x 20 | T1 |

### A.2 Tilesets (pret:data/layouts/layouts.json @731ad5b, primary/secondary fields)

- All 87 A.3 layouts use the General primary (238 decomp layouts do). Encoded as: primary == primary of layout 10.
  ROM: T5.
- The Lavaridge secondary (ALIAS_TILESETS, rel:408) is used by exactly {13, 28, 136, 292, 293, 336, 337, 338, 339,
  340, 341, 379, 380}. Encoded as: secondary == secondary of layout 13. ROM: T6. No tileset addresses are tabled
  (the ROM supplies them through S0).

### A.3 Upstream-outdoor layouts in name order (87; pret:data/layouts/layouts.json + data/maps/*/map.json @731ad5b; LC_ALL=C sort of layout names)

```
192,196,345,265,12,9,14,5,292,13,6,10,3,7,136,302,303,11,16,1,135,17,18,19,20,287,21,22,23,24,25,26,27,392,28,
29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,263,46,47,319,48,49,50,4,239,394,238,241,395,240,321,331,438,
2,8,357,290,291,406,410,274,411,51,52,53,412,282,146,283,130,15
```
`rg_name_rank(id)` = position in this list. ROM: T2 checks the set (82 map layouts + the 5 alts of A.4); the order
itself is decomp-only.

### A.4 Outdoor alternates (alt -> base; pret:data/layouts/layouts.json @731ad5b, `_alt` names)

`46->263, 319->47, 357->8, 392->27, 438->331`. The full upstream set has 8 more pairs, none outdoor (326->156,
432->58, 433->322, 434->323, 435->324, 436->325, 437->330, 441->439): not tabled. ROM: T4.

### A.5 Outdoor maps in map-folder name order (82; pret:data/maps/map_groups.json + data/maps/*/map.json @731ad5b; LC_ALL=C sort of folder names; types ROUTE/TOWN/CITY/UNDERWATER/OCEAN_ROUTE)

As `group/num:layout`:
```
24/60:192 24/64:196 26/14:345 26/4:265 0/11:12 0/8:9 0/13:14 0/4:5 24/13:292 0/12:13 0/5:6 0/9:10 0/2:3 0/6:7
24/12:136 24/21:302 24/22:303 0/10:11 0/15:16 0/0:1 24/11:135 0/16:17 0/17:18 0/18:19 0/19:20 27/0:287 0/20:21
0/21:22 0/22:23 0/23:24 0/24:25 0/25:26 0/26:27 0/27:28 0/28:29 0/29:30 0/30:31 0/31:32 0/32:33 0/33:34 0/34:35
0/35:36 0/36:37 0/37:38 0/38:39 0/39:40 0/40:41 0/41:42 0/42:43 0/43:44 0/44:45 0/45:263 0/46:47 0/47:48 0/48:49
0/49:50 0/3:4 26/1:239 26/12:394 26/0:238 26/3:241 26/13:395 26/2:240 24/78:321 24/85:331 0/1:2 0/7:8 26/9:290
26/10:291 24/101:406 0/55:410 0/50:274 0/56:411 0/51:51 0/52:52 0/53:53 0/54:412 24/69:282 24/26:146 24/70:283
24/5:130 0/14:15
```
ROM: T2 (set and types), T3 (each group/num -> layout). Connection lists inside a map are in ROM order, which equals
the map.json array order (the decomp's map tool emits arrays in order; checked on 20, T8). Folder resolution of
connection targets never mismatches (64 targets checked).

### A.6 Behaviour sets (pret:include/constants/metatile_behaviors.h @731ad5b; enum without explicit values, count 0xF0)

| set | values | upstream |
|---|---|---|
| FLAT_BEHAVIOURS | 0x0A, 0x60, 0x69, 0x6C, 0x70-0x78, 0x7A-0x7F, 0x8B, 0x8C, 0x8D, 0xBE, 0xEA (24 values; 0x2B is not in the set) | rel:612-618 |
| SANDS | 0x06, 0x21, 0xBF | rel:619 |
| WATERFALL | 0x13 | rel:605 |
| BERRY_TREE_SOIL | 0xA0 | rel:1122, rel:3184 |
| JUMPS | 0x38 E, 0x39 W, 0x3A N, 0x3B S, 0x3C-0x3F corners with two directions each (rel:181-183) | rel:181 |

Cross-checks: S0's independently sourced `rg_is_water` set agrees with the decomp's water names; the house-door
values 0x69, 0x8D, 0x8B used by S2 fall inside FLAT. 0xBB (a jump-named mat) is not a jump. ROM: T9.

### A.7 Direction sort key (rel:801-831; strings sort down < left < right < up)

ROM connection dir -> key: 1 (down) -> 0, 3 (left) -> 1, 4 (right) -> 2, 2 (up) -> 3. Dive (5) / emerge (6) are not
links. Census over outdoor maps (ROM: T7): down 27, up 27, left 40, right 40, dive 7, emerge 7.

### A.8 CPython hash and set-order known answers (CPython 3.14.0, 64-bit builtins; no upstream code involved)

| input | expected |
|---|---|
| hash(-1), hash(2^61-1), hash(2^61) | -2, 0, 1 |
| hash((1,2)) | -3550055125485641917 |
| hash((1,2,3)) | 529344067295497451 |
| hash((255,0,255)) | -2057465547446278849 |
| hash((0,0,0)) | 3010437511937009226 |
| hash((222,180,164)) | 8411117838844654149 |
| set order of [(222,180,164),(189,148,139),(238,213,205),(131,90,90),(98,65,82),(65,49,65),(156,115,115)] | [(65,49,65),(222,180,164),(238,213,205),(98,65,82),(189,148,139),(131,90,90),(156,115,115)] |
| set order of [5,3,40,13,21,8,16,0,-16,-32] | [0,-32,3,5,40,8,13,16,-16,21] |
| set order of [(9,8),(1,2),(3,4),(17,1),(2,9),(40,3)] | [(17,1),(1,2),(3,4),(40,3),(2,9),(9,8)] |
| set order of [(i*7%31, i*3%17, 200-i) for i in 0..29] | (12,11,185),(17,2,171),(21,9,197),(14,6,198),(15,16,189),(10,16,172),(20,7,175),(16,9,180),(30,15,178),(22,2,188),(7,3,199),(18,4,193),(8,13,190),(19,14,184),(1,10,191),(4,15,195),(29,5,187),(25,7,192),(2,3,182),(3,13,173),(6,1,177),(9,6,181),(28,12,196),(5,8,186),(23,12,179),(13,4,176),(26,0,183),(11,1,194),(27,10,174),(0,0,200) |

These vectors are interpreter facts, not decomp data; they need no PROVENANCE row beyond "CPython builtin".

---

## Appendix B. Working citation notes

- Export semantics (rows, variants, cuts, trailer, layout sequence): rel:3683-3808; main's layout list rel:3824-3830.
- Variant key uses `art.primary`/`art.secondary` of the layout's own art (AliasArt subclasses the layout's art,
  rel:459) and `own_metatile`: so key = (own tileset of the half, own metatile, mask).
- Ledges: rel:2333-2464 (ledge_cells 2333, ENABLED junction skip 2353, ledge_berms 2367, touching rule 2458);
  layout_heights 2544-2562 (own-art rule 2560); ledge_layouts 2565; relief_cells 2605; cell_grid 2600.
- Drawn: find_drawn 633, drawn_group 746, DRAWN_EXCLUDED 771, drawn_ok 776, map_links 801, _gauss_seidel 850,
  world_levels 959, drawn_canvas 1089, ROLE_MATCH 1272, WRAP 1329-1335, split_wrapped 1500, drawn_prepare 1564,
  solve_drawn 1851, ledges_on_ground 2466, pier_ends 2508, awash 2580.
- Export helpers: HEIGHT_UNIT 2811, _cut_mask 2828, FILL 2867, cut_cells 2870, plain_ground 2904, _behind 2975,
  rim_cells 3002, SPREAD/NO_FACE 3088-3089, spread 3092, cell_shapes 3148.
- Oracle: rchk:59-63 constants, rchk:118-157 faults, rchk:159-170 score.
- Consumer: relief.c:100-200 (validation, first-row-wins on duplicate id, unit scaling on bit 14).
