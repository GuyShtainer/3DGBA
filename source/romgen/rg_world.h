/* rg_world.h -- the ROM world romgen generators read (3DGBA, GPLv3). Pure C.
 *
 * Replaces the upstream Layout / MapEvents / pair_for inputs, read from the ROM image instead of a
 * decomp tree. Spec: docs/phase33-romgen/SPEC-S0-S1.md section 1. Emerald, plus (Phase 34 G1) FireRed and
 * LeafGreen rev 1 through the game profile (rg_gameprof.h): every ROM-layout constant is read from RgWorld.prof. */
#ifndef RG_WORLD_H
#define RG_WORLD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rg_gameprof.h"

#define RG_NONE 0xFFFFu
/* Emerald only (the relief S3 modules): generic code reads prof->nPrimMetatiles instead. */
#define RG_NUM_PRIMARY 512u
#define RG_MAX_GROUPS 64u

typedef enum {
    RG_OK = 0,
    RG_ERR_GAME,                 /* not a supported cartridge (Emerald, FireRed rev 1, LeafGreen rev 1), or its tables are missing */
    RG_ERR_NOT_BPEE = RG_ERR_GAME,   /* the pre-Phase-34 name: same value, so the CLI exit codes do not move */
    RG_ERR_LAYOUT_ORDER,
    RG_ERR_LAYOUT_TABLE,
    RG_ERR_MAP_GROUPS,
    RG_ERR_TILESET,
    RG_ERR_NOMEM,
    RG_ERR_CANCELLED,
    RG_ERR_TOO_BIG,              /* a buildings.bin field would overflow its width */
    RG_ERR_BUILDINGS,            /* a model or atlas the format cannot hold */
    RG_ERR_TABLES,               /* the relief number tables do not describe this ROM (SPEC-S3 3.3) */
    RG_ERR_RELIEF                /* a relief.bin field would overflow its width, or the mode is not built yet */
} RgErr;

/* Explicit little-endian reads: no casts, so host and device agree. */
static inline uint16_t rg_rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}
static inline uint32_t rg_rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Progress callback shared by the generators: done of total units (layouts). */
typedef void (*RgProgressFn)(void *ctx, unsigned done, unsigned total);

typedef struct { int16_t x, y; } RgCell;

typedef struct RgTileset {
    uint32_t addr;               /* GBA address; 0 = the NULL tileset */
    bool compressed, secondary;
    const uint8_t *tilesRom;     /* raw ROM pointer (LZ77 stream or 4bpp) */
    uint32_t tilesBytes;         /* decoded length (exact if compressed) */
    const uint8_t *palettes;     /* 512 B, read with rg_rd16 */
    const uint8_t *metatiles;    /* 16 B per metatile */
    const uint8_t *attrs;        /* profile attrBytes (2 on Emerald, 4 on FRLG) per metatile */
    uint16_t metatileCount;
} RgTileset;

typedef struct RgLayout {
    uint16_t id;                 /* 1-based gMapLayouts position */
    uint16_t w, h;
    const uint8_t *blocks;       /* w*h u16, ROM (NULL when not present) */
    const RgTileset *ts[2];      /* primary, secondary (never NULL; addr 0 = empty) */
    uint16_t pairIndex;
    uint8_t present;             /* entry non-NULL and blockdata in ROM */
    uint8_t used;                /* referenced by >= 1 map header */
    uint8_t outdoor;             /* after alternate inheritance */
    uint16_t altOf;              /* base layout id this one inherits from, 0 = none */
    const RgCell *warps; uint16_t warpCount;   /* union, sorted (y, x), unique */
    const RgCell *signs; uint16_t signCount;   /* union, sorted (y, x), unique */
    const GameProfile *prof;     /* set by rg_world_open; NULL (hand-built layouts) means the Emerald row */
} RgLayout;

static inline const GameProfile *rg_lprof(const RgLayout *L)
{
    return L->prof != NULL ? L->prof : gameprof_emerald();
}

typedef struct RgMap {
    uint8_t group, num, mapType;
    uint16_t layoutId;
    uint32_t addr;               /* GBA address of the MapHeader */
} RgMap;

