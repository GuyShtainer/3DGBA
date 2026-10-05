# Phase 33 ROMGEN, SPEC for S0 (ROM world) and S1 (regions.bin, signposts.bin)

Status: build-ready spec, 2026-10-05. Contract: `docs/phase33-romgen/PHASE.md`.

Citation keys used throughout:
- `cells:N` = `projects/_reference/pokeemerald-3Ds-dualscreen/3ds_port/scripts/voxel_cells.py` line N
- `art:N` = `.../scripts/voxel_art.py`, `regions:N` = `.../scripts/gen_voxel_regions.py`,
  `signs:N` = `.../scripts/gen_voxel_sign_masks.py`, `smask:N` = `.../scripts/voxel_sign_mask.py`
- `vtree:N` = `.../builder/emerald3ds_builder/vtree.py`, `manifest:N` = `.../tools/vtree_manifest.py`
- `vregions.c:N`, `vsign.c:N` = our vendored consumers `source/voxel/voxel_regions.c`, `source/voxel/voxel_sign.c`
- **M1** = a measurement made for this spec on the user's BPEE ROM (`roms/emerald.gba`, SHA-1
  `f3ae088181bf583e55daf962a92bb46f4f1d07b7`) with a throwaway script of ours (not Zallax code, not
  committed). Every M1 number is re-asserted by a host test in real-ROM mode (section 7).

All Python was read as specification only; none of it was run. No pret tree was read.

---

## 0. What the generators actually consume

The upstream generators read a decomp-layout tree. Zallax's builder rebuilds that tree from the ROM
(`vtree:90-147`), so the full list of ROM-derived inputs is exactly what `vtree.py` + `vtree_manifest.py`
emit:

| Tree input | Read by | ROM source (ours) |
|---|---|---|
| `layouts.json`: ordered list, per entry `width`, `height`, `primary_tileset`, `secondary_tileset`, `blockdata_filepath` | `cells:152-173`, `regions:47-63`, `signs:48-55` | `gMapLayouts` table (`GBA_ADDR_MAP_LAYOUTS`), `MapLayout` struct (section 1.2) |
| `map.json` per map: `layout`, `map_type`, `warp_events[x,y]`, `bg_events[type=="sign"][x,y]` | `cells:105-138` | `gMapGroups` → `MapHeader` → `MapEvents` (section 1.3) |
| `tiles.4bpp` (decompressed), `palettes/NN.gbapal` ×16, `metatiles.bin`, `metatile_attributes.bin` per tileset | `art:46-103`, `cells:97-102,161-162` | `Tileset` struct (section 1.4) |
| `metatile_behaviors.h` enum (name → value) | `cells:55-88` | fixed C table (section 2) |
| map names (to find "alternate" layouts) | `cells:397-415` | **not in the ROM**; replaced by a structural rule (section 1.5) |

