// test_voxel_world.c -- host test for the vendored world module (voxel_world.c) running on top of our
// adapter: phase 32 P2, SPEC-port section 9.2. Synthetic state only (vx_fixture.h).
//
//   clang -std=c11 -Wall -Wextra -O2 -fsanitize=address,undefined -DVOXEL_HOST_FILES -DCTR_VOXEL_LIGHTING=1 \
//         -I source/voxel -I test/host test/host/test_voxel_world.c \
//         source/voxel/voxel_world.c source/voxel/voxel_regions.c source/voxel/voxel_relief.c \
//         source/voxel/voxel_building.c source/voxel/voxel_sign.c source/voxel/voxel_arena.c \
//         source/voxel/voxel_atlas.c source/voxel/voxel_mesh_builder.c source/voxel/voxel_tree.c \
//         source/voxel/voxel_lighting.c source/voxel/voxel_daylight.c source/voxel/voxel_grade.c source/voxel/voxel_entities.c \
//         source/voxel/voxel_camera.c source/voxel/vx_adapter.c source/voxel/vx_snapshot.c \
//         source/voxel/vx_lz77.c source/voxel/vx_behavior.c source/voxel/vx_data.c \
//         source/voxel/ctr_shims_pure.c source/voxel/vx_battle_stub.c source/romgen/rg_gameprof.c -lm -o /tmp/tvwo && /tmp/tvwo
//
// (the world module drags the mesh/atlas modules in at link time; ctr_voxel.c is the only GPU file and
// is never host-built.)
#include <math.h>
#include <stdio.h>

#include "vx_fixture.h"
#include "voxel_world.h"
#include "voxel_tree.h"
#include "voxel_atlas.h"
#include "voxel_lighting.h"
#include "rg_gameprof.h"

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

/* ---- Phase 34 T1: the tree tables come from the game profile ---- */

/* The Emerald tables exactly as voxel_tree.c carried them before T1 (the oracle for "byte-identical"). */
static int OldPart(int metatileId)
{
    switch (metatileId)
    {
    case 0x1D4: case 0x1D6: return 0;
    case 0x1D5: case 0x1D7: return 1;
    case 0x1DC: case 0x1DE: case 0x1E4: case 0x1E6: return 2;
    case 0x1DD: case 0x1DF: case 0x1E5: case 0x1E7: return 3;
    case 0x1EC: return 2;
    case 0x1ED: return 3;
    case 0x016: case 0x017: case 0x0C6: case 0x0C7:
    case 0x1F4: case 0x1F5: return VOXEL_TREE_SMALL;
    default: return -1;
    }
}

static int OldGround(int metatileId)
{
    switch (metatileId)
    {
    case 0x1C6: case 0x1C7: return 0x00D;
    case 0x1CE: case 0x1CF: return 0x001;
    case 0x00E: case 0x00F: case 0x040: return 0x001;
    case 0x01D: return 0x002;
    case 0x025: return 0x00D;
    case 0x02D: return 0x0A1;
    case 0x035: case 0x193: return 0x170;
    case 0x0CE: return 0x091;
    default: return metatileId;
    }
}