/* A map connection (MapHeader+0xC -> {s32 count; ptr}; 12-byte entries). dir: 1 down, 2 up,
 * 3 left, 4 right (dive 5 / emerge 6 are filtered out by rg_map_connections). */
typedef struct RgConn {
    uint8_t dir;
    int32_t offset;
    uint8_t group, num;
} RgConn;

typedef struct RgBlock RgBlock;

typedef struct RgWorld {
    const uint8_t *rom;
    size_t romSize;
    const GameProfile *prof;     /* the detected game (rg_world_open) */
    uint32_t layoutTable;        /* GBA address of gMapLayouts as used */
    uint16_t layoutCount;        /* 442 on BPEE, 384 on FRLG */
    RgLayout *layouts;           /* [id-1] */
    uint16_t tilesetCount;
    RgTileset *tilesets;         /* index 0 = the NULL tileset */
    uint16_t pairCount;
    struct { uint16_t ts[2]; } *pairs;
    uint16_t mapCount;           /* 518 on BPEE, 425 on FRLG */
    RgMap *maps;                 /* group-major order */
    uint16_t groupStart[RG_MAX_GROUPS], groupCount[RG_MAX_GROUPS];   /* [prof->groupCount] used */
    uint32_t outdoorMaps;
    uint32_t warpEvents, signEvents;   /* raw event totals over all maps */
    RgBlock *arena;              /* every allocation above lives here */
} RgWorld;

RgErr rg_world_open(RgWorld *w, const uint8_t *rom, size_t romSize);
/* SPEC-P34 1.3: finds gMapLayouts by searching the ROM for the layout pointer of header (3, 0) (aligned, table + 4*(id-1)),
 * accepting the first hit that agrees with EVERY map header (rd32(table + 4*(layoutId-1)) == header.layout). 0 = not found.
 * Needs w->prof and w->rom only (so it can run on an opened world). */
uint32_t rg_find_map_layouts(const RgWorld *w);
void  rg_world_close(RgWorld *w);
const char *rg_err_str(RgErr e);

const RgMap *rg_world_map(const RgWorld *w, unsigned group, unsigned num);
/* Writes up to max connections of dir 1..4 into out (out may be NULL with max 0); returns how many
 * exist (so a result > max means truncated). Unknown map or no connections: 0. */
unsigned rg_map_connections(const RgWorld *w, unsigned group, unsigned num, RgConn *out, unsigned max);
/* The same including dive (5) and emerge (6) entries, ROM order (S3.2: the T7 census). */
unsigned rg_map_connections_all(const RgWorld *w, unsigned group, unsigned num, RgConn *out, unsigned max);

/* Cell queries (cells:177-202). Off-map: metatile RG_NONE, blocked false, elevation 0. */
static inline bool rg_off(const RgLayout *L, int x, int y)
{
    return x < 0 || y < 0 || x >= (int)L->w || y >= (int)L->h;
}
uint16_t rg_metatile(const RgLayout *L, int x, int y);
bool     rg_blocked(const RgLayout *L, int x, int y);
uint8_t  rg_elev(const RgLayout *L, int x, int y);
uint32_t rg_attr(const RgLayout *L, uint16_t metatile);   /* u16 on Emerald, u32 on FRLG; 0 past metatileCount / RG_NONE */
uint16_t rg_behaviour(const RgLayout *L, int x, int y);   /* attribute & prof->behMask (0xFF Emerald, 0x1FF FRLG) */
bool     rg_touches_walkable(const RgLayout *L, int x, int y);
bool     rg_has_warp(const RgLayout *L, int x, int y);
bool     rg_has_sign(const RgLayout *L, int x, int y);
/* G3: the 8 raw u16 entries of a metatile (lower layer 0..3, upper 4..7), from the primary tileset for
 * ids < prof->nPrimMetatiles (512 / 640) else the secondary. False when out of range (upstream `entries` returning None, props:80). */
bool rg_metatile_entries(const RgLayout *L, uint16_t metatile, uint16_t out[8]);
/* The tileset address a metatile id is drawn from (cells post_key). */
uint32_t rg_tileset_addr_of(const RgLayout *L, uint16_t metatile);

#endif
