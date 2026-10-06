/* rg_run.c -- the romgen driver (3DGBA, GPLv3). Original to 3DGBA; the generators it calls carry the MIT notices. */
#include "rg_run.h"

#include <stdlib.h>
#include <string.h>

#include "rg_regions.h"
#include "rg_signs.h"
#include "rg_bspecs.h"
#include "rg_kspecs.h"

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

/* S3: relief.bin from the world (and, for FULL, the roles). */
static RgErr run_relief(const RgWorld *w, const RgRoles *r, const RgRunOpts *o, RgOutput *out)
{
    double t0 = now(o);
    RgErr e = rg_relief_build(w, r, o->relief, o->progress, o->ctx, o->cancel, &out->relief, &out->reliefSize, &out->rst);

    out->msRelief = now(o) - t0;
    return e;
}

/* S2: models -> gates -> buildings.bin. The pair-grouped passes live inside the builders; no RgPair is open here. */
static RgErr run_buildings(const RgWorld *w, const RgRunOpts *o, RgOutput *out)
{
    RgBuildModels ms;
    RgBuildStats st;
    RgErr e;
    unsigned i;
    double t0 = now(o);
    size_t n;

    if (cancelled(o))
        return RG_ERR_CANCELLED;
    /* the profile's recipe table; the Emerald row leaves it NULL (rg_gameprof.c must not link rg_bspecs.c) and uses rg_specs */
    if (w->prof->specs != NULL) {
        e = rg_build_models(w, w->prof->specs, w->prof->nSpecs, &ms);
    } else if (w->prof->game != GP_EMERALD) {
        /* Phase 34 B0: FireRed / LeafGreen use the Kanto table (rg_kspecs.c); empty until the K slices, which is a valid
         * 0-model buildings.bin the consumer loads (the device then draws the fallback boxes). It is not read through
         * the profile's `specs` pointer because rg_gameprof.c is also linked by the renderer-only builds. */
        unsigned nk = 0;
        const RgSpec *k = rg_kspecs_table(w->prof, &nk);

        e = rg_build_models(w, k, nk, &ms);
    } else {
        e = rg_build_models(w, rg_specs, rg_spec_count, &ms);
    }
    out->msBuildModels = now(o) - t0;
    if (e != RG_OK) {
        rg_models_free(&ms);
        return e;
    }
    t0 = now(o);
    for (i = 0; i < ms.n; i++) {
        RgOrthoResult ortho;
        unsigned dens = 0;

        if (!rg_model_gate(&ms.m[i], &ortho, &dens)) {
            rg_models_free(&ms);
            return RG_ERR_NOMEM;
        }
        if (ortho.wrong || ortho.missing || ortho.extra || dens) {
            if (out->buildingsFailed < RG_MAX_SKIPPED) {
                strncpy(out->failedNames[out->buildingsFailed], ms.m[i].spec->name, 63);
                out->failedNames[out->buildingsFailed][63] = '\0';
            }
            out->buildingsFailed++;
        }
    }
    out->msChecks = now(o) - t0;
    t0 = now(o);
    memset(&st, 0, sizeof(st));
    n = rg_buildings_write(w, &ms, NULL, 0, &st);
    if (n > 0) {
        out->buildings = (uint8_t *)malloc(n);
        if (out->buildings == NULL || rg_buildings_write(w, &ms, out->buildings, n, &st) != n)
            e = RG_ERR_NOMEM;
        else
            out->buildingsSize = n;
    } else {
        e = st.err != RG_OK ? st.err : RG_ERR_BUILDINGS;
    }
    out->msWriteBuildings = now(o) - t0;
    out->bModels = st.models;
    out->bPages = st.pages;
    out->bPageModels = st.pageModels;
    out->bPlacements = st.placements;
    out->bVertices = st.vertices;
    out->bMasks = st.masks;
    out->bVariants = st.variants;
    rg_models_free(&ms);
    return e;
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
    if (w.prof->game != GP_EMERALD) {
        /* Phase 34: FireRed / LeafGreen. B0 turns the buildings on (the Kanto table, empty until the K slices; it needs
         * no roles). Regions and signposts arrive in G2; relief stays OFF until L1, so the Emerald-only relief modules
         * are never reached. */
        if (opts != NULL && opts->wantBuildings)
            e = run_buildings(&w, opts, out);
        if (e != RG_OK) {
            rg_output_free(out);
            rg_world_close(&w);
            return e;
        }
        out->layouts = w.layoutCount;
        out->maps = w.mapCount;
        out->tilesets = w.tilesetCount - 1u;
        out->pairs = w.pairCount;
        out->outdoorMaps = w.outdoorMaps;
        rg_world_close(&w);
        return RG_OK;
    }
    e = rg_roles_init(&w, &r);
    if (e != RG_OK) {
        rg_world_close(&w);
        return e;
    }
    for (li = 0; li < w.layoutCount; li++)
        total += w.layouts[li].present;
    for (pi = 0; pi < w.pairCount && e == RG_OK; pi++)
        e = run_pair(&w, &r, &signs, pi, opts, out, &done, total);
    if (e == RG_OK && opts != NULL && opts->wantBuildings)
        e = run_buildings(&w, opts, out);
    if (e == RG_OK && opts != NULL && opts->relief != RG_RELIEF_OFF)
        e = run_relief(&w, &r, opts, out);
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
    free(out->buildings);
    free(out->relief);
    out->regions = out->signs = out->buildings = out->relief = NULL;
    out->regionsSize = out->signsSize = out->buildingsSize = out->reliefSize = 0;
}
