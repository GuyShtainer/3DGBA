/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/gen_voxel_buildings.py
 * (build_models, cell_heights, cell_footprints, find_placements, same_building_pixels, ground_patch,
 * placement_patches, pack_atlas, texel_offset, export), MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
#include "rg_buildings.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- helpers ------------------------------------------------------------------------------- */

uint32_t rg_layout_fnv(const RgLayout *L)
{
    uint32_t h = 2166136261u;
    size_t i, n = (size_t)L->w * L->h * 2u;

    if (L->blocks == NULL)
        return 0;
    for (i = 0; i < n; i++) {
        h ^= L->blocks[i];
        h *= 16777619u;
    }
    return h;
}

typedef struct PairCache { const RgWorld *w; RgPair *p; int idx; } PairCache;

static RgPair *pc_get(PairCache *c, uint16_t idx)
{
    if (c->p != NULL && c->idx == (int)idx)
        return c->p;
    if (c->p != NULL)
        rg_pair_close(c->p);
    c->p = rg_pair_open(c->w, idx);
    c->idx = c->p != NULL ? (int)idx : -1;
    return c->p;
}

static void pc_close(PairCache *c)
{
    if (c->p != NULL)
        rg_pair_close(c->p);
    c->p = NULL;
    c->idx = -1;
}

/* ---- models -------------------------------------------------------------------------------- */

void rg_models_free(RgBuildModels *ms)
{
    unsigned i;

    for (i = 0; i < ms->n; i++) {
        rg_img_free(&ms->m[i].art);
        rg_mesh_free(&ms->m[i].mesh);
        free(ms->m[i].owned);
        free(ms->m[i].own);
    }
    free(ms->m);
    memset(ms, 0, sizeof(*ms));
}

RgErr rg_build_models(const RgWorld *w, const RgSpec *specs, unsigned nSpecs, RgBuildModels *out)
{
    PairCache pc;
    unsigned i;
    RgErr err = RG_OK;

    memset(out, 0, sizeof(*out));
    pc.w = w; pc.p = NULL; pc.idx = -1;
    out->m = (RgBuildModel *)calloc(nSpecs ? nSpecs : 1u, sizeof(RgBuildModel));
    if (out->m == NULL)
        return RG_ERR_NOMEM;
    for (i = 0; i < nSpecs; i++) {
        const RgSpec *s = &specs[i];
        const RgLayout *L;
        RgBuildModel *m;
        RgPartList parts;
        RgPair *pair;
        bool ok;

        if (s->kind != RG_SPEC_DIRECT) {              /* components / kit / props / interior: S2.5-S2.6 */
            err = RG_ERR_BUILDINGS;
            break;
        }
        if (s->layoutId == 0 || s->layoutId > w->layoutCount) {
            if (out->skipped < RG_MAX_SKIPPED) out->skippedNames[out->skipped] = s->name;
            out->skipped++;
            continue;
        }
        L = &w->layouts[s->layoutId - 1];
        if (!L->present || rg_layout_fnv(L) != s->layoutFnv) {
            if (out->skipped < RG_MAX_SKIPPED) out->skippedNames[out->skipped] = s->name;
            out->skipped++;
            continue;
        }
        pair = pc_get(&pc, L->pairIndex);
        if (pair == NULL) {
            err = RG_ERR_NOMEM;
            break;
        }
        m = &out->m[out->n];
        m->spec = s;
        m->layout = L;
        m->w = (uint8_t)s->rect[2];
        m->h = (uint8_t)s->rect[3];
        m->groundMetatile = s->ground[0];
        rg_mesh_init(&m->mesh);
        out->n++;                                      /* from here rg_models_free owns it */
        if (!rg_building_art(w, pair, L, s->rect[0], s->rect[1], s->rect[2], s->rect[3], s->ground, s->nGround, NULL,
                             false, &m->art)) {
            err = RG_ERR_BUILDINGS;
            break;
        }
        rg_parts_init(&parts);
        ok = s->parts(s, s->arg0, s->arg1, &parts) && rg_parts_emit(&parts, &m->mesh);
        rg_parts_free(&parts);
        if (!ok || m->mesh.failed) {
            err = RG_ERR_BUILDINGS;
            break;
        }
    }
    pc_close(&pc);
    if (err != RG_OK)
        rg_models_free(out);
    return err;
}

bool rg_model_gate(const RgBuildModel *m, RgOrthoResult *ortho, unsigned *densityBad)
{
    const RgSpec *s = m->spec;

    if (!rg_ortho_check(&m->mesh, &m->art, s->exact, s->nExact, NULL, ortho))
        return false;
    *densityBad = rg_density_check(&m->mesh, &m->art, NULL, 0);
    return true;
}

/* ---- cell heights and footprints ----------------------------------------------------------- */

static double area_xz(const RgPt *p, unsigned n)
{
    double terms[RG_CLIP_MAX];
    unsigned i;

    for (i = 0; i < n; i++)
        terms[i] = p[i].c[0] * p[(i + 1) % n].c[2] - p[(i + 1) % n].c[0] * p[i].c[2];
    return fabs(rg_pysum(terms, n)) / 2;
}

