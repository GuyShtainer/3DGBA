/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/gen_voxel_buildings.py
 * (component_specs, piece_spec, prop_specs, seam_art, pick_side, flank_band, kit_specs) and voxel_props.py
 * (OBJECTS, find, cells_of, beyond, connections), MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
#include "rg_bexpand.h"

#include <assert.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- shared helpers ------------------------------------------------------------------------ */

static int fdiv2(int a) { return a >= 0 ? a / 2 : -((-a + 1) / 2); }     /* Python a // 2 */
static int fmod2(int a) { return ((a % 2) + 2) % 2; }                     /* Python a % 2 */

static uint16_t mt_at(const RgLayout *L, int x, int y)
{
    return (uint16_t)(rg_rd16(L->blocks + 2u * ((size_t)y * L->w + (size_t)x)) & 0x3FFu);
}

/* Fills the private copy of the spec, the name and the one exact rect (0, 0, w*16, h*16). */
static RgBuildModel *new_model(RgBuildModels *ms, const RgSpec *s, const RgLayout *L, int x, int y, int w, int h,
                               const char *fmt, unsigned a, int b, int c, RgSpecKind kind)
{
    RgBuildModel *m = rg_models_push(ms);
    RgXSpec *xs;

    if (m == NULL)
        return NULL;
    xs = (RgXSpec *)calloc(1, sizeof(RgXSpec));
    if (xs == NULL)
        return NULL;
    m->xSpec = xs;                      /* owned by the model: rg_models_free releases it */
    xs->spec = *s;
    snprintf(xs->name, sizeof(xs->name), fmt, s->name, a, b, c);
    xs->spec.name = xs->name;
    xs->spec.kind = kind;
    xs->spec.layoutId = L->id;
    xs->spec.rect[0] = (int16_t)x; xs->spec.rect[1] = (int16_t)y;
    xs->spec.rect[2] = (int16_t)w; xs->spec.rect[3] = (int16_t)h;
    xs->exact.x0 = 0; xs->exact.y0 = 0;
    xs->exact.x1 = (int16_t)(w * 16); xs->exact.y1 = (int16_t)(h * 16);
    xs->exact.behind = false;
    xs->spec.exact = &xs->exact;
    xs->spec.nExact = 1;
    m->spec = &xs->spec;
    m->layout = L;
    m->w = (uint8_t)w;
    m->h = (uint8_t)h;
    m->groundMetatile = s->ground[0];
    rg_mesh_init(&m->mesh);
    return m;
}

/* ---- components (gen:38-90, 145-171) ------------------------------------------------------- */

typedef struct Pat { uint16_t *v; unsigned n, cap; bool bad; } Pat;
static void pat_put(Pat *p, unsigned v)
{
    if (p->bad)
        return;
    if (p->n == p->cap) {
        unsigned cap = p->cap ? p->cap * 2u : 64u;
        uint16_t *nv = (uint16_t *)realloc(p->v, (size_t)cap * 2u);

        if (nv == NULL) {
            p->bad = true;
            return;
        }
        p->v = nv;
        p->cap = cap;
    }
    p->v[p->n++] = (uint16_t)v;
}

static bool is_tile(const RgLayout *L, const bool *tileSet, int x, int y)
{
    return x >= 0 && y >= 0 && x < (int)L->w && y < (int)L->h && tileSet[mt_at(L, x, y)];
}

/* piece_spec: one model for `cells` (indices into the layout) of the run they belong to. A run is exactly the
 * set of 4-connected tile cells, so "(x, y) in run" is "(x, y) holds a tile of the set". */
static RgErr make_piece(RgBuildModels *ms, const RgSpec *s, const RgComponentsCfg *cfg, const RgLayout *L,
                        const bool *tileSet, const uint32_t *cells, unsigned n, Pat *pat)
{
    int x0 = 1 << 20, y0 = 1 << 20, x1 = -1, y1 = -1, w, h, i, j;
    unsigned k;
    RgBuildModel *m;
    unsigned lw = L->w;
    unsigned nSouth = 0, nNorth = 0;

    for (k = 0; k < n; k++) {
        int cx = (int)(cells[k] % lw), cy = (int)(cells[k] / lw);

        if (cx < x0) x0 = cx;
        if (cy < y0) y0 = cy;
        if (cx + 1 > x1) x1 = cx + 1;
        if (cy + 1 > y1) y1 = cy + 1;
    }
    w = x1 - x0;
    h = y1 - y0;
    if (w > 255 || h > 255)
        return RG_ERR_TOO_BIG;
    m = new_model(ms, s, L, x0, y0, w, h, "%s_%u_%d_%d", L->id, x0, y0, RG_SPEC_COMPONENTS);
    if (m == NULL)
        return RG_ERR_NOMEM;
    m->comp = cfg;
    m->owned = (uint8_t *)calloc((size_t)w * h, 1);
    m->south = (uint8_t *)calloc((size_t)w * h, 1);
    m->north = (uint8_t *)calloc((size_t)w * h, 1);
    m->east = (uint8_t *)calloc((size_t)h, 1);
    m->west = (uint8_t *)calloc((size_t)h, 1);
    if (m->owned == NULL || m->south == NULL || m->north == NULL || m->east == NULL || m->west == NULL)
        return RG_ERR_NOMEM;
    for (k = 0; k < n; k++)
        m->owned[((int)(cells[k] / lw) - y0) * w + ((int)(cells[k] % lw) - x0)] = 1;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) {
            if (!m->owned[j * w + i])
                continue;
            if (is_tile(L, tileSet, x0 + i, y0 + j + 1) && !(j + 1 < h && m->owned[(j + 1) * w + i])) {
                m->south[j * w + i] = 1; nSouth++;
            }
            if (is_tile(L, tileSet, x0 + i, y0 + j - 1) && !(j - 1 >= 0 && m->owned[(j - 1) * w + i])) {
                m->north[j * w + i] = 1; nNorth++;
            }
            if (is_tile(L, tileSet, x0 + i + 1, y0 + j) && !(i + 1 < w && m->owned[j * w + i + 1]))
                m->east[j] = 1;
            if (is_tile(L, tileSet, x0 + i - 1, y0 + j) && !(i - 1 >= 0 && m->owned[j * w + i - 1]))
                m->west[j] = 1;
        }
    /* the pattern (identity of a block): owned (i, j, metatile) row-major, then each sorted set with its metatile
     * neighbours; counts prefix every section so two patterns are equal iff the bytes are */
    pat->n = 0;
    pat->bad = false;
    {
        unsigned nOwned = 0;

        for (k = 0; k < (unsigned)(w * h); k++)
            nOwned += m->owned[k];
        pat_put(pat, nOwned);
        for (j = 0; j < h; j++)
            for (i = 0; i < w; i++)
                if (m->owned[j * w + i]) {
                    pat_put(pat, (unsigned)i); pat_put(pat, (unsigned)j); pat_put(pat, mt_at(L, x0 + i, y0 + j));
                }
        pat_put(pat, nSouth);
        for (i = 0; i < w; i++)
            for (j = 0; j < h; j++)
                if (m->south[j * w + i]) { pat_put(pat, (unsigned)i); pat_put(pat, (unsigned)j); }
        for (i = 0; i < w; i++)
            for (j = 0; j < h; j++)
                if (m->south[j * w + i]) pat_put(pat, mt_at(L, x0 + i, y0 + j + 1));
        pat_put(pat, nNorth);
        for (i = 0; i < w; i++)
            for (j = 0; j < h; j++)
                if (m->north[j * w + i]) { pat_put(pat, (unsigned)i); pat_put(pat, (unsigned)j); }
        for (i = 0; i < w; i++)
            for (j = 0; j < h; j++)
                if (m->north[j * w + i]) pat_put(pat, mt_at(L, x0 + i, y0 + j - 1));
        for (j = 0; j < h; j++)
            pat_put(pat, m->east[j]);
        for (j = 0; j < h; j++)
            pat_put(pat, m->west[j]);
    }
    return pat->bad ? RG_ERR_NOMEM : RG_OK;
}

