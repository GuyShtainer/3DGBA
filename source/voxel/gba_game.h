/* gba_game.h -- the game-side vocabulary the voxel overworld reads (3DGBA, GPLv3).
 *
 * Written from docs/phase31-diorama/SPEC-data.md section 10 (deleted 2026-10-06 with the old diorama
 * attempt; `git show f1c09c7:docs/phase31-diorama/SPEC-data.md`) and docs/phase32-voxel/SPEC-port.md
 * section 2, plus numbers measured from the user's own ROM (see the "measured" notes). It
 * replaces every pret header the vendored voxel code used to include. No pret source text, struct
 * definition or comment is copied here.
 *
 * Two kinds of thing live in this file:
 *   1. GBA_ADDR_* / GBA_OFF_* / behaviour, weather and map-type numbers: facts about Pokemon
 *      Emerald (BPEE, USA/Europe, SHA-1 f3ae0881...). The adapter decodes emulated memory with them.
 *   2. HOST mirror structs: our own structs (host pointers, only the fields the vendored code
 *      reads, our declaration order). They are NOT GBA layouts; vx_adapter.c fills them from
 *      the snapshot. Struct tags and field names keep the vendored spelling so the vendored
 *      lines need no edits (names are interface facts, not expression).
 *
 * Phase 34: the GBA_ADDR_* / NUM_*_IN_PRIMARY / ... numbers below are now "the Emerald row" of the game profile
 * (source/romgen/rg_gameprof.c builds that row FROM these macros). The vendored renderer reads the active
 * profile through VXP(field) instead of the macros, so FireRed/LeafGreen can be another row.
 *
 * Pure C: no libctru. Host tests include this file directly. */
#ifndef VX_GBA_GAME_H
#define VX_GBA_GAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../romgen/rg_gameprof.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef uint8_t bool8;
/* The vendored code guards game-state reads with PLATFORM_3DS; here that means "the adapter
 * provides them". */
#define PLATFORM_3DS 1

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

/* A 32-bit GBA address. Fields that stay GBA addresses (callbacks, sprite templates) are typed
 * GbaPtr, never a host pointer, so a mistaken dereference is a compile error. */
typedef uint32_t GbaPtr;

/* ---- GBA memory map ------------------------------------------------------------------- */
#define GBA_ROM_BASE 0x08000000u
#define GBA_EWRAM_BASE 0x02000000u
#define GBA_EWRAM_SIZE 0x40000u
#define GBA_IWRAM_BASE 0x03000000u
#define GBA_IWRAM_SIZE 0x8000u
#define GBA_PLTT_BASE 0x05000000u
#define GBA_VRAM_BASE 0x06000000u
#define GBA_VRAM_BG_SIZE 0x8000u  /* BG character data, 0x06000000.. */
#define GBA_VRAM_OBJ_OFF 0x10000u /* OBJ character data starts here in VRAM */
#define GBA_VRAM_OBJ_SIZE 0x8000u
#define GBA_IO_DISPCNT 0x04000000u
#define GBA_IO_BLDCNT 0x04000050u
#define GBA_IO_BLDALPHA 0x04000052u
#define GBA_IO_BLDY 0x04000054u

/* ---- Addresses (BPEE). Canonical table: SPEC-data section 10.1 -------------------------- */
#define GBA_ADDR_GMAIN 0x030022C0u
#define GBA_ADDR_SB1_PTR 0x03005D8Cu
#define GBA_ADDR_BACKUP_LAYOUT 0x03005DC0u
#define GBA_ADDR_BACKUP_MAP 0x02032318u
#define GBA_BACKUP_MAP_BYTES 0x5000u
#define GBA_ADDR_MAP_HEADER 0x02037318u
#define GBA_ADDR_OBJECT_EVENTS 0x02037350u
#define GBA_ADDR_PLAYER_AVATAR 0x02037590u
#define GBA_ADDR_SPRITES 0x02020630u
#define GBA_ADDR_PLTT_UNFADED 0x02037714u
#define GBA_ADDR_PALETTE_FADE 0x02037FD4u
#define GBA_ADDR_WEATHER 0x02038454u
#define GBA_ADDR_MAP_GROUPS 0x08486578u
#define GBA_ADDR_MAP_LAYOUTS 0x08481DD4u
#define GBA_ADDR_GFX_INFO_PTRS 0x08505620u
#define GBA_ADDR_FLDEFF_TEMPLATES 0x085059F8u
#define GBA_ADDR_TILESET_GENERAL 0x083DF704u
#define GBA_ADDR_TILESET_BUILDING 0x083DF884u
#define GBA_ADDR_TILESET_FORTREE 0x083DF7C4u
#define GBA_ADDR_TILESET_GENERIC_BUILDING 0x083DFB6Cu
/* Field callbacks, with the thumb bit set as they appear in gMain.callback2. */
#define CB2_Overworld 0x08085E5Du
#define CB2_OverworldBasic 0x08085E51u

