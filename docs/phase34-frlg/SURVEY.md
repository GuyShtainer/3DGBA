# PHASE 34 SURVEY — the Zallax-style voxel look for FireRed / LeafGreen

Status: survey only, nothing built. 2026-10-06. No C code in this document.

**Legal / provenance rules (binding, ip-legal verdict).** Numbers only: ids, addresses, value sets, permutations.
Short identifier names appear in comments only. No decomp text is quoted. Pinned references:
- pret/pokefirered `037335f4c725d7c9aecdac87066f2002b4bd7e14` (2026-09-26)
- pret/pokeemerald `731ad5bfd6e6f265508d0efcca0ba42f9dcf5881` (the Phase 33 pin)
- Gummygamer gen1recomp-voxel-frlg `7a55b219aeae1d494ba09f2ccb4710066306bf58` (MIT)
- FireRedHD2D `1aea16bcae96164828de0a04f778748be2a913ff` (MIT)

**Measurement keys.** **M1** = a number measured on Guy's own ROM with a throwaway Python script of ours (not Zallax
code, nothing committed): `roms/firered.gba` = BPRE rev 1, SHA-1 `dd5945db9b930750cb39d00c84da8571feebf417`;
`roms/leafgreen.gba` = BPGE rev 1, SHA-1 `7862c67bdecbe21d1d69ce082ce34327e1c6ed5e`.
Both header revision bytes (0xBC) are 0x01. The rev 0 cartridges were not available here.

Sections:
1. ROM layer  2. Behaviour values  3. Renderer side  4. Buildings  5. Terrain / relief
6. Trees, props, signposts, regions  7. Recommended plan  8. Open questions

---

## 1. ROM layer: what is the same as Emerald, what moved

### 1.1 Table addresses (M1)

| Item | Emerald BPEE | FireRed rev1 (BPRE) | LeafGreen rev1 (BPGE) |
|---|---|---|---|
| `gMapGroups` (u32 pointer array) | 0x08486578 | 0x08352718 | 0x083526F8 |
| `gMapGroups`, rev 0 (from `gamestate.h`, not re-measured) | n/a | 0x083526A8 | 0x08352688 |
| `gMapLayouts` (u32 pointer array, 1-based ids) | 0x08481DD4 | **0x0834EBFC** | **0x0834EBDC** |
| group count | 34 | **43** | 43 |
| map headers | 518 | **425** | 425 |
| layout slots | 442 (none NULL) | **384 slots, 18 NULL** | 384 slots, 18 NULL |
| max `layoutId` used by a header | 440 | 383 | 383 |
| General (primary) tileset | 0x083DF704 | **0x082D4B04** | 0x082D4AE4 |
| Building (primary) tileset | 0x083DF884 | **0x082D4C24** | 0x082D4C04 |
| distinct non-NULL tilesets in layouts | 73 | **63** (+ the NULL tileset) | 63 |

Method (a scratch Python script, ROM bytes only): read the 43 group pointers at `gMapGroups`. Each group's map count
= (next group array start - this start) / 4, the last group ending at the table itself (the same rule as SPEC-S0-S1
1.3). Group counts are [5,123,60,66,4,6,8,10,6,8,20,10,8,2,10,4,2,2,2,1,1,2,2,3,2,3,2,1,1,1,1,7,5,5,8,8,5,5,1,1,1,2,1]
= 425, with 0 bad pointers. `gMapLayouts` was then found by searching the ROM for the u32 pointer of layout-id-1's
`MapLayout`. The hit at the addresses above is **consistent for all 309 distinct layout ids** that headers reference
(`rd32(gMapLayouts + 4*(id-1)) == header.layoutPtr`). That is the same 1-based self-validation Phase 33 uses.

**FR vs LG.** The two ROMs have identical group/map/layout structure. Every address is a constant shift of -0x20 for
the data tables in this region (and 0x20 on the `gMapGroups` table). Both have the same 384-slot layout table, so the
generated *geometry* is the same for FR and LG except where the layout content itself differs (LG has a different
tileset palette set in some places and different wild data, which romgen ignores). **Design consequence:** do not
carry a per-game, per-rev address table for `gMapLayouts`. Find it at runtime: take header 3/0's layout pointer
and the header's `layoutId`, search the ROM for that pointer, and validate with the all-headers consistency rule. The
table is found in about a 16 MB scan. `gMapGroups` is the only address that must come from a profile, and
`gamestate.h:423` already holds all four values (FR rev0/1, LG rev0/1) with the same self-validating probe idea. The
rev 0 numbers for `gMapLayouts` were not measured (no rev 0 ROM here); the runtime search covers them.