static void TestTreeTables(void)
{
    static const int kKantoTrees[] = {0x14, 0x15, 0x16, 0x17, 0x1C, 0x1D, 0x1E, 0x1F, 0x24, 0x25, 0x26, 0x27};
    uint8_t hdr[0xC0];
    int id;

    /* Emerald: every id, plus out-of-range ids, gives what the old switch gave. */
    gVxProf = NULL;
    for (id = 0; id < 1024; ++id)
    {
        CHECK(VoxelTree_Part(id) == OldPart(id));
        CHECK(VoxelTree_GroundMetatile(id) == OldGround(id));
    }
    CHECK(VoxelTree_Part(-1) == -1 && VoxelTree_Part(1024) == -1 && VoxelTree_Part(0x7FFF) == -1);
    CHECK(VoxelTree_GroundMetatile(-1) == -1 && VoxelTree_GroundMetatile(1024) == 1024);
    CHECK(gameprof_emerald()->treePartCount == 20 && gameprof_emerald()->treeGroundCount == 13);   /* 20 parts, 13 grounds */

    /* FireRed / LeafGreen rev 1: the Kanto table. Header-only fake images (code + revision) pick the rows. */
    for (int game = 0; game < 2; ++game)
    {
        const GameProfile *p;

        memset(hdr, 0, sizeof hdr);
        memcpy(hdr + 0xAC, game == 0 ? "BPRE" : "BPGE", 4);
        hdr[0xBC] = 1;
        p = gameprof_detect(hdr, sizeof hdr);
        CHECK(p != NULL && p->game == (game == 0 ? GP_FIRERED : GP_LEAFGREEN));
        if (p == NULL)
            continue;
        gVxProf = p;
        CHECK(p->treePart != NULL && p->treePartCount == 12 && p->treeGround == NULL && p->treeGroundCount == 0);
        for (int k = 0; k < p->treePartCount; ++k)       /* every tree id is a primary metatile, below 640 */
            CHECK(p->treePart[2 * k] >= 0 && p->treePart[2 * k] < (int)p->nPrimMetatiles && p->treePart[2 * k] < 640);
        for (id = -2; id < 1030; ++id)
        {
            int want = -1;

            for (int k = 0; k < 12; ++k)
                if (kKantoTrees[k] == id)
                    want = (id & 1) | (((id == 0x1C || id == 0x1D || id == 0x1E || id == 0x1F) ? 0 : 1) << 1);
            CHECK(VoxelTree_Part(id) == want);
            CHECK(VoxelTree_GroundMetatile(id) == id);    /* Kanto has no ground replacements */
        }
        /* Pallet's border block (SPEC T1): 1C 1D over 14 15 is the quadrants 0 1 / 2 3. */
        CHECK(VoxelTree_Part(0x1C) == 0 && VoxelTree_Part(0x1D) == 1 && VoxelTree_Part(0x14) == 2 && VoxelTree_Part(0x15) == 3);
        CHECK(VoxelTree_Part(0x001) == -1 && VoxelTree_Part(0x00D) == -1 && VoxelTree_Part(0x005) == -1);   /* grass, tall grass, bush */
        CHECK(VoxelTree_Part(0x1D4) == -1 && VoxelTree_Part(0x016 + 0x100) == -1);   /* Emerald's ids are not Kanto's */
    }
    gVxProf = NULL;
    CHECK(VoxelTree_Part(0x1D4) == 0 && VoxelTree_Part(0x1C) == -1);   /* and back: the Emerald table again */
}

/* Look backlog L1: the shrub tables and their lookup. A secondary id is a shrub only with its own secondary tileset,
 * compared by ROM address; a primary one only on the General tileset; nothing indoors; never a tree part. */
