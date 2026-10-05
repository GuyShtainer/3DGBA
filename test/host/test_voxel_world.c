// test_voxel_world.c -- host test for the vendored world module (voxel_world.c) running on top of our
// adapter: phase 32 P2, SPEC-port section 9.2. Synthetic state only (vx_fixture.h).
//
//   clang -std=c11 -Wall -Wextra -O2 -fsanitize=address,undefined -DVOXEL_HOST_FILES -DCTR_VOXEL_LIGHTING=1 \
//         -I source/voxel -I test/host test/host/test_voxel_world.c \
//         source/voxel/voxel_world.c source/voxel/voxel_regions.c source/voxel/voxel_relief.c \
//         source/voxel/voxel_building.c source/voxel/voxel_sign.c source/voxel/voxel_arena.c \
//         source/voxel/voxel_atlas.c source/voxel/voxel_mesh_builder.c source/voxel/voxel_tree.c \
//         source/voxel/voxel_lighting.c source/voxel/voxel_grade.c source/voxel/voxel_entities.c \
//         source/voxel/voxel_camera.c source/voxel/vx_adapter.c source/voxel/vx_snapshot.c \
//         source/voxel/vx_lz77.c source/voxel/vx_behavior.c source/voxel/vx_data.c \
//         source/voxel/ctr_shims_pure.c source/voxel/vx_battle_stub.c -lm -o /tmp/tvwo && /tmp/tvwo
//
// (the world module drags the mesh/atlas modules in at link time; ctr_voxel.c is the only GPU file and
// is never host-built.)
#include <math.h>
#include <stdio.h>

#include "vx_fixture.h"
#include "voxel_world.h"

static int sChecks, sFails;
#define CHECK(c) do { ++sChecks; if (!(c)) { ++sFails; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static void Take(void)
{
    VxMemSrc s = fxSrc();
    CHECK(vx_snapshot_take(fxSnap, &s));
    CHECK(vx_adapter_decode(fxSnap));
    VoxelWorld_BeginBatch();
    VoxelWorld_BuildInstances();
}

static unsigned BackupIdx(int worldX, int worldY)
{
    return (unsigned)(worldX + 7) + 35u * (unsigned)(worldY + 7);
}

static void TestInstances(void)
{
    const VoxelMapInstance *i0, *n, *e;
    float px, pz;
    int g, m, w, h;

    Take();
    CHECK(VoxelWorld_IsMapAvailable());
    CHECK(VoxelWorld_InstanceCount() == 3);
    i0 = VoxelWorld_Instance(0); n = VoxelWorld_Instance(1); e = VoxelWorld_Instance(2);
    CHECK(i0 != NULL && n != NULL && e != NULL && VoxelWorld_Instance(3) == NULL);
    CHECK(i0->originX == 0 && i0->originY == 0 && i0->width == 20 && i0->height == 20 && !i0->indoor);
    CHECK(i0->layoutId == 1 && i0->primaryTileset == &gTileset_General && i0->secondaryTileset == &gTileset_Fortree);
    CHECK(n->originX == 0 && n->originY == -16 && n->width == 16 && n->height == 16 && n->layoutId == 2);   /* north, offset 0 */
    CHECK(e->originX == 20 && e->originY == -2 && e->secondaryTileset == &gTileset_GenericBuilding);        /* east, offset -2 */
    CHECK(VoxelWorld_GetInstanceAt(0, 0) == i0 && VoxelWorld_GetInstanceAt(19, 19) == i0);
    CHECK(VoxelWorld_GetInstanceAt(5, -3) == n && VoxelWorld_GetInstanceAt(25, 0) == e);
    CHECK(VoxelWorld_GetInstanceAt(-1, 0) == NULL && VoxelWorld_GetInstanceAt(0, 21) == NULL);
    /* cells: the current map reads the live backup grid, neighbours the ROM layout */
    CHECK(VoxelWorld_GetMetatileId(0, 0) == (int)(BackupIdx(0, 0) % 89));
    CHECK(VoxelWorld_GetMetatileId(3, 4) == (int)(BackupIdx(3, 4) % 89));
    CHECK(VoxelWorld_GetMetatileId(3, -4) == (int)(((12 * 16 + 3) % 97) & 0x3FF));       /* B local (3,12) */
    CHECK(VoxelWorld_GetMetatileId(21, 1) == (int)(((3 * 16 + 1) % 97) & 0x3FF));        /* B local (1,3) */
    CHECK(VoxelWorld_GetCollision(3, 4) == 0);
    CHECK(VoxelWorld_UsesTreeSprites(i0) && !VoxelWorld_UsesTreeSprites(NULL));
    VoxelWorld_GetPlayerWorldCoords(&px, &pz);
    CHECK(px == 5.0f && pz == 6.0f);
    VoxelWorld_GetLocation(&g, &m);
    CHECK(g == 0 && m == 0);
    VoxelWorld_GetMapDimensions(&w, &h);
    CHECK(w == 20 && h == 20);
    CHECK(!VoxelWorld_Underground());
}

