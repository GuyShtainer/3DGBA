/* romgen_cli.c -- host tool: reads a Pokemon Emerald (BPEE) .gba and writes the voxel data files
 * (3DGBA, GPLv3). Pure host code around the romgen cores in source/romgen/.
 *
 *   romgen author ROM.gba census|art|preview|check|placements ...   (the Phase 34 authoring tool, rg_author.c)
 *   romgen ROM.gba OUTDIR [--time] [--only regions,signposts,buildings,relief] [--relief ledges|full|off] [--relief-layout ID]
 *         [--relief-log] [--relief-prep] [--relief-world] [--relief-sum neumaier|naive|both] [--dump-roles LAYOUT_ID] [--dump-model NAME]
 *
 * Writes OUTDIR/regions.bin, signposts.bin, buildings.bin and relief.bin (--relief full, the whole export of S3.7, is the default since S3.8; --relief ledges is the S3a ledges-only file).
 * --relief-layout ID prints that layout's relief.bin row and every cell's 25 heights. --dump-model NAME prints one model's summary and
 * writes nothing. Built with -DRG_MEMCOUNT (`make -C tools/romgen mem`, build/romgen_mem) --time also prints the
 * peak live heap of rg_run. The output is derived from the user's ROM: write it
 * outside the repo or under an ignored path. --dump-roles prints a layout with upstream's letters
 * (. floor, ~ water, _ ledge, = stairs, W wall, T tree, o prop, % shelf, | fence, # cliff, S signpost). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rg_author.h"
#include "rg_rdrawn.h"
#include "rg_rprep.h"
#include "rg_rworld.h"
#include "rg_run.h"
#include "rg_bspecs.h"
#include "rg_gameprof.h"
#include "rg_world.h"

#ifdef RG_MEMCOUNT
#include "rg_memcount.h"
#endif

static double NowMs(void)
{
    struct timespec ts;
    double ms;

    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    ms = (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
#ifdef RG_MEMCOUNT
    {   /* rg_run calls this at every phase boundary: log each time the peak has grown by >= 2 MB */
        static size_t lastLogged;
        static double first;

        if (first == 0.0)
            first = ms;
        if (rgm_peak() >= lastLogged + 2u * 1048576u) {
            lastLogged = rgm_peak();
            printf("memory: peak reached %.1f MB at +%.0f ms\n", (double)lastLogged / 1048576.0, ms - first);
        }
    }
#endif
    return ms;
}

static uint8_t *ReadFile(const char *path, size_t *n)
{
    FILE *fp = fopen(path, "rb");
    uint8_t *buf;
    long len;

    if (fp == NULL)
        return NULL;
    if (fseek(fp, 0, SEEK_END) != 0 || (len = ftell(fp)) <= 0 || len > 0x2000000L || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }
    buf = (uint8_t *)malloc((size_t)len);
    if (buf == NULL || fread(buf, 1, (size_t)len, fp) != (size_t)len) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    *n = (size_t)len;
    return buf;
}

static bool WriteFile(const char *dir, const char *name, const uint8_t *b, size_t n)
{
    char path[1024];
    FILE *fp;
    bool ok;

    snprintf(path, sizeof(path), "%s/%s", dir, name);
    fp = fopen(path, "wb");
    if (fp == NULL) {
        fprintf(stderr, "romgen: cannot write %s\n", path);
        return false;
    }
    ok = fwrite(b, 1, n, fp) == n;
    return fclose(fp) == 0 && ok;
}