typedef struct PatRef { uint16_t *v; unsigned n; } PatRef;

RgErr rg_expand_components(const RgWorld *w, const RgSpec *s, RgBuildModels *ms)
{
    const RgComponentsCfg *cfg = (const RgComponentsCfg *)s->ext;
    bool tileSet[1024];
    unsigned idx, first = ms->n, k;
    PatRef *refs = NULL;
    unsigned nRefs = 0, capRefs = 0;
    Pat pat;
    RgErr err = RG_OK;

    memset(tileSet, 0, sizeof(tileSet));
    memset(&pat, 0, sizeof(pat));
    for (k = 0; k < cfg->nTiles; k++)
        tileSet[cfg->tiles[k] & 0x3FFu] = true;
    for (idx = 0; idx < w->layoutCount && err == RG_OK; idx++) {
        const RgLayout *E = &w->layouts[idx];
        unsigned lw = E->w, lh = E->h, x, y;
        uint8_t *seen;
        uint32_t *stack, *cells;

        if (!E->present || E->blocks == NULL || E->ts[1]->addr != cfg->secondaryAddr)
            continue;
        seen = (uint8_t *)calloc((size_t)lw * lh, 1);
        stack = (uint32_t *)malloc((size_t)lw * lh * 4u);
        cells = (uint32_t *)malloc((size_t)lw * lh * 4u);
        if (seen == NULL || stack == NULL || cells == NULL) {
            free(seen); free(stack); free(cells);
            err = RG_ERR_NOMEM;
            break;
        }
        for (y = 0; y < lh && err == RG_OK; y++)
            for (x = 0; x < lw && err == RG_OK; x++) {
                unsigned nTodo = 0, nCells = 0;

                if (seen[y * lw + x] || !tileSet[mt_at(E, (int)x, (int)y)])
                    continue;
                stack[nTodo++] = y * lw + x;
                seen[y * lw + x] = 1;
                while (nTodo > 0) {                      /* LIFO, neighbours +x -x +y -y (list.pop()) */
                    uint32_t c = stack[--nTodo];
                    int cx = (int)(c % lw), cy = (int)(c / lw), q;
                    static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};

                    cells[nCells++] = c;
                    for (q = 0; q < 4; q++) {
                        int nx = cx + dx[q], ny = cy + dy[q];

                        if (nx >= 0 && nx < (int)lw && ny >= 0 && ny < (int)lh && !seen[(unsigned)ny * lw + (unsigned)nx]
                            && tileSet[mt_at(E, nx, ny)]) {
                            seen[(unsigned)ny * lw + (unsigned)nx] = 1;
                            stack[nTodo++] = (unsigned)ny * lw + (unsigned)nx;
                        }
                    }
                }
                if (cfg->block <= 0) {
                    err = make_piece(ms, s, cfg, E, tileSet, cells, nCells, &pat);
                    if (err == RG_OK) {
                        /* a hedge: no merging, but keep a ref slot so indices line up with models */
                        if (nRefs == capRefs) {
                            unsigned cap = capRefs ? capRefs * 2u : 64u;
                            PatRef *nr = (PatRef *)realloc(refs, (size_t)cap * sizeof(PatRef));

                            if (nr == NULL) { err = RG_ERR_NOMEM; break; }
                            refs = nr; capRefs = cap;
                        }
                        refs[nRefs].v = NULL; refs[nRefs].n = 0; nRefs++;
                    }
                } else {
                    /* blocks aligned on the map, in order of first appearance in the run's cell order */
                    int size = cfg->block;
                    unsigned nb = 0, c;
                    int (*keys)[2] = (int (*)[2])malloc((size_t)nCells * sizeof(*keys));
                    uint32_t *blockOf = (uint32_t *)malloc((size_t)nCells * 4u);
                    uint32_t *piece = (uint32_t *)malloc((size_t)nCells * 4u);

                    if (keys == NULL || blockOf == NULL || piece == NULL) {
                        free(keys); free(blockOf); free(piece);
                        err = RG_ERR_NOMEM;
                        break;
                    }
                    for (c = 0; c < nCells; c++) {
                        int bx = (int)(cells[c] % lw) / size, by = (int)(cells[c] / lw) / size;
                        unsigned b;

                        for (b = 0; b < nb; b++)
                            if (keys[b][0] == bx && keys[b][1] == by)
                                break;
                        if (b == nb) {
                            keys[nb][0] = bx; keys[nb][1] = by; nb++;
                        }
                        blockOf[c] = b;
                    }
                    for (k = 0; k < nb && err == RG_OK; k++) {
                        unsigned np = 0;

                        for (c = 0; c < nCells; c++)
                            if (blockOf[c] == k)
                                piece[np++] = cells[c];
                        err = make_piece(ms, s, cfg, E, tileSet, piece, np, &pat);
                        if (err != RG_OK)
                            break;
                        if (nRefs == capRefs) {
                            unsigned cap = capRefs ? capRefs * 2u : 64u;
                            PatRef *nr = (PatRef *)realloc(refs, (size_t)cap * sizeof(PatRef));

                            if (nr == NULL) { err = RG_ERR_NOMEM; break; }
                            refs = nr; capRefs = cap;
                        }
                        refs[nRefs].v = (uint16_t *)malloc((size_t)pat.n * 2u);
                        if (refs[nRefs].v == NULL) { err = RG_ERR_NOMEM; break; }
                        memcpy(refs[nRefs].v, pat.v, (size_t)pat.n * 2u);
                        refs[nRefs].n = pat.n;
                        nRefs++;
                    }
                    free(keys); free(blockOf); free(piece);
                }
            }
        free(seen); free(stack); free(cells);
    }
    /* identical blocks: one model, placed at each (gen:77-86) */
    if (err == RG_OK && cfg->block > 0) {
        unsigned dst = first, src;

        for (src = first; src < ms->n && err == RG_OK; src++) {
            RgBuildModel *a = &ms->m[src];
            unsigned q;
            bool merged = false;

            for (q = first; q < dst; q++) {
                RgBuildModel *b = &ms->m[q];

                if (b->layout == a->layout && refs[q - first].n == refs[src - first].n
                    && memcmp(refs[q - first].v, refs[src - first].v, (size_t)refs[q - first].n * 2u) == 0) {
                    b->repeat[b->nRepeat][0] = (uint16_t)a->xSpec->spec.rect[0];
                    b->repeat[b->nRepeat][1] = (uint16_t)a->xSpec->spec.rect[1];
                    b->nRepeat++;
                    merged = true;
                    break;
                }
            }
            if (merged) {
                rg_model_release(a);
                free(refs[src - first].v);
                refs[src - first].v = NULL;
                continue;
            }
            a->repeat = (uint16_t (*)[2])malloc((size_t)(ms->n - first) * sizeof(uint16_t[2]));
            if (a->repeat == NULL) {
                err = RG_ERR_NOMEM;
                break;
            }
            a->repeat[0][0] = (uint16_t)a->xSpec->spec.rect[0];
            a->repeat[0][1] = (uint16_t)a->xSpec->spec.rect[1];
            a->nRepeat = 1;
            if (dst != src) {
                ms->m[dst] = *a;
                memset(a, 0, sizeof(*a));              /* the slot's contents moved: never freed twice */
                refs[dst - first] = refs[src - first];
                refs[src - first].v = NULL;
            }
            dst++;
        }
        if (err == RG_OK)
            ms->n = dst;
    }
    if (err == RG_OK) {
        if (strcmp(s->name, "hedge") == 0)
            ms->nHedge += ms->n - first;
        else
            ms->nRailing += ms->n - first;
    }
    for (k = 0; k < nRefs; k++)
        free(refs[k].v);
    free(refs);
    free(pat.v);
    return err;
}

