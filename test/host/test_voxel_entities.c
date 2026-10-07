// test_voxel_entities.c -- host test for the entity module (voxel_entities.c): sub-tile movement
// interpolation, the STEP table, Emit on the synthetic fixture, look L9 tall grass. Phase 32 P2, SPEC-port section 9.2.
// STEP: the five step lengths in voxel_entities.c (16,8,6,4,2) were re-derived from the game ROM
// (BPEE 0x0850E768, five u16) and are re-checked here when roms/emerald.gba is present.
//
//   clang -std=c11 -Wall -Wextra -O2 -fsanitize=address,undefined -DVOXEL_HOST_FILES -DCTR_VOXEL_LIGHTING=1 \
//         -I source/voxel -I test/host test/host/test_voxel_entities.c \
//         source/voxel/voxel_world.c source/voxel/voxel_regions.c source/voxel/voxel_relief.c \
//         source/voxel/voxel_building.c source/voxel/voxel_sign.c source/voxel/voxel_arena.c \
//         source/voxel/voxel_atlas.c source/voxel/voxel_mesh_builder.c source/voxel/voxel_tree.c \
//         source/voxel/voxel_lighting.c source/voxel/voxel_daylight.c source/voxel/voxel_grade.c source/voxel/voxel_entities.c \
//         source/voxel/voxel_camera.c source/voxel/vx_adapter.c source/voxel/vx_snapshot.c \
//         source/voxel/vx_lz77.c source/voxel/vx_behavior.c source/voxel/vx_data.c \
//         source/voxel/ctr_shims_pure.c source/voxel/vx_battle_stub.c source/romgen/rg_gameprof.c -lm -o /tmp/tven && /tmp/tven
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "vx_fixture.h"
#include "voxel_entities.h"

static int sChecks, sFails;
#define CHECK(c) do { ++sChecks; if (!(c)) { ++sFails; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

#define OBJ 0x02037350u
#define SPR 0x02020630u

static void Take(void)
{
    VxMemSrc s = fxSrc();
    CHECK(vx_snapshot_take(fxSnap, &s));
    CHECK(vx_adapter_decode(fxSnap));
    VoxelWorld_BeginBatch();
    VoxelWorld_BuildInstances();
}

static int Near(float a, float b) { return fabsf(a - b) < 1e-4f; }

static void TestStepTable(void)
{
    static const int want[5] = {16, 8, 6, 4, 2};
    FILE *f = fopen("roms/emerald.gba", "rb");
    uint8_t buf[10];

    if (f == NULL) { printf("note: roms/emerald.gba absent, STEP ROM re-check skipped\n"); return; }
    CHECK(fseek(f, 0x0050E768, SEEK_SET) == 0 && fread(buf, 1, 10, f) == 10);
    fclose(f);
    for (int i = 0; i < 5; ++i)
        CHECK((int)(buf[2 * i] | (buf[2 * i + 1] << 8)) == want[i]);
}

static void TestMovement(void)
{
    float x, z;

    Take();
    /* standing: previous == current -> the tile itself, at instance 0's origin (0,0) */
    VoxelEntities_Reset();
    VoxelEntities_GetPlayerWorldPos(&x, &z);
    CHECK(Near(x, 5.0f) && Near(z, 6.0f));
    /* mid-step east: previous x 11, current x 12, speed index 1 (8 frames), timer 4 -> halfway */
    E16(OBJ + 0x14, 11); E16(OBJ + 0x16, 13);
    E8(OBJ, 0x01 | 0x02);
    E16(SPR + 0x2E + 8, 1); E16(SPR + 0x2E + 10, 4);
    Take();
    VoxelEntities_GetPlayerWorldPos(&x, &z);
    CHECK(Near(x, 4.5f) && Near(z, 6.0f));
    E16(SPR + 0x2E + 10, 8); Take();
    VoxelEntities_GetPlayerWorldPos(&x, &z);
    CHECK(Near(x, 5.0f));                                            /* timer at step length: arrived */
    E16(SPR + 0x2E + 10, 100); Take();
    VoxelEntities_GetPlayerWorldPos(&x, &z);
    CHECK(Near(x, 5.0f));                                            /* clamped, never overshoots */
    E16(SPR + 0x2E + 8, 9); E16(SPR + 0x2E + 10, 1); Take();
    VoxelEntities_GetPlayerWorldPos(&x, &z);
    CHECK(Near(x, 5.0f));                                            /* unknown speed index: no interpolation */
    E16(SPR + 0x2E + 8, 4); E16(SPR + 0x2E + 10, 1); Take();
    VoxelEntities_GetPlayerWorldPos(&x, &z);
    CHECK(Near(x, 4.5f));                                            /* fastest speed: 2 frames per tile */
    E8(OBJ, 0x01); Take();                                           /* idle flag clear: stands at current */
    VoxelEntities_GetPlayerWorldPos(&x, &z);
    CHECK(Near(x, 5.0f));
    E16(OBJ + 0x14, 12); E16(OBJ + 0x16, 13);
}

