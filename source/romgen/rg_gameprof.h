/* rg_gameprof.h -- the per-game profile (3DGBA, GPLv3). Pure C.
 *
 * One struct that both layers read: romgen (the ROM layout constants and behaviour sets) and the voxel
 * renderer (the same, plus the RAM anchors). Emerald is one row; FireRed and LeafGreen rev 1 are two more.
 * There is no #if per game. Spec: docs/phase34-frlg/SPEC.md section 1.
 *
 * Phase 34 slice R0: only the Emerald row is real. The FRLG rows exist with game == GP_NONE, so
 * gameprof_detect() still refuses those cartridges; R1 fills their anchors, R2 enables them.
 *
 * Terms are fixed: "game profile", type GameProfile, functions gameprof_*. */
#ifndef RG_GAMEPROF_H
#define RG_GAMEPROF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Phase 35: Ruby and Sapphire are appended, so the existing values (and romgen_dev's per-game bit) do not move. */
typedef enum { GP_NONE = 0, GP_EMERALD, GP_FIRERED, GP_LEAFGREEN, GP_RUBY, GP_SAPPHIRE } GpGame;

/* 512 bits: metatile behaviour values 0..0x1FF. */
typedef struct GpBehSet { uint32_t w[16]; } GpBehSet;

static inline bool gp_beh(const GpBehSet *s, unsigned b)
{
    return b < 512u && ((s->w[b >> 5] >> (b & 31u)) & 1u) != 0u;
}

struct RgSpec;

/* Look backlog L1: a one-cell shrub (a bush drawn on a metatile's upper layer over its ground). `tileset` is the GBA
 * address of the secondary tileset the id belongs to (a secondary id means nothing without it), 0 for a primary id;
 * `metatile` < 1024. */
typedef struct GpShrub { uint32_t tileset; uint16_t metatile; uint8_t kind; } GpShrub;
/* Look backlog L8: what a table entry stands up as. 0 (the default of every L1 initialiser) is the one-cell bush; the others
 * are props drawn on the same slot machinery (upper layer = the art, lower layer = the ground under it): a fence picket
 * row (GP_PROP_FENCE_EW), fence posts seen along their run (GP_PROP_FENCE_NS), a rock whose ground colours are keyed out
 * of its art (GP_PROP_ROCK), a flower bed keyed the same way (GP_PROP_FLOWER). */
enum { GP_PROP_BUSH = 0, GP_PROP_FENCE_EW, GP_PROP_FENCE_NS, GP_PROP_ROCK, GP_PROP_FLOWER };

