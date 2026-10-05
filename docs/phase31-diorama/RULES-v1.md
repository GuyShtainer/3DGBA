# Phase 31 — the v1 WORLD RULES, as validated on real maps (binding for the C module)

Written 2026-09-10 from `tools/diorama/dioref.py` (the Python reference model) after running it
over eight real Emerald maps dumped from ROM. **Where `SPEC-world.md` and this file differ, this
file wins for v1** — every rule here was looked at on a real map; the deviations from
`RESEARCH-classification.md` are the decisions D1–D3 recorded in `BUILDLOG.md`.

## 1. Inputs per map (all static, all in the DIOF fixture — see §5)

- layout `w × h`, the cell grid `u16[w*h]` (row-major, y grows SOUTH): id = `cell & 0x3FF`,
  collision = `(cell >> 10) & 3` (0 = passable), elevation = `cell >> 12`.
- both tilesets' metatile attributes `u16[512]` each: behaviour = `attr & 0xFF`, layer type =
  `(attr & 0xF000) >> 12` (unused by v1 classification).
- both tilesets' metatile tables `u16[512*8]` and 4bpp tiles `u8[512*32]` (for the VOID rule and
  the atlas), both palette tables `u16[16*16]` (BGR555).
- map type (`8` INDOOR / `9` SECRET_BASE ⇒ the indoor branch; everything else outdoor).
- the connection list: (direction 1 S / 2 N / 3 W / 4 E, offset, neighbour w/h, neighbour cells,
  tilesetsMatch). DIVE(5)/EMERGE(6) are skipped.

## 2. Instancing (RESEARCH §6, PHASE bound 5)

Instance 0 = the current map at origin (0,0). Each first-ring neighbour gets an integer origin:
north `(offset, −nh)`, south `(offset, h)`, west `(−nw, offset)`, east `(w, offset)`. World-cell
lookup = first instance whose bounds contain the point (instance 0 first); outside all instances
⇒ collision 0, id 0, class VOID. Neighbours are classified and meshed like the current map, but
their faces are TEXTURED only when `tilesetsMatch`; otherwise they get the untextured dark ground
(one flat colour), never the wrong art.

## 3. Classification — first match wins, evaluated per world cell

Classes (v1 enum, in this order): `VOID, FLAT, DECAL, LOW, LEDGE, WALL, ROOF, FURNITURE, BED,
TABLE, COUNTER, SIGN, WATER, VEG` (14). BED/TABLE/SIGN/DECAL are reserved: v1 never produces
them (D3) but the mesh recipes exist so a later id table can switch them on.

```
0. not inside any instance                                  -> VOID
INDOOR branch (map type 8 or 9):
1. metatile is BLANK: all 8 tile entries reference a tile whose 32 bytes are all zero  -> VOID
2. behaviour == MB_PC or MB_TELEVISION                        -> FURNITURE
3. behaviour in {MB_BOOKSHELF, MB_POKEMART_SHELF, MB_POKEMON_CENTER_BOOKSHELF, MB_LIBRARY_SHELVES} -> WALL
4. behaviour == MB_COUNTER                                    -> COUNTER
5. behaviour name contains DOOR (MB_ANIMATED_DOOR, MB_NON_ANIMATED_DOOR, MB_WATER_DOOR, MB_*_DOOR)  -> WALL
OUTDOOR branch:
6. behaviour in WATER set                                     -> WATER
     WATER = every MB_ whose name contains "WATER" or "CURRENT", plus MB_NO_SURFACING and
     MB_WATERFALL, MINUS MB_SHALLOW_WATER and MB_PUDDLE (walkable). Numeric values: SPEC-data.
7. behaviour in {MB_TALL_GRASS, MB_LONG_GRASS, MB_LONG_GRASS_SOUTH_EDGE, MB_ASHGRASS}  -> FLAT (never volume)
8. behaviour in {MB_JUMP_EAST, _WEST, _NORTH, _SOUTH, _NORTHEAST, _NORTHWEST, _SOUTHEAST, _SOUTHWEST} -> LEDGE
BOTH branches, solid cells only (collision != 0):
9. D1 VEG (outdoor only): the same metatile id occurs in a SOLID cell at (x, y+dy) for some
   dy in {-4,-3,-2,-1,+1,+2,+3,+4}                              -> VEG
10. collision at (x, y-1)                                     -> WALL
11. collision at (x, y+1)                                     -> ROOF
12.                                                           -> LOW
Passable cells, indoor only:
13. collision at (x,y-1) AND (x+1,y) AND (x-1,y)              -> WALL   (alcove)
14.                                                           -> FLAT
```
Polarity: "above" is y−1 (north, up-screen). Neighbour lookups cross instance borders.

