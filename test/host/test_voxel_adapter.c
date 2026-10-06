// test_voxel_adapter.c -- host test for the snapshot + adapter layer (vx_snapshot.c, vx_adapter.c,
// vx_behavior.c). Phase 32 P2, SPEC-port sections 2 and 9.2. Synthetic game state only (vx_fixture.h).
//
//   clang -std=c11 -Wall -Wextra -O2 -fsanitize=address,undefined -I source/voxel -I test/host \
//         test/host/test_voxel_adapter.c source/voxel/vx_adapter.c source/voxel/vx_snapshot.c \
//         source/voxel/vx_lz77.c source/voxel/vx_behavior.c source/voxel/ctr_shims_pure.c source/romgen/rg_gameprof.c \
//         -o /tmp/tvad && /tmp/tvad
//
// Optional: `/tmp/tvad --real DUMPDIR ROM.gba` decodes a real RAM dump made by tools/voxel/probe_ram.c
// (local only, never committed) and prints what the world sees.
#include <stdio.h>
#include <string.h>

#include "vx_fixture.h"
#include "rg_fixture.h"

static int sChecks, sFails;
#define CHECK(c) do { ++sChecks; if (!(c)) { ++sFails; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static int InRom(const void *p)
{
    return (const uint8_t *)p >= fxRom && (const uint8_t *)p < fxRom + FX_ROM_SIZE;
}

static void Retake(void)
{
    VxMemSrc s = fxSrc();
    CHECK(vx_snapshot_take(fxSnap, &s));
}