static void TestEmit(void)
{
    static uint16_t atlas[VOXEL_SPRITE_PIXELS];
    static VoxelVertex verts[4096];
    VoxelBuilder b;
    VoxelCamera cam;
    unsigned updates, again;

    Take();
    VoxelCamera_Init(&cam);
    VoxelEntities_Reset();
    memset(atlas, 0, sizeof atlas);
    VoxelBuilder_Init(&b, verts, 4096);
    updates = VoxelEntities_Emit(&b, atlas, &cam, NULL, NULL);
    CHECK(updates <= VOXEL_SPRITE_SLOTS && b.dropped == 0 && b.count % 3 == 0);
    for (unsigned i = 0; i < b.count; ++i)
        CHECK(isfinite(verts[i].x) && isfinite(verts[i].y) && isfinite(verts[i].z));
    /* second frame, nothing changed: nothing is re-decoded, geometry is the same size */
    {
        unsigned n1 = b.count;
        VoxelBuilder_Init(&b, verts, 4096);
        again = VoxelEntities_Emit(&b, atlas, &cam, NULL, NULL);
        CHECK(again == 0 && b.count == n1);
    }
    /* hidden player emits no quad for it */
    E8(OBJ, 0x01 | 0x08);                                             /* invisible bit */
    Take();
    VoxelBuilder_Init(&b, verts, 4096);
    VoxelEntities_Emit(&b, atlas, &cam, NULL, NULL);
    CHECK(VoxelEntities_PlayerVertexFirst() == -1);
    E8(OBJ, 0x01);
}

/*
 * look L9: tall grass stands up in front of an object on its tile, and the
 * player standing in it is two quads so the x-ray can leave the feet out.
 * Sprite 6 is the GBA's tall grass field effect (template FLDEFFOBJ_TALL_GRASS
 * of the fixture's table at 0x085059F8: 0x08700000 + 0x40 * 4), 16x16, on the
 * player's tile (map 12,13 = world 5,6); long grass (15) is the control.
 */
#define FX_TALL_GRASS 0x08700100u
#define FX_LONG_GRASS 0x087003C0u
#define GRASS_SPR (SPR + 6u * 0x44u)

static void PutGrass(uint32_t spr, uint32_t template, int mapX, int mapY)
{
    E16(spr + 0, 0x0040);                       /* y 0x40, square */
    E16(spr + 2, 0x0080 | (1u << 14));          /* x 0x80, size 1: 16x16 */
    E16(spr + 4, 0x200 | (3u << 12));           /* its own tiles, not the player's */
    E32(spr + 0x14, template);
    E16(spr + 0x20, 128); E16(spr + 0x22, 72);
    E16(spr + 0x2E + 2, (uint32_t)mapX); E16(spr + 0x2E + 4, (uint32_t)mapY);
    E8(spr + 0x3E, 0x03);                       /* inUse, coordOffsetEnabled */
}

static float MinY(const VoxelVertex *v, unsigned n)
{ float m = v[0].y; for (unsigned i = 1; i < n; ++i) if (v[i].y < m) m = v[i].y; return m; }
static float MaxY(const VoxelVertex *v, unsigned n)
{ float m = v[0].y; for (unsigned i = 1; i < n; ++i) if (v[i].y > m) m = v[i].y; return m; }

