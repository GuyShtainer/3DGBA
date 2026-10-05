# Phase 31 — SPEC-world: the pure-C diorama world module (`source/diorama.{c,h}`)

**Status:** DRAFT, written 2026-09-10. Binding parent: `docs/phase31-diorama/PHASE.md` (bounds 4, 5,
7; invariants 3, 6). Algorithm source: `docs/phase31-diorama/RESEARCH-classification.md` (the
clean-room idea distillation) — nothing under `projects/_reference/` was opened.

**Sibling specs.** `SPEC-data.md` owns every emulated address, every struct offset and every
numeric Emerald constant (metatile behaviour values, the indoor id list, map-type values, the
connection walk). `SPEC-render.md` owns the shader, the atlas bytes, the VBO upload, the draw
calls and the frame budget. This spec owns **the module in between**: its data model, its rules,
its arithmetic, its memory ceiling and its host suite. Where the three meet, the meeting point is
a **struct defined here** and *filled* by SPEC-data (§4) or *consumed* by SPEC-render (§7, §8).

**House rules this obeys** (`~/.claude/c-golden-rules.md`, the NASA Power-of-10 adaptation):
every loop has a provable bound (rule 2); no recursion anywhere — the flood fill is a BFS over an
explicit fixed queue (rule 1); no allocation, every array fixed and sized from the *measured*
maximum (rule 3); functions stay under a page (rule 4); ≥2 assertions per function (rule 5);
every parameter validated at the boundary (rule 7); at most one level of dereference and no
per-vertex function pointers (rule 9); `-Wall -Wextra` clean (rule 10).

> **Reconciliation with `RULES-v1.md` (2026-09-10 13:49) — binding.** After this spec's first
> draft, `tools/diorama/dioref.py` was run over eight real Emerald maps dumped from ROM and three
> decisions came out of the previews. **Where `RULES-v1.md` differs from `RESEARCH-classification.md`
> or from an earlier draft of this file, RULES-v1 wins**, and this document has been rewritten to
> match it: (**D1**) a new class **`VEG`** for periodic solid bands, per-cell 2-tall boxes,
> excluded from structure grouping — without it Oldale's tree line massed as one 32-tall wall;
> (**D2**) structure height cap **`HMAX = 6`**, not 4; (**D3**) the research's indoor
> metatile-**id** table is **dropped** (its ids do not correspond to those objects on Emerald's
> real `GenericBuilding` / `BrendansMaysHouse` sheets) and replaced by a tileset-agnostic
> blank-metatile VOID rule plus behaviour predicates. The class enum is therefore **14 members in
> RULES-v1's order**, the fixture format is **diodump's `DIOF` TLV** with **`DIOG`/`DIOA`** goldens,
> and every cap in §3 is re-derived from the real fixtures (§3.1, §7.9). Sections 5, 6 and 7 below
> are the C-level statement of RULES-v1 §3, §4 and §5; where this file adds detail (winding, UV,
> tints, caps, failure paths) it adds it *under* those rules, never against them.

## 1. SCOPE AND NON-GOALS

### 1.1 What this module is

`source/diorama.{c,h}` is the **whole brain** of the diorama and **none of its body**. It turns a
handful of read-only pokes at the emulated GBA into:

1. a **classified world** — every tile of the current Emerald map plus its first-ring neighbour
   maps, labelled with one of 13 shape classes (§5);
2. a **structure table** — contiguous WALL/ROOF regions grouped into volumes with one coherent
   height, bounding box and cutaway state (§6);
3. a **vertex + index stream** — the diorama's static geometry, in a caller-provided buffer, in a
   GPU-agnostic float format (§7);
4. **camera state and matrices** — a pitched orbit-follow camera with an explicit `float m[16]`
   look-at, plus the rebase/snap policy across map changes (§8);
5. **billboard quads** — yaw-aligned, foot-anchored, depth-biased (§9);
6. **change detection** — when any of the above must be rebuilt (§10).

Per PHASE invariant 3 it is header-free pure C (`<stdint.h>`, `<string.h>`, `<math.h>` — `math.h`
only in the camera half, exactly as `tilt.c` already does) and it dual-compiles on the PC, where
`test/host/test_diorama.c` proves every rule above with no GBA, no 3DS and no GPU present.

### 1.2 What this module is NOT (v1 non-goals)

| Not here | Whose job | Why |
|---|---|---|
| Any GPU call, `C3D_*`/`C2D_*` type, texture, VBO, shader or draw | `main.c` + `SPEC-render.md` | PHASE invariant 3: "`main.c` owns textures, VBO upload and the draw calls — nothing else." |
| Every emulated **address** and Emerald **constant** (behaviour byte values, the indoor id list, map-type values, struct offsets, the connection walk) | `SPEC-data.md` | Keeps the rules portable: §5 is written in symbolic roles, and the numbers arrive as two dense tables (§4.3). FR/LG later is a data change, not a code change. |
| The **atlas composition** (tileset decompression, metatile layer flatten, BGR555→RGBA) | slice S3 + `SPEC-render.md` | This module only needs the *slot arithmetic* (§7.2), not the pixels. |
| The **OAM sprite decode** | `peersprite.{c,h}` generalised, slice S3 | Already exists and is already pure C; §9 only places the quad. |
| The **gate** (when the diorama is allowed to draw) | `tilt_target_level()` | PHASE invariant 5: one gate ladder, no second one. This module has no gate; a shut gate simply means nobody calls it. |
| Elevation / bridges / multi-level cliffs | after v1 | RESEARCH §8.3.11 + the sibling finding: naive elevation-as-geometry tears maps apart. The elevation nibble is *stored* (§3.3) and *ignored* (§5). |
| Tall-grass tufts, water animation, shadows, day/night, stereo | after v1 | PHASE "After v1". |
| Writing anything, anywhere, to game memory | nobody, ever | PHASE bound 6 / invariant 2. §2 is structurally incapable of it. |

### 1.3 The class roster — three dropped, one added, four reserved

RESEARCH §3.1 records that the reference declares `TREE`, `BUILDING` and `STAIRS` and never
returns them, and §8.3.2 says: "Decide each class: implement it for real or delete it."

- **Dropped: `TREE`, `BUILDING`, `STAIRS`.** Trees are handled properly now — by `VEG`, from data,
  not by a tileset guess; `BUILDING` was only ever an alias for WALL+ROOF membership, which the
  structure table expresses correctly; `STAIRS` had a recipe and no producer.
- **Added: `VEG`** (RULES-v1 D1) — a solid cell whose metatile id repeats in another solid cell
  within ±1..4 rows is part of a *periodic band* (tree line, hedge, rock/cliff band). It is
  excluded from structure grouping and extruded per cell to height 2. This is the fix for the
  reference's known "trees are buildings" weakness, and it was found by looking at real previews:
  without it Oldale's tree line became a single 32-tall wall and Rustboro's east band 36-tall.
- **Reserved (in the enum, with a mesh recipe, with no producer in v1): `BED`, `TABLE`, `SIGN`,
  `DECAL`.** RULES-v1 D3 dropped the indoor id table that used to produce them, because the ids
  did not match Emerald's real sheets. The recipes stay so that a later per-tileset id table —
  keyed by the secondary tileset's ROM address, each id verified against a rendered sheet —
  switches them on with no code change. **These four are the one deliberate exception to "every
  class has a producer", and the suite asserts the exception explicitly** (T18: exactly
  {DECAL, BED, TABLE, SIGN} are unreachable in v1, every other class is reachable, every class has
  a unique name, a colour and a `switch` arm in the emitter).

### 1.4 One-line contract

> `dio_world_load()` reads the game **once per map**; everything after that is a pure function of
> the owned copy — which is exactly why the whole module is testable on a PC with a fake bus and
> hand-built grids.
## 2. THE BUS — `DioBus`

### 2.1 The struct (the `PsprBus` shape, verbatim)

```c
// diorama.h — the ONLY way this module touches emulated memory. Three READS. No write, ever.
// Identical in shape to PsprBus (source/peersprite.h) so main.c can hand both modules the same
// three gbacore_read* thunks and one ctx (PHASE invariant 2: reads go through
// gbacore_read8/16/32 only).
typedef struct {
	uint8_t  (*rd8 )(void* ctx, uint32_t addr);
	uint16_t (*rd16)(void* ctx, uint32_t addr);
	uint32_t (*rd32)(void* ctx, uint32_t addr);
	void*    ctx;
} DioBus;
```

**There is no `wr8`/`wr16`/`wr32` and there never will be.** This is the same property phase 20
made load-bearing (`peersprite.h`: "A phase that adds a write here is a phase that failed"): with
no write entry point in the vtable, no amount of later editing inside `diorama.c` can corrupt a
save, desync the link, or perturb emulation timing. It is PHASE bound 6 enforced by the type
system rather than by discipline.

### 2.2 Where reads are allowed to happen

Only **two** functions in the module ever dereference `bus`:

| Function | Reads | Cadence |
|---|---|---|
| `dio_world_load()` (§4) | the map header/layout scalars, the live grid, the neighbour ROM grids, the two 1024-entry attribute tables | once per map change (§10) |
| `dio_hash_step()` (§10.2) | ≤ `DIO_HASH_SLICE` (256) live-grid cells | once per frame, fixed cost |

`dio_classify_all`, `dio_group_structures`, `dio_mesh_build`, `dio_camera_*`,
`dio_billboard_emit`, `dio_class_name`, `dio_class_color` **take no bus at all** — their
signatures do not mention one. That is the mechanical guarantee behind §1.4, and it is why the
host suite can build a `DioWorld` by hand (`memcpy` into `w.cells`) and exercise every rule
without a fake bus.

Both bus-touching functions are called from **main.c's parked window** — the same place
`build_depth_grid`/`bg0_scan`/`presence_read` already read (PHASE invariant 4). Neither is ever
called between `C3D_FrameBegin/End`.

### 2.3 Validation at the boundary (golden rule 7)

Every bus entry point starts with the same three-line guard, and the guard is *tested*:

```c
if (!bus || !bus->rd8 || !bus->rd16 || !bus->rd32) { dio_world_clear(w); return 0; }
```

A missing callback is a caller bug, not a crash: the world comes back empty (`w->ok == 0`) and
§4.5's failure contract puts the top screen back on the flat frame. `rd32` is used for pointers
and for the 2-cells-at-a-time grid sweep; `rd16` for single cells and dims; `rd8` for the
attribute bytes and the map type.

### 2.4 Read-count budget (asserted, not claimed)

For the largest Emerald world (§3.5: 22 400 cells across 5 instances) one `dio_world_load()`
costs, worst case:

| What | Reads | Note |
|---|---|---|
| header + layout + tileset pointers + map type + connection list | ≤ 64 | exact list in SPEC-data |
| current-map live grid | 6 400 × `rd16` → **3 200 × `rd32`** | two cells per read; the grid is 4-byte aligned per SPEC-data |
| neighbour ROM grids (≤ 4) | ≤ 16 000 × `rd16` → **8 000 × `rd32`** | |
| attribute tables (primary + secondary) | 2 × 1 024 × `rd16` = **2 048** | behaviour + layer come from the same u16 |
| **total** | **≈ 13 300 reads, once per map** | |

