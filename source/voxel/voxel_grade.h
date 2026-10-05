/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/src/voxel/voxel_grade.h,
 * MIT License - see source/voxel/NOTICE.md. */
/*
 * The voxel world's colour grade: the art a little more saturated, applied as
 * its colours become textures - palettes, sprites, the tree texture, building
 * pages - so it costs nothing per frame. The 2D picture is not graded.
 */
#ifndef VOXEL_GRADE_H
#define VOXEL_GRADE_H

#include <stdint.h>

/* Builds the table. Before any texture is made, and once. */
void VoxelGrade_Init(void);
/* A GBA BGR555 colour to the graded, opaque PICA RGBA5551 texel. */
uint16_t VoxelGrade_RGBA5551(uint16_t bgr15);
/* Grades RGBA5551 texels in place, keeping each one's alpha bit. */
void VoxelGrade_Texels(uint16_t *texels, unsigned count);
/* Scales RGBA5551 texels' brightness in place, keeping their alpha bit. */
void VoxelGrade_Brighten(uint16_t *texels, unsigned count, float factor);

#endif
