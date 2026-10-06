// test_romgen_frlg_world.c -- Phase 34 slice G1: romgen opens FireRed and LeafGreen (rev 1) through the game profile.
// Real-ROM census pins on BOTH carts (SPEC-P34 section 4 "G1"; the numbers are ROM-measured by the M1 survey and
// re-measured here), plus synthetic gate checks. The ROMs are found through ROMGEN_ROM_FR / ROMGEN_ROM_LG, or else as
// firered.gba / leafgreen.gba beside ROMGEN_ROM (see rg_fixture.h). Without them the real-ROM part prints SKIP.
//
//   ROMGEN_ROM=$PWD/roms/emerald.gba make -C tools/romgen test T=frlg_world
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rg_fixture.h"
#include "rg_gameprof.h"
#include "rg_world.h"

static int sChecks, sFails, sSkipped;
#define CHECK(c) do { ++sChecks; if (!(c)) { ++sFails; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static const uint8_t kGroupSizes[43] = {5, 123, 60, 66, 4, 6, 8, 10, 6, 8, 20, 10, 8, 2, 10, 4, 2, 2, 2, 1, 1, 2,
                                        2, 3, 2, 3, 2, 1, 1, 1, 1, 7, 5, 5, 8, 8, 5, 5, 1, 1, 1, 2, 1};

typedef struct {
    const char *name;
    const char *env;
    GpGame game;
    uint32_t mapGroups, mapLayouts, general, building;
    uint8_t *rom;
    size_t n;
    RgWorld w;
    RgErr err;
} Cart;

static Cart sCart[2] = {
    {"FireRed rev1", FXR_ENV_FR, GP_FIRERED, 0x08352718u, 0x0834EBFCu, 0x082D4B04u, 0x082D4C24u, NULL, 0, {0}, RG_OK},
    {"LeafGreen rev1", FXR_ENV_LG, GP_LEAFGREEN, 0x083526F8u, 0x0834EBDCu, 0x082D4AE4u, 0x082D4C04u, NULL, 0, {0}, RG_OK},
};

/* ---- synthetic: the game gate ---- */
static void TestGate(void)
{
    RgWorld w;
    uint8_t rom[0x1000];
    const GameProfile *p;

    memset(rom, 0, sizeof rom);
    CHECK(rg_world_open(&w, rom, sizeof rom) == RG_ERR_GAME);       /* zero header: not a supported game */
    CHECK(RG_ERR_NOT_BPEE == RG_ERR_GAME);                          /* the old name is an alias, exit codes do not move */
    memcpy(rom + 0xAC, "BPRE", 4);
    rom[0xBC] = 0;                                                  /* rev 0 is refused (lead decision 9.1 #7) */
    CHECK(gameprof_detect_romgen(rom, sizeof rom) == NULL);
    CHECK(rg_world_open(&w, rom, sizeof rom) == RG_ERR_GAME);
    rom[0xBC] = 1;
    p = gameprof_detect_romgen(rom, sizeof rom);
    CHECK(p != NULL && p->game == GP_FIRERED && p->groupCount == 43 && p->layoutBytes == 26);
    CHECK(rg_world_open(&w, rom, sizeof rom) == RG_ERR_GAME);       /* header ok, tables outside this tiny buffer */
    CHECK(gameprof_detect(rom, sizeof rom) == NULL);                /* the renderer path still refuses (R1/R2 own it) */
    memcpy(rom + 0xAC, "BPGE", 4);
    p = gameprof_detect_romgen(rom, sizeof rom);
    CHECK(p != NULL && p->game == GP_LEAFGREEN && p->dataSubdir[3] == 'E');
    memcpy(rom + 0xAC, "BPEE", 4);
    CHECK(gameprof_detect_romgen(rom, sizeof rom) == gameprof_emerald());
    memcpy(rom + 0xAC, "AXVE", 4);
    CHECK(gameprof_detect_romgen(rom, sizeof rom) == NULL);
}

