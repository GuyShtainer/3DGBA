/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/gen_voxel_relief.py (drawn_canvas,
 * drawn_role, ROLE_REFERENCE, rocky water, the majority vote: rel:1089-1325), MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
/* rg_rcanvas.c -- a drawn group's per-pixel canvas (3DGBA, GPLv3). Pure C. */
#include "rg_rcanvas.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "rg_behavior.h"
#include "rg_bexpand.h"
#include "rg_bimg.h"
#include "rg_ledge.h"
#include "rg_pyset.h"
#include "rg_ralias.h"
#include "rg_rrock.h"
#include "rg_rtables.h"
#include "voxel_regions.h"

#define MAJORITY 2                 /* half-width of the vote window (rel:594) */
#define ROLE_MATCH 0.9             /* share of the rock pixels of both that agree (rel:1272) */
#define ROCKY_WATER_PX 128         /* ROCKY_WATER 0.5 of 256 pixels (rel:609): sum >= 0.5 * 256 */
#define MAX_REFS 32u
#define NO_COLOUR (-1)

/* ---- rock colour classes (rel:559-563) ---- */

enum { C_TOP = 1, C_FACE = 2, C_FLECK = 4, C_RIM = 8 };

static unsigned rock_class(uint8_t r, uint8_t g, uint8_t b)
{
    unsigned i, cls = 0;

    for (i = 0; i < 2; i++)
        if (RG_ROCK_TOP[i][0] == r && RG_ROCK_TOP[i][1] == g && RG_ROCK_TOP[i][2] == b)
            cls |= C_TOP;
    for (i = 0; i < 3; i++)
        if (RG_ROCK_FACE[i][0] == r && RG_ROCK_FACE[i][1] == g && RG_ROCK_FACE[i][2] == b)
            cls |= C_FACE;
    if (RG_ROCK_FLECK[0][0] == r && RG_ROCK_FLECK[0][1] == g && RG_ROCK_FLECK[0][2] == b)
        cls |= C_FLECK;
    if (RG_ROCK_RIM[0][0] == r && RG_ROCK_RIM[0][1] == g && RG_ROCK_RIM[0][2] == b)
        cls |= C_RIM;
    return cls;
}

/* rel:1253-1259: TOP, then FACE, FLECK, RIM, else GROUND. */
static uint8_t pixel_kind(const uint8_t *px)
{
    unsigned cls = rock_class(px[0], px[1], px[2]);

    if (cls & C_TOP) return RG_K_TOP;
    if (cls & C_FACE) return RG_K_FACE;
    if (cls & C_FLECK) return RG_K_FLECK;
    if (cls & C_RIM) return RG_K_RIM;
    return RG_K_GROUND;
}

/* ---- small caches ---- */

typedef struct CacheEnt { uint32_t a, b, m; uint8_t val, used; } CacheEnt;
typedef struct Cache { CacheEnt *e; size_t cap, count; } Cache;

static size_t cache_slot(const Cache *c, uint32_t a, uint32_t b, uint32_t m)
{
    size_t h = (size_t)(a * 2654435761u) ^ (size_t)(b * 40503u) ^ (size_t)(m * 2246822519u);

    assert(c->cap != 0 && (c->cap & (c->cap - 1u)) == 0);
    h &= c->cap - 1u;
    while (c->e[h].used && !(c->e[h].a == a && c->e[h].b == b && c->e[h].m == m))
        h = (h + 1u) & (c->cap - 1u);
    return h;
}

static const CacheEnt *cache_find(const Cache *c, uint32_t a, uint32_t b, uint32_t m)
{
    size_t h;

    if (c->cap == 0)
        return NULL;
    h = cache_slot(c, a, b, m);
    return c->e[h].used ? &c->e[h] : NULL;
}

static bool cache_put(Cache *c, uint32_t a, uint32_t b, uint32_t m, uint8_t val)
{
    size_t h;

    if (c->cap == 0 || (c->count + 1u) * 2u > c->cap) {
        Cache n;
        size_t i;

        n.cap = c->cap ? c->cap * 2u : 256u;
        n.count = 0;
        n.e = (CacheEnt *)calloc(n.cap, sizeof(CacheEnt));
        if (n.e == NULL)
            return false;
        for (i = 0; i < c->cap; i++)
            if (c->e[i].used) {
                h = cache_slot(&n, c->e[i].a, c->e[i].b, c->e[i].m);
                n.e[h] = c->e[i];
                n.count++;
            }
        free(c->e);
        *c = n;
    }
    h = cache_slot(c, a, b, m);
    if (!c->e[h].used)
        c->count++;
    c->e[h].a = a; c->e[h].b = b; c->e[h].m = m; c->e[h].val = val; c->e[h].used = 1;
    return true;
}

