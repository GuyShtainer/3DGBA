/* rg_world.h -- the ROM world romgen generators read (3DGBA, GPLv3). Pure C.
 *
 * Replaces the upstream Layout / MapEvents / pair_for inputs, read from the ROM image instead of a
 * decomp tree. Spec: docs/phase33-romgen/SPEC-S0-S1.md section 1. BPEE only. */
#ifndef RG_WORLD_H
#define RG_WORLD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RG_NONE 0xFFFFu
#define RG_NUM_PRIMARY 512u

typedef enum {
    RG_OK = 0,
    RG_ERR_NOT_BPEE,
    RG_ERR_LAYOUT_ORDER,
    RG_ERR_LAYOUT_TABLE,
    RG_ERR_MAP_GROUPS,
    RG_ERR_TILESET,
    RG_ERR_NOMEM
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
    const uint8_t *attrs;        /* 2 B per metatile */
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
} RgLayout;

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
    uint16_t layoutCount;        /* 442 on BPEE */
    RgLayout *layouts;           /* [id-1] */
    uint16_t tilesetCount;
    RgTileset *tilesets;         /* index 0 = the NULL tileset */
    uint16_t pairCount;
    struct { uint16_t ts[2]; } *pairs;
    uint16_t mapCount;           /* 518 on BPEE */
    RgMap *maps;                 /* group-major order */
    uint16_t groupStart[34], groupCount[34];
    uint32_t outdoorMaps;
    uint32_t warpEvents, signEvents;   /* raw event totals over all maps */
    RgBlock *arena;              /* every allocation above lives here */
} RgWorld;

RgErr rg_world_open(RgWorld *w, const uint8_t *rom, size_t romSize);
void  rg_world_close(RgWorld *w);
const char *rg_err_str(RgErr e);

const RgMap *rg_world_map(const RgWorld *w, unsigned group, unsigned num);
/* Writes up to max connections of dir 1..4 into out (out may be NULL with max 0); returns how many
 * exist (so a result > max means truncated). Unknown map or no connections: 0. */
unsigned rg_map_connections(const RgWorld *w, unsigned group, unsigned num, RgConn *out, unsigned max);

/* Cell queries (cells:177-202). Off-map: metatile RG_NONE, blocked false, elevation 0. */
static inline bool rg_off(const RgLayout *L, int x, int y)
{
    return x < 0 || y < 0 || x >= (int)L->w || y >= (int)L->h;
}
uint16_t rg_metatile(const RgLayout *L, int x, int y);
bool     rg_blocked(const RgLayout *L, int x, int y);
uint8_t  rg_elev(const RgLayout *L, int x, int y);
uint16_t rg_attr(const RgLayout *L, uint16_t metatile);   /* 0 past metatileCount / RG_NONE */
uint8_t  rg_behaviour(const RgLayout *L, int x, int y);
bool     rg_touches_walkable(const RgLayout *L, int x, int y);
bool     rg_has_warp(const RgLayout *L, int x, int y);
bool     rg_has_sign(const RgLayout *L, int x, int y);
/* The tileset address a metatile id is drawn from (cells post_key). */
uint32_t rg_tileset_addr_of(const RgLayout *L, uint16_t metatile);

#endif
