/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/voxel_cells.py (Layout.role_at
 * and its helpers), MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
#include "rg_roles.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "rg_behavior.h"
#include "voxel_regions.h"


bool rg_is_ledge_junction(const RgLayout *L, int x, int y)
{
    bool horizontal, vertical;

    const GpBehSet *jump;

    assert(L != NULL);
    if (!rg_blocked(L, x, y))
        return false;
    jump = &rg_lprof(L)->jump;
    horizontal = gp_beh(jump, rg_behaviour(L, x - 1, y)) || gp_beh(jump, rg_behaviour(L, x + 1, y));
    vertical = gp_beh(jump, rg_behaviour(L, x, y - 1)) || gp_beh(jump, rg_behaviour(L, x, y + 1));
    return horizontal && vertical;
}

typedef struct {
    const RgWorld *w;
    const RgRoles *r;
    RgPair *pair;
    const RgLayout *L;
    uint8_t *houses;             /* w*h flags */
    uint32_t *lamps;             /* sorted (post << 16 | head) */
    size_t nLamps;
    bool lampsBuilt;
} Ctx;

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : (x > y ? 1 : 0);
}

static int cmp_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : (x > y ? 1 : 0);
}

/* ---- cell predicates (cells:275-278, 306-319) -------------------------------------------- */
static bool open_post(const RgLayout *L, int x, int y)
{
    return L->outdoor && rg_blocked(L, x, y) && !rg_blocked(L, x + 1, y) && !rg_blocked(L, x - 1, y)
        && !rg_blocked(L, x, y + 1);
}

static bool is_house(const Ctx *c, int x, int y)
{
    return !rg_off(c->L, x, y) && c->houses[(size_t)y * c->L->w + (size_t)x];
}