typedef struct Ref { int32_t col[256]; unsigned n; uint8_t role; } Ref;

struct RgRCtx {
    const RgWorld *w;
    const RgRoles *r;
    RgProps *props;
    uint32_t general;            /* the General primary tileset's address */
    Cache roles, rocky;
    bool refsInit, verify;
    unsigned nRefs, conflicts;
    Ref refs[MAX_REFS];
};

RgRCtx *rg_rctx_new(const RgWorld *w, const RgRoles *r, RgErr *err)
{
    RgRCtx *c;

    assert(w != NULL && r != NULL && err != NULL);
    *err = RG_OK;
    c = (RgRCtx *)calloc(1, sizeof(*c));
    if (c == NULL) {
        *err = RG_ERR_NOMEM;
        return NULL;
    }
    c->w = w;
    c->r = r;
    c->general = w->layouts[RG_WORLD_ROOT - 1u].ts[0]->addr;
    c->props = rg_props_open(w, err);
    if (c->props == NULL) {
        if (*err == RG_OK)
            *err = RG_ERR_NOMEM;
        free(c);
        return NULL;
    }
    return c;
}

void rg_rctx_free(RgRCtx *c)
{
    if (c == NULL)
        return;
    rg_props_close(c->props);
    free(c->roles.e);
    free(c->rocky.e);
    free(c);
}

void rg_rctx_verify(RgRCtx *c, bool on)
{
    assert(c != NULL);
    c->verify = on;
}

unsigned rg_rctx_conflicts(const RgRCtx *c)
{
    assert(c != NULL);
    return c->conflicts;
}

/* ---- drawn_role (rel:1270-1324) ---- */

/* _rock_pixels (rel:1277-1280): col[y*16+x] = 0xRRGGBB of a rock-coloured pixel, else NO_COLOUR. Returns the count,
 * or -1 on no memory. */
