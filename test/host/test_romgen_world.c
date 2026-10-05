// test_romgen_world.c -- host test for source/romgen/rg_world.c (phase 33 S0.1, SPEC-S0-S1 sections 1.1-1.5, 7.1).
// Synthetic mini-ROM checks always run; the real-ROM pins run when ROMGEN_ROM=/path/emerald.gba is set
// (otherwise they print SKIP and still pass).
//
//   clang -std=c11 -Wall -Wextra -O2 -ffp-contract=off -fsanitize=address,undefined \
//         -I source/romgen -I source/voxel -I test/host test/host/test_romgen_world.c \
//         source/romgen/rg_world.c source/voxel/vx_lz77.c -o /tmp/trgw && /tmp/trgw
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rg_world.h"
#include "rg_fixture.h"

static int sChecks, sFails;
#define CHECK(c) do { ++sChecks; if (!(c)) { ++sFails; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static uint8_t sZeroTiles[512 * 32];
static uint16_t sMt[512 * 8], sAttr[512];

static uint32_t Ts(RgFx *f, int comp, int sec, unsigned count, int adj)
{
    return fxr_tileset(f, comp, sec, sZeroTiles, 64, NULL, sMt, sAttr, count, adj);
}

static void TestSynthetic(void)
{
    RgFx f;
    RgWorld w;
    uint32_t tsA, tsB, tsC;
    unsigned L2, L3, L4, L5;
    const int16_t wa[2][2] = {{5, 3}, {1, 7}};
    const int16_t wb[2][2] = {{5, 3}, {2, 2}};
    const uint16_t sa[3][3] = {{4, 4, 0}, {6, 6, 7}, {9, 9, 4}};
    const uint16_t sb[3][3] = {{4, 4, 0}, {8, 8, 5}, {3, 3, 3}};
    uint16_t blocks[20 * 20];
    uint16_t blocksB[20 * 20];
    int i;
    uint32_t h1;
    const int32_t conns[4][4] = {{2, 5, 0, 1}, {5, 0, 3, 3}, {4, -7, 0, 0}, {6, 0, 1, 1}};
    RgConn out[8];

    for (i = 0; i < 512 * 8; i++) sMt[i] = (uint16_t)i;
    for (i = 0; i < 512; i++) sAttr[i] = (uint16_t)(0x1000 + i);

    fxr_init(&f);
    tsA = Ts(&f, 1, 0, 8, 1);      /* compressed, 8 metatiles, adjacent */
    tsB = Ts(&f, 0, 1, 40, 0);     /* uncompressed, gap -> 512 fallback */
    tsC = Ts(&f, 0, 1, 16, 1);
    for (i = 0; i < 400; i++) blocks[i] = (uint16_t)((i % 8) | (((i % 5) == 0) << 10) | ((i % 16) << 12));
    L2 = fxr_layout(&f, 20, 20, blocks, tsA, tsB);       /* id 2 */
    L3 = fxr_layout_null(&f);                            /* id 3: NULL entry */
    L4 = fxr_layout(&f, 20, 20, blocks, tsA, 0);         /* id 4: NULL secondary */
    memcpy(blocksB, blocks, sizeof(blocks));
    for (i = 0; i < 100; i++) blocksB[i] ^= 1;           /* 75% equal */
    L5 = fxr_layout(&f, 20, 20, blocksB, tsA, tsB);      /* id 5: unused, alt of 2 */
    CHECK(L2 == 2 && L3 == 3 && L4 == 4 && L5 == 5);
    (void)fxr_layout(&f, 20, 20, blocksB, tsA, tsC);     /* id 6: unused, tileset differs -> no alt */
    (void)fxr_layout(&f, 19, 20, blocks, tsA, tsB);      /* id 7: unused, size differs -> no alt */
    {
        uint16_t half[400];
        for (i = 0; i < 400; i++) half[i] = (uint16_t)(blocks[i] ^ (i < 201 ? 1 : 0));   /* 199/400 equal */
        (void)fxr_layout(&f, 20, 20, half, tsA, tsB);    /* id 8: just under 0.5 -> no alt */
    }
    (void)fxr_map(&f, 0, MAP_TYPE_ROUTE, 2, wa, 2, sa, 3);          /* group 0 num 0 */
    h1 = fxr_map(&f, 0, MAP_TYPE_INDOOR, 2, wb, 2, sb, 3);          /* same layout, indoor: union */
    fxr_conns(&f, h1, conns, 4);
    (void)fxr_map(&f, 1, MAP_TYPE_INDOOR, 4, NULL, 0, NULL, 0);
    fxr_finish(&f);

    CHECK(rg_world_open(&w, f.rom, FXR_ROM_SIZE) == RG_OK);
    CHECK(w.layoutCount == 8);                       /* trailing... table = 8 entries */
    CHECK(w.layouts[0].present && w.layouts[0].used);
    CHECK(!w.layouts[2].present);                    /* NULL entry skipped */
    CHECK(w.layouts[1].present && w.layouts[1].w == 20 && w.layouts[1].h == 20);
    CHECK(w.layouts[3].ts[1]->addr == 0 && w.layouts[3].ts[0]->addr == tsA);   /* NULL secondary */
    CHECK(w.tilesets[0].addr == 0);
    /* tileset decode facts */
    CHECK(w.layouts[1].ts[0]->compressed && w.layouts[1].ts[0]->tilesBytes == 64);
    CHECK(w.layouts[1].ts[0]->metatileCount == 8);   /* adjacency rule */
    CHECK(w.layouts[1].ts[1]->metatileCount == 512); /* gap -> fallback */
    CHECK(!w.layouts[1].ts[1]->compressed && w.layouts[1].ts[1]->secondary);
    CHECK(w.layouts[1].ts[1]->tilesBytes == 512 * 32 || w.layouts[1].ts[1]->tilesBytes > 0);
    CHECK(rg_attr(&w.layouts[1], 3) == 0x1003);      /* primary table */
    CHECK(rg_attr(&w.layouts[1], 8) == 0);           /* past primary count 8 */
    CHECK(rg_attr(&w.layouts[1], 512 + 30) == 0x101E); /* secondary at id - 512 */
    CHECK(rg_attr(&w.layouts[1], RG_NONE) == 0);
    CHECK(rg_attr(&w.layouts[3], 512 + 1) == 0);     /* NULL secondary */
    CHECK(w.tilesetCount == 4);                      /* NULL + tsA + tsB + tsC (layout 6) */
    /* pairs: first-appearance order by layout id */
    CHECK(w.pairs[w.layouts[1].pairIndex].ts[0] == 1);
    CHECK(w.layouts[4].pairIndex == w.layouts[1].pairIndex);
    CHECK(w.layouts[5].pairIndex != w.layouts[1].pairIndex);
    /* groups: 34 groups, group 0 has 2 maps, group 1 has 1, others one filler each */
    CHECK(w.groupCount[0] == 2 && w.groupCount[1] == 1 && w.groupCount[2] == 1 && w.groupCount[33] == 1);
    CHECK(w.mapCount == 2 + 1 + 32);
    CHECK(rg_world_map(&w, 0, 1) != NULL && rg_world_map(&w, 0, 1)->mapType == MAP_TYPE_INDOOR);
    CHECK(rg_world_map(&w, 0, 2) == NULL && rg_world_map(&w, 40, 0) == NULL);
    /* outdoor = any map; the indoor map's events union with the route's */
    CHECK(w.layouts[1].outdoor && !w.layouts[3].outdoor);
    CHECK(w.layouts[1].warpCount == 3);              /* (5,3) duplicate collapses */
    CHECK(w.layouts[1].warps[0].y == 2 && w.layouts[1].warps[0].x == 2);   /* sorted (y, x) */
    CHECK(w.layouts[1].warps[1].y == 3 && w.layouts[1].warps[2].y == 7);
    CHECK(rg_has_warp(&w.layouts[1], 5, 3) && rg_has_warp(&w.layouts[1], 1, 7) && !rg_has_warp(&w.layouts[1], 3, 5));
    CHECK(w.layouts[1].signCount == 3);              /* kinds 0,4 (x2 same cell),... */
    CHECK(rg_has_sign(&w.layouts[1], 4, 4));         /* kind 0 */
    CHECK(rg_has_sign(&w.layouts[1], 9, 9));         /* kind 4 is a sign */
    CHECK(rg_has_sign(&w.layouts[1], 3, 3));         /* kind 3 from map B is a sign (kind <= 4) */
    CHECK(!rg_has_sign(&w.layouts[1], 6, 6));        /* kind 7 */
    CHECK(!rg_has_sign(&w.layouts[1], 8, 8));        /* kind 5 */
    CHECK(w.signEvents == 4 && w.warpEvents == 4);
    /* alternates: id 5 (75% equal, same size and pair) inherits; 6/7/8 do not */
    CHECK(w.layouts[4].altOf == 2 && w.layouts[4].outdoor && w.layouts[4].warpCount == 3);
    CHECK(w.layouts[5].altOf == 0 && w.layouts[6].altOf == 0 && w.layouts[7].altOf == 0);
    CHECK(!w.layouts[4].used);
    /* cell queries */
    CHECK(rg_metatile(&w.layouts[1], 3, 0) == (blocks[3] & 0x3FF));
    CHECK(rg_metatile(&w.layouts[1], -1, 0) == RG_NONE && rg_metatile(&w.layouts[1], 20, 0) == RG_NONE);
    CHECK(rg_blocked(&w.layouts[1], 0, 0) && !rg_blocked(&w.layouts[1], 1, 0) && !rg_blocked(&w.layouts[1], -1, -1));
    CHECK(rg_elev(&w.layouts[1], 5, 0) == 5 && rg_elev(&w.layouts[1], 99, 0) == 0);
    CHECK(rg_behaviour(&w.layouts[1], 2, 0) == (uint8_t)(0x1002 & 0xFF));
    CHECK(rg_behaviour(&w.layouts[1], -3, 0) == 0);
    CHECK(rg_touches_walkable(&w.layouts[1], 0, 0));
    CHECK(rg_tileset_addr_of(&w.layouts[1], 3) == tsA && rg_tileset_addr_of(&w.layouts[1], 600) == tsB);
    /* connections: dirs 1..4 kept in order, dive/emerge filtered */
    CHECK(rg_map_connections(&w, 0, 1, out, 8) == 2);
    CHECK(out[0].dir == 2 && out[0].offset == 5 && out[0].group == 0 && out[0].num == 1);
    CHECK(out[1].dir == 4 && out[1].offset == -7 && out[1].group == 0 && out[1].num == 0);
    CHECK(rg_map_connections(&w, 0, 1, out, 1) == 2 && out[0].dir == 2);   /* truncation reports the true count */
    CHECK(rg_map_connections(&w, 0, 0, out, 8) == 0 && rg_map_connections(&w, 9, 9, out, 8) == 0);
    rg_world_close(&w);

    /* gate: not BPEE */
    {
        uint8_t c = f.rom[0xAF];
        f.rom[0xAF] = 'X';
        CHECK(rg_world_open(&w, f.rom, FXR_ROM_SIZE) == RG_ERR_NOT_BPEE);
        f.rom[0xAF] = c;
        CHECK(rg_world_open(&w, f.rom, 0x100) == RG_ERR_NOT_BPEE);
    }
    /* a header whose layoutId disagrees with its pointer */
    fxr_p16(&f, FXR_OFF(f.mapHdr[0]) + GBA_OFF_MH_LAYOUT_ID, 4);
    CHECK(rg_world_open(&w, f.rom, FXR_ROM_SIZE) == RG_ERR_LAYOUT_ORDER);
    fxr_p16(&f, FXR_OFF(f.mapHdr[0]) + GBA_OFF_MH_LAYOUT_ID, 2);
    CHECK(rg_world_open(&w, f.rom, FXR_ROM_SIZE) == RG_OK);
    rg_world_close(&w);
    /* the walk stops at an implausible entry: poke layout 8's table slot to junk */
    fxr_p32(&f, 0x481DD4 + 4 * 7, 0x08000010u);
    CHECK(rg_world_open(&w, f.rom, FXR_ROM_SIZE) == RG_ERR_LAYOUT_ORDER || w.layoutCount == 7);   /* maps no longer match or count drops */
    rg_world_close(&w);
    free(f.rom);
}

