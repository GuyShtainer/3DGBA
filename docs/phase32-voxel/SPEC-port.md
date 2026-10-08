# Phase 32 — SPEC-port: porting the ZallaxDev voxel overworld into 3DGBA

Status: SPEC (2026-10-05). Contract: `PHASE.md` (this folder). Legal: `LEGAL-zallax-port.md`.
Upstream pin: `ZallaxDev/pokeemerald-3Ds-dualscreen` **v0.2.0 = c330c0a** (study copy at
`projects/_reference/pokeemerald-3Ds-dualscreen`, HEAD 42085a1; `3ds_port/src/voxel/` is
byte-identical between the two — vendor with `git show c330c0a:<path>`, never from HEAD).
Data: `docs/phase31-diorama/SPEC-data.md` §10 (addresses, asserts A1–A15).

Binding invariants restated (PHASE.md): presentation-only, **zero game-RAM writes**; VOXEL 3D
off ⇒ today's frame byte-identical; no edits to `celiolink.c`/`netlink.c`/`wireless.c`; one
render thread owns the GPU; game state is snapshotted in the parked window and everything after
`C3D_FrameEnd` reads only the snapshot; never ship generated data; suites green + BUILDLOG +
`tools/closeout.sh`; exit gate emulator-visible, hardware-final.

---

## 1. File plan

### 1.1 Layout

All port code lives in **`source/voxel/`** (new). The Makefile changes one line:
`SOURCES := source` → `SOURCES := source source/voxel` (the PICAFILES/BINFILES globs at
Makefile:103-115 and the `%.shbin` rule at :183 then pick up `source/voxel/voxel.v.pica`
with no further edit). Include path: vendored files keep `#include "voxel_world.h"` style
(same directory); our shims live in `source/voxel/` too, so no `-I` change.

### 1.2 Vendored (copied at c330c0a, header line added, upstream comments kept)

| Upstream `3ds_port/src/voxel/` | Lines | Treatment |
|---|---:|---|
| `ctr_voxel.c` / `.h` | 5946 | **modified**: battle calls (4257, 4336, 4338) route to our stub; shader + tree loads (54, 1549; `voxel_tree.h:8`) switch from romfs to embedded blobs; `CtrVoxel_Draw` (~5703) gains the per-eye camera (§7); `CtrAudio_StartWorker` call deleted (we own audio) |
| `voxel_world.c` / `.h` | 1167 | **modified**: pret includes deleted → `gba_game.h`; `extern const struct Tileset gTileset_*` (35-37) deleted; Fortree special case (535) re-expressed (§2.6); `GetCurrentWeather`, `LZDecompressWram`, `GetMapHeaderFromConnection`, `MetatileBehavior_*` resolve to our adapter |
| `voxel_entities.c` / `.h` | 1054 | **modified**: pret includes → `gba_game.h`; `IsTemplate` param type `const struct SpriteTemplate *` → `GbaPtr` (668-687); `VRAM_`/`PLTT`/`REG_DISPCNT` reads (≈201, ≈341) → snapshot accessors; `sStepFrames` (33) re-derived (§2.7) |
| `voxel_atlas.c` / `.h` | 769 | **modified**: pret count macros → `gba_game.h` constants; `Port_GetMapLayoutById` → adapter; `sSolidColors` RGB2 macro → our `GBA_RGB5` (values are port-chosen — confirmed §2.8) |
| `voxel_mesh_builder.c` / `.h` | 1334 | as-is (no pret, no platform symbols) |
| `voxel_lighting.c` / `.h` | 837 | as-is |
| `voxel_building.c` / `.h` | 558 | as-is (`PORT_LOG`, `VoxelFile_Open` resolve to our shims) |
| `voxel_relief.c` / `.h` | 515 | as-is |
| `voxel_sign.c` / `.h` | 417 | as-is |
| `voxel_tree.c` / `.h` | 140 | **modified**: art source = embedded `voxel_trees_bin` (§1.4) |
| `voxel_regions.c` / `.h` | 107 | as-is |
| `voxel_arena.c` / `.h` | 103 | as-is |
| `voxel_camera.c` / `.h` | 94 | **modified** only by §7 (eye offset input); settings calls resolve to our shim |
| `voxel_grade.c` / `.h` | 72 | as-is |
| `voxel_file.h` | — | as-is (`VOXEL_HOST_FILES` ⇒ fopen for the host build, else `CtrData_Open`) |
| `voxel.v.pica` | 85 | as-is (built to `voxel_shbin.h`/`_shbin` by the Makefile rule) |
| `NOTICE.md` | 34 | copied verbatim to `source/voxel/NOTICE.md` (carries both MIT notices) |
| `3ds_port/assets/voxel/trees/*.png` | — | **corrected 2026-10-08:** NOT original art — Emerald's own tree metatiles (99.7–100% pixel match; upstream's MIT grant excludes game graphics). Never committed; builds embed a local copy only when present |

**Not vendored:** `voxel_battle.c/.h` (replaced by our stub, §2.9), all of `3ds_video*.c`,
`3ds_settings.c`, `3ds_data.c`, `3ds_pak.c`, `3ds_assets.c`, `main_3ds.c`, every
`patches/pokeemerald/*.patch`, every recipe/pak/bin. The pak and loose-bin readers are
**our own code** written against the format described in §4.5.

### 1.3 Ours (new, GPLv3, written fresh)