static void TestHappyPath(void)
{
    const struct MapHeader *h1, *h2;
    const struct MapLayout *lay;

    CHECK(vx_adapter_decode(fxSnap));
    CHECK(vx_adapter_error() == VX_OK);
    CHECK(vx_snap() == fxSnap);
    lay = gMapHeader.mapLayout;
    CHECK(lay != NULL && lay->width == 20 && lay->height == 20);
    CHECK(gMapHeader.mapLayoutId == 1 && gMapHeader.weather == 2 && gMapHeader.mapType == 3);
    CHECK(InRom(lay->map) && InRom(lay->border));
    CHECK(lay->map[0] == (0 | 0x3000) && lay->map[98] == ((98 % 97) | 0x3000));
    CHECK(lay->primaryTileset == &gTileset_General);
    CHECK(lay->secondaryTileset == &gTileset_Fortree);
    CHECK(gTileset_Fortree.isCompressed && gTileset_Fortree.isSecondary && !gTileset_General.isSecondary);
    CHECK(InRom(gTileset_General.tiles) && InRom(gTileset_General.palettes) && InRom(gTileset_General.metatiles)
          && InRom(gTileset_General.metatileAttributes));
    CHECK(gTileset_General.metatiles[9] == 9 && gTileset_Fortree.metatiles[16 * 3] == 48);
    /* asset sizes: raw = 16 KiB, compressed = packed stream length, palettes 512, tables per count */
    CHECK(Port_GetAssetSizeExact(gTileset_General.tiles) == 16384u);
    CHECK(Port_GetAssetSizeExact(gTileset_Fortree.tiles) == fxLz1Len);
    CHECK(Port_GetAssetSizeExact(gTileset_General.palettes) == 512u);
    CHECK(Port_GetAssetSizeExact(gTileset_General.metatiles) == 512u * 16u);
    CHECK(Port_GetAssetSizeExact(gTileset_General.metatileAttributes) == 1024u);
    CHECK(Port_GetAssetSizeExact(NULL) == 0 && Port_GetAssetSizeExact(fxRom + 3) == 0);
    {   /* our LZ decoder reproduces the fixture tiles through the world's call */
        static uint8_t out[16384];
        CHECK(vx_lz77_decode(gTileset_Fortree.tiles, Port_GetAssetSizeExact(gTileset_Fortree.tiles), out, sizeof out) == 16384u);
        CHECK(out[0] == (uint8_t)(0 * 3 + 1) && out[1000] == (uint8_t)(1000 * 3 + 1));
    }
    /* backup map + saveblock + objects */
    CHECK(gBackupMapLayout.width == 35 && gBackupMapLayout.height == 34);
    CHECK(gBackupMapLayout.map[0] == (0 | 0x3000) && gBackupMapLayout.map[100] == ((100 % 89) | 0x3000));
    CHECK(gSaveBlock1Ptr != NULL && gSaveBlock1Ptr->pos.x == 5 && gSaveBlock1Ptr->pos.y == 6);
    CHECK(gPlayerAvatar.objectEventId == 0 && gObjectEvents[0].active && gObjectEvents[0].isPlayer);
    CHECK(gObjectEvents[0].graphicsId == 0x59 && gObjectEvents[0].currentElevation == 3);
    CHECK(gObjectEvents[0].currentCoords.x == 12 && gObjectEvents[0].currentCoords.y == 13);
    CHECK(gObjectEvents[0].facingDirection == 1);
    CHECK(gObjectEvents[1].active && !gObjectEvents[1].isPlayer && gObjectEvents[1].graphicsId == 0x14);
    CHECK(!gObjectEvents[2].active);
    CHECK(gMain.callback2 == CB2_Overworld && !gMain.inBattle);
    /* events + connections */
    CHECK(gMapHeader.events != NULL && gMapHeader.events->objectEventCount == 2 && gMapHeader.events->bgEventCount == 1);
    CHECK(gMapHeader.events->objectEvents[0].graphicsId == 0x14 && gMapHeader.events->objectEvents[1].graphicsId == 0x22);
    CHECK(gMapHeader.events->bgEvents[0].x == 5 && gMapHeader.events->bgEvents[0].y == 9
          && gMapHeader.events->bgEvents[0].elevation == 3 && gMapHeader.events->bgEvents[0].kind == 1);
    CHECK(gMapHeader.connections != NULL && gMapHeader.connections->count == 2);
    CHECK(gMapHeader.connections->connections[0].direction == 2 && gMapHeader.connections->connections[0].offset == 0);
    CHECK(gMapHeader.connections->connections[1].direction == 4 && gMapHeader.connections->connections[1].offset == -2);
    h1 = GetMapHeaderFromConnection(&gMapHeader.connections->connections[0]);
    h2 = GetMapHeaderFromConnection(&gMapHeader.connections->connections[1]);
    CHECK(h1 != NULL && h1 == h2 && h1->mapLayout->width == 16 && h1->mapLayout->secondaryTileset == &gTileset_GenericBuilding);
    CHECK(Port_GetMapLayoutById(2) == h1->mapLayout && Port_GetMapLayoutById(0) == NULL && Port_GetMapLayoutById(1) == lay);
    /* sprites: OAM words decoded by shifts */
    CHECK(gSprites[0].inUse && gSprites[0].oam.y == 0x50 && gSprites[0].oam.shape == 2 && gSprites[0].oam.size == 2);
    CHECK(gSprites[0].oam.x == 0x1AB && gSprites[0].oam.matrixNum == 9);
    CHECK(gSprites[0].oam.tileNum == 0x123 && gSprites[0].oam.priority == 2 && gSprites[0].oam.paletteNum == 7);
    CHECK(gSprites[0].x == 120 && gSprites[0].y == 80);
    CHECK(!CtrSprite_IsVoxelWeather(&gSprites[0]) && CtrSprite_IsVoxelWeather(&gSprites[5]) && !CtrSprite_IsVoxelWeather(&gSprites[6]));
    /* graphics info + images */
    {
        const struct ObjectEventGraphicsInfo *g = GetObjectEventGraphicsInfo(0x59);
        CHECK(g != NULL && g->size == 512 && g->width == 16 && g->height == 32 && g->images != NULL);
        CHECK(g->images[0].size == 256 && InRom(g->images[0].data));
        CHECK(Port_GetSpriteFrameSize(g->images[0].data, g->images[0].size) == 256);
        CHECK(Port_PeekSpriteFramePointer(g->images[0].data, 256, 10) == (const u8 *)g->images[0].data + 10);
        CHECK(Port_PeekSpriteFramePointer(g->images[0].data, 256, 256) == NULL);
        CHECK(GetObjectEventGraphicsInfo(250) == GetObjectEventGraphicsInfo(0));
        CHECK(GetObjectEventGraphicsInfo(0x59) == g);
    }
    /* weather, fade, palettes, field-effect templates */
    CHECK(GetCurrentWeather() == WEATHER_RAIN && gWeatherPtr->palProcessingState == WEATHER_PAL_STATE_IDLE);
    CHECK(gWeatherPtr->currBlendEVA == 9 && gWeatherPtr->fogHSpritesCreated == 0);
    CHECK(gPaletteFade.y == 5 && gPaletteFade.active);
    CHECK(gPlttBufferUnfaded[7] == 7 && vx_snap()->pltt[7] == (7 ^ 0x7FFF));
    CHECK(gFieldEffectObjectTemplatePointers[0] == 0x08700000u && gFieldEffectObjectTemplatePointers[36] == 0x08700000u + 36u * 0x40u);
    /* pointer identity across decodes, and across snapshot changes of RAM-only state */
    {
        const struct MapLayout *l0 = gMapHeader.mapLayout;
        const struct Tileset *t0 = &gTileset_Fortree;
        E16(0x02025A58, 9);
        Retake();
        CHECK(vx_adapter_decode(fxSnap));
        CHECK(gMapHeader.mapLayout == l0 && &gTileset_Fortree == t0 && gSaveBlock1Ptr->pos.x == 9);
        E16(0x02025A58, 5);
        Retake();
    }
}

