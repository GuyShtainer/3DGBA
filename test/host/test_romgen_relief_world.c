// test_romgen_relief_world.c -- host test for phase 33 S3.5 (SPEC-S3 section 6): rg_rworld (_gauss_seidel, _robust,
// _give_up_seams, _blocks, world_levels, the GROUND_SPREAD loop, the R2 near-half-level log).
// Synthetic checks always run; the real-ROM checks run with ROMGEN_ROM=/abs/path/emerald.gba (else SKIP).
//   make -C tools/romgen test T=relief_world        (RG_PIN_DUMP=1 prints the pin table instead of checking it)
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rg_rworld.h"

static int sChecks, sFails, sSkips;
#define CHECK(c) do { ++sChecks; if (!(c)) { ++sFails; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static uint32_t sRng = 777u;
static uint32_t Rnd(void) { sRng = sRng * 1664525u + 1013904223u; return sRng >> 8; }

/* ---- sum(): known answers from CPython 3.14.0's builtin sum over the same mixed int/float lists ---- */

static void TestWsum(void)
{
    const double a[10] = {0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1};
    const uint8_t af[10] = {0};
    const double b[3] = {0.1, 1e16, -1e16};
    const uint8_t bi[3] = {0, 1, 1};
    const double c[3] = {1e16, 0.1, -1e16};
    const uint8_t ci[3] = {1, 0, 1};
    const double d[7] = {0.01, 16, 0.01, 16, 0.01, 0.01, 0.01};
    const uint8_t di[7] = {0, 1, 0, 1, 0, 0, 0};
    const double e[5] = {16, 16, 0.01, 0.01, 0.01};
    const uint8_t ei[5] = {1, 1, 0, 0, 0};
    const double f[3] = {1, 2, 3};
    const uint8_t fi[3] = {1, 1, 1};

    CHECK(rg_wl_wsum(a, af, 10, false) == 1.0);                     /* sum([0.1] * 10) */
    CHECK(rg_wl_wsum(a, af, 10, true) == 0.9999999999999999);       /* 3.11: naive */
    CHECK(rg_wl_wsum(b, bi, 3, false) == 0.1);                      /* sum([0.1, 10**16, -10**16]) */
    CHECK(rg_wl_wsum(c, ci, 3, false) == 0.0);                      /* sum([10**16, 0.1, -10**16]): first float added plainly */
    CHECK(rg_wl_wsum(d, di, 7, false) == 32.05);
    CHECK(rg_wl_wsum(e, ei, 5, false) == 32.03);
    CHECK(rg_wl_wsum(f, fi, 3, false) == 6.0 && rg_wl_wsum(f, fi, 3, true) == 6.0);
}

/* ---- Gauss-Seidel against a dense normal-equation solve ---- */

/* h[a] - h[b] = d, weights w: minimise sum w (h[a] - h[b] - d)^2 with the fixed nodes pinned; Gaussian elimination. */
static void DenseSolve(unsigned n, const RgWlSample *s, unsigned ns, const uint8_t *fx, const double *fv, double *out)
{
    enum { N = 16 };
    double A[N][N + 1];
    unsigned i, j, k;

    memset(A, 0, sizeof A);
    for (k = 0; k < ns; k++) {
        const int a = s[k].a, b = s[k].b;

        A[a][a] += s[k].w; A[b][b] += s[k].w; A[a][b] -= s[k].w; A[b][a] -= s[k].w;
        A[a][N] += s[k].w * s[k].d; A[b][N] -= s[k].w * s[k].d;
    }
    for (i = 0; i < n; i++)
        if (fx[i]) {
            for (j = 0; j <= N; j++) A[i][j] = 0;
            A[i][i] = 1;
            A[i][N] = fv[i];
        }
    for (i = 0; i < n; i++) {
        unsigned p = i;

        for (k = i + 1; k < n; k++) if (fabs(A[k][i]) > fabs(A[p][i])) p = k;
        for (j = 0; j <= N; j++) { double t = A[i][j]; A[i][j] = A[p][j]; A[p][j] = t; }
        for (k = 0; k < n; k++) {
            double f;

            if (k == i || A[i][i] == 0.0) continue;
            f = A[k][i] / A[i][i];
            for (j = i; j <= N; j++) A[k][j] -= f * A[i][j];
        }
    }
    for (i = 0; i < n; i++) out[i] = A[i][N] / A[i][i];
}