bool rg_cell_heights(const RgBuildModel *m, uint8_t *out)
{
    unsigned w = m->w, h = m->h, i, cx, cy;
    double *tops = (double *)calloc((size_t)w * h, sizeof(double));

    if (tops == NULL)
        return false;
    for (i = 0; i < m->mesh.n; i++) {
        const RgTri *t = &m->mesh.t[i];
        RgPt pts[3];
        double x0, x1, z0, z1;
        unsigned k;

        memset(pts, 0, sizeof(pts));
        for (k = 0; k < 3; k++) {
            pts[k].c[0] = t->p[k].x; pts[k].c[1] = t->p[k].y; pts[k].c[2] = t->p[k].z;
        }
        x0 = x1 = pts[0].c[0];
        z0 = z1 = pts[0].c[2];
        for (k = 1; k < 3; k++) {
            if (pts[k].c[0] < x0) x0 = pts[k].c[0];
            if (pts[k].c[0] > x1) x1 = pts[k].c[0];
            if (pts[k].c[2] < z0) z0 = pts[k].c[2];
            if (pts[k].c[2] > z1) z1 = pts[k].c[2];
        }
        x0 = x0 - 2 * RG_EPS; x1 = x1 + 2 * RG_EPS;
        z0 = z0 - 2 * RG_EPS; z1 = z1 + 2 * RG_EPS;
        for (cy = 0; cy < h; cy++) {
            if (z1 < cy * 16.0 || z0 > cy * 16.0 + 16)
                continue;
            for (cx = 0; cx < w; cx++) {
                RgPt a[RG_CLIP_MAX], b[RG_CLIP_MAX];
                unsigned n, s;
                static const int axes[3] = {0, 2, 2};
                static const bool keeps[3] = {false, true, false};

                if (x1 < cx * 16.0 || x0 > cx * 16.0 + 16)
                    continue;
                n = rg_clip(pts, 3, 3, 0, cx * 16.0, true, a);
                for (s = 0; s < 3; s++) {
                    double val = s == 0 ? cx * 16.0 + 16 : s == 1 ? cy * 16.0 : cy * 16.0 + 16;

                    if (n >= 3) {
                        n = rg_clip(a, n, 3, (unsigned)axes[s], val, keeps[s], b);
                        memcpy(a, b, n * sizeof(RgPt));
                    }
                }
                if (n >= 3 && area_xz(a, n) > 1e-6) {
                    double top = a[0].c[1];
                    unsigned idx = cy * w + cx;

                    for (k = 1; k < n; k++)
                        if (a[k].c[1] > top)
                            top = a[k].c[1];
                    if (top > tops[idx])
                        tops[idx] = top;
                }
            }
        }
    }
    for (i = 0; i < w * h; i++) {
        int v = (int)nearbyint(tops[i]);          /* Python round(): half to even */

        out[i] = (uint8_t)(v < 255 ? v : 255);
    }
    free(tops);
    return true;
}

void rg_masks_free(RgMaskSet *s)
{
    free(s->rows);
    memset(s, 0, sizeof(*s));
}

bool rg_cell_footprints(const RgBuildModel *m, int32_t *maskOf, RgMaskSet *masks)
{
    unsigned w = m->w, h = m->h, i, cx, cy, z, x;
    size_t gw = (size_t)w * 16u, gh = (size_t)h * 16u;
    uint8_t *grid;

    for (i = 0; i < w * h; i++)
        maskOf[i] = -1;
    if (m->spec->kind == RG_SPEC_INTERIOR)
        return true;
    grid = (uint8_t *)calloc(gw * gh, 1);
    if (grid == NULL)
        return false;
    for (i = 0; i < m->mesh.n; i++) {
        const RgTri *t = &m->mesh.t[i];
        double ax = t->p[0].x, az = t->p[0].z, bx = t->p[1].x, bz = t->p[1].z, cx2 = t->p[2].x, cz = t->p[2].z;
        double area, mnx, mxx, mnz, mxz, ymax;
        long x0, x1, z0, z1, pz, px;

        ymax = t->p[0].y;
        if (t->p[1].y > ymax) ymax = t->p[1].y;
        if (t->p[2].y > ymax) ymax = t->p[2].y;
        if (ymax < 1.0)
            continue;                                   /* a decal or a patch on the ground casts nothing */
        area = (bx - ax) * (cz - az) - (bz - az) * (cx2 - ax);
        if (fabs(area) < 1e-6)
            continue;
        mnx = ax < bx ? ax : bx; if (cx2 < mnx) mnx = cx2;
        mxx = ax > bx ? ax : bx; if (cx2 > mxx) mxx = cx2;
        mnz = az < bz ? az : bz; if (cz < mnz) mnz = cz;
        mxz = az > bz ? az : bz; if (cz > mxz) mxz = cz;
        x0 = (long)floor(mnx); if (x0 < 0) x0 = 0;
        x1 = (long)ceil(mxx);  if (x1 > (long)gw) x1 = (long)gw;
        z0 = (long)floor(mnz); if (z0 < 0) z0 = 0;
        z1 = (long)ceil(mxz);  if (z1 > (long)gh) z1 = (long)gh;
        for (pz = z0; pz < z1; pz++) {
            for (px = x0; px < x1; px++) {
                double qx = px + 0.5, qz = pz + 0.5;
                double d1 = (bx - ax) * (qz - az) - (bz - az) * (qx - ax);
                double d2 = (cx2 - bx) * (qz - bz) - (cz - bz) * (qx - bx);
                double d3 = (ax - cx2) * (qz - cz) - (az - cz) * (qx - cx2);

                if ((d1 >= 0 && d2 >= 0 && d3 >= 0) || (d1 <= 0 && d2 <= 0 && d3 <= 0))
                    grid[(size_t)pz * gw + (size_t)px] = 1;
            }
        }
    }
    for (cy = 0; cy < h; cy++) {
        for (cx = 0; cx < w; cx++) {
            uint16_t rows[16];
            unsigned n = 0;

            for (z = 0; z < 16; z++) {
                unsigned r = 0;

                for (x = 0; x < 16; x++)
                    if (grid[((size_t)cy * 16u + z) * gw + (size_t)cx * 16u + x]) {
                        r |= 1u << x;
                        n++;
                    }
                rows[z] = (uint16_t)r;
            }
            if (n > 0 && n < RG_FOOTPRINT_FULL) {
                unsigned k;

                for (k = 0; k < masks->n; k++)
                    if (memcmp(masks->rows[k], rows, sizeof(rows)) == 0)
                        break;
                if (k == masks->n) {
                    if (masks->n == masks->cap) {
                        unsigned cap = masks->cap ? masks->cap * 2u : 16u;
                        uint16_t (*nr)[16] = (uint16_t (*)[16])realloc(masks->rows, (size_t)cap * sizeof(*nr));

                        if (nr == NULL) {
                            free(grid);
                            return false;
                        }
                        masks->rows = nr;
                        masks->cap = cap;
                    }
                    memcpy(masks->rows[masks->n++], rows, sizeof(rows));
                }
                maskOf[cy * w + cx] = (int32_t)k;
            }
        }
    }
    free(grid);
    return true;
}

/* ---- placements ---------------------------------------------------------------------------- */

void rg_placements_free(RgPlacementList *l)
{
    unsigned i, k;

    for (i = 0; i < l->n; i++) {
        free(l->p[i].odd);
        for (k = 0; k < l->p[i].nCells; k++)
            rg_img_free(&l->p[i].cells[k].img);
        free(l->p[i].cells);
    }
    free(l->p);
    memset(l, 0, sizeof(*l));
}

