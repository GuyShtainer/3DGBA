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
/* Look backlog L1: one-cell shrubs, ROM-measured with `romgen author ROM shrubs` (+ `shrubs TS` for a family's walkable
 * members) and checked by eye on its contact and context sheets (PROVENANCE "ROM-measured, no decomp"). Every one draws
 * its bush on the upper layer and its ground on the lower one (test_romgen_shrubs pins that). Emerald: the Dewford
 * tileset's hedge bushes (Dewford Town, Route 106), with 0x239 the bush over the sand in front of a hedge and 0x23A the
 * trunk cell under a bush; three round bushes of the tileset at 0x083DF80C (layout 9); Slateport's round bush; two round
 * bushes of the tileset at 0x083DF86C (layout 345). */
static const GpShrub kEmeraldShrubs[] = {
    {0x083DF74Cu, 0x239, GP_PROP_BUSH}, {0x083DF74Cu, 0x23A, GP_PROP_BUSH}, {0x083DF74Cu, 0x242, GP_PROP_BUSH}, {0x083DF74Cu, 0x243, GP_PROP_BUSH}, {0x083DF74Cu, 0x247, GP_PROP_BUSH},
    {0x083DF80Cu, 0x220, GP_PROP_BUSH}, {0x083DF80Cu, 0x23B, GP_PROP_BUSH}, {0x083DF80Cu, 0x23F, GP_PROP_BUSH},
    {0x083DF764u, 0x243, GP_PROP_BUSH},
    {0x083DF86Cu, 0x202, GP_PROP_BUSH}, {0x083DF86Cu, 0x203, GP_PROP_BUSH},
    /* Look L8 props, General tileset (ROM-measured, `romgen author ROM props`, PROVENANCE): the white picket row 0x149 and
     * the fence posts 0x140 / 0x142 (the upper layer is the fence, the lower one grass), the boulders 0x0E0 (on grass),
     * 0x0E2 (on sand), 0x0E1 (on the cliff colour), and the red flower bed 0x004 (upper layer opaque, keyed against its
     * own lower layer). Emerald's sea rocks are building-pipeline models already and are not here. */
    {0, 0x149, GP_PROP_FENCE_EW}, {0, 0x140, GP_PROP_FENCE_NS}, {0, 0x142, GP_PROP_FENCE_NS},
    {0, 0x0E0, GP_PROP_ROCK}, {0, 0x0E1, GP_PROP_ROCK}, {0, 0x0E2, GP_PROP_ROCK},
    {0, 0x004, GP_PROP_FLOWER},
};
/* FireRed / LeafGreen rev 1: the General round bush 0x005 (Pallet Town, Route 1 and on), and the round bushes 0x2F4 and
 * 0x2E0 of two secondary tilesets (layouts 147 and 100), whose addresses differ by 0x20 between the two games. */
/* Look L8 props shared by FireRed and LeafGreen (the General tileset's ids and art are identical, ROM-measured, `romgen
 * author ROM props`; only the palette differs): the white picket rows 0x0E7 / 0x0EC / 0x0ED and the log row 0x0E6, the
 * fence posts seen along their run 0x0F4 / 0x0F5 / 0x0EF (white) and 0x0F0 / 0x0F1 (wooden), the grey bollard pairs 0x0D6 /
 * 0x0D7 / 0x0B4 / 0x0B5, the sea rocks (upper layer = rock plus a foam ring in water colours, lower layer = water): the
 * four of the open sea 0x110 0x111 0x118 0x119, 0x1CB 0x1CC 0x1D3 0x1D4, 0x212 0x213 0x21A 0x21B and the deep-water copies
 * 0x244 0x245 0x24C 0x24D, and the red flower bed 0x004. */