static int rock_pixels(const RgAlias *al, RgPair *pair, uint16_t m, bool lowerOnly, int32_t col[256])
{
    RgImage im;
    unsigned k;
    int n = 0;

    if (!rg_alias_cell_image(al, pair, m, lowerOnly, &im))
        return -1;
    for (k = 0; k < 256; k++) {
        const uint8_t *p = im.px + 4u * k;

        if (rock_class(p[0], p[1], p[2]) != 0) {
            col[k] = (int32_t)(((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2]);
            n++;
        } else {
            col[k] = NO_COLOUR;
        }
    }
    rg_img_free(&im);
    return n;
}

/* ROLE_REFERENCE's iteration order (rel:1270-1271): FACE_SOUTH's ids below 0x200 in CPython set order, then
 * SIDE_WEST's, then SIDE_EAST's. Python set literals of these ids fill in source order. */
static unsigned reference_order(uint16_t *ids, uint8_t *roles)
{
    static const int64_t kFace[13] = {0x07c, 0x0a9, 0x09f, 0x0a7, 0x091, 0x079, 0x1b0, 0x0af, 0x0cf, 0x1f0, 0x1f1,
                                      0x33b, 0x33c};
    static const int64_t kWest[2] = {0x070, 0x073};
    static const int64_t kEast[3] = {0x072, 0x075, 0x0a2};
    const int64_t *seq[3] = {kFace, kWest, kEast};
    const unsigned len[3] = {13, 2, 3};
    unsigned s, i, n = 0, k;

    for (s = 0; s < 3; s++) {
        int64_t hashes[13], order[13];
        uint64_t scratch[512];

        assert(rg_set_scratch_bytes(len[s]) <= sizeof(scratch));
        for (i = 0; i < len[s]; i++)
            hashes[i] = rg_hash64_int(seq[s][i]);
        k = rg_set_order(seq[s], hashes, len[s], order, scratch);
        for (i = 0; i < k; i++)
            if (order[i] < 0x200) {
                assert(n < MAX_REFS);
                ids[n] = (uint16_t)order[i];
                roles[n++] = (uint8_t)(s + 1u);
            }
    }
    return n;
}

static bool refs_init(RgRCtx *c, const RgAlias *al, RgPair *pair)
{
    uint16_t ids[MAX_REFS];
    uint8_t roles[MAX_REFS];
    unsigned n = reference_order(ids, roles), i;

    assert(c != NULL && pair != NULL);
    for (i = 0; i < n; i++) {
        int k = rock_pixels(al, pair, ids[i], false, c->refs[i].col);

        if (k < 0)
            return false;
        c->refs[i].n = (unsigned)k;
        c->refs[i].role = roles[i];
    }
    c->nRefs = n;
    c->refsInit = true;
    return true;
}

static bool is_reference_tile(unsigned m)
{
    return m < 0x200u && (rg_is_face_south(m) || rg_is_side_west(m) || rg_is_side_east(m));
}

/* The first reference whose rock pixels agree with a[] (rel:1308-1313), or 0. */
static uint8_t match_reference(const RgRCtx *c, const int32_t a[256], unsigned na)
{
    unsigned i, q;

    for (i = 0; i < c->nRefs; i++) {
        const Ref *b = &c->refs[i];
        unsigned same = 0, both = 0;

        for (q = 0; q < 256; q++) {
            same += (a[q] != NO_COLOUR && b->col[q] == a[q]) ? 1u : 0u;
            both += (a[q] != NO_COLOUR && b->col[q] != NO_COLOUR) ? 1u : 0u;
        }
        /* |set(a) | set(b)| = na + nb - |intersection of the pixel sets| */
        if ((double)same >= ROLE_MATCH * (double)(na + b->n - both))
            return b->role;
    }
    return 0;
}

/* The upper layer is a cover that is not rock, a quarter of the cell at least (rel:1297-1302). */
static int has_cover(const RgAlias *al, RgPair *pair, uint16_t m, bool *upper)
{
    RgImage hi, lo;
    unsigned k, cover = 0, rockCover = 0;

    if (!rg_alias_cell_image(al, pair, m, false, &hi))
        return -1;
    if (!rg_alias_cell_image(al, pair, m, true, &lo)) {
        rg_img_free(&hi);
        return -1;
    }
    for (k = 0; k < 256; k++) {
        const uint8_t *h = hi.px + 4u * k, *l = lo.px + 4u * k;

        if (memcmp(h, l, 4) != 0) {
            cover++;
            rockCover += rock_class(h[0], h[1], h[2]) != 0 ? 1u : 0u;
        }
    }
    *upper = cover >= 64u && rockCover == 0;
    rg_img_free(&hi);
    rg_img_free(&lo);
    return 0;
}

static int role_compute(RgRCtx *c, const RgAlias *al, RgPair *pair, uint16_t m, uint8_t *out)
{
    bool upper = false;
    unsigned pass;

    *out = 0;
    if (rg_is_face_south(m) || rg_is_side_west(m) || rg_is_side_east(m)) {
        *out = rg_is_face_south(m) ? 1u : rg_is_side_west(m) ? 2u : 3u;
        return 0;
    }
    if (!c->refsInit && !refs_init(c, al, pair))
        return -1;
    if (has_cover(al, pair, m, &upper) < 0)
        return -1;
    for (pass = 0; pass < 2; pass++) {
        int32_t a[256];
        int na;
        uint8_t role;

        if (pass == 1 && !upper)
            break;
        na = rock_pixels(al, pair, m, pass == 1, a);
        if (na < 0)
            return -1;
        if (na == 0)
            continue;
        role = match_reference(c, a, (unsigned)na);
        if (role != 0) {
            *out = (uint8_t)(role | ((pass == 1 && !is_reference_tile(m)) ? 4u : 0u));
            return 0;
        }
    }
    return 0;
}

uint8_t rg_canvas_drawn_role(RgRCtx *c, const RgAlias *al, RgPair *pair, const RgLayout *L, uint16_t m)
{
    const CacheEnt *hit;
    uint8_t v = 0;

    assert(c != NULL && pair != NULL && L != NULL);
    hit = cache_find(&c->roles, L->ts[0]->addr, L->ts[1]->addr, m);
    if (hit != NULL && !c->verify)
        return hit->val;
    if (hit != NULL) {                    /* verify: would the value differ? */
        uint8_t was = hit->val;

        if (role_compute(c, al, pair, m, &v) == 0 && v != was) {
            c->conflicts++;
        }
        return was;
    }
    if (L->ts[0]->addr == c->general) {
        if (role_compute(c, al, pair, m, &v) != 0)
            return 0xFF;                  /* no memory: the caller fails the build */
    } else if (rg_is_face_south(m) || rg_is_side_west(m) || rg_is_side_east(m)) {
        v = rg_is_face_south(m) ? 1u : rg_is_side_west(m) ? 2u : 3u;
    }
    if (!cache_put(&c->roles, L->ts[0]->addr, L->ts[1]->addr, m, v))
        return 0xFF;
    return v;
}

/* ---- the majority vote (rel:1226-1260) ---- */

/* Adds (sign = +1) or removes (-1) pixel row y of `kind` to the per-column tallies. */
static void tally_row(const uint8_t *kind, int w, int y, int sign, int32_t *colT, int32_t *colF)
{
    const uint8_t *row = kind + (size_t)y * (size_t)w;
    int x;

    assert(colT != NULL && colF != NULL);
    for (x = 0; x < w; x++) {
        colT[x] += (row[x] == RG_K_TOP || row[x] == RG_K_RIM) ? sign : 0;
        colF[x] += (row[x] == RG_K_FACE) ? sign : 0;
    }
}

static void vote_row(const uint8_t *row, int w, const int32_t *colT, const int32_t *colF, uint8_t *out)
{
    int32_t t = 0, f = 0;
    int x;

    assert(row != NULL && out != NULL);
    for (x = 0; x <= MAJORITY && x < w; x++) {      /* the window of x = 0: columns 0..R */
        t += colT[x];
        f += colF[x];
    }
    for (x = 0; x < w; x++) {
        if (row[x] == RG_K_TOP || row[x] == RG_K_FACE || row[x] == RG_K_FLECK)
            out[x] = t > f ? RG_K_TOP : RG_K_FACE;
        if (x + 1 + MAJORITY < w) {
            t += colT[x + 1 + MAJORITY];
            f += colF[x + 1 + MAJORITY];
        }
        if (x - MAJORITY >= 0) {
            t -= colT[x - MAJORITY];
            f -= colF[x - MAJORITY];
        }
    }
}

bool rg_canvas_vote(const uint8_t *kind, int w, int h, uint8_t *voted)
{
    int32_t *colT, *colF;
    int y;

    assert(kind != NULL && voted != NULL && kind != voted && w > 0 && h > 0);
    memcpy(voted, kind, (size_t)w * (size_t)h);
    colT = (int32_t *)calloc((size_t)w, sizeof(int32_t));
    colF = (int32_t *)calloc((size_t)w, sizeof(int32_t));
    if (colT == NULL || colF == NULL) {
        free(colT);
        free(colF);
        return false;
    }
    for (y = 0; y < MAJORITY && y < h; y++)    /* rows 0..R-1; row y+R joins before each vote */
        tally_row(kind, w, y, 1, colT, colF);
    for (y = 0; y < h; y++) {
        if (y + MAJORITY < h)
            tally_row(kind, w, y + MAJORITY, 1, colT, colF);
        if (y - MAJORITY - 1 >= 0)
            tally_row(kind, w, y - MAJORITY - 1, -1, colT, colF);
        vote_row(kind + (size_t)y * (size_t)w, w, colT, colF, voted + (size_t)y * (size_t)w);
    }
    free(colT);
    free(colF);
    return true;
}

/* ---- drawn_canvas (rel:1089-1260) ---- */

typedef struct Mem {
    const RgLayout *L;
    const uint8_t *roles;
    RgAlias *al;
    RgPair *pair;
    int ox, oy;
    uint8_t *props;              /* w*h: cells a modelled object stands on */
    RgLedgeSet ledges;           /* only for an alias layout (rel:1119-1124) */
    bool haveLedges;
} Mem;

typedef struct Build {
    RgRCtx *c;
    Mem *m;
    unsigned n;
    RgCanvas *cv;
    uint8_t *kind;               /* raw per-pixel kinds, freed after the vote */
    size_t W, H;
} Build;

static uint8_t role_of(const Mem *m, int cx, int cy)
{
    assert(cx >= 0 && cy >= 0 && cx < (int)m->L->w && cy < (int)m->L->h);
    return m->roles[(size_t)cy * m->L->w + (size_t)cx];
}

static size_t cell_ix(const Build *b, const Mem *m, int cx, int cy)
{
    return (size_t)(m->oy + cy) * (size_t)b->cv->cw + (size_t)(m->ox + cx);
}

static void fill_cell(Build *b, const Mem *m, int cx, int cy, uint8_t k)
{
    int j;

    assert(b->kind != NULL && k < RG_K_COUNT);
    for (j = 0; j < 16; j++)
        memset(b->kind + (size_t)((m->oy + cy) * 16 + j) * b->W + (size_t)((m->ox + cx) * 16), k, 16);
}

static void paint_cell(Build *b, const Mem *m, int cx, int cy, const RgImage *im, bool stair)
{
    int i, j;

    assert(im != NULL && im->w == 16 && im->h == 16);
    for (j = 0; j < 16; j++) {
        uint8_t *row = b->kind + (size_t)((m->oy + cy) * 16 + j) * b->W + (size_t)((m->ox + cx) * 16);

        for (i = 0; i < 16; i++)
            row[i] = stair ? (uint8_t)RG_K_FREE : pixel_kind(im->px + 4u * (unsigned)(j * 16 + i));
    }
}

/* rel:1123-1131 keep and 1133-1141 props-in-water: the cells drawn as flat ground whatever their art. */
static bool cell_ground(Build *b, const Mem *m, int cx, int cy)
{
    const RgLayout *L = m->L;
    uint8_t *flat = &b->cv->flat[cell_ix(b, m, cx, cy)];
    uint16_t mt, ent[8];
    unsigned k;

    if (m->haveLedges && (m->ledges.at[(size_t)cy * L->w + (size_t)cx] >= 0 ||
                          rg_behaviour(L, cx, cy) == RG_MB_BERRY_TREE_SOIL)) {
        *flat = RG_FLAT_FLOOR;
        fill_cell(b, m, cx, cy, RG_K_GROUND);
        return true;
    }
    if (!m->props[(size_t)cy * L->w + (size_t)cx])
        return false;
    mt = rg_alias_metatile(m->al, L, cx, cy);
    if (!rg_metatile_entries(L, mt, ent))
        return false;
    for (k = 0; k < 4; k++)
        if ((ent[k] & 0x3FFu) < 432u || (ent[k] & 0x3FFu) >= 462u)
            return false;
    *flat = RG_FLAT_WATER;
    fill_cell(b, m, cx, cy, RG_K_GROUND);
    return true;
}

/* rel:1147-1151: water drawn as rock, cached by (secondary tileset, metatile). Returns 1/0, -1 on no memory. */
static int rocky_water(Build *b, const Mem *m, uint16_t mt)
{
    RgRCtx *c = b->c;
    const CacheEnt *hit = cache_find(&c->rocky, m->L->ts[1]->addr, 0, mt);
    RgImage im;
    unsigned k, n = 0;

    if (hit != NULL && !c->verify)
        return hit->val;
    if (!rg_alias_cell_image(m->al, m->pair, mt, false, &im))
        return -1;
    for (k = 0; k < 256; k++)
        n += rock_class(im.px[4u * k], im.px[4u * k + 1], im.px[4u * k + 2]) != 0 ? 1u : 0u;
    rg_img_free(&im);
    if (hit != NULL) {
        c->conflicts += (hit->val != (n >= ROCKY_WATER_PX ? 1u : 0u)) ? 1u : 0u;
        return hit->val;
    }
    return cache_put(&c->rocky, m->L->ts[1]->addr, 0, mt, n >= ROCKY_WATER_PX ? 1 : 0) ? (n >= ROCKY_WATER_PX) : -1;
}

/* rel:1157-1165: is the cell flat ground, and which kind. 0 when not. rocky: the water cell is drawn as rock. */
static uint8_t flat_kind(const Mem *m, int cx, int cy, uint8_t role, uint16_t mt, uint8_t drawnRole, int rocky)
{
    const RgLayout *L = m->L;
    unsigned beh = rg_behaviour(L, cx, cy);
    bool plain;

    if (drawnRole == 1u || rg_is_face_south(mt))
        return 0;
    plain = (role == VOXEL_ROLE_FLOOR || role == VOXEL_ROLE_SIGNPOST || role == VOXEL_ROLE_WATER) &&
            !rg_has_warp(L, cx, cy) && !rg_covers(m->pair, mt) &&
            !(role == VOXEL_ROLE_WATER && rocky > 0) && !rg_is_sea_cap(mt);
    if (!plain && !rg_is_flat_behaviour(beh))
        return 0;
    if (rg_is_flat_behaviour(beh))
        return RG_FLAT_BRIDGE;
    return role == VOXEL_ROLE_WATER ? RG_FLAT_WATER : role == VOXEL_ROLE_SIGNPOST ? RG_FLAT_SIGNPOST : RG_FLAT_FLOOR;
}

/* rel:1143-1215 for one ordinary cell. Returns false on no memory. */
static bool cell_drawn(Build *b, const Mem *m, int cx, int cy)
{
    const RgLayout *L = m->L;
    size_t ci = cell_ix(b, m, cx, cy);
    uint8_t role = role_of(m, cx, cy), dr, fk;
    uint16_t mt = rg_alias_metatile(m->al, L, cx, cy);
    bool lowerOnly, stair;
    int rocky = 0;
    RgImage im;

    b->cv->blocked[ci] = (rg_blocked(L, cx, cy) &&
                          (role == VOXEL_ROLE_TREE || role == VOXEL_ROLE_WALL || role == VOXEL_ROLE_PROP)) ? 1 : 0;
    dr = rg_canvas_drawn_role(b->c, m->al, m->pair, L, mt);
    if (dr == 0xFF)
        return false;
    if ((dr & 4u) && role != VOXEL_ROLE_TREE)
        dr = 0;                                      /* under a roof: the house's, not a face */
    b->cv->side[ci] = (dr & 3u) == 2u ? 1 : (dr & 3u) == 3u ? -1 : 0;
    b->cv->faceLow[ci] = (dr & 3u) == 1u ? 1 : 0;
    if (rg_behaviour(L, cx, cy) == RG_MB_WATERFALL) {     /* a waterfall is a face of water, falling a level */
        b->cv->flat[ci] = RG_FLAT_FALL;
        fill_cell(b, m, cx, cy, RG_K_FACE);
        return true;
    }
    if (role == VOXEL_ROLE_WATER && (rocky = rocky_water(b, m, mt)) < 0)
        return false;
    fk = flat_kind(m, cx, cy, role, mt, (uint8_t)(dr & 3u), rocky);
    b->cv->flat[ci] = fk;
    lowerOnly = m->props[(size_t)cy * L->w + (size_t)cx] != 0 || dr == 5u;   /* ("face", True): rel:1175-1182 */
    stair = role == VOXEL_ROLE_STAIR && fk == 0;
    if (rg_is_dirt(mt)) {
        fill_cell(b, m, cx, cy, RG_K_GROUND);
        return true;
    }
    if (!rg_alias_cell_image(m->al, m->pair, mt, lowerOnly, &im))
        return false;
    paint_cell(b, m, cx, cy, &im, stair);
    rg_img_free(&im);
    return true;
}

/* rel:1217-1232: a bridge's ends, drawn over the player where they cross a rock's lip, are the bridge. Same
 * visiting order as upstream (members, rows, columns), updates visible at once; capped at cells + 1 passes. */
static bool bridge_pass(Build *b)
{
    static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
    bool grew = false;
    unsigned k, i;
    int cx, cy;

    for (k = 0; k < b->n; k++) {
        const Mem *m = &b->m[k];

        for (cy = 0; cy < (int)m->L->h; cy++)
            for (cx = 0; cx < (int)m->L->w; cx++) {
                uint8_t role = role_of(m, cx, cy);

                if (b->cv->flat[cell_ix(b, m, cx, cy)] != 0 || (role != VOXEL_ROLE_FLOOR && role != VOXEL_ROLE_STAIR))
                    continue;
                for (i = 0; i < 4; i++) {
                    int nx = cx + dx[i], ny = cy + dy[i];

                    if (nx >= 0 && ny >= 0 && nx < (int)m->L->w && ny < (int)m->L->h &&
                        b->cv->flat[cell_ix(b, m, nx, ny)] == RG_FLAT_BRIDGE) {
                        b->cv->flat[cell_ix(b, m, cx, cy)] = RG_FLAT_BRIDGE;
                        grew = true;
                        break;
                    }
                }
            }
    }
    return grew;
}

static void bridge_grow(Build *b)
{
    size_t cap = (size_t)b->cv->cw * (size_t)b->cv->ch + 1u, it;

    for (it = 0; it < cap; it++)
        if (!bridge_pass(b))
            return;
    assert(!"bridge_grow did not converge");
}

/* rel:1236-1271 one layout's planks: walked planks drawn over the player, not on sand, under a cover. */
static bool is_plank(const Build *b, const Mem *m, int cx, int cy)
{
    return b->cv->flat[cell_ix(b, m, cx, cy)] == 0 && role_of(m, cx, cy) == VOXEL_ROLE_FLOOR &&
           !rg_blocked(m->L, cx, cy) && !rg_is_sand(rg_behaviour(m->L, cx, cy)) &&
           rg_covers(m->pair, rg_alias_metatile(m->al, m->L, cx, cy));
}

/* Floods one run of planks from (sx, sy) (LIFO, as upstream), marking seen; returns the set of directions that
 * have water beside some plank of the run, and adds the run to `inRun` (cell flags). stack: w*h ints. */
static unsigned plank_run(const Build *b, const Mem *m, const uint8_t *planks, uint8_t *seen, uint8_t *inRun,
                          int32_t *stack, int sx, int sy)
{
    static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
    const int w = (int)m->L->w, h = (int)m->L->h;
    unsigned sides = 0, d;
    size_t sp = 0;

    assert(planks != NULL && seen != NULL && inRun != NULL && stack != NULL);
    stack[sp++] = sy * w + sx;
    seen[sy * w + sx] = 1;
    while (sp > 0) {
        int c = stack[--sp], x = c % w, y = c / w;

        inRun[c] = 1;
        for (d = 0; d < 4; d++) {
            int nx = x + dx[d], ny = y + dy[d];

            if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                continue;
            if (role_of(m, nx, ny) == VOXEL_ROLE_WATER)
                sides |= 1u << d;
            if (planks[ny * w + nx] && !seen[ny * w + nx]) {
                seen[ny * w + nx] = 1;
                stack[sp++] = ny * w + nx;
            }
        }
    }
    (void)b;
    return sides;
}

static bool pier_member(Build *b, const Mem *m)
{
    const int w = (int)m->L->w, h = (int)m->L->h;
    size_t n = (size_t)w * (size_t)h, i;
    uint8_t *planks = (uint8_t *)calloc(n, 1), *seen = (uint8_t *)calloc(n, 1), *run = (uint8_t *)malloc(n);
    int32_t *stack = (int32_t *)malloc(n * sizeof(int32_t));
    bool ok = planks && seen && run && stack;
    int x, y;

    if (ok) {
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++)
                planks[y * w + x] = is_plank(b, m, x, y) ? 1 : 0;
        for (i = 0; i < n; i++) {
            unsigned sides, bits;

            if (!planks[i] || seen[i])
                continue;
            memset(run, 0, n);
            sides = plank_run(b, m, planks, seen, run, stack, (int)(i % (size_t)w), (int)(i / (size_t)w));
            bits = (sides & 1u) + ((sides >> 1) & 1u) + ((sides >> 2) & 1u) + ((sides >> 3) & 1u);
            if (bits >= 3u)       /* water on three sides of it at least (rel:1264) */
                for (x = 0; x < (int)n; x++)
                    if (run[x])
                        b->cv->pier[cell_ix(b, m, x % w, x / w)] = 1;
        }
    }
    free(planks); free(seen); free(run); free(stack);
    return ok;
}