**Pallet Town sanity row (FR rev1, M1):** map (group 3, num 0): header 0x08350688, layoutId 78, mapType 1, 24x20,
tilesets (0x082D4B04 General, 0x082D4B1C pallet_town), 3 warps (6,7)/(15,7)/(16,13), 5 bg events at
(16,16)/(4,7)/(13,7)/(9,11)/(5,14), all kind 0. Route 1 (3,19) is layout 89, 24x40, and shares the same two tilesets. Group 4 map 0
(the player's house 1F) is layout 1, 13x10, tileset pair (0x082D4C24 Building, 0x082D4CE4).

### 1.2 Struct differences (confirmed against pret and the ROM)

| Struct | Emerald | FRLG | Effect on romgen |
|---|---|---|---|
| `MapLayout` | 24 B: w, h, border*, map*, primary*, secondary* | same 24 B **plus u8 borderWidth @0x18, u8 borderHeight @0x19** | Border block is not 2x2 any more. M1: 331 layouts are 2x2, 7 are 3x2, 28 have **0x0** (indoor "no border"). romgen never reads the border (grep `border` in `source/romgen`: 0 hits), so no change. The renderer's adapter reads 4 border cells (`vx_adapter.c:302`, `voxel_world.c:865`), which is fine for 2x2 and 3x2 and must tolerate 0x0 (indoor). |
| `MapHeader` | layout* @0, events* @4, ..., layoutId u16 @0x12, mapType u8 @0x17 | **same offsets** for these four (regionMapSectionId @0x14, cave @0x15, weather @0x16 identical) | None. `GBA_OFF_MH_*` reusable as is. |
| `MapEvents` | counts @0..3, objects @4, warps @8, coords @0xC, bgs @0x10 | **same** | None. M1 FR: 1294 warp events; bg kinds 0 x422, 7 x183, 1 x73, 3 x14, 4 x10. |
| `WarpEvent` | 8 B: x, y, elev, warpId, mapNum, mapGroup | same | None. |
| `BgEvent` | 12 B, kind u8 @5; sign = kind <= 4 | same layout; kinds 0 any / 1 N / 2 S / 3 E / 4 W, **7 hidden item** (8 secret base is unused) | The Emerald "sign = kind <= 4" rule works. FRLG has 0 secret bases. 506 sign events in FR vs 533 in Emerald; 183 hidden items are dropped by the same rule. |
| `Tileset` | isCompressed @0, isSecondary @1, tiles* @4, palettes* @8, metatiles* @0xC, **attrs* @0x10** | same first four; **callback* @0x10, attrs* @0x14** | `rg_world.c` reads attrs at +0x10: **must read +0x14**. (Already known: `SPEC-S0-S1.md` 1.4 mentions it.) |
| metatile attribute | **u16**; behaviour = low 8 bits; layer type bits 12-15 | **u32**; behaviour = **bits 0-8 (mask 0x1FF)**, terrain 9-13, enc type 24-26, layer type 29-30 | `rg_attr` returns u16 and `rg_behaviour` masks 0xFF. Both need u32/9-bit. M1: 143 505 of the used cells are behaviour 0 and the high bits are populated (bits 14-23 non-zero in 325 of 640 General entries). |
| primary sizes | 512 metatiles / 512 tiles / 6 palettes | **640 metatiles / 640 tiles / 7 palettes** (13 total; 1024 total tiles and metatiles) | **The biggest edit**: `RG_NUM_PRIMARY 512`, the palette split `pal < 6`, the tile split `tile < 512`, the uncompressed length cap `512*32`, `RgPair.layer[RG_METATILES]` sizing, plus the same constants scattered through the Zallax-derived helpers. About 14 sites in `rg_world.c`/`rg_art.c` (grep: 512, `< 6u`, `0x3FF`). |
| metatile record | 8 u16 per metatile (16 B), tile = bits 0-9, hflip 0x400, vflip 0x800, pal = >>12 | **identical** | `rg_art` pixel logic is reusable unchanged. |
| metatile count rule | `(attrs - metatiles) / 16` (u16 attrs: 2 B/metatile x 8... works because attrs follow metatiles) | the same `(attrs - metatiles) % 16 == 0` holds in FRLG with u32 attrs placed after the metatiles: M1 General = **640**, the secondaries 79 to 384 (89, 95, 79, 134, 118 ...), 0 remainder in all 63 | The rule is reusable, with the cap raised from 512 to 640 (primary) and 384 (secondary, i.e. <= 1024 - 640). |
| cell word | metatile & 0x3FF, collision 0xC00, elevation 0xF000 | identical masks | `rg_metatile` reusable. The metatile id space is 10 bits, so secondary ids run 640..1023. |
| ROM total | 16 MB | 16 MB | Pointer guard unchanged. |

### 1.3 What `rg_world` and `rg_art` need to change

Everything is parameterisable by one profile struct (section 7), with these fields:
- `numPrimaryMetatiles` (512/640), `numPrimaryTiles` (512/640), `numPrimaryPals` (6/7), `maxMetatiles` (1024 total).
- `attrBytes` (2/4), `attrOffsetInTileset` (0x10/0x14), `behaviourMask` (0xFF/0x1FF).
- `gMapGroups` address candidates, group count (34/43), `gMapLayouts` (found, not stored).
- Game-code gate (`BPEE` / `BPRE` / `BPGE`) and its error codes. `RG_ERR_NOT_BPEE` becomes a generic game error.
- The outdoor map-type set {ROUTE 3, TOWN 1, UNDERWATER 5, CITY 2, OCEAN_ROUTE 6} transfers verbatim: FRLG uses the
  same numbers 1-9 (`gba_game.h:181-190`; FRLG leaves 2, 5, 6, 7, 9 unused). M1 FR types: 8 indoor x262, 3 route x57,
  4 underground x87 (caves, correctly non-outdoor), 1 town x19. So **76 outdoor maps** (Emerald: 82).

Other Emerald-isms in the Phase 33 code that FRLG will not satisfy:
1. `rg_world_open` verifies `BPEE` and the table invariant. Replace the gate by a profile table.
2. The alternate-layout inheritance rule (SPEC-S0-S1 1.5) is purely structural (size + tilesets + >= 0.5 shared
   blockdata) and so transfers. In FRLG the alternates are few (layouts not referenced by any header: e.g. the unused
   dummy slots); the rule needs a re-measure. Not a blocker.
3. Fingerprint tests pin M1 numbers per ROM (442 layouts, 518 maps, 15 alternates). Each profile needs its own pinned
   census; the FR numbers above (384/18/425/309) are the first ones.
4. Palette split: in Emerald, "indices 0-5 primary, 6-12 secondary, whichever tileset the tile comes from".
   FRLG: 0-6 primary, 7-12 secondary (M1 not re-checked on pixels, the constant is `NUM_PALS_IN_PRIMARY 7`).
   Plan: assert by rendering Pallet Town once and eyeballing.

---

## 2. Behaviour values: FRLG numbering vs Emerald

Emerald's enum was renumbered in FRLG: the low range 0x00-0x5x is mostly shared (water, currents, jumps' first
four), but **everything from 0x60 up differs**, and several Emerald names do not exist at all. The `rg_behavior.h`
sets therefore cannot be reused as is; using them on FR would classify a Kanto dresser as a door, stair warps as
"flat", and rock stairs as water. Numbers below are from the two pinned header files (Emerald enum counted,
FRLG `#define`s) and then checked against what Kanto metatiles actually carry (M1 census over every cell of every
layout in FR rev1).

