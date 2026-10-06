/* rg_world.c -- the ROM world: layouts, maps, events, connections, tilesets, pairs (3DGBA, GPLv3).
 *
 * Replaces the inputs upstream's voxel_cells.py (Layout / MapEvents / pair_for / alternate_layouts)
 * read from a decomp tree. Behaviour spec: docs/phase33-romgen/SPEC-S0-S1.md section 1. Original
 * to 3DGBA (no upstream code), so no MIT notice. Pure C. */
#include "rg_world.h"

#include <stdlib.h>
#include <string.h>

#include "gba_game.h"
#include "vx_lz77.h"

#define RG_MAX_LAYOUTS 4096u
#define RG_MAX_DIM 1024
#define RG_MAX_TILES_BYTES 65536u
#define RG_BLOCK_BYTES 65536u

struct RgBlock {
    RgBlock *next;
    size_t used, cap;
    uint64_t data[1];
};

/* ---- arena ------------------------------------------------------------------------------- */
static void *rg_alloc(RgWorld *w, size_t n)
{
    RgBlock *b = w->arena;
    size_t cap;
    void *p;

    n = (n + 7u) & ~(size_t)7u;
    if (n == 0)
        n = 8;
    if (b == NULL || b->cap - b->used < n) {
        cap = n > RG_BLOCK_BYTES ? n : RG_BLOCK_BYTES;
        b = (RgBlock *)calloc(1, sizeof(RgBlock) + cap);
        if (b == NULL)
            return NULL;
        b->cap = cap;
        b->next = w->arena;
        w->arena = b;
    }
    p = (uint8_t *)b->data + b->used;
    b->used += n;
    return p;
}

void rg_world_close(RgWorld *w)
{
    RgBlock *b;
    RgBlock *next;

    if (w == NULL)
        return;
    for (b = w->arena; b != NULL; b = next) {
        next = b->next;
        free(b);
    }
    memset(w, 0, sizeof(*w));
}

const char *rg_err_str(RgErr e)
{
    switch (e) {
    case RG_OK: return "ok";
    case RG_ERR_NOT_BPEE: return "not Pokemon Emerald (BPEE) or tables missing";
    case RG_ERR_LAYOUT_ORDER: return "map header layout id disagrees with the layout table";
    case RG_ERR_LAYOUT_TABLE: return "layout table unreadable";
    case RG_ERR_MAP_GROUPS: return "map group table or map events unreadable";
    case RG_ERR_TILESET: return "tileset unreadable";
    case RG_ERR_NOMEM: return "out of memory";
    case RG_ERR_CANCELLED: return "cancelled";
    case RG_ERR_TOO_BIG: return "a buildings.bin field overflows its width";
    case RG_ERR_BUILDINGS: return "building model or atlas not representable";
    case RG_ERR_TABLES: return "relief tables do not describe this ROM";
    case RG_ERR_RELIEF: return "relief.bin field overflow or relief mode not available";
    }
    return "?";
}

/* ---- ROM access -------------------------------------------------------------------------- */
static bool ptr_ok(const RgWorld *w, uint32_t p, size_t len)
{
    size_t off;

    if ((p >> 24) != 0x08u)
        return false;
    off = (size_t)(p - GBA_ROM_BASE);
    return off <= w->romSize && len <= w->romSize - off;
}

static const uint8_t *rom_at(const RgWorld *w, uint32_t p)
{
    return w->rom + (size_t)(p - GBA_ROM_BASE);
}

/* The section 1.2 plausibility test for a MapLayout struct at GBA address p. */
static bool layout_plausible(const RgWorld *w, uint32_t p)
{
    const uint8_t *s;
    int32_t lw, lh;
    uint32_t t;

    if (!ptr_ok(w, p, GBA_ROM_MAPLAYOUT_BYTES))
        return false;
    s = rom_at(w, p);
    lw = (int32_t)rg_rd32(s);
    lh = (int32_t)rg_rd32(s + 4);
    if (lw < 1 || lw > RG_MAX_DIM || lh < 1 || lh > RG_MAX_DIM)
        return false;
    if (!ptr_ok(w, rg_rd32(s + 8), 0) || !ptr_ok(w, rg_rd32(s + 12), 0))
        return false;
    t = rg_rd32(s + 0x10);
    if (t != 0 && !ptr_ok(w, t, 0))
        return false;
    t = rg_rd32(s + 0x14);
    if (t != 0 && !ptr_ok(w, t, 0))
        return false;
    return true;
}