/* ---- relief helpers (gen:174-288) ---------------------------------------------------------- */

static bool opaque(const RgImage *im, int x, int y)
{
    return im->px[((size_t)y * (size_t)im->w + (size_t)x) * 4u + 3u] >= 128;
}

RgTile rg_pick_side(const RgImage *art, int height)
{
    int W = art->w, H = art->h, u, v, width, k;
    int *ends = (int *)malloc((size_t)W * sizeof(int));        /* -1 = None */
    int bestU = 0, bestV = 0, score = -1;

    if (ends == NULL)
        return rg_tile(0, 0, 1, 1);
    for (u = 0; u < W; u++) {
        int end = -1;
        bool full;

        for (v = H - 1; v >= 0; v--)
            if (opaque(art, u, v)) {
                end = v + 1;
                break;
            }
        full = end >= 0 && end >= height;
        if (full)
            for (v = end - height; v < end; v++)
                if (!opaque(art, u, v)) {
                    full = false;
                    break;
                }
        ends[u] = full ? end : -1;
    }
    {
        static const int widths[3] = {8, 6, 4};
        int wi;

        for (wi = 0; wi < 3; wi++) {
            int have = 0, bu = 0, be = 0;

            width = widths[wi];
            for (u = 0; u + width <= W; u++) {
                int e = ends[u];
                bool same = e >= 0;

                for (k = 0; same && k < width; k++)
                    same = ends[u + k] == e;
                if (same && (!have || e > be)) {
                    have = 1;
                    bu = u;
                    be = e;
                }
            }
            if (have) {
                free(ends);
                return rg_tile(bu, be - height, bu + width, be);
            }
        }
    }
    /* no straight front: the block of the object's own drawing with the most drawn pixels */
    {
        int vmax = H - height + 1 > 1 ? H - height + 1 : 1;
        int umax = W - 4 + 1 > 1 ? W - 4 + 1 : 1;
        int ww = W < 4 ? W : 4, hh = height < H ? height : H;

        for (v = 0; v < vmax; v++)
            for (u = 0; u < umax; u++) {
                int n = 0, i, j;

                for (i = 0; i < ww; i++)
                    for (j = 0; j < hh; j++)
                        if (u + i < W && v + j < H && opaque(art, u + i, v + j))
                            n++;
                if (n > score) {
                    bestU = u;
                    bestV = v;
                    score = n;
                }
            }
        free(ends);
        return rg_tile(bestU, bestV, bestU + ww, bestV + hh);
    }
}

/* the column's drawn run from row 0, gaps of `hull` closed, reaches `rows` rows */
static bool carries(const RgImage *img, int u, int rows, int hull)
{
    int end = 0, gap = 0, v;

    for (v = 0; v < img->h; v++) {
        if (opaque(img, u, v)) {
            end = v + 1;
            gap = 0;
        } else {
            gap++;
            if (gap > hull)
                break;
        }
    }
    return opaque(img, u, 0) && end >= rows;
}

bool rg_seam_art(RgPair *pair, const RgLayout *L, const RgBuildModel *m, const RgImage *art, int height,
                 RgImage *out, uint8_t *openS, uint8_t *openN, unsigned *clash)
{
    const RgSpec *s = m->spec;
    const RgComponentsCfg *cfg = m->comp;
    int x = s->rect[0], y = s->rect[1], w = m->w, h = m->h, i, j, k, v;
    bool anyS = false, anyN = false;
    uint8_t one = 1;
    unsigned cap = 0;
    uint8_t *colUsed = NULL;

    for (k = 0; k < w * h; k++) {
        anyS = anyS || m->south[k];
        anyN = anyN || m->north[k];
    }
    if (!anyS && !anyN)
        return rg_img_new(out, art->w, art->h) && (memcpy(out->px, art->px, (size_t)art->w * (size_t)art->h * 4u), true);
    (void)cap;
    if (anyS) {
        if (!rg_img_new(out, w * 16, h * 16 + height))
            return false;
        rg_img_paste(out, art, 0, 0);
        colUsed = (uint8_t *)calloc((size_t)w, 1);
        if (colUsed == NULL)
            return false;
    } else if (!rg_img_new(out, art->w, art->h) || (memcpy(out->px, art->px, (size_t)art->w * (size_t)art->h * 4u), false)) {
        return false;
    }
    /* south cells: upstream iterates a frozenset; the result is order-free only while no two share a column */
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) {
            RgImage cell;

            if (!m->south[j * w + i])
                continue;
            if (colUsed[i]++)
                (*clash)++;
            if (!rg_building_art(NULL, pair, L, x + i, y + j + 1, 1, 1, s->ground, s->nGround, &one, cfg->upper, &cell))
                return false;
            for (k = 0; k < 16; k++) {
                int u = i * 16 + k;

                if (!opaque(art, u, h * 16 - 1) || !opaque(&cell, k, 0))
                    continue;
                for (v = 0; v < height; v++)
                    memcpy(out->px + ((size_t)(h * 16 + v) * (size_t)out->w + (size_t)u) * 4u,
                           cell.px + ((size_t)v * 16u + (size_t)k) * 4u, 4);
                if (carries(&cell, k, height + 1, cfg->hull))
                    openS[u] = 1;
            }
            rg_img_free(&cell);
        }
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) {
            RgImage cell;

            if (!m->north[j * w + i])
                continue;
            if (!rg_building_art(NULL, pair, L, x + i, y + j - 1, 1, 1, s->ground, s->nGround, &one, cfg->upper, &cell))
                return false;
            for (k = 0; k < 16; k++) {
                int u = i * 16 + k;

                if (opaque(art, u, 0) && opaque(&cell, k, 15))
                    openN[u] = 1;
            }
            rg_img_free(&cell);
        }
    free(colUsed);
    return true;
}

