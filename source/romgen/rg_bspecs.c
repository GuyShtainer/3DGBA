/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/voxel_building_specs.py
 * (littleroot_house, HOUSE_EXACT, the first two SPECS rows), MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
#include "rg_bspecs.h"

#include <string.h>

#define GRASS 0x001
#define PITCH 22.0
#define ROOF_C0 12.0
#define ROOF_C1 68.0

static RgStrip strip2(double a, double b)
{
    RgStrip s;

    memset(&s, 0, sizeof(s));
    s.fixed[0] = a;
    s.fixed[1] = b;
    return s;
}

static RgStrip with_wrap(RgStrip s, double lo, double hi)
{
    s.hasWrap = true;
    s.wrap[0] = lo;
    s.wrap[1] = hi;
    return s;
}

static RgStrip with_repeat(RgStrip s, double c, double d)
{
    s.hasRepeat = true;
    s.repeat[0] = c;
    s.repeat[1] = d;
    return s;
}

static void set_pt(double (*poly)[2], unsigned i, double z, double y)
{
    poly[i][0] = z;
    poly[i][1] = y;
}

bool rg_littleroot_house(const RgSpec *spec, int plasterX, int unused, RgPartList *out)
{
    double px0 = plasterX, px1 = (double)plasterX + 8;
    double front_z = 80, back_z = 16;
    double overhang = 2;
    RgPart *base, *lo, *storey, *hi;
    RgHip *lower, *upper;
    RgPrism *pr;
    double z_wall, y_base, zf_hi, y0_hi, zb_hi, storey_back, wall_top;
    RgTile post, corner, plasterTop;

    (void)spec;
    (void)unused;
    /* parts are added in the order the upstream list returns them: base, lower, storey, upper */
    base = rg_parts_add(out, RG_P_PRISM, "ground_floor");
    lo = rg_parts_add(out, RG_P_HIPROOF, "roof_lo");
    storey = rg_parts_add(out, RG_P_PRISM, "storey");
    hi = rg_parts_add(out, RG_P_HIPROOF, "roof_hi");
    if (base == NULL || lo == NULL || storey == NULL || hi == NULL)
        return false;

    lower = &lo->u.hip;
    lower->x0 = 0; lower->x1 = 82;
    lower->zf = front_z + overhang; lower->zb = back_z - overhang; lower->y0 = 28;
    lower->fascia = rg_strip_fin(with_wrap(strip2(52, 54), ROOF_C0, ROOF_C1));
    /* the seven rows drawn in front of the upper storey, then courses continuing in phase */
    {
        RgStrip s = with_wrap(with_repeat(strip2(45, 52), 16, 20), ROOF_C0, ROOF_C1);

        s.hasStart = true;
        s.start = 16;
        lower->slope = rg_strip_fin(s);
    }
    lower->teeth[0] = 25; lower->teeth[1] = 32;
    lower->cap[0] = 17; lower->cap[1] = 25;
    lower->pitch = PITCH; lower->run = 7;
    lower->ridgeU[0] = 0; lower->ridgeU[1] = 80;
    lower->endTile = rg_tile(0, 25, 8, 32);
    lower->ridge = true;
    rg_hip_init(lower);

    /* The upper storey stands on the pent roof where its seventh row ends. */
    rg_hip_slope_point(lower, 7, &z_wall, &y_base);
    zf_hi = z_wall + overhang;
    y0_hi = zf_hi - 38;          /* the upper fascia is drawn on rows 36-37 */
    zb_hi = (lower->zf + lower->zb) - zf_hi;
    upper = &hi->u.hip;
    upper->x0 = 8; upper->x1 = 72;
    upper->zf = zf_hi; upper->zb = zb_hi; upper->y0 = y0_hi;
    upper->fascia = rg_strip_fin(with_wrap(strip2(36, 38), ROOF_C0, ROOF_C1));
    upper->slope = rg_strip_fin(with_wrap(with_repeat(strip2(32, 36), 16, 32), ROOF_C0, ROOF_C1));
    upper->teeth[0] = 9; upper->teeth[1] = 16;
    upper->cap[0] = 1; upper->cap[1] = 9;
    upper->pitch = PITCH; upper->run = 6;
    upper->ridgeU[0] = 8; upper->ridgeU[1] = 72;
    upper->endTile = rg_tile(12, 9, 20, 16);
    upper->ridge = true;
    rg_hip_init(upper);

    storey_back = zb_hi + overhang;
    post = rg_tile_top(10, 38, 14, 45, y0_hi);
    pr = &storey->u.prism;
    pr->x0 = 10; pr->x1 = 70;
    pr->nPoly = 4;
    set_pt(pr->poly, 0, z_wall, 28);
    set_pt(pr->poly, 1, z_wall, y0_hi);
    set_pt(pr->poly, 2, storey_back, y0_hi);
    set_pt(pr->poly, 3, storey_back, 28);
    pr->edges[0].kind = RG_EM_PROJ;
    pr->edges[0].proj = rg_proj_rows(38, 45);
    pr->edges[2].kind = RG_EM_TILE;
    pr->edges[2].tile = rg_tile_top(20, 38, 28, 45, y0_hi);
    pr->skip = (1u << 1) | (1u << 3);
    pr->west = pr->east = true;
    pr->hasCaps = true;
    pr->nCaps = 1;
    pr->caps[0] = rg_band(-64, 200, rg_tile_top(20, 38, 28, 45, y0_hi), z_wall);
    pr->caps[0].hasFront = true; pr->caps[0].front = post;
    pr->caps[0].hasBack = true;  pr->caps[0].back = post;
    rg_band_z1(&pr->caps[0], storey_back);

    wall_top = 28;
    corner = rg_tile_top(2, 54, 8, 80, 26);
    plasterTop = rg_tile_top(px0, 54, px1, 80, 26);
    pr = &base->u.prism;
    pr->x0 = 2; pr->x1 = 80;
    pr->nPoly = 4;
    set_pt(pr->poly, 0, front_z, 0);
    set_pt(pr->poly, 1, front_z, wall_top);
    set_pt(pr->poly, 2, back_z, wall_top);
    set_pt(pr->poly, 3, back_z, 0);
    pr->edges[0].kind = RG_EM_PROJ;
    pr->edges[0].proj = rg_proj_rows(54, 80);
    pr->edges[2].kind = RG_EM_TILE;
    pr->edges[2].tile = plasterTop;
    pr->skip = (1u << 1) | (1u << 3);
    pr->west = pr->east = true;
    pr->hasCaps = true;
    pr->nCaps = 2;
    pr->caps[0] = rg_band(0, 26, plasterTop, front_z);
    pr->caps[0].hasFront = true; pr->caps[0].front = corner;
    pr->caps[0].hasBack = true;  pr->caps[0].back = corner;
    rg_band_z1(&pr->caps[0], back_z);
    pr->caps[1] = rg_band(26, wall_top + 1, rg_tile_top(px0, 54, px1, 55, wall_top), front_z);
    return !out->failed;
}

/* sp:99 */
static const RgExact kHouseExact[4] = {
    {2, 54, 80, 80, false}, {12, 45, 68, 54, false}, {10, 38, 70, 45, false}, {12, 16, 68, 38, false}};

/* Layout 10 = LAYOUT_LITTLEROOT_TOWN, pinned by the FNV-1a-32 of its blockdata (SPEC-S2 section 3.1). */
const RgSpec rg_specs[] = {
    {"littleroot_house_w", RG_SPEC_DIRECT, 10, 0xEFE99674u, {2, 4, 5, 5}, {0, 0}, {GRASS}, 1, kHouseExact, 4,
     rg_littleroot_house, 8, 0},
    {"littleroot_house_e", RG_SPEC_DIRECT, 10, 0xEFE99674u, {13, 4, 5, 5}, {0, 0}, {GRASS}, 1, kHouseExact, 4,
     rg_littleroot_house, 64, 0},
};
const unsigned rg_spec_count = sizeof(rg_specs) / sizeof(rg_specs[0]);
