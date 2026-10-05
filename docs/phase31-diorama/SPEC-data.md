# SPEC-data — Phase 31 "DIORAMA" — Emerald (BPEE) data contract

Status: **COMPLETE** (2026-09-10, slice S0). Scope: **BPEE (Pokémon Emerald, US) only**.
Every claim carries a citation: `file:line` for repo files; pret paths are relative to
`https://raw.githubusercontent.com/pret/pokeemerald/master/` (fetched 2026-09-10 to
`/tmp/pret/src/`); `pokeemerald.sym` is pret's byte-matched `symbols` branch map at
`/tmp/pret/pokeemerald.sym`. Nothing under `projects/_reference/` was opened (PHASE.md
invariant 6). §10 is the consolidated address table; §10.3 lists what is still unverified.

**Read §5.2 first if you are about to port `RESEARCH-classification.md` §3.3 — that indoor
metatile-id table is disproved here and must not be implemented as written.**

## 1. GATE inputs

The diorama has **no gate of its own** (PHASE.md invariant 5): it rides `tilt_target_level()`
plus three *data-validity* preconditions that the existing readers already compute. All four
groups below are evaluated in the parked window; the render block only reads the snapshot.

### 1.1 The existing gate ladder (reuse verbatim, add nothing)

| # | Rule | Where it lives | Note for the diorama |
|---|---|---|---|
| G1 | `userLevel <= 0` → off | `source/tilt.c:144` | level 4 = diorama (PHASE.md §"What bounds it" 3) |
| G2 | `!isN3DS` → off | `source/tilt.c:145` | New 3DS only, already true for us |
| G3 | `menuOpen` → off | `source/tilt.c:146` | pause menu closes the scene |
| G4 | `wlOn \|\| netOn` → off | `source/tilt.c:147` | never during a link |
| G5 | `!ok` (no `GameProfile`) | `source/fieldgate.h:42` | plus §1.4: BPEE only |
| G6 | `ctx != FIELD_CTX_OVERWORLD` | `source/fieldgate.h:43` | `GCTX_OVERWORLD` == `FIELD_CTX_OVERWORLD` is `_Static_assert`-pinned at `source/main.c:1224` |
| G7 | `!sb1Valid \|\| px < 0` | `source/fieldgate.h:44` | the save is actually loaded |
| G8 | `textDlg` | `source/fieldgate.h:45` | a script is talking |
| G9 | bottom screen + touch active | `source/tilt.c:152` | irrelevant: diorama is top-screen only |
| G10 | top screen + `stereoEngaged` | `source/tilt.c:153` | v1: stereo is an "After v1" item, so G10 stands |
| G11 | focus-switch, other screen | `source/tilt.c:155` | unchanged |

`GCTX_OVERWORLD` is a **fall-through** class, not a positive match (`source/gamestate.c:1138-1139`
sets `ctx = GCTX_OVERWORLD; ctxResolved = false`), i.e. "the overworld **or** an unclassified
screen". That is exactly the pokeemerald-3d stance (`RESEARCH-presenter.md` §3.6: fall back to
exactness) only if the *data* preconditions below also hold — they are what turns a fall-through
into a positive "the field engine is running" signal.

### 1.2 "Overworld running" — the data preconditions (the diorama's own three)

| # | Precondition | Read | Rationale |
|---|---|---|---|
| D1 | `gBackupMapLayout.map` is EWRAM | `(read32(0x03005DC8) >> 24) == 0x02` | the live grid pointer; the exact guard `build_depth_grid` already applies (`source/main.c:948`) |
| D2 | `1 <= width <= 512` and `1 <= height <= 512` | `read32(0x03005DC0)`, `read32(0x03005DC4)` | same guard, `source/main.c:948`. Real bound: `width*height <= MAX_MAP_DATA_SIZE` = 10240 (`src/fieldmap.c:98`, `include/fieldmap.h:10`) — assert that too (§10) |
| D3 | `gMapHeader.mapLayout` is ROM | `p = read32(0x02037318)`, then `(p>>24)==0x08 \|\| (p>>24)==0x09` | `metatile_layer` already assumes it (`source/main.c:917`); `source/fieldtrav.c:1041` (`ft_rom_ptr`) is the canonical test — reuse it, don't retype it |

D1–D3 are the whole "is the field engine live" test. There is deliberately **no** check on
`gMain.callback2` beyond what G6 already does: the callback whitelist lives in `gamestate.c` and
duplicating it here is the exact defect class `gamestate.c:8-11` warns about.

### 1.3 Frame-level exclusions the diorama adds (cheap, all already read)

| Signal | Read | Why |
|---|---|---|
| `gPaletteFade` active | `0x02037FD4` (`pokeemerald.sym`, size 0x0C) | a fade is a screen-space effect the projection cannot honour (`RESEARCH-presenter.md` §3.6). See §4.6 for the field layout and the recommended predicate |
| BG mode != 0 or BG2/BG3 disabled | `DISPCNT` `0x04000000`, bits 0-2 and 0x0400/0x0800 | the field BGs are mode 0, BG1/2/3; `bg0_scan` already reads DISPCNT this way (`source/main.c:2161`) |
| BG0 panel rects | `d->nui` from `bg0_scan` (`source/main.c:2160`) | v1 falls back whole when a textbox is up; O5 would composite instead |

### 1.4 Profile gate

BPEE only in v1 (PHASE.md §"What bounds it" 1). The check is `p->code` == `"BPEE"` on the
profile `profile_for(topCore)` returns (`source/main.c:3624`). Every address in this document is
BPEE's; **no address here may be reused for BPRE/BPGE** — `gamestate.h:255-265` documents the exact
trap (the plausible derivation `gObjectEvents - 0x38` is right for Emerald and wrong for FRLG).

## 2. MAP LAYOUT

### 2.1 `gMapHeader` (EWRAM, static per map)

`gMapHeader` = **0x02037318**, size 0x1C (`/tmp/pret/pokeemerald.sym`: `02037318 g 0000001c gMapHeader`).
Struct: pret `include/global.fieldmap.h:171-192`.

| Off | Type | Field | Diorama use |
|---|---|---|---|
| 0x00 | `const struct MapLayout *` | `mapLayout` | §2.2 — the whole static chain |
| 0x04 | `const struct MapEvents *` | `events` | warps (already used by `fieldpath.c:161`); not needed by v1 |
| 0x08 | `const u8 *` | `mapScripts` | — |
| 0x0C | `const struct MapConnections *` | `connections` | §3 |
| 0x10 | `u16` | `music` | — |
| **0x12** | `u16` | **`mapLayoutId`** | the layout identity; §7.3 map-change signal |
| 0x14 | `u8` | `regionMapSectionId` | — |
| 0x15 | `u8` | `cave` | polish only (After v1) |
| 0x16 | `u8` | `weather` | polish only (After v1) |
| **0x17** | `u8` | **`mapType`** | §5.1 — indoor vs outdoor branch |
| 0x1A | bitfield | `allowCycling/Escaping/Running/showMapName` | — |
| 0x1B | `u8` | `battleType` | — |

`mapHeader` is already in the BPEE profile row twice: `mapHeader` and `mapHeaderPath`, both
`0x02037318` (`source/gamestate.c:41`, `:47`). **Use `mapHeaderPath`**, not `mapHeader`: the latter
doubles as the phase-14 depth-path gate (`source/gamestate.h:254-256`) and its meaning must not
drift. Both are the same value on BPEE, so this is free.

### 2.2 `struct MapLayout` (ROM, static per map)

pret `include/global.fieldmap.h:75-83`. Confirmed in-repo at `source/fieldtrav.c:1035` and used at
`source/main.c:917` and `source/fieldpath.c:118-127`.

| Off | Type | Field | Notes |
|---|---|---|---|
| 0x00 | `s32` | `width` | tiles, **playable area only** (border excluded) |
| 0x04 | `s32` | `height` | tiles |
| 0x08 | `const u16 *` | `border` | 4 entries = a 2×2 metatile block, §2.3 |
| 0x0C | `const u16 *` | `map` | `width*height` u16, row-major, **no padding** |
| 0x10 | `const struct Tileset *` | `primaryTileset` | §4 |
| 0x14 | `const struct Tileset *` | `secondaryTileset` | §4 |

There is **no layout-id field inside `MapLayout`** — the id is `gMapHeader.mapLayoutId` (+0x12) and
its mirror `SaveBlock1.mapLayoutId` (+0x32, `include/global.h:997`). pret's id space is
`include/constants/layouts.h` / `data/layouts/layouts.json`; the diorama needs the id only as an
**equality token** (cache key / change signal), never as a lookup, so neither file has to ship.

### 2.3 The border metatile (2×2)

`GetBorderBlockAt(x, y)` = `gMapHeader.mapLayout->border[((x + 1) & 1) + (((y + 1) & 1) << 1)] | MAPGRID_IMPASSABLE`
— pret `src/fieldmap.c:50`.

Read that literally:

- `border` is **4 u16 map-grid words** laid out as a 2×2 tile block, indexed
  `[(x+1 & 1) + 2*((y+1 & 1))]` — i.e. the pattern tiles **odd** grid coordinates to slot 0.
- The returned block always has `MAPGRID_IMPASSABLE` (= `MAPGRID_COLLISION_MASK` = **0x0C00**,
  `include/global.fieldmap.h:8` / `:34`) OR-ed in: **every border cell is collision 3**.
- It is only consulted **outside** `gBackupMapLayout`'s bounds (`src/fieldmap.c:54`), or when a cell
  reads `MAPGRID_UNDEFINED` (`src/fieldmap.c:336-344`).

Diorama consequence: never run the collision-neighbourhood extrusion (`RESEARCH-classification.md`
§3.4) on border cells — they are all "solid" and would ring every map in fake walls. Treat
out-of-grid and `MAPGRID_UNDEFINED` as **VOID**, exactly as the reference did
(`RESEARCH-classification.md` §6 "outside all instances → VOID / metatile 0 / collision 0"), and use
the border metatile *only* as ground art if v1 ever wants a skirt.

### 2.4 The LIVE grid: `gBackupMapLayout`

`gBackupMapLayout` = **0x03005DC0**, size 0x0C (`pokeemerald.sym`). `struct BackupMapLayout`
(`include/global.fieldmap.h:85-90`):

| Off | Type | Field | BPEE address |
|---|---|---|---|
| 0x00 | `s32` | `width` | 0x03005DC0 |
| 0x04 | `s32` | `height` | 0x03005DC4 |
| 0x08 | `u16 *` | `map` | 0x03005DC8 → **0x02032318** (`sBackupMapData`, `pokeemerald.sym`, size 0x5000 = 10240 u16) |

Already in the profile as `mapLayout` = `0x03005DC0` (`source/gamestate.c:38`) and read exactly this
way at `source/main.c:945-948`.

**Dimensions (VERIFIED-SRC, `src/fieldmap.c:95-96`):**

```
gBackupMapLayout.width  = mapLayout->width  + MAP_OFFSET_W    // + 15
gBackupMapLayout.height = mapLayout->height + MAP_OFFSET_H    // + 14
```

`MAP_OFFSET = 7`, `MAP_OFFSET_W = MAP_OFFSET*2+1 = 15`, `MAP_OFFSET_H = MAP_OFFSET*2 = 14`
(`include/fieldmap.h:18-20`; mirrored in-repo at `source/fieldpath.c:11-13`).
Note the **asymmetry**: W is 15 (7 left + map + 8 right), H is 14 (7 top + map + 7 bottom).

**The +7 rule (VERIFIED-SRC, `src/fieldmap.c:105-115`):** `InitBackupMapLayoutData` copies the ROM
layout to `dest = map + width*MAP_OFFSET + MAP_OFFSET`, so

```
grid_index(mapLocalX, mapLocalY) = (mapLocalX + 7) + backupW * (mapLocalY + 7)
```

with `mapLocalX ∈ [0, layoutW)`, `mapLocalY ∈ [0, layoutH)`.
`source/fieldpath.c:97-102` (`grid_word`) implements exactly this and is the in-repo reference.

**Confirming `build_depth_grid`'s "+7 cancels" comment (`source/main.c:951-953`).** That comment is
CORRECT, and here is the derivation it compresses:

- `ts.px/py` = `SaveBlock1.pos.x/y` (`source/gamestate.c:934-935`), and `SaveBlock1.pos` is the
  **top-left of the 15×14 visible window in _grid_ coordinates** — proven by `SaveMapView`
  (`src/fieldmap.c:396-403`) which indexes `sBackupMapData[width*i + j]` for `i = pos.y .. pos.y+13`,
  `j = pos.x .. pos.x+14`, and by `DrawWholeMapViewInternal(gSaveBlock1Ptr->pos.x, …)`
  (`src/field_camera.c:96`) whose `DrawMetatileAt` calls feed `MapGridGetMetatileIdAt` directly.
- `SaveBlock1.pos` is **simultaneously** (i) that window top-left in grid space and (ii) the
  camera-focus tile in **map-local** space — `GetCameraFocusCoords` returns `pos + MAP_OFFSET` and
  `SetCameraFocusCoords` stores `x − MAP_OFFSET` (`src/fieldmap.c:747-755`). Both readings are the
  same statement because the 15-wide window is centred on tile index 7. See §6.1 for the second
  reading and for the fact that `pos` follows the **camera**, not the avatar.
- The camera focus therefore sits at grid `(pos.x + 7, pos.y + 7)`, i.e. **screen tile column 7,
  row 7** of the 15×14 window — and, because the screen shows only 10 of those 14 rows, at
  **screen row 5** of the visible 15×10 (`source/main.c:955`'s `gy = py + r + 2` ⇒ `r = 5` when
  `gy = py + 7`).
- So `gx = px + c` is the *grid* index of visible column `c`; the map-local x is `px + c - 7`.
  The `+7` of the border and the `-7` of player-centring cancel **only in grid space** — the
  diorama must NOT reuse `gx` as a map-local coordinate.

**Corollary the diorama must not miss:** the 7-tile border ring of the live grid is **not** border
metatiles wherever a connection exists — `InitBackupMapLayoutConnections` copies the neighbour's
real rows/columns into it (`src/fieldmap.c:118-157`, §3.3). So the live grid alone already gives
7 tiles of *correct, current-tileset-indexed* neighbour terrain for free.