static int Rejects(VxError want)
{
    int ok = !vx_adapter_decode(fxSnap) && gMapHeader.mapLayout == NULL && gSaveBlock1Ptr == NULL && vx_adapter_error() == want;
    return ok;
}

static void TestGuards(void)
{
    uint8_t save[64];

    /* A1: backup map pointer outside the backup buffer; and unaligned */
    I32(0x03005DC8, 0x02032318 + 0x5000); Retake(); CHECK(Rejects(VX_ERR_A1_MAP_PTR));
    I32(0x03005DC8, 0x02032319); Retake(); CHECK(Rejects(VX_ERR_A1_MAP_PTR));
    I32(0x03005DC8, 0x02032318);
    /* A2: dims zero / huge */
    I32(0x03005DC0, 0); Retake(); CHECK(Rejects(VX_ERR_A2_DIMS));
    I32(0x03005DC0, 5000); Retake(); CHECK(Rejects(VX_ERR_A2_DIMS));
    I32(0x03005DC0, FX_NUM_CELLS_W + 15);
    /* A3: backup dims disagree with the layout (a transition in flight) */
    I32(0x03005DC4, FX_NUM_CELLS_H + 14 + 1); Retake(); CHECK(Rejects(VX_ERR_A3_MISMATCH));
    I32(0x03005DC4, FX_NUM_CELLS_H + 14);
    /* A4: header layout pointer outside the ROM, null, RAM, or past the end */
    memcpy(save, fxEwram + (0x02037318 - 0x02000000), 28);
    E32(0x02037318, 0x02000000); Retake(); CHECK(Rejects(VX_ERR_A4_ROM_PTR));
    E32(0x02037318, 0); Retake(); CHECK(Rejects(VX_ERR_A4_ROM_PTR));
    E32(0x02037318, 0x08000000u + FX_ROM_SIZE - 4); Retake(); CHECK(Rejects(VX_ERR_A4_ROM_PTR));
    memcpy(fxEwram + (0x02037318 - 0x02000000), save, 28);
    /* layout / tileset content: misaligned map pointer, bad tileset flags, bad dims */
    R32(FX_LAYOUT_A + 12, FX_MAP_A + 1);
    vx_adapter_set_rom(fxRom, FX_ROM_SIZE); Retake(); CHECK(Rejects(VX_ERR_A4_ROM_PTR));
    R32(FX_LAYOUT_A + 12, FX_MAP_A);
    fxRom[ROMOFF(FX_TS_FORTREE)] = 7;
    vx_adapter_set_rom(fxRom, FX_ROM_SIZE); Retake(); CHECK(Rejects(VX_ERR_A4_ROM_PTR));
    fxRom[ROMOFF(FX_TS_FORTREE)] = 1;
    R32(FX_TS_FORTREE + 4, 0x08000000u + FX_ROM_SIZE + 0x100); /* tiles outside the ROM */
    vx_adapter_set_rom(fxRom, FX_ROM_SIZE); Retake(); CHECK(Rejects(VX_ERR_A4_ROM_PTR));
    R32(FX_TS_FORTREE + 4, TsData(1));
    fxRom[ROMOFF(TsData(1)) + 0] = 0x20;                       /* LZ header type wrong */
    vx_adapter_set_rom(fxRom, FX_ROM_SIZE); Retake(); CHECK(Rejects(VX_ERR_A4_ROM_PTR));
    fxRom[ROMOFF(TsData(1)) + 0] = 0x10;
    R32(FX_LAYOUT_A + 0, 0); R32(FX_LAYOUT_A + 4, 0);
    vx_adapter_set_rom(fxRom, FX_ROM_SIZE); Retake(); CHECK(Rejects(VX_ERR_A4_ROM_PTR));
    R32(FX_LAYOUT_A + 0, 20); R32(FX_LAYOUT_A + 4, 20);
    vx_adapter_set_rom(fxRom, FX_ROM_SIZE); Retake(); CHECK(vx_adapter_decode(fxSnap));
    /* A9: no player object / not the player / avatar index out of range / no save block */
    E8(0x02037352, 0); Retake(); CHECK(Rejects(VX_ERR_A9_PLAYER));
    E8(0x02037352, 1);
    E8(0x02037350, 0); Retake(); CHECK(Rejects(VX_ERR_A9_PLAYER));
    E8(0x02037350, 1);
    E8(0x02037595, 99); Retake(); CHECK(Rejects(VX_ERR_A9_PLAYER));
    E8(0x02037595, 0);
    I32(0x03005D8C, 0x04000000); Retake(); CHECK(Rejects(VX_ERR_A9_PLAYER));
    I32(0x03005D8C, 0x02025A58);
    /* decoded fine but not an overworld callback: true, with the reason recorded */
    I32(0x030022C4, 0x08000301); Retake();
    CHECK(vx_adapter_decode(fxSnap) && vx_adapter_error() == VX_ERR_CB2 && gMain.callback2 == 0x08000301u);
    I32(0x030022C4, 0x08085E51); Retake();
    CHECK(vx_adapter_decode(fxSnap) && vx_adapter_error() == VX_OK);
    I32(0x030022C4, 0x08085E5D);
    /* in-battle bit */
    fxIwram[0x22C0 + 0x439] = 2; Retake();
    CHECK(vx_adapter_decode(fxSnap) && gMain.inBattle);
    fxIwram[0x22C0 + 0x439] = 0;
    /* an invalid snapshot, a NULL snapshot, no ROM */
    Retake();
    fxSnap->valid = false;
    CHECK(!vx_adapter_decode(fxSnap) && vx_adapter_error() == VX_ERR_SNAPSHOT && gMapHeader.mapLayout == NULL);
    CHECK(!vx_adapter_decode(NULL) && vx_adapter_error() == VX_ERR_SNAPSHOT);
    Retake();
    vx_adapter_set_rom(NULL, 0);
    CHECK(!vx_adapter_decode(fxSnap) && vx_adapter_error() == VX_ERR_NO_ROM && gMapHeader.mapLayout == NULL);
    CHECK(GetObjectEventGraphicsInfo(1) == NULL && Port_GetMapLayoutById(1) == NULL);
    vx_adapter_set_rom(fxRom, FX_ROM_SIZE);
    CHECK(vx_adapter_decode(fxSnap));
}

