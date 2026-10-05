/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/voxel_building.py
 * (geometry kernel, texture mappings, every part class), MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
/* rg_geom.h -- the building mesh and its part types (3DGBA, GPLv3). Pure C. SPEC-S2 section 1.3, 2.
 *
 * Doubles everywhere, as Python floats; compile with -ffp-contract=off. Relief and Mound (vb:877-1383) are S2.5. */
#ifndef RG_GEOM_H
#define RG_GEOM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rg_bimg.h"

/* vb:68-79 */
#define RG_EPS 1e-6
#define RG_SHADE_ART 1.0
#define RG_SHADE_WEST 0.80
#define RG_SHADE_EAST 0.72
#define RG_SHADE_BACK 0.66
#define RG_SHADE_WOUND 0.90

/* sum() of floats: CPython 3.12+ is Neumaier-compensated, 3.11 naive. Default 1 = CI's 3.12 (SPEC-S2 H1/A1). */
#ifndef RG_PYSUM_COMPENSATED
#define RG_PYSUM_COMPENSATED 1
#endif
double rg_pysum(const double *v, unsigned n);

/* H8: Python's // and % floor toward minus infinity. */
int rg_floordiv(int a, int b);
int rg_floormod(int a, int b);

/* H7: stable merge sort (qsort is not stable). False when the scratch allocation fails. */
bool rg_stable_sort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *));

/* ---- mesh ---- */

enum { RG_TAG_CLAMP = 1, RG_TAG_PROJ = 2, RG_TAG_DEPTH = 4, RG_TAG_BEHIND = 8 };   /* suffix flags of the tag name */
#define RG_TAG_LEN 64
#define RG_NO_TAG 0xFFFFu

typedef struct RgVtx { double x, y, z, u, v; } RgVtx;
typedef struct RgTri {
    RgVtx p[3];
    double shade;
    uint16_t tag;                /* index into RgMesh.names */
    uint8_t flags;               /* RG_TAG_* of that name, cached so the checks never parse strings */
} RgTri;
typedef struct RgMesh {
    RgTri *t;
    unsigned n, cap;
    char (*names)[RG_TAG_LEN];
    unsigned nNames, capNames;
    bool failed;                 /* sticky: an allocation or table limit was hit */
} RgMesh;

void rg_mesh_init(RgMesh *m);
void rg_mesh_free(RgMesh *m);
/* Interns a tag name; RG_NO_TAG on failure (also sets m->failed). */
uint16_t rg_mesh_tag(RgMesh *m, const char *name);
void rg_mesh_tri(RgMesh *m, const RgVtx *a, const RgVtx *b, const RgVtx *c, double shade, uint16_t tag);
/* Triangle fan (pts[0], pts[i], pts[i+1]) as Mesh.poly. */
void rg_mesh_poly(RgMesh *m, const RgVtx *pts, unsigned n, double shade, uint16_t tag);

/* ---- kernel (vb:296-394) ---- */

/* A tuple of up to 5 coordinates; nc says how many are live. */
typedef struct RgPt { double c[5]; } RgPt;
#define RG_CLIP_MAX 32u
/* Sutherland-Hodgman against axis = val, keeping >= (keep) or <= (!keep), EPS inclusive. `out` holds >= 2n.
 * Every live coordinate is interpolated. Returns the output count. */
unsigned rg_clip(const RgPt *in, unsigned n, unsigned nc, unsigned axis, double val, bool keep, RgPt *out);
/* Ear clipping of a simple (z, y) polygon, either winding; tri[] holds n-2 index triples. Returns the triangle
 * count (guard 10000 iterations as upstream). n <= 16. */
unsigned rg_triangulate(const double (*zy)[2], unsigned n, unsigned (*tri)[3]);
bool rg_polygon_ccw(const double (*zy)[2], unsigned n);