bool rg_flank_band(RgPair *pair, const RgImage *art, const RgComponentsCfg *c, int height, RgImage *out,
                   RgTile *tile, bool *has)
{
    RgCellPx cp;
    RgImage cell;
    int rows = height + 2, x, y;

    if (c->flank < 0) {
        *has = false;
        if (!rg_img_new(out, art->w, art->h))
            return false;
        memcpy(out->px, art->px, (size_t)art->w * (size_t)art->h * 4u);
        return true;
    }
    if (!rg_img_new(&cell, 16, 16))
        return false;
    rg_cell_px(pair, (uint16_t)c->flank, 1, &cp);
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++)
            if (cp.idx[y][x] != 0)
                rg_c5_rgba(cp.c[y][x], 255, cell.px + ((size_t)y * 16u + (size_t)x) * 4u);
    if (!rg_img_new(out, art->w, art->h + rows)) {
        rg_img_free(&cell);
        return false;
    }
    rg_img_paste(out, art, 0, 0);
    for (y = 0; y < rows; y++)                                   /* cell.crop((0, 16 - rows, 16, 16)) */
        memcpy(out->px + ((size_t)(art->h + y) * (size_t)out->w) * 4u,
               cell.px + ((size_t)(16 - rows + y) * 16u) * 4u, 16u * 4u);
    rg_img_free(&cell);
    *tile = rg_tile(0, art->h, 16, art->h + rows);
    *has = true;
    return true;
}

RgErr rg_build_component(const RgWorld *w, RgPair *pair, RgBuildModel *m)
{
    const RgSpec *s = m->spec;
    const RgComponentsCfg *cfg = m->comp;
    RgImage art0, art1, art2;
    RgRelief R;
    RgTile side, flank;
    bool hasFlank = false;
    uint8_t *openS = NULL, *openN = NULL;
    unsigned clash = 0;
    RgErr err = RG_ERR_BUILDINGS;

    memset(&art0, 0, sizeof(art0)); memset(&art1, 0, sizeof(art1)); memset(&art2, 0, sizeof(art2));
    memset(&flank, 0, sizeof(flank));
    if (!rg_building_art(w, pair, m->layout, s->rect[0], s->rect[1], s->rect[2], s->rect[3], s->ground, s->nGround,
                         m->owned, cfg->upper, &art0))
        return RG_ERR_NOMEM;
    side = rg_pick_side(&art0, cfg->height);
    openS = (uint8_t *)calloc((size_t)art0.w, 1);
    openN = (uint8_t *)calloc((size_t)art0.w, 1);
    if (openS == NULL || openN == NULL) {
        err = RG_ERR_NOMEM;
        goto out;
    }
    if (!rg_seam_art(pair, m->layout, m, &art0, cfg->height, &art1, openS, openN, &clash)) {
        err = RG_ERR_NOMEM;
        goto out;
    }
    if (!rg_flank_band(pair, &art1, cfg, cfg->height, &art2, &flank, &hasFlank)) {
        err = RG_ERR_NOMEM;
        goto out;
    }
    memset(&R, 0, sizeof(R));
    R.art = &art2;
    R.height = cfg->height;
    R.side = side;
    R.hull = cfg->hull;
    R.bridge = cfg->bridge;
    R.hasSeam = true;
    R.seamRows = m->h * 16;
    R.openS = openS;
    R.openN = openN;
    R.hasFlank = hasFlank;
    R.flank = flank;
    R.seamW = m->west;
    R.seamE = m->east;
    R.nSeamRows = m->h;
    if (!rg_emit_relief(&R, "relief", &m->mesh) || m->mesh.failed)
        goto out;
    m->art = art2;
    memset(&art2, 0, sizeof(art2));
    m->seamClash = clash;
    err = RG_OK;
out:
    rg_img_free(&art0); rg_img_free(&art1); rg_img_free(&art2);
    free(openS); free(openN);
    return err;
}

/* ---- kit (gen:291-324) --------------------------------------------------------------------- */

typedef struct GridRef { uint16_t *mt; unsigned n; } GridRef;

static bool in_set(const uint16_t *set, unsigned n, unsigned v)
{
    unsigned i;

    for (i = 0; i < n; i++)
        if (set[i] == v)
            return true;
    return false;
}