static void mem_free(Mem *m)
{
    rg_alias_free(m->al);
    rg_pair_close(m->pair);
    free(m->props);
    if (m->haveLedges)
        rg_ledge_set_free(&m->ledges);
    memset(m, 0, sizeof(*m));
}

static RgErr mem_open(RgRCtx *c, const RgDrawnMember *dm, Mem *m)
{
    const RgLayout *L = &c->w->layouts[dm->layout - 1u];
    RgErr e;

    assert(L->present && L->w > 0 && L->h > 0);
    memset(m, 0, sizeof(*m));
    m->L = L;
    m->ox = dm->x;
    m->oy = dm->y;
    m->roles = rg_roles_of(c->r, dm->layout);
    if (m->roles == NULL)
        return RG_ERR_TABLES;
    e = rg_alias_of(c->w, dm->layout, &m->al);
    if (e != RG_OK)
        return e;
    m->pair = rg_pair_open(c->w, L->pairIndex);
    m->props = (uint8_t *)calloc((size_t)L->w * L->h, 1);
    if (m->pair == NULL || m->props == NULL)
        return RG_ERR_NOMEM;
    e = rg_props_cells(c->props, dm->layout, m->props);
    if (e != RG_OK)
        return e;
    if (m->al != NULL) {                 /* rel:1119-1124: an alias layout keeps its ledges and berry soil as ground */
        if (!rg_ledge_cells(L, true, &m->ledges))
            return RG_ERR_NOMEM;
        m->haveLedges = true;
    }
    return RG_OK;
}

