/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/voxel_building_specs.py
 * (littleroot_house, HOUSE_EXACT, the first two SPECS rows), MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
#include "rg_bspecs.h"
#include "rg_hspecs.h"
#include "rg_brooms.h"

#include <math.h>
#include <stdlib.h>
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


/* ---- shared helpers for the S2.4 builders ---- */

static RgStrip with_tail(RgStrip s, double a, double b)
{
    s.hasTail = true;
    s.tail[0] = a;
    s.tail[1] = b;
    return s;
}

static void prism_init(RgPrism *p, double x0, double x1)
{
    p->x0 = x0;
    p->x1 = x1;
    p->west = p->east = true;                 /* Prism(west=True, east=True) defaults, vb:272 */
}

static void edge_proj(RgPrism *p, unsigned i, double lo, double hi)
{
    p->edges[i].kind = RG_EM_PROJ;
    p->edges[i].proj = rg_proj_rows(lo, hi);
}

static void edge_tile(RgPrism *p, unsigned i, RgTile t)
{
    p->edges[i].kind = RG_EM_TILE;
    p->edges[i].tile = t;
}

static void edge_strip(RgPrism *p, unsigned i, RgStrip s)
{
    p->edges[i].kind = RG_EM_STRIP;
    p->edges[i].strip = rg_strip_fin(s);      /* Strip.__init__ resolves `start` */
}

/* [(front, ylo), (front, yhi), (back, yhi), (back, ylo)]: the (z, y) section every house prism uses */
static void box_poly(RgPrism *p, double front, double ylo, double yhi, double back)
{
    p->nPoly = 4;
    set_pt(p->poly, 0, front, ylo);
    set_pt(p->poly, 1, front, yhi);
    set_pt(p->poly, 2, back, yhi);
    set_pt(p->poly, 3, back, ylo);
}

static void add_cap(RgPrism *p, RgBand b)
{
    p->hasCaps = true;
    p->caps[p->nCaps++] = b;
}

/* Band(y0, y1, tile, z0, front=front, back=back, z1=z1) */
static RgBand band_fb(double y0, double y1, RgTile tile, double z0, RgTile front, RgTile back, double z1)
{
    RgBand b = rg_band(y0, y1, tile, z0);

    b.hasFront = true; b.front = front;
    b.hasBack = true;  b.back = back;
    rg_band_z1(&b, z1);
    return b;
}

#define SKIP(i) (1u << (i))

/* sp:101-150 littleroot_lab. Appends [ground_floor, roof, vent_box, vent_cowl]. */
bool rg_littleroot_lab(const RgSpec *spec, int unused0, int unused1, RgPartList *out)
{
    double front_z = 80, back_z = 16, overhang = 2, wall_top = 29;
    RgPart *base = rg_parts_add(out, RG_P_PRISM, "ground_floor");
    RgPart *rf = rg_parts_add(out, RG_P_HIPROOF, "roof");
    RgPart *box = rg_parts_add(out, RG_P_PRISM, "vent_box");
    RgPart *cowl = rg_parts_add(out, RG_P_CYLINDER, "vent_cowl");
    RgHip *roof;
    RgPrism *pr;
    double zbox, ybox, ytop;
    RgTile grey, post, plaster;

    (void)spec; (void)unused0; (void)unused1;
    if (base == NULL || rf == NULL || box == NULL || cowl == NULL)
        return false;
    roof = &rf->u.hip;
    roof->x0 = 0; roof->x1 = 114;
    roof->zf = front_z + overhang; roof->zb = back_z - overhang; roof->y0 = 29;
    roof->fascia = rg_strip_fin(with_wrap(strip2(50, 53), 8, 104));
    roof->slope = rg_strip_fin(with_wrap(with_repeat(strip2(46, 50), 34, 46), 8, 104));
    roof->teeth[0] = 10; roof->teeth[1] = 15;
    roof->cap[0] = 1; roof->cap[1] = 10;
    roof->pitch = 18.0; roof->run = 6;
    roof->ridgeU[0] = 0; roof->ridgeU[1] = 112;
    roof->endTile = rg_tile(48, 10, 57, 15);
    roof->ridge = true;
    roof->hasRidgeWrap = true; roof->ridgeWrap[0] = 48; roof->ridgeWrap[1] = 104;
    rg_hip_init(roof);

    /* Ventilator: box front bottom on the slope where art row 32 lies. */
    rg_hip_slope_point(roof, 50 - 32, &zbox, &ybox);
    ytop = zbox - 25;
    grey = rg_tile_top(20, 25, 40, 32, ytop);
    pr = &box->u.prism;
    prism_init(pr, 16, 48);
    pr->nPoly = 4;
    set_pt(pr->poly, 0, zbox, ybox - 6);
    set_pt(pr->poly, 1, zbox, ytop);
    set_pt(pr->poly, 2, zbox - 10, ytop);
    set_pt(pr->poly, 3, zbox - 10, ybox - 6);
    edge_proj(pr, 0, 25, 32);
    edge_tile(pr, 2, grey);
    pr->skip = SKIP(3);
    add_cap(pr, rg_band(-64, 200, grey, zbox));

    cowl->u.cyl.cx = 32; cowl->u.cyl.cz = zbox - 10; cowl->u.cyl.rx = 12; cowl->u.cyl.rz = 9;
    cowl->u.cyl.y0 = ytop - 4; cowl->u.cyl.y1 = ytop + 4;
    cowl->u.cyl.backTile = rg_tile(24, 16, 40, 25);
    cowl->u.cyl.sides = 16;

    post = rg_tile_top(2, 53, 8, 80, 27);
    plaster = rg_tile_top(8, 53, 16, 80, 27);
    pr = &base->u.prism;
    prism_init(pr, 2, 112);
    box_poly(pr, front_z, 0, wall_top, back_z);
    edge_proj(pr, 0, 53, 80);
    edge_tile(pr, 2, plaster);
    pr->skip = SKIP(1) | SKIP(3);
    add_cap(pr, band_fb(0, 27, plaster, front_z, post, post, back_z));
    add_cap(pr, rg_band(27, wall_top + 1, rg_tile_top(8, 53, 16, 54, wall_top), front_z));
    return !out->failed;
}

/* sp:164-196 center_or_mart. The Center adds a crown vault. */
static bool center_or_mart(RgPartList *out, double ribA, double ribB, bool crown)
{
    static const double kArch[24][2] = {
        {16, 0}, {16.5, 1}, {17.5, 2}, {18.5, 3}, {19.5, 4}, {20.5, 4}, {21.5, 5}, {22.5, 6}, {23.5, 6}, {24.5, 7},
        {26.5, 7}, {27.5, 8}, {36.5, 8}, {37.5, 7}, {39.5, 7}, {40.5, 6}, {41.5, 6}, {42.5, 5}, {43.5, 4}, {44.5, 4},
        {45.5, 3}, {46.5, 2}, {47.5, 1}, {48, 0}};
    double front = 64, back = 16;
    RgPart *body = rg_parts_add(out, RG_P_FRUSTUM, "body");
    RgFrustum *f;
    unsigned i;

    if (body == NULL)
        return false;
    f = &body->u.frustum;
    f->nPlan = 8;
    set_pt(f->plan, 0, 8, front);
    set_pt(f->plan, 1, 56, front);
    set_pt(f->plan, 2, 64, front - 8);
    set_pt(f->plan, 3, 64, back + 8);
    set_pt(f->plan, 4, 56, back);
    set_pt(f->plan, 5, 8, back);
    set_pt(f->plan, 6, 0, back + 8);
    set_pt(f->plan, 7, 0, front - 8);
    f->wallTop = 26;
    f->wallSide = rg_strip_fin(with_wrap(strip2(38, 64), 8, 16));
    f->bandRise = 7;
    f->bandSide = rg_strip_fin(with_wrap(strip2(24, 38), 8, 16));
    f->top = rg_strip_fin(with_wrap(with_repeat(strip2(9, 24), ribA, ribB), 8, 56));
    if (crown) {
        RgPart *v = rg_parts_add(out, RG_P_VAULT, "crown");

        if (v == NULL)
            return false;
        for (i = 0; i < 24; i++)
            set_pt(v->u.vault.profile, i, kArch[i][0], kArch[i][1]);
        v->u.vault.nProfile = 24;
        v->u.vault.zf = 57; v->u.vault.zb = 41; v->u.vault.y0 = 33;
    }
    return !out->failed;
}

bool rg_pokemon_center(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return center_or_mart(out, 9, 16, true);
}

bool rg_poke_mart(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return center_or_mart(out, 12, 16, false);
}

/* sp:204-237 oldale_house. */
bool rg_oldale_house(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    double front_z = 64, back_z = 16, overhang = 2, wall_top = 30;
    RgPart *base = rg_parts_add(out, RG_P_PRISM, "ground_floor");
    RgPart *rf = rg_parts_add(out, RG_P_HIPROOF, "roof");
    RgHip *roof;
    RgPrism *pr;
    RgTile post, plaster;

    (void)spec; (void)a0; (void)a1;
    if (base == NULL || rf == NULL)
        return false;
    roof = &rf->u.hip;
    roof->x0 = 0; roof->x1 = 64;
    roof->zf = front_z + overhang; roof->zb = back_z - overhang; roof->y0 = 30;
    roof->fascia = rg_strip_fin(with_wrap(strip2(34, 36), 8, 56));
    roof->slope = rg_strip_fin(with_wrap(with_repeat(strip2(30, 34), 14, 30), 8, 56));
    roof->teeth[0] = 10; roof->teeth[1] = 14;
    roof->cap[0] = 0; roof->cap[1] = 10;
    roof->pitch = 22.0; roof->run = 6;
    roof->ridgeU[0] = 0; roof->ridgeU[1] = 64;
    roof->endTile = rg_tile(8, 10, 18, 14);
    roof->ridge = true;
    rg_hip_init(roof);

    post = rg_tile_top(2, 36, 9, 64, 28);
    plaster = rg_tile_top(9, 36, 15, 64, 28);
    pr = &base->u.prism;
    prism_init(pr, 2, 64);
    box_poly(pr, front_z, 0, wall_top, back_z);
    edge_proj(pr, 0, 36, 64);
    edge_tile(pr, 2, plaster);
    pr->skip = SKIP(1) | SKIP(3);
    add_cap(pr, band_fb(0, 28, plaster, front_z, post, post, back_z));
    add_cap(pr, rg_band(28, wall_top + 1, rg_tile_top(9, 36, 15, 37, wall_top), front_z));
    return !out->failed;
}