static void DumpRoles(const RgOutput *o, unsigned id)
{
    static const char kLetters[11] = {'.', '~', '_', '=', 'W', 'T', 'o', '%', '|', '#', 'S'};
    const uint8_t *b = o->regions;
    unsigned count, i, x, y;

    if (b == NULL) {   /* e.g. --only buildings: no regions were made */
        fprintf(stderr, "romgen: --dump-roles needs regions in the run (not --only without regions)\n");
        return;
    }
    count = (unsigned)(b[4] | (b[5] << 8));

    for (i = 0; i < count; i++) {
        const uint8_t *row = b + 8 + 12u * i;
        unsigned lid = (unsigned)(row[0] | (row[1] << 8)), w = (unsigned)(row[2] | (row[3] << 8)), h = (unsigned)(row[4] | (row[5] << 8));
        uint32_t off = (uint32_t)row[8] | ((uint32_t)row[9] << 8) | ((uint32_t)row[10] << 16) | ((uint32_t)row[11] << 24);

        if (lid != id)
            continue;
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++)
                putchar(kLetters[b[off + y * w + x] % 11u]);
            putchar('\n');
        }
        return;
    }
    fprintf(stderr, "romgen: layout %u is not in the output\n", id);
}

/* Prints one relief.bin row (id, cells, size, flags, base) and every cell's 25 stored heights. */
static void DumpReliefLayout(const RgOutput *o, unsigned id)
{
    const uint8_t *b = o->relief;
    unsigned count, i, k, j;

    if (b == NULL) {
        fprintf(stderr, "romgen: no relief.bin built (--relief off or --only without relief)\n");
        return;
    }
    count = (unsigned)(b[4] | (b[5] << 8));
    for (i = 0; i < count; i++) {
        const uint8_t *row = b + 8 + 14u * i;
        unsigned lid = (unsigned)(row[0] | (row[1] << 8)), cells = (unsigned)(row[2] | (row[3] << 8));
        unsigned w = (unsigned)(row[4] | (row[5] << 8)), hf = (unsigned)(row[6] | (row[7] << 8));
        uint32_t off = (uint32_t)row[8] | ((uint32_t)row[9] << 8) | ((uint32_t)row[10] << 16) | ((uint32_t)row[11] << 24);

        if (lid != id)
            continue;
        printf("relief layout %u: %u cells, %ux%u, flags %02x, base %d\n", lid, cells, w, hf & 0x3FFFu, hf >> 14,
               (int)(int16_t)(row[12] | (row[13] << 8)));
        for (k = 0; k < cells; k++) {
            const uint8_t *c = b + off + 27u * k;

            printf("cell (%u,%u):", c[0], c[1]);
            for (j = 0; j < 25; j++)
                printf(" %d", (int)(int8_t)c[2 + j]);
            putchar('\n');
        }
        return;
    }
    fprintf(stderr, "romgen: layout %u has no relief row\n", id);
}

static const char *KindName(RgSpecKind k)
{
    static const char *const kN[] = {"direct", "components", "kit", "props", "interior"};
    return (unsigned)k < 5u ? kN[k] : "?";
}

/* Builds every model (no output files), finds NAME and prints parts, triangles, the gate and its placements. */
static int DumpModel(const uint8_t *rom, size_t n, const char *name)
{
    RgWorld w;
    RgBuildModels ms;
    RgErr e = rg_world_open(&w, rom, n);
    unsigned i, k, found = 0;

    if (e != RG_OK) {
        fprintf(stderr, "romgen: %s\n", rg_err_str(e));
        return 1;
    }
    e = rg_build_models(&w, rg_specs, rg_spec_count, &ms);
    if (e != RG_OK) {
        fprintf(stderr, "romgen: %s\n", rg_err_str(e));
        rg_world_close(&w);
        return 1;
    }
    for (i = 0; i < ms.n && !found; i++) {
        const RgBuildModel *m = &ms.m[i];
        RgOrthoResult ortho;
        unsigned dens = 0;
        RgPlacementList pl;

        if (strcmp(m->spec->name, name) != 0)
            continue;
        found = 1;
        memset(&pl, 0, sizeof(pl));
        printf("model %s: kind %s, layout %u, %ux%u cells, art %dx%d px\n", name, KindName(m->spec->kind),
               m->spec->layoutId, m->w, m->h, m->art.w, m->art.h);
        printf("  parts %u, triangles %u\n", m->mesh.nNames, m->mesh.n);
        for (k = 0; k < m->mesh.nNames && k < 24; k++)
            printf("    part %u: %s\n", k, m->mesh.names[k]);
        if (rg_model_gate(m, &ortho, &dens))
            printf("  gate: ortho wrong %u missing %u extra %u, density %u -> %s\n", ortho.wrong, ortho.missing, ortho.extra,
                   dens, (ortho.wrong || ortho.missing || ortho.extra || dens) ? "FAIL" : "pass");
        else
            printf("  gate: out of memory\n");
        if (rg_find_placements(&w, m, &pl) == RG_OK) {
            printf("  placements: %u\n", pl.n);
            for (k = 0; k < pl.n && k < 16; k++)
                printf("    layout %u at cell (%d,%d) ground 0x%04X%s, %u odd cell(s)\n", pl.p[k].layout, pl.p[k].px,
                       pl.p[k].py, pl.p[k].ground, pl.p[k].patchAll ? " patchAll" : "", pl.p[k].nOdd);
        } else {
            printf("  placements: error\n");
        }
        rg_placements_free(&pl);
    }
    if (!found)
        fprintf(stderr, "romgen: no model named %s (%u models built)\n", name, ms.n);
    rg_models_free(&ms);
    rg_world_close(&w);
    return found ? 0 : 1;
}

