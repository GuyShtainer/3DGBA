/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/gen_voxel_buildings.py
 * (_inside, _inside_grid, cell_keys, same_room, reuse_pieces, register_piece, place_reused, reuse_everywhere,
 * interior_specs, build_models' bare twin, room_check), MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
#include "rg_binterior.h"
#include "rg_bexpand.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- _inside / _inside_grid (gen:327-378) ---------------------------------------------------------- */

bool rg_inside(const RgShape *s, int x, int y)
{
    double cx = x + 0.5, cy = y + 0.5;
    unsigned i, k;

    for (i = 0; i < s->n; i++) {
        const RgShapePart *p = &s->p[i];

        if (p->kind == RG_SH_ELLIPSE) {
            double a = (cx - p->v[0]) / p->v[2], b = (cy - p->v[1]) / p->v[3];

            if (a * a + b * b <= 1.0)
                return true;
        } else if (p->kind == RG_SH_RECT) {
            if (p->v[0] <= cx && cx < p->v[2] && p->v[1] <= cy && cy < p->v[3])
                return true;
        } else {
            bool hit = false;

            for (k = 0; k < p->n; k++) {
                unsigned k2 = (k + 1u) % p->n;
                double ax = p->v[2 * k], ay = p->v[2 * k + 1], bx = p->v[2 * k2], by = p->v[2 * k2 + 1];

                if ((ay > cy) != (by > cy) && cx < ax + (bx - ax) * (cy - ay) / (by - ay))
                    hit = !hit;
            }
            if (hit)
                return true;
        }
    }
    return false;
}

void rg_inside_grid(const RgShape *s, int W, int H, uint8_t *out)
{
    double mnx = 0, mxx = 0, mny = 0, mxy = 0;
    bool any = false;
    unsigned i, k;
    int x0, x1, y0, y1, x, y;

    memset(out, 0, (size_t)W * (size_t)H);
    for (i = 0; i < s->n; i++) {
        const RgShapePart *p = &s->p[i];
        double lx[8], ly[8];
        unsigned n = 0;

        if (p->kind == RG_SH_ELLIPSE) {
            lx[0] = p->v[0] - fabs(p->v[2]); lx[1] = p->v[0] + fabs(p->v[2]);
            ly[0] = p->v[1] - fabs(p->v[3]); ly[1] = p->v[1] + fabs(p->v[3]);
            n = 2;
        } else if (p->kind == RG_SH_RECT) {
            lx[0] = p->v[0]; lx[1] = p->v[2]; ly[0] = p->v[1]; ly[1] = p->v[3];
            n = 2;
        } else {
            for (k = 0; k < p->n && k < 4; k++) {
                lx[k] = p->v[2 * k];
                ly[k] = p->v[2 * k + 1];
            }
            n = k;
        }
        for (k = 0; k < n; k++) {
            if (!any || lx[k] < mnx) mnx = lx[k];
            if (!any || lx[k] > mxx) mxx = lx[k];
            if (!any || ly[k] < mny) mny = ly[k];
            if (!any || ly[k] > mxy) mxy = ly[k];
            any = true;
        }
    }
    if (!any)
        return;
    x0 = (int)floor(mnx) - 2; if (x0 < 0) x0 = 0;
    x1 = (int)ceil(mxx) + 2;  if (x1 > W) x1 = W;
    y0 = (int)floor(mny) - 2; if (y0 < 0) y0 = 0;
    y1 = (int)ceil(mxy) + 2;  if (y1 > H) y1 = H;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++)
            out[(size_t)y * (size_t)W + (size_t)x] = rg_inside(s, x, y) ? 1u : 0u;
}

const RgRoomDef *rg_room_of_layout(uint16_t layoutId)
{
    unsigned i;

    for (i = 0; i < rg_spec_count; i++)
        if (rg_specs[i].kind == RG_SPEC_INTERIOR && rg_specs[i].layoutId == layoutId)
            return (const RgRoomDef *)rg_specs[i].ext;
    return NULL;
}

/* ---- cell keys: a cell's drawing as bytes, interned (gen:407-421) -------------------------------------- */

typedef struct KeyTab {
    uint8_t (*img)[1024];               /* 16x16 RGBA per key */
    uint64_t *hash;
    int32_t *slot;                      /* open addressing: key id + 1, 0 = empty */
    unsigned n, cap, nSlot;
} KeyTab;

static uint64_t hash1024(const uint8_t *b)
{
    uint64_t h = 1469598103934665603ull;
    unsigned i;

    for (i = 0; i < 1024; i++) {
        h ^= b[i];
        h *= 1099511628211ull;
    }
    return h;
}

static bool kt_rehash(KeyTab *t, unsigned nSlot)
{
    int32_t *ns = (int32_t *)calloc(nSlot, sizeof(int32_t));
    unsigned i;

    if (ns == NULL)
        return false;
    for (i = 0; i < t->n; i++) {
        unsigned s = (unsigned)(t->hash[i] & (nSlot - 1u));

        while (ns[s] != 0)
            s = (s + 1u) & (nSlot - 1u);
        ns[s] = (int32_t)i + 1;
    }
    free(t->slot);
    t->slot = ns;
    t->nSlot = nSlot;
    return true;
}

/* Returns the key id of the 16x16 RGBA image, interning it; -1 on no memory. */
static int32_t kt_intern(KeyTab *t, const uint8_t *px)
{
    uint64_t h = hash1024(px);
    unsigned s;

    if (t->nSlot == 0 && !kt_rehash(t, 4096u))
        return -1;
    s = (unsigned)(h & (t->nSlot - 1u));
    while (t->slot[s] != 0) {
        unsigned id = (unsigned)t->slot[s] - 1u;

        if (t->hash[id] == h && memcmp(t->img[id], px, 1024) == 0)
            return (int32_t)id;
        s = (s + 1u) & (t->nSlot - 1u);
    }
    if (t->n == t->cap) {
        unsigned cap = t->cap ? t->cap * 2u : 1024u;
        uint8_t (*ni)[1024] = (uint8_t (*)[1024])realloc(t->img, (size_t)cap * 1024u);
        uint64_t *nh;

        if (ni == NULL)
            return -1;
        t->img = ni;
        nh = (uint64_t *)realloc(t->hash, (size_t)cap * sizeof(uint64_t));
        if (nh == NULL)
            return -1;
        t->hash = nh;
        t->cap = cap;
    }
    memcpy(t->img[t->n], px, 1024);
    t->hash[t->n] = h;
    t->n++;
    t->slot[s] = (int32_t)t->n;
    if (t->n * 2u > t->nSlot && !kt_rehash(t, t->nSlot * 2u))
        return -1;
    return (int32_t)(t->n - 1u);
}

/* ---- the furniture modelled so far (gen:390-462) ------------------------------------------------------ */

typedef struct RPiece {
    unsigned model;                     /* index into ms->m */
    uint16_t layout;                    /* the room it was modelled in */
    uint8_t w, h;
    uint32_t *pix;                      /* (v << 16 | u): the room pixels it draws, local to its rectangle */
    unsigned nPix;
    int32_t *keys;                      /* w*h: the cells' key ids */
    uint32_t *cellStart;                /* w*h+1: CSR into off / rgb */
    uint16_t *off;                      /* pixel index (y*16+x) in the cell */
    uint32_t *rgb;                      /* the drawing's colour there, 0xRRGGBB */
    int ai, aj;                         /* the anchor cell */
    bool loose;
} RPiece;

typedef struct Reuse {
    const RgWorld *w;
    KeyTab kt;
    int32_t *memo;                      /* (pair, metatile) -> key id + 1 */
    unsigned memoN;
    RPiece *pc;
    unsigned n, cap;
    uint16_t modelled[64];
    unsigned nModelled;
    uint32_t *stamp;                    /* per key: the generation its cached `drawn` verdict belongs to */
    uint8_t *val;
    unsigned stampCap;
    uint32_t gen;
    uint8_t *bits;                      /* per key: drawn somewhere in the room being scanned */
    RgPairCache pcache;
} Reuse;

#define RNONE (-2)
#define RREUSED (-1)

static void rpiece_free(RPiece *p)
{
    free(p->pix); free(p->keys); free(p->cellStart); free(p->off); free(p->rgb);
    memset(p, 0, sizeof(*p));
}

void rg_interior_free(RgBuildModels *ms)
{
    Reuse *R = (Reuse *)ms->reuse;
    unsigned i;

    if (R == NULL)
        return;
    for (i = 0; i < R->n; i++)
        rpiece_free(&R->pc[i]);
    free(R->pc);
    free(R->kt.img); free(R->kt.hash); free(R->kt.slot);
    free(R->memo); free(R->stamp); free(R->val); free(R->bits);
    rg_pc_close(&R->pcache);
    free(R);
    ms->reuse = NULL;
}

RgErr rg_interior_prepare(RgBuildModels *ms, const RgWorld *w, const RgSpec *specs, unsigned nSpecs)
{
    Reuse *R;
    unsigned i;
    bool any = false;

    for (i = 0; i < nSpecs; i++)
        any = any || specs[i].kind == RG_SPEC_INTERIOR;
    if (!any)
        return RG_OK;
    R = (Reuse *)calloc(1, sizeof(Reuse));
    if (R == NULL)
        return RG_ERR_NOMEM;
    R->w = w;
    R->pcache.w = w; R->pcache.p = NULL; R->pcache.idx = -1;
    R->memoN = (unsigned)w->pairCount * 1024u;
    R->memo = (int32_t *)calloc(R->memoN ? R->memoN : 1u, sizeof(int32_t));
    if (R->memo == NULL) {
        free(R);
        return RG_ERR_NOMEM;
    }
    for (i = 0; i < nSpecs; i++)
        if (specs[i].kind == RG_SPEC_INTERIOR && R->nModelled < 64u)
            R->modelled[R->nModelled++] = specs[i].layoutId;
    ms->reuse = R;
    return RG_OK;
}

