/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/gen_voxel_relief.py (_gauss_seidel,
 * _robust, _give_up_seams, _blocks, world_levels: rel:762-1086), MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
/* rg_rworld.c -- the world's levels (3DGBA, GPLv3). Pure C.
 *
 * Float order follows upstream (SPEC-S3 2.2): Gauss-Seidel updates in place in node order, each update's two sums are
 * Python sum() (Neumaier from CPython 3.12, in 3.14 ints in the float path too; naive in 3.11). Weights keep their
 * Python type (int 1 / 16, float 0.01 / 1e6) because an int sum is exact and the first float after ints is added
 * plainly. Python round() is half-even (nearbyint). */
#include "rg_rworld.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "rg_geom.h"
#include "rg_pyset.h"
#include "rg_rlat.h"

RgWorldOpts rg_world_opts_default(void)
{
    RgWorldOpts o;

    o.naiveSum = !RG_PYSUM_COMPENSATED;
    return o;
}

/* ---- sums with CPython's typing ---- */

/* One sample / adjacency entry: d the offset, w the weight, wi 1 when the weight is a Python int, o the other node. */
typedef struct Smp { double d, w; int32_t o; uint8_t wi; } Smp;

/* sum() of floats only (the update numerators). */
static double fsum_mode(const double *v, unsigned n, bool naive)
{
    double f = 0.0, c = 0.0;
    unsigned i;

    if (naive) {
        for (i = 0; i < n; i++)
            f += v[i];
        return f;
    }
    for (i = 0; i < n; i++) {
        double x = v[i], t = f + x;

        c += fabs(f) >= fabs(x) ? (f - t) + x : (x - t) + f;
        f = t;
    }
    return (c != 0.0 && isfinite(c)) ? f + c : f;
}

/* sum() of weights, CPython 3.14 (checked against the interpreter's builtin sum on mixed lists, test A): ints exactly
 * while they last; the first float is added to that int sum plainly (PyNumber_Add), then every item, ints included,
 * goes through the Neumaier loop. All-float lists are the plain compensated sum. */
static double wsum(const Smp *s, unsigned n, bool naive, uint8_t *allInt)
{
    double acc = 0.0, c = 0.0;
    unsigned i = 0;

    *allInt = 0;
    if (naive) {
        for (i = 0; i < n; i++)
            acc += s[i].w;
        for (i = 0; i < n && s[i].wi; i++) {}
        *allInt = (uint8_t)(i == n);
        return acc;
    }
    while (i < n && s[i].wi)
        acc += s[i++].w;
    if (i == n) {
        *allInt = 1;
        return acc;
    }
    acc += s[i++].w;                                  /* int sum + first float: one plain add */
    for (; i < n; i++) {
        double x = s[i].w, t = acc + x;

        c += fabs(acc) >= fabs(x) ? (acc - t) + x : (x - t) + acc;
        acc = t;
    }
    return (c != 0.0 && isfinite(c)) ? acc + c : acc;
}

static int cmp_dbl(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;

    return (x > y) - (x < y);
}

/* sorted(v)[len(v) // 2] (index form, no NaN reaches it). */
static bool median_of(const double *v, unsigned n, double *out)
{
    double *t;

    assert(n > 0 && out != NULL);
    t = (double *)malloc(n * sizeof(double));
    if (t == NULL)
        return false;
    memcpy(t, v, n * sizeof(double));
    qsort(t, n, sizeof(double), cmp_dbl);
    *out = t[n / 2u];
    free(t);
    return true;
}

static bool median_d(const Smp *s, unsigned n, double *out)
{
    double *t = (double *)malloc((n ? n : 1u) * sizeof(double));
    unsigned i;
    bool ok;

    if (t == NULL)
        return false;
    for (i = 0; i < n; i++)
        t[i] = s[i].d;
    ok = median_of(t, n, out);
    free(t);
    return ok;
}

/* ---- sample sets: dict {(a, b): [(d, w)...]} in key insertion order ---- */

typedef struct SKey { int32_t a, b; unsigned n, cap; Smp *s; } SKey;
typedef struct SSet { SKey *k; unsigned n, cap; uint32_t *ht; unsigned htCap; } SSet;

static void sset_free(SSet *s)
{
    unsigned i;

    for (i = 0; i < s->n; i++)
        free(s->k[i].s);
    free(s->k);
    free(s->ht);
    memset(s, 0, sizeof(*s));
}

static uint32_t pair_hash(int32_t a, int32_t b)
{
    return ((uint32_t)a * 2654435761u) ^ ((uint32_t)b * 40503u + 0x9E3779B9u);
}

static bool sset_rehash(SSet *s)
{
    unsigned cap = s->htCap ? s->htCap * 2u : 64u, i;
    uint32_t *ht = (uint32_t *)calloc(cap, sizeof(uint32_t));

    if (ht == NULL)
        return false;
    for (i = 0; i < s->n; i++) {
        unsigned at = pair_hash(s->k[i].a, s->k[i].b) & (cap - 1u);

        while (ht[at])
            at = (at + 1u) & (cap - 1u);
        ht[at] = i + 1u;
    }
    free(s->ht);
    s->ht = ht;
    s->htCap = cap;
    return true;
}

/* The key's slot, created at the end when new; NULL on no memory. Pointers die at the next call. */
static SKey *sset_get(SSet *s, int32_t a, int32_t b)
{
    unsigned at;

    if ((s->n + 1u) * 2u > s->htCap && !sset_rehash(s))
        return NULL;
    at = pair_hash(a, b) & (s->htCap - 1u);
    while (s->ht[at]) {
        SKey *k = &s->k[s->ht[at] - 1u];

        if (k->a == a && k->b == b)
            return k;
        at = (at + 1u) & (s->htCap - 1u);
    }
    if (s->n == s->cap) {
        unsigned nc = s->cap ? s->cap * 2u : 32u;
        SKey *nk = (SKey *)realloc(s->k, nc * sizeof(SKey));

        if (nk == NULL)
            return NULL;
        s->k = nk;
        s->cap = nc;
    }
    memset(&s->k[s->n], 0, sizeof(SKey));
    s->k[s->n].a = a;
    s->k[s->n].b = b;
    s->ht[at] = ++s->n;
    return &s->k[s->n - 1u];
}