/* --relief-log (S3.3): the drawn groups find_drawn makes (members in BFS order with their cell offsets, canvas in cells). */
static int DumpDrawn(const uint8_t *rom, size_t n)
{
    RgWorld w;
    RgDrawn *d = (RgDrawn *)malloc(sizeof *d);
    RgErr e;
    unsigned g, i;

    if (d == NULL || (e = rg_world_open(&w, rom, n)) != RG_OK) {
        free(d);
        return 1;
    }
    e = rg_drawn_find(&w, d);
    if (e != RG_OK) {
        fprintf(stderr, "romgen: find_drawn: %s\n", rg_err_str(e));
        rg_world_close(&w);
        free(d);
        return 1;
    }
    printf("find_drawn: %u seeds, %u links, %u groups\n", d->nSeeds, d->nLinks, d->nGroups);
    for (g = 0; g < d->nGroups; g++) {
        const RgDrawnGroup *G = &d->groups[g];

        printf("  group %u%s%s: %ux%u cells, %u member(s):", G->key, G->isAlt ? " (alternate)" : "",
               rg_drawn_excluded(G) ? " (excluded)" : "", G->cellsW, G->cellsH, G->nMembers);
        for (i = 0; i < G->nMembers; i++)
            printf(" %u@%d,%d", d->pool[G->first + i].layout, d->pool[G->first + i].x, d->pool[G->first + i].y);
        printf("\n");
    }
    rg_world_close(&w);
    free(d);
    return 0;
}

/* --relief-prep (S3.4): canvas + drawn_prepare for every group in world_levels' order (the excluded group skipped), one
 * line per group; with the mem build (`make -C tools/romgen mem`) the peak live heap of each group is printed too. The
 * relief.bin writers do not run: this is the S3.4 measurement tool. */
