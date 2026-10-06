// test_romgen_gameprof.c -- host test for source/romgen/rg_gameprof.c (phase 34 R0, SPEC section 1, R0 tests).
// Emerald field == macro for every field; for each of the 11 behaviour sets and every b in 0..511 the bitset
// equals the existing predicate; detect refuses BPRE/BPGE (rows still empty), a short buffer and a zero header;
// the VXP() accessor falls back to the Emerald row; real ROM (ROMGEN_ROM set): the bits 8-9 hazard of SPEC 1.2
// over every attribute of every Emerald tileset (the measurement R0 owes the lead).
//
//   make -C tools/romgen test T=gameprof     (links every rg_*.c plus the voxel consumer; see tools/romgen/Makefile)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gba_game.h"
#include "rg_behavior.h"
#include "rg_gameprof.h"
#include "rg_world.h"

static int sChecks, sFails;
#define CHECK(c) do { ++sChecks; if (!(c)) { ++sFails; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static void TestFields(void)
{
    const GameProfile *p = gameprof_emerald();

    CHECK(p != NULL && p->game == GP_EMERALD && memcmp(p->code, "BPEE", 4) == 0 && p->dataSubdir[0] == '\0');
    CHECK(p->mapGroups == GBA_ADDR_MAP_GROUPS && p->groupCount == GBA_MAP_GROUP_COUNT && p->groupCount == 34);
    CHECK(p->mapLayouts == GBA_ADDR_MAP_LAYOUTS && p->layoutSlots == 442);
    CHECK(p->nPrimMetatiles == NUM_METATILES_IN_PRIMARY && p->nPrimMetatiles == RG_NUM_PRIMARY);
    CHECK(p->nPrimTiles == NUM_TILES_IN_PRIMARY && p->nPrimPals == NUM_PALS_IN_PRIMARY);
    CHECK(p->nMetatilesTotal == NUM_METATILES_TOTAL && p->nMetatilesTotal == 1024);
    CHECK(p->tilesetAttrOff == 0x10 && p->attrBytes == 2);
    CHECK(p->behMask == 0x00FFu && p->behMask == GBA_BEHAVIOR_MASK && UNPACK_BEHAVIOR(0xFFFFu) == 0xFFu);
    CHECK(p->layerMask == 0xF000u && p->layerShift == 12);
    CHECK(p->layoutBytes == GBA_ROM_MAPLAYOUT_BYTES && p->layoutBytes == 24);
    CHECK(p->tsGeneral == GBA_ADDR_TILESET_GENERAL && p->tsBuilding == GBA_ADDR_TILESET_BUILDING);
    CHECK(p->houseHalfWidth == 5 && p->houseHeight == 7);
    CHECK(p->gMain == GBA_ADDR_GMAIN && p->sb1Ptr == GBA_ADDR_SB1_PTR && p->backupLayout == GBA_ADDR_BACKUP_LAYOUT);
    CHECK(p->backupMap == GBA_ADDR_BACKUP_MAP && p->mapHeader == GBA_ADDR_MAP_HEADER);
    CHECK(p->objEvents == GBA_ADDR_OBJECT_EVENTS && p->playerAvatar == GBA_ADDR_PLAYER_AVATAR);
    CHECK(p->sprites == GBA_ADDR_SPRITES && p->plttUnfaded == GBA_ADDR_PLTT_UNFADED);
    CHECK(p->paletteFade == GBA_ADDR_PALETTE_FADE && p->playerAvatarBytes == GBA_PLAYER_AVATAR_BYTES);
    CHECK(p->weather == GBA_ADDR_WEATHER && p->weatherPtr == 0);
    CHECK(p->weatherOff[0] == GBA_OFF_WEATHER_CURR && p->weatherOff[1] == GBA_OFF_WEATHER_PALSTATE);
    CHECK(p->weatherOff[2] == GBA_OFF_WEATHER_EVA && p->weatherOff[3] == GBA_OFF_WEATHER_FOGH);
    CHECK(p->weatherOff[4] == GBA_OFF_WEATHER_FOGD);
    CHECK(p->gfxInfoPtrs == GBA_ADDR_GFX_INFO_PTRS && p->gfxInfoCount == GBA_GFX_INFO_COUNT);
    CHECK(p->fldeffTemplates == GBA_ADDR_FLDEFF_TEMPLATES && p->fldeffCount == GBA_FLDEFF_TEMPLATE_COUNT);
    CHECK(p->cb2Overworld == CB2_Overworld && p->cb2OverworldBasic == CB2_OverworldBasic);
    CHECK(p->emeraldIdTables && p->interiors3d && p->treePart == NULL && p->specs == NULL);
    CHECK(gameprof_emerald() == p);
}

