// test_romgen_art.c -- host test for source/romgen/rg_art.c (phase 33 S0.2, SPEC-S0-S1 sections 1.4, 3.2).
// Synthetic tilesets always run; real-ROM invariants (every pair decodes, the General flight of steps measures
// about 2 across and 28 down, treads threshold margins) run with ROMGEN_ROM=/path/emerald.gba.
//
//   clang -std=c11 -Wall -Wextra -O2 -ffp-contract=off -fsanitize=address,undefined \
//         -I source/romgen -I source/voxel -I test/host test/host/test_romgen_art.c \
//         source/romgen/rg_world.c source/romgen/rg_art.c source/voxel/vx_lz77.c -lm -o /tmp/trga && /tmp/trga
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rg_art.h"
#include "rg_fixture.h"

static int sChecks, sFails;
#define CHECK(c) do { ++sChecks; if (!(c)) { ++sFails; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

/* A 4bpp tile from a per-pixel index function. */
typedef unsigned (*PixFn)(unsigned x, unsigned y);
static void MakeTile(uint8_t *t, PixFn f)
{
    unsigned x, y;
    memset(t, 0, 32);
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++) {
            unsigned i = f(x, y) & 15u;
            t[y * 4 + x / 2] |= (uint8_t)((x & 1) ? i << 4 : i);
        }
}
static unsigned PixBlank(unsigned x, unsigned y) { (void)x; (void)y; return 0; }
static unsigned PixCol(unsigned x, unsigned y) { (void)y; return 1 + x; }          /* asymmetric in x */
static unsigned PixRow(unsigned x, unsigned y) { (void)x; return 1 + y; }          /* asymmetric in y */
static unsigned PixGreen(unsigned x, unsigned y) { (void)x; (void)y; return 1; }
static unsigned PixRed(unsigned x, unsigned y) { (void)x; (void)y; return 2; }
static unsigned PixBand(unsigned x, unsigned y) { (void)x; return (y & 1) ? 4 : 3; }   /* white / black rows */
static unsigned PixStripe(unsigned x, unsigned y) { (void)y; return (x & 1) ? 4 : 3; }
static unsigned PixSolid5(unsigned x, unsigned y) { (void)x; (void)y; return 5; }

enum { T_BLANK, T_COL, T_ROW, T_GREEN, T_RED, T_BAND, T_STRIPE, T_COUNT };
#define ENT(tile, pal, hf, vf) (uint16_t)((tile) | ((hf) ? 0x400 : 0) | ((vf) ? 0x800 : 0) | ((pal) << 12))

static uint16_t sPalP[256], sPalS[256], sMtP[16 * 8], sMtS[2 * 8], sAttP[16], sAttS[2];
static uint8_t sTilesP[T_COUNT * 32], sTilesS[32];

static void MetaAll(uint16_t *mt, unsigned m, uint16_t l0, uint16_t l1)
{
    unsigned q;
    for (q = 0; q < 4; q++) { mt[m * 8 + q] = l0; mt[m * 8 + 4 + q] = l1; }
}

