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

typedef enum { GP_NONE = 0, GP_EMERALD, GP_FIRERED, GP_LEAFGREEN } GpGame;

/* 512 bits: metatile behaviour values 0..0x1FF. */
typedef struct GpBehSet { uint32_t w[16]; } GpBehSet;

static inline bool gp_beh(const GpBehSet *s, unsigned b)
{
    return b < 512u && ((s->w[b >> 5] >> (b & 31u)) & 1u) != 0u;
}

struct RgSpec;

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
    GpBehSet surfable, reflective, ice, shallowFlowing, furniture; /* renderer sets */
    uint8_t houseHalfWidth, houseHeight;   /* rg_roles is_house window */
    /* ---- RAM and ROM anchors: renderer only ---- */
    uint32_t gMain, sb1Ptr, backupLayout, backupMap, mapHeader, objEvents, playerAvatar, sprites, plttUnfaded,
        paletteFade;               /* backupMap: fixed EWRAM address (Emerald); 0 = derive from the live pointer (R2) */
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
    const int16_t *treePart;
    uint16_t treePartCount;        /* renderer tree table (T1); NULL = none */
    const int16_t *treeGround;
    bool emeraldIdTables;          /* Fortree puddles, GenericBuilding interior ids: Emerald only */
    bool interiors3d;              /* false on FRLG: indoor maps hand back to the 2D frame */
    bool rendererOn;               /* gameprof_detect() returns the row only when set (Emerald; FRLG from R2) */
    const struct RgSpec *specs;    /* building recipe table (romgen); wired in G1, NULL until then */
    unsigned nSpecs;
} GameProfile;

/* Reads the code (0xAC) and revision (0xBC) from a ROM image. NULL = unsupported (wrong game, wrong rev, short
 * buffer). Rows whose game is GP_NONE are never returned. */
const GameProfile *gameprof_detect(const uint8_t *rom, size_t size);

/* Phase 34 G1: the same detection for romgen (and for vx_host's anchor self-check): it also returns the FireRed and
 * LeafGreen rev 1 rows. gameprof_detect() keeps refusing a non-Emerald row until R2 sets its `rendererOn`, so the
 * renderer is not enabled by accident. */
const GameProfile *gameprof_detect_romgen(const uint8_t *rom, size_t size);

/* The Emerald row. Its behaviour sets are filled on first use from the existing predicates; the first call
 * must not race with another thread's first call (vx_host and romgen both call it before any worker runs). */
const GameProfile *gameprof_emerald(void);

#endif
