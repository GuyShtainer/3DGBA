// test_romgen_relief_solve.c -- host test for phase 33 S3.6 (SPEC-S3 section 6): rg_rsolve / rg_rplain (solve_drawn,
// solve, awash, ledges_on_ground, pier_ends, layout_heights).
// Synthetic checks always run; the real-ROM checks run with ROMGEN_ROM=/abs/path/emerald.gba (else SKIP).
//   make -C tools/romgen test T=relief_solve        (RG_PIN_DUMP=1 prints the pin table instead of checking it)
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rg_rsolve.h"

static int sChecks, sFails, sSkips;
#define CHECK(c) do { ++sChecks; if (!(c)) { ++sFails; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static uint64_t Fnv(uint64_t h, int64_t v)
{
    int i;

    for (i = 0; i < 8; i++) { h ^= (uint64_t)(v >> (8 * i)) & 0xFFu; h *= 1099511628211ULL; }
    return h;
}
static uint64_t FnvD(uint64_t h, double d)
{
    int64_t b;

    memcpy(&b, &d, 8);
    return Fnv(h, b);
}
#define FNV0 14695981039346656037ULL

static uint64_t LatHash(const RgLat *L)
{
    uint64_t h = FNV0;
    size_t i, n = (size_t)(L->w * RG_P + 1) * (size_t)(L->h * RG_P + 1);

    h = Fnv(Fnv(h, L->w), L->h);
    for (i = 0; i < n; i++) h = FnvD(h, L->v[i]);
    return h;
}

/* ---- D3: sqrt(dx*dx + dy*dy) is bit-identical to CPython 3.14's math.hypot on integers 0..64 ---- */

static void TestHypot(void)
{
    uint64_t h = FNV0;
    int a, b;

    for (a = 0; a <= 64; a++)
        for (b = 0; b <= 64; b++) h = FnvD(h, sqrt((double)(a * a + b * b)));
    CHECK(h == 0xed111a40d76c3471ULL);        /* recorded from math.hypot over the same grid (CPython 3.14.0 builtin) */
}

/* ---- real ROM ---- */

static uint8_t *ReadAll(const char *path, size_t *n)
{
    FILE *fp = fopen(path, "rb");
    long len;
    uint8_t *buf;

    if (fp == NULL) return NULL;
    fseek(fp, 0, SEEK_END); len = ftell(fp); fseek(fp, 0, SEEK_SET);
    buf = (uint8_t *)malloc((size_t)len);
    if (fread(buf, 1, (size_t)len, fp) != (size_t)len) { free(buf); fclose(fp); return NULL; }
    fclose(fp);
    *n = (size_t)len;
    return buf;
}

#include "test_romgen_relief_solve_pins.inc"

typedef struct Real { RgWorld w; RgRoles r; RgDrawn *d; RgRCtx *ctx; RgPrep *preps; RgLevels lv; RgSolved sv; } Real;

static double Ms(clock_t a) { return 1000.0 * (double)(clock() - a) / (double)CLOCKS_PER_SEC; }

static uint64_t AllHash(const RgSolved *s)
{
    uint64_t h = FNV0;
    unsigned i;

    for (i = 0; i < 512; i++)
        if (s->have[i]) h = Fnv(Fnv(Fnv(h, i), s->base[i]), (int64_t)LatHash(&s->shift[i]));
    return h;
}

static bool Finite(const RgLat *L)
{
    size_t i, n = (size_t)(L->w * RG_P + 1) * (size_t)(L->h * RG_P + 1);

    for (i = 0; i < n; i++)
        if (!isfinite(L->v[i])) return false;
    return true;
}

/* Every soil footprint cell's 25 points stand at or above its level (the lattice only ever raises them), absolute. */
static unsigned LowFootprints(const Real *R, unsigned *footCells)
{
    unsigned low = 0, g, m;

    *footCells = 0;
    for (g = 0; g < R->sv.nGroups; g++) {
        const RgSolvedGroup *G = R->sv.grp[g];

        for (m = 0; G != NULL && m < G->nMem; m++) {
            const RgCanvasMember *mm = &G->mem[m];
            const RgLat *L = &R->sv.shift[mm->layout];
            int x, y, i, j;

            if (!R->sv.have[mm->layout] || R->sv.groupOf[mm->layout] != g) continue;
            for (y = 0; y < mm->h; y++)
                for (x = 0; x < mm->w; x++) {
                    int32_t lv = G->cell[(size_t)(mm->oy + y) * (size_t)G->cw + (size_t)(mm->ox + x)];
                    RgGrid gr;

                    if (lv == RG_LV_NONE || !G->soil[(size_t)(mm->oy + y) * (size_t)G->cw + (size_t)(mm->ox + x)]) continue;
                    (*footCells)++;
                    rg_cell_grid(L, x, y, &gr);
                    for (j = 0; j < RG_SIDE; j++)
                        for (i = 0; i < RG_SIDE; i++)
                            low += gr.g[j][i] + (double)R->sv.base[mm->layout] < (double)lv - 1e-9;
                }
        }
    }
    return low;
}

