// test_romgen_gameprof.c -- host test for source/romgen/rg_gameprof.c (phase 34 R0, SPEC section 1, R0 tests).
// Emerald field == macro for every field; for each of the 11 behaviour sets and every b in 0..511 the bitset
// equals the existing predicate; detect refuses BPRE/BPGE (rows still empty), a short buffer and a zero header;
// the VXP() accessor falls back to the Emerald row; real ROM (ROMGEN_ROM set): the bits 8-9 hazard of SPEC 1.2
// over every attribute of every Emerald tileset (the measurement R0 owes the lead).
//
// Phase 34 R1 adds the FireRed / LeafGreen anchors and the self-check of source/romgen/rg_anchor.c: the ROM checks
// on both real ROMs (ROMGEN_ROM_FR / ROMGEN_ROM_LG, absolute paths; skip cleanly when unset), one corrupted ROM
// per ROM check, "LG differs from FR" for every address the SURVEY lists as differing, and a synthetic RAM image
// per RAM check, each corrupting exactly one field.
//
//   make -C tools/romgen test T=gameprof     (links every rg_*.c plus the voxel consumer; see tools/romgen/Makefile)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gba_game.h"
#include "rg_anchor.h"
#include "rg_behavior.h"
#include "rg_fixture.h"
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
        CHECK(gp_beh(&p->surfable, b) == (u8ok && MetatileBehavior_EmeraldIsSurfableWaterOrUnderwater((u8)b)));
        CHECK(gp_beh(&p->reflective, b) == (u8ok && MetatileBehavior_EmeraldIsReflective((u8)b)));
        CHECK(gp_beh(&p->ice, b) == (u8ok && MetatileBehavior_EmeraldIsIce((u8)b)));
        CHECK(gp_beh(&p->shallowFlowing, b) == (u8ok && MetatileBehavior_EmeraldIsShallowFlowingWater((u8)b)));
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
    /* Phase 34 R2: the renderer detects FireRed / LeafGreen rev 1 (and only rev 1). */
    p = gameprof_detect(rom, 0x100);
    CHECK(p != NULL && p->game == GP_FIRERED && p == gameprof_detect_romgen(rom, 0x100));
    memcpy(rom + 0xAC, "BPGE", 4);
    p = gameprof_detect(rom, 0x100);
    CHECK(p != NULL && p->game == GP_LEAFGREEN && p == gameprof_detect_romgen(rom, 0x100));
    rom[0xBC] = 0;
    CHECK(gameprof_detect(rom, 0x100) == NULL);            /* rev 0: refused */
    memcpy(rom + 0xAC, "BPRE", 4);
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


/* ---- Phase 34 R1: anchors and the self-check ---------------------------------------------------------------- */

static const GameProfile *FrlgRow(const uint8_t *rom, size_t n)
{
    return gameprof_detect_romgen(rom, n);
}

