/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/voxel_building_specs.py
 * (piece(), facet(), the 13 room piece tables and their helpers), MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
#include "rg_brooms.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the piece list ------------------------------------------------------------------------ */

static RgPieceDef sSink;        /* where a failed add writes, so the tables need no per-line checks (l->failed is sticky) */

void rg_pl_free(RgPieceList *l)
{
    free(l->p);
    memset(l, 0, sizeof(*l));
}

RgPieceDef *rg_pl_add(RgPieceList *l, const char *name, int height)
{
    RgPieceDef *p;

    if (l->n == l->cap) {
        unsigned cap = l->cap ? l->cap * 2u : 32u;
        RgPieceDef *np = (RgPieceDef *)realloc(l->p, (size_t)cap * sizeof(RgPieceDef));

        if (np == NULL) {
            l->failed = true;
            memset(&sSink, 0, sizeof(sSink));
            return &sSink;
        }
        l->p = np;
        l->cap = cap;
    }
    p = &l->p[l->n++];
    memset(p, 0, sizeof(*p));
    strncpy(p->name, name, RG_PC_NAME - 1u);
    p->height = height;
    return p;
}

/* ---- builders: one per keyword of piece() ---------------------------------------------------- */

static void srect(RgPieceDef *p, int x0, int y0, int x1, int y1)
{
    RgShapePart *s;

    if (p->shape.n >= RG_SHAPE_MAX)
        return;
    s = &p->shape.p[p->shape.n++];
    s->kind = RG_SH_RECT;
    s->v[0] = x0; s->v[1] = y0; s->v[2] = x1; s->v[3] = y1;
}

static void sellipse(RgPieceDef *p, double cx, double cy, double rx, double ry)
{
    RgShapePart *s;

    if (p->shape.n >= RG_SHAPE_MAX)
        return;
    s = &p->shape.p[p->shape.n++];
    s->kind = RG_SH_ELLIPSE;
    s->v[0] = cx; s->v[1] = cy; s->v[2] = rx; s->v[3] = ry;
}

static RgPieceDef *pr(RgPieceList *L, const char *name, int x0, int y0, int x1, int y1, int height)
{
    RgPieceDef *p = rg_pl_add(L, name, height);

    srect(p, x0, y0, x1, y1);
    return p;
}

static void leave(RgPieceDef *p, const uint32_t *c, unsigned n)
{
    unsigned i;

    for (i = 0; i < n && p->nLeave < RG_PC_LEAVE; i++)
        p->leave[p->nLeave++] = c[i];
}

static void side(RgPieceDef *p, int a, int b, int c, int d)
{
    p->hasSide = true; p->side[0] = a; p->side[1] = b; p->side[2] = c; p->side[3] = d;
}

static void topr(RgPieceDef *p, int a, int b, int c, int d)
{
    p->hasTop = true; p->top[0] = a; p->top[1] = b; p->top[2] = c; p->top[3] = d;
}

static void back(RgPieceDef *p, int v) { p->hasBack = true; p->back = v; }
static void foot(RgPieceDef *p, int v) { p->hasFoot = true; p->foot = v; }
static void against(RgPieceDef *p, int v) { p->hasAgainst = true; p->against = v; }
static void claim(RgPieceDef *p, int a, int b, int c, int d)
{
    p->hasClaim = true; p->claim[0] = a; p->claim[1] = b; p->claim[2] = c; p->claim[3] = d;
}

static void wall(RgPieceDef *p, double ax, double az, double bx, double bz)
{
    RgWallDef *w;

    if (p->nWalls >= RG_PC_WALLS)
        return;
    w = &p->walls[p->nWalls++];
    w->a[0] = ax; w->a[1] = az; w->b[0] = bx; w->b[1] = bz;
    w->hasH = false;
}

static void wallh(RgPieceDef *p, double ax, double az, double bx, double bz, int h)
{
    RgWallDef *w;

    if (p->nWalls >= RG_PC_WALLS)
        return;
    w = &p->walls[p->nWalls++];
    w->a[0] = ax; w->a[1] = az; w->b[0] = bx; w->b[1] = bz;
    w->hasH = true; w->h = h;
}

static void cell(RgPieceDef *p, int cx, int cy)
{
    if (p->nCells >= 2)
        return;
    p->cells[p->nCells][0] = cx;
    p->cells[p->nCells][1] = cy;
    p->nCells++;
}

/* sp:616-625 facet(): a chamfered corner, its shape the polygon (xa, ta), (xb, tb), (xb, fb), (xa, fa). */
static RgPieceDef *facet(RgPieceList *L, const char *name, double xa, double ta, double fa, double xb, double tb,
                         double fb, int height)
{
    RgPieceDef *p = rg_pl_add(L, name, height);
    RgShapePart *s = &p->shape.p[0];

    p->shape.n = 1;
    s->kind = RG_SH_POLY;
    s->n = 4;
    s->v[0] = xa; s->v[1] = ta; s->v[2] = xb; s->v[3] = tb;
    s->v[4] = xb; s->v[5] = fb; s->v[6] = xa; s->v[7] = fa;
    p->hasFacet = true;
    p->facet[0][0] = xa; p->facet[0][1] = ta; p->facet[0][2] = fa;
    p->facet[1][0] = xb; p->facet[1][1] = tb; p->facet[1][2] = fb;
    return p;
}

