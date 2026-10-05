// test_romgen_bimg.c -- host test for the S2.1 additions: rg_art.c (rg_cell_px, rg_subtile_px), rg_world.c
// (rg_metatile_entries) and rg_bimg.c (images, rg_cell_image, rg_building_art, the colour constants).
// phase 33 S2.1, SPEC-S2 sections 1.1, 1.2, 6.1. Synthetic mini-ROM always; the real ROM
// (ROMGEN_ROM=/path/emerald.gba) adds Littleroot's house art.
//
//   clang -std=c11 -Wall -Wextra -O2 -ffp-contract=off -fsanitize=address,undefined \
//         -I source/romgen -I source/voxel -I test/host test/host/test_romgen_bimg.c \
//         source/romgen/rg_world.c source/romgen/rg_art.c source/romgen/rg_bimg.c source/voxel/vx_lz77.c \
//         -lm -o /tmp/trgi && /tmp/trgi
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rg_bimg.h"
#include "rg_fixture.h"

static int sChecks, sFails, sSkips;
#define CHECK(c) do { ++sChecks; if (!(c)) { ++sFails; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

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
static unsigned PixCol(unsigned x, unsigned y) { (void)y; return x & 1u ? 0 : 1 + (x >> 1); }   /* idx 0 on odd columns */
static unsigned PixSolid(unsigned x, unsigned y) { (void)x; (void)y; return 5; }
static unsigned PixOne(unsigned x, unsigned y) { (void)x; (void)y; return 1; }
static unsigned PixTwo(unsigned x, unsigned y) { (void)x; (void)y; return 2; }
enum { T_BLANK, T_COL, T_SOLID, T_ONE, T_TWO, T_COUNT };
#define ENT(tile, pal, hf, vf) (uint16_t)((tile) | ((hf) ? 0x400 : 0) | ((vf) ? 0x800 : 0) | ((pal) << 12))

static uint16_t sPalP[256], sPalS[256], sMtP[16 * 8], sAttP[16], sMtS[8], sAttS[1];
static uint8_t sTilesP[T_COUNT * 32], sTilesS[32];

static void MetaAll(uint16_t *mt, unsigned m, uint16_t l0, uint16_t l1)
{
    unsigned q;
    for (q = 0; q < 4; q++) { mt[m * 8 + q] = l0; mt[m * 8 + 4 + q] = l1; }
}

static const uint8_t *Px(const RgImage *im, int x, int y) { return im->px + 4 * ((size_t)y * (size_t)im->w + (size_t)x); }

static unsigned Hex2(const char *s)
{
    char t[3];
    t[0] = s[0]; t[1] = s[1]; t[2] = 0;
    return (unsigned)strtoul(t, NULL, 16);
}