static bool canvas_alloc(RgCanvas *cv, Build *b)
{
    size_t cells = (size_t)cv->cw * (size_t)cv->ch, i;

    cv->blocked = (uint8_t *)calloc(cells, 1);
    cv->side = (int8_t *)calloc(cells, 1);
    cv->flat = (uint8_t *)calloc(cells, 1);
    cv->faceLow = (uint8_t *)calloc(cells, 1);
    cv->pier = (uint8_t *)calloc(cells, 1);
    cv->meta = (uint16_t *)malloc(cells * sizeof(uint16_t));
    b->kind = (uint8_t *)malloc(b->W * b->H);
    if (!cv->blocked || !cv->side || !cv->flat || !cv->faceLow || !cv->pier || !cv->meta || !b->kind)
        return false;
    memset(b->kind, RG_K_VOID, b->W * b->H);
    for (i = 0; i < cells; i++)
        cv->meta[i] = 0xFFFFu;
    return true;
}

/* rel:1597-1603 metatile_at: the first member holding the cell reads its aliased metatile. */
static void fill_meta(const Build *b)
{
    unsigned k;
    int cx, cy;

    for (k = 0; k < b->n; k++) {
        const Mem *m = &b->m[k];

        for (cy = 0; cy < (int)m->L->h; cy++)
            for (cx = 0; cx < (int)m->L->w; cx++) {
                uint16_t *slot = &b->cv->meta[cell_ix(b, m, cx, cy)];

                if (*slot == 0xFFFFu)
                    *slot = rg_alias_metatile(m->al, m->L, cx, cy);
            }
    }
}