static void TestGauss(void)
{
    enum { N = 12, S = 40 };
    RgWlSample s[S];
    uint8_t fx[N] = {0};
    double fv[N] = {0}, dense[N], *gs;
    unsigned i, trial, bad = 0;
    RgWlSample chain[2] = {{1, 0, 5.0, 1.0, 1}, {2, 1, 3.0, 1.0, 1}};
    RgWlSample two[2] = {{1, 0, 0.0, 1.0, 1}, {1, 0, 10.0, 3.0, 1}};
    uint8_t cfx[3] = {1, 0, 0};
    double cfv[3] = {0, 0, 0}, *h;

    h = rg_wl_gauss(3, chain, 2, cfx, cfv, false);
    CHECK(h != NULL && fabs(h[0]) < 1e-9 && fabs(h[1] - 5.0) < 1e-2 && fabs(h[2] - 8.0) < 1e-2);
    free(h);
    h = rg_wl_gauss(2, two, 2, cfx, cfv, false);
    CHECK(h != NULL && fabs(h[1] - 7.5) < 1e-9);                      /* the weighted mean */
    free(h);
    for (trial = 0; trial < 20; trial++) {
        for (i = 0; i < S; i++) {
            s[i].a = (int32_t)(Rnd() % N);
            do { s[i].b = (int32_t)(Rnd() % N); } while (s[i].b == s[i].a);
            s[i].d = (double)((int)(Rnd() % 41u) - 20);
            s[i].w = (double)(1 + Rnd() % 5u);
            s[i].wi = 1;
        }
        for (i = 0; i < N; i++) s[i % S].a = (int32_t)i;               /* every node has an edge */
        memset(fx, 0, sizeof fx);
        fx[0] = fx[5] = 1; fv[0] = 0; fv[5] = 10;
        DenseSolve(N, s, S, fx, fv, dense);
        gs = rg_wl_gauss(N, s, S, fx, fv, false);
        CHECK(gs != NULL);
        for (i = 0; gs != NULL && i < N; i++) bad += fabs(gs[i] - dense[i]) > 0.1;
        free(gs);
    }
    CHECK(bad == 0);
}