/* sp:243-286 briney_house. Appends [ground_floor, lattice, roof]. */
bool rg_briney_house(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    double front_z = 64, back_z = 16, overhang = 2, floor_top = 16, wall_top = 27;
    RgPart *gf = rg_parts_add(out, RG_P_PRISM, "ground_floor");
    RgPart *up = rg_parts_add(out, RG_P_PRISM, "lattice");
    RgPart *rf = rg_parts_add(out, RG_P_HIPROOF, "roof");
    RgHip *roof;
    RgPrism *pr;
    RgTile post, reed, lattice, lattice_end;

    (void)spec; (void)a0; (void)a1;
    if (gf == NULL || up == NULL || rf == NULL)
        return false;
    roof = &rf->u.hip;
    roof->x0 = 0; roof->x1 = 80;
    roof->zf = front_z + overhang; roof->zb = back_z - overhang; roof->y0 = 27;
    roof->fascia = rg_strip_fin(with_wrap(strip2(36, 39), 8, 72));
    roof->slope = rg_strip_fin(with_wrap(with_repeat(strip2(16, 36), 16, 24), 8, 72));
    roof->teeth[0] = 9; roof->teeth[1] = 16;
    roof->cap[0] = 0; roof->cap[1] = 9;
    roof->pitch = 22.0; roof->run = 6;
    roof->ridgeU[0] = 0; roof->ridgeU[1] = 80;
    roof->endTile = rg_tile(8, 9, 18, 16);
    roof->ridge = true;
    rg_hip_init(roof);

    post = rg_tile_top(1, 48, 8, 64, floor_top);
    reed = rg_tile_top(8, 48, 18, 64, floor_top);
    lattice = rg_tile_top(8, 39, 16, 48, 25);
    lattice_end = rg_tile_top(0, 39, 3, 48, 25);
    pr = &gf->u.prism;
    prism_init(pr, 1, 80);
    box_poly(pr, front_z, 0, floor_top, back_z);
    edge_proj(pr, 0, 48, 64);
    edge_tile(pr, 2, reed);
    pr->skip = SKIP(1) | SKIP(3);
    add_cap(pr, band_fb(0, floor_top, reed, front_z, post, post, back_z));
    pr = &up->u.prism;
    prism_init(pr, 0, 80);
    box_poly(pr, front_z, floor_top, wall_top, back_z);
    edge_proj(pr, 0, 39, 48);
    edge_tile(pr, 2, lattice);
    pr->skip = SKIP(1) | SKIP(3);
    add_cap(pr, band_fb(floor_top, wall_top + 1, lattice, front_z, lattice_end, lattice_end, back_z));
    return !out->failed;
}

/* sp:292-322 flower_shop. Appends [ground_floor, roof]. */
bool rg_flower_shop(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    double front = 64, back = 16;
    double wall = 64 - 38, top = wall + (38 - 28);
    RgPart *bd = rg_parts_add(out, RG_P_PRISM, "ground_floor");
    RgPart *rf = rg_parts_add(out, RG_P_PRISM, "roof");
    RgPrism *pr;
    RgTile post, siding, rim;

    (void)spec; (void)a0; (void)a1;
    if (bd == NULL || rf == NULL)
        return false;
    post = rg_tile_top(1, 38, 6, 64, wall);
    siding = rg_tile_top(72, 40, 80, 48, wall - 2);
    rim = rg_tile_top(9, 28, 17, 38, top);
    pr = &bd->u.prism;
    prism_init(pr, 1, 95);
    box_poly(pr, front, 0, wall, back);
    edge_proj(pr, 0, 38, 64);
    edge_tile(pr, 2, siding);
    pr->skip = SKIP(1) | SKIP(3);
    add_cap(pr, band_fb(0, wall, siding, front, post, post, back));
    pr = &rf->u.prism;
    prism_init(pr, 0, 96);
    box_poly(pr, front, wall, top, back);
    edge_proj(pr, 0, 28, 38);
    edge_strip(pr, 1, with_tail(with_repeat(strip2(4, 28), 20, 28), 0, 4));
    edge_tile(pr, 2, rim);
    pr->skip = SKIP(3);
    add_cap(pr, rg_band(wall, top + 1, rim, front));
    return !out->failed;
}

/* sp:327-361 kit_house(width): arg0 = width in pixels. Appends [ground_floor, roof]. */
bool rg_kit_house(const RgSpec *spec, int width, int a1, RgPartList *out)
{
    double front_z = 64, back_z = 16, overhang = 2, wall_top = 28, w = width;
    RgPart *base = rg_parts_add(out, RG_P_PRISM, "ground_floor");
    RgPart *rf = rg_parts_add(out, RG_P_HIPROOF, "roof");
    RgHip *roof;
    RgPrism *pr;
    RgTile post, boards;

    (void)spec; (void)a1;
    if (base == NULL || rf == NULL)
        return false;
    roof = &rf->u.hip;
    roof->x0 = 0; roof->x1 = w;
    roof->zf = front_z + overhang; roof->zb = back_z - overhang; roof->y0 = 28;
    roof->fascia = rg_strip_fin(with_wrap(strip2(35, 38), 8, w - 8));
    roof->slope = rg_strip_fin(with_wrap(with_repeat(strip2(32, 35), 16, 32), 8, w - 8));
    roof->teeth[0] = 10; roof->teeth[1] = 16;
    roof->cap[0] = 0; roof->cap[1] = 10;
    roof->pitch = 22.0; roof->run = 6;
    roof->ridgeU[0] = 0; roof->ridgeU[1] = w;
    roof->endTile = rg_tile(8, 10, 18, 16);
    roof->ridge = true;
    rg_hip_init(roof);

    post = rg_tile_top(2, 38, 8, 64, 26);
    boards = rg_tile_top(9, 38, 15, 64, 26);
    pr = &base->u.prism;
    prism_init(pr, 2, w - 2);
    box_poly(pr, front_z, 0, wall_top, back_z);
    edge_proj(pr, 0, 38, 64);
    edge_tile(pr, 2, boards);
    pr->skip = SKIP(1) | SKIP(3);
    add_cap(pr, band_fb(0, 26, boards, front_z, post, post, back_z));
    add_cap(pr, rg_band(26, wall_top + 1, rg_tile_top(9, 38, 15, 39, wall_top), front_z));
    return !out->failed;
}

/* sp:367-402 gym. Appends [body, roof, porch_roof, porch]. */
bool rg_gym(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    double front = 72, porch = 80, back = 2, wall_top = 27, roof_top = 31;
    RgPart *bd = rg_parts_add(out, RG_P_PRISM, "body");
    RgPart *rf = rg_parts_add(out, RG_P_PRISM, "roof");
    RgPart *pf = rg_parts_add(out, RG_P_PRISM, "porch_roof");
    RgPart *pw = rg_parts_add(out, RG_P_WALLS, "porch");
    RgPrism *pr;
    RgTile panel, corner, lip;

    (void)spec; (void)a0; (void)a1;
    if (bd == NULL || rf == NULL || pf == NULL || pw == NULL)
        return false;
    panel = rg_tile_top(72, 45, 88, 72, wall_top);
    corner = rg_tile_top(88, 45, 94, 72, wall_top);
    lip = rg_tile_top(8, 41, 16, 45, roof_top);
    pr = &bd->u.prism;
    prism_init(pr, 2, 94);
    box_poly(pr, front, -1, wall_top, back);
    edge_proj(pr, 0, 45, 72);
    edge_tile(pr, 2, panel);
    pr->skip = SKIP(1) | SKIP(3);
    add_cap(pr, band_fb(-1, wall_top, panel, front, corner, corner, back));
    pr = &rf->u.prism;
    prism_init(pr, 0, 96);
    box_poly(pr, front, wall_top, roof_top, back);
    edge_proj(pr, 0, 41, 45);
    edge_strip(pr, 1, with_repeat(strip2(5, 41), 5, 7));
    edge_tile(pr, 2, lip);
    pr->skip = SKIP(3);
    add_cap(pr, rg_band(wall_top, roof_top + 1, lip, front));
    pr = &pf->u.prism;
    prism_init(pr, 40, 72);
    box_poly(pr, porch, wall_top, roof_top, front);
    edge_proj(pr, 0, 49, 54);
    pr->skip = SKIP(2) | SKIP(3);
    add_cap(pr, rg_band(wall_top, roof_top + 1, lip, porch));
    pw->u.walls.nPlan = 4;
    set_pt(pw->u.walls.plan, 0, 40, front);
    set_pt(pw->u.walls.plan, 1, 48, porch);
    set_pt(pw->u.walls.plan, 2, 64, porch);
    set_pt(pw->u.walls.plan, 3, 72, front);
    pw->u.walls.y0 = -1; pw->u.walls.y1 = wall_top;
    return !out->failed;
}

/* sp:482-504 flat_part: one flat-roofed volume over [x0, x1). Appends [name, name_roof]. */
bool rg_flat_part(RgPartList *out, const char *name, double x0, double x1, double front, double back,
                  const double roof[3][2], const double cornice[2], double facade_top, const double brick[4],
                  const double rim[4], bool west, bool east)
{
    char nm[RG_NAME_LEN];
    double wall = front - facade_top, top = wall + (cornice[1] - cornice[0]);
    RgPart *bd, *sl;
    RgPrism *pr;
    RgTile brickT, rimT;

    if (out == NULL || name == NULL || strlen(name) + 6 >= RG_NAME_LEN)
        return false;
    bd = rg_parts_add(out, RG_P_PRISM, name);
    memcpy(nm, name, strlen(name) + 1);
    strcat(nm, "_roof");
    sl = rg_parts_add(out, RG_P_PRISM, nm);
    if (bd == NULL || sl == NULL)
        return false;
    brickT = rg_tile_top(brick[0], brick[1], brick[2], brick[3], wall);
    rimT = rg_tile_top(rim[0], rim[1], rim[2], rim[3], top);
    pr = &bd->u.prism;
    prism_init(pr, x0, x1);
    pr->west = west; pr->east = east;
    box_poly(pr, front, 0, wall, back);
    edge_proj(pr, 0, facade_top, front);
    edge_tile(pr, 2, brickT);
    pr->skip = SKIP(1) | SKIP(3);
    add_cap(pr, rg_band(0, wall, brickT, front));
    pr = &sl->u.prism;
    prism_init(pr, x0, x1);
    pr->west = west; pr->east = east;
    box_poly(pr, front, wall, top, back);
    edge_proj(pr, 0, cornice[0], cornice[1]);
    edge_strip(pr, 1, with_tail(with_repeat(strip2(roof[0][0], roof[0][1]), roof[1][0], roof[1][1]), roof[2][0],
                                roof[2][1]));
    edge_tile(pr, 2, rimT);
    pr->skip = SKIP(3);
    add_cap(pr, rg_band(wall, top + 1, rimT, front));
    return !out->failed;
}