static void TestImages(void)
{
    RgImage a, b, c;
    int bb[4];
    unsigned c5;

    CHECK(!rg_img_new(&a, 0, 4) && !rg_img_new(&a, 4, 5000));
    CHECK(rg_img_new(&a, 8, 6) && a.px[3] == 0 && a.px[4 * 47 + 3] == 0);
    CHECK(!rg_img_bbox(&a, bb));                                   /* nothing opaque: None */
    a.px[4 * (2 * 8 + 3)] = 9; a.px[4 * (2 * 8 + 3) + 3] = 255;
    a.px[4 * (4 * 8 + 6)] = 7; a.px[4 * (4 * 8 + 6) + 3] = 255;
    CHECK(rg_img_bbox(&a, bb) && bb[0] == 3 && bb[1] == 2 && bb[2] == 7 && bb[3] == 5);   /* half-open */
    CHECK(rg_img_crop(&a, bb, &b) && b.w == 4 && b.h == 3 && Px(&b, 0, 0)[0] == 9 && Px(&b, 3, 2)[0] == 7);
    { int big[4] = {0, 0, 9, 6}; CHECK(!rg_img_crop(&a, big, &c)); }
    CHECK(rg_img_new(&c, 4, 3));
    CHECK(rg_img_hash(&b) != rg_img_hash(&c) && !rg_img_equal(&b, &c));
    rg_img_paste(&c, &b, 0, 0);
    CHECK(rg_img_equal(&b, &c) && rg_img_hash(&b) == rg_img_hash(&c));
    rg_img_paste(&a, &b, 6, 4);                                    /* clipped to the right and bottom edges */
    CHECK(Px(&a, 6, 4)[0] == 9 && Px(&a, 7, 5)[3] == 0);
    rg_img_paste(&a, &b, -2, -1);                                  /* clipped to the left and top edges */
    CHECK(Px(&a, 0, 0)[0] == Px(&b, 2, 1)[0]);
    rg_img_free(&a); rg_img_free(&b); rg_img_free(&c);
    CHECK(a.px == NULL && a.w == 0);

    /* G4: c5 -> 8 bit is injective and (c8 >> 3) == c5 (the texel packing relies on it) */
    for (c5 = 0; c5 < 32; c5++) {
        CHECK((unsigned)(rg_c5_to_8(c5) >> 3) == c5);
        CHECK(c5 == 0 || rg_c5_to_8(c5) > rg_c5_to_8(c5 - 1));
    }
    CHECK(rg_c5_to_8(0) == 0 && rg_c5_to_8(31) == 255 && rg_c5_to_8(16) == 131);
    {   /* every colour constant of voxel_building_specs.py has a 555 preimage; one that has none is rejected */
        static const char *const K[] = {
            "f6b473", "de7b31", "ffe6b4", "cdc58b", "eedea4", "ffffc5", "e6e6b4", "de9c62", "ffffff", "8bd5de", "d5ded5",
            "b4b4a4", "83b4b4", "d5d5b4", "b4a44a", "947329", "ac8b39", "cdc55a", "6a94c5", "8bbdf6", "838394", "d57bac",
            "629c8b", "ded552", "bdb431", "8b8b8b", "9c9410", "ffcd8b", "f6f6a4", "fff683", "bdac52", "949494", "bdbdac" };
        unsigned i, ok = 0;
        uint16_t v;
        for (i = 0; i < sizeof(K) / sizeof(K[0]); i++) {
            unsigned r8, g8, b8;
            ok += rg_hex_to_c5(K[i], &v);
            CHECK(rg_hex_to_c5(K[i], &v));
            r8 = rg_c5_to_8(v & 31u); g8 = rg_c5_to_8((v >> 5) & 31u); b8 = rg_c5_to_8((v >> 10) & 31u);
            CHECK(r8 == Hex2(K[i]) && g8 == Hex2(K[i] + 2) && b8 == Hex2(K[i] + 4));
        }
        CHECK(ok == sizeof(K) / sizeof(K[0]));
        CHECK(!rg_hex_to_c5("f6b474", &v) && !rg_hex_to_c5("f6b47", &v) && !rg_hex_to_c5("f6b4733", &v) && !rg_hex_to_c5("xx0000", &v));
    }
}

