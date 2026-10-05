/* rg_behavior.h -- metatile behaviour sets romgen classifies by (3DGBA, GPLv3). Pure C.
 *
 * The ROM stores only the numbers (low byte of each metatile attribute), so the sets the upstream
 * generator names by enum are fixed value tables here. Sources per value: docs/phase33-romgen/
 * SPEC-S0-S1.md section 2 (our gba_game.h, SPEC-data, and a census of the user's ROM). */
#ifndef RG_BEHAVIOR_H
#define RG_BEHAVIOR_H

#include <stdbool.h>
#include <stdint.h>

/* "Drawn as water", not the surfable set (MB_WATER_DOOR 0x6C, 0x6D, 0x6F, 0x1A are not water). */
static inline bool rg_is_water(unsigned b)
{
    switch (b) {
    case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17:
    case 0x19: case 0x22: case 0x28: case 0x2A: case 0x2B:
    case 0x50: case 0x51: case 0x52: case 0x53:
        return true;
    default:
        return false;
    }
}

/* Every MB_JUMP_*: E, W, N, S and the four diagonals. */
static inline bool rg_is_jump(unsigned b)
{
    return b >= 0x38u && b <= 0x3Fu;
}

/* MB_ANIMATED_DOOR 0x69, MB_PETALBURG_GYM_DOOR 0x8D, MB_CLOSED_SOOTOPOLIS_DOOR 0x8B. */
static inline bool rg_is_house_door(unsigned b)
{
    return b == 0x69u || b == 0x8Du || b == 0x8Bu;
}

#endif