/* gen:1191-1206: does `metatile`, drawn by layout `cand`, show every pixel the model owns in cell (i, j)? */
static bool same_building_pixels(PairCache *pc, const RgLayout *cand, const RgBuildModel *m, unsigned metatile,
                                 unsigned i, unsigned j, bool *nomem)
{
    RgImage img;
    unsigned owned = 0, x, y;
    bool same = true;
    RgPair *pair = pc_get(pc, cand->pairIndex);

    if (pair == NULL || !rg_cell_image(pair, (uint16_t)metatile, &img)) {
        *nomem = true;
        return false;
    }
    for (y = 0; y < 16 && same; y++) {
        for (x = 0; x < 16; x++) {
            const uint8_t *a = m->art.px + (((size_t)j * 16u + y) * (size_t)m->art.w + (size_t)i * 16u + x) * 4u;
            const uint8_t *p = img.px + ((size_t)y * 16u + x) * 4u;

            if (a[3] < 128)
                continue;
            owned++;
            if (p[0] != a[0] || p[1] != a[1] || p[2] != a[2]) {
                same = false;
                break;
            }
        }
    }
    rg_img_free(&img);
    return same && owned > 0;
}

static RgErr push_placement(RgPlacementList *l, const RgPlacement *pl)
{
    if (l->n == l->cap) {
        unsigned cap = l->cap ? l->cap * 2u : 8u;
        RgPlacement *np = (RgPlacement *)realloc(l->p, (size_t)cap * sizeof(RgPlacement));

        if (np == NULL)
            return RG_ERR_NOMEM;
        l->p = np;
        l->cap = cap;
    }
    l->p[l->n++] = *pl;
    return RG_OK;
}

RgErr rg_find_placements(const RgWorld *w, const RgBuildModel *m, RgPlacementList *out)
{
    const RgSpec *s = m->spec;
    const RgLayout *ref = m->layout;
    unsigned cw = m->w, ch = m->h, r0 = 0, r1 = ch, i, j, idx;
    int x = s->rect[0], y = s->rect[1];
    uint16_t template_[256];
    unsigned core[256][2], nCore = 0;
    bool primaryOnly = true, nomem = false;
    PairCache pc;
    RgErr err = RG_OK;

    memset(out, 0, sizeof(*out));
    if (s->kind != RG_SPEC_DIRECT || cw * ch > 256u || m->owned != NULL)
        return RG_ERR_BUILDINGS;
    if (s->matchRows[0] != 0 || s->matchRows[1] != 0) {
        r0 = (unsigned)s->matchRows[0];
        r1 = (unsigned)s->matchRows[1];
    }
    for (j = 0; j < ch; j++)
        for (i = 0; i < cw; i++)
            template_[j * cw + i] = rg_metatile(ref, x + (int)i, y + (int)j);
    for (j = r0; j < r1; j++)                           /* core sorted by (j, i), all cells owned */
        for (i = 0; i < cw; i++) {
            core[nCore][0] = i;
            core[nCore][1] = j;
            nCore++;
            if (template_[j * cw + i] >= RG_NUM_PRIMARY)
                primaryOnly = false;
        }
    if (nCore == 0)
        return RG_OK;
    pc.w = w; pc.p = NULL; pc.idx = -1;
    for (idx = 0; idx < w->layoutCount && err == RG_OK; idx++) {
        const RgLayout *E = &w->layouts[idx];
        unsigned lw = E->w, lh = E->h, i0 = core[0][0], j0 = core[0][1];
        int py, px;

        if (!E->present || E->blocks == NULL)
            continue;
        if (E->ts[0]->addr != ref->ts[0]->addr)
            continue;
        if (!primaryOnly && E->ts[1]->addr != ref->ts[1]->addr)
            continue;
        for (py = 0; py + (int)ch <= (int)lh; py++) {
            for (px = 0; px + (int)cw <= (int)lw; px++) {
                bool all = true;
                unsigned k;
                int ring[1024][2], nRing = 0, yy, xx;
                uint16_t ground;
                RgPlacement pl;

                if ((rg_rd16(E->blocks + 2u * ((size_t)(py + (int)j0) * lw + (size_t)(px + (int)i0))) & 0x3FFu)
                    != template_[j0 * cw + i0])
                    continue;
                for (k = 0; k < nCore && all; k++) {
                    unsigned ci = core[k][0], cj = core[k][1];
                    unsigned got = rg_rd16(E->blocks + 2u * ((size_t)(py + (int)cj) * lw + (size_t)(px + (int)ci))) & 0x3FFu;

                    if (got == template_[cj * cw + ci])
                        continue;
                    if (!same_building_pixels(&pc, E, m, got, ci, cj, &nomem))
                        all = false;
                    if (nomem) {
                        err = RG_ERR_NOMEM;
                        goto done;
                    }
                }
                if (!all)
                    continue;
                for (yy = py - 1; yy < py + (int)ch + 1; yy++) {
                    for (xx = px - 1; xx < px + (int)cw + 1; xx++) {
                        unsigned cell, mt, q;
                        bool inside = px <= xx && xx < px + (int)cw && py <= yy && yy < py + (int)ch;

                        if (inside || xx < 0 || yy < 0 || xx >= (int)lw || yy >= (int)lh)
                            continue;
                        cell = rg_rd16(E->blocks + 2u * ((size_t)yy * lw + (size_t)xx));
                        if (cell & 0xC00u)
                            continue;
                        mt = cell & 0x3FFu;
                        for (q = 0; q < (unsigned)nRing; q++)
                            if ((unsigned)ring[q][0] == mt)
                                break;
                        if (q == (unsigned)nRing) {
                            ring[nRing][0] = (int)mt;
                            ring[nRing][1] = 0;
                            nRing++;
                        }
                        ring[q][1]++;
                    }
                }
                ground = m->groundMetatile;
                if (nRing > 0) {                       /* first maximum in first-seen order */
                    int best = 0, b;

                    for (b = 1; b < nRing; b++)
                        if (ring[b][1] > ring[best][1])
                            best = b;
                    ground = (uint16_t)ring[best][0];
                }
                memset(&pl, 0, sizeof(pl));
                pl.layout = E->id;
                pl.px = (int16_t)px;
                pl.py = (int16_t)py;
                pl.ground = ground;
                pl.odd = (uint8_t (*)[2])malloc((size_t)cw * ch * 2u);
                if (pl.odd == NULL) {
                    err = RG_ERR_NOMEM;
                    goto done;
                }
                for (j = 0; j < ch; j++)
                    for (i = 0; i < cw; i++)
                        if ((rg_rd16(E->blocks + 2u * ((size_t)(py + (int)j) * lw + (size_t)(px + (int)i))) & 0x3FFu)
                            != template_[j * cw + i]) {
                            pl.odd[pl.nOdd][0] = (uint8_t)i;
                            pl.odd[pl.nOdd][1] = (uint8_t)j;
                            pl.nOdd++;
                        }
                if (push_placement(out, &pl) != RG_OK) {
                    free(pl.odd);
                    err = RG_ERR_NOMEM;
                    goto done;
                }
            }
        }
    }
done:
    pc_close(&pc);
    if (err != RG_OK)
        rg_placements_free(out);
    return err;
}

