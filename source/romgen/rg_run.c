/* rg_run.c -- the romgen driver (3DGBA, GPLv3). Original to 3DGBA; the generators it calls carry the MIT notices. */
#include "rg_run.h"

#include <stdlib.h>
#include <string.h>

#include "rg_regions.h"
#include "rg_signs.h"

static double now(const RgRunOpts *o)
{
    return (o != NULL && o->nowMs != NULL) ? o->nowMs() : 0.0;
}

static bool cancelled(const RgRunOpts *o)
{
    return o != NULL && o->cancel != NULL && *o->cancel != 0;
}

/* One pair's layouts: roles then signs. Returns RG_OK, RG_ERR_NOMEM or RG_ERR_CANCELLED. */
static RgErr run_pair(const RgWorld *w, RgRoles *r, RgSignList *signs, unsigned pi, const RgRunOpts *o,
                      RgOutput *out, unsigned *done, unsigned total)
{
    RgPair *p = rg_pair_open(w, (uint16_t)pi);
    unsigned li;
    RgErr e = RG_OK;

    if (p == NULL)
        return RG_ERR_NOMEM;
    for (li = 0; li < w->layoutCount && e == RG_OK; li++) {
        const RgLayout *L = &w->layouts[li];
        double t0;

        if (!L->present || L->pairIndex != pi)
            continue;
        if (cancelled(o)) {
            e = RG_ERR_CANCELLED;
            break;
        }
        t0 = now(o);
        if (!rg_roles_layout(w, r, p, L, r->data + r->off[li]))
            e = RG_ERR_NOMEM;
        out->msRoles += now(o) - t0;
        t0 = now(o);
        if (e == RG_OK && o != NULL && o->wantSigns && !rg_signs_layout(p, L, r->data + r->off[li], signs))
            e = RG_ERR_NOMEM;
        out->msSigns += now(o) - t0;
        if (e == RG_OK && o != NULL && o->progress != NULL)
            o->progress(o->ctx, ++*done, total);
    }
    rg_pair_close(p);
    return e;
}

static RgErr write_outputs(const RgWorld *w, const RgRoles *r, RgSignList *signs, RgOutput *out)
{
    size_t n = rg_regions_write(w, r, NULL, 0);

    out->regions = (uint8_t *)malloc(n ? n : 1);
    if (out->regions == NULL || n == 0 || rg_regions_write(w, r, out->regions, n) != n)
        return RG_ERR_NOMEM;
    out->regionsSize = n;
    rg_signs_finish(signs);
    out->signCount = (unsigned)signs->count;
    out->headCount = rg_signs_with_head(signs);
    n = rg_signs_write(signs, NULL, 0);
    if (n > 0) {
        size_t i;
        unsigned k;
        out->signs = (uint8_t *)malloc(n);
        if (out->signs == NULL || rg_signs_write(signs, out->signs, n) != n)
            return RG_ERR_NOMEM;
        out->signsSize = n;
        for (i = 0; i < signs->count; i++) {
            unsigned any = 0;
            for (k = 0; k < 16; k++)
                any |= signs->rec[i].rows[k];
            out->emptyMasks += any == 0;
        }
    }
    return RG_OK;
}

RgErr rg_run(const uint8_t *rom, size_t romSize, const RgRunOpts *opts, RgOutput *out)
{
    RgWorld w;
    RgRoles r;
    RgSignList signs;
    unsigned pi, li, done = 0, total = 0;
    double t0;
    RgErr e;

    if (out == NULL)
        return RG_ERR_NOMEM;
    memset(out, 0, sizeof(*out));
    memset(&signs, 0, sizeof(signs));
    t0 = now(opts);
    e = rg_world_open(&w, rom, romSize);
    if (e != RG_OK)
        return e;
    out->msWorld = now(opts) - t0;
    e = rg_roles_init(&w, &r);
    if (e != RG_OK) {
        rg_world_close(&w);
        return e;
    }
    for (li = 0; li < w.layoutCount; li++)
        total += w.layouts[li].present;
    for (pi = 0; pi < w.pairCount && e == RG_OK; pi++)
        e = run_pair(&w, &r, &signs, pi, opts, out, &done, total);
    t0 = now(opts);
    if (e == RG_OK)
        e = write_outputs(&w, &r, &signs, out);
    out->msWrite = now(opts) - t0;
    if (e == RG_OK) {
        out->layouts = w.layoutCount;
        out->maps = w.mapCount;
        out->tilesets = w.tilesetCount - 1u;
        out->pairs = w.pairCount;
        out->outdoorMaps = w.outdoorMaps;
        for (li = 0; li < r.total; li++)
            if (r.data[li] < 11)
                out->roleCount[r.data[li]]++;
    } else {
        rg_output_free(out);
    }
    rg_signs_free(&signs);
    rg_roles_free(&r);
    rg_world_close(&w);
    return e;
}

void rg_output_free(RgOutput *out)
{
    if (out == NULL)
        return;
    free(out->regions);
    free(out->signs);
    out->regions = out->signs = NULL;
    out->regionsSize = out->signsSize = 0;
}