static void TestAnchorRowsDiffer(void)
{
    uint8_t h[0x100];
    const GameProfile *fr, *lg;
    int c;

    memset(h, 0, sizeof h);
    memcpy(h + 0xAC, "BPRE", 4); h[0xBC] = 1;
    fr = FrlgRow(h, sizeof h);
    memcpy(h + 0xAC, "BPGE", 4);
    lg = FrlgRow(h, sizeof h);
    CHECK(fr != NULL && lg != NULL && fr != lg && fr->game == GP_FIRERED && lg->game == GP_LEAFGREEN);
    if (fr == NULL || lg == NULL) return;
    /* Every ROM address the SURVEY lists as differing between FR and LG must differ (copying FR's value to LG is the
     * documented BPGE failure mode). LeafGreen's are NOT a constant shift: the weather const is -0x1C4. */
    CHECK(fr->mapGroups != lg->mapGroups && fr->mapLayouts != lg->mapLayouts);
    CHECK(fr->tsGeneral != lg->tsGeneral && fr->tsBuilding != lg->tsBuilding);
    CHECK(fr->weatherPtr != lg->weatherPtr && fr->gfxInfoPtrs != lg->gfxInfoPtrs && fr->fldeffTemplates != lg->fldeffTemplates);
    CHECK(fr->weatherPtr - lg->weatherPtr == 0x1C4u);
    CHECK(fr->gfxInfoPtrs - lg->gfxInfoPtrs == 0x20u && fr->fldeffTemplates - lg->fldeffTemplates == 0x20u);
    /* The RAM map and the overworld callbacks are one value in both (measured on LG itself, see BUILDLOG-P34 R1). */
    CHECK(fr->gMain == lg->gMain && fr->sb1Ptr == lg->sb1Ptr && fr->backupLayout == lg->backupLayout);
    CHECK(fr->mapHeader == lg->mapHeader && fr->objEvents == lg->objEvents && fr->playerAvatar == lg->playerAvatar);
    CHECK(fr->sprites == lg->sprites && fr->plttUnfaded == lg->plttUnfaded && fr->paletteFade == lg->paletteFade);
    CHECK(fr->cb2Overworld == lg->cb2Overworld && fr->cb2OverworldBasic == lg->cb2OverworldBasic);
    CHECK(memcmp(fr->weatherOff, lg->weatherOff, sizeof fr->weatherOff) == 0);
    /* The pinned values (provenance in docs/PROVENANCE.md). */
    CHECK(fr->gMain == 0x030030F0u && fr->sb1Ptr == 0x03005008u && fr->backupLayout == 0x03005040u);
    CHECK(fr->mapHeader == 0x02036DFCu && fr->objEvents == 0x02036E38u && fr->playerAvatar == 0x02037078u);
    CHECK(fr->sprites == 0x0202063Cu && fr->plttUnfaded == 0x020371F8u && fr->paletteFade == 0x02037AB8u);
    CHECK(fr->weatherPtr == 0x083C2C2Cu && lg->weatherPtr == 0x083C2A68u && fr->weather == 0 && lg->weather == 0);
    CHECK(fr->weatherOff[0] == 0x6D0 && fr->weatherOff[1] == 0x6C6 && fr->weatherOff[2] == 0x730);
    CHECK(fr->weatherOff[3] == 0x6FB && fr->weatherOff[4] == 0x724);
    CHECK(fr->gfxInfoPtrs == 0x0839FE20u && lg->gfxInfoPtrs == 0x0839FE00u && fr->gfxInfoCount == 152);
    CHECK(fr->fldeffTemplates == 0x083A0080u && lg->fldeffTemplates == 0x083A0060u && fr->fldeffCount == 36);
    CHECK(fr->cb2Overworld == 0x080565C9u && fr->cb2OverworldBasic == 0x080565BDu);
    /* R2: both rows drive the renderer (the runtime anchor self-check still gates each bind). */
    CHECK(fr->rendererOn && lg->rendererOn && gameprof_emerald()->rendererOn);
    for (c = VXA_ROM_FIRST; c <= VXA_RAM_LAST; c++) CHECK(strcmp(vx_anchor_name(c), "?") != 0);
}