RgErr rg_placement_patches(const RgWorld *w, const RgBuildModel *m, RgPlacement *pl)
{
    unsigned cw = m->w, ch = m->h, k;
    uint8_t *tops = (uint8_t *)malloc((size_t)cw * ch);
    const RgLayout *E = &w->layouts[pl->layout - 1];
    PairCache pc;
    RgErr err = RG_OK;

    if (tops == NULL || !rg_cell_heights(m, tops)) {
        free(tops);
        return RG_ERR_NOMEM;
    }
    pc.w = w; pc.p = NULL; pc.idx = -1;
    pl->cells = (RgPatchCell *)calloc(pl->nOdd ? pl->nOdd : 1u, sizeof(RgPatchCell));
    pl->nCells = 0;
    if (pl->cells == NULL) {
        free(tops);
        return RG_ERR_NOMEM;
    }
    for (k = 0; k < pl->nOdd; k++) {
        unsigned i = pl->odd[k][0], j = pl->odd[k][1], x, y;
        RgImage img;
        RgPair *pair;
        bool any = false;

        if (tops[j * cw + i] != 0)
            continue;                                  /* the model stands there; the map's own paint is lost */
        pair = pc_get(&pc, E->pairIndex);
        if (pair == NULL || !rg_cell_image(pair, rg_metatile(E, pl->px + (int)i, pl->py + (int)j), &img)) {
            err = RG_ERR_NOMEM;
            break;
        }
        for (y = 0; y < 16; y++)
            for (x = 0; x < 16; x++) {
                const uint8_t *a = m->art.px + (((size_t)j * 16u + y) * (size_t)m->art.w + (size_t)i * 16u + x) * 4u;
                uint8_t *p = img.px + ((size_t)y * 16u + x) * 4u;

                if (a[3] >= 128)
                    p[0] = p[1] = p[2] = p[3] = 0;
            }
        {
            int bb[4];

            any = rg_img_bbox(&img, bb);
        }
        if (!any) {
            rg_img_free(&img);
            continue;
        }
        pl->cells[pl->nCells].i = (uint8_t)i;
        pl->cells[pl->nCells].j = (uint8_t)j;
        pl->cells[pl->nCells].img = img;
        pl->cells[pl->nCells].patch = -1;
        pl->nCells++;
    }
    pc_close(&pc);
    free(tops);
    return err;
}

/* ---- atlas --------------------------------------------------------------------------------- */

uint32_t rg_texel_offset(unsigned x, unsigned y, unsigned width)
{
    uint32_t morton = 0;
    unsigned bit;

    for (bit = 0; bit < 3; bit++)
        morton |= (((x >> bit) & 1u) << (2 * bit)) | (((y >> bit) & 1u) << (2 * bit + 1));
    return ((y / 8u) * (width / 8u) + x / 8u) * 64u + morton;
}

typedef struct AtlasItem { unsigned idx; int w, h; } AtlasItem;
static int cmp_atlas(const void *a, const void *b)
{
    const AtlasItem *x = (const AtlasItem *)a, *y = (const AtlasItem *)b;

    if (x->h != y->h)
        return x->h > y->h ? -1 : 1;                    /* (-h, -w) */
    if (x->w != y->w)
        return x->w > y->w ? -1 : 1;
    return 0;
}

typedef struct AtlasSize { int w, h; } AtlasSize;
static int cmp_size(const void *a, const void *b)
{
    const AtlasSize *x = (const AtlasSize *)a, *y = (const AtlasSize *)b;
    int ax = x->w * x->h, ay = y->w * y->h;

    if (ax != ay)
        return ax < ay ? -1 : 1;
    if (x->h != y->h)
        return x->h < y->h ? -1 : 1;
    return 0;
}

static bool skyline(const AtlasItem *order, unsigned n, int tw, int th, int *sky, int (*spots)[2])
{
    unsigned k;
    int x;

    memset(sky, 0, (size_t)tw * sizeof(int));
    for (k = 0; k < n; k++) {
        int aw = order[k].w, ah = order[k].h, bestY = 0, bestX = 0;
        bool have = false;

        if (aw > tw)
            return false;
        for (x = 0; x + aw <= tw; x++) {
            int yv = sky[x], q;

            for (q = x + 1; q < x + aw; q++)
                if (sky[q] > yv)
                    yv = sky[q];
            if (yv + ah <= th && (!have || yv < bestY)) {
                bestY = yv;
                bestX = x;
                have = true;
            }
        }
        if (!have)
            return false;
        spots[order[k].idx][0] = bestX;
        spots[order[k].idx][1] = bestY;
        for (x = bestX; x < bestX + aw; x++)
            sky[x] = bestY + ah;
    }
    return true;
}