/* ---- tilesets ---------------------------------------------------------------------------- */
static RgErr read_tileset(RgWorld *w, uint32_t addr, RgTileset *t)
{
    const uint8_t *s;
    uint32_t tiles, pals, meta, attr;
    size_t dec;
    size_t avail;
    uint32_t count;

    if (!ptr_ok(w, addr, GBA_ROM_TILESET_BYTES))
        return RG_ERR_TILESET;
    s = rom_at(w, addr);
    t->addr = addr;
    t->compressed = s[0] != 0;
    t->secondary = s[1] != 0;
    tiles = rg_rd32(s + 4);
    pals = rg_rd32(s + 8);
    meta = rg_rd32(s + 0xC);
    attr = rg_rd32(s + 0x10);
    if (!ptr_ok(w, tiles, 0) || !ptr_ok(w, pals, 512) || !ptr_ok(w, meta, 0) || !ptr_ok(w, attr, 0))
        return RG_ERR_TILESET;
    t->tilesRom = rom_at(w, tiles);
    avail = w->romSize - (size_t)(tiles - GBA_ROM_BASE);
    if (t->compressed) {
        dec = 0;
        if (vx_lz77_scan(t->tilesRom, avail, &dec) == 0 || dec == 0 || dec > RG_MAX_TILES_BYTES)
            return RG_ERR_TILESET;
        t->tilesBytes = (uint32_t)dec;
    } else {
        t->tilesBytes = (uint32_t)(avail < 512u * 32u ? avail : 512u * 32u);
    }
    t->palettes = rom_at(w, pals);
    t->metatiles = rom_at(w, meta);
    t->attrs = rom_at(w, attr);
    count = 512;
    if (attr > meta && ((attr - meta) % 16u) == 0 && ((attr - meta) / 16u) <= 512u)
        count = (attr - meta) / 16u;
    if (!ptr_ok(w, meta, (size_t)count * 16u) || !ptr_ok(w, attr, (size_t)count * 2u))
        return RG_ERR_TILESET;
    t->metatileCount = (uint16_t)count;
    return RG_OK;
}

static RgErr intern_tileset(RgWorld *w, uint32_t addr, uint16_t *index)
{
    unsigned i;
    RgErr e;

    if (addr == 0) {
        *index = 0;
        return RG_OK;
    }
    for (i = 1; i < w->tilesetCount; i++) {
        if (w->tilesets[i].addr == addr) {
            *index = (uint16_t)i;
            return RG_OK;
        }
    }
    e = read_tileset(w, addr, &w->tilesets[w->tilesetCount]);
    if (e != RG_OK)
        return e;
    *index = w->tilesetCount++;
    return RG_OK;
}

/* ---- layouts ----------------------------------------------------------------------------- */
static RgErr read_layouts(RgWorld *w, uint16_t (*tsIdx)[2])
{
    uint32_t tbl = GBA_ADDR_MAP_LAYOUTS;
    unsigned n = 0, i;
    uint32_t e;

    while (n < RG_MAX_LAYOUTS && ptr_ok(w, tbl + 4u * n, 4)) {
        e = rg_rd32(rom_at(w, tbl + 4u * n));
        if (e != 0 && !layout_plausible(w, e))
            break;
        n++;
    }
    while (n > 0 && rg_rd32(rom_at(w, tbl + 4u * (n - 1))) == 0)
        n--;   /* trailing NULL entries are not layouts */
    if (n == 0)
        return RG_ERR_LAYOUT_TABLE;
    w->layoutCount = (uint16_t)n;
    w->layouts = (RgLayout *)rg_alloc(w, n * sizeof(RgLayout));
    w->tilesets = (RgTileset *)rg_alloc(w, (2u * n + 1u) * sizeof(RgTileset));
    w->pairs = rg_alloc(w, (size_t)n * sizeof(w->pairs[0]));
    if (w->layouts == NULL || w->tilesets == NULL || w->pairs == NULL)
        return RG_ERR_NOMEM;
    w->tilesetCount = 1;   /* index 0 = the NULL tileset (all zero) */
    for (i = 0; i < n; i++) {
        RgLayout *L = &w->layouts[i];
        const uint8_t *s;
        RgErr err;
        size_t cells;

        L->id = (uint16_t)(i + 1);
        L->ts[0] = L->ts[1] = &w->tilesets[0];
        e = rg_rd32(rom_at(w, tbl + 4u * i));
        if (e == 0)
            continue;
        s = rom_at(w, e);
        L->w = (uint16_t)rg_rd32(s);
        L->h = (uint16_t)rg_rd32(s + 4);
        cells = (size_t)L->w * L->h;
        if (!ptr_ok(w, rg_rd32(s + 12), cells * 2u))
            continue;   /* short blockdata: skipped, as upstream skips a missing one */
        L->blocks = rom_at(w, rg_rd32(s + 12));
        err = intern_tileset(w, rg_rd32(s + 0x10), &tsIdx[i][0]);
        if (err == RG_OK)
            err = intern_tileset(w, rg_rd32(s + 0x14), &tsIdx[i][1]);
        if (err != RG_OK)
            return err;
        L->ts[0] = &w->tilesets[tsIdx[i][0]];
        L->ts[1] = &w->tilesets[tsIdx[i][1]];
        L->present = 1;
    }
    return RG_OK;
}

