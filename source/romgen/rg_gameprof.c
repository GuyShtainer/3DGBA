/* rg_gameprof.c -- the per-game profile rows (3DGBA, GPLv3). Pure C. See rg_gameprof.h. */
#include "rg_gameprof.h"

#include <string.h>

#include "gba_game.h"
#include "rg_behavior.h"

#define GP_HDR_CODE 0xACu
#define GP_HDR_REV 0xBCu
#define GP_HDR_MIN 0xC0u
#define GP_BEH_VALUES 512u

/* T1: the tree tables, flat pairs (see rg_gameprof.h). Emerald's are the tables voxel_tree.c carried before the
 * profile had them (Zallax's General-tileset ids), unchanged; test_voxel_world pins every id 0..1023. */
static const int16_t kEmeraldTreePart[] = {
    0x1D4, 0, 0x1D6, 0, 0x1D5, 1, 0x1D7, 1,                       /* upper row of a large tree, incl. the forest edge */
    0x1DC, 2, 0x1DE, 2, 0x1E4, 2, 0x1E6, 2, 0x1EC, 2,            /* lower row; 1EC under a small tree's canopy top */
    0x1DD, 3, 0x1DF, 3, 0x1E5, 3, 0x1E7, 3, 0x1ED, 3,
    0x016, 4, 0x017, 4, 0x0C6, 4, 0x0C7, 4, 0x1F4, 4, 0x1F5, 4,  /* small trees (4 = VOXEL_TREE_SMALL) */
};
static const int16_t kEmeraldTreeGround[] = {
    0x1C6, 0x00D, 0x1C7, 0x00D, 0x1CE, 0x001, 0x1CF, 0x001,       /* canopy fringes: tall grass / grass beneath */
    0x00E, 0x001, 0x00F, 0x001, 0x040, 0x001,                     /* a small tree's canopy top; fence feet: grass */
    0x01D, 0x002, 0x025, 0x00D, 0x02D, 0x0A1, 0x035, 0x170, 0x193, 0x170, 0x0CE, 0x091,
};
/* FireRed / LeafGreen rev 1 (identical on both, measured with `romgen author ROM trees`, PROVENANCE "ROM-measured, no
 * decomp"): the General tileset's tree wall. Top row (parts 0/1) 0x1C/0x1D with the edge variants 0x1E/0x1F; bottom row
 * (parts 2/3) 0x14/0x15, the edge variants 0x16/0x17, and the trunk row 0x24/0x25 with its variants 0x26/0x27. Even ids
 * are the left column, odd the right. No ground replacements: no tree metatile paints canopy over other ground. */
static const int16_t kFrlgTreePart[] = {
    0x1C, 0, 0x1E, 0, 0x1D, 1, 0x1F, 1,
    0x14, 2, 0x16, 2, 0x24, 2, 0x26, 2,
    0x15, 3, 0x17, 3, 0x25, 3, 0x27, 3,
};
#define GP_FRLG_TREES .treePart = kFrlgTreePart, .treePartCount = sizeof kFrlgTreePart / sizeof kFrlgTreePart[0] / 2u