/* ---- colour constants (sp:590, 612-613, 698, 770-773, 909-912, 976) ---------------------------- */

static const uint32_t kWallColours[3] = {0xf6b473, 0xde7b31, 0xffe6b4};
static const uint32_t kCenterFloor[3] = {0xcdc58b, 0xeedea4, 0xffffc5};
static const uint32_t kCenterCounter[5] = {0xffffc5, 0xe6e6b4, 0xde9c62, 0xffe6b4, 0xde7b31};
static const uint32_t kMartWall[5] = {0xffffff, 0x8bd5de, 0xd5ded5, 0xb4b4a4, 0x83b4b4};
static const uint32_t kHouseWall[3] = {0xd5d5b4, 0xb4b4a4, 0xffffff};
static const uint32_t kHouseFloor[4] = {0xb4a44a, 0x947329, 0xac8b39, 0xcdc55a};
static const uint32_t kBlueRug[3] = {0x6a94c5, 0x8bbdf6, 0x838394};
static const uint32_t kPinkRug[2] = {0xd57bac, 0x838394};
static const uint32_t kGenericWall[4] = {0xd5d5b4, 0xb4b4a4, 0xffffff, 0x629c8b};
static const uint32_t kGenericFloor[4] = {0xded552, 0xbdb431, 0x8b8b8b, 0x9c9410};
static const uint32_t kGenericRug[2] = {0xffcd8b, 0xf6f6a4};
static const uint32_t kGenericTable[2] = {0xfff683, 0xbdac52};
static const uint32_t kLabShadow[2] = {0xb4b4a4, 0x949494};

unsigned rg_room_colours(const uint32_t **out)
{
    static const uint32_t all[] = {
        0xf6b473, 0xde7b31, 0xffe6b4, 0xcdc58b, 0xeedea4, 0xffffc5, 0xe6e6b4, 0xde9c62, 0xffffff, 0x8bd5de,
        0xd5ded5, 0xb4b4a4, 0x83b4b4, 0xd5d5b4, 0xb4a44a, 0x947329, 0xac8b39, 0xcdc55a, 0x6a94c5, 0x8bbdf6,
        0x838394, 0xd57bac, 0x629c8b, 0xded552, 0xbdb431, 0x8b8b8b, 0x9c9410, 0xffcd8b, 0xf6f6a4, 0xfff683,
        0xbdac52, 0x949494};

    *out = all;
    return (unsigned)(sizeof(all) / sizeof(all[0]));
}

/* ---- the Pokemon Centers and the Mart (sp:628-768) ------------------------------------------------- */

/* sp:631-649: the Center's walls, 32 px; `front` is where the floor ends. */
static void center_walls(RgPieceList *L, int front)
{
    RgPieceDef *p;

    p = facet(L, "corner_w", 0, 15, 47, 16, -1, 31, 32);
    wall(p, 0, front, 0, 47);
    side(p, 64, 0, 80, 32);
    cell(p, 0, front / 16 - 1);
    p = facet(L, "corner_e", 208, -1, 31, 224, 15, 47, 32);
    wall(p, 224, 47, 224, front);
    side(p, 64, 0, 80, 32);
    cell(p, 13, front / 16 - 1);
    p = pr(L, "wall_e", 128, 0, 208, 32, 32);
    p->fill = 16;
    side(p, 64, 0, 80, 32);
    p = pr(L, "wall", 16, 0, 128, 32, 32);
    p->fill = 16;
    side(p, 64, 0, 80, 32);
}

static bool pieces_pc1f(RgPieceList *L)
{
    RgPieceDef *p;

    p = rg_pl_add(L, "ball_w", 12); sellipse(p, 72, 45.5, 7.6, 7.6); p->base = 10;
    leave(p, kCenterCounter, 5);
    p = rg_pl_add(L, "ball_e", 12); sellipse(p, 152, 45.5, 7.6, 7.6); p->base = 10;
    leave(p, kCenterCounter, 5);
    p = pr(L, "counter", 64, 24, 80, 64, 10);
    srect(p, 144, 24, 161, 64); srect(p, 64, 44, 161, 64);
    p->fill = 1; back(p, 48);
    p = pr(L, "phone", 126, 22, 146, 45, 16); leave(p, kWallColours, 3); back(p, 32);
    p = pr(L, "healer", 80, 14, 112, 45, 24); leave(p, kWallColours, 3); back(p, 32);
    p = pr(L, "pc", 158, 6, 178, 42, 26); leave(p, kWallColours, 3); back(p, 32);
    p = pr(L, "bookcase", 32, 14, 65, 42, 18); leave(p, kWallColours, 3); back(p, 32);
    p = pr(L, "plant", 14, 10, 34, 40, 26); leave(p, kWallColours, 3); p->card = true;
    p = pr(L, "stool_1", 16, 47, 32, 64, 6); leave(p, kCenterFloor, 3);
    p = pr(L, "stool_2", 32, 47, 48, 64, 6); leave(p, kCenterFloor, 3);
    p = pr(L, "stool_3", 160, 95, 176, 111, 6); leave(p, kCenterFloor, 3);
    p = pr(L, "stool_4", 160, 111, 176, 127, 6); leave(p, kCenterFloor, 3);
    p = pr(L, "stool_5", 176, 127, 192, 144, 6); leave(p, kCenterFloor, 3);
    p = pr(L, "stool_6", 192, 127, 208, 144, 6); leave(p, kCenterFloor, 3);
    p = pr(L, "table", 176, 95, 208, 128, 8); leave(p, kCenterFloor, 3);
    p = pr(L, "escalator", 0, 78, 34, 123, 8); leave(p, kCenterFloor, 3);
    center_walls(L, 144);
    return !L->failed;
}