/* ---- map groups, events ------------------------------------------------------------------ */
static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : (x > y ? 1 : 0);
}

typedef struct { uint64_t *v; size_t n, cap; } KeyVec;

static bool kv_push(KeyVec *k, uint64_t v)
{
    if (k->n == k->cap) {
        size_t nc = k->cap ? k->cap * 2 : 1024;
        uint64_t *nv = (uint64_t *)realloc(k->v, nc * sizeof(uint64_t));
        if (nv == NULL)
            return false;
        k->v = nv;
        k->cap = nc;
    }
    k->v[k->n++] = v;
    return true;
}

static uint64_t cell_key(unsigned layoutIdx, int x, int y)
{
    return ((uint64_t)layoutIdx << 32) | ((uint64_t)(uint32_t)(y + 32768) << 16)
         | (uint64_t)(uint32_t)(x + 32768);
}

static RgErr read_events(RgWorld *w, const RgMap *m, KeyVec *warps, KeyVec *signs)
{
    const uint8_t *h = rom_at(w, m->addr);
    uint32_t ev = rg_rd32(h + GBA_OFF_MH_EVENTS);
    const uint8_t *s;
    unsigned wc, bc, i;
    uint32_t wp, bp;

    if (ev == 0)
        return RG_OK;
    if (!ptr_ok(w, ev, 0x14))
        return RG_ERR_MAP_GROUPS;
    s = rom_at(w, ev);
    wc = s[1];
    bc = s[3];
    wp = rg_rd32(s + 8);
    bp = rg_rd32(s + 0x10);
    if ((wc && !ptr_ok(w, wp, (size_t)wc * 8u)) || (bc && !ptr_ok(w, bp, (size_t)bc * GBA_ROM_BGEVENT_STRIDE)))
        return RG_ERR_MAP_GROUPS;
    for (i = 0; i < wc; i++) {
        const uint8_t *e = rom_at(w, wp) + 8u * i;
        w->warpEvents++;
        if (!kv_push(warps, cell_key(m->layoutId - 1u, (int16_t)rg_rd16(e), (int16_t)rg_rd16(e + 2))))
            return RG_ERR_NOMEM;
    }
    for (i = 0; i < bc; i++) {
        const uint8_t *e = rom_at(w, bp) + (size_t)GBA_ROM_BGEVENT_STRIDE * i;
        if (e[5] > 4)
            continue;   /* hidden items and secret bases are not signs */
        w->signEvents++;
        if (!kv_push(signs, cell_key(m->layoutId - 1u, (int16_t)rg_rd16(e), (int16_t)rg_rd16(e + 2))))
            return RG_ERR_NOMEM;
    }
    return RG_OK;
}