static void TestArenaDoesNotLeakOnBadData(void)
{
    /* A rejected map decoded 5000 times must not exhaust the arena: the good map still decodes. */
    fxRom[ROMOFF(TsData(1))] = 0x20;
    vx_adapter_set_rom(fxRom, FX_ROM_SIZE);
    for (int i = 0; i < 5000; ++i)
        if (vx_adapter_decode(fxSnap)) { CHECK(0); break; }
    fxRom[ROMOFF(TsData(1))] = 0x10;
    CHECK(vx_adapter_decode(fxSnap) && vx_adapter_error() == VX_OK);
    for (int i = 0; i < 5000; ++i)
        if (!vx_adapter_decode(fxSnap)) { CHECK(0); break; }
    CHECK(gMapHeader.mapLayout != NULL);
}

static void TestBehaviour(void)
{
    CHECK(MetatileBehavior_IsReflective(0x10) && MetatileBehavior_IsReflective(0x16) && MetatileBehavior_IsReflective(0x1A)
          && MetatileBehavior_IsReflective(0x20) && MetatileBehavior_IsReflective(0x14) && MetatileBehavior_IsReflective(0x2B));
    CHECK(!MetatileBehavior_IsReflective(0x00) && !MetatileBehavior_IsReflective(0x11) && !MetatileBehavior_IsReflective(0x83));
    CHECK(MetatileBehavior_IsIce(0x20) && !MetatileBehavior_IsIce(0x21));
    CHECK(MetatileBehavior_IsPuddle(0x16) && !MetatileBehavior_IsPuddle(0x17));
    CHECK(MetatileBehavior_IsPC(0x83) && !MetatileBehavior_IsPC(0x84) && !MetatileBehavior_IsPC(0x85));
    CHECK(MetatileBehavior_IsShallowFlowingWater(0x17) && MetatileBehavior_IsShallowFlowingWater(0x1B)
          && MetatileBehavior_IsShallowFlowingWater(0x1C) && !MetatileBehavior_IsShallowFlowingWater(0x18));
    CHECK(MB_CABLE_BOX_RESULTS_1 == 0x84 && MB_TELEVISION == 0x86 && MB_SECRET_BASE_REGISTER_PC == 0xB1 && MB_HOT_SPRINGS == 0x28);
    CHECK(MetatileBehavior_IsSecretBasePC(0xB0) && !MetatileBehavior_IsSecretBasePC(0xB1) && !MetatileBehavior_IsSecretBasePC(0xB2));
    CHECK(MetatileBehavior_IsPlayerRoomPCOn(0xC5) && !MetatileBehavior_IsPlayerRoomPCOn(0xC4));
    CHECK(MetatileBehavior_IsCounter(0x80) && !MetatileBehavior_IsCounter(0x81));
    int n = 0;
    for (unsigned b = 0; b < 256; ++b) n += MetatileBehavior_IsReflective((u8)b);
    CHECK(n == 6);
}

