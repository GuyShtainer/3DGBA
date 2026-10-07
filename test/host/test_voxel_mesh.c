// test_voxel_mesh.c -- host test for the mesh builder + atlas composer over the synthetic fixture:
// phase 32 P2, SPEC-port section 9.2. Checks vertex bookkeeping, overflow accounting, deterministic
// geometry and that every emitted vertex is finite and inside the atlas UV range.
//
//   clang -std=c11 -Wall -Wextra -O2 -fsanitize=address,undefined -DVOXEL_HOST_FILES -DCTR_VOXEL_LIGHTING=1 \
//         -I source/voxel -I test/host test/host/test_voxel_mesh.c \
//         source/voxel/voxel_world.c source/voxel/voxel_regions.c source/voxel/voxel_relief.c \
//         source/voxel/voxel_building.c source/voxel/voxel_sign.c source/voxel/voxel_arena.c \
//         source/voxel/voxel_atlas.c source/voxel/voxel_mesh_builder.c source/voxel/voxel_tree.c \
//         source/voxel/voxel_lighting.c source/voxel/voxel_daylight.c source/voxel/voxel_grade.c source/voxel/voxel_entities.c \
//         source/voxel/voxel_camera.c source/voxel/vx_adapter.c source/voxel/vx_snapshot.c \
//         source/voxel/vx_lz77.c source/voxel/vx_behavior.c source/voxel/vx_data.c \
//         source/voxel/ctr_shims_pure.c source/voxel/vx_battle_stub.c source/romgen/rg_gameprof.c -lm -o /tmp/tvme && /tmp/tvme
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vx_fixture.h"
#include "voxel_atlas.h"
#include "voxel_mesh_builder.h"
#include "voxel_tree.h"
#include "rg_gameprof.h"

static int sChecks, sFails;
#define CHECK(c) do { ++sChecks; if (!(c)) { ++sFails; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

#define CAP 200000u
static VoxelVertex sVerts[CAP], sVerts2[CAP];

static int AllFinite(const VoxelVertex *v, unsigned n)
{
    for (unsigned i = 0; i < n; ++i)
        if (!isfinite(v[i].x) || !isfinite(v[i].y) || !isfinite(v[i].z) || !isfinite(v[i].u) ||
            !isfinite(v[i].v) || !isfinite(v[i].shade))
            return 0;
    return 1;
}

static void TestBuilderBasics(void)
{
    VoxelBuilder b;
    VoxelVertex a = {0, 0, 0, 0, 0, 1}, bb = {1, 0, 0, 1, 0, 1}, c = {1, 0, 1, 1, 1, 1}, d = {0, 0, 1, 0, 1, 1};
    VoxelVertex tiny[5];

    VoxelBuilder_Init(&b, tiny, 5);
    VoxelBuilder_Tri(&b, &a, &bb, &c);
    CHECK(b.count == 3 && b.dropped == 0);
    VoxelBuilder_Quad(&b, &a, &bb, &c, &d);                 /* needs 6, only 2 left */
    CHECK(b.count == 3 && b.dropped > 0);                   /* refused whole, never half-written, never silent */
    VoxelBuilder_Init(&b, tiny, 5);
    VoxelBuilder_SetOrigin(&b, 10, 20);
    VoxelBuilder_Tri(&b, &a, &bb, &c);
    CHECK(tiny[0].x == -10.0f && tiny[0].z == -20.0f);       /* origin is subtracted */
}

/* look L8: the fence, rock and flower cards - vertex counts, finite, inside their cell, and the heights they promise */
static void TestPropCards(void)
{
    static VoxelVertex v[64];
    VoxelBuilder b;
    float top = 0.0f, zmin = 1e9f, zmax = -1e9f;

    VoxelBuilder_Init(&b, v, 64);
    VoxelTree_EmitFenceCard(&b, 5, 7, 0, 0, 1, 1);
    CHECK(b.count == 6 && b.dropped == 0 && AllFinite(v, b.count));
    for (unsigned i = 0; i < b.count; ++i) {
        if (v[i].y > top) top = v[i].y;
        if (v[i].x < 5.0f - 1e-4f || v[i].x > 6.0f + 1e-4f) top = 99.0f;
    }
    CHECK(top > 0.5f && top < 0.7f);                            /* a fence is 0.5-0.7 of a tile tall, in its own cell */
    VoxelBuilder_Init(&b, v, 64);
    VoxelTree_EmitRockCard(&b, 5, 7, 0, 0, 1, 1);
    CHECK(b.count == 6 && b.dropped == 0 && AllFinite(v, b.count));
    top = 0.0f;
    for (unsigned i = 0; i < b.count; ++i) {
        if (v[i].y > top) top = v[i].y;
        if (v[i].z < zmin) zmin = v[i].z;
        if (v[i].z > zmax) zmax = v[i].z;
    }
    CHECK(top > 0.5f && top < 0.8f && zmin >= 7.0f - 1e-4f && zmax <= 8.0f + 1e-4f);   /* low, leaning, inside the cell */
    VoxelBuilder_Init(&b, v, 64);
    VoxelTree_EmitGrassCard(&b, 5, 7, 0, 0, 1, 1);
    CHECK(b.count == 6u * VOXEL_GRASS_CARDS && AllFinite(v, b.count));      /* a flower bed's two crossed cards */
    VoxelBuilder_Init(&b, v, 5);
    VoxelTree_EmitFenceCard(&b, 0, 0, 0, 0, 1, 1);
    CHECK(b.count == 3 && b.dropped > 0);                       /* a full chunk drops the triangle that does not fit, and counts it */
    CHECK(VoxelTree_PropKind(1000u) == GP_PROP_BUSH);           /* out of the table: a bush, never a crash */
}