static int PrepAll(const uint8_t *rom, size_t n)
{
    RgWorld w;
    RgRoles r;
    RgDrawn *d = (RgDrawn *)malloc(sizeof *d);
    RgRCtx *ctx = NULL;
    RgErr e = RG_OK;
    unsigned g, done = 0;
    double t0 = NowMs(), tg;
#ifdef RG_MEMCOUNT
    size_t worst = 0;
#endif

    if (d == NULL || (e = rg_world_open(&w, rom, n)) != RG_OK)
        goto fail0;
    if ((e = rg_roles_init(&w, &r)) != RG_OK || (e = rg_roles_all(&w, &r, false, NULL, NULL)) != RG_OK)
        goto fail1;
    if ((e = rg_drawn_find(&w, d)) != RG_OK || (ctx = rg_rctx_new(&w, &r, &e)) == NULL)
        goto fail2;
    for (g = 0; g < d->nGroups; g++) {
        const RgDrawnGroup *G = &d->groups[g];
        RgPrep p;
        unsigned i, big = 0;
        uint64_t drops = 0;

        if (rg_drawn_excluded(G))
            continue;
#ifdef RG_MEMCOUNT
        rgm_reset();
#endif
        tg = NowMs();
        e = rg_prepare(ctx, d, G, &p);
        if (e != RG_OK) {
            rg_prep_free(&p);
            goto fail3;
        }
        for (i = 0; i < p.nRegions; i++)
            big += p.big[i];
        for (i = 0; i < p.nRuns; i++)
            drops += p.runs[i].n;
        printf("prep group %u%s: %ux%u px, %u regions (%u big), %u wrap cut(s), %u run keys, %llu drops, %u ties, %.0f ms",
               G->key, G->isAlt ? " (alt)" : "", (unsigned)(G->cellsW * 16u), (unsigned)(G->cellsH * 16u), p.nRegions, big,
               p.wrapCuts, p.nRuns, (unsigned long long)drops, p.nTies, NowMs() - tg);
#ifdef RG_MEMCOUNT
        printf(", peak %.1f MB", (double)rgm_peak() / 1048576.0);
        worst = rgm_peak() > worst ? rgm_peak() : worst;
#endif
        printf("\n");
        rg_prep_free(&p);
        done++;
    }
    printf("prep: %u groups, %.0f ms total, %u role-cache disagreement(s)", done, NowMs() - t0, rg_rctx_conflicts(ctx));
#ifdef RG_MEMCOUNT
    printf(", worst peak %.1f MB (%zu bytes)", (double)worst / 1048576.0, worst);
#endif
    printf("\n");
    rg_rctx_free(ctx);
    rg_roles_free(&r);
    rg_world_close(&w);
    free(d);
    return 0;
fail3:
    rg_rctx_free(ctx);
fail2:
fail1:
    rg_roles_free(&r);
    rg_world_close(&w);
fail0:
    fprintf(stderr, "romgen: --relief-prep: %s\n", rg_err_str(e));
    free(d);
    return 1;
}

static int CmpI32(const void *a, const void *b)
{
    int32_t x = *(const int32_t *)a, y = *(const int32_t *)b;

    return (x > y) - (x < y);
}

/* One group's terrace levels: "level:count" over its big regions, ascending. */
static void PrintGroupLevels(const RgPrep *p, const RgLevels *lv, unsigned g)
{
    int32_t *v = (int32_t *)malloc(((size_t)p->nRegions + 1u) * sizeof(int32_t));
    unsigned i, nv = 0, j;

    if (v == NULL)
        return;
    for (i = 0; i < p->nRegions; i++)
        if (p->big[i])
            v[nv++] = lv->level[g][i];
    qsort(v, nv, sizeof(int32_t), CmpI32);
    printf("   levels (px:terraces):");
    for (i = 0; i < nv; i = j) {
        for (j = i; j < nv && v[j] == v[i]; j++) {}
        printf(" %d:%u", (int)v[i], j - i);
    }
    printf("\n");
    free(v);
}

static void PrintLevels(const RgDrawn *d, const RgPrep *preps, const RgLevels *lv, const char *mode)
{
    unsigned g, i;

    printf("world levels (%s sums): %u ground-spread round(s), %u nodes, %u samples, %u seam pair(s) given up\n", mode,
           lv->solves, lv->nodes, lv->samples, lv->seamsDropped);
    for (g = 0; g < d->nGroups; g++) {
        const RgDrawnGroup *G = &d->groups[g];

        printf("  group %u%s: %s", G->key, G->isAlt ? " (alt)" : "",
               rg_drawn_excluded(G) ? "EXCLUDED (DRAWN_EXCLUDED)" : lv->ok[g] ? "ok" : "DROPPED (ground spread)");
        if (!rg_drawn_excluded(G))
            printf(", spread %.2f%%", 100.0 * lv->quality[g]);
        printf("\n");
        if (lv->ok[g])
            PrintGroupLevels(&preps[g], lv, g);
    }
    for (i = 0; i < 512; i++)
        if (lv->hasBase[i] && lv->base[i] != 0)
            printf("  plain map layout %u stands at %+d px\n", i, (int)lv->base[i]);
    printf("  seam cells given up: %u over %u pair(s)\n", lv->brokenCells, lv->nBroken);
    for (i = 0; i < lv->nBroken; i++)
        printf("    step at the seam %s %u / %s %u: %u cells\n", lv->broken[i].kindA ? "layout" : "group",
               lv->broken[i].kindA ? lv->broken[i].idA : d->groups[lv->broken[i].idA].key,
               lv->broken[i].kindB ? "layout" : "group",
               lv->broken[i].kindB ? lv->broken[i].idB : d->groups[lv->broken[i].idB].key, lv->broken[i].cells);
    printf("  R2: %u pre-rounding level(s) within %.1f px of a half level (smallest distance seen %.6g px)\n", lv->nNear,
           RG_HALF_LOG_PX, lv->minHalfDistPx);
    for (i = 0; i < lv->nNear; i++)
        printf("    near-half kind %u group-key %u idx %d value %.17g dist %.6g px\n", lv->near_[i].kind,
               lv->near_[i].kind == 2 ? 0u : (unsigned)d->groups[lv->near_[i].group].key, (int)lv->near_[i].idx, lv->near_[i].value, lv->near_[i].distPx);
}