static bool skey_push(SKey *k, double d, double w, uint8_t wi)
{
    if (k->n == k->cap) {
        unsigned nc = k->cap ? k->cap * 2u : 4u;
        Smp *ns = (Smp *)realloc(k->s, nc * sizeof(Smp));

        if (ns == NULL)
            return false;
        k->s = ns;
        k->cap = nc;
    }
    k->s[k->n].d = d;
    k->s[k->n].w = w;
    k->s[k->n].o = 0;
    k->s[k->n].wi = wi;
    k->n++;
    return true;
}

static bool sset_add(SSet *s, int32_t a, int32_t b, double d, double w, uint8_t wi)
{
    SKey *k = sset_get(s, a, b);

    return k != NULL && skey_push(k, d, w, wi);
}

/* ---- _gauss_seidel (rel:851-875) ---- */

typedef struct Pair { int32_t a, b; double d, w; uint8_t wi, alive; } Pair;

typedef struct Adj { unsigned *start; Smp *e; double *terms; } Adj;

static void adj_free(Adj *g)
{
    free(g->start);
    free(g->e);
    free(g->terms);
    memset(g, 0, sizeof(*g));
}

/* adj[a].append((b, d, w)); adj[b].append((a, -d, w)) in pair order. */
static bool adj_build(Adj *g, unsigned n, const Pair *p, unsigned np)
{
    unsigned i, total = 0, maxDeg = 0;
    unsigned *cur;

    memset(g, 0, sizeof(*g));
    g->start = (unsigned *)calloc((size_t)n + 2u, sizeof(unsigned));
    if (g->start == NULL)
        return false;
    for (i = 0; i < np; i++) {
        if (!p[i].alive)
            continue;
        g->start[p[i].a + 1]++;
        g->start[p[i].b + 1]++;
        total += 2u;
    }
    for (i = 0; i < n; i++) {
        if (g->start[i + 1] > maxDeg)
            maxDeg = g->start[i + 1];
        g->start[i + 1] += g->start[i];
    }
    g->e = (Smp *)malloc((total ? total : 1u) * sizeof(Smp));
    g->terms = (double *)malloc((maxDeg ? maxDeg : 1u) * sizeof(double));
    cur = (unsigned *)malloc(((size_t)n + 1u) * sizeof(unsigned));
    if (g->e == NULL || g->terms == NULL || cur == NULL) {
        free(cur);
        adj_free(g);
        return false;
    }
    memcpy(cur, g->start, ((size_t)n + 1u) * sizeof(unsigned));
    for (i = 0; i < np; i++) {
        Smp x;

        if (!p[i].alive)
            continue;
        x.d = p[i].d; x.w = p[i].w; x.wi = p[i].wi; x.o = p[i].b;
        g->e[cur[p[i].a]++] = x;
        x.d = -p[i].d; x.o = p[i].a;
        g->e[cur[p[i].b]++] = x;
    }
    free(cur);
    return true;
}

/* Least squares over h[a] - h[b] = d (weights w), the nodes with fx[] held at fv[]. Returns n doubles or NULL. */
static double *gauss_seidel(unsigned n, const Pair *p, unsigned np, const uint8_t *fx, const double *fv, bool naive)
{
    double *h = (double *)calloc(n ? n : 1u, sizeof(double));
    Adj g;
    unsigned it, r, k;

    if (h == NULL || !adj_build(&g, n, p, np)) {
        free(h);
        return NULL;
    }
    for (r = 0; r < n; r++)
        if (fx[r])
            h[r] = fv[r];
    for (it = 0; it < RG_GS_ITER; it++) {
        double delta = 0.0;

        for (r = 0; r < n; r++) {
            const unsigned lo = g.start[r], cnt = g.start[r + 1] - lo;
            uint8_t allInt;
            double v, ch;

            if (fx[r] || cnt == 0)
                continue;
            for (k = 0; k < cnt; k++)
                g.terms[k] = g.e[lo + k].w * (h[g.e[lo + k].o] + g.e[lo + k].d);
            v = fsum_mode(g.terms, cnt, naive) / wsum(&g.e[lo], cnt, naive, &allInt);
            ch = fabs(v - h[r]);
            delta = delta > ch ? delta : ch;
            h[r] = v;
        }
        if (delta < RG_GS_DELTA)
            break;
    }
    adj_free(&g);
    return h;
}

/* ---- _robust (rel:878-900) ---- */

typedef struct Ties { const RgTie *t; unsigned n; } Ties;

static bool is_tie(const Ties *t, int32_t a, int32_t b)
{
    unsigned i;

    for (i = 0; i < t->n; i++)
        if (t->t[i].a == a && t->t[i].b == b)
            return true;
    return false;
}

static bool pair_from_samples(Pair *p, const SKey *k, const Smp *s, unsigned n, bool naive)
{
    double d;

    if (!median_d(s, n, &d))
        return false;
    p->a = k->a; p->b = k->b; p->d = d;
    p->w = wsum(s, n, naive, &p->wi);
    p->alive = 1;
    return true;
}

static void pairs_ties(Pair *p, const Ties *t)
{
    unsigned i;

    for (i = 0; i < t->n; i++) {
        p[i].a = t->t[i].a; p[i].b = t->t[i].b;
        p[i].d = 0.0; p[i].w = RG_HARD_WEIGHT; p[i].wi = 0; p[i].alive = 1;
    }
}