static bool pieces_pc2f(RgPieceList *L)
{
    RgPieceDef *p;

    p = pr(L, "terminal_1", 48, 40, 64, 73, 24); leave(p, kWallColours, 3); back(p, 48);
    p = pr(L, "terminal_2", 128, 54, 144, 81, 20); back(p, 64);
    p = pr(L, "terminal_3", 192, 54, 208, 81, 20); back(p, 64);
    p = pr(L, "gate_1", 80, 46, 96, 57, 8);
    p = pr(L, "gate_2", 144, 46, 160, 57, 8);
    p = pr(L, "counter", 0, 40, 48, 63, 9);
    srect(p, 64, 40, 80, 63); srect(p, 96, 40, 128, 63); srect(p, 160, 40, 192, 63); srect(p, 208, 40, 218, 63);
    p->fill = 1; back(p, 48);
    p = pr(L, "tube_1", 48, 6, 64, 42, 24); leave(p, kWallColours, 3);
    p = pr(L, "tube_2", 128, 6, 144, 58, 33); leave(p, kWallColours, 3);
    p = pr(L, "tube_3", 192, 6, 208, 58, 33); leave(p, kWallColours, 3);
    p = pr(L, "plant_1", 80, 128, 96, 160, 24); p->card = true;
    p = pr(L, "plant_2", 96, 128, 112, 160, 24); p->card = true;
    p = pr(L, "plant_3", 144, 128, 160, 160, 24); p->card = true;
    p = pr(L, "plant_4", 160, 128, 176, 160, 24); p->card = true;
    p = pr(L, "escalator", 0, 86, 34, 122, 8); leave(p, kCenterFloor, 3);
    center_walls(L, 160);
    return !L->failed;
}

static bool pieces_mart(RgPieceList *L)
{
    RgPieceDef *p;

    p = pr(L, "till", 32, 22, 48, 42, 12); p->base = 15; leave(p, kMartWall, 5);
    p = pr(L, "counter", 32, 24, 48, 80, 15); srect(p, 0, 58, 48, 80);
    p->fill = 1; back(p, 64); topr(p, 34, 56, 46, 70);
    p = pr(L, "shelves_back", 96, 14, 160, 42, 20); leave(p, kMartWall, 5); back(p, 32);
    p = pr(L, "plant", 160, 30, 172, 64, 26); leave(p, kMartWall, 5); p->card = true;
    p = pr(L, "shelf_1", 96, 62, 112, 111, 14); back(p, 64);
    p = pr(L, "shelf_2", 112, 62, 128, 111, 14); back(p, 64);
    p = pr(L, "shelf_3", 160, 54, 172, 111, 14); back(p, 64);
    p = facet(L, "corner_w", 0, 6, 40, 7, -1, 33, 34);
    wall(p, 0, 128, 0, 40); side(p, 32, 0, 48, 32); cell(p, 0, 7);
    p = facet(L, "corner_e", 169, -1, 33, 176, 6, 40, 34);
    wall(p, 176, 40, 176, 128); side(p, 32, 0, 48, 32); cell(p, 10, 7);
    p = pr(L, "wall", 7, 0, 169, 32, 32); p->fill = 16; side(p, 32, 0, 48, 32);
    return !L->failed;
}

/* sp:745-751 Lavaridge's Center: its own plants and walls, the rest the other Centers' (reused). */
static bool pieces_lavaridge(RgPieceList *L)
{
    RgPieceDef *p;
    RgPieceList walls;
    unsigned i;

    p = pr(L, "plant_w", 14, 10, 34, 40, 26); leave(p, kWallColours, 3);
    p = pr(L, "plant_e", 46, 10, 66, 40, 26); leave(p, kWallColours, 3);
    memset(&walls, 0, sizeof(walls));
    center_walls(&walls, 144);
    if (walls.failed) {
        rg_pl_free(&walls);
        return false;
    }
    for (i = 0; i < walls.n; i++) {
        if (strcmp(walls.p[i].name, "wall_e") == 0)
            continue;
        p = rg_pl_add(L, walls.p[i].name, 0);
        *p = walls.p[i];
    }
    rg_pl_free(&walls);
    return !L->failed;
}

/* ---- Littleroot's two houses (sp:770-906) --------------------------------------------------------- */

/* sp:776-780: a kitchen chair seen from the side, its back and its seat. */
static void house_chair(RgPieceList *L, const char *prefix, const int b[4], const int s[4], const uint32_t *lv,
                        unsigned nLv, bool seat)
{
    char nm[RG_PC_NAME];
    RgPieceDef *p;

    snprintf(nm, sizeof(nm), "%s_back", prefix);
    p = pr(L, nm, b[0], b[1], b[2], b[3], 9); leave(p, lv, nLv); p->solid = true;
    if (!seat)
        return;
    snprintf(nm, sizeof(nm), "%s_seat", prefix);
    p = pr(L, nm, s[0], s[1], s[2], s[3], 5); leave(p, lv, nLv); p->solid = true;
}