| File | Purpose |
|---|---|
| `source/voxel/gba_game.h` | our struct/constant vocabulary for the game side (§2) — written from SPEC-data, our field names, no pret structs |
| `source/voxel/vx_adapter.c/.h` | snapshot decode, ROM interning, the globals the vendored code reads (§2–§3) |
| `source/voxel/vx_snapshot.c/.h` | the parked-window copy (`vx_snapshot_take`) (§3); pure C, no libctru |
| `source/voxel/vx_lz77.c/.h` | our own GBA LZ77 (type 0x10) decoder + packed-length scanner |
| `source/voxel/vx_behavior.c` | `MetatileBehavior_*` predicates as our own behavior-id tables (from SPEC-data's behavior list) |
| `source/voxel/vx_battle_stub.c` | `VoxelBattle_*` stubs (§2.9) |
| `source/voxel/ctr_shims.c/.h` | `CtrVideo_*`, `CtrAssets_*`, `CtrPlatform_*`, `CtrLog_*`, `CtrSettings_Voxel*`, `CtrSprite_IsVoxelWeather`, `PORT_LOG` (§4) |
| `source/voxel/vx_data.c/.h` | `CtrData_Open`: our pak reader + loose fallback (§4.5) |
| `source/voxel/vx_overlay.c/.h` | overlay texture build (key → alpha) (§6) |
| `source/voxel/vx_stereo.c/.h` | per-eye camera math (§7), pure C |
| `data/voxel_trees.bin` | 8192 B packed from the vendored tree PNGs by `tools/voxel/pack_trees.py` (ours) — MIT art, safe to ship |
| `patches/mgba-backdrop-key.patch` | the mGBA software-renderer patch (§6.3), MPL-2.0 file-level, applied by the existing patch flow |
| `test/host/test_voxel_*.c` | §9 |

Edits to existing files: `Makefile` (SOURCES), `source/main.c` (§5, §8), `source/gbacore.c/.h`
(`gbacore_mem_block`, `gbacore_set_overlay_mode`, §3/§6), `source/tilt.c/.h` (`voxel_gate`, §5.3),
repo `NOTICE`, the About/credits screen text, `docs/BUILDLOG.md`.

### 1.4 Header text

Every vendored file gets, as its **first** lines, above the untouched upstream comment:

```c
/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/src/voxel/<file>,
 * MIT License — see source/voxel/NOTICE.md. */
```

Files vendored without code changes (mesh_builder, lighting, …) still get the line (LEGAL:
"every vendored file"); the word "Modified" is then accurate because the include set changes.
Our own files carry the normal 3DGBA GPLv3 header and no upstream text.

### 1.5 NOTICE and credits

- `source/voxel/NOTICE.md` — verbatim copy of upstream `voxel/NOTICE.md` (Zallax MIT @ c330c0a +
  pokeemerald-multiplatform MIT @ db1cab3d).
- Repo `NOTICE` — new section "Voxel overworld (source/voxel/)" quoting both MIT notices in full.
- About/credits (the menu's credits text): two lines — "Voxel overworld: ZallaxDev (MIT), from
  pokeemerald-3Ds-dualscreen v0.2.0" and "incl. code from pokeemerald-multiplatform (MIT)".
- README "Voxel 3D" note: the feature needs user-built data (`emerald3ds.pak` made by the user from
  their own ROM with upstream's tooling); 3DGBA does not ship or download it.

---

## 2. Every pret touchpoint → `gba_game.h` + adapter

### 2.1 Pointer strategy (decided): host-layout mirror, ROM interned, EWRAM decoded per snapshot

The vendored code dereferences game structures as C pointers (`gMapHeader.mapLayout->primaryTileset->metatiles[i]`).
In our app those structures live in emulated memory with 32-bit GBA addresses. Options considered:

| Option | Verdict |
|---|---|
| (a) Rewrite every deref to `read_u32(addr+off)` | rejected: ~120 sites across 4 files, high merge cost against future upstream, easy to get wrong silently |
| (b) Map GBA address space into the host at the same addresses | impossible on 3DS (no fixed mappings at 0x02000000/0x08000000 in our process) |
| **(c) Host mirror structs** with host pointers, decoded from GBA memory by our adapter | **chosen** — upstream already does exactly this for its host test (`3ds_port/tests/voxel_world_hash_test.c`, 188 lines, includes `../src/voxel/voxel_world.c` against stub structs with host pointers), so the vendored code is known to compile and run unchanged against it |

Rules of (c):

1. **ROM objects are interned.** `MapHeader`, `MapLayout`, `Tileset`, `MapConnections`/`MapConnection[]`,
   `MapEvents` (bg events for signposts), `ObjectEventGraphicsInfo`: decoded once per GBA address into a
   session cache `vx_rom_intern` (open-addressed table, key = GBA address, value = host object; arena-
   allocated, freed on ROM unload). ROM is immutable, so one host object per address keeps **pointer
   identity** valid — the vendored code memoizes by pointer (`voxel_world.c` instance cache, atlas
   tileset keys, `VoxelWorld_Hash`) and compares `&gTileset_X` against `layout->primaryTileset`.
2. **Payload pointers are direct host pointers into the ROM buffer.** `layout->map`, `layout->border`,
   `tileset->tiles`, `->palettes`, `->metatiles`, `->metatileAttributes` become
   `romBase + (gbaAddr - 0x08000000)`; `romBase`/`romSize` from `gbacore_mem_block(core, GBA_REGION_ROM0)`
   (new wrapper over `core->getMemoryBlock`, mGBA `src/gba/core.c:1150`). The ROM buffer is never
   written after load (the GPIO RTC write window at 0x080000C4 is a register, not the buffer), so
   reading it from AfterSubmit while a worker runs is race-free. u16/u32 arrays are used in place:
   ARM11 and the GBA are both little-endian and ROM payloads are naturally aligned (asserted per pointer:
   `(addr & 1)==0` for u16 arrays, `(addr & 3)==0` for u32 arrays — misaligned ⇒ reject the map).
3. **EWRAM/IWRAM objects are decoded per snapshot** (§3) into host globals of the same names the vendored
   code expects (`gMapHeader`, `gBackupMapLayout`, `gSaveBlock1Ptr`, `gObjectEvents[16]`, `gPlayerAvatar`,
   `gSprites[65]`, `gMain`, `gWeatherPtr`, `gPaletteFade`, `gPlttBufferUnfaded`). Pointers inside them that
   point at ROM are interned (rule 1); pointers into EWRAM are rebased into the snapshot copy
   (`gBackupMapLayout.map` → the snapshot's copy of `sBackupMapData`).
4. **Every pointer is validated before decode** with the SPEC-data guards: ROM pointer ⇔
   `(p>>24)==0x08 && p-0x08000000 < romSize` (`ft_rom_ptr`), EWRAM ⇔ `(p>>24)==0x02 && p<0x02040000`.
   A failed guard ⇒ the adapter returns "map unavailable" ⇒ `VoxelWorld_IsMapAvailable()` false ⇒ the
   voxel gate drops to flat for that frame (never a crash, never a partial world).
5. **Shims for upstream port helpers:** `Port_ResolveAssetPointer(p)` = identity (we already return host
   pointers). `Port_GetAssetSizeExact(p)` = lookup in a size registry filled while decoding tilesets (§2.5);
   unknown ⇒ 0. `Voxel_LoadTilesStep` **fails on packedSize 0**, so the registry is mandatory for tiles.

`GbaPtr` = `uint32_t`; every field that stays a GBA address (callbacks, sprite templates) is typed
`GbaPtr`, never a host pointer, so a mistaken deref is a compile error.

### 2.2 `source/voxel/gba_game.h` — vocabulary (our names; written from SPEC-data §10)

The vendored code names fields the pret way (`mapLayout`, `primaryTileset`, `currentCoords`…). LEGAL
asks for our own names "where practical". Practical split:

- **Struct tags and field names the vendored code dereferences keep upstream's spelling** (renaming them
  means editing ~200 vendored lines for no legal gain — field names are not expression; the layout facts
  come from our SPEC-data, and **our host structs are not GBA layouts**: they hold host pointers, drop
  every field the voxel code does not read, and are declared in our own order).
- **Everything else is ours**: address constants (`GBA_ADDR_*`), the raw-offset tables (`GBA_OFF_*`) the
  adapter decodes with, behavior ids, the snapshot struct, the decode functions.
- No pret header is included anywhere (`grep -rn 'include "global\|include "constants/' source/voxel` = 0
  is a closeout check).

Host mirror structs (complete — only fields the vendored code reads):

```c
typedef uint32_t GbaPtr;
struct Tileset        { bool8 isCompressed, isSecondary; const void *tiles; const u16 *palettes;
                        const u16 *metatiles; const u16 *metatileAttributes; GbaPtr gbaAddr; };
struct MapLayout      { s32 width, height; const u16 *border, *map;
                        const struct Tileset *primaryTileset, *secondaryTileset; };
struct MapConnection  { u8 direction; s32 offset; u8 mapGroup, mapNum; };
struct MapConnections { s32 count; const struct MapConnection *connections; };
struct BgEvent        { u16 x, y; u8 elevation, kind; };            /* signposts only */
struct MapEvents      { u8 bgEventCount; const struct BgEvent *bgEvents; };
struct MapHeader      { const struct MapLayout *mapLayout; const struct MapEvents *events;
                        const struct MapConnections *connections; u16 mapLayoutId;
                        u8 regionMapSectionId, cave, weather, mapType; GbaPtr gbaAddr; };
struct BackupMapLayout{ s32 width, height; u16 *map; };
struct Coords16       { s16 x, y; };
struct ObjectEvent    { u32 active:1, singleMovementActive:1, heldMovementActive:1, invisible:1, isPlayer:1;
                        u8 spriteId, graphicsId, currentElevation, facingDirection;
                        struct Coords16 currentCoords, previousCoords; };
struct OamData        { u32 y:8, affineMode:2, objMode:2, mosaic:1, bpp:1, shape:2,
                            x:9, matrixNum:5, size:2; u16 tileNum:10, priority:2, paletteNum:4, affineParam; };
struct Sprite         { struct OamData oam; GbaPtr template; s16 x, y, x2, y2;
                        s8 centerToCornerVecX, centerToCornerVecY; s16 data[8];
                        u8 inUse:1, coordOffsetEnabled:1, invisible:1; u8 subpriority; };
struct SpriteFrameImage { const void *data; u16 size; };
struct ObjectEventGraphicsInfo { s16 width, height; u16 size; const struct SpriteFrameImage *images; };
struct PlayerAvatar   { u8 flags, spriteId, objectEventId; };
struct SaveBlock1     { struct Coords16 pos; u8 mapGroup, mapNum; };      /* pointed to by gSaveBlock1Ptr */
struct Main           { GbaPtr callback2; u8 inBattle; };
struct PaletteFade    { u8 y; bool8 active; };
struct Weather        { u8 currWeather, palProcessingState, currBlendEVA, fogHSpritesCreated, fogDSpritesCreated; };
```

`OamData` is the one struct whose **bit layout must equal the hardware's** (the vendored code
reads `sprite->oam.tileNum` etc. from a decoded copy; we decode OAM attributes by explicit shifts
into it — we do not memcpy GBA bytes onto host bitfields).

### 2.3 Raw layouts the adapter decodes (GBA side; BPEE)

Addresses verified against `/tmp/pret/pokeemerald.sym` (copy kept in the scratchpad of the spec
session; SPEC-data §10.1 is the canonical table). Struct offsets: ✅ = verified in SPEC-data / diodump,
**P2a** = must be confirmed in the first step of slice P2 (vendor + adapter) by the offset probe (§2.10) before any code depends on it.

| Object | GBA address | Size | Fields read (offset) |
|---|---|---:|---|
| `gMain` (IWRAM) | 0x030022C0 | 0x43C | callback2 @0x04 ✅; inBattle = byte @0x439 bit 1 (**P2a**) |
| `gSaveBlock1Ptr` (IWRAM ptr) | 0x03005D8C | 4 | → SaveBlock1: pos.x @0, pos.y @2, mapGroup @4, mapNum @5 ✅ |
| `gBackupMapLayout` (IWRAM) | 0x03005DC0 | 12 | width @0, height @4, map ptr @8 ✅ (A2/A3) |
| `sBackupMapData` (EWRAM) | 0x02032318 | 0x5000 | u16 cells; mask id 0x03FF, collision 0x0C00>>10, elevation 0xF000>>12 ✅ |
| `gMapHeader` (EWRAM) | 0x02037318 | 0x1C | mapLayout @0, events @4, connections @0xC, mapLayoutId @0x12, regionMapSec @0x14, cave @0x15, weather @0x16, mapType @0x17 ✅ |
| `gObjectEvents` (EWRAM) | 0x02037350 | 16×0x24 | byte0: active b0, singleMovementActive b1, heldMovementActive b6 (**P2a**); byte1 invisible b5 ✅; byte2 isPlayer b0 ✅; spriteId @4, graphicsId @5, elevation @0xB low nibble, currentCoords @0x10, previousCoords @0x14, facing @0x18 low nibble ✅ |
| `gPlayerAvatar` (EWRAM) | 0x02037590 | 0x24 | flags @0, spriteId @4, objectEventId @5 ✅ (A9) |
| `gSprites` (EWRAM) | 0x02020630 | 65×0x44 | oam @0 (8 B), template @0x14, x/y @0x20/0x22, x2/y2 @0x24/0x26, c2cVec @0x28/0x29, data[8] @0x2E, flags @0x3E (inUse b0, coordOffsetEnabled b1, invisible b2), subpriority @0x43 (template/data/subpriority **P2a**) |
| `gPlttBufferUnfaded` (EWRAM) | 0x02037714 | 0x400 | 512 × u16 BGR555 |
| `gPaletteFade` (EWRAM) | 0x02037FD4 | 0xC | y = (u16@4 >> 6) & 31, active = u16@6 bit 15 (**P2a**) |
| `gWeather` (EWRAM; `gWeatherPtr` @0x0854C14C is a ROM const → it) | 0x02038454 | 0x750 | currWeather, palProcessingState, currBlendEVA, fogHSpritesCreated, fogDSpritesCreated (**P2a**, all five) |
| MapLayout (ROM) | via header | 24 | w @0, h @4, border @8, map @0xC, prim @0x10, sec @0x14 ✅ |
| Tileset (ROM) | via layout | 24 | isCompressed @0, isSecondary @1, tiles @4, palettes @8, metatiles @0xC, attrs @0x10 ✅ |
| MapConnections (ROM) | via header | 8 | count @0, ptr @4; entries stride 12: dir @0, offset s32 @4, group @8, num @9 ✅ (A7/A8) |
| MapEvents (ROM) | via header | 20 | bgEventCount @3, bgEvents @0x10; BgEvent stride 12: x @0, y @2, elevation @4, kind @5 (**P2a**) |
| `gMapGroups` (ROM) | 0x08486578 | — | `GetMapHeaderFromConnection`: `rd32(rd32(gMapGroups+4*group)+4*num)` |
| `gMapLayouts` (ROM) | 0x08481DD4 | — | `Port_GetMapLayoutById(id)` = intern(`rd32(gMapLayouts + 4*(id-1))`), id 0 ⇒ NULL |
| `gObjectEventGraphicsInfoPointers` (ROM) | 0x08505620 | 0x3BC (239 ptrs) | → GraphicsInfo: size u16 @6, width s16 @8, height s16 @0xA, images ptr @0x1C (**P2a**); SpriteFrameImage stride 8: data @0, size u16 @4 |
| `gFieldEffectObjectTemplatePointers` (ROM) | 0x085059F8 | 0x94 (37 ptrs) | copied to a host `GbaPtr[37]` at ROM load |
| `gTileset_General` / `_Fortree` / `_GenericBuilding` (ROM) | 0x083DF704 / 0x083DF7C4 / 0x083DFB6C | 24 each | interned at ROM load |
| `CB2_Overworld` / `CB2_OverworldBasic` | 0x08085E5C / 0x08085E50 | — | compared with the thumb bit: 0x08085E5D / 0x08085E51 |
| PLTT (hardware) | 0x05000000 | 0x400 | OBJ half @+0x200 |
| OAM (hardware) | 0x07000000 | 0x400 | not read by the voxel code (it reads `gSprites[].oam`); not snapshotted |
| VRAM BG char | 0x06000000 | 0x8000 | tileset-anim diff (§2.11) |
| VRAM OBJ | 0x06010000 | 0x8000 | `VRAM_+0x10000` in `GatherSource` |
| DISPCNT | 0x04000000 | 2 | bit 6 = OBJ 1D mapping |

`gMain.inBattle` is read only by our battle stub. Weather offsets are needed by `voxel_world.c` only
(fog sprite flags, blend EVA for the fade, palProcessingState for screen-fade detection,
`GetCurrentWeather()` = `currWeather`).

### 2.4 Deref sites that change (compiler-driven)

Method: delete the pret includes, add `#include "gba_game.h"`, build the host target (§9). Every
remaining error is a site; the list below is the expected set from the identifier inventory and must
match the compiler's list in P2 (a mismatch is investigated, not papered over).

| File:line (c330c0a) | Today | Change |
|---|---|---|
| `voxel_world.c:35-37` | `extern const struct Tileset gTileset_General, gTileset_Fortree, gTileset_GenericBuilding;` | **deleted**; `gba_game.h` defines `#define gTileset_General (*vx_tileset_at(GBA_ADDR_TILESET_GENERAL))` (same for the other two), so `&gTileset_General` is the interned host pointer and every `== &gTileset_X` comparison keeps working unchanged |
| `voxel_world.c` (4 sites, `gMain.callback2 == CB2_Overworld…`) | function-pointer compare | unchanged text; `gMain.callback2` is a `GbaPtr`, `CB2_Overworld`/`CB2_OverworldBasic` are `#define … 0x08085E5Du`/`0x08085E51u` |
| `voxel_world.c` (`LZDecompressWram`) | pret BIOS-call wrapper | macro → `vx_lz77_decode(src, dst, dstCap)` (ours) |
| `voxel_world.c` (`GetMapHeaderFromConnection` ×3) | pret | adapter function, interned result |
| `voxel_world.c` (`GetObjectEventGraphicsInfo` ×2) / `voxel_entities.c` | pret | adapter: intern via `gObjectEventGraphicsInfoPointers[id]`; id ≥ 239 ⇒ the id-0 entry (pret's own fallback behaviour, re-expressed) |
| `voxel_world.c` (`GetCurrentWeather`, `gWeatherPtr->…` ×7) | pret | `GetCurrentWeather()` → `gWeatherPtr->currWeather`; `gWeatherPtr` = `&g_vxWeather` (host decode) |
| `voxel_world.c` (`MetatileBehavior_Is*` ×9, `MB_*` ×6, `UNPACK_BEHAVIOR`) | pret | `vx_behavior.c` (ours): `MetatileBehavior_IsX(u8 b)` = membership in a behavior-id set from SPEC-data's behavior table |
| `voxel_world.c` (`gPlttBufferUnfaded`, `PLTT`, `BG_PLTT_ID`) | EWRAM/hardware | snapshot arrays |
| `voxel_world.c:535` | Fortree special case | §2.6 |
| `voxel_entities.c:~201` (`GatherSource`) | `(const u8 *)VRAM_ + 0x10000`, `REG_DISPCNT & DISPCNT_OBJ_1D_MAP` | `vx_snap()->vramObj`, `vx_snap()->dispcnt & 0x40` |
| `voxel_entities.c:~341` (`RefreshSlot`) | `(const u16 *)(PLTT + 0x200)` | `vx_snap()->pltt + 256` |
| `voxel_entities.c:668-687` (`IsTemplate`, `IsDecal`, `IsTileEffect`) | `const struct SpriteTemplate *t` compared to `gFieldEffectObjectTemplatePointers[FLDEFFOBJ_X]` | param type → `GbaPtr`; array is our host `GbaPtr[37]`; `FLDEFFOBJ_*` indices are constants in `gba_game.h` (TALL_GRASS 4, LONG_GRASS 15, SURF_BLOB 7, SHADOW_S 0, SHADOW_XL 3, SAND_FOOTPRINTS 11, DEEP_SAND_FOOTPRINTS 23, RIPPLE 5, BIKE_TIRE_TRACKS 27 — **P2a**: confirm each index against the ROM table by template content) |
| `voxel_entities.c:~291-312` (`StandingFootPad`: `Port_GetSpriteFrameSize`, `Port_PeekSpriteFramePointer`) | upstream port helpers | adapter: frame size = `images[0].size`; peek = host pointer of `images[frame].data` in ROM |
| `voxel_entities.c:857-866, 905-906, 929` | `sprite->template`, `gSprites[...]`, `CtrSprite_IsVoxelWeather` | type change only; weather id §4.6 |
| `voxel_atlas.c:~163, ~221` (`TilesetOf`) | `Port_GetMapLayoutById` | adapter (§2.3) |
| `voxel_atlas.c` (`NUM_*` ×26, `RGB2`) | pret constants | `gba_game.h` constants: NUM_TILES_IN_PRIMARY 512, NUM_METATILES_IN_PRIMARY 512, NUM_METATILES_TOTAL 1024, NUM_TILES_PER_METATILE 8, NUM_PALS_IN_PRIMARY 6, NUM_PALS_TOTAL 13, TILE_SIZE_4BPP 32, MAX_SPRITES 64, OBJECT_EVENTS_COUNT 16, MAP_OFFSET 7, MAPGRID_* masks; `RGB2(r,g,b)` → `GBA_RGB5(r,g,b)` |
| `voxel_world.c` (`MAP_TYPE_*`, `CONNECTION_*`, `WEATHER_*` ×13, `WEATHER_PAL_STATE_*`) | pret enums | `gba_game.h` values from SPEC-data (map types, connection dirs 1..6 ✅; weather ids **P2a**) |
| `ctr_voxel.c:4257, 4336, 4338` | `VoxelBattle_*` | resolve to our stub (§2.9), no text change |

### 2.5 Asset size registry

Filled when a Tileset is interned:
- `tiles`, compressed (`isCompressed`): run `vx_lz77_scan(romPtr, romEnd)` → packed length (header 0x10,
  decoded size in bytes 1-3; walk flag bytes until the decoded count is reached). Fails (bad header / runs
  off ROM) ⇒ tileset rejected ⇒ map unavailable.
- `tiles`, uncompressed: primary 512×32 = 16384 B, secondary 512×32 = 16384 B, clipped to `romSize - off`.
- `palettes`: 16×16×2 = 512 B; `metatiles`: NUM_METATILES×16 B; `metatileAttributes`: NUM_METATILES×2 B
  (primary 512, secondary 512); clipped to ROM end. `Port_GetAssetSizeExact` returns 0 for metatiles if
  unknown, which upstream treats as "use the limit" (OK).

### 2.6 Fortree special case (`voxel_world.c:535`)

Upstream special-cases Fortree's treehouse floors. Re-express as a **tileset identity check from our
docs**: `layout->secondaryTileset == &gTileset_Fortree`, with `gTileset_Fortree` resolving to the
interned object at **0x083DF7C4** (SPEC-data §10.1). The comment cites SPEC-data, not pret.

### 2.7 `sStepFrames {16,8,6,4,2}` (`voxel_entities.c:33`)

Upstream table of frames per tile-step by movement speed (walk-slow…fastest). Per LEGAL, we
**re-derive by measurement**: in P2 the host test replays a recorded gamestate log (`sdmc:/cias/netlogs/`
gs lines from the phase-13 logger) and counts frames between `currentCoords` changes per movement speed;
the table is then written from those counts with a comment citing the measuring test, and
`voxel_entities.c:33` keeps upstream's line with our citation comment ("values re-measured,
test/host/test_voxel_entities.c:STEP"). If measurement disagrees, the measured value wins.

### 2.8 `sSolidColors` (`voxel_atlas.c`)

Confirmed **port-chosen**: a small table of flat colours the atlas uses for untextured voxel faces
(not palette data copied from the game; values do not appear in the ROM's palettes as a block). Kept as
vendored MIT code; only `RGB2` → `GBA_RGB5`.

### 2.9 Battle stub (`source/voxel/vx_battle_stub.c`, ours)

```c
bool VoxelBattle_GameInBattle(void);      /* return vx_snap()->inBattle; */
bool VoxelBattle_IsLink(void);            /* false */
void VoxelBattle_BeginStage(void);        /* {} */
bool VoxelBattle_StepStage(void);         /* false */
unsigned VoxelBattle_Shadows(void);       /* 0 */
```
`CtrVoxel_BeginBattle` is never called. Battle scenery = a future phase. The voxel gate is off in battle
anyway (cb2 ≠ overworld), so these only satisfy the link.

### 2.10 P2a offset probe

`tools/voxel/offsets.sh` (ours): compiles a throwaway `offsetof` probe with `arm-none-eabi-gcc` against a
**pret checkout outside the repo** (`$PRET_DIR`, never vendored) and prints numbers; the numbers — facts,
not expression — are written into `gba_game.h`'s `GBA_OFF_*` table with a "measured by offsets.sh" comment.
Then a **runtime sanity check** on a real save (host test with a RAM dump, §9.3) confirms: player object
`isPlayer` set, its coords == `gSaveBlock1Ptr->pos`, its spriteId's `gSprites[].inUse`, `gMain.callback2`
== CB2_Overworld while walking, `gPaletteFade.active` toggles across a door. A failed check blocks the rest of P2.

### 2.11 Tileset animation

The emulator has no `CtrVoxel_NotifyTilesetAnimWrite` hook (upstream patches the game's
`TransferTilesetAnimsBuffer`). Ours: the snapshot keeps BG char VRAM 0x06000000-0x06007FFF (32 KiB).
After the copy, `vx_tileanim_diff()` compares it 32 B (one 4bpp tile) at a time against the previous
snapshot (1024 tiles; u32-wise compare, ~8 K compares/frame) and calls
`CtrVoxel_NotifyTilesetAnimWrite(first, count)` per changed run, **from the main thread after
`C3D_FrameEnd`** (inside our AfterSubmit wrapper). A map load (layout change) suppresses notify for that
frame (the whole atlas rebuilds anyway). `CtrVideo_GetBgVram()` returns the snapshot copy.

---

## 3. Threading and the snapshot

### 3.1 Where the parked window is

`main.c:3505-3511` (non-menu path): `LightEvent_Wait(&emuA.done); LightEvent_Wait(&emuB.done);` then
`upload_frame` for both cores. Between that wait and the kick at `main.c:4044-4045`
(`LightEvent_Signal(&emuA.go/emuB.go)`) both workers are **parked**: GBA RAM/VRAM/PLTT are stable and
safe to read from the main thread. After the kick the workers run the next frame **concurrently with
`C3D_FrameBegin` (4559) … `C3D_FrameEnd` (5369) and AfterSubmit** — so nothing after 4044 may read live
GBA memory. This is why the snapshot is mandatory, not an optimisation.

Under `linkOn || netOn || wlOn` the guard at 3505 is false: workers free-run and **there is no parked
window**. The voxel gate is therefore off under any link (§5.3, `linkOn` added to G4's wl/net).

### 3.2 `vx_snapshot_take(const GbaCore *top)` — call site and rule

Inserted immediately after the two `upload_frame` calls at `main.c:3510` (inside the same `if`), and only
when `voxCandidate` is true (setting on ∧ data present ∧ game code BPEE ∧ N3DS ∧ !linkOn/netOn/wlOn —
the cheap, frame-stable part of the gate; computed from the previous frame's state). `top` is the core
whose frame goes to the top screen (`emuA` unless the screen-swap setting puts emuB there — reuse the
same selector `render_game` uses for `topL`). Off ⇒ no call ⇒ zero cost and byte-identical behaviour.

It reads host pointers from `gbacore_mem_block()` (EWRAM 0x2, IWRAM 0x3, PALETTE 0x5, VRAM 0x6,
mGBA `core.c:1150`) with plain `memcpy` — **read-only**; no `busWrite*`, no `gbacore_write*`, ever.

### 3.3 What is copied per frame (byte cost)

| Item | Source | Bytes |
|---|---|---:|
| backup map cells | `sBackupMapData`, `w*h*2` with w,h from `gBackupMapLayout` (A2: w×h ≤ 10240) | ≤ 20 480 |
| `gBackupMapLayout` | IWRAM 0x03005DC0 | 12 |
| `gMapHeader` | EWRAM 0x02037318 | 28 |
| SaveBlock1 head | `*gSaveBlock1Ptr` (ptr @0x03005D8C), pos + group/num | 8 |
| `gObjectEvents` | 0x02037350 | 576 |
| `gPlayerAvatar` | 0x02037590 | 36 |
| `gSprites` | 0x02020630 | 4 420 |
| `gMain` callback2 + flags byte | 0x030022C4, 0x030026F9 | 5 |
| `gPaletteFade` | 0x02037FD4 | 12 |
| `gWeather` fields | 5 bytes at the P2a offsets | 5 |
| `gPlttBufferUnfaded` | 0x02037714 | 1 024 |
| PLTT (hardware, post-fade) | 0x05000000 | 1 024 |
| VRAM BG char | 0x06000000 | 32 768 |
| VRAM OBJ | 0x06010000 | 32 768 |
| DISPCNT, BLDCNT, BLDALPHA, BLDY | IO 0x04000000/0x50/0x52/0x54 via `gbacore_read16` | 8 |
| **Total** | | **≈ 93.2 KiB** worst case |

Cost: ≈93 KiB memcpy FCRAM→FCRAM at 804 MHz + L2 ≈ 0.3-0.5 ms (estimate; measured in P3 by the existing
frame-timing diag, `HW-1` in §10). Resident: one snapshot (93 KiB) + previous BG VRAM for the anim diff
(32 KiB) = **≈125 KiB heap** (malloc, not linear — the GPU never reads it).

One buffer suffices: the snapshot is written only in the parked window and read only by the main thread
later in the same iteration (decode before `C3D_FrameBegin`, `CtrVoxel_Update` after it, AfterSubmit
after `C3D_FrameEnd`); the next write happens one full iteration later, after AfterSubmit returned.

### 3.4 Decode (main thread, before `C3D_FrameBegin`)

`vx_adapter_decode(&snap)` turns the raw snapshot into the host globals (§2.1 rule 3): validates A1-A15
(SPEC-data §10.2), interns ROM objects reached from `gMapHeader` (first visit of a map pays the intern +
LZ scan; later frames are cache hits), rebases `gBackupMapLayout.map` into `snap.map`. Fails ⇒
`snap.valid=false` ⇒ `VoxelWorld_IsMapAvailable()` false. Order: decode runs before the gate is
evaluated (the gate needs `cb2`, `sb1Valid`, ctx).

### 3.5 Who may read what, when

| Phase | Thread | May read | Must not |
|---|---|---|---|
| parked window (3505-4044) | main | live GBA memory (read-only), ROM | write any GBA memory |
| `CtrVoxel_Update` (after 4559) | main | snapshot, host globals, ROM buffer | live EWRAM/VRAM/PLTT/IO |
| `CtrVoxel_Draw` | main | same + GPU | — |
| `CtrVoxel_AfterSubmit` (after 5369) | main | snapshot, ROM buffer, voxel caches | live GBA memory (workers running) |
| emu workers | cores 1/2 | their own core | any voxel state |

The vendored code starts **one** thread of its own: `StreamWorker` (`ctr_voxel.c:980`, created at
:1095 on core 1, fallback :1099 on the app core), which reads building pages from the data file
(`VoxelBuildings_ReadPage`) and flushes them with `GSPGPU_FlushDataCache` — file I/O + a cache flush, **no
GPU commands and no GBA memory**, so it does not breach "one render thread owns the GPU". Kept as-is;
it lands on core 1 next to our audio thread (`audio.c:207`). Our `APT_SetAppCpuTimeLimit(80)`
(`main.c:5677`) satisfies its `limit >= 30` check (:1093), so it never lowers our limit. Its priority must
be **below** the audio thread's (`mainPrio - 1`): the shim passes `mainPrio + 2` (the vendored line takes
the priority from a variable set in `StreamStart`; that one assignment is a marked modification).
`CtrAssets_Prefetch*` (the other StreamWorker job) is inert in our build (§4.3). The ROM buffer is the
only memory shared with a running emulator worker, and it is immutable.

**Core-0 contention (important).** `emuA` runs on core 0 at `mainPrio + 1` (`main.c:3120`), i.e. on the
main thread's core at lower priority — it only gets CPU while main is blocked (vsync/GPU waits). Every
millisecond of voxel CPU work on the main thread (decode, `CtrVoxel_Update`, `AfterSubmit` chunk builds)
is a millisecond taken from emuA's frame. `AfterSubmit` therefore runs under a hard per-frame budget
(§4.4 `CtrPlatform_GetTiming`), and the frame-budget HW check (§10 HW-1) is the gate for P6.

### 3.6 Image/world skew

The overlay image uploaded at 3510 is frame N-1's; the snapshot is RAM at the end of N-1. Both describe
the same emulated frame; the only skew is the GBA's own (game logic runs in vblank, after the frame
scanned out), i.e. the world may lead the BG0 text by ≤1 frame. Invisible in practice (text boxes do not
move with the camera).

---

## 4. The shims (`source/voxel/ctr_shims.c/.h`, `vx_data.c`; all ours)

Every upstream platform symbol the vendored files call, with our semantics. Upstream's implementations
(`3ds_video.c`, `3ds_assets.c`, `3ds_platform.c`, `3ds_data.c`, `3ds_pak.c`) are **not** vendored; the
pure helpers below are re-expressed from their documented behaviour (bit layouts of the PICA200 tiling and
of RGBA5551 are hardware facts).

### 4.1 `CtrVideo_*`

| Symbol | Callers | Our semantics |
|---|---|---|
| `uint32_t CtrVideo_Texel(unsigned x, unsigned y, unsigned width)` | ctr_voxel ×4, atlas ×4, entities ×2 | PICA200 8×8 Morton tile index: `((y>>3)*(width>>3) + (x>>3))*64 + morton(x&7, y&7)`, morton interleaving x bits to even, y bits to odd positions. Host-tested against a brute-force reference (§9) |
| `uint16_t CtrVideo_RGBA5551(uint16_t bgr555)` | grade ×2 | GBA BGR555 → PICA RGBA5551, alpha 1: R→bits 11-15, G→6-10, B→1-5 |
| `unsigned CtrVideo_ObjTile(unsigned base, unsigned x, unsigned y, unsigned width, bool color256, bool mapping1d)` | entities | GBA OBJ tile index of the 8×8 cell (x,y) of a sprite: 1D mapping row stride `width/8` tiles, 2D stride 32; 8bpp counts 2 tile units per cell and clears base bit 0; result `& 1023` |
| `bool CtrVideo_TryVoxelUpload(void)` | ctr_voxel ×7 | reserve **2** GX commands for one upload (a texture copy + its flush); false when it would exceed the budget (§4.2) |
| `unsigned CtrVideo_VoxelUploadsLeft(void)` | ctr_voxel:4201 | GX commands still free this frame for voxel uploads (§4.2) |
| `void CtrVideo_RequestPlaneRelease(void)` | ctr_voxel:2071, 2185 | upstream frees its compositor's 3D depth planes to make VRAM for atlases. We have no such planes: **no-op** + a one-shot log; the atlas stays "capacity blocked" and the status string reports it (VRAM sizing §4.7 must make this unreachable on a normal map) |
| `void CtrVideo_Present(void)` | ctr_voxel (comments + 2 calls in the battle/warm-up path) | upstream's frame open/close. **Never called in our build**: both call sites are in paths gated by `CtrVoxel_BeginBattle`/standalone warm-up that we do not enter; the shim is `assert(0)`-in-debug, no-op in release, so a wrong path is caught on the host |
| `const CtrVideoStats *CtrVideo_GetStats(void)` | ctr_voxel:3414 (diag) | static struct, fields zero except `frames` (our frame counter) |
| `const uint8_t *CtrVideo_GetBgVram(void)` | ctr_voxel:4206 | snapshot BG char VRAM (§2.11) |

### 4.2 GX queue budget

citro3d's GX command queue has **32 entries per frame**; overflow is a `svcBreak` in `gxCmdQueueAdd`
(a hard crash, not a dropped command). Upstream knows the live fill by wrapping `GX_BindQueue`
(`3ds_video.c:20-24`, `-Wl,--wrap=GX_BindQueue`) and keeps `sRenderReserve` (24, or 28 in battle) for its
own frame. We do the same, ours:

- `LDFLAGS += -Wl,--wrap=GX_BindQueue`; `__wrap_GX_BindQueue` records the queue pointer, then calls
  `__real_GX_BindQueue`.
- `TryVoxelUpload`: `used = max(queue->numEntries, sVoxCmds)`; succeed iff `used + 2 + reserve ≤ 32`.
- **Our reserve = 16**, from a count of our own per-frame transfers with voxel on: 2 `upload_frame`
  DisplayTransfers (`main.c:703-712`) — both already queued by the time Update runs, so they show in
  `numEntries` — + 1 overlay DisplayTransfer (§6.4) + up to 2 for presence/pop sheets (disabled under
  voxel, §5.4, counted anyway) + C3D's own per-frame command-list submits for **each target drawn**
  (top-L, top-R in stereo, bottom: 3, plus 1 each for the voxel escape's flush, citro2d restore, and
  the bottom-screen UI = ~6) + 5 margin. P6 measures the real peak (log `queue->numEntries` high-water
  at `C3D_FrameEnd`) and replaces 16 with peak + 4.
- `VoxelUploadsLeft` = `32 - used - reserve` (≥ 0). `VOXEL_CHUNK_UPLOADS_MAX` (8) stays the upper cap.
- `sVoxCmds` reset at `C3D_FrameBegin` (our wrapper call right after `main.c:4559`).

### 4.3 `CtrAssets_*`

Upstream's asset system serves **pret game data from its pak** (pointer → payload) and prefetches
payloads near the view (`ctr_voxel.c:1002-1117`). In our build every game payload is already in the ROM
buffer (host pointers, §2.1), so:

| Symbol | Ours |
|---|---|
| `bool CtrAssets_PrefetchFind(const void *ptr, CtrAssetPrefetch *out)` | `false` always (nothing to prefetch) → the ahead-reader never starts |
| `void *CtrAssets_PrefetchRead(const CtrAssetPrefetch *)` | `NULL` (unreachable) |
| `void CtrAssets_PrefetchAdopt(const CtrAssetPrefetch *, void *)` | no-op (unreachable) |
| `const struct CtrAssetStats *CtrAssets_GetStats(void)` | static zeroed struct (`evictions`, `misses` stay 0, so the diff logic at :1019, :3584 is inert) |

`CtrAssetPrefetch`/`CtrAssetStats` are declared in our `ctr_shims.h` with the field names the vendored
code reads (`index`, `size`, `path`; `evictions`, `misses`).

### 4.4 `CtrPlatform_*`, `CtrLog`, `CtrSettings_*`, `CtrSprite_*`, `PORT_LOG`

| Symbol | Ours |
|---|---|
| `const CtrTiming *CtrPlatform_GetTiming(void)` | struct with `frameMs, workMs, peakWorkMs, gameMs, vblankMs, bottomMs, slowFrames` (names as read at :3455, :3908). Budget mechanics upstream: `SpareMs` (:3452) = `VOXEL_WORK_TARGET_MS - (workMs - lastBuildMs)`; AfterSubmit (:3922) = `VOXEL_FRAME_MS - (now - frameBeginTick) - sOthersMs - VOXEL_AFTER_MARGIN_MS`, `sOthersMs` tracking `gameMs + vblankMs` (capped at `VOXEL_OTHERS_CAP_MS`). Ours: `frameMs` = our frame period, `workMs` = main-thread CPU ms of the last frame (existing diag timer), **`gameMs` = emuA's last `emu_step` ms** (emuA shares core 0 with main, §3.5 — that time is exactly the "others" share the vendored budget must leave free), `vblankMs` = 0, `frameBeginTick` = `svcGetSystemTick()` taken right before `C3D_FrameBegin` (`main.c:4559`). Result: AfterSubmit only builds in core-0 time emuA does not need. P6 verifies on hardware that emuA's frame time does not regress with voxel on (HW-1) |
| `void CtrLog_Write(int channel, const char *fmt, ...)` (×36) | → our existing diag log ring (`diag_log` / `sdmc:/cias/netlogs/` when logging is on), prefix `VOX:`; channel constants `CTR_LOG_ERROR/FS/VOXEL…` defined in `ctr_shims.h` |
| `PORT_LOG(...)` | macro → `CtrLog_Write(CTR_LOG_VOXEL, ...)` |
| `int CtrSettings_VoxelPitch(void)` / `CtrSettings_VoxelZoom(void)` | our `g_settings.voxPitch` / `voxZoom` mapped through the upstream ranges (§8) |
| `bool CtrSprite_IsVoxelWeather(const struct Sprite *s)` | §4.6 |
| `void CtrAudio_StartWorker(void)` | removed from ctr_voxel (§1.2); not defined |

### 4.5 `CtrData_Open` — pak + loose fallback (`vx_data.c`)

The vendored code opens data through `VoxelFile_Open(path)` (`voxel_file.h`) →
`CtrData_Open(path)` returning a `FILE *`. Paths used: `voxel/regions.bin` (`voxel_regions.c:10`),
`voxel/relief.bin` (`voxel_relief.c:41`), `voxel/buildings.bin` (`voxel_building.c:44`, read by the
page stream), and the sign file opened by `voxel_sign.c`.

Lookup order (first hit wins; result cached per session):

1. **Pak** `sdmc:/3ds/emerald3ds/emerald3ds.pak`.
2. **Loose** `sdmc:/3ds/3DGBA/voxel/<basename>` (e.g. `…/voxel/relief.bin`).
3. Neither ⇒ `NULL` ⇒ `CtrVoxel_Init` fails ⇒ status "data missing" (§8.3).

Pak format at v0.2.0 (the file format is a fact we read; the reader is our own code):

- **Header, 64 B, little-endian:** magic `"EM3DPAK\0"` @0; schema u32 @8 (must be **1**); engineAbi u32 @12;
  romSha1[20] @16; entryCount u32 @36 (1 ≤ n ≤ 2^20); indexOffset u64 @40; dataOffset u64 @48;
  indexCrc u32 @56; headerCrc u32 @60 = CRC-32 (IEEE, reflected, poly 0xEDB88320) of bytes 0..59.
- **Index:** `entryCount` × 40 B at `indexOffset`, CRC-32 over the whole index = `indexCrc`. Entry: id u64 @0
  (FNV-1a 64 of the path string exactly as given, e.g. `"voxel/relief.bin"`; offset basis
  0xcbf29ce484222325, prime 0x100000001b3), type u32 @8, flags u32 @12 (bit 0 = compressed — **must be 0**
  at v0.2.0), offset u64 @16 (≥ dataOffset), storedSize u32 @24 (== rawSize), rawSize u32 @28, crc32 u32
  @32, reserved @36. Ids strictly ascending ⇒ binary search.
- **Checks ours enforces:** magic, header CRC, schema 1, index CRC, ascending ids, uncompressed,
  bounds (`offset + size ≤ file size`), per-entry CRC on first open (≤ 1 MiB entries; larger ones are
  checked lazily by the page stream's slice reads).
- **engineAbi: not enforced.** Upstream compares it against its own `romfs:engine/abi.bin`, which we do
  not have. We log it. The voxel files' own headers (each `voxel/*.bin` carries its own magic/version,
  checked by the vendored readers) are the real compatibility gate.
- **romSha1:** compared against the **SHA-1 of the loaded ROM** (computed once at ROM load over
  `romSize` bytes with our SHA-1, ~0.15 s for 16 MiB; only when VOXEL 3D is on or the data check is
  requested). Mismatch ⇒ pak rejected, status "data built for a different ROM". Upstream's supported ROM is
  Emerald (USA, Europe), SHA-1 `f3ae088181bf583e55daf962a92bb46f4f1d07b7`.
- **Returned handle:** `FILE *` via `fopencookie` over (pakFile, entry.offset, entry.rawSize) — read/seek
  clamped to the entry. newlib on devkitARM provides `fopencookie` (upstream relies on it too).

Loose fallback: plain `fopen`, no CRC (the vendored readers' own header checks apply). Never
redistributed: `.gitignore` gains `*.pak` and `voxel/*.bin` at repo root; closeout greps `git ls-files`.

### 4.6 Weather sprite identification (`CtrSprite_IsVoxelWeather`)

Upstream decides from the game's own sprite callbacks (via a pret patch, `0005-port-hooks.patch`). Ours
decides from `sprite->template` (a `GbaPtr`) ∈ the set of weather `SpriteTemplate` ROM addresses (BPEE,
from the sym file):

| Weather object | Template address |
|---|---|
| cloud | 0x0854FB78 |
| rain | 0x0854FC2C |
| snowflake | 0x0854FC8C |
| fog (horizontal) | 0x0854FD18 |
| ash | 0x0854FD58 |
| fog (diagonal) | 0x0854FD8C |
| sandstorm | 0x0854FDC4 |
| bubble (underwater) | 0x0854FE44 |

Linear scan of 8 u32s. A sprite with `inUse` and one of these templates is excluded from the voxel
billboards (it is a screen-space effect). Under overlay option A (§6) these sprites are not drawn at
all in voxel mode — see §6.5.

### 4.7 Memory: VRAM arenas and the linear heap

**VRAM (6 MiB)** — vendored allocations at Init: chunk meshes `vramAlloc(1536 KiB)` (:1602), pages
`vramAlloc(768 KiB)` (:1609), atlases up to `VOXEL_ATLAS_SLOTS` 6 × 512×256 RGBA5551 = 6 × 256 KiB =
1.5 MiB (allocated on demand) ⇒ **3.75 MiB**. Depth: we draw into our existing top targets (§5.2), which
have no depth buffer today; at lazy init **one** D24S8 buffer (240×400×4 = 375 KiB, `vramAlloc`) is attached
to both `top` and `topR` with `C3D_FrameBufDepth` (the eyes render sequentially and each clears it).
Voxel + depth ≈ **4.1 MiB**. Ours today: top L/R + bottom colour targets (RGBA8, 375+375+300 KiB ≈ 1.05
MiB) plus whatever offscreen targets/textures our passes keep in VRAM (preTgt, DoF/bloom targets —
**measure**). Sum ≥ 5.15 MiB of 6 — tight. Rule: `CtrVoxel_Init`
runs **lazily on first gate-on**, and logs `vramSpaceFree()` before/after; if `vramSpaceFree() <
VX_VRAM_NEED` (4.2 MiB) it does not init and the status reads "not enough VRAM". P3 measures the true
free figure on hardware (HW-3).

**Linear heap** — vendored linearAllocs: `sStaging` 12288 × 16 B = 192 KiB (:1624), `sDynamic`
`VOXEL_DYNAMIC_VERTICES` × 16 B (:1628, < 64 KiB), `sAtlasStaging` 512×256×2 = 256 KiB (:1642), `sStream.buffer`
`VOXEL_PAGE_SLICE` × 2 = 128 KiB (:1071), `sAnimOut` (:1688, small), rays/motes/fog sheets (:4998, 5104,
5287, < 100 KiB together), sprite atlas `C3D_TexInit` 256×256 RGBA5551 = 128 KiB, tree atlas 64×64×2 =
8 KiB, plus our overlay texture 256×256×2 = 128 KiB and its linear source 240×160×2 = 75 KiB. Total ≈
**1.1 MiB**. Upstream sets `__ctru_linear_heap_size = 8 MiB` (`main_3ds.c:10`); we have **no override**
(libctru default: a share of the app heap — under SystemMode 64MB/Ext 124MB this is ≥ 24 MiB, **measure**
with `linearSpaceFree()` at boot, HW-3). mGBA's two ROM buffers (32 MiB each via `ctru-heap.c`) are regular
heap, not linear. Expectation: 1.1 MiB fits comfortably; P3 logs `linearSpaceFree()` before/after Init.

**Main heap** — `sScratch` malloc((8192+1152) × sizeof(VoxelVertex)), `sDynamicScratch`, world caches
(arena), our snapshot 125 KiB (§3.3), the intern cache (< 64 KiB for all of Hoenn's tilesets/layouts
touched in a session; payloads are not copied). Decompressed tile sheets live in the vendored world's own
buffers (≤ 2 × 16 KiB per instance).

---

## 5. Render insertion in `main.c`

Line numbers are the working tree of 2026-10-05 (HEAD 12d2492 + the uncommitted phase-32-T edits);
re-anchor by the quoted code, not the number.

### 5.1 Frame order with voxel on

```
3505  if (!linkOn && !netOn && !wlOn && workersRunning) { Wait(A.done); Wait(B.done);
3510      upload_frame(A); upload_frame(B);
NEW       if (voxCandidate) { vx_snapshot_take(topG); vx_overlay_note_uploaded(topG); }   // §3.2, §6.6
          workersRunning = false; }
 ...      (gs log / tiltSnap fill — unchanged)
NEW   vx_adapter_decode();                                // §3.4, only if a snapshot was taken
NEW   voxIn = …; voxTop = voxel_gate(&voxIn);             // §5.3 — before the overlay mask decision
NEW   gbacore_set_overlay_mode(topG->core, vx_overlay_want(voxTop));   // §6.6, still parked
4044  LightEvent_Signal(&emuA.go); LightEvent_Signal(&emuB.go);   // workers run frame N
 ...
NEW   vxFrameTick = svcGetSystemTick();
4559  C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
NEW   vx_frame_begin();                                    // reset GX-command count (§4.2)
NEW   voxReady = voxTop && CtrVoxel_Update();              // after FrameBegin (ctr_voxel.h contract)
NEW   voxBlank = voxTop && !voxReady && CtrVoxel_IsWarmingUp();
 ...      tilt gate block 4567-4625 (tiltTop now also requires !voxTop, §5.4)
4871  render_game(topG, top, …)        → replaced by vx_draw_eye(top, -eye) when voxDraw (§5.2)
5113  render_game(topG, topR, …)       → replaced by vx_draw_eye(topR, +eye) when voxDraw && s3dOn
5151  render_game(botG, bot, …)        unchanged — the bottom screen never shows voxel
5369  C3D_FrameEnd(0);
NEW   if (voxReady) vx_after_submit(vxFrameTick);          // CtrVoxel_AfterSubmit + tile-anim notify (§2.11)
```

`voxCandidate` (for the snapshot at 3510) is the frame-stable subset of the gate, computed at the end of
the previous iteration: `g_prefs.voxel && vxDataOk && topIsBPEE && isN3DS && !linkOn && !netOn && !wlOn`.

### 5.2 `vx_draw_eye(C3D_RenderTarget *t, float eye)` — the top-screen draw

`voxDraw = voxReady || (voxBlank) || vx_overlay_needs_world()` (the last term is the one-frame lag rule of
§6.6). Body:

1. `C2D_TargetClear(t, clrSky)` — clears colour **and depth** (citro2d's clear covers the depth buffer when
   the target has one; a D24S8 depth buffer is attached to the two top targets at lazy voxel init with `C3D_FrameBufDepth`, §4.7). `clrSky` = black.
2. If `voxReady` (including the closing frame of §6.6): `C2D_Flush();` then
   `CtrVoxel_Draw(t, eye)` — the vendored draw binds its own program, attribute layout, buffers, texenv,
   depth test, cull mode and calls `C3D_FrameDrawOn(t)` (`ctr_voxel.c` ~5713); it never opens or closes a
   frame. **Full 400×240**: the voxel camera's viewport is the whole top target (Zallax renders into a 400-
   wide logical surface, `CTR_GAME_WIDTH 400`, `3ds_video.h:12`); there is no 240×160 letterbox and
   `scaleMode[0]` is ignored in voxel mode.
3. **citro2d restore** (the same escape pattern as `tilt_draw_image`, `main.c:1305-1378`, and SPEC-render
   §6.3's list): `C2D_Prepare()` (re-binds citro2d's shader program, attribute info, buffer info, texenv
   stages and the 2D projection uniforms), `C3D_DepthTest(false, GPU_GEQUAL, GPU_WRITE_ALL)`,
   `C3D_CullFace(GPU_CULL_NONE)`, `C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA,
   GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA)`, `C3D_FragOpMode(GPU_FRAGOPMODE_GL)`,
   reset `C3D_FogGasMode(GPU_NO_FOG, …)` (the vendored lighting may enable fog), then `C2D_SceneBegin(t)`.
   Anything citro2d draws afterwards (overlay, HUD, toast, focus bar) is then in the state it expects.
4. Overlay: draw the overlay texture (§6) as one `C2D_DrawImageAt` quad, **at the GBA image's on-screen
   rect** for `scaleMode[0]` (the same `sx/sy`, centring as `render_game`, `main.c:2397-2400`), so BG0 text
   lands where the player is used to it. Alpha 0 texels show the world.
5. `voxBlank` (warming up, no world yet): steps 2-3 skipped; draw the **unmasked** flat frame as today
   (`render_game(...)` as before). Never a black hole — invariant 1's spirit (§6.6 guarantees the frame
   being shown was rendered unmasked).

Everything after (HUD text, toasts, `draw_load_error`, the focus bar) draws as today.

### 5.3 The gate — `int voxel_gate(const VoxGateIn *in)` (new, pure C, in `tilt.c`/`tilt.h`)

Reuses the tilt gate's predicates where the meaning is the same; same per-screen snapshot (`tiltSnap[0]`,
the top screen's game).

| Rule | Source | Voxel |
|---|---|---|
| G2 N3DS | `tilt_target_level` | **kept**: `isN3DS` required |
| G3 our menu open | kept | off while `menuOpen` |
| G4 wl/net | kept **+ `linkOn`** | off under any link — no parked window (§3.1) |
| G5 profile ok | kept | `ok` |
| G6 ctx == OVERWORLD | **replaced** | `ctx ∈ {GCTX_OVERWORLD, GCTX_FIELDMENU}` (START menu and field menus overlay the world via BG0) |
| G7 sb1Valid | kept | required |
| G8 textDlg | **dropped** | text boxes are BG0 → they overlay the world (§6); no reason to drop to flat |
| G9 touch | n/a | bottom only, voxel is top only |
| G10 stereo wins | **dropped** | voxel supports stereo (§7) |
| G11 fsOn | kept | off when the top game is the frameskip-starved one (`fsOn && focScreen != 0`) |
| new: user setting | `g_prefs.voxel` | VOXEL 3D on |
| new: game | ROM header code | `BPEE` only (addresses are BPEE; FR/LG/RS = off, status "Emerald only") |
| new: data | `vxDataOk` | pak or loose data opened and validated (§4.5) |
| new: callback | snapshot `gMain.callback2` | ∈ {0x08085E5D, 0x08085E51} — the same test as `VoxelWorld_IsMapAvailable()`; covers battle, bag, summary, title, warps-in-progress |
| new: snapshot | `snap.valid` | A1-A15 passed this frame |
| new: init | `CtrVoxel_IsAvailable()` | lazy Init succeeded (VRAM/linear budget, §4.7) |

Returns 0/1. Host-tested like `tilt_target_level` (§9.1). Hysteresis: none in the gate itself — the
overlay state machine (§6.6) and the warm-up blank already smooth transitions; door/warp transitions
fade through the game's own palette fade, which the vendored code reads (`gPaletteFade`).

### 5.4 Exclusions while `voxTop`

Mutually exclusive with the HD-2D tilt and the stereo passes that assume a flat GBA image on top (the
same six sites SPEC-render §1.2 lists for the diorama, re-anchored):

| Site (today) | Change |
|---|---|
| `bool tiltTop = tilt_active(&tiltTw[0]);` (~4826) | `&& !voxTop` |
| `bool popPass = pop3d && !tiltTop;` (4836) | `&& !voxTop` |
| `bool uipop = s3dOn && …` (4837) | `&& !voxTop` |
| `bool dofPass = …` (4846) | `&& !voxTop` |
| `bool bloomPass = …` (4847) | `&& !voxTop` (the vendored world has its own bloom, `CtrVoxel_Bloom()`; not composited in phase 32) |
| `bool litPass = …` (4849) | `&& !voxTop` (the vendored world has its own time-of-day/weather lighting) |
| presence draw on top (phase 15 sites, ~4841 / ~5076 in SPEC-render's numbering) | skipped for the top screen while `voxTop` (presence anchors are in GBA pixel space, wrong over a 3D camera) |

The tilt gate also gets `gi.userLevel = voxTop ? 0 : …` for the top screen so the tween parks at 0 (no
snap) when voxel engages.

### 5.5 Same-frame fallback

`CtrVoxel_Update()` can return false on any frame (`ctr_voxel.c:4252` "off", :4277 "nomap" when
`VoxelWorld_Instance(0)` is NULL; on that path the vendored draw has no instance, so there is **no "last
world" to reuse**). Rules:

- uploaded image **unmasked** (the usual case for warm-up / gate-off): draw the flat path exactly as
  today (`render_game`).
- uploaded image **masked** (mask was requested one frame ago, §6.6) and `Update` false: draw the overlay
  over the cleared target (black) for this one frame. Where this can happen in practice it is invisible:
  the callback leaves CB2_Overworld only after the game's own fade/battle transition has gone black, a
  failed map decode is followed within one frame by an unmasked image, and our menu/link toggles keep
  the world (they are "closing frames", §6.6, with `Update` still true).

Voxel off (setting, gate) ⇒ zero voxel calls, no snapshot, mask off ⇒ byte-identical frame (invariant 1;
host check §9.4). The one non-identical artefact of having *used* voxel in a session is the depth buffer
attached to the top targets at lazy init (VRAM only, never sampled by the flat path).

---

## 6. The overlay: what of the GBA image is drawn over the world

The world replaces the GBA's map layers (BG1-BG3) and its object sprites (the vendored code draws its own
billboards for every object event, `voxel_entities.c`). What must survive on top: **BG0** — text boxes,
the START menu, signpost/map-name banners, the "Save?" prompt, field-move prompts.

### 6.1 Zallax's approach (for comparison; not ported)

Upstream owns the PPU: it reimplements the GBA's layer compositor in `3ds_video.c` and, in voxel mode,
`ComposeVoxelOverlay` (`3ds_video.c` ~4351-4372 at c330c0a) draws (1) weather OBJs with blending, (2) BG0
shifted by `CTR_FIELD_UI_SHIFT` (text windows moved for the 400-wide frame), (3) screen-space OBJs, all
over the logical surface after `CtrVoxel_Draw`. It can do this because it renders the GBA layers itself,
per layer, from game RAM. **We cannot**: mGBA's software renderer produces one composited 240×160 frame;
we never see layers separately. Porting upstream's compositor is excluded (LEGAL: ours, not theirs; and it
is ~5 kLoC of PPU).

### 6.2 Options

| | A. mGBA per-layer disable + backdrop key | B. our own BG0 mini-PPU from snapshot VRAM | C. palette[0] colour key, no patch |
|---|---|---|---|
| How | top core renders BG0 only (`enableVideoLayer` BG1/2/3/OBJ off), backdrop written as a key colour; key → alpha 0 | decode BG0 tilemap + 4bpp tiles + palette from the snapshot ourselves into an RGBA texture | top core renders BG0 only; pixels equal to the backdrop colour → alpha 0 |
| Exactness | exact (mGBA's own BG0, windows, scroll, mosaic, palette fades) | approximate (we must reimplement scroll, windows, priority vs BG1 text frames, blending) | holes wherever BG0 uses the backdrop colour (black indoor backdrop vs black text-frame pixels) |
| Cost | mGBA does *less* work (3 layers + OBJ off); one 240×160 CPU key pass (≈38 K px, < 0.3 ms) + 1 DisplayTransfer | ~0.5-1 ms CPU + a whole new renderer to test | as A |
| Code | `gbacore_set_overlay_mode` + **a ~20-line mGBA patch** (MPL file, `patches/`, libmgba rebuild) | ~600 lines ours | no patch |
| Weather sprites | lost (OBJ layer off) | lost unless we also do OBJs | lost |

**Decision: A.** Exactness is the deciding factor — text is the one thing the player must read, and mGBA
already renders it perfectly. B is a large second renderer for no gain. C is A minus the patch and fails
on real content (Emerald's indoor maps use black backdrop; text-window frames contain black). C is kept
only as a **build-time fallback** if the patch cannot be applied (`VX_OVERLAY_KEY_PATCH 0`), documented as
lossy.

### 6.3 The mGBA patch (`patches/mgba-backdrop-key.patch`, MPL-2.0 file-level)

In `src/gba/renderers/video-software.c` (+ its struct in `include/mgba/internal/gba/renderers/video-software.h`):

- add `bool backdropKey;` to `struct GBAVideoSoftwareRenderer` (default false ⇒ unpatched behaviour, so
  voxel-off frames are byte-identical);
- at the backdrop fill (~:936, where `softwareRenderer->row[x] = backdrop` is set per scanline): when
  `backdropKey`, write **`VX_KEY565 = 0x0020`** instead of the backdrop colour. Why this value can never
  be produced by a real pixel: mGBA's 555→565 expansion `(c & 0x3E0) << 1` leaves green bit 5 of the 565
  word always 0, and the blend/brighten/darken masks (`software-private.h:217-262`, mask 0x7C0 for green)
  never set it either;
- in `GBAVideoSoftwareRendererPostprocessBuffer` skip the `target2Bd` alpha-mix block when `backdropKey`
  (otherwise a BG0 pixel alpha-blended with the backdrop would mix the key into real colour);
- brighten/darken (BLDY) are applied per pixel only to pixels carrying the target-1 flag; backdrop-
  initialised pixels are written without `FLAG_REBLEND`, so the key survives a fade. **P3 verifies** this
  with a host-side libmgba render of a door fade (§9.3) — if the key does get darkened, the patch also skips
  backdrop pixels in the brighten/darken loop.

The patch is licensed MPL-2.0 (modified mGBA file stays MPL, source published in `patches/`), consistent
with the release-prep rule already in memory (commit the mGBA mod as a patch).

### 6.4 Overlay texture build (`vx_overlay.c`)

Per frame in voxel mode, in the parked window right after `upload_frame(topG)` (`main.c:3510`):

1. `vx_overlay_build(const u16 *fb565, u16 *dst5551)`: for the 240×160 frame (`e->fb`, stride
   `GBA_FB_STRIDE`), `dst = (px == VX_KEY565) ? 0 : rgb565_to_rgba5551(px) | 1`. dst is a linear buffer
   (`linearAlloc` 256×160×2 = 80 KiB, stride 256). ≈ 38 400 px, NEON-free plain C loop, < 0.3 ms.
2. `GSPGPU_FlushDataCache(dst, …)`; `GX_DisplayTransfer(dst, GX_BUFFER_DIM(256,160), ovTex.data,
   GX_BUFFER_DIM(256,256)…, RGB5A1→RGB5A1, tiled out)` — the same linear→tiled pattern as `upload_frame`
   (`main.c:703-712`); **one GX command**, counted in the §4.2 reserve.
3. `ovTex`: `C3D_TexInit(256, 256, GPU_RGBA5551)` (128 KiB linear) at lazy init; `GPU_NEAREST` when
   `smooth[0]` is off, `GPU_LINEAR` when on (same as the game texture).

The normal game texture upload still runs (it is what the flat path needs on any frame the gate drops).

### 6.5 What is suppressed, and what is lost

Masked out on the top core in voxel mode: **BG1, BG2, BG3** (map layers — replaced by the world), **OBJ**
(all sprites — the world draws object events as billboards from `gSprites`/`gObjectEvents`), and the
**backdrop** (keyed). Kept: **BG0** with its windows, scroll, palette and BLDY fades.

Lost compared to Zallax:
- **Weather OBJs** (rain streaks, snow, ash, sandstorm, fog sheets, underwater bubbles). The vendored world
  already darkens/fogs/tints for weather (`ctr_voxel.c` ~4736-4782: weather → lighting, fog density from
  `VoxelWorld_FogDensity()`), so the *mood* survives; the particles do not. A future slice can draw them as
  billboards from `gSprites` whose template ∈ §4.6's set (they are already identified there).
- **Screen-space OBJs over the field** (the Fly bird, the Pokémon cry-out during a field move, the "!"
  emote is an object-anchored OBJ and *is* drawn by the world). Acceptable for phase 32; listed in §10.
- Things the game draws on BG1-BG3 that are UI, not map: the **map-name popup** is BG0 (kept); the
  **START menu** is BG0 (kept); **field-move/fishing prompts** BG0 (kept). Known exception: none found in
  Emerald's field; P6's on-hardware checklist walks every field UI once (HW-5).

`CtrVoxel_SetBrightness(bg, spr, white)` is fed from the snapshot's BLDCNT/BLDY with our own expression
of the GBA rule (effect = BLDCNT bits 6-7; 2 = brighten, 3 = darken; level = min(BLDY & 31, 16)/16;
applied to bg when any of BLDCNT bits 1-3 is set, to sprites when bit 4 is set), so the world fades with
the game's door/warp fades exactly as the GBA image would.

### 6.6 The one-frame-lag state machine

The mask is applied when the core **renders** a frame; we see that frame one iteration later. So every
uploaded frame carries a tag `masked` (copied from the core's mask state at the moment the worker was
kicked, stored in `EmuInstance.ovMasked` by `vx_overlay_note_uploaded`).

```
want = voxTop && CtrVoxel_IsAvailable() && worldReadyLastFrame      // decided in the parked window
gbacore_set_overlay_mode(topCore, want)                              // affects the NEXT rendered frame
draw this frame:
  uploaded.masked && voxReady          → world + overlay (normal)
  uploaded.masked && !voxReady         → closing frame: overlay over black (rare, §5.5)
  !uploaded.masked && voxReady         → opening frame: flat image as today (world warms behind; no mix)
  !uploaded.masked && !voxReady        → flat (voxel off)
```

Consequences: engaging takes 2 frames (world ready → mask requested → first masked frame displayed), and
the first displayed voxel frame already has a complete world. Disengaging takes 1 frame; on that frame the
world is still drawn when `Update` succeeds (menu/link toggles), black otherwise. `gbacore_set_overlay_mode`
is only ever called in the parked window (the renderer runs on the worker thread; mGBA's software renderer
is not threaded in our build, so the flag is read by the worker during `emu_step` and written by main only
while parked — no lock needed). It calls `core->enableVideoLayer(core, id, on)` for ids 1, 2, 3, 4 (OBJ)
(`external/mgba/src/gba/core.c:1513`) and sets `((struct GBAVideoSoftwareRenderer *)gba->video.renderer)->
backdropKey`. Our "Layers" debug setting (if any is user-visible) is restored from its saved state when the
mask turns off.

---

## 7. Per-eye stereo

Upstream renders one eye: `CtrVoxel_Draw` starts with `(void)eyeOffset;` under the comment "Stereoscopy is
V8; the first milestone renders one eye" (`ctr_voxel.c:~5703`), and its compositor turns stereo off in voxel
mode. Stereo is ours (LEGAL: re-express, never paste from `3ds_video.c`). The math lives in
`source/voxel/vx_stereo.c` (pure C, host-tested); the vendored file gets three marked edits.

### 7.1 Camera model (as upstream, for reference)

`CameraMatrices` (`ctr_voxel.c:692`): `Mtx_Persp(fov 35°, aspect 400/240, near 1, far 200)` + `Mtx_LookAt(eye,
target, up)`; eye placed by `voxel_camera.c` `Place()` south of and above the player: horizontal distance
`d = (8 + min(0.2·max(mapW,mapH), 5)) · 100 / zoom%`, height `tan(pitch)·d`, pitch from settings (§8).
`fit=true` folds in `FitToLogicalSurface` (:4574): upstream draws to a 512×256 off-screen surface and blits
it. **We draw straight into the 3DS top framebuffers (portrait 240×400)**, so `fit=true` is replaced by our
`vx_fit_to_screen()`; no logical surface, saving 2 × 512 KiB VRAM and one blit per eye.

### 7.2 Per-eye projection: parallel cameras, asymmetric frustum

For eye `e ∈ {−1 (left), +1 (right)}`, interaxial `i` (world units = tiles), convergence distance `D`:

- **View:** the mono `Mtx_LookAt` view, then translate along the camera's right axis by `−e·i/2`
  (parallel cameras — no toe-in, so no vertical parallax/keystone).
- **Projection:** the mono frustum at the near plane `[l, r] = ±n·tan(hfov/2)` shifted by `+e·(i/2)·n/D`
  (`l' = l + e·i·n/(2D)`, `r' = r + e·i·n/(2D)`) — the off-axis frustum that makes the two eyes'
  images coincide at depth `D`.
- **Screen fit:** rotate clip space 90° for the portrait framebuffer (clip `x' = y`, `y' = −x`, the same
  rotation citro3d's `*Tilt` projections apply). Equivalent library call: citro3d
  `Mtx_PerspStereoTilt(&p, fovy, C3D_AspectRatioTop, VOXEL_NEAR, VOXEL_FAR, e·i, D, false)` (citro3d's
  stereo helper implements exactly this shift); we use it if the P4 host test (§9.2) shows it reproduces
  the mono `CameraMatrices(fit=false)` frame at `i = 0` (corner rays, `CtrVoxel_ProjectPictureTile`
  agreement within 0.5 px), else our own `vx_fit_to_screen()` formula above.
- `i = VX_IOD_MAX · osGet3DSliderState()` with **`VX_IOD_MAX = 0.40` tiles** (tunable on HW, HW-4);
  slider 0 ⇒ `i = 0` ⇒ both eyes identical ⇒ the right eye is skipped (`s3dOn` false, mono path).
- **Convergence `D` = the eye-to-player distance**, `D = d / cos(pitch)` (the player's tile sits on the
  screen plane; the world recedes behind, near trees/roofs pop out).

Disparity budget (on-screen px, `f_px = 200 / tan(hfov/2)`, hfov from vfov 35° and aspect 5/3 ⇒
`tan(hfov/2) ≈ 0.525`, `f_px ≈ 381`): a point at depth `z` shows `i·(1/D − 1/z)·f_px` px. Defaults pitch
40°, zoom 100 %, `d ≈ 9..13`, `D ≈ 11.7..17`: far ground (`z ≈ 30`) ≈ **+8 px** behind, a roof/tree a few
tiles in front of the player (`z ≈ 6`) ≈ **−12 px** out, at full slider. Inside the 3DS comfort range;
P6 tunes `VX_IOD_MAX` by eye.

### 7.3 Overlay at screen depth

The BG0 overlay (§6) is drawn **identically in both eyes** (zero disparity) ⇒ it sits on the convergence
plane, i.e. at the player's depth: the text box reads as "on the screen glass", the world around it. No
per-eye shift of text (shifting text is what made phase-14's `uipop` need text detection; not needed here).

### 7.4 Culling margin

`UpdateFrustum` (`ctr_voxel.c:744`) cuts the side planes from the mono camera once per frame in `Update`
(both eyes draw the same chunk list). With stereo the eyes see slightly wider. Our edit: when `i > 0`,
the frustum is built from a **union camera** = the mono eye pulled back along its view axis by
`b = (i/2) / tan(hfov/2)`, same fov — its pyramid contains both off-axis eye pyramids at every depth ≥ the
near plane (proof sketch in `vx_stereo.c`'s header; host-tested by sampling both eyes' edge rays, §9.2).
Cost: `b ≤ 0.2/0.525 ≈ 0.38` tiles ⇒ a few percent more chunks drawn at full slider.

### 7.5 The vendored edits (marked `/* 3DGBA: stereo */`)

1. New API (ours, in `ctr_voxel.h`): `void CtrVoxel_SetStereo(float interaxial, float convergenceOrZero);`
   called before `CtrVoxel_Update()` each frame (`0` = mono).
2. `CameraMatrices(proj, view, fit)` gains `float eye`; `fit=true` path → `vx_stereo_projection(...)` +
   view shift; `fit=false` (frustum, gloom, `ProjectPictureTile`) stays mono, except `UpdateFrustum` uses the
   union camera when `interaxial > 0`.
3. `CtrVoxel_Draw(target, eyeOffset)`: `(void)eyeOffset;` → the eye sign is passed into `CameraMatrices`;
   gloom placement (`PlaceGloom`) per eye. Main calls `CtrVoxel_Draw(top, −1)` and, when `s3dOn`,
   `CtrVoxel_Draw(topR, +1)` (`main.c` 4871 / 5113 sites).

### 7.6 Cost

Update (world, culling, chunk scheduling) runs **once**; Draw runs per eye: **2× GPU vertex/fragment work**
and 2× the Draw call's CPU command-list building (the vendored Draw walks the visible chunk list and issues
one `C3D_DrawArrays` per chunk/material run). Expected CPU +1-2 ms on core 0 at full stereo (estimate);
the GPU side is the real question at 400×240×2 with fog + haze texenv stages. Measured in P6 (HW-2) with the
existing frame-time diag; if the GPU misses 60 fps the fallback is a **stereo-off-in-voxel** rule (gate
`s3dOn && voxTop` → mono), which is exactly upstream's behaviour.

---

## 8. Settings

### 8.1 Values

| Setting | Values | Default | Source of the range |
|---|---|---|---|
| VOXEL 3D | off / on | **off** | — (off keeps invariant 1 for every existing user) |
| 3D ANGLE (camera pitch) | 34°, 37°, 40°, 43°, 46° | 40° (index 2) | upstream `3ds_settings.c:26` `sPitches`, default index 2 (:29) |
| 3D ZOOM | 90 %, 100 %, 110 %, 120 % | 100 % (index 1) | upstream `3ds_settings.c:27` `sZooms`, default index 1 (:29) |

The ranges are parameters, not expression; we re-state them in `vx_settings.h` (`VX_PITCH_DEG[5]`,
`VX_ZOOM_PCT[4]`) and our `CtrSettings_VoxelPitch()/VoxelZoom()` shims return `VX_PITCH_DEG[g_prefs.voxPitch]`
/ `VX_ZOOM_PCT[g_prefs.voxZoom]` — the values `voxel_camera.c` `AdaptDistance()` consumes.

### 8.2 Persistence (`Settings`, `main.c:~2450-2500`)

Append three `s32` fields **after the last field present when P3 lands** (today `traverse` at index 25; the
uncommitted phase-32-T work may append first — rebase on it, never insert): `voxel`, `voxPitch`, `voxZoom`.
Same pattern as `tilt`/`presence`/`traverse`: no magic bump (`SETTINGS_MAGIC 0x33424744`), the length
ladder in `settings_load` gains `lenVoxel = offsetof(Settings, voxZoom) + 4`; older files load with the
defaults above; values are reduced modulo their range on load (`% 2`, `% 5`, `% 4`) like `tiltLevel`
(`main.c:2550`). New `_Static_assert`s for `sizeof(Settings)` and each new `offsetof`, and
`test/host/test_tilt.c` TEST 6's mirrored layout is updated in the same edit (the asserts' own comment
requires it). `g_prefs` (UiPrefs, `theme.h:92`) gains `voxel`, `voxPitch`, `voxZoom`.

### 8.3 PT_ENHANCE placement

`PT_ENHANCE` (`main.c:2634`) is **full** as laid out: its comment records 7 px free below the tilt row
(y 200-225; the hint band owns y ≥ 226, `contentH` 226 ⇒ `maxScroll` 0). The menu already supports content
scrolling (`uihit_content_h` / `uihit_max_scroll`, `main.c:4062`, drag-scroll from phase 19). So the
three rows go **below** the tilt row and the tab becomes scrollable — no plate art change, no existing row
moves:

| Row | PCtl | Geometry (content coords) | Caption |
|---|---|---|---|
| VOXEL 3D | `{PK_TOG, ACT_VOXEL, 0, 276, 250, 34, 18, "VOXEL 3D", OV_SECTION_TIGHT}` | toggle in the same x276 column as the five baked toggles | "VOXEL 3D" |
| 3D ANGLE | `{PK_SEG, ACT_VOXPITCH, 5, 140, 290, 170, 26, "3D ANGLE", OV_SECTION_TIGHT}` | 5 rungs, labels `34° 37° 40° 43° 46°` | "3D ANGLE" |
| 3D ZOOM | `{PK_SEG, ACT_VOXZOOM, 4, 140, 336, 170, 26, "3D ZOOM", OV_SECTION_TIGHT}` | 4 rungs, labels `90 100 110 120` | "3D ZOOM" |


> 2026-10-07: the tilt row is gone and the three rows moved up under LDR Bloom: VOXEL 3D y141, 3D ANGLE y181, 3D ZOOM y227, contentH 253, maxScroll 27 (see `PT_ENHANCE` in main.c). The y values and the 362/136 figures above are the original design.
`contentH` becomes 362 ⇒ `maxScroll` = 362 − 226 = 136 px. Each new `ACT_*` gets its case in the three
`switch`es that read/write PK_SEG/PK_TOG values (`main.c:4169/4176`, `5295` label table, `5544/5550`) and in
`PUSH(...)` bookkeeping (`main.c:3058`). Captions follow phase 19's L3.2.6 rule (caption ink 17 px above its
control, clear of neighbours); exact y's are re-checked in the P3 UI capture against the theme's plate and
may move by a few px — the order and the scroll decision are what this spec fixes. The ANGLE/ZOOM rows are
**dimmed** (drawn, not hit-testable) while VOXEL 3D is off. A "Voxel" pill joins the enhance pills
(`main.c:3144` pattern: `PILL("Voxel", g_prefs.voxel && voxTop, g_ui.acc)`).

Interaction with TILT: both may be set; on the top screen voxel wins while `voxTop` (§5.4), tilt applies
whenever voxel is gated off (other contexts, FR/LG on top, bottom screen).

### 8.4 Status text (hint band, y ≥ 226)

While the VOXEL 3D row has focus, or for 3 s after it is switched on, the hint band shows one line from
`vx_status()` (ours; wraps `CtrVoxel_Status()` + our own checks), first match wins:

| Condition | Text |
|---|---|
| top game is not Pokémon Emerald (BPEE) | `Voxel 3D: Emerald only` |
| Old 3DS | `Voxel 3D: needs a New 3DS` |
| no pak and no loose data | `Voxel 3D: data missing — see README (sdmc:/3ds/emerald3ds/)` |
| pak ROM SHA-1 ≠ loaded ROM | `Voxel 3D: data is for a different ROM` |
| pak/bin header or CRC bad | `Voxel 3D: data damaged` |
| init failed for VRAM/linear memory | `Voxel 3D: not enough video memory` |
| link active | `Voxel 3D: paused during link` |
| otherwise | `Voxel 3D: ` + `CtrVoxel_Status()` (`"ready"`, `"nomap"`, warm-up %) |

The data check (`vx_data_probe()`: open + header/CRC/SHA-1) runs once when the toggle is switched on and on
ROM load while it is on — never on the frame path.

---

## 9. Host test plan (macOS clang, dual-compiled)

House pattern (`HANDOFF.md:294-298`): one self-contained `test/host/test_*.c` per area, built with
`clang -std=c11 -Wall -Wextra -O2 -I source -I source/voxel -I test/host test/host/test_X.c <sources> -o /tmp/tX && /tmp/tX`,
using `test/host/ctr_shim.h` for u8/u16/u32. The vendored game-side modules are pure C already (upstream
host-builds them in `3ds_port/tests/`); the GPU file `ctr_voxel.c` is **not** host-built.

### 9.1 What is host-built

| Unit | Sources | Notes |
|---|---|---|
| world | `voxel_world.c`, `voxel_regions.c`, `voxel_relief.c`, `voxel_building.c`, `voxel_sign.c`, `voxel_arena.c` + our `vx_adapter.c`, `vx_snapshot.c`, `vx_lz77.c`, `vx_behavior.c`, `vx_data.c` | `-DVOXEL_HOST_FILES` ⇒ `VoxelFile_Open` uses `fopen` on a test fixture dir |
| mesh | `voxel_mesh_builder.c`, `voxel_lighting.c`, `voxel_atlas.c`, `voxel_grade.c`, `voxel_tree.c` | `CtrVideo_Texel/RGBA5551/ObjTile` from our `ctr_shims.c` (pure part split into `ctr_shims_pure.c` so the host never sees libctru) |
| entities | `voxel_entities.c`, `vx_battle_stub.c` | |
| gate / stereo | `tilt.c` (`voxel_gate`), `vx_stereo.c` | |

Every one also cross-compiles for ARM in the normal `make` — that is the "dual-compiled" proof; any
libctru include in these files is a host build break by construction.

### 9.2 Tests

| Test file | Checks |
|---|---|
| `test_voxel_adapter.c` | **synthetic snapshot**: a fake 2 MiB "ROM" buffer with a hand-built MapHeader → MapLayout (w 20, h 20) → 2 Tilesets (one LZ-compressed, one raw), MapConnections (north + east), MapEvents (1 bg event), GraphicsInfo table, field-effect template table; a fake EWRAM/IWRAM with gMapHeader, gBackupMapLayout (35×34, A3), sBackupMapData, gSaveBlock1, gObjectEvents (player + 2 NPCs), gPlayerAvatar, gSprites, gMain.callback2 = CB2_Overworld. Asserts: decode valid; interned pointer identity stable across 2 decodes; `&gTileset_General` == the interned object at 0x083DF704; payload pointers land inside the fake ROM; every guard (A1-A15) rejects its corrupted variant (one case per assert); misaligned u16 pointer rejected; `Port_GetAssetSizeExact` = packed LZ length for the compressed tileset, 16384 for the raw one |
| `test_voxel_lz77.c` | our decoder vs a reference encoder written in the test (round-trip on random + repetitive data, 0..64 KiB); `vx_lz77_scan` length = encoder output length; truncated/overflowing streams rejected without reading past the end |
| `test_voxel_world.c` | builds `VoxelWorld_BuildInstances()` on the synthetic snapshot: instance 0 is the current map, connections produce instances at the right offsets, `VoxelWorld_IsMapAvailable()` true for CB2_Overworld and false for any other callback, `VoxelWorld_Hash` stable across identical snapshots and different when one map cell changes (the upstream hash-test idea, re-expressed against our adapter) |
| `test_voxel_mesh.c` | meshes one chunk of the synthetic world; asserts vertex count > 0, all positions inside the chunk bounds × `VOXEL_POS_SCALE 512`, no NaN texcoords; golden count recorded on first run and pinned |
| `test_voxel_shims.c` | `CtrVideo_Texel` vs a brute-force Morton reference for every (x,y) in 512×256; `RGBA5551` on all 32768 colours vs the bit formula; `ObjTile` 1D/2D, 4/8bpp corner cases; GX budget arithmetic (`TryVoxelUpload` refuses at `used+2+reserve > 32`); pak parser: good file, each header field corrupted (magic, CRC, schema, count), index CRC, unsorted ids, compressed flag, out-of-bounds entry, ROM SHA-1 mismatch; FNV-1a path ids vs known vectors; loose fallback order |
| `test_voxel_overlay.c` | key → alpha: `VX_KEY565` → 0x0000, every other 565 value → alpha 1 with the right RGB; proof that no 555→565 expansion (all 32768 inputs) and no brighten/darken result (all EVY 0..16) produces `VX_KEY565`; the §6.6 state machine table (4 rows) incl. engage = 2 frames, disengage = 1 |
| `test_voxel_gate.c` (or TEST block in `test_tilt.c`) | `voxel_gate` truth table: each rule of §5.3 flips the result alone; FIELDMENU on, textDlg ignored, linkOn off |
| `test_voxel_stereo.c` | slider 0 ⇒ both eye matrices == mono; the player tile projects to the same pixel in both eyes (convergence) within 0.01 px; a far point has positive, a near point negative disparity with the §7.2 magnitude; union-camera frustum contains 1000 sampled edge rays of both eyes; `Mtx_PerspStereoTilt` equivalence check (§7.2) using a host copy of citro3d's math only if it is header-only, else our formula vs a reference |
| `test_voxel_entities.c` | `sStepFrames` re-measurement (§2.7) from a recorded gs log fixture; weather template set: sprites with each of the 8 templates excluded, others kept |
| `test_tilt.c` TEST 6 | Settings layout mirror updated for the three new fields (§8.2) |

### 9.3 Optional real dump (local only, never committed)

`tools/voxel/dump_snapshot.py` (ours) uses the existing Azahar/GDB tooling (`tools/emutest/gdbio.py`,
the reader `tools/diorama/diodump.py` already uses) to save a real `VxSnapshot` + the ROM path to
`/tmp/vx_real/` while standing in Littleroot, Route 119 (Fortree tileset), a building interior, Fortree,
and in rain. `test_voxel_adapter.c --real /tmp/vx_real/<name>` then runs decode + world build on it.
**The dump contains game RAM and is generated data: it is never committed** (`.gitignore`: `/tmp` is outside
the repo anyway; the script refuses an output path inside the repo). This is also how the P2a offset
facts (§2.10) and the §6.3 door-fade key check are confirmed against real memory.

### 9.4 Invariant-1 check

`tools/emutest` already captures framebuffers (`fbdump.py`). P6 adds one emutest case: same savestate,
VOXEL 3D off, 120 frames, top-screen capture hash == the pre-phase-32 build's hash (byte-identical). Then
VOXEL 3D on with no data present: same hash (gate off, status "data missing").

---

## 10. Risks, and what only hardware can prove

### 10.1 Risks

| # | Risk | Likelihood / impact | Mitigation |
|---|---|---|---|
| R1 | **mGBA patch** (§6.3) needs a libmgba rebuild; the build path for our libmgba must be confirmed (who builds it, from which `external/mgba` revision) | certain work / blocks §6 | P3 owns it via `devkitarm-3ds-build`; fallback C (palette-0 key, lossy) behind `VX_OVERLAY_KEY_PATCH 0` so P2 never waits on it |
| R2 | **VRAM** ~4.1 MiB for voxel + depth on top of our ≥1.05 MiB targets + unmeasured offscreen passes (§4.7) | medium / init fails ⇒ feature unusable | lazy init with a measured threshold; reduce `VOXEL_ATLAS_SLOTS` 6→4 (−512 KiB) as a marked tunable; free our DoF/bloom VRAM while voxTop (they are excluded anyway) |
| R3 | **Core-0 contention**: emuA shares core 0 with the main thread (`main.c:3120`); voxel CPU on main slows the top game | high / visible slowdown | `gameMs` = emuA's step time feeds the vendored budget (§4.4); measured HW-1; if emuA misses frames, move emuA to core 2 with emuB in single-game voxel use (out of scope here, noted for P6) |
| R4 | **P2a offsets** unverified (Sprite.template/data/subpriority, ObjectEvent movement bits, gMain.inBattle, PaletteFade, Weather ×5, GraphicsInfo, BgEvent, FLDEFFOBJ indices) | medium / wrong billboards, wrong fades | P2a probe (§2.10) + real-dump checks (§9.3) before P2 code depends on them |
| R5 | **Pak coupling**: the pak's internal voxel `.bin` versions follow upstream; a newer upstream data tool may produce files our vendored v0.2.0 readers reject | medium / "data damaged" | the vendored readers' own magic/version checks give a clear status; README pins "build the data with upstream v0.2.0's tool"; loose-file fallback |
| R6 | **engineAbi not checked** (we lack upstream's `engine/abi.bin`) | low / a mismatched pak passes the header | voxel `.bin` self-checks are the real gate; status shows the pak's ABI in the log |
| R7 | **Weather particles and screen-space OBJs lost** in voxel mode (§6.5) | certain / cosmetic | world lighting/fog keeps weather mood; future slice: weather billboards from §4.6's template set |
| R8 | **Linear heap default** unmeasured (no `__ctru_linear_heap_size` override; upstream uses 8 MiB) | low / init fail | `linearSpaceFree()` logged at boot and around Init (HW-3); voxel needs ≈1.1 MiB |
| R9 | **GX queue overflow** = `svcBreak` crash, not a dropped upload | low with the wrap, fatal if wrong | `--wrap=GX_BindQueue` live fill + reserve 16, measured high-water (§4.2); host test of the arithmetic |
| R10 | `StreamWorker` on core 1 competes with our audio thread | low / audio crackle | priority below audio (§3.5); HW-6 listens for it |
| R11 | **Link sessions**: no parked window ⇒ voxel off under any link (§3.1) | certain / by design | status "paused during link"; the M4 wireless work is unaffected (no edits to celiolink/netlink/wireless) |
| R12 | **Upstream merge drift**: vendored files are modified (≈30 marked edits, mostly ctr_voxel.c) | low | every edit tagged `/* 3DGBA: … */`; `git show c330c0a:` + the tags make a re-vendor a mechanical diff |
| R13 | **Legal**: tree PNGs are MIT original art (not game-derived) per upstream's generator script; every other data file is user-generated and never shipped | low | `release-legal-audit` before any push of phase 32; `.gitignore` for `*.pak`, `voxel/*.bin`; closeout `git ls-files` check |

### 10.2 Hardware-only list (Azahar cannot prove these; PHASE.md exit gate)

| ID | What | Pass criterion |
|---|---|---|
| HW-1 | Frame budget with voxel on, mono: main-thread ms (decode + Update + Draw + AfterSubmit) and **emuA's step time** on core 0 | 60 fps held walking Route 119 and Lilycove; emuA step time within 5 % of voxel-off |
| HW-2 | Stereo cost: same as HW-1 with the 3D slider at max | 60 fps, or the stereo-off-in-voxel fallback engages (§7.6) |
| HW-3 | VRAM + linear free at boot / before / after `CtrVoxel_Init` (logged) | Init succeeds with ≥ 256 KiB VRAM headroom |
| HW-4 | Stereo comfort: `VX_IOD_MAX`, convergence on the player, overlay text at screen depth | no eye strain at full slider (subjective sign-off by Guy) |
| HW-5 | Field UI walk-through: START menu, text boxes, signposts, map-name popup, Save prompt, fishing, surfing, door/warp fades, cave darkness, rain/fog | every UI legible and on top; fades match the GBA image; no holes in text frames (key correctness) |
| HW-6 | Audio with StreamWorker active during map streaming | no crackle/underrun across 5 map transitions |
| HW-7 | GX queue high-water with voxel + stereo + overlay | < 28 of 32 (re-sets the reserve) |
| HW-8 | Real pak on SD: open, SHA-1 check time at ROM load, page stream | pak accepted; status "ready" within 2 s of entering the overworld |

### 10.3 Blockers

None blocks writing P2 code (P0 legal is done; this spec is P1). P3 (overlay) is blocked on R1 (libmgba rebuild path). P2's entity
billboards are blocked on R4's P2a offset probe. The exit gate is blocked on HW-1..HW-8 (real New 3DS).