/* Each pair's median, the samples more than a half level (8 px) off the first answer dropped, solved again. The pairs
 * in `ties` (already in rg_set_order) are one level whatever the rest says. NULL on no memory. */
static double *robust(unsigned n, const SSet *sm, const uint8_t *fx, const double *fv, const Ties *ties, bool naive)
{
    Pair *p = (Pair *)malloc(((size_t)sm->n + ties->n + 1u) * sizeof(Pair));
    double *first = NULL, *out = NULL;
    unsigned np = ties->n, i, j;
    bool ok = p != NULL;

    if (ok)
        pairs_ties(p, ties);
    for (i = 0; ok && i < sm->n; i++) {
        if (is_tie(ties, sm->k[i].a, sm->k[i].b))
            continue;
        ok = pair_from_samples(&p[np++], &sm->k[i], sm->k[i].s, sm->k[i].n, naive);
    }
    if (ok)
        first = gauss_seidel(n, p, np, fx, fv, naive);
    ok = ok && first != NULL;
    np = ties->n;
    for (i = 0; ok && i < sm->n; i++) {
        const SKey *k = &sm->k[i];
        Smp *good;
        unsigned ng = 0;

        if (is_tie(ties, k->a, k->b))
            continue;
        good = (Smp *)malloc((k->n ? k->n : 1u) * sizeof(Smp));
        if (good == NULL) {
            ok = false;
            break;
        }
        for (j = 0; j < k->n; j++)
            if (fabs(first[k->a] - first[k->b] - k->s[j].d) <= 8.0)
                good[ng++] = k->s[j];
        if (ng)
            ok = pair_from_samples(&p[np++], k, good, ng, naive);
        free(good);
    }
    if (ok)
        out = gauss_seidel(n, p, np, fx, fv, naive);
    free(first);
    free(p);
    return out;
}

/* ---- _give_up_seams (rel:903-945) ---- */

/* round(v / LEVEL) * LEVEL as a Python int (half-even). */
static int32_t whole_level(double v)
{
    return (int32_t)(RG_LEVEL * (int)nearbyint(v / (double)RG_LEVEL));
}

/* The index of the worst pair (first of the largest error, strict >) over the alive pairs that pass `useWhole`, or -1. */
static int worst_pair(const Pair *p, unsigned np, const double *off, const int32_t *whole)
{
    int worst = -1;
    double err = whole ? 0.0 : 8.0;
    unsigned i;

    for (i = 0; i < np; i++) {
        double e;

        if (!p[i].alive || !(p[i].w >= (double)RG_SEAM_WEIGHT))
            continue;
        e = fabs(off[p[i].a] - off[p[i].b] - p[i].d);
        if (whole != NULL && !(fabs((double)(whole[p[i].a] - whole[p[i].b]) - p[i].d) > 8.0))
            continue;
        if (e > err) {
            worst = (int)i;
            err = e;
        }
    }
    return worst;
}

/* Solved, and while some pair is joined more than a half level off what it asks, the worst is given up. */
static double *give_up_seams(unsigned n, const SSet *sm, const uint8_t *fx, const double *fv, bool naive, unsigned *dropped)
{
    Pair *p = (Pair *)malloc(((size_t)sm->n + 1u) * sizeof(Pair));
    int32_t *whole = (int32_t *)malloc(((size_t)n + 1u) * sizeof(int32_t));
    unsigned i;
    bool ok = p != NULL && whole != NULL;

    for (i = 0; ok && i < sm->n; i++)
        ok = pair_from_samples(&p[i], &sm->k[i], sm->k[i].s, sm->k[i].n, naive);
    *dropped = 0;
    while (ok) {
        double *off = gauss_seidel(n, p, sm->n, fx, fv, naive);
        int worst;

        if (off == NULL)
            break;
        worst = worst_pair(p, sm->n, off, NULL);
        if (worst < 0) {
            for (i = 0; i < n; i++)
                whole[i] = whole_level(off[i]);
            worst = worst_pair(p, sm->n, off, whole);
        }
        if (worst < 0) {
            free(p);
            free(whole);
            return off;
        }
        p[worst].alive = 0;
        (*dropped)++;
        free(off);
    }
    free(p);
    free(whole);
    return NULL;
}

/* ---- _blocks (rel:948-976) ---- */

typedef struct Local { double *level; int32_t *block; unsigned n, nBlocks; } Local;

static void local_free(Local *l)
{
    free(l->level);
    free(l->block);
    memset(l, 0, sizeof(*l));
}

typedef struct SizeIdx { uint32_t size, idx; } SizeIdx;

static int cmp_size_desc(const void *a, const void *b)
{
    const SizeIdx *x = (const SizeIdx *)a, *y = (const SizeIdx *)b;

    return (x->size < y->size) - (x->size > y->size);
}