/* ---- Phase 34 R2: the FireRed / LeafGreen additions ---- */
static const GameProfile *FrlgProfile(GpGame g)
{
    static uint8_t hdr[0x100];

    memset(hdr, 0, sizeof hdr);
    memcpy(hdr + 0xAC, g == GP_FIRERED ? "BPRE" : "BPGE", 4);
    hdr[0xBC] = 1;
    return gameprof_detect(hdr, sizeof hdr);
}

static void TestIntern32(void)
{
    /* little-endian words: behaviour 0x69 / layer 0; layer 1; layer 2 + behaviour 0x1FF; layer 3 + bits 8-9; junk bits */
    static const uint32_t in[] = {0x00000069u, 0x20000069u, 0x400001FFu, 0x60000169u, 0x1FFFFE00u, 0x80000000u, 0xFFFFFFFFu, 0};
    static const uint16_t want[] = {0x0069, 0x1069, 0x21FF, 0x3169, 0x0000, 0x0000, 0x31FF, 0x0000};
    uint8_t raw[sizeof in];
    uint16_t out[8];

    for (unsigned i = 0; i < 8; ++i)
        for (unsigned b = 0; b < 4; ++b)
            raw[4 * i + b] = (uint8_t)(in[i] >> (8 * b));
    gVxProf = FrlgProfile(GP_FIRERED);
    CHECK(gVxProf != NULL && gVxProf->game == GP_FIRERED);
    vx_intern_attrs32(raw, 8, out);
    for (unsigned i = 0; i < 8; ++i) CHECK(out[i] == want[i]);
    /* bits 8-9 survive (behaviour is 9 bits on FRLG), bit 31 and bits 10-28 never leak in */
    CHECK((out[3] & 0x1FFu) == 0x169u && (out[4] & 0x1FFu) == 0 && (out[7] & 0xFFFFu) == 0);
    gVxProf = FrlgProfile(GP_LEAFGREEN);
    vx_intern_attrs32(raw, 8, out);
    for (unsigned i = 0; i < 8; ++i) CHECK(out[i] == want[i]);
    gVxProf = NULL;
    vx_intern_attrs32(raw, 8, out);   /* the Emerald mask (0xFF) if anyone ever called it there: behaviour 8 bits only */
    CHECK(out[0] == 0x0069 && out[2] == 0x20FF && out[3] == 0x3069);
}