/* --relief-world (S3.5): canvas + prepare for every group, then world_levels under both sum modes; prints the levels
 * per group, the plain-map bases, the groups dropped by the ground-spread rule, the seams given up and the R2 log. */
static int WorldAll(const uint8_t *rom, size_t n, int sumMode)
{
    RgWorld w;
    RgRoles r;
    RgDrawn *d = (RgDrawn *)malloc(sizeof *d);
    RgRCtx *ctx = NULL;
    RgPrep *preps = NULL;
    RgErr e = RG_OK;
    double t0 = NowMs();
    unsigned m;

    if (d == NULL || (e = rg_world_open(&w, rom, n)) != RG_OK)
        goto fail0;
    if ((e = rg_roles_init(&w, &r)) != RG_OK || (e = rg_roles_all(&w, &r, false, NULL, NULL)) != RG_OK)
        goto fail1;
    if ((e = rg_drawn_find(&w, d)) != RG_OK || (ctx = rg_rctx_new(&w, &r, &e)) == NULL)
        goto fail2;
    preps = (RgPrep *)calloc(d->nGroups, sizeof(RgPrep));
    if (preps == NULL) {
        e = RG_ERR_NOMEM;
        goto fail3;
    }
    if ((e = rg_world_prep_all(ctx, d, preps)) != RG_OK)
        goto fail4;
    printf("prepared %u groups in %.0f ms\n", d->nGroups, NowMs() - t0);
    for (m = 0; m < (sumMode == 2 ? 2u : 1u); m++) {
        RgLevels lv;
        RgWorldOpts o = rg_world_opts_default();
        double t1 = NowMs();

        if (sumMode == 2)
            o.naiveSum = m == 1;
        else
            o.naiveSum = sumMode == 1;
        e = rg_world_levels(&w, d, preps, &o, &lv);
        if (e != RG_OK)
            goto fail4;
        PrintLevels(d, preps, &lv, o.naiveSum ? "naive" : "Neumaier");
        printf("  world_levels: %.0f ms\n", NowMs() - t1);
        rg_levels_free(&lv);
    }
fail4:
    rg_world_prep_free(preps, d->nGroups);
    free(preps);
fail3:
    rg_rctx_free(ctx);
fail2:
fail1:
    rg_roles_free(&r);
    rg_world_close(&w);
fail0:
    if (e != RG_OK)
        fprintf(stderr, "romgen: --relief-world: %s\n", rg_err_str(e));
    free(d);
    return e != RG_OK;
}