### 2.5 The per-cell u16 encoding

pret `include/global.fieldmap.h:7-12`:

| Field | Mask | Shift | Range | In-repo use |
|---|---|---|---|---|
| metatile id | `MAPGRID_METATILE_ID_MASK` **0x03FF** | 0 | 0..1023 | `source/main.c:961` (`e & 0x03FF`), `source/fieldpath.c:110` |
| collision | `MAPGRID_COLLISION_MASK` **0x0C00** | 10 | 0..3, 0 = passable | `source/fieldpath.c:151`, `source/main.c:969` |
| elevation | `MAPGRID_ELEVATION_MASK` **0xF000** | 12 | 0..15 | `source/fieldpath.c:155`, `source/main.c:969` |

Sentinels and constants:

| Name | Value | Meaning | Cite |
|---|---|---|---|
| `MAPGRID_UNDEFINED` | **0x03FF** | all metatile bits set, nothing else = "no data here" | `include/global.fieldmap.h:31` |
| `MAPGRID_IMPASSABLE` | **0x0C00** | GF's manual "set all collision bits" | `include/global.fieldmap.h:34` |
| `ELEVATION_TRANSITION` | 0 | compatible with anything | `include/global.fieldmap.h:16` |
| `ELEVATION_SURF` | 1 | water plane | `include/global.fieldmap.h:17` |
| `ELEVATION_DEFAULT` | 3 | ordinary ground | `include/global.fieldmap.h:18` |
| `ELEVATION_MULTI_LEVEL` | 15 | bridges | `include/global.fieldmap.h:19` |

Engine semantics the classifier must respect (all VERIFIED-SRC):

- `MapGridGetCollisionAt` returns **1** (not 0) for `MAPGRID_UNDEFINED` (`src/fieldmap.c:328-333`).
  The diorama returns **VOID** instead (PHASE.md — a hole must not become a wall).
- `MapGridGetMetatileIdAt` falls back to the *border* metatile id for `MAPGRID_UNDEFINED`
  (`src/fieldmap.c:336-344`).
- The elevation nibble is **not** height. `source/main.c:969` already skips elevation 1
  (surf water: "impassable but flat"). `RESEARCH-classification.md` §8.3.11 is binding: elevation is
  **not** geometry in v1.

## 3. NEIGHBOUR MAPS (connections)

### 3.0 The headline: 7 tiles of neighbour terrain are ALREADY in the live grid

`InitBackupMapLayoutConnections` (pret `src/fieldmap.c:118-157`) runs at every map load and copies
the **real neighbour rows/columns** into `gBackupMapLayout`'s border ring — `MAP_OFFSET` (7) deep on
N/S/W and `MAP_OFFSET + 1` (8) wide on E (`src/fieldmap.c:207`, `:243`, `:265`, `:305`). So before
any connection walking, the diorama already gets:

| Direction | Grid rows/cols filled | Source rows/cols of the neighbour | pret |
|---|---|---|---|
| NORTH | grid y `0..6` | neighbour y `cHeight-7 .. cHeight-1` | `FillNorthConnection`, `src/fieldmap.c:211-243` |
| SOUTH | grid y `mapH+7 .. mapH+13` | neighbour y `0..6` | `FillSouthConnection`, `:177-207` |
| WEST | grid x `0..6` | neighbour x `cWidth-7 .. cWidth-1` | `FillWestConnection`, `:245-289` |
| EAST | grid x `mapW+7 .. mapW+14` (8 wide) | neighbour x `0..7` | `FillEastConnection`, `:291-305` |

In all four the lateral placement is `offset + MAP_OFFSET` in grid space, clipped to
`[0, gBackupMapLayout.width/height)` (`src/fieldmap.c:186-205` and twins).