bool rg_pack_atlas(const RgImage *const *imgs, unsigned n, int *tw, int *th, int (*spots)[2])
{
    static const int dims[5] = {64, 128, 256, 512, 1024};
    AtlasSize sizes[25];
    unsigned ns = 0, i, a, b;
    AtlasItem *order;
    int sky[1024];

    order = (AtlasItem *)malloc((size_t)(n ? n : 1u) * sizeof(AtlasItem));
    if (order == NULL)
        return false;
    for (i = 0; i < n; i++) {
        order[i].idx = i;
        order[i].w = imgs[i]->w;
        order[i].h = imgs[i]->h;
    }
    if (!rg_stable_sort(order, n, sizeof(AtlasItem), cmp_atlas)) {
        free(order);
        return false;
    }
    for (a = 0; a < 5; a++)
        for (b = 0; b < 5; b++)
            if (dims[a] <= 8 * dims[b] && dims[b] <= 8 * dims[a]) {
                sizes[ns].w = dims[a];
                sizes[ns].h = dims[b];
                ns++;
            }
    if (!rg_stable_sort(sizes, ns, sizeof(AtlasSize), cmp_size)) {
        free(order);
        return false;
    }
    for (i = 0; i < ns; i++)
        if (skyline(order, n, sizes[i].w, sizes[i].h, sky, spots)) {
            *tw = sizes[i].w;
            *th = sizes[i].h;
            free(order);
            return true;
        }
    free(order);
    return false;
}

/* ---- the writer ---------------------------------------------------------------------------- */

typedef struct Found { uint16_t lid; unsigned model; RgPlacement *pl; unsigned seq; } Found;
typedef struct OutPlacement { uint16_t lid, pm, x, y, ground, extraCount; uint32_t extraFirst; } OutPlacement;
typedef struct PageModel { uint16_t model, page; int16_t ox, oy; } PageModel;

static int cmp_found(const void *a, const void *b)
{
    const Found *x = (const Found *)a, *y = (const Found *)b;

    return x->lid != y->lid ? (x->lid < y->lid ? -1 : 1) : 0;
}

static int cmp_out(const void *a, const void *b)
{
    const OutPlacement *x = (const OutPlacement *)a, *y = (const OutPlacement *)b;

    if (x->lid != y->lid) return x->lid < y->lid ? -1 : 1;
    if (x->pm != y->pm) return x->pm < y->pm ? -1 : 1;
    if (x->x != y->x) return x->x < y->x ? -1 : 1;
    if (x->y != y->y) return x->y < y->y ? -1 : 1;
    if (x->ground != y->ground) return x->ground < y->ground ? -1 : 1;
    if (x->extraCount != y->extraCount) return x->extraCount < y->extraCount ? -1 : 1;
    if (x->extraFirst != y->extraFirst) return x->extraFirst < y->extraFirst ? -1 : 1;
    return 0;
}

typedef struct ByteBuf { uint8_t *d; size_t n, cap; bool bad; } ByteBuf;
static void bb_put(ByteBuf *b, const void *src, size_t n)
{
    if (b->bad)
        return;
    if (b->n + n > b->cap) {
        size_t cap = b->cap ? b->cap : 4096u;
        uint8_t *nd;

        while (cap < b->n + n)
            cap *= 2u;
        nd = (uint8_t *)realloc(b->d, cap);
        if (nd == NULL) {
            b->bad = true;
            return;
        }
        b->d = nd;
        b->cap = cap;
    }
    if (n > 0)
        memcpy(b->d + b->n, src, n);
    b->n += n;
}
static void bb_u8(ByteBuf *b, unsigned v) { uint8_t x = (uint8_t)v; bb_put(b, &x, 1); }
static void bb_u16(ByteBuf *b, unsigned v) { uint8_t x[2]; x[0] = (uint8_t)v; x[1] = (uint8_t)(v >> 8); bb_put(b, x, 2); }
static void bb_i16(ByteBuf *b, int v) { bb_u16(b, (unsigned)(v & 0xFFFF)); }
static void bb_u32(ByteBuf *b, uint32_t v)
{
    uint8_t x[4];

    x[0] = (uint8_t)v; x[1] = (uint8_t)(v >> 8); x[2] = (uint8_t)(v >> 16); x[3] = (uint8_t)(v >> 24);
    bb_put(b, x, 4);
}
static void bb_f32(ByteBuf *b, float f)
{
    uint32_t u;

    memcpy(&u, &f, 4);
    bb_u32(b, u);
}

typedef struct FVec { float *v; size_t n, cap; bool bad; } FVec;
static void fv_put6(FVec *f, double a, double b, double c, double d, double e, double g)
{
    if (f->bad)
        return;
    if (f->n + 6 > f->cap) {
        size_t cap = f->cap ? f->cap * 2u : 6144u;
        float *nv = (float *)realloc(f->v, cap * sizeof(float));

        if (nv == NULL) {
            f->bad = true;
            return;
        }
        f->v = nv;
        f->cap = cap;
    }
    f->v[f->n++] = (float)a; f->v[f->n++] = (float)b; f->v[f->n++] = (float)c;
    f->v[f->n++] = (float)d; f->v[f->n++] = (float)e; f->v[f->n++] = (float)g;
}

static size_t fail(RgBuildStats *st, RgErr e, const char *field)
{
    st->err = e;
    st->errField = field;
    return 0;
}