#define GBA_GFX_INFO_COUNT 239u
#define GBA_FLDEFF_TEMPLATE_COUNT 37u
#define GBA_MAP_GROUP_COUNT 34u

/* ---- Raw offsets the adapter decodes with ----------------------------------------------- */
/* Verified in SPEC-data / diodump (rows marked V) or measured on a running ROM by
 * tools/voxel/probe_ram.c + probe_check.py and ROM accessor immediates (see docs/phase32-voxel/BUILDLOG-P2.md). */
#define GBA_OFF_MAIN_CALLBACK2 0x04u        /* V */
#define GBA_OFF_MAIN_FLAGS 0x439u           /* V (FreeRestoreBattleData clears bit 1): bit 1 = in battle */
#define GBA_MAIN_INBATTLE_BIT 0x02u
#define GBA_OFF_SB1_POSX 0x00u              /* V */
#define GBA_OFF_SB1_POSY 0x02u              /* V */
#define GBA_OFF_SB1_MAPGROUP 0x04u          /* V */
#define GBA_OFF_SB1_MAPNUM 0x05u            /* V */
#define GBA_OFF_BKL_WIDTH 0x00u             /* V */
#define GBA_OFF_BKL_HEIGHT 0x04u            /* V */
#define GBA_OFF_BKL_MAP 0x08u               /* V */
#define GBA_OFF_MH_LAYOUT 0x00u             /* V */
#define GBA_OFF_MH_EVENTS 0x04u             /* V */
#define GBA_OFF_MH_CONNECTIONS 0x0Cu        /* V */
#define GBA_OFF_MH_LAYOUT_ID 0x12u          /* V */
#define GBA_OFF_MH_REGION_SEC 0x14u         /* V */
#define GBA_OFF_MH_CAVE 0x15u               /* V */
#define GBA_OFF_MH_WEATHER 0x16u            /* V */
#define GBA_OFF_MH_MAPTYPE 0x17u            /* V */
#define GBA_MAP_HEADER_BYTES 0x1Cu
#define GBA_OBJECT_EVENT_STRIDE 0x24u
#define GBA_OFF_OE_FLAGS0 0x00u             /* bit0 active, bit1 singleMovement, bit6 heldMovement (V: ROM tests 0x42 / bit 6) */
#define GBA_OFF_OE_FLAGS1 0x01u             /* bit5 invisible (V) */
#define GBA_OFF_OE_FLAGS2 0x02u             /* bit0 isPlayer (V), bit1 hasReflection (V: ROM tests mask 0x20001) */
#define GBA_OFF_OE_SPRITE_ID 0x04u          /* V */
#define GBA_OFF_OE_GFX_ID 0x05u             /* V */
#define GBA_OFF_OE_ELEVATION 0x0Bu          /* V low nibble */
#define GBA_OFF_OE_CUR_X 0x10u              /* V */
#define GBA_OFF_OE_PREV_X 0x14u             /* V */
#define GBA_OFF_OE_FACING 0x18u             /* V low nibble */
#define GBA_OBJECT_EVENT_COUNT 16u
#define GBA_OFF_PA_FLAGS 0x00u              /* V */
#define GBA_OFF_PA_SPRITE_ID 0x04u          /* V */
#define GBA_OFF_PA_OBJECT_ID 0x05u          /* V */
#define GBA_PLAYER_AVATAR_BYTES 0x24u
#define GBA_SPRITE_STRIDE 0x44u
#define GBA_SPRITE_COUNT 65u
#define GBA_OFF_SP_OAM 0x00u                /* V */
#define GBA_OFF_SP_TEMPLATE 0x14u           /* V: ROM pointers in live sprites */
#define GBA_OFF_SP_X 0x20u                  /* V */
#define GBA_OFF_SP_Y 0x22u
#define GBA_OFF_SP_X2 0x24u
#define GBA_OFF_SP_Y2 0x26u
#define GBA_OFF_SP_C2C_X 0x28u
#define GBA_OFF_SP_C2C_Y 0x29u
#define GBA_OFF_SP_DATA 0x2Eu               /* V: data[0] = object id on field sprites */
#define GBA_OFF_SP_FLAGS 0x3Eu              /* V: bit0 inUse, bit1 coordOffsetEnabled, bit2 invisible */
#define GBA_OFF_SP_SUBPRIORITY 0x43u        /* V: last byte of the 0x44 struct */
#define GBA_OFF_FADE_Y_WORD 0x04u           /* V (BeginNormalPaletteFade): y = (u16 at +4 >> 6) & 31 */
#define GBA_OFF_FADE_ACTIVE_WORD 0x06u      /* V: active = bit 15 of the u16 at +6 */
#define GBA_PALETTE_FADE_BYTES 0x0Cu
#define GBA_PLTT_BYTES 0x400u