/* sp:783-797: the sides and back of a doorway's recess. */
static void stairwell(RgPieceList *L, const char *name, int x0, int x1, int front, int back_, int opening,
                      const int sd[4], int crest)
{
    RgPieceDef *p = rg_pl_add(L, name, 32);
    int low = opening < back_ - crest ? opening : back_ - crest;
    int mid = crest + opening;

    side(p, sd[0], sd[1], sd[2], sd[3]);
    if (mid >= front || mid <= back_) {
        wallh(p, x0, front, x0, back_, low);
        wallh(p, x1, back_, x1, front, low);
    } else {
        wallh(p, x0, front, x0, mid, opening);
        wallh(p, x0, mid, x0, back_, low);
        wallh(p, x1, back_, x1, mid, low);
        wallh(p, x1, mid, x1, front, opening);
    }
    wallh(p, x0, back_, x1, back_, low);
}

/* The four kitchen chairs of Littleroot's ground floors: nw, sw, ne, se with their back and seat rectangles. */
typedef struct ChairSet { int b[4][4], s[4][4]; } ChairSet;

static void four_chairs(RgPieceList *L, const ChairSet *c, const uint32_t *rug, unsigned nRug, bool seats)
{
    static const char *const names[4] = {"chair_nw", "chair_sw", "chair_ne", "chair_se"};
    unsigned i;

    for (i = 0; i < 4; i++)
        house_chair(L, names[i], c->b[i], c->s[i], rug, nRug, seats);
}

static bool pieces_brendan_1f(RgPieceList *L)
{
    static const ChairSet cs = {{{33, 96, 37, 112}, {33, 112, 37, 128}, {91, 96, 95, 112}, {91, 112, 95, 128}},
                                {{37, 100, 47, 112}, {37, 116, 47, 128}, {81, 100, 91, 112}, {81, 116, 91, 128}}};
    static const int sdSink[4] = {128, 29, 144, 34};
    RgPieceDef *p;

    four_chairs(L, &cs, kBlueRug, 3, true);
    p = pr(L, "table", 50, 96, 78, 128, 8); leave(p, kBlueRug, 3); p->solid = true;
    p = pr(L, "tv", 64, 61, 80, 88, 20); back(p, 72); topr(p, 68, 62, 78, 63); side(p, 64, 68, 66, 88);
    p = pr(L, "cabinet", 32, 65, 63, 88, 9); back(p, 72);
    p = pr(L, "fridge", 0, 18, 16, 48, 24); leave(p, kHouseFloor, 4); back(p, 32);
    p = pr(L, "tap", 22, 24, 29, 29, 5); p->base = 8; leave(p, kHouseWall, 2);
    p = pr(L, "sink", 16, 29, 48, 41, 8); srect(p, 17, 41, 47, 48); back(p, 32); topr(p, 18, 38, 46, 39);
    p = pr(L, "dresser", 49, 19, 72, 48, 23); back(p, 32);
    stairwell(L, "stairwell", 128, 144, 48, 29, 19, sdSink, 16);
    p = pr(L, "wall_e", 112, 32, 113, 48, 32);
    srect(p, 113, 16, 176, 29); srect(p, 113, 29, 128, 48); srect(p, 144, 29, 176, 48);
    p->fill = 16; foot(p, 48); side(p, 156, 16, 172, 48); wall(p, 112, 32, 112, 48);
    p = pr(L, "wall", 0, 0, 113, 32, 32); srect(p, 113, 0, 117, 16);
    p->fill = 16; foot(p, 32); side(p, 72, 0, 80, 32);
    p = rg_pl_add(L, "side_w", 32); side(p, 72, 0, 80, 32); wall(p, 0, 144, 0, 32);
    p = rg_pl_add(L, "side_e", 32); side(p, 156, 16, 172, 48); wall(p, 176, 48, 176, 144);
    return !L->failed;
}

static bool pieces_may_1f(RgPieceList *L)
{
    static const ChairSet cs = {{{81, 96, 85, 112}, {81, 112, 85, 128}, {139, 96, 143, 112}, {139, 112, 143, 128}},
                                {{85, 100, 95, 112}, {85, 116, 95, 128}, {129, 100, 139, 112}, {129, 116, 139, 128}}};
    static const int sdStair[4] = {32, 29, 48, 34};
    RgPieceDef *p;

    four_chairs(L, &cs, kPinkRug, 2, false);       /* only the backs: the seats are Brendan's, reused */
    p = pr(L, "table", 98, 96, 126, 128, 8); leave(p, kPinkRug, 2); p->solid = true;
    p = pr(L, "dresser", 105, 19, 128, 48, 23); back(p, 32);
    stairwell(L, "stairwell", 32, 48, 48, 29, 19, sdStair, 16);
    p = pr(L, "wall_w", 63, 32, 64, 48, 32);
    srect(p, 0, 16, 63, 29); srect(p, 0, 29, 32, 48); srect(p, 48, 29, 63, 48);
    p->fill = 16; foot(p, 48); side(p, 4, 16, 20, 48); wall(p, 64, 48, 64, 32);
    p = pr(L, "wall", 63, 0, 176, 32, 32); srect(p, 59, 0, 63, 16);
    p->fill = 16; foot(p, 32); side(p, 96, 0, 104, 32);
    p = rg_pl_add(L, "side_w", 32); side(p, 4, 16, 20, 48); wall(p, 0, 144, 0, 48);
    p = rg_pl_add(L, "side_e", 32); side(p, 96, 0, 104, 32); wall(p, 176, 32, 176, 144);
    return !L->failed;
}