/* sp:408-461 flat_block: a flat-roofed block (any size), optionally with a roof unit. */
static bool flat_slab(RgPartList *out, const char *name, double x0, double x1, double width, double front,
                      double back, double wall, double top, const double roof[3][2], double c0, double c1,
                      RgTile rim, double offset)
{
    RgPart *sl = rg_parts_add(out, RG_P_PRISM, name);
    RgPrism *pr;
    RgStrip s;

    if (sl == NULL)
        return false;
    pr = &sl->u.prism;
    prism_init(pr, x0, x1);
    pr->west = (x0 == 0); pr->east = (x1 == width);
    box_poly(pr, front, wall, top, back);
    edge_proj(pr, 0, c0, c1);
    s = with_tail(with_repeat(strip2(roof[0][0], roof[0][1]), roof[1][0], roof[1][1]), roof[2][0], roof[2][1]);
    if (offset != 0.0) {
        s.hasRepeatOffset = true;
        s.repeatOffset = offset;
    }
    edge_strip(pr, 1, s);
    edge_tile(pr, 2, rim);
    pr->skip = SKIP(3);
    add_cap(pr, rg_band(wall, top + 1, rim, front));
    return true;
}

bool rg_flat_block(RgPartList *out, double width, double height, const double roof[3][2], const double cornice[2],
                   double facade_top, const double *unit /* NULL or ux0, ux1, t0, t1, t2 */)
{
    double front = height, back = 16;
    double wall = height - facade_top, c0 = cornice[0], c1 = cornice[1], top = wall + (c1 - c0);
    RgPart *bd;
    RgPrism *pr;
    RgTile brick, pilaster, rim;

    if (out == NULL)
        return false;
    bd = rg_parts_add(out, RG_P_PRISM, "body");
    if (bd == NULL)
        return false;
    brick = rg_tile_top(8, facade_top, 16, facade_top + 16, wall);
    pilaster = rg_tile_top(0, facade_top, 8, facade_top + 16, wall);
    rim = rg_tile_top(8, c0, 16, c1, top);
    pr = &bd->u.prism;
    prism_init(pr, 0, width);
    box_poly(pr, front, -1, wall, back);
    edge_proj(pr, 0, facade_top, height);
    edge_tile(pr, 2, brick);
    pr->skip = SKIP(1) | SKIP(3);
    add_cap(pr, band_fb(-1, wall, brick, front, pilaster, pilaster, back));
    if (unit == NULL)
        return flat_slab(out, "roof", 0, width, width, front, back, wall, top, roof, c0, c1, rim, 0.0) &&
               !out->failed;
    {
        double ux0 = unit[0], ux1 = unit[1], t0 = unit[2], t1 = unit[3], t2 = unit[4];
        double zu = t2 + top;
        RgPart *un;
        RgTile side;

        if (!flat_slab(out, "roof_w", 0, ux0, width, front, back, wall, top, roof, c0, c1, rim, 0.0) ||
            !flat_slab(out, "roof_u", ux0, ux1, width, front, back, wall, top, roof, c0, c1, rim, 8 - ux0) ||
            !flat_slab(out, "roof_e", ux1, width, width, front, back, wall, top, roof, c0, c1, rim, 0.0))
            return false;
        side = rg_tile_top(ux0 + 8, t1, ux0 + 16, t2, top + (t2 - t1));
        un = rg_parts_add(out, RG_P_PRISM, "unit");
        if (un == NULL)
            return false;
        pr = &un->u.prism;
        prism_init(pr, ux0, ux1);
        pr->nPoly = 4;
        set_pt(pr->poly, 0, zu, top - 1);
        set_pt(pr->poly, 1, zu, top + (t2 - t1));
        set_pt(pr->poly, 2, zu - (t1 - t0), top + (t2 - t1));
        set_pt(pr->poly, 3, zu - (t1 - t0), top - 1);
        edge_proj(pr, 0, t1, t2);
        edge_proj(pr, 1, t0, t1);
        edge_tile(pr, 2, side);
        pr->skip = SKIP(3);
        add_cap(pr, rg_band(top - 1, top + (t2 - t1) + 1, side, zu));
    }
    return !out->failed;
}

/* sp:507-528 devon. */
bool rg_devon(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    static const double wingRoof[3][2] = {{8, 32}, {8, 10}, {2, 8}};
    static const double towerRoof[3][2] = {{9, 40}, {9, 17}, {0, 9}};
    static const double wingCorn[2] = {32, 40}, towerCorn[2] = {40, 56};
    static const double wwBrick[4] = {8, 40, 48, 72}, wwRim[4] = {8, 32, 48, 40};
    static const double weBrick[4] = {112, 40, 152, 72}, weRim[4] = {112, 32, 152, 40};
    static const double tBrick[4] = {48, 56, 56, 88}, tRim[4] = {56, 40, 64, 56};

    (void)spec; (void)a0; (void)a1;
    return rg_flat_part(out, "wing_w", 0, 48, 128, 0, wingRoof, wingCorn, 40, wwBrick, wwRim, true, false) &&
           rg_flat_part(out, "wing_e", 112, 160, 128, 0, wingRoof, wingCorn, 40, weBrick, weRim, false, true) &&
           rg_flat_part(out, "tower", 48, 112, 144, 0, towerRoof, towerCorn, 56, tBrick, tRim, true, true);
}

/* sp:534-577 fountain (+ Relief_top, Jet). Appends [basin, fountain.top, bowl, jet]. */
bool rg_fountain(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    double h = 9, front = 46;
    RgPart *walls = rg_parts_add(out, RG_P_WALLS, "basin");
    RgPart *ft = rg_parts_add(out, RG_P_FOUNTAIN_TOP, "fountain_top");
    RgPart *bowl = rg_parts_add(out, RG_P_CYLINDER, "bowl");
    RgPart *jet = rg_parts_add(out, RG_P_JET, "jet");
    RgFountainTop *f;

    (void)spec; (void)a0; (void)a1;
    if (walls == NULL || ft == NULL || bowl == NULL || jet == NULL)
        return false;
    f = &ft->u.ftop;
    f->nPoly = 8;
    f->h = h;
    set_pt(f->poly, 0, 9, front);
    set_pt(f->poly, 1, 39, front);
    set_pt(f->poly, 2, 47, front - 8);
    set_pt(f->poly, 3, 47, 20);
    set_pt(f->poly, 4, 37, 10);
    set_pt(f->poly, 5, 11, 10);
    set_pt(f->poly, 6, 1, 20);
    set_pt(f->poly, 7, 1, front - 8);
    walls->u.walls.nPlan = 4;
    set_pt(walls->u.walls.plan, 0, 1, front - 8);
    set_pt(walls->u.walls.plan, 1, 9, front);
    set_pt(walls->u.walls.plan, 2, 39, front);
    set_pt(walls->u.walls.plan, 3, 47, front - 8);
    walls->u.walls.y0 = -1; walls->u.walls.y1 = h;
    bowl->u.cyl.cx = 24; bowl->u.cyl.cz = 31; bowl->u.cyl.rx = 8; bowl->u.cyl.rz = 7;
    bowl->u.cyl.y0 = h - 1; bowl->u.cyl.y1 = 14;
    bowl->u.cyl.backTile = rg_tile(16, 22, 32, 29);
    bowl->u.cyl.sides = 16;
    jet->u.jet.x0 = 21; jet->u.jet.x1 = 27; jet->u.jet.z = 31; jet->u.jet.y0 = 14; jet->u.jet.y1 = 26;
    return !out->failed;
}

/* sp:99 */
static const RgExact kHouseExact[4] = {
    {2, 54, 80, 80, false}, {12, 45, 68, 54, false}, {10, 38, 70, 45, false}, {12, 16, 68, 38, false}};
/* sp:152, 200-202, 240, 289, 325, 364, 406, 531 */
static const RgExact kLabExact[3] = {{2, 53, 112, 80, false}, {8, 32, 104, 53, false}, {48, 16, 104, 32, false}};
static const RgExact kCenterExact[3] = {{8, 30, 56, 64, false}, {0, 38, 64, 64, false}, {12, 9, 52, 30, false}};
static const RgExact kCrownExact[4] = {{8, 30, 56, 64, false}, {0, 38, 64, 64, false}, {12, 9, 52, 30, false},
                                       {16, 0, 48, 24, true}};
static const RgExact kOldaleExact[2] = {{2, 36, 64, 64, false}, {8, 14, 56, 36, false}};
static const RgExact kBrineyExact[3] = {{1, 39, 80, 64, false}, {0, 39, 1, 48, false}, {8, 16, 72, 39, false}};
static const RgExact kFlowerExact[2] = {{0, 4, 96, 38, false}, {1, 38, 95, 64, false}};
static const RgExact kKit4Exact[2] = {{2, 38, 62, 64, false}, {8, 16, 56, 38, false}};
static const RgExact kKit5Exact[2] = {{2, 38, 78, 64, false}, {8, 16, 72, 38, false}};
static const RgExact kGymExact[4] = {
    {2, 45, 40, 72, false}, {72, 45, 94, 72, false}, {3, 5, 93, 45, false}, {40, 41, 72, 80, false}};
static const RgExact kDevonExact[2] = {{0, 8, 160, 128, false}, {48, 128, 112, 144, false}};
static const RgExact kFountainExact[1] = {{0, 0, 48, 48, false}};

/* sp:464-479 */
bool rg_stone_block(const RgSpec *s, int a0, int a1, RgPartList *out)
{
    static const double roof[3][2] = {{7, 39}, {7, 11}, {0, 7}};
    static const double cornice[2] = {39, 48};

    (void)a0; (void)a1;
    return rg_flat_block(out, s->rect[2] * 16.0, s->rect[3] * 16.0, roof, cornice, 48, NULL);
}

bool rg_olive_block(const RgSpec *s, int a0, int a1, RgPartList *out)
{
    static const double roof[3][2] = {{8, 40}, {8, 16}, {0, 8}};
    static const double cornice[2] = {40, 48};
    double width = s->rect[2] * 16.0, unit[5];

    (void)a1;
    unit[0] = width - 24; unit[1] = width - 4; unit[2] = 1; unit[3] = 16; unit[4] = 31;
    return rg_flat_block(out, width, s->rect[3] * 16.0, roof, cornice, 48, a0 ? unit : NULL);
}

void rg_flat_block_exact(RgExact *out, int width, int height, int firstRoofRow)
{
    out->x0 = 0; out->y0 = (int16_t)firstRoofRow; out->x1 = (int16_t)width; out->y1 = (int16_t)height;
    out->behind = false;
}

/* The expanders' configs (sp:1124-1255). */
static const uint16_t kHedgeTiles[15] = {0x23c, 0x23d, 0x23e, 0x244, 0x245, 0x246, 0x24c, 0x24d, 0x24e,
                                         0x254, 0x255, 0x256, 0x264, 0x265, 0x266};
