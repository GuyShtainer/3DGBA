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
 * columns 80-104: its top face 0-8, its front 9-20. The shape is rg_flat_block's (a front prism, a roof slab cut in
 * three around the unit, the unit), written out here so the end walls take plain brick instead of a porthole. */
#define LAB_W 112.0
#define LAB_FRONT 64.0
#define LAB_BACK 16.0
#define LAB_WALL 32.0                       /* facade rows 32-63 */
#define LAB_TOP 40.0                        /* wall + the 8 cornice rows */

static void pl_roof_slab(RgPartList *out, const char *name, double x0, double x1, double offset)
{
    RgPart *sl = rg_parts_add(out, RG_P_PRISM, name);
    RgPrism *pr;
    RgStrip s;

    if (sl == NULL)
        return;
    pr = &sl->u.prism;
    pr->x0 = x0; pr->x1 = x1;
    pr->west = (x0 == 0); pr->east = (x1 == LAB_W);
    pl_box(pr, LAB_FRONT, LAB_WALL, LAB_TOP, LAB_BACK);
    pr->edges[0].kind = RG_EM_PROJ;
    pr->edges[0].proj = rg_proj_rows(24, 32);                       /* cornice */
    memset(&s, 0, sizeof(s));
    s.fixed[0] = 0; s.fixed[1] = 24;                                /* the visible roof, rows 0-23 */
    s.hasRepeat = true; s.repeat[0] = 8; s.repeat[1] = 16;          /* the grid, one cell high, beyond the art */
    s.hasTail = true; s.tail[0] = 0; s.tail[1] = 8;
    if (offset != 0.0) {
        s.hasRepeatOffset = true;
        s.repeatOffset = offset;
    }
    pr->edges[1].kind = RG_EM_STRIP;
    pr->edges[1].strip = rg_strip_fin(s);
    pr->edges[2].kind = RG_EM_TILE;
    pr->edges[2].tile = rg_tile_top(8, 24, 16, 32, LAB_TOP);
    pr->skip = 1u << 3;
    pr->hasCaps = true;
    pr->caps[pr->nCaps++] = rg_band(LAB_WALL, LAB_TOP + 1, rg_tile_top(8, 24, 16, 32, LAB_TOP), LAB_FRONT);
}

static bool k_pallet_lab(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    static const double ux0 = 80, ux1 = 104, t0 = 0, t1 = 9, t2 = 21;   /* the vent: top rows t0-t1, front rows t1-t2 */
    RgPart *bd, *un;
    RgPrism *pr;
    RgBand b;
    RgTile brick, pilaster, side;
    double zu;

    (void)spec; (void)a0; (void)a1;
    bd = rg_parts_add(out, RG_P_PRISM, "body");
    if (bd == NULL)
        return false;
    brick = rg_tile_top(48, 32, 56, 48, LAB_WALL);      /* plain yellow brick above the door */
    pilaster = rg_tile_top(0, 32, 8, 48, LAB_WALL);     /* the outlined corner column */
    pr = &bd->u.prism;
    pr->x0 = 0; pr->x1 = LAB_W;
    pr->west = pr->east = true;
    pl_box(pr, LAB_FRONT, -1, LAB_WALL, LAB_BACK);
    pr->edges[0].kind = RG_EM_PROJ;
    pr->edges[0].proj = rg_proj_rows(32, 64);                       /* facade */
    pr->edges[2].kind = RG_EM_TILE;
    pr->edges[2].tile = brick;
    pr->skip = (1u << 1) | (1u << 3);
    b = rg_band(-1, LAB_WALL, brick, LAB_FRONT);
    b.hasFront = true; b.front = pilaster;
    b.hasBack = true;  b.back = pilaster;
    rg_band_z1(&b, LAB_BACK);
    pr->hasCaps = true;
    pr->caps[pr->nCaps++] = b;

    pl_roof_slab(out, "roof_w", 0, ux0, 0.0);
    pl_roof_slab(out, "roof_u", ux0, ux1, 8 - ux0);
    pl_roof_slab(out, "roof_e", ux1, LAB_W, 0.0);

    zu = t2 + LAB_TOP;
    side = rg_tile_top(ux0 + 8, t1, ux0 + 16, t2, LAB_TOP + (t2 - t1));
    un = rg_parts_add(out, RG_P_PRISM, "vent");
    if (un == NULL)
        return false;
    pr = &un->u.prism;
    pr->x0 = ux0; pr->x1 = ux1;
    pr->west = pr->east = true;
    pr->nPoly = 4;
    pl_pt(pr->poly, 0, zu, LAB_TOP - 1);
    pl_pt(pr->poly, 1, zu, LAB_TOP + (t2 - t1));
    pl_pt(pr->poly, 2, zu - (t1 - t0), LAB_TOP + (t2 - t1));
    pl_pt(pr->poly, 3, zu - (t1 - t0), LAB_TOP - 1);
    pr->edges[0].kind = RG_EM_PROJ;
    pr->edges[0].proj = rg_proj_rows(t1, t2);
    pr->edges[1].kind = RG_EM_PROJ;
    pr->edges[1].proj = rg_proj_rows(t0, t1);
    pr->edges[2].kind = RG_EM_TILE;
    pr->edges[2].tile = side;
    pr->skip = 1u << 3;
    pr->hasCaps = true;
    pr->caps[pr->nCaps++] = rg_band(LAB_TOP - 1, LAB_TOP + (t2 - t1) + 1, side, zu);
    return !out->failed;
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