static void Put32(uint8_t *rom, uint32_t addr, uint32_t v)
{
    uint8_t *p = rom + (addr - 0x08000000u);
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static uint32_t Get32(const uint8_t *rom, uint32_t addr)
{
    return rg_rd32(rom + (addr - 0x08000000u));
}

/* One ROM, one expected row: the checks pass, then one corrupted copy per ROM check fails with exactly its number. */
static void RomChecks(const char *tag, uint8_t *rom, size_t n, GpGame game, const char *code)
{
    const GameProfile *p = FrlgRow(rom, n);
    GameProfile q;
    uint8_t *c;
    uint32_t hdr30, lay30, hdr40, lay40, id30;
    int r;

    CHECK(p != NULL && p->game == game && memcmp(p->code, code, 4) == 0);
    if (p == NULL) return;
    r = vx_anchor_check_rom(p, rom, n);
    if (r != 0) printf("FAIL %s ROM check %d (%s) value 0x%08X\n", tag, r, vx_anchor_name(r), vx_anchor_last_value());
    CHECK(r == 0);
    {   /* the other game's row must refuse this ROM at check 1 (header), never pass */
        uint8_t h[0x100];
        const GameProfile *other;
        memset(h, 0, sizeof h);
        memcpy(h + 0xAC, game == GP_FIRERED ? "BPGE" : "BPRE", 4); h[0xBC] = 1;
        other = FrlgRow(h, sizeof h);
        CHECK(other != NULL && other != p && vx_anchor_check_rom(other, rom, n) == 1);
    }
    CHECK(vx_anchor_check_rom(gameprof_emerald(), rom, n) == 1);   /* only the FRLG rows are checked */
    CHECK(vx_anchor_check_rom(NULL, rom, n) == 1 && vx_anchor_check_rom(p, NULL, 0) == 1 && vx_anchor_check_rom(p, rom, 0x80) == 1);
    /* 2: an anchor outside the image */
    q = *p; q.weatherPtr = 0x09FFFFF0u;
    CHECK(vx_anchor_check_rom(&q, rom, n) == 2);
    q = *p; q.mapGroups = 0x08000000u + (uint32_t)n - 8u;
    CHECK(vx_anchor_check_rom(&q, rom, n) == 2);
    c = (uint8_t *)malloc(n);
    CHECK(c != NULL);
    if (c == NULL) return;
    memcpy(c, rom, n); c[0xBC] = 0;                       /* 1: wrong revision byte */
    CHECK(vx_anchor_check_rom(p, c, n) == 1);
    hdr30 = Get32(rom, Get32(rom, p->mapGroups + 4u * 3u));
    lay30 = Get32(rom, hdr30);
    id30 = (uint32_t)(rom[hdr30 - 0x08000000u + 0x12] | (rom[hdr30 - 0x08000000u + 0x13] << 8));
    hdr40 = Get32(rom, Get32(rom, p->mapGroups + 4u * 4u));
    lay40 = Get32(rom, hdr40);
    CHECK(Get32(rom, lay30 + 0x10) == p->tsGeneral && Get32(rom, lay40 + 0x10) == p->tsBuilding);
    /* 3: group pointers out of order / not in ROM */
    memcpy(c, rom, n); Put32(c, p->mapGroups + 4u * 5u, Get32(c, p->mapGroups + 4u * 4u));
    CHECK(vx_anchor_check_rom(p, c, n) == 3);
    memcpy(c, rom, n); Put32(c, p->mapGroups + 4u * 40u, 0x02000000u);
    CHECK(vx_anchor_check_rom(p, c, n) == 3);
    /* 4: the layout table entry of (3,0)'s layout no longer matches its header */
    memcpy(c, rom, n); Put32(c, p->mapLayouts + 4u * (id30 - 1u), lay30 + 0x20u);
    CHECK(vx_anchor_check_rom(p, c, n) == 4);
    /* 5 / 6: the primary tileset pointer of (3,0)'s / (4,0)'s layout */
    memcpy(c, rom, n); Put32(c, lay30 + 0x10, p->tsGeneral + 0x20u);
    CHECK(vx_anchor_check_rom(p, c, n) == 5);
    memcpy(c, rom, n); Put32(c, lay40 + 0x10, p->tsBuilding + 0x20u);
    CHECK(vx_anchor_check_rom(p, c, n) == 6);
    /* 7: one graphics-table entry is not a record / a record's images pointer is not a ROM pointer */
    memcpy(c, rom, n); Put32(c, p->gfxInfoPtrs + 4u * 100u, 0);
    CHECK(vx_anchor_check_rom(p, c, n) == 7);
    memcpy(c, rom, n); Put32(c, Get32(rom, p->gfxInfoPtrs + 4u * 7u) + 0x1Cu, 0x03000000u);
    CHECK(vx_anchor_check_rom(p, c, n) == 7);
    /* 8: an index the renderer uses lost its template / its callback is not thumb */
    memcpy(c, rom, n); Put32(c, p->fldeffTemplates + 4u * 4u, 0);
    CHECK(vx_anchor_check_rom(p, c, n) == 8);
    memcpy(c, rom, n); Put32(c, Get32(rom, p->fldeffTemplates + 4u * 5u) + 0x14u, 0x080565C8u);
    CHECK(vx_anchor_check_rom(p, c, n) == 8);
    /* 9: the weather constant no longer holds an EWRAM address / the struct would run past EWRAM */
    memcpy(c, rom, n); Put32(c, p->weatherPtr, 0x08000000u);
    CHECK(vx_anchor_check_rom(p, c, n) == 9);
    memcpy(c, rom, n); Put32(c, p->weatherPtr, 0x0203FF00u);
    CHECK(vx_anchor_check_rom(p, c, n) == 9);
    free(c);
    /* 10: callbacks; the &gPaletteFade literal tie; CB2_OverworldBasic must be the push {lr} thunk */
    q = *p; q.cb2Overworld = p->cb2Overworld & ~1u;
    CHECK(vx_anchor_check_rom(&q, rom, n) == 10);
    q = *p; q.paletteFade = p->paletteFade + 4u;
    CHECK(vx_anchor_check_rom(&q, rom, n) == 10);
    q = *p; q.cb2OverworldBasic = p->cb2Overworld;
    CHECK(vx_anchor_check_rom(&q, rom, n) == 10);
}

static void TestRomAnchors(void)
{
    size_t nf = 0, nl = 0;
    uint8_t *fr = fxr_load_rom(FXR_ENV_FR, &nf), *lg = fxr_load_rom(FXR_ENV_LG, &nl);

    if (fr == NULL || lg == NULL) {
        printf("SKIP real-ROM anchor checks (set ROMGEN_ROM_FR and ROMGEN_ROM_LG to absolute paths)\n");
        free(fr); free(lg);
        return;
    }
    RomChecks("FR", fr, nf, GP_FIRERED, "BPRE");
    RomChecks("LG", lg, nl, GP_LEAFGREEN, "BPGE");
    {   /* LG's row must not pass FR's data even with the code forged (the copied-address failure mode) */
        uint8_t *f = (uint8_t *)malloc(nf);
        const GameProfile *lgrow = FrlgRow(lg, nl);
        CHECK(f != NULL && lgrow != NULL);
        if (f != NULL && lgrow != NULL) {
            memcpy(f, fr, nf);
            memcpy(f + 0xAC, "BPGE", 4);
            CHECK(vx_anchor_check_rom(lgrow, f, nf) != 0);
        }
        free(f);
    }
    free(fr); free(lg);
}

/* ---- synthetic RAM images: one corrupted field per RAM check ---- */

typedef struct {
    uint8_t *rom; size_t n;
    uint8_t *ew, *iw;
    GameProfile p;
} Syn;

static void SPut32(uint8_t *b, uint32_t off, uint32_t v) { b[off] = (uint8_t)v; b[off + 1] = (uint8_t)(v >> 8); b[off + 2] = (uint8_t)(v >> 16); b[off + 3] = (uint8_t)(v >> 24); }
static void SPut16(uint8_t *b, uint32_t off, uint32_t v) { b[off] = (uint8_t)v; b[off + 1] = (uint8_t)(v >> 8); }

/* A small ROM with one map group of one map (header, an 8x6 layout) and the constants the RAM checks read, plus a
 * RAM image in which every RAM check passes. */
static void SynBuild(Syn *s)
{
    static const uint8_t one[1] = {1};
    const GameProfile *fr;
    uint8_t h[0x100];
    uint8_t *e, *i;
    uint32_t pa, ob;

    memset(h, 0, sizeof h); memcpy(h + 0xAC, "BPRE", 4); h[0xBC] = 1;
    fr = FrlgRow(h, sizeof h);
    s->p = *fr;
    s->n = 0x40000; s->rom = (uint8_t *)calloc(1, s->n);
    s->ew = (uint8_t *)calloc(1, 0x40000); s->iw = (uint8_t *)calloc(1, 0x8000);
    s->p.groupCount = 1; s->p.groupSizes = one;
    s->p.mapGroups = 0x08000100u;                       /* table -> group array (+0x110) -> header (+0x120) -> layout (+0x160) */
    SPut32(s->rom, 0x100, 0x08000110u); SPut32(s->rom, 0x110, 0x08000120u);
    SPut32(s->rom, 0x120, 0x08000160u);
    SPut16(s->rom, 0x120 + 0x12, 7);                    /* layoutId */
    SPut32(s->rom, 0x160 + 0, 8); SPut32(s->rom, 0x160 + 4, 6);   /* layout width, height */
    s->p.weatherPtr = 0x08000200u; SPut32(s->rom, 0x200, 0x02038F00u);
    s->p.cb2Overworld = 0x08000301u; s->p.cb2OverworldBasic = 0x08000281u;
    i = s->iw; e = s->ew;
    SPut32(i, s->p.gMain - 0x03000000u + 4, s->p.cb2Overworld);
    SPut32(i, s->p.sb1Ptr - 0x03000000u, 0x02025000u);
    SPut16(e, 0x25000 + 0, 10); SPut16(e, 0x25000 + 2, 20); e[0x25000 + 4] = 0; e[0x25000 + 5] = 0;   /* pos (10,20), map (0,0) */
    SPut32(i, s->p.backupLayout - 0x03000000u + 0, 8 + 15); SPut32(i, s->p.backupLayout - 0x03000000u + 4, 6 + 14);
    SPut32(i, s->p.backupLayout - 0x03000000u + 8, 0x02030000u);
    SPut16(e, s->p.mapHeader - 0x02000000u + 0x12, 7);
    pa = s->p.playerAvatar - 0x02000000u;
    e[pa + 4] = 2; e[pa + 5] = 3;                       /* spriteId 2, objectId 3 */
    ob = s->p.objEvents - 0x02000000u + 3u * 0x24u;
    e[ob + 2] = 1;                                      /* isPlayer */
    SPut16(e, ob + 0x10, 17); SPut16(e, ob + 0x12, 27); /* 10 + 7, 20 + 7 */
    SPut32(e, s->p.sprites - 0x02000000u + 2u * 0x44u + 0x08u, 0x08000400u);
    SPut16(e, s->p.paletteFade - 0x02000000u + 4, 5u << 6);
    e[0x38F00 + s->p.weatherOff[0]] = 3; e[0x38F00 + s->p.weatherOff[1]] = 3;
}

#define BREAK_AND_CHECK(want, STMT, UNDO) do { STMT; rc = vx_anchor_check_ram(&s.p, s.rom, s.n, &r); \
        if (rc != (want)) printf("FAIL RAM check expected %d got %d (%s)\n", (want), rc, vx_anchor_name(rc)); \
        CHECK(rc == (want)); UNDO; } while (0)