/* Weather block (gWeather) field offsets, measured by ROM accessor immediates: read from the immediates of the
 * ROM's weather accessors (GetCurrentWeather 0x6D0, the fade-in test 0x6C6, the blend-coefficient
 * setter 0x730 (a u16; its low byte is read), the two fog init routines 0x6FB / 0x724). */
#define GBA_OFF_WEATHER_CURR 0x6D0u
#define GBA_OFF_WEATHER_PALSTATE 0x6C6u
#define GBA_OFF_WEATHER_EVA 0x730u
#define GBA_OFF_WEATHER_FOGH 0x6FBu
#define GBA_OFF_WEATHER_FOGD 0x724u

/* ROM-side objects (decoded by the adapter, interned). */
#define GBA_ROM_MAPLAYOUT_BYTES 24u
#define GBA_ROM_TILESET_BYTES 24u
#define GBA_ROM_CONNECTION_STRIDE 12u
#define GBA_ROM_BGEVENT_STRIDE 12u
#define GBA_ROM_GFXINFO_SIZE_OFF 0x06u     /* V(rom) */
#define GBA_ROM_GFXINFO_WIDTH_OFF 0x08u
#define GBA_ROM_GFXINFO_HEIGHT_OFF 0x0Au
#define GBA_ROM_GFXINFO_IMAGES_OFF 0x1Cu
#define GBA_ROM_IMAGE_STRIDE 8u

/* ---- Map / tile constants (SPEC-data 5.x, diodump header) -------------------------------- */
#define NUM_TILES_IN_PRIMARY 512
#define NUM_METATILES_IN_PRIMARY 512
#define NUM_METATILES_TOTAL 1024
#define NUM_TILES_PER_METATILE 8
#define NUM_PALS_IN_PRIMARY 6
#define NUM_PALS_TOTAL 13
#define TILE_SIZE_4BPP 32
#define MAX_SPRITES 64
#define OBJECT_EVENTS_COUNT 16
#define MAP_OFFSET 7
#define MAPGRID_METATILE_ID_MASK 0x03FF
#define MAPGRID_COLLISION_MASK 0x0C00
#define MAPGRID_COLLISION_SHIFT 10
#define MAPGRID_ELEVATION_MASK 0xF000
#define MAPGRID_ELEVATION_SHIFT 12
#define GBA_BEHAVIOR_MASK 0x00FFu
#define GBA_ATTR_LAYER_MASK 0xF000u /* metatile-attribute layer type, Emerald packing */
#define GBA_ATTR_LAYER_SHIFT 12
#define UNPACK_BEHAVIOR(attr) ((attr) & (unsigned)VXP(behMask))

