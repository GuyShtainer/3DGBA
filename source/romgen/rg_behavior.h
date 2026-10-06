/* rg_behavior.h -- metatile behaviour sets romgen classifies by (3DGBA, GPLv3). Pure C.
 *
 * The ROM stores only the numbers (low byte of each metatile attribute), so the sets the upstream
 * generator names by enum are fixed value tables here. Sources per value: docs/phase33-romgen/
 * SPEC-S0-S1.md section 2 (our gba_game.h, SPEC-data, and a census of the user's ROM). */
#ifndef RG_BEHAVIOR_H
#define RG_BEHAVIOR_H

#include <stdbool.h>
#include <stdint.h>

/* PHASE 34: these predicates are the EMERALD sets and keep their exact meaning for the Emerald-only relief modules
 * (rg_ralias, rg_r*). Generic romgen code reads the per-game sets from the profile instead: gp_beh(&rg_lprof(L)->water, b),
 * see rg_gameprof.h (the Emerald row is generated from these predicates and a test pins the equality). */

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

/* Behaviour sets of the relief generator (A.6, SPEC-S3; pokeemerald@731ad5b include/constants/metatile_behaviors.h,
 * enum counted to 0xF0). Numbers only. ROM spot check: T9. */
#define RG_MB_WATERFALL 0x13u
#define RG_MB_BERRY_TREE_SOIL 0xA0u

/* FLAT_BEHAVIOURS (rel:612-618): bridges, logs, doors and the no-running mat; 24 values. */
static inline bool rg_is_flat_behaviour(unsigned b)
{
    return b == 0x0Au || b == 0x60u || b == 0x69u || b == 0x6Cu || (b >= 0x70u && b <= 0x78u)
           || (b >= 0x7Au && b <= 0x7Fu) || b == 0x8Bu || b == 0x8Cu || b == 0x8Du || b == 0xBEu || b == 0xEAu;
}

/* SANDS (rel:619). */
static inline bool rg_is_sand(unsigned b)
{
    return b == 0x06u || b == 0x21u || b == 0xBFu;
}

#endif