size_t rg_buildings_write(const RgWorld *w, const RgBuildModels *models, uint8_t *out, size_t cap, RgBuildStats *stats)
{
    RgBuildStats local;
    RgBuildStats *st = stats != NULL ? stats : &local;
    unsigned nm = models->n, mi, f, i;
    RgPlacementList *pls = NULL;
    Found *found = NULL;
    unsigned nFound = 0;
    FVec verts;
    ByteBuf heights, quarters, body, blob, footBuf;
    uint32_t *recFirst = NULL, *recCount = NULL, *recHeights = NULL;
    RgMaskSet masks;
    int (*crop)[4] = NULL;
    PageModel *pms = NULL;
    unsigned nPms = 0, capPms = 0;
    OutPlacement *placements = NULL;
    unsigned nPl = 0;
    uint16_t *pageTexW = NULL, *pageTexH = NULL;
    uint16_t **texels = NULL;
    unsigned nPages = 0;
    size_t result = 0;
    RgErr e;

    memset(st, 0, sizeof(*st));
    memset(&verts, 0, sizeof(verts)); memset(&heights, 0, sizeof(heights)); memset(&quarters, 0, sizeof(quarters));
    memset(&body, 0, sizeof(body)); memset(&blob, 0, sizeof(blob)); memset(&footBuf, 0, sizeof(footBuf));
    memset(&masks, 0, sizeof(masks));

    pls = (RgPlacementList *)calloc(nm ? nm : 1u, sizeof(RgPlacementList));
    recFirst = (uint32_t *)calloc(nm ? nm : 1u, 4);
    recCount = (uint32_t *)calloc(nm ? nm : 1u, 4);
    recHeights = (uint32_t *)calloc(nm ? nm : 1u, 4);
    crop = (int (*)[4])calloc(nm ? nm : 1u, sizeof(*crop));
    if (pls == NULL || recFirst == NULL || recCount == NULL || recHeights == NULL || crop == NULL) {
        result = fail(st, RG_ERR_NOMEM, "alloc");
        goto cleanup;
    }

    /* every placement of every model, in model order (found order) */
    for (mi = 0; mi < nm; mi++) {
        e = rg_find_placements(w, &models->m[mi], &pls[mi]);
        if (e != RG_OK) {
            result = fail(st, e, "find_placements");
            goto cleanup;
        }
        for (i = 0; i < pls[mi].n; i++) {
            e = rg_placement_patches(w, &models->m[mi], &pls[mi].p[i]);
            if (e != RG_OK) {
                result = fail(st, e, "placement_patches");
                goto cleanup;
            }
            nFound++;
        }
    }
    found = (Found *)calloc(nFound ? nFound : 1u, sizeof(Found));
    if (found == NULL) {
        result = fail(st, RG_ERR_NOMEM, "alloc");
        goto cleanup;
    }
    f = 0;
    for (mi = 0; mi < nm; mi++)
        for (i = 0; i < pls[mi].n; i++) {
            found[f].lid = pls[mi].p[i].layout;
            found[f].model = mi;
            found[f].pl = &pls[mi].p[i];
            found[f].seq = f;
            f++;
        }
    for (mi = 0; mi < nm; mi++) {
        int bb[4];

        if (rg_img_bbox(&models->m[mi].art, bb))
            memcpy(crop[mi], bb, sizeof(bb));
        else {
            crop[mi][0] = 0; crop[mi][1] = 0; crop[mi][2] = 1; crop[mi][3] = 1;
        }
    }

    /* models: geometry once, uv in the model's own art pixels */
    {
        unsigned totalCells = 0;
        unsigned bodyModels = nm;

        for (mi = 0; mi < nm; mi++) {
            const RgBuildModel *m = &models->m[mi];
            unsigned t, k, cells = (unsigned)m->w * m->h;
            uint8_t *hb;
            int32_t *maskOf;

            recFirst[mi] = (uint32_t)(verts.n / 6u);
            for (t = 0; t < m->mesh.n; t++)
                for (k = 0; k < 3; k++) {
                    const RgVtx *v = &m->mesh.t[t].p[k];

                    fv_put6(&verts, v->x / 16.0, v->y / 16.0, v->z / 16.0, v->u, v->v, m->mesh.t[t].shade);
                }
            recCount[mi] = (uint32_t)(verts.n / 6u) - recFirst[mi];
            recHeights[mi] = (uint32_t)heights.n;
            hb = (uint8_t *)malloc(cells ? cells : 1u);
            maskOf = (int32_t *)malloc((cells ? cells : 1u) * sizeof(int32_t));
            if (hb == NULL || maskOf == NULL || !rg_cell_heights(m, hb) || !rg_cell_footprints(m, maskOf, &masks)) {
                free(hb);
                free(maskOf);
                result = fail(st, RG_ERR_NOMEM, "cell data");
                goto cleanup;
            }
            for (k = 0; k < cells; k++) {
                bool owned = m->owned == NULL || m->owned[k];

                bb_u8(&heights, owned ? hb[k] : 255u);
                bb_u8(&quarters, 0);
                bb_u16(&footBuf, maskOf[k] < 0 ? 0xFFFFu : (unsigned)maskOf[k]);
            }
            totalCells += cells;
            free(hb);
            free(maskOf);
        }
        (void)totalCells;
        (void)bodyModels;
    }
    if (verts.bad || heights.bad || quarters.bad || footBuf.bad) {
        result = fail(st, RG_ERR_NOMEM, "alloc");
        goto cleanup;
    }

    /* pages: one per layout that places anything */
    if (!rg_stable_sort(found, nFound, sizeof(Found), cmp_found)) {
        result = fail(st, RG_ERR_NOMEM, "sort");
        goto cleanup;
    }
    {
        unsigned start = 0;

        pageTexW = (uint16_t *)calloc(RG_MAX_PAGES + 1u, 2);
        pageTexH = (uint16_t *)calloc(RG_MAX_PAGES + 1u, 2);
        texels = (uint16_t **)calloc(RG_MAX_PAGES + 1u, sizeof(uint16_t *));
        placements = (OutPlacement *)calloc(nFound ? nFound : 1u, sizeof(OutPlacement));
        if (pageTexW == NULL || pageTexH == NULL || texels == NULL || placements == NULL) {
            result = fail(st, RG_ERR_NOMEM, "alloc");
            goto cleanup;
        }
        while (start < nFound) {
            unsigned end = start, used[64], nUsed = 0, k, nImgs = 0, p;
            uint16_t lid = found[start].lid;
            const RgImage **imgs = NULL;
            RgImage *modelCrops = NULL;
            RgImage **patchImgs = NULL;
            unsigned nPatch = 0;
            int (*spots)[2] = NULL;
            int tw = 0, th = 0, page, pmOf[64];
            bool ok = true;

            while (end < nFound && found[end].lid == lid)
                end++;
            /* used = sorted distinct model indices placed here */
            for (k = start; k < end; k++) {
                unsigned q, mdl = found[k].model;

                for (q = 0; q < nUsed; q++)
                    if (used[q] == mdl)
                        break;
                if (q == nUsed) {
                    if (nUsed >= 64u) {
                        result = fail(st, RG_ERR_TOO_BIG, "models per page");
                        goto cleanup;
                    }
                    used[nUsed++] = mdl;
                }
            }
            for (k = 1; k < nUsed; k++) {                /* insertion sort ascending */
                unsigned key = used[k], q;

                for (q = k; q > 0 && used[q - 1] > key; q--)
                    used[q] = used[q - 1];
                used[q] = key;
            }
            /* distinct patch images in insertion order (hash + memcmp) */
            {
                unsigned totalCells = 0, capP;

                for (k = start; k < end; k++)
                    totalCells += found[k].pl->nCells;
                capP = totalCells ? totalCells : 1u;
                patchImgs = (RgImage **)calloc(capP, sizeof(RgImage *));
                modelCrops = (RgImage *)calloc(nUsed ? nUsed : 1u, sizeof(RgImage));
                imgs = (const RgImage **)calloc(nUsed + capP, sizeof(RgImage *));
                spots = (int (*)[2])calloc(nUsed + capP, sizeof(*spots));
                if (patchImgs == NULL || modelCrops == NULL || imgs == NULL || spots == NULL) {
                    ok = false;
                } else {
                    for (k = start; k < end && ok; k++) {
                        for (p = 0; p < found[k].pl->nCells; p++) {
                            RgPatchCell *c = &found[k].pl->cells[p];
                            unsigned q;

                            for (q = 0; q < nPatch; q++)
                                if (rg_img_equal(patchImgs[q], &c->img))
                                    break;
                            if (q == nPatch)
                                patchImgs[nPatch++] = &c->img;
                            c->patch = (int)q;
                        }
                    }
                    for (k = 0; k < nUsed && ok; k++) {
                        if (!rg_img_crop(&models->m[used[k]].art, crop[used[k]], &modelCrops[k]))
                            ok = false;
                        imgs[nImgs++] = &modelCrops[k];
                    }
                    for (k = 0; k < nPatch; k++)
                        imgs[nImgs++] = patchImgs[k];
                }
            }
            if (ok && !rg_pack_atlas(imgs, nImgs, &tw, &th, spots)) {
                for (k = 0; k < nUsed; k++) rg_img_free(&modelCrops[k]);
                free(modelCrops); free(patchImgs); free(imgs); free(spots);
                result = fail(st, RG_ERR_BUILDINGS, "atlas does not fit 1024x1024");
                goto cleanup;
            }
            if (!ok) {
                if (modelCrops != NULL) for (k = 0; k < nUsed; k++) rg_img_free(&modelCrops[k]);
                free(modelCrops); free(patchImgs); free(imgs); free(spots);
                result = fail(st, RG_ERR_NOMEM, "alloc");
                goto cleanup;
            }
            if (tw * th > RG_MAX_TEXTURE_W * RG_MAX_TEXTURE_H) {
                for (k = 0; k < nUsed; k++) rg_img_free(&modelCrops[k]);
                free(modelCrops); free(patchImgs); free(imgs); free(spots);
                result = fail(st, RG_ERR_TOO_BIG, "page larger than 512x512");
                goto cleanup;
            }
            if (nPages >= RG_MAX_PAGES) {
                for (k = 0; k < nUsed; k++) rg_img_free(&modelCrops[k]);
                free(modelCrops); free(patchImgs); free(imgs); free(spots);
                result = fail(st, RG_ERR_TOO_BIG, "pages");
                goto cleanup;
            }
            page = (int)nPages;
            texels[nPages] = (uint16_t *)calloc((size_t)tw * (size_t)th, 2);
            if (texels[nPages] == NULL) {
                for (k = 0; k < nUsed; k++) rg_img_free(&modelCrops[k]);
                free(modelCrops); free(patchImgs); free(imgs); free(spots);
                result = fail(st, RG_ERR_NOMEM, "alloc");
                goto cleanup;
            }
            for (k = 0; k < nImgs; k++) {
                const RgImage *im = imgs[k];
                int x, y;

                for (y = 0; y < im->h; y++)
                    for (x = 0; x < im->w; x++) {
                        const uint8_t *px = im->px + ((size_t)y * (size_t)im->w + (size_t)x) * 4u;
                        unsigned value = (unsigned)(px[0] >> 3) << 11 | (unsigned)(px[1] >> 3) << 6 |
                                         (unsigned)(px[2] >> 3) << 1 | (px[3] >= 128 ? 1u : 0u);

                        texels[nPages][rg_texel_offset((unsigned)(spots[k][0] + x), (unsigned)(spots[k][1] + y), (unsigned)tw)] =
                            (uint16_t)value;
                    }
            }
            pageTexW[nPages] = (uint16_t)tw;
            pageTexH[nPages] = (uint16_t)th;
            nPages++;
            for (k = 0; k < nUsed; k++) {
                int ox = spots[k][0] - crop[used[k]][0], oy = spots[k][1] - crop[used[k]][1];

                if (ox < -32768 || ox > 32767 || oy < -32768 || oy > 32767) {
                    result = fail(st, RG_ERR_TOO_BIG, "page-model offset");
                    for (p = 0; p < nUsed; p++) rg_img_free(&modelCrops[p]);
                    free(modelCrops); free(patchImgs); free(imgs); free(spots);
                    goto cleanup;
                }
                if (nPms == capPms) {
                    unsigned nc = capPms ? capPms * 2u : 16u;
                    PageModel *np = (PageModel *)realloc(pms, (size_t)nc * sizeof(PageModel));

                    if (np == NULL) {
                        result = fail(st, RG_ERR_NOMEM, "alloc");
                        for (p = 0; p < nUsed; p++) rg_img_free(&modelCrops[p]);
                        free(modelCrops); free(patchImgs); free(imgs); free(spots);
                        goto cleanup;
                    }
                    pms = np;
                    capPms = nc;
                }
                pmOf[k] = (int)nPms;
                pms[nPms].model = (uint16_t)used[k];
                pms[nPms].page = (uint16_t)page;
                pms[nPms].ox = (int16_t)ox;
                pms[nPms].oy = (int16_t)oy;
                nPms++;
            }
            for (k = start; k < end; k++) {
                const RgPlacement *pl = found[k].pl;
                unsigned first = (unsigned)(verts.n / 6u), q;

                for (p = 0; p < pl->nCells; p++) {
                    const RgPatchCell *c = &pl->cells[p];
                    double ox = spots[nUsed + (unsigned)c->patch][0], oy = spots[nUsed + (unsigned)c->patch][1];
                    double i0 = c->i, j0 = c->j;

                    /* a, b, c, a, c, d (gen:1398-1404): cell units, uv in page pixels */
                    fv_put6(&verts, i0, 0.01, j0, ox, oy, 1.0);
                    fv_put6(&verts, i0 + 1, 0.01, j0, ox + 16, oy, 1.0);
                    fv_put6(&verts, i0 + 1, 0.01, j0 + 1, ox + 16, oy + 16, 1.0);
                    fv_put6(&verts, i0, 0.01, j0, ox, oy, 1.0);
                    fv_put6(&verts, i0 + 1, 0.01, j0 + 1, ox + 16, oy + 16, 1.0);
                    fv_put6(&verts, i0, 0.01, j0 + 1, ox, oy + 16, 1.0);
                }
                for (q = 0; q < nUsed; q++)
                    if (used[q] == found[k].model)
                        break;
                placements[nPl].lid = lid;
                placements[nPl].pm = (uint16_t)pmOf[q];
                placements[nPl].x = (uint16_t)pl->px;
                placements[nPl].y = (uint16_t)pl->py;
                placements[nPl].ground = pl->ground;
                placements[nPl].extraCount = (uint16_t)((verts.n / 6u) - first);
                placements[nPl].extraFirst = (uint32_t)first;
                nPl++;
            }
            for (k = 0; k < nUsed; k++)
                rg_img_free(&modelCrops[k]);
            free(modelCrops); free(patchImgs); free(imgs); free(spots);
            start = end;
        }
    }
    if (verts.bad) {
        result = fail(st, RG_ERR_NOMEM, "alloc");
        goto cleanup;
    }
    if (!rg_stable_sort(placements, nPl, sizeof(OutPlacement), cmp_out)) {
        result = fail(st, RG_ERR_NOMEM, "sort");
        goto cleanup;
    }

    /* field widths (SPEC-S2 section 4): fail cleanly, never truncate */
    if (nm > 65535u) { result = fail(st, RG_ERR_TOO_BIG, "models"); goto cleanup; }
    if (nPms > 65535u) { result = fail(st, RG_ERR_TOO_BIG, "pageModels"); goto cleanup; }
    if (nPl > 65535u) { result = fail(st, RG_ERR_TOO_BIG, "placements"); goto cleanup; }
    if (heights.n > 65535u) { result = fail(st, RG_ERR_TOO_BIG, "heightBytes"); goto cleanup; }
    if (masks.n > 65535u) { result = fail(st, RG_ERR_TOO_BIG, "masks"); goto cleanup; }
    for (mi = 0; mi < nm; mi++)
        if (models->m[mi].w > 255 || models->m[mi].h > 255) { result = fail(st, RG_ERR_TOO_BIG, "model w/h"); goto cleanup; }

    /* head */
    bb_put(&blob, "VXB7", 4);
    bb_u16(&blob, nPages); bb_u16(&blob, nm); bb_u16(&blob, nPms); bb_u16(&blob, nPl);
    bb_u16(&blob, (unsigned)heights.n); bb_u16(&blob, masks.n);
    bb_u32(&blob, (uint32_t)(verts.n / 6u));
    bb_u16(&blob, 0);                                    /* variants: props only (S2.5) */
    bb_u16(&blob, 0);
    /* body */
    for (mi = 0; mi < nm; mi++) {
        bb_u8(&body, models->m[mi].w); bb_u8(&body, models->m[mi].h); bb_u16(&body, models->m[mi].groundMetatile);
        bb_u32(&body, recFirst[mi]); bb_u32(&body, recCount[mi]); bb_u32(&body, recHeights[mi]);
    }
    for (i = 0; i < nPms; i++) {
        bb_u16(&body, pms[i].model); bb_u16(&body, pms[i].page); bb_i16(&body, pms[i].ox); bb_i16(&body, pms[i].oy);
    }
    for (i = 0; i < nPl; i++) {
        bb_u16(&body, placements[i].lid); bb_u16(&body, placements[i].pm); bb_u16(&body, placements[i].x);
        bb_u16(&body, placements[i].y); bb_u16(&body, placements[i].ground); bb_u16(&body, placements[i].extraCount);
        bb_u32(&body, placements[i].extraFirst);
    }
    bb_put(&body, heights.d, heights.n);
    if (heights.n % 2u)
        bb_u8(&body, 0);
    bb_put(&body, footBuf.d, footBuf.n);
    for (i = 0; i < masks.n; i++) {
        unsigned r;

        for (r = 0; r < 16; r++)
            bb_u16(&body, masks.rows[i][r]);
    }
    bb_put(&body, quarters.d, quarters.n);
    if (quarters.n % 2u)
        bb_u8(&body, 0);
    {
        size_t fixed = blob.n + 8u * nPages + body.n, pad = (4u - fixed % 4u) % 4u, off;

        for (i = 0; i < pad; i++)
            bb_u8(&body, 0);
        for (i = 0; i < verts.n; i++)
            bb_f32(&body, verts.v[i]);
        off = blob.n + 8u * nPages + body.n;
        for (i = 0; i < nPages; i++) {
            bb_u16(&blob, pageTexW[i]); bb_u16(&blob, pageTexH[i]); bb_u32(&blob, (uint32_t)off);
            off += (size_t)pageTexW[i] * pageTexH[i] * 2u;
        }
        bb_put(&blob, body.d, body.n);
        for (i = 0; i < nPages; i++) {
            size_t t, n = (size_t)pageTexW[i] * pageTexH[i];

            for (t = 0; t < n; t++)
                bb_u16(&blob, texels[i][t]);
        }
    }
    if (blob.bad || body.bad) {
        result = fail(st, RG_ERR_NOMEM, "alloc");
        goto cleanup;
    }
    st->models = nm; st->pages = nPages; st->pageModels = nPms; st->placements = nPl;
    st->vertices = (unsigned)(verts.n / 6u); st->masks = masks.n; st->variants = 0; st->fileSize = blob.n;
    for (i = 0; i < nPages; i++) { st->pageW[i] = pageTexW[i]; st->pageH[i] = pageTexH[i]; }
    st->err = RG_OK;
    if (out == NULL) {
        result = blob.n;
    } else if (cap < blob.n) {
        result = fail(st, RG_ERR_TOO_BIG, "output buffer");
    } else {
        memcpy(out, blob.d, blob.n);
        result = blob.n;
    }

cleanup:
    if (pls != NULL)
        for (mi = 0; mi < nm; mi++)
            rg_placements_free(&pls[mi]);
    free(pls);
    free(found); free(recFirst); free(recCount); free(recHeights); free(crop); free(pms); free(placements);
    if (texels != NULL)
        for (i = 0; i <= RG_MAX_PAGES; i++)
            free(texels[i]);
    free(texels); free(pageTexW); free(pageTexH);
    free(verts.v); free(heights.d); free(quarters.d); free(body.d); free(blob.d); free(footBuf.d);
    rg_masks_free(&masks);
    return result;
}
