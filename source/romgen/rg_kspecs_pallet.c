/* rg_kspecs_pallet.c -- Pallet Town building recipes (FireRed / LeafGreen layout 78). 3DGBA original work, GPLv3,
 * "Kanto recipes". Phase 34 slice K1 (docs/phase34-frlg/SPEC.md sections 5.3-5.6).
 *
 * Every number below was read off the `romgen author ... art` pictures of the user's own ROM: art rows are numbers in
 * a 16-px-per-cell picture, row = z - y for the oblique camera of the renderer (so a wall that is `h` high and stands at
 * depth z shows between rows z - h and z). The builders reuse the Emerald part library (rg_geom) unchanged.
 *
 *   k_pallet_house  5x4 cells (80x64): a gabled / hipped house, roof slope above a plain facade. Two placements.
 *   k_pallet_lab    7x4 cells (112x64): Oak's Lab, a flat roof with a vent unit over a yellow brick front. */
#include "rg_bspecs.h"

#include <string.h>

#define PL_GRASS 0x001
/* Layout 78 (Pallet Town), FNV-1a-32 of its blockdata; FireRed and LeafGreen are identical (SPEC section 5.3). */
#define L_PALLET 78, 0x843369BBu

static RgStrip pl_strip2(double a, double b)
{
    RgStrip s;

    memset(&s, 0, sizeof(s));
    s.fixed[0] = a;
    s.fixed[1] = b;
    return s;
}

static RgStrip pl_wrap(RgStrip s, double lo, double hi)
{
    s.hasWrap = true;
    s.wrap[0] = lo;
    s.wrap[1] = hi;
    return s;
}

static RgStrip pl_repeat(RgStrip s, double c, double d)
{
    s.hasRepeat = true;
    s.repeat[0] = c;
    s.repeat[1] = d;
    return s;
}

static void pl_pt(double (*poly)[2], unsigned i, double z, double y)
{
    poly[i][0] = z;
    poly[i][1] = y;
}

/* [(front, ylo), (front, yhi), (back, yhi), (back, ylo)] */
static void pl_box(RgPrism *p, double front, double ylo, double yhi, double back)
{
    p->nPoly = 4;
    pl_pt(p->poly, 0, front, ylo);
    pl_pt(p->poly, 1, front, yhi);
    pl_pt(p->poly, 2, back, yhi);
    pl_pt(p->poly, 3, back, ylo);
}

/* ---- k_pallet_house: 80x64 art ------------------------------------------------------------------------------------ */
/* Art rows: roof slope 0-23 (a stripe every 8 rows), eave 24-31 (outline, red gutter, grey underside), facade 32-63.
 * The hip ends are the peach (west) and red (east) bands in columns 0-12 and 68-80. */
static bool k_pallet_house(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    double front_z = 64, back_z = 27, overhang = 2, wall_top = 34;
    RgPart *base = rg_parts_add(out, RG_P_PRISM, "ground_floor");
    RgPart *rf = rg_parts_add(out, RG_P_HIPROOF, "roof");
    RgHip *roof;
    RgPrism *pr;
    RgBand b;
    RgTile post, plaster;

    (void)spec; (void)a0; (void)a1;
    if (base == NULL || rf == NULL)
        return false;
    roof = &rf->u.hip;
    roof->x0 = 0; roof->x1 = 80;
    roof->zf = front_z + overhang; roof->zb = back_z - overhang; roof->y0 = wall_top;
    roof->fascia = rg_strip_fin(pl_wrap(pl_strip2(24, 32), 14, 66));
    roof->slope = rg_strip_fin(pl_wrap(pl_repeat(pl_strip2(20, 24), 12, 20), 14, 66));
    roof->teeth[0] = 14; roof->teeth[1] = 16;
    roof->cap[0] = 12; roof->cap[1] = 16;
    roof->pitch = 22.0; roof->run = 12;
    roof->ridgeU[0] = 0; roof->ridgeU[1] = 80;
    roof->endTile = rg_tile(14, 12, 20, 16);
    roof->ridge = true;
    roof->hasRidgeWrap = true; roof->ridgeWrap[0] = 14; roof->ridgeWrap[1] = 66;
    rg_hip_init(roof);

    post = rg_tile_top(2, 32, 8, 64, 32);
    plaster = rg_tile_top(9, 32, 15, 64, 32);
    pr = &base->u.prism;
    pr->x0 = 2; pr->x1 = 78;
    pr->west = pr->east = true;
    pl_box(pr, front_z, 0, wall_top, back_z);
    pr->edges[0].kind = RG_EM_PROJ;
    pr->edges[0].proj = rg_proj_rows(32, 64);
    pr->edges[2].kind = RG_EM_TILE;
    pr->edges[2].tile = plaster;
    pr->skip = (1u << 1) | (1u << 3);
    b = rg_band(0, 32, plaster, front_z);
    b.hasFront = true; b.front = post;
    b.hasBack = true;  b.back = post;
    rg_band_z1(&b, back_z);
    pr->hasCaps = true;
    pr->caps[pr->nCaps++] = b;
    pr->caps[pr->nCaps++] = rg_band(32, wall_top + 1, rg_tile_top(9, 32, 15, 33, wall_top), front_z);
    return !out->failed;
}

/* ---- k_pallet_lab: 112x64 art ------------------------------------------------------------------------------------- */
/* Art rows: flat roof 0-23 (a 8x8 grid), cornice 24-31, yellow brick facade 32-63. A vent unit stands on the roof at
 * columns 80-104: its top 0-8, its front 9-20. rg_flat_block is the Emerald flat-roofed block builder. */
static bool k_pallet_lab(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    static const double roof[3][2] = {{0, 24}, {8, 16}, {0, 8}};
    static const double cornice[2] = {24, 32};
    static const double unit[5] = {80, 104, 0, 9, 21};

    (void)spec; (void)a0; (void)a1;
    return rg_flat_block(out, 112, 64, roof, cornice, 32, unit);
}

/* x, y, x1, y1 in art pixels; the comments name the part each rectangle pins. */
static const RgExact kPalletHouseExact[2] = {
    {2, 32, 78, 64, false},     /* facade */
    {14, 0, 66, 32, false},     /* roof slope and eave */
};
static const RgExact kPalletLabExact[2] = {
    {0, 32, 112, 64, false},    /* facade */
    {0, 0, 112, 32, false},     /* roof, vent unit and cornice */
};

const RgSpec rg_kspecs_pallet[] = {
    {"k_pallet_house", RG_SPEC_DIRECT, L_PALLET, {5, 4, 5, 4}, {0, 0}, {PL_GRASS}, 1, kPalletHouseExact, 2,
     k_pallet_house, 0, 0, NULL},
    {"k_pallet_lab", RG_SPEC_DIRECT, L_PALLET, {13, 10, 7, 4}, {0, 0}, {PL_GRASS}, 1, kPalletLabExact, 2,
     k_pallet_lab, 0, 0, NULL},
};
const unsigned rg_kspecs_pallet_count = sizeof(rg_kspecs_pallet) / sizeof(rg_kspecs_pallet[0]);