### 2.1 Per set used by `rg_*`

| `rg_*` set (Emerald values) | FRLG equivalent | Notes |
|---|---|---|
| **water** `rg_is_water`: 0x10 pond, 0x11 interior-deep, 0x12 deep, 0x13 waterfall, 0x14 sootopolis-deep, 0x15 ocean, 0x16 puddle, 0x17 shallow, 0x19 no-surfacing, 0x22 seaweed, 0x28 hot springs, 0x2A seaweed-no-surfacing, 0x2B reflection-under-bridge, 0x50-0x53 currents | **0x10 pond, 0x11 fast water, 0x12 deep, 0x13 waterfall, 0x15 ocean, 0x16 puddle, 0x17 shallow, 0x19 underwater-blocked-above, 0x1B cycling-road water, 0x22 seaweed, 0x28 hot springs, 0x50-0x53 currents** | 0x14 does not exist in FRLG. **0x2A (rock stairs) and 0x2B (sand cave) are NOT water in FRLG** (M1: 469 and 516 cells, they are cave floors). 0x1B (751 cells in FR) is the Route 17 / Seafoam water-bike variant and has to be added. M1 FR water-ish cells: 0x15 x40 507, 0x11 x2 997, 0x17 x1 134, 0x10 x647, 0x13 x92, 0x16 x122, 0x1B x751, 0x28 x37, currents 0x50-0x53. |
| **jump / ledge** `rg_is_jump` = 0x38..0x3F | **0x38..0x3B only** (E, W, N, S) | FRLG has no diagonal ledges. Must narrow the range or the 0x3C-0x3F tests would read unrelated values (nothing uses them in FR, so it is harmless but sloppy). M1: 0x38 x41, 0x39 x46, 0x3B x1022; **no north ledge (0x3A) cell is used in any FR layout**. Kanto ledges are essentially all south-facing. |
| **doors** `rg_is_house_door`: 0x69 animated, 0x8D petalburg gym, 0x8B closed sootopolis | **0x69 warp door only** (M1: 165 cells), plus 0x60 cave door (174), 0x61 ladder, 0x62-0x65 arrow warps, 0x66 fall warp, 0x67 regular warp, 0x6C-0x6F stair warps | 0x69 is the one value that survives with the same meaning (door with animation). **0x8B in FR is MB_DRESSER and 0x8D is the cable-club monitor**: drop both, or buildings code would call a dresser a door. Door detection in Kanto = 0x69 for houses and shops, plus 0x67 regular warp for plain-mat exits and 0x60 for cave mouths. |
| **flat** (`rg_is_flat_behaviour`: bridges 0x70-0x7F, logs, 0x0A no-running, 0x60, 0x69, 0x6C, 0x8B-0x8D, 0xBE, 0xEA) | FRLG has **no bridge, log or Pacifidlog behaviours at all**. 0x0A running-disallowed (M1: not used; the bike-forbidden indoors use 0x08 cave / 0x0B indoor encounter), 0x60 cave door, 0x69 warp door, 0x6C-0x6F stair warps, 0x6A/0x6B escalators | The Kanto bridges (Route 12/25 etc.) are normal metatiles with water behaviour 0x1B or 0x17 underneath; the plank art is tile data. So "bridge" cannot be detected by behaviour. Kanto's only elevated-walk pieces are metatile-id-based (see section 5). |
| **sand** `SANDS`: 0x06 deep sand, 0x21 sand, 0xBF secret-base sand ornament | **0x21 sand only** (M1: 2878 cells; also 0x2B sand-cave 516) | |
| **tall grass** (relief / props): 0x02 tall grass, 0x03 long grass, 0x07 short grass, 0x09 | **0x02 tall grass only** (M1: 5701 cells) | Emerald's long/short grass behaviours do not exist. Plain grass is behaviour 0x00 with a particular tile. |
| **cave / indoor encounter** | 0x08 cave (33 090 cells, mostly the cave floors), 0x0B indoor encounter, 0x0C mountain top (1127 cells) | 0x0C is Mt. Moon / Rock Tunnel / Victory Road. |
| **solid walls etc.** | 0x30-0x33 impassable E/W/N/S (M1: 0x32 x2205 - these are the ledge-like cliff edges), 0x20 strength button, 0x23 ice (189 cells in Seafoam), 0x26 thin ice, 0x27 cracked ice | |
| **furniture/props as behaviours** | 0x80 counter, 0x81 bookshelf, 0x82 pokemart shelf, 0x83 PC, 0x84 signpost, 0x85 region map, 0x86 television, 0x87 Pokemon Center sign, 0x88 Poke Mart sign, 0x89 cabinet, ... 0xA3 | These are indoor/interactive; in Emerald they sit at about the same range 0x80-0xA3, but the **members differ**. The signpost behaviour 0x84 (M1: 239 cells) is what marks Kanto's wooden signs and could replace the BgEvent rule as a cross-check. |
| **cycling road** | 0xD0 pull-down (2041 cells), 0xD1 pull-down with grass (66) | One more Kanto-specific value. |
| **number space** | 9 bits (0x1FF), `NUM_METATILE_BEHAVIORS 0xF0` | Max value seen in Kanto layouts: 0xE0 (11 cells). Emerald counts to 0xF0 too. |

### 2.2 The M1 census, for reference (behaviour value: cell count, FR rev1, all layouts)

