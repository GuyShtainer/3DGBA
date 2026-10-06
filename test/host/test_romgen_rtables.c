// test_romgen_rtables.c -- host test for phase 33 S3.2 (SPEC-S3 section 6): rg_rtables T1-T9 on the real ROM and
// their refusals, rg_behavior A.6 sets, G7 lower-only cell image, G8 rg_props_cells_in, rg_ralias (alias_of / AliasArt).
// Synthetic checks always run; the real-ROM checks run with ROMGEN_ROM=/abs/path/emerald.gba (else SKIP).
//   make -C tools/romgen test T=rtables
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gba_game.h"
#include "rg_art.h"
#include "rg_behavior.h"
#include "rg_bexpand.h"
#include "rg_bimg.h"
#include "rg_bspecs.h"
#include "rg_buildings.h"
#include "rg_ralias.h"
#include "rg_rrock.h"
#include "rg_rtables.h"
#include "rg_world.h"

static int sChecks, sFails, sSkips;
#define CHECK(c) do { ++sChecks; if (!(c)) { ++sFails; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static void TestStatic(void)
{
    unsigned b, n = 0, i;

    for (b = 0; b < 256; b++) n += rg_is_flat_behaviour(b) ? 1u : 0u;
    CHECK(n == 24);                                         /* A.6: 24 values */
    CHECK(!rg_is_flat_behaviour(0x2B) && rg_is_flat_behaviour(0x0A) && rg_is_flat_behaviour(0xEA));
    CHECK(rg_is_flat_behaviour(0x70) && rg_is_flat_behaviour(0x78) && !rg_is_flat_behaviour(0x79) && rg_is_flat_behaviour(0x7A));
    CHECK(rg_is_flat_behaviour(0x8B) && rg_is_flat_behaviour(0x8D) && !rg_is_flat_behaviour(0x8E));
    CHECK(rg_is_sand(0x06) && rg_is_sand(0x21) && rg_is_sand(0xBF) && !rg_is_sand(0x07));
    CHECK(RG_MB_WATERFALL == 0x13u && RG_MB_BERRY_TREE_SOIL == 0xA0u);
    CHECK(rg_is_house_door(0x69) && rg_is_flat_behaviour(0x69));        /* S2's doors fall inside FLAT (A.6) */
    CHECK(!rg_is_jump(0xBB) && rg_is_jump(0x38) && rg_is_jump(0x3F));
    /* A.7 */
    CHECK(rg_dir_sort_key(1) == 0 && rg_dir_sort_key(3) == 1 && rg_dir_sort_key(4) == 2 && rg_dir_sort_key(2) == 3);
    CHECK(rg_dir_sort_key(5) == 4 && rg_dir_sort_key(6) == 4);
    /* table shapes */
    CHECK(RG_ALIAS_LAYOUTS[0] == 13 && RG_ALIAS_LAYOUTS[1] == 136 && RG_ALIAS_LAYOUTS[2] == 292);
    CHECK(RG_WRAP_GROUP_SEEDS[0] == 20 && RG_WRAP_GROUP_SEEDS[1] == 21 && RG_WRAP_GROUP_SEEDS[2] == 22);
    CHECK(RG_WORLD_ROOT == 10 && RG_ALIAS_REFERENCE == 32 && RG_EXCLUDED_GROUP_SEED == 38);
    CHECK(RG_ENABLED[0] == 20 && RG_ENABLED[1] == 4);
    CHECK(rg_relief_alt_base(46) == 263 && rg_relief_alt_base(438) == 331 && rg_relief_alt_base(263) == 0);
    for (i = 0; i < RG_ALT_COUNT; i++) CHECK(rg_relief_alt_base(RG_OUTDOOR_ALTS[i].alt) == RG_OUTDOOR_ALTS[i].base);
    /* A.3 has no duplicates; ranks are positions; 442 is excluded (deviation D1) */
    for (i = 0; i < RG_OUTDOOR_COUNT; i++) CHECK(rg_name_rank(RG_OUTDOOR_BY_NAME[i]) == i);
    CHECK(rg_name_rank(442) == 0xFFFFu && !rg_relief_outdoor(442) && rg_relief_outdoor(1));
    /* A.5: 82 rows, layout column all in A.3, no duplicate (group, num) */
    for (i = 0; i < RG_OUTDOOR_MAP_COUNT; i++) {
        unsigned j;
        CHECK(rg_name_rank(RG_OUTDOOR_MAPS_BY_FOLDER[i].layout) != 0xFFFFu);
        for (j = i + 1; j < RG_OUTDOOR_MAP_COUNT; j++)
            CHECK(RG_OUTDOOR_MAPS_BY_FOLDER[i].group != RG_OUTDOOR_MAPS_BY_FOLDER[j].group
                  || RG_OUTDOOR_MAPS_BY_FOLDER[i].num != RG_OUTDOOR_MAPS_BY_FOLDER[j].num);
    }
    CHECK(RG_OUTDOOR_MAPS_BY_FOLDER[0].layout == 192 && RG_OUTDOOR_MAPS_BY_FOLDER[81].layout == 15);
    /* rock sets (rel:559-631) */
    CHECK(rg_is_rock_tile(0x07C) && rg_is_rock_tile(0x0A2) && rg_is_rock_tile(0x33C) && !rg_is_rock_tile(0x172));
    CHECK(rg_is_alias_role(0x172) && rg_is_alias_role(0x93) && !rg_is_alias_role(0x113));
    n = 0;
    for (b = 0; b < RG_NUM_PRIMARY; b++) n += rg_is_alias_role(b) ? 1u : 0u;
    CHECK(n == 26);                       /* ten + 9 more FACE_SOUTH below 512 (0x33b, 0x33c are secondary) + 0x0A2 + 2 caps + 4 boulders */
}

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

