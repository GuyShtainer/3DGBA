/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/voxel_building_specs.py
 * (piece(), facet(), the 13 room piece tables and their helpers), MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
/* rg_brooms.h -- the interior room tables: a room cut into pieces, front first (3DGBA, GPLv3). Pure C.
 * SPEC-S2 section 1.6, 3.2, 6.1 row S2.6. Shapes are in pixels of the room's drawing. */
#ifndef RG_BROOMS_H
#define RG_BROOMS_H

#include <stdbool.h>
#include <stdint.h>

typedef enum { RG_SH_RECT = 0, RG_SH_ELLIPSE, RG_SH_POLY } RgShapeKind;
/* RECT: v = x0, y0, x1, y1. ELLIPSE: v = cx, cy, rx, ry. POLY: n points, v = x0, y0, x1, y1, ... (n <= 4). */
typedef struct RgShapePart { uint8_t kind, n; double v[8]; } RgShapePart;
#define RG_SHAPE_MAX 8u
typedef struct RgShape { RgShapePart p[RG_SHAPE_MAX]; unsigned n; } RgShape;

typedef struct RgWallDef { double a[2], b[2]; bool hasH; int h; } RgWallDef;     /* ((x, z), (x, z)[, height]) */

#define RG_PC_NAME 24
#define RG_PC_LEAVE 10u
#define RG_PC_WALLS 6u
/* sp:593-626 piece() / facet(): one field per keyword argument. Colours are RGB888 (0xRRGGBB). */
typedef struct RgPieceDef {
    char name[RG_PC_NAME];
    RgShape shape;
    int height, base;
    int fill;                           /* 0 = None */
    uint32_t leave[RG_PC_LEAVE];
    unsigned nLeave;
    bool hasSide;  int side[4];
    bool solid;
    bool hasBack;  int back;
    bool card;
    bool hasTop;   int top[4];
    bool hasFoot;  int foot;
    RgWallDef walls[RG_PC_WALLS];
    unsigned nWalls;
    int cells[2][2];
    unsigned nCells;
    bool hasAgainst; int against;
    bool hasClaim; int claim[4];        /* a list of one rectangle in every table */
    bool hasFacet; double facet[2][3];  /* (x, top, foot) twice */
} RgPieceDef;

typedef struct RgPieceList { RgPieceDef *p; unsigned n, cap; bool failed; } RgPieceList;
void rg_pl_free(RgPieceList *l);
RgPieceDef *rg_pl_add(RgPieceList *l, const char *name, int height);      /* zeroed; NULL on no memory */

typedef struct RgRoomDef {
    bool (*pieces)(RgPieceList *out);
    RgShape open;                       /* the room's front corners: floor in the round where the drawing is black */
    uint16_t shade[8];                  /* the floor's shaded variants (metatiles) */
    unsigned nShade;
} RgRoomDef;

extern const RgRoomDef rg_room_pc1f, rg_room_pc2f, rg_room_mart, rg_room_brendan_1f, rg_room_brendan_2f,
    rg_room_may_1f, rg_room_may_2f, rg_room_lab, rg_room_lab_table, rg_room_lavaridge_pc1f, rg_room_house1,
    rg_room_house2, rg_room_rustboro_gym;

/* Every RGB888 colour constant the tables use, for the host test (each must have a BGR555 preimage). */
unsigned rg_room_colours(const uint32_t **out);

#endif