static RgErr read_maps(RgWorld *w, KeyVec *warps, KeyVec *signs)
{
    uint32_t start[GBA_MAP_GROUP_COUNT];
    unsigned g, g2, total = 0, k;
    uint32_t next;
    const uint8_t *tbl;

    if (!ptr_ok(w, GBA_ADDR_MAP_GROUPS, 4u * GBA_MAP_GROUP_COUNT))
        return RG_ERR_MAP_GROUPS;
    tbl = rom_at(w, GBA_ADDR_MAP_GROUPS);
    for (g = 0; g < GBA_MAP_GROUP_COUNT; g++) {
        start[g] = rg_rd32(tbl + 4u * g);
        if (!ptr_ok(w, start[g], 0) || start[g] >= GBA_ADDR_MAP_GROUPS || (start[g] & 3u))
            return RG_ERR_MAP_GROUPS;
    }
    for (g = 0; g < GBA_MAP_GROUP_COUNT; g++) {
        bool dup = false;
        next = GBA_ADDR_MAP_GROUPS;
        for (g2 = 0; g2 < GBA_MAP_GROUP_COUNT; g2++) {
            if (g2 < g && start[g2] == start[g])
                dup = true;
            if (start[g2] > start[g] && start[g2] < next)
                next = start[g2];
        }
        w->groupCount[g] = dup ? 0 : (uint16_t)((next - start[g]) / 4u);
        w->groupStart[g] = (uint16_t)total;
        total += w->groupCount[g];
    }
    w->mapCount = (uint16_t)total;
    w->maps = (RgMap *)rg_alloc(w, (size_t)total * sizeof(RgMap));
    if (w->maps == NULL)
        return RG_ERR_NOMEM;
    for (g = 0, k = 0; g < GBA_MAP_GROUP_COUNT; g++) {
        unsigned i;
        for (i = 0; i < w->groupCount[g]; i++, k++) {
            uint32_t hp = rg_rd32(rom_at(w, start[g] + 4u * i));
            const uint8_t *h;
            RgMap *m = &w->maps[k];
            uint16_t lid;
            RgErr e;

            if (!ptr_ok(w, hp, GBA_MAP_HEADER_BYTES) || !layout_plausible(w, rg_rd32(rom_at(w, hp))))
                return RG_ERR_MAP_GROUPS;
            h = rom_at(w, hp);
            lid = rg_rd16(h + GBA_OFF_MH_LAYOUT_ID);
            if (lid < 1 || lid > w->layoutCount
                || rg_rd32(rom_at(w, GBA_ADDR_MAP_LAYOUTS + 4u * (lid - 1u))) != rg_rd32(h))
                return RG_ERR_LAYOUT_ORDER;
            m->group = (uint8_t)g;
            m->num = (uint8_t)i;
            m->mapType = h[GBA_OFF_MH_MAPTYPE];
            m->layoutId = lid;
            m->addr = hp;
            w->layouts[lid - 1].used = 1;
            if (m->mapType == MAP_TYPE_ROUTE || m->mapType == MAP_TYPE_TOWN || m->mapType == MAP_TYPE_UNDERWATER
                || m->mapType == MAP_TYPE_CITY || m->mapType == MAP_TYPE_OCEAN_ROUTE) {
                w->layouts[lid - 1].outdoor = 1;
                w->outdoorMaps++;
            }
            e = read_events(w, m, warps, signs);
            if (e != RG_OK)
                return e;
        }
    }
    return RG_OK;
}

/* Sorts + dedupes a key vector and hands each layout its slice (arena copy). */
static RgErr assign_cells(RgWorld *w, KeyVec *kv, bool isSign)
{
    size_t i = 0, j;

    if (kv->n > 1)
        qsort(kv->v, kv->n, sizeof(uint64_t), cmp_u64);
    while (i < kv->n) {
        unsigned li = (unsigned)(kv->v[i] >> 32);
        size_t n = 0;
        RgCell *cells;
        uint64_t prev = ~(uint64_t)0;

        for (j = i; j < kv->n && (unsigned)(kv->v[j] >> 32) == li; j++) {
            if (kv->v[j] != prev)
                n++;
            prev = kv->v[j];
        }
        cells = (RgCell *)rg_alloc(w, n * sizeof(RgCell));
        if (cells == NULL)
            return RG_ERR_NOMEM;
        n = 0;
        prev = ~(uint64_t)0;
        for (j = i; j < kv->n && (unsigned)(kv->v[j] >> 32) == li; j++) {
            if (kv->v[j] != prev) {
                cells[n].x = (int16_t)((int)(kv->v[j] & 0xFFFFu) - 32768);
                cells[n].y = (int16_t)((int)((kv->v[j] >> 16) & 0xFFFFu) - 32768);
                n++;
            }
            prev = kv->v[j];
        }
        if (isSign) {
            w->layouts[li].signs = cells;
            w->layouts[li].signCount = (uint16_t)n;
        } else {
            w->layouts[li].warps = cells;
            w->layouts[li].warpCount = (uint16_t)n;
        }
        i = j;
    }
    return RG_OK;
}

/* ---- alternates (section 1.5, structural rule) ------------------------------------------- */
static unsigned equal_words(const RgLayout *a, const RgLayout *b)
{
    size_t n = (size_t)a->w * a->h, i;
    unsigned eq = 0;

    for (i = 0; i < n; i++)
        eq += rg_rd16(a->blocks + 2 * i) == rg_rd16(b->blocks + 2 * i);
    return eq;
}