static void TestRamChecks(void)
{
    Syn s;
    VxaRam r;
    int rc;
    uint32_t gm, sb, bk, mh, pa, ob, sp, pf;

    SynBuild(&s);
    r.ewram = s.ew; r.iwram = s.iw;
    rc = vx_anchor_check_ram(&s.p, s.rom, s.n, &r);
    if (rc != 0) printf("FAIL baseline RAM check %d (%s) 0x%08X\n", rc, vx_anchor_name(rc), vx_anchor_last_value());
    CHECK(rc == 0);
    gm = s.p.gMain - 0x03000000u; sb = s.p.sb1Ptr - 0x03000000u; bk = s.p.backupLayout - 0x03000000u;
    mh = s.p.mapHeader - 0x02000000u; pa = s.p.playerAvatar - 0x02000000u;
    ob = s.p.objEvents - 0x02000000u + 3u * 0x24u; sp = s.p.sprites - 0x02000000u + 2u * 0x44u;
    pf = s.p.paletteFade - 0x02000000u;
    BREAK_AND_CHECK(11, SPut32(s.iw, gm + 4, 0x02000000u), SPut32(s.iw, gm + 4, s.p.cb2Overworld));
    BREAK_AND_CHECK(11, SPut32(s.iw, gm + 4, s.p.cb2Overworld & ~1u), SPut32(s.iw, gm + 4, s.p.cb2Overworld));
    BREAK_AND_CHECK(12, SPut32(s.iw, sb, 0x03000100u), SPut32(s.iw, sb, 0x02025000u));
    BREAK_AND_CHECK(12, SPut32(s.iw, sb, 0x02025002u), SPut32(s.iw, sb, 0x02025000u));
    BREAK_AND_CHECK(13, SPut32(s.iw, bk + 8, 0x08000000u), SPut32(s.iw, bk + 8, 0x02030000u));
    BREAK_AND_CHECK(13, SPut32(s.iw, bk + 8, 0x0203FFF0u), SPut32(s.iw, bk + 8, 0x02030000u));   /* buffer runs past EWRAM */
    BREAK_AND_CHECK(14, SPut32(s.iw, bk + 0, 8 + 14), SPut32(s.iw, bk + 0, 8 + 15));
    BREAK_AND_CHECK(14, SPut32(s.iw, bk + 4, 6 + 15), SPut32(s.iw, bk + 4, 6 + 14));
    BREAK_AND_CHECK(14, s.ew[0x25000 + 4] = 9, s.ew[0x25000 + 4] = 0);                          /* sb1 names a map the ROM lacks */
    BREAK_AND_CHECK(15, SPut16(s.ew, mh + 0x12, 8), SPut16(s.ew, mh + 0x12, 7));
    BREAK_AND_CHECK(16, s.ew[pa + 5] = 16, s.ew[pa + 5] = 3);
    BREAK_AND_CHECK(16, s.ew[ob + 2] = 0, s.ew[ob + 2] = 1);
    BREAK_AND_CHECK(16, SPut16(s.ew, ob + 0x10, 16), SPut16(s.ew, ob + 0x10, 17));
    BREAK_AND_CHECK(16, SPut16(s.ew, ob + 0x12, 28), SPut16(s.ew, ob + 0x12, 27));
    BREAK_AND_CHECK(17, s.ew[pa + 4] = 65, s.ew[pa + 4] = 2);
    BREAK_AND_CHECK(17, SPut32(s.ew, sp + 0x08, 0x03000000u), SPut32(s.ew, sp + 0x08, 0x08000400u));
    BREAK_AND_CHECK(18, SPut16(s.ew, pf + 4, 17u << 6), SPut16(s.ew, pf + 4, 5u << 6));
    BREAK_AND_CHECK(19, SPut32(s.rom, 0x200, 0x08000000u), SPut32(s.rom, 0x200, 0x02038F00u));
    BREAK_AND_CHECK(19, s.ew[0x38F00 + s.p.weatherOff[0]] = 15, s.ew[0x38F00 + s.p.weatherOff[0]] = 3);
    BREAK_AND_CHECK(19, s.ew[0x38F00 + s.p.weatherOff[1]] = 4, s.ew[0x38F00 + s.p.weatherOff[1]] = 3);
    BREAK_AND_CHECK(20, SPut32(s.iw, gm + 4, 0x08000401u), SPut32(s.iw, gm + 4, s.p.cb2Overworld));   /* thumb ROM pointer, not an overworld callback */
    BREAK_AND_CHECK(20, s.iw[gm + 0x439] = 0x02, s.iw[gm + 0x439] = 0);                        /* in battle */
    rc = vx_anchor_check_ram(&s.p, s.rom, s.n, &r);
    CHECK(rc == 0);
    CHECK(vx_anchor_check_ram(NULL, s.rom, s.n, &r) == 11 && vx_anchor_check_ram(&s.p, s.rom, s.n, NULL) == 11);
    CHECK(vx_anchor_check_ram(gameprof_emerald(), s.rom, s.n, &r) == 11);
    free(s.rom); free(s.ew); free(s.iw);
}

int main(void)
{
    TestFields();
    TestSets();
    TestDetect();
    TestAccessor();
    TestRealRom();
    TestAnchorRowsDiffer();
    TestRomAnchors();
    TestRamChecks();
    printf("test_romgen_gameprof: %d checks, %d failures\n", sChecks, sFails);
    return sFails != 0;
}