/* ---- the profile rows themselves ---- */
static void TestRows(void)
{
    unsigned c, g, b;
    uint8_t rom[0x100];

    for (c = 0; c < 2; c++) {
        const GameProfile *p;

        memset(rom, 0, sizeof rom);
        memcpy(rom + 0xAC, c == 0 ? "BPRE" : "BPGE", 4);
        rom[0xBC] = 1;
        p = gameprof_detect_romgen(rom, sizeof rom);
        CHECK(p != NULL && p->game == sCart[c].game);
        if (p == NULL) continue;
        CHECK(p->mapGroups == sCart[c].mapGroups && p->mapLayouts == sCart[c].mapLayouts);
        CHECK(p->tsGeneral == sCart[c].general && p->tsBuilding == sCart[c].building);
        CHECK(p->groupCount == 43 && p->layoutSlots == 384 && p->nMetatilesTotal == 1024);
        CHECK(p->nPrimMetatiles == 640 && p->nPrimTiles == 640 && p->nPrimPals == 7);
        CHECK(p->tilesetAttrOff == 0x14 && p->attrBytes == 4 && p->behMask == 0x1FF);
        CHECK(p->layerMask == 0x60000000u && p->layerShift == 29 && p->layoutBytes == 26);
        CHECK(!p->emeraldIdTables && !p->interiors3d);
        CHECK(p->groupSizes != NULL && memcmp(p->groupSizes, kGroupSizes, sizeof kGroupSizes) == 0);
        /* SPEC section 2, set by set (every b in 0..511 against an independent hand list) */
        for (b = 0; b < 512; b++) {
            bool water = b == 0x10 || b == 0x11 || b == 0x12 || b == 0x13 || b == 0x15 || b == 0x16 || b == 0x17
                         || b == 0x19 || b == 0x1B || b == 0x22 || b == 0x28 || (b >= 0x50 && b <= 0x53);
            bool surf = b == 0x10 || b == 0x11 || b == 0x12 || b == 0x13 || b == 0x15 || b == 0x19 || b == 0x1B
                        || b == 0x22 || (b >= 0x50 && b <= 0x53);
            CHECK(gp_beh(&p->water, b) == water);
            CHECK(gp_beh(&p->surfable, b) == surf);
            CHECK(gp_beh(&p->jump, b) == (b >= 0x38 && b <= 0x3B));
            CHECK(gp_beh(&p->houseDoor, b) == (b == 0x69));
            CHECK(gp_beh(&p->sand, b) == (b == 0x21));
            CHECK(gp_beh(&p->tallGrass, b) == (b == 0x02));
            CHECK(gp_beh(&p->signpost, b) == (b == 0x84));
            CHECK(gp_beh(&p->reflective, b) == (b == 0x10 || b == 0x16 || b == 0x23));
            CHECK(gp_beh(&p->ice, b) == (b == 0x23));
            CHECK(gp_beh(&p->shallowFlowing, b) == (b == 0x17));
            CHECK(gp_beh(&p->furniture, b) == (b == 0x80 || b == 0x83 || b == 0x86));
        }
        CHECK(!gp_beh(&p->water, 0x2A) && !gp_beh(&p->water, 0x2B) && !gp_beh(&p->water, 0x14));
        CHECK(!gp_beh(&p->houseDoor, 0x8B) && !gp_beh(&p->houseDoor, 0x8D));
        for (g = 0; g < 43; g++) CHECK(p->groupSizes[g] == kGroupSizes[g]);
    }
}

