// test_romgen_pyset.c -- host test for phase 33 S3.2: rg_pyset (CPython hash / set order / commonest), SPEC-S3 A.8.
// The vectors are interpreter facts (CPython 3.14.0 builtins hash() and set), no upstream code involved.
//   make -C tools/romgen test T=pyset
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rg_pyset.h"

static int sChecks, sFails;
#define CHECK(c) do { ++sChecks; if (!(c)) { ++sFails; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static int64_t Pack(const int *t, unsigned n)   /* tuple of small non-negative ints -> opaque key */
{
    int64_t k = 0;
    unsigned i;
    for (i = 0; i < n; i++) k = k * 1000 + t[i];
    return k + 1;                              /* never 0 */
}
static int64_t HashT(const int *t, unsigned n)
{
    int64_t it[4];
    unsigned i;
    for (i = 0; i < n; i++) it[i] = t[i];
    return rg_hash64_tuple(it, n);
}

/* set order of m tuples of arity n; compares against the expected tuple list. */
static void TupleOrder(const int *seq, unsigned m, unsigned n, const int *want)
{
    int64_t *keys = malloc(m * 8), *hs = malloc(m * 8), *out = malloc(m * 8);
    void *scr = malloc(rg_set_scratch_bytes(m));
    unsigned i, c, ok = 1;

    for (i = 0; i < m; i++) { keys[i] = Pack(seq + i * n, n); hs[i] = HashT(seq + i * n, n); }
    c = rg_set_order(keys, hs, m, out, scr);
    CHECK(c == m);
    for (i = 0; i < c && i < m; i++)
        if (out[i] != Pack(want + i * n, n)) ok = 0;
    CHECK(ok);
    free(keys); free(hs); free(out); free(scr);
}

static void TestHash(void)
{
    int64_t a[3];

    CHECK(rg_hash64_int(-1) == -2);
    CHECK(rg_hash64_int((((int64_t)1) << 61) - 1) == 0);
    CHECK(rg_hash64_int(((int64_t)1) << 61) == 1);
    CHECK(rg_hash64_int(0) == 0 && rg_hash64_int(7) == 7 && rg_hash64_int(-7) == -7);
    a[0] = 1; a[1] = 2;
    CHECK(rg_hash64_tuple(a, 2) == -3550055125485641917ll);
    a[2] = 3;
    CHECK(rg_hash64_tuple(a, 3) == 529344067295497451ll);
    a[0] = 255; a[1] = 0; a[2] = 255;
    CHECK(rg_hash64_tuple(a, 3) == -2057465547446278849ll);
    a[0] = a[1] = a[2] = 0;
    CHECK(rg_hash64_tuple(a, 3) == 3010437511937009226ll);
    a[0] = 222; a[1] = 180; a[2] = 164;
    CHECK(rg_hash64_tuple(a, 3) == 8411117838844654149ll);
}

