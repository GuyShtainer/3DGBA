/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/gen_voxel_relief.py (layout_heights,
 * relief_cells, cell_grid, export), MIT License - see source/voxel/NOTICE.md. Portions Copyright (c) Dust Zallax, MIT. */
#include "rg_relief.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "rg_ledge.h"
#include "rg_relief_write.h"
#include "rg_rtables.h"

/* H14: max(-128, min(127, int(round(v)))), half-even (nearbyint under FE_TONEAREST). */
static int8_t stored(double v, int unit)
{
    double r = nearbyint(v / (double)unit);

    return (int8_t)(r < -128.0 ? -128 : (r > 127.0 ? 127 : (int)r));
}

int rg_relief_cells(const RgLat *lat, int unit, uint8_t **out)
{
    unsigned n = 0, cap = 64;
    uint8_t *buf = (uint8_t *)malloc((size_t)cap * RG_RELIEF_CELL_BYTES);
    int x, y;

    assert(lat != NULL && out != NULL && unit >= 1);
    *out = NULL;
    if (buf == NULL)
        return -1;
    for (y = 0; y < lat->h; y++) {
        for (x = 0; x < lat->w; x++) {
            RgGrid g;
            int i, j;
            bool any = false;

            rg_cell_grid(lat, x, y, &g);
            for (j = 0; j < RG_SIDE; j++)
                for (i = 0; i < RG_SIDE; i++)
                    any = any || fabs(g.g[j][i]) > 0.25;
            if (!any)
                continue;
            if (n == cap) {
                uint8_t *nb = (uint8_t *)realloc(buf, (size_t)cap * 2u * RG_RELIEF_CELL_BYTES);

                if (nb == NULL) {
                    free(buf);
                    return -1;
                }
                buf = nb;
                cap *= 2u;
            }
            buf[(size_t)n * RG_RELIEF_CELL_BYTES] = (uint8_t)x;
            buf[(size_t)n * RG_RELIEF_CELL_BYTES + 1] = (uint8_t)y;
            for (j = 0; j < RG_SIDE; j++)
                for (i = 0; i < RG_SIDE; i++)
                    buf[(size_t)n * RG_RELIEF_CELL_BYTES + 2u + (size_t)(j * RG_SIDE + i)] = (uint8_t)stored(g.g[j][i], unit);
            n++;
        }
    }
    if (n == 0) {
        free(buf);
        return 0;
    }
    *out = buf;
    return (int)n;
}

/* One ledge layout: flat lattice + berms -> a row (S3a: base 0, flags clear; a row only when it has cells). */
static RgErr ledge_row(const RgWorld *w, RgPair *pair, const RgLayout *L, RgReliefRow *row, uint8_t **cells,
                       RgReliefStats *st)
{
    RgLat lat;
    int led, n;
    RgErr e = RG_OK;

    (void)w;
    if (L->w > 255u || L->h > 255u)
        return RG_ERR_RELIEF;
    if (!rg_lat_new(&lat, (int)L->w, (int)L->h))
        return RG_ERR_NOMEM;
    led = rg_ledge_berms(L, pair, &lat);
    n = led < 0 ? -1 : rg_relief_cells(&lat, 1, cells);
    rg_lat_free(&lat);
    if (n < 0)
        return RG_ERR_NOMEM;
    st->ledgeCells += (unsigned)led;
    row->id = L->id;
    row->w = L->w;
    row->hFlags = L->h;
    row->base = 0;
    row->nCells = (uint32_t)n;
    row->cells = *cells;
    return e;
}

typedef struct Ledges {
    uint16_t ids[512];
    unsigned n;
    RgReliefRow rows[512];
    uint8_t *cells[512];
    bool have[512];
} Ledges;

static void ledges_free(Ledges *l)
{
    unsigned i;

    for (i = 0; i < l->n; i++)
        free(l->cells[i]);
    free(l);
}

/* All ledge layouts, one open pair at a time (a pair's layouts are consecutive in this walk). */
static RgErr ledges_run(const RgWorld *w, Ledges *l, RgProgressFn progress, void *ctx, const volatile int *cancel,
                        RgReliefStats *st)
{
    unsigned pi, i, done = 0;
    RgErr e = RG_OK;

    for (pi = 0; pi < w->pairCount && e == RG_OK; pi++) {
        RgPair *pair = NULL;

        for (i = 0; i < l->n && e == RG_OK; i++) {
            const RgLayout *L = &w->layouts[l->ids[i] - 1u];

            if (L->pairIndex != pi)
                continue;
            if (cancel != NULL && *cancel != 0) {
                e = RG_ERR_CANCELLED;
                break;
            }
            if (pair == NULL && (pair = rg_pair_open(w, (uint16_t)pi)) == NULL) {
                e = RG_ERR_NOMEM;
                break;
            }
            e = ledge_row(w, pair, L, &l->rows[i], &l->cells[i], st);
            l->have[i] = e == RG_OK;
            if (e == RG_OK && progress != NULL)
                progress(ctx, ++done, l->n);
        }
        if (pair != NULL)
            rg_pair_close(pair);
    }
    return e;
}

static RgErr ledges_serialise(Ledges *l, uint8_t **out, size_t *outSize, RgReliefStats *st)
{
    RgReliefRow rows[512];
    unsigned i, n = 0;
    size_t sz = 0;
    RgErr e;

    /* ids ascend (rg_ledge_layouts walks layouts by id); a row exists only with cells (rel export `continue`) */
    for (i = 0; i < l->n; i++) {
        if (!l->have[i] || l->rows[i].nCells == 0)
            continue;
        rows[n++] = l->rows[i];
        st->cells += l->rows[i].nCells;
    }
    st->rows = n;
    e = rg_relief_write(rows, n, NULL, 0, NULL, 0, NULL, 0, &sz);
    if (e != RG_OK)
        return e;
    *out = (uint8_t *)malloc(sz);
    if (*out == NULL)
        return RG_ERR_NOMEM;
    e = rg_relief_write(rows, n, NULL, 0, NULL, 0, *out, sz, outSize);
    if (e != RG_OK) {
        free(*out);
        *out = NULL;
    }
    return e;
}

RgErr rg_relief_build(const RgWorld *w, const RgRoles *r, RgReliefMode mode, RgProgressFn progress, void *ctx,
                      const volatile int *cancel, uint8_t **out, size_t *outSize, RgReliefStats *st)
{
    RgReliefStats local;
    Ledges *l;
    uint16_t ids[512];
    unsigned total;
    RgErr e;

    (void)r;
    assert(w != NULL && out != NULL && outSize != NULL);
    if (st == NULL)
        st = &local;
    memset(st, 0, sizeof(*st));
    *out = NULL;
    *outSize = 0;
    if (mode != RG_RELIEF_LEDGES)
        return RG_ERR_RELIEF;           /* FULL arrives with S3.7; OFF is not a build */
    {
        const char *why = NULL;

        if (!rg_rtables_check(w, &why))
            return RG_ERR_TABLES;
    }
    total = rg_ledge_layouts(w, ids, 512);
    if (total > 512)
        return RG_ERR_RELIEF;
    l = (Ledges *)calloc(1, sizeof(*l));
    if (l == NULL)
        return RG_ERR_NOMEM;
    memcpy(l->ids, ids, total * sizeof(ids[0]));
    l->n = total;
    st->ledgeLayouts = total;
    e = ledges_run(w, l, progress, ctx, cancel, st);
    if (e == RG_OK)
        e = ledges_serialise(l, out, outSize, st);
    ledges_free(l);
    return e;
}
