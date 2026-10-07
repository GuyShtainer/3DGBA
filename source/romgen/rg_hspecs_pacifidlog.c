/* rg_hspecs_pacifidlog.c -- Pacifidlog Town recipe (3DGBA original work, GPLv3). Phase 36 slice H2.
 *
 * Every number was read off `romgen author roms/emerald.gba art 16 ...` (Ruby and Sapphire share the layout byte for
 * byte, docs/phase36-hoenn/PHASE.md). The town's five huts are one drawing: the same 3x4 metatiles (rows 10-13 of the
 * hut at door (16,13)) stand at every door, so one model takes five placements.
 *
 *   pacifidlog_hut   3x4 cells (48x64), layout 16 rect (15, 10): the round hut with the thatched roof and its finial,
 *                    standing on its raft */
#include "rg_hspecs.h"

#include <string.h>

static RgStrip h_strip(double v0, double v1, double u0, double u1)
{
    RgStrip s;

    memset(&s, 0, sizeof(s));
    s.fixed[0] = v0;
    s.fixed[1] = v1;
    s.hasWrap = true;
    s.wrap[0] = u0;
    s.wrap[1] = u1;
    return rg_strip_fin(s);
}

/* The hut is round, so it is an octagonal frustum: walls (the plank front with the door) 42-64, the thatch as the
 * 45-degree band over rows 16-42, a level top; the finial (rows 1-16) stands on the top as a projected slab. Every face
 * the front camera sees is projected (RG_P_FRUSTUM draws them so), the side and back faces tile the front's planks and
 * thatch. The thatch's 2 px overhang past the walls is left out. */
bool rg_h_pacifidlog_hut(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    const double front = 63, back = 35, l = 3, r = 45, c = 8, wall = 21, rise = 13;
    RgPart *body = rg_parts_add(out, RG_P_FRUSTUM, "hut");
    RgFrustum *f;
    double zt = front - rise, yt = wall + rise;
    const double fin[4][2] = {{zt, yt}, {zt, yt + 15}, {zt - 1, yt + 15}, {zt - 1, yt}};
    const double finRows[4][2] = {{zt - yt - 15, zt - yt}, {0, 0}, {0, 0}, {0, 0}};

    (void)spec; (void)a0; (void)a1;
    if (body == NULL)
        return false;
    f = &body->u.frustum;
    f->nPlan = 8;
    f->plan[0][0] = l + c; f->plan[0][1] = front;
    f->plan[1][0] = r - c; f->plan[1][1] = front;
    f->plan[2][0] = r;     f->plan[2][1] = front - c;
    f->plan[3][0] = r;     f->plan[3][1] = back + c;
    f->plan[4][0] = r - c; f->plan[4][1] = back;
    f->plan[5][0] = l + c; f->plan[5][1] = back;
    f->plan[6][0] = l;     f->plan[6][1] = back + c;
    f->plan[7][0] = l;     f->plan[7][1] = front - c;
    f->wallTop = wall;
    f->wallSide = h_strip(42, 63, 5, 15);
    f->bandRise = rise;
    f->bandSide = h_strip(28, 41, 8, 40);
    f->top = h_strip(17, 27, 16, 32);
    return rg_h_profile(out, "finial", 17, 31, true, 4, fin, finRows) && !out->failed;
}

/* The finial's knob and the thatch's crown, the front of the thatch and the walls with the door. */
const RgExact rg_h_pacifidlog_hut_exact[3] = {
    {19, 2, 29, 16, false},     /* finial */
    {16, 17, 32, 42, false},    /* thatch, front facet */
    {16, 42, 32, 63, false},    /* door facet */
};
