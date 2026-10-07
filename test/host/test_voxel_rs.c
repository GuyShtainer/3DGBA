// test_voxel_rs.c -- Phase 35 S2: the renderer path on the real Ruby / Sapphire rev 2 ROMs. The adapter builds
// Littleroot Town (group 0, map 9) and Rustboro City (0, 3) from the ROM with NO data files, through the RS profile:
// SaveBlock1 read directly (no pointer), the object events in IWRAM, the weatherPtr deref, the live backup-map
// pointer, map connections and the world's cell queries.
// Real ROMs come from ROMGEN_ROM_RUBY / ROMGEN_ROM_SAPP (absolute paths) or ruby.gba / sapphire.gba beside
// ROMGEN_ROM; a missing ROM prints SKIP (the gate sets the variables, so no SKIP may appear there). No game bytes
// are committed.
//
//   make -C tools/romgen vtest   (builds this with the other world suites, run from the repo root)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rg_fixture.h"
#include "vx_fixture_frlg.h"
#include "voxel_world.h"
#include "voxel_tree.h"

static int sChecks, sFails, sSkips;
#define CHECK(c) do { ++sChecks; if (!(c)) { ++sFails; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

/* One town: header from the ROM, a live-state image with the player at (px, py), snapshot -> adapter -> world. */
static void Town(const char *label, const uint8_t *rom, const GameProfile *prof, unsigned num, int w, int h, int px,
                 int py, unsigned wantInstances, int *doorsOut)
{
    struct MapConnection conn = {0, 0, 0, (u8)num};
    const struct MapHeader *hdr = GetMapHeaderFromConnection(&conn);
    const struct MapLayout *lay;
    FrState st;
    int doors = 0;

    CHECK(hdr != NULL && hdr->mapLayout != NULL);
    if (hdr == NULL || hdr->mapLayout == NULL)
        return;
    lay = hdr->mapLayout;
    CHECK(lay->width == w && lay->height == h);
    CHECK(lay->borderWidth == 2 && lay->borderHeight == 2 && lay->border != NULL);   /* Emerald's 24-byte layout: 2x2 */
    CHECK(vx_map_is_outdoor(hdr->mapType));
    CHECK(lay->primaryTileset == &gTileset_General && lay->secondaryTileset != NULL);
    CHECK(Port_GetAssetSizeExact(lay->primaryTileset->metatileAttributes) == 512u * 2u);   /* u16 attributes, zero-copy */

    FrBuildAt(&st, rom, prof, hdr, 0, num, px, py);
    {
        VxMemSrc src = {st.ewram, st.iwram, st.pltt, st.vram, 0x1040, 0, 0, 0};
        VxSnapshot *snap = calloc(1, sizeof *snap);

        CHECK(snap != NULL && vx_snapshot_take(snap, &src));
        CHECK(snap->sb1Valid && snap->sb1Ptr == prof->sb1Ptr);                /* direct: the struct's own address */
        CHECK(snap->sb1[4] == 0 && snap->sb1[5] == num);
        CHECK(snap->backupMapBase == FR_BACKUP_MAP && snap->backupMapCells == (unsigned)((w + 15) * (h + 14)));
        CHECK(snap->weather[0] == 2 && snap->weather[1] == 3 && snap->weather[2] == 9);   /* through weatherPtr */
        CHECK(snap->objEvents[0] == 0x01 && snap->objEvents[2] == 0x01);     /* copied from IWRAM */
        CHECK(!snap->inBattle);
        CHECK(vx_adapter_decode(snap) && vx_adapter_error() == VX_OK);
        CHECK(gBackupMapLayout.map == snap->backupMap && gBackupMapLayout.width == w + 15 && gBackupMapLayout.height == h + 14);
        CHECK(gMapHeader.mapLayout == lay && gMapHeader.mapLayoutId == hdr->mapLayoutId);
        CHECK(gPlayerAvatar.objectEventId == 0 && gObjectEvents[0].isPlayer && gObjectEvents[0].currentCoords.x == px + 7);
        CHECK(GetCurrentWeather() == 2);
        VoxelWorld_BeginBatch();
        VoxelWorld_BuildInstances();
        CHECK(VoxelWorld_IsMapAvailable());
        CHECK(VoxelWorld_InstanceCount() == wantInstances);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                if (VoxelWorld_GetMetatileBehavior(x, y) == 0x69u)
                    ++doors;
        CHECK(VoxelWorld_BorderMetatile(-2, -2) >= 0 && VoxelWorld_BorderMetatile(w + 6, h + 6) >= 0);
        CHECK(VoxelWorld_Instance(0) != NULL && VoxelWorld_Instance(0)->mapGroup == 0 && VoxelWorld_Instance(0)->mapNum == (int)num);
        printf("  %s: %dx%d, %u instances (", label, w, h, VoxelWorld_InstanceCount());
        for (unsigned k = 0; k < VoxelWorld_InstanceCount(); ++k)
            printf("%s%d.%d", k ? " " : "", VoxelWorld_Instance(k)->mapGroup, VoxelWorld_Instance(k)->mapNum);
        printf("), %d door cells (0x69)\n", doors);
        free(snap);
    }
    *doorsOut = doors;
    FrFree(&st);
}

static void RunGame(const char *env, GpGame game, const char *code, const char *label)
{
    size_t n = 0;
    uint8_t *rom = fxr_load_rom(env, &n);
    const GameProfile *prof;
    int doorsL = 0, doorsR = 0;

    if (rom == NULL) { printf("SKIP %s: ROM not found (%s)\n", label, env); ++sSkips; return; }
    prof = gameprof_detect(rom, n);   /* S2: the renderer entry returns the RS rows */
    CHECK(prof != NULL && prof->game == game && memcmp(prof->code, code, 4) == 0);
    if (prof == NULL) { free(rom); return; }
    CHECK(prof->sb1Direct && prof->mainFlagsOff == 0x43Du && !prof->interiors3d && !prof->emeraldIdTables);
    gVxProf = prof;
    vx_adapter_set_rom(rom, n);
    /* Instances = the map, its connections and theirs (voxel_world.c BuildInstances): Littleroot + Route 101 + Oldale, as
     * Emerald's Littleroot in test_voxel_world; Rustboro + its three neighbours + the four maps one crossing further. */
    Town(label, rom, prof, 9, 20, 20, 10, 10, 3, &doorsL);
    CHECK(doorsL > 0);
    Town(label, rom, prof, 3, 40, 60, 16, 30, 8, &doorsR);
    CHECK(doorsR > doorsL);
    gVxProf = NULL;
    vx_adapter_set_rom(NULL, 0);
    free(rom);
}

int main(void)
{
    RunGame(FXR_ENV_RUBY, GP_RUBY, "AXVE", "Ruby");
    RunGame(FXR_ENV_SAPP, GP_SAPPHIRE, "AXPE", "Sapphire");
    printf("test_voxel_rs: %d checks, %d failures, %d skipped\n", sChecks, sFails, sSkips);
    return sFails != 0;
}