static const uint16_t kRailTiles[28] = {0x2A7, 0x2FC, 0x318, 0x319, 0x31A, 0x315, 0x31D, 0x320, 0x321,
                                        0x32C, 0x32D, 0x2BE, 0x2BF, 0x2CD, 0x352, 0x2E9,
                                        0x2B7, 0x2C6, 0x2C7, 0x2D5, 0x2D7, 0x2DC, 0x2DE, 0x2DF,
                                        0x2E6, 0x2E7, 0x2EC, 0x31B};
#define TS_PETALBURG 0x083DF71Cu
#define TS_RUSTBORO 0x083DF734u
static const RgComponentsCfg kHedge = {TS_PETALBURG, kHedgeTiles, 15, 11, 0, 0, 0, false, -1};
static const RgComponentsCfg kRailing = {TS_RUSTBORO, kRailTiles, 28, 12, 12, 3, 4, true, 0x2A7};
static const RgKitCfg kStoneKit = {0x224, 0x21c, {0x225, 0}, {0x226, 0}, 1, 1, 7};
static const RgKitCfg kOliveKit = {0x220, 0x240, {0x221, 0x222}, {0x223, 0x23F}, 2, 2, 8};
static const RgPropsCfg kSeaRock = {RG_OBJ_SEA_ROCK, 1.0, 2, {{222, 230, 238}}, 1};
static const RgPropsCfg kSandBoulder = {RG_OBJ_SAND_BOULDER, 1.0, 2, {{0, 0, 0}}, 0};
static const RgPropsCfg kSeaStack = {RG_OBJ_SEA_STACK, 1.6, 4, {{131, 131, 139}}, 1};

/* Layout ids and blockdata fingerprints (SPEC-S2 section 3.1): 1 Petalburg, 3 Mauville, 4 Rustboro,
 * 10 Littleroot, 11 Oldale, 20 Route 104. The fingerprints are of the user's BPEE ROM (host test pins them). */
#define L_PETALBURG 1, 0xCA6DFAA0u
#define L_MAUVILLE 3, 0x6FFC5818u
#define L_RUSTBORO 4, 0xA55404CFu
#define L_LITTLEROOT 10, 0xEFE99674u
#define L_OLDALE 11, 0x52C922B6u
#define L_ROUTE104 20, 0x157E3492u
#define RUST_GROUND {0x2BB, 0x2C3, GRASS}, 3

/* The rows of sp:1092-1375 in upstream order (interior rows arrive S2.6). Components / kit / props rows are
 * expanded by rg_bexpand.c; their rect, exact and parts are per expanded model. */
const RgSpec rg_specs[] = {
    {"littleroot_house_w", RG_SPEC_DIRECT, L_LITTLEROOT, {2, 4, 5, 5}, {0, 0}, {GRASS}, 1, kHouseExact, 4,
     rg_littleroot_house, 8, 0, NULL},
    {"littleroot_house_e", RG_SPEC_DIRECT, L_LITTLEROOT, {13, 4, 5, 5}, {0, 0}, {GRASS}, 1, kHouseExact, 4,
     rg_littleroot_house, 64, 0, NULL},
    {"littleroot_lab", RG_SPEC_DIRECT, L_LITTLEROOT, {3, 12, 7, 5}, {0, 0}, {GRASS}, 1, kLabExact, 3,
     rg_littleroot_lab, 0, 0, NULL},
    {"pokemon_center", RG_SPEC_DIRECT, L_PETALBURG, {19, 13, 4, 4}, {1, 4}, {GRASS}, 1, kCrownExact, 4,
     rg_pokemon_center, 0, 0, NULL},
    {"poke_mart", RG_SPEC_DIRECT, L_MAUVILLE, {22, 11, 4, 4}, {1, 4}, {GRASS}, 1, kCenterExact, 3,
     rg_poke_mart, 0, 0, NULL},
    {"oldale_house", RG_SPEC_DIRECT, L_OLDALE, {4, 4, 4, 4}, {0, 0}, {GRASS}, 1, kOldaleExact, 2,
     rg_oldale_house, 0, 0, NULL},
    {"briney_house", RG_SPEC_DIRECT, L_ROUTE104, {15, 47, 5, 4}, {0, 0}, {GRASS}, 1, kBrineyExact, 3,
     rg_briney_house, 0, 0, NULL},
    {"flower_shop", RG_SPEC_DIRECT, L_ROUTE104, {3, 15, 6, 4}, {0, 0}, {GRASS, 0x206, 0x207}, 3, kFlowerExact, 2,
     rg_flower_shop, 0, 0, NULL},
    {"kit_house_4", RG_SPEC_DIRECT, L_PETALBURG, {9, 16, 4, 4}, {0, 0}, {GRASS}, 1, kKit4Exact, 2,
     rg_kit_house, 64, 0, NULL},
    {"kit_house_5", RG_SPEC_DIRECT, L_PETALBURG, {5, 2, 5, 4}, {0, 0}, {GRASS}, 1, kKit5Exact, 2,
     rg_kit_house, 80, 0, NULL},
    {"gym", RG_SPEC_DIRECT, L_PETALBURG, {12, 4, 6, 5}, {0, 4}, {GRASS}, 1, kGymExact, 4, rg_gym, 0, 0, NULL},
    /* sp:1118 hedges: every connected run of the Petalburg-tileset layouts */
    {"hedge", RG_SPEC_COMPONENTS, 0, 0, {0, 0, 0, 0}, {0, 0}, {GRASS}, 1, NULL, 0, NULL, 0, 0, &kHedge},
    /* sp:1142, 1153 Rustboro's kit blocks, found by corner / top / end / foot */
    {"rustboro_stone", RG_SPEC_KIT, L_RUSTBORO, {0, 0, 0, 0}, {0, 0}, RUST_GROUND, NULL, 0, rg_stone_block, 0, 0,
     &kStoneKit},
    {"rustboro_olive", RG_SPEC_KIT, L_RUSTBORO, {0, 0, 0, 0}, {0, 0}, RUST_GROUND, NULL, 0, rg_olive_block, 0, 0,
     &kOliveKit},
    {"gym_rustboro", RG_SPEC_DIRECT, L_RUSTBORO, {24, 15, 6, 5}, {0, 0}, RUST_GROUND, kGymExact, 4, rg_gym, 0, 0,
     NULL},
    {"railing", RG_SPEC_COMPONENTS, 0, 0, {0, 0, 0, 0}, {0, 0}, {0x2BB, 0x2C3, GRASS}, 3, NULL, 0, NULL, 0, 0, &kRailing},
    {"sea_rock", RG_SPEC_PROPS, 0, 0, {0, 0, 0, 0}, {0, 0}, {0x170}, 1, NULL, 0, NULL, 0, 0, &kSeaRock},
    {"sand_boulder", RG_SPEC_PROPS, 0, 0, {0, 0, 0, 0}, {0, 0}, {0x124}, 1, NULL, 0, NULL, 0, 0, &kSandBoulder},
    {"sea_stack", RG_SPEC_PROPS, 0, 0, {0, 0, 0, 0}, {0, 0}, {0x170}, 1, NULL, 0, NULL, 0, 0, &kSeaStack},
    {"devon_corporation", RG_SPEC_DIRECT, L_RUSTBORO, {7, 7, 10, 9}, {0, 0}, RUST_GROUND, kDevonExact, 2,
     rg_devon, 0, 0, NULL},
    {"rustboro_fountain", RG_SPEC_DIRECT, L_RUSTBORO, {27, 38, 3, 3}, {0, 0}, RUST_GROUND, kFountainExact, 1,
     rg_fountain, 0, 0, NULL},
    /* Phase 36: the Hoenn recipes (rg_hspecs.h, our own work), ahead of the interior rows */
    RG_HSPECS_DEWFORD_ROWS
    RG_HSPECS_MAUVILLE_ROWS
    RG_HSPECS_VERDANTURF_ROWS
    RG_HSPECS_FALLARBOR_ROWS
    RG_HSPECS_SLATEPORT_ROWS
    RG_HSPECS_FORTREE_ROWS
    RG_HSPECS_LAVARIDGE_ROWS
    RG_HSPECS_PACIFIDLOG_ROWS
    RG_HSPECS_LILYCOVE_ROWS
    RG_HSPECS_MOSSDEEP_ROWS
    /* sp:1296-1375 the 13 interior rows: a room cut into pieces (rg_brooms.c); layout ids and pins as SPEC-S2 3.1 */
    {"pc1f", RG_SPEC_INTERIOR, 61, 0xBBF5FE0Du, {0, 0, 0, 0}, {0, 0}, {0x202}, 1, NULL, 0, NULL, 0, 0, &rg_room_pc1f},
    {"pc2f", RG_SPEC_INTERIOR, 62, 0x2C4488F9u, {0, 0, 0, 0}, {0, 0}, {0x202}, 1, NULL, 0, NULL, 0, 0, &rg_room_pc2f},
    {"mart", RG_SPEC_INTERIOR, 63, 0x13A673F9u, {0, 0, 0, 0}, {0, 0}, {0x201}, 1, NULL, 0, NULL, 0, 0, &rg_room_mart},
    {"brendan_1f", RG_SPEC_INTERIOR, 54, 0x74435B94u, {0, 0, 0, 0}, {0, 0}, {0x201}, 1, NULL, 0, NULL, 0, 0, &rg_room_brendan_1f},
    {"brendan_2f", RG_SPEC_INTERIOR, 55, 0xEB9E8528u, {0, 0, 0, 0}, {0, 0}, {0x201}, 1, NULL, 0, NULL, 0, 0, &rg_room_brendan_2f},
    {"may_1f", RG_SPEC_INTERIOR, 56, 0x933D5B5Eu, {0, 0, 0, 0}, {0, 0}, {0x201}, 1, NULL, 0, NULL, 0, 0, &rg_room_may_1f},
    {"may_2f", RG_SPEC_INTERIOR, 57, 0xEC70A737u, {0, 0, 0, 0}, {0, 0}, {0x201}, 1, NULL, 0, NULL, 0, 0, &rg_room_may_2f},
    {"lab", RG_SPEC_INTERIOR, 58, 0xB02DC393u, {0, 0, 0, 0}, {0, 0}, {0x202}, 1, NULL, 0, NULL, 0, 0, &rg_room_lab},
    {"lab_table", RG_SPEC_INTERIOR, 432, 0xE9140827u, {0, 0, 0, 0}, {0, 0}, {0x202}, 1, NULL, 0, NULL, 0, 0, &rg_room_lab_table},
    {"lavaridge_pc1f", RG_SPEC_INTERIOR, 71, 0x7A6744BCu, {0, 0, 0, 0}, {0, 0}, {0x202}, 1, NULL, 0, NULL, 0, 0, &rg_room_lavaridge_pc1f},
    {"house1", RG_SPEC_INTERIOR, 59, 0x0E2EFCEEu, {0, 0, 0, 0}, {0, 0}, {0x223}, 1, NULL, 0, NULL, 0, 0, &rg_room_house1},
    {"house2", RG_SPEC_INTERIOR, 60, 0x1DF7BAB3u, {0, 0, 0, 0}, {0, 0}, {0x223}, 1, NULL, 0, NULL, 0, 0, &rg_room_house2},
    {"rustboro_gym", RG_SPEC_INTERIOR, 94, 0xC053E45Eu, {0, 0, 0, 0}, {0, 0}, {0x201}, 1, NULL, 0, NULL, 0, 0, &rg_room_rustboro_gym},
};
const unsigned rg_spec_count = sizeof(rg_specs) / sizeof(rg_specs[0]);

