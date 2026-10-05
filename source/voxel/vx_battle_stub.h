/* vx_battle_stub.h -- battle scenery interface the vendored ctr_voxel.c links against (3DGBA,
 * GPLv3). Our own declarations; battle scenery is a future phase, so every function is a stub
 * (vx_battle_stub.c). The tuning constants are the ones ctr_voxel.c's battle camera reads. */
#ifndef VX_BATTLE_STUB_H
#define VX_BATTLE_STUB_H

#include <stdbool.h>

#define VOXEL_BATTLE_PITCH 30.0f
#define VOXEL_BATTLE_DISTANCE 7.0f
#define VOXEL_BATTLE_FOV 35.0f

bool VoxelBattle_GameInBattle(void);
bool VoxelBattle_IsLink(void);
void VoxelBattle_BeginStage(void);
bool VoxelBattle_StepStage(unsigned cells, unsigned candidates, float *targetX, float *targetZ,
                           float *ground);

typedef struct
{
    float x, y, rx, ry;
} VoxelBattleShadow;
unsigned VoxelBattle_Shadows(VoxelBattleShadow *out, unsigned max);

#endif
