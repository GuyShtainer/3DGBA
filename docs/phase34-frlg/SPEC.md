# PHASE 34 FRLG, implementation SPEC

Status: draft for lead review (2026-10-06). No C written; nothing committed. Contract: `PHASE.md`. Facts: `SURVEY.md`.

## 0. Coexistence with the in-flight Emerald work (S3.4-S3.8)

At the time of writing, S3.4 (`rg_rcanvas`, `rg_rprep`, `rg_rwrap`, `romgen_cli.c`, `test_romgen_relief_canvas*`) is
uncommitted in the tree, and S3.5-S3.8 are still to come. Phase 34 must neither block that work nor rebase it.

### 0.1 Files each side owns

| Files | S3.4-S3.8 (Emerald relief) | Phase 34 |
|---|---|---|
| `rg_rcanvas`, `rg_rprep`, `rg_rwrap`, `rg_rworld`, `rg_rsolve`, `rg_rcut`, `rg_rshape` (new) | **owns** | never edits. These are Emerald-only code paths; FRLG never calls them (section 7) |
| `rg_relief.{h,c}`, `rg_rtables.{h,c}`, `rg_rrock.h`, `rg_ralias.c`, `rg_rdrawn.c`, `rg_pyset.c` | **owns** (S3.7 edits `rg_relief.c`) | edits `rg_relief.c` / `rg_ledge.c` only in **L1, after S3.7 is committed** |
| `romgen_cli.c` | S3.4 (now), S3.8 | additive hunks only (new flags in their own functions, one `--game` line in `main`) in G1/B0 |
| `source/romgen_dev.c` | S3.8 (the "do not overwrite a full relief" check) | one additive hunk in G2 (per-game output dir). **Deferred to after S3.8** if S3.8 is not in yet |
| `rg_world.{h,c}`, `rg_art.{h,c}`, `rg_behavior.h`, `rg_roles.c`, `rg_signs.c`, `rg_buildings.c` (3 sites), `rg_run.{h,c}` (one opts field) | not planned to be touched by S3.5-S3.8 (checked against SPEC-S3 section 6) | **G1/G2** |
| `source/voxel/*`, `source/vx_host.c`, `source/gamestate.*` | never | **R0-R2, T1** |
| new: `source/romgen/rg_gameprof.{h,c}`, `source/romgen/rg_kspec*.c`, `tools/romgen/rg_author*.c`, `test/host/test_romgen_gameprof.c`, `test/host/test_romgen_frlg_*.c` | never | Phase 34 |

### 0.2 When each Phase 34 track may land

