// test_voxel_entities.c -- host test for the entity module (voxel_entities.c): sub-tile movement
// interpolation, the STEP table, Emit on the synthetic fixture. Phase 32 P2, SPEC-port section 9.2.
// STEP: the five step lengths in voxel_entities.c (16,8,6,4,2) were re-derived from the game ROM
// (BPEE 0x0850E768, five u16) and are re-checked here when roms/emerald.gba is present.
//
//   clang -std=c11 -Wall -Wextra -O2 -fsanitize=address,undefined -DVOXEL_HOST_FILES -DCTR_VOXEL_LIGHTING=1 \
//         -I source/voxel -I test/host test/host/test_voxel_entities.c \
//         source/voxel/voxel_world.c source/voxel/voxel_regions.c source/voxel/voxel_relief.c \
//         source/voxel/voxel_building.c source/voxel/voxel_sign.c source/voxel/voxel_arena.c \
//         source/voxel/voxel_atlas.c source/voxel/voxel_mesh_builder.c source/voxel/voxel_tree.c \
//         source/voxel/voxel_lighting.c source/voxel/voxel_grade.c source/voxel/voxel_entities.c \
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

int main(void)
{
    CHECK(fxInit() == 0);
    TestStepTable();
    TestMovement();
    TestEmit();
    printf("test_voxel_entities: %d checks, %d failures\n", sChecks, sFails);
    return sFails != 0;
}