#define GP_FRLG_PROPS \
    {0, 0x0E7, GP_PROP_FENCE_EW}, {0, 0x0EC, GP_PROP_FENCE_EW}, {0, 0x0ED, GP_PROP_FENCE_EW}, {0, 0x0E6, GP_PROP_FENCE_EW}, \
    {0, 0x0F4, GP_PROP_FENCE_NS}, {0, 0x0F5, GP_PROP_FENCE_NS}, {0, 0x0EF, GP_PROP_FENCE_NS}, {0, 0x0F0, GP_PROP_FENCE_NS}, \
    {0, 0x0F1, GP_PROP_FENCE_NS}, {0, 0x0D6, GP_PROP_FENCE_EW}, {0, 0x0D7, GP_PROP_FENCE_EW}, {0, 0x0B4, GP_PROP_FENCE_EW}, \
    {0, 0x0B5, GP_PROP_FENCE_EW}, \
    {0, 0x110, GP_PROP_ROCK}, {0, 0x111, GP_PROP_ROCK}, {0, 0x118, GP_PROP_ROCK}, {0, 0x119, GP_PROP_ROCK}, \
    {0, 0x1CB, GP_PROP_ROCK}, {0, 0x1CC, GP_PROP_ROCK}, {0, 0x1D3, GP_PROP_ROCK}, {0, 0x1D4, GP_PROP_ROCK}, \
    {0, 0x212, GP_PROP_ROCK}, {0, 0x213, GP_PROP_ROCK}, {0, 0x21A, GP_PROP_ROCK}, {0, 0x21B, GP_PROP_ROCK}, \
    {0, 0x244, GP_PROP_ROCK}, {0, 0x245, GP_PROP_ROCK}, {0, 0x24C, GP_PROP_ROCK}, {0, 0x24D, GP_PROP_ROCK}, \
    {0, 0x004, GP_PROP_FLOWER}
static const GpShrub kFireRedShrubs[] = {{0, 0x005, GP_PROP_BUSH}, {0x082D4BC4u, 0x2F4, GP_PROP_BUSH}, {0x082D4B7Cu, 0x2E0, GP_PROP_BUSH}, GP_FRLG_PROPS};
static const GpShrub kLeafGreenShrubs[] = {{0, 0x005, GP_PROP_BUSH}, {0x082D4BA4u, 0x2F4, GP_PROP_BUSH}, {0x082D4B5Cu, 0x2E0, GP_PROP_BUSH}, GP_FRLG_PROPS};
#define GP_SHRUBS(t) .shrubs = t, .shrubCount = (uint8_t)(sizeof t / sizeof t[0])
/* Look L2: tall-grass metatiles (behaviour 0x02) whose ground is not the General plain grass (grassGround): Emerald 0x206 of
 * the tileset at 0x083DF794 draws its blades over a sandy-brown ground (ROM-measured, `romgen author ROM grass`). */
static const GpShrub kEmeraldGrassSkip[] = {{0x083DF794u, 0x206, GP_PROP_BUSH}};

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
    GP_SHRUBS(kEmeraldShrubs),
    .grassGround = 0x001, .grassSkip = kEmeraldGrassSkip, .grassSkipCount = 1,
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

/* G2: houseHalfWidth 8 / houseHeight 7 are measured: door-to-building extents over the 191 census doors (SPEC section 5.1 seeds),
 * 8 columns cover 98 % and 7 rows cover 97 % (the Emerald placeholders 5 / 7 cover 83 % / 97 %). */
#define GP_FRLG_COMMON                                                                                              \
    .rev = 1, .groupCount = 43, .groupSizes = kFrlgGroupSizes, .layoutSlots = 384, .nPrimMetatiles = 640,         \
    .nPrimTiles = 640, .nPrimPals = 7, .nMetatilesTotal = 1024, .tilesetAttrOff = 0x14, .attrBytes = 4,           \
    .behMask = 0x1FF, .layerMask = 0x60000000u, .layerShift = 29, .layoutBytes = 26, .houseHalfWidth = 8,          \
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
                               .fldeffTemplates = 0x083A0080u, GP_SHRUBS(kFireRedShrubs), .grassGround = 0x001};
static GameProfile sLeafGreen = {GP_FRLG_COMMON, .game = GP_LEAFGREEN, .code = {'B', 'P', 'G', 'E'},
                                 .dataSubdir = "BPGE", .mapGroups = 0x083526F8u, .mapLayouts = 0x0834EBDCu,
                                 .tsGeneral = 0x082D4AE4u, .tsBuilding = 0x082D4C04u, .weatherPtr = 0x083C2A68u,
                                 .gfxInfoPtrs = 0x0839FE00u, .fldeffTemplates = 0x083A0060u,
                                 GP_SHRUBS(kLeafGreenShrubs), .grassGround = 0x001};