static void TestBorderAndProfileHelpers(void)
{
    struct MapLayout l22 = {0}, l32 = {0}, l11 = {0}, lnone = {0};
    int phaseOk = 1;

    l22.borderWidth = l22.borderHeight = 2;
    l32.borderWidth = 3; l32.borderHeight = 2;
    l11.borderWidth = l11.borderHeight = 1;   /* a 0x0 border is stored as one cell */
    CHECK(vx_border_cells(&l22) == 4 && vx_border_cells(&l32) == 6 && vx_border_cells(&l11) == 1 && vx_border_cells(&lnone) == 4);
    /* 2x2 (and "unset") reproduce the Emerald indexing ((bx + 1) & 1) + ((by + 1) & 1) * 2 for every cell */
    for (int by = -40; by < 80; ++by)
        for (int bx = -40; bx < 80; ++bx)
        {
            int old = ((bx + 1) & 1) + (((by + 1) & 1) * 2);
            if (vx_border_cell(&l22, bx, by) != old || vx_border_cell(&lnone, bx, by) != old) phaseOk = 0;
        }
    CHECK(phaseOk);
    /* 3x2: period 3 in x, 2 in y, every cell reachable, always inside [0, 6) */
    {
        int seen[6] = {0}, inRange = 1, periodic = 1;

        for (int by = -30; by < 30; ++by)
            for (int bx = -30; bx < 30; ++bx)
            {
                int c = vx_border_cell(&l32, bx, by);
                if (c < 0 || c >= 6) inRange = 0; else seen[c] = 1;
                if (vx_border_cell(&l32, bx + 3, by) != c || vx_border_cell(&l32, bx, by + 2) != c) periodic = 0;
            }
        CHECK(inRange && periodic);
        for (int c = 0; c < 6; ++c) CHECK(seen[c]);
        /* cell (MAP_OFFSET, MAP_OFFSET) is the layout's own origin: border cell 0; the next column / row is the next cell */
        CHECK(vx_border_cell(&l32, MAP_OFFSET, MAP_OFFSET) == 0 && vx_border_cell(&l32, MAP_OFFSET + 1, MAP_OFFSET) == 1
              && vx_border_cell(&l32, MAP_OFFSET + 2, MAP_OFFSET) == 2 && vx_border_cell(&l32, MAP_OFFSET, MAP_OFFSET + 1) == 3);
    }
    for (int by = -9; by < 9; ++by)
        for (int bx = -9; bx < 9; ++bx) CHECK(vx_border_cell(&l11, bx, by) == 0);

    CHECK(strcmp(vx_profile_data_dir(NULL), "sdmc:/3ds/3DGBA/voxel") == 0);
    CHECK(strcmp(vx_profile_data_dir(gameprof_emerald()), "sdmc:/3ds/3DGBA/voxel") == 0);
    CHECK(strcmp(vx_profile_data_dir(FrlgProfile(GP_FIRERED)), "sdmc:/3ds/3DGBA/voxel/BPRE") == 0);
    CHECK(strcmp(vx_profile_data_dir(FrlgProfile(GP_LEAFGREEN)), "sdmc:/3ds/3DGBA/voxel/BPGE") == 0);
    CHECK(strcmp(vx_profile_pak_path(gameprof_emerald()), "sdmc:/3ds/emerald3ds/emerald3ds.pak") == 0);
    CHECK(strstr(vx_profile_pak_path(FrlgProfile(GP_FIRERED)), "/BPRE/") != NULL);
    for (unsigned t = 0; t < 16; ++t)
        CHECK(vx_map_is_outdoor(t) == (t == 1 || t == 2 || t == 3 || t == 5 || t == 6));

    /* the renderer's behaviour predicates follow the active profile (Emerald by default) */
    gVxProf = FrlgProfile(GP_FIRERED);
    CHECK(MetatileBehavior_IsSurfableWaterOrUnderwater(0x15) && MetatileBehavior_IsSurfableWaterOrUnderwater(0x1B)
          && !MetatileBehavior_IsSurfableWaterOrUnderwater(0x14) && !MetatileBehavior_IsSurfableWaterOrUnderwater(0x2A)
          && !MetatileBehavior_IsSurfableWaterOrUnderwater(0x6C));
    CHECK(MetatileBehavior_IsReflective(0x16) && MetatileBehavior_IsReflective(0x23) && !MetatileBehavior_IsReflective(0x14)
          && !MetatileBehavior_IsReflective(0x2B));
    CHECK(MetatileBehavior_IsIce(0x23) && !MetatileBehavior_IsIce(0x20));
    CHECK(MetatileBehavior_IsShallowFlowingWater(0x17) && !MetatileBehavior_IsShallowFlowingWater(0x1B));
    gVxProf = NULL;
    CHECK(MetatileBehavior_IsSurfableWaterOrUnderwater(0x14) && MetatileBehavior_IsIce(0x20) && MetatileBehavior_IsReflective(0x2B));
}

