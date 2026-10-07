// test_romgen_rs_world.c -- Phase 35 S0: romgen opens Ruby and Sapphire (rev 2) and writes all four files.
// Synthetic gate checks (rev 2 only, romgen-only: the renderer path refuses both), the profile rows (Emerald's ROM constants
// and behaviour sets), then on the real carts: the census pins (docs/phase35-rs/PHASE.md "Recon"), Ruby vs Sapphire
// blockdata, the recipe table (Emerald's exterior rows, components retargeted; S4: six rows repinned, the RS lab), and a
// full rg_run whose regions / signposts / relief / buildings files are pinned by SHA-1 and read back by the vendored
// consumers. Ruby and Sapphire give byte-identical files. ROMs: ROMGEN_ROM_RUBY / ROMGEN_ROM_SAPP, else ruby.gba / sapphire.gba beside ROMGEN_ROM (rg_fixture.h); without
// them the real-ROM part prints SKIP.
//
//   ROMGEN_ROM_RUBY=$PWD/roms/ruby.gba ROMGEN_ROM_SAPP=$PWD/roms/sapphire.gba make -C tools/romgen test T=rs_world
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rg_fixture.h"
#include "rg_buildings.h"
#include "rg_gameprof.h"
#include "rg_rsspecs.h"
#include "rg_run.h"
#include "rg_world.h"
#include "voxel_building.h"
#include "voxel_regions.h"
#include "voxel_relief.h"
#include "voxel_sign.h"
#include "voxel_world.h"

static int sChecks, sFails, sSkipped;
#define CHECK(c) do { ++sChecks; if (!(c)) { ++sFails; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

/* ---- pins (measured on Ruby rev 2; Sapphire is byte-identical) ---- */
#define REGIONS_SHA1 "1a09cd5fa1eab91dd0fa11f88b42b038856144d8"
#define SIGNPOSTS_SHA1 "9b4d379cb2a40e522d3e681ed03110785f3243d4"
#define RELIEF_SHA1 "215a12d9c4ee8f4747e66d895f37d6ed1f8c4b5c"
/* S4: a03a3b74 -> 40135582 (oldale_house and the Rustboro set repinned to the RS layouts, rs_littleroot_lab in the lab);
 * Phase 36 H1: 40135582 -> 91257d8b (21 Hoenn models: Dewford, Mauville, Verdanturf, Fallarbor, Slateport) */
#define BUILDINGS_SHA1 "91257d8b2ff5a894db3d7c2b2e85e890ea08275e"
#define LITTLEROOT_LAYOUT 10u      /* the town whose two houses match Emerald's pins */

static const uint8_t kGroupSizes[34] = {54, 5, 5, 6, 7, 7, 8, 7, 7, 13, 8, 17, 10, 24, 13, 13, 14, 2, 2, 2, 3, 1, 1,
                                        1, 86, 44, 12, 2, 1, 13, 1, 1, 3, 1};

typedef struct {
    const char *name, *env, *code;
    GpGame game;
    uint32_t mapGroups, mapLayouts, general, building, petalburg, rustboro;
    uint8_t *rom;
    size_t n;
    RgWorld w;
    RgErr err;
    RgOutput out;
    char sha[4][41];             /* regions, signposts, relief, buildings */
} Cart;

static Cart sCart[2] = {
    {"Ruby rev2", FXR_ENV_RUBY, "AXVE", GP_RUBY, 0x083085A0u, 0x08304F30u, 0x08286D0Cu, 0x08286E5Cu, 0x08286D24u, 0x08286D3Cu,
     NULL, 0, {0}, RG_OK, {0}, {{0}}},
    {"Sapphire rev2", FXR_ENV_SAPP, "AXPE", GP_SAPPHIRE, 0x08308530u, 0x08304EC0u, 0x08286C9Cu, 0x08286DECu, 0x08286CB4u,
     0x08286CCCu, NULL, 0, {0}, RG_OK, {0}, {{0}}},
};