/* The Emerald row is built from the existing macros so it cannot drift (SPEC 1.2 rule 1). */
static GameProfile sEmerald = {
    .game = GP_EMERALD,
    .code = {'B', 'P', 'E', 'E'},
    .rev = 0,
    .dataSubdir = "",
    .mapGroups = GBA_ADDR_MAP_GROUPS,
    .groupCount = GBA_MAP_GROUP_COUNT,
    .groupSizes = NULL,
    .mapLayouts = GBA_ADDR_MAP_LAYOUTS,
    .layoutSlots = 442,
    .nPrimMetatiles = NUM_METATILES_IN_PRIMARY,
    .nPrimTiles = NUM_TILES_IN_PRIMARY,
    .nPrimPals = NUM_PALS_IN_PRIMARY,
    .nMetatilesTotal = NUM_METATILES_TOTAL,
    .tilesetAttrOff = 0x10,
    .attrBytes = 2,
    .behMask = GBA_BEHAVIOR_MASK,
    .layerMask = GBA_ATTR_LAYER_MASK,
    .layerShift = GBA_ATTR_LAYER_SHIFT,
    .layoutBytes = GBA_ROM_MAPLAYOUT_BYTES,
    .tsGeneral = GBA_ADDR_TILESET_GENERAL,
    .tsBuilding = GBA_ADDR_TILESET_BUILDING,
    .houseHalfWidth = 5,
    .houseHeight = 7,
    .gMain = GBA_ADDR_GMAIN,
    .sb1Ptr = GBA_ADDR_SB1_PTR,
    .backupLayout = GBA_ADDR_BACKUP_LAYOUT,
    .backupMap = GBA_ADDR_BACKUP_MAP,
    .mapHeader = GBA_ADDR_MAP_HEADER,
    .objEvents = GBA_ADDR_OBJECT_EVENTS,
    .playerAvatar = GBA_ADDR_PLAYER_AVATAR,
    .sprites = GBA_ADDR_SPRITES,
    .plttUnfaded = GBA_ADDR_PLTT_UNFADED,
    .paletteFade = GBA_ADDR_PALETTE_FADE,
    .playerAvatarBytes = GBA_PLAYER_AVATAR_BYTES,
    .weather = GBA_ADDR_WEATHER,
    .weatherPtr = 0,
    .weatherOff = {GBA_OFF_WEATHER_CURR, GBA_OFF_WEATHER_PALSTATE, GBA_OFF_WEATHER_EVA, GBA_OFF_WEATHER_FOGH,
                   GBA_OFF_WEATHER_FOGD},
    .gfxInfoPtrs = GBA_ADDR_GFX_INFO_PTRS,
    .gfxInfoCount = GBA_GFX_INFO_COUNT,
    .fldeffTemplates = GBA_ADDR_FLDEFF_TEMPLATES,
    .fldeffCount = GBA_FLDEFF_TEMPLATE_COUNT,
    .cb2Overworld = CB2_Overworld,
    .cb2OverworldBasic = CB2_OverworldBasic,
    .treePart = kEmeraldTreePart,
    .treePartCount = sizeof kEmeraldTreePart / sizeof kEmeraldTreePart[0] / 2u,
    .treeGround = kEmeraldTreeGround,
    .treeGroundCount = sizeof kEmeraldTreeGround / sizeof kEmeraldTreeGround[0] / 2u,
    .emeraldIdTables = true,
    .interiors3d = true,
    .rendererOn = true,
};

/* FireRed / LeafGreen rev 1 (Phase 34 G1 + R1): the ROM layer, the romgen behaviour sets and the renderer anchors.
 * ROM-layer numbers measured on the user's ROMs (docs/phase34-frlg/SURVEY.md, M1) and, for the behaviour values,
 * pokefirered@037335f include/constants/metatile_behaviors.h (numbers only), see docs/PROVENANCE.md. The R1 anchors
 * (the RAM block and the five ROM anchors) are listed per value in docs/PROVENANCE.md and docs/phase34-frlg/BUILDLOG-P34.md
 * with the method of each. `rendererOn` is true from R2: gameprof_detect() (the renderer's entry) returns these rows;
 * vx_host still runs the anchor self-check on every bind and map change and falls back to 2D on a failure. */
static const uint8_t kFrlgGroupSizes[43] = {5, 123, 60, 66, 4, 6, 8, 10, 6, 8, 20, 10, 8, 2, 10, 4, 2, 2, 2, 1, 1, 2,
                                            2, 3, 2, 3, 2, 1, 1, 1, 1, 7, 5, 5, 8, 8, 5, 5, 1, 1, 1, 2, 1};