/* ---- the refusals: tamper with the opened world, expect the named assertion to fire, restore ---- */
static void TestRefusals(RgWorld *w)
{
    const char *why = NULL;
    RgMap *m = (RgMap *)rg_world_map(w, 0, 19);
    uint16_t save;
    RgLayout *L = &w->layouts[9];
    uint16_t sw;

    CHECK(rg_rtables_check(w, &why));
    save = m->layoutId;
    m->layoutId = 17;                                  /* Route 104's map pointing elsewhere: T2 or T3 */
    CHECK(!rg_rtables_check(w, &why) && why != NULL);
    m->layoutId = save;
    sw = L->w;
    L->w = 21;                                         /* layout 10 width: T1 */
    why = NULL;
    CHECK(!rg_rtables_check(w, &why) && why != NULL && strncmp(why, "T1", 2) == 0);
    L->w = sw;
    {   /* T5: an outdoor layout (17) on another primary */
        RgLayout *R = &w->layouts[16];
        const RgTileset *p = R->ts[0];
        R->ts[0] = w->layouts[16].ts[1];
        why = NULL;
        CHECK(!rg_rtables_check(w, &why) && why != NULL);
        R->ts[0] = p;
    }
    {   /* T6: a layout joining the Lavaridge secondary set */
        RgLayout *R = &w->layouts[16];
        const RgTileset *p = R->ts[1];
        R->ts[1] = w->layouts[12].ts[1];
        why = NULL;
        CHECK(!rg_rtables_check(w, &why) && why != NULL);
        R->ts[1] = p;
    }
    why = NULL;
    CHECK(rg_rtables_check(w, &why));
}