static void Census(const Cart *C)
{
    const RgWorld *w = &C->w;
    const GameProfile *p = w->prof;
    unsigned g, i, nullSlots = 0, used = 0, general = 0, building = 0, maxMt;
    unsigned long beh[512];
    uint32_t tbl;
    const RgMap *m;
    const RgLayout *L;

    CHECK(p != NULL && p->game == C->game);
    if (p == NULL) return;
    /* groups and maps */
    for (g = 0; g < 43; g++) CHECK(w->groupCount[g] == kGroupSizes[g]);
    CHECK(w->mapCount == 425);
    /* the layout table: found at runtime, equal to the stored value, and consistent with every header */
    tbl = rg_find_map_layouts(w);
    CHECK(tbl == C->mapLayouts && tbl == p->mapLayouts);
    for (i = 0; i < w->layoutCount; i++) nullSlots += rg_rd32(w->rom + (tbl - 0x08000000u) + 4u * i) == 0;
    for (i = 0; i < w->layoutCount; i++) used += w->layouts[i].used;
    printf("  %s: layoutCount %u, NULL slots %u, referenced %u, tilesets %u, pairs %u, outdoor %u, warps %u, signs %u\n",
           C->name, (unsigned)w->layoutCount, nullSlots, used, (unsigned)w->tilesetCount - 1u, (unsigned)w->pairCount,
           (unsigned)w->outdoorMaps, (unsigned)w->warpEvents, (unsigned)w->signEvents);
    CHECK(w->layoutCount == p->layoutSlots);
    CHECK(nullSlots == 18);
    CHECK(used == 309);
    CHECK(w->tilesetCount - 1u == 63);
    CHECK(w->outdoorMaps == 76);
    CHECK(w->warpEvents == 1294);
    CHECK(w->signEvents == 519);   /* SURVEY said 506; true count 519 = bg kinds 0 x422, 1 x73, 3 x14, 4 x10 (SPEC mismatch) */
    CHECK(w->pairCount == 62);
    /* every map header's layout slot agrees with the header (the all-headers rule), and ids stay in range */
    for (i = 0; i < w->mapCount; i++) {
        const uint8_t *h = w->rom + (w->maps[i].addr - 0x08000000u);
        CHECK(w->maps[i].layoutId >= 1 && w->maps[i].layoutId <= w->layoutCount);
        CHECK(rg_rd32(w->rom + (tbl - 0x08000000u) + 4u * (w->maps[i].layoutId - 1u)) == rg_rd32(h));
    }
    /* General / Building primaries */
    m = rg_world_map(w, 3, 0);
    CHECK(m != NULL && m->layoutId == 78 && m->mapType == 1);
    L = &w->layouts[m->layoutId - 1];
    CHECK(L->ts[0]->addr == C->general && L->w == 24 && L->h == 20 && L->prof == p);
    m = rg_world_map(w, 4, 0);
    CHECK(m != NULL && m->layoutId == 1);
    L = &w->layouts[m->layoutId - 1];
    CHECK(L->ts[0]->addr == C->building && L->w == 13 && L->h == 10);
    for (i = 0; i < w->layoutCount; i++) {
        general += w->layouts[i].present && w->layouts[i].ts[0]->addr == C->general;
        building += w->layouts[i].present && w->layouts[i].ts[0]->addr == C->building;
    }
    printf("  %s: layouts on General %u, on Building %u\n", C->name, general, building);
    CHECK(general == 181);
    /* attribute caps: every tileset's metatile count fits its half of the 1024 */
    maxMt = 0;
    for (i = 1; i < w->tilesetCount; i++) {
        const RgTileset *t = &w->tilesets[i];
        unsigned cap = t->secondary ? p->nMetatilesTotal - p->nPrimMetatiles : p->nPrimMetatiles;
        CHECK(t->metatileCount >= 1 && t->metatileCount <= cap);
        CHECK(t->tilesBytes > 0 && t->tilesBytes <= (t->secondary ? 384u : 640u) * 32u);
        if (!t->secondary && t->metatileCount > maxMt) maxMt = t->metatileCount;
    }
    CHECK(maxMt == 640);
    /* Pallet Town (group 3, num 0): the three doors, the u32 attribute, the behaviour census */
    m = rg_world_map(w, 3, 0);
    L = &w->layouts[m->layoutId - 1];
    CHECK(rg_behaviour(L, 6, 7) == 0x69 && rg_behaviour(L, 15, 7) == 0x69 && rg_behaviour(L, 16, 13) == 0x69);
    CHECK((rg_attr(L, rg_metatile(L, 6, 7)) & 0x1FFu) == 0x69u);
    CHECK(rg_attr(L, rg_metatile(L, 6, 7)) > 0xFFFFu);              /* a real u32: terrain / layer bits above bit 16 */
    CHECK(gp_beh(&p->houseDoor, rg_behaviour(L, 6, 7)));
    CHECK(rg_blocked(L, 6, 7) && rg_blocked(L, 15, 7) && rg_blocked(L, 16, 13));   /* doors are collision-blocked (SPEC 2) */
    CHECK(L->warpCount == 3 && rg_has_warp(L, 6, 7) && rg_has_warp(L, 15, 7) && rg_has_warp(L, 16, 13));
    CHECK(L->signCount == 5);
    {
        static const uint16_t pond[] = {0x123, 0x12A, 0x12B, 0x12C, 0x2D1, 0x2D2};
        for (i = 0; i < sizeof pond / sizeof pond[0]; i++) {
            CHECK((rg_attr(L, pond[i]) & 0x1FFu) == 0x15u);
            CHECK(gp_beh(&p->water, rg_attr(L, pond[i]) & 0x1FFu));
        }
    }
    /* behaviour census over every cell of every present layout (SPEC section 2, last column) */
    memset(beh, 0, sizeof beh);
    for (i = 0; i < w->layoutCount; i++) {
        const RgLayout *Li = &w->layouts[i];
        int x, y;
        if (!Li->present) continue;
        for (y = 0; y < (int)Li->h; y++)
            for (x = 0; x < (int)Li->w; x++) {
                unsigned b = rg_behaviour(Li, x, y);
                if (b < 512) beh[b]++;
            }
    }
    printf("  %s: census 0x15 %lu 0x1B %lu 0x02 %lu 0x21 %lu 0x38 %lu 0x39 %lu 0x3A %lu 0x3B %lu 0x69 %lu 0x84 %lu\n", C->name,
           beh[0x15], beh[0x1B], beh[0x02], beh[0x21], beh[0x38], beh[0x39], beh[0x3A], beh[0x3B], beh[0x69], beh[0x84]);
    CHECK(beh[0x15] == 40507 && beh[0x1B] == 751 && beh[0x02] == 5701 && beh[0x21] == 2878);
    CHECK(beh[0x38] == 41 && beh[0x39] == 46 && beh[0x3A] == 0 && beh[0x3B] == 1022);
    for (i = 0x3C; i <= 0x3F; i++) CHECK(beh[i] == 0);              /* no diagonal ledges in Kanto */
    {
        unsigned top = 0;
        for (i = 0; i < 512; i++) if (beh[i]) top = i;
        printf("  %s: largest behaviour value used by any layout: 0x%X\n", C->name, top);
        CHECK(top == 0xE0);
    }
}