/* Blocks of regions joined by a run: blocks flooded from the biggest big region down (index order on equal sizes). */
static bool blocks_flood(const RgPrep *p, Local *l, uint8_t *anchor)
{
    const unsigned n = p->nRegions;
    unsigned *start = (unsigned *)calloc((size_t)n + 2u, sizeof(unsigned)), *nb, *stack;
    SizeIdx *ord = (SizeIdx *)malloc(((size_t)n + 1u) * sizeof(SizeIdx));
    unsigned i, r, k = 0;
    bool ok;

    nb = (unsigned *)malloc(((size_t)p->nRuns * 2u + 1u) * sizeof(unsigned));
    stack = (unsigned *)malloc(((size_t)n + 1u) * sizeof(unsigned));
    ok = start && ord && nb && stack;
    for (i = 0; ok && i < p->nRuns; i++) {
        start[p->runs[i].a + 1]++;
        start[p->runs[i].b + 1]++;
    }
    for (i = 0; ok && i < n; i++)
        start[i + 1] += start[i];
    if (ok) {
        unsigned *cur = (unsigned *)malloc(((size_t)n + 1u) * sizeof(unsigned));

        ok = cur != NULL;
        if (ok) {
            memcpy(cur, start, ((size_t)n + 1u) * sizeof(unsigned));
            for (i = 0; i < p->nRuns; i++) {
                nb[cur[p->runs[i].a]++] = (unsigned)p->runs[i].b;
                nb[cur[p->runs[i].b]++] = (unsigned)p->runs[i].a;
            }
            for (i = 0; i < n; i++) {
                ord[i].size = p->sizes[i];
                ord[i].idx = i;
            }
            ok = rg_stable_sort(ord, n, sizeof(SizeIdx), cmp_size_desc);
        }
        free(cur);
    }
    for (i = 0; ok && i < n; i++) {
        unsigned sp = 0;

        r = ord[i].idx;
        if (l->block[r] >= 0 || !p->big[r])
            continue;
        anchor[r] = 1;
        l->block[r] = (int32_t)k;
        stack[sp++] = r;
        while (sp) {
            unsigned c = stack[--sp], j;

            for (j = start[c]; j < start[c + 1]; j++)
                if (l->block[nb[j]] < 0) {
                    l->block[nb[j]] = (int32_t)k;
                    stack[sp++] = nb[j];
                }
        }
        k++;
    }
    l->nBlocks = k;
    free(start); free(ord); free(nb); free(stack);
    return ok;
}

/* The ties in CPython set order (rg_set_order over the (a, b) tuple hashes), into *out (n entries). */
static bool ties_in_set_order(const RgPrep *p, RgTie **out, unsigned *nOut)
{
    const unsigned n = p->nTies;
    int64_t *keys, *hashes, *ord;
    void *scratch;
    unsigned i, m;

    *out = NULL;
    *nOut = 0;
    if (n == 0)
        return true;
    keys = (int64_t *)malloc((size_t)n * 3u * sizeof(int64_t));
    scratch = malloc(rg_set_scratch_bytes(n));
    *out = (RgTie *)malloc((size_t)n * sizeof(RgTie));
    if (keys == NULL || scratch == NULL || *out == NULL) {
        free(keys); free(scratch); free(*out);
        *out = NULL;
        return false;
    }
    hashes = keys + n;
    ord = keys + 2u * n;
    for (i = 0; i < n; i++) {
        int64_t ab[2];

        ab[0] = p->ties[i].a; ab[1] = p->ties[i].b;
        keys[i] = (int64_t)(((uint64_t)(uint32_t)p->ties[i].a << 32) | (uint32_t)p->ties[i].b);
        hashes[i] = rg_hash64_tuple(ab, 2);
    }
    m = rg_set_order(keys, hashes, n, ord, scratch);
    for (i = 0; i < m; i++) {
        (*out)[i].a = (int32_t)((uint64_t)ord[i] >> 32);
        (*out)[i].b = (int32_t)(uint32_t)ord[i];
    }
    *nOut = m;
    free(keys);
    free(scratch);
    return true;
}

static bool blocks_of(const RgPrep *p, bool naive, Local *l)
{
    const unsigned n = p->nRegions;
    uint8_t *anchor = (uint8_t *)calloc(n ? n : 1u, 1);
    double *zero = (double *)calloc(n ? n : 1u, sizeof(double));
    RgTie *ties = NULL;
    Ties tv = {NULL, 0};
    SSet sm;
    unsigned i, j;
    bool ok;

    memset(l, 0, sizeof(*l));
    memset(&sm, 0, sizeof(sm));
    l->n = n;
    l->block = (int32_t *)malloc((n ? n : 1u) * sizeof(int32_t));
    ok = anchor && zero && l->block && ties_in_set_order(p, &ties, &tv.n);
    if (!ok) {
        free(anchor); free(zero); free(ties); local_free(l);
        return false;
    }
    tv.t = ties;
    for (i = 0; i < n; i++)
        l->block[i] = -1;
    ok = blocks_flood(p, l, anchor);
    for (i = 0; ok && i < p->nRuns; i++) {
        SKey *k = sset_get(&sm, p->runs[i].a, p->runs[i].b);

        ok = k != NULL;
        for (j = 0; ok && j < p->runs[i].n; j++)
            ok = skey_push(k, (double)p->runs[i].v[j], 1.0, 1);
    }
    if (ok) {
        l->level = robust(n, &sm, anchor, zero, &tv, naive);
        ok = l->level != NULL;
    }
    sset_free(&sm);
    free(anchor); free(zero); free(ties);
    if (!ok)
        local_free(l);
    return ok;
}

/* ---- test API ---- */

double rg_wl_wsum(const double *w, const uint8_t *wi, unsigned n, bool naive)
{
    Smp *s = (Smp *)calloc(n ? n : 1u, sizeof(Smp));
    uint8_t allInt;
    double r;
    unsigned i;

    if (s == NULL)
        return NAN;
    for (i = 0; i < n; i++) {
        s[i].w = w[i];
        s[i].wi = wi[i];
    }
    r = wsum(s, n, naive, &allInt);
    free(s);
    return r;
}

static bool sset_from_flat(SSet *sm, const RgWlSample *s, unsigned n)
{
    unsigned i;

    memset(sm, 0, sizeof(*sm));
    for (i = 0; i < n; i++)
        if (!sset_add(sm, s[i].a, s[i].b, s[i].d, s[i].w, s[i].wi)) {
            sset_free(sm);
            return false;
        }
    return true;
}