static void find_alternates(RgWorld *w)
{
    unsigned u, b;

    for (u = 0; u < w->layoutCount; u++) {
        RgLayout *U = &w->layouts[u];
        unsigned best = 0, bestId = 0;

        if (!U->present || U->used)
            continue;
        for (b = 0; b < w->layoutCount; b++) {
            const RgLayout *B = &w->layouts[b];
            unsigned eq;

            if (!B->present || !B->used || B->w != U->w || B->h != U->h
                || B->ts[0]->addr != U->ts[0]->addr || B->ts[1]->addr != U->ts[1]->addr)
                continue;
            eq = equal_words(U, B);
            if (2u * eq >= (unsigned)U->w * U->h && eq > best) {
                best = eq;
                bestId = B->id;
            }
        }
        if (bestId != 0) {
            const RgLayout *B = &w->layouts[bestId - 1];
            U->altOf = (uint16_t)bestId;
            if (B->outdoor)
                U->outdoor = 1;
            if (B->warpCount) {
                U->warps = B->warps;
                U->warpCount = B->warpCount;
            }
            if (B->signCount) {
                U->signs = B->signs;
                U->signCount = B->signCount;
            }
        }
    }
}

static RgErr build_pairs(RgWorld *w, uint16_t (*tsIdx)[2])
{
    unsigned i, p;

    for (i = 0; i < w->layoutCount; i++) {
        if (!w->layouts[i].present)
            continue;
        for (p = 0; p < w->pairCount; p++)
            if (w->pairs[p].ts[0] == tsIdx[i][0] && w->pairs[p].ts[1] == tsIdx[i][1])
                break;
        if (p == w->pairCount) {
            w->pairs[p].ts[0] = tsIdx[i][0];
            w->pairs[p].ts[1] = tsIdx[i][1];
            w->pairCount++;
        }
        w->layouts[i].pairIndex = (uint16_t)p;
    }
    return RG_OK;
}

RgErr rg_world_open(RgWorld *w, const uint8_t *rom, size_t romSize)
{
    KeyVec warps = {0}, signs = {0};
    uint16_t (*tsIdx)[2] = NULL;
    RgErr e;

    if (w == NULL || rom == NULL)
        return RG_ERR_NOT_BPEE;
    memset(w, 0, sizeof(*w));
    w->rom = rom;
    w->romSize = romSize;
    if (romSize < 0xB0u || memcmp(rom + 0xAC, "BPEE", 4) != 0
        || !ptr_ok(w, GBA_ADDR_MAP_LAYOUTS, 4) || !ptr_ok(w, GBA_ADDR_MAP_GROUPS, 4u * GBA_MAP_GROUP_COUNT))
        return RG_ERR_NOT_BPEE;
    tsIdx = (uint16_t (*)[2])calloc(RG_MAX_LAYOUTS, sizeof(*tsIdx));
    if (tsIdx == NULL)
        return RG_ERR_NOMEM;
    e = read_layouts(w, tsIdx);
    if (e == RG_OK)
        e = read_maps(w, &warps, &signs);
    if (e == RG_OK)
        e = assign_cells(w, &warps, false);
    if (e == RG_OK)
        e = assign_cells(w, &signs, true);
    if (e == RG_OK) {
        find_alternates(w);
        e = build_pairs(w, tsIdx);
    }
    free(tsIdx);
    free(warps.v);
    free(signs.v);
    if (e != RG_OK)
        rg_world_close(w);
    return e;
}

/* ---- queries ----------------------------------------------------------------------------- */
const RgMap *rg_world_map(const RgWorld *w, unsigned group, unsigned num)
{
    if (w == NULL || group >= GBA_MAP_GROUP_COUNT || num >= w->groupCount[group])
        return NULL;
    return &w->maps[w->groupStart[group] + num];
}