static void TestSynthetic(void)
{
    RgFx f;
    RgWorld w;
    uint32_t tsP, tsS;
    unsigned p, i, x, y, m;
    RgPair *pair;
    RgCellPx cp;
    uint16_t sc[64];
    uint8_t si[64];
    /* layout 2x2: metatiles 0 (ground), 1 (building with ground painted in), 2, 3 */
    uint16_t blocks[4] = {0, 1, 2, 3};
    uint16_t ent[8];

    for (p = 0; p < 16; p++)
        for (i = 0; i < 16; i++) { sPalP[p * 16 + i] = (uint16_t)(0x1000 | (p << 4) | i); sPalS[p * 16 + i] = (uint16_t)(0x2000 | (p << 4) | i); }
    MakeTile(sTilesP + T_BLANK * 32, PixBlank); MakeTile(sTilesP + T_COL * 32, PixCol); MakeTile(sTilesP + T_SOLID * 32, PixSolid);
    MakeTile(sTilesP + T_ONE * 32, PixOne); MakeTile(sTilesP + T_TWO * 32, PixTwo);
    MakeTile(sTilesS, PixSolid);
    MetaAll(sMtP, 0, ENT(T_BLANK, 1, 0, 0), ENT(T_BLANK, 0, 0, 0));          /* m0: blank lower, palette 1: all palette-1 slot 0 */
    sMtP[8 + 0] = ENT(T_COL, 2, 0, 0);                                       /* m1 q0: idx 0 on odd columns, palette 2 */
    sMtP[8 + 1] = ENT(T_COL, 2, 1, 0);                                       /* q1: hflip */
    sMtP[8 + 2] = ENT(T_SOLID, 7, 0, 0);                                     /* q2: palette 7 = the SECONDARY's */
    sMtP[8 + 3] = ENT(600, 3, 0, 0);                                         /* q3: tile past the data: magenta */
    sMtP[8 + 4] = ENT(T_ONE, 1, 0, 0); sMtP[8 + 5] = ENT(T_BLANK, 0, 0, 0);  /* upper: q0 colour, q1 transparent, q2 colour, q3 past */
    sMtP[8 + 6] = ENT(T_TWO, 1, 0, 1); sMtP[8 + 7] = ENT(601, 0, 0, 0);
    MetaAll(sMtP, 2, ENT(T_ONE, 1, 0, 0), ENT(T_BLANK, 0, 0, 0));            /* m2: opaque solid colour of palette 1 idx 1 */
    MetaAll(sMtP, 3, ENT(T_COL, 1, 0, 0), ENT(T_BLANK, 0, 0, 0));
    MetaAll(sMtS, 0, ENT(T_SOLID, 8, 0, 0), ENT(T_BLANK, 0, 0, 0));          /* id 512: secondary palette 8 */
    fxr_init(&f);
    tsP = fxr_tileset(&f, 1, 0, sTilesP, sizeof(sTilesP), sPalP, sMtP, sAttP, 4, 1);
    tsS = fxr_tileset(&f, 1, 1, sTilesS, sizeof(sTilesS), sPalS, sMtS, sAttS, 1, 1);
    (void)fxr_layout(&f, 2, 2, blocks, tsP, tsS);
    fxr_finish(&f);
    CHECK(rg_world_open(&w, f.rom, FXR_ROM_SIZE) == RG_OK);
    pair = rg_pair_open(&w, w.layouts[1].pairIndex);
    CHECK(pair != NULL);

    /* rg_cell_px equals rg_layer wherever rg_layer draws; lower layer also draws idx 0 with palette slot 0 */
    for (m = 0; m < 4; m++) {
        unsigned layer;
        for (layer = 0; layer < 2; layer++) {
            const RgLayer *l = rg_layer(pair, (uint16_t)m, (int)layer);
            rg_cell_px(pair, (uint16_t)m, (int)layer, &cp);
            for (y = 0; y < 16; y++)
                for (x = 0; x < 16; x++) {
                    if (rg_layer_has(l, (int)x, (int)y)) {
                        CHECK(((cp.drawn[y] >> x) & 1u) && cp.c[y][x] == l->c[y][x] && cp.idx[y][x] != 0);
                    } else if (layer == 1) {
                        CHECK(!((cp.drawn[y] >> x) & 1u) && cp.idx[y][x] == 0);
                    }
                }
        }
    }
    rg_cell_px(pair, 0, 0, &cp);                                              /* m0: blank, palette 1: slot 0 colour everywhere */
    for (y = 0; y < 16; y++) {
        CHECK(cp.drawn[y] == 0xFFFFu);
        for (x = 0; x < 16; x++) CHECK(cp.c[y][x] == (0x1000 | (1 << 4)) && cp.idx[y][x] == 0);
    }
    rg_cell_px(pair, 1, 0, &cp);                                              /* m1 lower */
    CHECK(cp.idx[0][1] == 0 && cp.c[0][1] == (0x1000 | (2 << 4) | 0) && ((cp.drawn[0] >> 1) & 1u));   /* idx 0 -> slot 0 of palette 2, opaque */
    CHECK(cp.idx[0][0] == 1 && cp.c[0][0] == (0x1000 | (2 << 4) | 1));
    CHECK(cp.idx[0][8 + 7] == 1 && cp.idx[0][8 + 6] == 0);                    /* q1 hflip */
    CHECK(cp.c[8][0] == (0x2000 | (7 << 4) | 5) && cp.idx[8][0] == 5);        /* q2: palette 7 from the secondary */
    for (y = 8; y < 16; y++)                                                   /* q3: past the tile data: magenta idx 0, drawn */
        for (x = 8; x < 16; x++) CHECK(cp.c[y][x] == 0x7C1F && cp.idx[y][x] == 0 && ((cp.drawn[y] >> x) & 1u));
    rg_cell_px(pair, 1, 1, &cp);                                              /* m1 upper: q3 past the data is NOT drawn */
    CHECK(((cp.drawn[0]) & 0x00FFu) == 0x00FFu && (cp.drawn[0] & 0xFF00u) == 0);
    CHECK((cp.drawn[8] & 0xFF00u) == 0 && (cp.drawn[8] & 0x00FFu) == 0x00FFu);
    CHECK(cp.c[8][0] == (0x1000 | (1 << 4) | 2));                             /* q2 upper: vflip solid 2, palette 1 */
    rg_cell_px(pair, 512, 0, &cp);                                            /* secondary id: palette 8 from the secondary */
    CHECK(cp.c[3][3] == (0x2000 | (8 << 4) | 5));
    rg_cell_px(pair, 513, 0, &cp);                                            /* past the secondary's count: all magenta */
    CHECK(cp.drawn[0] == 0xFFFFu && cp.c[5][5] == 0x7C1F && cp.idx[5][5] == 0);
    rg_cell_px(pair, 513, 1, &cp);
    CHECK(cp.drawn[0] == 0 && cp.drawn[15] == 0);
    rg_cell_px(pair, 0, 7, &cp);
    CHECK(cp.drawn[0] == 0);

    /* G2: a subtile by (tile, palette) */
    rg_subtile_px(pair, T_COL, 2, sc, si);
    CHECK(si[0] == 1 && si[1] == 0 && sc[1] == (0x1000 | (2 << 4)) && sc[2] == (0x1000 | (2 << 4) | 2));
    rg_subtile_px(pair, T_SOLID, 7, sc, si);                                   /* palette 7 -> secondary */
    CHECK(sc[0] == (0x2000 | (7 << 4) | 5) && si[63] == 5);
    rg_subtile_px(pair, 512, 0, sc, si);                                       /* secondary tile 0 */
    CHECK(si[10] == 5 && sc[10] == (0x1000 | 5));
    rg_subtile_px(pair, 700, 0, sc, si);                                       /* past the data */
    for (i = 0; i < 64; i++) CHECK(sc[i] == 0x7C1F && si[i] == 0);

    /* G3: raw entries, primary and secondary split, out of range */
    CHECK(rg_metatile_entries(&w.layouts[1], 1, ent) && ent[0] == ENT(T_COL, 2, 0, 0) && ent[3] == ENT(600, 3, 0, 0) && ent[4] == ENT(T_ONE, 1, 0, 0));
    CHECK(rg_metatile_entries(&w.layouts[1], 512, ent) && ent[0] == ENT(T_SOLID, 8, 0, 0));
    CHECK(!rg_metatile_entries(&w.layouts[1], 513, ent) && !rg_metatile_entries(&w.layouts[1], 4, ent));
    CHECK(!rg_metatile_entries(&w.layouts[1], RG_NONE, ent));

    /* rg_cell_image: opaque, upper where idx != 0 else lower */
    {
        RgImage im;
        CHECK(rg_cell_image(pair, 1, &im) && im.w == 16 && im.h == 16);
        for (y = 0; y < 16; y++) for (x = 0; x < 16; x++) CHECK(Px(&im, (int)x, (int)y)[3] == 255);
        CHECK(Px(&im, 0, 0)[0] == rg_c5_to_8((0x1000 | (1 << 4) | 1) & 31u));    /* upper q0 idx 1 palette 1 wins over the lower */
        rg_img_free(&im);
    }

    /* rg_building_art: ground = metatile 0 (blank, palette 1 slot 0). Cell (1,0) = m1 has ground painted in beside the building. */
    {
        RgImage art;
        uint16_t ground[1] = {0};
        static const uint8_t own[4] = {0, 1, 0, 0};
        unsigned opaque = 0;

        CHECK(rg_building_art(&w, pair, &w.layouts[1], 0, 0, 2, 2, ground, 1, NULL, false, &art));
        CHECK(art.w == 32 && art.h == 32);
        for (y = 0; y < 32; y++) for (x = 0; x < 32; x++) {
            const uint8_t *q = Px(&art, (int)x, (int)y);
            CHECK(q[3] == 0 || q[3] == 255);
            if (q[3] == 0) CHECK(q[0] == 0 && q[1] == 0 && q[2] == 0);
            opaque += q[3] != 0;
        }
        CHECK(Px(&art, 0, 0)[3] == 0 && Px(&art, 15, 15)[3] == 0);              /* cell (0,0) is ground whole */
        CHECK(Px(&art, 16, 0)[3] == 255 && Px(&art, 17, 0)[3] == 255);           /* cell (1,0): m1 q0 upper colour */
        CHECK(Px(&art, 24, 0)[3] == 255);                                        /* m1 q1: a lower block that is not ground is drawn whole, idx 0 included */
        CHECK(Px(&art, 0, 16)[3] == 255 && Px(&art, 16, 16)[3] == 255);          /* cells (0,1), (1,1): not ground */
        (void)opaque;
        rg_img_free(&art);
        CHECK(rg_building_art(&w, pair, &w.layouts[1], 0, 0, 2, 2, ground, 1, own, false, &art));   /* only cell (1,0) drawn */
        CHECK(Px(&art, 0, 16)[3] == 0 && Px(&art, 3, 3)[3] == 0);
        rg_img_free(&art);
        CHECK(rg_building_art(&w, pair, &w.layouts[1], 0, 0, 2, 2, ground, 1, NULL, true, &art));    /* upper layer only */
        CHECK(Px(&art, 16, 0)[3] == 255 && Px(&art, 16, 16)[3] == 0 && Px(&art, 0, 16)[3] == 0);
        rg_img_free(&art);
        CHECK(!rg_building_art(&w, pair, &w.layouts[1], 1, 1, 2, 2, ground, 1, NULL, false, &art));  /* runs off the map */
        CHECK(art.px == NULL);
    }
    rg_pair_close(pair);
    rg_world_close(&w);
    free(f.rom);
}