/* ---- Phase 34 side walls --------------------------------------------------------------------------------------------- */

/* ---- look L5: dress the end faces from the building's own front art ---------------------------------------------- */
/* The ROM draws a building's front only, so an end face is derived from the front's rows. The wall below the eave is a
 * one-texel-wide COLUMN of the facade, stretched along the depth: the column that matches the facade's most common
 * colour in the most rows, so it carries the wall band and the base stripe (and the eave shadow) at the heights the front
 * has them. Where the depth allows, a window of the same facade (a run of columns that differ from the wall in the middle
 * but not at the foot, so a door or a pilaster is not mistaken for one) is laid at its true 1:1 size in the middle of the
 * depth. A mirrored gable's end triangle above the eave takes the facade's wall colour instead of the roof's.
 * Every piece is one projected polygon (RgBand.proj): no tiling, so it costs no more than the flat patch it replaces. */
#define SD_MAXW 256

static uint32_t sd_px(const RgImage *a, int x, int y)
{
    const uint8_t *p;

    if (x < 0 || y < 0 || x >= a->w || y >= a->h)
        return 0u;
    p = a->px + ((size_t)y * (size_t)a->w + (size_t)x) * 4u;
    if (p[3] < 128)
        return 0u;                                  /* transparent: never a wall colour */
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | 0xFFu;
}

typedef struct SdDress {
    double uCol, uWin0, uWin1, gableU, gableV;
    bool window;
} SdDress;

/* Reads the wall rows of the prism (front at z = zfront, y in [ylo, yhi)) off the art; false = nothing trustworthy. */
static bool sd_analyse(const RgImage *art, double x0, double x1, double zfront, double ylo, double yhi, SdDress *d)
{
    int c0 = (int)floor(x0 + 1e-6), c1 = (int)ceil(x1 - 1e-6), nc, nr, r, c, k, best = -1, bestScore = -1;
    int r0 = (int)floor(zfront - yhi + 1e-6), r1 = (int)ceil(zfront - ylo - 1e-6);
    uint32_t mode[SD_MAXW];
    int diff[SD_MAXW];
    bool win[SD_MAXW];
    double mid;

    if (art == NULL || art->px == NULL || c0 < 0 || c1 > art->w || r0 < 0 || r1 > art->h || c1 <= c0 || r1 - r0 < 4 ||
        r1 - r0 > SD_MAXW || c1 - c0 > SD_MAXW)
        return false;
    nc = c1 - c0;
    nr = r1 - r0;
    for (r = 0; r < nr; r++) {                      /* the facade's most common colour in each row */
        uint32_t cand[SD_MAXW];
        int n[SD_MAXW], nd = 0, top = 0;

        for (c = 0; c < nc; c++) {
            uint32_t px = sd_px(art, c0 + c, r0 + r);

            if (px == 0u)
                continue;
            for (k = 0; k < nd && cand[k] != px; k++)
                ;
            if (k == nd) {
                cand[nd] = px;
                n[nd++] = 0;
            }
            n[k]++;
        }
        mode[r] = 0u;
        for (k = 0; k < nd; k++)
            if (n[k] > top) {
                top = n[k];
                mode[r] = cand[k];
            }
    }
    mid = (nc - 1) / 2.0;
    for (c = 0; c < nc; c++) {
        int score = 0;

        for (r = 0; r < nr; r++)
            if (mode[r] != 0u && sd_px(art, c0 + c, r0 + r) == mode[r])
                score++;
        diff[c] = nr - score;
        if (score > bestScore || (score == bestScore && fabs(c - mid) < fabs(best - mid))) {
            bestScore = score;
            best = c;
        }
    }
    if (best < 0 || bestScore * 10 < nr * 6)
        return false;
    d->uCol = c0 + best + 0.5;
    {   /* the wall colour: the column's most common texel (its row is the gable's) */
        int bestN = 0;

        d->gableU = d->uCol;
        d->gableV = r0 + nr / 2 + 0.5;
        for (r = 0; r < nr; r++) {
            uint32_t px = sd_px(art, c0 + best, r0 + r);
            int n = 0;

            for (k = 0; k < nr; k++)
                n += sd_px(art, c0 + best, r0 + k) == px;
            if (px != 0u && n > bestN) {
                bestN = n;
                d->gableV = r0 + r + 0.5;
            }
        }
    }
    /* windows: a column that differs from the wall in the middle rows but not in the lowest three (a door or a pilaster
     * stands on the ground; a window has the wall's own foot below it) */
    for (c = 0; c < nc; c++) {
        int foot = 0;

        for (r = nr - 3; r < nr; r++)
            foot += sd_px(art, c0 + c, r0 + r) == mode[r];
        win[c] = diff[c] >= 3 && foot == 3 && c > 0 && c < nc - 1;
    }
    d->window = false;
    for (c = 0; c < nc;) {
        int a = c, b;

        if (!win[c]) {
            c++;
            continue;
        }
        for (b = c; b < nc && (win[b] || (b + 1 < nc && win[b + 1])); b++)
            ;                                       /* a one-column gap stays inside the window */
        if (b - a >= 10 && b - a <= 40 && (!d->window || b - a > (int)(d->uWin1 - d->uWin0))) {
            d->window = true;
            d->uWin0 = c0 + a;
            d->uWin1 = c0 + b;
        }
        c = b;
    }
    return true;
}

static RgBand sd_band(double y0, double y1, double z0, double sLo, double sHi, double pu0, double pdu, double pv0, double pdv)
{
    RgBand b = rg_band(y0, y1, rg_tile(0, 0, 1, 1), z0);

    b.proj = true;
    b.sLo = sLo; b.sHi = sHi;
    b.pu0 = pu0; b.pdu = pdu; b.pv0 = pv0; b.pdv = pdv;
    return b;
}

/* Fills pr->caps from the art; false = leave the flat patches. */
static bool sd_dress(RgPrism *pr, const RgSideCfg *cfg, const RgImage *art, double ytop, double zfront)
{
    SdDress d;
    double ybot = 1e30, zback = 1e30, yw1 = cfg->eave < ytop ? cfg->eave : ytop, depth;
    unsigned k;

    for (k = 0; k < pr->nPoly; k++) {
        if (pr->poly[k][1] < ybot) ybot = pr->poly[k][1];
        if (pr->poly[k][0] < zback && pr->poly[k][1] < yw1 - RG_EPS) zback = pr->poly[k][0];
    }
    if (ybot < 0.0)
        ybot = 0.0;
    if (yw1 - ybot < 4.0 || !sd_analyse(art, pr->x0, pr->x1, zfront, ybot, yw1, &d))
        return false;
    depth = zfront - zback;
    pr->nCaps = 0;
    if (d.window && depth >= (d.uWin1 - d.uWin0) + 12.0) {
        double w = d.uWin1 - d.uWin0, sw0 = floor((depth - w) / 2.0);

        pr->caps[pr->nCaps++] = sd_band(-1, yw1, zfront, 0.0, sw0, d.uCol, 0, zfront, 1);
        pr->caps[pr->nCaps++] = sd_band(-1, yw1, zfront, sw0, sw0 + w, d.uWin0, 1, zfront, 1);
        pr->caps[pr->nCaps++] = sd_band(-1, yw1, zfront, sw0 + w, 1e9, d.uCol, 0, zfront, 1);
    } else {
        pr->caps[pr->nCaps++] = sd_band(-1, yw1, zfront, 0.0, 1e9, d.uCol, 0, zfront, 1);
    }
    if (ytop > cfg->eave) {
        if (pr->mirrored)                           /* a gable end: wall colour */
            pr->caps[pr->nCaps++] = sd_band(cfg->eave, ytop + 1, zfront, 0.0, 1e9, d.gableU, 0, d.gableV, 0);
        else
            pr->caps[pr->nCaps++] = rg_band(cfg->eave, ytop + 1,
                                            rg_tile_top(cfg->roof[0], cfg->roof[1], cfg->roof[2], cfg->roof[3], ytop), zfront);
    }
    return true;
}

bool rg_close_sides(const RgSpec *s, RgPartList *parts, const RgImage *art)
{
    const RgSideCfg *table;
    unsigned i;

    if (s->kind != RG_SPEC_DIRECT || s->ext == NULL)
        return true;
    table = (const RgSideCfg *)s->ext;
    for (i = 0; i < parts->n; i++) {
        RgPart *p = rg_parts_at(parts, i);
        RgPrism *pr;
        const RgSideCfg *cfg = table;
        double ytop = -1e30, zfront = -1e30;
        bool west, east;
        unsigned k;

        if (p == NULL || p->kind != RG_P_PRISM || p->u.prism.hasCaps)
            continue;
        while (cfg->part != NULL && strncmp(p->name, cfg->part, strlen(cfg->part)) != 0 && !cfg->last)
            cfg++;
        if (cfg->part != NULL && strncmp(p->name, cfg->part, strlen(cfg->part)) != 0)
            continue;
        west = rg_prism_exposed(parts, i, false, NULL, NULL) > 0;
        east = rg_prism_exposed(parts, i, true, NULL, NULL) > 0;
        if (!west && !east)
            continue;
        pr = &p->u.prism;
        for (k = 0; k < pr->nPoly; k++) {
            if (pr->poly[k][1] > ytop) ytop = pr->poly[k][1];
            if (pr->poly[k][0] > zfront) zfront = pr->poly[k][0];
        }
        pr->west = west;
        pr->east = east;
        pr->hasCaps = true;
        pr->nCaps = 0;
        if (art != NULL && sd_dress(pr, cfg, art, ytop, zfront))
            continue;
        pr->nCaps = 0;
        pr->caps[pr->nCaps++] = rg_band(-1, cfg->eave < ytop ? cfg->eave : ytop + 1,
                                        rg_tile_top(cfg->wall[0], cfg->wall[1], cfg->wall[2], cfg->wall[3], ytop), zfront);
        if (ytop > cfg->eave)
            pr->caps[pr->nCaps++] = rg_band(cfg->eave, ytop + 1,
                                            rg_tile_top(cfg->roof[0], cfg->roof[1], cfg->roof[2], cfg->roof[3], ytop), zfront);
    }
    return !parts->failed;
}