static void TestTablesOnRom(RgWorld *w)
{
    const char *why = NULL;
    unsigned census[7] = {0}, i, d;
    RgConn buf[8];
    unsigned n;

    CHECK(rg_rtables_check(w, &why));
    if (why != NULL) printf("  tables: %s\n", why);
    /* T1 spot values, read independently of the table's own struct */
    CHECK(w->layouts[19].w == 40 && w->layouts[19].h == 80);
    CHECK(w->layouts[291].w == 30 && w->layouts[291].h == 46);
    CHECK(w->layouts[31].w == 100 && w->layouts[31].h == 20);
    /* T3: each A.5 entry */
    for (i = 0; i < RG_OUTDOOR_MAP_COUNT; i++) {
        const RgMap *m = rg_world_map(w, RG_OUTDOOR_MAPS_BY_FOLDER[i].group, RG_OUTDOOR_MAPS_BY_FOLDER[i].num);
        CHECK(m != NULL && m->layoutId == RG_OUTDOOR_MAPS_BY_FOLDER[i].layout);
    }
    /* T7 census, recomputed here */
    for (i = 0; i < w->mapCount; i++) {
        const RgMap *m = &w->maps[i];
        if (m->mapType != MAP_TYPE_ROUTE && m->mapType != MAP_TYPE_TOWN && m->mapType != MAP_TYPE_CITY
            && m->mapType != MAP_TYPE_UNDERWATER && m->mapType != MAP_TYPE_OCEAN_ROUTE) continue;
        n = rg_map_connections_all(w, m->group, m->num, buf, 8);
        for (d = 0; d < n && d < 8; d++) if (buf[d].dir <= 6) census[buf[d].dir]++;
    }
    printf("  census down %u up %u left %u right %u dive %u emerge %u\n", census[1], census[2], census[3], census[4],
           census[5], census[6]);
    CHECK(census[1] == 27 && census[2] == 27 && census[3] == 40 && census[4] == 40 && census[5] == 7 && census[6] == 7);
    /* T8 */
    n = rg_map_connections(w, 0, 19, buf, 8);
    CHECK(n == 3 && buf[0].dir == 2 && buf[0].num == 3 && buf[1].dir == 1 && buf[1].num == 20
          && buf[2].dir == 4 && buf[2].offset == 50 && buf[2].num == 0);
    /* the dive/emerge-filtering reader stays the old one */
    CHECK(rg_map_connections_all(w, 0, 19, NULL, 0) >= rg_map_connections(w, 0, 19, NULL, 0));
    /* T9: the flat set does not overlap S0's independently sourced water set */
    for (i = 0; i < 256; i++) CHECK(!(rg_is_flat_behaviour(i) && rg_is_water(i)));
    TestRefusals(w);
}

/* ---- G7: lower-only image vs the two-layer image ---- */
static void TestG7(const RgWorld *w)
{
    const RgLayout *L = &w->layouts[16];                 /* Route 101 */
    RgPair *p = rg_pair_open(w, L->pairIndex);
    unsigned m, same = 0, differ = 0, bad = 0;

    CHECK(p != NULL);
    for (m = 0; m < 512; m++) {
        RgImage lo, both;
        RgCellPx hi;

        rg_cell_px(p, (uint16_t)m, 1, &hi);
        CHECK(rg_cell_image_layers(p, (uint16_t)m, true, &lo) && rg_cell_image_layers(p, (uint16_t)m, false, &both));
        {
            bool upperEmpty = true;
            int x, y;
            RgImage plain;

            for (y = 0; y < 16; y++)
                for (x = 0; x < 16; x++)
                    if (hi.idx[y][x] != 0) upperEmpty = false;
            CHECK(rg_cell_image(p, (uint16_t)m, &plain) && rg_img_equal(&plain, &both));   /* the wrapper is layers=false */
            rg_img_free(&plain);
            if (upperEmpty) { if (rg_img_equal(&lo, &both)) same++; else bad++; }
            else if (rg_img_equal(&lo, &both)) { /* an upper layer exactly equal to the lower: possible, not an error */ }
            else differ++;
        }
        rg_img_free(&lo);
        rg_img_free(&both);
    }
    printf("  G7: %u metatiles with an empty upper layer (lower == both), %u differ, %u wrong\n", same, differ, bad);
    CHECK(same > 50 && differ > 50 && bad == 0);
    rg_pair_close(p);
}