static void TestRobustGiveUp(void)
{
    /* an outlier more than half a level off the first answer is dropped, the pair solved again */
    RgWlSample o[4] = {{1, 0, 0.0, 1.0, 1}, {1, 0, 0.0, 1.0, 1}, {1, 0, 0.0, 1.0, 1}, {1, 0, 20.0, 1.0, 1}};
    uint8_t fx[3] = {1, 0, 0};
    double fv[3] = {0, 0, 0}, *h;
    /* a tie keeps 2 at 1's level whatever the samples say */
    RgWlSample t[2] = {{1, 0, 10.0, 1.0, 1}, {2, 0, 30.0, 1.0, 1}};
    RgTie tie[1] = {{2, 1}};
    /* a loop of three seams, 33 off: spread 11 each, each > 8: one is given up */
    RgWlSample loop[3] = {{1, 0, 16.0, 16.0, 1}, {2, 1, 16.0, 16.0, 1}, {0, 2, 1.0, 16.0, 1}};
    /* a loop 16 short: 5.33 each, under a half level, but the whole levels disagree: given up by the second test */
    RgWlSample shortLoop[3] = {{1, 0, 16.0, 16.0, 1}, {2, 1, 16.0, 16.0, 1}, {0, 2, -16.0, 16.0, 1}};
    unsigned dropped = 99;
    int i;

    h = rg_wl_robust(3, o, 4, fx, fv, NULL, 0, false);
    CHECK(h != NULL && fabs(h[1]) < 1e-9);
    free(h);
    h = rg_wl_robust(3, t, 2, fx, fv, tie, 1, false);
    CHECK(h != NULL && fabs(h[2] - h[1]) < 1e-3);
    free(h);
    h = rg_wl_give_up(3, loop, 3, fx, fv, false, &dropped);
    CHECK(h != NULL && dropped == 1);
    for (i = 0; h != NULL && i < 3; i++) CHECK(isfinite(h[i]));
    free(h);
    dropped = 99;
    h = rg_wl_give_up(3, shortLoop, 3, fx, fv, false, &dropped);
    CHECK(h != NULL && dropped == 1);
    free(h);
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

static uint64_t Fnv(uint64_t h, int64_t v)
{
    int i;

    for (i = 0; i < 8; i++) { h ^= (uint64_t)(v >> (8 * i)) & 0xFFu; h *= 1099511628211ULL; }
    return h;
}

typedef struct Pin { uint16_t key; uint8_t isAlt, ok; uint32_t nLevel; uint64_t hash; } Pin;
#include "test_romgen_relief_world_pins.inc"
#define NPINS ((unsigned)(sizeof kPins / sizeof kPins[0]))

static uint64_t LevelHash(const RgLevels *lv, unsigned g)
{
    uint64_t h = 14695981039346656037ULL;
    unsigned i;

    h = Fnv(h, lv->ok[g]);
    for (i = 0; lv->level[g] != NULL && i < lv->nLevel[g]; i++) h = Fnv(h, lv->level[g][i]);
    return h;
}

/* everything the result says, hashed: levels, bases, the seams given up, the near log's counts */
static uint64_t AllHash(const RgLevels *lv)
{
    uint64_t h = 14695981039346656037ULL;
    unsigned g, i;

    for (g = 0; g < lv->nGroups; g++) h = Fnv(h, (int64_t)LevelHash(lv, g));
    for (i = 0; i < 512; i++) { h = Fnv(h, lv->hasBase[i]); h = Fnv(h, lv->base[i]); }
    for (i = 0; i < lv->nBroken; i++) {
        h = Fnv(h, lv->broken[i].kindA); h = Fnv(h, lv->broken[i].idA);
        h = Fnv(h, lv->broken[i].kindB); h = Fnv(h, lv->broken[i].idB); h = Fnv(h, lv->broken[i].cells);
    }
    return Fnv(Fnv(h, lv->brokenCells), lv->nNear);
}

typedef struct Real { RgWorld w; RgRoles r; RgDrawn *d; RgRCtx *ctx; RgPrep *preps; } Real;

static int Seams(const Real *R, const RgLevels *lv, unsigned *mismatch, unsigned *uncovered);

static void CheckLevels(const Real *R, const RgLevels *lv, unsigned *nOk, unsigned *nDropped)
{
    const RgDrawn *d = R->d;
    unsigned g, i, mul = 0;

    *nOk = *nDropped = 0;
    for (g = 0; g < d->nGroups; g++) {
        const RgDrawnGroup *G = &d->groups[g];

        if (rg_drawn_excluded(G)) {
            CHECK(!lv->ok[g] && lv->level[g] == NULL && !lv->dropped[g]);
            continue;
        }
        if (lv->ok[g]) {
            (*nOk)++;
            CHECK(lv->level[g] != NULL && lv->nLevel[g] == R->preps[g].nRegions);
            CHECK(lv->quality[g] <= RG_GROUND_SPREAD);
            for (i = 0; i < lv->nLevel[g]; i++) mul += lv->level[g][i] % RG_LEVEL != 0;     /* H13: whole levels */
        } else {
            (*nDropped)++;
            CHECK(lv->dropped[g] && lv->level[g] == NULL && lv->quality[g] > RG_GROUND_SPREAD);
        }
    }
    CHECK(mul == 0);
    for (i = 0; i < 512; i++)
        if (lv->hasBase[i]) CHECK(lv->base[i] % RG_LEVEL == 0);
}

/* The R2 log: every entry is within RG_HALF_LOG_PX of a half level, and a terrace entry is the rounding of its value. */
static void CheckNear(const Real *R, const RgLevels *lv)
{
    unsigned i, exact = 0;

    for (i = 0; i < lv->nNear; i++) {
        const RgNearHalf *n = &lv->near_[i];
        double t = n->value - 16.0 * floor(n->value / 16.0);

        CHECK(n->distPx <= RG_HALF_LOG_PX && fabs(fabs(t - 8.0) - n->distPx) < 1e-9);
        exact += n->distPx == 0.0;
        if (n->kind == 0)
            CHECK(lv->ok[n->group] && lv->level[n->group][n->idx] == 16 * (int)nearbyint(n->value / 16.0) &&
                  R->preps[n->group].big[n->idx]);
    }
    CHECK(lv->minHalfDistPx >= 0.0 && lv->minHalfDistPx <= RG_HALF_LOG_PX);
    for (i = 0; i < lv->nNear; i++)                       /* R2: nothing but the exact ties is within a hair of a boundary */
        CHECK(lv->near_[i].distPx == 0.0 || lv->near_[i].distPx > 1e-3);
    CHECK(exact == 6);        /* group 345's three terraces 8 px and three 24 px off their anchors: exact in any sum order */
}

static void Dump(const RgDrawn *d, const RgLevels *lv)
{
    unsigned g;

    printf("static const Pin kPins[] = {\n");
    for (g = 0; g < d->nGroups; g++)
        printf("    {%u, %u, %u, %u, 0x%016llxULL},\n", d->groups[g].key, d->groups[g].isAlt, lv->ok[g], lv->nLevel[g],
               (unsigned long long)LevelHash(lv, g));
    printf("};\nstatic const uint64_t kAllHash = 0x%016llxULL;\n", (unsigned long long)AllHash(lv));
}

static void CheckPins(const RgDrawn *d, const RgLevels *lv)
{
    unsigned g, bad = 0;

    CHECK(d->nGroups == NPINS);
    for (g = 0; g < d->nGroups && g < NPINS; g++)
        bad += !(d->groups[g].key == kPins[g].key && d->groups[g].isAlt == kPins[g].isAlt && lv->ok[g] == kPins[g].ok &&
                 lv->nLevel[g] == kPins[g].nLevel && LevelHash(lv, g) == kPins[g].hash);
    CHECK(bad == 0);
    CHECK(AllHash(lv) == kAllHash);
}

static void CheckFacts(const Real *R, const RgLevels *lv)
{
    unsigned i, nz = 0;
    int dropped302 = 0;

    CHECK(lv->solves == 2);                               /* one round to find the massif group, one without it */
    for (i = 0; i < R->d->nGroups; i++)
        if (lv->dropped[i]) dropped302 += R->d->groups[i].key == 302 && !R->d->groups[i].isAlt;
    CHECK(dropped302 == 1);
    CHECK(lv->quality[0] <= RG_GROUND_SPREAD);
    CHECK(lv->hasBase[RG_WORLD_ROOT] && lv->base[RG_WORLD_ROOT] == 0);       /* Littleroot stands at 0 */
    for (i = 0; i < 512; i++)
        if (lv->hasBase[i] && lv->base[i] != 0) { nz++; CHECK(i == 14 && lv->base[i] == 48); }
    CHECK(nz == 1);
    CHECK(lv->nBroken == 8 && lv->brokenCells == 102);
    {
        const RgDrawnGroup *g20 = rg_drawn_group(R->d, 20, true, lv->ok);

        CHECK(g20 != NULL && g20->key == 20);
        CHECK(rg_drawn_group(R->d, 38, true, lv->ok) == NULL);        /* DRAWN_EXCLUDED */
        CHECK(rg_drawn_group(R->d, 302, true, lv->ok) == NULL);       /* dropped by the ground-spread rule */
        CHECK(rg_drawn_group(R->d, 302, false, lv->ok) != NULL);
    }
}

/* Seam cells (rel:1004-1030) re-walked here: where both sides stand at levels (a terrace or a plain map), they agree
 * unless the seam pair was given up. Returns the number of seam cells compared. */
static int Seams(const Real *R, const RgLevels *lv, unsigned *mismatch, unsigned *uncovered)
{
    RgMapLink *links = (RgMapLink *)malloc(RG_MAP_LINKS_MAX * sizeof *links);
    int owner[512], cmp = 0;
    unsigned i, j, g, m, nl;

    for (i = 0; i < 512; i++) owner[i] = -1;
    for (g = 0; g < R->d->nGroups; g++)
        for (m = 0; lv->ok[g] && m < R->d->groups[g].nMembers; m++) {
            uint16_t lid = R->d->pool[R->d->groups[g].first + m].layout;

            if (owner[lid] < 0) owner[lid] = (int)g;
        }
    *mismatch = *uncovered = 0;
    nl = rg_map_links_sorted(&R->w, links, RG_MAP_LINKS_MAX);
    for (i = 0; i < nl; i++) {
        const RgMapLink *L = &links[i];
        const RgLayout *la = &R->w.layouts[L->a - 1], *lb = &R->w.layouts[L->b - 1];
        RgSeamCell sc[1024];
        unsigned ns;

        if (owner[L->a] >= 0 && owner[L->a] == owner[L->b]) continue;
        ns = rg_seam_cells(L->dir, L->offset, la->w, la->h, lb->w, lb->h, sc, 1024u);
        for (j = 0; j < ns; j++) {
            int lvl[2], side;
            const uint16_t lid[2] = {L->a, L->b};
            const uint8_t edge[2] = {sc[j].aEdge, sc[j].bEdge};
            const int idx[2] = {sc[j].aIdx, sc[j].bIdx};

            for (side = 0; side < 2; side++) {
                int o = owner[lid[side]];

                if (o < 0) { lvl[side] = lv->base[lid[side]]; continue; }
                {
                    const RgPrep *p = &R->preps[o];
                    unsigned mm, e = edge[side] == 1 ? RG_EDGE_DOWN : edge[side] == 2 ? RG_EDGE_UP : edge[side] == 3 ? RG_EDGE_LEFT : RG_EDGE_RIGHT;
                    int32_t r;

                    for (mm = 0; p->cv.mem[mm].layout != lid[side]; mm++) {}
                    r = p->edges[p->edgeOff[mm * 4u + e] + (unsigned)idx[side]];
                    lvl[side] = r < 0 ? 0x7fffffff : lv->level[o][r];
                }
            }
            if (lvl[0] == 0x7fffffff || lvl[1] == 0x7fffffff) continue;
            cmp++;
            if (lvl[0] != lvl[1]) {
                unsigned q, hit = 0;
                int oa = owner[L->a], ob = owner[L->b];

                (*mismatch)++;
                for (q = 0; q < lv->nBroken; q++)
                    hit |= lv->broken[q].kindA == (oa < 0) && lv->broken[q].kindB == (ob < 0) &&
                           lv->broken[q].idA == (oa < 0 ? L->a : oa) && lv->broken[q].idB == (ob < 0 ? L->b : ob);
                *uncovered += !hit;
                if (!hit && getenv("RG_DEBUG")) printf("uncovered: %u(owner %d lvl %d) -> %u(owner %d lvl %d) dir %u off %d\n", L->a, oa, lvl[0], L->b, ob, lvl[1], L->dir, (int)L->offset);
            }
        }
    }
    free(links);
    return cmp;
}

static void RealChecks(Real *R, int dump)
{
    RgLevels a, b, c;
    RgWorldOpts neu = {false}, nai = {true};
    unsigned nOk, nDropped, mism, unc, i;

    CHECK(rg_world_levels(&R->w, R->d, R->preps, &neu, &a) == RG_OK);
    CHECK(rg_world_levels(&R->w, R->d, R->preps, &nai, &b) == RG_OK);
    CHECK(rg_world_levels(&R->w, R->d, R->preps, &neu, &c) == RG_OK);
    if (dump) { Dump(R->d, &a); goto done; }
    CHECK(AllHash(&a) == AllHash(&c) && a.nNear == c.nNear);                 /* deterministic */
    CHECK(AllHash(&a) == AllHash(&b));                                       /* both sum modes: the same levels, bases, seams */
    CHECK(a.nNear == b.nNear && a.nNear == 90);
    for (i = 0; i < a.nNear && i < b.nNear; i++) {
        CHECK(a.near_[i].kind == b.near_[i].kind && a.near_[i].group == b.near_[i].group && a.near_[i].idx == b.near_[i].idx);
        CHECK(fabs(a.near_[i].value - b.near_[i].value) < 1e-9);             /* ulp-level apart, never a rounding apart */
        CHECK(a.near_[i].distPx != 0.0 || a.near_[i].value == b.near_[i].value);   /* an exact tie is bit-equal */
    }
    CheckLevels(R, &a, &nOk, &nDropped);
    CHECK(nOk == 33 && nDropped == 1);
    CheckNear(R, &a);
    CheckFacts(R, &a);
    CheckPins(R->d, &a);
    CHECK(Seams(R, &a, &mism, &unc) > 1000);
    /* Every disagreeing cell is in a seam given up, except one seam cell (both directions, layouts 13/28, 32 vs 48): its two
     * region values (offset + level) straddle a half level although the nodes' offsets are within 8 px, the same
     * per-region rounding upstream does (rel:1036-1040). Pinned so a change shows. */
    CHECK(unc == 2 && mism == 104);
    printf("  seam cells compared with agreeing levels; mismatching cells %u (uncovered %u), levels hash %016llx\n",
           mism, unc, (unsigned long long)AllHash(&a));
done:
    rg_levels_free(&a);
    rg_levels_free(&b);
    rg_levels_free(&c);
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
    TestWsum();
    TestGauss();
    TestRobustGiveUp();
    TestRealRom();
    printf("test_romgen_relief_world: %d checks, %d failures%s\n", sChecks, sFails, sSkips ? " (SKIPPED real-ROM parts)" : "");
    return sFails != 0;
}