**Consequence for v1:** if the diorama simply classifies the whole `backupW × backupH` grid instead
of only the playable `layoutW × layoutH` rectangle, it gets a free 7-tile skirt of *real neighbour
terrain* that (a) kills the §3.4 edge artifact (`RESEARCH-classification.md` §6: "without
connections loaded, edge tiles silently become ROOF/LOW"), and (b) needs **zero** extra reads.
The catch is texturing: those cells' metatile ids index the **current** map's tilesets, not the
neighbour's — the same limitation pret itself has (`source/fieldpath.c:105-107` already says so).
That is *exactly* PHASE.md bound 5: same-tileset neighbours texture correctly, different-tileset
neighbours must be drawn untextured. Since the copy is by raw id, "same tileset pair" is the only
condition under which the skirt is visually right — see §3.4.

Full instancing beyond 7 tiles (the `RESEARCH-classification.md` §6 model) is still needed for a
town-scale diorama; §3.1-§3.3 pin it.

### 3.1 `MapHeader.connections` → `MapConnections` → `MapConnection[]`

`gMapHeader + 0x0C` → `const struct MapConnections *` (ROM; may be **NULL** — check before use,
pret marks the null deref as UB at `src/fieldmap.c:641`).

`struct MapConnections` (`include/global.fieldmap.h:165-169`):

| Off | Type | Field |
|---|---|---|
| 0x00 | `s32` | `count` |
| 0x04 | `const struct MapConnection *` | `connections` |

size **8** — VERIFIED-SYM: `LittlerootTown_MapConnections` 0x0848660C, next symbol
`OldaleTown_MapConnectionsList` 0x08486614 (`pokeemerald.sym`).

`struct MapConnection` (`include/global.fieldmap.h:157-163`):

| Off | Type | Field | Note |
|---|---|---|---|
| 0x00 | `u8` | `direction` | 1..6, §3.2 |
| 0x01 | — | (3 bytes padding) | forced by the `s32` that follows |
| 0x04 | `s32` | `offset` | **signed**, lateral tile offset |
| 0x08 | `u8` | `mapGroup` | |
| 0x09 | `u8` | `mapNum` | |
| 0x0A | — | (2 bytes padding) | |

**stride = 0x0C (12) — VERIFIED-SYM**, not derived: `LittlerootTown_MapConnectionsList` 0x08486600
→ `LittlerootTown_MapConnections` 0x0848660C = 12 bytes for Littleroot's **1** connection;
`OldaleTown_MapConnectionsList` 0x08486614 → `OldaleTown_MapConnections` 0x08486638 = 0x24 = **3**×12;
Dewford/Fallarbor/Verdanturf/Pacifidlog/Petalburg all 0x18 = 2×12 (`pokeemerald.sym`).

### 3.2 Direction constants

pret `include/constants/global.h:147-154`:

| Name | Value | Diorama |
|---|---|---|
| `CONNECTION_INVALID` | −1 | — |
| `CONNECTION_NONE` | 0 | — |
| `CONNECTION_SOUTH` | **1** | instance below |
| `CONNECTION_NORTH` | **2** | instance above |
| `CONNECTION_WEST` | **3** | instance left |
| `CONNECTION_EAST` | **4** | instance right |
| `CONNECTION_DIVE` | **5** | **SKIP** — a vertical world switch, not a spatial neighbour |
| `CONNECTION_EMERGE` | **6** | **SKIP** — same |

pret itself skips 5/6 when looking for a spatial neighbour (`GetMapConnectionAtPos`,
`src/fieldmap.c:729-731`), and `InitBackupMapLayoutConnections`'s switch has no case for them
(`src/fieldmap.c:137-155`). This matches `RESEARCH-classification.md` §6 exactly.

### 3.3 mapGroup/mapNum → `MapHeader`, and the ROM grid

pret: `Overworld_GetMapHeaderByGroupAndId(g, n) { return gMapGroups[g][n]; }`
(`src/overworld.c:579-582`), with `extern const struct MapHeader *const *const gMapGroups[]`
(`src/overworld.c:96`).

**`gMapGroups` = 0x08486578** (`pokeemerald.sym`), already in the BPEE profile as
`mapGroupsRom` (`source/gamestate.c:125`; `mapGroupsRomAlt = 0` — Emerald ships one revision).
Group count: `gMapGroups` spans 0x08486578 → 0x08486600 (`LittlerootTown_MapConnectionsList`,
the next symbol) = 0x88 = **34 pointers** = pret's `MAP_GROUPS_COUNT` for Emerald.

**The repo already implements the whole walk**: `fieldtrav_rom_map()`
(`source/fieldtrav.c:1061-1085`) does exactly

```
grp = read32(mapGroupsRom + 4*group)         // must be ROM
hdr = read32(grp + 4*num)                    // must be ROM
lay = read32(hdr + 0x00); ev = read32(hdr + 0x04)
w   = (s32)read32(lay + 0x00); h = (s32)read32(lay + 0x04); grid = read32(lay + 0x0C)
```

with the guards `group ∈ [0,63]`, `num ∈ [0,255]`, `1 <= w,h <= 256`, and `ft_rom_ptr` on every
pointer (`source/fieldtrav.c:1041`: `(a>>24)==0x08 || ==0x09`). **The diorama must reuse this
function, not re-derive it** — it is host-tested and already shipped. It returns
`FtRomMap { header, layout, events, grid, w, h }`.

For a neighbour's **tilesets** (needed to decide "same atlas or not", §3.4) read
`lay + 0x10` (primary) and `lay + 0x14` (secondary) with the same ROM guard.

Neighbour grid indexing is **unpadded**: `neighbourGrid + 2*(x + w*y)` for `x ∈ [0,w)`,
`y ∈ [0,h)` — `FillConnection` uses `&map[mapWidth * y2 + x2]` (`src/fieldmap.c:166`), and
`source/fieldtrav.c:1105` (`ft_rb_read16`) is the in-repo implementation. Off-grid = VOID.

### 3.4 Origin formulas, re-derived from pret's own fill math

`RESEARCH-classification.md` §6 gives origins; here they are re-derived from
`FillSouth/North/West/EastConnection` so they are ours, not inherited.

Work in **map-local coordinates of the current map** (origin = current map's tile (0,0)).
Each fill writes to grid cell `(gx, gy)`; map-local = grid − 7. Let `cW`/`cH` be the neighbour's
layout width/height and `off` = `connection.offset`.

| Dir | pret writes at grid | source cell of neighbour | ⇒ map-local origin of neighbour = (localX − nx, localY − ny) | pret cite |
|---|---|---|---|---|
| SOUTH | `x = off+7`, `y = mapH+7` | `(x2=0, y2=0)` | **(off, mapH)** | `src/fieldmap.c:190,207` |
| NORTH | `x = off+7`, `y = 0` | `(x2=0, y2=cH−7)` | **(off, −cH)** | `:222,243` |
| WEST | `x = 0`, `y = off+7` | `(x2=cW−7, y2=0)` | **(−cW, off)** | `:255,289` |
| EAST | `x = mapW+7`, `y = off+7` | `(x2=0, y2=0)` | **(mapW, off)** | `:294,305` |

Worked check (SOUTH): grid `(off+7, mapH+7)` ⇒ map-local `(off, mapH)`; that cell holds neighbour
`(0,0)`; so neighbour `(nx,ny)` sits at map-local `(off+nx, mapH+ny)` ⇒ origin `(off, mapH)`. ✔
Worked check (NORTH): grid `(off+7, 0)` ⇒ map-local `(off, −7)`; that cell holds neighbour
`(0, cH−7)`; so neighbour `(nx, ny)` sits at `(off+nx, −7 + ny−(cH−7))` = `(off+nx, ny−cH)`
⇒ origin `(off, −cH)`. ✔

These are identical to `RESEARCH-classification.md` §6 — **confirmed, not assumed**.

Clipping note (`FillSouthConnection`, `src/fieldmap.c:186-205`): when `off+7 < 0` pret clips the
copy and advances `x2`. The diorama does **not** clip — it instances the whole neighbour and lets
the world-space accessor decide — so it must not copy pret's clipping arithmetic.

### 3.5 Same-atlas test (PHASE.md bound 5)

A neighbour instance may be textured **iff**
`neighbourLayout->primaryTileset == currentLayout->primaryTileset` **and**
`neighbourLayout->secondaryTileset == currentLayout->secondaryTileset` (pointer equality on the
two ROM pointers at `lay+0x10` / `lay+0x14`). Anything else ⇒ untextured dark ground; **never**
sample the current atlas with the neighbour's ids (that is the reference's own acknowledged bug,
`RESEARCH-classification.md` §5.2.6 / §8.3.8).

Primary-only equality is *not* enough: metatile ids ≥ 512 resolve through the secondary
(`GetMetatileAttributesById`, `src/fieldmap.c:373-388`), so a shared primary with a different
secondary still mis-textures every id ≥ 512.

## 4. TILESETS

### 4.1 `struct Tileset`

pret `include/global.fieldmap.h:64-73`. **size 0x18 = 24 — VERIFIED-SYM**: every
`gTileset_*` symbol in `pokeemerald.sym` has size `00000018` (e.g. `083df884 g 00000018
gTileset_Building`, `083dfb6c g 00000018 gTileset_GenericBuilding`).

| Off | Type | Field | Diorama use |
|---|---|---|---|
| 0x00 | `bool8` | `isCompressed` | only matters for the ROM-decompression path (After v1); the VRAM route (§4.3) needs it never |
| 0x01 | `bool8` | `isSecondary` | `LoadTilesetPalette` branches on it (`src/fieldmap.c:836-853`) — informational |
| 0x04 | `const u32 *` | `tiles` | ROM 4bpp tile graphics (LZ77 when `isCompressed`) |
| 0x08 | `const u16 (*)[16]` | `palettes` | ROM palettes, 16 colours each |
| 0x0C | `const u16 *` | `metatiles` | **8 u16 per metatile**, §4.4 |
| 0x10 | `const u16 *` | `metatileAttributes` | **u16 per metatile** in Emerald, §4.5 |
| 0x14 | `TilesetCB` | `callback` | animation callback — do NOT call, do NOT need |

⚠️ FRLG differs (attributes are `u32` at +0x14 and the callback at +0x10) — out of scope, but the
repo already encodes the split at `source/fieldpath.c:122-127`; keep the BPEE branch.

### 4.2 Split constants

pret `include/fieldmap.h:4-12`:

| Name | Value |
|---|---|
| `NUM_TILES_IN_PRIMARY` | **512** |
| `NUM_TILES_TOTAL` | **1024** |
| `NUM_METATILES_IN_PRIMARY` | **512** |
| `NUM_METATILES_TOTAL` | **1024** |
| `NUM_PALS_IN_PRIMARY` | **6** |
| `NUM_PALS_TOTAL` | **13** |
| `NUM_TILES_PER_METATILE` | **8** |
| `MAX_MAP_DATA_SIZE` | **10240** |
| `METATILE_ROW_WIDTH` | **8** (`include/global.fieldmap.h:60`) — metatiles are authored in rows of 8, so a 2×2 object is `n, n+1, n+8, n+9`; useful for recognising multi-tile furniture |

Metatile resolution (`GetMetatileAttributesById`, `src/fieldmap.c:373-388`; `DrawMetatileAt`,
`src/field_camera.c:226-243`):

```
id  < 512  -> primaryTileset,   index = id
id  < 1024 -> secondaryTileset, index = id - 512
id >= 1024 -> invalid (MB_INVALID)
```

Already implemented at `source/main.c:930-932` and `source/fieldpath.c:120-124`.

Size sanity (VERIFIED-SYM): `gMetatiles_General` size 0x2000 = 512 × 16 B = 512 metatiles × 8 u16 ✔;
`gMetatileAttributes_General` size 0x400 = 512 × u16 ✔. Secondary tilesets are **not** all 512
long: `gMetatileAttributes_Shop` is 0x242 (289 entries) while `gMetatileAttributes_GenericBuilding`
is 0x400 (512). ⇒ **never read past a secondary's table without a guard**; a map only ever
references ids its own tilesets define, so in practice the guard is "the read must stay in ROM".

### 4.3 Where the tiles LIVE IN VRAM (the whole point: no decompression)

The overworld BG templates (`sOverworldBgTemplates`, pret `src/overworld.c:266-303`):

| BG | `charBaseIndex` | `mapBaseIndex` | priority | role |
|---|---|---|---|---|
| 0 | **2** | 31 | 0 | text/window layer (what `bg0_scan` reads, `source/main.c:2160`) |
| 1 | **0** | 29 | 1 | metatile **top** layer |
| 2 | **0** | 28 | 2 | metatile middle layer |
| 3 | **0** | 30 | 3 | metatile bottom layer |

Tileset upload (`src/fieldmap.c:798-807`, `:856-869`):

```
CopyPrimaryTilesetToVram   -> CopyTilesetToVram(primary,   512, offset=0)
CopySecondaryTilesetToVram -> CopyTilesetToVram(secondary, 512, offset=512)
CopyTilesetToVram(...)     -> LoadBgTiles(2, tiles, numTiles*32, offset)
```

`LoadBgTiles(bg, src, size, destOffset)` converts `destOffset` **tiles → bytes**:
`tileOffset = (baseTile + destOffset) * 0x20` for 4bpp (`src/bg.c:380-387`), then
`LoadBgVram` adds `charBaseIndex * BG_CHAR_SIZE + BG_VRAM` (`src/bg.c:184-186`). BG2 has
`charBaseIndex = 0` and `baseTile = 0`, so:

> **Global tile `T` (0..1023) is at GBA VRAM `0x06000000 + 32*T`.**
> Primary tiles 0..511 → 0x06000000..0x06003FFF; secondary tiles → 0x06004000..0x06007FFF.
> 4bpp, 32 bytes/tile, standard GBA planar-nibble order (low nibble = LEFT pixel — the same
> convention `pspr_tile_index` already documents, `source/peersprite.h:250-252`).

That covers the **current** map's atlas with no LZ77 decoder at all. A neighbour with different
tilesets is *not* in VRAM — hence PHASE.md bound 5.

Region check: OBJ character VRAM starts at 0x06010000 (`source/peersprite.h:82`), so the BG range
0x06000000..0x06007FFF never collides with the sprite decoder's reads.

### 4.4 The metatile struct (8 u16)

`DrawMetatileAt`/`DrawMetatile` (pret `src/field_camera.c:226-311`) is the ground truth:

```
tiles = tileset->metatiles + metatileIndex * NUM_TILES_PER_METATILE   // 8 u16 = 16 bytes
tiles[0..3] = BOTTOM layer quadrants: TL, TR, BL, BR
tiles[4..7] = TOP    layer quadrants: TL, TR, BL, BR
```

(pret writes `offset`, `offset+1`, `offset+0x20`, `offset+0x21` into a 32-wide tilemap — that is
TL, TR, BL, BR.)

Each u16 is a standard GBA **BG text-mode tilemap entry**:

| Bits | Field |
|---|---|
| 0-9 | tile id (0..1023) → VRAM `0x06000000 + 32*id` (§4.3) |
| 10 | H flip |
| 11 | V flip |
| 12-15 | BG palette slot (0..15) |

Compositing rule (from the three `DrawMetatile` cases, `src/field_camera.c:248-308`): the metatile
is always **bottom layer first, top layer over it**, colour index 0 transparent. The *layer type*
only decides which hardware BG each half is written to (and in `METATILE_LAYER_TYPE_NORMAL` the
bottom BG gets a garbage constant `0x3014`, `src/field_camera.c:290-293` — proof that layer type is
a compositing target, **not** a z-order the atlas must honour). So the atlas composer flattens
`tiles[0..3]` then `tiles[4..7]` and is done — matching `RESEARCH-classification.md` §5.1.

A metatile is 16×16 px = 2×2 tiles of 8×8.

### 4.5 The metatile ATTRIBUTE u16 (Emerald)

pret `include/global.fieldmap.h:36-47`:

| Field | Mask | Shift | Notes |
|---|---|---|---|
| behavior | `METATILE_ATTR_BEHAVIOR_MASK` **0x00FF** | 0 | §5.2 |
| (unused) | 0x0F00 | 8 | 4 bits, always 0 in vanilla data |
| layer type | `METATILE_ATTR_LAYER_MASK` **0xF000** | 12 | 0/1/2 only |

Layer types (`include/global.fieldmap.h:49-53`):

| Value | Name | Halves drawn to |
|---|---|---|
| 0 | `METATILE_LAYER_TYPE_NORMAL` | middle (BG2) + top (BG1) — "covers sprites" |
| 1 | `METATILE_LAYER_TYPE_COVERED` | bottom (BG3) + middle (BG2) — sprites draw over it |
| 2 | `METATILE_LAYER_TYPE_SPLIT` | bottom (BG3) + top (BG1) — the sprite is sandwiched |

`source/main.c:918-935` (`metatile_layer`) already reads exactly this chain and caches it per
layout with a 1024-entry memo; the diorama should **reuse that shape** (per-map rebuild keyed on the
`MapLayout` pointer) rather than adding a second cache.

FR/LG note (out of scope, stated so nobody generalises): attributes are `u32` at `Tileset+0x14`
with a 9-bit behavior mask 0x1FF — `source/fieldpath.c:122-131` carries the split.

### 4.6 Palette source for the atlas (settles PHASE.md O4)

Where the field palettes go (`LoadTilesetPalette`, pret `src/fieldmap.c:831-853`;
`LoadPrimaryTilesetPalette`/`LoadSecondaryTilesetPalette`, `:871-879`):

| BG palette slots | Content |
|---|---|
| 0..5 | primary tileset's 6 palettes — but **BG colour index 0 is force-written to `RGB_BLACK`** and the primary's own colours are loaded from `palettes[0]+1` into slot 0 index 1 onward (`src/fieldmap.c:838-840`) |
| 6..12 | secondary tileset's 7 palettes (`palettes[NUM_PALS_IN_PRIMARY]`, `src/fieldmap.c:844`) |
| 13..15 | not field tilesets (text/UI, weather) |

Three candidate sources:

| Source | Address | Carries | Verdict |
|---|---|---|---|
| Hardware BG PALRAM | `0x05000000 + 32*slot + 2*idx` | fades, weather tint, flash | fallback only |
| `gPlttBufferFaded` | `0x02037B14` (`pokeemerald.sym`, 0x400 B) | same as PALRAM (it is the source of the DMA) | redundant |
| **`gPlttBufferUnfaded`** | **`0x02037714`** (`pokeemerald.sym`, 0x400 B) | authored colours, no fade | **RECOMMENDED** |

**Recommendation (O4): build the atlas from `gPlttBufferUnfaded`.** Layout is
`u16[512]` = **BG banks 0-15 first, then OBJ banks 0-15**, so BG slot `p`, colour `c` is at
`0x02037714 + 32*p + 2*c` (the OBJ half starts at +512 bytes — `source/gamestate.h:307-308` states
the same convention for the sprite path, and `pspr_gather_palette` already reads it that way,
`source/peersprite.h:281`).

Reasons: (1) it is already a profile column (`plttUnfaded`, `source/gamestate.c:57`) so no new
address ships; (2) the atlas is rebuilt **per map**, not per frame — baking a mid-fade PALRAM
snapshot into it would freeze that fade for the whole map; (3) the same argument the sprite path
already made and shipped (`source/gamestate.h:298-300`: the faded buffer "carries screen fades").

**How a fade/weather tint should read in the diorama (the answer, in order):**

1. **v1: it does not.** The gate (§1.3) shuts on `gPaletteFade` activity, so the flat frame
   (which *is* the faded image) plays the fade. `gPaletteFade` = `0x02037FD4`, 12 bytes; the
   usable predicate is a non-zero **`active`** flag — its exact bit offset is **NOT verified
   here** (`struct PaletteFadeControl` is a bitfield union in `include/palette.h`, which this
   spec did not read), so the implementer must either read that header or, cheaper and safer,
   compare `gPlttBufferFaded` against `gPlttBufferUnfaded` over the 13 field slots (26 `u32`
   reads) and treat "differs" as "a fade/tint is on". **Flagged as the one open item in §10.**
2. **After v1:** keep the unfaded atlas and apply the fade as a **uniform vertex/fragment tint**
   computed from that same comparison — one colour multiply, no atlas rebuild. Weather (fog,
   darkness) is the same lever.

BGR555 → RGBA8 conversion already exists and is host-tested: `pspr_bgr555_to_rgba8`
(`source/peersprite.h:248`), with index 0 = transparent (`source/peersprite.h:261-266`). For the
atlas, layer 0's index-0 texels must become **opaque backdrop**, not transparent, or the ground
will have holes — see `RESEARCH-classification.md` §5.1 (layer-0 colour 0 = the primary tileset's
backdrop, i.e. BG palette slot 0 index 0, which pret forces to `RGB_BLACK`, `src/fieldmap.c:838`).

## 5. MAP TYPE + INDOOR TABLE + behavior constants

### 5.1 `MapHeader.mapType`

`gMapHeader + 0x17`, `u8` (pret `include/global.fieldmap.h:187`). Constants
(`include/constants/map_types.h:4-13`):

| Value | Name | Diorama branch |
|---|---|---|
| 0 | `MAP_TYPE_NONE` | outdoor (fail-safe) |
| 1 | `MAP_TYPE_TOWN` | outdoor |
| 2 | `MAP_TYPE_CITY` | outdoor |
| 3 | `MAP_TYPE_ROUTE` | outdoor |
| 4 | `MAP_TYPE_UNDERGROUND` | outdoor (caves mass fine under the neighbourhood rule) |
| 5 | `MAP_TYPE_UNDERWATER` | outdoor |
| 6 | `MAP_TYPE_OCEAN_ROUTE` | outdoor |
| 7 | `MAP_TYPE_UNKNOWN` | outdoor (unused by any map) |
| **8** | **`MAP_TYPE_INDOOR`** | **indoor branch** |
| **9** | **`MAP_TYPE_SECRET_BASE`** | **indoor branch** |

`RESEARCH-classification.md` §3.2 uses exactly this split (indoor ∪ secret base vs everything else).

### 5.2 THE INDOOR ID TABLE IS WRONG — do not port it

**Verdict: `RESEARCH-classification.md` §3.3's literal metatile-id list must NOT be
implemented as written.** Two independent defects, both verified below.

#### 5.2.1 Defect A — the ids are not globally meaningful; they are per-*secondary-tileset*

Indoor maps in Emerald use primary `gTileset_Building` and one of ~20 secondaries
(`data/layouts/layouts.json`, fetched 2026-09-10: 38 layouts are
`gTileset_Building` + `gTileset_GenericBuilding`, plus `gTileset_BrendansMaysHouse`,
`gTileset_PokemonCenter`, `gTileset_Shop`, `gTileset_Lab`, `gTileset_Contest`, …).
`gTileset_Building` itself has only **8 metatiles**
(`data/tilesets/primary/building/metatiles.bin` = 128 bytes = 8 × 16; attributes = 16 bytes,
values `00 00 86 86 83 83 65 65` = NORMAL, NORMAL, TV, TV, PC, PC, SOUTH_ARROW_WARP ×2 —
matching `METATILE_Building_TV_Off 0x002` / `PC_Off 0x004`, `include/constants/metatile_labels.h:115-118`).
So essentially **every** indoor metatile id is ≥ 512 and resolves through *whichever secondary is
loaded*.

The same numeric id therefore means different things in different rooms. Decoded from pret's own
`metatile_attributes.bin` files (fetched 2026-09-10):

| id | in `GenericBuilding` | in `PokemonCenter` | in `Shop` | in `BrendansMaysHouse` |
|---|---|---|---|---|
| 514 | MB_NORMAL | MB_NORMAL | MB_NORMAL | **MB_SOUTH_ARROW_WARP** |
| 517 | MB_NORMAL | **MB_COUNTER** | MB_NORMAL | MB_NORMAL |
| 533/534 | MB_NORMAL | **MB_REGION_MAP** | 534 = **MB_SOUTH_ARROW_WARP** | MB_NORMAL |
| 558 | MB_NORMAL (never used) | **MB_POKEMON_CENTER_BOOKSHELF** | MB_NORMAL | MB_NORMAL |
| 570 | MB_NORMAL | MB_NORMAL | **MB_COUNTER** | out of range |
| 575 | MB_NORMAL | **MB_NON_ANIMATED_DOOR** | MB_NORMAL | MB_NORMAL |
| 576/577 | MB_NORMAL | MB_NORMAL | **MB_COUNTER** ×2 | out of range |
| 589 | MB_NORMAL | MB_DEEP_SAND (sic) | MB_NORMAL | MB_NORMAL |

A rule "id 517 → DECAL" is a floor mat in one room, a shop counter in another.

#### 5.2.2 Defect B — even for `gTileset_GenericBuilding` the claims are mostly wrong

Method (fully reproducible): decode `data/tilesets/secondary/generic_building/metatile_attributes.bin`
(1024 B = 512 entries covering ids 512..1023) for behaviour + layer type, then histogram every
`(id, collision, elevation)` over **all 38** `Building`+`GenericBuilding` layouts' `map.bin`
blockdata (pret `data/layouts/*/map.bin`), then read the 8-stride tileset structure
(`METATILE_ROW_WIDTH 8`, `include/global.fieldmap.h:60`) around each hit.

| Research id(s) | Research claim | Behaviour in GenericBuilding | Uses across 38 maps (collision, elevation) | **What it actually is** | Verdict |
|---|---|---|---|---|---|
| 622 | VOID — "black filler outside room bounds" | MB_NORMAL, layer COVERED | 3 × (1, 0) | **top half of a BOOKSHELF**: always `622,623` directly above `630,631`, and 630/631 **are** `MB_BOOKSHELF` (0xE1) | **WRONG — dangerous.** VOID would delete the top of every bookshelf |
| 570 | FURNITURE — "TV" | MB_NORMAL | 1 × (0, 3) | a passable floor tile in `RustboroCity_Flat1_2F` | **WRONG.** The GenericBuilding TVs are 542 and 552 (`MB_TELEVISION` 0x86); the *generic* TV/PC are primary ids 2-5 |
| 533, 534 | WALL — "bookshelf/shop shelf" | MB_NORMAL, layer COVERED | 37 × (1,0) / 3 × (1,0) | **top half of a wall unit** whose bottom pair is `541, 542` — and 542 is `MB_TELEVISION` | Outcome (WALL) accidentally OK; **reason wrong**, and the neighbourhood rule already yields WALL for an impassable row-0 tile with an impassable tile below |
| 576, 577, 584, 585, 586 | TABLE | all MB_NORMAL | 576: 3×(0,3) · 577: **0 uses** · 584: 7×(1,·) · 585: 5×(1,·) · 586: 1×(1,0) | **584/585 (+592/593) is a real 2×2 impassable table/desk** ✔; **576 is a passable floor tile**, 577 is unused | **Half wrong.** Keep 584/585/586, drop 576/577 |
| 565, 558, 566 | FURNITURE — "chairs" | MB_NORMAL, layer COVERED | 565: 5×(0,3) · 558: **0 uses** · 566: 20×(0,3) | **565/566/567 + 573/574/575 + 581/582/583 is a 3×3 nine-slice RUG** (all passable, elevation 3, drawn *under* the player) | **WRONG.** A rug is a DECAL, never a chair |
| 578 | SIGN | MB_NORMAL | **0 uses** | unused in every GenericBuilding map | **WRONG / dead** |
| 589 | WALL — "indoor stairwell" | MB_NORMAL | 3 × (1, 0) | **top-right of the 2×2 BED** `588,589 / 596,597` (HouseWithBed, LilycoveCity_PokemonTrainerFanClub) | **WRONG.** GenericBuilding's stairs are `METATILE_GenericBuilding_TrickHouse_Stairs_Down 0x219 = 537` (`include/constants/metatile_labels.h:250`) |
| 514, 515, 516, 517 | DECAL — "floor mats" | MB_NORMAL | 514/515/516: 1 use each (MossdeepCity_GameCorner_B1F) · **517: 75 uses, all (1, 0)** | **the UPPER BACK-WALL row** — 517 is *the* generic upper wall tile, sitting above 525 (its lower half) in every house | **WRONG — dangerous.** DECAL would flatten every back wall |
| 567, 568, 575 | BED | MB_NORMAL | 567: 5×(0,3) · 568: 3×(0,3) · 575: 9×(0,·) | 567/575 are rug cells (see 565/566); 568 is a floor-shadow tile under the back wall | **WRONG.** The real bed is 588/589/596/597 |

pret's own `metatile_labels.h` cannot arbitrate most of this: it labels only script-referenced
metatiles (~150 in the whole game), and its entire `gTileset_GenericBuilding` block is **three**
entries — `TableEdge 0x2F1 (753)`, `TrickHouse_Door_Closed 0x21B (539)`,
`TrickHouse_Stairs_Down 0x219 (537)` (`include/constants/metatile_labels.h:247-250`). **Every id in
the research table is UNLABELED in pret.** That is why the verification above uses the attribute
files + the shipped blockdata instead — those are ground truth, not naming.

#### 5.2.3 What to implement instead

1. **v1: no id table at all.** The indoor branch runs behaviour predicates (§5.3) + the
   collision-neighbourhood rule (`RESEARCH-classification.md` §3.4) + the alcove rule. The
   evidence above shows the neighbourhood rule already gets 517/525 (back wall), 533/534 (wall
   unit), 584/585 (table) and 588/589 (bed) right *as massing*, because they are simply impassable
   tiles in the right vertical arrangement.
2. **The one genuinely useful indoor signal that is NOT an id:** in these tilesets, wall-plane
   furniture is authored at **elevation 0** while floor furniture is at **elevation 3**
   (bookshelf tops 622/623 = (1,0); back wall 517 = (1,0); bed 588/589/596/597 = (1,0);
   rug = (0,3); floor = (0,3)). Elevation 0 is `ELEVATION_TRANSITION`
   (`include/global.fieldmap.h:16`) — "compatible with anything" — which in an indoor room is how
   GF marks *the wall band*. Use it as a hint, never as geometry (`RESEARCH-classification.md`
   §8.3.11).
3. **If an id table is ever added, it MUST be keyed on the secondary-tileset POINTER**
   (`MapLayout+0x14`, a ROM address that is stable per build), never on the id alone, and it must
   be one-id-one-entry with a build-time uniqueness assert (`RESEARCH-classification.md` §8.3.3).
   A verified starter set for `gTileset_GenericBuilding` only, from §5.2.2:
   `588,589,596,597 = BED` · `584,585,592,593 = TABLE` · `565,566,567,573,574,575,581,582,583 = DECAL (rug)` ·
   `622,623 = the top of a bookshelf (WALL)`.

### 5.3 Behaviour constants the classifier needs (Emerald, exact values)

Enumerated from `include/constants/metatile_behaviors.h` (a plain `enum`, so the value is the
ordinal; `NUM_METATILE_BEHAVIORS` = 0xF0, `MB_INVALID` = 0xFF).

**Surfable water — the authoritative set is pret's `sTileBitAttributes` `TILE_FLAG_SURFABLE`
(`src/metatile_behavior.c:6, 9-…`), read by `MetatileBehavior_IsSurfableWaterOrUnderwater`
(`src/metatile_behavior.c:280-286`). All 16 members:**

| Value | Name |
|---|---|
| 0x10 | `MB_POND_WATER` |
| 0x11 | `MB_INTERIOR_DEEP_WATER` |
| 0x12 | `MB_DEEP_WATER` |
| 0x13 | `MB_WATERFALL` |
| 0x14 | `MB_SOOTOPOLIS_DEEP_WATER` |
| 0x15 | `MB_OCEAN_WATER` |
| 0x19 | `MB_NO_SURFACING` |
| 0x22 | `MB_SEAWEED` |
| 0x2A | `MB_SEAWEED_NO_SURFACING` |
| 0x50 | `MB_EASTWARD_CURRENT` |
| 0x51 | `MB_WESTWARD_CURRENT` |
| 0x52 | `MB_NORTHWARD_CURRENT` |
| 0x53 | `MB_SOUTHWARD_CURRENT` |
| 0x6C | `MB_WATER_DOOR` |
| 0x6D | `MB_WATER_SOUTH_ARROW_WARP` |
| 0x6F | `MB_UNUSED_6F` |

Near-water but **NOT** surfable (do not add them, they are walkable ground):
`MB_PUDDLE` 0x16, `MB_SHALLOW_WATER` 0x17.

**Grass:**

| Value | Name | Note |
|---|---|---|
| 0x02 | `MB_TALL_GRASS` | `MetatileBehavior_IsTallGrass`, `src/metatile_behavior.c:729-735` |
| 0x03 | `MB_LONG_GRASS` | `:737-743` |
| 0x09 | `MB_LONG_GRASS_SOUTH_EDGE` | the bottom edge tile of a long-grass patch |
| 0x07 | `MB_SHORT_GRASS` | decorative, walkable |
| 0x24 | `MB_ASHGRASS` | Route 113 ash |

All are **passable** — `RESEARCH-classification.md` §8.3.13 is binding: never give grass
collision-derived volume.

**The four jump ledges** (`MetatileBehavior_IsJumpEast/West/North/South`,
`src/metatile_behavior.c:143-174`):

| Value | Name |
|---|---|
| 0x38 | `MB_JUMP_EAST` |
| 0x39 | `MB_JUMP_WEST` |
| 0x3A | `MB_JUMP_NORTH` |
| 0x3B | `MB_JUMP_SOUTH` |

(The four diagonals `MB_JUMP_NORTHEAST` 0x3C … `MB_JUMP_SOUTHWEST` 0x3F exist but have no
`IsJump*` predicate; treat them as LEDGE too or ignore — they are rare.)

**Doors / warps that must stay in the facade plane:**

| Value | Name | pret predicate |
|---|---|---|
| 0x69 | `MB_ANIMATED_DOOR` | `IsWarpDoor` / `IsDoor`, `src/metatile_behavior.c:220-235` |
| 0x8D | `MB_PETALBURG_GYM_DOOR` | `IsDoor`, `:229-230` |
| 0x60 | `MB_NON_ANIMATED_DOOR` | `IsNonAnimDoor`, `:262-269` |
| 0x6C | `MB_WATER_DOOR` | `IsNonAnimDoor` (also surfable — water wins) |
| 0x6E | `MB_DEEP_SOUTH_WARP` | `IsNonAnimDoor` |
| 0x65 | `MB_SOUTH_ARROW_WARP` | the building **exit mat** — passable, must stay flat |
| 0x64 / 0x62 / 0x63 | `MB_NORTH/EAST/WEST_ARROW_WARP` | passable arrow warps |
| 0x61 | `MB_LADDER` | |
| 0x6A / 0x6B | `MB_UP_ESCALATOR` / `MB_DOWN_ESCALATOR` | |

`source/fieldpath.c:27-53` already carries this exact RSE table (with the warp-kind semantics) and
is host-tested — **reuse `fieldpath_kind_for_behaviour()` rather than retyping the numbers**.

**Interactive furniture:**

| Value | Name | pret predicate |
|---|---|---|
| 0x80 | `MB_COUNTER` | `MetatileBehavior_IsCounter`, `src/metatile_behavior.c:473-479` |
| 0x83 | `MB_PC` | `MetatileBehavior_IsPC`, `:491-497` |
| 0x86 | `MB_TELEVISION` | `MetatileBehavior_IsPlayerFacingTVScreen`, `:481-489` |
| 0xC5 | `MB_PLAYER_ROOM_PC_ON` | the player's own bedroom PC while lit |

**The four bookshelf/shelf behaviours** (`RESEARCH-classification.md` §3.3 rule 3's real set):

| Value | Name | pret predicate |
|---|---|---|
| 0xE0 | `MB_PICTURE_BOOK_SHELF` | `IsPictureBookShelf`, `src/metatile_behavior.c:1288-1294` |
| 0xE1 | `MB_BOOKSHELF` | `IsBookShelf`, `:1296-1302` |
| 0xE2 | `MB_POKEMON_CENTER_BOOKSHELF` | `IsPokeCenterBookShelf`, `:1304` |
| 0xE5 | `MB_SHOP_SHELF` | `IsShopShelf`, `:1328-1334` |

(Adjacent and worth the same treatment if wanted: `MB_VASE` 0xE3, `MB_TRASH_CAN` 0xE4,
`MB_BLUEPRINT` 0xE6.)

**"Impassable but flat" cases worth an explicit rule** — these are collision ≠ 0 tiles that must
**not** extrude, or the map grows walls where the art is floor:

| Value | Name | Why |
|---|---|---|
| 0x30-0x37 | `MB_IMPASSABLE_EAST/WEST/NORTH/SOUTH/NORTHEAST/NORTHWEST/SOUTHEAST/SOUTHWEST` | directional blockers on otherwise ordinary ground (cliff edges, rails) |
| 0xC0 | `MB_IMPASSABLE_SOUTH_AND_NORTH` | ditto |
| 0xC1 | `MB_IMPASSABLE_WEST_AND_EAST` | ditto |
| 0xD2 | `MB_CRACKED_FLOOR` | Sky Pillar floor that collapses — **flat floor art**, must stay FLAT |
| 0x66 | `MB_CRACKED_FLOOR_HOLE` | the hole itself; flat |
| 0xD0 / 0xD1 | `MB_MUDDY_SLOPE` / `MB_BUMPY_SLOPE` | slopes; flat ground art |
| 0xD3-0xD6 | `MB_ISOLATED_VERTICAL_RAIL` … `MB_HORIZONTAL_RAIL` | thin rails on flat ground |
| 0x01 | `MB_SECRET_BASE_WALL` | secret-base wall — genuinely a wall, listed so it is not lumped with the flat ones |
| elevation 1 | (not a behaviour) | surf water is **collision 0** and impassable by *elevation*, `source/fieldpath.c:147-156`. `source/main.c:969` already excludes `(e>>12)==1` from the stand-up rule — keep that |

`MB_INVALID` = **0xFF** (`include/constants/metatile_behaviors.h:248`, `UCHAR_MAX`) is what
`GetMetatileAttributesById` returns for an id ≥ 1024 — treat as VOID.

## 6. PLAYER + OBJECT EVENTS

### 6.1 `SaveBlock1.pos` — the CAMERA-FOCUS tile, not the avatar tile

`gSaveBlock1Ptr` = **0x03005D8C** (`pokeemerald.sym`; profile column `sb1ptr`,
`source/gamestate.c:23`). Deref → EWRAM `struct SaveBlock1`
(`include/global.h:984-997`):

| Off | Type | Field | Read |
|---|---|---|---|
| 0x00 | `s16` | `pos.x` | `read16(sb1 + 0)` — `source/gamestate.c:934` |
| 0x02 | `s16` | `pos.y` | `read16(sb1 + 2)` — `:935` |
| 0x04 | `u8` | `location.mapGroup` | `:936` |
| 0x05 | `u8` | `location.mapNum` | `:937` |
| 0x32 | `u16` | `mapLayoutId` | mirror of `gMapHeader.mapLayoutId` |
| 0x34 | `u16[256]` | `mapView` | the saved 15×14 window (§2.4) — not needed |

**`pos` is MAP-LOCAL (0-based, border excluded) and it tracks the CAMERA, not the player.**
Both facts are load-bearing and both are verified:

- map-local: `GetCameraFocusCoords` returns `pos + MAP_OFFSET` and `SetCameraFocusCoords` stores
  `x - MAP_OFFSET` (pret `src/fieldmap.c:747-755`); `SetPlayerCoordsFromWarp` assigns
  `pos = warps[warpId].x/y` and warp events are map-local (`src/overworld.c:605-610`, matching
  `source/fieldpath.c:161-179`).
- camera, not player: `CameraMove` is what advances `pos` (`src/fieldmap.c:612-628`), driven by the
  camera object which tracks *whatever sprite* `InitCameraUpdateCallback` was given
  (`src/field_camera.c:351-357`). The repo already documents the split — the gs-logger header says
  "px,py = camera tile; objX,objY = true avatar tile" (`source/gamestate.c:1333`) — and
  `source/presence.h:214-218` records the measured one-frame, one-tile disagreement between the two
  during a normal step (`PRES_OBJ_TOL 1`).

Equivalently: `pos` is also the **grid** index of the visible 15×14 window's top-left (§2.4), which
is why `build_depth_grid`'s `gx = px + c` works.

### 6.2 `gPlayerAvatar`

`gPlayerAvatar` = **0x02037590**, size 0x24 (`pokeemerald.sym`; profile column `playerAvatar`,
`source/gamestate.c:57`). pret `include/global.fieldmap.h:342-361`:

| Off | Type | Field | Diorama use |
|---|---|---|---|
| 0x00 | `u8` | `flags` | `PLAYER_AVATAR_FLAG_*` (ON_FOOT 0x01, MACH_BIKE 0x02, ACRO_BIKE 0x04, SURFING 0x08, UNDERWATER 0x10, CONTROLLABLE 0x20, FORCED_MOVE 0x40, DASH 0x80 — `include/global.fieldmap.h:288-295`) |
| 0x01 | `u8` | `transitionFlags` | — |
| **0x02** | `u8` | **`runningState`** | 0 `NOT_MOVING`, 1 `TURN_DIRECTION`, 2 `MOVING` (`include/global.fieldmap.h:327-332`) — used by §7.2 |
| **0x03** | `u8` | **`tileTransitionState`** | 0 `T_NOT_MOVING`, 1 `T_TILE_TRANSITION`, 2 `T_TILE_CENTER` (`:335-340`) — used by §7.2 |
| 0x04 | `u8` | `spriteId` | the existing cross-check (`source/peersprite.h:113`) |
| **0x05** | `u8` | **`objectEventId`** | **the player's index into `gObjectEvents`** |
| 0x06 | `bool8` | `preventStep` | — |
| 0x07 | `u8` | `gender` | — |

**Do not hard-code the player's object-event slot to 0.** It *is* 0 in practice (the avatar is
spawned first, `InitPlayerAvatar` → `SpawnSpecialObjectEvent`, `src/field_player_avatar.c:1399-1407`)
and both `source/gamestate.c:964` and `source/fieldpath.c:139` currently assume it — but the
authoritative index is `gPlayerAvatar.objectEventId` and the authoritative flag is the object
event's own `isPlayer` bit (§6.3). Read the index; assert `isPlayer`.

### 6.3 `struct ObjectEvent` — full field map (size 0x24)

`gObjectEvents` = **0x02037350**, size **0x240 = 16 × 0x24** (`pokeemerald.sym`;
`OBJECT_EVENTS_COUNT` = 16, `include/constants/global.h:46`). Profile column `mapObjects`
(`source/gamestate.c:42`). pret `include/global.fieldmap.h:194-249`.

The first four bytes are one `u32` bitfield; on little-endian ARM, bit *n* of the u32 is bit
`n & 7` of byte `n >> 3`:

| Byte | Bit | Field | Diorama |
|---|---|---|---|
| **0x00** | 0 | **`active`** | slot in use — the existing test `read32(oe) & 1` (`source/main.c:3660`) |
| 0x00 | 1 | `singleMovementActive` | |
| 0x00 | 2-5 | `triggerGroundEffectsOnMove/OnStop`, `disableCoveringGroundEffects`, `landingJump` | |
| 0x00 | 6-7 | `heldMovementActive`, `heldMovementFinished` | |
| 0x01 | 0-2 | `frozen`, `facingDirectionLocked`, `disableAnim` | |
| 0x01 | 3-4 | `enableAnim`, `inanimate` | |
| **0x01** | **5** | **`invisible`** | mask 0x20 of byte 0x01 — skip the billboard |
| **0x01** | **6** | **`offScreen`** | mask 0x40 of byte 0x01 — the engine's own cull |
| 0x01 | 7 | `trackedByCamera` | 1 on whatever the camera follows (usually the player) |
| **0x02** | **0** | **`isPlayer`** | mask 0x01 of byte 0x02 — the player discriminator |
| 0x02 | 1-7 | `hasReflection`, `inShortGrass`, `inShallowFlowingWater`, `inSandPile`, `inHotSprings`, `hasShadow`, `spriteAnimPausedBackup` | `hasShadow` (mask 0x40) is a free input for the After-v1 shadow pass |
| 0x03 | 0-3 | `spriteAffineAnimPausedBackup`, `disableJumpLandingGroundEffect`, `fixedPriority`, `hideReflection` | |

| Off | Type | Field | Diorama |
|---|---|---|---|
| **0x04** | `u8` | **`spriteId`** | index into `gSprites` (< 64) |
| **0x05** | `u8` | **`graphicsId`** | the FORM (already in `PsprHdr.graphicsId`) |
| 0x06 | `u8` | `movementType` | |
| 0x07 | `u8` | `trainerType` | |
| 0x08 | `u8` | `localId` | |
| 0x09 | `u8` | `mapNum` | which map the object belongs to |
| 0x0A | `u8` | `mapGroup` | |
| **0x0B** | `u8` | **`currentElevation` : low nibble, `previousElevation` : high nibble** | `source/fieldpath.c:139` reads `&0x0F` (current); `source/main.c:3663` reads `>>4` (previous, = the engine's draw-priority tier). **Both are correct** |
| 0x0C | `s16,s16` | `initialCoords` | spawn tile (grid coords) |
| **0x10** | `s16` | **`currentCoords.x`** | **grid coords (= map-local + 7)** |
| **0x12** | `s16` | **`currentCoords.y`** | |
| **0x14** | `s16` | **`previousCoords.x`** | the tile the object came from |
| **0x16** | `s16` | **`previousCoords.y`** | |
| **0x18** | `u8` | **`facingDirection` : low nibble, `movementDirection` : high nibble** | `DIR_NONE 0, DIR_SOUTH 1, DIR_NORTH 2, DIR_WEST 3, DIR_EAST 4` (`include/constants/global.h:137-141`) |
| 0x19 | `u8` | `range.rangeX` low nibble / `rangeY` high nibble | (packed struct after the u16 bitfield unit at 0x18) |
| 0x1A | `u8` | `fieldEffectSpriteId` | |
| 0x1B | `u8` | `warpArrowSpriteId` | |
| 0x1C | `u8` | `movementActionId` | |
| 0x1D | `u8` | `trainerRange_berryTreeId` | |
| **0x1E** | `u8` | **`currentMetatileBehavior`** | free — the tile the object stands on |
| 0x1F | `u8` | `previousMetatileBehavior` | |
| 0x20 | `u8` | `previousMovementDirection` | |
| 0x21 | `u8` | `directionSequenceIndex` | |
| 0x22 | `u8` | `playerCopyableMovement` | |
| 0x23 | — | padding | |

#### 6.3.1 The `currentCoords` question — RESOLVED, there is no conflict

The prompt flags "two claims about `currentCoords`". There is only one: **+0x10 (x) / +0x12 (y)**,
and every source agrees:

| Source | Claim |
|---|---|
| pret `include/global.fieldmap.h:206` | `/*0x10*/ struct Coords16 currentCoords;` (with `initialCoords` at 0x0C, `previousCoords` at 0x14) |
| `source/gamestate.h:195` | "`+0x10/+0x12 currentCoords x/y`" |
| `source/gamestate.c:965-966` | `read16(mapObjects + 0x10)` / `+ 0x12`, commented "(grid, +7)" |
| `source/main.c:3661-3662` | same offsets, same comment |
| `source/presence.h:151` | "`objX, objY` = `gObjectEvents[0].currentCoords` (= px+7/py+7 in the field)" |

**They are GRID coordinates**, i.e. map-local **+ MAP_OFFSET (7)** — verified two ways:
`InitPlayerAvatar` builds its template from `x - MAP_OFFSET`/`y - MAP_OFFSET` and
`SpawnSpecialObjectEvent` adds it back (`src/field_player_avatar.c:1389-1390`), and
`TryMoveObjectEventToMapCoords` adds `MAP_OFFSET` before calling `MoveObjectEventToMapCoords`
(`src/event_object_movement.c:2151-2158`). `source/presence.h:61-64` (`PRES_MAP_OFFSET 7`) says the
same. Map-local = `currentCoords - 7`.

The offset that IS worth double-checking on hardware is `facingDirection` at **+0x18** — the repo
still carries "offset verify-on-hw" at `source/gamestate.c:967`. It is now **VERIFIED-SRC**:
`u16 facingDirection:4; u16 movementDirection:4;` occupies the u16 storage unit at 0x18, so the
low nibble of byte 0x18 is `facingDirection` and the high nibble is `movementDirection`; the
following `range` struct takes byte 0x19, which is what puts `fieldEffectSpriteId` at 0x1A
(pret's own annotation). The repo's `read8(mapObjects + 0x18) & 0x0F` is right.

#### 6.3.2 Step semantics (needed by §7)

- On a walk step, `ShiftObjectEventCoords` runs **at the start**: `previousCoords ← currentCoords`,
  `currentCoords ← destination` (`src/event_object_movement.c:2117-2123`, called from e.g.
  `InitNpcForWalkSlow`, `:5128-5142`). So **`currentCoords` is the DESTINATION for the whole step**.
- The per-frame interpolation lives in the SPRITE: `NpcTakeStep` calls `Step1/2/3/4/8`
  (`:8191-8219`) which add `sDirectionToVectors[dir]` × {1,2,3,4,8} to **`sprite->x` / `sprite->y`**
  directly (not `x2`/`y2`). Direction vectors (`:907-917`): `DIR_SOUTH (0,+1)`, `DIR_NORTH (0,−1)`,
  `DIR_WEST (−1,0)`, `DIR_EAST (+1,0)`.
- A **jump** (ledge hop) moves horizontally with `Step1` and puts the **arc in `sprite->y2`**
  (`DoJumpSpriteMovement`, `:8462-8492`, `sprite->y2 = GetJumpY(...)`, reset to 0 at the end).
  ⇒ in the diorama `y2` is **height**, not depth (§7.1).
- When the camera crosses a map connection, `UpdateObjectEventCoordsForCameraUpdate` shifts
  `initialCoords`/`currentCoords`/`previousCoords` of **every active object** by `gCamera.x/y`
  (`:2166-2189`) — the world re-bases (§7.3).

### 6.4 `struct Sprite` — the placement fields

`gSprites` = **0x02020630**, size 0x1144 = 65 × 0x44 (`pokeemerald.sym`; profile column `sprites`,
`source/gamestate.c:57`). `MAX_SPRITES` = 64 (`include/sprite.h:5`) — the 65th entry is the
sentinel. pret `include/sprite.h:194-242`:

| Off | Type | Field | Diorama |
|---|---|---|---|
| 0x00 | `struct OamData` | `oam` (8 bytes = attr0/attr1/attr2 + subpriority-ish packing) | the billboard decode (§9); already `PSPR_SPR_OAM` |
| 0x18 | `const struct SubspriteTable *` | `subspriteTables` | non-NULL ⇒ the sprite draws as several OAM entries (§9.3) |
| **0x20** | `s16` | **`x`** | map-anchored pixel X |
| **0x22** | `s16` | **`y`** | map-anchored pixel Y |
| **0x24** | `s16` | **`x2`** | per-anim offset (figure-8 etc.) |
| **0x26** | `s16` | **`y2`** | per-anim offset — **the jump arc** |
| **0x28** | `s8` | **`centerToCornerVecX`** | `-(gfxWidth >> 1)` |
| **0x29** | `s8` | **`centerToCornerVecY`** | `-(gfxHeight >> 1)` — must be undone to compare feet |
| 0x2A | `u8` | `animNum` | already `PSPR_SPR_ANIM` (u16: low = animNum, high = animCmdIndex) |
| 0x2B | `u8` | `animCmdIndex` | |
| 0x2E | `s16[8]` | `data[0..7]` | movement-action scratch; `data[4]`=speed, `data[5]`=timer for walks, but **re-purposed per action** — do not build the world position on it |
| **0x3E** | bits | `inUse`:0x01, `coordOffsetEnabled`:0x02, **`invisible`:0x04** | already `PSPR_SPR_FLAGS` (`source/peersprite.h:97`) |
| 0x3F | bits | `hFlip`:0x01, `vFlip`:0x02, … | the OAM flips are in `oam` too |
| 0x40 | `u16` | `sheetTileStart` | |
| 0x42 | `u8` | `subspriteTableNum`:6 / `subspriteMode`:2 | already `PSPR_SPR_SUBTBL` |
| 0x43 | `u8` | `subpriority` | |

Every offset above already appears in `source/peersprite.h:94-99` for 0x00/0x2A/0x3E/0x42; the
diorama adds only 0x18-0x29.

**How the OAM position is produced** (`UpdateOamCoords`, pret `src/sprite.c:339-359`):

```
if (coordOffsetEnabled)  oam.x = x + x2 + centerToCornerVecX + gSpriteCoordOffsetX
                         oam.y = y + y2 + centerToCornerVecY + gSpriteCoordOffsetY
else                     oam.x = x + x2 + centerToCornerVecX     (same for y)
```

Object-event sprites always set `coordOffsetEnabled = TRUE`
(`src/event_object_movement.c:1466`, `:1625`, `:1788`).

### 6.5 DISPCNT OBJ mapping mode

`InitOverworldGraphicsRegisters` writes
`DISPCNT_OBJ_ON | DISPCNT_WIN0_ON | DISPCNT_WIN1_ON | DISPCNT_OBJ_1D_MAP | DISPCNT_HBLANK_INTERVAL`
(pret `src/overworld.c:2122-2123`). So in the overworld **DISPCNT bit 6 (0x0040) is SET = 1-D OBJ
character mapping**, and OBJ drawing (bit 12, 0x1000) is on.

`peersprite` already reads and honours this: `h.map1d = (dispcnt >> 6) & 1` with a documented
default of 1 when the IO read looks unwired (`source/peersprite.c:118-124`), and
`pspr_tile_offset` implements both mappings (`source/peersprite.c:154-158`). The diorama reuses it
unchanged — do not re-derive.

## 7. CAMERA / SUB-TILE PLACEMENT (PHASE O1)

### 7.1 The exact world position of every object event — derivation

**Candidate (b) — object coords + step timer — is REJECTED.** `currentCoords` jumps a whole tile at
step *start* (§6.3.2), so it needs a progress term, and the only progress terms are `sprite->data[4]`
(speed class) and `data[5]` (timer) — slots that are re-purposed by every other movement action
(`data[4]`=`sDistance`, `data[5]`=`sJumpType`, `data[6]`=`sTimer` during a jump,
`src/event_object_movement.c:8451-8453`). A table of per-action slot meanings is exactly the
fragility `RESEARCH-classification.md` §8.3 warns about.

**Candidate (a) — sprite positions — is ACCEPTED, but with `sprite->x/y`, NOT the OAM coords.**
`RESEARCH-classification.md` §5.5's warning ("the sprite's *screen* coords must never be used")
is about `oam.x/y`; those add `gSpriteCoordOffset*`, which carries the scripted pan
(`gSpriteCoordOffsetX = gTotalCameraPixelOffsetX − sHorizontalCameraPan`,
pret `src/field_camera.c:459-462`) and the vertical +32 pan and every shake.
**`sprite->x/y` is the map-anchored value, one addition upstream of that.**

Derivation. Every object-event sprite is anchored by `SetSpritePosToMapCoords`
(`src/event_object_movement.c:4801-4818`, and `GetMapCoordsFromSpritePos` `:4793-4799` at spawn):

```
sprite->x = 16*(gridX - pos.x) + dx + 8
sprite->y = 16*(gridY - pos.y) + dy + 16 + centerToCornerVecY
dx = -gTotalCameraPixelOffsetX - gFieldCamera.x + 16*sgn(gFieldCamera.x)
dy = -gTotalCameraPixelOffsetY - gFieldCamera.y + 16*sgn(gFieldCamera.y)
```

Define `C_x ≜ 16*pos.x + gFieldCamera.x - 16*sgn(gFieldCamera.x) + gTotalCameraPixelOffsetX`.
Then `sprite->x = 16*gridX - C_x + 8`, i.e.

> **`sprite->x + 8`-normalised anchors differ from `16 × gridX` by the SAME constant `C_x` for
> every object-event sprite, and `C_x` is invariant over time while a map is loaded.**

Invariance proof: over one camera tile-step east, `CameraUpdate` does
`gTotalCameraPixelOffsetX -= movementSpeedX` each frame (Σ = −16) while `CameraMove` does
`pos.x += 1` (+16) and `gFieldCamera.x` wraps 0→…→0 (`src/field_camera.c:406-421`,
`src/fieldmap.c:612-613`). Net change of `C_x` = 0. Across a **connection** rebase, `pos` jumps by
`Δ` and every object's coords are shifted by the same `Δ`
(`UpdateObjectEventCoordsForCameraUpdate`, `src/event_object_movement.c:2166-2189`) — `gridX − pos.x`
is preserved, so `C_x` is still invariant. ∎

Therefore **differences between sprite anchors are exact world-pixel differences**, free of pan,
shake, scroll and screen space:

```
Sx(i) = sprite_i->x + sprite_i->x2                      // world pixel X, shared arbitrary origin
Sz(i) = sprite_i->y - sprite_i->centerToCornerVecY      // world pixel Y (depth); NOTE: no y2
Hy(i) = -sprite_i->y2                                   // HEIGHT above ground (jump arc), px, up +
```

(`y2` is excluded from depth and promoted to height because a ledge hop is a vertical arc in the
game's screen space — §6.3.2. `x2` stays in X: it is a genuine horizontal offset.)

### 7.2 The formula, with its absolute anchor

One absolute anchor is still needed. Use the **player's own object event**, whose tile is exact,
plus the camera's sub-tile accumulator:

```
pi   = read8(gPlayerAvatar + 0x05)                     // objectEventId
P    = gObjectEvents + 0x24*pi                         // must have active=1 and isPlayer=1
ps   = gSprites + 0x44 * read8(P + 0x04)               // the player's sprite

// sub-tile: gFieldCamera.x/y in (-16,16); the +-16 term undoes "pos advanced at step start"
sgn(v) = (v > 0) - (v < 0)
subX = (s32)read32(gFieldCamera + 0x10);  subX -= 16*sgn(subX)
subY = (s32)read32(gFieldCamera + 0x14);  subY -= 16*sgn(subY)
if (read8(gPlayerAvatar + 0x02) == 0 && read8(gPlayerAvatar + 0x03) == 0) subX = subY = 0;
      // runningState == NOT_MOVING && tileTransitionState == T_NOT_MOVING
      // -> the player is standing; any residual gFieldCamera is a SCRIPTED camera, not the avatar

playerPxX = 16*(s16)read16(P + 0x10) + subX            // grid-pixel X of the player
playerPxY = 16*(s16)read16(P + 0x12) + subY

// every active object event i (including the player, for which the deltas are 0)
objPxX(i) = playerPxX + (s16)(Sx(i) - Sx(player))
objPxY(i) = playerPxY + (s16)(Sz(i) - Sz(player))

// world units, MAP-LOCAL, 1 tile = 1.0
worldX(i) = objPxX(i)/16.0f - MAP_OFFSET
worldZ(i) = objPxY(i)/16.0f - MAP_OFFSET
worldY(i) = Hy(i)/16.0f                                 // 0 except mid-jump
```

The `(s16)` cast on the difference makes it wrap-safe (`sprite->x/y` are `s16`; `C_x` is only
defined mod 65536 because `gTotalCameraPixelOffsetX` is a `u16`, `src/field_camera.c:43-44`).

Properties:

| Situation | Behaviour |
|---|---|
| Player walking | `Sx/Sz` deltas are 0 for the player; `subX/subY` gives his exact sub-tile; NPCs interpolate through their own `sprite->x/y` |
| NPC walking, player still | player term is a pure tile; NPC delta carries the NPC's sub-tile |
| Scripted camera pan / shake | cancels in the difference; `subX/subY` forced to 0 by the standing test ⇒ **exact** |
| Bike / running (`MOVE_SPEED_FAST_*`) | `Step2/3/4/8` scale `sprite->x/y`; nothing else changes |
| Ledge hop | horizontal from `Step1`, arc from `y2` → `worldY` |
| Surf / fishing / field moves | the avatar's graphicsId changes; positions unaffected |
| Camera tracks a non-player sprite (`InitCameraUpdateCallback`) | `trackedByCamera` (byte 0x01 bit 7) says which object; the player is standing then, so `subX/subY = 0` and the answer is still exact |

### 7.3 Worked example

Setup: Littleroot Town (20×20, `LittlerootTown_Layout_Blockdata` 0x083E9F64 → 0x083EA284 = 800 B =
400 u16 = 20×20 — VERIFIED-SYM). Player at map-local (10, 9) ⇒ grid (17, 16) ⇒
`SaveBlock1.pos = (10, 9)`. `gBackupMapLayout.width = 20 + 15 = 35`, `height = 20 + 14 = 34`.

Frame N (standing): `runningState = 0`, `tileTransitionState = 0`, `gFieldCamera.x = 0`
⇒ `subX = subY = 0`, `playerPxX = 16*17 = 272`, `playerPxY = 16*16 = 256`
⇒ `worldX = 272/16 − 7 = 10.0`, `worldZ = 256/16 − 7 = 9.0`. ✔ matches `pos`.

An NPC standing one tile north-east, grid (18, 15): its sprite was anchored with the same `C_x`, so
`Sx(npc) − Sx(player) = 16*(18−17) = +16` and `Sz(npc) − Sz(player) = 16*(15−16) = −16`
⇒ `objPx = (288, 240)` ⇒ `worldX = 11.0`, `worldZ = 8.0`. ✔

Frame N+6 (player walking EAST at `MOVE_SPEED_NORMAL`, 6 of 16 frames done):
`ShiftObjectEventCoords` already set `currentCoords = (18, 16)`, `previousCoords = (17, 16)`.
`gFieldCamera.x = 6`, so `subX = 6 − 16 = −10`, `runningState = MOVING` so it is kept.
`playerPxX = 16*18 + (−10) = 278` ⇒ `worldX = 278/16 − 7 = 10.375` = **10 + 6/16**. ✔
The stationary NPC: `Sx(npc) − Sx(player)` has shrunk by 6 (the player's sprite advanced 6 px), so
`objPxX(npc) = 278 + (16 − 6) = 288` ⇒ `worldX = 11.0` — **unchanged**, as it must be. ✔

### 7.4 Map-change signal, and connection-walk vs warp

Two identity tokens, both already read every frame:

| Token | Address | Cite |
|---|---|---|
| `gMapHeader.mapLayout` (pointer) | `read32(0x02037318)` | `source/main.c:917` already caches on it |
| `gMapHeader.mapLayoutId` (u16) | `read16(0x02037318 + 0x12)` | §2.1 |
| `SaveBlock1.location.mapGroup/mapNum` | `read8(sb1+4)`, `read8(sb1+5)` | `source/gamestate.c:936-937` |

A change in **any** of them ⇒ rebuild the static geometry (§8).

**Discriminating a connection walk from a warp — use `gCamera`.**

`gCamera` = **0x02037334**, size 0x0C (`pokeemerald.sym`; it sits immediately after `gMapHeader`).
`struct Camera` (`include/global.fieldmap.h:364-369`): `bool8 active:1` at +0x00 (bit 0),
`s32 x` at **+0x04**, `s32 y` at **+0x08**.

`CameraMove` (pret `src/fieldmap.c:603-632`):

```
gCamera.active = FALSE;
direction = GetPostCameraMoveMapBorderId(x, y);
if (direction is NONE or INVALID) { pos += (x,y); }          // ordinary step, no rebase
else {
    SaveMapView(); old = pos;
    connection = GetIncomingConnection(direction, pos.x, pos.y);
    SetPositionFromConnection(connection, direction, x, y);  // pos jumps into the new map
    LoadMapFromCameraTransition(connection->mapGroup, connection->mapNum);
    gCamera.active = TRUE;
    gCamera.x = old.x - pos.x;  gCamera.y = old.y - pos.y;
    pos += (x,y);
    MoveMapViewToBackup(direction);
}
```

⇒ **`gCamera.active == 1` with non-zero `gCamera.x/y` is set only by a walked connection
crossing.** A warp never goes through `CameraMove`; it runs the full map-load path
(`LoadCurrentMapData`, `src/overworld.c:589-595`) and leaves `gCamera` alone.

Rebase math: object coords are updated as `coord -= gCamera.(x,y)`
(`src/event_object_movement.c:2178-2183`), so a point whose old-map-local coord was `L_old` has
new-map-local coord `L_new = L_old − gCamera.(x,y)`. **The diorama translates its existing world
origin by `(−gCamera.x, −gCamera.y)` and keeps the camera exactly where it is** — the
"no visible camera jump" trick of `RESEARCH-classification.md` §6. On a warp (no `gCamera`) the
camera **snaps** to the player.

Two practical notes:

1. `gCamera.active` is only cleared by the *next* `CameraMove`, i.e. after the player walks one more
   tile. So on the frame the diorama first observes the new map identity, `gCamera` is still
   populated — read it then, latch it, and clear your own copy.
2. Confirm with the connection list (`RESEARCH-classification.md` §6): search the **old** map's
   `MapConnections` (§3.1) for the new `(mapGroup, mapNum)`. If found ⇒ connection walk (and the
   direction tells you which origin formula of §3.4 to invert); if not ⇒ warp. Doing both makes the
   classification self-checking and costs one small ROM scan on a map change only.

## 8. READ CADENCE + THREAD RULE

### 8.1 Where the reads must run (PHASE.md invariant 4)

The **parked window** is the stretch of `main.c`'s loop after both workers have been joined and
before they are restarted:

| Boundary | `source/main.c` |
|---|---|
| Workers joined (`LightEvent_Wait(&emuA.done); LightEvent_Wait(&emuB.done)`) | **3506-3512** (and the menu-entry twin at 3496-3501) |
| `build_depth_grid` / `bg0_scan` / OAM scan block | **3622-3679** ("stereoscopic depth: TOP game overworld state + on-screen OAM rects (cores parked)", `:3622`) |
| `game_read` ×2 + gs-log + `TiltSnap` fill | **3680-3712** |
| `presence_read_fill` / `presence_read_sprite` | **3739**, **3762** ("The SECOND reader, and the last one, in the SAME parked window", `:3748`) |
| Workers restarted (`workersRunning = true`) | **4046** |

**All diorama bus reads go between 3679 and 3712**, immediately after the existing depth block, in
the same `if (depth3d.overworld && topCore)` shape. Rationale, quoted from the file: "Workers are
parked here -> touch RAM access safe" (`source/main.c:3505`).

**All GPU/texture/VBO writes go inside `C3D_FrameBegin` … `C3D_FrameEnd`** — opened at
`source/main.c:4507`, closed at `:5314`. The rule and its reason are already written down at
`source/main.c:1550-1554`: "Writing the texture in the parked window — before `C3D_FrameBegin` —
would race the still-in-flight [previous] frame". `pspr_blit_tiled` is explicitly documented as a
plain CPU store that is legal *inside* the frame and must be placed there
(`source/peersprite.h:325-327`).

### 8.2 How data crosses the boundary — copy the two precedents exactly

| Precedent | Shape | Filled | Consumed |
|---|---|---|---|
| `DepthSnap depth3d` | one POD struct on the loop's stack, `main.c:3182` | parked window, `:3625-3678` | render block, `:2810+` (`main.c:1209` says so verbatim) |
| `TiltSnap tiltSnap[2]` | POD, **indexed by SCREEN**, `main.c:3196` | parked window, `:3702-3706` | gate, inside the frame, `:4556-4560` |

The diorama adds **one** such struct — call it `DioSnap` — with the same rules:
POD, memset-able, no pointers, indexed by SCREEN (top only in v1), zero-init means "gate off".
`main.c:1208-1214` is the contract to copy: "No new `game_read`, no new `profile_for`, no new
`gbacore_read*`" where the existing ones suffice — the diorama should reuse the `GameState ts`
already read at `:3625` rather than calling `game_read` a third time.

### 8.3 Per-MAP reads (rebuilt only when §7.4 says the map changed)

| What | Reads | Bytes |
|---|---|---|
| `gMapHeader` header block (layout ptr, events, connections, layoutId, mapType) | 4 × `read32` + 2 × `read16`/`read8` | 20 |
| `MapLayout` (w, h, border, map, primary, secondary) | 6 × `read32` | 24 |
| Border 2×2 | 4 × `read16` | 8 |
| **Live grid** `gBackupMapLayout` (w, h, map ptr) + the whole grid | 3 × `read32` + `backupW*backupH` × `read16` | 12 + **2·(W+15)·(H+14)** |
| Both `Tileset` structs | 2 × 6 × `read32` | 48 |
| Metatile **attributes** for the ids in use | ≤ 1024 × `read16` (memoised per id, like `metatile_layer`, `main.c:918-935`) | ≤ 2048 |
| Metatile **entries** for the ids in use | ≤ 1024 × 8 × `read16` | ≤ 16384 |
| Tile pixels from VRAM for those ids | ≤ 1024 × 8 × 32 B, via `read32` | ≤ 262144 (see note) |
| BG palettes (13 slots × 16 colours) | 208 × `read16` from `gPlttBufferUnfaded` | 416 |
| Connections (count + N × 12 B) + each neighbour's header/layout/tilesets | 2 + 8·N `read32`; `fieldtrav_rom_map` per neighbour | ~50·N |
| Neighbour ROM grids (only for instanced neighbours) | `w*h` × `read16` each | 2·w·h each |

Live-grid magnitudes (real Emerald layouts): Littleroot 20×20 ⇒ 35×34 = 1190 cells = **2380 B**;
House1 10×9 ⇒ 25×23 = 575 cells = **1150 B**; a big route (e.g. 60×60) ⇒ 75×74 = 5550 cells =
**11.1 kB**. `MAX_MAP_DATA_SIZE` caps the live grid at **10240 cells = 20480 B** absolutely
(`include/fieldmap.h:10`, enforced at `src/fieldmap.c:98`).

Tile-pixel note: the ≤256 kB figure is the worst case "every one of 1024 metatiles is used". Do what
the reference does (`RESEARCH-classification.md` §5.1): **mark only the metatile ids the grid
actually references (plus the 4 border entries) and compose only those.** A real Emerald map uses
on the order of 100-300 distinct metatiles, so the realistic figure is 25-75 kB of `read32`, once
per map, in the parked window — comparable to what `presence` already does on a sprite change
(`source/peersprite.h:293-296`).

### 8.4 Per-FRAME reads

| What | Reads | Bytes | Note |
|---|---|---|---|
| Gate/data preconditions (§1.2) | 3 × `read32` | 12 | reuse `ts` for ctx/px/sb1Valid |
| `gFieldCamera.x/y` | 2 × `read32` | 8 | **already read** by `build_depth_grid` (`main.c:942-943`) — reuse |
| `gPlayerAvatar` bytes 0x02, 0x03, 0x05 | 1 × `read32` | 4 | one aligned word covers 0x00-0x03; +1 `read8` for 0x05 |
| Object events: `active`/flags word, `spriteId`+`graphicsId`, elevation, `currentCoords`, `previousCoords`, `facingDirection` | 16 × (1×`read32` + 1×`read16` + 1×`read8` + 2×`read32` + 1×`read8`) | ≤ 16 × 16 = **256** | `main.c:3658-3667` already reads `active` + `currentCoords` + elevation for all 16 — **extend that loop, do not add a second one** |
| Sprites for the active objects: `x,y` (0x20), `x2,y2` (0x24), `c2cX/Y` (0x28), flags (0x3E), `oam` (0x00-0x05), `animNum` (0x2A), `subspriteTables` (0x18) | ≤ 16 × 8 reads | ≤ 16 × 26 ≈ **416** | only for objects that passed `active && !invisible && !offScreen` |
| Billboard pixel re-decode | `pspr_capture`'s own budget: **28 reads steady state**, +64/+128 `read32` on an animation change | — | `source/peersprite.h:287-296` |

Total steady-state per frame, 16 NPCs on screen: **≈ 700 bytes / ≈ 200 bus reads**, plus the
billboard captures. For scale, `source/peersprite.h:296` records that "the same parked window
already performs ~150 reads in `build_depth_grid`".

### 8.5 Live-grid change detection (PHASE.md O3)

The live grid mutates without a map change: door open/close animations
(`CurrentMapDrawMetatileAt` / `DrawDoorMetatileAt`, `include/field_camera.h:22-24`), cut trees,
smashed rocks, secret-base edits, `MapGridSetMetatileIdAt` from scripts
(`src/fieldmap.c:356-371`), and the Trick House.

Three options, with costs (grid = `backupW × backupH` u16, §8.3):

| Option | Cost per frame | Latency | Verdict |
|---|---|---|---|
| Full re-read + memcmp | 2.4-11 kB of `read16` + a compare | 1 frame | too expensive at 60 Hz |
| **Rolling hash of a stripe** — hash 1/8th of the grid each frame (round-robin), 8 stripes | 300 B-1.4 kB | ≤ 8 frames (133 ms) | **RECOMMENDED for v1** |
| Visible-window-only hash (the 15×14 window at `pos`) | 210 cells = 420 B | 1 frame, but blind off-screen | good complement; free, since the window is what the player sees |

**Recommendation (O3): hash the visible 15×14 window every frame (420 B) AND one 1/8 stripe of the
full grid per frame (round-robin).** A door the player is standing in front of re-meshes within one
frame; anything off-screen within 8. Use a cheap 32-bit FNV/xor-rotate over the raw u16s — the words
already carry metatile id + collision + elevation, so any semantic change moves the hash.

Rebuild scope on a hit: v1 may rebuild the whole static VBO (it is a per-map-sized job, not a
per-frame one, and doors are rare). A future optimisation is a per-stripe dirty flag.

### 8.6 What must NOT be done

- No read from a worker thread; no read outside 3506…4046 (`source/main.c`).
- No GPU call from the parked window; no bus read from inside `C3D_FrameBegin/End`.
- No write of any kind to emulated memory, ever (PHASE.md invariant 6; the bus the diorama gets is
  read-only by construction, like `PsprBus`, `source/peersprite.h:190-197`).

## 9. OAM BILLBOARD DECODE

### 9.1 What `peersprite` already provides, reusable AS-IS

All of the following are pure C, host-tested, and shipped
(`source/peersprite.h`, `source/peersprite.c`):

| Function | Contract | Reuse |
|---|---|---|
| `pspr_oam_size(shape, size, &w, &h)` | the standard GBA OBJ shape/size table; shape 3 ⇒ (0,0) (`peersprite.h:225`) | **as-is** |
| `pspr_tile_offset(tx, ty, tilesW, map1d)` | 1D: `ty*tilesW+tx`; 2D: `ty*32+tx` (`:240`) | **as-is** |
| `pspr_max_tile_offset(w, h, map1d)` | bounds-check input (`:243`) | **as-is** |
| `pspr_bgr555_to_rgba8(c)` | BGR555 → 0xRRGGBBAA, 5→8 replicating high bits (`:248`) | **as-is**, also for the metatile atlas (§4.6) |
| `pspr_tile_index(tile32, x, y)` | 4bpp nibble → palette index; **low nibble = LEFT pixel** (`:252`) | **as-is**, also for the atlas |
| `pspr_decode(tiles, n, pal, hdr, out, cap)` | detile + palette + flip in one pass; index 0 → `0x00000000` (`:261`) | **as-is** |
| `pspr_bleed_edges(px, w, h)` | RGB bleed into transparent texels, **alpha untouched** (`:268`) | **as-is** — the diorama samples `GPU_LINEAR` too |
| `pspr_gather_palette(bus, plttUnfaded, pal, out)` | 16 `rd16`; `gPlttBufferUnfaded` primary, hardware PLTT `0x05000200` fallback (`:281`) | **as-is** |
| `pspr_gather_tiles(bus, hdr, out, cap)` | `(w/8)*(h/8)` tiles in **raster** order (`:285`) | **as-is** |
| `pspr_hdr_changed` / `pspr_pal_changed` | field-by-field change key, `subTbl` deliberately excluded (`:273-274`) | **as-is** |
| `pspr_morton8`, `pspr_tex_offset`, `pspr_blit_tiled`, `pspr_encode_tiled`, `pspr_verify_tiling` | the 3DS tiled-RGBA8 encode + the init-time byte-exact tiling proof (`:306-333`) | **as-is** |
| `PsprBus` | `rd8/rd16/rd32 + ctx`, no writes (`:192-197`) | **the shape the diorama's own bus must copy** (PHASE.md invariant 3) |

### 9.2 What must be generalised

| # | Today | Needed | Why |
|---|---|---|---|
| G1 | **one** sprite per game (the peer avatar); `PsprCapture` is "one per GAME" (`peersprite.h:174`) | **up to 16** — `OBJECT_EVENTS_COUNT` (`include/constants/global.h:46`). Budget 16 captures; typical on-screen count is 2-6 | every active object event becomes a billboard |
| G2 | source = `gObjectEvents[0].spriteId` with a `gPlayerAvatar.spriteId` cross-check (`PSPR_R_MISMATCH`, `peersprite.h:49`) | source = **each** object event's own `spriteId`; the avatar cross-check applies **only** to the object whose `isPlayer` bit is set (§6.3) | NPCs have no `gPlayerAvatar` entry — the existing clause would refuse every one of them |
| G3 | refuses `w > 32 \|\| h > 32` (`PSPR_R_SIZE`, `peersprite.h:54`; the same 32×32 cap is in `main.c:3643`) | overworld object events are 16×32 (shape 2, size 2 — `peersprite.h:218-222`); a few are **32×32** (big NPCs, the Wailmer doll, sailors) and a handful **64×64** via subsprites | 32×32 must be allowed (it already is, `PSPR_MAX_W/H = 32`, `peersprite.h:64-65`); **64×64 is not** — see G4 |
| G4 | `subspriteTableNum` is read but "DIAGNOSTICS ONLY" (`peersprite.h:98`) | a sprite with `subspriteTables != NULL` (`Sprite+0x18`) and `subspriteMode != SUBSPRITES_OFF` draws as **several OAM entries** and its `oam` alone describes only one piece | v1: **refuse** such objects (new reason code, e.g. `PSPR_R_SUBSPRITE`) and fall back to no billboard for that NPC — never draw a quarter of a sprite. Note `ObjectEventGraphicsInfo` carries `width`/`height` at +0x08/+0x0A and `subspriteTables` at +0x14 (`include/global.fieldmap.h:257-274`, `width`/`height` at `:263-264`, `subspriteTables` at `:271`) if a later slice wants the true extent |
| G5 | affine refused outright (`PSPR_R_AFFINE`, `peersprite.h:52`) | **keep refusing.** Overworld object events are never affine; a rotating/scaling sprite is a field effect, and `attr1[9:13]` is a matrix index there, not flips | correctness over coverage — the same stance as `RESEARCH-presenter.md` §3.6 |
| G6 | one live-cell slot in a 128×128 sheet (`PSPR_LIVE_SLOTS 4`, `peersprite.h:76-79`) | an atlas sized for 16 × 32×32 cells = 4×4 grid of 32×32 = **128×128**, which is exactly the existing sheet's geometry | reuse the sheet; re-slot it |
| G7 | change key includes `graphicsId`, `tileNum`, `animNum`, `animCmdIndex`, `pal`, flips, `w/h`, `map1d` (`peersprite.c:265`) | unchanged — but keyed **per object slot**, and the slot must be invalidated when the object's `localId`/`graphicsId` changes (an NPC leaving and another entering can reuse a slot) | otherwise a despawned NPC's pixels get drawn on a new one |

### 9.3 Per-object accept ladder for the diorama (v1)

In order; first refusal wins, and the object simply gets no billboard that frame:

1. `active` bit set (`ObjectEvent+0x00` bit 0).
2. `invisible` clear (`+0x01` bit 5) and `offScreen` clear (`+0x01` bit 6).
3. `spriteId < 64` and `!= SPRITE_NONE (0xFF)`.
4. `gSprites[spriteId].inUse` (`+0x3E` bit 0) and `!invisible` (`+0x3E` bit 2).
5. `subspriteTables == NULL` (`Sprite+0x18`) — else refuse (G4).
6. Everything `pspr_resolve` already checks: affine mode, 4bpp, shape/size ≤ 32×32, DISPCNT mode/OBJ
   enable, tile range inside OBJ VRAM (`source/peersprite.c:110-140`).
7. If `isPlayer`: also require `gPlayerAvatar.spriteId == ObjectEvent.spriteId` (the existing
   `PSPR_R_MISMATCH` clause).

## 10. CONSOLIDATED ADDRESS TABLE + "what could be wrong"

### 10.1 Every address the diorama touches (BPEE, US, one revision)

Verification key: **VERIFIED-SYM** = read from `/tmp/pret/pokeemerald.sym` this session ·
**VERIFIED-SRC** = read from a pret source/header line cited in this document ·
**VERIFIED-DATA** = decoded from a pret data file (`*.bin` / `layouts.json`) this session ·
**IN-REPO** = already shipped and hardware-exercised in this project · **DERIVED** = computed from
the above, no independent read.

| Name | Address / offset | Size | Status | Where |
|---|---|---|---|---|
| `gMapHeader` | `0x02037318` | 0x1C | VERIFIED-SYM + IN-REPO (`gamestate.c:41`,`:47`) | §2.1 |
| `.mapLayout` | `+0x00` | ptr | VERIFIED-SRC + IN-REPO (`main.c:917`) | §2.1 |
| `.events` | `+0x04` | ptr | VERIFIED-SRC + IN-REPO (`fieldpath.c:161`) | §2.1 |
| `.connections` | `+0x0C` | ptr | VERIFIED-SRC | §3.1 |
| `.mapLayoutId` | `+0x12` | u16 | VERIFIED-SRC | §7.4 |
| `.mapType` | `+0x17` | u8 | VERIFIED-SRC | §5.1 |
| `struct MapLayout` `.width/.height` | `+0x00 / +0x04` | s32 | VERIFIED-SRC + IN-REPO (`fieldtrav.c:1076-1077`) | §2.2 |
| `.border` | `+0x08` | ptr → 4×u16 | VERIFIED-SRC + VERIFIED-SYM (`LittlerootTown_Layout_Border` 0x083E9F5C → Blockdata 0x083E9F64 = 8 B) | §2.3 |
| `.map` | `+0x0C` | ptr | VERIFIED-SRC + IN-REPO | §2.2 |
| `.primaryTileset / .secondaryTileset` | `+0x10 / +0x14` | ptr | VERIFIED-SRC + IN-REPO (`main.c:927`) | §2.2 |
| `gBackupMapLayout` | `0x03005DC0` | 0x0C | VERIFIED-SYM + IN-REPO (`gamestate.c:38`) | §2.4 |
| `.width / .height / .map` | `+0x00 / +0x04 / +0x08` | s32,s32,ptr | VERIFIED-SRC + IN-REPO (`main.c:945-947`) | §2.4 |
| `sBackupMapData` (the target) | `0x02032318` | 0x5000 | VERIFIED-SYM | §2.4 |
| `struct MapConnections` `.count/.connections` | `+0x00 / +0x04` | s32, ptr | VERIFIED-SRC; size 8 VERIFIED-SYM | §3.1 |
| `struct MapConnection` `.direction/.offset/.mapGroup/.mapNum` | `+0x00 / +0x04 / +0x08 / +0x09`, **stride 0x0C** | 12 | VERIFIED-SRC + **stride VERIFIED-SYM** | §3.1 |
| `gMapGroups` | `0x08486578` | 34 ptrs (0x88) | VERIFIED-SYM + IN-REPO (`gamestate.c:125`) | §3.3 |
| `struct Tileset` `.isCompressed/.isSecondary/.tiles/.palettes/.metatiles/.metatileAttributes/.callback` | `+0x00/+0x01/+0x04/+0x08/+0x0C/+0x10/+0x14`, size 0x18 | 24 | VERIFIED-SRC; **size VERIFIED-SYM** | §4.1 |
| Field BG tile VRAM (global tile T) | `0x06000000 + 32*T`, T ∈ 0..1023 | 32 kB | VERIFIED-SRC (`overworld.c:286-294` + `fieldmap.c:798-807` + `bg.c:380-387`) | §4.3 |
| Hardware BG PALRAM | `0x05000000 + 32*slot + 2*idx` | 512 B | standard GBA | §4.6 |
| `gPlttBufferUnfaded` | `0x02037714` | 0x400 | VERIFIED-SYM + IN-REPO (`gamestate.c:57`) | §4.6 |
| `gPlttBufferFaded` | `0x02037B14` | 0x400 | VERIFIED-SYM | §4.6 |
| `gPaletteFade` | `0x02037FD4` | 0x0C | VERIFIED-SYM; **field layout NOT verified** | §1.3, §10.3 |
| `gSaveBlock1Ptr` | `0x03005D8C` | ptr | VERIFIED-SYM + IN-REPO (`gamestate.c:23`) | §6.1 |
| `SaveBlock1.pos.x/.y` | `sb1 + 0x00 / +0x02` | s16 | VERIFIED-SRC + IN-REPO (`gamestate.c:934-935`) | §6.1 |
| `SaveBlock1.location.mapGroup/.mapNum` | `sb1 + 0x04 / +0x05` | u8 | VERIFIED-SRC + IN-REPO (`gamestate.c:936-937`) | §6.1 |
| `gPlayerAvatar` | `0x02037590` | 0x24 | VERIFIED-SYM + IN-REPO (`gamestate.c:57`) | §6.2 |
| `.runningState / .tileTransitionState` | `+0x02 / +0x03` | u8 | VERIFIED-SRC | §6.2 |
| `.spriteId / .objectEventId` | `+0x04 / +0x05` | u8 | VERIFIED-SRC (`peersprite.h:112-113` has `+0x04`) | §6.2 |
| `gObjectEvents` | `0x02037350` | 0x240 = 16×0x24 | VERIFIED-SYM + IN-REPO (`gamestate.c:42`) | §6.3 |
| `.active` / `.invisible` / `.offScreen` / `.isPlayer` | byte `+0x00` bit 0 / `+0x01` bit 5 / `+0x01` bit 6 / `+0x02` bit 0 | bits | VERIFIED-SRC (`active` also IN-REPO, `main.c:3660`) | §6.3 |
| `.spriteId / .graphicsId` | `+0x04 / +0x05` | u8 | VERIFIED-SRC + IN-REPO (`peersprite.h:107-108`) | §6.3 |
| `.currentElevation / .previousElevation` | `+0x0B` low / high nibble | u8 | VERIFIED-SRC + IN-REPO (`fieldpath.c:139`, `main.c:3663`) | §6.3 |
| `.currentCoords.x/.y` | `+0x10 / +0x12` | s16 | VERIFIED-SRC + IN-REPO (`gamestate.c:965-966`) | §6.3.1 |
| `.previousCoords.x/.y` | `+0x14 / +0x16` | s16 | VERIFIED-SRC | §6.3 |
| `.facingDirection / .movementDirection` | `+0x18` low / high nibble | u8 | **VERIFIED-SRC** (was "verify-on-hw" at `gamestate.c:967`) | §6.3.1 |
| `.currentMetatileBehavior` | `+0x1E` | u8 | VERIFIED-SRC | §6.3 |
| `gSprites` | `0x02020630`, stride `0x44`, 64 + 1 | 0x1144 | VERIFIED-SYM + IN-REPO (`gamestate.c:57`, `peersprite.h:94`) | §6.4 |
| `.oam` | `+0x00` | 8 | IN-REPO (`peersprite.h:95`) | §9 |
| `.subspriteTables` | `+0x18` | ptr | VERIFIED-SRC | §9.2 G4 |
| `.x / .y` | `+0x20 / +0x22` | s16 | VERIFIED-SRC | §7.1 |
| `.x2 / .y2` | `+0x24 / +0x26` | s16 | VERIFIED-SRC | §7.1 |
| `.centerToCornerVecX / Y` | `+0x28 / +0x29` | s8 | VERIFIED-SRC | §7.1 |
| `.animNum / .animCmdIndex` | `+0x2A / +0x2B` | u8 | VERIFIED-SRC + IN-REPO (`peersprite.h:96`) | §9 |
| `.inUse / .coordOffsetEnabled / .invisible` | `+0x3E` bits 0/1/2 | bits | VERIFIED-SRC + IN-REPO (`peersprite.h:97`) | §9 |
| `.subspriteTableNum / .subspriteMode` | `+0x42` | u8 | IN-REPO (`peersprite.h:98`) | §9.2 |
| `gFieldCamera` | `0x03005DD0` | 0x18 | VERIFIED-SYM + IN-REPO (`gamestate.c:44`) | §7.2 |
| `.x / .y` | `+0x10 / +0x14` | s32 | VERIFIED-SRC (`include/field_camera.h:5-12`) + IN-REPO (`main.c:942-943`) | §7.2 |
| `gTotalCameraPixelOffsetX / Y` | `0x03005DEC` / `0x03005DE8` | u16 | VERIFIED-SYM (**note the X/Y order is the reverse of the declaration order**) | §7.1 |
| `gSpriteCoordOffsetX / Y` | `0x02021BBC` / `0x02021BBE` | s16 | VERIFIED-SYM + IN-REPO (`gamestate.c:38`) | §7.1 (diagnostic only — the diorama does **not** use it) |
| `gCamera` | `0x02037334` | 0x0C | **VERIFIED-SYM — NEW, not yet in `GameProfile`** | §7.4 |
| `.active / .x / .y` | `+0x00` bit 0 / `+0x04` / `+0x08` | bit, s32, s32 | VERIFIED-SRC | §7.4 |
| `REG_DISPCNT` | `0x04000000` | u16 | IN-REPO (`main.c:2161`, `peersprite.h:85`) | §6.5 |
| OBJ char VRAM | `0x06010000 .. 0x06017FFF` | 32 kB | IN-REPO (`peersprite.h:82-83`) | §9 |
| OBJ PALRAM (fallback) | `0x05000200` | 512 B | IN-REPO (`peersprite.h:84`) | §9 |

**Exactly one new `GameProfile` column is required: `gCamera` (0x02037334).** Everything else is
either already a column (`mapLayout`, `mapHeaderPath`, `mapObjects`, `fieldCamera`, `sprites`,
`plttUnfaded`, `playerAvatar`, `sb1ptr`, `mapGroupsRom`) or a constant offset from one.
`PROFILES[]` is **positional-initialised**, so appending is the only safe edit
(`source/gamestate.h:296`, `:274-279`).

### 10.2 Runtime assertions the implementer must add

Model them on the guards that already ship: `(ptr >> 24) == 0x02` for EWRAM
(`source/main.c:948`), `ft_rom_ptr` for ROM (`source/fieldtrav.c:1041`).

| # | Assert | Failure action |
|---|---|---|
| A1 | `(gBackupMapLayout.map >> 24) == 0x02` | gate off this frame |
| A2 | `1 <= backupW <= 271 && 1 <= backupH <= 270 && backupW*backupH <= 10240` | gate off (`MAX_MAP_DATA_SIZE`, `include/fieldmap.h:10`) |
| A3 | `backupW == layoutW + 15 && backupH == layoutH + 14` | gate off — the two structs have disagreed ⇒ a transition is in flight |
| A4 | `ft_rom_ptr(mapLayout)` and `ft_rom_ptr(primary)` and `ft_rom_ptr(secondary)` | gate off |
| A5 | `ft_rom_ptr(primary->metatiles / ->metatileAttributes)` and same for secondary | skip that tileset's ids (draw untextured) |
| A6 | metatile id `< 1024`; secondary index `< (attributes table length)` — unknowable at runtime, so bound by "the computed address is still ROM" | treat as VOID |
| A7 | `connections != NULL && 1 <= count <= 16 && ft_rom_ptr(connections->connections)` | no neighbour instancing |
| A8 | each connection: `direction ∈ [1,6]`, `mapGroup < 34`, `mapNum < 256`, `\|offset\| <= 512` | skip that connection |
| A9 | `gPlayerAvatar.objectEventId < 16` **and** `gObjectEvents[id].active` **and** `isPlayer` | gate off (the field is not assembled) |
| A10 | every object: `spriteId < 64` (`SPRITE_NONE` = 0xFF is excluded by this) | skip that billboard |
| A11 | `\|Sx(i) − Sx(player)\| < 16 * 512` and same for Z, after the `(s16)` cast | skip that billboard (a stale/torn sprite slot) |
| A12 | `\|gFieldCamera.x\| < 16 && \|gFieldCamera.y\| < 16` | clamp to 0 — the exact guard `main.c:944-945` already applies |
| A13 | `gCamera.x/y` magnitude `<= 512` when `active` | ignore the rebase, snap the camera instead |
| A14 | tile address `0x06000000 + 32*T` with `T < 1024` | refuse (would leave BG char VRAM) |
| A15 | palette slot `< 16`, colour index `< 16` | by construction from 4-bit fields |

### 10.3 What could still be wrong (open items, ranked)

1. **`gPaletteFade`'s field layout is NOT verified here.** §1.3/§4.6 need an "is a fade active"
   predicate; this document did not read `include/palette.h`. **Recommended workaround that needs
   no new fact:** compare `gPlttBufferFaded` (0x02037B14) against `gPlttBufferUnfaded` (0x02037714)
   over the 13 field palette slots (208 u16 = 52 `read32` pairs, or 13 if only slot heads are
   sampled) and treat "differs" as "a fade/weather tint is on". Verify the struct before using the
   flag directly.
2. **`struct MapConnection`'s internal offsets are VERIFIED-SRC, only the STRIDE is VERIFIED-SYM.**
   The 3-byte pad after `direction` and the 2-byte tail pad are ABI inference from the `s32`. A12/A8
   catch a wrong guess (directions outside 1..6, absurd offsets) — assert them and log once.
3. **The `gTotalCameraPixelOffsetX/Y` symbol order is reversed** relative to the declaration order
   in `src/field_camera.c:43-44` (Y is declared first, X has the *higher* address 0x03005DEC).
   §7.1's derivation does not use them, but anyone adding a scripted-pan term must not swap them.
4. **The player's object-event slot is assumed 0 in two shipped call sites**
   (`source/gamestate.c:964`, `source/fieldpath.c:139`). The diorama reads
   `gPlayerAvatar.objectEventId` instead. If a hardware run ever shows those two disagreeing, the
   shipped sites are the ones that are wrong.
5. **`RESEARCH-classification.md` §3.3's indoor id table is disproved** (§5.2) but **not replaced by
   a verified table for every indoor secondary** — only for `gTileset_GenericBuilding`, and only for
   the handful of objects §5.2.2 identified. v1 must therefore run the indoor branch on behaviour +
   collision + neighbourhood alone.
6. **Neighbour instancing beyond the free 7-tile skirt is unproven on real maps.** §3.4's origin
   formulas are re-derived from pret, but no Emerald map has been walked with them yet; the
   emulator screenshot gate (PHASE.md invariant 8) is where they get proven.
7. **`sprite->x/y` invariance (§7.1) is a proof from pret sources, not a measurement.** It should be
   the first thing the `tools/emutest` gdb channel checks: log `Sx(i) − Sx(player)` for a standing
   NPC across a full 16-frame player step and assert it changes by exactly ±16 and no more.
8. **32×32 object events exist and 64×64 subsprite objects exist**; §9.2 G4 refuses the latter.
   Nobody has counted how many Emerald NPCs that removes — measure before deciding it is acceptable.