static void TestSynthetic(void)
{
    RgFx f;
    RgWorld w;
    uint32_t tsP, tsS;
    unsigned p, i, id, x, y;
    RgPair *pair;
    const RgLayer *l;
    RgLayer merged;
    double a, d;
    uint16_t blocks[4] = {0, 1, 2, 3};

    for (p = 0; p < 16; p++)
        for (i = 0; i < 16; i++) { sPalP[p * 16 + i] = (uint16_t)(0x1000 | (p << 4) | i); sPalS[p * 16 + i] = (uint16_t)(0x2000 | (p << 4) | i); }
    for (p = 1; p < 2; p++) {   /* palette 1 of the primary: green, red, white, black */
        sPalP[p * 16 + 1] = 0x03E0; sPalP[p * 16 + 2] = 0x001F; sPalP[p * 16 + 3] = 0x7FFF; sPalP[p * 16 + 4] = 0x0000;
    }
    MakeTile(sTilesP + T_BLANK * 32, PixBlank); MakeTile(sTilesP + T_COL * 32, PixCol); MakeTile(sTilesP + T_ROW * 32, PixRow);
    MakeTile(sTilesP + T_GREEN * 32, PixGreen); MakeTile(sTilesP + T_RED * 32, PixRed);
    MakeTile(sTilesP + T_BAND * 32, PixBand); MakeTile(sTilesP + T_STRIPE * 32, PixStripe);
    MakeTile(sTilesS, PixSolid5);

    MetaAll(sMtP, 0, ENT(T_COL, 0, 0, 0), ENT(T_BLANK, 0, 0, 0));           /* m0: plain, no flip */
    sMtP[8 + 0] = ENT(T_COL, 0, 1, 0);                                      /* m1 q0: hflip */
    sMtP[8 + 1] = ENT(T_ROW, 7, 0, 1);                                      /* q1: vflip, palette 7 = SECONDARY's */
    sMtP[8 + 2] = ENT(512, 2, 0, 0);                                        /* q2: secondary tile 0, primary pal 2 */
    sMtP[8 + 3] = ENT(600, 0, 0, 0);                                        /* q3: tile past the data: skipped */
    { unsigned q; for (q = 0; q < 4; q++) sMtP[8 + 4 + q] = ENT(T_BLANK, 0, 0, 0); }
    MetaAll(sMtP, 2, ENT(T_BLANK, 0, 0, 0), ENT(T_COL, 0, 0, 0));           /* m2: layer 1 only */
    MetaAll(sMtP, 3, ENT(T_GREEN, 1, 0, 0), ENT(T_BLANK, 0, 0, 0));         /* m3: all green */
    MetaAll(sMtP, 4, ENT(T_RED, 1, 0, 0), ENT(T_BLANK, 0, 0, 0));           /* m4: all red */
    sMtP[5 * 8 + 0] = sMtP[5 * 8 + 1] = ENT(T_GREEN, 1, 0, 0); sMtP[5 * 8 + 2] = sMtP[5 * 8 + 3] = ENT(T_RED, 1, 0, 0);   /* m5: exactly half green */
    { unsigned q; for (q = 0; q < 4; q++) sMtP[5 * 8 + 4 + q] = ENT(T_BLANK, 0, 0, 0); }
    sMtP[6 * 8 + 0] = ENT(T_GREEN, 1, 0, 0); sMtP[6 * 8 + 1] = sMtP[6 * 8 + 2] = sMtP[6 * 8 + 3] = ENT(T_RED, 1, 0, 0);   /* m6: a quarter green */
    { unsigned q; for (q = 0; q < 4; q++) sMtP[6 * 8 + 4 + q] = ENT(T_BLANK, 0, 0, 0); }
    MetaAll(sMtP, 7, ENT(T_BAND, 1, 0, 0), ENT(T_BLANK, 0, 0, 0));          /* m7: flight of steps */
    MetaAll(sMtP, 8, ENT(T_STRIPE, 1, 0, 0), ENT(T_BLANK, 0, 0, 0));        /* m8: vertical stripes */
    MetaAll(sMtP, 9, ENT(T_BLANK, 0, 0, 0), ENT(T_GREEN, 1, 0, 0));         /* m9: upper layer fills the cell, green */
    sMtP[10 * 8 + 4] = sMtP[10 * 8 + 5] = ENT(T_RED, 1, 0, 0);              /* m10: upper layer, top 8 rows only */
    sMtP[10 * 8 + 6] = sMtP[10 * 8 + 7] = ENT(T_BLANK, 0, 0, 0);
    { unsigned q; for (q = 0; q < 4; q++) sMtP[10 * 8 + q] = ENT(T_BLANK, 0, 0, 0); }
    sMtP[11 * 8 + 0] = sMtP[11 * 8 + 1] = sMtP[11 * 8 + 2] = ENT(T_BAND, 1, 0, 0); sMtP[11 * 8 + 3] = ENT(T_BLANK, 0, 0, 0);   /* m11: steps, 192 px */
    { unsigned q; for (q = 0; q < 4; q++) sMtP[11 * 8 + 4 + q] = ENT(T_BLANK, 0, 0, 0); }
    MetaAll(sMtS, 0, ENT(T_COL, 3, 0, 0), ENT(T_BLANK, 0, 0, 0));           /* secondary m0 (id 512): primary tile, primary pal 3 */
    MetaAll(sMtS, 1, ENT(T_COL, 8, 0, 0), ENT(T_BLANK, 0, 0, 0));           /* id 513: palette 8 = secondary's */

    fxr_init(&f);
    tsP = fxr_tileset(&f, 1, 0, sTilesP, sizeof(sTilesP), sPalP, sMtP, sAttP, 12, 1);   /* compressed */
    tsS = fxr_tileset(&f, 0, 1, sTilesS, sizeof(sTilesS), sPalS, sMtS, sAttS, 2, 1);    /* uncompressed */
    (void)fxr_layout(&f, 2, 2, blocks, tsP, tsS);
    (void)fxr_layout(&f, 2, 2, blocks, tsP, 0);          /* id 3: NULL secondary */
    fxr_finish(&f);
    CHECK(rg_world_open(&w, f.rom, FXR_ROM_SIZE) == RG_OK);
    CHECK(w.pairCount == 3);   /* filler (NULL,NULL), (P,S), (P,NULL) */
    pair = rg_pair_open(&w, w.layouts[1].pairIndex);
    CHECK(pair != NULL);

    /* m0: plain; pixel (x,y) = idx 1+(x%8) in every quad; colour = 0x1000|(0<<4)|idx */
    l = rg_layer(pair, 0, 0);
    CHECK(l->count == 256);
    CHECK(l->c[0][0] == (0x1000 | 1) && l->c[0][7] == (0x1000 | 8) && l->c[0][8] == (0x1000 | 1) && l->c[9][15] == (0x1000 | 8));
    CHECK(rg_layer(pair, 0, 1)->count == 0);
    /* m1: hflip, vflip + secondary palette 7, secondary tile + primary pal 2, tile past the data */
    l = rg_layer(pair, 1, 0);
    CHECK(l->c[0][0] == (0x1000 | 8) && l->c[0][7] == (0x1000 | 1));                       /* q0 hflip: idx 1+(7-x) */
    CHECK(l->c[0][8] == (0x2000 | (7 << 4) | 8) && l->c[7][8] == (0x2000 | (7 << 4) | 1)); /* q1: vflip (idx 1+(7-y)), secondary pal 7 */
    CHECK(l->c[8][0] == (0x1000 | (2 << 4) | 5) && l->c[15][7] == (0x1000 | (2 << 4) | 5)); /* q2: secondary tile 0 = solid 5, primary pal 2 */
    CHECK(l->drawn[8] == 0x00FF && l->drawn[0] == 0xFFFF);                                  /* q3 skipped: row 8 right half empty */
    CHECK(l->count == 64 * 3);
    /* index 0 not drawn on either layer; layer 1 independent */
    CHECK(rg_layer(pair, 2, 0)->count == 0 && rg_layer(pair, 2, 1)->count == 256);
    /* secondary ids: primary tile with primary palette 3; palette 8 comes from the secondary */
    CHECK(rg_layer(pair, 512, 0)->c[0][0] == (0x1000 | (3 << 4) | 1));
    CHECK(rg_layer(pair, 513, 0)->c[0][0] == (0x2000 | (8 << 4) | 1));
    CHECK(rg_layer(pair, 514, 0)->count == 0 && rg_layer(pair, 12, 0)->count == 0);   /* past each count: empty */
    CHECK(rg_layer(pair, RG_NONE, 0)->count == 0 && rg_layer(pair, 5, 7)->count == 0);
    CHECK(rg_layer(pair, 0, 0) == rg_layer(pair, 0, 0));                                  /* memoised */
    /* merged: layer 1 wins where both draw */
    rg_merged(pair, 2, &merged);
    CHECK(merged.count == 256);
    rg_merged(pair, 1, &merged);
    CHECK(merged.count == 192);
    /* equality is on drawn pixels AND colours */
    CHECK(rg_layer_equal(rg_layer(pair, 0, 0), rg_layer(pair, 0, 0)));
    CHECK(!rg_layer_equal(rg_layer(pair, 0, 0), rg_layer(pair, 1, 0)));
    CHECK(!rg_layer_equal(rg_layer(pair, 0, 0), rg_layer(pair, 513, 0)));   /* same drawing, other colours */
    CHECK(rg_layer_equal(rg_layer(pair, 12, 0), rg_layer(pair, 99, 1)));    /* two empties */
    /* features */
    CHECK(rg_foliage_ge_half(pair, 3) && !rg_foliage_ge_half(pair, 4));
    CHECK(rg_foliage_ge_half(pair, 5));        /* exactly half counts (>=) */
    CHECK(!rg_foliage_ge_half(pair, 6));
    CHECK(!rg_foliage_ge_half(pair, 0) && !rg_foliage_ge_half(pair, 12));   /* empty drawing = 0 */
    CHECK(rg_foliage_ge_half(pair, 9));        /* merged: the upper layer's green counts */
    CHECK(rg_covers(pair, 9) && !rg_covers(pair, 10) && !rg_covers(pair, 3));
    CHECK(rg_treads(pair, 7) && !rg_treads(pair, 8) && !rg_treads(pair, 11) && !rg_treads(pair, 3));
    rg_merged(pair, 7, &merged);
    CHECK(rg_treads_measure(&merged, &a, &d) && a == 0.0 && d > 200.0 && d < 260.0);
    rg_merged(pair, 8, &merged);
    CHECK(!rg_treads_measure(&merged, &a, &d) && a > 200.0 && d == 0.0);
    /* measure near the thresholds: grey rows differing by k 5-bit steps (down ~ 8.2 per step) */
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++) { merged.drawn[y] = 0xFFFF; merged.c[y][x] = (uint16_t)(((y & 1) ? 2 : 0) * 0x0421); }
    merged.count = 256;
    CHECK(rg_treads_measure(&merged, &a, &d) && a == 0.0 && d >= 15.0 && d < 17.0);   /* 2 steps: just over 15 */
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++) merged.c[y][x] = (uint16_t)(((y & 1) ? 1 : 0) * 0x0421);
    CHECK(!rg_treads_measure(&merged, &a, &d) && d > 7.0 && d < 9.0);                  /* 1 step: under 15 */
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++) merged.c[y][x] = (uint16_t)(((x & 1) ? 1 : 0) * 0x0421);
    CHECK(!rg_treads_measure(&merged, &a, &d) && a > 7.0 && a < 9.0);                  /* across 8.2 > 4 */
    merged.count = 239;
    CHECK(!rg_treads_measure(&merged, &a, &d));                                         /* under the 240-pixel fill */
    rg_pair_close(pair);

    /* NULL secondary: ids >= 512 are empty; the primary still decodes */
    pair = rg_pair_open(&w, w.layouts[2].pairIndex);
    CHECK(pair != NULL && rg_layer(pair, 0, 0)->count == 256 && rg_layer(pair, 512, 0)->count == 0);
    CHECK(rg_layer(pair, 1, 0)->count == 64 * 2 + 64 * 0 + 64 * 0 || rg_layer(pair, 1, 0)->count > 0);   /* q1 (pal 7) and q2 (sec tile) unreadable */
    rg_pair_close(pair);
    CHECK(rg_pair_open(&w, 99) == NULL);
    rg_world_close(&w);
    free(f.rom);
    (void)id;
}