- **Renderer track (R0, R1, R2, T1's renderer half): any time**, including while an S3 slice is open. It touches no
  romgen file. Its one new romgen-directory file (`rg_gameprof.{h,c}`, created in R0) is new, so it cannot collide. It
  is pure data plus accessors that the existing `rg_*` code does not include until G1.
- **Romgen track (G1, G2, B0, K*): only at a seam between two S3 slices.** A seam means the previous S3 slice is
  committed with its BUILDLOG entry, and no S3 agent is running. This is the natural rhythm anyway under Guy's
  "one subagent at a time" rule. **Recommended: the S3.4/S3.5 seam** (right after S3.4 commits). G1 is one
  session, and every later S3 slice then starts from a tree where the profile exists. The S3 slices need no change:
  every Emerald-only identifier they use stays (`RG_NUM_PRIMARY`, `rg_is_water`, `rg_is_flat_behaviour`, `rg_is_sand`,
  `rg_behaviour`) with its exact current meaning (section 1.4). The fallback is the S3.8 seam, if the lead prefers
  Emerald relief closed first. G1 is written so either seam works.
- **L1 (Kanto ledges): after S3.7 is committed.** It edits `rg_relief.c` (layout selection), which S3.7 rewrites for
  FULL mode.
- **The device-hook hunk of G2: after S3.8.** Until then FRLG data is produced by the host CLI and copied to the SD
  card, which is the same PC-fallback path S3.8 documents.

### 0.3 The Emerald regression gate (hard, every G/B/K/L slice)

Before the slice starts, the agent records from the committed tree, with `ROMGEN_ROM` set to the **absolute** path of
`roms/emerald.gba`:
1. `tools/romgen/build/romgen $ROMGEN_ROM OUT --time` (plus `--relief full` once S3.7 exists): the SHA-1 of
   `regions.bin`, `signposts.bin`, `buildings.bin`, `relief.bin`.
2. `make -C tools/romgen test`: every suite's check count (none may print "skipped").
3. The renderer suites `test/host/test_voxel_*.c`, each built by the command line in its own file header (there is
   no make target for them yet; R0 adds `make -C tools/romgen vtest`, which runs them all): their check counts.

The slice is done only when all three are **identical** afterwards. The SHA-1s go into the slice's BUILDLOG entry,
before and after. The R slices use the renderer half: the `test_voxel_*` counts, plus one Azahar screenshot at a
fixed Emerald save-warp spot (Littleroot, `emerald-littleroot.sav`), pixel-diffed against the pre-slice capture.
The diff must be 0 apart from the sparkle particles, as in the S3.1 evidence.


## 1. The per-game profile

### 1.1 One struct, one file, both layers

New files `source/romgen/rg_gameprof.{h,c}`. They live in `source/romgen/` so that both build globs pick them up with
**no Makefile edit**: the device `SOURCES` includes `source/romgen`, and `tools/romgen/Makefile` and its test target
glob `rg_*.c`. They are pure C11 (`<stdint.h> <stdbool.h> <string.h>` only) and include nothing from romgen or the
renderer. The renderer includes them as `"../romgen/rg_gameprof.h"`. The terms are fixed: **game profile**, type
`GameProfile`, functions `gameprof_*`. No synonyms.

```c
typedef enum { GP_NONE = 0, GP_EMERALD, GP_FIRERED, GP_LEAFGREEN } GpGame;
typedef struct GpBehSet { uint32_t w[16]; } GpBehSet;          /* 512 bits: behaviour values 0..0x1FF */
static inline bool gp_beh(const GpBehSet *s, unsigned b) { return b < 512u && ((s->w[b >> 5] >> (b & 31u)) & 1u); }

typedef struct GameProfile {
    GpGame game; char code[4]; uint8_t rev;              /* header 0xAC code, 0xBC revision byte */
    const char *dataSubdir;                              /* "" Emerald (unchanged path); "BPRE" / "BPGE" */
    /* ---- ROM layer: romgen and renderer ---- */
    uint32_t mapGroups;                                  /* gMapGroups for this rev */
    uint8_t  groupCount;                                 /* 34 / 43 */
    const uint8_t *groupSizes;                           /* census pin: maps per group (43 numbers for FRLG) */
    uint32_t mapLayouts;                                 /* the known value, or 0 = runtime search (1.3) */
    uint16_t layoutSlots;                                /* 442 / 384 */
    uint16_t nPrimMetatiles, nPrimTiles; uint8_t nPrimPals;   /* 512,512,6 / 640,640,7 */
    uint16_t nMetatilesTotal;                            /* 1024 both */
    uint8_t  tilesetAttrOff, attrBytes;                  /* 0x10,2 / 0x14,4 */
    uint16_t behMask;                                    /* 0xFF / 0x1FF */
    uint32_t layerMask; uint8_t layerShift;              /* 0xF000,12 / 0x60000000,29 */
    uint8_t  layoutBytes;                                /* MapLayout size read and bounds-checked: 24 / 26 (border w,h) */
    uint32_t tsGeneral, tsBuilding;                      /* 0 = derive at runtime (1.3) */
    GpBehSet water, jump, houseDoor, sand, tallGrass, signpost;   /* romgen sets (section 2) */
    GpBehSet surfable, reflective, ice, shallowFlowing, furniture; /* renderer sets (section 2) */
    uint8_t  houseHalfWidth, houseHeight;                /* rg_roles is_house window: 5,7 / measured in G2 */
    /* ---- RAM and ROM anchors: renderer only (section 3) ---- */
    uint32_t gMain, sb1Ptr, backupLayout, mapHeader, objEvents, playerAvatar, sprites, plttUnfaded, paletteFade;
    uint8_t  playerAvatarBytes;                          /* 0x24 / 0x20 */
    uint32_t weather; uint32_t weatherPtr;               /* Emerald: weather direct; FRLG: weather = 0, weatherPtr = ROM const */
    uint16_t weatherOff[5];                              /* curr, palState, eva, fogH, fogD */
    uint32_t gfxInfoPtrs; uint16_t gfxInfoCount;         /* 239 / 152 (+ the VAR ids, see 3.2) */
    uint32_t fldeffTemplates; uint8_t fldeffCount;       /* 37 / 36 */
    uint32_t cb2Overworld, cb2OverworldBasic;            /* thumb bit set */
    /* ---- per-game tables ---- */
    const int16_t *treePart; uint16_t treePartCount;     /* renderer tree table (T1); NULL = no tree sprites */
    const int16_t *treeGround;                           /* ground replacement per tree part */
    bool emeraldIdTables;                                /* Fortree puddles, GenericBuilding interior ids: Emerald only */
    bool interiors3d;                                    /* false on FRLG: indoor maps hand back to the 2D frame */
    const struct RgSpec *specs; unsigned nSpecs;         /* building recipe table (romgen) */
} GameProfile;

const GameProfile *gameprof_detect(const uint8_t *rom, size_t size);  /* code + rev; NULL = unsupported */
const GameProfile *gameprof_emerald(void);
```

`rg_gameprof.c` holds three const rows (BPEE, BPRE rev1, BPGE rev1). The `specs` pointers are filled through an
`extern` table in `rg_bspecs.c` (Emerald, unchanged) and in `rg_kspecs.c` (Kanto, new). In the device build the
renderer never dereferences `specs`, and the linker drops nothing it uses.

### 1.2 Emerald stays byte-identical: three rules

1. **The Emerald row is built from the existing macros, so it cannot drift**: `.mapGroups = GBA_ADDR_MAP_GROUPS`,
   `.gMain = GBA_ADDR_GMAIN`, `.nPrimMetatiles = NUM_METATILES_IN_PRIMARY` and so on. The macros in `gba_game.h`
   stay, documented as "the Emerald row". The Emerald behaviour bitsets are generated at first use from the existing
   predicates (`rg_is_water`, `rg_is_jump`, `rg_is_house_door`, and the renderer's `MetatileBehavior_*`), never
   hand-typed. A host test, `test_romgen_gameprof.c`, asserts for **every b in 0..511** that the bitset equals the predicate,
   and for every Emerald field that it equals its macro.
2. **The Emerald code paths keep their old identifiers and meanings.** `RG_NUM_PRIMARY` stays `512u` and is used only
   by Emerald-only code (`rg_ralias.c`, and the relief S3 modules). `rg_is_water()`, `rg_is_flat_behaviour()`,
   `rg_is_sand()` and `rg_behaviour()` keep their Emerald semantics for the S3 modules. Generic code switches to the
   profile accessors of 1.4. So S3.5-S3.8 can be written against today's API without knowing about Phase 34.
3. **Golden outputs.** Section 0.3: the SHA-1s of the four files plus every suite count, before and after, identical.

One renderer-side hazard the test must close: R2 widens `UNPACK_BEHAVIOR` from `0xFF` to the profile's `0x1FF`.
For Emerald that is a no-op **only if** attribute bits 8 and 9 are zero in every Emerald metatile. R0 asserts this on
the user's Emerald ROM, over every attribute of every tileset (`test_romgen_gameprof.c`, real-ROM part). If it fails, the
Emerald row keeps mask `0xFF` and FRLG packs differently (1.5).

### 1.3 What is derived at runtime instead of stored

- **`gMapLayouts`** (SURVEY 1.1): read header (3, 0)'s layout pointer and its `layoutId`. Search the ROM for that u32
  pointer, aligned to 4, so that `table + 4*(id-1)` holds it. Then validate with the all-headers rule: for every map
  header, `rd32(table + 4*(layoutId-1)) == header.layout`. FR rev 1 must find 0x0834EBFC, LG rev 1 0x0834EBDC,
  each consistent for all 309 referenced ids (pinned in the test). The profile still stores the measured value; the
  search confirms it and is the path for an unknown rev. Cost: one 16 MB scan per world open on the host (~10 ms).
  On the device, skip the scan when the stored value validates.
- **General and Building primaries**: the General tileset is the primary of the layout of header (3, 0). That is a
  test pin, not a heuristic: FR 0x082D4B04 / LG 0x082D4AE4, used by 181 layouts. The Building primary is the primary
  of header (4, 0)'s layout: FR 0x082D4C24 / LG 0x082D4C04. Both values are stored, and also asserted.
- **`gBackupMapData`** (renderer): not stored. It is the `map` pointer held at `backupLayout + 8` at runtime, accepted
  only if it lies in EWRAM and `width * height * 2 <= 0x5000`.

### 1.4 romgen changes (slice G1), site by site

| Site | Now | After G1 |
|---|---|---|
| `rg_world_open` gate (`rg_world.c:495-501`) | `memcmp(rom+0xAC, "BPEE")`, then `GBA_ADDR_*` | `w->prof = gameprof_detect(rom, size)`. NULL means `RG_ERR_GAME` (renamed from `RG_ERR_NOT_BPEE`; the old enum value is kept as an alias, so the CLI's exit codes do not move). Table addresses come from the profile |
| `RgWorld.groupStart[34], groupCount[34]` | fixed 34 | `[RG_MAX_GROUPS 64]`, loops to `prof->groupCount` |
| `read_layout` bounds (`GBA_ROM_MAPLAYOUT_BYTES`) | 24 | `prof->layoutBytes` |
| `read_tileset` attrs offset `s + 0x10` | fixed | `s + prof->tilesetAttrOff` |
| uncompressed tiles cap `512u * 32u` | fixed | `prof->nPrimTiles * 32u` for a primary, `(1024 - nPrimTiles) * 32u` for a secondary (Emerald: 512 either way, unchanged) |
| metatile count rule `<= 512`, default 512 | fixed | `<= prof->nPrimMetatiles` for primaries, `<= nMetatilesTotal - nPrimMetatiles` for secondaries; default = that cap (Emerald: 512 and 512) |
| `ptr_ok(attr, count * 2)` | 2 B | `count * prof->attrBytes` |
| `rg_attr` returns `uint16_t`, split at `RG_NUM_PRIMARY` | | returns `uint32_t`, split at `L->prof->nPrimMetatiles`, reads `attrBytes`. `RgLayout` gains `const GameProfile *prof` (set in `read_layouts`) |
| `rg_behaviour` `& 0xFF`, returns `uint8_t` | | `& L->prof->behMask`, returns `uint16_t`. Callers that store it in a `uint8_t` (`rg_ledge.c` x4, `rg_roles.c`, `rg_rcanvas.c`) stay correct on both games: the largest FRLG value used by any Kanto layout is 0xE0 (M1). G1 adds `-Wconversion` for those files in the test build and widens only the generic ones |
| `rg_metatile_entries`, `rg_tileset_addr_of` (`< 512`) | | `< L->prof->nPrimMetatiles` |
| `rg_art.c` palette split `pal < 6u` (2 sites), tile split `tile < 512u` (2 sites) | | `pal < p->prof->nPrimPals`, `tile < p->prof->nPrimTiles`. `RgPair` gains `prof` |
| `rg_buildings.c:657,749,1113` (`>= RG_NUM_PRIMARY`) | | `>= E->prof->nPrimMetatiles` (generic placement code that Kanto recipes go through) |
| `rg_roles.c` behaviour tests (6 sites), `HOUSE_HALF_WIDTH/HEIGHT` | Emerald predicates and constants | `gp_beh(&L->prof->water, b)` etc.; `L->prof->houseHalfWidth/Height` |
| `rg_signs.c` sign rule (kind <= 4) | | unchanged (same in FRLG, SURVEY 1.2) |
| `rg_run` | Emerald specs | `rg_build_models` iterates `w->prof->specs` (Emerald: `rg_specs`, the same table object, so the same order and bytes) |
| `rg_ralias.c`, the relief S3 modules | `RG_NUM_PRIMARY`, Emerald predicates | **unchanged** (Emerald-only, section 7) |

`rg_rtables_check` already refuses a non-Emerald world (T1 dimensions). `rg_relief_build` must not even reach it on
FRLG. G1 adds one line at the top of `rg_run`'s relief call: if `w->prof->game != GP_EMERALD`, the relief mode is
forced to OFF until L1. That line sits in `rg_run.c`, not in `rg_relief.c`.

### 1.5 Renderer changes (slices R0-R2)

The renderer reads a process-wide `const GameProfile *gVxProf`, set by `vx_host.c` `Rebind()` from
`gameprof_detect()`. The vendored files change from `GBA_ADDR_X` to `VXP(x)` (a macro for `gVxProf->x`). There are
about 65 sites: `gba_game.h` 25, `vx_snapshot.c` 13, `voxel_atlas.c` 13, `vx_adapter.c` 12, `voxel_world.c` 2, by grep.
`gba_game.h` keeps the Emerald macros, which romgen still uses until G1 and which the Emerald row is built from.

- **Snapshot** (`vx_snapshot.c`): every copy source address and size from the profile (`playerAvatarBytes` 0x20 on
  FRLG). Weather: on FRLG resolve `weather = rd32(weatherPtr)` once per bind, then check it lies in EWRAM.
- **Tileset intern** (`vx_adapter.c:227-279`): FRLG attributes are u32 in ROM. Convert on intern into a host u16 array
  that the vendored code reads unchanged: `out = (a & 0x1FF) | (((a >> 29) & 3) << 12)` (behaviour 9 bits; layer type
  into bits 12-13, where Emerald keeps its layer type). Memory: 2 B x 1024 per interned tileset. Emerald keeps the
  zero-copy ROM pointer. `UNPACK_BEHAVIOR` becomes `& VXP(behMask)`.
- **Primary splits**: `NUM_TILES_IN_PRIMARY` / `NUM_METATILES_IN_PRIMARY` / `NUM_PALS_IN_PRIMARY` become profile reads
  in `voxel_atlas.c` and `vx_adapter.c`. `NUM_METATILES_TOTAL` 1024 and `MAP_OFFSET` 7 are the same in both games.
- **Border**: `vx_adapter.c:302` and `voxel_world.c:865` read 4 border cells. FRLG has 2x2 (331 layouts), 3x2 (7) and
  0x0 (28, all indoor) borders. Read `w = layout[0x18], h = layout[0x19]`; for 0x0 use metatile 0 (indoor only, and
  never rendered on FRLG v1, 1.6).
- **Emerald-only id tables** (Fortree puddle range 0x288-0x29A, the GenericBuilding interior ids, the tree table):
  guarded by `VXP(emeraldIdTables)` or by the profile's `treePart` table (T1).
- **Connections**: `vx_adapter.c:413` checks `group < GBA_MAP_GROUP_COUNT`; it becomes `VXP(groupCount)`.

### 1.6 Game detection, data directory, indoor hand-back (R2)

- `vx_host.c`: `sBpee` becomes `sGame` (`GpGame`). Detection is `gameprof_detect` plus the anchor self-check of
  section 3.3. The status string "Voxel 3D: Emerald only" becomes "Voxel 3D: Emerald, FireRed, LeafGreen (rev 1)".
  The not-detected reason (wrong game, wrong rev, or failed anchor N) goes to `voxel.log`.
- **Data directory**: loose files have no ROM pin (Phase 33 PHASE.md), so two games must not share a directory.
  Emerald keeps `sdmc:/3ds/3DGBA/voxel/` (no move, no migration). FRLG uses `sdmc:/3ds/3DGBA/voxel/BPRE/` and
  `.../BPGE/`, set by `vx_data_set_paths(NULL, dir)` in `Rebind`. romgen's device hook writes to the same per-game
  directory (G2, after S3.8). The host CLI writes wherever it is told.
- **Indoor and cave maps on FRLG**: the voxel path hands the frame back to the normal 2D GBA output whenever
  `mapType` (snapshot of `gMapHeader + 0x17`) is not one of {1, 2, 3, 5, 6}. On Emerald the path is unchanged
  (`interiors3d = true`). The switch happens during the door fade, so no pop should be visible. This is checked in
  Azahar at M0 on the Pallet house doors.


## 2. FRLG behaviour table

Sources: the Emerald values are the current code (`rg_behavior.h`, `vx_behavior.c`, `gba_game.h`). The FRLG values
are pokefirered@037335f `include/constants/metatile_behaviors.h` (numbers only), each **cross-checked against cell
counts on FR rev 1** (M1, every cell of every layout; SURVEY 2.2). The 9-bit mask is 0x1FF; the largest value any Kanto
layout uses is 0xE0. Every set is a `GpBehSet` in the FRLG row. Every row of this table becomes a `test_romgen_gameprof.c`
assertion, plus one ROM assertion where the ROM can witness it (last column). A PROVENANCE row covers the whole table.

| Set (`GameProfile` field) | Used by | Emerald (unchanged) | FRLG rev 1 | ROM assertion (FR and LG) |
|---|---|---|---|---|
| `water` | rg_roles (water role), regions | 0x10-0x17, 0x19, 0x22, 0x28, 0x2A, 0x2B, 0x50-0x53 | **0x10, 0x11, 0x12, 0x13, 0x15, 0x16, 0x17, 0x19, 0x1B, 0x22, 0x28, 0x50-0x53** (no 0x14; **0x2A rock stairs and 0x2B sand cave are NOT water**; 0x1B cycling-road water added) | Pallet's pond cells (0x123, 0x12A-0x12C, 0x2D1, 0x2D2) carry 0x15; census 0x15 x40 507, 0x1B x751; no 0x2A/0x2B cell on an outdoor layout is classified water |
| `jump` (ledges) | rg_roles, rg_ledge (L1) | 0x38-0x3F | **0x38, 0x39, 0x3A, 0x3B** (E, W, N, S; no diagonals) | census 0x38 x41, 0x39 x46, 0x3B x1022, 0x3A x0 |
| `houseDoor` | rg_roles is_house | 0x69, 0x8B, 0x8D | **0x69 only** (0x8B is a dresser, 0x8D the cable-club monitor in FRLG) | Pallet doors (6,7), (15,7), (16,13) are 0x69; 116 outdoor 0x69 warps lead indoors |
| `sand` | relief / props | 0x06, 0x21, 0xBF | **0x21** | census 0x21 x2 878 |
| `tallGrass` | renderer grass, props | 0x02, 0x03, 0x07, 0x09 | **0x02** | census 0x02 x5 701 |
| `signpost` (new, cross-check only) | rg_signs test | none | **0x84** | every sign BgEvent in Pallet (5) lands on a 0x84 cell (mailboxes 0x2AD, fence signs 0x002, post 0x003); the Kanto-wide hit rate is pinned in G2 |
| `surfable` | `MetatileBehavior_IsSurfableWaterOrUnderwater` | 16 values incl. 0x14, 0x2A, 0x6C, 0x6D, 0x6F | **0x10-0x13, 0x15, 0x19, 0x1B, 0x22, 0x50-0x53** | 0x2A/0x6C/0x6D/0x6F are rock stairs and stair warps in FRLG: must be excluded |
| `reflective` | `IsReflective` | 0x10, 0x14, 0x16, 0x1A, 0x20, 0x2B | **0x10, 0x16, 0x23** | ice 0x23 x189 (Seafoam, a cave, so never outdoor; harmless) |
| `ice` | `IsIce` | 0x20 | **0x23** (FRLG 0x20 is the strength button) | |
| `shallowFlowing` | `IsShallowFlowingWater` | 0x17, 0x1B, 0x1C | **0x17** | |
| `furniture` | PC / counter / TV props (interiors) | 0x80, 0x83, 0x86, 0xB0, 0xB1, 0xC5 | **0x80, 0x83, 0x86** (0x84 is the signpost; 0xB0/0xB1/0xC5 do not exist). Unused on FRLG v1 (interiors are 2D) | |
| flat (relief) | Emerald full relief only | rg_is_flat_behaviour (24 values) | not in the profile. L2, if built, defines its own Kanto set: 0x60, 0x69, 0x6A-0x6F | |

Kanto-specific facts the generators must respect:
- **There are no bridge or log behaviours.** M1 histograms of Routes 12, 13, 24 (Nugget Bridge) and 25 show only 0x00
  (walkable or blocked), 0x15 sea, 0x02 grass, ledges and a few 0x84/0x69/0x2A cells. So bridge decks are behaviour
  0x00 cells over 0x15 water, and the water role never claims them. Sea water 0x15 is collision-**walkable** (surf),
  like Emerald's. G2 pins Route 24's regions bytes and Azahar checks that the deck does not sink.
- **Door cells are collision-blocked in FRLG.** All three Pallet doors (metatiles 0x2A3, 0x2AC) have the collision
  bits set, and so do the 0x69 cells on Routes 12 and 25. A door is entered through its warp event, not by walking
  onto a passable cell. Any FRLG code that searches for "the walkable door cell" (`rg_roles` is_house) must not
  require walkability. G2 adds a test on Pallet's three doors.
- **Kanto doors include arrow warps.** Route gates and some huts are entered over 0x62-0x65 arrow mats or plain 0x00
  cells, not 0x69. The building **census** (section 5) therefore finds buildings by *warp to an indoor map*
  (structural), not by behaviour. The regions `is_house` role keeps `houseDoor`. That is enough for regions, because
  every gate also gets its own recipe.
- Cycling road 0xD0/0xD1 and mountain top 0x0C are plain ground for every generator.


## 3. Anchors table (verified vs to-harvest)

### 3.1 The table

Status keys:
- **V-ROM**: measured on Guy's FR/LG rev 1 ROMs (M1, SURVEY).
- **V-GS**: already in `source/gamestate.c` for BPRE/BPGE, verified there (pret sym or live).
- **V-PRET**: a pret@037335f number for a ROM-independent fact (struct offset, enum value). It is asserted at runtime
  where possible.
- **H**: to harvest in slice R1.

The harvest method is the Phase 32 BUILDLOG-P2 method (pret's `sym_*.txt` give section order only, not addresses):
find the function by its known neighbours in the ROM, read the literal-pool constant, and confirm it live in Azahar.
LG is harvested **on LG itself**. Copying a FireRed ROM address to LeafGreen is the documented BPGE failure mode
(`gamestate.c:434`, `:554`).

| Anchor (`GameProfile` field) | Emerald | FR rev 1 | LG rev 1 | Status | Runtime self-check (3.3) |
|---|---|---|---|---|---|
| `gMain` | 0x030022C0 | 0x030030F0 | 0x030030F0 | V-GS (rev 0 = rev 1) | callback2 is a thumb ROM pointer |
| gMain+4 callback2, +0x439 bit 1 in-battle | same | same offsets | same | V-PRET | |
| `sb1Ptr` | 0x03005D8C | 0x03005008 | 0x03005008 | V-GS | the pointer is in EWRAM |
| `backupLayout` (12 B) | 0x03005DC0 | 0x03005040 | 0x03005040 | V-GS | width = layout w + 15, height = h + 14 |
| gBackupMapData | 0x02032318 | derived (1.3) | derived | live pointer | in EWRAM, size <= 0x5000 |
| `mapHeader` (0x1C B) | 0x02037318 | 0x02036DFC | 0x02036DFC | V-GS (pret sym, rev 0 = rev 1) | +0x12 layoutId equals the ROM header's layoutId for sb1's (group, num) |
| `objEvents` (16 x 0x24) | 0x02037350 | 0x02036E38 | 0x02036E38 | V-GS (resolved: memory note 3dgba-gamestate-logger) | the player object's current coords = sb1 pos + 7 |
| `playerAvatar` | 0x02037590 (0x24 B) | 0x02037078 (**0x20 B**) | 0x02037078 | V-GS; size V-PRET | +5 objectId < 16 and that object has the isPlayer bit |
| `sprites` (65 x 0x44) | 0x02020630 | 0x0202063C | 0x0202063C | V-GS | the player sprite's template pointer is a ROM pointer |
| `plttUnfaded` | 0x02037714 | 0x020371F8 | 0x020371F8 | V-GS | |
| `paletteFade` | 0x02037FD4 | H | H | H: `BeginNormalPaletteFade` literal pool | y field in 0..16 |
| weather (direct) / `weatherPtr` | 0x02038454 direct | H: the ROM const that holds the EWRAM address (pret: FRLG reaches the struct through a const pointer) | H | H | current id in 0..14 (ids identical, V-PRET) |
| `weatherOff[5]` (curr, palState, eva, fogH, fogD) | 0x6D0, 0x6C6, 0x730, 0x6FB, 0x724 | H (different struct layout) | same as FR (same code; asserted) | H: accessor immediates, as for Emerald | palState in 0..3 |
| `gfxInfoPtrs` + `gfxInfoCount` | 0x08505620, 239 | H; 152 base ids V-PRET (plus the 240+ VAR ids) | H | H: find by content (152 consecutive ROM pointers to 36-B records whose +0x1C images pointer is a ROM pointer) | record offsets 0x06/0x08/0x0A/0x1C identical (V-PRET); the player's graphicsId resolves |
| `fldeffTemplates` + count | 0x085059F8, 37 | H; 36 entries V-PRET | H | H: content search (36 sprite-template pointers) | the indices the renderer uses (0, 3, 4, 5, 7, 11, 15, 23, 27) have the same meaning in FRLG (V-PRET) |
| `cb2Overworld`, `cb2OverworldBasic` | 0x08085E5D / 0x08085E51 | H | H | H: gamestate's field fingerprint lists already log raw callback2 on the overworld (memory note: game-state logger); read it while walking in Pallet, then confirm in the ROM | equals callback2 when sb1 location is an outdoor map |
| `mapGroups` | 0x08486578 | 0x08352718 | 0x083526F8 | V-ROM (rev 0: 0x083526A8 / 0x08352688 from `gamestate.h:423`, not measured) | 43 group pointers in ROM; the group sizes pin |
| `mapLayouts` | 0x08481DD4 | 0x0834EBFC | 0x0834EBDC | V-ROM, also runtime search (1.3) | all-headers rule, 309 ids |
| `tsGeneral` | 0x083DF704 | 0x082D4B04 | 0x082D4AE4 | V-ROM | = primary of header (3, 0)'s layout |
| `tsBuilding` | 0x083DF884 | 0x082D4C24 | 0x082D4C04 | V-ROM | = primary of header (4, 0)'s layout |
| group count / maps / layout slots | 34 / 518 / 442 | 43 / 425 / 384 (18 NULL, 309 referenced) | same | V-ROM | pinned in `test_romgen_frlg_world.c` |
| Fortree, GenericBuilding (Emerald ids) | 0x083DF7C4, 0x083DFB6C | none | none | n/a | `emeraldIdTables = false` |

### 3.2 Provenance and the VAR graphics ids

FRLG object graphics ids 240 and up are VAR ids (resolved through script variables at runtime, V-PRET); the renderer
resolves them the way it does on Emerald, with the FRLG base `gfxInfoCount` 152 as the bound of the direct range.

Each harvested value gets a PROVENANCE row: "ROM-measured (literal pool at 0x08xxxxxx), confirmed live in Azahar on
<date>". When the method was a pret sym lookup, the row says that instead, with the commit pin.

### 3.3 The runtime self-check (R1)

`vx_host.c` `Rebind()` runs the **ROM checks** once per bind. They are cheap: the header and code, the mapGroups
probe, the mapLayouts validation, and the General/Building primaries. The **RAM checks** run on the first overworld
frame after a bind, and again after every map change (`sb1` location differs from the previous frame's). Any failure
sets `sGame = GP_NONE` with a numbered reason (`voxel.log`: `vx: anchor 7 (backupLayout width) failed: 0x...`). The
frame then shows 2D. A failed check never retries in a loop: one log line per bind. This is what makes a rev 0 cart or
a ROM hack safe: it gets 2D, not a crash.


## 4. Slices

Every slice ends green before the next one starts:
- `make -C tools/romgen test` passes under ASan/UBSan, the device `make` links, and `make -C tools/romgen vtest`
  passes once R0 adds it.
- The **Emerald regression gate (0.3)** is identical before and after. The R slices use its renderer half.
- The BUILDLOG-P34 entry holds the commands and their outputs, with nothing paraphrased.
- PROVENANCE.md has a row for every table the slice adds.
- No reference code was executed.

Line counts are C plus tests and are rough. The S2 estimates landed within about 25 %. The **track** column says which
landing rule of 0.2 applies.

| Slice | Lines (C + test) | Track | Depends on | Milestone |
|---|---|---|---|---|
| R0 profile plumbing, Emerald only | ~350 + 150 | renderer (any time) | none | M0 |
| R1 anchor harvest + self-check | ~200 + 100 (+ the harvest session) | renderer | R0 | M0 |
| R2 FRLG renderer: tileset intern, borders, hand-back, data dir | ~400 + 150 | renderer | R1 | **M0** |
| G1 romgen profile + FRLG world open | ~450 + 250 | romgen (S3 seam) | R0 | M1 |
| G2 FRLG art, regions, signposts | ~250 + 200 | romgen (S3 seam; device hunk after S3.8) | G1 | M1 |
| T1 Kanto trees | ~150 + 100 (host tool) + 80 | renderer table + host tool | R2, G2 | M1 |
| B0 authoring toolchain + FRLG buildings plumbing | ~900 + 150 (+ 150 test) | romgen (S3 seam) | G2 | M1 |
| **K1 Pallet** (house, lab) | ~470 | romgen | B0 | **M1** |
| K2 landmarks (Center, Mart, Gym) | ~450 | romgen | K1 | M2 |
| L1 Kanto ledges | ~250 + 150 | romgen (**after S3.7**) | G2 | M2 |
| K3-K11 mainland towns | ~5 580 (section 6) | romgen | K2 | M3 |
| KS1-KS3 Sevii | ~1 700 | romgen | K11 | M4 |
| V1 review + release audit | docs | none | all | M5 |
| L2 drawn Kanto relief (optional) | ~800 + 200 | romgen (after S3.8) | L1 | after M3 |

Total, without L2: about 12 600 lines, of which about 8 200 are recipes (K1-KS3). The S2 recipe density was about 70 lines per
Emerald direct model, and Kanto models are of the same kind.

### R0 Profile plumbing, Emerald only: ~350 + 150

- **Files**: `source/romgen/rg_gameprof.{h,c}` (new: the struct, the BPEE row, empty BPRE/BPGE rows with `game =
  GP_NONE` so that detection still refuses them); `source/voxel/gba_game.h` (comments only: "the Emerald row");
  `vx_snapshot.c`, `voxel_atlas.c`, `vx_adapter.c`, `voxel_world.c` (`GBA_ADDR_X` → `VXP(x)`, about 65 sites);
  `source/vx_host.c` (`sBpee` → `sGame`, `gVxProf`); `tools/romgen/Makefile` (a `vtest` target that builds and runs every
  `test/host/test_voxel_*.c` with its header's command line); `test/host/test_romgen_gameprof.c`.
- **Functions**: `gameprof_detect`, `gameprof_emerald`, `gp_beh`, the lazy builder of the Emerald bitsets from the
  existing predicates.
- **Tests**: Emerald field = macro for every field; bitset = predicate for b in 0..511, for each of the 11 sets;
  detect refuses a buffer with the code BPRE (the rows are still empty), a short buffer and a zero header; real ROM:
  the bits 8-9 hazard of 1.2 over every attribute of every Emerald tileset.
- **Done-gate**: renderer half of 0.3. The `test_voxel_*` counts are identical, and the Littleroot Azahar capture
  diffs to 0 apart from the sparkle. The device build links. **No Emerald behaviour changes, because there is no FRLG
  row yet.**

### R1 Anchor harvest and self-check: ~200 + 100

- **Session work** (BUILDLOG-P34, no code): harvest every H row of section 3, **on FR and then separately on LG**,
  using the Phase 32 literal-pool method plus a live Azahar read (FR save-warped into Pallet). Each value gets its own
  PROVENANCE line.
- **Files**: `rg_gameprof.c` (fill both rows' anchors; `game` stays `GP_NONE` until R2), `source/vx_host.c` (the
  self-check of 3.3, numbered reasons), `test_romgen_gameprof.c`.
- **Functions**: `vx_anchor_check_rom(prof, rom)`, `vx_anchor_check_ram(prof)`, both returning the first failing
  check number or 0.
- **Tests**: the ROM checks on both of Guy's ROMs (FR, LG) with the real-ROM env vars `ROMGEN_ROM_FR` and
  `ROMGEN_ROM_LG`, which skip cleanly when unset; LG's row differs from FR's in every ROM address the SURVEY lists as
  differing (a test that fails if someone copies FR's value); synthetic RAM images for the RAM checks (one per check,
  each corrupting exactly one field).
- **Done-gate**: Emerald renderer half of 0.3 identical. In Azahar with FR and with LG, the voxel.log line
  `vx: anchors ok (BPRE rev 1)` / `(BPGE rev 1)` while still rendering 2D (the rows are disabled until R2).

### R2 The FRLG renderer path → M0: ~400 + 150

- **Files**: `vx_adapter.c` (u32→u16 attribute intern, border w/h, primary splits, group count), `voxel_atlas.c`
  (primary splits), `vx_snapshot.c` (sizes, `weatherPtr`), `voxel_world.c` (border), `vx_host.c` (detection enabled,
  per-game data dir, indoor hand-back, status string), `source/settings*`/`ui` (only the status string),
  `rg_gameprof.c` (set `game` on the FRLG rows), `test/host/test_voxel_adapter.c` (extend).
- **Functions**: `vx_intern_attrs32(src, n, out)`, `vx_profile_data_dir(prof)`, `vx_map_is_outdoor(mapType)`.
- **Tests**: intern known answers (Pallet door metatile 0x2A3's attribute → behaviour 0x69, its layer bits;
  synthetic values with bits 29-30 set); a 0x0 border yields metatile 0 and a 3x2 border tiles correctly; on FR rev 1,
  `vx_adapter` builds the Pallet world from the real ROM in the host fixture (`VOXEL_HOST_FILES`) with no data files.
- **Done-gate**: **M0 in Azahar on FR and on LG**: Pallet in voxel mode with no data files, boxes for buildings, Oak's
  Lab entered and left (2D inside, 3D outside, no garbage frame at the fade), Route 1 walked to Viridian (a map
  connection). Emerald renderer half of 0.3 identical. Then the first FRLG hardware check, which can ride along with
  M1's hardware run.

### G1 romgen profile and FRLG world open: ~450 + 250

- **Lands at an S3 seam (0.2).** It is one session.
- **Files**: `rg_world.{h,c}`, `rg_art.{h,c}`, `rg_behavior.h` (Emerald predicates stay; a comment points to the
  profile), `rg_roles.c`, `rg_buildings.c` (3 sites), `rg_run.{h,c}` (specs from the profile; relief OFF on non-Emerald),
  `romgen_cli.c` (one line: print the detected game), `rg_gameprof.c` (FRLG romgen sets of section 2),
  `test/host/test_romgen_frlg_world.c`, `test/host/rg_fixture.h` (FR/LG ROM env vars).
- **Functions**: every site of 1.4; `rg_find_map_layouts(w)` (the runtime search of 1.3).
- **Tests** on FR and on LG, as census pins: 43 groups with the 43 group sizes; 425 maps; 384 layout slots, 18 NULL,
  309 referenced; 63 tilesets; 76 outdoor maps (types 1, 2, 3, 5, 6); 1294 warps; the gMapLayouts search finds
  0x0834EBFC / 0x0834EBDC and validates all headers; General and Building primaries as in section 3; every FRLG
  tileset's attribute count fits its cap; `rg_attr` of Pallet (6,7) = a u32 whose behaviour is 0x69; FR and LG
  blockdata are byte-identical in all 384 layouts (the fact that makes one recipe table serve both games).
- **Done-gate**: **full 0.3 gate** (the SHA-1s of the four Emerald files and every suite count). `romgen firered.gba
  OUT` opens the world and writes nothing yet (buildings/regions/signposts are disabled for FRLG until G2) with exit
  code 0.

### G2 FRLG art, regions and signposts: ~250 + 200

- **Files**: `rg_art.c` (7 primary palettes; the FRLG tile split), `rg_roles.c` (`is_house` window from the profile;
  door cells may be blocked, see section 2), `rg_run.c` (enable regions + signposts on FRLG), `rg_gameprof.c`
  (`houseHalfWidth/Height` measured here), `source/romgen_dev.c` (per-game output dir: **this hunk only after S3.8**),
  `test/host/test_romgen_frlg_regions.c`.
- **Tests**: every Pallet cell's palette comes from the right tileset (palettes 0-6 primary, 7-12 secondary);
  `rg_cell_image` of the Pallet house door metatile is non-empty and its pixel SHA-1 is pinned; Pallet's regions:
  the pond is water, the three doors make house roles, the bytes are pinned after a visual check; Route 24's bridge
  deck is not water (section 2); signposts: Pallet has exactly 5 cut-out signs at the census cells, the Kanto-wide count
  is pinned, and every sign lands on a 0x84 cell or the rate is logged; the files round-trip through
  `VoxelRegions_Init` / `VoxelSigns_Init`.
- **Done-gate**: full 0.3 gate. Azahar on FR: Pallet with terrain, water and signs from data files; the boxes remain.

### T1 Kanto trees: ~150 + 100 (host tool) + 80

- **Why a slice**: the renderer's tree sprites come from an Emerald id table (`voxel_tree.c`). FRLG trees are other
  metatiles of another General tileset, so without a table they render as flat ground.
- **Host tool** `tools/romgen/rg_author_trees.c` (part of the B0 binary if B0 is first, otherwise its own target):
  it lists every General metatile whose art is at least 50 % foliage pixels (Gummygamer's rule, re-implemented and
  credited), grouped into 2x2 tree blocks by their occurrence on outdoor layouts. Pallet's border trees
  (0x14-0x1F, 0x24-0x27) are its known answer. The output is a list of numbers for a human/agent to review, not a file
  that gets committed.
- **Files**: `rg_gameprof.c` (`treePart`, `treeGround` for the FRLG rows: numbers only, PROVENANCE "ROM-measured, no
  decomp"), `voxel_tree.c` (read the table from the profile; the Emerald table is the Emerald row's), test extension in
  `test_voxel_world.c`.
- **Tests**: the Emerald table is byte-identical to today's; each FRLG tree id is a primary metatile below 640;
  Pallet's border block 0x1C, 0x1D, 0x14, 0x15 resolves to tree parts.
- **Done-gate**: renderer half of 0.3; Azahar: Pallet's and Route 1's tree walls are trees.

### B0 Authoring toolchain and FRLG buildings plumbing: ~900 + 150 + 150

- **Files**: `tools/romgen/rg_author.c` (the flags of section 5), `tools/romgen/rg_png.c` (a minimal PNG writer:
  stored deflate blocks plus CRC/Adler, about 120 lines, no dependency), `tools/romgen/Makefile` (link both into
  `build/romgen`; one hunk), `romgen_cli.c` (dispatch to `rg_author_main` when the first argument is `author`),
  `source/romgen/rg_kspecs.c` (the empty Kanto table plus the aggregator), `rg_gameprof.c` (`specs` for FRLG),
  `rg_buildings.c` (the deterministic FRLG order of principle 6, only where the Emerald path uses a CPython order:
  under `prof->game != GP_EMERALD`), `test/host/test_romgen_frlg_buildings.c`.
- **Functions**: `rg_author_census`, `rg_author_art`, `rg_author_preview`, `rg_author_check`,
  `rg_author_placements`, `rg_png_write_rgba`.
- **Tests**: the PNG writer's output is decoded by the test's own inflate-stored reader and compared pixel for pixel;
  `--census` on FR reproduces the M1 numbers: 277 outdoor door warps, 152 placements, 108 mainland and 44 Sevii, and
  per-map counts as in section 6 (pinned); an empty Kanto spec table produces a `buildings.bin` with 0 models that the
  consumer loads (the device then shows the fallback boxes, as today).
- **Done-gate**: full 0.3 gate; the census table of section 6 printed by the tool and copied into the BUILDLOG
  (counts only); the renders land under `tools/romgen/out/` (git-ignored, checked with `git status`).

### K1 Pallet → M1: ~470

- **Models**: `k_pallet_house` (one model, two placements: x5-9 y4-7 and x14-18 y4-7, 5x4 cells, 80x64 art, door at
  (6,7) / (15,7)), `k_pallet_lab` (x13-19 y10-13, 7x4, 112x64, door (16,13)). Both on layout 78, FNV-pinned.
- **Files**: `source/romgen/rg_kspecs_pallet.c`, `test/host/test_romgen_frlg_buildings.c` (extend).
- **Tests**: both models pass ortho 0/0/0 and density empty; placements are exactly these three; a
  `buildings.bin` with two models round-trips through `VoxelBuildings_Init`; the file SHA-1 is pinned after the visual
  check.
- **Done-gate**: full 0.3 gate; the per-model checklist of 5.4 for each model; **M1 in Azahar on FR and LG**
  (section 8); then the M1 hardware run.

### K2 landmarks → M2: ~450

- **Models**: `k_center` (5x4; 16 exact placements), `k_mart` (4x3; 11), `k_gym` (core rect; 7), plus the variants
  the census finds (Saffron's Center and Gym, One Island's Center, Indigo Plateau: each in its own town slice).
- **Done-gate**: as K1, with every placement listed by `--placements` and equal to the census numbers.

### L1 Kanto ledges → M2: ~250 + 150

Section 7.1. **After S3.7 is committed.**

### K3-K11, KS1-KS3: section 6

### V1 review and release audit → M5

An opus-model adversarial review of the whole phase; `release-legal-audit` (no ROM-derived bytes in the repo or the
`.cia`, PROVENANCE complete, no decomp text); the UI exposes FRLG behind the voxel setting. Hardware sign-off on FR
and on LG.

### L2 drawn Kanto relief (optional): ~800 + 200

Section 7.2.


## 5. Buildings: census and recipe-authoring workflow

### 5.1 The census (M1 numbers, FR rev 1; LG has identical blockdata)

**What counts as a building** is decided structurally, not by behaviour, because Kanto gates and huts are entered over
arrow mats or plain cells (section 2). The rule: for every outdoor map (map type 1, 2, 3, 5 or 6: 76 maps), every
warp event whose destination map has type 8 (indoor) is a door. All the doors of one map that lead to the same
destination map make **one placement** (double doors, two-door gatehouses). Cave mouths (destination type 4) and
map-to-map warps (type 3) are not buildings.

| Measure (FR rev 1, M1) | Count |
|---|---|
| Door warps on outdoor maps | 277 |
| Building placements (unique indoor destination per outdoor map) | **152** |
| ... on the mainland | **108** (74 in towns, 34 on routes, the Forest and the Safari Zone) |
| ... in the Sevii Islands, incl. Navel Rock and Birth Island | **44** |
| Excluded: the S.S. Anne gangway in Vermilion (the destination is the ship, not a building on the map) | 1 of the 108 |
| Pokemon Center, exact 5x4 match of the cell contents | 16 placements (Viridian, Pewter, Cerulean, Lavender, Vermilion, Celadon, Fuchsia, Cinnabar, Two-Seven Island, Route 4, Route 10). Saffron, One Island and Indigo Plateau are variants |
| Poke Mart, exact 4x3 match | 11 (Viridian, Pewter, Cerulean, Lavender, Vermilion, Fuchsia, Cinnabar, Three, Four, Six, Seven Island) |
| Gym, exact match of the core rect | 7 (Viridian, Pewter, Cerulean, Vermilion, Celadon, Fuchsia, Cinnabar). Saffron's Gym differs |
| **Distinct models (estimate)** | **about 59 mainland + about 18 Sevii, about 77 in total** (3 of them are the landmarks) |

The model count is an estimate. The throwaway flood-fill used for it merges buildings that touch, so the exact number
comes from B0's `--census` plus the agents' rect choices, and is pinned per town slice. A **model** is one recipe; it
is placed wherever the exact cell contents of its rect recur (the existing `find_placements` rule, which also covers
the second Pallet house and every landmark). Gatehouses that straddle a map connection (Saffron's four gates, the
Route 22/23 gate, the Viridian Forest gates) show up on both maps. Each map's visible half is its own placement,
and usually its own model.

### 5.2 The authoring CLI (host only, B0)

`tools/romgen/build/romgen author ROM <command> ...` writes under `tools/romgen/out/author/<BPRE|BPGE|BPEE>/`. B0 adds
`tools/romgen/out/` to `.gitignore` and checks it with `git check-ignore -v`. These files are ROM-derived pictures, so
they are never committed and never sent anywhere. Agents look at them with their image reader.

| Command | Output | Purpose |
|---|---|---|
| `census [--map G/N]` | stdout table: map (G/N), layout id, door cells, destination (G/N), seed rect, the cell-content signature, the model that covers it (or `-`). Last line: `covered N / 152` | The worklist, and the coverage meter of every K slice |
| `art LAYOUT X Y W H` | `L<id>_<x>_<y>_1x.png` (exactly the art `rg_building_art` returns, alpha kept); `_4x.png` (nearest-neighbour x4 with a 16-px cell grid and a pixel ruler every 8 px along two edges); `_lower.png`, `_upper.png` (each metatile layer alone); `_coll.png` (the 4x image tinted: blocked cells red, doors green, 0x84 signs blue, water cyan). stdout: one line per cell `x y metatile behaviour collision layerType`, all numbers | Choosing the rect and the exact rectangles in art pixels, and seeing which rows are roof (upper layer) and which are facade |
| `preview SPEC` | `<spec>_ortho.png` (the model rasterised by `rg_bcheck`'s Raster, which is what the gate judges), `<spec>_diff.png` (the art, with wrong pixels red, missing blue, extra yellow), `<spec>_fl.png`, `_fr.png`, `_top.png` (a small textured z-buffer render from front-left, front-right and the renderer's default camera pitch) | Seeing what is wrong before reading numbers. **An aid, not the gate** |
| `check [SPEC|town|all]` | per spec: ortho wrong/missing/extra per exact rect, the density list, placements found vs expected, the consumer round trip of a one-model file; exit code 1 on any failure | **The gate.** It is the same code as the host tests |
| `placements SPEC` | every (layout, x, y) where the spec lands, across all 384 layouts | Catching a model that lands where it should not, and finding the reuse a spec was meant to have |

Every image is written by `rg_png.c` (stored deflate, no compression dependency). The oblique renderer is about
250 lines inside the 900 of B0. It reuses the f32 mesh that the writer already builds.

### 5.3 Recipe files and names

- One file per town slice: `source/romgen/rg_kspecs_<town>.c` (`pallet`, `landmarks`, `viridian`, ...). Each
  exports `const RgSpec rg_kspecs_<town>[]` and its count. `rg_kspecs.c` concatenates them, in story order, into the
  profile's `specs`. The order is fixed by that file, so models and pages are emitted deterministically (principle 6).
- The rows use Emerald's `RgSpec` unchanged (`rg_bspecs.h`): name, kind (almost always `direct`), layout id + FNV-1a
  pin of the layout's blockdata, rect, match rows, ground metatiles, exact rects, builder + two int args. The layout
  ids are FRLG ids (Pallet = 78). FR and LG share them because the blockdata is identical, which a test pins (G1).
- Names: `k_<town>_<thing>` (`k_pallet_house`, `k_pallet_lab`, `k_celadon_dept`); landmarks `k_center`, `k_mart`,
  `k_gym`. The names never reach the file; they appear only in reports.
- Builders: one C function per distinct shape, over the ported part library (`rg_geom`: Prism, HipRoof, Frustum,
  Vault, Strip, Band, Card, Facet, Cylinder, Walls, PlainWall, Decal, Lifted). Parametric families (a gable house of
  width w) are one function with args, the way `kit_house(width)` is. File header: GPLv3, "3DGBA original work, Kanto
  recipes"; no Zallax header unless a function is a port of a Zallax builder.

### 5.4 Shape precedents (which Emerald builder to start from)

| Kanto shape | Examples | Emerald precedent (SPEC-S2 3.3) |
|---|---|---|
| Gabled house, side gable visible | Pallet houses, most town houses | `littleroot_house` (two-storey hip/gable), `kit_house(width)` |
| Lab / wide hall with a flat front | Oak's Lab, Cinnabar Lab, Pewter Museum | `littleroot_lab` |
| Pokemon Center / Poke Mart | the landmarks | `center_or_mart` (crown for the Center) |
| Gym | 7 exact + Saffron | `gym` |
| Flat-roofed block, many storeys | Celadon Dept. Store, Condominiums, Silph Co., Game Corner, Hotel | `flat_block`, `flat_part`, `devon` |
| Gatehouse | Route 5-8 / 11-18 / 22 gates, Viridian Forest gates | `flat_block` with `Strip` bands |
| Tower | Pokemon Tower | stacked `Prism` + `Frustum` (new builder) |
| Small hut | Underground Path entrances, rest houses, Sea Cottage | `oldale_house` |

### 5.5 The per-model done checklist (copied into each K slice's BUILDLOG)

1. The census row is covered: `census` shows the model on every placement the town slice owns.
2. The rect and the exact rects were chosen from the `art` 4x image. Each exact rect is written as numbers, with a
   one-word comment (roof, facade, eave).
3. `check`: ortho 0/0/0 for every exact rect, density empty, round trip OK.
4. `placements` equals the expected list (count and positions). No placement on a layout the town does not own,
   unless it is a deliberate reuse that is noted.
5. The `preview` images were looked at: the silhouette reads right from all three views. This is judgment, and it is
   recorded as "viewed" plus one sentence.
6. Full Emerald 0.3 gate identical. A K slice adds only to the Kanto table, but the gate is cheap and catches shared
   helpers being touched.
7. Azahar: a screenshot at the town's save-warp spot (section 8) on FR, and once per slice on LG.

### 5.6 How a K slice is run (agent brief template)

One agent per town slice, sequential (Guy's rule 7). Model: **sonnet** for the authoring of a town once K1 and K2 have
set the pattern; **opus** reviews the look of K1 and K2 and every tenth model after that (judgment). Fable is not used.
Brief:
- **Inputs**: the town's census rows (from `census --map`), this section, SPEC-S2 3.3 for the part library, and the
  previous town's recipe file as the style reference.
- **Loop per model**: `art` → write the row + builder → `check` → `preview` → fix → repeat until `check` passes, then
  the checklist of 5.5.
- **Bank to disk after every model**: the recipe file and a BUILDLOG line, so a session-limit kill loses at most one
  model (memory note: agent session-limit hygiene).
- **Never**: read pret map JSON or scripts for shapes (numbers only, and here the ROM art is the only source needed);
  execute any reference code; commit (the lead commits).
- **Hand-back**: models done, `covered N / 152`, the check output, anything that would not pass and why.


## 6. Per-town recipe slices

The placements are measured (M1, the structural rule of 5.1). "Landmark" = placements covered by the K2 models through
an exact match, so the town slice authors nothing for them. "New models" is an estimate that the slice replaces with
the pinned number. The lines are the recipe files plus test extensions, at about 70-90 lines per model (the S2
density) plus about 40 per town for the rows and pins.

| Slice | Maps (outdoor) | Placements | Landmark placements | New models (est.) | Lines (est.) | Notes |
|---|---|---|---|---|---|---|
| K1 | Pallet Town | 3 | 0 | 2 (house x2, Oak's Lab) | ~470 (first slice: patterns, pins, test scaffolding) | **M1** |
| K2 | landmarks, placed Kanto-wide | (34 across all towns) | Center 16, Mart 11, Gym 7 | 3 | ~450 | M2 |
| K3 | Viridian City, Route 2, Viridian Forest | 5 + 4 + 2 = 11 | 3 | ~5 (house, school, Route 2 house, Route 2 east building, the Forest gates) | ~600 | |
| K4 | Pewter City | 6 | 3 | ~3 (museum, two houses) | ~280 | |
| K5 | Cerulean City, Route 4, Route 25 | 9 + 1 + 1 = 11 | 4 (Center x2, Mart, Gym) | ~5 (bike shop, houses, Sea Cottage) | ~500 | Route 24 has no building |
| K6 | Vermilion City, Routes 5-8 | 8 + 3 + 2 + 2 + 2 = 17 (16 without the S.S. Anne) | 3 | ~6 (fan club, houses, Day Care, Underground Path hut x4 placements, the Route 5/6 and 7/8 gate halves) | ~650 | |
| K7 | Lavender Town, Route 10 | 6 + 2 = 8 | 3 (Center x2, Mart) | ~5 (Pokemon Tower, houses, Volunteer House, Power Plant) | ~450 | |
| K8 | Celadon City | 9 | 2 (Center, Gym) | ~7 (Dept. Store, Condominiums, Game Corner, Prize Room, Hotel, house, restaurant) | ~900 | the largest buildings |
| K9 | Fuchsia City, Safari Zone (4 maps), Routes 11, 12, 15, 16, 18 | 9 + 6 + 1 + 2 + 1 + 2 + 1 = 22 | 3 | ~9 (houses, Safari entrance + office, Warden's house, rest house, secret house, the two-storey gates, fishing house) | ~850 | |
| K10 | Saffron City | 13 | 1 (Mart) | ~9 (Center variant, Gym, Silph Co., Dojo, Copycat's house, Mr. Psychic's, house, Trainer Fan Club, the four gate halves) | ~900 | |
| K11 | Cinnabar Island, Indigo Plateau, Routes 22, 23 | 5 + 1 + 1 + 1 = 8 | 3 | ~5 (Pokemon Lab, Mansion, Indigo Plateau building, the Route 22/23 gate halves) | ~450 | **M3** |
| KS1 | One, Two (+ Cape Brink), Three Island (+ Port) | 4 + 5 + 8 = 17 | 3 (Center x2, Mart) | ~7 (harbor, One Island Center variant, houses, Joyful Game Corner) | ~650 | |
| KS2 | Four, Five Island (+ Resort Gorgeous, Meadow) | 7 + 6 = 13 | 3 | ~6 (Lorelei's house, Day Care, houses, Rocket Warehouse) | ~550 | |
| KS3 | Six (+ Water Path), Seven (+ Trainer Tower, Sevault Canyon), Navel Rock, Birth Island | 6 + 6 + 1 + 1 = 14 | 4 (Center x2, Mart x2) | ~5 (houses, Trainer Tower lobby, the two harbors) | ~500 | **M4** |
| **Total** | 76 outdoor maps (47 of them have buildings) | **152** (108 + 44) | 34 | **~77** | **~8 200** | |

The landmark column sums to 34 = 16 + 11 + 7. Mainland placements: 3 + 11 + 6 + 11 + 17 + 8 + 9 + 22 + 13 + 8 = 108.
Sevii placements: 17 + 13 + 14 = 44.

Hardware runs: after K1 (M1), K2 + L1 (M2), K5, K8, K11 (M3), KS3 (M4). That is the "one hardware run per two towns"
rhythm of PHASE.md.


## 7. Relief plan for Kanto

Emerald relief (S3) is a byte-faithful port of Zallax's generator, driven by Emerald name-order tables
(`rg_rtables`: the A.3 outdoor list, A.5 folder order, ENABLED, drawn groups, rock tables). None of those tables has
a Kanto counterpart, and `rg_rtables_check` already refuses a non-Emerald world. Kanto relief is therefore **our own
deterministic code over the same writer**, and FRLG never calls the S3 drawn-relief modules (`rg_rcanvas`, `rg_rprep`,
`rg_rwrap`, `rg_rworld`, `rg_rsolve`, `rg_rcut`, `rg_rshape`). Until L1, G1 forces the relief mode to OFF on FRLG
(1.4), so `relief.bin` is simply not written and the renderer's existing "no relief file" path is used. That is the M1
state.

### 7.1 L1: Kanto ledges (`relief.bin` LEDGES mode). After S3.7 is committed

- **Layouts**: the outdoor set comes from the **map types** (1, 2, 3, 5, 6) of the map headers that reference the
  layout, not from `rg_relief_outdoor` (an Emerald name table). New function `rg_relief_outdoor_layout(w, id)`:
  Emerald returns `rg_relief_outdoor(id)` (unchanged, so the bytes do not move), FRLG returns the map-type rule. Then
  `rg_ledge_layouts` calls the new function. The layouts are walked in ascending id order, as today.
- **Behaviours**: `rg_is_jump` becomes `gp_beh(&prof->jump, b)` inside `rg_ledge.c`. FRLG's jump set is 0x38-0x3B,
  with the same direction meaning as Emerald's (0x38 E, 0x39 W, 0x3A N, 0x3B S, checked against pret@037335f numbers
  and against the cell census: 0x3B x1022 are the south ledges of Routes 1, 2, 22 ...). `jump_dirs` keeps its table;
  the diagonals are never reached on FRLG. The `assert` uses the profile set.
- **ENABLED** (two Emerald layouts that skip the junction rule): an Emerald list. FRLG has none (`rg_ledge_enabled`
  returns false for every non-Emerald world).
- **Lip geometry**: `RG_LIP 6`, `RG_LIP_WIDTH`, the back width and the ground-colour rings are Zallax's Emerald
  tuning. Kanto ledge art is a different drawing (a lighter lip and a darker shadow row), so L1 **re-measures** them on
  Route 1 / Route 22 with the `art` command, and the FRLG row carries its own values if they differ (`ledgeLip`,
  `ledgeWidth`, `ledgeBack` in the profile; the Emerald row = the current constants).
- **Tests** (`test_romgen_frlg_relief.c`): Route 1 (layout 89) has a ledges row whose cell count is pinned; a synthetic
  3x3 layout with one FRLG south ledge gives the hand-computed heights (as S3.1's test); no ledge cell is produced on a
  non-outdoor layout; the full 0.3 gate on Emerald.
- **Done-gate**: Azahar on Route 1 (FR and LG): ledges rise, the player hops over them, nothing floats or sinks.
  Hardware with M2.

### 7.2 L2: drawn Kanto relief (optional, after M3 and after S3.8)

Cliffs, mountain slopes (Mt. Moon's outside, Route 3/4, Victory Road's approach, Mt. Ember on One Island) would need
what S3 calls drawn relief. The S3 pipeline is bound to Emerald's tables and to byte-faithfulness with Zallax's
output. For Kanto there is no upstream output to match, so L2 would be:
- A **Kanto rock-tile table**: the General and secondary metatiles that draw cliff faces, measured from the ROM by a
  host tool (the same kind of foliage/colour test as T1, plus the collision of the cells). Numbers only, with a
  PROVENANCE row "ROM-measured, no decomp".
- Our own **height assignment**: connected cliff regions, heights from the number of cliff-face rows, written as a
  plain deterministic BFS in (layout id, y, x) order. It reuses the relief writer and lattice (`rg_rlat`,
  `rg_relief_write`), not the S3 canvas/solver.
- Acceptance is visual only (Azahar plus hardware) together with determinism and round-trip tests, since there is no
  oracle.

It is optional because Kanto's outdoor maps are much flatter than Hoenn's, and ledges plus buildings plus trees carry
most of the look. The lead decides after M3.


## 8. Emulator verification plan per milestone

The tools are the ones Phase 33 used: the Azahar harness (`tools/emutest`, `azctl boot --stage-roms <name>`), with
a **patched copy** of Guy's save under the git-ignored `roms/` (memory note: emutest save warp). Guy's original
`roms/firered.sav` and `roms/leafgreen.sav` are never written. Every milestone runs on **FR and on LG**. The data
files are generated by the host CLI from the matching ROM and copied to `sdmc:/3ds/3DGBA/voxel/BPRE/` or `.../BPGE/`.

### 8.1 Save warp on FRLG: what is the same and what differs

The same as Emerald (pret@037335f numbers, then asserted on Guy's saves by the patcher before it writes):
- 2 slots x 14 sectors, a 4 KiB sector, 3968 data bytes per sector (`SECTOR_DATA_SIZE`), a footer with section id at
  +0xFF4, checksum at +0xFF6, signature 0x08012025 at +0xFF8, save index at +0xFFC. The live slot is the one with the
  higher index.
- SaveBlock2 (section 0) +0x09 `specialSaveWarpFlags`, bit 0 = continue-game warp.
- SaveBlock1 (section 1) +0x0C `continueGameWarp` = {s8 group, s8 num, s8 warpId = -1, pad, s16 x, s16 y}.
- The 16-bit checksum: the fold of the u32 sum over the section's **data length**.

What differs: the data length of each section (FRLG's SaveBlock2 is smaller than Emerald's 3884). The patcher does not
hard-code it. It **self-calibrates**: for each of the 14 sections of the unmodified save it finds the length L
(a multiple of 4, at most 3968) whose checksum equals the stored one, refuses to continue if any section has no such L
or more than one, and then reuses L after patching. The result is printed and recorded once in BUILDLOG-P34. The
patcher also refuses a target cell that is collision-blocked in the ROM layout.

The same Emerald rules carry over: patching `pos`/`location` alone does not move the player; base a warp on a save
made in a town, not in a special area.

### 8.2 Warp targets and what each milestone checks

Map numbers measured on FR (group 3): Pallet 0, Viridian 1, Pewter 2, Cerulean 3, Lavender 4, Vermilion 5, Celadon 6,
Fuchsia 7, Cinnabar 8, Indigo Plateau 9, Saffron 10, One-Seven Island 12-18 (12 One, 13 Two, 14 Three, 15 Four,
16 Five, 17 Seven, 18 Six), Route N = 18 + N for N 1-20, Route 21 = 39 and 40 (two maps), Route 22-25 = 41-44. The x, y of
each target is the cell south of the town's first census door, checked as walkable by the patcher. Each target save is
`roms/frlg-<town>.sav` / `.gba` (a symlink to `firered.gba` or `leafgreen.gba`).

| Milestone | Warp target(s) | Checks (FR and LG) |
|---|---|---|
| M0 (R2) | Pallet 3/0 | voxel on with no data files; status string; `vx: anchors ok`; walk into the player's house (2D inside) and out (3D, no garbage frame at the fade); walk north into Route 1 3/19 (the connection renders); Emerald Littleroot capture unchanged |
| M1 (K1) | Pallet 3/0, Route 1 3/19 | terrain, the pond is water, five signs as cut-outs, tree walls are trees, the two houses and the Lab are modelled (not boxes) from four camera angles; the doors are where the warps are (walk in and out of each); screenshots kept in the BUILDLOG; then **the hardware run** (frame time with data loaded, 3D slider, the look) |
| M2 (K2 + L1) | Viridian 3/1, Pewter 3/2, Cerulean 3/3, Route 22 3/41, Route 1 | Center/Mart/Gym modelled at every exact-match placement in these towns; Route 1 and Route 22 ledges rise and the hop works; hardware run |
| M3 (K3-K11) | each town's target, plus one route per town slice | per town: every census placement modelled (`covered` count), no box left except the excluded S.S. Anne gangway; one hardware run per two towns |
| M4 (KS1-KS3) | One-Seven Island 3/12-3/18 | as M3 |
| M5 | all of the above on both games | release-legal-audit; hardware sign-off on FR and LG |

Anything Azahar shows is "emulator-verified". The milestone is closed only by the hardware line in the BUILDLOG
(PHASE.md "Done = real New-3DS hardware").


## 9. Open questions (for the lead / Guy)

1. **Landing seam for G1**: the S3.4/S3.5 seam (recommended: one session, and every later S3 slice starts from a tree
   with the profile) or the S3.8 seam (Emerald relief closed first)? Section 0.2.
2. **The bits 8-9 hazard** (1.2): if any Emerald metatile attribute has bit 8 or 9 set, the Emerald row keeps mask 0xFF.
   R0 measures it. If it is set, does the lead accept the different FRLG packing path described in 1.5?
3. **Caves stay 2D** on FRLG (87 cave maps), the same as indoor maps. Confirm. The alternative is flat voxel caves,
   which is what Emerald shows today.
4. **The S.S. Anne**: exclude the gangway (current plan), or a ship model later?
5. **Landmark variants**: the Saffron, One Island and Indigo Plateau Centers and the Saffron Gym get their own models
   in their town slices (plan). Alternatively the landmark builders could take parameters. The agent decides from the
   `art` images in K2.
6. **Tree table** (T1): a fixed list measured once (plan) or a runtime foliage classifier? The fixed list is cheaper
   on the device and can be reviewed. The classifier would also cover hacks, which are out of scope.
7. **rev 0** (and the rev 0 `gMapGroups` values in `gamestate.h`): it needs a dump to verify. Does Guy have, or want,
   one?
8. **The FRLG save-warp checksum length**: self-calibrated (8.1) rather than taken from pret. Fine for the harness?
9. **Gatehouse halves**: a gate that straddles a map connection becomes two placements and usually two models (5.1).
   Should a later slice add a cross-map model (one model, drawn across the seam)? That needs a renderer change, so not
   in v1.
10. **L2 drawn relief**: decide after M3, from how flat M3 looks.

### 9.1 Lead decisions, 2026-10-06

1. G1 lands at the **S3.5/S3.6 seam**: S3.5 had already started when this spec arrived.
2. R0 measures bits 8-9. If any are set, the lead accepts the 1.5 packing path.
3. Caves stay 2D in v1.
4. The S.S. Anne gangway is excluded.
5. The K2 agent decides from the `art` images.
6. Fixed tree table.
7. rev1 only (Guy owns rev1 carts).
8. Self-calibrated checksum is fine.
9. Two models in v1, no cross-map model.
10. L2 is decided after M3.

R-track slices run in a separate git worktree while S3 slices are in flight, so the two device builds don't collide.