int main(int argc, char **argv)
{
    const char *romPath = NULL, *outDir = NULL;
    bool timing = false, wantRegions = true, wantSigns = true, wantBuildings = true;
    bool wantRelief = true;
    RgReliefMode reliefMode = RG_RELIEF_FULL;   /* S3.8: the whole relief is the default; --relief ledges is the S3a file */
    int dumpId = 0, reliefId = 0, i;
    bool reliefLog = false, reliefPrep = false, reliefWorld = false;
    int sumMode = 2;                      /* --relief-sum neumaier|naive|both */
    const char *dumpModel = NULL;
    size_t n = 0;
    uint8_t *rom;
    RgRunOpts opts;
    RgOutput out;
    RgErr e;
    double t0;

    if (argc >= 2 && strcmp(argv[1], "author") == 0)   /* Phase 34 B0: romgen author ROM <command> ... (rg_author.c) */
        return rg_author_main(argc, argv);
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--time") == 0) {
            timing = true;
        } else if (strcmp(argv[i], "--only") == 0 && i + 1 < argc) {
            const char *v = argv[++i];
            wantRegions = strstr(v, "regions") != NULL;
            wantSigns = strstr(v, "signposts") != NULL;
            wantBuildings = strstr(v, "buildings") != NULL;
            wantRelief = strstr(v, "relief") != NULL;
        } else if (strcmp(argv[i], "--relief") == 0 && i + 1 < argc) {
            const char *v = argv[++i];

            if (strcmp(v, "ledges") == 0) {
                reliefMode = RG_RELIEF_LEDGES;
            } else if (strcmp(v, "full") == 0) {
                reliefMode = RG_RELIEF_FULL;
            } else if (strcmp(v, "off") == 0) {
                wantRelief = false;
            } else {
                fprintf(stderr, "romgen: --relief %s is not known (ledges|full|off)\n", v);
                return 2;
            }
        } else if (strcmp(argv[i], "--relief-log") == 0) {
            reliefLog = true;
        } else if (strcmp(argv[i], "--relief-prep") == 0) {
            reliefPrep = true;
        } else if (strcmp(argv[i], "--relief-world") == 0) {
            reliefWorld = true;
        } else if (strcmp(argv[i], "--relief-sum") == 0 && i + 1 < argc) {
            const char *v = argv[++i];

            sumMode = strcmp(v, "naive") == 0 ? 1 : strcmp(v, "neumaier") == 0 ? 0 : 2;
        } else if (strcmp(argv[i], "--relief-layout") == 0 && i + 1 < argc) {
            reliefId = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--dump-roles") == 0 && i + 1 < argc) {
            dumpId = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--dump-model") == 0 && i + 1 < argc) {
            dumpModel = argv[++i];
        } else if (romPath == NULL) {
            romPath = argv[i];
        } else if (outDir == NULL) {
            outDir = argv[i];
        }
    }
    if (romPath == NULL || (outDir == NULL && dumpModel == NULL)) {
        fprintf(stderr, "usage: romgen ROM.gba OUTDIR [--time] [--only regions,signposts,buildings,relief] [--relief ledges|full|off] [--relief-layout ID] [--relief-log] [--relief-prep] [--relief-world] [--relief-sum neumaier|naive|both] [--dump-roles LAYOUT_ID] [--dump-model NAME]\n");
        return 2;
    }
    rom = ReadFile(romPath, &n);
    if (rom == NULL) {
        fprintf(stderr, "romgen: cannot read %s\n", romPath);
        return 1;
    }
    { const GameProfile *gp = gameprof_detect_romgen(rom, n); printf("game: %s\n", gp == NULL ? "unsupported" : gp->game == GP_EMERALD ? "Emerald" : gp->game == GP_FIRERED ? "FireRed rev 1" : "LeafGreen rev 1"); }
    if (dumpModel != NULL) {
        int rc = DumpModel(rom, n, dumpModel);
        free(rom);
        return rc;
    }
    if (reliefWorld) {
        int rc = WorldAll(rom, n, sumMode);
        free(rom);
        return rc;
    }
    if (reliefPrep) {
        int rc = PrepAll(rom, n);
        free(rom);
        return rc;
    }
    if (reliefLog && DumpDrawn(rom, n) != 0)
        fprintf(stderr, "romgen: --relief-log failed\n");
    memset(&opts, 0, sizeof(opts));
    opts.nowMs = NowMs;
    opts.wantSigns = wantSigns;
    opts.wantBuildings = wantBuildings;
    opts.relief = wantRelief ? reliefMode : RG_RELIEF_OFF;