/* The real ROMs, when ROMGEN_ROM_FR / _LG are set: every layout's border size (SPEC-P34 1.5: 2x2, 3x2 and 0x0). */
static void TestRealLayouts(const char *env, GpGame game, int want22, int want32, int want00)
{
    size_t n = 0;
    uint8_t *rom = fxr_load_rom(env, &n);
    int c22 = 0, c32 = 0, c00 = 0, bad = 0;

    if (rom == NULL) { printf("SKIP %s: ROM not found\n", env); return; }
    gVxProf = gameprof_detect(rom, n);
    CHECK(gVxProf != NULL && gVxProf->game == game);
    if (gVxProf == NULL) { free(rom); return; }
    vx_adapter_set_rom(rom, n);
    for (unsigned id = 1; id <= gVxProf->layoutSlots; ++id)
    {
        const struct MapLayout *l = Port_GetMapLayoutById((u16)id);

        if (l == NULL) { ++bad; continue; }
        if (l->borderWidth == 2 && l->borderHeight == 2) ++c22;
        else if (l->borderWidth == 3 && l->borderHeight == 2) ++c32;
        else if (l->borderWidth == 1 && l->borderHeight == 1) { ++c00; CHECK(l->border[0] == 0); }
        else CHECK(0);
    }
    printf("  %s: layouts 2x2 %d, 3x2 %d, 0x0 %d, unreadable %d\n", env, c22, c32, c00, bad);
    /* SURVEY: 384 slots, 18 NULL, and 331 / 7 / 28 by border size. The adapter refuses layout 24 (a non-NULL slot with a
     * NULL primary tileset: unused dev data), so 330 + 7 + 28 are readable and 19 slots are not. */
    CHECK(c22 == want22 && c32 == want32 && c00 == want00 && bad == 19);
    vx_adapter_set_rom(NULL, 0);
    gVxProf = NULL;
    free(rom);
}