RgErr rg_expand_kit(const RgWorld *w, const RgSpec *s, RgBuildModels *ms)
{
    const RgKitCfg *cfg = (const RgKitCfg *)s->ext;
    const RgLayout *L;
    GridRef *grids = NULL;
    unsigned nGrids = 0, capGrids = 0, k;
    int x, y, lw, lh;
    RgErr err = RG_OK;
    unsigned first = ms->n;

    if (s->layoutId == 0 || s->layoutId > w->layoutCount)
        goto skip;
    L = &w->layouts[s->layoutId - 1];
    if (!L->present || L->blocks == NULL || rg_layout_fnv(L) != s->layoutFnv)
        goto skip;
    lw = L->w;
    lh = L->h;
    for (y = 0; y < lh && err == RG_OK; y++)
        for (x = 0; x < lw && err == RG_OK; x++) {
            int x2, y2, bw, bh, i, j;
            uint16_t *grid;
            RgBuildModel *m;
            bool dup = false;

            if (mt_at(L, x, y) != cfg->corner)
                continue;
            x2 = x + 1;
            while (x2 < lw && in_set(cfg->top, cfg->nTop, mt_at(L, x2, y)))
                x2++;
            if (x2 >= lw || !in_set(cfg->end, cfg->nEnd, mt_at(L, x2, y)))
                continue;
            y2 = y + 1;
            while (y2 < lh && mt_at(L, x, y2) != cfg->foot)
                y2++;
            if (y2 >= lh)
                continue;
            bw = x2 - x + 1;
            bh = y2 - y + 1;
            grid = (uint16_t *)malloc((size_t)bw * bh * 2u);
            if (grid == NULL) {
                err = RG_ERR_NOMEM;
                break;
            }
            for (j = 0; j < bh; j++)
                for (i = 0; i < bw; i++)
                    grid[j * bw + i] = mt_at(L, x + i, y + j);
            for (k = 0; k < nGrids && !dup; k++)
                dup = grids[k].n == (unsigned)(bw * bh) && memcmp(grids[k].mt, grid, (size_t)bw * bh * 2u) == 0;
            if (dup) {
                free(grid);
                continue;
            }
            if (nGrids == capGrids) {
                unsigned cap = capGrids ? capGrids * 2u : 16u;
                GridRef *ng = (GridRef *)realloc(grids, (size_t)cap * sizeof(GridRef));

                if (ng == NULL) {
                    free(grid);
                    err = RG_ERR_NOMEM;
                    break;
                }
                grids = ng;
                capGrids = cap;
            }
            grids[nGrids].mt = grid;
            grids[nGrids].n = (unsigned)(bw * bh);
            nGrids++;
            m = new_model(ms, s, L, x, y, bw, bh, "%s_%u_%d", (unsigned)x, y, 0, RG_SPEC_DIRECT);
            if (m == NULL) {
                err = RG_ERR_NOMEM;
                break;
            }
            m->xSpec->spec.arg0 = (mt_at(L, x2, y) == 0x223 && in_set(grid, (unsigned)bw, 0x222)) ? 1 : 0;
            rg_flat_block_exact(&m->xSpec->exact, bw * 16, bh * 16, cfg->firstRoofRow);
        }
    for (k = 0; k < nGrids; k++)
        free(grids[k].mt);
    free(grids);
    if (err == RG_OK)
        ms->nKit += ms->n - first;
    return err;
skip:
    if (ms->skipped < RG_MAX_SKIPPED)
        ms->skippedNames[ms->skipped] = s->name;
    ms->skipped++;
    return RG_OK;
}

/* ---- props (voxel_props.py, gen:93-142) ---------------------------------------------------- */


typedef struct PGrid { int tile; bool optional; int pal; } PGrid;      /* tile -1 = None */
typedef struct PObj { int gw, gh, palette; PGrid g[16]; } PObj;

#define N_ -1, false, 0
static const PObj kObjects[RG_OBJ_COUNT] = {
    /* sea_rock */
    {2, 2, 1, {{141, false, 1}, {142, false, 1}, {157, false, 1}, {158, false, 1}}},
    /* sand_boulder */
    {2, 2, 3, {{88, false, 3}, {89, false, 3}, {104, false, 3}, {105, false, 3}}},
    /* sea_stack: the corner (69, True, 0) is drawn in palette 0 in some copies, left out in others */
    {4, 4, 3, {{N_}, {65, false, 3}, {68, false, 3}, {69, true, 0},
               {80, false, 3}, {81, false, 3}, {84, false, 3}, {85, false, 3},
               {128, false, 3}, {129, false, 3}, {132, false, 3}, {133, false, 3},
               {144, false, 3}, {145, false, 3}, {148, false, 3}, {149, false, 3}}},
};

typedef struct Conn { uint16_t b; int dx, dy; } Conn;
typedef struct ExpCtx {
    const RgWorld *w;
    uint32_t **subs;                    /* per layout id: 2w x 2h upper subtiles, 0 = none (built lazily) */
    Conn **conns;                       /* per layout id */
    unsigned *nConns;
} ExpCtx;

static bool layout_ok(const RgWorld *w, unsigned id)
{
    return id >= 1 && id <= w->layoutCount && w->layouts[id - 1].present && w->layouts[id - 1].blocks != NULL;
}

/* connections(): where each neighbour's origin lies in a layout's cells (props:93-121), deduplicated. */
static RgErr build_conns(ExpCtx *c, unsigned *ambiguous)
{
    const RgWorld *w = c->w;
    unsigned mi, cnt, i, id;

    c->conns = (Conn **)calloc((size_t)w->layoutCount + 1u, sizeof(Conn *));
    c->nConns = (unsigned *)calloc((size_t)w->layoutCount + 1u, sizeof(unsigned));
    if (c->conns == NULL || c->nConns == NULL)
        return RG_ERR_NOMEM;
    for (mi = 0; mi < w->mapCount; mi++) {
        const RgMap *m = &w->maps[mi];
        RgConn buf[64];
        unsigned a = m->layoutId;

        if (!layout_ok(w, a))
            continue;
        cnt = rg_map_connections(w, m->group, m->num, buf, 64);
        if (cnt > 64)
            cnt = 64;
        for (i = 0; i < cnt; i++) {
            const RgMap *o = rg_world_map(w, buf[i].group, buf[i].num);
            unsigned b, k;
            int dx, dy, off = buf[i].offset;
            const RgLayout *A = &w->layouts[a - 1], *B;
            Conn *nc;
            bool dup = false;

            if (o == NULL || !layout_ok(w, o->layoutId) || o->layoutId == a)
                continue;
            b = o->layoutId;
            B = &w->layouts[b - 1];
            switch (buf[i].dir) {
            case 1: dx = off; dy = A->h; break;                 /* down */
            case 2: dx = off; dy = -(int)B->h; break;           /* up */
            case 4: dx = A->w; dy = off; break;                 /* right */
            case 3: dx = -(int)B->w; dy = off; break;           /* left */
            default: continue;
            }
            for (k = 0; k < c->nConns[a] && !dup; k++)
                dup = c->conns[a][k].b == b && c->conns[a][k].dx == dx && c->conns[a][k].dy == dy;
            if (dup)
                continue;
            nc = (Conn *)realloc(c->conns[a], (size_t)(c->nConns[a] + 1u) * sizeof(Conn));
            if (nc == NULL)
                return RG_ERR_NOMEM;
            c->conns[a] = nc;
            nc[c->nConns[a]].b = (uint16_t)b;
            nc[c->nConns[a]].dx = dx;
            nc[c->nConns[a]].dy = dy;
            c->nConns[a]++;
        }
    }
    /* the connections assert: no cell outside a layout lies in two neighbours' rectangles */
    for (id = 1; id <= w->layoutCount; id++) {
        unsigned p, q;
        int aw, ah;

        if (c->nConns[id] < 2)
            continue;
        aw = w->layouts[id - 1].w;
        ah = w->layouts[id - 1].h;
        for (p = 0; p < c->nConns[id]; p++)
            for (q = p + 1; q < c->nConns[id]; q++) {
                const Conn *A = &c->conns[id][p], *B = &c->conns[id][q];
                int ax1 = A->dx + w->layouts[A->b - 1].w, ay1 = A->dy + w->layouts[A->b - 1].h;
                int bx1 = B->dx + w->layouts[B->b - 1].w, by1 = B->dy + w->layouts[B->b - 1].h;
                int x0 = A->dx > B->dx ? A->dx : B->dx, y0 = A->dy > B->dy ? A->dy : B->dy;
                int x1 = ax1 < bx1 ? ax1 : bx1, y1 = ay1 < by1 ? ay1 : by1;

                if (x0 < x1 && y0 < y1 && (x0 < 0 || y0 < 0 || x1 > aw || y1 > ah))
                    (*ambiguous)++;
            }
    }
    return RG_OK;
}