/* Ruby / Sapphire rev 2 (Phase 35 S0): the ROM layer only, measured on the user's carts (docs/phase35-rs/PHASE.md, recon;
 * PROVENANCE "ROM-measured"). AXVE / AXPE rev 2. The map data are in Emerald's format (map header 0x1C, layout 24 bytes,
 * tileset attributes u16 at +0x10, 512 primary metatiles and tiles, 6 primary palettes, the behaviour numbering Emerald
 * kept), so the Emerald constants and behaviour sets serve. gMapGroups and gMapLayouts were found by searching each ROM for
 * the table every map header agrees with; the group sizes are that table's pointer gaps (34 groups, 394 maps). Ruby and
 * Sapphire hold byte-identical blockdata in all 332 layouts and identical content in all 56 tilesets; only the addresses
 * differ. The renderer anchors are not harvested (all 0, `rendererOn` false): gameprof_detect() refuses these rows. */
static const uint8_t kRsGroupSizes[34] = {54, 5, 5, 6, 7, 7, 8, 7, 7, 13, 8, 17, 10, 24, 13, 13, 14, 2, 2, 2, 3, 1, 1,
                                          1, 86, 44, 12, 2, 1, 13, 1, 1, 3, 1};

#define GP_RS_COMMON                                                                                                  \
    .rev = 2, .groupCount = 34, .groupSizes = kRsGroupSizes, .layoutSlots = 332,                                      \
    .nPrimMetatiles = NUM_METATILES_IN_PRIMARY, .nPrimTiles = NUM_TILES_IN_PRIMARY, .nPrimPals = NUM_PALS_IN_PRIMARY, \
    .nMetatilesTotal = NUM_METATILES_TOTAL, .tilesetAttrOff = 0x10, .attrBytes = 2, .behMask = GBA_BEHAVIOR_MASK,     \
    .layerMask = GBA_ATTR_LAYER_MASK, .layerShift = GBA_ATTR_LAYER_SHIFT, .layoutBytes = GBA_ROM_MAPLAYOUT_BYTES,     \
    .houseHalfWidth = 5, .houseHeight = 7, .emeraldIdTables = false, .interiors3d = false, .rendererOn = false

static GameProfile sRuby = {GP_RS_COMMON, .game = GP_RUBY, .code = {'A', 'X', 'V', 'E'}, .dataSubdir = "AXVE",
                            .mapGroups = 0x083085A0u, .mapLayouts = 0x08304F30u, .tsGeneral = 0x08286D0Cu,
                            .tsBuilding = 0x08286E5Cu};
static GameProfile sSapphire = {GP_RS_COMMON, .game = GP_SAPPHIRE, .code = {'A', 'X', 'P', 'E'}, .dataSubdir = "AXPE",
                                .mapGroups = 0x08308530u, .mapLayouts = 0x08304EC0u, .tsGeneral = 0x08286C9Cu,
                                .tsBuilding = 0x08286DECu};

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
    memset(&p->bladeGrass, 0, sizeof p->bladeGrass);
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
        if (b == 0x02u) set_bit(&p->bladeGrass, b);   /* L2: tall grass proper (0x03 long, 0x07 short, 0x09 ash grass are not) */
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
    p->water = p->jump = p->houseDoor = p->sand = p->tallGrass = p->signpost = p->bladeGrass = zero;
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
    set_bit(&p->bladeGrass, 0x02);
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
        build_emerald_sets(&sRuby);         /* Phase 35: the behaviour numbering Emerald kept from Ruby / Sapphire */
        build_emerald_sets(&sSapphire);
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
    const GameProfile *rows[5];
    unsigned i;

    if (rom == NULL || size < GP_HDR_MIN)
        return NULL;
    frlg_rows_init();
    rows[0] = gameprof_emerald();
    rows[1] = &sFireRed;
    rows[2] = &sLeafGreen;
    rows[3] = &sRuby;
    rows[4] = &sSapphire;
    for (i = 0; i < 5u; i++) {
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