typedef struct RgTile {          /* vb:268-277 */
    double rect[4];              /* u0, v0, u1, v1 */
    double s0;
    bool hasTop;
    double top;
    bool flip;
} RgTile;
typedef struct RgProj {          /* vb:250-265 */
    bool hasLo, hasHi;
    double lo, hi;
} RgProj;
typedef struct RgStrip {         /* vb:535-611; call rg_strip_fin after filling */
    double fixed[2];
    bool hasRepeat;  double repeat[2];
    bool hasStart;   double start;     /* start: resolved by rg_strip_fin */
    bool hasWrap;    double wrap[2];
    double offset;
    bool fromEnd;
    bool hasTail;    double tail[2];
    bool hasRepeatOffset; double repeatOffset;
} RgStrip;
typedef struct RgBand {          /* vb:280-293 */
    double y0, y1;
    RgTile tile;
    double z0;
    bool hasFront; RgTile front;
    bool hasBack;  RgTile back;
    double length;               /* z0 - z1, or 1e9 when no z1 was given */
} RgBand;

RgTile rg_tile(double u0, double v0, double u1, double v1);
RgTile rg_tile_top(double u0, double v0, double u1, double v1, double top);
RgProj rg_proj(void);                           /* no clamp */
RgProj rg_proj_rows(double lo, double hi);
RgStrip rg_strip_fin(RgStrip s);                /* resolves `start` as Strip.__init__ */
RgBand rg_band(double y0, double y1, RgTile tile, double z0);
void rg_band_z1(RgBand *b, double z1);

typedef void (*RgPieceFn)(void *ctx, const RgPt *pts, unsigned n);   /* pts.c = s, t, u, v */
/* vb:357-394: poly2d (s,t) split into pieces that each sit in one tile; each piece has (u,v) appended and is handed
 * to cb in upstream order. False when the tile is degenerate or the span is absurd (> 1e6 tiles). */
bool rg_tile_pieces(const RgPt *poly2d, unsigned n, const RgTile *tile, RgPieceFn cb, void *ctx);

/* vb:614-645: a planar convex face (pts, 3D) textured by a Strip. sdir/tdir are the unit directions. */
void rg_strip_face(RgMesh *m, const double (*pts)[3], unsigned n, const double origin[3], const double sdir[3],
                   const double tdir[3], const RgStrip *strip, double shade, const char *tag);
void rg_unit3(const double v[3], double out[3]);       /* vb:648 _unit */

/* ---- parts ---- */

typedef enum { RG_P_PRISM, RG_P_HIPROOF, RG_P_FRUSTUM, RG_P_VAULT, RG_P_WALLS, RG_P_LIFTED, RG_P_CARD,
               RG_P_FACET, RG_P_PLAINWALL, RG_P_DECAL, RG_P_CYLINDER, RG_P_FOUNTAIN_TOP, RG_P_JET } RgPartKind;

typedef enum { RG_EM_NONE = 0, RG_EM_PROJ, RG_EM_STRIP, RG_EM_TILE } RgEdgeKind;
typedef struct RgEdgeMat { RgEdgeKind kind; RgProj proj; RgStrip strip; RgTile tile; } RgEdgeMat;

#define RG_PRISM_PTS 8
#define RG_PRISM_CAPS 4
typedef struct RgPrism {         /* vb:419-440 */
    double x0, x1;
    double poly[RG_PRISM_PTS][2];            /* (z, y), counter-clockwise */
    unsigned nPoly;
    RgEdgeMat edges[RG_PRISM_PTS];
    bool hasCaps;
    RgBand caps[RG_PRISM_CAPS];
    unsigned nCaps;
    bool west, east;
    uint32_t skip;                           /* bit i = edge i skipped */
} RgPrism;