/* ---- look L6: rg_close_backs -------------------------------------------------------------------------------------- */
/* The GBA art draws a building's front only, so many builders stop a roof at its ridge or end a block in a skipped back
 * wall, which leaves the model open from behind. An edge of a solid prism is OPEN when it is not drawn (skipped, or a
 * back-facing NONE edge), does not face the ground or the camera, and some point half a pixel behind it lies in no
 * other solid part. A prism with an open edge is closed in one of three ways:
 *  - pitched: when the front chain (the edges from the front-bottom corner, counter-clockwise, up to the first open one)
 *    ends in a slope that rises toward the back and nothing drawn lies behind it, the roof part of that chain (above the
 *    eave) is first laid down to RG_ROOF_PITCH (look L6b: same art rows, more depth, less height), then mirrored about
 *    the ridge, clipped at the footprint's back row, and a back wall drops to the ground. The rear slope keeps the
 *    front's pitch and its rows (a mirrored projection); a parapet over the ridge gets a 45-degree back in the roof
 *    colour;
 *  - otherwise every open edge is drawn flat: the side-closure wall patch below the eave, the roof patch above it (a spec
 *    without a RgSideCfg takes a texel of the front wall's and the roof's own rows);
 *  - when rg_spec_parts' front guard finds a flat face showing through the art (a transparent texel of the top in front
 *    of it), the prism instead runs a 45-degree back down from the top of its front chain: a face the front camera sees
 *    edge-on. A face that shows only at its ends is trimmed there instead (at most CB_TRIM px, a quarter of its width). */
void (*rg_close_backs_log)(const char *spec, const char *part, const char *what);

/* look L6b: the pitch (degrees) a mirrored roof is laid down to. The device camera only ever looks north from the south
 * (yaw 0); the view ray to a 40 px ridge is 21-33 degrees above the horizon at the default 40-degree pitch and 15-26 at
 * the lowest preset (34), so a rear slope steeper than that is hidden behind its own ridge. The builders' 45-degree
 * slopes were exactly that: the house read as a box that stops at the ridge. At 15 the rear slope shows at every preset
 * (edge-on only at 34 degrees with the house at the top of a tiny map). */
#define RG_ROOF_PITCH 15.0
#define RG_DEG2RAD_CB 0.017453292519943295
#define CB_COVER_STEP 4.0
#define CB_NO_MIRROR 0x80000000u
#define CB_CHAMFER 0x40000000u
#define CB_NO_CHAMFER 0x20000000u
#define CB_EDGES 0x0FFFFFFFu

static bool cb_point_solid(const RgPartList *parts, unsigned self, double x, double z, double y)
{
    unsigned j, k;

    for (j = 0; j < parts->n; j++) {
        const RgPart *q = rg_parts_at(parts, j);
        double sec[2][RG_SEC_PTS][2];
        unsigned n[2], ns;

        if (j == self || q == NULL)
            continue;
        ns = rg_part_section(q, x, 0.0, sec, n);
        for (k = 0; k < ns; k++)
            if (rg_poly_inside((const double (*)[2])sec[k], n[k], z, y, 0.0))
                return true;
    }
    return false;
}

/* 1 when every sample half a pixel behind edge i (over the prism's width) falls in another solid */
static bool cb_covered(const RgPartList *parts, unsigned self, const RgPrism *pr, unsigned i, double nz, double ny)
{
    const double *a = pr->poly[i], *b = pr->poly[(i + 1) % pr->nPoly];
    double len = hypot(b[0] - a[0], b[1] - a[1]), x, t;
    unsigned hit = 0, all = 0;

    for (x = pr->x0 + 1.0; x <= pr->x1 - 1.0 + RG_EPS; x += CB_COVER_STEP)
        for (t = 0.5 / len; t < 1.0; t += (len > 8.0 ? 4.0 : 1.0) / len) {
            double z = a[0] + (b[0] - a[0]) * t + nz * 0.5, y = a[1] + (b[1] - a[1]) * t + ny * 0.5;

            all++;
            if (cb_point_solid(parts, self, x, z, y))
                hit++;
        }
    return all > 0 && hit == all;
}

static void cb_normal(const RgPrism *pr, unsigned i, double *nz, double *ny)
{
    const double *a = pr->poly[i], *b = pr->poly[(i + 1) % pr->nPoly];
    double dz = b[0] - a[0], dy = b[1] - a[1], len = hypot(dz, dy);

    *nz = len > RG_EPS ? dy / len : 0.0;
    *ny = len > RG_EPS ? -dz / len : 0.0;
}

static bool cb_edge_open(const RgPartList *parts, unsigned self, const RgPrism *pr, unsigned i)
{
    double nz, ny;
    bool drawn;

    cb_normal(pr, i, &nz, &ny);
    if (ny < -0.5 || nz > 0.3)          /* the ground, or a face toward the camera (the art may sit on a sheet in front) */
        return false;
    drawn = !(pr->skip & (1u << i)) && (pr->edges[i].kind != RG_EM_NONE || nz + ny > RG_EPS);
    return !drawn && !cb_covered(parts, self, pr, i, nz, ny);
}

/* a texel of edge i's own projected rows, near the prism's west end (false when the edge is not projected) */
static bool cb_texel(const RgPrism *pr, unsigned i, double out[2])
{
    const RgEdgeMat *e = &pr->edges[i];
    const double *a = pr->poly[i], *b = pr->poly[(i + 1) % pr->nPoly];
    double v;

    if (pr->skip & (1u << i))
        return false;
    if (e->kind == RG_EM_FLAT) {
        out[0] = e->flat[0];
        out[1] = e->flat[1];
        return true;
    }
    if (e->kind != RG_EM_PROJ && e->kind != RG_EM_NONE)
        return false;
    v = (a[0] - a[1] + b[0] - b[1]) / 2;
    if (e->kind == RG_EM_PROJ && e->proj.hasLo && v < e->proj.lo + 0.5)
        v = e->proj.lo + 0.5;
    if (e->kind == RG_EM_PROJ && e->proj.hasHi && v > e->proj.hi - 0.5)
        v = e->proj.hi - 0.5;
    out[0] = pr->x0 + 2.5;
    out[1] = v;
    return true;
}

static void cb_flat(RgEdgeMat *e, const double uv[2])
{
    memset(e, 0, sizeof(*e));
    e->kind = RG_EM_FLAT;
    e->flat[0] = uv[0];
    e->flat[1] = uv[1];
}

static RgEdgeMat cb_mirror(const RgEdgeMat *src, double axis)
{
    RgEdgeMat e = *src;

    if (e.kind == RG_EM_NONE) {
        memset(&e, 0, sizeof(e));
        e.kind = RG_EM_PROJ;
        e.proj = rg_proj();
    }
    if (e.kind == RG_EM_PROJ) {
        e.proj.mirror = true;
        e.proj.axis = axis;
    } else if (e.kind == RG_EM_STRIP) {
        e.strip.fromEnd = !e.strip.fromEnd;
    }
    return e;
}

typedef struct CbPoly {
    double p[RG_PRISM_PTS][2];
    RgEdgeMat e[RG_PRISM_PTS];
    uint32_t skip;
    unsigned n;
    bool over;
} CbPoly;

static void cb_add(CbPoly *q, double z, double y, const RgEdgeMat *e, bool skip)
{
    if (q->n >= RG_PRISM_PTS) {
        q->over = true;
        return;
    }
    q->p[q->n][0] = z;
    q->p[q->n][1] = y;
    q->e[q->n] = *e;
    if (skip)
        q->skip |= 1u << q->n;
    q->n++;
}