static bool pieces_brendan_2f(RgPieceList *L)
{
    static const int sdStair[4] = {113, 14, 127, 26};
    RgPieceDef *p;

    p = pr(L, "chair_back", 1, 33, 5, 48, 9); leave(p, kHouseFloor, 4); p->solid = true;
    p = pr(L, "chair_seat", 5, 36, 14, 48, 5); leave(p, kHouseFloor, 4); p->solid = true;
    p = pr(L, "pc", 1, 9, 16, 30, 16); p->base = 9; leave(p, kHouseWall, 3); back(p, 32);
    p = pr(L, "stereo", 18, 24, 30, 31, 6); p->base = 9; p->solid = true;
    p = pr(L, "desk", 0, 20, 32, 40, 9); leave(p, kHouseFloor, 4); against(p, 32);
    p = pr(L, "console", 51, 24, 64, 40, 9); leave(p, kHouseWall, 3); leave(p, kHouseFloor, 4); back(p, 32);
    p = pr(L, "tv", 64, 13, 80, 40, 20); back(p, 32); topr(p, 68, 14, 78, 15); side(p, 64, 20, 66, 40);
    p = pr(L, "bed_head", 12, 62, 36, 70, 7); p->base = 7; leave(p, kHouseFloor, 4);
    p = pr(L, "bed", 12, 70, 36, 93, 7); leave(p, kHouseFloor, 4); p->solid = true; claim(p, 12, 70, 36, 72);
    stairwell(L, "stairwell", 112, 128, 32, 13, 19, sdStair, 0);
    p = pr(L, "wall", 0, 0, 112, 32, 32); srect(p, 112, 0, 128, 13); srect(p, 128, 0, 144, 32);
    p->fill = 16; foot(p, 32); side(p, 96, 0, 104, 32);
    p = rg_pl_add(L, "side_w", 32); side(p, 96, 0, 104, 32); wall(p, 0, 128, 0, 32);
    p = rg_pl_add(L, "side_e", 32); side(p, 96, 0, 104, 32); wall(p, 144, 32, 144, 128);
    return !L->failed;
}

static bool pieces_may_2f(RgPieceList *L)
{
    static const int sdStair[4] = {17, 14, 31, 26};
    RgPieceDef *p;

    p = pr(L, "chair_back", 139, 33, 143, 48, 9); leave(p, kHouseFloor, 4); p->solid = true;
    p = pr(L, "chair_seat", 130, 36, 139, 48, 5); leave(p, kHouseFloor, 4); p->solid = true;
    p = pr(L, "pc", 128, 9, 143, 30, 16); p->base = 9; leave(p, kHouseWall, 3); back(p, 32);
    p = pr(L, "stereo", 114, 24, 126, 31, 6); p->base = 9; p->solid = true;
    p = pr(L, "desk", 112, 20, 144, 40, 9); leave(p, kHouseFloor, 4); against(p, 32);
    p = pr(L, "console", 83, 24, 98, 40, 9); leave(p, kHouseWall, 3); leave(p, kHouseFloor, 4); back(p, 32);
    p = pr(L, "bed_head", 108, 62, 132, 70, 7); p->base = 7; leave(p, kHouseFloor, 4);
    p = pr(L, "bed", 108, 70, 132, 93, 7); leave(p, kHouseFloor, 4); p->solid = true; claim(p, 108, 70, 132, 72);
    stairwell(L, "stairwell", 16, 32, 32, 13, 19, sdStair, 0);
    p = pr(L, "wall", 0, 0, 16, 32, 32); srect(p, 16, 0, 32, 13); srect(p, 32, 0, 144, 32);
    p->fill = 16; foot(p, 32); side(p, 40, 0, 48, 32);
    p = rg_pl_add(L, "side_w", 32); side(p, 40, 0, 48, 32); wall(p, 0, 128, 0, 32);
    p = rg_pl_add(L, "side_e", 32); side(p, 40, 0, 48, 32); wall(p, 144, 32, 144, 128);
    return !L->failed;
}

/* ---- the two houses most of Hoenn lives in (sp:909-973) ------------------------------------------- */

/* sp:915-922 */
static void potted_plant(RgPieceDef *p, int x, int y)
{
    srect(p, x, y, x + 16, y + 13); srect(p, x + 3, y + 13, x + 13, y + 14);
    srect(p, x + 5, y + 14, x + 11, y + 16); srect(p, x + 6, y + 16, x + 10, y + 19);
    srect(p, x + 3, y + 19, x + 13, y + 23); srect(p, x + 2, y + 23, x + 14, y + 30);
    srect(p, x + 4, y + 30, x + 5, y + 31); srect(p, x + 11, y + 30, x + 12, y + 31);
}

static void generic_wall_floor(RgPieceDef *p)
{
    leave(p, kGenericWall, 4);
    leave(p, kGenericFloor, 4);
}