/* beyond(): (neighbour layout, x, y) of a cell outside a layout; the first connection that holds it. */
static bool beyond(const ExpCtx *c, unsigned lid, int x, int y, unsigned *b, int *bx, int *by)
{
    unsigned k;

    for (k = 0; k < c->nConns[lid]; k++) {
        const Conn *cn = &c->conns[lid][k];
        const RgLayout *B = &c->w->layouts[cn->b - 1];

        if (0 <= x - cn->dx && x - cn->dx < (int)B->w && 0 <= y - cn->dy && y - cn->dy < (int)B->h) {
            *b = cn->b;
            *bx = x - cn->dx;
            *by = y - cn->dy;
            return true;
        }
    }
    return false;
}

/* upper_subtiles(): {(sx, sy): (tile, palette, flips)}, packed tile | pal << 10 | flips << 14 (0 = absent: a
 * present entry has a non-zero tile). */
static const uint32_t *subs_of(ExpCtx *c, unsigned lid)
{
    const RgLayout *L = &c->w->layouts[lid - 1];
    unsigned sw = 2u * L->w, x, y, q;
    uint32_t *a;

    if (c->subs[lid] != NULL)
        return c->subs[lid];
    a = (uint32_t *)calloc((size_t)sw * 2u * L->h, 4u);
    if (a == NULL)
        return NULL;
    for (y = 0; y < L->h; y++)
        for (x = 0; x < L->w; x++) {
            uint16_t e[8];

            if (!rg_metatile_entries(L, mt_at(L, (int)x, (int)y), e))
                continue;
            for (q = 0; q < 4; q++)
                if (e[4 + q] & 0x3FFu)
                    a[(2u * y + (q >> 1)) * sw + 2u * x + (q & 1u)] =
                        (uint32_t)((e[4 + q] & 0x3FFu) | (((e[4 + q] >> 12) & 0xFu) << 10) | (((e[4 + q] >> 10) & 3u) << 14));
        }
    c->subs[lid] = a;
    return a;
}

/* sub_at(): the packed upper subtile at (sx, sy) of layout `lid`, or across a seam; 0 = None. */
static uint32_t sub_at(ExpCtx *c, unsigned lid, int sx, int sy, bool *nomem)
{
    const RgLayout *L = &c->w->layouts[lid - 1];
    const uint32_t *a;
    unsigned b;
    int bx, by;
    const RgLayout *B;

    if (0 <= sx && sx < 2 * (int)L->w && 0 <= sy && sy < 2 * (int)L->h) {
        a = subs_of(c, lid);
        if (a == NULL) { *nomem = true; return 0; }
        return a[(size_t)sy * 2u * L->w + (size_t)sx];
    }
    if (!beyond(c, lid, fdiv2(sx), fdiv2(sy), &b, &bx, &by))
        return 0;
    B = &c->w->layouts[b - 1];
    if (B->ts[0]->addr != L->ts[0]->addr)
        return 0;
    a = subs_of(c, b);
    if (a == NULL) { *nomem = true; return 0; }
    return a[(size_t)(by * 2 + fmod2(sy)) * 2u * B->w + (size_t)(bx * 2 + fmod2(sx))];
}

typedef struct Found { int x0, y0; unsigned mask; } Found;
typedef struct Hit { uint16_t lid; int x0, y0; unsigned mask; unsigned group; } Hit;

static int cmp_found(const void *a, const void *b)
{
    const Found *x = (const Found *)a, *y = (const Found *)b;

    if (x->x0 != y->x0) return x->x0 < y->x0 ? -1 : 1;
    if (x->y0 != y->y0) return x->y0 < y->y0 ? -1 : 1;
    return x->mask != y->mask ? (x->mask < y->mask ? -1 : 1) : 0;
}

/* find(): the object's top-left subtile and the grid entries drawn there, sorted (x0, y0). */
static RgErr find_obj(ExpCtx *c, unsigned lid, const PObj *obj, Found **res, unsigned *nres)
{
    const RgLayout *L = &c->w->layouts[lid - 1];
    const uint32_t *subs;
    int ai = -1, aj = -1, at = 0, i, j, sx, sy;
    unsigned cap = 0;
    bool nomem = false;

    *res = NULL;
    *nres = 0;
    if (L->ts[0]->addr != rg_lprof(L)->tsGeneral)   /* TS_GENERAL on Emerald; Phase 35: this cartridge's General */
        return RG_OK;
    for (j = 0; j < obj->gh && ai < 0; j++)
        for (i = 0; i < obj->gw; i++) {
            const PGrid *t = &obj->g[j * obj->gw + i];

            if (t->tile >= 0 && !t->optional) {
                ai = i; aj = j; at = t->tile;
                break;
            }
        }
    subs = subs_of(c, lid);
    if (subs == NULL)
        return RG_ERR_NOMEM;
    for (sy = 0; sy < 2 * (int)L->h; sy++)
        for (sx = 0; sx < 2 * (int)L->w; sx++) {
            uint32_t v = subs[(size_t)sy * 2u * L->w + (size_t)sx];
            int x0, y0;
            unsigned present = 0;
            bool ok = true;

            if (v == 0 || (int)(v & 0x3FFu) != at || (int)((v >> 10) & 0xFu) != obj->palette || (v >> 14) != 0)
                continue;
            x0 = sx - ai;
            y0 = sy - aj;
            for (j = 0; j < obj->gh && ok; j++)
                for (i = 0; i < obj->gw; i++) {
                    const PGrid *t = &obj->g[j * obj->gw + i];
                    uint32_t got;

                    if (t->tile < 0)
                        continue;
                    got = sub_at(c, lid, x0 + i, y0 + j, &nomem);
                    if (nomem)
                        return RG_ERR_NOMEM;
                    if (got == (uint32_t)(t->tile | (t->pal << 10)))
                        present |= 1u << (j * obj->gw + i);
                    else if (!t->optional) {
                        ok = false;
                        break;
                    }
                }
            if (ok) {
                if (*nres == cap) {
                    unsigned nc = cap ? cap * 2u : 8u;
                    Found *nf = (Found *)realloc(*res, (size_t)nc * sizeof(Found));

                    if (nf == NULL)
                        return RG_ERR_NOMEM;
                    *res = nf;
                    cap = nc;
                }
                (*res)[*nres].x0 = x0; (*res)[*nres].y0 = y0; (*res)[*nres].mask = present;
                (*nres)++;
            }
        }
    if (*nres > 1)
        qsort(*res, *nres, sizeof(Found), cmp_found);
    return RG_OK;
}

