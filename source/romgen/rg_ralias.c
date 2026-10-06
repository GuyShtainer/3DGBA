/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/gen_voxel_relief.py
 * (alias_of, AliasArt, layout_art, own_id: rel:408-502), MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
#include "rg_ralias.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "rg_rrock.h"
#include "rg_rtables.h"

#define MT_COUNT 1024u                 /* metatile ids are 10 bits */

typedef struct Drawing { uint8_t lab[256]; } Drawing;   /* _drawing (rel:415-419): colours numbered as they come */
typedef struct Known { Drawing d; uint16_t m; } Known;
typedef struct Vote { uint32_t a, b; unsigned n; } Vote;

static uint32_t rgb_at(const RgImage *im, unsigned k)
{
    const uint8_t *p = im->px + 4u * k;

    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

static void drawing_of(const RgImage *im, Drawing *d)
{
    uint32_t seen[256];
    unsigned k, j, n = 0;

    assert(im->w == 16 && im->h == 16);
    for (k = 0; k < 256; k++) {
        uint32_t c = rgb_at(im, k);

        for (j = 0; j < n && seen[j] != c; j++)
            ;
        if (j == n)
            seen[n++] = c;
        d->lab[k] = (uint8_t)j;            /* up to 256 distinct colours: fits */
    }
}

static const Known *find_known(const Known *kn, unsigned n, const Drawing *d)
{
    unsigned i;

    for (i = 0; i < n; i++)
        if (memcmp(kn[i].d.lab, d->lab, 256) == 0)
            return &kn[i];
    return NULL;
}

static bool add_vote(Vote **v, unsigned *n, unsigned *cap, uint32_t a, uint32_t b, unsigned cnt)
{
    unsigned i;

    for (i = 0; i < *n; i++)
        if ((*v)[i].a == a && (*v)[i].b == b) {
            (*v)[i].n += cnt;
            return true;
        }
    if (*n == *cap) {
        unsigned nc = *cap ? *cap * 2u : 256u;
        Vote *nv = (Vote *)realloc(*v, (size_t)nc * sizeof(Vote));

        if (nv == NULL)
            return false;
        *v = nv;
        *cap = nc;
    }
    (*v)[*n].a = a; (*v)[*n].b = b; (*v)[*n].n = cnt;
    (*n)++;
    return true;
}

/* known = {drawing: first role id} over the reference layout's primary-tileset roles (rel:430-434). */
static RgErr build_known(RgPair *ref, Known **kn, unsigned *nk)
{
    unsigned m;

    *kn = (Known *)malloc(RG_NUM_PRIMARY * sizeof(Known));
    *nk = 0;
    if (*kn == NULL)
        return RG_ERR_NOMEM;
    for (m = 0; m < RG_NUM_PRIMARY; m++) {
        RgImage im;
        Drawing d;

        if (!rg_is_alias_role(m))
            continue;
        if (!rg_cell_image(ref, (uint16_t)m, &im))
            return RG_ERR_NOMEM;
        drawing_of(&im, &d);
        rg_img_free(&im);
        if (find_known(*kn, *nk, &d) == NULL) {      /* setdefault: the first (smallest) id keeps the drawing */
            (*kn)[*nk].d = d;
            (*kn)[*nk].m = (uint16_t)m;
            (*nk)++;
        }
    }
    return RG_OK;
}

static bool is_alias_layout(uint16_t id)
{
    unsigned i;

    for (i = 0; i < 3; i++)
        if (RG_ALIAS_LAYOUTS[i] == id)
            return true;
    return false;
}

/* The most-voted b of each a, first maximal in insertion order (Counter.most_common(1)); rel:452. */
static RgErr build_colours(RgAlias *al, const Vote *v, unsigned nv)
{
    unsigned i, j;

    al->colFrom = (uint32_t *)malloc((size_t)(nv ? nv : 1u) * 4u);
    al->colTo = (uint32_t *)malloc((size_t)(nv ? nv : 1u) * 4u);
    if (al->colFrom == NULL || al->colTo == NULL)
        return RG_ERR_NOMEM;
    for (i = 0; i < nv; i++) {
        unsigned best = i;
        bool first = true;

        for (j = 0; j < i; j++)
            if (v[j].a == v[i].a)
                first = false;
        if (!first)
            continue;
        for (j = i; j < nv; j++)
            if (v[j].a == v[i].a && v[j].n > v[best].n)
                best = j;
        al->colFrom[al->nColour] = v[i].a;
        al->colTo[al->nColour] = v[best].b;
        al->nColour++;
    }
    return RG_OK;
}

/* back (rel:453-455): sorted(alias.items(), key=-used) is stable; setdefault keeps the most used own id. */
static RgErr build_back(RgAlias *al, const unsigned *used)
{
    unsigned i, j;
    uint16_t *ord = (uint16_t *)malloc((size_t)(al->nAlias ? al->nAlias : 1u) * 2u);

    al->backGen = (uint16_t *)malloc((size_t)(al->nAlias ? al->nAlias : 1u) * 2u);
    al->backOwn = (uint16_t *)malloc((size_t)(al->nAlias ? al->nAlias : 1u) * 2u);
    if (ord == NULL || al->backGen == NULL || al->backOwn == NULL) {
        free(ord);
        return RG_ERR_NOMEM;
    }
    for (i = 0; i < al->nAlias; i++) {            /* stable insertion sort by used count, descending */
        j = i;
        while (j > 0 && used[al->own[ord[j - 1]]] < used[al->own[i]]) {
            ord[j] = ord[j - 1];
            j--;
        }
        ord[j] = (uint16_t)i;
    }
    for (i = 0; i < al->nAlias; i++) {
        uint16_t g = al->gen[ord[i]];

        for (j = 0; j < al->nBack && al->backGen[j] != g; j++)
            ;
        if (j == al->nBack) {
            al->backGen[j] = g;
            al->backOwn[j] = al->own[ord[i]];
            al->nBack++;
        }
    }
    free(ord);
    for (i = 0; i < MT_COUNT; i++) {
        al->toGen[i] = (uint16_t)i;
        al->toOwn[i] = (uint16_t)i;
    }
    for (i = 0; i < al->nAlias; i++)
        al->toGen[al->own[i]] = al->gen[i];
    for (i = 0; i < al->nBack; i++)
        al->toOwn[al->backGen[i]] = al->backOwn[i];
    return RG_OK;
}

void rg_alias_free(RgAlias *a)
{
    if (a != NULL) {
        free(a->own); free(a->gen); free(a->backGen); free(a->backOwn); free(a->colFrom); free(a->colTo);
        free(a);
    }
}

/* One aliased metatile: its drawing read as the reference's, and the votes of its colours. */
static RgErr scan_used(const RgWorld *w, const RgLayout *L, RgPair *own, RgPair *ref, const Known *kn, unsigned nk,
                       RgAlias *al, Vote **votes, unsigned *nv, unsigned *vcap, unsigned *used)
{
    uint16_t order[MT_COUNT];
    unsigned nOrder = 0, i;
    int x, y;

    (void)w;
    for (y = 0; y < (int)L->h; y++)
        for (x = 0; x < (int)L->w; x++) {
            uint16_t m = rg_metatile(L, x, y);

            if (m >= MT_COUNT)
                continue;
            if (used[m]++ == 0)
                order[nOrder++] = m;
        }
    al->own = (uint16_t *)malloc((size_t)(nOrder ? nOrder : 1u) * 2u);
    al->gen = (uint16_t *)malloc((size_t)(nOrder ? nOrder : 1u) * 2u);
    if (al->own == NULL || al->gen == NULL)
        return RG_ERR_NOMEM;
    for (i = 0; i < nOrder; i++) {
        uint16_t m = order[i];
        RgImage im, gi;
        Drawing d;
        const Known *g;
        unsigned k;
        bool ok = true;

        if (m < RG_NUM_PRIMARY)
            continue;
        if (!rg_cell_image(own, m, &im))
            return RG_ERR_NOMEM;
        drawing_of(&im, &d);
        g = find_known(kn, nk, &d);
        if (g == NULL) {
            rg_img_free(&im);
            continue;
        }
        al->own[al->nAlias] = m;
        al->gen[al->nAlias] = g->m;
        al->nAlias++;
        if (!rg_cell_image(ref, g->m, &gi)) {
            rg_img_free(&im);
            return RG_ERR_NOMEM;
        }
        for (k = 0; k < 256 && ok; k++)
            ok = add_vote(votes, nv, vcap, rgb_at(&im, k), rgb_at(&gi, k), used[m]);
        rg_img_free(&im);
        rg_img_free(&gi);
        if (!ok)
            return RG_ERR_NOMEM;
    }
    return RG_OK;
}

RgErr rg_alias_of(const RgWorld *w, uint16_t layoutId, RgAlias **out)
{
    const RgLayout *L;
    RgPair *own = NULL, *ref = NULL;
    Known *kn = NULL;
    unsigned nk = 0, nv = 0, vcap = 0;
    unsigned *used = NULL;
    Vote *votes = NULL;
    RgAlias *al = NULL;
    RgErr e = RG_OK;

    assert(w != NULL && out != NULL);
    *out = NULL;
    if (!is_alias_layout(layoutId) || layoutId > w->layoutCount)
        return RG_OK;
    L = &w->layouts[layoutId - 1u];
    if (!L->present || L->ts[1]->addr != w->layouts[RG_ALIAS_LAYOUTS[0] - 1u].ts[1]->addr)
        return RG_OK;                              /* not on the Lavaridge secondary (rel:427) */
    own = rg_pair_open(w, L->pairIndex);
    ref = rg_pair_open(w, w->layouts[RG_ALIAS_REFERENCE - 1u].pairIndex);
    al = (RgAlias *)calloc(1, sizeof(RgAlias));
    used = (unsigned *)calloc(MT_COUNT, sizeof(unsigned));
    if (own == NULL || ref == NULL || al == NULL || used == NULL)
        e = RG_ERR_NOMEM;
    if (e == RG_OK)
        e = build_known(ref, &kn, &nk);
    if (e == RG_OK)
        e = scan_used(w, L, own, ref, kn, nk, al, &votes, &nv, &vcap, used);
    if (e == RG_OK && al->nAlias > 0) {            /* `if not alias: return None` (rel:449) */
        al->layoutId = layoutId;
        e = build_colours(al, votes, nv);
        if (e == RG_OK)
            e = build_back(al, used);
        if (e == RG_OK) {
            *out = al;
            al = NULL;
        }
    }
    rg_alias_free(al);
    free(votes);
    free(kn);
    free(used);
    rg_pair_close(own);
    rg_pair_close(ref);
    return e;
}

uint16_t rg_alias_metatile(const RgAlias *a, const RgLayout *L, int x, int y)
{
    uint16_t m = rg_metatile(L, x, y);

    return (a != NULL && m < MT_COUNT) ? a->toGen[m] : m;
}

uint16_t rg_alias_own_id(const RgAlias *a, uint16_t m)
{
    return (a != NULL && m < MT_COUNT) ? a->toOwn[m] : m;
}

bool rg_alias_cell_image(const RgAlias *a, RgPair *ownPair, uint16_t m, bool lowerOnly, RgImage *out16)
{
    unsigned k, i;

    if (!rg_cell_image_layers(ownPair, m, lowerOnly, out16))
        return false;
    if (a == NULL || m < RG_NUM_PRIMARY)
        return true;
    for (k = 0; k < 256; k++) {
        uint8_t *p = out16->px + 4u * k;
        uint32_t c = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];

        for (i = 0; i < a->nColour; i++)
            if (a->colFrom[i] == c) {
                p[0] = (uint8_t)(a->colTo[i] >> 16);
                p[1] = (uint8_t)(a->colTo[i] >> 8);
                p[2] = (uint8_t)a->colTo[i];
                break;
            }
    }
    return true;
}