Tilesets are identified upstream by **name** (`gTileset_X`); we identify them by **ROM address**
(the `Tileset` struct's GBA address). Names are only keys, so the substitution is exact, except
for the one place a name is interpreted (alternate layouts, section 1.5) and one place a name can
fail to resolve (section 1.4, note T3).

---

## 1. S0: the ROM world (C API replacing `Layout`, `MapEvents`, `pair_for`, `voxel_art.Pair`)

### 1.1 ROM access rules (shared by every romgen module)

- The ROM is one immutable byte buffer (`const uint8_t *rom, size_t romSize`). On device it is mGBA's
  ROM block (the same buffer `vx_adapter_set_rom` receives, `source/voxel/vx_adapter.c:549`); on
  the host the CLI `fread`s the `.gba`. Generators never copy it.
- A ROM pointer is valid iff `(p >> 24) == 0x08 && p - 0x08000000 + len <= romSize`
  (the SPEC-port rule 4 guard, `docs/phase32-voxel/SPEC-port.md` §2.1). u16 arrays need `p & 1 == 0`,
  u32 words `p & 3 == 0`. Reads are explicit little-endian (`rg_rd16/rg_rd32`), never casts, so the
  host build is endian- and alignment-clean.
- Constants come from `source/voxel/gba_game.h` (pure C, already host-included): `GBA_ADDR_MAP_LAYOUTS`
  0x08481DD4, `GBA_ADDR_MAP_GROUPS` 0x08486578, `GBA_MAP_GROUP_COUNT` 34, `GBA_OFF_MH_*`,
  `MAPGRID_*`, `NUM_*`, `MAP_TYPE_*`. romgen adds only what section 1.2-1.4 lists as new.
- The ROM gate: romgen runs only on BPEE. `rg_world_open` checks game code `"BPEE"` at ROM 0xAC and
  the layout-table invariant of section 1.2; anything else returns `RG_ERR_NOT_BPEE` and nothing is
  written. (S4 keys the cache by SHA-1 on top of this.)

### 1.2 Layouts: enumeration, order, ids

**Order.** Upstream layout ids are 1-based positions in `layouts.json` (`regions:54-55`,
`signs:54`), and the builder resolves a map's `layout` as `layouts[mapLayoutId-1]` (`vtree:52-57`),
which presumes the JSON order equals the ROM table order. In the ROM, `gMapLayouts` is an array of
`MapLayout*`; layout id `i` (1-based) is entry `i-1`. That is the same rule our adapter already uses
(`Port_GetMapLayoutById`, `vx_adapter.c:481-488`, which the renderer and `VoxelRegions_RoleAt` key by).

**Verification (mandatory, in `rg_world_open`, and a host test):** for every map header H reachable
through `gMapGroups` (section 1.3), `rd32(gMapLayouts + 4*(H.layoutId-1)) == H.layoutPtr`, where
`H.layoutId = rd16(H+0x12)` (`GBA_OFF_MH_LAYOUT_ID`) and `H.layoutPtr = rd32(H+0)`. The game itself
stores both, so agreement over all maps proves the 1-based table order. **M1: 518 headers, 0
mismatches, max layoutId 440.** A single mismatch ⇒ `RG_ERR_LAYOUT_ORDER`, nothing generated.

**Table length.** The ROM has no count. Walk entries from index 0; stop at the first entry that is
neither 0 nor a pointer to a *plausible* `MapLayout`:
`1 <= w,h <= 1024` (s32 at +0, +4), `border` (+8) and `map` (+0xC) valid ROM pointers, and each
tileset pointer (+0x10, +0x14) either 0 or a valid ROM pointer. **M1: 442 layouts, no NULL
entries, table ends at 0x084824BC (next word 0x085261B8 fails the w/h test).** Note:
layout 242 (58×26) has `secondaryTileset == NULL`, so a walk that demands both tilesets stops at
241. That is why the rule allows 0 (and why romgen must NOT reuse `InternLayout`,
`vx_adapter.c:281-318`, which rejects NULL tilesets and caps w at 271).

A NULL table entry (none in BPEE) is skipped by every generator, as upstream skips a layout whose
blockdata file is missing (`regions:56-59`). Upstream would `KeyError` on a nameless placeholder
entry (`vtree:109-111` writes `{"id": ...}` only; `regions:56` indexes `blockdata_filepath`), so
upstream Emerald evidently has none; ours simply skips.

**MapLayout fields** (24 bytes, `SPEC-port.md` §2.3 row "MapLayout", ✅): `w s32 @0, h s32 @4,
border* @8, map* @0xC, primary* @0x10, secondary* @0x14`. Blockdata is `w*h` u16 at `map`; it must
fit in the ROM (`regions:61-63` skips a short blockdata; ours rejects the layout the same way).
Cell word: metatile `& 0x3FF`, collision `(>>10) & 3`, elevation `(>>12) & 0xF` (`cells:180-187`,
`MAPGRID_*` in gba_game.h).

**M1 sizes:** 442 layouts, 325 479 cells total, largest 6 400 cells, max w 140, max h 140;
36 layouts referenced by no map header: 46, 72, 73, 75, 83, 84, 170-183, 242, 312, 319, 326, 357,
359, 392, 432-438, 441, 442.

### 1.3 Maps and events, mapped to layouts

**Enumeration.** `gMapGroups` (0x08486578) is 34 pointers to per-group arrays of `MapHeader*`. The ROM
stores no per-group count. Rule: group g's count = `(next - start_g) / 4`, where `next` is the
smallest group-array start greater than `start_g`, or `GBA_ADDR_MAP_GROUPS` itself for the last
array (the arrays are laid out back to back, immediately before the group table). Every entry must
be a valid ROM pointer to a header whose layout pointer passes 1.2's plausibility test, else
`RG_ERR_MAP_GROUPS`. **M1: counts [57,5,5,6,7,8,9,7,7,14,8,17,10,23,13,15,15,2,2,2,3,1,1,1,108,61,89,2,1,13,1,1,3,1]
= 518 maps, 0 bad**, which equals the 518 already measured independently by phase 28
(`source/fieldtrav.h:556-557`) and in `source/presence.h:84`.

**Per header** (`GBA_OFF_MH_*`): layout* @0, events* @4, layoutId u16 @0x12, mapType u8 @0x17.

**MapEvents** (`docs/phase18-crisp/SPEC-door.md:263-265`, `source/fieldtrav.c:1032-1044`,
BUILDLOG-P2 slice B): `objectCount u8 @0, warpCount u8 @1, coordCount u8 @2, bgCount u8 @3,
objects* @4, warps* @8, coords* @0xC, bgs* @0x10`. Events pointer 0 ⇒ no events.
- `WarpEvent`, stride 8: `x s16 @0, y s16 @2, elevation u8 @4, warpId u8 @5, mapNum u8 @6, mapGroup u8 @7`.
  Upstream keeps `(x, y)` only (`cells:123-124`, `vtree:64-70`).
- `BgEvent`, stride 12 (`GBA_ROM_BGEVENT_STRIDE`): `x u16 @0, y u16 @2, elevation u8 @4, kind u8 @5`.
  A **sign** is `kind <= 4` (`vtree:26-28,74-76`; kinds 0-4 = read from any side / one side; others
  are hidden items and secret bases). **M1 census over 518 maps:** kind 0 ×415, 1 ×106, 3 ×6, 4 ×6,
  7 ×112, 8 ×75 ⇒ 533 sign events; 1 313 warp events.

**Outdoor.** `OUTDOOR_MAP_TYPES = {ROUTE, TOWN, UNDERWATER, CITY, OCEAN_ROUTE}` (`cells:39-40`) =
mapType ∈ {3, 1, 5, 2, 6} (`MAP_TYPE_*`, gba_game.h:181-190). **M1:** types 1×7, 2×9, 3×41, 4×105, 5×14,
6×11, 8×307, 9×24 ⇒ 82 outdoor maps.

**Per-layout aggregation** (`cells:109-127`): a layout is outdoor iff **any** map using it is
outdoor; its warp set and sign set are the **union** over all maps using it (sets: duplicates
collapse; `self.warps` is sorted, `cells:164`, but only ever used as a set). Events of maps whose
layout id is out of range are ignored.

### 1.4 Tilesets: tiles, palettes, metatiles, attributes

**Tileset struct** (24 bytes, `SPEC-port.md` §2.3 ✅; `vx_adapter.c:227-279`):
`isCompressed u8 @0, isSecondary u8 @1, tiles* @4, palettes* @8, metatiles* @0xC, attributes* @0x10`.
Emerald attributes are u16; behaviour = `attr & 0xFF` (`cells:198`, `UNPACK_BEHAVIOR`). (FireRed
differs: u32 at @0x14, `docs/phase18-crisp/evidence/door/gen3map.py:7` — out of scope, BPEE only.)

- **Tiles.** `isCompressed != 0` ⇒ LZ77 type 0x10 stream; decode with our `vx_lz77_decode`
  (`source/voxel/vx_lz77.h`, pure C, 173 host checks); the decoded length is the header's size field
  and is the exact `len(tiles.4bpp)` upstream sees (`art:55-57`; `manifest:51-53` copies `tiles.4bpp`
  as the decompressed stream). Uncompressed: the ROM does not record the length; use
  `min(512*32, romEnd - tiles)` (as `vx_adapter.c:257-258`). **Note T1:** upstream returns "unreadable"
  for a tile id past the end of a short `tiles.4bpp` (`art:114-117`); for an *uncompressed* tileset
  ours would read the bytes that follow. Only metatiles that reference tiles past the real data
  differ, which real map metatiles do not do. M1: 7 of 73 tilesets are uncompressed (0x083DF95C,
  0x083DFA04/1C/34/4C/64/7C, 0x083DFC5C). Accepted; flagged in the risk list.
- **Palettes.** 16 palettes × 16 colours u16 BGR555 at `palettes*` (512 B). Colour → RGB888 as
  `((c & 31) * 255 / 31, ((c >> 5) & 31) * 255 / 31, ((c >> 10) & 31) * 255 / 31)` with integer
  division (`art:63-67`). Bit 15 is ignored upstream, so **every colour comparison in romgen compares
  `c & 0x7FFF`** (the 5→8 map is injective, so equal RGB888 ⇔ equal 15-bit value).
- **Metatile count.** The ROM does not store it; upstream knows it from the file size
  (`art:95-97` returns "no entries" past the end, `cells:194-195` returns attribute 0 past the end).
  Rule: if `attributes > metatiles` and `(attributes - metatiles) % 16 == 0` and the quotient is
  `<= 512`, count = quotient (the two arrays are emitted back to back per tileset); else 512.
  M1: General = 512, GenericBuilding (0x083DFB6C) = 512, the Building primary 0x083DF884 = 8,
  smallest secondary 38. A host test pins every tileset's count (section 7).
- **Metatile entry** (8 u16 per metatile, `art:91-97,143-163`): entries 0-3 = layer 0 (lower), 4-7 =
  layer 1 (upper); quad q → pixel offset `((q & 1) * 8, (q >> 1) * 8)`; entry bits: tile `& 0x3FF`,
  hflip `0x400`, vflip `0x800`, palette `>> 12`.
- **Primary/secondary split** (`art:91-93,105-128`): metatile id `< 512` → primary's tables, else
  secondary's at `id - 512`. Tile id `< 512` → primary tiles, else secondary at `id - 512`.
  Palette id `< 6` → primary's `palettes[id]`, else **secondary's `palettes[id]`** (index not
  rebased: the secondary's own 16-entry array is indexed with 6..15). The two splits are independent.
- **Index 0 is not drawn**, on both layers (`art:130-141,160-161`).
- **NULL tileset** (layout 242's secondary): no tiles, no palettes, no metatiles, attributes all 0
  (`vtree:45-46` maps it to `"NULL"`, `art:50-54` then yields an empty `Graphics`).
- **Note T3 (ambiguity, cannot be resolved without the decomp):** upstream finds a tileset's art by
  turning its symbol name into a folder name (`art:37-43`). If that folder did not exist for some
  tileset, upstream saw **no pixels** for it (foliage 0, never treads, never covers). Ours always sees
  the ROM's pixels. If such a tileset exists, our roles differ from Zallax's for it, and ours are the
  more faithful. Not fixable by us; listed as a risk.

**M1 tileset census:** 73 distinct non-NULL tilesets, 76 distinct (primary, secondary) pairs;
3 tilesets flagged primary (0x083DF704 General, 0x083DF884, 0x083DFC5C); all decoded tiles together
888 128 B; the largest pair 32 768 B.

### 1.5 Alternate layouts (`cells:128-138, 397-415`): the one name-based rule

Upstream: a layout used by no map inherits outdoor/warps/signs from the used layout whose **name** is
a prefix of its own (`base + "_"`) and whose size is equal; longest name wins (`cells:412-414`). The
docstring names the three it is for: Route 111 after the tower falls, Route 131 with the Sky Pillar,
Sootopolis in the legends' battle (`cells:128-131`). The ROM holds no names, so this rule cannot be
ported literally.

**Our rule (structural):** unused layout U inherits from used layout B iff `w,h` equal **and** the
`(primary, secondary)` tileset addresses equal **and** the share of equal blockdata words is
`>= 0.5`; the B with the highest share wins (ties: lowest id). **M1 result** (U→B, share):
46→263 (0.70), 312→162 (0.59), 319→47 (0.86), 326→156 (0.92), 357→8 (0.82), 392→27 (0.997),
432→58 (0.95), 433→322 (0.89), 434→323 (0.74), 435→324 (0.92), 436→325 (0.79), 437→330 (0.92),
438→331 (0.76), 441→439 (0.83), 442→1 (1.00). Of these, the outdoor bases are 263, 47, 8, 27, 331, 1.
357→8 is the Sootopolis twin (both use secondary 0x083DF83C, the only tileset holding behaviour 0x8B);
392→27 (40×140) is the Route 111 twin; 319→47 (60×40) is the expected Route 131 twin.

Why this is safe: an alternate only matters when a script swaps it in, and then it *is* its base's
twin; a false positive is a layout the game never shows, so its extra roles are inert. The
divergence from upstream (we may inherit for layouts upstream did not, e.g. 442→1 if 442 is not
named `LAYOUT_<base>_…`) is accepted and recorded. The 15-pair table above is pinned by a real-ROM
host test so a code change cannot silently move it.

Optional, later: the exact set is the set of layout ids named by the game's "set map layout" script
command in map scripts. Deriving that needs a script walker; not in S0.

### 1.6 The C API (`source/romgen/rg_world.h`, `rg_art.h`)

```c
/* rg_world.h -- the ROM world romgen generators read (3DGBA, GPLv3). Pure C. */
typedef enum { RG_OK = 0, RG_ERR_NOT_BPEE, RG_ERR_LAYOUT_ORDER, RG_ERR_LAYOUT_TABLE,
               RG_ERR_MAP_GROUPS, RG_ERR_TILESET, RG_ERR_NOMEM } RgErr;

typedef struct { int16_t x, y; } RgCell;

typedef struct RgTileset {
    uint32_t addr;               /* GBA address; 0 = NULL tileset */
    bool compressed, secondary;
    const uint8_t *tilesRom;     /* raw ROM pointer (LZ77 stream or 4bpp) */
    uint32_t tilesBytes;         /* decoded length (exact if compressed) */
    const uint8_t *palettes;     /* 512 B, read with rg_rd16 */
    const uint8_t *metatiles;    /* 16 B per metatile */
    const uint8_t *attrs;        /* 2 B per metatile */
    uint16_t metatileCount;      /* section 1.4 rule */
} RgTileset;

typedef struct RgLayout {
    uint16_t id;                 /* 1-based gMapLayouts position */
    uint16_t w, h;
    const uint8_t *blocks;       /* w*h u16, ROM */
    const RgTileset *ts[2];      /* primary, secondary (never NULL; addr 0 = empty) */
    uint16_t pairIndex;          /* index into world->pairs */
    uint8_t  present;            /* entry non-NULL and blockdata in ROM */
    uint8_t  used;               /* referenced by >= 1 map header */
    uint8_t  outdoor;            /* section 1.3, after 1.5 inheritance */
    uint16_t altOf;              /* section 1.5 base id, 0 = none */
    const RgCell *warps; uint16_t warpCount;   /* union, sorted (y, x), unique */
    const RgCell *signs; uint16_t signCount;   /* union, sorted (y, x), unique */
} RgLayout;

typedef struct RgWorld {
    const uint8_t *rom; size_t romSize;
    uint16_t layoutCount;        /* 442 on BPEE */
    RgLayout *layouts;           /* [id-1] */
    uint16_t tilesetCount; RgTileset *tilesets;      /* index 0 = the NULL tileset */
    uint16_t pairCount;    struct { uint16_t ts[2]; } *pairs;
    uint16_t mapCount;           /* 518 on BPEE */
    /* one arena: everything above lives in it; ~60 KB on BPEE */
} RgWorld;

RgErr rg_world_open(RgWorld *w, const uint8_t *rom, size_t romSize);
void  rg_world_close(RgWorld *w);

/* cell queries (cells:177-202); off-map: metatile RG_NONE (0xFFFF), blocked false, elevation 0 */
static inline bool     rg_off(const RgLayout *L, int x, int y);
uint16_t rg_metatile(const RgLayout *L, int x, int y);
bool     rg_blocked (const RgLayout *L, int x, int y);
uint8_t  rg_elev    (const RgLayout *L, int x, int y);
uint16_t rg_attr    (const RgLayout *L, uint16_t metatile);      /* 0 past metatileCount / RG_NONE */
uint8_t  rg_behaviour(const RgLayout *L, int x, int y);          /* rg_attr(...) & 0xFF */
bool     rg_touches_walkable(const RgLayout *L, int x, int y);   /* 4-neighbours in map, unblocked */
bool     rg_has_warp(const RgLayout *L, int x, int y);           /* binary search */
bool     rg_has_sign(const RgLayout *L, int x, int y);
```

```c
/* rg_art.h -- a tileset pair's pixels (art:80-163). Pure C. */
typedef struct {
    uint16_t drawn[16];          /* bit x of row y = pixel (x,y) drawn (palette index != 0) */
    uint16_t c[16][16];          /* BGR555 & 0x7FFF, valid where drawn */
    uint16_t count;              /* number of drawn pixels */
} RgLayer;

typedef struct RgPair RgPair;    /* opaque: decoded tiles of both tilesets + per-metatile memo */
RgPair *rg_pair_open(const RgWorld *w, uint16_t pairIndex);   /* decodes both tile blocks */
void    rg_pair_close(RgPair *p);
const RgLayer *rg_layer(RgPair *p, uint16_t metatile, int layer);  /* lazily built, memoised */
/* derived, memoised per metatile (section 3.2): */
uint8_t rg_foliage_ge_half(RgPair *p, uint16_t metatile);   /* foliage(m) >= FOLIAGE */
bool    rg_treads(RgPair *p, uint16_t metatile);
bool    rg_covers(RgPair *p, uint16_t metatile);
```

`rg_layer` reproduces `_layer_pixels` exactly (`art:143-163`): later quads overwrite nothing (quads
do not overlap); a quad whose entry is unreadable (tile past `tilesBytes`, palette missing, NULL
tileset) contributes nothing; a metatile past the tileset's `metatileCount` yields an empty layer.
The "merged" drawing (`dict(layer0); update(layer1)`, `cells:210-211,235-236`) is a helper
`rg_merged(p, m, RgLayer *out)`: layer-1 pixels win where both draw.

### 1.7 Memory budget (3DS: ~40 MB linear free; target << 4 MB peak for S1)

| Item | Size (BPEE) | Lifetime |
|---|---|---|
| ROM | 16 MB, **not copied** (mGBA's buffer) | whole run |
| `RgWorld` arena (layouts, events, tileset table, pairs) | ~60 KB (442 layouts × ~40 B + 1 313 warps + 533 signs × 4 B + 73 tilesets) | whole run |
| One open `RgPair`: decoded tiles 2 × 16 KB + layer memo 1 024 × 2 × 544 B + feature memo 1 024 × 4 B | ~1.15 MB worst case | one pair at a time |
| Roles of every layout (1 byte/cell, for regions.bin and the S1 cross-layout passes) | 325 KB | whole S1 |
| Sign records (72 B each; ≤ a few hundred) | < 64 KB | S1 |
| regions.bin output | 8 + 12 × 442 + 325 479 ≈ 331 KB (consumer limit 2 MiB, `vregions.c:15`) | written, freed |

Rule: **never hold more than one pair's decoded art.** Generators iterate layouts grouped by
`pairIndex` (stable: pairs in order of first appearance by layout id, layouts by id within a pair),
open the pair, process its layouts, close it. Output tables are indexed by layout id, so emission
order is independent of processing order. Peak ≈ 1.6 MB.

---

## 2. Metatile behaviour values (`source/romgen/rg_behavior.h`)

Upstream reads names off the decomp enum (`cells:55-71`); the builder ships them as a list
(`manifest:107-117`, `vtree:138-145`). The ROM stores only the numbers in each tileset's attribute
array, so the sets are fixed C tables. Every value below is a fact about Emerald's data.

**WATER** (`cells:80-85`; "drawn as water", not the surfable set):

| Value | Name | Our source |
|---|---|---|
| 0x10 | MB_POND_WATER | `source/voxel/gba_game.h:223` |
| 0x11 | MB_INTERIOR_DEEP_WATER | `docs/phase31-diorama/SPEC-data.md:692` |
| 0x12 | MB_DEEP_WATER | SPEC-data.md:693 |
| 0x13 | MB_WATERFALL | SPEC-data.md:694; `docs/phase21-touch-census/PHASE26-AUDIT.md:54` |
| 0x14 | MB_SOOTOPOLIS_DEEP_WATER | gba_game.h:226 |
| 0x15 | MB_OCEAN_WATER | SPEC-data.md:696 |
| 0x16 | MB_PUDDLE | gba_game.h:224 |
| 0x17 | MB_SHALLOW_WATER | gba_game.h:225 |
| 0x19 | MB_NO_SURFACING | SPEC-data.md:697 |
| 0x22 | MB_SEAWEED | SPEC-data.md:698 |
| 0x28 | MB_HOT_SPRINGS | gba_game.h:231 |
| 0x2A | MB_SEAWEED_NO_SURFACING | SPEC-data.md:699 |
| 0x2B | MB_REFLECTION_UNDER_BRIDGE | gba_game.h:232 |
| 0x50-0x53 | MB_EASTWARD/WESTWARD/NORTHWARD/SOUTHWARD_CURRENT | SPEC-data.md:700-703; PHASE29-AUDIT.md:78 |

Not water, on purpose (`cells:77-79`): bridges, deep sand, `MB_WATER_DOOR` 0x6C,
`MB_WATER_SOUTH_ARROW_WARP` 0x6D, `MB_UNUSED_6F`, `MB_UNUSED_SOOTOPOLIS_DEEP_WATER_2` 0x1A.
Do **not** reuse `MetatileBehavior_IsSurfableWaterOrUnderwater` (`source/voxel/vx_behavior.c:30-35`):
that set includes 0x6C/0x6D/0x6F and excludes 0x16/0x17/0x28/0x2B.

**JUMPS** = every `MB_JUMP_*` (`cells:86`) = **0x38-0x3F** (E, W, N, S, then the four diagonals;
SPEC-data.md:729-735). M1 blockdata census: 0x38 ×121, 0x39 ×169, 0x3B ×686, 0x3E ×34, 0x3F ×44, all
collision 1. The diagonals are used, so the full range matters.

**HOUSE_DOORS** (`cells:87-88`):

| Value | Name | Source / verification |
|---|---|---|
| 0x69 | MB_ANIMATED_DOOR | SPEC-data.md:741; phase 18 census `docs/phase18-crisp/evidence/door/extract-2026-08-12.txt` (192 warp tiles) |
| 0x8D | MB_PETALBURG_GYM_DOOR | SPEC-data.md:742 |
| 0x8B | MB_CLOSED_SOOTOPOLIS_DOOR | **new, not yet in our tree.** Fact: enum ordinal two below 0x8D, one below MB_TRICK_HOUSE_PUZZLE_DOOR 0x8C (phase 18 census: 8 warps on 0x8C, collision 1). **Verify from the ROM (M1, done):** across all 73 tilesets' attribute arrays, 0x8B occurs in exactly 2 metatiles, both in secondary tileset 0x083DF83C, which only layout 8 (map 0/7, MAP_TYPE_CITY, 60×60 = Sootopolis) and its unused twin 357 use. |

M1: in static blockdata 0x69 occurs on 235 cells (211 collision 1); **0x8B and 0x8D occur on 0
cells** (they are placed by map scripts at runtime). So on BPEE only 0x69 can change the output; the
other two are kept for fidelity.

Role numbers are the consumer's (`source/voxel/voxel_regions.h:28-39`): FLOOR 0, WATER 1, LEDGE 2,
STAIR 3, WALL 4, TREE 5, PROP 6, SHELF 7, FENCE 8, CLIFF 9, SIGNPOST 10. Upstream parses them from the
header (`regions:24-33`); ours includes the header.

---

## 3. S1a: the roles (`source/romgen/rg_roles.c`, port of `voxel_cells.Layout`)

### 3.1 Constants (all `cells:38-52` unless noted)

| Name | Value | Use |
|---|---|---|
| NUM_PRIMARY | 512 | metatile/tile split |
| HOUSE_HALF_WIDTH | 5 | house flood: `|x - doorX| <= 5` |
| HOUSE_HEIGHT | 7 | house flood: `doorY - 7 <= y <= doorY` |
| FOLIAGE | 0.5 | `foliage(m) >= 0.5` (exact integer form: `drawn > 0 && 2*green >= drawn`) |
| green pixel | `g > 64 && g > r + 16 && g > b + 16` on RGB888 ints | `cells:212` |
| TREAD_ACROSS | 4.0 | treads: across `<= 4.0` |
| TREAD_DOWN | 15.0 | treads: down `>= 15.0` |
| tread fill | `>= 240` drawn pixels of 256 | `cells:237` |
| COVER_SLACK | 2 | covers: rows with `>= 8` upper-layer pixels `>= 16 - 2 = 14` (`cells:222-225`) |

### 3.2 Per-metatile features (memoised per pair, `cells:170-173` share them across layouts of a pair)

- `foliage(m)` (`cells:206-214`): over the merged drawing (layer 1 over layer 0), share of pixels
  that are green. Empty drawing ⇒ 0.
- `covers(m)` (`cells:216-226`): count drawn **layer-1** pixels per row; true iff at least 14 rows
  have 8 or more.
- `treads(m)` (`cells:228-245`): merged drawing; false unless `count >= 240`. Then
  `lum[y][x] = 0.3*r + 0.59*g + 0.11*b` (doubles, evaluated left to right) for drawn pixels and `0.0`
  for undrawn ones; `across = (Σ_{y=0..15} Σ_{x=0..14} |lum[y][x] - lum[y][x+1]|) / 240.0` summed row
  by row, x inner; `down = (Σ_{y=0..14} Σ_{x=0..15} |lum[y][x] - lum[y+1][x]|) / 240.0`, y outer, x
  inner; true iff `across <= 4.0 && down >= 15.0`. **Bit-exactness:** the sums start at 0 and add in
  that order (Python `sum()` order); compile romgen with `-ffp-contract=off` and never
  `-ffast-math`, so no FMA or reassociation moves a value across a threshold. Host and device then
  agree bit for bit (both IEEE double).

### 3.3 Cell predicates (L = one layout; off-map cells: metatile none, blocked false)

```
open_post(x,y)      = L.outdoor && blocked(x,y)
                      && !blocked(x+1,y) && !blocked(x-1,y) && !blocked(x,y+1)     # cells:275-278
                      (off-map neighbours count as open)
touches_walkable    = any 4-neighbour in map and !blocked                           # cells:200-202
is_ledge_junction   = blocked && (JUMPS at x-1 or x+1) && (JUMPS at y-1 or y+1)     # cells:333-340
                      (defined upstream, never used by role_at; ported for S2/S3)
```

**houses()** (`cells:249-271`), memoised per layout, a set of cells:
```
for each warp (dx,dy) of L:                       # union over all maps of the layout
    if behaviour(dx,dy) not in HOUSE_DOORS: continue        # off-map => behaviour 0 => skip
    seed = (dx, dy-1); if off_map(seed) or !blocked(seed): continue
    flood from seed (4-neighbour, a per-door "seen" set); the seed is always taken;
    a neighbour n joins iff  in map && blocked(n) && |n.x-dx| <= 5 && dy-7 <= n.y <= dy
                             && !(foliage(metatile(n)) >= 0.5)
    every visited cell joins the result
```
The result is a set; flood order does not matter (the join test depends only on n and the door).

**post_metatiles** (`cells:372-394`), one global set, computed once **before** any role, over every
present layout L that has at least one sign (after section 1.5 inheritance), in id order:
`for (x,y) in L.signs: if in map and open_post(x,y): add (tilesetAddrOf(L, m), m)` where
`m = metatile(x,y)` and `tilesetAddrOf = m < 512 ? primary.addr : secondary.addr` (`post_key`,
`cells:372-374`). It needs collision bits only, no pixels.

**free_post(x,y)** (`cells:306-319`):
```
if !open_post(x,y): false
if (x,y) in L.signs: true
m = metatile(x,y)
if covers(m): false
if foliage(m) >= 0.5 or (x,y) in houses(): false
return !blocked(x,y-1) or metatile(x,y-1) != m       # north off-map => true
```

**lamps()** (`cells:321-331`), memoised per layout, a set of `(post, head)` metatile pairs:
for `y = 1..h-1`, `x = 0..w-1`: add `(metatile(x,y), metatile(x,y-1))` iff
`(x,y) not in L.signs && !blocked(x,y-1) && layer1(metatile(x,y-1)) has a drawn pixel && free_post(x,y)`.
Store as a small sorted array of u32 `(post << 16) | head`.

**is_signpost(x,y)** (`cells:280-304`):
```
if !L.outdoor or !blocked(x,y): false
south_ok = in_map(x,y+1) && !blocked(x,y+1)
if south_ok && (tilesetAddrOf(L, m), m) in post_metatiles: true          # m = metatile(x,y)
if free_post(x,y): true
if !south_ok: false
if y == 0 or (metatile(x,y), metatile(x,y-1)) not in lamps(): false     # y==0: north is "None"
sides = [(x-1,y), (x+1,y)] filtered by blocked()                         # off-map => not blocked
return len(sides) == 1 and sides[0] in houses()
```

### 3.4 `role_at(x, y)` (`cells:342-369`), in this exact order

```
b = behaviour(x,y)
if b in WATER:                      WATER    (1)
elif b in JUMPS:                    LEDGE    (2)
elif !blocked(x,y):                 treads(metatile) ? STAIR (3) : FLOOR (0)
elif is_signpost(x,y):              SIGNPOST (10)
elif (x,y) in houses():             WALL     (4)
elif foliage(metatile) >= 0.5:      TREE     (5)
elif (in_map(x,y-1) && !blocked(x,y-1) && in_map(x,y+1) && !blocked(x,y+1))
  or (in_map(x-1,y) && !blocked(x-1,y) && in_map(x+1,y) && !blocked(x+1,y)):
                                    FENCE    (8)
elif touches_walkable(x,y):         CLIFF    (9)
else:                               SHELF    (7)
```
**PROP (6) is never produced** although the docstring lists it (`cells:23`): ambiguity resolved in
favour of the code. Note the fence test, unlike `open_post`, requires the neighbours to be **in**
the map. Upstream memoises per cell (`cells:343-368`); ours computes each cell once into the role
array, so no memo is needed.

Cost per layout: O(w·h) plus one flood per house door (each bounded by 11×8 cells) plus the lamps
scan (O(w·h), lazily, only for layouts that reach the lamp branch).

---

## 4. S1a output: `regions.bin` (VXR5)

Generator: `regions:42-83`. Consumer: `vregions.c:38-91`. Little-endian throughout.

```
off  size  field
0    4     "VXR5"
4    2     count (u16)            number of index rows
6    2     0 (u16, reserved)      the consumer does not read it
8    12*count index rows, ascending layoutId:
           +0 u16 layoutId (1-based gMapLayouts position, never 0)
           +2 u16 width
           +4 u16 height
           +6 u16 0 (reserved)
           +8 u32 absolute file offset of this layout's role bytes
...  role bytes: width*height bytes per layout, row major (y outer), one role 0..10 per cell,
     concatenated in index order with no padding
```
- Rows: every **present** layout (section 1.2), in id order; on BPEE all 442. Upstream includes indoor
  layouts too (`regions:55-69`), so do we.
- Consumer acceptance (`vregions.c:47-80`), each a generator assertion: file `8 <= size <= 2 MiB`;
  `count != 0`; `8 + 12*count <= size`; ids strictly ascending and non-zero; `w*h != 0`;
  `offset >= 8 + 12*count`; `offset + w*h <= size`; every role byte `< 11`. The consumer packs to
  4 bits per cell; the file holds one byte per cell.
- The `role_at` origin is layout-local, matching `VoxelRegions_RoleAt(layoutId, localX, localY)`
  (`vregions.c:102-110`) as called by `voxel_sign.c` (`vsign.c:262-263,277`).

---

## 5. S1b: `signposts.bin` (VXS2)

Generator: `signs:22-86` + `smask:14-87`. Consumer: `vsign.c:14-80` (load, binary search), `:269-305`
(use).

### 5.1 Which cells

For every present layout, if outdoor (`signs:57-58`): every cell with `role == SIGNPOST` (from the
S1a role array, `signs:64`) yields one record. Records sorted by `(layoutId, y, x)` (`signs:73`),
which is the consumer's binary-search key `(layout << 20) | (y << 10) | x` (`vsign.c:71-76`); so
x, y < 1024 and the order must be exactly this. Consumer accepts `1 <= count <= 65535`
(`vsign.c:55`); zero signposts ⇒ write no file (the loader would reject it anyway).

### 5.2 `metatile_mask(x, y)` → 16 rows (`smask:47-52`)

```
ground = set of colours (BGR555 & 0x7FFF):                                   # smask:14-25
    for (dx,dy) in ((-1,0),(1,0),(0,1),(-1,1),(1,1)):
        n = (x+dx, y+dy); skip if off-map or blocked(n)
        mn = metatile(n); skip if layer1(mn) has any drawn pixel
        add every drawn colour of layer0(mn)
pixels = { p in layer0(m) : colour(p) not in ground }  ∪  { every drawn p of layer1(m) }
rows   = cutout_mask(pixels)
```
`cutout_mask(solid)` (`smask:28-44`): flood "outside" from **every border pixel** (x ∈ {0,15} or
y ∈ {0,15}) through non-solid pixels, 4-connected; a solid border pixel is not entered. Result row y
has bit x set iff `(x,y)` is **not** outside (solid pixels plus enclosed holes). Bit 0 = leftmost
pixel, row 0 = top (`source/voxel/voxel_sign.h:13`). The ground set is at most 5×256 colours; use a
32 768-bit bitmap (4 KB) cleared per call, or a sorted array.

### 5.3 `head_mask(rows, north)` → 16 rows (`smask:55-87`)

Computed only if `y > 0 && !blocked(x, y-1)` (`signs:68-69`), else all zero.
```
HEAD_MAX_WIDTH = 14, HEAD_MAX_PIXELS = 160                                  # smask:57-58
if popcount(rows[0]) > 14: zero
upper = drawn pixels of layer1(north)
seeds = { (sx,15) in upper : rows[0] has a bit at sx-1, sx or sx+1 (within 0..15) }
head  = 8-connected flood of seeds within upper
if head empty: zero
if (max x - min x + 1) > 14 or |head| > 160: zero
return cutout_mask(head)
```

### 5.4 `head_ground(x, y-1)` → u16 metatile (`signs:22-44`)

Computed only if the head mask is non-zero (`signs:70-71`); otherwise the field is 0. With
`(hx, hy) = (x, y-1)` the head cell:
```
own = layer0(metatile(hx,hy))                      # compared as (drawn mask, colours) equality
counts = ordered map metatile -> count (insertion order)
for dy in (-1, 0, 1): for dx in (-1, 0, 1):       # this loop order is the tie-break order
    skip (0,0) and (0,1)                           # the head cell and the sign under it
    n = (hx+dx, hy+dy); skip if off-map or blocked(n)
    mn = metatile(n); skip if layer1(mn) has any drawn pixel
    if layer0(mn) == own: return mn                # same drawn pixels AND same colours
    counts[mn] += 1
if counts: return the metatile with the highest count; on a tie the one inserted FIRST
           (Python max over a dict, signs:43)
return in_map(hx, hy+2) ? metatile(hx, hy+2) : 0
```
"Equal layer" = equal `drawn[16]` and equal `c & 0x7FFF` at every drawn pixel (dict equality of
`{(x,y): rgb}`, `signs:39`). Colours are compared, not palette indices.

### 5.5 Byte layout

```
off  size  field
0    4     "VXS2"
4    4     count (u32), 1..65535
8    72*count records, sorted (layout, y, x):          # struct.pack("<HHH16H16HH"), signs:84
           +0  u16 layoutId
           +2  u16 x               (layout-local)
           +4  u16 y
           +6  u16 rows[16]        sign mask, row 0 = top, bit 0 = left
           +38 u16 head[16]        lantern mask in the cell north, all 0 for none
           +70 u16 headGround      metatile id (0 when head is all 0)
```
The consumer `fread`s the records straight into `SignRecord` (`vsign.c:20-26,60`): 35 u16, no
padding, host little-endian; the generator writes explicit LE bytes.

---

## 6. Files and modules

All new files carry the "Modified for 3DGBA (GPLv3), 2026" header plus the MIT notice where they port
Zallax logic (LEGAL-zallax-port.md option 2a): `rg_roles.c`, `rg_regions.c`, `rg_signs.c`, `rg_art.c`
(port of voxel_art). `rg_world.c`, `rg_behavior.h`, the CLI and the tests are ours alone (GPLv3
header only). Generator cores include only `<stdint.h> <stdbool.h> <stddef.h> <string.h> <stdlib.h>`,
`gba_game.h` and `vx_lz77.h`. No libctru, citro or stdio in cores: output goes to a caller buffer.

| File | Role |
|---|---|
| `source/romgen/rg_world.{h,c}` | S0: ROM gate, layout table, map groups, events, tilesets, pairs, alternates (sections 1.1-1.6) |
| `source/romgen/rg_art.{h,c}` | S0: pair decode, `rg_layer`, `rg_merged`, foliage/treads/covers memo (1.6, 3.2) |
| `source/romgen/rg_behavior.h` | S0: WATER/JUMPS/HOUSE_DOORS tables (section 2) |
| `source/romgen/rg_roles.{h,c}` | S1a: post_metatiles, houses, lamps, is_signpost, role_at; fills `uint8_t roles[]` per layout |
| `source/romgen/rg_regions.{h,c}` | S1a: VXR5 serialiser: `size_t rg_regions_write(const RgWorld*, const RgRoles*, uint8_t *out, size_t cap)` (call with `out=NULL` for the size) |
| `source/romgen/rg_signs.{h,c}` | S1b: metatile_mask, head_mask, head_ground, VXS2 serialiser (same buffer convention) |
| `source/romgen/rg_run.{h,c}` | Driver used by both CLI and device: opens the world, runs S1 by pair, reports progress via a callback `void (*progress)(void *ctx, unsigned done, unsigned total)` and a cancel flag. No file I/O: hands back buffers. |
| `tools/romgen/romgen_cli.c` | Host CLI: `romgen ROM.gba OUTDIR [--only regions,signposts] [--time] [--dump-roles LAYOUT_ID]`. Reads the ROM, runs `rg_run`, writes `OUTDIR/regions.bin`, `OUTDIR/signposts.bin`, prints per-generator wall time and the M1 counts. `--dump-roles` prints a layout with upstream's letters (`cells:427-430`: `~ _ = . S W T | # % o`) for eyeballing. |
| `test/host/test_romgen_world.c` | S0 tests |
| `test/host/test_romgen_roles.c` | S1a tests (synthetic + real-ROM) |
| `test/host/test_romgen_regions.c` | VXR5 round trip through vendored `voxel_regions.c` |
| `test/host/test_romgen_signs.c` | VXS2 round trip through vendored `voxel_sign.c` |
| `test/host/rg_fixture.h` | Synthetic mini-ROM builder (tilesets, layouts, events) in the style of `test/host/vx_fixture.h` |

Device integration (writing `sdmc:/3DGBA/voxel/*.bin`, SHA-1 cache, progress screen) is S4, not here;
`rg_run` is the seam. `.gitignore` already ignores `voxel/*.bin` (`.gitignore:58-61`); the CLI's
OUTDIR must be outside the repo or under an ignored path, and the tests write to `mkdtemp` dirs.

Build line (host, every test):
```
clang -std=c11 -Wall -Wextra -O2 -ffp-contract=off -fsanitize=address,undefined \
      -I source/romgen -I source/voxel -I test/host test/host/test_romgen_X.c \
      source/romgen/rg_world.c source/romgen/rg_art.c source/romgen/rg_roles.c \
      source/romgen/rg_regions.c source/romgen/rg_signs.c source/voxel/vx_lz77.c [consumer files] -o /tmp/trgX
```
Device Makefile: add `source/romgen` to SOURCES and `-ffp-contract=off` for that directory (a
per-directory CFLAGS rule; romgen is not hot enough to need FMA).

---

## 7. Test plan

No reference output exists (PHASE.md "Validation"). Three layers: synthetic unit tests, real-ROM
invariants, consumer round trips. Real-ROM mode: `ROMGEN_ROM=/path/emerald.gba`; without it those
tests print `SKIP` and still pass, so the suites run in CI without a ROM. Nothing ROM-derived is ever
written into the repo.

### 7.1 `test_romgen_world.c` (S0)

Synthetic (rg_fixture.h): NULL tileset; uncompressed and LZ77 tiles; metatile count by adjacency and
the 512 fallback; palette split at 6 vs tile split at 512 independently; hflip/vflip; index 0 not
drawn on either layer; tile past `tilesBytes` ⇒ quad skipped; layout table walk stopping at an
implausible entry; NULL entry skipped; a header whose layoutId disagrees ⇒ `RG_ERR_LAYOUT_ORDER`;
group counts by adjacency; outdoor = any map; warp/sign union across two maps sharing a layout;
kind 5..8 bg events are not signs; alternate rule (equal size + pair + share ≥ 0.5; highest share wins).

Real ROM (M1 pins): 442 layouts; 518 maps with the exact group-count vector of section 1.3; 0 order
mismatches; layout 242 has a NULL secondary; the 36 unused ids; 82 outdoor maps; 533 sign events and
1 313 warps; 73 tilesets / 76 pairs; General's metatile count 512 and 0x083DF884's 8; behaviour 0x8B
only in tileset 0x083DF83C; the 15 alternates of section 1.5 exactly.

### 7.2 `test_romgen_roles.c` (S1a)

Synthetic: one hand-built layout per branch of role_at (water beats blocked; jump beats walkable;
stair via a banded 16×16 drawing with across ≤ 4 / down ≥ 15 and its near-miss twin; signpost from a
sign event; post_metatiles carrying a sign metatile from layout A to a wall-backed cell in layout B;
the lamp branch with exactly one blocked side in houses; house flood limits ±5 / 7 rows / foliage
stop; fence both orientations; fence NOT when a neighbour is off-map; cliff vs shelf). treads
boundary: drawings placed at across = 4.0 exactly and down = 15.0 exactly ⇒ true.

Real-ROM invariants (from upstream docstrings, resolved via `(group, num)` so no names are needed):
1. Every role is in {0,1,2,3,4,5,7,8,9,10}; PROP never appears.
2. Every FLOOR/STAIR cell is unblocked; every SIGNPOST/WALL/TREE/FENCE/CLIFF/SHELF cell is blocked;
   every cell with a WATER behaviour is WATER; every JUMPS cell is LEDGE.
3. SIGNPOST only in outdoor layouts.
4. **Route 104 (map 0/19, layout 20, 40×80): its signs open on three sides are SIGNPOST at their
   event cells** (PHASE.md validation 2). M1 sign events: (20,50) (27,66) (23,5) (7,20) (17,23).
5. **Littleroot (map 0/9, layout 10, 20×20): the house signs stand against the house's wall and are
   still SIGNPOST** (`cells:287-289`). M1 sign events: (15,13) (6,17) (7,8) (12,8); (7,8) and (12,8)
   are the wall-backed ones. Assert all four are SIGNPOST.
6. **Rustboro (map 0/3, layout 4, 40×60): it has SIGNPOST cells with no sign event (street lamps,
   `cells:282-283`) and at least one of them comes from the lamp branch (one blocked side, in
   houses(), `cells:297-299`).**
7. **The General tileset's flight of steps measures about 2 across and 28 down** (`cells:46-48`):
   some metatile < 512 has treads true with across in [1.5, 2.5] and down in [27, 29]; print it.
8. Every WALL cell lies within 5 columns and 7 rows above some warp on a HOUSE_DOORS cell.
9. Determinism: two runs and pair-order permutation give byte-identical output.

### 7.3 `test_romgen_regions.c` (S1a output)

Write regions.bin to `tmpdir/voxel/regions.bin`, `chdir(tmpdir)`, build the vendored consumer with
`-DVOXEL_HOST_FILES` (`source/voxel/voxel_file.h:15-16`), call `VoxelRegions_Init()` and assert
`VoxelRegions_RoleAt(id, x, y) == roles[id][y*w+x]` for **every** cell of every layout, plus
`VOXEL_ROLE_FLOOR` for an out-of-range cell and an absent id. Negative: corrupt magic, a descending
id, a role byte 11 ⇒ `VoxelRegions_Init()` false (proves our writer and their parser agree on what is
invalid). Links `voxel_regions.c` + `ctr_shims_pure.c`.

### 7.4 `test_romgen_signs.c` (S1b output)

Unit: `cutout_mask` (ring with a hole ⇒ hole filled; solid border pixel stays set; empty ⇒ 0);
`head_mask` rejections (`popcount(rows[0]) > 14`, width 15, 161 pixels, no 8-connected seed);
`head_ground` exact-match return, then the first-inserted tie-break, then the `y+2` fallback, then 0.
Round trip: write signposts.bin + regions.bin, init both vendored consumers (`VoxelRegions_Init`,
`VoxelSign_Init`), and for every record assert `VoxelSign_IsCell(inst, x, y)` with a
`VoxelMapInstance{layoutId, originX=0, originY=0}`; for records with a head assert
`VoxelSign_HeadGround(inst, x, y-1, &m)` returns true and `m == headGround`; for a cell that is not a
record assert false. Links the same consumer set as `test/host/test_voxel_mesh.c:5-13`.
Real-ROM: Rustboro has records with a non-zero head (the lamps' lanterns); every record's
`rows` is non-zero; record count printed.

### 7.5 Emulator comparison (PHASE.md validation 3)

After S1, in the app with only regions.bin + signposts.bin present: Littleroot, Rustboro and Route 104
signs and lamps render as pixel-masked posts (not boxes), compared by eye with Zallax's README
screenshots. Recorded in the BUILDLOG, not automated.

---

## 8. Slice order and exit criteria

| Step | Content | Exit criterion |
|---|---|---|
| S0.1 | `rg_world` (1.1-1.3, 1.5) + world tests | test_romgen_world green synthetic + real-ROM pins of 7.1 |
| S0.2 | `rg_art` + tileset decode (1.4, 3.2) | layer decode/flip/palette-split tests green; the General flight measures (7.2 item 7) |
| S0.3 | CLI skeleton with `--dump-roles` stub and `--time` | `romgen emerald.gba /tmp/x` opens the world and prints the M1 counts |
| S1.1 | `rg_roles` | test_romgen_roles green incl. invariants 1-9 on the real ROM |
| S1.2 | `rg_regions` + round trip | test_romgen_regions green; every cell agrees through the vendored parser |
| S1.3 | `rg_signs` + round trip | test_romgen_signs green; records sorted, count in 1..65535 |
| S1.4 | Timing + in-app check | CLI `--time` on the PC recorded; the same `rg_run` timed on New 3DS in a debug build (number in BUILDLOG; S4 decides on-device vs PC fallback); 7.5 eyeball check |

S1 is "done" when S1.1-S1.3 are green and S1.4's numbers are recorded. S2 (buildings) and S3
(relief) consume `RgWorld`, `RgPair` and the role array (see SURVEY-S2-S3.md), so the S0 API must not
change shape after S1 without updating that survey.

---

## 9. Open points and ambiguities (recorded, not guessed)

1. **Alternate layouts** (1.5): structural rule, not upstream's name rule. May differ for unused
   layouts; inert for layouts the game never shows.
2. **Tileset art folder lookup** (1.4 T3): if upstream ever failed to find a tileset's folder it saw no
   pixels; we always see them.
3. **Uncompressed tile length** (1.4 T1): unknown in the ROM; 512×32 clip.
4. **PROP role** is listed in the docstring but unreachable (3.4).
5. **Placeholder layouts**: upstream would crash on a nameless layouts.json entry (`regions:56`); BPEE
   has none (M1: no NULL entries), so it cannot matter here.
6. **`MB_CLOSED_SOOTOPOLIS_DOOR` = 0x8B and `MB_PETALBURG_GYM_DOOR` = 0x8D** never occur in static
   blockdata (M1), so they cannot be checked against output; 0x8B is verified by tileset census only.