static void TestAvailability(void)
{
    Take();
    CHECK(VoxelWorld_IsMapAvailable());
    I32(0x030022C4, 0x08000301); Take();
    CHECK(!VoxelWorld_IsMapAvailable());                       /* decode fine, callback is not the overworld */
    I32(0x030022C4, 0x08085E51); Take();
    CHECK(VoxelWorld_IsMapAvailable());                        /* CB2_OverworldBasic counts */
    I32(0x030022C4, 0x08085E5D);
    I32(0x03005D8C, 0x04000000);
    { VxMemSrc s = fxSrc(); CHECK(vx_snapshot_take(fxSnap, &s)); }
    CHECK(!vx_adapter_decode(fxSnap)); VoxelWorld_BuildInstances();
    CHECK(!VoxelWorld_IsMapAvailable() && VoxelWorld_InstanceCount() == 0);   /* a rejected map drops to flat */
    I32(0x03005D8C, 0x02025A58);
    fxIwram[0x22C0 + 0x439] = 2; Take();
    CHECK(VoxelWorld_IsBattleMapAvailable());
    fxIwram[0x22C0 + 0x439] = 0; Take();
    CHECK(!VoxelWorld_IsBattleMapAvailable());
}

static void TestWeatherAndFade(void)
{
    float amount, rgb[3];
    static const struct { int id; VoxelWeatherClass cls; } map[] = {
        {WEATHER_NONE, VOXEL_WEATHER_CLEAR}, {WEATHER_SUNNY_CLOUDS, VOXEL_WEATHER_SHADE}, {WEATHER_SUNNY, VOXEL_WEATHER_SUN},
        {WEATHER_RAIN, VOXEL_WEATHER_RAIN}, {WEATHER_SNOW, VOXEL_WEATHER_PARTICLES}, {WEATHER_RAIN_THUNDERSTORM, VOXEL_WEATHER_RAIN},
        {WEATHER_FOG_HORIZONTAL, VOXEL_WEATHER_FOG}, {WEATHER_VOLCANIC_ASH, VOXEL_WEATHER_PARTICLES},
        {WEATHER_SANDSTORM, VOXEL_WEATHER_PARTICLES}, {WEATHER_FOG_DIAGONAL, VOXEL_WEATHER_FOG},
        {WEATHER_UNDERWATER, VOXEL_WEATHER_FOG}, {WEATHER_SHADE, VOXEL_WEATHER_SHADE}, {WEATHER_DROUGHT, VOXEL_WEATHER_SUN},
        {WEATHER_DOWNPOUR, VOXEL_WEATHER_RAIN}, {WEATHER_UNDERWATER_BUBBLES, VOXEL_WEATHER_FOG}};

    for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); ++i)
    {
        E8(0x02038454 + 0x6D0, (uint32_t)map[i].id); Take();
        CHECK(VoxelWorld_Weather() == map[i].cls);
    }
    E8(0x02038454 + 0x6D0, WEATHER_RAIN);
    /* fog density: sprites created -> EVA/12, clamped; none -> 0 */
    E8(0x02038454 + 0x730, 6); E8(0x02038454 + 0x6FB, 0); Take();
    CHECK(VoxelWorld_FogDensity() == 0.0f);
    E8(0x02038454 + 0x6FB, 1); Take();
    CHECK(fabsf(VoxelWorld_FogDensity() - 0.5f) < 1e-6f);
    E8(0x02038454 + 0x730, 40); Take();
    CHECK(VoxelWorld_FogDensity() == 1.0f);
    E8(0x02038454 + 0x6FB, 0); E8(0x02038454 + 0x6FB + 0x29, 1); Take();   /* fogD at 0x724 */
    CHECK(VoxelWorld_FogDensity() == 1.0f);
    E8(0x02038454 + 0x6FB + 0x29, 0);
    /* screen fade: active fade with shown != unfaded reports; idle with nothing fading does not */
    Take();
    CHECK(VoxelWorld_ScreenFade(&amount, rgb) && amount > 0.0f);
    E16(0x02037FD4 + 4, 0); E16(0x02037FD4 + 6, 0); Take();
    CHECK(!VoxelWorld_ScreenFade(&amount, rgb) && amount == 0.0f);        /* palettes differ but nothing is fading */
    E8(0x02038454 + 0x6C6, 1); Take();                                      /* weather fading the screen in */
    CHECK(VoxelWorld_ScreenFade(&amount, rgb));
    E8(0x02038454 + 0x6C6, 3);
    for (int i = 0; i < 512; ++i) P16(fxPltt, 2u * (uint32_t)i, (uint32_t)i); /* shown == unfaded */
    E16(0x02037FD4 + 6, 0x8000); Take();
    CHECK(!VoxelWorld_ScreenFade(&amount, rgb));
    E16(0x02037FD4 + 4, 5u << 6);
    for (int i = 0; i < 512; ++i) P16(fxPltt, 2u * (uint32_t)i, (uint32_t)i ^ 0x7FFF);
}