0x00 143 505; 0x02 5 701; 0x08 33 090; 0x0C 1 127; 0x10 647; 0x11 2 997; 0x13 92; 0x15 40 507; 0x16 122; 0x17 1 134;
0x1B 751; 0x20 15; 0x21 2 878; 0x23 189; 0x26 9; 0x28 37; 0x2A 469; 0x2B 516; 0x30 38; 0x31 25; 0x32 2 205; 0x33 11;
0x38 41; 0x39 46; 0x3B 1 022; 0x50-0x58 about 500; 0x60 174; 0x61 149; 0x62 33; 0x63 31; 0x64 16; 0x65 227; 0x66 33;
0x67 64; 0x69 165; 0x6A 4; 0x6B 2; 0x6C 36; 0x6D 47; 0x6E 84; 0x6F 50; 0x71 1; 0x80-0xA3 (furniture) about 1 900;
0xD0 2 041; 0xD1 66; 0xE0 11.

Layer-type bits (attr bits 29-30): in the General primary 341 metatiles are type 0 and 299 type 1 (covered). No
type 2 in General. The layer type does not matter to the Phase 33 art code (it composes lower+upper in fixed order,
`rg_art.c:75-110`), but the **renderer's treatment of "which half is above the player" is read from the layer
type in FRLG and from attr bits 12-15 in Emerald** (check `source/voxel` for any `>> 12`).

### 2.3 Required new table

A `RgGameProfile` behaviour block: `is_water`, `is_jump`, `is_house_door`, `is_flat`, `is_sand`, `is_tall_grass`,
as per-profile function pointers or 256-bit bitsets (a bitset is simpler and data-driven, and it needs 9 bits: use
a 512-bit set). No algorithmic change in `rg_roles.c`/`rg_ledge.c`/`rg_grelief.c`.

---

## 3. Renderer side (`source/voxel` + `vx_host.c`)

The renderer does not read the ROM tables directly. `vx_snapshot.c` copies fixed RAM blocks out of the emulated
core and `vx_adapter.c` rebuilds the decomp-style globals (`gMapHeader`, `gBackupMapLayout`, `gObjectEvents`,
tileset objects...) from them. Every address is a compile-time `GBA_ADDR_*` macro in `gba_game.h`, and the Emerald
gate is `sBpee` (`vx_host.c:68`, `:423`). So the work is "turn ~18 macros into a per-game runtime profile".

### 3.1 RAM and ROM anchors: what we already have for FRLG, what must be harvested