#ifdef RG_MEMCOUNT
    rgm_reset();   /* the ROM buffer read above is not rg_run's: count from here */
#endif
    t0 = NowMs();
    e = rg_run(rom, n, &opts, &out);
    if (e != RG_OK) {
        fprintf(stderr, "romgen: %s\n", rg_err_str(e));
        free(rom);
        return 1;
    }
    printf("world: %u layouts, %u maps, %u tilesets, %u pairs, %u outdoor maps\n", out.layouts, out.maps, out.tilesets,
           out.pairs, out.outdoorMaps);
    printf("roles: floor %u water %u ledge %u stair %u wall %u tree %u prop %u shelf %u fence %u cliff %u signpost %u\n",
           out.roleCount[0], out.roleCount[1], out.roleCount[2], out.roleCount[3], out.roleCount[4], out.roleCount[5],
           out.roleCount[6], out.roleCount[7], out.roleCount[8], out.roleCount[9], out.roleCount[10]);
    if (wantRegions && WriteFile(outDir, "regions.bin", out.regions, out.regionsSize))
        printf("regions.bin: %zu bytes, %u layouts\n", out.regionsSize, out.layouts);
    if (wantSigns && out.signs != NULL && WriteFile(outDir, "signposts.bin", out.signs, out.signsSize))
        printf("signposts.bin: %zu bytes, %u records (%u with a head, %u with an empty mask)\n", out.signsSize, out.signCount,
               out.headCount, out.emptyMasks);
    if (wantBuildings && out.buildings != NULL && WriteFile(outDir, "buildings.bin", out.buildings, out.buildingsSize)) {
        printf("buildings.bin: %zu bytes, %u models, %u pages, %u page-models, %u placements, %u vertices, %u masks, %u variants\n",
               out.buildingsSize, out.bModels, out.bPages, out.bPageModels, out.bPlacements, out.bVertices, out.bMasks,
               out.bVariants);
        printf("buildings gate: %u failing model(s)\n", out.buildingsFailed);
    }
    if (wantRelief && out.relief != NULL && WriteFile(outDir, "relief.bin", out.relief, out.reliefSize))
        printf("relief.bin: %zu bytes, %u rows, %u cells, %u ledge layouts, %u ledge cells (mode %s)\n", out.reliefSize,
               out.rst.rows, out.rst.cells, out.rst.ledgeLayouts, out.rst.ledgeCells, (reliefMode == RG_RELIEF_FULL && out.rst.drawnRows > 0) ? "full" : "ledges");
    if (wantRelief && out.relief != NULL && reliefMode == RG_RELIEF_FULL && out.rst.drawnRows > 0)
        printf("relief full: %u drawn rows, %u cut variants, %u cut table cells, %u groups (%u kept)\n", out.rst.drawnRows,
               out.rst.variants, out.rst.cuts, out.rst.groups, out.rst.groupsOk);
    if (timing)
        printf("time: total %.1f ms (world %.1f, roles %.1f, signs %.1f, serialise %.1f)\n", NowMs() - t0, out.msWorld,
               out.msRoles, out.msSigns, out.msWrite);
    if (timing && wantBuildings)
        printf("time: buildings models %.1f ms, gates %.1f ms, placements+write %.1f ms\n", out.msBuildModels, out.msChecks,
               out.msWriteBuildings);
    if (timing && wantRelief)
        printf("time: relief %.1f ms\n", out.msRelief);
#ifdef RG_MEMCOUNT
    if (timing)
        printf("memory: peak live heap of rg_run %.2f MB (%zu bytes), %zu allocations, %zu bytes still live (the outputs)\n",
               (double)rgm_peak() / 1048576.0, rgm_peak(), rgm_count(), rgm_live());
#endif
    if (dumpId > 0)
        DumpRoles(&out, (unsigned)dumpId);
    if (reliefId > 0)
        DumpReliefLayout(&out, (unsigned)reliefId);
    rg_output_free(&out);
    free(rom);
    return 0;
}