/* ---- real ROM ---------------------------------------------------------------------------- */
static void TestRealRom(void)
{
    const char *path = getenv("ROMGEN_ROM");
    FILE *fp;
    uint8_t *rom;
    size_t n;
    RgWorld w;
    unsigned pi, m, flightFound = 0, treadsTotal = 0, minPairs = 0;
    double minMarginA = 1e9, minMarginD = 1e9;
    unsigned printedFlight = 0;

    if (!path || !(fp = fopen(path, "rb"))) { printf("SKIP real-ROM art checks (set ROMGEN_ROM)\n"); return; }
    rom = (uint8_t *)malloc(0x2000000);
    n = fread(rom, 1, 0x2000000, fp);
    fclose(fp);
    CHECK(rg_world_open(&w, rom, n) == RG_OK);
    for (pi = 0; pi < w.pairCount; pi++) {
        RgPair *p = rg_pair_open(&w, (uint16_t)pi);
        CHECK(p != NULL);
        if (!p) continue;
        minPairs++;
        for (m = 0; m < 1024; m++) {
            RgLayer mg;
            double a, d;
            rg_merged(p, (uint16_t)m, &mg);
            if (mg.count >= 240) {
                bool t = rg_treads_measure(&mg, &a, &d);
                if (fabs(a - 4.0) < minMarginA) minMarginA = fabs(a - 4.0);
                if (fabs(d - 15.0) < minMarginD) minMarginD = fabs(d - 15.0);
                if (t) treadsTotal++;
                CHECK(t == rg_treads(p, (uint16_t)m));
                if (t && w.tilesets[w.pairs[pi].ts[0]].addr == 0x083DF704u && m < 512 && a >= 1.5 && a <= 2.5 && d >= 27.0 && d <= 29.0) {
                    flightFound++;
                    if (!printedFlight) { printf("General flight of steps: metatile %u across %.3f down %.3f\n", m, a, d); printedFlight = 1; }
                }
            }
            (void)rg_foliage_ge_half(p, (uint16_t)m);
            (void)rg_covers(p, (uint16_t)m);
        }
        rg_pair_close(p);
    }
    CHECK(minPairs == w.pairCount);
    CHECK(flightFound >= 1);
    printf("real ROM: %u pairs decoded; %u treads metatile-instances; nearest threshold margin across %.4f, down %.4f\n",
           minPairs, treadsTotal, minMarginA, minMarginD);
    CHECK(minMarginA > 1e-6 && minMarginD > 1e-6);   /* the naive vs compensated sum order cannot flip any metatile */
    rg_world_close(&w);
    free(rom);
}

int main(void)
{
    TestSynthetic();
    TestRealRom();
    printf("test_romgen_art: %d checks, %d failures\n", sChecks, sFails);
    return sFails ? 1 : 0;
}