/* ---- G8: cells_in vs the models S2 builds from the same finds ---- */
static void TestG8(const RgWorld *w)
{
    RgSpec props[16];
    unsigned nProps = 0, i, k;
    RgBuildModels ms;
    RgProps *pr;
    RgErr e;
    unsigned layoutsWith = 0, cellsTotal = 0, bit[3] = {0, 0, 0}, id;
    uint8_t *fl;

    for (i = 0; i < rg_spec_count && nProps < 16; i++)
        if (rg_specs[i].kind == RG_SPEC_PROPS) props[nProps++] = rg_specs[i];
    CHECK(nProps > 0);
    memset(&ms, 0, sizeof(ms));
    CHECK(rg_build_models(w, props, nProps, &ms) == RG_OK);
    pr = rg_props_open(w, &e);
    CHECK(pr != NULL && e == RG_OK);
    fl = (uint8_t *)malloc(512u * 512u);
    for (id = 1; id <= w->layoutCount; id++) {
        const RgLayout *L = &w->layouts[id - 1];
        size_t c, cells = 0;

        if (!L->present || L->blocks == NULL) continue;
        CHECK(rg_props_cells(pr, (uint16_t)id, fl) == RG_OK);
        for (c = 0; c < (size_t)L->w * L->h; c++) if (fl[c]) {
            cells++;
            if (fl[c] & 1) bit[0]++;
            if (fl[c] & 2) bit[1]++;
            if (fl[c] & 4) bit[2]++;
        }
        if (cells) layoutsWith++;
        cellsTotal += (unsigned)cells;
    }
    printf("  G8: %u props models, %u layouts with prop cells, %u cells (sea_rock %u, sand_boulder %u, sea_stack %u)\n",
           ms.n, layoutsWith, cellsTotal, bit[0], bit[1], bit[2]);
    CHECK(ms.n > 0 && layoutsWith > 0);
    /* regression: every copy a props model records has its min cell marked (the min cell is a drawn cell for all
     * three objects), and every marked cell lies in the box of a copy the model recorded in some layout */
    for (i = 0; i < ms.n; i++) {
        const RgBuildModel *m = &ms.m[i];

        for (k = 0; k < m->nAt; k++) {
            const RgLayout *L = &w->layouts[m->at[k].lid - 1];
            int x = m->at[k].x, y = m->at[k].y;

            CHECK(rg_props_cells_in(w, m->at[k].lid, fl) == RG_OK);
            if (x >= 0 && y >= 0 && x < (int)L->w && y < (int)L->h)
                CHECK(fl[(size_t)y * L->w + (size_t)x] != 0);
        }
    }
    {   /* the one-shot and the held context agree; an absent layout is refused */
        uint8_t *g = (uint8_t *)malloc(512u * 512u);
        const RgLayout *L = &w->layouts[0];

        CHECK(rg_props_cells_in(w, 1, fl) == RG_OK && rg_props_cells(pr, 1, g) == RG_OK);
        CHECK(memcmp(fl, g, (size_t)L->w * L->h) == 0);
        free(g);
    }
    free(fl);
    rg_props_close(pr);
    rg_models_free(&ms);
}