static bool Tall(unsigned b) { return b == 0x02 || b == 0x03 || b == 0x07 || b == 0x09; }

static void TestSets(void)
{
    const GameProfile *p = gameprof_emerald();
    unsigned b, n[11] = {0};

    for (b = 0; b < 512; b++) {
        bool u8ok = b < 256u;
        bool fur = u8ok && (MetatileBehavior_IsCounter((u8)b) || MetatileBehavior_IsPC((u8)b)
                            || MetatileBehavior_IsSecretBasePC((u8)b) || MetatileBehavior_IsPlayerRoomPCOn((u8)b)
                            || b == MB_SECRET_BASE_REGISTER_PC || b == MB_TELEVISION);
        CHECK(gp_beh(&p->water, b) == rg_is_water(b));
        CHECK(gp_beh(&p->jump, b) == rg_is_jump(b));
        CHECK(gp_beh(&p->houseDoor, b) == rg_is_house_door(b));
        CHECK(gp_beh(&p->sand, b) == rg_is_sand(b));
        CHECK(gp_beh(&p->tallGrass, b) == Tall(b));
        CHECK(!gp_beh(&p->signpost, b));
        CHECK(gp_beh(&p->surfable, b) == (u8ok && MetatileBehavior_IsSurfableWaterOrUnderwater((u8)b)));
        CHECK(gp_beh(&p->reflective, b) == (u8ok && MetatileBehavior_IsReflective((u8)b)));
        CHECK(gp_beh(&p->ice, b) == (u8ok && MetatileBehavior_IsIce((u8)b)));
        CHECK(gp_beh(&p->shallowFlowing, b) == (u8ok && MetatileBehavior_IsShallowFlowingWater((u8)b)));
        CHECK(gp_beh(&p->furniture, b) == fur);
        n[0] += gp_beh(&p->water, b); n[1] += gp_beh(&p->jump, b); n[2] += gp_beh(&p->houseDoor, b);
        n[3] += gp_beh(&p->sand, b); n[4] += gp_beh(&p->tallGrass, b); n[6] += gp_beh(&p->surfable, b);
        n[7] += gp_beh(&p->reflective, b); n[8] += gp_beh(&p->ice, b); n[9] += gp_beh(&p->shallowFlowing, b);
        n[10] += gp_beh(&p->furniture, b);
    }
    /* known sizes (water 17 values incl. 0x2A/0x2B, jump 8, houseDoor 3, sand 3, grass 4, surfable 16, ...) */
    CHECK(n[0] == 17 && n[1] == 8 && n[2] == 3 && n[3] == 3 && n[4] == 4);
    CHECK(n[6] == 16 && n[7] == 6 && n[8] == 1 && n[9] == 3 && n[10] == 6);
    CHECK(!gp_beh(&p->water, 512) && !gp_beh(&p->water, 0xFFFFu));   /* out of range is never a member */
}

static void TestDetect(void)
{
    uint8_t *rom = (uint8_t *)calloc(1, 0x100);
    const GameProfile *p;

    CHECK(rom != NULL);
    CHECK(gameprof_detect(NULL, 0x100) == NULL);
    CHECK(gameprof_detect(rom, 0x100) == NULL);            /* zero header */
    memcpy(rom + 0xAC, "BPEE", 4);
    p = gameprof_detect(rom, 0x100);
    CHECK(p == gameprof_emerald());
    rom[0xBC] = 1;
    CHECK(gameprof_detect(rom, 0x100) == gameprof_emerald());   /* Emerald matches by code, as before */
    CHECK(gameprof_detect(rom, 0xBF) == NULL && gameprof_detect(rom, 0) == NULL);   /* short buffer */
    memcpy(rom + 0xAC, "BPRE", 4);
    CHECK(gameprof_detect(rom, 0x100) == NULL);            /* FireRed rev 1: row still empty (R2 enables it) */
    memcpy(rom + 0xAC, "BPGE", 4);
    CHECK(gameprof_detect(rom, 0x100) == NULL);
    rom[0xBC] = 0;
    CHECK(gameprof_detect(rom, 0x100) == NULL);
    memcpy(rom + 0xAC, "BPEJ", 4);
    CHECK(gameprof_detect(rom, 0x100) == NULL);
    free(rom);
}