/* cell_keys (gen:407-421): each cell's key id, row-major. False on no memory. */
static bool cell_keys(Reuse *R, const RgLayout *L, int32_t *out)
{
    RgPair *pair = rg_pc_get(&R->pcache, L->pairIndex);
    unsigned x, y;

    if (pair == NULL)
        return false;
    for (y = 0; y < L->h; y++) {
        for (x = 0; x < L->w; x++) {
            unsigned mt = rg_metatile(L, (int)x, (int)y) & 0x3FFu;
            size_t mi = (size_t)L->pairIndex * 1024u + mt;
            int32_t id;

            if (mi < R->memoN && R->memo[mi] != 0) {
                out[y * L->w + x] = R->memo[mi] - 1;
                continue;
            }
            {
                RgImage img;

                if (!rg_cell_image(pair, (uint16_t)mt, &img))
                    return false;
                id = kt_intern(&R->kt, img.px);
                rg_img_free(&img);
            }
            if (id < 0)
                return false;
            if (mi < R->memoN)
                R->memo[mi] = id + 1;
            out[y * L->w + x] = id;
        }
    }
    return true;
}

static bool same_room(const RgLayout *a, const RgLayout *b)
{
    size_t i, n = (size_t)a->w * a->h;

    if (a->w != b->w || a->h != b->h)
        return false;
    for (i = 0; i < n; i++)
        if ((rg_rd16(a->blocks + 2u * i) & 0x3FFu) != (rg_rd16(b->blocks + 2u * i) & 0x3FFu))
            return false;
    return true;
}

/* A cell key's pixels match a piece cell's wanted colours (gen:464-466 _cell_drawn). */
static bool cell_drawn(const Reuse *R, int32_t key, const RPiece *p, unsigned cell)
{
    const uint8_t *img = R->kt.img[key];
    unsigned k;

    for (k = p->cellStart[cell]; k < p->cellStart[cell + 1]; k++) {
        const uint8_t *px = img + (size_t)p->off[k] * 4u;

        if (((uint32_t)px[0] << 16 | (uint32_t)px[1] << 8 | px[2]) != p->rgb[k])
            return false;
    }
    return true;
}

/* gen:493 register_piece. `keys` are the room's cell keys; hidden / own are w*16 x h*16 flags (or NULL). */
static RgErr register_piece(Reuse *R, unsigned model, const RgLayout *L, const int32_t *keys, int x0, int y0,
                            unsigned w, unsigned h, const RgImage *art, const uint8_t *hidden, bool loose,
                            const uint8_t *own)
{
    RPiece p;
    unsigned u, v, i, j, nx = w * 16u, k, best = 0, bestN = 0;
    unsigned *cnt = NULL;

    memset(&p, 0, sizeof(p));
    p.pix = (uint32_t *)malloc((size_t)nx * h * 16u * sizeof(uint32_t) + sizeof(uint32_t));
    if (p.pix == NULL)
        return RG_ERR_NOMEM;
    for (v = 0; v < h * 16u; v++)
        for (u = 0; u < nx; u++) {
            size_t o = (size_t)v * nx + u;

            if (art->px[((size_t)v * (size_t)art->w + u) * 4u + 3u] >= 128 && !(hidden != NULL && hidden[o])
                && (own == NULL || !loose || own[o]))
                p.pix[p.nPix++] = (uint32_t)v << 16 | u;
        }
    if (p.nPix == 0) {
        free(p.pix);
        return RG_OK;
    }
    p.model = model;
    p.layout = L->id;
    p.w = (uint8_t)w;
    p.h = (uint8_t)h;
    p.loose = loose && p.nPix >= 64u;                   /* LOOSE_PIXELS (gen:464) */
    p.keys = (int32_t *)malloc((size_t)w * h * sizeof(int32_t));
    p.cellStart = (uint32_t *)calloc((size_t)w * h + 1u, sizeof(uint32_t));
    cnt = (unsigned *)calloc((size_t)w * h + 1u, sizeof(unsigned));
    p.off = (uint16_t *)malloc((size_t)p.nPix * sizeof(uint16_t));
    p.rgb = (uint32_t *)malloc((size_t)p.nPix * sizeof(uint32_t));
    if (p.keys == NULL || p.cellStart == NULL || cnt == NULL || p.off == NULL || p.rgb == NULL) {
        free(cnt);
        rpiece_free(&p);
        return RG_ERR_NOMEM;
    }
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            p.keys[j * w + i] = keys[(unsigned)(y0 + (int)j) * L->w + (unsigned)(x0 + (int)i)];
    for (k = 0; k < p.nPix; k++)
        cnt[((p.pix[k] >> 16) / 16u) * w + (p.pix[k] & 0xFFFFu) / 16u]++;
    for (i = 0; i < w * h; i++)
        p.cellStart[i + 1] = p.cellStart[i] + cnt[i];
    memset(cnt, 0, ((size_t)w * h + 1u) * sizeof(unsigned));
    for (k = 0; k < p.nPix; k++) {
        unsigned pv = p.pix[k] >> 16, pu = p.pix[k] & 0xFFFFu, cell = (pv / 16u) * w + pu / 16u;
        unsigned slot = p.cellStart[cell] + cnt[cell]++;
        unsigned pxi = (pv % 16u) * 16u + pu % 16u;
        const uint8_t *kp = R->kt.img[p.keys[cell]] + (size_t)pxi * 4u;

        p.off[slot] = (uint16_t)pxi;
        p.rgb[slot] = (uint32_t)kp[0] << 16 | (uint32_t)kp[1] << 8 | kp[2];
    }
    for (i = 0; i < w * h; i++)                         /* the anchor: the cell with the most pixels, first in row-major */
        if (cnt[i] > bestN) {
            bestN = cnt[i];
            best = i;
        }
    p.ai = (int)(best % w);
    p.aj = (int)(best / w);
    free(cnt);
    if (R->n == R->cap) {
        unsigned cap = R->cap ? R->cap * 2u : 64u;
        RPiece *np = (RPiece *)realloc(R->pc, (size_t)cap * sizeof(RPiece));

        if (np == NULL) {
            rpiece_free(&p);
            return RG_ERR_NOMEM;
        }
        R->pc = np;
        R->cap = cap;
    }
    R->pc[R->n++] = p;
    return RG_OK;
}

/* ---- reuse_pieces / place_reused (gen:424-539) --------------------------------------------------------- */

typedef struct Placed { unsigned piece; int x, y; bool exact; } Placed;

static bool ensure_stamp(Reuse *R)
{
    if (R->stampCap < R->kt.n) {
        unsigned cap = R->kt.n + 1024u;
        uint32_t *ns = (uint32_t *)calloc(cap, sizeof(uint32_t));
        uint8_t *nv = (uint8_t *)calloc(cap, 1);
        uint8_t *nb = (uint8_t *)calloc(cap, 1);

        if (ns == NULL || nv == NULL || nb == NULL) {
            free(ns); free(nv); free(nb);
            return false;
        }
        free(R->stamp); free(R->val); free(R->bits);
        R->stamp = ns; R->val = nv; R->bits = nb;
        R->stampCap = cap;
        R->gen = 0;
    }
    return true;
}

/* All of piece p's cells are drawn at (x, y) (gen:529-531), the anchor first. */
static bool drawn_at(const Reuse *R, const RPiece *p, const int32_t *keys, unsigned lw, int x, int y)
{
    unsigned i, j;

    for (j = 0; j < p->h; j++)
        for (i = 0; i < p->w; i++) {
            unsigned c = j * p->w + i;

            if (p->cellStart[c + 1] == p->cellStart[c])
                continue;
            if (!cell_drawn(R, keys[(unsigned)(y + (int)j) * lw + (unsigned)(x + (int)i)], p, c))
                return false;
        }
    return true;
}

static bool exact_at(const RPiece *p, const int32_t *keys, unsigned lw, int x, int y)
{
    unsigned i, j;

    for (j = 0; j < p->h; j++)
        for (i = 0; i < p->w; i++)
            if (keys[(unsigned)(y + (int)j) * lw + (unsigned)(x + (int)i)] != p->keys[j * p->w + i])
                return false;
    return true;
}

static RgErr push_placed(Placed **arr, unsigned *n, unsigned *cap, const Placed *pl)
{
    if (*n == *cap) {
        unsigned nc = *cap ? *cap * 2u : 16u;
        Placed *np = (Placed *)realloc(*arr, (size_t)nc * sizeof(Placed));

        if (np == NULL)
            return RG_ERR_NOMEM;
        *arr = np;
        *cap = nc;
    }
    (*arr)[(*n)++] = *pl;
    return RG_OK;
}