static void TestTallGrass(void)
{
    static uint16_t atlas[VOXEL_SPRITE_PIXELS];
    static VoxelVertex verts[4096];
    VoxelBuilder b;
    VoxelCamera cam;
    unsigned n0;

    E8(SPR + 0x3E, 0x03);                       /* the player's sprite on the map: drawn */
    Take();
    CHECK(VoxelEntities_StandsTileEffect(FX_TALL_GRASS));
    CHECK(!VoxelEntities_StandsTileEffect(FX_LONG_GRASS));
    CHECK(!VoxelEntities_StandsTileEffect(0));
    VoxelCamera_Init(&cam);
    VoxelEntities_Reset();
    VoxelBuilder_Init(&b, verts, 4096);
    VoxelEntities_Emit(&b, atlas, &cam, NULL, NULL);
    n0 = b.count;
    CHECK(VoxelEntities_PlayerVertexFirst() == 0);                    /* one quad, the first */

    /* tall grass on the player's tile: one more quad for the grass, one for the split */
    PutGrass(GRASS_SPR, FX_TALL_GRASS, 12, 13);
    Take();
    VoxelBuilder_Init(&b, verts, 4096);
    VoxelEntities_Emit(&b, atlas, &cam, NULL, NULL);
    CHECK(b.dropped == 0 && b.count == n0 + 12);
    if (b.count == n0 + 12)
    {
        const VoxelVertex *lo = &verts[0], *hi = &verts[6], *g = &verts[b.count - 6];
        float yaw = cam.yaw * (3.14159265358979323846f / 180.0f);
        float stretch = 1.0f / sqrtf(cosf(cam.pitch * (3.14159265358979323846f / 180.0f)));
        float tx = sinf(yaw), tz = cosf(yaw);
        float pmx = (lo[0].x + lo[1].x) * 0.5f, pmz = (lo[0].z + lo[1].z) * 0.5f;
        float gmx = (g[0].x + g[1].x) * 0.5f, gmz = (g[0].z + g[1].z) * 0.5f;

        CHECK(VoxelEntities_PlayerVertexFirst() == 6);                /* x-ray draws the upper quad */
        /* the split: lower quad 8 rows tall, upper quad picks up where it ends, in u/v too */
        CHECK(Near(MaxY(lo, 6) - MinY(lo, 6), 8.0f * stretch / 16.0f));
        CHECK(Near(MinY(hi, 6), MaxY(lo, 6)));
        CHECK(Near(hi[0].v, lo[2].v) && Near(hi[0].u, lo[0].u) && Near(hi[1].u, lo[1].u));
        CHECK(Near(lo[0].x, hi[0].x) && Near(lo[0].z, hi[0].z));     /* same plane */
        /* the grass stands up (not a decal), feet on the ground with the player's */
        CHECK(MaxY(g, 6) - MinY(g, 6) > 0.5f);
        CHECK(Near(MinY(g, 6), MinY(lo, 6)));
        CHECK(Near(g[0].x, g[5].x) && Near(g[0].z, g[5].z));        /* vertical edge */
        /* 0.04 in front of the player's card, towards the camera, not sideways */
        CHECK(fabsf((gmx - pmx) * tx + (gmz - pmz) * tz - 0.04f) < 1e-3f);
        CHECK(fabsf((gmx - pmx) * tz - (gmz - pmz) * tx) < 1e-3f);
    }

    /* the NPC (map 15,9) in tall grass is not split: the x-ray is the player's */
    PutGrass(GRASS_SPR, FX_TALL_GRASS, 15, 9);
    Take();
    VoxelBuilder_Init(&b, verts, 4096);
    VoxelEntities_Emit(&b, atlas, &cam, NULL, NULL);
    CHECK(b.count == n0 + 6 && VoxelEntities_PlayerVertexFirst() == 0);

    /* long grass on the player's tile stays a decal on the ground, the player one quad */
    PutGrass(GRASS_SPR, FX_LONG_GRASS, 12, 13);
    Take();
    VoxelBuilder_Init(&b, verts, 4096);
    VoxelEntities_Emit(&b, atlas, &cam, NULL, NULL);
    CHECK(b.count == n0 + 6 && VoxelEntities_PlayerVertexFirst() == 0);
    if (b.count == n0 + 6)
    {
        const VoxelVertex *g = &verts[b.count - 6];
        CHECK(MaxY(g, 6) - MinY(g, 6) < 0.05f);
    }
    E8(GRASS_SPR + 0x3E, 0);
    E8(SPR + 0x3E, 0x01);
}

int main(void)
{
    CHECK(fxInit() == 0);
    TestStepTable();
    TestMovement();
    TestEmit();
    TestTallGrass();
    printf("test_voxel_entities: %d checks, %d failures\n", sChecks, sFails);
    return sFails != 0;
}