/* ---- real ROM (M1 pins) ------------------------------------------------------------------ */
static uint8_t *LoadRom(size_t *n)
{
    const char *p = getenv("ROMGEN_ROM");
    FILE *fp;
    uint8_t *b;
    if (!p) return NULL;
    fp = fopen(p, "rb");
    if (!fp) return NULL;
    b = (uint8_t *)malloc(0x2000000);
    *n = fread(b, 1, 0x2000000, fp);
    fclose(fp);
    return b;
}

static void TestRealRom(void)
{
    static const uint16_t kGroups[34] = {57,5,5,6,7,8,9,7,7,14,8,17,10,23,13,15,15,2,2,2,3,1,1,1,108,61,89,2,1,13,1,1,3,1};
    static const uint16_t kUnused[36] = {46,72,73,75,83,84,170,171,172,173,174,175,176,177,178,179,180,181,182,183,
                                          242,312,319,326,357,359,392,432,433,434,435,436,437,438,441,442};
    static const uint16_t kAlt[15][2] = {{46,263},{312,162},{319,47},{326,156},{357,8},{392,27},{432,58},{433,322},
                                          {434,323},{435,324},{436,325},{437,330},{438,331},{441,439},{442,1}};
    size_t n = 0;
    uint8_t *rom = LoadRom(&n);
    RgWorld w;
    unsigned g, i, k, altCount = 0, unusedCount = 0, nullSecondaries = 0;
    uint32_t mism = 0;
    RgConn cn[8];
    unsigned hist[7] = {0};
    unsigned census = 0;

    if (!rom) { printf("SKIP real-ROM pins (set ROMGEN_ROM)\n"); return; }
    CHECK(rg_world_open(&w, rom, n) == RG_OK);
    CHECK(w.layoutCount == 442);
    CHECK(w.mapCount == 518);
    for (g = 0; g < 34; g++) CHECK(w.groupCount[g] == kGroups[g]);
    for (i = 0; i < w.mapCount; i++) {
        const RgMap *m = &w.maps[i];
        mism += rg_rd32(rom + (m->addr - 0x08000000u)) != rg_rd32(rom + (0x481DD4u + 4u * (m->layoutId - 1u)));
    }
    CHECK(mism == 0);
    CHECK(w.layouts[241].ts[1]->addr == 0 && w.layouts[241].w == 58 && w.layouts[241].h == 26);
    for (i = 0; i < w.layoutCount; i++) {
        CHECK(w.layouts[i].present);
        nullSecondaries += w.layouts[i].ts[1]->addr == 0;
        if (!w.layouts[i].used) {
            CHECK(unusedCount < 36 && w.layouts[i].id == kUnused[unusedCount]);
            unusedCount++;
        }
        if (w.layouts[i].altOf) altCount++;
    }
    CHECK(unusedCount == 36);
    CHECK(nullSecondaries == 1);
    CHECK(w.outdoorMaps == 82);
    CHECK(w.signEvents == 533 && w.warpEvents == 1313);
    CHECK(w.tilesetCount == 74 && w.pairCount == 76);   /* 73 tilesets + the NULL one */
    CHECK(altCount == 15);
    for (k = 0; k < 15; k++) CHECK(w.layouts[kAlt[k][0] - 1].altOf == kAlt[k][1]);
    {   /* General (0x083DF704) = 512 metatiles; the Building primary 0x083DF884 = 8 */
        unsigned gen = 0, bld = 0;
        for (i = 0; i < w.tilesetCount; i++) {
            if (w.tilesets[i].addr == 0x083DF704u) { CHECK(w.tilesets[i].metatileCount == 512); gen = 1; }
            if (w.tilesets[i].addr == 0x083DFB6Cu) { CHECK(w.tilesets[i].metatileCount == 512); }
            if (w.tilesets[i].addr == 0x083DF884u) { CHECK(w.tilesets[i].metatileCount == 8); bld = 1; }
        }
        CHECK(gen && bld);
    }
    {   /* behaviour 0x8B appears only in tileset 0x083DF83C */
        unsigned only = 1;
        for (i = 1; i < w.tilesetCount; i++) {
            unsigned m, has = 0;
            for (m = 0; m < w.tilesets[i].metatileCount; m++)
                has += (rg_rd16(w.tilesets[i].attrs + 2 * m) & 0xFF) == 0x8B;
            if (has) { only &= w.tilesets[i].addr == 0x083DF83Cu; census += has; }
        }
        CHECK(only && census == 2);
    }
    {   /* the pinned layouts: Route 104 = id 20 (40x80), Littleroot = 10 (20x20), Rustboro = 4 (40x60) */
        const RgMap *r104 = rg_world_map(&w, 0, 19), *lit = rg_world_map(&w, 0, 9), *rus = rg_world_map(&w, 0, 3);
        CHECK(r104 && r104->layoutId == 20 && w.layouts[19].w == 40 && w.layouts[19].h == 80);
        CHECK(lit && lit->layoutId == 10 && w.layouts[9].w == 20 && w.layouts[9].h == 20);
        CHECK(rus && rus->layoutId == 4 && w.layouts[3].w == 40 && w.layouts[3].h == 60);
        CHECK(rg_has_sign(&w.layouts[19], 20, 50) && rg_has_sign(&w.layouts[19], 27, 66) && rg_has_sign(&w.layouts[19], 23, 5)
              && rg_has_sign(&w.layouts[19], 7, 20) && rg_has_sign(&w.layouts[19], 17, 23));
        CHECK(rg_has_sign(&w.layouts[9], 15, 13) && rg_has_sign(&w.layouts[9], 6, 17) && rg_has_sign(&w.layouts[9], 7, 8)
              && rg_has_sign(&w.layouts[9], 12, 8));
    }
    /* connections census: 1x27 2x27 3x40 4x40 (dive/emerge filtered) and the verified pairs */
    for (i = 0; i < w.mapCount; i++) {
        unsigned c = rg_map_connections(&w, w.maps[i].group, w.maps[i].num, cn, 8), j;
        for (j = 0; j < c && j < 8; j++) hist[cn[j].dir]++;
    }
    CHECK(hist[1] == 27 && hist[2] == 27 && hist[3] == 40 && hist[4] == 40);
    CHECK(rg_map_connections(&w, 0, 16, cn, 8) >= 1);
    {
        unsigned c = rg_map_connections(&w, 0, 19, cn, 8), j, found = 0;   /* Route 104 -> Petalburg 0/0, right, offset 50 */
        for (j = 0; j < c; j++) if (cn[j].dir == 4 && cn[j].group == 0 && cn[j].num == 0 && cn[j].offset == 50) found = 1;
        CHECK(found);
        c = rg_map_connections(&w, 0, 9, cn, 8);   /* Littleroot -> Route 101 (0/16) is up */
        found = 0;
        for (j = 0; j < c; j++) if (cn[j].dir == 2 && cn[j].group == 0 && cn[j].num == 16) found = 1;
        CHECK(found);
        c = rg_map_connections(&w, 0, 16, cn, 8);  /* Route 101 -> Littleroot is down */
        found = 0;
        for (j = 0; j < c; j++) if (cn[j].dir == 1 && cn[j].group == 0 && cn[j].num == 9) found = 1;
        CHECK(found);
    }
    printf("real ROM: %u layouts, %u maps, %u tilesets, %u pairs, %u outdoor maps\n", w.layoutCount, w.mapCount,
           w.tilesetCount - 1, w.pairCount, w.outdoorMaps);
    rg_world_close(&w);
    free(rom);
}

int main(void)
{
    TestSynthetic();
    TestRealRom();
    printf("test_romgen_world: %d checks, %d failures\n", sChecks, sFails);
    return sFails ? 1 : 0;
}