/* One scan of layout L for every piece modelled so far; fills `placed` in upstream order. */
static RgErr reuse_pieces(Reuse *R, const RgLayout *L, const int32_t *keys, Placed **placed, unsigned *nPlaced)
{
    const unsigned lw = L->w, lh = L->h;
    const size_t PW = (size_t)lw * 16u;
    uint8_t *taken = (uint8_t *)calloc(PW * lh * 16u + 1u, 1);
    unsigned *cand = (unsigned *)malloc((size_t)(R->n + 1u) * sizeof(unsigned));
    unsigned nCand = 0, cap = 0, i, pass;
    RgErr err = RG_OK;

    *placed = NULL;
    *nPlaced = 0;
    if (taken == NULL || cand == NULL) {
        free(taken); free(cand);
        return RG_ERR_NOMEM;
    }
    if (!ensure_stamp(R)) {
        free(taken); free(cand);
        return RG_ERR_NOMEM;
    }
    memset(R->bits, 0, R->stampCap);
    for (i = 0; i < lw * lh; i++)
        R->bits[keys[i]] = 1;
    for (i = 0; i < R->n; i++) {
        const RPiece *p = &R->pc[i];

        if (p->layout == L->id)
            continue;
        if (same_room(&R->w->layouts[p->layout - 1], L))     /* the same room: its own pieces stand in it already */
            continue;
        cand[nCand++] = i;
    }
    for (pass = 0; pass < 2 && err == RG_OK; pass++) {
        bool exact = pass == 0;
        unsigned c;

        for (c = 0; c < nCand && err == RG_OK; c++) {
            const RPiece *p = &R->pc[cand[c]];
            int x, y;

            if (!exact && !p->loose)
                continue;
            if (exact && !R->bits[p->keys[0]])
                continue;                                    /* its first cell is drawn nowhere here */
            R->gen++;
            for (y = 0; y + (int)p->h <= (int)lh && err == RG_OK; y++) {
                for (x = 0; x + (int)p->w <= (int)lw && err == RG_OK; x++) {
                    unsigned k;
                    bool clash = false;
                    Placed pl;

                    if (exact) {
                        if (keys[(unsigned)y * lw + (unsigned)x] != p->keys[0] || !exact_at(p, keys, lw, x, y))
                            continue;
                    } else {
                        int32_t ak = keys[(unsigned)(y + p->aj) * lw + (unsigned)(x + p->ai)];

                        if (R->stamp[ak] != R->gen) {        /* _loose_starts: the anchor cell's pixels drawn here */
                            R->stamp[ak] = R->gen;
                            R->val[ak] = cell_drawn(R, ak, p, (unsigned)p->aj * p->w + (unsigned)p->ai) ? 1u : 0u;
                        }
                        if (!R->val[ak] || !drawn_at(R, p, keys, lw, x, y))
                            continue;
                    }
                    for (k = 0; k < p->nPix && !clash; k++) {
                        unsigned u = p->pix[k] & 0xFFFFu, v = p->pix[k] >> 16;

                        clash = taken[((size_t)y * 16u + v) * PW + (size_t)x * 16u + u] != 0;
                    }
                    if (clash)
                        continue;
                    for (k = 0; k < p->nPix; k++) {
                        unsigned u = p->pix[k] & 0xFFFFu, v = p->pix[k] >> 16;

                        taken[((size_t)y * 16u + v) * PW + (size_t)x * 16u + u] = 1;
                    }
                    pl.piece = cand[c]; pl.x = x; pl.y = y; pl.exact = exact;
                    err = push_placed(placed, nPlaced, &cap, &pl);
                }
            }
        }
    }
    free(taken);
    free(cand);
    if (err != RG_OK) {
        free(*placed);
        *placed = NULL;
        *nPlaced = 0;
    }
    return err;
}

static RgErr model_add_reused(RgBuildModel *m, const RgReuseAt *r)
{
    RgReuseAt *nr = (RgReuseAt *)realloc(m->reused, (size_t)(m->nReused + 1u) * sizeof(RgReuseAt));

    if (nr == NULL)
        return RG_ERR_NOMEM;
    m->reused = nr;
    m->reused[m->nReused++] = *r;
    return RG_OK;
}

static RgErr model_add_bare(RgBuildModel *m, uint16_t lid, int x, int y)
{
    uint16_t (*nb)[3] = (uint16_t (*)[3])realloc(m->bareAt, (size_t)(m->nBareAt + 1u) * sizeof(uint16_t[3]));

    if (nb == NULL)
        return RG_ERR_NOMEM;
    m->bareAt = nb;
    m->bareAt[m->nBareAt][0] = lid;
    m->bareAt[m->nBareAt][1] = (uint16_t)x;
    m->bareAt[m->nBareAt][2] = (uint16_t)y;
    m->nBareAt++;
    return RG_OK;
}

/* gen:496-517 place_reused: stands the known furniture in L. `hasGround` false = a room nobody modelled. Marks the
 * pixels it draws in reusedPx (L->w*16 x L->h*16 flags) when that is given. *nOut = placements. */
static RgErr place_reused(Reuse *R, RgBuildModels *ms, const RgLayout *L, const int32_t *keys, bool hasGround,
                          uint16_t ground, uint8_t *reusedPx, unsigned *nOut)
{
    Placed *placed;
    unsigned n, i, k;
    RgErr err = reuse_pieces(R, L, keys, &placed, &n);

    *nOut = 0;
    for (i = 0; i < n && err == RG_OK; i++) {
        const RPiece *p = &R->pc[placed[i].piece];
        RgBuildModel *m = &ms->m[p->model];

        if (placed[i].exact) {
            RgReuseAt r;

            r.lid = L->id; r.x = (int16_t)placed[i].x; r.y = (int16_t)placed[i].y;
            r.noGround = !hasGround;
            r.ground = hasGround ? ground : 0;
            err = model_add_reused(m, &r);
            ms->nReuseExact++;
        } else {
            err = model_add_bare(m, L->id, placed[i].x, placed[i].y);
            ms->nReuseBare++;
        }
        if (reusedPx != NULL)
            for (k = 0; k < p->nPix; k++) {
                unsigned u = p->pix[k] & 0xFFFFu, v = p->pix[k] >> 16;

                reusedPx[((size_t)placed[i].y * 16u + v) * ((size_t)L->w * 16u) + (size_t)placed[i].x * 16u + u] = 1;
            }
    }
    *nOut = n;
    free(placed);
    return err;
}

/* ---- interior_specs (gen:559-874) ---------------------------------------------------------------------- */

typedef struct FillPx { uint16_t x, y; uint8_t c[4]; } FillPx;

typedef struct Room {
    const RgWorld *w;
    Reuse *R;
    RgBuildModels *ms;
    const RgSpec *s;
    const RgRoomDef *def;
    const RgLayout *L;
    RgPair *pair;
    int W, H;
    RgImage full, filled;
    RgPieceList pl;
    unsigned n;
    size_t px;                          /* W * H */
    uint8_t *match, *ground, *opened, *grids, *left, *seen, *isWall, *reusedPx;
    int16_t *owner, *decalOf;
    uint32_t *shade, *stack, *leaveSet;
    unsigned nShade;
    int32_t *keys;
    FillPx **fill;
    unsigned *nFill;
    int (*rect)[4];
} Room;

static int cmp_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;

    return x < y ? -1 : (x > y ? 1 : 0);
}

static bool in_set(const uint32_t *set, unsigned n, uint32_t v)
{
    unsigned lo = 0, hi = n;

    while (lo < hi) {
        unsigned mid = (lo + hi) / 2u;

        if (set[mid] == v)
            return true;
        if (set[mid] < v)
            lo = mid + 1u;
        else
            hi = mid;
    }
    return false;
}

static unsigned sort_unique(uint32_t *a, unsigned n)
{
    unsigned i, m = 0;

    qsort(a, n, sizeof(uint32_t), cmp_u32);
    for (i = 0; i < n; i++)
        if (m == 0 || a[m - 1] != a[i])
            a[m++] = a[i];
    return m;
}

static const uint8_t *fpx(const Room *c, int x, int y)
{
    return c->full.px + ((size_t)y * (size_t)c->W + (size_t)x) * 4u;
}