| Anchor (macro) | Emerald | FR / LG (both revs) | Source for FRLG |
|---|---|---|---|
| gMain | 0x030022C0 | 0x030030F0 | `gamestate.h` (verified vs pret sym) |
| gMain+4 callback2, +0x439 inBattle bit 1 | same | **same offsets** (checked in pret `main.h`) | none needed |
| gSaveBlock1Ptr | 0x03005D8C | 0x03005008 | `gamestate.c` BPRE/BPGE row |
| gBackupMapLayout (IWRAM, 12 B) | 0x03005DC0 | 0x03005040 | same row, `mapLayout` |
| **gBackupMapData** (EWRAM buffer; adapter check A1) | 0x02032318 | **UNKNOWN** (not in `gamestate`) | harvest: it is the `map` pointer that gBackupMapLayout holds at runtime, so read it live and validate it lies in EWRAM; drop the fixed-address check |
| gMapHeader (0x1C B) | 0x02037318 | 0x02036DFC | `gamestate.h` |
| gObjectEvents (16 x 0x24) | 0x02037350 | 0x02036E38 | `gamestate.c` (stride 0x24 and fields to +0x18 identical, checked in pret) |
| gPlayerAvatar | 0x02037590 (0x24 B) | 0x02037078 (**0x20 B**) | `gamestate.c`; the adapter reads +0, +4, +5 only, and the FR struct has these, so only the copy size shrinks |
| gSprites (65 x 0x44) | 0x02020630 | 0x0202063C | `gamestate.c` |
| gPlttBufferUnfaded | 0x02037714 | 0x020371F8 | `gamestate.c` |
| gPaletteFade | 0x02037FD4 | **UNKNOWN** | harvest (pret symbol map, or find via the ROM's `BeginNormalPaletteFade` literal pool, as BUILDLOG-P2 did for Emerald) |
| gWeather block + 5 field offsets (0x6D0, 0x6C6, 0x730, 0x6FB, 0x724) | 0x02038454 | **UNKNOWN**; the weather struct is a different layout | harvest the same way; weather ids 0-14 are **identical** in FRLG (checked), so the id translation is free |
| gMapGroups | 0x08486578 | 0x08352718 (FR r1) / 0x083526F8 (LG r1); r0 0x083526A8 / 0x08352688 | M1 + `gamestate.h:423` |
| gMapLayouts | 0x08481DD4 | 0x0834EBFC / 0x0834EBDC (r1) | M1; runtime-search recommended (1.1) |
| `gObjectEventGraphicsInfoPointers` + count | 0x08505620, 239 | **UNKNOWN**; pret says 152 graphics ids (240-entry table?) | harvest; the adapter indexes `graphicsId < GBA_GFX_INFO_COUNT` (`vx_adapter.c:498`) |
| field-effect object template table (37 entries) | 0x085059F8 | **UNKNOWN** | harvest; the `FLDEFFOBJ_*` index constants (`gba_game.h:226-235`: tall grass 4, ripple 5, ...) are positional and must be re-checked for FRLG |
| CB2_Overworld / CB2_OverworldBasic (ROM, thumb) | 0x08085E5D / 0x08085E51 | **UNKNOWN** | harvest; `voxel_world.c:264`, `vx_adapter.c:713` compare against these |
| tileset General / Fortree / GenericBuilding | 0x083DF704 / ...7C4 / ...B6C | General **0x082D4B04**, Building primary 0x082D4C24 (LG -0x20) | M1; **rev0 values differ**, so derive at runtime (the primary tileset used by the most outdoor layouts; 181 layouts use 0x082D4B04) |
| `MAP_OFFSET` 7, `MAX_MAP_DATA_SIZE` 10240, metatiles-total 1024, tile 4bpp 32 B | same | **same** (pret `fieldmap.h`) | none: `GBA_BACKUP_MAP_BYTES` and the 10240-cell snapshot buffer carry over |

Unknowns need a "symbol harvest" slice. No FRLG `.sym` is local; the pret `symbols` branch holds rev0/rev1 maps for FR
and LG (the repo's `gamestate.h` cites them as `pokefirered.sym` / `_rev1.sym`). Doing the harvest from the pinned
pret commit's `sym_*.txt` plus disassembly literal pools is the Phase 32 BUILDLOG-P2 method. The adapter must be
self-validating per game (the `gamestate` probe idea): each anchor is checked against the live state before use, so a wrong
rev disables voxel instead of reading garbage.

### 3.2 Struct-level changes in the adapter (`vx_adapter.c`)

1. **Tileset decode** (`:227-279`): read attrs at +0x14 as u32, with 640/7/640 split constants. **Trick that keeps the
   200 vendored lines unchanged:** convert on intern to the host `u16 metatileAttributes[]` the vendored code expects,
   packed as `behaviour(9 bits) | layerType << 12` (Emerald's own layout puts the layer type at bits 12-15). Then
   `UNPACK_BEHAVIOR` widens from 0xFF to 0x1FF (safe for Emerald: its enum stops at 0xF0). Host structs stay u16.
2. **`NUM_TILES_IN_PRIMARY` / `NUM_METATILES_IN_PRIMARY` / `NUM_PALS_IN_PRIMARY`** (`gba_game.h:166-176`): 512/512/6 become
   per-game values (640/640/7). They are used all through `voxel_atlas.c` (the colour split between primary and secondary
   palette) and `voxel_world.c` (`used[]` arrays sized by `NUM_METATILES_TOTAL 1024`, which is unchanged).
3. **Metatile ids**: all secondary-tileset ids are 640..1023 instead of 512..1023. Any renderer table keyed by metatile id
   (tree parts, interior furniture ids, `Fortree` puddle exclusions at 0x288-0x29A) is Emerald-only; see below.
4. **Layer type**: the renderer treats `metatile` as lower+upper and draws "upper over the player" by its own
   rules; FRLG's layer types (0 normal, 1 covered, none of type 2 in General) map the same way as Emerald's NORMAL/COVERED. Verify on Pallet Town.
5. **Border**: 3x2/0x0 borders; the adapter reads `border[0..3]` only.
6. **Header decode** (`DecodeHeader`, 0x1C bytes) and connections: unchanged (same offsets, 12-byte connection entries).
7. **Object events**: unchanged through `+0x18` (current coords @0x10, facing @0x18, flags bits). The sprite template
   pointer lookup (`GBA_OFF_SP_TEMPLATE 0x14`) and the OAM struct are engine-level and identical.
8. **GFX info table**: FRLG sprite sizes/images offsets (`GBA_ROM_GFXINFO_*`, `:150-155`) must be re-measured; the
   FRLG `ObjectEventGraphicsInfo` struct is believed to be the same 36 B, but this is an assumption to check.

### 3.3 Behaviour-driven code in the renderer (`vx_behavior.c`, `voxel_world.c`)

| Site | Emerald logic | FRLG change |
|---|---|---|
| `MetatileBehavior_IsSurfableWaterOrUnderwater` | 16 values incl. 0x14, 0x2A, 0x6C, 0x6D, 0x6F | FRLG: 0x10, 0x11, 0x12, 0x13, 0x15, 0x19, 0x22, 0x50-0x53 (+0x1B for water-bike water if wanted). 0x2A/0x6C/0x6D/0x6F in FRLG are rock stairs and stair warps: **must be excluded** |
| `IsReflective` | pond 0x10, puddle 0x16, 0x1A, ice 0x20, 0x14, 0x2B | FRLG: pond 0x10, puddle 0x16, ice 0x23 (not 0x20: FRLG 0x20 is the strength button). 0x1A/0x14/0x2B do not exist |
| `IsIce` | 0x20 | **0x23** (M1: 189 cells in Seafoam) |
| `IsShallowFlowingWater` | 0x17, 0x1B, 0x1C | FRLG: 0x17 only (0x1B is cycling-road water) |
| furniture: PC 0x83, counter 0x80, TV 0x86 | same values in FRLG | reusable. **0x84 is "cable box results" in Emerald but SIGNPOST in FRLG**, so keep it out of the furniture set. 0xB0, 0xB1, 0xC5 (secret base) do not exist in FRLG |
| `VoxelWorld_UsesTreeSprites` | `primary == gTileset_General` and not indoor | FR General address (derived at runtime) |
| interior furniture ids (576/577/584-586 table, 565/558/566/570 furniture, 578 sign, 514-517 decal, 567/568/575 bed), keyed to the Emerald GenericBuilding tileset | | **meaningless for Kanto**. Default FRLG interiors to FLAT until a Kanto interior id table exists (see section 4: interiors are the weakest part) |
| `VoxelTree_Part` / `GroundMetatile` | 0x1D4..0x1F5, 0x016/0x017/0x0C6/0x0C7 etc.; Emerald General ids | **entirely Emerald**. Kanto needs its own table (section 6) |

### 3.4 Host (`vx_host.c`)

Replace `sBpee` with a `VxGame` enum {NONE, EMERALD, FIRERED, LEAFGREEN} chosen from the game code plus a self-validating
anchor check; the "Voxel 3D: Emerald only" status string (`:423`) becomes the per-game list. `vx_data` loads `regions.bin` etc.
by ROM SHA-1 already, so per-game data files need no change; each game's files go to the same loose directory
under their own SHA-1-derived names. Check that `vx_data_set_rom_sha1` already keys filenames (it keys validity, not names:
two games' loose files with the same names would collide, so name them per game, e.g. `regions.bin` -> `<code>_<rev>/regions.bin`).

The Phase 32 per-ROM gate `sDataOk` already tolerates "no data files" (flat ground + extruded houses), which is
the zero-data baseline for FRLG.

---

## 4. Buildings (buildings.bin, VXB7)

### 4.1 What Zallax's table looks like and why it does not transfer

`rg_bspecs.c` (853 lines) + `rg_bexpand.c` (1303) + `rg_binterior.c` (1846) + `rg_brooms.c` (645) + `rg_buildings.c` (1614): the spec rows
name Emerald layouts (by id + FNV pin of the blockdata) and Emerald tileset addresses (`gTileset_Petalburg`, `gTileset_Rustboro`), and
list metatile ids (kit corners, feet, tops) and the exact pixel rectangles for hand-picked buildings. Nothing in them matches Kanto: the layouts, the tilesets, and
the art are different. **The geometry machinery (`rg_geom`, `rg_bimg`, atlas packer, VXB7 writer, `rg_bcheck`) is
game-independent and reusable**; only the spec data and the "what is a building" decision need replacing.

### 4.2 Kanto building census (M1, FR rev1)

- 76 outdoor maps use 16 distinct outdoor secondary tilesets (e.g. pallet 0x82D4B1C, viridian 0x82D4B34, pewter ...4B4C, cerulean ...4B64,
  lavender ...4B7C, vermilion ...4B94, celadon ...4BAC, fuchsia ...4BC4, cinnabar ...4BDC, indigo ...4BF4, sevii 1-7 secondaries ...50BC-...50EC, 4E34).
- Warp tiles on outdoor maps by behaviour: **0x69 warp door x116** (the houses, shops, gyms: these are the buildings),
  0x65 south-arrow x48 (route gates, cave mouths), 0x60 cave door x48, 0x62-0x64 other arrows. Towns: Pallet 3, Viridian 5,
  Pewter 6, Cerulean 8, Vermilion 7, Lavender 5, Celadon 10, Fuchsia 9, Cinnabar 5, Saffron 10, Sevii ~30 doors.
  So **about 100 buildings in ~25 layouts**, most reusing the same few facade shapes inside a town. (Emerald: 40 spec rows over 18 layouts.)

### 4.3 Options

| Option | What | Output | Est. cost | Verdict |
|---|---|---|---|---|
| **(a) Gummygamer's runtime approach** (MIT) | In the renderer, no data files. Classify a cell `wall`/`ground`/`water`/`void` from collision; a wall is a box whose height = **run length of solid cells down its column** (1 deep 8px, 2 deep 14, 3 deep 18, 4+ deep 22 game px); a walkable cell whose upper layer is the overhang of the solid cell south joins it; top and south face keep the tile art, east/west/north faces take the tile's average colour (shaded); outdoors, short (<=3 cells) or narrow (<=2 wide) or green structures become sprite cards, not boxes. | Boxy houses with the real roof as top art; trees as cards | Our baseline already extrudes houses (Phase 32 fallback). Porting its three refinements (run height, side colour, card props) into `voxel_mesh_builder`/`voxel_world`: **~350-500 C lines** | Cheapest, and the only one that is ROM-independent. Look: "miniature", not Zallax-grade |
| **(b) Author Kanto recipes** (Zallax style) | New spec rows per distinct building model: layout id + rect + part list + roof pitch | Real roofs, chimneys, gables | ~35 distinct models x 30-40 lines of table + PC verification loop per model, plus interiors (Center, Mart, lab) another ~600: **about 1500-2200 lines of data + 3-5 sessions of visual tuning** | Highest quality, highest cost. Only worth it for landmarks |
| **(c) Generic automatic extractor** | From the metatile layers + collision + behaviour: find each building as a blocked connected component directly north of a 0x69 door (and 0x60 for caves); split facade vs roof by the metatile's lower/upper layer (the upper layer of the top rows is the roof edge, the "overhang"); fit a box with a one-slope or two-slope roof from the roof rows' width; synthesise `VXB7` models via `rg_geom`/`rg_bimg` | Real art, roofs, in the same file format, no renderer change | **~1200-1800 lines C** (component finder ~300, facade/roof split + hull ~500, roof synth ~400, verification ~200) plus tuning on ~25 layouts. Estimated 70-80% of buildings correct without hand data; the rest fall to (b)-style overrides | Best value if (a) is not enough |

Reference check on the parts that make (c) feasible: in Emerald, Zallax's `rg_roles.c` already decides "is a house" geometrically from
a door (`HOUSE_HALF_WIDTH 5`, `HOUSE_HEIGHT 7` search window), so a door-driven building finder is within the existing design;
Kanto's windows are smaller (houses 4-5 wide, e.g. Pallet's) so these constants must be re-tuned, not reused blindly.

FireRedHD2D (MIT, Rust, 3447-line `voxel.rs` + `voxel/*.rs`) is a second reference for (c): "blocked cells extruded from the live map,
trees as whole artwork units, windows/doors stay on facades, hand-verified furniture profiles for Oak's lab and the Viridian Mart, the rest by a colour classifier".
It reads RAM only (no table addresses beyond gMain 0x030030F0), so it has nothing to reuse for the ROM layer; its value is the list of what was needed to make
Pallet/Viridian/Route 1 look right, and its stated failure modes (caves, odd buildings, unclassified furniture).

**Interiors.** `rg_binterior` + `rg_brooms` (2491 lines) are Emerald-specific (named rooms). FRLG interiors share a handful of tilesets
(general building 1/2, Pokemon Center, Mart, Lab, Museum, Gym 1-8, etc.). Recommend interiors stay FLAT in M0-M3 and become their own phase.

---

## 5. Terrain / relief

`rg_relief`/`rg_grelief`/`rg_ledge`/`rg_rdrawn`/`rg_ralias` (about 2800 lines). What keys on Emerald:
- `rg_rrock.h`: **explicit numeric metatile-id sets of the Emerald General tileset** (boulders 0x93/0x94/0x9B/0x9C, side-west 0x70/0x73, side-east 0x72/0x75/0xA2, sea caps 0x172/0x174, 24 FACE_SOUTH-ish ids, DIRT 0x113-0x115/0x14B-0x14D) and **five exact rock colours** (tops 0xDEB4A4/0xBD948B, rim, three face colours). They are what makes a metatile a "rock wall top", "face" or "side".
- `rg_behavior.h` (jump = ledge, flat set, sand set): replaced by the Kanto sets in section 2.
- Hand-drawn relief paths (`rg_rdrawn`): an Emerald table of per-layout drawn lines (a PC authoring product, not derivable).
- Layout aliases (`rg_ralias`): Emerald layout ids.

**Does the logic transfer?** The generator's algorithm (terraces by elevation/ledge, edge profiles, face/side classification, lattice sizing, `relief.bin` writer) is game-independent; its **inputs are tables for one tileset**. For Kanto:
1. Kanto General (0x082D4B04) has its own cliff art. Ledge tops are behaviours 0x38/0x39/0x3B (M1: 9+9+11 metatiles at ids 0x83-0xC9 in General) and rock stairs 0x2A (ids 0x74, 0x89, 0x91). The cliff/rock-face art uses different colours, so a fresh set of ids and ~5 colours has to be read from the tileset art (**~150 numbers, derived with a throwaway PC tool**, then pinned in a test).
2. Kanto has far less terraced relief than Hoenn: outdoor cliffs are Route 3/4 (Mt. Moon slopes), Route 9/10, Route 22/23 (Victory Road) and the Indigo Plateau; caves (type 4, 87 maps) are not outdoor and not relieved by this generator. The ledge hop (south-only; M1: no north-ledge cell at all) is a flat 1-cell drop that the existing ledge code handles with a profile for 0x3B/0x38/0x39 only.
3. Drawn-path relief (`rg_rdrawn`): omit for Kanto at first (flat terraces are acceptable; Gummygamer also has zero relief: ground 0, ledge 0, water -3 game px).

Estimate: **~200 lines of tables + ~80 test lines for the first useful Kanto relief (ledges + the few cliff groups); a fuller relief pass is ~500 lines plus PC tuning**. The ROM-wide cost is dominated by tuning, not code.

---

## 6. Trees, props, signposts, regions: per generator

| Generator / consumer | Transferable? | Needed for FRLG |
|---|---|---|
| **regions.bin** (`rg_roles`, `rg_ralias`, `rg_regions`) | Mostly. Roles come from collision + behaviour + art (water/ledge by behaviour, `rg_foliage_ge_half` by colour, fence/cliff/shelf by neighbour collision, `is_house` geometrically from doors, `is_signpost`/`is_lamp` geometric). | Behaviour sets (section 2); re-tune `is_house`/`is_lamp` for Kanto shapes; the alternate-layout re-measure; keys by layout id (Kanto ids 1-383). ~100-150 line delta once the profile exists |
| **signposts.bin** (`rg_signs`, ~319 lines) | Yes. Sign positions come from BgEvents kinds <= 4 (FR: 506 events) and the mask comes from the tile art at the event. | Profile only; the Emerald sign art (wooden post) differs from Kanto's, so check each mask against 8 sample signs. In Kanto signposts also carry behaviour 0x84 (M1: 239 cells), usable as a cross-check; Pokemon Center / Mart signs 0x87/0x88 are separate props |
| **trees** (`voxel_tree.c` ids) | **No**: numeric Emerald General ids (large trees 0x1D4-0x1F7, small 0x16/0x17/0xC6/0xC7, ground replacements). Gummygamer's method transfers: a metatile is foliage when >= 50% of its non-ground pixels are green (with >= 30 non-ground pixels), plus the "gap between trunks" rule; and the border of an outdoor map is trees on most Kanto maps. | A Kanto table of ~20-30 ids (parts + ground replacement), derived by the PC tool using the foliage test, then checked in the emulator on Route 1/2 and Viridian Forest (Viridian Forest is a cave-type map; check) |
| **props** (`voxel_props` boulders/signs; `rg_bexpand` props) | Partly | Kanto: cut trees and boulders are object events (graphics), not metatiles, so they already appear as sprites. Metatile props: fences (Route 1 ledges are separate), lamps? none, flowers (animated tiles). Defer |
| **relief.bin** | see section 5 | |
| **regions.bin water**: the renderer sinks water by role; Kanto sea 0x15 (40 507 cells) | Yes | none beyond profile |
| **animated tiles** (`CtrVoxel_NotifyTilesetAnimWrite`) | The tileset-animation write hook watches VRAM writes (engine-level); animations differ (Kanto: water, flowers, Cinnabar/Seafoam) but the mechanism is generic | verify the primary-animation tile ranges: FR animates different tile ids than Emerald (check the hook's tile-range assumption) |

---

## 7. Recommended plan

### 7.1 Principle

One **per-game profile struct** for both layers (romgen and renderer): this keeps Emerald behaviour byte-identical and makes FR/LG two more rows. Proposed fields:

```
RgGameProfile / VxGameProfile (names indicative):
  gameCode, rev detect, group count (34/43), mapGroups candidates[2]
  numPrimaryMetatiles, numPrimaryTiles, numPrimaryPals, attrBytes, attrOffset, behaviourMask
  behaviour bitsets: water, jump, house door, flat, sand, tall grass, reflective, ice, surfable
  RAM anchors (renderer only): sb1Ptr, backupLayout, mapHeader, objEvents, playerAvatar(+size), sprites, plttUnfaded, paletteFade,
                               weather(+5 offsets), gfxInfoPtrs(+count), fldeffTemplates, CB2 pair
  tables: rock ids/colours (relief), tree ids, interior ids (or "none")
```

Everything shared with Emerald: the full geometry/atlas/VXB7/VXL4 writers, the LZ77 decoder, the world open/layout/event code, connections, alternate-layout rule, sign
generator, role classifier skeleton. **Estimated shared code reuse: about 85% of the 14.9k romgen lines are untouched**.

### 7.2 Slices

| Slice | Content | New/changed C lines | Verify |
|---|---|---|---|
| **P0 profile plumbing (renderer)** | `VxGameProfile`; macros to runtime variables (~40 sites); sBpee -> `VxGame`; u32-attr intern with 640/7 split; harvest the UNKNOWN anchors (gPaletteFade, weather, gfx-info, fldeff, CB2 pair, backup map) with self-validation | 500-700 + ~150 host test | Host suite for tileset decode of FR General; emulator: Pallet Town in 3D with **no data files** (flat ground + extruded houses). |
| **P1 romgen ROM layer** | `RgGameProfile`; rg_world/rg_art constants (14 sites), FR/LG census pins (384/18/425/309, 63 tilesets), runtime `gMapLayouts` search | 250-350 changed + 200 test | `rg_world_open` on FR and LG: all-headers consistency, metatile counts 640/79..384 pinned |
| **P2 regions + signposts** | Kanto behaviour sets, `is_house`/`is_lamp` retune, alternates, per-game output file names | 150-250 | regions.bin / signposts.bin through the vendored parsers; Pallet signs at the 5 bg positions; the two house doors (6,7) and (15,7) and lab door (16,13) give WALL regions |
| **P3 trees + Kanto General tables** | Foliage-derived tree table (~25 ids), ground replacements, `VoxelWorld_UsesTreeSprites` for the FR General; behaviour shapes | 150-250 + a PC helper (not shipped) | Route 1, Viridian, Pewter in the emulator |
| **P4 buildings (a)+(c)** | Port Gummygamer's run-height/side-colour/cards into the renderer fallback (a); door-driven extractor emitting VXB7 (c) | (a) 350-500; (c) 1200-1800 | Pallet, Viridian, Pewter, Cerulean, Vermilion, Celadon visually; per-model checks via `rg_bcheck` |
| **P5 relief** | Kanto rock ids/colours, ledge profile 0x38/0x39/0x3B, Route 3/4, Victory Road | 200-500 | Route 3, Route 22/23 |
| **P6 interiors (own phase)** | FRLG GenericBuilding / Center / Mart / Lab id tables or generic classifier | 400-800 | Oak's lab, Center, Mart |
| **P7 adversarial review + release-legal-audit** | | | numbers-only check of every table, no decomp text, per-game data files untracked |

**Total new/changed: roughly 3500-5500 C lines** (renderer plumbing about 700; romgen delta 700-1000; trees/relief tables 400-700; buildings 1550-2300; interiors 400-800), most of it in P4.

### 7.3 Minimal first visible milestone

**M0 = "Pallet Town in 3D" via P0 alone** (no romgen data): player walks in Pallet Town with the Zallax camera, extruded houses, flat trees. That proves the anchor harvest, the 640/7 tileset split and the u32 attributes on real FR and LG ROMs. Then **M1 = P1+P2+P3**: Pallet Town and Route 1 with proper water/signposts/trees and real regions. Pallet (24x20, three doors, five signs, tileset pair 0x82D4B04 + 0x82D4B1C, shared with Route 1) is the best smoke map because it is the smallest, all connected maps use the same secondary, and Oak's lab/houses are one-room interiors.

### 7.4 Licensing and provenance for this phase

- Zallax code we port keeps the MIT header + "Modified for 3DGBA (GPLv3)" per `LEGAL-zallax-port.md` (already the file pattern).
- If Gummygamer's run-height rules and keyed-prop logic are re-implemented in C, record: "approach/algorithm from Gummygamer/gen1recomp-voxel-frlg @ 7a55b21, MIT; re-implemented, constants (8/14/18/22 px heights, 3-cell/2-wide prop thresholds, 50% green rule) are numeric facts"; add its MIT notice to `source/voxel/NOTICE.md` if any text is copied (a clean rewrite from the description needs no notice, but we should credit it).
- FireRedHD2D (MIT): idea-level only; no code taken.
- pret/pokefirered: numbers only, pinned `037335f`. All FRLG tables in the repo are ids/addresses/colour values with short identifier names in comments.
- Generated data (per-game regions/signposts/buildings/relief) stays ROM-derived, git-ignored and out of release assets; the audit already covers this for Emerald.

---

## 8. Open questions for Guy

1. **Interiors in scope?** Recommend: no (flat) until the overworld is solid; the Kanto interior look would be its own phase.
2. **rev0 support?** Guy's FR and LG are rev1 (both M1-verified). rev0 needs only a few more constants (gMapGroups rev0 values are known) but cannot be verified here with no rev0 ROM. Ship rev1 first?
3. **Quality bar for buildings:** accept generic (a)+(c) (box/gable look, real art) for all of Kanto, with hand recipes (b) only for a few landmarks (Pokemon Center, Mart, Oak's Lab, gyms)? Or hold out for full Zallax-style hand recipes (about 1500-2200 data lines)?
4. **FR and LG together?** The geometry is identical, so LG is nearly free after FR (only the -0x20 table shift and a palette check). Confirm both in the first release.
5. **Sevii Islands** (about 30 of the building doors): include or defer to after Kanto proper?
6. The unknown anchors need a **symbol harvest**; if pret's `symbols` branch maps are acceptable as a number source (as done for the presence phase), this is one short slice. If not, each anchor must be found by disassembly of Guy's ROM.
7. Should the voxel option stay "Emerald only" in the UI until M1 passes on hardware (New 3DS), per the hardware sign-off rule?