static bool pieces_house1(RgPieceList *L)
{
    static const int bN[4] = {74, 64, 78, 80}, sN[4] = {65, 68, 74, 80};
    static const int bS[4] = {74, 80, 78, 96}, sS[4] = {65, 84, 74, 96};
    RgPieceDef *p;

    house_chair(L, "chair_n", bN, sN, kGenericRug, 2, true);
    house_chair(L, "chair_s", bS, sS, kGenericRug, 2, true);
    p = pr(L, "teapot", 49, 72, 59, 80, 5); p->base = 11; leave(p, kGenericTable, 2);
    p = pr(L, "table", 34, 64, 62, 97, 11); leave(p, kGenericRug, 2); p->solid = true; p->fill = 1; foot(p, 97);
    p = rg_pl_add(L, "plant_se", 31); potted_plant(p, 144, 113); leave(p, kGenericFloor, 4); p->card = true;
    p = rg_pl_add(L, "plant_1", 31); potted_plant(p, 128, 17); generic_wall_floor(p); p->card = true;
    p = rg_pl_add(L, "plant_2", 31); potted_plant(p, 144, 17); generic_wall_floor(p); p->card = true;
    p = pr(L, "case", 0, 12, 32, 40, 20); generic_wall_floor(p); back(p, 32);
    p = pr(L, "drawers", 33, 11, 57, 41, 21); generic_wall_floor(p); back(p, 32);
    p = pr(L, "wall", 0, 0, 160, 32, 32); p->fill = 16; foot(p, 32); side(p, 64, 0, 80, 32);
    p = rg_pl_add(L, "side_w", 32); side(p, 64, 0, 80, 32); wall(p, 0, 144, 0, 32);
    p = rg_pl_add(L, "side_e", 32); side(p, 64, 0, 80, 32); wall(p, 160, 32, 160, 144);
    return !L->failed;
}

static bool pieces_house2(RgPieceList *L)
{
    static const int bNW[4] = {66, 64, 70, 80}, sNW[4] = {70, 68, 79, 80};
    static const int bSW[4] = {66, 80, 70, 96}, sSW[4] = {70, 84, 79, 96};
    RgPieceDef *p;

    house_chair(L, "chair_nw", bNW, sNW, kGenericFloor, 4, true);
    house_chair(L, "chair_sw", bSW, sSW, kGenericFloor, 4, true);
    p = pr(L, "chair_ne_back", 122, 64, 126, 80, 9); leave(p, kGenericFloor, 4); p->solid = true;
    p = pr(L, "chair_se_back", 122, 80, 126, 96, 9); leave(p, kGenericFloor, 4); p->solid = true;
    p = pr(L, "table", 82, 64, 110, 96, 10); leave(p, kGenericFloor, 4); p->solid = true;
    p = pr(L, "tv", 32, 13, 48, 39, 19); generic_wall_floor(p); back(p, 32); topr(p, 36, 14, 46, 15);
    side(p, 32, 20, 34, 39);
    p = pr(L, "tap", 133, 16, 142, 25, 8); p->base = 8; leave(p, kGenericWall, 2);
    p = pr(L, "cupboard", 112, 17, 128, 40, 16); generic_wall_floor(p); back(p, 32);
    p = pr(L, "sink", 128, 21, 160, 40, 8); generic_wall_floor(p); against(p, 32);
    p = pr(L, "fridge", 160, 10, 176, 40, 24); generic_wall_floor(p); back(p, 32);
    p = pr(L, "wall", 0, 0, 176, 32, 32); p->fill = 16; foot(p, 32); side(p, 48, 0, 64, 32);
    p = rg_pl_add(L, "side_w", 32); side(p, 48, 0, 64, 32); wall(p, 0, 128, 0, 32);
    p = rg_pl_add(L, "side_e", 32); side(p, 48, 0, 64, 32); wall(p, 176, 32, 176, 128);
    return !L->failed;
}

/* ---- Professor Birch's lab (sp:976-1037) ---------------------------------------------------------- */