static void PrintGroups(const Real *R)
{
    unsigned g, m;

    for (g = 0; g < R->sv.nGroups; g++) {
        const RgSolvedGroup *G = R->sv.grp[g];

        if (G == NULL) continue;
        printf("  group %3u%s %3dx%-3d rock %5u shapes %5u crests %3u lay sweeps %2u%s h %.1f..%.1f bases:", G->key,
               G->isAlt ? "alt" : "   ", G->cw, G->ch, G->nRock, G->nShapes, G->nCrests, G->laySweeps,
               G->layCapped ? "(CAPPED)" : "", G->minH, G->maxH);
        for (m = 0; m < G->nMem; m++)
            if (R->sv.have[G->mem[m].layout] && R->sv.groupOf[G->mem[m].layout] == g)
                printf(" %u:%d", G->mem[m].layout, R->sv.base[G->mem[m].layout]);
        printf("\n");
    }
}

static void Dump(const Real *R, uint64_t heights, uint64_t p4, uint64_t p20)
{
    unsigned i;

    printf("typedef struct LayoutPin { uint16_t lid; int32_t base; uint64_t hash; } LayoutPin;\nstatic const LayoutPin kPins[] = {\n");
    for (i = 0; i < 512; i++)
        if (R->sv.have[i]) printf("    {%u, %d, 0x%016llxULL},\n", i, R->sv.base[i], (unsigned long long)LatHash(&R->sv.shift[i]));
    printf("};\n#define NPINS ((unsigned)(sizeof kPins / sizeof kPins[0]))\n");
    printf("static const uint64_t kAllHash = 0x%016llxULL, kHeightsHash = 0x%016llxULL, kPlain4Hash = 0x%016llxULL, kPlain20Hash = 0x%016llxULL;\n",
           (unsigned long long)AllHash(&R->sv), (unsigned long long)heights, (unsigned long long)p4, (unsigned long long)p20);
}

static void CheckPins(const Real *R)
{
    unsigned i, n = 0, bad = 0;

    for (i = 0; i < 512; i++)
        if (R->sv.have[i]) {
            if (n < NPINS)
                bad += !(kPins[n].lid == i && kPins[n].base == R->sv.base[i] && kPins[n].hash == LatHash(&R->sv.shift[i]));
            n++;
        }
    CHECK(n == NPINS && bad == 0);
    CHECK(AllHash(&R->sv) == kAllHash);
}

/* rg_solve_plain on a layout, the whole result checked for sanity and hashed. */
static uint64_t PlainOne(const Real *R, uint16_t id, RgPlainStats *st)
{
    const RgLayout *L = &R->w.layouts[id - 1];
    RgPair *pair = rg_pair_open(&R->w, L->pairIndex);
    RgLat lat;
    uint64_t h = 0;
    clock_t t0 = clock();

    CHECK(pair != NULL && rg_solve_plain(&R->w, &R->r, L, pair, &lat, st) == RG_OK);
    CHECK(Finite(&lat) && lat.w == (int)L->w && lat.h == (int)L->h);
    CHECK(st->sweeps >= 1 && st->sweeps < RG_SOLVE_SWEEPS);              /* converged inside the 400-sweep cap */
    h = LatHash(&lat);
    printf("  solve(%u): %ux%u, %u land regions, %u sweeps, %u mounds (%u awash), h %.2f..%.2f, %.1f ms\n", id, L->w, L->h,
           st->regions, st->sweeps, st->mounds, st->awashMasses, st->minH, st->maxH, Ms(t0));
    rg_lat_free(&lat);
    rg_pair_close(pair);
    return h;
}

static uint64_t HeightsAll(Real *R, unsigned *nRows, unsigned *nBerm)
{
    uint64_t h = FNV0;
    unsigned id;

    *nRows = *nBerm = 0;
    for (id = 1; id <= R->w.layoutCount; id++) {
        RgLat lat;
        int led = 0;

        if (!R->w.layouts[id - 1].present || !rg_relief_outdoor((uint16_t)id)) continue;
        CHECK(rg_layout_heights(&R->w, &R->r, R->d, &R->lv, &R->sv, (uint16_t)id, &lat, &led) == RG_OK);
        if (lat.v == NULL) continue;
        CHECK(Finite(&lat));
        h = Fnv(Fnv(h, id), (int64_t)LatHash(&lat));
        (*nRows)++;
        *nBerm += (unsigned)led;
        rg_lat_free(&lat);
    }
    return h;
}

/* A layout that is neither drawn nor ENABLED: its lattice is the flat lattice plus its berms, exactly S3a's. */
static unsigned FlatLedgeIdentity(Real *R)
{
    unsigned id, checked = 0;

    for (id = 1; id <= R->w.layoutCount; id++) {
        const RgLayout *L = &R->w.layouts[id - 1];
        RgLat a, b;
        RgPair *pair;
        int led;

        if (!L->present || !rg_relief_outdoor((uint16_t)id) || rg_is_enabled_layout((uint16_t)id) ||
            rg_drawn_group(R->d, (uint16_t)id, true, R->lv.ok) != NULL)
            continue;
        pair = rg_pair_open(&R->w, L->pairIndex);
        CHECK(pair != NULL && rg_lat_new(&b, L->w, L->h));
        led = rg_ledge_berms(L, pair, &b);
        CHECK(led >= 0 && rg_layout_heights(&R->w, &R->r, R->d, &R->lv, &R->sv, (uint16_t)id, &a, NULL) == RG_OK);
        CHECK(memcmp(a.v, b.v, (size_t)(L->w * RG_P + 1) * (size_t)(L->h * RG_P + 1) * sizeof(double)) == 0);
        checked++;
        rg_lat_free(&a);
        rg_lat_free(&b);
        rg_pair_close(pair);
    }
    return checked;
}

