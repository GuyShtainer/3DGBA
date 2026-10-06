/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/gen_voxel_relief.py (drawn_prepare,
 * split_wrapped and its helpers: rel:1335-1849), MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
/* rg_rprep.h -- what a group's drawing says: regions, drops, runs, ties, per-cell stats, map edges (3DGBA, GPLv3).
 * Pure C. Spec: docs/phase33-romgen/SPEC-S3.md sections 1.5, 6 (S3.4). The levels come from world_levels (S3.5). */
#ifndef RG_RPREP_H
#define RG_RPREP_H

#include "rg_rcanvas.h"

#define RG_REGION_MIN 200u           /* smaller patches of top are not terraces (rel:592) */
#define RG_THIN 7u                   /* nor is a strip of top thinner than this down its columns, on average (rel:593) */
#define RG_RIM_RISE 16               /* a rim is a step of this up to the south (rel:564) */
#define RG_SIDE_RISE 16              /* a west or east face is a level (rel:584) */
#define RG_LEVEL 16                  /* pixels per level (rel:166) */
#define RG_WALKED 1000u              /* zero drops a walked link adds (rel:1564-1849) */
#define RG_WRAP_DROP 6
#define RG_WRAP_COLUMNS 3
#define RG_WRAP_CUT 24
#define RG_WRAP_POCKET 64
#define RG_WRAP_REACH 64
#define RG_WRAP_DEEP 5

/* The drops two regions are joined by (upstream runs[(a, b)] list), keys in first-insertion order. */
typedef struct RgRunKey { int32_t a, b; uint32_t n, cap; int32_t *v; } RgRunKey;
/* ties: pairs "the cartridge says are one level", unique, in insertion order (rg_set_order input in S3.5). */
typedef struct RgTie { int32_t a, b; } RgTie;
/* stats[cy][cx] = (n, top, ground, counts) or None (n == 0): counts are the big regions drawn in the cell,
 * (region, pixels), first-seen order, at counts[cOff .. cOff + nCounts). */
typedef struct RgCellStat { uint16_t n, top, ground, nCounts; uint32_t cOff; } RgCellStat;
typedef struct RgCount { int32_t region; uint32_t n; } RgCount;

enum { RG_EDGE_UP = 0, RG_EDGE_DOWN, RG_EDGE_LEFT, RG_EDGE_RIGHT };   /* "up", "down", "left", "right" (rel:1830) */

typedef struct RgPrep {
    RgCanvas cv;                     /* owned: members, kind, blocked, side, flat, faceLow, pier, meta */
    int32_t *region;                 /* per pixel: terrace or ground region, -1 none (rock, void, free) */
    uint32_t nRegions, capRegions;
    uint32_t *sizes;                 /* pixels per region */
    uint8_t *big;                    /* a terrace: REGION_MIN pixels and not thin (or cut apart) */
    uint8_t *cutApart;               /* regions split_wrapped cut (upstream cut_apart) */
    unsigned nRuns, capRuns;
    RgRunKey *runs;
    unsigned nTies, capTies;
    RgTie *ties;
    RgCellStat *stats;               /* cw*ch */
    RgCount *counts;
    size_t nCounts, capCounts;
    uint8_t *freeMid;                /* cw*ch: the cell's centre pixel is FREE */
    int32_t *edges;                  /* per member and edge, the pool; edgeOff[m * 4 + e] is its start */
    uint32_t *edgeOff;               /* length of an up/down edge = member w, of left/right = member h */
    unsigned wrapCuts;               /* regions split_wrapped made (new labels) */
    bool wrapped;                    /* this group went through split_wrapped */
} RgPrep;

/* Appends a region of `count` pixels (sizes, big, cutApart grow together). False on no memory. */
bool rg_prep_add_region(RgPrep *p, uint32_t count);

/* split_wrapped (rel:1521-1561): cuts the regions the faces say are two; returns false on no memory. Marks cutApart
 * and counts wrapCuts. Only ever called for the WRAP groups. */
bool rg_split_wrapped(RgPrep *p);

/* drawn_prepare for a built canvas (rel:1564-1849). `wrap` runs split_wrapped (the WRAP_GROUPS). Takes ownership of
 * *cv (zeroed on return, success or not); free the result with rg_prep_free even after a failure. */
RgErr rg_prep_run(RgCanvas *cv, bool wrap, RgPrep *out);
/* canvas + prepare for one group of rg_drawn_find: wrap = a seed group named by RG_WRAP_GROUP_SEEDS (not an
 * alternate group). */
RgErr rg_prepare(RgRCtx *c, const RgDrawn *d, const RgDrawnGroup *g, RgPrep *out);
bool rg_group_is_wrap(const RgDrawnGroup *g);
void rg_prep_free(RgPrep *p);

#endif
