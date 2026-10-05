/* vx_battle_stub.c -- battle scenery stubs (3DGBA, GPLv3). CtrVoxel_BeginBattle is never called
 * in this build, so these only satisfy the link; the voxel gate is off outside the field. */
#include "vx_battle_stub.h"

#include "vx_adapter.h"

bool VoxelBattle_GameInBattle(void)
{
    return vx_snap()->inBattle != 0;
}

bool VoxelBattle_IsLink(void)
{
    return false;
}

void VoxelBattle_BeginStage(void)
{
}

bool VoxelBattle_StepStage(unsigned cells, unsigned candidates, float *targetX, float *targetZ,
                           float *ground)
{
    (void)cells;
    (void)candidates;
    (void)targetX;
    (void)targetZ;
    (void)ground;
    return false;
}

unsigned VoxelBattle_Shadows(VoxelBattleShadow *out, unsigned max)
{
    (void)out;
    (void)max;
    return 0;
}
