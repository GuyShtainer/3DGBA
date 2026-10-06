/* vx_behavior.c -- metatile behaviour predicates for the voxel classifier (3DGBA, GPLv3).
 * Our own tables: membership in the behaviour-id sets the game's own predicates compare against.
 * The ids come from SPEC-data section 5.3 and from the comparison immediates read straight out of
 * the user's ROM (docs/phase32-voxel/BUILDLOG-P2.md, "behaviour ids"). */
#include "gba_game.h"

static bool8 InSet(u8 b, const u8 *set, unsigned count)
{
    for (unsigned i = 0; i < count; ++i)
        if (set[i] == b)
            return TRUE;
    return FALSE;
}

bool8 MetatileBehavior_EmeraldIsReflective(u8 b)
{
    static const u8 set[] = {MB_POND_WATER, MB_PUDDLE, MB_UNUSED_SOOTOPOLIS_DEEP_WATER_2, MB_ICE,
                             MB_SOOTOPOLIS_DEEP_WATER, MB_REFLECTION_UNDER_BRIDGE};
    return InSet(b, set, sizeof(set));
}

bool8 MetatileBehavior_EmeraldIsIce(u8 b)
{
    return b == MB_ICE;
}

/* The 16 behaviours the game flags as surfable (SPEC-data 5.3). */
bool8 MetatileBehavior_EmeraldIsSurfableWaterOrUnderwater(u8 b)
{
    static const u8 set[] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x19, 0x22,
                             0x2A, 0x50, 0x51, 0x52, 0x53, 0x6C, 0x6D, 0x6F};
    return InSet(b, set, sizeof(set));
}

/* The renderer's predicates read the active game profile's sets (Phase 34 R2). The Emerald row's sets are built from
 * the MetatileBehavior_Emerald* tables above, so for Emerald these answer exactly what they always did (b is a u8). */
bool8 MetatileBehavior_IsReflective(u8 b) { return gp_beh(&VXP(reflective), b) ? TRUE : FALSE; }
bool8 MetatileBehavior_IsIce(u8 b) { return gp_beh(&VXP(ice), b) ? TRUE : FALSE; }
bool8 MetatileBehavior_IsSurfableWaterOrUnderwater(u8 b) { return gp_beh(&VXP(surfable), b) ? TRUE : FALSE; }
bool8 MetatileBehavior_IsShallowFlowingWater(u8 b) { return gp_beh(&VXP(shallowFlowing), b) ? TRUE : FALSE; }

bool8 MetatileBehavior_IsPuddle(u8 b)
{
    return b == MB_PUDDLE;
}

bool8 MetatileBehavior_EmeraldIsShallowFlowingWater(u8 b)
{
    static const u8 set[] = {MB_SHALLOW_WATER, MB_STAIRS_OUTSIDE_ABANDONED_SHIP,
                             MB_SHOAL_CAVE_ENTRANCE};
    return InSet(b, set, sizeof(set));
}

bool8 MetatileBehavior_IsCounter(u8 b)
{
    return b == MB_COUNTER;
}

bool8 MetatileBehavior_IsPC(u8 b)
{
    return b == MB_PC;
}

bool8 MetatileBehavior_IsSecretBasePC(u8 b)
{
    return b == MB_SECRET_BASE_PC;
}

bool8 MetatileBehavior_IsPlayerRoomPCOn(u8 b)
{
    return b == MB_PLAYER_ROOM_PC_ON;
}