/* builds the mirrored outline; false (prism untouched) when the shape does not fit */
static bool cb_mirror_prism(RgPrism *pr, unsigned v0, unsigned nChain, double eave, const double wallUv[2],
                            const double roofUv[2], bool *steep)
{
    unsigned n = pr->nPoly, k, S = nChain, e;
    double c[RG_PRISM_PTS + 1][2], zmin = 0.0, axis, ybot, rise = 0.0;
    RgEdgeMat ce[RG_PRISM_PTS], wall, roof;
    uint32_t cskip = 0;
    CbPoly q;

    memset(&q, 0, sizeof(q));
    cb_flat(&wall, wallUv);
    cb_flat(&roof, roofUv);
    for (k = 0; k <= nChain; k++) {
        unsigned i = (v0 + k) % n;

        c[k][0] = pr->poly[i][0];
        c[k][1] = pr->poly[i][1];
        if (k < nChain) {
            ce[k] = pr->edges[i];
            if (pr->skip & (1u << i))
                cskip |= 1u << k;
        }
    }
    for (k = 0; k < n; k++)
        if (pr->poly[k][0] < zmin)
            zmin = pr->poly[k][0];
    /* a vertical parapet run over the slope top S */
    while (S > 1 && fabs(c[S][0] - c[S - 1][0]) < RG_EPS && c[S][1] > c[S - 1][1])
        S--;
    if (S == 0 || !(c[S][0] < c[S - 1][0] - RG_EPS && c[S][1] > c[S - 1][1] + RG_EPS))
        return false;
    rise = c[nChain][1] - c[S][1];
    axis = c[S][0] - rise / 2;
    ybot = c[0][1];
    for (e = 0; e < S && c[e][1] < eave - RG_EPS; e++)
        ;
    if (e >= S)
        e = S - 1;
    /* look L6b: the roof is laid down to RG_ROOF_PITCH before it is mirrored. Every chain edge above the eave that rises
     * back more steeply keeps its dz + dy (its art rows: v = z - y at both ends, so the ortho front is the same picture)
     * and trades height for depth; the points behind it move with it. */
    for (k = e; k < S; k++) {
        double dz = c[k + 1][0] - c[k][0], dy = c[k + 1][1] - c[k][1], rows, nd, nh, tp = tan(RG_ROOF_PITCH * RG_DEG2RAD_CB);
        unsigned j;

        if (dz > -RG_EPS || dy <= RG_EPS || dy / -dz <= tp + 1e-9)
            continue;
        rows = -dz + dy;
        nd = rows / (1 + tp);
        nh = rows - nd;
        for (j = k + 1; j <= nChain; j++) {
            c[j][0] += -nd - dz;
            c[j][1] += nh - dy;
        }
    }
    rise = c[nChain][1] - c[S][1];
    axis = c[S][0] - rise / 2;
    for (k = 0; k < nChain; k++)
        cb_add(&q, c[k][0], c[k][1], &ce[k], (cskip >> k) & 1u);
    {
        /* mirrored points c[S]' (parapet only) .. c[e]'; the edge into each is the mirror of the chain edge it copies */
        double m[RG_PRISM_PTS + 1][2];
        RgEdgeMat me[RG_PRISM_PTS + 1];
        unsigned nm = 0;
        double prev[2];

        prev[0] = c[nChain][0];
        prev[1] = c[nChain][1];
        if (rise > RG_EPS) {
            m[nm][0] = 2 * axis - c[S][0]; m[nm][1] = c[S][1];
            me[nm] = roof;               /* R -> S' */
            nm++;
        }
        for (k = S; k-- > e;) {
            m[nm][0] = 2 * axis - c[k][0]; m[nm][1] = c[k][1];
            me[nm] = cb_mirror(&ce[k], axis);
            nm++;
        }
        cb_add(&q, prev[0], prev[1], &me[0], false);   /* R starts the edge into m[0] */
        for (k = 0; k < nm; k++) {
            double z = m[k][0], y = m[k][1];
            bool last = k + 1 == nm;
            double nzv, nyv, dz = z - prev[0], dy = y - prev[1], len = hypot(dz, dy);

            nzv = len > RG_EPS ? dy / len : 0;
            nyv = len > RG_EPS ? -dz / len : 0;
            if (nzv + nyv > RG_EPS && me[k].kind != RG_EM_FLAT)
                *steep = false;          /* a rear face shallower than 45 degrees shows over the ridge (L6b: always, by design) */
            if (z < zmin - RG_EPS) {
                double t = (zmin - prev[0]) / (z - prev[0]);

                z = zmin;
                y = prev[1] + (y - prev[1]) * t;
                last = true;
            }
            if (last) {
                /* the back wall: roof colour above the eave, wall colour below */
                if (y > eave + RG_EPS && eave > ybot + RG_EPS) {
                    cb_add(&q, z, y, &roof, false);
                    cb_add(&q, z, eave, &wall, false);
                } else if (y > ybot + RG_EPS) {
                    cb_add(&q, z, y, y > eave + RG_EPS ? &roof : &wall, false);
                }
                cb_add(&q, z, ybot, &wall, true);   /* the bottom back to c[0] */
                break;
            }
            cb_add(&q, z, y, &me[k + 1], false);
            prev[0] = z;
            prev[1] = y;
        }
    }
    if (q.over || q.n < 3 || !rg_polygon_ccw((const double (*)[2])q.p, q.n))
        return false;
    for (k = 0; k < q.n; k++) {
        pr->poly[k][0] = q.p[k][0];
        pr->poly[k][1] = q.p[k][1];
        pr->edges[k] = q.e[k];
    }
    pr->nPoly = q.n;
    pr->skip = q.skip;
    pr->closed = 0;
    for (k = nChain; k < q.n; k++)
        if (!(q.skip & (1u << k)))
            pr->closed |= 1u << k;
    pr->mirrored = true;
    return true;
}

static bool cb_cross(const double *a, const double *b, const double *c, const double *d)
{
    double d1 = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
    double d2 = (b[0] - a[0]) * (d[1] - a[1]) - (b[1] - a[1]) * (d[0] - a[0]);
    double d3 = (d[0] - c[0]) * (a[1] - c[1]) - (d[1] - c[1]) * (a[0] - c[0]);
    double d4 = (d[0] - c[0]) * (b[1] - c[1]) - (d[1] - c[1]) * (b[0] - c[0]);

    return ((d1 > RG_EPS && d2 < -RG_EPS) || (d1 < -RG_EPS && d2 > RG_EPS)) &&
           ((d3 > RG_EPS && d4 < -RG_EPS) || (d3 < -RG_EPS && d4 > RG_EPS));
}

/* The fallback when a flat face would show through the art: from the top of the front chain R the outline runs down
 * and back at 45 degrees (a face the front camera sees edge-on, so it never adds a pixel) to the ground or the
 * footprint's back row, where a back wall drops the rest of the way. */
static bool cb_chamfer_prism(RgPrism *pr, unsigned v0, unsigned nChain, double eave, const double wallUv[2],
                             const double roofUv[2])
{
    unsigned n = pr->nPoly, k, j;
    double zmin = 0.0, ybot, R[2], drop;
    RgEdgeMat wall, roof;
    CbPoly q;

    memset(&q, 0, sizeof(q));
    cb_flat(&wall, wallUv);
    cb_flat(&roof, roofUv);
    for (k = 0; k < n; k++)
        if (pr->poly[k][0] < zmin)
            zmin = pr->poly[k][0];
    ybot = pr->poly[v0][1];
    R[0] = pr->poly[(v0 + nChain) % n][0];
    R[1] = pr->poly[(v0 + nChain) % n][1];
    if (nChain == 0 || R[1] <= ybot + RG_EPS)
        return false;
    for (k = 0; k < nChain; k++) {
        unsigned i = (v0 + k) % n;

        cb_add(&q, pr->poly[i][0], pr->poly[i][1], &pr->edges[i], (pr->skip >> i) & 1u);
    }
    drop = R[1] - ybot;
    if (R[0] - drop >= zmin - RG_EPS) {
        cb_add(&q, R[0], R[1], R[1] > eave + RG_EPS ? &roof : &wall, false);
        cb_add(&q, R[0] - drop, ybot, &wall, true);
    } else {
        double yc = R[1] - (R[0] - zmin);

        cb_add(&q, R[0], R[1], R[1] > eave + RG_EPS ? &roof : &wall, false);
        cb_add(&q, zmin, yc, yc > eave + RG_EPS ? &roof : &wall, false);
        cb_add(&q, zmin, ybot, &wall, true);
    }
    if (q.over || q.n < 3 || !rg_polygon_ccw((const double (*)[2])q.p, q.n))
        return false;
    for (k = 0; k < q.n; k++)
        for (j = k + 2; j < q.n; j++)
            if (!(k == 0 && j == q.n - 1) && cb_cross(q.p[k], q.p[(k + 1) % q.n], q.p[j], q.p[(j + 1) % q.n]))
                return false;
    for (k = 0; k < q.n; k++) {
        pr->poly[k][0] = q.p[k][0];
        pr->poly[k][1] = q.p[k][1];
        pr->edges[k] = q.e[k];
    }
    pr->nPoly = q.n;
    pr->skip = q.skip;
    pr->closed = 0;
    for (k = nChain; k < q.n; k++)
        if (!(q.skip & (1u << k)))
            pr->closed |= 1u << k;
    pr->chamfered = true;
    return true;
}

static void cb_trim(RgPrism *pr, const RgBackDeny *d)
{
    unsigned k;

    for (k = 0; d != NULL && k < pr->nPoly; k++)
        if (pr->closed & (1u << k)) {
            pr->edges[k].trim[0] = d->trim[k][0];
            pr->edges[k].trim[1] = d->trim[k][1];
        }
}

bool rg_close_backs(const RgSpec *s, RgPartList *parts, const RgBackDeny *deny)
{
    const RgSideCfg *table = s->kind == RG_SPEC_DIRECT ? (const RgSideCfg *)s->ext : NULL;
    unsigned i;

    for (i = 0; i < parts->n; i++) {
        RgPart *p = rg_parts_at(parts, i);
        RgPrism *pr;
        const RgSideCfg *cfg = NULL;
        bool open[RG_PRISM_PTS], raw[RG_PRISM_PTS], any = false, rest = true, steep = true;
        uint32_t no = deny != NULL ? deny[i].mask : 0u;
        unsigned n, k, v0 = 0, nChain;
        double wallUv[2], roofUv[2], eave;

        if (p == NULL || p->kind != RG_P_PRISM || rg_prism_is_sheet(&p->u.prism))
            continue;
        pr = &p->u.prism;
        n = pr->nPoly;
        for (k = 0; k < n; k++) {
            raw[k] = cb_edge_open(parts, i, pr, k);
            open[k] = raw[k] && !(no & (1u << k));
            any = any || raw[k];
        }
        if (!any)
            continue;
        if (table != NULL) {
            const RgSideCfg *t = table;

            while (t->part != NULL && strncmp(p->name, t->part, strlen(t->part)) != 0 && !t->last)
                t++;
            if (t->part == NULL || strncmp(p->name, t->part, strlen(t->part)) == 0)
                cfg = t;
        }
        for (k = 1; k < n; k++)
            if (pr->poly[k][1] < pr->poly[v0][1] - RG_EPS ||
                (fabs(pr->poly[k][1] - pr->poly[v0][1]) <= RG_EPS && pr->poly[k][0] > pr->poly[v0][0]))
                v0 = k;
        for (nChain = 0; nChain < n && !raw[(v0 + nChain) % n]; nChain++)
            ;
        for (k = nChain; k < n; k++) {
            unsigned j = (v0 + k) % n;
            double nz, ny;

            cb_normal(pr, j, &nz, &ny);
            if (!raw[j] && ny >= -0.5 && !(pr->skip & (1u << j)) &&
                (pr->edges[j].kind != RG_EM_NONE || nz + ny > RG_EPS))     /* a drawn face behind the chain stays */
                rest = false;
        }
        /* the eave: the side closure's, else where the front chain first leaves the vertical */
        eave = pr->poly[v0][1];
        for (k = 0; k < nChain; k++) {
            const double *a = pr->poly[(v0 + k) % n], *b = pr->poly[(v0 + k + 1) % n];

            if (fabs(b[0] - a[0]) > RG_EPS) {
                eave = a[1];
                break;
            }
        }
        if (cfg != NULL) {
            eave = cfg->eave;
            wallUv[0] = (cfg->wall[0] + cfg->wall[2]) / 2; wallUv[1] = (cfg->wall[1] + cfg->wall[3]) / 2;
            roofUv[0] = (cfg->roof[0] + cfg->roof[2]) / 2; roofUv[1] = (cfg->roof[1] + cfg->roof[3]) / 2;
        } else {
            bool w = false, r = false;

            for (k = 0; k < nChain && !w; k++)
                w = cb_texel(pr, (v0 + k) % n, wallUv);
            for (k = nChain; k-- > 0 && !r;)
                r = cb_texel(pr, (v0 + k) % n, roofUv);
            if (!w && !r) {
                for (k = 0; k < n && !w; k++)
                    w = cb_texel(pr, k, wallUv);
                if (!w) {
                    wallUv[0] = pr->x0 + 0.5;
                    wallUv[1] = 0.5;
                }
            }
            if (!w)
                memcpy(wallUv, roofUv, sizeof(wallUv));
            if (!r)
                memcpy(roofUv, wallUv, sizeof(roofUv));
        }
        if (!(no & CB_NO_MIRROR) && nChain > 0 && nChain < n && rest &&
            cb_mirror_prism(pr, v0, nChain, eave, wallUv, roofUv, &steep)) {
            cb_trim(pr, deny != NULL ? &deny[i] : NULL);
            continue;
        }
        if ((no & CB_CHAMFER) && !(no & CB_NO_CHAMFER) && nChain < n && rest &&
            cb_chamfer_prism(pr, v0, nChain, eave, wallUv, roofUv)) {
            cb_trim(pr, deny != NULL ? &deny[i] : NULL);
            continue;
        }
        for (k = 0; k < n; k++) {
            const double *a = pr->poly[k], *b = pr->poly[(k + 1) % n];

            if (!open[k])
                continue;
            cb_flat(&pr->edges[k], (a[1] + b[1]) / 2 > eave + RG_EPS ? roofUv : wallUv);
            pr->skip &= ~(1u << k);
            pr->closed |= 1u << k;
        }
        cb_trim(pr, deny != NULL ? &deny[i] : NULL);
    }
    return !parts->failed;
}