typedef struct RgHip {           /* vb:653-704 */
    double x0, x1, zf, zb, y0;
    RgStrip fascia, slope;
    double teeth[2], cap[2];
    double pitch, run, ridgeU[2];
    RgTile endTile;
    bool ridge;
    bool hasRidgeWrap; double ridgeWrap[2];
    double ye, zrf, zrb, rise, yr, yt;       /* derived by rg_hip_init */
} RgHip;
void rg_hip_init(RgHip *h);
/* (z, y) of the front slope `texels` art rows above its eave (vb:690-695). */
void rg_hip_slope_point(const RgHip *h, double texels, double *z, double *y);

#define RG_PLAN_PTS 16
typedef struct RgFrustum {       /* vb:774-827 */
    double plan[RG_PLAN_PTS][2];             /* (x, z) listed clockwise on screen */
    unsigned nPlan;
    double wallTop;
    RgStrip wallSide;
    double bandRise;
    RgStrip bandSide, top;
} RgFrustum;
typedef struct RgVault { double profile[RG_PLAN_PTS * 2][2]; unsigned nProfile; double zf, zb, y0; } RgVault;   /* vb:830 */
typedef struct RgWalls { double plan[RG_PLAN_PTS][2]; unsigned nPlan; double y0, y1; } RgWalls;                  /* vb:858 */
typedef struct RgCard { const RgImage *art; double foot; double voff; } RgCard;                                  /* vb:1386 */
typedef struct RgFacet { double a[2], b[2]; double ha, hb; } RgFacet;                                            /* vb:1415 */
typedef struct RgPlainWall { double a[2], b[2]; double y0, y1; RgTile tile; } RgPlainWall;                       /* vb:1432 */
typedef struct RgDecal { const int16_t (*cells)[2]; unsigned nCells; double vOffset, lift; } RgDecal;           /* vb:1453 */
typedef struct RgCylinder {      /* vb:1474 */
    double cx, cz, rx, rz, y0, y1;
    RgTile backTile;
    unsigned sides;
} RgCylinder;

/* sp:552-577: the fountain's own parts. FountainTop = a flat polygon (x, z) at height h, projected (the rim and
 * water) plus the side quads the drawing never shows; Jet = a standing projected plane at depth z. */
#define RG_FTOP_PTS 8
typedef struct RgFountainTop { double poly[RG_FTOP_PTS][2]; unsigned nPoly; double h; } RgFountainTop;
typedef struct RgJet { double x0, x1, z, y0, y1; } RgJet;

#define RG_NAME_LEN 48
typedef struct RgPart {
    RgPartKind kind;
    char name[RG_NAME_LEN];
    union {
        RgPrism prism;
        RgHip hip;
        RgFrustum frustum;
        RgVault vault;
        RgWalls walls;
        struct { const struct RgPart *part; double base; } lifted;   /* vb:1145: moved by (0, base, base) */
        RgCard card;
        RgFacet facet;
        RgPlainWall plain;
        RgDecal decal;
        RgCylinder cyl;
        RgFountainTop ftop;
        RgJet jet;
    } u;
} RgPart;

/* Appends the part's triangles in upstream emission order. False for an invalid part (a prism that is not
 * counter-clockwise, a degenerate tile) or when the mesh ran out of memory. */
bool rg_part_emit(const RgPart *p, RgMesh *m);

/* A growing list with stable addresses (a Lifted part points at another entry): chunks of 64. */
#define RG_PART_CHUNK 64u
#define RG_PART_CHUNKS 64u
typedef struct RgPartList { RgPart *chunk[RG_PART_CHUNKS]; unsigned n; bool failed; } RgPartList;
void rg_parts_init(RgPartList *l);
void rg_parts_free(RgPartList *l);
RgPart *rg_parts_add(RgPartList *l, RgPartKind kind, const char *name);   /* zeroed; NULL on failure */
RgPart *rg_parts_at(const RgPartList *l, unsigned i);
/* Emits every part in list order (Model.__init__, vb:1520). */
bool rg_parts_emit(const RgPartList *l, RgMesh *m);

#endif