/* cells_of(): the bounding box (min cell, size) of the cells an object found at subtile (sx, sy) is drawn over. */
static void cells_box(const PObj *obj, int sx, int sy, int *cx0, int *cy0, int *cx1, int *cy1)
{
    int i, j;

    *cx0 = *cy0 = 1 << 20;
    *cx1 = *cy1 = -(1 << 20);
    for (j = 0; j < obj->gh; j++)
        for (i = 0; i < obj->gw; i++) {
            int cx, cy;

            if (obj->g[j * obj->gw + i].tile < 0)
                continue;
            cx = fdiv2(sx + i);
            cy = fdiv2(sy + j);
            if (cx < *cx0) *cx0 = cx;
            if (cy < *cy0) *cy0 = cy;
            if (cx > *cx1) *cx1 = cx;
            if (cy > *cy1) *cy1 = cy;
        }
}

static void ctx_free(ExpCtx *c)
{
    unsigned id;

    if (c->subs != NULL)
        for (id = 0; id <= c->w->layoutCount; id++)
            free(c->subs[id]);
    free(c->subs);
    if (c->conns != NULL)
        for (id = 0; id <= c->w->layoutCount; id++)
            free(c->conns[id]);
    free(c->conns);
    free(c->nConns);
    c->subs = NULL; c->conns = NULL; c->nConns = NULL;
}

RgErr rg_expand_props(const RgWorld *w, const RgSpec *s, RgBuildModels *ms)
{
    const RgPropsCfg *cfg = (const RgPropsCfg *)s->ext;
    const PObj *obj = &kObjects[cfg->object];
    ExpCtx ctx;
    Hit *hits = NULL;
    unsigned nHits = 0, capHits = 0, id, g, k;
    unsigned nGroups = 0;
    struct { int ox, oy; unsigned mask, count; } groups[64];
    unsigned order[64];
    RgErr err = RG_OK;
    unsigned first = ms->n, nOut = 0;
    RgPairCache pc;

    memset(&ctx, 0, sizeof(ctx));
    memset(&pc, 0, sizeof(pc));
    ctx.w = w;
    pc.w = w; pc.idx = -1;
    ctx.subs = (uint32_t **)calloc((size_t)w->layoutCount + 1u, sizeof(uint32_t *));
    if (ctx.subs == NULL)
        return RG_ERR_NOMEM;
    err = build_conns(&ctx, &ms->connAmbiguous);
    /* everywhere(): layouts in id order (upstream sorts by name: divergence A2), each layout's finds sorted */
    for (id = 1; id <= w->layoutCount && err == RG_OK; id++) {
        Found *res;
        unsigned n;

        if (!layout_ok(w, id))
            continue;
        err = find_obj(&ctx, id, obj, &res, &n);
        for (k = 0; k < n && err == RG_OK; k++) {
            unsigned gi;
            int ox = fmod2(res[k].x0), oy = fmod2(res[k].y0);

            for (gi = 0; gi < nGroups; gi++)
                if (groups[gi].ox == ox && groups[gi].oy == oy && groups[gi].mask == res[k].mask)
                    break;
            if (gi == nGroups) {
                if (nGroups == 64) { err = RG_ERR_TOO_BIG; break; }
                groups[nGroups].ox = ox; groups[nGroups].oy = oy; groups[nGroups].mask = res[k].mask;
                groups[nGroups].count = 0;
                nGroups++;
            }
            groups[gi].count++;
            if (nHits == capHits) {
                unsigned cap = capHits ? capHits * 2u : 64u;
                Hit *nh = (Hit *)realloc(hits, (size_t)cap * sizeof(Hit));

                if (nh == NULL) { err = RG_ERR_NOMEM; break; }
                hits = nh;
                capHits = cap;
            }
            hits[nHits].lid = (uint16_t)id; hits[nHits].x0 = res[k].x0; hits[nHits].y0 = res[k].y0;
            hits[nHits].mask = res[k].mask; hits[nHits].group = gi;
            nHits++;
        }
        free(res);
    }
    /* sorted(groups.items(), key=-len): stable, by size descending */
    for (g = 0; g < nGroups; g++) {
        unsigned q = g;

        while (q > 0 && groups[order[q - 1]].count < groups[g].count) {
            order[q] = order[q - 1];
            q--;
        }
        order[q] = g;
    }
    for (g = 1; g < nGroups && err == RG_OK; g++)
        if (groups[order[g]].count == groups[order[g - 1]].count)
            ms->propTies++;
    for (g = 0; g < nGroups && err == RG_OK; g++) {
        unsigned gi = order[g], firstHit = 0, nw = 0;
        const Hit *h0;
        int cx0, cy0, cx1, cy1, bw, bh, i, j, ox, oy;
        RgBuildModel *m;
        RgImage art;
        RgPair *pair;

        while (hits[firstHit].group != gi)
            firstHit++;
        h0 = &hits[firstHit];
        cells_box(obj, h0->x0, h0->y0, &cx0, &cy0, &cx1, &cy1);
        bw = cx1 - cx0 + 1;
        bh = cy1 - cy0 + 1;
        if (bw > 255 || bh > 255) { err = RG_ERR_TOO_BIG; break; }
        m = new_model(ms, s, &w->layouts[h0->lid - 1], cx0, cy0, bw, bh,
                      "%s_%u", nOut, 0, 0, RG_SPEC_PROPS);
        if (m == NULL) { err = RG_ERR_NOMEM; break; }
        nOut++;
        m->prop = cfg;
        m->quads = (uint8_t *)calloc((size_t)bw * bh, 1);
        if (m->quads == NULL || !rg_img_new(&art, bw * 16, bh * 16)) { err = RG_ERR_NOMEM; break; }
        pair = rg_pc_get(&pc, w->layouts[h0->lid - 1].pairIndex);
        if (pair == NULL) { rg_img_free(&art); err = RG_ERR_NOMEM; break; }
        ox = fmod2(h0->x0);
        oy = fmod2(h0->y0);
        for (j = 0; j < obj->gh; j++)
            for (i = 0; i < obj->gw; i++) {
                const PGrid *t = &obj->g[j * obj->gw + i];
                uint16_t col[64];
                uint8_t idx[64];
                int X, Y, kk;

                if (t->tile < 0 || !((h0->mask >> (j * obj->gw + i)) & 1u))
                    continue;
                rg_subtile_px(pair, (uint16_t)t->tile, (uint8_t)t->pal, col, idx);
                X = ox * 8 + i * 8;
                Y = oy * 8 + j * 8;
                for (kk = 0; kk < 64; kk++)
                    if (idx[kk]) {
                        int px = X + kk % 8, py = Y + kk / 8;

                        if (px >= art.w || py >= art.h) { err = RG_ERR_BUILDINGS; break; }
                        rg_c5_rgba(col[kk], 255, art.px + ((size_t)py * (size_t)art.w + (size_t)px) * 4u);
                    }
                if (err != RG_OK)
                    break;
                if (Y / 16 >= bh || X / 16 >= bw) { err = RG_ERR_BUILDINGS; break; }
                m->quads[(Y / 16) * bw + X / 16] |= (uint8_t)(1u << ((Y % 16) / 8 * 2 + (X % 16) / 8));
            }
        if (err != RG_OK) { rg_img_free(&art); break; }
        m->drawing = art;
        m->hasDrawing = true;
        {
            int ring[RG_MOUND_RING][3];

            for (k = 0; k < cfg->nRing; k++) {
                ring[k][0] = cfg->ring[k][0]; ring[k][1] = cfg->ring[k][1]; ring[k][2] = cfg->ring[k][2];
            }
            if (!rg_mound_with_ring(&m->drawing, (const int (*)[3])ring, cfg->nRing, &m->art)) { err = RG_ERR_NOMEM; break; }
        }
        m->artReady = true;
        for (k = firstHit; k < nHits; k++)
            if (hits[k].group == gi)
                nw++;
        m->at = (RgAt *)malloc((size_t)nw * sizeof(RgAt));
        if (m->at == NULL) { err = RG_ERR_NOMEM; break; }
        for (k = firstHit; k < nHits; k++)
            if (hits[k].group == gi) {
                int a0, b0, a1, b1;

                cells_box(obj, hits[k].x0, hits[k].y0, &a0, &b0, &a1, &b1);
                m->at[m->nAt].lid = hits[k].lid;
                m->at[m->nAt].x = (int16_t)a0;
                m->at[m->nAt].y = (int16_t)b0;
                m->nAt++;
            }
    }
    if (err == RG_OK)
        ms->nProps += ms->n - first;
    rg_pc_close(&pc);
    free(hits);
    ctx_free(&ctx);
    return err;
}