/* The front guard: a closure face the front camera can see (through a transparent texel of the art, or a rear slope
 * shallower than 45 degrees) would change the front, so it is denied and the parts rebuilt: a mirrored prism falls back
 * to flat faces, a flat face stays open. The raster is the ortho check's (same projection, same alpha rule). */
static bool cb_name_edge(const char *tag, char *part, unsigned *edge)
{
    const char *e = strrchr(tag, '.');
    size_t len;
    unsigned v = 0;

    if (e == NULL || e[1] != 'e' || e[2] < '0' || e[2] > '9')
        return false;
    len = (size_t)(e - tag);
    if (len >= RG_NAME_LEN)
        return false;
    memcpy(part, tag, len);
    part[len] = 0;
    for (e += 2; *e >= '0' && *e <= '9'; e++)
        v = v * 10u + (unsigned)(*e - '0');
    *edge = v;
    return true;
}

/* A closure face's pixels that the ortho check would judge (inside an exact rect, or anywhere when the spec has none; a
 * transparent art pixel under a `behind` rect is allowed) break the front. When they all sit within CB_TRIM px of the
 * face's ends (a transparent corner texel of the art) the face is trimmed there; otherwise it is denied. */
#define CB_TRIM 8
static bool cb_judged(const RgSpec *s, const RgImage *art, int x, int y)
{
    unsigned r;
    bool clear;

    if (x < 0 || y < 0 || x >= art->w || y >= art->h)
        return false;
    clear = art->px[((size_t)y * (size_t)art->w + (size_t)x) * 4u + 3u] < 128;
    if (s->nExact == 0)
        return true;
    for (r = 0; r < s->nExact; r++) {
        const RgExact *e = &s->exact[r];

        if (x >= e->x0 && x < e->x1 && y >= e->y0 && y < e->y1 && !(clear && e->behind))
            return true;
    }
    return false;
}

static int cb_guard(const RgSpec *s, const RgPartList *parts, const RgImage *art, RgBackDeny *deny)
{
    enum { M = 32 };
    RgMesh mesh;
    RgRaster ras;
    int16_t *lo = NULL, *hi = NULL;
    unsigned i, nNames, k;
    int added = 0, x, y;

    rg_mesh_init(&mesh);
    if (!rg_parts_emit(parts, &mesh) || mesh.failed || !rg_raster_init(&ras, art->w + 2 * M, art->h + M)) {
        rg_mesh_free(&mesh);
        return -1;
    }
    for (i = 0; i < mesh.n; i++) {
        const RgTri *t = &mesh.t[i];
        double vs[3][6];

        if (t->flags & (RG_TAG_DEPTH | RG_TAG_BEHIND))
            continue;
        for (k = 0; k < 3; k++) {
            vs[k][0] = t->p[k].x + M;
            vs[k][1] = t->p[k].z - t->p[k].y + M;
            vs[k][2] = t->p[k].y + t->p[k].z;
            vs[k][3] = 1.0;
            vs[k][4] = t->p[k].u;
            vs[k][5] = t->p[k].v;
        }
        rg_raster_draw(&ras, vs, art, t->shade, (int16_t)t->tag);
    }
    nNames = mesh.nNames;
    lo = (int16_t *)malloc((nNames > 0 ? nNames : 1u) * sizeof(int16_t));
    hi = (int16_t *)malloc((nNames > 0 ? nNames : 1u) * sizeof(int16_t));
    if (lo == NULL || hi == NULL) {
        free(lo);
        free(hi);
        rg_raster_free(&ras);
        rg_mesh_free(&mesh);
        return -1;
    }
    for (k = 0; k < nNames; k++) {
        lo[k] = INT16_MAX;
        hi[k] = INT16_MIN;
    }
    for (y = 0; y < art->h; y++)
        for (x = 0; x < art->w; x++) {
            int16_t o = ras.owner[(size_t)(y + M) * (size_t)ras.w + (size_t)(x + M)];

            if (o < 0 || (unsigned)o >= nNames || !cb_judged(s, art, x, y))
                continue;
            if (x < lo[o]) lo[o] = (int16_t)x;
            if (x > hi[o]) hi[o] = (int16_t)x;
        }
    for (k = 0; k < nNames; k++) {
        char part[RG_NAME_LEN];
        unsigned edge, j;

        if (lo[k] > hi[k] || !cb_name_edge(mesh.names[k], part, &edge) || edge >= RG_PRISM_PTS)
            continue;
        for (j = 0; j < parts->n; j++) {
            const RgPart *p = rg_parts_at(parts, j);
            const RgPrism *pr;
            RgBackDeny *d = &deny[j];
            double t0, t1;

            if (p == NULL || p->kind != RG_P_PRISM || strcmp(p->name, part) != 0)
                continue;
            pr = &p->u.prism;
            if (!(pr->closed & (1u << edge)) || hi[k] < pr->x0 - 0.5 || lo[k] > pr->x1 - 0.5)
                continue;
            /* the columns to leave out: from each end of the face to its farthest judged pixel there; -1 when one sits
             * in the middle */
            t0 = t1 = 0;
            for (y = 0; y < art->h && t0 >= 0; y++)
                for (x = lo[k]; x <= hi[k]; x++) {
                    if (ras.owner[(size_t)(y + M) * (size_t)ras.w + (size_t)(x + M)] != (int16_t)k ||
                        x < pr->x0 - 0.5 || x > pr->x1 - 0.5 || !cb_judged(s, art, x, y))
                        continue;
                    if (x < pr->x0 + CB_TRIM) {
                        if (x + 1 - pr->x0 > t0) t0 = x + 1 - pr->x0;
                    } else if (x >= pr->x1 - CB_TRIM) {
                        if (pr->x1 - x > t1) t1 = pr->x1 - x;
                    } else {
                        t0 = -1;
                        break;
                    }
                }
            if ((pr->edges[edge].kind == RG_EM_FLAT || pr->edges[edge].kind == RG_EM_PROJ) && t0 >= 0 && t1 >= 0 &&
                t0 <= (pr->x1 - pr->x0) / 4 && t1 <= (pr->x1 - pr->x0) / 4 &&
                (t0 > d->trim[edge][0] || t1 > d->trim[edge][1])) {
                if (t0 > d->trim[edge][0]) d->trim[edge][0] = (uint8_t)t0;
                if (t1 > d->trim[edge][1]) d->trim[edge][1] = (uint8_t)t1;
            } else if (pr->mirrored) {
                if (d->mask & CB_NO_MIRROR)
                    continue;
                d->mask |= CB_NO_MIRROR;
                memset(d->trim, 0, sizeof(d->trim));
            } else if (pr->chamfered) {
                if (d->mask & CB_NO_CHAMFER)
                    continue;
                d->mask |= CB_NO_CHAMFER;
                memset(d->trim, 0, sizeof(d->trim));
            } else {
                if (d->mask & (1u << edge))
                    continue;
                d->mask |= 1u << edge;
                if (!(d->mask & CB_NO_CHAMFER) && !(d->mask & CB_CHAMFER)) {
                    d->mask |= CB_CHAMFER;
                    memset(d->trim, 0, sizeof(d->trim));
                }
            }
            added++;
        }
    }
    free(lo);
    free(hi);
    rg_raster_free(&ras);
    rg_mesh_free(&mesh);
    return added;
}

static bool cb_build(const RgSpec *s, RgPartList *parts, const RgBackDeny *deny, const RgImage *art)
{
    return s->parts(s, s->arg0, s->arg1, parts) && rg_close_backs(s, parts, deny) && rg_close_sides(s, parts, art) &&
           !parts->failed;
}

bool rg_spec_parts(const RgSpec *s, const RgImage *art, RgPartList *parts)
{
    RgBackDeny *deny = NULL;
    unsigned round, i;
    bool ok;

    if (s->parts == NULL)
        return false;
    ok = cb_build(s, parts, NULL, art);
    for (round = 0; ok && art != NULL && round < 16u; round++) {
        int added;

        if (deny == NULL && (deny = (RgBackDeny *)calloc(parts->n > 0 ? parts->n : 1u, sizeof(RgBackDeny))) == NULL)
            return false;
        added = cb_guard(s, parts, art, deny);
        if (added < 0) {
            ok = false;
            break;
        }
        if (added == 0)
            break;
        rg_parts_free(parts);
        rg_parts_init(parts);
        ok = cb_build(s, parts, deny, art);
    }
    if (ok && rg_close_backs_log != NULL)
        for (i = 0; i < parts->n; i++) {
            const RgPart *p = rg_parts_at(parts, i);
            const char *what = NULL;
            bool trimmed = false;
            unsigned k;

            if (p == NULL || p->kind != RG_P_PRISM)
                continue;
            for (k = 0; deny != NULL && k < RG_PRISM_PTS; k++)
                trimmed = trimmed || deny[i].trim[k][0] || deny[i].trim[k][1];
            if (p->u.prism.mirrored)
                what = trimmed ? "rear slope mirrored (ends trimmed for the front)" : "rear slope mirrored";
            else if (p->u.prism.chamfered)
                what = trimmed ? "45-degree back (ends trimmed for the front)" : "45-degree back (a flat face would show)";
            else if (p->u.prism.closed != 0)
                what = deny != NULL && (deny[i].mask & CB_EDGES) != 0 ? "flat faces (some denied: the front would change)"
                       : trimmed ? "flat faces (ends trimmed for the front)" : "flat faces";
            else if (deny != NULL && (deny[i].mask & CB_EDGES) != 0)
                what = "left open (closing it would change the front)";
            if (what != NULL)
                rg_close_backs_log(s->name, p->name, what);
        }
    free(deny);
    return ok;
}