static uint32_t rgb_at(const Room *c, int x, int y)
{
    const uint8_t *p = fpx(c, x, y);

    return (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
}

static void room_free(Room *c)
{
    unsigned k;

    rg_img_free(&c->full);
    rg_img_free(&c->filled);
    rg_pl_free(&c->pl);
    free(c->match); free(c->ground); free(c->opened); free(c->grids); free(c->left); free(c->seen);
    free(c->isWall); free(c->reusedPx); free(c->owner); free(c->decalOf); free(c->shade); free(c->stack);
    free(c->leaveSet); free(c->keys); free(c->nFill); free(c->rect);
    if (c->fill != NULL)
        for (k = 0; k < c->n; k++)
            free(c->fill[k]);
    free(c->fill);
}

/* The room's image, the floor match mask, every piece's shape grid and the ground flood (gen:579-620). */
static RgErr room_setup(Room *c)
{
    const RgLayout *L = c->L;
    unsigned x, y, k, i;
    uint32_t *sh;
    RgImage floors[4];
    unsigned nFloor = 0;
    RgErr err = RG_ERR_NOMEM;

    memset(floors, 0, sizeof(floors));
    c->W = (int)L->w * 16;
    c->H = (int)L->h * 16;
    c->px = (size_t)c->W * (size_t)c->H;
    c->pair = rg_pc_get(&c->R->pcache, L->pairIndex);
    if (c->pair == NULL)
        return RG_ERR_NOMEM;
    if (!rg_img_new(&c->full, c->W, c->H) || !rg_img_new(&c->filled, c->W, c->H))
        return RG_ERR_NOMEM;
    for (y = 0; y < L->h; y++)
        for (x = 0; x < L->w; x++) {
            RgImage cell;

            if (!rg_cell_image(c->pair, rg_metatile(L, (int)x, (int)y), &cell))
                return RG_ERR_NOMEM;
            rg_img_paste(&c->full, &cell, (int)x * 16, (int)y * 16);
            rg_img_free(&cell);
        }
    c->n = c->pl.n;
    c->match = (uint8_t *)calloc(c->px, 1);
    c->ground = (uint8_t *)calloc(c->px, 1);
    c->opened = (uint8_t *)calloc(c->px, 1);
    c->left = (uint8_t *)calloc(c->px, 1);
    c->seen = (uint8_t *)calloc(c->px, 1);
    c->grids = (uint8_t *)calloc(c->px * (c->n ? c->n : 1u), 1);
    c->owner = (int16_t *)malloc(c->px * sizeof(int16_t));
    c->stack = (uint32_t *)malloc(c->px * sizeof(uint32_t) + 4u);
    c->isWall = (uint8_t *)calloc(c->n + 1u, 1);
    c->reusedPx = (uint8_t *)calloc(c->px, 1);
    c->keys = (int32_t *)malloc((size_t)L->w * L->h * sizeof(int32_t));
    c->fill = (FillPx **)calloc(c->n + 1u, sizeof(FillPx *));
    c->nFill = (unsigned *)calloc(c->n + 1u, sizeof(unsigned));
    c->rect = (int (*)[4])calloc(c->n + 1u, sizeof(int[4]));
    c->decalOf = (int16_t *)malloc((size_t)L->w * L->h * sizeof(int16_t));
    if (!c->match || !c->ground || !c->opened || !c->left || !c->seen || !c->grids || !c->owner || !c->stack
        || !c->isWall || !c->reusedPx || !c->keys || !c->fill || !c->nFill || !c->rect || !c->decalOf)
        return RG_ERR_NOMEM;
    if (!cell_keys(c->R, L, c->keys))
        return RG_ERR_NOMEM;
    for (i = 0; i < c->px; i++)
        c->owner[i] = RNONE;
    for (i = 0; i < (unsigned)L->w * L->h; i++)
        c->decalOf[i] = -1;
    for (i = 0; i < c->s->nGround && nFloor < 4u; i++, nFloor++)
        if (!rg_cell_image(c->pair, c->s->ground[i], &floors[nFloor]))
            goto out;
    for (y = 0; y < (unsigned)c->H; y++)
        for (x = 0; x < (unsigned)c->W; x++) {
            for (i = 0; i < nFloor; i++)
                if (memcmp(floors[i].px + ((size_t)(y % 16u) * 16u + x % 16u) * 4u,
                           fpx(c, (int)x, (int)y), 4) == 0) {
                    c->match[(size_t)y * (size_t)c->W + x] = 1;
                    break;
                }
        }
    for (k = 0; k < c->n; k++)
        rg_inside_grid(&c->pl.p[k].shape, c->W, c->H, c->grids + (size_t)k * c->px);
    for (i = 0; i < c->px; i++) {
        bool shaped = false;

        for (k = 0; k < c->n && !shaped; k++)
            shaped = c->grids[(size_t)k * c->px + i] != 0;
        c->ground[i] = (uint8_t)(c->match[i] && !shaped);
    }
    rg_inside_grid(&c->def->open, c->W, c->H, c->opened);
    {   /* the ground flood (gen:596-603): reachability, so any order */
        size_t sp = 0;

        for (i = 0; i < c->px; i++)
            if (c->ground[i])
                c->stack[sp++] = (uint32_t)i;
        while (sp > 0) {
            uint32_t cur = c->stack[--sp];
            int x0 = (int)(cur % (unsigned)c->W), y0 = (int)(cur / (unsigned)c->W), d;
            static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};

            for (d = 0; d < 4; d++) {
                int nx = x0 + dx[d], ny = y0 + dy[d];
                size_t ni;

                if (nx < 0 || ny < 0 || nx >= c->W || ny >= c->H)
                    continue;
                ni = (size_t)ny * (size_t)c->W + (size_t)nx;
                if (c->match[ni] && !c->ground[ni]) {
                    c->ground[ni] = 1;
                    c->stack[sp++] = (uint32_t)ni;
                }
            }
        }
    }
    sh = (uint32_t *)malloc((size_t)(c->def->nShade ? c->def->nShade : 1u) * 256u * sizeof(uint32_t));
    if (sh == NULL)
        goto out;
    c->shade = sh;
    for (i = 0; i < c->def->nShade; i++) {                  /* the floor's shaded variants (gen:605-608) */
        RgImage cell;
        unsigned q;

        if (!rg_cell_image(c->pair, c->def->shade[i], &cell))
            goto out;
        for (q = 0; q < 256u; q++)
            c->shade[c->nShade++] = (uint32_t)cell.px[q * 4u] << 16 | (uint32_t)cell.px[q * 4u + 1u] << 8
                                    | cell.px[q * 4u + 2u];
        rg_img_free(&cell);
    }
    c->nShade = sort_unique(c->shade, c->nShade);
    err = RG_OK;
out:
    for (i = 0; i < nFloor; i++)
        rg_img_free(&floors[i]);
    return err;
}

/* Which pixels each piece owns (gen:609-654). */
static RgErr room_owner(Room *c)
{
    unsigned n = 0, k, i;
    int x, y;
    RgErr err = place_reused(c->R, c->ms, c->L, c->keys, true, c->s->ground[0], c->reusedPx, &n);

    if (err != RG_OK)
        return err;
    for (i = 0; i < c->px; i++)
        if (c->reusedPx[i])
            c->owner[i] = RREUSED;
    c->leaveSet = (uint32_t *)malloc(((size_t)c->nShade + RG_PC_LEAVE + 1u) * sizeof(uint32_t));
    if (c->leaveSet == NULL)
        return RG_ERR_NOMEM;
    for (k = 0; k < c->n; k++) {
        const RgPieceDef *pc = &c->pl.p[k];
        const uint8_t *inside = c->grids + (size_t)k * c->px;
        unsigned nl = 0;
        size_t sp = 0;

        /* what the piece leaves: its `leave` colours where they run on out of its shape and, but for a wall, the
         * floor's shadows */
        for (i = 0; i < pc->nLeave; i++)
            c->leaveSet[nl++] = pc->leave[i];
        if (!(pc->fill || pc->hasFacet))
            for (i = 0; i < c->nShade; i++)
                c->leaveSet[nl++] = c->shade[i];
        nl = sort_unique(c->leaveSet, nl);
        memset(c->left, 0, c->px);
        if (nl > 0) {
            memset(c->seen, 0, c->px);
            for (i = 0; i < c->px; i++)
                if (!inside[i] && in_set(c->leaveSet, nl, rgb_at(c, (int)(i % (unsigned)c->W), (int)(i / (unsigned)c->W)))) {
                    c->seen[i] = 1;
                    c->stack[sp++] = (uint32_t)i;
                }
            while (sp > 0) {
                uint32_t cur = c->stack[--sp];
                int x0 = (int)(cur % (unsigned)c->W), y0 = (int)(cur / (unsigned)c->W), d;
                static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};

                for (d = 0; d < 4; d++) {
                    int nx = x0 + dx[d], ny = y0 + dy[d];
                    size_t ni;

                    if (nx < 0 || ny < 0 || nx >= c->W || ny >= c->H)
                        continue;
                    ni = (size_t)ny * (size_t)c->W + (size_t)nx;
                    if (!c->seen[ni] && in_set(c->leaveSet, nl, rgb_at(c, nx, ny))) {
                        c->seen[ni] = 1;
                        c->stack[sp++] = (uint32_t)ni;
                        if (inside[ni])
                            c->left[ni] = 1;
                    }
                }
            }
        }
        for (y = 0; y < c->H; y++)
            for (x = 0; x < c->W; x++) {
                size_t o = (size_t)y * (size_t)c->W + (size_t)x;

                if (c->owner[o] != RNONE || !inside[o] || c->left[o])
                    continue;
                if (!c->ground[o] || pc->fill || pc->hasFacet
                    || (pc->hasClaim && x >= pc->claim[0] && x < pc->claim[2] && y >= pc->claim[1] && y < pc->claim[3]))
                    c->owner[o] = (int16_t)k;
            }
    }
    return RG_OK;
}

/* What the pieces in front hide of a wall: its own pixels `period` columns away (gen:667-682). */
static bool fill_source(const Room *c, int k, int x, int y, int period, int *src)
{
    int d, s;

    for (d = period; d < c->W; d += period)
        for (s = 0; s < 2; s++) {
            int xx = s == 0 ? x - d : x + d;
            int ow;

            if (xx < 0 || xx >= c->W)
                continue;
            ow = c->owner[(size_t)y * (size_t)c->W + (size_t)xx];
            if (ow >= 0 && c->isWall[ow]) {
                (void)k;
                *src = xx;
                return true;
            }
        }
    return false;
}

static RgErr fill_add(Room *c, unsigned k, int x, int y, const uint8_t *rgba)
{
    unsigned n = c->nFill[k];
    FillPx *f;

    if (n == 0 || (n >= 64u && (n & (n - 1u)) == 0)) {
        f = (FillPx *)realloc(c->fill[k], (size_t)(n ? n * 2u : 64u) * sizeof(FillPx));
        if (f == NULL)
            return RG_ERR_NOMEM;
        c->fill[k] = f;
    }
    f = &c->fill[k][n];
    f->x = (uint16_t)x;
    f->y = (uint16_t)y;
    memcpy(f->c, rgba, 4);
    c->nFill[k] = n + 1u;
    return RG_OK;
}