unsigned rg_map_connections(const RgWorld *w, unsigned group, unsigned num, RgConn *out, unsigned max)
{
    const RgMap *m = rg_world_map(w, group, num);
    uint32_t cp, lp;
    const uint8_t *c, *l;
    uint32_t count, i;
    unsigned found = 0;

    if (m == NULL)
        return 0;
    cp = rg_rd32(rom_at(w, m->addr) + GBA_OFF_MH_CONNECTIONS);
    if (cp == 0 || !ptr_ok(w, cp, 8))
        return 0;
    c = rom_at(w, cp);
    count = rg_rd32(c);
    lp = rg_rd32(c + 4);
    if (count == 0 || count > 64 || !ptr_ok(w, lp, (size_t)count * GBA_ROM_CONNECTION_STRIDE))
        return 0;
    l = rom_at(w, lp);
    for (i = 0; i < count; i++) {
        const uint8_t *e = l + (size_t)GBA_ROM_CONNECTION_STRIDE * i;
        if (e[0] < CONNECTION_SOUTH || e[0] > CONNECTION_EAST)
            continue;   /* dive / emerge */
        if (out != NULL && found < max) {
            out[found].dir = e[0];
            out[found].offset = (int32_t)rg_rd32(e + 4);
            out[found].group = e[8];
            out[found].num = e[9];
        }
        found++;
    }
    return found;
}

uint16_t rg_metatile(const RgLayout *L, int x, int y)
{
    if (rg_off(L, x, y) || L->blocks == NULL)
        return RG_NONE;
    return rg_rd16(L->blocks + 2u * ((unsigned)y * L->w + (unsigned)x)) & MAPGRID_METATILE_ID_MASK;
}

bool rg_blocked(const RgLayout *L, int x, int y)
{
    if (rg_off(L, x, y) || L->blocks == NULL)
        return false;
    return ((rg_rd16(L->blocks + 2u * ((unsigned)y * L->w + (unsigned)x)) >> MAPGRID_COLLISION_SHIFT) & 3u) != 0;
}

uint8_t rg_elev(const RgLayout *L, int x, int y)
{
    if (rg_off(L, x, y) || L->blocks == NULL)
        return 0;
    return (uint8_t)((rg_rd16(L->blocks + 2u * ((unsigned)y * L->w + (unsigned)x)) >> MAPGRID_ELEVATION_SHIFT) & 0xFu);
}

uint16_t rg_attr(const RgLayout *L, uint16_t metatile)
{
    const RgTileset *t;
    unsigned which, idx;

    if (metatile == RG_NONE)
        return 0;
    which = metatile < RG_NUM_PRIMARY ? 0u : 1u;
    t = L->ts[which];
    idx = metatile - which * RG_NUM_PRIMARY;
    if (t->attrs == NULL || idx >= t->metatileCount)
        return 0;
    return rg_rd16(t->attrs + 2u * idx);
}

uint8_t rg_behaviour(const RgLayout *L, int x, int y)
{
    return (uint8_t)(rg_attr(L, rg_metatile(L, x, y)) & 0xFFu);
}

bool rg_touches_walkable(const RgLayout *L, int x, int y)
{
    static const int8_t nb[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
    unsigned i;

    for (i = 0; i < 4; i++) {
        int nx = x + nb[i][0], ny = y + nb[i][1];
        if (!rg_off(L, nx, ny) && !rg_blocked(L, nx, ny))
            return true;
    }
    return false;
}

static bool has_cell(const RgCell *c, unsigned n, int x, int y)
{
    unsigned lo = 0, hi = n;

    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        int cy = c[mid].y, cx = c[mid].x;
        if (cy == y && cx == x)
            return true;
        if (cy < y || (cy == y && cx < x))
            lo = mid + 1;
        else
            hi = mid;
    }
    return false;
}

bool rg_has_warp(const RgLayout *L, int x, int y)
{
    return has_cell(L->warps, L->warpCount, x, y);
}

bool rg_has_sign(const RgLayout *L, int x, int y)
{
    return has_cell(L->signs, L->signCount, x, y);
}

uint32_t rg_tileset_addr_of(const RgLayout *L, uint16_t metatile)
{
    return metatile < RG_NUM_PRIMARY ? L->ts[0]->addr : L->ts[1]->addr;
}

bool rg_metatile_entries(const RgLayout *L, uint16_t metatile, uint16_t out[8])
{
    unsigned which = metatile < RG_NUM_PRIMARY ? 0u : 1u;
    const RgTileset *t;
    unsigned index, k;

    if (L == NULL || out == NULL || metatile == RG_NONE)
        return false;
    t = L->ts[which];
    index = metatile - which * RG_NUM_PRIMARY;
    if (t == NULL || t->metatiles == NULL || index >= t->metatileCount)
        return false;
    for (k = 0; k < 8; k++)
        out[k] = rg_rd16(t->metatiles + 16u * index + 2u * k);
    return true;
}