double *rg_wl_robust(unsigned n, const RgWlSample *s, unsigned nSamples, const uint8_t *fx, const double *fv,
                     const RgTie *ties, unsigned nTies, bool naive)
{
    SSet sm;
    Ties tv;
    double *r;

    if (!sset_from_flat(&sm, s, nSamples))
        return NULL;
    tv.t = ties;
    tv.n = nTies;
    r = robust(n, &sm, fx, fv, &tv, naive);
    sset_free(&sm);
    return r;
}

double *rg_wl_give_up(unsigned n, const RgWlSample *s, unsigned nSamples, const uint8_t *fx, const double *fv, bool naive,
                      unsigned *dropped)
{
    SSet sm;
    double *r;

    if (!sset_from_flat(&sm, s, nSamples))
        return NULL;
    r = give_up_seams(n, &sm, fx, fv, naive, dropped);
    sset_free(&sm);
    return r;
}

double *rg_wl_gauss(unsigned n, const RgWlSample *s, unsigned nSamples, const uint8_t *fx, const double *fv, bool naive)
{
    Pair *p = (Pair *)malloc(((size_t)nSamples + 1u) * sizeof(Pair));
    double *r;
    unsigned i;

    if (p == NULL)
        return NULL;
    for (i = 0; i < nSamples; i++) {
        p[i].a = s[i].a; p[i].b = s[i].b; p[i].d = s[i].d; p[i].w = s[i].w; p[i].wi = s[i].wi; p[i].alive = 1;
    }
    r = gauss_seidel(n, p, nSamples, fx, fv, naive);
    free(p);
    return r;
}

/* ---- prepare and trim ---- */

static void prep_trim(RgPrep *p)
{
    RgCanvasMember *mem = p->cv.mem;
    unsigned nMem = p->cv.nMem;
    int cw = p->cv.cw, ch = p->cv.ch;

    free(p->region); free(p->cutApart); free(p->freeMid);
    p->region = NULL; p->cutApart = NULL; p->freeMid = NULL;
    p->cv.mem = NULL;
    rg_canvas_free(&p->cv);
    p->cv.mem = mem;
    p->cv.nMem = nMem;
    p->cv.cw = cw;
    p->cv.ch = ch;
}

void rg_world_prep_free(RgPrep *preps, unsigned n)
{
    unsigned g;

    for (g = 0; preps != NULL && g < n; g++)
        rg_prep_free(&preps[g]);
}

RgErr rg_world_prep_all(RgRCtx *c, const RgDrawn *d, RgPrep *preps)
{
    unsigned g;

    assert(c != NULL && d != NULL && preps != NULL);
    memset(preps, 0, (size_t)d->nGroups * sizeof(RgPrep));
    for (g = 0; g < d->nGroups; g++) {
        RgErr e;

        if (rg_drawn_excluded(&d->groups[g]))
            continue;
        e = rg_prepare(c, d, &d->groups[g], &preps[g]);
        if (e != RG_OK) {
            rg_world_prep_free(preps, d->nGroups);
            return e;
        }
        prep_trim(&preps[g]);
    }
    return RG_OK;
}

/* ---- world_levels: node table, samples, one solve ---- */

typedef struct NodeKey { uint8_t kind; int32_t id, k; } NodeKey;   /* kind 0: ("b", group, block), 1: ("f", layout) */

typedef struct Nodes { NodeKey *v; unsigned n, cap; } Nodes;

/* node(key): index, created at the end when new; -1 on no memory. */
static int node_of(Nodes *t, uint8_t kind, int32_t id, int32_t k)
{
    unsigned i;

    for (i = 0; i < t->n; i++)
        if (t->v[i].kind == kind && t->v[i].id == id && t->v[i].k == k)
            return (int)i;
    if (t->n == t->cap) {
        unsigned nc = t->cap ? t->cap * 2u : 64u;
        NodeKey *nv = (NodeKey *)realloc(t->v, nc * sizeof(NodeKey));

        if (nv == NULL)
            return -1;
        t->v = nv;
        t->cap = nc;
    }
    t->v[t->n].kind = kind;
    t->v[t->n].id = id;
    t->v[t->n].k = k;
    return (int)t->n++;
}

static int node_find(const Nodes *t, uint8_t kind, int32_t id, int32_t k)
{
    unsigned i;

    for (i = 0; i < t->n; i++)
        if (t->v[i].kind == kind && t->v[i].id == id && t->v[i].k == k)
            return (int)i;
    return -1;
}

typedef struct Ctx {
    const RgWorld *w;
    const RgDrawn *d;
    const RgPrep *preps;
    Local *local;                    /* per group (candidates only) */
    const RgMapLink *links;
    unsigned nLinks;
    bool naive;
} Ctx;

static bool add_loose(const Ctx *c, const uint8_t *in, Nodes *nd, SSet *sm)
{
    unsigned g, k;

    for (g = 0; g < c->d->nGroups; g++) {
        int mainN;

        if (!in[g])
            continue;
        mainN = node_of(nd, 0, (int32_t)g, 0);
        for (k = 1; mainN >= 0 && k < c->local[g].nBlocks; k++) {
            int nk = node_of(nd, 0, (int32_t)g, (int32_t)k);

            if (nk < 0 || !sset_add(sm, nk, mainN, 0.0, RG_LOOSE_WEIGHT, 0))
                return false;
        }
        if (mainN < 0)
            return false;
    }
    return true;
}