## 4. Structures and heights

- BFS (explicit fixed queue, 4-neighbourhood) over cells classified WALL or ROOF, across all
  instances, seeded in row-major world order (y then x). VEG is NOT a structure member.
- Structure height = `min(HMAX=6, max over members of colwalk(member))` where
  `colwalk(x,y)`: count consecutive cells going NORTH from (x,y) while class ∈ {WALL, ROOF},
  counting the first ROOF met and stopping after it; minimum 1.
- Structure record: member count, bbox (minx, miny, maxx, maxy in world cells), height.
- Golden ordering: structures sorted by (miny, minx, maxy, maxx).
- Cutaway (render-time, not in the golden): a structure whose bbox `maxy > playerY + 0.5`
  renders at height 1 with its roof and north faces suppressed (RESEARCH §4.3).

## 5. Per-class box heights (tile units) and faces (RESEARCH §5 + D1)

| class | shape | height | top face art | side faces |
|---|---|---|---|---|
| FLAT | ground quad | 0 | own | — |
| DECAL | ground quad lifted 0.02 | 0 | own | — |
| WATER | ground quad recessed | −0.1 | own | — |
| LOW / LEDGE | box | 0.4 | own | untextured grey, culled against same class |
| COUNTER | box | 0.7 | own | untextured grey, culled against same class |
| FURNITURE | box | 1.0 | own | own art (level 0) |
| VEG | box | 2 | own | level 0 = own art, level 1 = art of row y−1; culled against VEG |
| WALL/ROOF (structure) | one volume per structure | structure height H | art of row (y − (H−1)) | level i = art of row (y − i); exterior faces only |
| VOID | nothing | — | — | — |

Face tints (unified fake sun): top 1.00, north 0.90, south 0.85, east 0.82, west 0.75.
Wall columns classified WALL/ROOF but NOT reached by any structure cannot exist (every WALL/ROOF
cell is a structure member by construction) — the reference's separate wall-column pass is
therefore folded into the structure pass.

## 6. Fixture and golden formats (tools/diorama/, all git-ignored, regenerable)

- `test/fixtures/dio_<map>.bin` — DIOF TLV (header documented in `diodump.py`).
- `test/fixtures/dio_<map>.golden.bin` — `'DIOG' u32 ver=1 | s32 w, s32 h | u8 class[w*h]`
  (instance 0, row-major, the enum above) `| u32 nStructs | {s32 minx,miny,maxx,maxy; u32 n;
  u32 height}[nStructs]` in the §4 order. The C suite must reproduce the class grid byte-for-byte
  and the structure table record-for-record.
- `test/fixtures/dio_<map>.golden.json` — the same plus the behaviour sets the model used
  (`behaviourSets`) and instance origins (`instances_origin`), for cross-checking constants.
- `test/fixtures/dio_<map>.atlas.bin` — `'DIOA' u32 ver=1 | 1024 × 256 u16` raw BGR555: every
  metatile composed (top layer over bottom, colour-0 = transparent on top, backdrop = primary
  palette 0 colour 0 on the bottom, palettes 0–5 from the primary table, 6–15 from the secondary
  table, tiles < 512 primary / ≥ 512 secondary, h/v flips). The C atlas composer must match
  byte-for-byte at this stage; texture-format conversion comes after.
- Regenerate: `python3 tools/diorama/diodump.py --rom ROM --map <Sym> --out … --atlas …` then
  `python3 tools/diorama/dioref.py test/fixtures/dio_<map>.bin --golden …` (needs
  `/tmp/pret/pokeemerald.sym` and `/tmp/pret/metatile_behaviors.h`). Fixtures present today:
  littleroot, oldaletown, petalburgcity, rustborocity, route101, battlefrontier_outsidewest,
  battlefrontier_battlearenalobby, littleroottown_brendanshouse_1f.
- A suite that cannot find a fixture SKIPs that test LOUDLY (prints SKIP + reason) and still runs
  every synthetic test; it never silently passes.
