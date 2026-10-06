/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/src/voxel/voxel_tree.c,
 * MIT License - see source/voxel/NOTICE.md. */
#include <stddef.h>
#include "voxel_tree.h"
#include "voxel_relief.h"
#include "gba_game.h" /* 3DGBA: VXP() */

/* 3DGBA (Phase 34 T1): the tables live in the game profile (treePart / treeGround, flat int16 pairs, see
 * rg_gameprof.h) so each game brings its own metatile ids. They are expanded once per profile into two lookup tables:
 * these functions run per cell, per frame, and the ground map is walked over all 1024 ids on every atlas build. */
#define TREE_LUT_IDS 1024

static const GameProfile *sLutFor;
static int8_t sPartLut[TREE_LUT_IDS];
static int16_t sGroundLut[TREE_LUT_IDS];

static void TreeLut(void)
{
    const GameProfile *p = vx_prof();
    unsigned i;

    if (p == sLutFor)
        return;
    for (i = 0; i < TREE_LUT_IDS; ++i)
    {
        sPartLut[i] = -1;
        sGroundLut[i] = (int16_t)i;
    }
    for (i = 0; p->treePart != NULL && i < p->treePartCount; ++i)
        if ((unsigned)p->treePart[2 * i] < TREE_LUT_IDS)
            sPartLut[p->treePart[2 * i]] = (int8_t)p->treePart[2 * i + 1];
    for (i = 0; p->treeGround != NULL && i < p->treeGroundCount; ++i)
        if ((unsigned)p->treeGround[2 * i] < TREE_LUT_IDS)
            sGroundLut[p->treeGround[2 * i]] = p->treeGround[2 * i + 1];
    sLutFor = p;
}

int VoxelTree_Part(int metatileId)
{
    TreeLut();
    return (unsigned)metatileId < TREE_LUT_IDS ? sPartLut[metatileId] : -1;
}

int VoxelTree_GroundMetatile(int metatileId)
{
    TreeLut();
    return (unsigned)metatileId < TREE_LUT_IDS ? sGroundLut[metatileId] : metatileId;
}

/* The small crown is 16:32: width 1, length 2 tiles, at the same 50 degrees
 * and sunk the same way as the large one, standing on its one-cell trunk. */
static void EmitSmallCell(VoxelBuilder *builder, int x, int y)
{
    float wx = (float)x, wz = (float)y;
    const float rise = 1.532089f, run = 1.285575f;
    const float baseHeight = -0.10f;
    /* 0.15 further forward than half the large tree's: any less and the
     * leaves stand through the ground behind the trunk. */
    float baseZ = wz + 0.825f;

    VoxelMesh_Top(builder, wx, wz, 0.0f, 0.0f,
                  48.0f / VOXEL_TREE_TEXTURE_DIM, 0.5f, 1.0f, 0.25f, 1.0f);
    builder->rounded = true;
    VoxelBuilder_Quad(builder,
        &(VoxelVertex){wx,        baseHeight + rise, baseZ - run, 0.5f,  0.5f, 1.0f},
        &(VoxelVertex){wx + 1.0f, baseHeight + rise, baseZ - run, 0.75f, 0.5f, 1.0f},
        &(VoxelVertex){wx + 1.0f, baseHeight,        baseZ,       0.75f, 0.0f, 1.0f},
        &(VoxelVertex){wx,        baseHeight,        baseZ,       0.5f,  0.0f, 1.0f});
    builder->rounded = false;
}

static void EmitCell(VoxelBuilder *builder, int x, int y, int part)
{
    int col = part & 1, row = part >> 1;
    float wx = (float)x, wz = (float)y;
    float u0 = (32.0f + col * 16.0f) / VOXEL_TREE_TEXTURE_DIM;
    float u1 = u0 + 16.0f / VOXEL_TREE_TEXTURE_DIM;
    float v0 = 1.0f - row * 16.0f / VOXEL_TREE_TEXTURE_DIM;
    float v1 = v0 - 16.0f / VOXEL_TREE_TEXTURE_DIM;
    /* The crown keeps its 32:36 aspect ratio: width 2, length 2.25 tiles.
     * sin/cos(50 degrees), fixed in the world rather than camera billboarding.
     * Lower the transparent bottom margin into the ground so the visible
     * leaves overlap the trunk instead of exposing a horizontal cut. */
    const float rise = 1.723600f, run = 1.446272f;
    const float baseHeight = -0.10f;
    float baseZ = wz - row + 1.35f;
    float top = 1.0f - row * 0.5f, bottom = top - 0.5f;

    if (part == VOXEL_TREE_SMALL)
    {
        EmitSmallCell(builder, x, y);
        return;
    }
    VoxelMesh_Top(builder, wx, wz, 0.0f, 0.0f, u0, v0, u1, v1, 1.0f);

    u0 = col * 16.0f / VOXEL_TREE_TEXTURE_DIM;
    u1 = u0 + 16.0f / VOXEL_TREE_TEXTURE_DIM;
    v0 = 1.0f - row * 18.0f / VOXEL_TREE_TEXTURE_DIM;
    v1 = v0 - 18.0f / VOXEL_TREE_TEXTURE_DIM;
    /* A crown card: lit as the rounded crown it stands for. */
    builder->rounded = true;
    VoxelBuilder_Quad(builder,
        &(VoxelVertex){wx,        baseHeight + top * rise,    baseZ - top * run,    u0, v0, 1.0f},
        &(VoxelVertex){wx + 1.0f, baseHeight + top * rise,    baseZ - top * run,    u1, v0, 1.0f},
        &(VoxelVertex){wx + 1.0f, baseHeight + bottom * rise, baseZ - bottom * run, u1, v1, 1.0f},
        &(VoxelVertex){wx,        baseHeight + bottom * rise, baseZ - bottom * run, u0, v1, 1.0f});
    builder->rounded = false;
}

void VoxelTree_EmitInstance(VoxelBuilder *builder, const VoxelMapInstance *inst,
                            int x0, int y0, int x1, int y1)
{
    if (!VoxelWorld_UsesTreeSprites(inst))
        return;
    if (x0 < inst->originX) x0 = inst->originX;
    if (y0 < inst->originY) y0 = inst->originY;
    if (x1 > inst->originX + inst->width) x1 = inst->originX + inst->width;
    if (y1 > inst->originY + inst->height) y1 = inst->originY + inst->height;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
        {
            int part = VoxelTree_Part(VoxelWorld_GetMetatileId(x, y));
            if (part >= 0)
            {
                builder->lift = VoxelRelief_CellLift(inst, x, y);
                builder->shift = VoxelRelief_CellShift(inst, x, y);
                EmitCell(builder, x, y, part);
                builder->lift = 0.0f;
                builder->shift = 0.0f;
            }
        }
}

void VoxelTree_EmitBorder(VoxelBuilder *builder, int x0, int y0, int x1, int y1)
{
    if (!VoxelWorld_UsesTreeSprites(VoxelWorld_Instance(0)))
        return;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
        {
            int part;
            if (VoxelWorld_GetInstanceAt(x, y) != NULL)
                continue;
            part = VoxelTree_Part(VoxelWorld_BorderMetatile(x, y));
            if (part >= 0)
                EmitCell(builder, x, y, part);
        }
}