static void TestAccessor(void)
{
    const GameProfile *saved = gVxProf;

    gVxProf = NULL;
    CHECK(vx_prof() == gameprof_emerald() && VXP(nPrimMetatiles) == 512);
    gVxProf = gameprof_emerald();
    CHECK(vx_prof() == gameprof_emerald());
    gVxProf = saved;
}

static uint8_t *LoadRom(size_t *n)
{
    const char *path = getenv("ROMGEN_ROM");
    FILE *fp;
    uint8_t *b;

    if (!path) return NULL;
    fp = fopen(path, "rb");
    if (!fp) return NULL;
    b = (uint8_t *)malloc(0x2000000);
    *n = b ? fread(b, 1, 0x2000000, fp) : 0;
    fclose(fp);
    return b;
}

/* SPEC 1.2: widening UNPACK_BEHAVIOR to 0x1FF is a no-op for Emerald only if attribute bits 8-9 are zero everywhere. */
static void TestRealRom(void)
{
    size_t n = 0;
    uint8_t *rom = LoadRom(&n);
    RgWorld w;
    unsigned t, i, total = 0, bits89 = 0, bits811 = 0, layerNonzero = 0;
    unsigned layerSeen = 0;
    const GameProfile *p;

    if (!rom) { printf("SKIP real-ROM part (set ROMGEN_ROM)\n"); return; }
    p = gameprof_detect(rom, n);
    CHECK(p == gameprof_emerald());
    CHECK(rg_world_open(&w, rom, n) == RG_OK);
    CHECK(w.tilesetCount > 70);
    for (t = 1; t < w.tilesetCount; t++) {
        const RgTileset *ts = &w.tilesets[t];
        if (ts->addr == 0 || ts->attrs == NULL) continue;
        for (i = 0; i < ts->metatileCount; i++) {
            uint16_t a = rg_rd16(ts->attrs + 2u * i);
            total++;
            bits89 += (a & 0x0300u) != 0;
            bits811 += (a & 0x0F00u) != 0;
            layerNonzero += (a & p->layerMask) != 0;
            layerSeen |= 1u << ((a & p->layerMask) >> p->layerShift);
        }
    }
    printf("MEASURE bits 8-9 (0x0300): %u of %u attributes in %u tilesets are set; bits 8-11 (0x0F00): %u; "
           "layer-type nonzero: %u, layer values seen mask 0x%04X\n", bits89, total, (unsigned)w.tilesetCount - 1u,
           bits811, layerNonzero, layerSeen);
    CHECK(total > 10000);
    CHECK(bits89 == 0);            /* if this fails the Emerald row keeps mask 0xFF and FRLG packs differently (SPEC 1.5) */
    CHECK(rom[0xAC] == 'B' && p->tsGeneral - GBA_ROM_BASE + GBA_ROM_TILESET_BYTES < n);
    /* The General tileset the row names is the one the world's first outdoor layouts use. */
    {
        unsigned hits = 0;
        for (i = 0; i < w.layoutCount; i++)
            hits += w.layouts[i].ts[0]->addr == p->tsGeneral;
        CHECK(hits > 100);
    }
    {
        /* tsBuilding is a tileset header: isCompressed/isSecondary flags then three ROM pointers (+4 tiles, +8 palettes) */
        const uint8_t *b = rom + (p->tsBuilding - GBA_ROM_BASE);
        CHECK(b[0] <= 1 && b[1] <= 1 && (rg_rd32(b + 4) >> 24) == 8 && (rg_rd32(b + 8) >> 24) == 8);
    }
    rg_world_close(&w);
    free(rom);
}

int main(void)
{
    TestFields();
    TestSets();
    TestDetect();
    TestAccessor();
    TestRealRom();
    printf("test_romgen_gameprof: %d checks, %d failures\n", sChecks, sFails);
    return sFails != 0;
}
