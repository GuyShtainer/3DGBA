/* rg_hspecs_verdanturf.c -- Verdanturf Town recipes (3DGBA original work, GPLv3). Phase 36 slice H1.
 *
 * Every number was read off `romgen author roms/emerald.gba art 15 ...`. Kanto-style profile prisms: every face the front
 * camera sees is a PROJ edge.
 *
 *   verdanturf_house       4x4 cells (64x64), rect (0, 11) and (16, 12): the green plank-roofed houses
 *   verdanturf_house_w     5x4 cells (80x64), rect (8, 11): the wider one in the middle
 *   battle_tent_verdanturf 5x5 cells (80x80), rect (1, 3): the Battle Tent, a round tent under the cliff */
#include "rg_hspecs.h"

#include <math.h>

/* The plank roof, 64 art rows: the light cap and its yellow ridge board 0-13, the dark plank slope 13-37, the facade
 * with the door 37-64. */
bool rg_h_verdanturf_house(const RgSpec *spec, int width, int a1, RgPartList *out)
{
    (void)spec; (void)a1;
    return rg_h_gable(out, width, 64, 37, 13);
}

/* The tent, 80 art rows: the entrance front 55-80 on one wall, then the canvas as a quarter ellipse (26 px deep, 25 px
 * high) over rows 4-55 and a level crown 0-4 (the cliff's foot shows in its top corners). The back closes flat (L6). */
bool rg_h_battle_tent_v(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    enum { N = 9 };
    const double a = 26, b = 25, wall = 25, front = 80;
    double pts[N][2], rows[N][2];
    unsigned i;

    (void)spec; (void)a0; (void)a1;
    pts[0][0] = front; pts[0][1] = 0;
    for (i = 0; i <= 4; i++) {
        double t = i * 0.39269908169872414;     /* 22.5 degrees */

        pts[1 + i][0] = front - a + a * cos(t);
        pts[1 + i][1] = wall + b * sin(t);
    }
    pts[6][0] = pts[5][0] - 4; pts[6][1] = pts[5][1];
    pts[7][0] = pts[6][0] - 1; pts[7][1] = pts[6][1];
    pts[8][0] = pts[7][0]; pts[8][1] = 0;
    for (i = 0; i < N; i++) {
        const double *p = pts[i], *q = pts[(i + 1) % N];
        double r0 = p[0] - p[1], r1 = q[0] - q[1];

        rows[i][0] = rows[i][1] = 0;
        if (i < 6) {
            rows[i][0] = r0 < r1 ? r0 : r1;
            rows[i][1] = r0 < r1 ? r1 : r0;
        }
    }
    return rg_h_profile(out, "tent", 0, 80, true, N, (const double (*)[2])pts, (const double (*)[2])rows) &&
           !out->failed;
}

/* Row 0 is the cap's notched rim (ground pixels between the planks): the rear slope may show through it. */
const RgExact rg_h_verdanturf_house_exact[3] = {
    {0, 37, 64, 64, false},     /* facade */
    {0, 1, 64, 37, false},      /* cap, slope */
    {0, 0, 64, 1, true},
};
const RgExact rg_h_verdanturf_house_w_exact[3] = {
    {0, 37, 80, 64, false},
    {0, 1, 80, 37, false},
    {0, 0, 80, 1, true},
};
const RgExact rg_h_battle_tent_v_exact[2] = {
    {0, 55, 80, 80, false},     /* the entrance front */
    {0, 0, 80, 55, false},      /* the canvas */
};

/* look L5 (rg_close_sides): the ends are dressed from the facade's rows; these patches are the fallback. */
const RgSideCfg rg_h_verdanturf_house_side[1] = {
    {"house", {20, 48, 30, 56}, {20, 20, 30, 30}, 27, true},
};
const RgSideCfg rg_h_battle_tent_v_side[1] = {
    {"tent", {2, 70, 6, 76}, {12, 30, 20, 40}, 25, true},
};
