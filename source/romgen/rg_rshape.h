/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/gen_voxel_relief.py (rim_cells, SPREAD,
 * spread, cell_shapes: rel:3002-3682), MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
/* rg_rshape.h -- every cell's own shape in a drawn layout: the ground run on behind a terrace's rim, the rock to its
 * edge, the cliff walls (3DGBA, GPLv3). Pure C. Spec: docs/phase33-romgen/SPEC-S3.md sections 1.5, 6 (S3.7). */
#ifndef RG_RSHAPE_H
#define RG_RSHAPE_H

#include "rg_rcut.h"
#include "rg_rsolve.h"

#define RG_SPREAD 1.25               /* rel:3088 */
#define RG_FLANK 40                  /* rel:3407: how far a flank is searched for, pixels */

/* What cell_shapes reads of one drawn layout. `grp` is the solved group the layout stands in, (ox, oy) its cell
 * offset on the group canvas (DRAWN[group][lid]); `wrap` = group in WRAP_GROUPS; `h` is layout_heights' lattice. */
typedef struct RgShapeIn {
    RgCutArt *art;
    const RgLayout *L;
    const RgSolvedGroup *grp;
    int ox, oy;
    bool wrap;
    const RgLat *h;
    const RgCutRec *cut;             /* cut_cells' records for this layout */
    unsigned nCut;
} RgShapeIn;

/* One ground run on under a cell: rel `fills` entries (x, y, foot, ground[, GOES_ON]); ground -1 = None. */
typedef struct RgFill { uint8_t x, y; double foot; int32_t ground; uint8_t goesOn; } RgFill;

/* cell_shapes' results (and rim_cells'). grid[c] is meaningful where has[c]; wall* where hasWall[c]. */
typedef struct RgShapes {
    int w, h;
    RgGrid *grid;                    /* w*h */
    uint8_t *has;
    RgFill *fills;                   /* rim_cells' (sorted by (x, y)), then cell_shapes' appended ones */
    unsigned nFills, capFills;
    uint8_t *hasWall;
    uint16_t *wallFace;              /* the face metatile (analysis id) or RG_NO_FACE */
    uint8_t *wallBits;               /* edge_rock bits */
    unsigned faceTies;               /* diagnostic: faces Counter had a tie for the most common (first inserted won) */
} RgShapes;

void rg_shapes_free(RgShapes *s);

/* rel:3092-3146 spread: out = h with every rock fall spread evenly. content = w*h flags. */
RgErr rg_spread(const RgLat *h, const uint8_t *content, RgLat *out);

/* rel:3002-3086 rim_cells with base = the spread lattice (NULL: none). Fills out->grid/has and out->fills. */
RgErr rg_rim_cells(const RgShapeIn *in, const RgLat *base, RgShapes *out);

/* rel:3148-3682 cell_shapes. */
RgErr rg_cell_shapes(const RgShapeIn *in, RgShapes *out);

#endif