static void TestOrder(void)
{
    static const int rgb[] = {222,180,164, 189,148,139, 238,213,205, 131,90,90, 98,65,82, 65,49,65, 156,115,115};
    static const int rgbW[] = {65,49,65, 222,180,164, 238,213,205, 98,65,82, 189,148,139, 131,90,90, 156,115,115};
    static const int pr[] = {9,8, 1,2, 3,4, 17,1, 2,9, 40,3};
    static const int prW[] = {17,1, 1,2, 3,4, 40,3, 2,9, 9,8};
    static const int ints[] = {5, 3, 40, 13, 21, 8, 16, 0, -16, -32};
    static const int intsW[] = {0, -32, 3, 5, 40, 8, 13, 16, -16, 21};
    static const int big[30][3] = {
        {12,11,185},{17,2,171},{21,9,197},{14,6,198},{15,16,189},{10,16,172},{20,7,175},{16,9,180},{30,15,178},{22,2,188},
        {7,3,199},{18,4,193},{8,13,190},{19,14,184},{1,10,191},{4,15,195},{29,5,187},{25,7,192},{2,3,182},{3,13,173},
        {6,1,177},{9,6,181},{28,12,196},{5,8,186},{23,12,179},{13,4,176},{26,0,183},{11,1,194},{27,10,174},{0,0,200}};
    int seq[90], i;
    int64_t keys[10], hs[10], out[10];
    char scr[4096];
    unsigned c, ok = 1;

    TupleOrder(rgb, 7, 3, rgbW);
    TupleOrder(pr, 6, 2, prW);
    for (i = 0; i < 10; i++) { keys[i] = ints[i]; hs[i] = rg_hash64_int(ints[i]); }
    CHECK(rg_set_scratch_bytes(10) <= sizeof(scr));
    c = rg_set_order(keys, hs, 10, out, scr);
    CHECK(c == 10);
    for (i = 0; i < 10; i++) if (out[i] != intsW[i]) ok = 0;
    CHECK(ok);
    for (i = 0; i < 30; i++) { seq[3*i] = (i*7) % 31; seq[3*i+1] = (i*3) % 17; seq[3*i+2] = 200 - i; }
    TupleOrder(seq, 30, 3, &big[0][0]);
    {   /* duplicates collapse, order of first insertion into the table is irrelevant to the result */
        int dup[] = {1,2, 3,4, 1,2, 3,4, 1,2};
        int64_t k[5], h[5], o[5];
        char s2[2048];
        for (i = 0; i < 5; i++) { k[i] = Pack(dup + 2*i, 2); h[i] = HashT(dup + 2*i, 2); }
        c = rg_set_order(k, h, 5, o, s2);
        CHECK(c == 2);
    }
    {   /* a large run: 1000 distinct ints; the count and a permutation check (resizes several times) */
        int64_t *K = malloc(1000 * 8), *H = malloc(1000 * 8), *O = malloc(1000 * 8);
        void *S = malloc(rg_set_scratch_bytes(1000));
        unsigned char seen[1000];
        memset(seen, 0, sizeof(seen));
        for (i = 0; i < 1000; i++) { K[i] = i * 37 % 1000; H[i] = rg_hash64_int(K[i]); }
        c = rg_set_order(K, H, 1000, O, S);
        CHECK(c == 1000);
        ok = 1;
        for (i = 0; i < 1000; i++) { if (O[i] < 0 || O[i] >= 1000 || seen[O[i]]) ok = 0; else seen[O[i]] = 1; }
        CHECK(ok);
        free(K); free(H); free(O); free(S);
    }
}

static void TestCommonest(void)
{
    /* ties go to the first key in set order: set order of [5,3,40,13,21,8,16,0,-16,-32] starts 0, -32, 3 */
    int v[] = {5, 3, 40, 13, 21, 8, 16, 0, -16, -32};
    char scr[8192];
    uint16_t cs[7];
    int v2[] = {40, 5, 5, 40, 3};
    int i;

    CHECK(rg_commonest_scratch_bytes(10) <= sizeof(scr));
    CHECK(rg_commonest_int(v, 10, scr) == 0);
    CHECK(rg_commonest_int(v2, 5, scr) == 40 || rg_commonest_int(v2, 5, scr) == 5);
    /* 5 and 40 tie at 2: set order of [40,5,3] is table order: 40 -> slot 0 (hash 40 & 7 = 0), 5 -> slot 5, 3 -> 3 */
    CHECK(rg_commonest_int(v2, 5, scr) == 40);
    {   /* colours: 222,180,164 is the (r,g,b) of BGR555 (r=27? ) -- build from the 8-bit values by exact preimage */
        static const int c8[7][3] = {{222,180,164},{189,148,139},{238,213,205},{131,90,90},{98,65,82},{65,49,65},{156,115,115}};
        int k, ok = 1;
        for (i = 0; i < 7; i++) {
            unsigned ch[3];
            for (k = 0; k < 3; k++) {
                unsigned f;
                for (f = 0; f < 32; f++) if (f * 255u / 31u == (unsigned)c8[i][k]) break;
                if (f == 32) ok = 0; else ch[k] = f;
            }
            cs[i] = (uint16_t)(ch[0] | (ch[1] << 5) | (ch[2] << 10));
        }
        CHECK(ok);
        if (ok) {   /* all distinct, one count each: the winner is the first in set order (65,49,65) */
            uint16_t w = rg_commonest_rgb(cs, 7, scr);
            CHECK(w == cs[5]);
            cs[3] = cs[6];                       /* now (156,115,115) twice: it wins by count */
            CHECK(rg_commonest_rgb(cs, 7, scr) == cs[6]);
        }
    }
}

int main(void)
{
    TestHash();
    TestOrder();
    TestCommonest();
    printf("%d checks, %d failures\n", sChecks, sFails);
    return sFails != 0;
}