RgErr rg_build_prop(RgBuildModel *m)
{
    const RgPropsCfg *cfg = m->prop;
    RgMound mo;
    unsigned k;

    memset(&mo, 0, sizeof(mo));
    mo.full = &m->art;
    mo.rise = cfg->rise;
    mo.step = cfg->step;
    mo.backSteps = 3;
    mo.rows = m->h * 16;
    mo.nRing = cfg->nRing;
    for (k = 0; k < cfg->nRing; k++) {
        mo.ring[k][0] = cfg->ring[k][0]; mo.ring[k][1] = cfg->ring[k][1]; mo.ring[k][2] = cfg->ring[k][2];
    }
    return rg_emit_mound(&mo, "mound", &m->mesh) && !m->mesh.failed ? RG_OK : RG_ERR_BUILDINGS;
}

/* ---- G8: voxel_props.cells_in made public (props:225-242), SPEC-S3 1.4 ----------------------------------- */

struct RgProps { ExpCtx c; };

RgProps *rg_props_open(const RgWorld *w, RgErr *err)
{
    RgProps *p = (RgProps *)calloc(1, sizeof(RgProps));
    unsigned ambiguous = 0;
    RgErr e = RG_OK;

    assert(w != NULL);
    if (p != NULL) {
        p->c.w = w;
        p->c.subs = (uint32_t **)calloc((size_t)w->layoutCount + 1u, sizeof(uint32_t *));
        e = p->c.subs != NULL ? build_conns(&p->c, &ambiguous) : RG_ERR_NOMEM;
    } else
        e = RG_ERR_NOMEM;
    if (e != RG_OK) {
        if (p != NULL)
            ctx_free(&p->c);
        free(p);
        p = NULL;
    }
    if (err != NULL)
        *err = e;
    return p;
}

void rg_props_close(RgProps *p)
{
    if (p != NULL) {
        ctx_free(&p->c);
        free(p);
    }
}

/* Marks (bit o of cellFlags) every cell of layout `lid` that object o is drawn over, for finds in layout `src`
 * placed at (dx, dy). Cells that lie outside src are the seam's: only those count for a neighbour (props:235-241). */
static RgErr mark_cells(ExpCtx *c, const PObj *obj, unsigned o, unsigned lid, unsigned src, int dx, int dy,
                        uint8_t *flags)
{
    const RgLayout *L = &c->w->layouts[lid - 1u], *S = &c->w->layouts[src - 1u];
    Found *res;
    unsigned n, k;
    RgErr e = find_obj(c, src, obj, &res, &n);

    for (k = 0; k < n && e == RG_OK; k++) {
        int i, j;

        for (j = 0; j < obj->gh; j++)
            for (i = 0; i < obj->gw; i++) {
                int x, y;

                if (obj->g[j * obj->gw + i].tile < 0)
                    continue;
                x = fdiv2(res[k].x0 + i);
                y = fdiv2(res[k].y0 + j);
                if (src != lid && 0 <= x && x < (int)S->w && 0 <= y && y < (int)S->h)
                    continue;
                x += dx;
                y += dy;
                if (0 <= x && x < (int)L->w && 0 <= y && y < (int)L->h)
                    flags[(size_t)y * L->w + (size_t)x] |= (uint8_t)(1u << o);
            }
    }
    free(res);
    return e;
}

RgErr rg_props_cells(RgProps *p, uint16_t layoutId, uint8_t *cellFlags)
{
    ExpCtx *c;
    const RgLayout *L;
    unsigned o, k;
    RgErr e = RG_OK;

    assert(p != NULL && cellFlags != NULL);
    c = &p->c;
    if (!layout_ok(c->w, layoutId))
        return RG_ERR_LAYOUT_TABLE;
    L = &c->w->layouts[layoutId - 1u];
    memset(cellFlags, 0, (size_t)L->w * L->h);
    for (o = 0; o < RG_OBJ_COUNT && e == RG_OK; o++) {
        e = mark_cells(c, &kObjects[o], o, layoutId, layoutId, 0, 0, cellFlags);
        for (k = 0; k < c->nConns[layoutId] && e == RG_OK; k++)
            if (layout_ok(c->w, c->conns[layoutId][k].b))
                e = mark_cells(c, &kObjects[o], o, layoutId, c->conns[layoutId][k].b,
                               c->conns[layoutId][k].dx, c->conns[layoutId][k].dy, cellFlags);
    }
    return e;
}

RgErr rg_props_cells_in(const RgWorld *w, uint16_t layoutId, uint8_t *cellFlags)
{
    RgErr e;
    RgProps *p = rg_props_open(w, &e);

    if (p == NULL)
        return e;
    e = rg_props_cells(p, layoutId, cellFlags);
    rg_props_close(p);
    return e;
}
