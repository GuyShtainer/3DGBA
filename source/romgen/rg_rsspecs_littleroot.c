/* rg_rsspecs_littleroot.c -- the Ruby / Sapphire Littleroot lab (3DGBA original work, GPLv3). Phase 35 slice S4.
 *
 * Emerald's lab recipe (rg_littleroot_lab) builds on Ruby / Sapphire too (the layout pin matches) but fails its art gate:
 * RS draws the roof differently (a corrugated slope under a light ridge band, a square vent instead of Emerald's tiled
 * slope and round cowl). This is that building rebuilt for the RS art, in the Kanto recipes' style (rg_kspecs_*.c): every
 * face the front camera sees is a PROJ edge, which copies the art by row (art row = z - y), so the ortho gate holds by
 * construction where the geometry is right. Every number was read off `romgen author roms/ruby.gba art 10 3 12 7 5`
 * (Ruby and Sapphire are byte-identical here).
 *
 *   rs_littleroot_lab   7x5 cells (112x80 art), layout 10 rect (3, 12), one placement. */
#include "rg_bspecs.h"

#include <math.h>
#include <string.h>

#define L_LITTLEROOT 10, 0xEFE99674u      /* same layout and pin as Emerald's (docs/phase35-rs/PHASE.md, recon) */
#define RS_ROOF_PITCH 15.0                /* the slope is authored at rg_close_backs' L6b pitch, so it is not laid down */

static void rl_pt(double (*poly)[2], unsigned i, double z, double y)
{
    poly[i][0] = z;
    poly[i][1] = y;
}

/* A slice of a profile: poly points (z, y), each edge PROJ-textured from the listed art rows ({0, 0} = skipped). */
static bool rl_profile(RgPartList *out, const char *name, double x0, double x1, unsigned n, const double (*pts)[2],
                       const double (*rows)[2])
{
    RgPart *pt = rg_parts_add(out, RG_P_PRISM, name);
    RgPrism *pr;
    unsigned i;

    if (pt == NULL)
        return false;
    pr = &pt->u.prism;
    pr->x0 = x0; pr->x1 = x1;
    pr->west = pr->east = true;
    pr->nPoly = n;
    pr->skip = 0;
    for (i = 0; i < n; i++) {
        rl_pt(pr->poly, i, pts[i][0], pts[i][1]);
        if (rows[i][0] == 0 && rows[i][1] == 0) {
            pr->skip |= 1u << i;
        } else {
            pr->edges[i].kind = RG_EM_PROJ;
            pr->edges[i].proj = rg_proj_rows(rows[i][0], rows[i][1]);
        }
    }
    return true;
}

/* Rows (x 0-111): light ridge band 0-13, corrugated front slope 13-50, eave scallops and fascia 50-53, soffit shadow and
 * facade 53-80 (windows, the door at x 63-80, the posts at both ends). The vent stands on the slope over x 16-47: its
 * base meets the roof at row 31, the base's front face rows 22-31, the hood's front 19-22 and its top 1-19. */
static bool rs_littleroot_lab(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    const double front = 80, wallTop = 27, eave = 30, tp = tan(RS_ROOF_PITCH * 0.017453292519943295);
    double slopeD = 37.0 / (1.0 + tp), zr = front - slopeD, yr = eave + 37.0 - slopeD;
    double capD = 13.0 / (1.0 + tan(5.0 * 0.017453292519943295)), zc = zr - capD, yc = yr + 13.0 - capD;
    double t = (50.0 - 31.0) / 37.0, zv = front - t * slopeD, yv = eave + t * (37.0 - slopeD);
    double body[7][2], bodyRows[7][2] = {{53, 80}, {50, 53}, {31, 50}, {13, 31}, {0, 13}, {0, 0}, {0, 0}};
    double base[4][2], baseRows[4][2] = {{22, 31}, {4, 22}, {0, 0}, {0, 0}};
    double hood[4][2], hoodRows[4][2] = {{19, 22}, {1, 19}, {0, 0}, {0, 0}};
    RgPart *mid;

    (void)spec; (void)a0; (void)a1;
    /* the body: wall, fascia, the 15-degree slope (split at the vent's foot, row 31), then the ridge band as a 5-degree
     * rise to the top; the open back is mirrored about the ridge by rg_close_backs */
    rl_pt(body, 0, front, 0);
    rl_pt(body, 1, front, wallTop);
    rl_pt(body, 2, front, eave);
    rl_pt(body, 3, zv, yv);
    rl_pt(body, 4, zr, yr);
    rl_pt(body, 5, zc, yc);
    rl_pt(body, 6, zc, 0);
    /* the vent: its base sunk 6 px into the roof at the row-31 line of the slope; the hood on the base's front edge */
    rl_pt(base, 0, zv, yv - 6);
    rl_pt(base, 1, zv, zv - 22);
    rl_pt(base, 2, zv - 18, zv - 22);
    rl_pt(base, 3, zv - 18, yv - 6);
    rl_pt(hood, 0, zv, zv - 22);
    rl_pt(hood, 1, zv, zv - 19);
    rl_pt(hood, 2, zv - 18, zv - 19);
    rl_pt(hood, 3, zv - 18, zv - 22);
    if (!rl_profile(out, "body", 0, 19, 7, (const double (*)[2])body, (const double (*)[2])bodyRows) ||
        !rl_profile(out, "body_mid", 19, 44, 7, (const double (*)[2])body, (const double (*)[2])bodyRows) ||
        !rl_profile(out, "body", 44, 112, 7, (const double (*)[2])body, (const double (*)[2])bodyRows))
        return false;
    /* Behind the vent the art rows of the slope and the band hold the vent's own picture, which a PROJ face (and its
     * mirror on the rear slope) would paint on the roof a second time. The front camera never sees those faces (the vent
     * stands in front), so they take one texel of the slope's and of the band's colour instead (FLAT: a mirror keeps it). */
    mid = rg_parts_at(out, out->n - 2u);
    mid->u.prism.edges[3].kind = RG_EM_FLAT;
    mid->u.prism.edges[3].flat[0] = 62.5;
    mid->u.prism.edges[3].flat[1] = 34.5;
    mid->u.prism.edges[4].kind = RG_EM_FLAT;
    mid->u.prism.edges[4].flat[0] = 60.5;
    mid->u.prism.edges[4].flat[1] = 3.5;
    return rl_profile(out, "vent_base", 16, 48, 4, (const double (*)[2])base, (const double (*)[2])baseRows) &&
           rl_profile(out, "vent_hood", 19, 44, 4, (const double (*)[2])hood, (const double (*)[2])hoodRows) &&
           !out->failed;
}

static const RgExact kRsLabExact[2] = {
    {2, 53, 112, 80, false},    /* facade */
    {1, 1, 111, 53, false},     /* ridge band, slope with the vent, eave */
};

/* look L5 (rg_close_sides): the ends are dressed from the facade's own rows; these patches are the fallback. */
static const RgSideCfg kRsLabSide[1] = {
    {"body", {8, 72, 16, 78}, {60, 32, 64, 40}, 27, true},
};

const RgSpec rg_rsspecs_littleroot[] = {
    {"rs_littleroot_lab", RG_SPEC_DIRECT, L_LITTLEROOT, {3, 12, 7, 5}, {0, 0}, {0x001}, 1, kRsLabExact, 2,
     rs_littleroot_lab, 0, 0, kRsLabSide},
};
const unsigned rg_rsspecs_littleroot_count = sizeof(rg_rsspecs_littleroot) / sizeof(rg_rsspecs_littleroot[0]);