static bool pieces_lab(RgPieceList *L)
{
    RgPieceDef *p;

    p = pr(L, "plant_s", 48, 189, 64, 208, 18); p->card = true;
    p = pr(L, "chair_sw_back", 34, 160, 47, 167, 7); p->base = 4; leave(p, kLabShadow, 2); p->solid = true;
    p = pr(L, "chair_sw_seat", 34, 167, 47, 176, 4); leave(p, kLabShadow, 2); p->solid = true;
    p = pr(L, "books_sw1", 1, 160, 16, 176, 9); leave(p, kLabShadow, 2); back(p, 160);
    p = pr(L, "books_sw2", 1, 176, 16, 192, 9); leave(p, kLabShadow, 2); back(p, 176);
    p = pr(L, "pc_sw", 16, 145, 32, 160, 8); p->base = 8; p->solid = true;
    p = pr(L, "desk_sw", 16, 160, 32, 192, 8); leave(p, kLabShadow, 2); back(p, 160);
    p = pr(L, "plant_se", 192, 141, 208, 161, 18); p->card = true;
    p = pr(L, "boxes_se", 192, 164, 208, 192, 20); leave(p, kLabShadow, 2); back(p, 176);
    p = pr(L, "chair_se_back", 161, 160, 165, 176, 9); leave(p, kLabShadow, 2); p->solid = true;
    p = pr(L, "chair_se_seat", 165, 164, 176, 176, 5); leave(p, kLabShadow, 2); p->solid = true;
    p = pr(L, "pc_se", 176, 145, 192, 160, 8); p->base = 8; p->solid = true;
    p = pr(L, "desk_se", 176, 160, 192, 192, 8); leave(p, kLabShadow, 2); back(p, 160);
    p = rg_pl_add(L, "dome", 5); sellipse(p, 176, 103.5, 8.5, 7.5); p->base = 10;
    p = pr(L, "machine", 160, 95, 192, 128, 10); side(p, 168, 110, 184, 120);
    p = pr(L, "canisters", 192, 109, 208, 128, 16); leave(p, kLabShadow, 2); back(p, 112);
    p = pr(L, "box_w1", 8, 82, 23, 96, 6); p->base = 21; p->solid = true;
    p = pr(L, "box_w2", 40, 82, 55, 96, 6); p->base = 21; p->solid = true;
    p = pr(L, "bookcase_w1", 0, 85, 32, 128, 21); back(p, 96);
    p = pr(L, "bookcase_w2", 32, 85, 64, 128, 21); back(p, 96);
    p = pr(L, "boxes_ne1", 144, 56, 160, 80, 8); leave(p, kLabShadow, 2);
    p = pr(L, "boxes_ne2", 160, 52, 176, 80, 18); leave(p, kLabShadow, 2); back(p, 64);
    p = pr(L, "books_ne1", 177, 32, 193, 48, 8); leave(p, kLabShadow, 2); back(p, 32);
    p = pr(L, "books_ne2", 193, 32, 208, 48, 8); leave(p, kLabShadow, 2); back(p, 32);
    p = pr(L, "books_ne3", 193, 48, 208, 64, 8); leave(p, kLabShadow, 2); back(p, 48);
    p = pr(L, "chair_n_back", 75, 48, 79, 64, 9); leave(p, kLabShadow, 2); p->solid = true;
    p = pr(L, "chair_n_seat", 64, 52, 75, 64, 5); leave(p, kLabShadow, 2); p->solid = true;
    p = pr(L, "boxes_w", 0, 41, 16, 64, 7); leave(p, kLabShadow, 2);
    p = pr(L, "plant_nw", 32, 29, 48, 48, 18); p->card = true;
    p = pr(L, "computer", 49, 10, 72, 30, 17); p->base = 8; back(p, 32);
    p = pr(L, "desk_pc", 48, 20, 80, 39, 8); leave(p, kLabShadow, 2); against(p, 32);
    p = pr(L, "book_red", 117, 19, 128, 31, 11); p->base = 8; back(p, 32);
    p = pr(L, "book_open", 134, 19, 147, 30, 6); p->base = 8; p->solid = true;
    p = pr(L, "binder", 147, 17, 158, 31, 13); p->base = 8; back(p, 32);
    p = pr(L, "desk_a", 96, 20, 128, 39, 8); leave(p, kLabShadow, 2); against(p, 32);
    p = pr(L, "desk_b", 128, 20, 160, 39, 8); leave(p, kLabShadow, 2); against(p, 32);
    p = pr(L, "bookcase_nw", 0, 8, 32, 40, 21); against(p, 32);
    p = pr(L, "wall", 0, 0, 208, 32, 32); p->fill = 16; foot(p, 32); side(p, 80, 0, 96, 32);
    p = rg_pl_add(L, "side_w", 32); side(p, 80, 0, 96, 32); wall(p, 0, 208, 0, 32);
    p = rg_pl_add(L, "side_e", 32); side(p, 80, 0, 96, 32); wall(p, 208, 32, 208, 208);
    return !L->failed;
}

/* sp:1347-1353: after the starter the boxes by the machine give way to a table; the lab's two side walls are
 * its own (the rest of the furniture is reused). */
static bool pieces_lab_table(RgPieceList *L)
{
    RgPieceList lab;
    RgPieceDef *p;
    unsigned i;

    p = pr(L, "table", 128, 64, 176, 89, 8); leave(p, kLabShadow, 2); p->solid = true;
    memset(&lab, 0, sizeof(lab));
    if (!pieces_lab(&lab)) {
        rg_pl_free(&lab);
        return false;
    }
    for (i = 0; i < lab.n; i++)
        if (strcmp(lab.p[i].name, "side_w") == 0 || strcmp(lab.p[i].name, "side_e") == 0) {
            p = rg_pl_add(L, lab.p[i].name, 0);
            *p = lab.p[i];
        }
    rg_pl_free(&lab);
    return !L->failed;
}

/* ---- Rustboro's gym (sp:1040-1090) ------------------------------------------------------------------ */

/* sp:1044-1048: a maze block; each rect in cells, drawn from 8 rows north of its cells to its foot. */
static void gym_block(RgPieceList *L, const char *name, const int (*r)[4], unsigned n)
{
    RgPieceDef *p = rg_pl_add(L, name, 10);
    unsigned i;

    for (i = 0; i < n; i++)
        srect(p, r[i][0] * 16, r[i][1] * 16 - 8, r[i][2] * 16, r[i][3] * 16);
    side(p, 56, 118, 72, 128);
}