/* side(g, lid, edge, i): (node, level) or "None" (returns 0 with *node = -2 when the edge has no terrace there). */
static bool seam_side(const Ctx *c, Nodes *nd, int owner, uint16_t lid, uint8_t edge, int idx, int *node, double *level)
{
    const RgPrep *p;
    unsigned m;
    int32_t r;

    if (owner < 0) {
        *node = node_of(nd, 1, lid, 0);
        *level = 0.0;
        return *node >= 0;
    }
    p = &c->preps[owner];
    for (m = 0; m < p->cv.nMem && p->cv.mem[m].layout != lid; m++) {}
    assert(m < p->cv.nMem);
    r = p->edges[p->edgeOff[m * 4u + edge] + (unsigned)idx];
    if (r < 0) {
        *node = -2;
        return true;
    }
    *node = node_of(nd, 0, owner, c->local[owner].block[r]);
    *level = c->local[owner].level[r];
    return *node >= 0;
}

static uint8_t edge_of_dir(uint8_t dir)
{
    return dir == 1 ? RG_EDGE_DOWN : dir == 2 ? RG_EDGE_UP : dir == 3 ? RG_EDGE_LEFT : RG_EDGE_RIGHT;
}

static bool add_seams(const Ctx *c, const int *owner, Nodes *nd, SSet *sm)
{
    unsigned i, j;

    for (i = 0; i < c->nLinks; i++) {
        const RgMapLink *L = &c->links[i];
        const RgLayout *la = &c->w->layouts[L->a - 1], *lb = &c->w->layouts[L->b - 1];
        RgSeamCell sc[1024];
        unsigned ns;

        if (owner[L->a] >= 0 && owner[L->a] == owner[L->b])
            continue;                /* a seam inside a group: its canvas has it */
        ns = rg_seam_cells(L->dir, L->offset, la->w, la->h, lb->w, lb->h, sc, 1024u);
        for (j = 0; j < ns; j++) {
            int na, nb;
            double lva = 0.0, lvb = 0.0;

            if (!seam_side(c, nd, owner[L->a], L->a, edge_of_dir(sc[j].aEdge), sc[j].aIdx, &na, &lva) ||
                !seam_side(c, nd, owner[L->b], L->b, edge_of_dir(sc[j].bEdge), sc[j].bIdx, &nb, &lvb))
                return false;
            if (na == -2 || nb == -2)
                continue;
            if (!sset_add(sm, na, nb, lvb - lva, (double)RG_SEAM_WEIGHT, 1))
                return false;        /* off[a] + h[a] = off[b] + h[b] */
        }
    }
    return true;
}

/* One piece of the world per connected component, one node held at 0 in each (Littleroot in its own). */
static bool fix_components(const Nodes *nd, const SSet *sm, uint8_t *fx, double *fv)
{
    const unsigned n = nd->n;
    unsigned *start = (unsigned *)calloc((size_t)n + 2u, sizeof(unsigned)), *nb, *cur, *stack;
    int *comp = (int *)malloc(((size_t)n + 1u) * sizeof(int));
    unsigned i, o, root, sp;
    int rootN = node_find(nd, 1, RG_WORLD_ROOT, 0);
    bool ok;

    nb = (unsigned *)malloc(((size_t)sm->n * 2u + 1u) * sizeof(unsigned));
    cur = (unsigned *)malloc(((size_t)n + 1u) * sizeof(unsigned));
    stack = (unsigned *)malloc(((size_t)n + 1u) * sizeof(unsigned));
    ok = start && comp && nb && cur && stack;
    for (i = 0; ok && i < sm->n; i++) {
        start[sm->k[i].a + 1]++;
        start[sm->k[i].b + 1]++;
    }
    for (i = 0; ok && i < n; i++)
        start[i + 1] += start[i];
    if (ok) {
        memcpy(cur, start, ((size_t)n + 1u) * sizeof(unsigned));
        for (i = 0; i < sm->n; i++) {
            nb[cur[sm->k[i].a]++] = (unsigned)sm->k[i].b;
            nb[cur[sm->k[i].b]++] = (unsigned)sm->k[i].a;
        }
        for (i = 0; i < n; i++)
            comp[i] = -1;
        for (o = 0; o < n + 1u; o++) {
            root = o == 0 ? (rootN >= 0 ? (unsigned)rootN : n) : o - 1u;
            if (root >= n || comp[root] >= 0)
                continue;
            comp[root] = (int)root;
            fx[root] = 1;
            fv[root] = 0.0;
            sp = 0;
            stack[sp++] = root;
            while (sp) {
                unsigned cn = stack[--sp], j;

                for (j = start[cn]; j < start[cn + 1]; j++)
                    if (comp[nb[j]] < 0) {
                        comp[nb[j]] = (int)root;
                        stack[sp++] = nb[j];
                    }
            }
        }
    }
    free(start); free(comp); free(nb); free(cur); free(stack);
    return ok;
}

/* ---- the R2 log and the rounding ---- */

/* distance in pixels from v to the nearest half level (v mod 16 == 8). */
static double half_dist(double v)
{
    double t = v - (double)RG_LEVEL * floor(v / (double)RG_LEVEL);

    return fabs(t - (double)RG_LEVEL / 2.0);
}

typedef struct NearLog { RgNearHalf *v; unsigned n, cap; double minDist; } NearLog;

static bool near_note(NearLog *lg, uint8_t kind, unsigned group, int32_t idx, double value)
{
    double dist = half_dist(value);

    if (dist < lg->minDist)
        lg->minDist = dist;
    if (dist > RG_HALF_LOG_PX)
        return true;
    if (lg->n == lg->cap) {
        unsigned nc = lg->cap ? lg->cap * 2u : 16u;
        RgNearHalf *nv = (RgNearHalf *)realloc(lg->v, nc * sizeof(RgNearHalf));

        if (nv == NULL)
            return false;
        lg->v = nv;
        lg->cap = nc;
    }
    lg->v[lg->n].kind = kind;
    lg->v[lg->n].group = (uint16_t)group;
    lg->v[lg->n].idx = idx;
    lg->v[lg->n].value = value;
    lg->v[lg->n].distPx = dist;
    lg->n++;
    return true;
}