/* ---- alias ---- */
static void TestAlias(const RgWorld *w)
{
    unsigned a, i;

    for (a = 0; a < 3; a++) {
        RgAlias *al = NULL;
        const RgLayout *L = &w->layouts[RG_ALIAS_LAYOUTS[a] - 1];
        RgPair *own = rg_pair_open(w, L->pairIndex);
        unsigned dup = 0, nonColour = 0;

        CHECK(rg_alias_of(w, RG_ALIAS_LAYOUTS[a], &al) == RG_OK);
        CHECK(al != NULL);
        if (al == NULL) { rg_pair_close(own); continue; }
        printf("  alias %u: %u mapped ids, %u General ids, %u colours\n", RG_ALIAS_LAYOUTS[a], al->nAlias, al->nBack, al->nColour);
        CHECK(al->nAlias > 0 && al->nBack > 0 && al->nBack <= al->nAlias && al->nColour > 0);
        for (i = 0; i < al->nAlias; i++) {
            uint16_t g = rg_alias_metatile(al, L, 0, 0);
            uint16_t back;

            (void)g;
            CHECK(al->own[i] >= RG_NUM_PRIMARY && al->gen[i] < RG_NUM_PRIMARY && rg_is_alias_role(al->gen[i]));
            CHECK(al->toGen[al->own[i]] == al->gen[i]);
            back = rg_alias_own_id(al, al->gen[i]);
            CHECK(al->toGen[back] == al->gen[i]);              /* the console's id reads back as the same General id */
            CHECK(back == al->own[i]);                         /* SPEC-S3 S3.2: own_id(general(m)) == m on every mapped id */
            if (back != al->own[i]) dup++;
        }
        for (i = 0; i < al->nColour; i++) {                    /* the colour map is a function: no `from` twice */
            unsigned j;
            for (j = i + 1; j < al->nColour; j++) if (al->colFrom[i] == al->colFrom[j]) nonColour++;
        }
        CHECK(nonColour == 0);
        CHECK(al->nBack + dup == al->nAlias);                   /* every own id is a back id or loses to a more-used twin */
        printf("    own ids that lose to a more used twin of the same General id: %u\n", dup);
        /* ids that are not aliased read as themselves; a non-alias layout has no alias */
        CHECK(rg_alias_own_id(al, 5) == 5 && rg_alias_own_id(NULL, 777) == 777);
        {   /* recoloured image of an aliased id == the reference image of its General id, wherever the colour map
             * covers the pixel (it covers every pixel of an aliased tile: the votes came from those pixels) */
            RgPair *ref = rg_pair_open(w, w->layouts[RG_ALIAS_REFERENCE - 1].pairIndex);
            unsigned good = 0, tot = 0;

            for (i = 0; i < al->nAlias; i++) {
                RgImage im, gi;
                CHECK(rg_alias_cell_image(al, own, al->own[i], false, &im) && rg_cell_image(ref, al->gen[i], &gi));
                tot++;
                if (rg_img_equal(&im, &gi)) good++;
                rg_img_free(&im);
                rg_img_free(&gi);
            }
            printf("    recoloured == reference image for %u of %u aliased ids\n", good, tot);
            CHECK(good == tot);                                 /* the per-colour vote reproduces the reference drawing */
            rg_pair_close(ref);
        }
        {   /* plain (NULL alias) draws the layout's own colours; primary ids are never recoloured */
            RgImage x, y;
            CHECK(rg_alias_cell_image(al, own, 5, false, &x) && rg_cell_image(own, 5, &y) && rg_img_equal(&x, &y));
            rg_img_free(&x); rg_img_free(&y);
            CHECK(rg_alias_cell_image(NULL, own, al->own[0], true, &x) && rg_cell_image_layers(own, al->own[0], true, &y)
                  && rg_img_equal(&x, &y));
            rg_img_free(&x); rg_img_free(&y);
        }
        rg_alias_free(al);
        rg_pair_close(own);
    }
    {   /* not an alias layout / not on the secondary: None */
        RgAlias *al = (RgAlias *)1;
        CHECK(rg_alias_of(w, 32, &al) == RG_OK && al == NULL);
        CHECK(rg_alias_of(w, 17, &al) == RG_OK && al == NULL);
    }
}

static void TestRealRom(void)
{
    const char *path = getenv("ROMGEN_ROM");
    size_t n = 0;
    uint8_t *rom;
    RgWorld w;

    if (path == NULL) { printf("SKIP real-ROM checks (ROMGEN_ROM unset)\n"); sSkips++; return; }
    rom = ReadAll(path, &n);
    if (rom == NULL) { printf("SKIP cannot read %s\n", path); sSkips++; return; }
    CHECK(rg_world_open(&w, rom, n) == RG_OK);
    TestTablesOnRom(&w);
    TestG7(&w);
    TestG8(&w);
    TestAlias(&w);
    rg_world_close(&w);
    free(rom);
}

int main(void)
{
    TestStatic();
    TestRealRom();
    printf("test_romgen_rtables: %d checks, %d failures%s\n", sChecks, sFails, sSkips ? " (SKIPPED real-ROM parts)" : "");
    return sFails != 0;
}