/* sp:1051-1054: a statue's outline, its cell's left edge at x. */
static void gym_statue(RgPieceDef *p, int x)
{
    srect(p, x, 272, x + 16, 302); srect(p, x + 1, 302, x + 15, 303); srect(p, x + 2, 303, x + 14, 304);
}

static bool pieces_gym(RgPieceList *L)
{
    static const int sw[2][4] = {{2, 13, 4, 15}, {2, 15, 5, 16}};
    static const int s_[1][4] = {{6, 15, 8, 16}};
    static const int w_[2][4] = {{4, 9, 5, 11}, {2, 11, 5, 12}};
    static const int c_[1][4] = {{6, 9, 8, 11}};
    static const int ea[2][4] = {{6, 12, 9, 13}, {7, 13, 9, 14}};
    static const int e_[1][4] = {{9, 6, 11, 16}};
    static const int n_[1][4] = {{3, 6, 9, 8}};
    static const int ww[1][4] = {{0, 6, 1, 16}};
    RgPieceDef *p;

    p = rg_pl_add(L, "statue_w_ball", 8); sellipse(p, 40, 279, 7, 7); p->base = 16;
    p = rg_pl_add(L, "statue_w", 16); gym_statue(p, 32); back(p, 288);
    p = rg_pl_add(L, "statue_e_ball", 8); sellipse(p, 136, 279, 7, 7); p->base = 16;
    p = rg_pl_add(L, "statue_e", 16); gym_statue(p, 128); back(p, 288);
    gym_block(L, "block_sw", sw, 2);
    gym_block(L, "block_s", s_, 1);
    gym_block(L, "block_w", w_, 2);
    gym_block(L, "block_c", c_, 1);
    gym_block(L, "block_e_arm", ea, 2);
    gym_block(L, "block_e", e_, 1);
    gym_block(L, "block_n", n_, 1);
    gym_block(L, "block_west", ww, 1);
    p = pr(L, "wall_w", 0, 0, 16, 48, 25); p->fill = 16; foot(p, 48); side(p, 32, 7, 48, 32);
    p = pr(L, "wall_e", 160, 0, 176, 48, 25); p->fill = 16; foot(p, 48); side(p, 32, 7, 48, 32);
    p = pr(L, "wall", 16, 0, 160, 32, 25); p->fill = 16; foot(p, 32); side(p, 32, 7, 48, 32);
    p = rg_pl_add(L, "side_w", 25); side(p, 32, 7, 48, 32); wall(p, 0, 176, 0, 48);
    p = rg_pl_add(L, "side_w2", 25); side(p, 32, 7, 48, 32); wall(p, 0, 320, 0, 176);
    p = rg_pl_add(L, "side_e", 25); side(p, 32, 7, 48, 32); wall(p, 176, 48, 176, 176);
    p = rg_pl_add(L, "side_e2", 25); side(p, 32, 7, 48, 32); wall(p, 176, 176, 176, 320);
    return !L->failed;
}

/* ---- the room descriptors (sp:1296-1375) ------------------------------------------------------------- */

#define POLY3(a, b, c, d, e, f) {RG_SH_POLY, 3, {a, b, c, d, e, f, 0, 0}}
#define CENTER_1F_OPEN {{POLY3(0, 126, 18, 144, 0, 144), POLY3(224, 126, 206, 144, 224, 144)}, 2}
#define CENTER_2F_OPEN {{POLY3(0, 142, 18, 160, 0, 160), POLY3(224, 142, 206, 160, 224, 160)}, 2}
#define MART_OPEN {{POLY3(0, 118, 10, 128, 0, 128), POLY3(176, 118, 166, 128, 176, 128)}, 2}
#define NO_OPEN {{{0}}, 0}

const RgRoomDef rg_room_pc1f = {pieces_pc1f, CENTER_1F_OPEN, {0}, 0};
const RgRoomDef rg_room_pc2f = {pieces_pc2f, CENTER_2F_OPEN, {0}, 0};
const RgRoomDef rg_room_mart = {pieces_mart, MART_OPEN, {0x202, 0x204, 0x206, 0x208, 0x20A}, 5};
const RgRoomDef rg_room_brendan_1f = {pieces_brendan_1f, NO_OPEN, {0}, 0};
const RgRoomDef rg_room_brendan_2f = {pieces_brendan_2f, NO_OPEN, {0}, 0};
const RgRoomDef rg_room_may_1f = {pieces_may_1f, NO_OPEN, {0}, 0};
const RgRoomDef rg_room_may_2f = {pieces_may_2f, NO_OPEN, {0}, 0};
const RgRoomDef rg_room_lab = {pieces_lab, NO_OPEN, {0}, 0};
const RgRoomDef rg_room_lab_table = {pieces_lab_table, NO_OPEN, {0}, 0};
const RgRoomDef rg_room_lavaridge_pc1f = {pieces_lavaridge, CENTER_1F_OPEN, {0}, 0};
const RgRoomDef rg_room_house1 = {pieces_house1, NO_OPEN, {0}, 0};
const RgRoomDef rg_room_house2 = {pieces_house2, NO_OPEN, {0}, 0};
const RgRoomDef rg_room_rustboro_gym = {pieces_gym, NO_OPEN, {0x202, 0x203, 0x204, 0x216, 0x22f, 0x237}, 6};