#define MAP_TYPE_NONE 0
#define MAP_TYPE_TOWN 1
#define MAP_TYPE_CITY 2
#define MAP_TYPE_ROUTE 3
#define MAP_TYPE_UNDERGROUND 4
#define MAP_TYPE_UNDERWATER 5
#define MAP_TYPE_OCEAN_ROUTE 6
#define MAP_TYPE_UNKNOWN 7
#define MAP_TYPE_INDOOR 8
#define MAP_TYPE_SECRET_BASE 9

#define CONNECTION_SOUTH 1
#define CONNECTION_NORTH 2
#define CONNECTION_WEST 3
#define CONNECTION_EAST 4
#define CONNECTION_DIVE 5
#define CONNECTION_EMERGE 6

/* Weather ids and palette-process states: read off the ROM's weather function table
 * (tools/voxel/BUILDLOG-P2.md, "weather ids"). */
#define WEATHER_NONE 0
#define WEATHER_SUNNY_CLOUDS 1
#define WEATHER_SUNNY 2
#define WEATHER_RAIN 3
#define WEATHER_SNOW 4
#define WEATHER_RAIN_THUNDERSTORM 5
#define WEATHER_FOG_HORIZONTAL 6
#define WEATHER_VOLCANIC_ASH 7
#define WEATHER_SANDSTORM 8
#define WEATHER_FOG_DIAGONAL 9
#define WEATHER_UNDERWATER 10
#define WEATHER_SHADE 11
#define WEATHER_DROUGHT 12
#define WEATHER_DOWNPOUR 13
#define WEATHER_UNDERWATER_BUBBLES 14
#define WEATHER_PAL_STATE_CHANGING_WEATHER 0
#define WEATHER_PAL_STATE_SCREEN_FADING_IN 1
#define WEATHER_PAL_STATE_SCREEN_FADING_OUT 2
#define WEATHER_PAL_STATE_IDLE 3

/* Metatile behaviour ids (SPEC-data 5.3; the ones SPEC-data lacks were read off the ROM's own
 * comparison immediates, see BUILDLOG-P2.md). */
#define MB_POND_WATER 0x10
#define MB_PUDDLE 0x16
#define MB_SHALLOW_WATER 0x17
#define MB_SOOTOPOLIS_DEEP_WATER 0x14
#define MB_UNUSED_SOOTOPOLIS_DEEP_WATER_2 0x1A
#define MB_STAIRS_OUTSIDE_ABANDONED_SHIP 0x1B
#define MB_SHOAL_CAVE_ENTRANCE 0x1C
#define MB_ICE 0x20
#define MB_HOT_SPRINGS 0x28
#define MB_REFLECTION_UNDER_BRIDGE 0x2B
#define MB_TELEVISION 0x86
#define MB_CABLE_BOX_RESULTS_1 0x84
#define MB_SECRET_BASE_PC 0xB0
#define MB_PLAYER_ROOM_PC_ON 0xC5
#define MB_COUNTER 0x80
#define MB_PC 0x83
/* MB_SECRET_BASE_REGISTER_PC is defined in vx_behavior.c's header block below. */
#define MB_SECRET_BASE_REGISTER_PC 0xB1

#define ST_OAM_AFFINE_OFF 0
#define ST_OAM_SQUARE 0
#define ST_OAM_H_RECTANGLE 1
#define ST_OAM_V_RECTANGLE 2

/* Field-effect object template indices (confirmed against the ROM's template table). */
#define FLDEFF_OBJ_SHADOW_S_INDEX 0
#define FLDEFFOBJ_SHADOW_S 0
#define FLDEFFOBJ_SHADOW_XL 3
#define FLDEFFOBJ_TALL_GRASS 4
#define FLDEFFOBJ_RIPPLE 5
#define FLDEFFOBJ_SURF_BLOB 7
#define FLDEFFOBJ_SAND_FOOTPRINTS 11
#define FLDEFFOBJ_LONG_GRASS 15
#define FLDEFFOBJ_DEEP_SAND_FOOTPRINTS 23
#define FLDEFFOBJ_BIKE_TIRE_TRACKS 27