#define GP_FRLG_COMMON                                                                                              \
    .rev = 1, .groupCount = 43, .groupSizes = kFrlgGroupSizes, .layoutSlots = 384, .nPrimMetatiles = 640,         \
    .nPrimTiles = 640, .nPrimPals = 7, .nMetatilesTotal = 1024, .tilesetAttrOff = 0x14, .attrBytes = 4,           \
    .behMask = 0x1FF, .layerMask = 0x60000000u, .layerShift = 29, .layoutBytes = 26, .houseHalfWidth = 5,          \
    .houseHeight = 7, .playerAvatarBytes = 0x20, .gfxInfoCount = 152, .fldeffCount = 36, .emeraldIdTables = false,  \
    .interiors3d = false, .rendererOn = true,                                                                      \
    .gMain = 0x030030F0u, .sb1Ptr = 0x03005008u, .backupLayout = 0x03005040u, .backupMap = 0,                     \
    .mapHeader = 0x02036DFCu, .objEvents = 0x02036E38u, .playerAvatar = 0x02037078u, .sprites = 0x0202063Cu,      \
    .plttUnfaded = 0x020371F8u, .paletteFade = 0x02037AB8u, .weather = 0,                                          \
    .weatherOff = {0x6D0, 0x6C6, 0x730, 0x6FB, 0x724},                                                             \
    .cb2Overworld = 0x080565C9u, .cb2OverworldBasic = 0x080565BDu,                                          \
    GP_FRLG_TREES

static GameProfile sFireRed = {GP_FRLG_COMMON, .game = GP_FIRERED, .code = {'B', 'P', 'R', 'E'}, .dataSubdir = "BPRE",
                               .mapGroups = 0x08352718u, .mapLayouts = 0x0834EBFCu, .tsGeneral = 0x082D4B04u,
                               .tsBuilding = 0x082D4C24u, .weatherPtr = 0x083C2C2Cu, .gfxInfoPtrs = 0x0839FE20u,
                               .fldeffTemplates = 0x083A0080u};
static GameProfile sLeafGreen = {GP_FRLG_COMMON, .game = GP_LEAFGREEN, .code = {'B', 'P', 'G', 'E'},
                                 .dataSubdir = "BPGE", .mapGroups = 0x083526F8u, .mapLayouts = 0x0834EBDCu,
                                 .tsGeneral = 0x082D4AE4u, .tsBuilding = 0x082D4C04u, .weatherPtr = 0x083C2A68u,
                                 .gfxInfoPtrs = 0x0839FE00u, .fldeffTemplates = 0x083A0060u};

static void set_bit(GpBehSet *s, unsigned b)
{
    s->w[b >> 5] |= 1u << (b & 31u);
}

/* Tall grass is not a predicate anywhere in the Emerald code; the four values are pinned by the host test. */
static bool is_tall_grass(unsigned b)
{
    return b == 0x02u || b == 0x03u || b == 0x07u || b == 0x09u;
}

/* The renderer predicates take a u8, so values >= 256 are never members. */
static bool in_surfable(unsigned b) { return b < 256u && MetatileBehavior_EmeraldIsSurfableWaterOrUnderwater((u8)b); }
static bool in_reflective(unsigned b) { return b < 256u && MetatileBehavior_EmeraldIsReflective((u8)b); }
static bool in_ice(unsigned b) { return b < 256u && MetatileBehavior_EmeraldIsIce((u8)b); }
static bool in_shallow(unsigned b) { return b < 256u && MetatileBehavior_EmeraldIsShallowFlowingWater((u8)b); }
static bool in_furniture(unsigned b)
{
    return b < 256u
           && (MetatileBehavior_IsCounter((u8)b) || MetatileBehavior_IsPC((u8)b)
               || MetatileBehavior_IsSecretBasePC((u8)b) || MetatileBehavior_IsPlayerRoomPCOn((u8)b)
               || b == MB_SECRET_BASE_REGISTER_PC || b == MB_TELEVISION);
}

static void build_emerald_sets(GameProfile *p)
{
    unsigned b;

    memset(&p->water, 0, sizeof p->water);
    memset(&p->jump, 0, sizeof p->jump);
    memset(&p->houseDoor, 0, sizeof p->houseDoor);
    memset(&p->sand, 0, sizeof p->sand);
    memset(&p->tallGrass, 0, sizeof p->tallGrass);
    memset(&p->signpost, 0, sizeof p->signpost);
    memset(&p->surfable, 0, sizeof p->surfable);
    memset(&p->reflective, 0, sizeof p->reflective);
    memset(&p->ice, 0, sizeof p->ice);
    memset(&p->shallowFlowing, 0, sizeof p->shallowFlowing);
    memset(&p->furniture, 0, sizeof p->furniture);
    for (b = 0; b < GP_BEH_VALUES; b++) {
        if (rg_is_water(b)) set_bit(&p->water, b);
        if (rg_is_jump(b)) set_bit(&p->jump, b);
        if (rg_is_house_door(b)) set_bit(&p->houseDoor, b);
        if (rg_is_sand(b)) set_bit(&p->sand, b);
        if (is_tall_grass(b)) set_bit(&p->tallGrass, b);
        if (in_surfable(b)) set_bit(&p->surfable, b);
        if (in_reflective(b)) set_bit(&p->reflective, b);
        if (in_ice(b)) set_bit(&p->ice, b);
        if (in_shallow(b)) set_bit(&p->shallowFlowing, b);
        if (in_furniture(b)) set_bit(&p->furniture, b);
    }
}