static void TestShrubTables(void)
{
    const VoxelMapInstance *i0;
    VoxelMapInstance inst;
    struct Tileset dewford, slateport, general;
    const GameProfile *em = gameprof_emerald();
    uint8_t hdr[0xC0];
    unsigned m = 0;

    Take();
    i0 = VoxelWorld_Instance(0);
    CHECK(i0 != NULL);
    if (i0 == NULL)
        return;
    gVxProf = NULL;
    CHECK(em->shrubs != NULL && em->shrubCount == 18 && em->shrubCount <= VOXEL_SHRUBS);
    for (unsigned k = 0; k < 11; ++k)      /* the 11 Emerald bushes are secondary ids with their tileset; L8 props follow */
        CHECK(em->shrubs[k].kind == GP_PROP_BUSH && em->shrubs[k].tileset != 0 && em->shrubs[k].metatile >= em->nPrimMetatiles
              && em->shrubs[k].metatile < 1024);
    for (unsigned k = 11; k < em->shrubCount; ++k)     /* L8: General-tileset props, never bushes */
        CHECK(em->shrubs[k].kind != GP_PROP_BUSH && em->shrubs[k].tileset == 0 && em->shrubs[k].metatile < em->nPrimMetatiles);
    memset(&dewford, 0, sizeof dewford);
    dewford.gbaAddr = 0x083DF74Cu;
    slateport = dewford;
    slateport.gbaAddr = 0x083DF764u;
    inst = *i0;                                        /* the fixture's General + Fortree instance, outdoors */
    CHECK(VoxelTree_Shrub(&inst, 0x243) == -1);        /* Fortree's 0x243 is not Dewford's */
    inst.secondaryTileset = &dewford;
    CHECK(VoxelTree_Shrub(&inst, 0x239) == 0 && VoxelTree_Shrub(&inst, 0x23A) == 1 && VoxelTree_Shrub(&inst, 0x242) == 2);
    CHECK(VoxelTree_Shrub(&inst, 0x243) == 3 && VoxelTree_Shrub(&inst, 0x247) == 4);
    CHECK(VoxelTree_Shrub(&inst, 0x244) == -1 && VoxelTree_Shrub(&inst, 0x220) == -1 && VoxelTree_Shrub(&inst, 0x005) == -1);
    CHECK(VoxelTree_Shrub(&inst, -1) == -1 && VoxelTree_Shrub(&inst, 1024) == -1);
    CHECK(VoxelTree_Part(0x243) == -1 && VoxelTree_Part(0x239) == -1);   /* not a tree part: drawn with the terrain */
    inst.secondaryTileset = &slateport;
    CHECK(VoxelTree_Shrub(&inst, 0x243) == 8);         /* the same id, the other tileset's entry */
    CHECK(VoxelTree_ShrubSource(inst.primaryTileset, &slateport, 8, &m) && m == 0x243);
    CHECK(!VoxelTree_ShrubSource(inst.primaryTileset, &dewford, 8, &m));
    CHECK(VoxelTree_ShrubSource(inst.primaryTileset, &dewford, 3, &m) && m == 0x243);
    CHECK(!VoxelTree_ShrubSource(inst.primaryTileset, &dewford, VOXEL_SHRUBS, &m));
    inst.indoor = true;
    CHECK(VoxelTree_Shrub(&inst, 0x243) == -1);        /* indoors: no tree sprites, no shrubs */
    inst.indoor = false;
    inst.primaryTileset = NULL;
    CHECK(VoxelTree_Shrub(&inst, 0x243) == -1);        /* not the General tileset */

    /* FireRed / LeafGreen: the General round bush 0x005 plus two secondary bushes at per-game addresses. */
    for (int game = 0; game < 2; ++game)
    {
        const GameProfile *p;

        memset(hdr, 0, sizeof hdr);
        memcpy(hdr + 0xAC, game == 0 ? "BPRE" : "BPGE", 4);
        hdr[0xBC] = 1;
        p = gameprof_detect(hdr, sizeof hdr);
        CHECK(p != NULL && p->shrubs != NULL && p->shrubCount == 33);   /* 3 bushes + the L8 props */
        if (p == NULL || p->shrubs == NULL)
            continue;
        CHECK(p->shrubs[0].tileset == 0 && p->shrubs[0].metatile == 0x005);
        CHECK(p->shrubs[1].metatile == 0x2F4 && p->shrubs[2].metatile == 0x2E0);
        CHECK(p->shrubs[1].tileset == (game == 0 ? 0x082D4BC4u : 0x082D4BA4u));
        CHECK(p->shrubs[2].tileset == (game == 0 ? 0x082D4B7Cu : 0x082D4B5Cu));
        gVxProf = p;
        memset(&general, 0, sizeof general);
        general.gbaAddr = p->tsGeneral;
        CHECK(VoxelTree_ShrubSource(&general, NULL, 0, &m) && m == 0x005);
        dewford.gbaAddr = p->shrubs[1].tileset;
        CHECK(VoxelTree_ShrubSource(&general, &dewford, 1, &m) && m == 0x2F4);
        CHECK(!VoxelTree_ShrubSource(&general, &dewford, 2, &m));
        general.gbaAddr = em->tsGeneral;                /* Emerald's General address is not Kanto's */
        CHECK(!VoxelTree_ShrubSource(&general, NULL, 0, &m));
    }
    gVxProf = NULL;
}

/* ---- L3: the sun is in front of the scene; nothing in the march assumes a sign ---- */

#if CTR_VOXEL_LIGHTING && defined(VOXEL_LIGHTING_TESTS)
extern bool gVoxelLightingStepEveryPoint;