#define ARRAY_COUNT(arr) (sizeof(arr) / sizeof((arr)[0]))

/* BGR555 from 5-bit components (r,g,b in 0..31). */
#define GBA_RGB5(r, g, b) ((u16)((r) | ((g) << 5) | ((b) << 10)))
#define RGB2(r, g, b) GBA_RGB5(r, g, b)

/* ---- The active game profile --------------------------------------------------------------- */
/* gVxProf is set by vx_host.c Rebind() (defined in vx_adapter.c). NULL = the Emerald row, so host tests and
 * every pre-Phase-34 caller behave exactly as before. */
extern const GameProfile *gVxProf;
static inline const GameProfile *vx_prof(void)
{
    return gVxProf != NULL ? gVxProf : gameprof_emerald();
}
#define VXP(field) (vx_prof()->field)

/* ---- Host mirror structs ----------------------------------------------------------------- */
struct Tileset
{
    bool8 isCompressed, isSecondary;
    const void *tiles;
    const u16 *palettes;
    const u16 *metatiles;
    const u16 *metatileAttributes;
    GbaPtr gbaAddr;
};

struct MapLayout
{
    s32 width, height;
    const u16 *border, *map;
    const struct Tileset *primaryTileset, *secondaryTileset;
    /* Effective border size in cells. Emerald: 2x2 always; FireRed/LeafGreen: 2x2, 3x2, or a 0x0 (indoor) border
     * that the adapter stores as one metatile-0 cell (1x1). 0 = not set by a hand-built layout: read as 2x2. */
    u8 borderWidth, borderHeight;
};

struct MapConnection
{
    u8 direction;
    s32 offset;
    u8 mapGroup, mapNum;
};

struct MapConnections
{
    s32 count;
    const struct MapConnection *connections;
};

struct BgEvent
{
    u16 x, y;
    u8 elevation, kind;
};

struct ObjectEventTemplate
{
    u8 graphicsId;
};

struct MapEvents
{
    u8 objectEventCount, bgEventCount;
    const struct ObjectEventTemplate *objectEvents;
    const struct BgEvent *bgEvents;
};

struct MapHeader
{
    const struct MapLayout *mapLayout;
    const struct MapEvents *events;
    const struct MapConnections *connections;
    u16 mapLayoutId;
    u8 regionMapSectionId, cave, weather, mapType;
    GbaPtr gbaAddr;
};

struct BackupMapLayout
{
    s32 width, height;
    u16 *map;
};

struct Coords16
{
    s16 x, y;
};

struct ObjectEvent
{
    u32 active : 1, singleMovementActive : 1, heldMovementActive : 1, invisible : 1, isPlayer : 1,
        hasReflection : 1;
    u8 spriteId, graphicsId, currentElevation, facingDirection;
    struct Coords16 currentCoords, previousCoords;
};

/* Bit layout of the hardware's OAM attribute words; decoded by explicit shifts, never memcpy'd. */
struct OamData
{
    u32 y : 8, affineMode : 2, objMode : 2, mosaic : 1, bpp : 1, shape : 2, x : 9, matrixNum : 5,
        size : 2;
    u16 tileNum : 10, priority : 2, paletteNum : 4;
    u16 affineParam;
};

struct SpriteTemplate; /* only ever a GbaPtr; never defined */

struct Sprite
{
    struct OamData oam;
    GbaPtr template;
    s16 x, y, x2, y2;
    s8 centerToCornerVecX, centerToCornerVecY;
    s16 data[8];
    u8 inUse : 1, coordOffsetEnabled : 1, invisible : 1;
    u8 subpriority;
};

struct SpriteFrameImage
{
    const void *data;
    u16 size;
    u32 offset; /* always 0: payloads are direct host pointers into the ROM buffer */
};

struct ObjectEventGraphicsInfo
{
    s16 width, height;
    u16 size;
    const struct SpriteFrameImage *images;
};