/* gen:660-690: the fill dict of each piece, then `filled` (the room with the hidden faces dressed in). */
static RgErr room_fills(Room *c)
{
    unsigned k, i;
    int x, y;
    RgErr err = RG_OK;

    for (k = 0; k < c->n; k++)
        c->isWall[k] = c->pl.p[k].fill != 0;
    for (k = 0; k < c->n && err == RG_OK; k++) {
        int period = c->pl.p[k].fill;
        const uint8_t *inside = c->grids + (size_t)k * c->px;

        if (!period)
            continue;
        for (y = 0; y < c->H && err == RG_OK; y++)
            for (x = 0; x < c->W && err == RG_OK; x++) {
                size_t o = (size_t)y * (size_t)c->W + (size_t)x;
                int src;

                if (c->owner[o] == RNONE || c->owner[o] >= (int)k || !inside[o])
                    continue;
                if (fill_source(c, (int)k, x, y, period, &src))
                    err = fill_add(c, k, x, y, fpx(c, src, y));
            }
    }
    if (err != RG_OK)
        return err;
    rg_img_paste(&c->filled, &c->full, 0, 0);
    for (k = 0; k < c->n; k++)
        for (i = 0; i < c->nFill[k]; i++) {
            const FillPx *f = &c->fill[k][i];

            memcpy(c->filled.px + ((size_t)f->y * (size_t)c->W + f->x) * 4u, f->c, 4);
        }
    return RG_OK;
}

typedef struct BBox { int x0, y0, x1, y1; } BBox;

static void bb_add(BBox *b, int x, int y)
{
    if (x < b->x0) b->x0 = x;
    if (x > b->x1) b->x1 = x;
    if (y < b->y0) b->y0 = y;
    if (y > b->y1) b->y1 = y;
}

static int clampi(int v, int hi)
{
    return v < hi ? v : hi;
}

/* gen:691-712: the cells each piece answers for. rect[k] = x0, y0, x1, y1 (half open) in cells. */
static RgErr room_rects(Room *c)
{
    BBox *bb = (BBox *)malloc((size_t)(c->n ? c->n : 1u) * sizeof(BBox));
    unsigned *nMine = (unsigned *)calloc(c->n ? c->n : 1u, sizeof(unsigned));
    unsigned k, i;
    size_t o;
    RgErr err = RG_ERR_NOMEM;

    if (bb == NULL || nMine == NULL)
        goto out;
    for (k = 0; k < c->n; k++) {
        bb[k].x0 = bb[k].y0 = 1 << 30;
        bb[k].x1 = bb[k].y1 = -(1 << 30);
    }
    for (o = 0; o < c->px; o++)
        if (c->owner[o] >= 0) {
            bb_add(&bb[c->owner[o]], (int)(o % (size_t)c->W), (int)(o / (size_t)c->W));
            nMine[c->owner[o]]++;
        }
    err = RG_OK;
    for (k = 0; k < c->n; k++) {
        const RgPieceDef *pc = &c->pl.p[k];

        for (i = 0; i < c->nFill[k]; i++)
            bb_add(&bb[k], c->fill[k][i].x, c->fill[k][i].y);
        if (pc->hasFacet) {
            bb_add(&bb[k], (int)pc->facet[0][0], (int)pc->facet[0][2] - 1);
            bb_add(&bb[k], (int)pc->facet[1][0] - 1, (int)pc->facet[1][2] - 1);
        }
        for (i = 0; i < pc->nWalls; i++) {
            bb_add(&bb[k], clampi((int)pc->walls[i].a[0], c->W - 1), clampi((int)pc->walls[i].a[1], c->H - 1));
            bb_add(&bb[k], clampi((int)pc->walls[i].b[0], c->W - 1), clampi((int)pc->walls[i].b[1], c->H - 1));
        }
        for (i = 0; i < pc->nCells; i++)
            bb_add(&bb[k], pc->cells[i][0] * 16, pc->cells[i][1] * 16);
        if ((nMine[k] == 0 && pc->nWalls == 0) || bb[k].x1 < bb[k].x0) {
            snprintf(c->ms->errPiece, sizeof(c->ms->errPiece), "%s_%s", c->s->name, pc->name);
            err = RG_ERR_BUILDINGS;
            break;
        }
        c->rect[k][0] = rg_floordiv(bb[k].x0, 16);
        c->rect[k][1] = rg_floordiv(bb[k].y0, 16);
        c->rect[k][2] = rg_floordiv(bb[k].x1, 16) + 1;
        c->rect[k][3] = rg_floordiv(bb[k].y1, 16) + 1;
        if (c->rect[k][0] < 0 || c->rect[k][1] < 0 || c->rect[k][2] > (int)c->L->w || c->rect[k][3] > (int)c->L->h) {
            snprintf(c->ms->errPiece, sizeof(c->ms->errPiece), "%s_%s rect", c->s->name, pc->name);
            err = RG_ERR_BUILDINGS;
            break;
        }
    }
    if (err == RG_OK) {                                 /* decal_of: the first piece (in order) whose rect holds the cell */
        for (k = 0; k < c->n; k++) {
            int cx, cy;

            for (cy = c->rect[k][1]; cy < c->rect[k][3]; cy++)
                for (cx = c->rect[k][0]; cx < c->rect[k][2]; cx++)
                    if (c->decalOf[(size_t)cy * c->L->w + (size_t)cx] < 0)
                        c->decalOf[(size_t)cy * c->L->w + (size_t)cx] = (int16_t)k;
        }
    }
out:
    free(bb);
    free(nMine);
    return err;
}

/* gen:713-725: the floor mark a piece hides at (x, y): the same mark where the row runs out of the piece both ways. */
static bool mark_under(const Room *c, int x, int y, uint8_t out[4])
{
    uint8_t found[2][4];
    int s;

    for (s = 0; s < 2; s++) {
        int step = s == 0 ? -1 : 1, xx = x;

        while (xx >= 0 && xx < c->W && c->owner[(size_t)y * (size_t)c->W + (size_t)xx] != RNONE)
            xx += step;
        if (xx < 0 || xx >= c->W || c->ground[(size_t)y * (size_t)c->W + (size_t)xx]
            || c->opened[(size_t)y * (size_t)c->W + (size_t)xx])
            return false;
        memcpy(found[s], fpx(c, xx, y), 4);
    }
    if (memcmp(found[0], found[1], 4) != 0)
        return false;
    memcpy(out, found[0], 4);
    return true;
}

/* ---- one piece: art, parts, mesh, model (gen:726-874) ---------------------------------------------------- */

typedef struct PB {
    int x0, y0, w, h, pw, ph, sh, sw, th, tw, ch, uo;
    uint8_t *own, *hidden;
    RgImage obj, art;
    int16_t (*cells)[2];
    int16_t (*loc)[2];
    int16_t (*hid)[2];
    unsigned nCells, nLoc, nHid, nMine;
} PB;

static void pb_free(PB *b)
{
    rg_img_free(&b->obj);
    rg_img_free(&b->art);
    free(b->own); free(b->hidden); free(b->cells); free(b->loc); free(b->hid);
    memset(b, 0, sizeof(*b));
}

static void put_px(RgImage *im, int x, int y, const uint8_t *rgba)
{
    if (x >= 0 && y >= 0 && x < im->w && y < im->h)
        memcpy(im->px + ((size_t)y * (size_t)im->w + (size_t)x) * 4u, rgba, 4);
}

static bool region_any(const RgImage *im, int x, int y)
{
    int i, j;

    for (j = 0; j < 16; j++)
        for (i = 0; i < 16; i++)
            if (im->px[((size_t)(y + j) * (size_t)im->w + (size_t)(x + i)) * 4u + 3u] != 0)
                return true;
    return false;
}

/* The piece's own pixels, the fill positions, its cells and the obj image (gen:731-745, 776-778). */
static RgErr pb_masks(const Room *c, unsigned k, PB *b)
{
    unsigned i;
    int x, y, cx, cy;
    size_t n;

    b->x0 = c->rect[k][0]; b->y0 = c->rect[k][1];
    b->w = c->rect[k][2] - b->x0; b->h = c->rect[k][3] - b->y0;
    b->pw = b->w * 16; b->ph = b->h * 16;
    n = (size_t)b->pw * (size_t)b->ph;
    b->own = (uint8_t *)calloc(n, 1);
    b->hidden = (uint8_t *)calloc(n, 1);
    b->cells = (int16_t (*)[2])malloc((size_t)(b->w * b->h) * sizeof(int16_t[2]));
    b->loc = (int16_t (*)[2])malloc((size_t)(b->w * b->h) * sizeof(int16_t[2]));
    b->hid = (int16_t (*)[2])malloc((size_t)(b->w * b->h) * sizeof(int16_t[2]));
    if (!b->own || !b->hidden || !b->cells || !b->loc || !b->hid || !rg_img_new(&b->obj, b->pw, b->ph))
        return RG_ERR_NOMEM;
    for (y = 0; y < b->ph; y++)
        for (x = 0; x < b->pw; x++)
            if (c->owner[(size_t)(y + b->y0 * 16) * (size_t)c->W + (size_t)(x + b->x0 * 16)] == (int)k) {
                b->own[(size_t)y * (size_t)b->pw + (size_t)x] = 1;
                b->nMine++;
                put_px(&b->obj, x, y, fpx(c, x + b->x0 * 16, y + b->y0 * 16));
            }
    for (i = 0; i < c->nFill[k]; i++) {
        const FillPx *f = &c->fill[k][i];
        int lx = f->x - b->x0 * 16, ly = f->y - b->y0 * 16;

        if (lx < 0 || ly < 0 || lx >= b->pw || ly >= b->ph)
            return RG_ERR_BUILDINGS;
        b->hidden[(size_t)ly * (size_t)b->pw + (size_t)lx] = 1;
        put_px(&b->obj, lx, ly, f->c);
    }
    for (cx = b->x0; cx < c->rect[k][2]; cx++)          /* sorted(cells): (cx, cy) order */
        for (cy = b->y0; cy < c->rect[k][3]; cy++)
            if (c->decalOf[(size_t)cy * c->L->w + (size_t)cx] == (int16_t)k) {
                b->cells[b->nCells][0] = (int16_t)cx;
                b->cells[b->nCells][1] = (int16_t)cy;
                b->nCells++;
            }
    return RG_OK;
}