static void TestGroundAndAtlas(void)
{
    static uint16_t atlas[VOXEL_ATLAS_PIXELS];
    static VoxelAtlasMap map, map2;
    VxMemSrc s = fxSrc();
    const VoxelMapInstance *i0;
    VoxelBuilder b, b2;
    unsigned n1;

    CHECK(vx_snapshot_take(fxSnap, &s));
    CHECK(vx_adapter_decode(fxSnap));
    VoxelWorld_BeginBatch();
    VoxelWorld_BuildInstances();
    i0 = VoxelWorld_Instance(0);
    CHECK(i0 != NULL);

    CHECK(VoxelAtlas_Build(i0, atlas, &map, false));
    CHECK(map.used > 0 && !map.overflowed);
    CHECK(VoxelAtlas_Build(i0, atlas, &map2, false) && map2.used == map.used);
    CHECK(memcmp(map.slotOf, map2.slotOf, sizeof map.slotOf) == 0);       /* deterministic */
    CHECK(VoxelAtlas_Build(i0, atlas, &map, true));                        /* extend with nothing new: still ok */

    VoxelMesh_BeginWindow(0, 0, 20, 20);
    VoxelBuilder_Init(&b, sVerts, CAP);
    VoxelBuilder_SetAtlas(&b, &map);
    VoxelMesh_EmitInstance(&b, i0, 0, 0, 20, 20);
    n1 = b.count;
    CHECK(n1 > 0 && n1 % 3 == 0 && b.dropped == 0);
    CHECK(AllFinite(sVerts, n1));
    for (unsigned i = 0; i < n1; ++i)
        if (sVerts[i].u < -0.001f || sVerts[i].u > 1.001f || sVerts[i].v < -0.001f || sVerts[i].v > 1.001f)
        {
            CHECK(!"uv out of range");
            break;
        }
    /* same input, same geometry (the renderer's chunk-signature caching depends on it) */
    VoxelMesh_BeginWindow(0, 0, 20, 20);
    VoxelBuilder_Init(&b2, sVerts2, CAP);
    VoxelBuilder_SetAtlas(&b2, &map);
    VoxelMesh_EmitInstance(&b2, i0, 0, 0, 20, 20);
    CHECK(b2.count == n1 && memcmp(sVerts, sVerts2, n1 * sizeof(VoxelVertex)) == 0);
    /* row-at-a-time == whole rectangle */
    VoxelMesh_BeginWindow(0, 0, 20, 20);
    VoxelBuilder_Init(&b2, sVerts2, CAP);
    VoxelBuilder_SetAtlas(&b2, &map);
    for (int y = 0; y < 20; ++y)
        VoxelMesh_EmitGroundRow(&b2, i0, 0, 20, y);
    CHECK(b2.count == n1 && memcmp(sVerts, sVerts2, n1 * sizeof(VoxelVertex)) == 0);
    /* a build into a too-small buffer drops, reports it, and stays in bounds */
    VoxelMesh_BeginWindow(0, 0, 20, 20);
    VoxelBuilder_Init(&b2, sVerts2, 30);
    VoxelBuilder_SetAtlas(&b2, &map);
    VoxelMesh_EmitInstance(&b2, i0, 0, 0, 20, 20);
    CHECK(b2.count <= 30 && b2.dropped > 0);
    /* an id the atlas never met is counted, not sampled blind */
    {
        VoxelAtlasMap empty;
        memset(&empty, 0xFF, sizeof empty);
        empty.used = 0; empty.overflowed = false;
        VoxelMesh_BeginWindow(0, 0, 20, 20);
        VoxelBuilder_Init(&b2, sVerts2, CAP);
        VoxelBuilder_SetAtlas(&b2, &empty);
        VoxelMesh_EmitInstance(&b2, i0, 0, 0, 20, 20);
        CHECK(b2.uncovered > 0 || b2.count < n1);
    }
    /* draft: six vertices per cell with a slot, -1 or counted otherwise */
    {
        unsigned unc = 0;
        int slot = VoxelMesh_DraftSlot(i0, &map, 3, 4, &unc);
        VoxelBuilder_Init(&b2, sVerts2, CAP);
        VoxelBuilder_SetAtlas(&b2, &map);
        if (slot >= 0)
        {
            VoxelMesh_DraftCell(&b2, i0, 3, 4, slot);
            CHECK(b2.count == 6 && AllFinite(sVerts2, 6));
        }
        else
            CHECK(unc == 0 || slot == -1);
    }
    /* border: instance 0 only, finite, in bounds */
    VoxelMesh_BeginWindow(-8, -8, 28, 28);
    VoxelBuilder_Init(&b2, sVerts2, CAP);
    VoxelBuilder_SetAtlas(&b2, &map);
    VoxelMesh_EmitBorder(&b2, -8, -8, 28, 28);
    CHECK(b2.dropped == 0 && AllFinite(sVerts2, b2.count));
}

int main(void)
{
    CHECK(fxInit() == 0);
    TestBuilderBasics();
    TestPropCards();
    TestGroundAndAtlas();
    printf("test_voxel_mesh: %d checks, %d failures\n", sChecks, sFails);
    return sFails != 0;
}