static RgErr paint_members(Build *b)
{
    unsigned k;
    int cx, cy;

    for (k = 0; k < b->n; k++) {
        const Mem *m = &b->m[k];

        for (cy = 0; cy < (int)m->L->h; cy++)
            for (cx = 0; cx < (int)m->L->w; cx++)
                if (!cell_ground(b, m, cx, cy) && !cell_drawn(b, m, cx, cy))
                    return RG_ERR_NOMEM;
    }
    bridge_grow(b);
    for (k = 0; k < b->n; k++)
        if (!pier_member(b, &b->m[k]))
            return RG_ERR_NOMEM;
    fill_meta(b);
    return RG_OK;
}

static RgErr build_members(RgRCtx *c, const RgDrawn *d, const RgDrawnGroup *g, Build *b)
{
    RgCanvas *cv = b->cv;
    unsigned k;
    RgErr e;

    cv->nMem = g->nMembers;
    cv->mem = (RgCanvasMember *)calloc(g->nMembers, sizeof(RgCanvasMember));
    b->m = (Mem *)calloc(g->nMembers, sizeof(Mem));
    if (cv->mem == NULL || b->m == NULL)
        return RG_ERR_NOMEM;
    for (k = 0; k < g->nMembers; k++) {
        const RgDrawnMember *dm = &d->pool[g->first + k];

        b->n = k + 1u;
        e = mem_open(c, dm, &b->m[k]);
        if (e != RG_OK)
            return e;
        cv->mem[k].layout = dm->layout;
        cv->mem[k].ox = dm->x;
        cv->mem[k].oy = dm->y;
        cv->mem[k].w = b->m[k].L->w;
        cv->mem[k].h = b->m[k].L->h;
        if (dm->x + (int)b->m[k].L->w > cv->cw) cv->cw = dm->x + (int)b->m[k].L->w;
        if (dm->y + (int)b->m[k].L->h > cv->ch) cv->ch = dm->y + (int)b->m[k].L->h;
    }
    return RG_OK;
}