/* ---- SHA-1 (same helper as test_romgen_frlg_regions.c) ---- */
static uint32_t Rol(uint32_t v, unsigned n) { return (v << n) | (v >> (32 - n)); }
static void Sha1(const uint8_t *d, size_t n, char hex[41])
{
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    size_t total = ((n + 8) / 64 + 1) * 64, i;
    uint8_t *m = (uint8_t *)calloc(total, 1);
    unsigned k;

    if (m == NULL) abort();
    memcpy(m, d, n);
    m[n] = 0x80;
    for (k = 0; k < 8; k++) m[total - 1 - k] = (uint8_t)(((uint64_t)n * 8u) >> (8 * k));
    for (i = 0; i < total; i += 64) {
        uint32_t w[80], a = h[0], b = h[1], c = h[2], dd = h[3], e = h[4];
        for (k = 0; k < 16; k++) w[k] = ((uint32_t)m[i + 4 * k] << 24) | ((uint32_t)m[i + 4 * k + 1] << 16) | ((uint32_t)m[i + 4 * k + 2] << 8) | m[i + 4 * k + 3];
        for (k = 16; k < 80; k++) w[k] = Rol(w[k - 3] ^ w[k - 8] ^ w[k - 14] ^ w[k - 16], 1);
        for (k = 0; k < 80; k++) {
            uint32_t f, kk, t;
            if (k < 20) { f = (b & c) | (~b & dd); kk = 0x5A827999u; }
            else if (k < 40) { f = b ^ c ^ dd; kk = 0x6ED9EBA1u; }
            else if (k < 60) { f = (b & c) | (b & dd) | (c & dd); kk = 0x8F1BBCDCu; }
            else { f = b ^ c ^ dd; kk = 0xCA62C1D6u; }
            t = Rol(a, 5) + f + e + kk + w[k];
            e = dd; dd = c; c = Rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += dd; h[4] += e;
    }
    for (k = 0; k < 5; k++) snprintf(hex + 8 * k, 9, "%08x", h[k]);
    free(m);
}

static void CheckPin(const char *what, const char *pin, const char *got)
{
    printf("pin %s = %s\n", what, got);
    CHECK(strcmp(pin, got) == 0);
}

/* ---- synthetic: the game gate ---- */
static void TestGate(void)
{
    static const char *codes[2] = {"AXVE", "AXPE"};
    uint8_t rom[0x1000];
    RgWorld w;
    unsigned c, rev;

    for (c = 0; c < 2; c++) {
        const GameProfile *p;

        memset(rom, 0, sizeof rom);
        memcpy(rom + 0xAC, codes[c], 4);
        for (rev = 0; rev < 4; rev++) {          /* rev 0 and 1 drift from rev 2: refused until measured (PHASE.md non-goals) */
            rom[0xBC] = (uint8_t)rev;
            p = gameprof_detect_romgen(rom, sizeof rom);
            CHECK((p != NULL) == (rev == 2));
            CHECK((gameprof_detect(rom, sizeof rom) != NULL) == (rev == 2));   /* S2: the renderer too, rev 2 only */
        }
        rom[0xBC] = 2;
        p = gameprof_detect_romgen(rom, sizeof rom);
        CHECK(p != NULL && p->game == (c == 0 ? GP_RUBY : GP_SAPPHIRE) && gp_is_rs(p) && !gp_is_kanto(p));
        CHECK(p != NULL && memcmp(p->dataSubdir, codes[c], 4) == 0 && p->dataSubdir[4] == 0);
        CHECK(gameprof_detect(rom, sizeof rom) == p && p->rendererOn);  /* S2: the renderer detects RS rev 2 */
        CHECK(rg_world_open(&w, rom, sizeof rom) == RG_ERR_GAME);       /* header ok, tables outside this tiny buffer */
    }
    CHECK(!gp_is_rs(gameprof_emerald()) && !gp_is_kanto(gameprof_emerald()));
}

/* ---- the profile rows: Emerald's ROM constants and behaviour sets, this cart's addresses ---- */
static void TestRows(void)
{
    const GameProfile *e = gameprof_emerald();
    uint8_t rom[0x100];
    unsigned c, b;

    for (c = 0; c < 2; c++) {
        const GameProfile *p;

        memset(rom, 0, sizeof rom);
        memcpy(rom + 0xAC, sCart[c].code, 4);
        rom[0xBC] = 2;
        p = gameprof_detect_romgen(rom, sizeof rom);
        CHECK(p != NULL && p->game == sCart[c].game);
        if (p == NULL) continue;
        CHECK(p->mapGroups == sCart[c].mapGroups && p->mapLayouts == sCart[c].mapLayouts);
        CHECK(p->tsGeneral == sCart[c].general && p->tsBuilding == sCart[c].building);
        CHECK(p->groupCount == 34 && p->layoutSlots == 332 && p->rendererOn);   /* S2: the renderer is on */
        CHECK(p->groupSizes != NULL && memcmp(p->groupSizes, kGroupSizes, sizeof kGroupSizes) == 0);
        CHECK(p->nPrimMetatiles == e->nPrimMetatiles && p->nPrimTiles == e->nPrimTiles && p->nPrimPals == e->nPrimPals);
        CHECK(p->nMetatilesTotal == e->nMetatilesTotal && p->tilesetAttrOff == e->tilesetAttrOff && p->attrBytes == e->attrBytes);
        CHECK(p->behMask == e->behMask && p->layerMask == e->layerMask && p->layerShift == e->layerShift);
        CHECK(p->layoutBytes == e->layoutBytes && p->houseHalfWidth == e->houseHalfWidth && p->houseHeight == e->houseHeight);
        CHECK(!p->emeraldIdTables && !p->interiors3d);
        for (b = 0; b < 512; b++) {
            CHECK(gp_beh(&p->water, b) == gp_beh(&e->water, b) && gp_beh(&p->jump, b) == gp_beh(&e->jump, b));
            CHECK(gp_beh(&p->houseDoor, b) == gp_beh(&e->houseDoor, b) && gp_beh(&p->sand, b) == gp_beh(&e->sand, b));
            CHECK(gp_beh(&p->tallGrass, b) == gp_beh(&e->tallGrass, b) && gp_beh(&p->signpost, b) == gp_beh(&e->signpost, b));
            CHECK(gp_beh(&p->bladeGrass, b) == gp_beh(&e->bladeGrass, b) && gp_beh(&p->surfable, b) == gp_beh(&e->surfable, b));
            CHECK(gp_beh(&p->reflective, b) == gp_beh(&e->reflective, b) && gp_beh(&p->ice, b) == gp_beh(&e->ice, b));
            CHECK(gp_beh(&p->shallowFlowing, b) == gp_beh(&e->shallowFlowing, b)
                  && gp_beh(&p->furniture, b) == gp_beh(&e->furniture, b));
        }
    }
}

/* ---- the recipe table: Emerald's rows without the interiors, components on this cart's secondary tilesets; S4: the six rows
 * whose RS buildings are unchanged repinned to the RS layouts, the lab replaced by the RS-own rs_littleroot_lab ---- */
static void TestSpecs(const Cart *C)
{
    static const char *kRepinned[6] = {"oldale_house", "rustboro_stone", "rustboro_olive", "gym_rustboro", "devon_corporation",
                                       "rustboro_fountain"};
    static const char *kTents[3][2] = {{"battle_tent_verdanturf", "rs_contest_verdanturf"},
                                       {"battle_tent_fallarbor", "rs_contest_fallarbor"},
                                       {"battle_tent_slateport", "rs_contest_slateport"}};
    const RgSpec *t;
    unsigned n = 0, i, interiors = 0, comp = 0, j, k, repinned = 0, pinned = 0, matching = 0, replaced = 0;

    t = rg_game_specs(C->w.prof, &n);
    for (i = 0; i < rg_spec_count; i++) interiors += rg_specs[i].kind == RG_SPEC_INTERIOR;
    CHECK(t != NULL && t != rg_specs && n == rg_spec_count - interiors);
    for (i = 0, j = 0; t != NULL && i < rg_spec_count; i++) {
        if (rg_specs[i].kind == RG_SPEC_INTERIOR) continue;
        if (strcmp(rg_specs[i].name, "littleroot_lab") == 0) {
            CHECK(j < n && strcmp(t[j].name, "rs_littleroot_lab") == 0 && t[j].kind == RG_SPEC_DIRECT);
            CHECK(t[j].layoutId == rg_specs[i].layoutId && t[j].layoutFnv == rg_specs[i].layoutFnv);
            CHECK(t[j].ext != NULL);                  /* look L5 side dressing on the RS-own recipe */
            j++;
            continue;
        }
        for (k = 0; k < 3; k++)
            if (strcmp(rg_specs[i].name, kTents[k][0]) == 0) break;
        if (k < 3) {                                  /* Phase 36 H1: a Contest Hall where Emerald has a Battle Tent */
            CHECK(j < n && strcmp(t[j].name, kTents[k][1]) == 0 && t[j].kind == RG_SPEC_DIRECT && t[j].ext != NULL);
            CHECK(t[j].layoutId == rg_specs[i].layoutId);
            replaced++;
            j++;
            continue;
        }
        CHECK(j < n && strcmp(t[j].name, rg_specs[i].name) == 0 && t[j].kind == rg_specs[i].kind);
        for (k = 0; k < 6; k++)
            if (strcmp(t[j].name, kRepinned[k]) == 0) {
                CHECK(t[j].layoutId == rg_specs[i].layoutId && t[j].layoutFnv != rg_specs[i].layoutFnv);
                CHECK(t[j].layoutFnv == (t[j].layoutId == 4 ? 0x5FF68C82u : 0x37D810BEu));
                repinned++;
            }
        if (t[j].kind == RG_SPEC_COMPONENTS) {
            const RgComponentsCfg *cfg = (const RgComponentsCfg *)t[j].ext;
            CHECK(cfg != NULL && (cfg->secondaryAddr == C->petalburg || cfg->secondaryAddr == C->rustboro));
            comp++;
        }
        j++;
    }
    CHECK(comp > 0 && repinned == 6 && replaced == 3);
    /* every pinned row now names this cart's layout (S0: 7 of 16 did not); 16 -> 37: Phase 36 H1's 21 Hoenn rows (the
     * Verdanturf, Fallarbor and Slateport ones retargeted to the RS layouts, rg_rsspecs.c); 37 -> 41: Phase 36 H2's 4 Hoenn rows
     * (gym_fortree, gym_lavaridge, fortree_hut, pacifidlog_hut) */
    for (j = 0; t != NULL && j < n; j++) {
        if (t[j].layoutId == 0) continue;
        pinned++;
        matching += t[j].layoutId <= C->w.layoutCount && rg_layout_fnv(&C->w.layouts[t[j].layoutId - 1u]) == t[j].layoutFnv;
    }
    CHECK(pinned == 41 && matching == 41);
    printf("  %s: %u recipe rows (%u Emerald rows, %u interiors left out, %u components retargeted, %u repinned, %u/%u pins "
           "match)\n", C->name, n, rg_spec_count, interiors, comp, repinned, matching, pinned);
    n = 7;
    CHECK(rg_rsspecs_table(gameprof_emerald(), &n) == NULL && n == 0);
}

static void Census(const Cart *C)
{
    const RgWorld *w = &C->w;
    const RgLayout *L;
    unsigned g, i, present = 0, onBuilding = 0;
    uint32_t tbl;

    CHECK(w->prof->game == C->game);
    for (g = 0; g < 34; g++) CHECK(w->groupCount[g] == kGroupSizes[g]);
    CHECK(w->mapCount == 394);
    tbl = rg_find_map_layouts(w);
    CHECK(tbl == C->mapLayouts);
    CHECK(w->layoutCount == 332);                    /* capped at layoutSlots: map headers follow the table directly */
    for (i = 0; i < w->layoutCount; i++) present += w->layouts[i].present;
    printf("  %s: layouts %u (present %u), maps %u, tilesets %u, pairs %u, outdoor %u\n", C->name, (unsigned)w->layoutCount,
           present, (unsigned)w->mapCount, (unsigned)w->tilesetCount - 1u, (unsigned)w->pairCount, (unsigned)w->outdoorMaps);
    CHECK(present == 332);
    CHECK(w->tilesetCount - 1u == 56 && w->pairCount == 55 && w->outdoorMaps == 75);
    for (i = 0; i < w->mapCount; i++) {
        const uint8_t *h = w->rom + (w->maps[i].addr - 0x08000000u);
        CHECK(w->maps[i].layoutId >= 1 && w->maps[i].layoutId <= w->layoutCount);
        CHECK(rg_rd32(w->rom + (tbl - 0x08000000u) + 4u * (w->maps[i].layoutId - 1u)) == rg_rd32(h));
    }
    /* Littleroot (layout 10): General primary, Petalburg secondary (the anchor the components remap uses) */
    L = &w->layouts[LITTLEROOT_LAYOUT - 1u];
    CHECK(L->present && L->ts[0]->addr == C->general && L->ts[1] != NULL && L->ts[1]->addr == C->petalburg);
    for (i = 0; i < w->layoutCount; i++)
        onBuilding += w->layouts[i].present && w->layouts[i].ts[0]->addr == C->building;
    CHECK(onBuilding > 0);
}

static void CompareCarts(void)
{
    const RgWorld *a = &sCart[0].w, *b = &sCart[1].w;
    unsigned i, same = 0;

    CHECK(a->layoutCount == b->layoutCount && a->mapCount == b->mapCount && a->tilesetCount == b->tilesetCount);
    for (i = 0; i < a->layoutCount && i < b->layoutCount; i++) {
        const RgLayout *A = &a->layouts[i], *B = &b->layouts[i];
        if (A->present && B->present && A->w == B->w && A->h == B->h
            && memcmp(A->blocks, B->blocks, (size_t)A->w * A->h * 2u) == 0)
            same++;
    }
    printf("  Ruby vs Sapphire: blockdata identical in %u of %u layouts\n", same, (unsigned)a->layoutCount);
    CHECK(same == 332);
    for (i = 0; i < a->mapCount && i < b->mapCount; i++)
        CHECK(a->maps[i].layoutId == b->maps[i].layoutId && a->maps[i].mapType == b->maps[i].mapType);
}

/* ---- the files round-trip through the vendored consumers ---- */
static void WriteFile(const char *p, const uint8_t *b, size_t n)
{
    FILE *fp = fopen(p, "wb");
    CHECK(fp != NULL && fwrite(b, 1, n, fp) == n);
    if (fp) fclose(fp);
}

static void Consume(const Cart *C)
{
    static const char *files[4] = {"regions.bin", "signposts.bin", "relief.bin", "buildings.bin"};
    const RgOutput *o = &C->out;
    char dir[64], cwd[1024], sub[96], p[160];
    VoxelMapInstance inst;
    unsigned k;

    strcpy(dir, "/tmp/rgrs.XXXXXX");
    if (!mkdtemp(dir) || !getcwd(cwd, sizeof cwd)) abort();
    snprintf(sub, sizeof sub, "%s/voxel", dir);
    if (mkdir(sub, 0755) != 0 || chdir(dir) != 0) abort();
    WriteFile("voxel/regions.bin", o->regions, o->regionsSize);
    WriteFile("voxel/signposts.bin", o->signs, o->signsSize);
    WriteFile("voxel/relief.bin", o->relief, o->reliefSize);
    WriteFile("voxel/buildings.bin", o->buildings, o->buildingsSize);
    CHECK(VoxelRegions_Init());
    VoxelSign_Init();
    CHECK(VoxelRelief_Init());
    CHECK(VoxelBuildings_Init());
    memset(&inst, 0, sizeof inst);
    inst.layoutId = LITTLEROOT_LAYOUT;
    CHECK(VoxelBuildings_PageOf(&inst) >= 0);        /* the two houses whose Emerald pins match */
    CHECK(VoxelBuildings_MaxTop() > 0.0f);
    CHECK(VoxelRegions_RoleAt(LITTLEROOT_LAYOUT, 0, 0) < 11u);
    VoxelBuildings_Shutdown();
    VoxelRelief_Shutdown();
    VoxelSign_Shutdown();
    VoxelRegions_Shutdown();
    if (chdir(cwd) != 0) abort();
    for (k = 0; k < 4; k++) {
        snprintf(p, sizeof p, "%s/voxel/%s", dir, files[k]);
        (void)unlink(p);
    }
    (void)rmdir(sub);
    (void)rmdir(dir);
}

static void Run(Cart *C)
{
    RgRunOpts opts;
    RgOutput *o = &C->out;

    memset(&opts, 0, sizeof opts);
    opts.wantSigns = true;
    opts.wantBuildings = true;
    opts.relief = RG_RELIEF_FULL;                     /* FULL is built as LEDGES off Emerald (no drawn relief) */
    CHECK(rg_run(C->rom, C->n, &opts, o) == RG_OK);
    if (o->regions == NULL || o->signs == NULL || o->relief == NULL || o->buildings == NULL) {
        CHECK(!"an output is missing");
        return;
    }
    printf("  %s: regions %zu, signposts %zu (%u records, %u with a head, %u empty), relief %zu (%u ledge layouts, %u ledge "
           "cells, %u drawn rows), buildings %zu (%u models, %u placements, %u failing, %u left out)\n",
           C->name, o->regionsSize, o->signsSize, o->signCount, o->headCount, o->emptyMasks, o->reliefSize, o->rst.ledgeLayouts,
           o->rst.ledgeCells, o->rst.drawnRows, o->buildingsSize, o->bModels, o->bPlacements, o->buildingsFailed,
           o->buildingsDropped);
    CHECK(o->layouts == 332 && o->maps == 394 && o->tilesets == 56 && o->pairs == 55 && o->outdoorMaps == 75);
    CHECK(o->regionsSize == 256216u && memcmp(o->regions, "VXR5", 4) == 0);
    CHECK(o->signsSize == 16136u && o->signCount == 224 && o->headCount == 22 && o->emptyMasks == 3);
    CHECK(o->reliefSize == 22872u && o->rst.ledgeLayouts == 20 && o->rst.ledgeCells == 851 && o->rst.drawnRows == 0);
    /* 3016324 B, 64 models, 2081 placements, 35508 vertices, 57 masks -> 3518812 / 85 / 2105 / 39252 / 60: Phase 36 H1
     * (Dewford, Mauville, Verdanturf, Fallarbor, Slateport; three Contest Halls in the Battle Tents' place) */
    /* 3016324 B, 64 models, 2081 placements, 35508 vertices, 57 masks -> 3518812 / 85 / 2105 / 39252 / 60: Phase 36 H1
     * (Dewford, Mauville, Verdanturf, Fallarbor, Slateport; three Contest Halls in the Battle Tents' place)
     * -> 3650372 / 89 / 2118 / 41256 / 69: Phase 36 H2 (Fortree, Lavaridge, Pacifidlog: 4 models, 13 placements) */
    CHECK(o->buildingsSize == 3650372u && o->bModels == 89 && o->bPages == 58 && o->bPlacements == 2118);
    CHECK(o->bVertices == 41256 && o->bMasks == 69 && o->bVariants == 66);
    /* S4: every model passes its art gate (S0 left the Emerald lab out here; rs_littleroot_lab replaces it) */
    CHECK(o->buildingsFailed == 0 && o->buildingsDropped == 0);
    CHECK(o->roleCount[VOXEL_ROLE_SIGNPOST] == 224);
    Sha1(o->regions, o->regionsSize, C->sha[0]);
    Sha1(o->signs, o->signsSize, C->sha[1]);
    Sha1(o->relief, o->reliefSize, C->sha[2]);
    Sha1(o->buildings, o->buildingsSize, C->sha[3]);
    Consume(C);
}

int main(void)
{
    unsigned c, k;

    TestGate();
    TestRows();
    for (c = 0; c < 2; c++) {
        sCart[c].rom = fxr_load_rom(sCart[c].env, &sCart[c].n);
        if (sCart[c].rom == NULL) {
            printf("SKIP %s: set %s (or ROMGEN_ROM with %s beside it)\n", sCart[c].name, sCart[c].env,
                   c == 0 ? "ruby.gba" : "sapphire.gba");
            sSkipped++;
            continue;
        }
        sCart[c].err = rg_world_open(&sCart[c].w, sCart[c].rom, sCart[c].n);
        CHECK(sCart[c].err == RG_OK);
        if (sCart[c].err != RG_OK) {
            printf("  %s: open failed: %s\n", sCart[c].name, rg_err_str(sCart[c].err));
            continue;
        }
        printf("== %s\n", sCart[c].name);
        Census(&sCart[c]);
        TestSpecs(&sCart[c]);
        Run(&sCart[c]);
    }
    for (c = 0; c < 2; c++) {
        if (sCart[c].rom == NULL || sCart[c].err != RG_OK) continue;
        CheckPin(c == 0 ? "Ruby regions.bin" : "Sapphire regions.bin", REGIONS_SHA1, sCart[c].sha[0]);
        CheckPin(c == 0 ? "Ruby signposts.bin" : "Sapphire signposts.bin", SIGNPOSTS_SHA1, sCart[c].sha[1]);
        CheckPin(c == 0 ? "Ruby relief.bin" : "Sapphire relief.bin", RELIEF_SHA1, sCart[c].sha[2]);
        CheckPin(c == 0 ? "Ruby buildings.bin" : "Sapphire buildings.bin", BUILDINGS_SHA1, sCart[c].sha[3]);
    }
    if (sCart[0].rom != NULL && sCart[1].rom != NULL && sCart[0].err == RG_OK && sCart[1].err == RG_OK) {
        CompareCarts();
        for (k = 0; k < 4; k++) CHECK(strcmp(sCart[0].sha[k], sCart[1].sha[k]) == 0);
    }
    for (c = 0; c < 2; c++) {
        if (sCart[c].rom == NULL) continue;
        rg_output_free(&sCart[c].out);
        if (sCart[c].err == RG_OK) rg_world_close(&sCart[c].w);
        free(sCart[c].rom);
    }
    printf("test_romgen_rs_world: %d checks, %d failures, %d skipped\n", sChecks, sFails, sSkipped);
    return sFails != 0;
}