typedef struct GameProfile {
    GpGame game;
    char code[4];                  /* header 0xAC game code */
    uint8_t rev;                   /* header 0xBC revision byte (Emerald: any, the row matches by code only) */
    const char *dataSubdir;        /* "" Emerald (unchanged path); "BPRE" / "BPGE" */
    /* ---- ROM layer: romgen and renderer ---- */
    uint32_t mapGroups;            /* gMapGroups for this rev */
    uint8_t groupCount;            /* 34 / 43 */
    const uint8_t *groupSizes;     /* census pin: maps per group; NULL = not pinned (Emerald, R0) */
    uint32_t mapLayouts;           /* the known value, or 0 = runtime search */
    uint16_t layoutSlots;          /* 442 / 384 */
    uint16_t nPrimMetatiles, nPrimTiles;
    uint8_t nPrimPals;
    uint16_t nMetatilesTotal;
    uint8_t tilesetAttrOff, attrBytes;
    uint16_t behMask;              /* behaviour bits of an attribute: 0xFF here (R2 may widen for FRLG: 0x1FF) */
    uint32_t layerMask;
    uint8_t layerShift;
    uint8_t layoutBytes;           /* MapLayout size read and bounds-checked */
    uint32_t tsGeneral, tsBuilding; /* 0 = derive at runtime */
    GpBehSet water, jump, houseDoor, sand, tallGrass, signpost;    /* romgen sets */
    GpBehSet bladeGrass;           /* look L2: behaviours that grow blade cards (tall grass proper: 0x02) */
    GpBehSet surfable, reflective, ice, shallowFlowing, furniture; /* renderer sets */
    uint8_t houseHalfWidth, houseHeight;   /* rg_roles is_house window */
    /* ---- RAM and ROM anchors: renderer only ---- */
    uint32_t gMain, sb1Ptr, backupLayout, backupMap, mapHeader, objEvents, playerAvatar, sprites, plttUnfaded,
        paletteFade;               /* backupMap: fixed EWRAM address (Emerald); 0 = derive from the live pointer (R2) */
    bool sb1Direct;                /* Ruby / Sapphire: sb1Ptr IS the SaveBlock1 struct (EWRAM), there is no pointer to it */
    uint16_t mainFlagsOff;         /* gMain byte whose bit 1 is inBattle: 0x439 Emerald / FRLG, 0x43D Ruby / Sapphire */
    uint8_t playerAvatarBytes;     /* 0x24 / 0x20 */
    uint32_t weather;              /* Emerald: the struct's address directly */
    uint32_t weatherPtr;           /* FRLG: a ROM constant that holds the EWRAM address (R1); 0 on Emerald */
    uint16_t weatherOff[5];        /* curr, palState, eva, fogH, fogD */
    uint32_t gfxInfoPtrs;
    uint16_t gfxInfoCount;         /* 239 / 152 */
    uint32_t fldeffTemplates;
    uint8_t fldeffCount;           /* 37 / 36 */
    uint32_t cb2Overworld, cb2OverworldBasic;   /* thumb bit set */
    /* ---- per-game tables ---- */
    /* Renderer tree tables (Phase 34 T1), read by voxel_tree.c; flat int16 pairs, NULL = no tree sprites.
     * treePart: {metatile id, part}, treePartCount pairs. part 0..3 = quadrant (row * 2 + col) of a 2x2 tree, 4 = a
     * one-cell small tree (VOXEL_TREE_SMALL). treeGround: {metatile id, replacement id}, treeGroundCount pairs: the
     * metatile drawn instead, because its own art carries canopy over some other ground. Ids are < 1024. */
    const int16_t *treePart;
    uint16_t treePartCount;
    const int16_t *treeGround;
    uint16_t treeGroundCount;
    /* Renderer shrub table (look backlog L1), read by voxel_tree.c: the one-cell bushes that stand up as a card of their
     * own upper layer over their lower layer (VOXEL_TREE_SHRUB). Not part of treePart: a tree crown is drawn from the
     * fixed General tree texture by id alone, a shrub from the cell's own atlas art and keyed by tileset too. */
    const GpShrub *shrubs;
    uint8_t shrubCount;
    /* Look L2 (tall grass), read by voxel_tree.c: a metatile whose behaviour is in `bladeGrass` stands up as a card of
     * its own LOWER layer (the blades are drawn there, the upper layer is empty) with the ground colour keyed out;
     * the flat cell keeps its own drawing. `grassGround` is the plain-grass metatile whose colours are that ground,
     * `grassSkip` the measured (tileset, id) exceptions whose ground is some other colour. */
    uint16_t grassGround;
    const GpShrub *grassSkip;
    uint8_t grassSkipCount;
    bool emeraldIdTables;         /* Fortree puddles, GenericBuilding interior ids: Emerald only */
    bool interiors3d;              /* false on FRLG: indoor maps hand back to the 2D frame */
    bool rendererOn;               /* gameprof_detect() returns the row only when set (Emerald; FRLG from R2; RS from S2) */
    const struct RgSpec *specs;    /* building recipe table (romgen); wired in G1, NULL until then */
    unsigned nSpecs;
} GameProfile;

/* Phase 35: the game families. Kanto = FireRed / LeafGreen (their own behaviour numbers and u32 attributes); Ruby /
 * Sapphire = the Emerald ROM layer (u16 attributes, the Emerald behaviour sets) with their own table addresses. Emerald's
 * Zallax-pinned paths (name-order tables, drawn relief, the interior specs) stay Emerald-only: test `game == GP_EMERALD`. */
static inline bool gp_is_kanto(const GameProfile *p)
{
    return p != NULL && (p->game == GP_FIRERED || p->game == GP_LEAFGREEN);
}
static inline bool gp_is_rs(const GameProfile *p)
{
    return p != NULL && (p->game == GP_RUBY || p->game == GP_SAPPHIRE);
}

/* Reads the code (0xAC) and revision (0xBC) from a ROM image. NULL = unsupported (wrong game, wrong rev, short
 * buffer). Rows whose game is GP_NONE are never returned. */
const GameProfile *gameprof_detect(const uint8_t *rom, size_t size);

/* Phase 34 G1: the same detection for romgen (and for vx_host's anchor self-check): it also returns the FireRed and
 * LeafGreen rev 1 rows, and (Phase 35 S0) the Ruby / Sapphire rev 2 rows. gameprof_detect() keeps refusing a row whose
 * `rendererOn` is not set (Ruby / Sapphire until their renderer slice), so the renderer is not enabled by accident. */
const GameProfile *gameprof_detect_romgen(const uint8_t *rom, size_t size);

/* The Emerald row. Its behaviour sets are filled on first use from the existing predicates; the first call
 * must not race with another thread's first call (vx_host and romgen both call it before any worker runs). */
const GameProfile *gameprof_emerald(void);

#endif
