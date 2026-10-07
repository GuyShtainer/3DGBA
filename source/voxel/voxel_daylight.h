/* voxel_daylight.h -- the day cycle of the voxel world's light (3DGBA, GPLv3). Look backlog L4.
 * Pure C, host-testable: no libctru, no GPU. The renderer (ctr_voxel.c) reads the clock, asks
 * this module where the sun is and what colour the light has, and re-bakes the chunks.
 *
 * Two clocks of different speed:
 *  - The SUN THAT IS BAKED into the chunks' vertex shades (voxel_lighting.c: the shadow march
 *    and the face term) moves in steps of VOXEL_SUN_STEP_MINUTES. A step re-bakes every lit
 *    chunk, a few per frame (ctr_voxel.c), so it is rare: 144 times a day.
 *  - The TINT (the grade's sun and shade colours, the haze colour, how much glow, dapple, rays
 *    and dust there is) is a few uniforms and changes continuously, every frame, for nothing.
 * The tint carries the visible change of the day; the baked sun only moves the shadows, a small
 * distance each step, so a step does not pop.
 *
 * The day passes through L3's sun at VOXEL_SUN_REF_MINUTE exactly, and its tint there is the
 * identity: at that time the picture is the one L3 made, bit for bit. */
#ifndef VOXEL_DAYLIGHT_H
#define VOXEL_DAYLIGHT_H

#include <stdbool.h>
#include <stdint.h>

/* L3's sun (look backlog L3): south of the scene, high, a little to the west. It lies along
 * (-DX, 1, -DZ) from a point, +Z south. The fixed sun when the cycle is off. */
#define VOXEL_SUN_REF_DX 0.45f
#define VOXEL_SUN_REF_DZ (-0.65f)
/* The minute of the day at which the cycle's sun is exactly L3's: 14:00. */
#define VOXEL_SUN_REF_MINUTE (14u * 60u)

#define VOXEL_DAY_MINUTES 1440u
#define VOXEL_SUN_STEP_MINUTES 10u
#define VOXEL_SUN_STEPS (VOXEL_DAY_MINUTES / VOXEL_SUN_STEP_MINUTES)

typedef struct
{
    float dx, dz;
} VoxelSunDir;

/* Multipliers on the weather's light (ctr_voxel.c VoxelLight): all 1 by day. */
typedef struct
{
    float sun[3], shade[3]; /* the grade at full sun and in shadow */
    float haze[3];          /* the colour the distance fades to */
    float glow;             /* rays, dust, bloom and the dapples' contrast: 0 at night */
    float shadow;           /* the people's cast shadows */
} VoxelDayTint;

/* The minute of the day, [0, 1440), wrapped from any value. */
float VoxelDaylight_Wrap(float minute);
/* Where the sun (by night the moon) is, continuously. Always south of the scene (dz < 0). */
VoxelSunDir VoxelDaylight_Sun(float minute);
/* The baked sun's step for a minute, and the sun of a step (the sun at the step's first minute). */
unsigned VoxelDaylight_Step(float minute);
VoxelSunDir VoxelDaylight_StepSun(unsigned step);
/* The step of VOXEL_SUN_REF_MINUTE, whose sun is L3's. */
#define VOXEL_SUN_REF_STEP (VOXEL_SUN_REF_MINUTE / VOXEL_SUN_STEP_MINUTES)
VoxelDayTint VoxelDaylight_Tint(float minute);

/*
 * The test/debug override, sdmc:/3DGBA/daylight.txt (read once by ctr_voxel.c; no UI):
 *   "off"          the cycle is off: L3's fixed sun, no tint, nothing re-baked
 *   "HH:MM"        the clock stands at that time of day
 *   "HH:MM xN"     ... and runs N times as fast from there (N 1..3600)
 * Anything else, or no file: the 3DS clock.
 */
typedef enum
{
    VOXEL_DAY_LIVE,  /* the 3DS clock */
    VOXEL_DAY_OFF,
    VOXEL_DAY_FIXED  /* `minute`, advancing `speed` minutes per real minute (0: standing) */
} VoxelDayMode;

typedef struct
{
    VoxelDayMode mode;
    float minute;
    float speed;
} VoxelDayOverride;

/* Parses the file's text; false (and LIVE) for anything not understood. */
bool VoxelDaylight_ParseOverride(const char *text, VoxelDayOverride *out);
/* The minute of the day the override says, `seconds` after it was read. */
float VoxelDaylight_OverrideMinute(const VoxelDayOverride *o, double seconds);

/*
 * The re-bake pace: at most `cap` sun re-bakes may START per frame (a started one is time-sliced
 * by the build budget like any other chunk). Tracks the frame it counts for itself.
 */
typedef struct
{
    uint32_t frame;
    unsigned started;
    unsigned cap;
} VoxelRebakePacer;

#define VOXEL_SUN_REBAKES_PER_FRAME 1u

void VoxelRebake_Init(VoxelRebakePacer *p, unsigned cap);
/* May a sun re-bake start in `frame`? Counts it if so. */
bool VoxelRebake_Take(VoxelRebakePacer *p, uint32_t frame);

#endif