static const RgLayout *Layout(const RgWorld *w, unsigned id) { return &w->layouts[id - 1]; }

static void TestRealRom(void)
{
    const char *path = getenv("ROMGEN_ROM");
    FILE *fp;
    long n;
    uint8_t *rom;
    RgWorld w;
    RgPair *pair;
    RgImage art, cell;
    uint16_t ground[1] = {0x001};
    int x, y, opaque = 0, bb[4];
    const RgLayout *L;

    if (!path || !(fp = fopen(path, "rb"))) { sSkips++; printf("SKIP real-ROM building art (set ROMGEN_ROM)\n"); return; }
    fseek(fp, 0, SEEK_END); n = ftell(fp); fseek(fp, 0, SEEK_SET);
    rom = (uint8_t *)malloc((size_t)n);
    CHECK(rom && fread(rom, 1, (size_t)n, fp) == (size_t)n);
    fclose(fp);
    CHECK(rg_world_open(&w, rom, (size_t)n) == RG_OK);
    L = Layout(&w, 10);                                         /* LAYOUT_LITTLEROOT_TOWN */
    pair = rg_pair_open(&w, L->pairIndex);
    CHECK(pair != NULL);
    CHECK(L->w == 20 && L->h == 20);
    CHECK(rg_building_art(&w, pair, L, 2, 4, 5, 5, ground, 1, NULL, false, &art));
    CHECK(art.w == 80 && art.h == 80);
    for (y = 0; y < 80; y++)
        for (x = 0; x < 80; x++) {
            const uint8_t *q = Px(&art, x, y);
            CHECK(q[3] == 0 || q[3] == 255);
            if (q[3] == 0) CHECK(q[0] == 0 && q[1] == 0 && q[2] == 0);
            opaque += q[3] != 0;
        }
    CHECK(opaque > 3000 && opaque < 6400);
    CHECK(rg_img_bbox(&art, bb));
    printf("real ROM: Littleroot house art 80x80, %d opaque pixels, bbox %d,%d..%d,%d\n", opaque, bb[0], bb[1], bb[2], bb[3]);
    /* the east house (13,4) is the same building: same bbox, and the ground metatile 0x001 is ground whole */
    rg_img_free(&art);
    CHECK(rg_cell_image(pair, 0x001, &cell));
    rg_img_free(&cell);
    CHECK(rg_building_art(&w, pair, L, 13, 4, 5, 5, ground, 1, NULL, false, &art));
    { int bb2[4]; CHECK(rg_img_bbox(&art, bb2) && bb2[0] == bb[0] && bb2[1] == bb[1] && bb2[2] == bb[2] && bb2[3] == bb[3]); }
    rg_img_free(&art);
    /* a single grass cell has no building pixels at all */
    CHECK(rg_building_art(&w, pair, L, 0, 0, 1, 1, ground, 1, NULL, false, &art));
    CHECK(rg_metatile(L, 0, 0) == 0x001 ? !rg_img_bbox(&art, bb) : true);
    rg_img_free(&art);
    rg_pair_close(pair);
    rg_world_close(&w);
    free(rom);
}

int main(void)
{
    TestImages();
    TestSynthetic();
    TestRealRom();
    printf("test_romgen_bimg: %d checks, %d failures, %d skipped\n", sChecks, sFails, sSkips);
    return sFails ? 1 : 0;
}