/* houses() (cells:249-271): the mass above each house door. Iterative flood, bounded by w*h. */
static bool build_houses(Ctx *c)
{
    static const int8_t nb[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
    const RgLayout *L = c->L;
    const GameProfile *gp = rg_lprof(L);
    size_t cells = (size_t)L->w * L->h, d;
    uint16_t *seen;
    int32_t *stack;

    c->houses = (uint8_t *)calloc(cells, 1);
    seen = (uint16_t *)calloc(cells, sizeof(uint16_t));
    stack = (int32_t *)malloc(cells * sizeof(int32_t));
    if (c->houses == NULL || seen == NULL || stack == NULL) {
        free(seen);
        free(stack);
        return false;
    }
    for (d = 0; d < L->warpCount; d++) {
        int dx = L->warps[d].x, dy = L->warps[d].y, sx = dx, sy = dy - 1;
        size_t sp = 0;
        uint16_t stamp = (uint16_t)(d + 1);

        if (!gp_beh(&gp->houseDoor, rg_behaviour(L, dx, dy)) || rg_off(L, sx, sy) || !rg_blocked(L, sx, sy))
            continue;
        stack[sp++] = sy * (int32_t)L->w + sx;
        seen[(size_t)sy * L->w + (size_t)sx] = stamp;
        while (sp > 0) {
            int32_t cur = stack[--sp];
            int x = (int)(cur % L->w), y = (int)(cur / L->w);
            unsigned k;

            c->houses[(size_t)cur] = 1;
            for (k = 0; k < 4; k++) {
                int nx = x + nb[k][0], ny = y + nb[k][1];
                size_t ni;
                if (rg_off(L, nx, ny) || !rg_blocked(L, nx, ny) || abs(nx - dx) > gp->houseHalfWidth
                    || ny > dy || ny < dy - gp->houseHeight)
                    continue;
                ni = (size_t)ny * L->w + (size_t)nx;
                if (seen[ni] == stamp || rg_foliage_ge_half(c->pair, rg_metatile(L, nx, ny)))
                    continue;
                seen[ni] = stamp;
                stack[sp++] = (int32_t)ni;
            }
        }
    }
    free(seen);
    free(stack);
    return true;
}

static bool free_post(const Ctx *c, int x, int y)
{
    const RgLayout *L = c->L;
    uint16_t m;

    if (!open_post(L, x, y))
        return false;
    if (rg_has_sign(L, x, y))
        return true;
    m = rg_metatile(L, x, y);
    if (rg_covers(c->pair, m) || rg_foliage_ge_half(c->pair, m) || is_house(c, x, y))
        return false;
    return !rg_blocked(L, x, y - 1) || rg_metatile(L, x, y - 1) != m;
}

/* lamps() (cells:321-331): (post, head) metatile pairs of free posts with a head drawn north. */
static bool build_lamps(Ctx *c)
{
    const RgLayout *L = c->L;
    size_t cap = 64, n = 0;
    int x, y;
    uint32_t *a = (uint32_t *)malloc(cap * sizeof(uint32_t));

    if (a == NULL)
        return false;
    for (y = 1; y < (int)L->h; y++) {
        for (x = 0; x < (int)L->w; x++) {
            uint16_t head;
            if (rg_has_sign(L, x, y) || rg_blocked(L, x, y - 1))
                continue;
            head = rg_metatile(L, x, y - 1);
            if (!rg_layer_any(rg_layer(c->pair, head, 1)) || !free_post(c, x, y))
                continue;
            if (n == cap) {
                uint32_t *na = (uint32_t *)realloc(a, cap * 2 * sizeof(uint32_t));
                if (na == NULL) {
                    free(a);
                    return false;
                }
                a = na;
                cap *= 2;
            }
            a[n++] = ((uint32_t)rg_metatile(L, x, y) << 16) | head;
        }
    }
    if (n > 1)
        qsort(a, n, sizeof(uint32_t), cmp_u32);
    c->lamps = a;
    c->nLamps = n;
    c->lampsBuilt = true;
    return true;
}

static bool is_lamp(Ctx *c, int x, int y)
{
    uint32_t key;

    if (!c->lampsBuilt && !build_lamps(c))
        return false;
    key = ((uint32_t)rg_metatile(c->L, x, y) << 16) | rg_metatile(c->L, x, y - 1);
    return c->nLamps > 0 && bsearch(&key, c->lamps, c->nLamps, sizeof(uint32_t), cmp_u32) != NULL;
}

static bool is_post_metatile(const Ctx *c, uint16_t m)
{
    uint64_t key = ((uint64_t)rg_tileset_addr_of(c->L, m) << 16) | m;
    return c->r->postCount > 0 && bsearch(&key, c->r->posts, c->r->postCount, sizeof(uint64_t), cmp_u64) != NULL;
}

/* is_signpost (cells:280-304). */
static bool is_signpost(Ctx *c, int x, int y)
{
    const RgLayout *L = c->L;
    bool southOk;
    int sides = 0, sx = 0, k;

    if (!L->outdoor || !rg_blocked(L, x, y))
        return false;
    southOk = !rg_off(L, x, y + 1) && !rg_blocked(L, x, y + 1);
    /* Phase 34 G2: in FireRed / LeafGreen the signpost behaviour (0x84) IS the sign, whatever stands beside it, and the
     * Emerald lantern / open-post heuristics below are not used (they find 12 cells that are not signs and miss Pallet's two
     * mailboxes, which touch the house wall). Emerald keeps its own tests unchanged: its output is pinned. */
    if (gp_is_kanto(rg_lprof(L)))
        return southOk && gp_beh(&rg_lprof(L)->signpost, rg_behaviour(L, x, y));
    if (southOk && is_post_metatile(c, rg_metatile(L, x, y)))
        return true;
    if (free_post(c, x, y))
        return true;
    if (!southOk || y == 0 || !is_lamp(c, x, y))
        return false;
    for (k = -1; k <= 1; k += 2) {
        if (rg_blocked(L, x + k, y)) {
            sides++;
            sx = x + k;
        }
    }
    return sides == 1 && is_house(c, sx, y);
}

static unsigned role_at(Ctx *c, int x, int y)
{
    const RgLayout *L = c->L;
    unsigned b = rg_behaviour(L, x, y);
    uint16_t m;

    if (gp_beh(&rg_lprof(L)->water, b))
        return VOXEL_ROLE_WATER;
    if (gp_beh(&rg_lprof(L)->jump, b))
        return VOXEL_ROLE_LEDGE;
    m = rg_metatile(L, x, y);
    if (!rg_blocked(L, x, y))
        return rg_treads(c->pair, m) ? VOXEL_ROLE_STAIR : VOXEL_ROLE_FLOOR;
    if (is_signpost(c, x, y))
        return VOXEL_ROLE_SIGNPOST;
    if (is_house(c, x, y))
        return VOXEL_ROLE_WALL;
    if (rg_foliage_ge_half(c->pair, m))
        return VOXEL_ROLE_TREE;
    if ((!rg_off(L, x, y - 1) && !rg_blocked(L, x, y - 1) && !rg_off(L, x, y + 1) && !rg_blocked(L, x, y + 1))
        || (!rg_off(L, x - 1, y) && !rg_blocked(L, x - 1, y) && !rg_off(L, x + 1, y) && !rg_blocked(L, x + 1, y)))
        return VOXEL_ROLE_FENCE;
    if (rg_touches_walkable(L, x, y))
        return VOXEL_ROLE_CLIFF;
    return VOXEL_ROLE_SHELF;
}

bool rg_roles_layout(const RgWorld *w, const RgRoles *r, RgPair *pair, const RgLayout *L, uint8_t *out)
{
    Ctx c;
    int x, y;
    bool ok;

    if (w == NULL || r == NULL || pair == NULL || L == NULL || out == NULL || !L->present)
        return false;
    memset(&c, 0, sizeof(c));
    c.w = w;
    c.r = r;
    c.pair = pair;
    c.L = L;
    ok = build_houses(&c);
    for (y = 0; ok && y < (int)L->h; y++)
        for (x = 0; x < (int)L->w; x++)
            out[(size_t)y * L->w + (size_t)x] = (uint8_t)role_at(&c, x, y);
    free(c.houses);
    free(c.lamps);
    return ok;
}

/* ---- post_metatiles (cells:372-394) ------------------------------------------------------ */
static RgErr build_posts(const RgWorld *w, RgRoles *r)
{
    size_t cap = 64, n = 0, i;
    uint64_t *a = (uint64_t *)malloc(cap * sizeof(uint64_t));
    unsigned li;

    if (a == NULL)
        return RG_ERR_NOMEM;
    for (li = 0; li < w->layoutCount; li++) {
        const RgLayout *L = &w->layouts[li];
        for (i = 0; L->present && i < L->signCount; i++) {
            int x = L->signs[i].x, y = L->signs[i].y;
            uint16_t m;
            if (rg_off(L, x, y) || !open_post(L, x, y))
                continue;
            m = rg_metatile(L, x, y);
            if (n == cap) {
                uint64_t *na = (uint64_t *)realloc(a, cap * 2 * sizeof(uint64_t));
                if (na == NULL) {
                    free(a);
                    return RG_ERR_NOMEM;
                }
                a = na;
                cap *= 2;
            }
            a[n++] = ((uint64_t)rg_tileset_addr_of(L, m) << 16) | m;
        }
    }
    if (n > 1) {
        size_t u = 1;
        qsort(a, n, sizeof(uint64_t), cmp_u64);
        for (i = 1; i < n; i++)
            if (a[i] != a[u - 1])
                a[u++] = a[i];
        n = u;
    }
    r->posts = a;
    r->postCount = n;
    return RG_OK;
}

RgErr rg_roles_init(const RgWorld *w, RgRoles *r)
{
    unsigned li;
    uint32_t total = 0;

    if (w == NULL || r == NULL)
        return RG_ERR_NOMEM;
    memset(r, 0, sizeof(*r));
    r->layoutCount = w->layoutCount;
    r->off = (uint32_t *)calloc(w->layoutCount, sizeof(uint32_t));
    if (r->off == NULL)
        return RG_ERR_NOMEM;
    for (li = 0; li < w->layoutCount; li++) {
        r->off[li] = total;
        if (w->layouts[li].present)
            total += (uint32_t)w->layouts[li].w * w->layouts[li].h;
    }
    r->total = total;
    r->data = (uint8_t *)calloc(total ? total : 1, 1);
    if (r->data == NULL) {
        rg_roles_free(r);
        return RG_ERR_NOMEM;
    }
    if (build_posts(w, r) != RG_OK) {
        rg_roles_free(r);
        return RG_ERR_NOMEM;
    }
    return RG_OK;
}

void rg_roles_free(RgRoles *r)
{
    if (r == NULL)
        return;
    free(r->data);
    free(r->off);
    free(r->posts);
    memset(r, 0, sizeof(*r));
}

const uint8_t *rg_roles_of(const RgRoles *r, uint16_t layoutId)
{
    if (r == NULL || r->data == NULL || layoutId < 1 || layoutId > r->layoutCount)
        return NULL;
    return r->data + r->off[layoutId - 1];
}

RgErr rg_roles_all(const RgWorld *w, RgRoles *r, bool reverse, RgProgressFn progress, void *ctx)
{
    unsigned k, li, done = 0, total = 0;
    RgErr e = rg_roles_init(w, r);

    if (e != RG_OK)
        return e;
    for (li = 0; li < w->layoutCount; li++)
        total += w->layouts[li].present;
    for (k = 0; k < w->pairCount; k++) {
        unsigned pi = reverse ? w->pairCount - 1u - k : k;
        RgPair *p = rg_pair_open(w, (uint16_t)pi);

        if (p == NULL) {
            rg_roles_free(r);
            return RG_ERR_NOMEM;
        }
        for (li = 0; li < w->layoutCount; li++) {
            const RgLayout *L = &w->layouts[li];
            if (!L->present || L->pairIndex != pi)
                continue;
            if (!rg_roles_layout(w, r, p, L, r->data + r->off[li])) {
                rg_pair_close(p);
                rg_roles_free(r);
                return RG_ERR_NOMEM;
            }
            if (progress != NULL)
                progress(ctx, ++done, total);
        }
        rg_pair_close(p);
    }
    return RG_OK;
}