static void set_range(GpBehSet *s, unsigned lo, unsigned hi)
{
    unsigned b;

    for (b = lo; b <= hi; b++)
        set_bit(s, b);
}

/* SPEC-P34 section 2, FRLG column. FR and LG share every set. */
static void build_frlg_sets(GameProfile *p)
{
    static const uint16_t kWater[] = {0x10, 0x11, 0x12, 0x13, 0x15, 0x16, 0x17, 0x19, 0x1B, 0x22, 0x28};
    static const uint16_t kSurf[] = {0x10, 0x11, 0x12, 0x13, 0x15, 0x19, 0x1B, 0x22};
    unsigned i;
    GpBehSet zero;

    memset(&zero, 0, sizeof zero);
    p->water = p->jump = p->houseDoor = p->sand = p->tallGrass = p->signpost = zero;
    p->surfable = p->reflective = p->ice = p->shallowFlowing = p->furniture = zero;
    for (i = 0; i < sizeof kWater / sizeof kWater[0]; i++)
        set_bit(&p->water, kWater[i]);
    set_range(&p->water, 0x50, 0x53);
    for (i = 0; i < sizeof kSurf / sizeof kSurf[0]; i++)
        set_bit(&p->surfable, kSurf[i]);
    set_range(&p->surfable, 0x50, 0x53);
    set_range(&p->jump, 0x38, 0x3B);              /* E, W, N, S: no diagonals in Kanto */
    set_bit(&p->houseDoor, 0x69);                 /* 0x8B / 0x8D are a dresser / the cable-club monitor here */
    set_bit(&p->sand, 0x21);
    set_bit(&p->tallGrass, 0x02);
    set_bit(&p->signpost, 0x84);
    set_bit(&p->reflective, 0x10);
    set_bit(&p->reflective, 0x16);
    set_bit(&p->reflective, 0x23);
    set_bit(&p->ice, 0x23);
    set_bit(&p->shallowFlowing, 0x17);
    set_bit(&p->furniture, 0x80);
    set_bit(&p->furniture, 0x83);
    set_bit(&p->furniture, 0x86);
}

static void frlg_rows_init(void)
{
    static bool built;

    if (!built) {
        build_frlg_sets(&sFireRed);
        build_frlg_sets(&sLeafGreen);
        built = true;
    }
}

const GameProfile *gameprof_emerald(void)
{
    static bool built;

    if (!built) {
        build_emerald_sets(&sEmerald);
        built = true;
    }
    return &sEmerald;
}

static const GameProfile *detect(const uint8_t *rom, size_t size, bool renderer)
{
    const GameProfile *rows[3];
    unsigned i;

    if (rom == NULL || size < GP_HDR_MIN)
        return NULL;
    frlg_rows_init();
    rows[0] = gameprof_emerald();
    rows[1] = &sFireRed;
    rows[2] = &sLeafGreen;
    for (i = 0; i < 3u; i++) {
        const GameProfile *r = rows[i];
        if (r->game == GP_NONE || memcmp(rom + GP_HDR_CODE, r->code, 4) != 0)
            continue;
        if (r->game != GP_EMERALD && rom[GP_HDR_REV] != r->rev)
            continue;
        if (renderer && !r->rendererOn)
            continue;   /* anchors harvested (R1) but the renderer path is R2's: refuse until it is switched on */
        return r;
    }
    return NULL;
}

const GameProfile *gameprof_detect(const uint8_t *rom, size_t size)
{
    return detect(rom, size, true);
}

const GameProfile *gameprof_detect_romgen(const uint8_t *rom, size_t size)
{
    return detect(rom, size, false);
}