/* A stretch of the room with the hidden faces filled, the piece's own pixels kept (gen:753-767). */
static RgErr pb_sample(const Room *c, unsigned k, PB *b, const int r[4], int dstY, bool keepMine)
{
    RgImage crop;
    int x, y;

    if (!rg_img_crop(&c->filled, r, &crop))
        return RG_ERR_BUILDINGS;
    if (keepMine)
        for (y = r[1]; y < r[3]; y++)
            for (x = r[0]; x < r[2]; x++)
                if (c->owner[(size_t)y * (size_t)c->W + (size_t)x] == (int)k)
                    put_px(&crop, x - r[0], y - r[1], fpx(c, x, y));
    {
        uint8_t *p = crop.px;
        size_t i;

        for (i = 0; i < (size_t)crop.w * (size_t)crop.h; i++)
            p[i * 4u + 3u] = 255;
    }
    rg_img_paste(&b->art, &crop, 0, dstY);
    rg_img_free(&crop);
    return RG_OK;
}

/* The art: [obj][marks][side][top][card][under] (gen:743-799). */
static RgErr pb_art(const Room *c, unsigned k, PB *b)
{
    const RgPieceDef *pc = &c->pl.p[k];
    unsigned i;
    int cw;
    RgErr err;

    b->sh = pc->hasSide ? pc->side[3] - pc->side[1] : 0;
    b->sw = pc->hasSide ? pc->side[2] - pc->side[0] : 0;
    b->th = pc->hasTop ? pc->top[3] - pc->top[1] : 0;
    b->tw = pc->hasTop ? pc->top[2] - pc->top[0] : 0;
    b->ch = pc->card ? b->ph : 0;
    b->uo = 2 * b->ph + b->sh + b->th + b->ch;
    cw = b->pw > b->sw ? b->pw : b->sw;
    cw = cw > b->tw ? cw : b->tw;
    if (!rg_img_new(&b->art, cw, b->uo + b->ph))
        return RG_ERR_NOMEM;
    if (pc->hasSide && (err = pb_sample(c, k, b, pc->side, 2 * b->ph, true)) != RG_OK)
        return err;
    if (pc->hasTop && (err = pb_sample(c, k, b, pc->top, 2 * b->ph + b->sh, false)) != RG_OK)
        return err;
    rg_img_paste(&b->art, &b->obj, 0, 0);
    for (i = 0; i < b->nCells; i++) {
        int cx = b->cells[i][0], cy = b->cells[i][1], jj, ii;

        for (jj = 0; jj < 16; jj++)
            for (ii = 0; ii < 16; ii++) {
                int x = cx * 16 + ii, y = cy * 16 + jj, lx = x - b->x0 * 16, ly = y - b->y0 * 16;
                size_t o = (size_t)y * (size_t)c->W + (size_t)x;
                uint8_t under[4];

                if (!c->ground[o] && c->owner[o] == RNONE && !c->opened[o]) {
                    put_px(&b->art, lx, ly, fpx(c, x, y));
                    put_px(&b->art, lx, ly + b->ph, fpx(c, x, y));
                } else if (c->owner[o] >= 0 && !c->pl.p[c->owner[o]].card && mark_under(c, x, y, under)) {
                    put_px(&b->art, lx, ly + b->uo, under);
                }
            }
    }
    return RG_OK;
}

static RgPart *part_new(RgPartList *pl, RgPartKind kind, const char *name)
{
    return rg_parts_add(pl, kind, name);
}

/* The furniture part of a piece: facet / relief / card / lifted relief (gen:800-846). */
static bool pb_body(const Room *c, unsigned k, PB *b, RgPartList *pl, RgPart *inner, RgTile side)
{
    const RgPieceDef *pc = &c->pl.p[k];
    bool hasFoot = false;
    int foot = 0, y, x;
    RgPart *p;

    if (pc->hasFacet) {
        double ox = b->x0 * 16.0, oz = b->y0 * 16.0;
        const double (*f)[3] = pc->facet;

        if ((p = part_new(pl, RG_P_FACET, pc->name)) == NULL)
            return false;
        p->u.facet.a[0] = f[0][0] - ox; p->u.facet.a[1] = f[0][2] + 1 - oz;
        p->u.facet.b[0] = f[1][0] - ox; p->u.facet.b[1] = f[1][2] + 1 - oz;
        p->u.facet.ha = f[0][2] + 1 - f[0][1];
        p->u.facet.hb = f[1][2] + 1 - f[1][1];
        return true;
    }
    if (b->nMine == 0)
        return true;                                    /* a stairwell's sides: walls only, drawn nowhere */
    if (pc->hasFoot) {
        foot = pc->foot - b->y0 * 16;
        hasFoot = true;
    } else if (!pc->fill) {
        int mx = -1;

        for (y = 0; y < b->ph; y++)
            for (x = 0; x < b->pw; x++)
                if (b->own[(size_t)y * (size_t)b->pw + (size_t)x])
                    mx = y;
        foot = mx + 1;
        hasFoot = true;
    }
    if (pc->card) {
        rg_img_paste(&b->art, &b->obj, 0, 2 * b->ph + b->sh + b->th);
        if ((p = part_new(pl, RG_P_CARD, pc->name)) == NULL)
            return false;
        p->u.card.art = &b->obj;
        p->u.card.foot = (double)foot;
        p->u.card.voff = (double)(2 * b->ph + b->sh + b->th);
        return true;
    }
    {
        RgRelief *R = pc->base ? &inner->u.relief : NULL;
        RgPart *outer = part_new(pl, pc->base ? RG_P_LIFTED : RG_P_RELIEF, pc->name);

        if (outer == NULL)
            return false;
        if (R == NULL)
            R = &outer->u.relief;
        else {
            inner->kind = RG_P_RELIEF;
            snprintf(inner->name, sizeof(inner->name), "%s", pc->name);
            outer->u.lifted.part = inner;
            outer->u.lifted.base = (double)pc->base;
        }
        memset(R, 0, sizeof(*R));
        R->art = &b->obj;
        R->height = pc->height;
        R->side = side;
        R->hasFoot = hasFoot; R->foot = foot;
        R->solid = pc->solid;
        if (pc->hasAgainst || pc->hasBack) {
            R->hasBack = true;
            R->back = (double)((pc->hasAgainst ? pc->against : pc->back) - b->y0 * 16 - pc->base);
        }
        if (pc->hasTop) {
            R->hasTopTile = true;
            R->topTile = rg_tile(0, 2 * b->ph + b->sh, b->tw, 2 * b->ph + b->sh + b->th);
        }
        R->against = pc->hasAgainst;
    }
    return true;
}

/* Side walls and the two decals (gen:847-870). */
static bool pb_extras(const Room *c, unsigned k, PB *b, RgPartList *pl, RgTile side)
{
    const RgPieceDef *pc = &c->pl.p[k];
    unsigned i;
    char nm[RG_NAME_LEN];
    RgPart *p;

    for (i = 0; i < pc->nWalls; i++) {
        const RgWallDef *wd = &pc->walls[i];
        double ox = b->x0 * 16.0, oz = b->y0 * 16.0;

        snprintf(nm, sizeof(nm), "%s_side%u%s", pc->name, i, b->nMine ? "" : "~behind");
        if ((p = part_new(pl, RG_P_PLAINWALL, nm)) == NULL)
            return false;
        p->u.plain.a[0] = wd->a[0] - ox; p->u.plain.a[1] = wd->a[1] - oz;
        p->u.plain.b[0] = wd->b[0] - ox; p->u.plain.b[1] = wd->b[1] - oz;
        p->u.plain.y0 = -1;
        p->u.plain.y1 = wd->hasH ? wd->h : pc->height;
        p->u.plain.tile = side;
    }
    for (i = 0; i < b->nCells; i++) {
        int lx = b->cells[i][0] - b->x0, ly = b->cells[i][1] - b->y0;

        if (region_any(&b->art, lx * 16, ly * 16 + b->ph)) {
            b->loc[b->nLoc][0] = (int16_t)lx; b->loc[b->nLoc][1] = (int16_t)ly;
            b->nLoc++;
        }
        if (region_any(&b->art, lx * 16, ly * 16 + b->uo)) {
            b->hid[b->nHid][0] = (int16_t)lx; b->hid[b->nHid][1] = (int16_t)ly;
            b->nHid++;
        }
    }
    if (b->nLoc > 0) {
        snprintf(nm, sizeof(nm), "%s_floor", pc->name);
        if ((p = part_new(pl, RG_P_DECAL, nm)) == NULL)
            return false;
        p->u.decal.cells = (const int16_t (*)[2])b->loc;
        p->u.decal.nCells = b->nLoc;
        p->u.decal.vOffset = b->ph;
        p->u.decal.lift = 0.5;
    }
    if (b->nHid > 0) {
        snprintf(nm, sizeof(nm), "%s_under~behind", pc->name);
        if ((p = part_new(pl, RG_P_DECAL, nm)) == NULL)
            return false;
        p->u.decal.cells = (const int16_t (*)[2])b->hid;
        p->u.decal.nCells = b->nHid;
        p->u.decal.vOffset = b->uo;
        p->u.decal.lift = 0.25;
    }
    return true;
}