For scale: `build_depth_grid` already performs ~150 reads **every frame**, so a map change costs
about 89 frames' worth of the reads the app already does — paid once, on a screen the player has
just walked into. The steady-state cost is §10.2's fixed 256 reads/frame. The host suite counts
reads with a `FakeBus` counter (the phase-20 pattern, `test_peersprite.c`'s `n8/n16/n32`) and
**asserts these bounds**, so a future edit that re-reads the grid per frame fails a test rather
than a frame budget.
## 3. DATA MODEL — `DioWorld`

### 3.1 Sizing from the real game, not from the research

RESEARCH §4.2/§8.3.4 records the reference's storage: a fixed **2048×2048** visited grid and a
second 2048×2048 consumed grid — 4 MB each of `.bss` "for what is at most a few-hundred-tile
world", which it calls "disqualifying as-is" on a 3DS heap. Every cap below is instead derived
from **real Emerald data**, measured on 2026-09-10:

*Source A —* pret's `data/layouts/layouts.json` (441 layouts) and all **518** `data/maps/*/map.json`
connection lists, fetched and reduced for this spec. *Source B —* `BUILDLOG.md`'s ROM walk of all
518 map headers (`tools/diorama/diodump.py`). *Source C —* the **eight ROM-dumped fixtures** in
`test/fixtures/`, run through `tools/diorama/dioref.py` — i.e. the RULES-v1 rules themselves, on
real maps, giving real class counts, structure counts, heights and (recomputed here) vertex counts.

| Measured fact | Value | Consequence |
|---|---|---|
| Largest single layout | **6 400 cells** (80×80: Routes 124/126/127 + Underwater twins) | — |
| Largest single dimension | **140** (Route 123 is 140 wide, Route 111 is 140 tall); tallest layout 40×140 | `DIO_MAX_MAP_W/H = 160` |
| Layout size distribution | median **216** cells (BUILDLOG's ROM walk: median 165, p90 1681); 54 % ≤ 256; p95 3 200; p99 6 400 | the caps are ~100× the median case |
| Max **lateral** connections (DIVE/EMERGE excluded) | **4** → **5 instances** (Route 124, Mauville City); 457/518 maps have **none** | `DIO_MAX_INSTANCES = 8` (1.6× headroom; the research's 16 is unmeasured) |
| Largest **world** (map + first ring) | **22 400 cells** (Route 124 + 4 neighbours); p99 17 600; **no map exceeds 24 576** | `DIO_MAX_CELLS = 24576` |
| Structure **density**, RULES-v1 rules over the 8 fixtures | max **3.0 %** of world cells (Route 101 36/1200, Littleroot 23/800, Rustboro 319/10 800) ⇒ ≈ **672** for a 22 400-cell world | `DIO_MAX_STRUCTS = 1024` (1.5×) |
| Largest single structure | **156 tiles** (Rustboro); 152 (Battle Frontier West); 132 (an indoor lobby) | `DIO_MAX_STRUCT_TILES = 2048` (13×) |
| Structure height under RULES-v1 | max **6** everywhere (Rustboro, Petalburg, Oldale, the lobby); 4 in Littleroot/Route 101/Oldale-outdoor | `DIO_H_MAX = 6` (RULES-v1 D2) |
| Vertices per world cell, this spec's recipes over the 8 fixtures | **8.5 – 11.4** outdoors, **15.0** on one tiny indoor lobby (a 132-tile height-6 room dominates 208 cells) | §7.9: budget at **12** |

Two of those rows are load-bearing and neither is in the research:

- **Structures are ~10× denser than a naive guess** (3 % of cells, not 0.3 %), because `VEG` peels
  the tree lines out of the blobs and leaves many small ones. `DIO_MAX_STRUCTS` had to go to 1024.
- **`VEG` also shrinks the largest blob by an order of magnitude** (156 tiles instead of the
  1 483-tile forest wall the RESEARCH-only rules produced on Route 119), which is what lets the
  BFS queue drop to 2048 entries.

### 3.2 Constants

```c
/* --- world extent (§3.1, measured) ------------------------------------------------------- */
#define DIO_MAX_INSTANCES     8      /* current map + first-ring laterals; measured max 5      */
#define DIO_MAX_CELLS     24576      /* total cells across all instances; measured max 22400   */
#define DIO_MAX_MAP_W       160      /* measured max 140                                       */
#define DIO_MAX_MAP_H       160      /* measured max 140                                       */
#define DIO_MAX_ROWS        288      /* world-space row span (union bbox); measured max 260    */

/* --- classification ---------------------------------------------------------------------- */
#define DIO_ATTR_IDS       1024      /* metatile id space (10 bits)                            */
#define DIO_TILESET_SPLIT   512      /* ids < 512 -> primary tileset, >= 512 -> secondary       */
#define DIO_BEH_VALUES      256      /* the behaviour byte is 8 bits                            */
#define DIO_VEG_PERIOD        4      /* RULES-v1 D1: search dy in -4..-1, +1..+4                */

/* --- structures -------------------------------------------------------------------------- */
#define DIO_MAX_STRUCTS    1024      /* measured worst-case projection ~672 (3.0 % of 22400)   */
#define DIO_MAX_STRUCT_TILES 2048    /* == the BFS queue capacity; measured max 156             */
#define DIO_H_MAX             6      /* RULES-v1 D2; measured max 6 on every fixture            */

/* --- sentinels --------------------------------------------------------------------------- */
#define DIO_SID_NONE     0xFFFFu     /* cell belongs to no structure                            */
#define DIO_INST_NONE      0xFFu
```

### 3.3 Cell encoding

One world cell is one `uint16_t`, **copied verbatim from the game's grid** with the border offset
already cancelled by the loader (§4.4) — the same encoding `diodump.py` writes into the `CELL`
section of a fixture, so a fixture cell and a live cell are the same 16 bits:

```
bit 15..12  elevation   (STORED, IGNORED by v1 — RESEARCH §8.3.11; kept so an "After v1"
                         elevation pass needs no re-read and no format change)
bit 11..10  collision   (0 = passable; non-zero = impassable)
bit  9..0   metatile id (0..1023)
```

```c
static inline uint16_t dio_cell_id  (uint16_t c) { return (uint16_t)(c & 0x03FFu); }
static inline uint8_t  dio_cell_coll(uint16_t c) { return (uint8_t )((c >> 10) & 3u); }
static inline uint8_t  dio_cell_elev(uint16_t c) { return (uint8_t )((c >> 12) & 15u); }
```

### 3.4 The structs

```c
/* One map instance. Instance 0 is ALWAYS the current map at world origin (0,0), read from the
   LIVE grid; instances 1.. are first-ring lateral neighbours from their static ROM grids
   (RESEARCH §6 / RULES-v1 §2). DIVE/EMERGE connections are never instanced.                   */
typedef struct {
	int16_t  ox, oy;        /* world-space tile origin of this instance's (0,0)                */
	uint16_t w, h;          /* dims, 1..DIO_MAX_MAP_W/H                                        */
	uint32_t cellOfs;       /* first cell of this instance inside DioWorld.cells[]             */
	uint32_t srcGrid;       /* the game address the cells came from (provenance / re-hash)     */
	uint8_t  textured;      /* 1 iff BOTH tilesets match instance 0's (PHASE bound 5)          */
	uint8_t  live;          /* 1 = read from the live backup layout (instance 0 only)          */
	uint8_t  dir;           /* DIO_DIR_* the connection came from; 0xFF for instance 0         */
	uint8_t  pad;
} DioInst;                  /* 20 bytes */

/* One grouped volume (§6). WORLD tile coords, bbox inclusive — the same four numbers, in the
   same order, that a DIOG golden record carries (§12.6).                                      */
typedef struct {
	int16_t  x0, y0, x1, y1;   /* inclusive bounding box (minx, miny, maxx, maxy)              */
	uint16_t nTiles;
	uint8_t  inst;             /* instance of the seed tile (atlas binding, §7.2)              */
	uint8_t  height;           /* 1..DIO_H_MAX, shared by the whole volume (§6.4)              */
	uint8_t  flags;            /* DIO_SF_TRUNCATED | DIO_SF_CUTAWAY                            */
	uint8_t  pad[3];
} DioStruct;                   /* 16 bytes */

#define DIO_SF_TRUNCATED  0x01u   /* the fill hit DIO_MAX_STRUCT_TILES and stopped early       */
#define DIO_SF_CUTAWAY    0x02u   /* recomputed every frame by dio_cutaway_update() (§6.5)     */

typedef struct {
	uint32_t layout;        /* the map-layout pointer                                          */
	uint16_t group, num;    /* saved-game map group / number                                   */
	uint16_t w, h;          /* current map dims                                                */
} DioMapKey;                /* the map-change key, §10.1                                       */

typedef struct {
	/* ---- identity / status ------------------------------------------------------------- */
	DioMapKey key;
	uint8_t   ok;             /* 1 = loaded and classified; 0 = empty -> caller draws flat     */
	uint8_t   mapType;        /* DIO_MT_OUTDOOR | DIO_MT_INDOOR (§5.2)                         */
	uint8_t   nInst;
	uint8_t   wflags;         /* DIO_WF_* overflow flags (§3.6)                                */
	uint16_t  nStructs;
	uint16_t  nCells;         /* == sum of inst[i].w*inst[i].h                                 */
	int16_t   wx0, wy0, wx1, wy1;   /* world bbox over all instances (exclusive hi)            */
	uint32_t  gridHash;       /* last completed live-grid hash (§10.2)                         */
	uint32_t  hashCursor, hashAccum;
	uint16_t  semConflicts;   /* diagnostic, §4.3                                              */
	uint16_t  pad0;

	/* ---- instances --------------------------------------------------------------------- */
	DioInst   inst[DIO_MAX_INSTANCES];

	/* ---- the OWNED copy of the world (never a pointer into game memory) ----------------- */
	uint16_t  cells   [DIO_MAX_CELLS];   /* id | collision | elevation, border cancelled       */
	uint8_t   cls     [DIO_MAX_CELLS];   /* DioClass, filled by dio_classify_all               */
	uint16_t  sid     [DIO_MAX_CELLS];   /* structure id or DIO_SID_NONE                       */
	uint8_t   consumed[DIO_MAX_CELLS];   /* cap-overflow column ownership only, §7.6           */

	/* ---- per-metatile-id caches (one read of each tileset table per map) ----------------- */
	uint8_t   attrBeh  [DIO_ATTR_IDS];   /* behaviour byte  = attribute & 0x00FF                */
	uint8_t   attrLayer[DIO_ATTR_IDS];   /* layer type      = (attribute & 0xF000) >> 12; v1
	                                        stores it and never reads it (§13 Q3)              */

	/* ---- the profile's semantics: numbers in, rules out (§4.3) --------------------------- */
	uint8_t   behRole[DIO_BEH_VALUES];   /* behaviour byte -> DIO_BEH_*                         */
	uint8_t   idBlank[DIO_ATTR_IDS / 8]; /* bit per metatile id: 1 = ALL 8 tile entries blank   */

	/* ---- structures + the ONE explicit BFS queue (no recursion, golden rule 1) ----------- */
	DioStruct structs[DIO_MAX_STRUCTS];
	uint32_t  bfsQ   [DIO_MAX_STRUCT_TILES];
} DioWorld;
```

`DioWorld` is a **POD with no pointers at all** — `memset`-able, `memcmp`-able, safe to hold across
frames. `main.c` owns exactly one, statically.

### 3.5 Dimension formulas and the total

Every grid is `[DIO_MAX_CELLS]`, i.e. sized to the **total cell count across all instances**, not
to `DIO_MAX_MAP_W × DIO_MAX_MAP_H` (25 600) and emphatically not to a 2048² world grid
(4 194 304). Instances pack back-to-back into that one pool at `inst[i].cellOfs`.

| Field | Formula | Bytes |
|---|---|---:|
| `cells` | `2 × DIO_MAX_CELLS` | 49 152 |
| `cls` | `1 × DIO_MAX_CELLS` | 24 576 |
| `sid` | `2 × DIO_MAX_CELLS` | 49 152 |
| `consumed` | `1 × DIO_MAX_CELLS` | 24 576 |
| `attrBeh` | `1 × DIO_ATTR_IDS` | 1 024 |
| `attrLayer` | `1 × DIO_ATTR_IDS` | 1 024 |
| `behRole` | `1 × DIO_BEH_VALUES` | 256 |
| `idBlank` | `DIO_ATTR_IDS / 8` | 128 |
| `structs` | `16 × DIO_MAX_STRUCTS` | 16 384 |
| `bfsQ` | `4 × DIO_MAX_STRUCT_TILES` | 8 192 |
| `inst` | `20 × DIO_MAX_INSTANCES` | 160 |
| header scalars (`key`, counts, bbox, hash state) | — | 64 |
| **`sizeof(DioWorld)`** | | **174 688 B = 170.6 KiB** |

**That is the number.** One `DioWorld` covering the largest world in Emerald costs **170.6 KiB of
`.bss`** — 2 % of the reference's two 2048² grids (8 MiB), small enough to sit in static data
beside the existing presence sheet and never touch the linear heap.
`_Static_assert(sizeof(DioWorld) <= 192u*1024u, "DioWorld outgrew its budget")` freezes the
promise; T20 asserts the exact byte count so a thoughtless field addition fails a test.

The **mesh output is not part of this** — it is caller-owned (§7.8) and lives in the linear heap:
`DIO_MAX_VERTS 65536 × 36 B` + `DIO_MAX_INDICES 98304 × 2 B` + `rowFirst[289] × 4 B` =
**2 557 060 B ≈ 2.44 MiB**, sized and justified in §7.9.

### 3.6 Overflow flags — a cap is never UB

```c
#define DIO_WF_CELLS      0x01u   /* the world would exceed DIO_MAX_CELLS -> neighbours dropped */
#define DIO_WF_INSTANCES  0x02u   /* more lateral connections than DIO_MAX_INSTANCES-1          */
#define DIO_WF_STRUCTS    0x04u   /* DIO_MAX_STRUCTS reached -> §6.6 fallback                   */
#define DIO_WF_STRUCTTILE 0x08u   /* some fill hit DIO_MAX_STRUCT_TILES (that struct is flagged) */
#define DIO_WF_DIMS       0x10u   /* a map reported dims outside 1..DIO_MAX_MAP_W/H             */
```

Every cap has a **defined, non-destructive** behaviour (§4.5, §6.6, §7.9): drop the *last*
neighbours (never the current map), truncate the *fill* (never the array), fall back to per-cell
volumes for unclaimed WALL/ROOF cells (never leave a hole), stop emitting vertices (never write
past the end). The flags reach `main.c` for the HUD and the BUILDLOG, so a cap that never fires in
Emerald is still visible the day it does.
## 4. LOAD — `dio_world_load()`

### 4.1 Signature

```c
/* Reads the game ONCE and builds the whole world: instances, owned cells, attribute caches,
   classification (§5), structures (§6) and the wall-column/consumed pass (§7.5).
   Returns 1 on success (w->ok == 1); returns 0 and leaves w EMPTY on any failure (§4.5).       */
int dio_world_load(const DioBus* bus, const DioMapAddrs* in, DioWorld* w);

/* memset + ok = 0 + nInst = 0. Cheap and always safe; also the failure path.                   */
void dio_world_clear(DioWorld* w);
```

### 4.2 The input struct — **this is where SPEC-world and SPEC-data meet**

`dio_world_load` deliberately does **no address arithmetic on the game's structures**. It is
handed a struct in which *every pointer has already been resolved and every Emerald constant has
already been translated*. SPEC-data owns filling it (from `gMapHeader` / `gBackupMapLayout` /
`gSaveBlock1Ptr` / the tileset headers); this module owns consuming it. The split means the module
never has to know a single BPEE address, and a FR/LG profile later changes only the filler.

```c
#define DIO_DIR_NORTH 0
#define DIO_DIR_SOUTH 1
#define DIO_DIR_WEST  2
#define DIO_DIR_EAST  3

#define DIO_MT_OUTDOOR 0    /* everything else                                                 */
#define DIO_MT_INDOOR  1    /* pret's indoor + secret-base map types (RESEARCH §3, world.c:220) */

/* The current map. Every field is already resolved: no header walk happens in this module.     */
typedef struct {
	uint32_t layout;        /* map-layout pointer  -> DioMapKey.layout (change detection)       */
	uint16_t group, num;    /* saved-game location -> DioMapKey                                 */
	uint16_t w, h;          /* PLAYABLE dims (border excluded)                                  */
	uint32_t grid;          /* address of cell (0,0) of the LIVE working copy, border ALREADY
	                           cancelled by SPEC-data: cell (x,y) is at grid + 2*(y*stride + x) */
	uint16_t stride;        /* cells per row of that live copy (= w + 2*border in Gen-3)        */
	uint16_t border;        /* the border constant, for provenance/assert only (7 in Gen-3)     */
	uint32_t attrPrimary;   /* metatileAttributes table of the primary tileset (u16 per id)     */
	uint32_t attrSecondary; /* ditto, secondary; 0 = absent -> ids >= 512 get behaviour 0       */
	uint32_t tsPrimary;     /* the two tileset pointers, ONLY used to compare with a
	   tsSecondary;            neighbour's pair for the `tilesetsMatch` decision (PHASE b.5)    */
	uint32_t tsSecondaryPad;
	uint8_t  mapType;       /* DIO_MT_*                                                          */
	uint8_t  pad[3];
	uint16_t playerX, playerY;  /* player tile in CURRENT-MAP coords, border already cancelled   */
} DioMapInfo;

/* One already-resolved lateral connection. DIVE/EMERGE are filtered out by SPEC-data and never
   appear here (RESEARCH §6). A connection whose target header/layout could not be resolved is
   likewise simply absent.                                                                      */
typedef struct {
	uint8_t  dir;           /* DIO_DIR_*                                                        */
	uint8_t  tilesetsMatch; /* 1 iff BOTH tilesets equal the current map's pair                 */
	int16_t  offset;        /* the connection's lateral offset, game units (tiles)              */
	uint16_t w, h;          /* the neighbour layout's dims                                      */
	uint16_t stride;        /* cells per row of the neighbour's STATIC ROM grid (== w)          */
	uint32_t grid;          /* address of neighbour cell (0,0) in ROM                           */
} DioConnInfo;

typedef struct {
	DioMapInfo  cur;
	DioConnInfo conn[DIO_MAX_INSTANCES - 1];
	uint8_t     nConn;      /* 0..DIO_MAX_INSTANCES-1; measured max in Emerald = 4              */
	uint8_t     pad[3];
	DioSemantics sem;       /* §4.3 — behRole + idBlank                                          */
} DioMapAddrs;
```

**Why `stride` is separate from `w`.** The live working copy is a *bordered* grid: its row pitch
is `w + 2*border`, while the neighbour's ROM grid is unbordered (`stride == w`). RESEARCH §2 calls
the border offset "the classic port bug" alongside Y polarity. Carrying `stride` explicitly means
the module's one indexing expression is `grid + 2*(y*stride + x)` for **both** kinds, and T6
builds a fixture with `border = 7`, `stride = w + 14` and proves cell (0,0) is the playable
corner and not a border tile.

### 4.3 `DioSemantics` — the numbers, as one dense table plus one bitmap

**RULES-v1 D3 dropped the research's indoor metatile-id table.** It was checked against Emerald's
real `gTileset_GenericBuilding` (73 indoor maps) and `gTileset_BrendansMaysHouse` sheets rendered
from ROM, and the ids do not correspond to those objects. So v1's indoor rules are
**tileset-agnostic**: behaviour predicates plus one blank-metatile VOID rule. RESEARCH §8.3.3's
warning ("576 is both TABLE and BED; use one table and assert uniqueness") is answered by not
having an id table at all — but the shape of the fix is preserved for the day one returns:
whatever ships must be a **dense array indexed by id**, in which a double entry is unrepresentable
rather than merely asserted-against.

```c
/* Behaviour-byte roles — portable across tilesets. SPEC-data supplies the numeric values; the
   SET MEMBERSHIP is RULES-v1 §3 and is reproduced there, not here.                             */
#define DIO_BEH_NONE     0
#define DIO_BEH_WATER    1   /* every MB_ whose name contains WATER or CURRENT, plus
                                MB_NO_SURFACING and MB_WATERFALL, MINUS MB_SHALLOW_WATER and
                                MB_PUDDLE (both walkable)                                       */
#define DIO_BEH_GRASS    2   /* MB_TALL_GRASS, MB_LONG_GRASS, MB_LONG_GRASS_SOUTH_EDGE,
                                MB_ASHGRASS — WALKABLE, never extruded                          */
#define DIO_BEH_LEDGE    3   /* every MB_JUMP_* (all eight, incl. the diagonals)                 */
#define DIO_BEH_PC       4   /* MB_PC                                                            */
#define DIO_BEH_TV       5   /* MB_TELEVISION                                                    */
#define DIO_BEH_SHELF    6   /* MB_BOOKSHELF, MB_POKEMART_SHELF, MB_POKEMON_CENTER_BOOKSHELF,
                                MB_LIBRARY_SHELVES                                              */
#define DIO_BEH_COUNTER  7   /* MB_COUNTER                                                       */
#define DIO_BEH_DOOR     8   /* every MB_ whose name contains DOOR                                */
#define DIO_BEH_ROLE_COUNT 9

typedef struct {
	uint8_t behRole[DIO_BEH_VALUES];      /* 256 B, indexed by the behaviour byte               */
	uint8_t idBlank[DIO_ATTR_IDS / 8];    /* 128 B, bit per metatile id (§4.3.1)                */
} DioSemantics;
```

#### 4.3.1 The blank-metatile bitmap — the one rule that needs tile *pixels*

RULES-v1 §3 rule 1: *a metatile whose 8 tile entries all reference an all-zero (blank) tile
renders as pure backdrop* — the black filler outside an indoor room — *and is not meshed at all*.
Evaluating that needs the metatile tables (`u16[512*8]` per tileset) and the 4bpp tile bytes
(`u8[512*32]` per tileset), which are atlas-shaped data this module has no business owning. So the
module owns the **rule** and exposes the **scanner**, and whoever already holds those bytes (the
S3 atlas composer on device; the fixture in the host suite) calls it once per map:

```c
/* Fills a 1024-bit bitmap: bit m = 1 iff EVERY one of metatile m's 8 tile entries points at a
   tile whose 32 bytes are all zero. Pure, no bus. tiles* are 512*32 bytes; mtl* are 512*8 u16.
   Ids < 512 resolve in the primary tables, >= 512 in the secondary at (id - 512); a tile INDEX
   < 512 resolves in the primary tile bytes, >= 512 in the secondary at (tile - 512).           */
void dio_blank_scan(uint8_t idBlank[DIO_ATTR_IDS / 8],
                    const uint16_t* mtlPrim, const uint16_t* mtlSec,
                    const uint8_t*  tilPrim, const uint8_t*  tilSec);

static inline int dio_id_blank(const DioWorld* w, uint16_t id)
{ return (w->idBlank[id >> 3] >> (id & 7)) & 1; }
```

This is exactly `dioref.py`'s `blank()`, so the host suite can feed it the `MTLP`/`MTLS`/`TILP`/
`TILS` sections of a `DIOF` fixture and reproduce the golden class grid byte-for-byte (§12.6).
128 bytes of state instead of 64 KiB of tile data held for a rule that fires on a few dozen ids.

`dio_world_load` also runs a cheap **consistency sweep** into `semConflicts` (a diagnostic, never
an abort): the number of ids that are simultaneously `idBlank` and carry a non-`NONE` behaviour
role. Emerald should report 0; a non-zero count in the log is how a tileset surprise announces
itself.

### 4.4 What the loader does, in order

1. **Guard** (§2.3) and `dio_world_clear(w)`.
2. **Validate `in->cur`** (§4.5). On any failure: return 0, `w->ok == 0`.
3. **Instance 0** = the current map at world origin (0,0), `live = 1`, `textured = 1`,
   `dir = 0xFF`, `cellOfs = 0`. Copy `w*h` cells with `rd32`-pairs from
   `grid + 2*(y*stride + x)`.
4. **Instances 1..n** — for each `conn[i]`, in list order, placed by RESEARCH §6:
   | dir | origin |
   |---|---|
   | NORTH | `(offset, −h_n)` |
   | SOUTH | `(offset, +h_cur)` |
   | WEST | `(−w_n, offset)` |
   | EAST | `(+w_cur, offset)` |
   `textured = conn.tilesetsMatch` (PHASE bound 5). Copy `w*h` ROM cells. If the running total
   would exceed `DIO_MAX_CELLS` **or** `DIO_MAX_INSTANCES`, set the matching `DIO_WF_*` flag and
   **stop adding neighbours** — instance 0 is never dropped, so the diorama degrades to "no
   neighbours" (visible only as the §5.6 seam), never to "no diorama".
5. **World bbox** `wx0/wy0/wx1/wy1` = the union of instance rects (exclusive hi), and
   `_Static_assert`-checked at runtime against `DIO_MAX_ROWS` (§7.8's row table).
6. **Attribute caches** — `attrBeh[id]`/`attrLayer[id]` for all 1024 ids: `id < 512` reads
   `attrPrimary + 2*id`, `id >= 512` reads `attrSecondary + 2*(id − 512)` (RESEARCH §1); a null
   `attrSecondary` yields behaviour 0 for the whole upper half. Behaviour = low 8 bits of the
   attribute word; layer type = `(attr & 0xF000) >> 12` (stored, unused in v1).
7. `w->behRole = in->sem.behRole`, `w->idBlank = in->sem.idBlank` (384 bytes, so the world\n   is self-contained afterwards), and the `semConflicts` sweep (§4.3.1).
8. **Classify** (§5) → `dio_classify_all(w)`.
9. **Group** (§6) → `dio_group_structures(w)`.
10. **Cap-overflow column marking** (§7.6) → `dio_columns_mark(w)` — a no-op unless\n    `DIO_WF_STRUCTS`/`DIO_WF_STRUCTTILE` left WALL/ROOF cells unclaimed.
11. `w->key = {layout, group, num, w, h}`; `w->gridHash = 0`, `hashCursor = 0` (§10.2 starts a
    fresh sweep); `w->ok = 1`; return 1.

Steps 8–10 take **no bus**: after step 7 the world is a closed system.

### 4.5 Sanity checks and the failure contract

| Check | Rejects |
|---|---|
| `bus` + all three callbacks non-NULL | a caller wiring bug |
| `in && w` non-NULL | ditto |
| `1 <= cur.w <= DIO_MAX_MAP_W` and `1 <= cur.h <= DIO_MAX_MAP_H` | a garbage header read mid-load-screen (real max 140) |
| `cur.w*cur.h <= DIO_MAX_CELLS` | ditto |
| `cur.stride >= cur.w` and `cur.stride <= cur.w + 64` | a bogus border/pitch |
| `cur.grid` and every `conn[i].grid` lie in a **plausible GBA region** — EWRAM `0x02000000..0x0203FFFF`, IWRAM `0x03000000..0x03007FFF`, or ROM `0x08000000..0x09FFFFFF` | a null/uninitialised pointer, the single most likely real failure (the game is mid-warp) |
| `attrPrimary` in a plausible region; `attrSecondary` either 0 or plausible | ditto |
| `cur.mapType <= DIO_MT_INDOOR` | an unmapped map-type value |
| `nConn <= DIO_MAX_INSTANCES-1` and every `conn[i].dir <= DIO_DIR_EAST` | a mis-walked connection list |
| every `conn[i]` dims within the same caps | ditto |

**The contract on failure is one sentence:** `dio_world_load` returns **0**, `w->ok` is **0**, and
`main.c` draws today's flat frame for that screen this frame — exactly the PHASE §"What ships" #5
stance ("fall back rather than approximate") and exactly what phase 20's reason-code ladder does
for a peer sprite. There is no partial world, no half-classified grid and no stale geometry: a
failed load `memset`s. The caller re-attempts on the next map-change trigger, so a transient
mid-warp read costs one flat frame, not a locked-out feature.
## 5. CLASSIFY

### 5.1 The class enum — 14 members, **RULES-v1's order is binding**

The numeric values matter: they are what a `DIOG` golden's `u8 class[w*h]` array holds, so the C
enum must equal `dioref.py`'s `range(14)` exactly or every fixture test fails on the first byte.

```c
typedef enum {
	DIO_VOID      = 0,   /* outside every instance, or a blank indoor metatile -> mesh nothing  */
	DIO_FLAT      = 1,   /* ground quad at y = 0                                                */
	DIO_DECAL     = 2,   /* RESERVED (no producer in v1): ground quad lifted +0.02              */
	DIO_LOW       = 3,   /* box, top 0.40 — isolated solids: fences, rocks, signposts           */
	DIO_LEDGE     = 4,   /* box, top 0.40 — jumpable ledge                                      */
	DIO_WALL      = 5,   /* structural: facade / cliff face / mid-stack row                     */
	DIO_ROOF      = 6,   /* structural: topmost row of a solid stack                            */
	DIO_FURNITURE = 7,   /* box, top 1.00, own art on its sides — PC / TV                       */
	DIO_BED       = 8,   /* RESERVED: box, top 0.35                                             */
	DIO_TABLE     = 9,   /* RESERVED: slab 0.50 + apron + corner legs                           */
	DIO_COUNTER   = 10,  /* box, top 0.70                                                       */
	DIO_SIGN      = 11,  /* RESERVED: upright quad, 1.0 tall, at the tile's mid-depth           */
	DIO_WATER     = 12,  /* ground quad recessed -0.10                                          */
	DIO_VEG       = 13,  /* RULES-v1 D1: periodic band -> per-cell 2-tall box                   */
	DIO_CLASS_COUNT = 14
} DioClass;

/* The ONE structural test: grouping (§6), face culling (§7) and the column walk all use it.
   WALL and ROOF are adjacent by construction, so this stays a range test.                      */
static inline int dio_is_mass(int c) { return c == DIO_WALL || c == DIO_ROOF; }
```

`_Static_assert(DIO_CLASS_COUNT == 14)` and `_Static_assert(DIO_VEG == 13)` guard the tables in
§7.7, §11.1 and §11.2 against a silent insert — an insert would not merely renumber an enum, it
would silently invalidate every checked-in golden.

### 5.2 Rule order — RULES-v1 §3, first match wins

`dio_classify_at(const DioWorld* w, int wx, int wy)` is a strict ordered if-chain.
`dio_classify_all(w)` runs it for every cell of every instance and caches the result in `cls[]` —
**classify once per map into a shape grid**, RESEARCH §8.3.1's loudest "do NOT copy" (the
reference re-classified every tile, with 4+ neighbour re-classifications each, every frame).

Inputs gathered once at the top: owning instance (none → `DIO_VOID`), cell, `id = cell & 0x3FF`,
`solid = (cell >> 10) & 3`, `beh = attrBeh[id]`, `role = behRole[beh]`.

| # | Branch | Test | → | Note |
|---|---|---|---|---|
| R0 | both | no owning instance at (wx,wy) | **VOID** | RULES-v1 §3 rule 0 |
| R1 | indoor | `dio_id_blank(w, id)` | **VOID** | rule 1 — pure backdrop, the black filler outside a room. Beats collision, so filler never masses |
| R2 | indoor | `role == PC` or `role == TV` | **FURNITURE** | rule 2 |
| R3 | indoor | `role == SHELF` | **WALL** | rule 3 — full-height shelving reads as wall |
| R4 | indoor | `role == COUNTER` | **COUNTER** | rule 4 |
| R5 | indoor | `role == DOOR` | **WALL** | rule 5 — doors stay embedded in the facade plane |
| R6 | outdoor | `role == WATER` | **WATER** | rule 6 |
| R7 | outdoor | `role == GRASS` | **FLAT** | rule 7 — never volume. Grass is *walkable*: giving it collision-derived height is RESEARCH §8.3.13's trap |
| R8 | outdoor | `role == LEDGE` | **LEDGE** | rule 8 — all eight `MB_JUMP_*` |
| R9 | outdoor, `solid` | **the VEG test** (§5.3) | **VEG** | rule 9 — RULES-v1 D1 |
| R10 | both, `solid` | collision at (wx, wy−1) ≠ 0 | **WALL** | rule 10 |
| R11 | both, `solid` | collision at (wx, wy+1) ≠ 0 | **ROOF** | rule 11 |
| R12 | both, `solid` | — | **LOW** | rule 12 |
| R13 | indoor, passable | collision at (wx,wy−1) **and** (wx+1,wy) **and** (wx−1,wy) all ≠ 0 | **WALL** | rule 13, the alcove (§5.5) |
| R14 | both | — | **FLAT** | rule 14 |

Note the shape of the chain: the branch-specific *behaviour* rules come first (R1–R8), then the
**shared** solid rules (R9–R12), then the indoor-only passable rule (R13), then FLAT. R9–R12 run
for **both** map types — an indoor wall is still classified by the collision neighbourhood, which
is why an indoor room's perimeter comes out as ROOF-over-WALL and groups into one volume.

### 5.3 R9 — the VEG rule (RULES-v1 D1), and why it exists

```
if outdoor and solid:
    for dy in (-4, -3, -2, -1, +1, +2, +3, +4):
        (inst2, cell2) = lookup(wx, wy + dy)
        if inst2 valid and collision(cell2) != 0 and id(cell2) == id:
            return DIO_VEG
```

A solid cell whose **exact metatile id** reappears in another **solid** cell within four rows
north or south is part of a *periodic band* — a tree line, a hedge, a rock or cliff band. Bands
are the failure case the reference never solved (RESEARCH §3.1: trees "classify as generic
WALL/ROOF and get massed as buildings"): a 32-row tree line flood-fills into one blob whose column
walk is 32, and it renders as a cliff.

Evidence from the real previews (BUILDLOG, 2026-09-10): **without** the rule, Oldale's tree line
became one 32-tall wall and Rustboro's east band 36-tall; **with** it, per-map maximum structure
heights fall to 4 (Littleroot, Route 101, Oldale) and 6 (Rustboro — its real buildings). And
buildings survive the rule, because their stacked rows are *distinct* metatiles: the same-id test
is exactly what separates "the same tree drawn again" from "the next row of a house".

Three properties the implementation must keep, each pinned by a test:

- **Outdoor only.** Indoor maps skip R9 entirely (an indoor tileset repeats floor metatiles
  everywhere, and floors are passable anyway, but a repeated *solid* wall metatile is common —
  running VEG indoors would shred room perimeters).
- **The lookup crosses instances**, so a tree line continuing into the neighbour map still reads
  as periodic at the seam.
- **VEG is not structural.** `dio_is_mass(DIO_VEG)` is false, so VEG never seeds or joins a BFS
  fill (§6.2), and a VEG cell is a hard boundary for a neighbouring building's fill.

The cost is `≤ 8` extra cell lookups per solid outdoor cell, paid once per map inside
`dio_classify_all` — with the worst measured world at 22 400 cells and ~30 % solid, that is under
54 000 lookups, each a bounds test plus an array read.

### 5.4 R10–R12 — the collision-neighbourhood extrusion heuristic

```
if collision(wx, wy-1): WALL      /* NORTH, up-screen: a lower row of a stack -> facade   */
if collision(wx, wy+1): ROOF      /* SOUTH: nothing solid above, solid below -> stack top */
else:                   LOW       /* an isolated solid: fence, rock, signpost             */
```

Properties this spec commits to preserving (RESEARCH §3.4):

- **`above` is tested first.** Solid above *and* below ⇒ WALL, not ROOF. A 2-row stack is ROOF over
  WALL; a 1-row strip is entirely LOW.
- **Horizontal neighbours are ignored here.** A 5-long east-west fence stays 5×LOW however long —
  correct, fences have no roof art. Fusion happens later, in the mesh (§7.7).
- **Lookups read raw collision, not class**, so the sweep is order-independent and single-pass.
  (The *column walk* of §6.4 does read classes and runs strictly afterwards.)
- **Lookups cross instance boundaries** (§5.6).
- **R6–R9 shadow this.** A water tile next to a cliff never becomes WALL; a ledge never becomes
  LOW-by-collision; a tree-line cell never becomes WALL.

### 5.5 R13 — the alcove rule (indoor only)

> A **passable** tile whose **north, east and west** neighbours are all solid is WALL anyway.

It is a niche cut into the room's back wall (a doorway recess); drawing it as floor punches a
visible hole in the wall plane. The south neighbour is deliberately **not** required — the opening
faces the camera (RESEARCH §3.4). "Solid" is `collision != 0` on the raw cell, through the same
cross-instance accessor as R10.

**The known miss is kept, on purpose.** RESEARCH §8.3.10: the rule is exact-pattern, so a recess
whose *east* neighbour happens to be passable is missed and shows a floor hole. v1 keeps it
because it is the understood, golden-matched behaviour, and T3 pins **both** the hit and the miss
(the indoor fixture contains one of each). Generalising it — "a passable tile unreachable from the
walkable region is a wall cell" — needs a flood fill from the player and is §13 Q4.

### 5.6 Polarity — the rule that inverts everything if you get it wrong

**y grows SOUTH (down-screen). "Above" is `y − 1`. "Below" is `y + 1`.** World coords are map
coords plus the instance origin; render coords are `X = worldX (east)`, `Z = worldY (south)`,
`Y = height (up)`; one tile is 1.0 in X/Z and one storey is 1.0 in Y; a cell spans
`[wx, wx+1] × [wy, wy+1]` (RESEARCH §2, RULES-v1 §3).

RESEARCH §3.4 closes with: *"Screen-Y polarity is the classic port bug: if your map reader's y
grows the other way, WALL and ROOF swap and every building renders upside-down-massed."* So the
suite pins it with a test whose failure message says exactly that:

> **T1 (polarity).** A 2-row solid stack at rows `y` and `y+1` in a 3×4 fixture must classify
> row `y` = **ROOF** and row `y+1` = **WALL**. Re-run mirrored in y: the classes swap. A
> single-cell solid is LOW in both. Then the mesh half: the roof quad of that stack is emitted at
> the **smaller** Z. Mutation gate: swapping the two collision lookups in R10/R11 must fail T1,
> and must fail it with a message that names the swap.

### 5.7 Edge of world — why neighbours are instanced (PHASE bound 5)

`dio_cell_at(w, wx, wy)` resolves a world coordinate to an owning instance: **instance 0's rect
first** (the fast path — 457 of 518 Emerald maps have no neighbour at all), then a linear scan over
`nInst ≤ 8`. Outside every instance it returns the sentinel cell `0x0000` — metatile 0,
**collision 0**, elevation 0 — and `dio_class_at` returns `DIO_VOID` (RULES-v1 §2).

That is the whole reason neighbours exist:

- **With** the north neighbour instanced, a solid tile on the current map's row 0 whose northern
  neighbour (the connected map's last row) is solid classifies **WALL** — correct: a facade whose
  roof lives in the next map.
- **Without** it, the lookup returns collision 0 and that tile silently becomes **ROOF** (or LOW),
  producing the seam artifact RESEARCH §3.4/§6 names: a row of stray roofs along every border.

T6 builds exactly that pair — one world with a north connection, one without, same current map —
and asserts the edge cell flips WALL↔ROOF. It also asserts the four origin formulas of §4.4 step 4
with non-zero and negative offsets, and that a corner diagonal between two instanced neighbours is
VOID (RESEARCH §6: "diagonal gaps at corners simply classify VOID").

### 5.8 API

```c
int  dio_classify_at (const DioWorld* w, int wx, int wy);   /* one cell, the §5.2 chain          */
void dio_classify_all(DioWorld* w);                          /* every cell of every instance      */
int  dio_class_at    (const DioWorld* w, int wx, int wy);    /* the CACHED class, or DIO_VOID     */
uint16_t dio_cell_at (const DioWorld* w, int wx, int wy);    /* the raw cell, or 0x0000           */
int  dio_coll_at     (const DioWorld* w, int wx, int wy);    /* collision, or 0 outside           */
int  dio_inst_at     (const DioWorld* w, int wx, int wy);    /* instance index, or DIO_INST_NONE  */
long dio_index_at    (const DioWorld* w, int wx, int wy);    /* index into cells[]/cls[], or -1   */
```

`dio_classify_all` is one doubly-bounded loop per instance, so its total iteration count is
provably `≤ DIO_MAX_CELLS` (golden rule 2) and it asserts that on entry and exit.
## 6. STRUCTURES

### 6.1 Why grouping exists

RESEARCH §4.3: per-tile extrusion gives **sawtooth roofs** (every facade column its own height, so
a house with a door gap becomes a comb of pillars), **interior walls** (quads between adjacent
solid tiles inside the blob, z-fighting at grazing angles) and **incoherent cutaway** (you cannot
lower "the building the player is behind" if the building is not an object). Grouping fixes all
three at once, and PHASE bound 4 makes it binding: *"Per-TILE massing with grouped structures —
never per-pixel heights."*

### 6.2 The fill — BFS with an explicit fixed queue, no recursion

```c
/* Groups every WALL/ROOF cell into structures. Pure: no bus, no allocation, no recursion.
   Returns the number of structures found. Sets DIO_WF_STRUCTS / DIO_WF_STRUCTTILE on overflow. */
int dio_group_structures(DioWorld* w);
```

- **Seeds.** Row-major **in WORLD order (y then x, across all instances)** — RULES-v1 §4 — seeded
  iff `dio_is_mass(cls[i])` and `sid[i] == DIO_SID_NONE`. World order, not per-instance order,
  because a blob may straddle a connection seam. (RESEARCH §4.1 also seeds TREE fills; `TREE` no
  longer exists, §1.3 — and `VEG` is explicitly **not** a member class, RULES-v1 §4, which is what
  keeps a tree line out of the building next to it.)
- **Fill.** Plain **BFS over the 4-neighbourhood** (N/S/E/W, no diagonals). A neighbour joins iff
  `dio_is_mass(class)` — WALL or ROOF, and *only* those two; VEG, LOW, WATER, FLAT and VOID are all
  hard boundaries.
- **The queue is `w->bfsQ[DIO_MAX_STRUCT_TILES]`**, a plain `uint32_t` array of cell indices with
  an integer `head`/`tail`. There is **no recursion anywhere in this module** (golden rule 1), and
  the loop bound is `tail ≤ DIO_MAX_STRUCT_TILES`, provable by inspection (golden rule 2).
- **`sid[]` doubles as the visited set** — a cell is stamped with its structure id *at enqueue
  time*, so it can never be enqueued twice and no separate visited bitmap exists. That single
  choice removes the reference's 4 MB 2048² visited grid (RESEARCH §4.2, §8.3.4) **and** its
  O(structures × tiles) membership scans (§8.3.5): membership is `sid[index] == s`, O(1).
- **Per structure we record** bbox (inclusive), `nTiles`, the seed's instance and, after the fill,
  the height (§6.4). We do **not** record a tile list — `sid[]` is the tile list, indexed the
  other way round.

Cost, from the real fixtures: structural cells (WALL+ROOF) are 4.4 % of Rustboro's world and
10.6 % of Battle Frontier West's, so a 22 400-cell world costs at most a few thousand enqueues —
a few hundred microseconds, once per map. (VEG is what pulled that number down: on the same maps
it claims 5–6 % of all cells.)

### 6.3 The structure-id grid

`sid[DIO_MAX_CELLS]` (`uint16_t`, `DIO_SID_NONE` = 0xFFFF) is the answer to RESEARCH §8.3.5. It is
also what makes §7's exterior-face rule a two-instruction test and what lets `dio_cutaway_update`
(§6.5) run every frame at zero cost per cell.

### 6.4 Height — max column walk, clamped to `DIO_H_MAX = 6`

The per-member column walk, stated **exactly as `dioref.py` runs it** so the C module reproduces a
`DIOG` golden's `height` field record-for-record:

```c
/* Counts cells going NORTH from (wx,wy) while the class is WALL or ROOF, counting the first ROOF
   it meets and stopping after it. Minimum 1. The loop is bounded by DIO_H_MAX because the caller
   clamps to it anyway, so an unbounded walk could never change the answer (golden rule 2).      */
int dio_column_height(const DioWorld* w, int wx, int wy);
```

```
k = 0 ; yy = wy
while k < DIO_H_MAX and class(wx, yy) in {WALL, ROOF}:
    k += 1
    if class(wx, yy) == ROOF: break
    yy -= 1
return max(k, 1)
```

The `k < DIO_H_MAX` guard is a safety bound, not a behaviour change: the result is clamped to
`DIO_H_MAX` immediately after, so `min(rawWalk, DIO_H_MAX)` is identical either way — T7 asserts
that equivalence on a 40-row column.

**The structure's shared height = `min(DIO_H_MAX, max over members of dio_column_height)`.** One
height for the whole volume ⇒ a flat, coherent roof plane (RESEARCH §4.3's "one height per
structure").

**Why 6 and not the reference's unbounded walk (RULES-v1 D2).** Run over the eight ROM fixtures,
`DIO_H_MAX = 6` binds in exactly the places it should: Rustboro, Petalburg, Oldale and the Battle
Arena lobby all reach 6, Littleroot and Route 101 top out at 4 on their own. BUILDLOG's finding is
that adjacent solid scenery (Petalburg's gym hedge) fuses into the building blob and dragged one
structure to 9 — so the cap is what stops a fused hedge from lifting a gym. **Disclosed
limitation:** fused scenery shares the building's height. Before VEG existed the same measurement
gave raw walks of 32 and 36 on tree lines; VEG (§5.3) removes that class of victim and the cap
handles the residue.

**Golden ordering.** `structs[]` is filled in **seed order**, which is row-major over the seed
tile — and that is *not* the DIOG golden's order, which sorts by
`(miny, minx, maxy, maxx)`. They differ for any L-shaped blob whose leftmost cell is not on its
topmost row. The host suite therefore **sorts a copy of the index array** by those four keys before
comparing (§12.6); the module itself is not required to store a sorted table, and T7 asserts both
facts (seed order is stable; sorted order matches the golden).

### 6.5 Cutaway — per structure, recomputed per frame, integer rule

```c
/* Sets/clears DIO_SF_CUTAWAY on every structure from the player's CURRENT tile. O(nStructs)
   (<= 1024 compares); safe to call every frame; does NOT invalidate the mesh by itself.        */
void dio_cutaway_update(DioWorld* w, int playerWX, int playerWY);
```

A structure is cut away iff its bbox extends **south of** the player (RULES-v1 §4: `maxy > playerY + 0.5`):

```
cutaway = (s->y1 > playerWY)
```

Derivation from RESEARCH §4.3's `maxY > playerZ + 0.5`: the structure occupies rows `y0..y1`
inclusive, i.e. Z ∈ `[y0, y1+1]`; the player's centre is at Z = `playerWY + 0.5`; so
`y1 + 1 > playerWY + 1` ⇔ `y1 > playerWY`. Integer, exact, no epsilon.

**Effect** (RESEARCH §4.3, §5.3): the structure renders at `height = 1`, its **roof quads are
suppressed** and its **north faces are suppressed**, so a building between the camera (which is
always south, §8) and the player collapses and the player stays visible. Because every non-VOID
cell also gets a ground quad (§7.4), a collapsed structure never reveals a hole in the floor.

**Where cutaway is consumed matters.** `dio_cutaway_update` only sets flags; the geometry that
reads them is emitted by `dio_mesh_build`. So a cutaway change requires a mesh rebuild, and §10.3
makes the player's tile part of the rebuild trigger. The alternative (emit both states and switch
with a shader uniform) is SPEC-render's call and is recorded in §13 Q5 — the flag-plus-rebuild
form is specified here because it is the one the host suite can prove.

RESEARCH §8.3.9's criticism is accepted and deliberately deferred: the bbox test is crude and the
collapse is binary, so whole structures pop between two states. What we keep is the thing the
research got right — **structure granularity**, so the hook is in the correct place for a later
fade or top-slice.

### 6.6 Caps — what happens, exactly

| Cap | Reached when | Behaviour | Flag |
|---|---|---|---|
| `DIO_MAX_STRUCTS` (1024) | more than 1024 blobs in one world (projected worst case ~672) | seeding **stops**; remaining WALL/ROOF cells keep `sid == DIO_SID_NONE` and are meshed as **per-cell volumes** by the §7.6 fallback — they still get geometry, just ungrouped | `DIO_WF_STRUCTS` |
| `DIO_MAX_STRUCT_TILES` (2048) | one blob exceeds 2048 tiles (measured max 156) | the fill **stops mid-BFS**; the tiles already stamped form the structure and carry `DIO_SF_TRUNCATED`; the *unreached* tiles keep `DIO_SID_NONE` and fall to the same §7.6 fallback | `DIO_WF_STRUCTTILE` |

RULES-v1 §5 states that a WALL/ROOF cell not reached by any structure "cannot exist" — and under
the caps above, on real Emerald data, it cannot. §7.6 exists for the case where that stops being
true: it is the *only* consumer of `sid == DIO_SID_NONE`, and it is why an overflow degrades to
"ungrouped massing" instead of "a hole where a building was". Neither cap is undefined behaviour
and neither writes out of bounds. T7 forces both with synthetic worlds (`DIO_MAX_STRUCTS+3`
separate blobs; one blob of `DIO_MAX_STRUCT_TILES+64` tiles) and asserts the flags, the truncation
mark, the absence of out-of-range `sid` values, and that the fallback covers every leftover cell.

### 6.7 API

```c
int  dio_group_structures(DioWorld* w);
void dio_cutaway_update  (DioWorld* w, int playerWX, int playerWY);
int  dio_column_height   (const DioWorld* w, int wx, int wy);
int  dio_struct_at       (const DioWorld* w, int wx, int wy);   /* sid or -1                    */
const DioStruct* dio_struct(const DioWorld* w, int s);          /* NULL if out of range         */
```
## 7. MESH

### 7.1 The output contract

```c
/* ONE vertex format for terrain, massing and billboards, so the render side binds one attribute
   layout and one shader. 9 floats = 36 bytes; the colour is FLOATS (not a packed u32) to match
   SPEC-render's vertex rule and to keep this module free of any packing convention.            */
typedef struct {
	float x, y, z;      /* world units: X east, Y up (1.0 = one storey), Z south                */
	float u, v;         /* atlas UV, 0..1                                                        */
	float r, g, b, a;   /* per-vertex tint, 0..1; a == 1.0 for every vertex this module emits    */
} DioVert;              /* 36 bytes; _Static_assert(sizeof(DioVert) == 36)                       */

#define DIO_MAX_VERTS    65536u    /* == the u16 index ceiling; see §7.9                        */
#define DIO_MAX_INDICES  98304u    /* 1.5 x verts (6 indices per 4-vertex quad)                 */

typedef struct {
	DioVert*  v;           /* caller-provided, >= vCap entries (linear heap, SPEC-render)       */
	uint16_t* idx;         /* caller-provided, >= iCap entries                                  */
	uint32_t  vCap, iCap;
	uint32_t  nVerts, nIdx;
	uint32_t  rowFirst[DIO_MAX_ROWS + 1];  /* rowFirst[r] = first index of world row wy0+r      */
	uint16_t  nRows;
	uint8_t   flags;       /* DIO_MF_*                                                           */
	uint8_t   pad;
	uint32_t  nQuads, nDropped;  /* quads emitted / quads refused for lack of room              */
} DioMesh;

#define DIO_MF_VERT_FULL  0x01u   /* vCap reached — emission stopped cleanly                    */
#define DIO_MF_IDX_FULL   0x02u   /* iCap reached — ditto                                       */
#define DIO_MF_CLIPPED    0x04u   /* the build used a clip rect smaller than the world (§7.9)   */
```

The **capacity contract**: `dio_emit_quad` refuses to start a quad unless *both* 4 vertices and 6
indices fit; on refusal it raises the flag, bumps `nDropped` and returns 0. There is no partial
quad and no out-of-bounds write, ever — a precondition assert plus a runtime test (golden rules 5
and 7). A full buffer degrades to "the far part of the world is missing", never to a corrupt draw.

**Winding and UV — one rule for every face in this spec.** Vertices go
`p0 = bottom-left, p1 = bottom-right, p2 = top-right, p3 = top-left` **as seen from outside the
surface**, with `uv = (u0,v1), (u1,v1), (u1,v0), (u0,v0)` and indices `(0,1,2), (0,2,3)`. The
outward normal is then `(p1−p0) × (p2−p0)` and the triangles are counter-clockwise from outside.
For a cell at `(wx, wy)`:

| Face | p0 | p1 | p2 | p3 | outward normal |
|---|---|---|---|---|---|
| top / ground (y = `h`) | `(wx, h, wy+1)` | `(wx+1, h, wy+1)` | `(wx+1, h, wy)` | `(wx, h, wy)` | +Y |
| south (z = `wy+1`) | `(wx, y0, z)` | `(wx+1, y0, z)` | `(wx+1, y1, z)` | `(wx, y1, z)` | +Z |
| north (z = `wy`) | `(wx+1, y0, z)` | `(wx, y0, z)` | `(wx, y1, z)` | `(wx+1, y1, z)` | −Z |
| east (x = `wx+1`) | `(x, y0, wy+1)` | `(x, y0, wy)` | `(x, y1, wy)` | `(x, y1, wy+1)` | +X |
| west (x = `wx`) | `(x, y0, wy)` | `(x, y0, wy+1)` | `(x, y1, wy+1)` | `(x, y1, wy)` | −X |

The top-face order (SW, SE, NE, NW) is what makes `v` grow **south** on the ground exactly as the
metatile art's `v` grows down; backwards, every roof is mirrored north–south. T11 asserts one
normal of every kind by computing the cross product inside the test.

RESEARCH §7.2 notes the reference disables backface culling because its winding is inconsistent.
Ours is consistent by construction, so SPEC-render **may** enable culling; §13 Q6 records the one
shape that would then need both sides (the reserved SIGN).

### 7.2 Texturing: the atlas is data, not a callback

```c
/* Atlas geometry only — the module never sees a pixel. Slot of metatile id m is
   (m % perRow, m / perRow) * slotPx  (RESEARCH §5.1; the DIOA golden is the same 1024-slot,
   256-texel-per-metatile ordering).                                                            */
typedef struct {
	uint16_t dim;      /* 512                                                                    */
	uint16_t slotPx;   /* 16                                                                     */
	uint16_t perRow;   /* 32                                                                     */
	uint16_t pad;
	float    solidU, solidV;  /* UV of one fully-opaque WHITE texel: the "untextured" sample     */
} DioAtlas;

void dio_uv_of(const DioAtlas* a, uint16_t id, float* u0, float* v0, float* u1, float* v1);
```

No per-vertex function pointer (golden rule 9): the caller hands over four numbers and the module
does the arithmetic inline. `solidU/solidV` is how an "untextured" face — a box side, a dark
neighbour — is drawn **without a second texture, a second draw call or a shader branch**: it
samples one white texel and takes its whole colour from the vertex tint. SPEC-render must reserve
that texel; with 1024 ids × 32 per row the 512² atlas is exactly full, so the reservation is
SPEC-render's problem and is recorded as §13 Q2.

**Cross-instance texturing** (RESEARCH §5.2.6 / §8.3.8) is fixed here rather than inherited: a face
textured from a row that lies in a *different* instance takes its slot from the **owning instance
of the source row**, and if that instance's `textured` flag is 0 the face falls back to the solid
texel at `DIO_MAT_UNTEX`. A neighbour is never drawn with the wrong art (PHASE bound 5, RULES-v1
§2).

### 7.3 Tints — ONE unified fake-sun set

RESEARCH §8.2 lists "two inconsistent sets (§5.3 vs §5.4) — unify in our port". RULES-v1 §5 fixes
the set; this is it:

```c
#define DIO_TINT_TOP    1.00f
#define DIO_TINT_NORTH  0.90f
#define DIO_TINT_SOUTH  0.85f
#define DIO_TINT_EAST   0.82f
#define DIO_TINT_WEST   0.75f
```

A face's colour is `tint × material`, where the **material** is 1.0 for every textured face (so the
art arrives unmodified except for the fake sun) and a fixed grey for the untextured sub-parts:

| Material | Value | Used by |
|---|---|---|
| `DIO_MAT_TEX` | 1.00 | every textured face: ground, roofs, structure levels, VEG levels, box tops, FURNITURE sides |
| `DIO_MAT_BOX` | 0.55 | LOW / LEDGE / BED box sides |
| `DIO_MAT_COUNTER` | 0.60 | COUNTER box sides |
| `DIO_MAT_APRON` | 0.35 | the reserved TABLE's apron and legs |
| `DIO_MAT_UNTEX` | 0.28 | every face of a `textured == 0` neighbour instance (PHASE bound 5) |

So a LOW fence's south side is `0.85 × 0.55 = 0.4675` grey — the research's hand-tuned greys, now
*derived* from one directional set instead of a second one. T11 asserts the product for one face
of each material.

### 7.4 One row-major sweep, four recipe families

RESEARCH §7.2 ordered the reference's frame as wall-column pass → structure pass → per-tile pass.
**RULES-v1 §5 folds the wall-column pass into the structure pass** ("every WALL/ROOF cell is a
structure member by construction"), so what remains is scheduled as a single **row-major sweep**
over world rows `wy0..wy1`, and within a row over `wx0..wx1`, dispatching each cell to its family.

Why one sweep: (a) it makes `rowFirst[]` (§7.8) meaningful, letting the render side draw a
contiguous z-band; (b) it makes `dio_mesh_build` a **pure function of `DioWorld`** with no
inter-pass ordering hazard. The one genuinely order-dependent thing the reference had — a column
consuming rows to its north that a row-major sweep would already have emitted — is hoisted out of
emission entirely, into `dio_columns_mark()` at load time (§7.6).

Per cell, in order:

1. `cls == DIO_VOID` → **emit nothing at all** (RULES-v1 §5).
2. **Ground quad.** Every non-VOID cell gets one textured quad at `y = groundY(cls)` (0.0 for
   most, **+0.02** DECAL, **−0.10** WATER), *unless* its own recipe covers the whole footprint at
   or above 0 — `dio_covers_ground(c)` is true for LOW, LEDGE, COUNTER, FURNITURE, BED and VEG.
   **WALL/ROOF cells do get a ground quad**: that is what guarantees a cut-away structure (§6.5)
   shows floor and not sky.
3. **Family S — structure member** (`sid != DIO_SID_NONE`): §7.5.
4. **Family V — VEG** (`cls == DIO_VEG`): §7.7.
5. **Family T — per-tile box/card** (everything else): §7.7.
6. **Family C — the cap-overflow fallback** (`dio_is_mass(cls)` but `sid == DIO_SID_NONE`): §7.6.

### 7.5 Family S — the structure pass

Per member tile of structure `s` with shared height `H` (§6.4) and `cut = (flags & DIO_SF_CUTAWAY)`:

- **Roof quad** at `y = (cut ? 1 : H)`, **skipped entirely when `cut`**, textured with the metatile
  `H − 1` rows north of the member — `(wx, wy − (H−1))` — which is the map's own roof art
  (RESEARCH §5.3, RULES-v1 §5). If that row lies outside every instance, fall back to the member's
  own metatile.
- **Wall faces, exterior only**: for each of the four sides, emit iff the neighbour on that side is
  **not** `dio_is_mass` — interior faces vanish (RESEARCH §4.3). North faces are additionally
  skipped when `cut`. Each face is `Heff = (cut ? 1 : H)` stacked 1-unit quads, level `i` textured
  with the metatile of `(wx, wy − i)`, tinted by direction (§7.3).
- **Interior faces are exactly zero**, and T8 proves it by counting: for the §12 house fixture the
  emitted side-face count equals the hand-computed exterior count, and the number of
  member-to-member adjacencies that produced a face is 0.

**Per-level vs per-column culling.** RESEARCH §8.3.7 records a real bug: the reference samples
east/west neighbours only at the base row, so a tall column beside a short one culls faces it
needs and leaves holes. Inside family S the bug cannot occur — a 4-neighbour that is
`dio_is_mass` necessarily belongs to the **same** structure (BFS would have merged them) and
therefore has the **same** height. The only place a height discontinuity can meet a structural
neighbour is family C, and §7.6 culls per level there. Stating this is worth the paragraph: it is
why the fix lives in one pass and not two.

### 7.6 Family C — the cap-overflow fallback (and `consumed[]`)

Under §6.6's caps, on real Emerald data, every WALL/ROOF cell is claimed by a structure — RULES-v1
§5 says so outright. Family C is what happens **if that ever stops being true** (a struct-cap or
tile-cap overflow), so that an overflow degrades instead of holing.

`dio_columns_mark(DioWorld* w)` runs once at load (step 10 of §4.4) and is a **no-op unless
`wflags` carries `DIO_WF_STRUCTS`/`DIO_WF_STRUCTTILE`**:

```
for every cell with dio_is_mass(cls) and sid == DIO_SID_NONE and consumed == 0, row-major:
    h = dio_column_height(w, wx, wy)          /* bounded by DIO_H_MAX */
    for k = 1 .. h-1: consumed[index(wx, wy-k)] = 1
```

`consumed[]` exists for exactly this: it stops a vertical run of unclaimed cells from emitting
three overlapping volumes (heights 3, 2, 1) instead of one. Emission for a marked column's base
cell is the structure recipe applied to a one-cell volume: top quad at `y = h` with the top row's
art, `h` stacked level quads per uncalled side, faces culled **per level** (sample the east/west
neighbour at `(wx±1, wy−i)`, not at the base row — RESEARCH §8.3.7's fix), the north neighbour
sampled beyond the column top at `(wx, wy−h)`, a face suppressed when that neighbour is WALL, ROOF
or VOID, and — if all four would be culled — **the south face forced on** (RESEARCH §5.2.3), so an
entombed column is never invisible-but-present. Cutaway uses the cell's own `wy > playerWY`.

Cells with `consumed[i] == 1` are **demoted to FLAT** for families C and T (RESEARCH §5.2.2's
"there must be floor beneath/behind a wall"): they keep the ground quad they got in step 2 and emit
no volume of their own.

### 7.7 Families V and T — the per-cell recipes

`(wx, wy)` spans X `[wx, wx+1]`, Z `[wy, wy+1]`; `R` = the cell's own atlas UV rect. **Side culling
for every box class:** a side is skipped when the neighbour on that side has the **same class**
(fusion — a run of fence tiles becomes one continuous block with no interior seams) **or** is VOID
(RESEARCH §5.4, RULES-v1 §5).

| Class | Geometry | Verts (max) |
|---|---|---:|
| **FLAT** | ground quad at 0, texture `R`, tint TOP | 4 |
| **WATER** | ground quad at **−0.10**, texture `R`, tint TOP — reads as water below bank level | 4 |
| **VEG** *(family V)* | **box, top 2.0**, top textured `R` tint TOP; **two levels** of side faces, level 0 textured `R`, **level 1 textured with the art of row `wy − 1`** (so a tree's canopy row wraps onto the upper band); sides culled against **VEG** neighbours and VOID | 4 + 4×2×4 = 36 |
| **LOW** | box top **0.40**, top `R` tint TOP; 4 sides at the solid texel, material `DIO_MAT_BOX`, fusion-culled | 20 |
| **LEDGE** | identical box to LOW — a jumpable ledge is a half-height step. Separate class for the overlay and later polish | 20 |
| **COUNTER** | box top **0.70** (waist height), sides `DIO_MAT_COUNTER` | 20 |
| **FURNITURE** | box top **1.00**, top textured `R` tint TOP, **sides textured with `R` too** (level 0 art — RULES-v1 §5: an appliance shows its own face), fusion-culled | 20 |
| **WALL / ROOF** | ground quad only here; the volume is family S or C | 4 |
| **VOID** | nothing | 0 |
| *reserved* **DECAL** | ground quad at **+0.02** — the z-fight guard for mats over the floor; the lift must beat the depth buffer's quantisation, which §8.4 puts at 0.006 units at 20 units | 4 |
| *reserved* **BED** | box top **0.35**, top textured `R`, sides `DIO_MAT_BOX`, fusion active so a 2×1 bed is one slab. *(The reference classifies BED and draws nothing — RESEARCH §5.4 flags it; this is the recipe it lacked.)* | 20 |
| *reserved* **TABLE** | slab top **0.50** (`R`, TOP); **apron** on each non-connected side, y `[0.40, 0.50]`, `DIO_MAT_APRON`; **legs** 0.10×0.10 from 0 to 0.40 at each corner where *both* adjoining sides are non-connected, so a 2×1 desk gets 4 outer legs, not 8 | 84 |
| *reserved* **SIGN** | ground quad at 0 **plus** one upright quad, 1.0 tall, at the tile's mid-depth `z = wy + 0.5`, spanning x `[wx, wx+1]`, y `[0, 1]`, textured `R`, tint SOUTH — a thin "cardboard cutout", never extruded | 8 |

The four *reserved* rows have no producer in v1 (§1.3) and are specified so a later per-tileset id
table switches them on without a code change. RESEARCH §5.4's unreachable **chair** branch (inset
seat + backrest) is deliberately **not** a class: chairs fall to LOW via collision, which BUILDLOG
calls "acceptable v1", and the idea is parked in §13 Q7 rather than shipped as a second dead
branch.

### 7.8 The build call

```c
typedef struct {
	DioAtlas atlas;
	int      clipX0, clipY0, clipX1, clipY1;  /* world tile rect, exclusive hi. Pass the world
	                                             bbox for a full build (§7.9)                    */
	uint8_t  untexturedMassing;  /* 1 = neighbours with textured==0 still get massing, drawn at
	                                DIO_MAT_UNTEX; 0 = those instances emit dark ground only     */
	uint8_t  pad[3];
} DioMeshIn;

/* Emits the whole static scene for the clip rect, row-major. Pure: no bus, no allocation.
   Returns 1 if everything fit, 0 if a capacity flag was raised (the mesh is still drawable).   */
int dio_mesh_build(const DioWorld* w, const DioMeshIn* in, DioMesh* m);
```

`rowFirst[r]` is written as world row `wy0 + r` begins, and `rowFirst[nRows]` is the total index
count, so `[rowFirst[a], rowFirst[b])` is a contiguous, directly drawable **z-band**. A band must
be grown `DIO_H_MAX` rows to the **north** of the visible rows, because a tall volume's geometry
belongs to its southernmost base row.

### 7.9 Worst-case vertex count — measured on the real fixtures

These recipes, run over the eight ROM-dumped fixtures with RULES-v1's classifier (`VEG`, `HMAX=6`):

| Fixture | world cells | structs | max struct | max H | **verts** | verts/cell |
|---|---:|---:|---:|---:|---:|---:|
| BattleFrontier_OutsideWest (56×72, 2 inst) | 9 216 | 184 | 152 | 6 | **105 272** | 11.42 |
| RustboroCity (40×60, 4 inst) | 10 800 | 319 | 156 | 6 | **113 836** | 10.54 |
| PetalburgCity (30×30, 3 inst) | 5 100 | 109 | 111 | 6 | **43 308** | 8.49 |
| OldaleTown (20×20, 4 inst) | 3 560 | 88 | 123 | 6 | **35 132** | 9.87 |
| Route101 (20×20, 3 inst) | 1 200 | 36 | 20 | 4 | **11 252** | 9.38 |
| LittlerootTown (20×20, 2 inst) | 800 | 23 | 20 | 4 | **7 812** | 9.77 |
| BattleFrontier_BattleArenaLobby (16×13, indoor) | 208 | 2 | 132 | 6 | **3 112** | 14.96 |
| LittlerootTown_BrendansHouse_1F (11×9, indoor) | 99 | 2 | 31 | 3 | **1 000** | 10.10 |

So the honest constant is **≈ 8.5–11.5 vertices per world cell outdoors**, with a **15.0** outlier
on a tiny indoor lobby where one 132-tile height-6 room dominates 208 cells. **Budget at 12.**

Consequences:

- **`DIO_MAX_VERTS = 65536` is not arbitrary** — it is the u16 index ceiling. A single draw with
  `uint16_t` indices addresses at most 65 536 distinct vertices, and PICA200/citro3d index formats
  are `u8`/`u16` only. Sizing the buffer to exactly that ceiling means v1 never needs multi-batch
  base-vertex bookkeeping.
- At 12 verts/cell that is **5 461 cells**, and **474 of Emerald's 518 maps (91.5 %) fit their
  entire world — map plus every first-ring neighbour — in one buffer**, because the median map is
  165 cells. PHASE bound 7 ("static geometry built once per map") therefore holds *literally* for
  nine maps in ten.
- **The 44 maps that do not fit** are the big towns and water routes (Rustboro's world already
  needs 113 836 verts; Route 124's 22 400-cell world extrapolates to ~270 000 ≈ 9.2 MiB). For
  those, `dio_mesh_build` takes a **clip rect** — a window centred on the player — and sets
  `DIO_MF_CLIPPED`. **64×64** is the specified default: the camera at its maximum distance sees a
  ground footprint of about **30 wide × 20 deep** (derived in §8.3), so 64×64 is a >2× margin,
  costs `4096 × 12 ≈ 49 200` verts, and needs a rebuild only when the player leaves the inner
  32×32 — about once every 16 tiles walked.
- **The policy in one line:** *if `worldCells × 12 ≤ DIO_MAX_VERTS`, build the whole world once per
  map; otherwise build a 64×64 clip window and rebuild it on window exit.*
  `dio_mesh_budget_cells()` returns `DIO_MAX_VERTS / 12 = 5461` so `main.c` decides from a number,
  not a guess. Note for SPEC-render: even a *fitting* world is more geometry than one frame should
  transform, which is what `rowFirst[]` is for.

Index count is `nVerts / 4 × 6 = 1.5 × nVerts`, hence `DIO_MAX_INDICES = 98304`. Total caller-owned
mesh memory: `65536×36 + 98304×2 + 289×4 = 2 557 060 B ≈ 2.44 MiB` of linear heap (§3.5).

### 7.10 API

```c
int  dio_mesh_build (const DioWorld* w, const DioMeshIn* in, DioMesh* m);
void dio_mesh_reset (DioMesh* m);        /* counts/flags to 0; does not touch the buffers        */
int  dio_emit_quad  (DioMesh* m, const float p[12], const float uv[8], float r, float g, float b);
void dio_uv_of      (const DioAtlas* a, uint16_t id, float* u0, float* v0, float* u1, float* v1);
void dio_columns_mark(DioWorld* w);      /* §7.6; no-op unless a structure cap overflowed        */
uint32_t dio_mesh_budget_cells(void);    /* DIO_MAX_VERTS / 12 == 5461                           */
int  dio_covers_ground(int cls);         /* LOW/LEDGE/COUNTER/FURNITURE/BED/VEG                  */
float dio_class_height(int cls);         /* 0, .02, -.1, .4, .7, 1.0, .35, .5, 2.0 per §7.7      */
```
## 8. CAMERA

### 8.1 State and defaults

```c
typedef struct {
	/* --- the follow target: a point on the ground plane, in WORLD tile units --------------- */
	float tx, tz;
	/* --- rig (RESEARCH §7.1) ------------------------------------------------------------- */
	float pitchRad;    /* DIO_CAM_PITCH_DEG 40 deg downward (pleasing range 35-50)             */
	float yawRad;      /* 0 = looking due NORTH. LOAD-BEARING: billboards yaw to it (§9)       */
	float dist;        /* adaptive, §8.2                                                        */
	float fovyRad;     /* DIO_CAM_FOV_DEG 35 deg — tight/telephoto, flattens toward "diorama"  */
	float nearZ, farZ; /* §8.4                                                                  */
	/* --- outputs, recomputed by dio_camera_update ----------------------------------------- */
	float eye[3], target[3], up[3];
	uint8_t snapArmed;  /* set by dio_camera_arm_snap(); consumed by the next update            */
	uint8_t pad[3];
} DioCam;

#define DIO_CAM_PITCH_DEG   40.0f
#define DIO_CAM_YAW_DEG      0.0f
#define DIO_CAM_FOV_DEG     35.0f
#define DIO_CAM_DIST_BASE    8.0f
#define DIO_CAM_DIST_K       0.2f
#define DIO_CAM_DIST_ADD_MAX 5.0f
#define DIO_CAM_FOLLOW       0.15f    /* per-frame exponential chase (RESEARCH §7.1)           */
#define DIO_CAM_NEAR         1.0f     /* §8.4 — NOT the reference's 0.1                         */
#define DIO_CAM_FAR        120.0f     /* §8.4 — NOT the reference's 200                         */
```

### 8.2 `dio_camera_update`

```c
/* dtFrames is in 60 Hz FRAMES (1.0 = one frame), so the caller may pass a fractional value from
   the wall clock exactly as tilt.c does with dtMs. Player position is the INTERPOLATED sub-tile
   world position (SPEC-data owns producing it, PHASE O1). mapW/mapH size the rig.              */
void dio_camera_init  (DioCam* c, int mapW, int mapH, float px, float pz);
void dio_camera_update(DioCam* c, float dtFrames, float px, float pz, int mapW, int mapH);
void dio_camera_snap  (DioCam* c, float px, float pz);      /* warp: target jumps               */
void dio_camera_rebase(DioCam* c, int dx, int dy);          /* walk-through connection, §8.5    */
void dio_camera_arm_snap(DioCam* c);                        /* gate re-open: snap next update   */
```

Each update, in order:

1. **Distance adapts to map size** — `dist = 8 + min(0.2 × max(mapW, mapH), 5)`, i.e. **8..13**
   tiles: small rooms frame closer, towns further (RESEARCH §7.1). Goldens: 10×10 → **10.0**,
   20×20 → **12.0**, and 30×30, 40×20, 40×60, 80×80, 40×140 all → **13.0** (the clamp binds from
   `max(w,h) ≥ 25`).
2. **Follow smoothing** — `t += (p − t) × a`, per axis X/Z, with
   `a = 1 − powf(1 − DIO_CAM_FOLLOW, dtFrames)`. At `dtFrames == 1` that is exactly 0.15, which
   makes the shipped behaviour identical to the reference's per-frame chase while staying correct
   for a fractional or dropped frame — the same argument `tilt.c` makes for a wall-clock tween.
   Goldens from `t = 0` toward `p = 10`: **1.5** (1 frame), **2.775** (2), **5.5629468750** (5),
   **8.0312559566** (10), **9.6124046892** (20). Half-frame `a = 0.0780455543`.
3. **Snap** — if `snapArmed`, `t` is set to `p` outright and the flag cleared (§8.5).
4. **Placement** (RESEARCH §7.1): the camera sits **south of and above** the target, looking north.
   ```
   eye    = ( tx + sin(yaw)·dist,  tan(pitch)·dist,  tz + cos(yaw)·dist )
   target = ( tx,                  0,                tz                 )
   up     = ( 0, 1, 0 )
   ```
   With yaw 0 and dist 12: `tan(40°) = 0.8390996312`, so a target of `(10, 0, 12)` gives
   **eye = (10, 10.0691955741, 24)** — the T14 golden. The eye-to-target distance is
   `dist / cos(pitch) = 15.6648874725`.

### 8.3 What the camera actually sees (the number §7.9 depends on)

At the maximum rig (`dist = 13`, pitch 40°, fovy 35°, top-screen aspect 400/240):

| | value |
|---|---|
| eye height | `tan(40°)×13 = 10.908` |
| near ground hit | `10.908 / tan(40°+17.5°) = 6.95` from the eye |
| far ground hit | `10.908 / tan(40°−17.5°) = 26.33` |
| **visible depth band** | **19.4 tiles** |
| far ground point distance | `28.50` |
| half-width there | `28.50 × (400/240) × tan(17.5°) = 14.98` |
| **visible width at the far edge** | **≈ 30 tiles** |

Hence §7.9's 64×64 clip window is a >2× margin in both axes, and the "grow the z-band by
`DIO_H_MAX` rows north" rule covers the tallest volume leaning into view.

### 8.4 Near and far — **changed from the reference, and the reason is the 16-bit depth buffer**

The reference uses `near 0.1 / far 200` (RESEARCH §7.1). PHASE "Existing machinery" records that
`C2D_CreateScreenTarget` gives the top target **`GPU_RB_DEPTH16`** — 65 535 depth steps. World-space
depth resolution at distance `z` is `dz = z² (1/n − 1/f) / 65535`:

| near / far | dz at z = 10 | **at z = 20** | at z = 30 | at z = 40 |
|---|---:|---:|---:|---:|
| 0.1 / 200 (the reference) | 0.0153 | **0.0610** | 0.1373 | 0.2440 |
| **1.0 / 120 (this spec)** | 0.0015 | **0.0061** | 0.0136 | 0.0242 |

The scene's own smallest depth separations are the **DECAL lift 0.02** and the **billboard bias
0.06** (§9.3). With the reference's near plane the quantisation at typical viewing distance
(0.061) is **three times larger than the decal lift** — the mats would z-fight guaranteed, and the
billboard bias would be marginal. With `near = 1.0, far = 120` the quantisation is 0.0061: the
decal lift clears it by 3.3×, the billboard bias by 10×. Nothing is lost: the nearest geometry is
~7 units from the eye (§8.3), so a 1.0 near plane never clips, and geometry beyond 120 units is
outside the clip window anyway.

This is a **hard requirement on SPEC-render**, not a preference: if the render side builds the
projection with different near/far, the decal and billboard biases in this module must be re-derived
from the table above.

### 8.5 Map changes — rebase or snap (RESEARCH §6)

When the map key changes (§10.1):

- **Walk-through border crossing** — the *new* map is found in the **old** instance table (i.e. the
  player walked into an instanced neighbour). The world rebases around the new map, so the camera
  must move with it: `dio_camera_rebase(c, dx, dy)` subtracts the neighbour's **old origin** from
  `tx`/`tz`. The eye is recomputed from the target, so nothing else needs shifting, and there is
  **no visible camera jump**. RESEARCH calls this "essential and easy to miss".
- **Warp / door** — the new map is *not* in the old table. `dio_camera_arm_snap()`, so the next
  update places the target exactly on the player.
- **Gate re-open** (the diorama was off and comes back) — also `arm_snap`, matching RESEARCH §7.2
  step 1 ("arm a camera snap for the return").

`dio_map_transition(const DioWorld* oldW, const DioMapKey* newKey, int* dx, int* dy)` returns 1 and
the old origin when the new map is an old instance, 0 otherwise — the decision is pure and
host-testable, and T14 exercises both arms.

### 8.6 Matrices — pure C, `float m[16]`, **row-major**

```c
/* Row-major: m[4*row + col]. Right-handed. Both are pure functions and both are host-tested
   against hand-computed clip coordinates (T15).                                                */
void dio_look_at(float m[16], const float eye[3], const float target[3], const float up[3]);
void dio_persp  (float m[16], float fovyRad, float aspect, float nearZ, float farZ);
void dio_mtx_mul(float out[16], const float a[16], const float b[16]);   /* out = a * b         */
void dio_camera_view(const DioCam* c, float m[16]);                       /* look_at of the rig  */
/* Stereo (PHASE "What ships" #6): shift the eye along the camera's RIGHT vector by `sep` while
   keeping the SAME target, so the eyes converge on the player's tile. sep = +/- interaxial/2.  */
void dio_camera_view_eye(const DioCam* c, float sep, float m[16]);
```

`dio_look_at` is the textbook right-handed construction, written out because a sign error here is a
blank screen:
`f = normalize(target − eye)`, `s = normalize(f × up)`, `u = s × f`; rows
`[s, −s·eye]`, `[u, −u·eye]`, `[−f, f·eye]`, `[0,0,0,1]`.

**T15 golden** for `eye = (10, 10.0691955741, 24)`, `target = (10, 0, 12)`, `up = (0,1,0)`:

```
row0   1.0000000000   0.0000000000   0.0000000000  -10.0000000000
row1   0.0000000000   0.7660444431  -0.6427876097    7.7134513162
row2   0.0000000000   0.6427876097   0.7660444431  -24.8574207894
row3   0.0000000000   0.0000000000   0.0000000000    1.0000000000
```

with `V·target = (0, 0, −15.6648874725, 1)` and `V·eye = (0, 0, 0, 1)` — two assertions that catch
almost every sign error on their own. `dio_persp` at `fovy 35°, aspect 400/240, near 1, far 120`:

```
row0   1.9029568814   0              0              0
row1   0              3.1715948024   0              0
row2   0              0             -1.0168067227  -2.0168067227
row3   0              0             -1.0000000000   0
```

and the MVP goldens: the target projects to NDC `(0, 0, 0.8880598)`; a point 5 tiles north of it to
`(0, 0.5228649, 0.9133548)` at `w = 19.4951097`; 5 tiles south to `(0, −0.8613095, 0.8463915)` at
`w = 11.8346653`; one unit **up** from the target to `(0, 0.1617339, 0.8825507)`.

#### 8.6.1 Handing a `float m[16]` to citro3d — **exact conversion**

`C3D_Mtx` is documented "Row-major 4×4 matrix", and it is a union of `C3D_FVec r[4]` with
`float m[16]`. `C3D_FVec`'s members are declared **`{ float w; float z; float y; float x; }`** —
i.e. **each row is stored REVERSED in the raw float array** (`maths.h`'s own `Mtx_Diagonal` writes
`out->r[0].x`, `out->r[1].y`, `out->r[2].z`, `out->r[3].w` for the diagonal, which is only the
identity if `.x` is column 0). So:

```c
/* row-major float[16]  ->  C3D_Mtx.   m[4*i + j] is row i, column j. */
for (int i = 0; i < 4; i++) {
	dst.r[i].x = src[4*i + 0];
	dst.r[i].y = src[4*i + 1];
	dst.r[i].z = src[4*i + 2];
	dst.r[i].w = src[4*i + 3];
}
/* equivalently, with the raw union:  dst.m[4*i + (3 - j)] = src[4*i + j];  */
```

Writing `memcpy(dst.m, src, 64)` instead is the trap: it transposes each row and produces a
plausible-looking, completely wrong camera. T15 asserts the conversion by re-deriving `dst.m[]`
index by index with the `3 − j` formula.

**What `main.c` actually does** (the low-risk split): take the **view** matrix from this module,
convert as above; take the **projection** from citro3d's `Mtx_PerspTilt(&proj, fovy, aspect, near,
far, …)`, which bakes both the 3DS screen rotation and citro3d's own depth range; then
`Mtx_Multiply(&mvp, &proj, &view)` and `C3D_FVUnifMtx4x4`. `dio_persp` exists so the host suite can
check the whole chain in pure C and so a non-rotated target (a debug window, a PC preview) has a
projection — it is **not** the shipped projection, and §13 Q8 records the one thing that must be
confirmed on screen: the handedness flag passed to `Mtx_PerspTilt` must match this module's
right-handed view, and a mismatch shows as a blank or inside-out scene in the very first Azahar
screenshot.
## 9. BILLBOARDS

### 9.1 The call

```c
/* Emits ONE yaw-aligned, foot-anchored, depth-biased quad (4 verts + 6 indices) into `m`.
   (wx, wy, wz): the sprite's FOOT point in world units — x/z are the interpolated sub-tile
                 position (tile centre = tile + 0.5), y is the ground height under it (0 in v1).
   w, h        : the quad's size in TILES (16 sprite px = 1.0 world unit, RESEARCH §5.5).
   u0..v1      : the sprite's UV rect in whatever texture the caller will bind.
   Returns 1 on success, 0 if the mesh had no room (flag set, nothing written).                 */
int dio_billboard_emit(const DioCam* c, DioMesh* m,
                       float wx, float wy, float wz, float w, float h,
                       float u0, float v0, float u1, float v1, float bias);
```

Billboards go into the **same `DioVert` format** as the world, so the render side keeps one vertex
layout and one shader; only the bound texture and the draw call differ.

### 9.2 Geometry — yaw-aligned, upright, foot-anchored

RESEARCH §5.5 records the reference's shortcut and §8.3.6 names it a defect: *"Billboards don't
face the camera — fine only at yaw ≈ 0."* We fix it, cheaply, by rotating about **Y only**:

```
right = ( cos(cam->yawRad), 0, -sin(cam->yawRad) )     /* == the camera's ground-plane right */
up    = ( 0, 1, 0 )                                     /* UPRIGHT, not pitch-aligned         */
base  = ( wx, wy, wz )  -  forwardXZ * bias             /* §9.3                                */
p0 = base - right*(w/2)                 uv (u0, v1)     /* bottom-left                         */
p1 = base + right*(w/2)                 uv (u1, v1)     /* bottom-right                        */
p2 = base + right*(w/2) + up*h          uv (u1, v0)     /* top-right                           */
p3 = base - right*(w/2) + up*h          uv (u0, v0)     /* top-left                            */
```

`forwardXZ = (−sin(yaw), 0, −cos(yaw))` is the camera's ground-plane view direction, so
`− forwardXZ · bias` moves the quad **toward the camera**. At the shipped `yaw = 0` this collapses
to `right = (1,0,0)`, `forwardXZ = (0,0,−1)`, i.e. a quad in the XY plane pushed +Z (south) —
identical to the reference's axis-aligned card, which is exactly why the fix is free: it is a
generalisation that reduces to today's behaviour and stays correct if a later phase ever turns the
yaw (free-look, or the 3-4-player camera).

**Upright, not pitch-aligned.** RESEARCH-polish §1.3/§9 describes leaning billboards ("a billboard
sprite leans back over the ground directly north of its feet") as a *polish* technique with its own
shadow consequences. v1 stands them straight up; leaning is an "After v1" knob and is §13 Q9.

Tint is `(1, 1, 1, 1)` — sprites carry their own colour; the fake sun does not apply to actors.

### 9.3 The depth bias — why feet are not swallowed by the ground

The quad's bottom edge sits **on** the ground plane, and the ground quad under it is also at
`y = 0`: two coplanar surfaces meeting along the foot line, which on a 16-bit depth buffer is a
guaranteed shimmering seam that eats the character's feet. RESEARCH-polish §1.3 records the fix as
one of exactly three targeted depth biases in a shipped voxel renderer ("billboard pull, grass
pull, flower pull"), each justified per case, replacing all painter's-order machinery: *"No y-sort
anywhere. Occlusion is the depth buffer."*

```c
#define DIO_BILL_BIAS 0.06f    /* world units, pulled toward the camera along forwardXZ */
```

**0.06 is derived, not guessed:** §8.4 puts the depth quantisation at 0.0061 world units at 20
units of distance with this spec's near/far, so 0.06 is **10 depth steps** — enough that the pull
always wins — while being 6 % of a tile, far too small to read as "floating in front of the
ground" and smaller than half the sub-tile step of a walking sprite. It is passed as a parameter
(not baked) so the render side can retune it from a screenshot without touching the module, and
`bias = 0` reproduces the un-pulled geometry for the test that shows the seam exists.

Two properties the tests pin (T16): the **foot line stays at the requested `wy`** (the bias moves
the quad in XZ only, so a character never floats above or sinks into the floor), and the quad's
**normal is anti-parallel to `forwardXZ`** (it faces the camera exactly).

### 9.4 Sizing and sub-tile placement

- **Scale**: 16 sprite pixels = 1.0 world unit (RESEARCH §5.5), so a 16×32 overworld trainer is
  `w = 1.0, h = 2.0` and stands two storeys tall — deliberately taller than a `LOW` fence (0.4) and
  shorter than a `VEG` band (2.0) or a house (up to 6).
- **Anchor**: the foot point is the sprite's tile plus (0.5, 0.5), lerped between the previous and
  current tile by the game's own step timer (RESEARCH §5.5). **The sprite's *screen* coordinates
  must never be used** — they embed the game camera's pan — and SPEC-data owns producing the
  interpolated world position (PHASE O1). This module takes floats and asks no questions.
- **Alpha cutout** at 0.5 so transparent texels do not write depth (RESEARCH §5.5) is a TEV/alpha-
  test setting and belongs to SPEC-render; every vertex this module emits has `a = 1.0`.
## 10. CHANGE DETECTION

PHASE bound 7: *"Static geometry is built once per map (and when the live grid changes — doors, cut
trees), never per frame."* RESEARCH §8.3.1 is the same instruction from the other side: the
reference rebuilt instances, classification and structures **every frame**, and on PICA200 "this is
the difference between shipping and not". This section is PHASE open question **O3**'s answer.

### 10.1 The map-change key

```c
typedef struct { uint32_t layout; uint16_t group, num, w, h; } DioMapKey;   /* §3.4 */
int dio_key_changed(const DioMapKey* a, const DioMapKey* b);   /* field by field, not memcmp */
```

`layout` (the map-layout pointer) alone is not enough — two maps can share a layout (Emerald's
`LAYOUT_ROUTE111` / `..._NO_MIRAGE_TOWER` pairs, the many house interiors reusing one layout) — and
group/num alone is not enough either, because a script can swap the layout under a fixed location.
Both, plus the dims as a cheap corruption check. Comparison is **field by field, not `memcmp`**, for
the same reason `pspr_hdr_changed` is: padding is not required to be zero.

A key change triggers a **full rebuild** — load, classify, group, mesh — and the camera
rebase-or-snap decision of §8.5.

### 10.2 The live-grid hash — a fixed cost per frame, not a spike

Only **instance 0** is live; neighbours are static ROM. So the hash covers instance 0's cells,
re-read from the game (not from our copy — the point is to notice that the game changed).

A full sweep of the largest map is 6 400 cells. Doing that every frame would add ~3 200 `rd32`s to
a frame that currently spends ~150 reads in `build_depth_grid`; doing it every N frames turns the
cost into a periodic spike. Neither is acceptable, so the sweep is **sliced**:

```c
#define DIO_HASH_SLICE 256    /* cells hashed per call — a FIXED per-frame cost */

/* Hashes the next DIO_HASH_SLICE cells of instance 0 from the game, advancing w->hashCursor.
   When the cursor wraps, compares the accumulated FNV-1a with w->gridHash, stores it, and
   returns 1 if it MOVED (i.e. the live grid changed). Returns 0 otherwise. Bus-reading — call
   it in the parked window (§2.2).                                                              */
int dio_hash_step(const DioBus* bus, DioWorld* w);
```

- **Cost**: a fixed **≤ 128 `rd32`** per frame (256 cells, two per read) — comparable to
  `build_depth_grid`'s existing ~150 and constant regardless of map size.
- **Latency**: a full sweep takes `ceil(cells / 256)` frames — **1 frame** for the median 165-cell
  map, **25 frames (0.42 s)** for the largest 6 400-cell one. A door animation runs ~8 frames, so
  on a big map the door may finish opening before the mesh notices; the geometry then catches up
  within half a second. That is the disclosed trade, and it is the right one: the alternative is
  either a per-frame spike or a per-frame diff of the whole grid.
- **Known blind spot**: a change that is made *and reverted* inside one sweep is invisible. Nothing
  in Emerald's field does that (doors, cut trees and secret-base edits all persist), and a stale
  mesh is a cosmetic error, never a correctness one.
- **Hash**: FNV-1a over the `uint16_t` cells (`h = (h ^ cell) * 16777619`), seeded per sweep from
  `2166136261`. Cheap, well-distributed, and identical on host and device — T17 asserts a golden
  hash for a fixed 64-cell grid so a future "optimisation" of the mixing cannot silently weaken it.

### 10.3 The rebuild contract

```c
#define DIO_REBUILD_NONE   0
#define DIO_REBUILD_MESH   1   /* re-run dio_mesh_build only (cutaway / window move)            */
#define DIO_REBUILD_WORLD  2   /* re-run load + classify + group + mesh                          */

/* The ONE place the policy lives, so main.c has no scattered rules. Pure: the caller passes what
   it already knows.                                                                             */
int dio_needs_rebuild(const DioWorld* w, const DioMapKey* liveKey, int hashMoved,
                      int playerWX, int playerWY, int lastCutawayWY,
                      int clipCX, int clipCY);
```

| Trigger | Result | Why |
|---|---|---|
| `dio_key_changed(&w->key, liveKey)` | **WORLD** | new map: cells, instances, attributes, everything |
| `hashMoved` (§10.2) | **WORLD** | a door opened / a tree was cut: classes and structures can both change |
| `playerWY != lastCutawayWY` | **MESH** | §6.5's cutaway set can only change when the player crosses a row |
| the player left the inner half of a clipped window (§7.9) | **MESH** | the 64×64 window must recentre |
| anything else (including sub-tile motion, facing, NPCs) | **NONE** | those live in the per-frame billboard pass, which is not geometry |
| `w->ok == 0` | **WORLD** | retry a failed load on the next opportunity (§4.5) |

Note the asymmetry that makes this cheap: the **player walking within a row costs nothing**, and
crossing a row costs a mesh rebuild only (classification and grouping are untouched). On a fitting
map that is one `dio_mesh_build` — a bounded row-major sweep over ≤ 5 461 cells — and on a clipped
map it is a 4 096-cell sweep. T17 asserts each row of that table, including that a pure sub-tile
move returns `NONE`.
## 11. DEBUG

(pending)

## 12. TEST PLAN — `test/host/test_diorama.c`

(pending)

## 13. OPEN QUESTIONS

(pending)

