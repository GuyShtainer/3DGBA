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
    printf("test_voxel_adapter: %d checks, %d failures\n", sChecks, sFails);
    return sFails != 0;
}