static void TestReflectionAndFortree(void)
{
    /* attribute index 0x88 (metatile 0x288 in a secondary tileset) = behaviour 0x16 (puddle);
     * index 0x74 (metatile 0x274) = puddle too. */
    P16(fxRom, ROMOFF(TsData(1) + 0x7000) + 2 * 0x88, 0x16);
    P16(fxRom, ROMOFF(TsData(1) + 0x7000) + 2 * 0x74, 0x16);
    P16(fxRom, ROMOFF(TsData(2) + 0x7000) + 2 * 0x88, 0x16);
    E16(0x02032318u + 2u * BackupIdx(1, 1), 0x288 | 0x3000);
    E16(0x02032318u + 2u * BackupIdx(2, 1), 0x274 | 0x3000);
    E16(0x02032318u + 2u * BackupIdx(3, 1), 0x010 | 0x3000);
    R16(FX_MAP_B + 2 * (2 * 16 + 2), 0x288);                       /* B local (2,2) = world (2,-14) */
    Take();
    CHECK(VoxelWorld_GetMetatileBehavior(1, 1) == 0x16);
    CHECK(!VoxelWorld_IsVisibleReflectiveSurface(1, 1));            /* Fortree 0x288 puddle: grass art */
    CHECK(VoxelWorld_IsVisibleReflectiveSurface(2, 1));             /* Fortree 0x274: blue puddle keeps reflection */
    CHECK(!VoxelWorld_IsVisibleReflectiveSurface(3, 1));            /* ordinary tile */
    CHECK(VoxelWorld_IsVisibleReflectiveSurface(2, -14));           /* same id in a non-Fortree tileset reflects */
    CHECK(!VoxelWorld_IsVisibleReflectiveSurface(-5, -5));          /* outside every map */
}

static void TestHashes(void)
{
    uint32_t live0, blk0, blkFar0;

    E16(0x02032318u + 2u * BackupIdx(1, 1), 0x010 | 0x3000);       /* back to a plain tile */
    Take();
    live0 = VoxelWorld_LiveDigest();
    blk0 = VoxelWorld_BlockHash(0, 0, 8, 8);
    blkFar0 = VoxelWorld_BlockHash(10, 10, 18, 18);
    CHECK(live0 != 0 && VoxelWorld_LiveDigest() == live0);
    Take();
    CHECK(VoxelWorld_LiveDigest() == live0 && VoxelWorld_BlockHash(0, 0, 8, 8) == blk0);       /* identical snapshot */
    E16(0x02032318u + 2u * BackupIdx(2, 2), 0x055 | 0x3000);                                    /* a door/script edit */
    Take();
    CHECK(VoxelWorld_LiveDigest() != live0);
    CHECK(VoxelWorld_BlockHash(0, 0, 8, 8) != blk0);
    CHECK(VoxelWorld_BlockHash(10, 10, 18, 18) == blkFar0);                                     /* per-rectangle: untouched block unchanged */
    CHECK(VoxelWorld_BlockHash(8, 8, 8, 8) == VoxelWorld_BlockHash(0, 0, 0, 0));                /* empty rectangles agree */
    E16(0x02032318u + 2u * BackupIdx(2, 2), (uint32_t)(BackupIdx(2, 2) % 89) | 0x3000);
    Take();
    CHECK(VoxelWorld_LiveDigest() == live0 && VoxelWorld_BlockHash(0, 0, 8, 8) == blk0);        /* undo restores */
}

int main(void)
{
    CHECK(fxInit() == 0);
    TestInstances();
    TestAvailability();
    TestWeatherAndFade();
    TestReflectionAndFortree();
    TestHashes();
    printf("test_voxel_world: %d checks, %d failures\n", sChecks, sFails);
    return sFails != 0;
}