/* A model slot with its own spec copy (as rg_bexpand.c's new_model, which is private to that file). */
static RgBuildModel *new_xmodel(RgBuildModels *ms, const RgSpec *s, const RgLayout *L, const int rect[4],
                                const char *pieceName)
{
    RgBuildModel *m = rg_models_push(ms);
    RgXSpec *xs;

    if (m == NULL)
        return NULL;
    xs = (RgXSpec *)calloc(1, sizeof(RgXSpec));
    if (xs == NULL)
        return NULL;
    m->xSpec = xs;
    xs->spec = *s;
    snprintf(xs->name, sizeof(xs->name), "%s_%s", s->name, pieceName);
    xs->spec.name = xs->name;
    xs->spec.layoutId = L->id;
    xs->spec.rect[0] = (int16_t)rect[0]; xs->spec.rect[1] = (int16_t)rect[1];
    xs->spec.rect[2] = (int16_t)(rect[2] - rect[0]); xs->spec.rect[3] = (int16_t)(rect[3] - rect[1]);
    xs->exact.x0 = 0; xs->exact.y0 = 0;
    xs->exact.x1 = (int16_t)(xs->spec.rect[2] * 16); xs->exact.y1 = (int16_t)(xs->spec.rect[3] * 16);
    xs->exact.behind = false;
    xs->spec.exact = &xs->exact;
    xs->spec.nExact = 1;
    m->spec = &xs->spec;
    m->layout = L;
    m->w = (uint8_t)xs->spec.rect[2];
    m->h = (uint8_t)xs->spec.rect[3];
    m->groundMetatile = s->ground[0];
    rg_mesh_init(&m->mesh);
    return m;
}

static RgErr piece_build(Room *c, unsigned k)
{
    const RgPieceDef *pc = &c->pl.p[k];
    PB b;
    RgPartList parts;
    RgPart inner;
    RgTile side;
    RgBuildModel *m;
    RgErr err;
    bool ok;

    memset(&b, 0, sizeof(b));
    memset(&inner, 0, sizeof(inner));
    rg_parts_init(&parts);
    if ((err = pb_masks(c, k, &b)) != RG_OK || (err = pb_art(c, k, &b)) != RG_OK)
        goto out;
    side = pc->hasSide ? rg_tile(0, 2 * b.ph, b.sw, 2 * b.ph + b.sh) : rg_pick_side(&b.obj, pc->height);
    err = RG_ERR_BUILDINGS;
    if (!pb_body(c, k, &b, &parts, &inner, side) || !pb_extras(c, k, &b, &parts, side))
        goto out;
    m = new_xmodel(c->ms, c->s, c->L, c->rect[k], pc->name);
    if (m == NULL) {
        err = RG_ERR_NOMEM;
        goto out;
    }
    ok = rg_parts_emit(&parts, &m->mesh) && !m->mesh.failed;
    if (!ok) {
        snprintf(c->ms->errPiece, sizeof(c->ms->errPiece), "%s", m->spec->name);
        goto out;
    }
    m->art = b.art;
    memset(&b.art, 0, sizeof(b.art));
    m->built = true;
    c->ms->nInterior++;
    err = register_piece(c->R, c->ms->n - 1u, c->L, c->keys, b.x0, b.y0, (unsigned)b.w, (unsigned)b.h, &m->art,
                         b.hidden, !(pc->fill || pc->hasFacet || pc->nWalls), b.own);
out:
    rg_parts_free(&parts);
    pb_free(&b);
    return err;
}

RgErr rg_expand_interior(const RgWorld *w, const RgSpec *s, RgBuildModels *ms)
{
    Room c;
    const RgLayout *L;
    unsigned k;
    RgErr err;

    if (s->layoutId == 0 || s->layoutId > w->layoutCount || ms->reuse == NULL)
        goto skip;
    L = &w->layouts[s->layoutId - 1];
    if (!L->present || L->blocks == NULL || rg_layout_fnv(L) != s->layoutFnv)
        goto skip;
    memset(&c, 0, sizeof(c));
    c.w = w; c.R = (Reuse *)ms->reuse; c.ms = ms; c.s = s; c.def = (const RgRoomDef *)s->ext; c.L = L;
    if (c.def == NULL || !c.def->pieces(&c.pl) || c.pl.failed) {
        err = RG_ERR_BUILDINGS;
        goto out;
    }
    err = room_setup(&c);
    if (err == RG_OK) err = room_owner(&c);
    if (err == RG_OK) err = room_fills(&c);
    if (err == RG_OK) err = room_rects(&c);
    for (k = 0; k < c.n && err == RG_OK; k++)
        err = piece_build(&c, k);
out:
    room_free(&c);
    return err;
skip:
    if (ms->skipped < RG_MAX_SKIPPED)
        ms->skippedNames[ms->skipped] = s->name;
    ms->skipped++;
    return RG_OK;
}

/* ---- reuse_everywhere and the bare twins (gen:542-556, 925-937) ------------------------------------------ */

static bool is_modelled(const Reuse *R, uint16_t id)
{
    unsigned i;

    for (i = 0; i < R->nModelled; i++)
        if (R->modelled[i] == id)
            return true;
    return false;
}

static RgErr reuse_everywhere(const RgWorld *w, RgBuildModels *ms, Reuse *R)
{
    unsigned idx;
    RgErr err = RG_OK;

    for (idx = 0; idx < w->layoutCount && err == RG_OK; idx++) {
        const RgLayout *L = &w->layouts[idx];
        int32_t *keys;
        unsigned n = 0;

        if (!L->present || L->blocks == NULL || L->outdoor || is_modelled(R, L->id)
            || L->ts[0]->addr == 0 || L->ts[1]->addr == 0)
            continue;
        keys = (int32_t *)malloc((size_t)L->w * L->h * sizeof(int32_t) + sizeof(int32_t));
        if (keys == NULL || !cell_keys(R, L, keys)) {
            free(keys);
            return RG_ERR_NOMEM;
        }
        err = place_reused(R, ms, L, keys, false, 0, NULL, &n);
        free(keys);
        if (n > 0)
            ms->nReuseRooms++;
    }
    return err;
}

static bool name_ends(const char *s, const char *tail)
{
    size_t a = strlen(s), b = strlen(tail);

    return a >= b && strcmp(s + a - b, tail) == 0;
}

/* The twin of model `m`: the same art, its mesh without the floor decals, its own pixels, standing only in the rooms
 * where reuse_pieces found it by those pixels. */
static RgErr make_twin(const Reuse *R, unsigned mi, const RgBuildModel *m, RgBuildModel *t)
{
    RgXSpec *xs = (RgXSpec *)calloc(1, sizeof(RgXSpec));
    const RPiece *rp = NULL;
    unsigned i, nr = 0;
    size_t px;

    for (i = 0; i < R->n; i++)
        if (R->pc[i].model == mi)
            rp = &R->pc[i];
    if (xs == NULL || rp == NULL) {
        free(xs);
        return RG_ERR_BUILDINGS;
    }
    memset(t, 0, sizeof(*t));
    *xs = *m->xSpec;
    snprintf(xs->name, sizeof(xs->name), "%.58s_bare", m->xSpec->name);
    xs->spec.name = xs->name;
    xs->spec.exact = &xs->exact;
    t->xSpec = xs;
    t->spec = &xs->spec;
    t->layout = m->layout;
    t->w = m->w; t->h = m->h;
    t->groundMetatile = m->groundMetatile;
    t->built = true;
    if (!rg_img_new(&t->art, m->art.w, m->art.h))
        return RG_ERR_NOMEM;
    memcpy(t->art.px, m->art.px, (size_t)m->art.w * (size_t)m->art.h * 4u);
    px = (size_t)m->w * 16u * (size_t)m->h * 16u;
    t->own = (uint8_t *)calloc(px, 1);
    t->reused = (RgReuseAt *)calloc(m->nBareAt ? m->nBareAt : 1u, sizeof(RgReuseAt));
    if (t->own == NULL || t->reused == NULL)
        return RG_ERR_NOMEM;
    for (i = 0; i < rp->nPix; i++)
        t->own[(size_t)(rp->pix[i] >> 16) * (size_t)m->w * 16u + (rp->pix[i] & 0xFFFFu)] = 1;
    for (i = 0; i < m->nBareAt; i++) {
        RgReuseAt *r = &t->reused[nr++];

        r->lid = m->bareAt[i][0]; r->x = (int16_t)m->bareAt[i][1]; r->y = (int16_t)m->bareAt[i][2];
        r->noGround = true;
        r->ground = 0;
    }
    t->nReused = nr;
    rg_mesh_init(&t->mesh);
    for (i = 0; i < m->mesh.nNames; i++)
        if (rg_mesh_tag(&t->mesh, m->mesh.names[i]) != i)
            return RG_ERR_BUILDINGS;
    for (i = 0; i < m->mesh.n; i++) {
        const RgTri *src = &m->mesh.t[i];

        if (name_ends(m->mesh.names[src->tag], "_floor.decal"))
            continue;
        rg_mesh_tri(&t->mesh, &src->p[0], &src->p[1], &src->p[2], src->shade, src->tag);
    }
    return t->mesh.failed ? RG_ERR_NOMEM : RG_OK;
}