static void RealChecks(Real *R, int dump)
{
    clock_t t0;
    unsigned foot = 0, low, nRows, nBerm, g, nSolved = 0, capped = 0, ownedBases = 0;
    RgSolved again;
    RgPlainStats s4, s20;
    uint64_t heights, p4, p20;
    RgWorldOpts neu = {false};

    CHECK(rg_world_levels(&R->w, R->d, R->preps, &neu, &R->lv) == RG_OK);
    t0 = clock();
    CHECK(rg_solve_all(R->ctx, &R->w, R->d, &R->lv, &R->sv) == RG_OK);
    printf("  solve_drawn: %.0f ms (prepare again + solve, every surviving group)\n", Ms(t0));
    for (g = 0; g < R->sv.nGroups; g++) {
        CHECK((R->sv.grp[g] != NULL) == (R->lv.ok[g] != 0));
        nSolved += R->sv.grp[g] != NULL;
        capped += R->sv.grp[g] != NULL && R->sv.grp[g]->layCapped;
    }
    CHECK(nSolved == 33 && capped == 0);
    {
        unsigned i;

        for (i = 0; i < 512; i++)
            if (R->sv.have[i]) {
                ownedBases++;
                CHECK(Finite(&R->sv.shift[i]));
            }
    }
    printf("  %u layouts own a lattice; groups:\n", ownedBases);
    PrintGroups(R);
    low = LowFootprints(R, &foot);
    CHECK(low == 0 && foot > 1000);
    printf("  footprint cells checked: %u, points below their level: %u\n", foot, low);
    t0 = clock();
    CHECK(rg_solve_all(R->ctx, &R->w, R->d, &R->lv, &again) == RG_OK);
    CHECK(AllHash(&again) == AllHash(&R->sv));                                        /* deterministic */
    rg_solved_free(&again);
    p4 = PlainOne(R, 4, &s4);
    p20 = PlainOne(R, 20, &s20);
    CHECK(s4.regions > 0);
    t0 = clock();
    heights = HeightsAll(R, &nRows, &nBerm);
    printf("  layout_heights over %u outdoor layouts: %.0f ms, %u berm cells, pier double writes %u\n", nRows, Ms(t0), nBerm,
           R->sv.pierDouble);
    CHECK(R->sv.pierDouble == 0);
    CHECK(nRows >= 80);
    printf("  flat+ledge identity checked on %u layouts\n", FlatLedgeIdentity(R));
    if (dump) { Dump(R, heights, p4, p20); return; }
    CheckPins(R);
    CHECK(heights == kHeightsHash && p4 == kPlain4Hash && p20 == kPlain20Hash);
}

static void TestRealRom(void)
{
    const char *path = getenv("ROMGEN_ROM");
    size_t n = 0;
    uint8_t *rom;
    Real R;
    RgErr e = RG_OK;

    memset(&R, 0, sizeof R);
    if (path == NULL || (rom = ReadAll(path, &n)) == NULL) {
        printf("SKIP real-ROM checks (ROMGEN_ROM not set or unreadable)\n");
        sSkips++;
        return;
    }
    R.d = (RgDrawn *)malloc(sizeof *R.d);
    CHECK(rg_world_open(&R.w, rom, n) == RG_OK);
    CHECK(rg_roles_init(&R.w, &R.r) == RG_OK && rg_roles_all(&R.w, &R.r, false, NULL, NULL) == RG_OK);
    CHECK(R.d != NULL && rg_drawn_find(&R.w, R.d) == RG_OK);
    R.ctx = rg_rctx_new(&R.w, &R.r, &e);
    CHECK(R.ctx != NULL);
    R.preps = (RgPrep *)calloc(R.d->nGroups, sizeof(RgPrep));
    CHECK(R.preps != NULL && rg_world_prep_all(R.ctx, R.d, R.preps) == RG_OK);
    RealChecks(&R, getenv("RG_PIN_DUMP") != NULL);
    rg_solved_free(&R.sv);
    rg_levels_free(&R.lv);
    rg_world_prep_free(R.preps, R.d->nGroups);
    free(R.preps);
    rg_rctx_free(R.ctx);
    free(R.d);
    rg_roles_free(&R.r);
    rg_world_close(&R.w);
    free(rom);
}

int main(void)
{
    TestHypot();
    TestRealRom();
    printf("test_romgen_relief_solve: %d checks, %d failures%s\n", sChecks, sFails, sSkips ? " (SKIPPED real-ROM parts)" : "");
    return sFails != 0;
}