static uint8_t *Slurp(const char *dir, const char *name, size_t *n)
{
    char path[512];
    FILE *f;
    uint8_t *b;

    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = fopen(path, "rb");
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END); *n = (size_t)ftell(f); rewind(f);
    b = malloc(*n);
    if (fread(b, 1, *n, f) != *n) { fclose(f); free(b); return NULL; }
    fclose(f);
    return b;
}

/* --real DUMPDIR ROM: decode a real RAM dump (never committed). Returns the process exit code. */
static int RealDump(const char *dir, const char *romPath)
{
    size_t rn, en, in, pn, vn;
    uint8_t *rom, *ew, *iw, *pl, *vr;
    VxMemSrc src;
    char path[512];
    FILE *f;
    VxSnapshot *snap = calloc(1, sizeof(*snap));

    snprintf(path, sizeof path, "%s", romPath);
    f = fopen(path, "rb");
    if (f == NULL) { printf("no ROM %s\n", romPath); return 2; }
    fseek(f, 0, SEEK_END); rn = (size_t)ftell(f); rewind(f);
    rom = malloc(rn);
    if (fread(rom, 1, rn, f) != rn) return 2;
    fclose(f);
    ew = Slurp(dir, "ewram.bin", &en); iw = Slurp(dir, "iwram.bin", &in);
    pl = Slurp(dir, "pltt.bin", &pn); vr = Slurp(dir, "vram.bin", &vn);
    if (!ew || !iw || !pl || !vr) { printf("incomplete dump in %s\n", dir); return 2; }
    src = (VxMemSrc){ew, iw, pl, vr, 0, 0, 0, 0};
    fxRom = rom;
    CHECK(vx_snapshot_take(snap, &src));
    vx_adapter_set_rom(rom, rn);
    CHECK(vx_adapter_decode(snap));
    printf("real dump %s: err=%d layout %dx%d primary=%p secondary=%p conns=%d weather=%d type=%d player gfx=0x%02x at (%d,%d)\n",
           dir, (int)vx_adapter_error(), gMapHeader.mapLayout ? gMapHeader.mapLayout->width : -1,
           gMapHeader.mapLayout ? gMapHeader.mapLayout->height : -1,
           gMapHeader.mapLayout ? (const void *)gMapHeader.mapLayout->primaryTileset : NULL,
           gMapHeader.mapLayout ? (const void *)gMapHeader.mapLayout->secondaryTileset : NULL,
           gMapHeader.connections ? gMapHeader.connections->count : 0, gMapHeader.weather, gMapHeader.mapType,
           gObjectEvents[gPlayerAvatar.objectEventId].graphicsId, gObjectEvents[gPlayerAvatar.objectEventId].currentCoords.x,
           gObjectEvents[gPlayerAvatar.objectEventId].currentCoords.y);
    CHECK(vx_adapter_error() == VX_OK && gMapHeader.mapLayout != NULL);
    if (gMapHeader.mapLayout != NULL)
    {
        const struct Tileset *t = gMapHeader.mapLayout->secondaryTileset;
        CHECK(Port_GetAssetSizeExact(t->tiles) > 0 && Port_GetAssetSizeExact(t->metatiles) > 0);
        CHECK(GetObjectEventGraphicsInfo(gObjectEvents[gPlayerAvatar.objectEventId].graphicsId) != NULL);
    }
    printf("test_voxel_adapter --real: %d checks, %d failures\n", sChecks, sFails);
    return sFails != 0;
}

int main(int argc, char **argv)
{
    if (argc == 4 && strcmp(argv[1], "--real") == 0)
        return RealDump(argv[2], argv[3]);
    CHECK(fxInit() == 0);
    TestHappyPath();
    TestBehaviour();
    TestGuards();
    TestArenaDoesNotLeakOnBadData();
    TestIntern32();
    TestBorderAndProfileHelpers();
    TestRealLayouts(FXR_ENV_FR, GP_FIRERED, 330, 7, 28);
    TestRealLayouts(FXR_ENV_LG, GP_LEAFGREEN, 330, 7, 28);
    printf("test_voxel_adapter: %d checks, %d failures\n", sChecks, sFails);
    return sFails != 0;
}
