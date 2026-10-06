/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/gen_voxel_relief.py (solve, awash,
 * solve_drawn, ledges_on_ground, pier_ends, layout_heights: rel:189-398, 1851-2336, 2466-2595), MIT License - see
 * source/voxel/NOTICE.md. Portions Copyright (c) Dust Zallax, MIT. */
/* rg_rsolve.h -- the height lattices: the drawn groups' terraces and rock solved cell by cell (solve_drawn), the
 * harmonic fallback for the ENABLED maps (solve), ledges laid on the ground, piers, and layout_heights (3DGBA,
 * GPLv3). Pure C. Spec: docs/phase33-romgen/SPEC-S3.md sections 1.5, 2.2, 2.3, 6 (S3.6). */
#ifndef RG_RSOLVE_H
#define RG_RSOLVE_H

#include "rg_ledge.h"
#include "rg_rlat.h"
#include "rg_rworld.h"

#define RG_LV_NONE INT32_MIN         /* upstream None in a level / top grid */
#define RG_MOUND 8                   /* mound rise per lattice step inland from the water (rel:169) */
#define RG_MOUND_MAX 24
#define RG_SOLVE_SWEEPS 400          /* solve(): Gauss-Seidel cap (rel:189-398) */
#define RG_SOLVE_TOL 0.01
#define RG_LAY_SWEEPS 64             /* solve_drawn: lay() sweeps (rel:2300) */
#define RG_SHORE_STRIP 8             /* rel:611 */
#define RG_RIDGE_TOP 2               /* rel:585 */
#define RG_PIER_REACH 2              /* rel:2505 */

/* rock tile kinds of _ROCK[group][1] */
enum { RG_RT_NONE = 0, RG_RT_FACE, RG_RT_BAND, RG_RT_CORNER };

/* One solved group: what S3.7 (cut_cells, cell_shapes) reads of _CELLS / _ROCK / _DRAWN_SIDE, plus the numbers the
 * done-gate reports. Cell arrays are cw*ch, row major. */
typedef struct RgSolvedGroup {
    uint16_t key;
    uint8_t isAlt;
    int cw, ch;
    int32_t *cell;                   /* _CELLS: footprint level, RG_LV_NONE for rock between */
    int32_t *top;                    /* _ROCK top of each rock cell, RG_LV_NONE */
    uint8_t *tile;                   /* _ROCK kinds: RG_RT_* (0 where not rock) */
    uint8_t *soil, *pier, *voidCell;
    int8_t *side;                    /* _DRAWN_SIDE */
    uint16_t *meta;                  /* metatile_at */
    unsigned nMem;
    RgCanvasMember *mem;
    /* stats */
    unsigned nRock, nShapes, nCrests, laySweeps;   /* laySweeps: passes run, the last of them without a change */
    uint8_t layCapped;               /* the 64-sweep cap was hit */
    double minH, maxH;               /* over the whole group lattice, after the None fill */
} RgSolvedGroup;

/* All of solve_drawn's products, by group index (RgDrawn order) and by layout id. */
typedef struct RgSolved {
    unsigned nGroups;
    RgSolvedGroup *grp[RG_DRAWN_MAX_GROUPS];      /* NULL: not solved (excluded, dropped, or not run) */
    uint8_t have[512];               /* _BASE / _SHIFT hold this layout */
    uint8_t groupOf[512];            /* index of the group that solved it */
    int32_t base[512];               /* _BASE: pixels */
    RgLat shift[512];                /* _SHIFT: the layout's lattice (value - base); pier_ends edits it in place */
    unsigned pierDouble;             /* pier_ends: a water cell written twice with different values (D4 assert: 0) */
} RgSolved;

/* solve_drawn (rel:1851-2336) for one prepared group. `level` = lv->level[g] (a level per region). Fills
 * out->grp[gi] and, for every member layout the group owns (rg_drawn_group(.., false) == g), base + shift. */
RgErr rg_solve_group(const RgWorld *w, const RgDrawn *d, unsigned gi, const RgPrep *p, const int32_t *level,
                     RgSolved *out);
/* Every group the world solve kept (lv->ok): re-prepares it with `c` (the context rg_world_prep_all used), solves. */
RgErr rg_solve_all(RgRCtx *c, const RgWorld *w, const RgDrawn *d, const RgLevels *lv, RgSolved *out);
void rg_solved_free(RgSolved *s);

/* solve() (rel:189-398): the per-layout harmonic lattice of an ENABLED map. `pair` = the layout's open pair (awash
 * reads its colours). roles from S1. stats may be NULL. */
typedef struct RgPlainStats { unsigned sweeps, regions, mounds, awashMasses; double minH, maxH; } RgPlainStats;
RgErr rg_solve_plain(const RgWorld *w, const RgRoles *r, const RgLayout *L, RgPair *pair, RgLat *out,
                     RgPlainStats *st);

/* rel:2466-2504: every ledge cell back on the plane of its four corners (h is the layout's lattice). */
RgErr rg_ledges_on_ground(const RgLayout *L, RgLat *h);
/* rel:2508-2541: a pier's planks run on into the water cell at its end. `sv` (the _SHIFT lattice) may be NULL.
 * Counts conflicting double writes into *conflicts (may be NULL). */
RgErr rg_pier_ends(const RgSolvedGroup *g, uint16_t layoutId, const RgLayout *L, RgPair *pair, const RgRoles *r,
                   RgLat *h, RgLat *sv, unsigned *conflicts);

/* layout_heights (rel:2544-2562): the lattice of a layout: solve_drawn's (when the group survived the world solve),
 * else solve() for an ENABLED map, else flat; ledges laid on the ground, piers, then the berms. *ledgeCells gets
 * rg_ledge_berms' count (may be NULL). `solved` is edited (pier_ends on _SHIFT) and must have been through
 * rg_solve_all with the same levels. */
RgErr rg_layout_heights(const RgWorld *w, const RgRoles *r, const RgDrawn *d, const RgLevels *lv, RgSolved *solved,
                        uint16_t layoutId, RgLat *h, int *ledgeCells);

#endif