static void CompareCarts(void)
{
    const RgWorld *a = &sCart[0].w, *b = &sCart[1].w;
    unsigned i, same = 0, k;

    CHECK(a->layoutCount == b->layoutCount && a->mapCount == b->mapCount);
    for (i = 0; i < a->layoutCount && i < b->layoutCount; i++) {
        const RgLayout *A = &a->layouts[i], *B = &b->layouts[i];
        CHECK(A->present == B->present && A->w == B->w && A->h == B->h);
        if (A->present && B->present && A->w == B->w && A->h == B->h) {
            size_t nb = (size_t)A->w * A->h * 2u;
            if (memcmp(A->blocks, B->blocks, nb) == 0) same++;
            CHECK(memcmp(A->blocks, B->blocks, nb) == 0);
        }
    }
    CHECK(same == 366);   /* 384 slots - 18 NULL: every present layout's blockdata is byte-identical on FR and LG */
    printf("  FR vs LG: blockdata identical in %u of %u slots (the rest are the 18 NULL slots)\n", same, (unsigned)a->layoutCount);
    for (k = 0; k < a->mapCount && k < b->mapCount; k++)
        CHECK(a->maps[k].layoutId == b->maps[k].layoutId && a->maps[k].mapType == b->maps[k].mapType);
}

int main(void)
{
    unsigned c, loaded = 0;

    TestGate();
    TestRows();
    for (c = 0; c < 2; c++) {
        sCart[c].rom = fxr_load_rom(sCart[c].env, &sCart[c].n);
        if (sCart[c].rom == NULL) {
            printf("SKIP %s: set ROMGEN_ROM (firered.gba / leafgreen.gba beside it) or %s\n", sCart[c].name, sCart[c].env);
            sSkipped++;
            continue;
        }
        sCart[c].err = rg_world_open(&sCart[c].w, sCart[c].rom, sCart[c].n);
        CHECK(sCart[c].err == RG_OK);
        if (sCart[c].err != RG_OK) {
            printf("  %s: open failed: %s\n", sCart[c].name, rg_err_str(sCart[c].err));
            continue;
        }
        loaded++;
        printf("== %s\n", sCart[c].name);
        Census(&sCart[c]);
    }
    if (loaded == 2) CompareCarts();
    for (c = 0; c < 2; c++) {
        if (sCart[c].err == RG_OK && sCart[c].rom != NULL) rg_world_close(&sCart[c].w);
        free(sCart[c].rom);
    }
    printf("test_romgen_frlg_world: %d checks, %d failures, %d skipped\n", sChecks, sFails, sSkipped);
    return sFails != 0;
}