typedef struct SolveOut {
    int32_t *level[RG_DRAWN_MAX_GROUPS];
    unsigned nLevel[RG_DRAWN_MAX_GROUPS];
    int32_t base[512];
    uint8_t hasBase[512];
    RgBroken broken[RG_LEVELS_MAX_BROKEN];
    unsigned nBroken, brokenCells;
    NearLog near_;
    unsigned nodes, samples, seamsDropped;
} SolveOut;

static void solveout_free(SolveOut *s, unsigned nGroups)
{
    unsigned g;

    for (g = 0; g < nGroups; g++)
        free(s->level[g]);
    free(s->near_.v);
    memset(s, 0, sizeof(*s));
}

static bool regions_of(const Ctx *c, const uint8_t *in, const Nodes *nd, const double *off, SolveOut *so)
{
    unsigned g, r;

    for (g = 0; g < c->d->nGroups; g++) {
        const Local *l = &c->local[g];
        int mainN, nodeI;

        if (!in[g])
            continue;
        mainN = node_find(nd, 0, (int32_t)g, 0);
        so->nLevel[g] = l->n;
        so->level[g] = (int32_t *)malloc(((size_t)l->n + 1u) * sizeof(int32_t));
        if (so->level[g] == NULL || mainN < 0 || !near_note(&so->near_, 1, g, -1, off[mainN]))
            return false;
        for (r = 0; r < l->n; r++) {
            nodeI = l->block[r] >= 0 ? node_find(nd, 0, (int32_t)g, l->block[r]) : -1;
            if (nodeI >= 0) {
                double v = off[nodeI] + l->level[r];

                so->level[g][r] = whole_level(v);
                if (c->preps[g].big[r] && !near_note(&so->near_, 0, g, (int32_t)r, v))
                    return false;
            } else {
                so->level[g][r] = whole_level(off[mainN]);
            }
        }
    }
    return true;
}

static bool bases_of(const Nodes *nd, const double *off, SolveOut *so)
{
    unsigned i;

    for (i = 0; i < nd->n; i++)
        if (nd->v[i].kind == 1) {
            so->base[nd->v[i].id] = whole_level(off[i]);
            so->hasBase[nd->v[i].id] = 1;
            if (!near_note(&so->near_, 2, 0, nd->v[i].id, off[i]))
                return false;
        }
    return true;
}

/* Counter[(names)] += 1 for every seam sample more than half a level off the solution. */
static void count_broken(const Nodes *nd, const SSet *sm, const double *off, SolveOut *so)
{
    unsigned i, j, q;

    for (i = 0; i < sm->n; i++) {
        const SKey *k = &sm->k[i];
        const NodeKey *na = &nd->v[k->a], *nb = &nd->v[k->b];

        for (j = 0; j < k->n; j++) {
            if (!(k->s[j].w == (double)RG_SEAM_WEIGHT && fabs(off[k->a] - off[k->b] - k->s[j].d) > 8.0))
                continue;
            for (q = 0; q < so->nBroken; q++)
                if (so->broken[q].kindA == na->kind && so->broken[q].kindB == nb->kind &&
                    so->broken[q].idA == (uint16_t)na->id && so->broken[q].idB == (uint16_t)nb->id)
                    break;
            if (q == so->nBroken && so->nBroken < RG_LEVELS_MAX_BROKEN) {
                so->broken[q].kindA = na->kind; so->broken[q].kindB = nb->kind;
                so->broken[q].idA = (uint16_t)na->id; so->broken[q].idB = (uint16_t)nb->id;
                so->broken[q].cells = 0;
                so->nBroken++;
            }
            if (q < so->nBroken)
                so->broken[q].cells++;
            so->brokenCells++;
        }
    }
}

/* solve(groups) (rel:978-1043). `in[g]` selects the groups. */
static bool solve_world(const Ctx *c, const uint8_t *in, SolveOut *so)
{
    Nodes nd = {NULL, 0, 0};
    SSet sm;
    int owner[512];
    uint8_t *fx = NULL;
    double *fv = NULL, *off = NULL;
    unsigned g, i, dropped = 0;
    bool ok;

    memset(&sm, 0, sizeof(sm));
    for (i = 0; i < 512; i++)
        owner[i] = -1;
    for (g = 0; g < c->d->nGroups; g++)       /* owner.setdefault(lid, g) in group order */
        for (i = 0; in[g] && i < c->d->groups[g].nMembers; i++) {
            uint16_t lid = c->d->pool[c->d->groups[g].first + i].layout;

            if (owner[lid] < 0)
                owner[lid] = (int)g;
        }
    ok = add_loose(c, in, &nd, &sm) && add_seams(c, owner, &nd, &sm);
    fx = (uint8_t *)calloc(nd.n ? nd.n : 1u, 1);
    fv = (double *)calloc(nd.n ? nd.n : 1u, sizeof(double));
    ok = ok && fx && fv && fix_components(&nd, &sm, fx, fv);
    if (ok)
        off = give_up_seams(nd.n, &sm, fx, fv, c->naive, &dropped);
    ok = ok && off != NULL;
    so->near_.minDist = 1e9;
    ok = ok && regions_of(c, in, &nd, off, so) && bases_of(&nd, off, so);
    if (ok) {
        count_broken(&nd, &sm, off, so);
        so->nodes = nd.n;
        for (i = 0; i < sm.n; i++)
            so->samples += sm.k[i].n;
        so->seamsDropped = dropped;
    }
    free(off); free(fx); free(fv); free(nd.v);
    sset_free(&sm);
    return ok;
}

/* ---- spread (rel:1045-1056) and the ground-spread loop ---- */