RgErr rg_canvas_build(RgRCtx *c, const RgDrawn *d, const RgDrawnGroup *g, RgCanvas *out)
{
    Build b;
    uint8_t *voted = NULL;
    RgErr e;
    unsigned k;

    assert(c != NULL && d != NULL && g != NULL && out != NULL && g->nMembers > 0);
    memset(out, 0, sizeof(*out));
    memset(&b, 0, sizeof(b));
    b.c = c;
    b.cv = out;
    e = build_members(c, d, g, &b);
    if (e == RG_OK) {
        assert(out->cw == (int)g->cellsW && out->ch == (int)g->cellsH);
        b.W = (size_t)out->cw * 16u;
        b.H = (size_t)out->ch * 16u;
        e = canvas_alloc(out, &b) ? paint_members(&b) : RG_ERR_NOMEM;
    }
    if (e == RG_OK) {
        voted = (uint8_t *)malloc(b.W * b.H);
        e = (voted != NULL && rg_canvas_vote(b.kind, (int)b.W, (int)b.H, voted)) ? RG_OK : RG_ERR_NOMEM;
    }
    for (k = 0; k < b.n; k++)
        mem_free(&b.m[k]);
    free(b.m);
    free(b.kind);
    if (e != RG_OK) {
        free(voted);
        rg_canvas_free(out);
        return e;
    }
    out->kind = voted;
    return RG_OK;
}

void rg_canvas_free(RgCanvas *cv)
{
    if (cv == NULL)
        return;
    free(cv->mem);
    free(cv->kind);
    free(cv->blocked);
    free(cv->side);
    free(cv->flat);
    free(cv->faceLow);
    free(cv->pier);
    free(cv->meta);
    memset(cv, 0, sizeof(*cv));
}