static void TestSun(void)
{
    unsigned shadowed = 0, compared = 0;

    /* The vector: south of the scene (+Z is south, the camera's side), so DZ < 0 and the shadows
     * (+DX, +DZ) run north, away from the camera; high enough for a roof to read. */
    CHECK(VOXEL_SUN_DZ < 0.0f);
    CHECK(sqrtf(VOXEL_SUN_DX * VOXEL_SUN_DX + VOXEL_SUN_DZ * VOXEL_SUN_DZ) < 1.0f);   /* above 45 degrees */
    /* The face term: ground full, the camera-facing south wall lit, west a little, north/east ambient. */
    CHECK(VoxelLighting_Face(0, 1, 0) == 1.0f);
    CHECK(VoxelLighting_Face(0, 0, 1) > VOXEL_AMBIENT + 0.15f);       /* a south wall: lit, not washed out */
    CHECK(VoxelLighting_Face(0, 0, 1) < 1.0f);
    CHECK(VoxelLighting_Face(-1, 0, 0) > VOXEL_AMBIENT);              /* west: lit a little */
    CHECK(VoxelLighting_Face(0, 0, 1) > VoxelLighting_Face(-1, 0, 0));
    CHECK(VoxelLighting_Face(0, 0, -1) == VOXEL_AMBIENT);             /* north wall: turned away */
    CHECK(VoxelLighting_Face(1, 0, 0) == VOXEL_AMBIENT);              /* east wall: turned away */
    /* A flat south-facing roof pitch is lit more than its north-facing twin: the roofs still read. */
    CHECK(VoxelLighting_Face(0, 1, 0.5f) > VoxelLighting_Face(0, 1, -0.5f));

    /* The skipping march agrees with the point-by-point one, over the whole fixture. */
    Take();
    VoxelLighting_Reset();
    for (int z = -4; z < 24; ++z)
        for (int x = -2; x < 24; ++x)
            for (int k = 0; k < 3; ++k)
            {
                float px = (float)x + 0.17f + 0.31f * (float)k, pz = (float)z + 0.62f - 0.23f * (float)k;
                float py = 0.4f + 0.8f * (float)k;
                float fast, ref;

                gVoxelLightingStepEveryPoint = false;
                VoxelLighting_Reset();
                fast = VoxelLighting_Sample(px, py, pz);
                gVoxelLightingStepEveryPoint = true;
                VoxelLighting_Reset();
                ref = VoxelLighting_Sample(px, py, pz);
                gVoxelLightingStepEveryPoint = false;
                CHECK(fast == ref);
                ++compared;
                if (ref < 1.0f)
                    ++shadowed;
            }
    CHECK(compared > 1000);
    printf("  sun march: %u points compared, %u in shadow\n", compared, shadowed);
}
#endif

/* Look backlog L2: the grass profile. Blade grass is behaviour 0x02 alone (not long 0x03, short 0x07, ash 0x09), the
 * ground key is the plain-grass metatile 0x001, and the skip list holds the one sandy-ground Emerald tileset. */
static void TestGrassProfile(void)
{
    const GameProfile *em = gameprof_emerald();
    unsigned n = 0;

    for (unsigned b = 0; b < 512; ++b)
        if (gp_beh(&em->bladeGrass, b)) { ++n; CHECK(b == 0x02u); }
    CHECK(n == 1 && gp_beh(&em->tallGrass, 0x02) && gp_beh(&em->tallGrass, 0x03));
    CHECK(!gp_beh(&em->bladeGrass, 0x03) && !gp_beh(&em->bladeGrass, 0x07) && !gp_beh(&em->bladeGrass, 0x09));
    CHECK(em->grassGround == 0x001u);
    CHECK(em->grassSkip != NULL && em->grassSkipCount == 1 && em->grassSkip[0].tileset == 0x083DF794u
          && em->grassSkip[0].metatile == 0x206u);
    CHECK(VOXEL_GRASS_FIRST == VOXEL_SHRUB_FIRST + 2u * VOXEL_SHRUBS && VOXEL_METATILE_IDS == VOXEL_GRASS_FIRST + VOXEL_GRASSES);
    CHECK(VOXEL_GRASS_BLADES(0) == VOXEL_GRASS_FIRST && VOXEL_GRASS_BLADES(VOXEL_GRASSES - 1) < VOXEL_METATILE_IDS);
    CHECK(VOXEL_GRASS_CARDS == 2u);
}

int main(void)
{
    CHECK(fxInit() == 0);
    TestInstances();
    TestAvailability();
    TestWeatherAndFade();
    TestReflectionAndFortree();
    TestHashes();
    TestTreeTables();
    TestShrubTables();
    TestGrassProfile();
#if CTR_VOXEL_LIGHTING && defined(VOXEL_LIGHTING_TESTS)
    TestSun();
#endif
    printf("test_voxel_world: %d checks, %d failures\n", sChecks, sFails);
    return sFails != 0;
}
