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
};
const unsigned rg_spec_count = sizeof(rg_specs) / sizeof(rg_specs[0]);