static RgErr add_twins(RgBuildModels *ms, Reuse *R)
{
    unsigned i, n = 0, nTwin = 0, o = 0;
    RgBuildModel *nm;
    RgErr err = RG_OK;

    for (i = 0; i < ms->n; i++)
        if (ms->m[i].nBareAt > 0)
            nTwin++;
    if (nTwin == 0)
        return RG_OK;
    n = ms->n + nTwin;
    nm = (RgBuildModel *)calloc(n, sizeof(RgBuildModel));
    if (nm == NULL)
        return RG_ERR_NOMEM;
    for (i = 0; i < ms->n; i++) {
        nm[o++] = ms->m[i];
        if (ms->m[i].nBareAt > 0) {
            err = make_twin(R, i, &ms->m[i], &nm[o]);
            if (err != RG_OK) {
                rg_model_release(&nm[o]);
                break;
            }
            o++;
        }
    }
    if (err != RG_OK) {                                 /* the originals stay in ms (still owned there) */
        for (i = 0; i < o; i++)
            if (nm[i].xSpec != NULL && nm[i].own != NULL)
                rg_model_release(&nm[i]);
        free(nm);
        return err;
    }
    free(ms->m);
    ms->m = nm;
    ms->n = n;
    ms->cap = n;
    ms->nTwin = nTwin;
    return RG_OK;
}

RgErr rg_interior_finish(const RgWorld *w, RgBuildModels *ms)
{
    Reuse *R = (Reuse *)ms->reuse;
    RgErr err;

    if (R == NULL)
        return RG_OK;
    err = reuse_everywhere(w, ms, R);
    if (err == RG_OK)
        err = add_twins(ms, R);
    rg_interior_free(ms);
    return err;
}

/* ---- room_check (gen:1518-1593) ------------------------------------------------------------------------- */

static void tri_cell(RgRaster *r, const RgImage *img, double x, double z, double depthLift, int16_t owner)
{
    double q[4][6];
    double X[4] = {x * 16, x * 16 + 16, x * 16 + 16, x * 16}, Z[4] = {z * 16, z * 16, z * 16 + 16, z * 16 + 16};
    double U[4] = {0, 16, 16, 0}, V[4] = {0, 0, 16, 16};
    static const unsigned tri[2][3] = {{0, 1, 2}, {0, 2, 3}};
    unsigned t, k;

    for (k = 0; k < 4; k++) {
        q[k][0] = X[k]; q[k][1] = Z[k]; q[k][2] = 0 + Z[k] + depthLift; q[k][3] = 1.0; q[k][4] = U[k]; q[k][5] = V[k];
    }
    for (t = 0; t < 2; t++) {
        double vs[3][6];

        for (k = 0; k < 3; k++)
            memcpy(vs[k], q[tri[t][k]], sizeof(vs[k]));
        rg_raster_draw(r, vs, img, 1.0, owner);
    }
}

static RgErr room_terrain(const RgBuildModels *ms, const RgPlacementList *pls, const RgLayout *L,
                          RgRaster *ras, RgPair *pair)
{
    int32_t *covered = (int32_t *)malloc((size_t)L->w * L->h * sizeof(int32_t));
    unsigned mi, i, x, y;
    RgErr err = RG_OK;

    if (covered == NULL)
        return RG_ERR_NOMEM;
    for (i = 0; i < (unsigned)L->w * L->h; i++)
        covered[i] = -1;
    for (mi = 0; mi < ms->n; mi++)
        for (i = 0; i < pls[mi].n; i++) {
            const RgPlacement *p = &pls[mi].p[i];

            if (p->layout != L->id)
                continue;
            for (y = 0; y < ms->m[mi].h; y++)
                for (x = 0; x < ms->m[mi].w; x++)
                    covered[(size_t)(p->py + (int)y) * L->w + (size_t)(p->px + (int)x)] = p->ground;
        }
    for (y = 0; y < L->h && err == RG_OK; y++)
        for (x = 0; x < L->w; x++) {
            RgImage img;
            int32_t c = covered[(size_t)y * L->w + x];

            if (!rg_cell_image(pair, c >= 0 ? (uint16_t)c : rg_metatile(L, (int)x, (int)y), &img)) {
                err = RG_ERR_NOMEM;
                break;
            }
            tri_cell(ras, &img, x, y, 0.0, -1);
            rg_img_free(&img);
        }
    free(covered);
    return err;
}

/* The map's own paint round a piece found on another floor, laid over the placement's floor (patch depth +0.01). */
static RgErr room_patches(const RgWorld *w, const RgBuildModels *ms, const RgPlacementList *pls, const RgLayout *L,
                          RgRaster *ras)
{
    unsigned mi, i, k;
    RgErr err = RG_OK;

    for (mi = 0; mi < ms->n && err == RG_OK; mi++)
        for (i = 0; i < pls[mi].n && err == RG_OK; i++) {
            RgPlacement tmp = pls[mi].p[i];

            if (tmp.layout != L->id || tmp.nOdd == 0)
                continue;
            tmp.cells = NULL;
            tmp.nCells = 0;
            err = rg_placement_patches(w, &ms->m[mi], &tmp);
            for (k = 0; k < tmp.nCells && err == RG_OK; k++)
                tri_cell(ras, &tmp.cells[k].img, tmp.px + tmp.cells[k].i, tmp.py + tmp.cells[k].j, 0.01, -2);
            for (k = 0; k < tmp.nCells; k++)
                rg_img_free(&tmp.cells[k].img);
            free(tmp.cells);
        }
    return err;
}

static void room_models(const RgBuildModels *ms, const RgPlacementList *pls, const RgLayout *L, RgRaster *ras)
{
    unsigned mi, i, t, k;

    for (mi = 0; mi < ms->n; mi++)
        for (i = 0; i < pls[mi].n; i++) {
            const RgPlacement *p = &pls[mi].p[i];
            const RgBuildModel *m = &ms->m[mi];

            if (p->layout != L->id)
                continue;
            for (t = 0; t < m->mesh.n; t++) {
                const RgTri *tr = &m->mesh.t[t];
                double vs[3][6];

                if (tr->flags & RG_TAG_DEPTH)
                    continue;                           /* real depth rising behind the drawing */
                for (k = 0; k < 3; k++) {
                    const RgVtx *v = &tr->p[k];

                    vs[k][0] = v->x + p->px * 16;
                    vs[k][1] = v->z + p->py * 16 - v->y;
                    vs[k][2] = v->y + v->z + p->py * 16;
                    vs[k][3] = 1.0; vs[k][4] = v->u; vs[k][5] = v->v;
                }
                rg_raster_draw(ras, vs, &m->art, tr->shade, (int16_t)mi);
            }
        }
}

/* The composed room against the cells' own images, minus the room's open corners. */
static void room_compare(const RgLayout *L, const RgRoomDef *def, RgPair *pair, const RgRaster *ras,
                         RgRoomResult *res)
{
    int W = (int)L->w * 16, H = (int)L->h * 16, x, y;
    RgImage full;
    bool first = true;

    if (!rg_img_new(&full, W, H)) {
        res->err = RG_ERR_NOMEM;
        return;
    }
    for (y = 0; y < (int)L->h && res->err == RG_OK; y++)
        for (x = 0; x < (int)L->w; x++) {
            RgImage cell;

            if (!rg_cell_image(pair, rg_metatile(L, x, y), &cell)) {
                res->err = RG_ERR_NOMEM;
                break;
            }
            rg_img_paste(&full, &cell, x * 16, y * 16);
            rg_img_free(&cell);
        }
    for (y = 0; y < H && res->err == RG_OK; y++)
        for (x = 0; x < W; x++) {
            size_t o = (size_t)y * (size_t)W + (size_t)x;

            if (def != NULL && rg_inside(&def->open, x, y))
                continue;                               /* floor in the round where the drawing is black */
            if (memcmp(full.px + o * 4u, ras->rgb + o * 3u, 3) == 0)
                continue;
            res->bad++;
            if (first) {
                res->firstX = x;
                res->firstY = y;
                res->firstOwner = ras->owner[o];
                first = false;
            }
        }
    rg_img_free(&full);
}

RgRoomResult rg_room_check(const RgWorld *w, const RgBuildModels *ms, const RgPlacementList *pls, uint16_t layoutId)
{
    RgRoomResult res;
    const RgLayout *L = (layoutId >= 1 && layoutId <= w->layoutCount) ? &w->layouts[layoutId - 1] : NULL;
    const RgRoomDef *def = rg_room_of_layout(layoutId);
    RgPairCache pcache;
    RgRaster ras;
    RgPair *pair;
    int W, H;

    memset(&res, 0, sizeof(res));
    res.firstOwner = -1;
    if (L == NULL || !L->present)
        return res.err = RG_ERR_BUILDINGS, res;
    W = (int)L->w * 16;
    H = (int)L->h * 16;
    pcache.w = w; pcache.p = NULL; pcache.idx = -1;
    pair = rg_pc_get(&pcache, L->pairIndex);
    if (pair == NULL || !rg_raster_init(&ras, W, H)) {
        rg_pc_close(&pcache);
        return res.err = RG_ERR_NOMEM, res;
    }
    res.err = room_terrain(ms, pls, L, &ras, pair);
    if (res.err == RG_OK)
        res.err = room_patches(w, ms, pls, L, &ras);
    if (res.err == RG_OK)
        room_models(ms, pls, L, &ras);
    if (res.err == RG_OK)
        room_compare(L, def, pair, &ras, &res);
    rg_raster_free(&ras);
    rg_pc_close(&pcache);
    return res;
}