struct PlayerAvatar
{
    u8 flags, spriteId, objectEventId;
};

struct SaveBlock1
{
    struct Coords16 pos;
    struct
    {
        u8 mapGroup, mapNum;
    } location;
};

struct Main
{
    GbaPtr callback2;
    u8 inBattle;
};

struct PaletteFade
{
    u8 y;
    bool8 active;
};

struct Weather
{
    u8 currWeather, palProcessingState, currBlendEVA, fogHSpritesCreated, fogDSpritesCreated;
};

/* ---- Host globals the vendored code reads (defined in vx_adapter.c, filled per snapshot) ---- */
extern struct MapHeader gMapHeader;
extern struct BackupMapLayout gBackupMapLayout;
extern struct SaveBlock1 *gSaveBlock1Ptr; /* NULL when the snapshot has none */
extern struct ObjectEvent gObjectEvents[OBJECT_EVENTS_COUNT];
extern struct PlayerAvatar gPlayerAvatar;
extern struct Sprite gSprites[GBA_SPRITE_COUNT];
extern struct Main gMain;
extern struct Weather *gWeatherPtr;
extern struct PaletteFade gPaletteFade;
extern u16 gPlttBufferUnfaded[512];
extern GbaPtr gFieldEffectObjectTemplatePointers[GBA_FLDEFF_TEMPLATE_COUNT];

/* ---- Adapter functions (vx_adapter.c / vx_behavior.c) ------------------------------------- */
const struct Tileset *vx_tileset_at(GbaPtr addr);
#define gTileset_General (*vx_tileset_at(VXP(tsGeneral)))
#define gTileset_Fortree (*vx_tileset_at(GBA_ADDR_TILESET_FORTREE))
#define gTileset_GenericBuilding (*vx_tileset_at(GBA_ADDR_TILESET_GENERIC_BUILDING))

const struct MapHeader *GetMapHeaderFromConnection(const struct MapConnection *conn);
const struct MapLayout *Port_GetMapLayoutById(u16 layoutId);
/* Border cell count (bw*bh, 2x2 when the layout does not say) and the index into layout->border of the cell the game's
 * GetBorderBlockAt picks for backup-layout coordinates (bx, by): ((x - MAP_OFFSET) mod bw) + ((y - MAP_OFFSET) mod bh) * bw. */
unsigned vx_border_cells(const struct MapLayout *layout);
int vx_border_cell(const struct MapLayout *layout, int bx, int by);
const struct ObjectEventGraphicsInfo *GetObjectEventGraphicsInfo(u8 graphicsId);
u8 GetCurrentWeather(void);
const void *Port_ResolveAssetPointer(const void *p);
u32 Port_GetAssetSizeExact(const void *p);
u32 Port_GetSpriteFrameSize(const void *data, u32 declaredSize);
const u8 *Port_PeekSpriteFramePointer(const void *data, u32 size, u32 offset);

/* Behaviour predicates (vx_behavior.c). */
bool8 MetatileBehavior_EmeraldIsReflective(u8 b);   /* the raw Emerald tables the Emerald profile row is built from */
bool8 MetatileBehavior_EmeraldIsIce(u8 b);
bool8 MetatileBehavior_EmeraldIsSurfableWaterOrUnderwater(u8 b);
bool8 MetatileBehavior_EmeraldIsShallowFlowingWater(u8 b);
bool8 MetatileBehavior_IsReflective(u8 b);
bool8 MetatileBehavior_IsIce(u8 b);
bool8 MetatileBehavior_IsSurfableWaterOrUnderwater(u8 b);
bool8 MetatileBehavior_IsPuddle(u8 b);
bool8 MetatileBehavior_IsShallowFlowingWater(u8 b);
bool8 MetatileBehavior_IsCounter(u8 b);
bool8 MetatileBehavior_IsPC(u8 b);
bool8 MetatileBehavior_IsSecretBasePC(u8 b);
bool8 MetatileBehavior_IsPlayerRoomPCOn(u8 b);

#endif /* VX_GBA_GAME_H */