/* The share of the group's footprint cells standing more than MASSIF off the level most of them stand at. */
static double spread_of(const RgPrep *p, const int32_t *level)
{
    int32_t key[1024];
    unsigned cnt[1024], nk = 0, total = 0, far = 0, i, c, mode = 0;
    size_t cell, cells = (size_t)p->cv.cw * (size_t)p->cv.ch;

    for (cell = 0; cell < cells; cell++) {
        const RgCellStat *st = &p->stats[cell];
        unsigned best = 0;
        uint32_t bn = 0;
        int32_t lv;

        if (st->n == 0 || st->nCounts == 0 || !((double)st->ground >= RG_FOOTPRINT * (double)st->n))
            continue;
        for (c = 0; c < st->nCounts; c++)
            if (c == 0 || p->counts[st->cOff + c].n > bn) {      /* max(counts, key=counts.get): first maximum */
                best = c;
                bn = p->counts[st->cOff + c].n;
            }
        lv = level[p->counts[st->cOff + best].region];
        for (i = 0; i < nk && key[i] != lv; i++) {}
        if (i == nk) {
            assert(nk < 1024u);
            key[nk] = lv;
            cnt[nk++] = 0;
        }
        cnt[i]++;
    }
    if (nk == 0)
        return 0.0;
    for (i = 0; i < nk; i++) {
        total += cnt[i];
        if (cnt[i] > cnt[mode])                                  /* Counter.most_common(1): first maximum */
            mode = i;
    }
    for (i = 0; i < nk; i++)
        if (abs(key[i] - key[mode]) > RG_MASSIF)
            far += cnt[i];
    return (double)far / (double)total;
}

static void levels_take(RgLevels *lv, SolveOut *so, unsigned nGroups)
{
    unsigned g;

    for (g = 0; g < nGroups; g++) {
        lv->level[g] = so->level[g];
        lv->nLevel[g] = so->nLevel[g];
        so->level[g] = NULL;
    }
    memcpy(lv->base, so->base, sizeof(lv->base));
    memcpy(lv->hasBase, so->hasBase, sizeof(lv->hasBase));
    memcpy(lv->broken, so->broken, sizeof(lv->broken));
    lv->nBroken = so->nBroken;
    lv->brokenCells = so->brokenCells;
    lv->near_ = so->near_.v;
    lv->nNear = so->near_.n;
    lv->minHalfDistPx = so->near_.minDist;
    so->near_.v = NULL;
    lv->nodes = so->nodes;
    lv->samples = so->samples;
    lv->seamsDropped = so->seamsDropped;
}

void rg_levels_free(RgLevels *lv)
{
    unsigned g;

    if (lv == NULL)
        return;
    for (g = 0; g < RG_DRAWN_MAX_GROUPS; g++)
        free(lv->level[g]);
    free(lv->near_);
    memset(lv, 0, sizeof(*lv));
}

static RgErr levels_loop(Ctx *c, uint8_t *in, RgLevels *out)
{
    const unsigned nG = c->d->nGroups;

    for (;;) {
        SolveOut *so = (SolveOut *)calloc(1, sizeof(SolveOut));
        unsigned g, bad = 0;

        if (so == NULL)
            return RG_ERR_NOMEM;
        if (!solve_world(c, in, so)) {
            solveout_free(so, nG);
            free(so);
            return RG_ERR_NOMEM;
        }
        out->solves++;
        for (g = 0; g < nG; g++)
            if (in[g])
                out->quality[g] = spread_of(&c->preps[g], so->level[g]);
        for (g = 0; g < nG; g++)
            if (in[g] && out->quality[g] > RG_GROUND_SPREAD)
                bad++;
        if (bad == 0) {
            levels_take(out, so, nG);
            free(so);
            return RG_OK;
        }
        for (g = 0; g < nG; g++)
            if (in[g] && out->quality[g] > RG_GROUND_SPREAD) {
                in[g] = 0;
                out->dropped[g] = 1;
            }
        solveout_free(so, nG);
        free(so);
    }
}

RgErr rg_world_levels(const RgWorld *w, const RgDrawn *d, const RgPrep *preps, const RgWorldOpts *o, RgLevels *out)
{
    RgMapLink *links = (RgMapLink *)malloc(RG_MAP_LINKS_MAX * sizeof(RgMapLink));
    Local local[RG_DRAWN_MAX_GROUPS];
    uint8_t in[RG_DRAWN_MAX_GROUPS];
    Ctx c;
    unsigned g;
    RgErr e = RG_OK;

    assert(w != NULL && d != NULL && preps != NULL && out != NULL && d->nGroups <= RG_DRAWN_MAX_GROUPS);
    memset(out, 0, sizeof(*out));
    memset(local, 0, sizeof(local));
    memset(in, 0, sizeof(in));
    if (links == NULL)
        return RG_ERR_NOMEM;
    out->nGroups = d->nGroups;
    c.w = w; c.d = d; c.preps = preps; c.local = local; c.links = links;
    c.nLinks = rg_map_links_sorted(w, links, RG_MAP_LINKS_MAX);
    c.naive = o != NULL ? o->naiveSum : rg_world_opts_default().naiveSum;
    for (g = 0; g < d->nGroups && e == RG_OK; g++) {
        if (rg_drawn_excluded(&d->groups[g]))
            continue;
        in[g] = 1;
        if (!blocks_of(&preps[g], c.naive, &local[g]))
            e = RG_ERR_NOMEM;
    }
    if (e == RG_OK)
        e = levels_loop(&c, in, out);
    for (g = 0; g < d->nGroups; g++) {
        out->ok[g] = e == RG_OK ? in[g] : 0;
        local_free(&local[g]);
    }
    free(links);
    if (e != RG_OK)
        rg_levels_free(out);
    return e;
}
