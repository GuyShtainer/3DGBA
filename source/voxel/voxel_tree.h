/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/src/voxel/voxel_tree.h,
 * MIT License - see source/voxel/NOTICE.md. */
/* General tileset trees: ground trunk and a north-leaning cutout crown. */
#ifndef CTR_VOXEL_TREE_H
#define CTR_VOXEL_TREE_H

#include "voxel_mesh_builder.h"

#define VOXEL_TREE_TEXTURE_DIM 64u
/* 3DGBA: the tree art is the embedded voxel_trees_bin (was romfs:/voxel/trees.rgba5551) */

/* A small tree is one cell: its trunk, with the whole crown standing on it. */
#define VOXEL_TREE_SMALL 4

/* 3DGBA (look backlog L1): a one-cell bush, the game profile's shrub table.
 * It stands up as a card of its own upper layer over its lower layer, both
 * from the map's atlas (VOXEL_SHRUB_GROUND / VOXEL_SHRUB_LEAVES), so unlike
 * the parts above it is drawn with the terrain, not with the tree texture,
 * and VoxelTree_Part never returns it. */
#define VOXEL_TREE_SHRUB 5

/* General-only IDs: -1 for other art, the 2x2 quadrant (row*2+col) of a
 * large tree, or VOXEL_TREE_SMALL. */
int VoxelTree_Part(int metatileId);
/* Remove the old canopy from the cell above a tree, leaving its ground. */
int VoxelTree_GroundMetatile(int metatileId);

/* The shrub (VOXEL_TREE_SHRUB) a cell's metatile is: its index in the game
 * profile's shrub table (< VOXEL_SHRUBS), or -1. Only where tree sprites are
 * drawn (VoxelWorld_UsesTreeSprites); a secondary id counts only with its own
 * secondary tileset. */
int VoxelTree_Shrub(const VoxelMapInstance *inst, int metatileId);
/* For the atlas: whether shrub `k` belongs to this tileset pair, and its
 * metatile id. */
bool VoxelTree_ShrubSource(const void *primaryTileset, const void *secondaryTileset,
                           unsigned k, unsigned *metatileId);
/* The bush card standing on cell (x, y), textured with the leaves at
 * [u0,u1] x [v0,v1] (v0 the top row). The builder's lift applies. */
void VoxelTree_EmitShrubCard(VoxelBuilder *builder, int x, int y,
                             float u0, float v0, float u1, float v1);

/* 3DGBA (look backlog L2): tall grass. The index k (< VOXEL_GRASSES) of a cell's metatile among its tileset pair's
 * blade-grass metatiles (profile bladeGrass behaviour, id order), or -1. Only where tree sprites are drawn. */
int VoxelTree_Grass(const VoxelMapInstance *inst, int metatileId);
#define VOXEL_GRASS_CARDS 2u   /* cards per grass cell (2 quads, 12 vertices) */
/* For the atlas: blade-grass entry k of this tileset pair, and its metatile id. */
bool VoxelTree_GrassSource(const void *primaryTileset, const void *secondaryTileset,
                           unsigned k, unsigned *metatileId);
/* The blade card standing on cell (x, y), textured with the blades at [u0,u1] x [v0,v1]. The builder's lift applies. */
void VoxelTree_EmitGrassCard(VoxelBuilder *builder, int x, int y,
                             float u0, float v0, float u1, float v1);

/* Appended after the ordinary terrain; these vertices use the tree texture. */
void VoxelTree_EmitInstance(VoxelBuilder *builder, const VoxelMapInstance *inst,
                            int x0, int y0, int x1, int y1);
void VoxelTree_EmitBorder(VoxelBuilder *builder, int x0, int y0, int x1, int y1);

#endif
