/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/gen_voxel_relief.py (_gauss_seidel,
 * _robust, _give_up_seams, _blocks, world_levels: rel:762-1086), MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
/* rg_rworld.h -- the world's levels: every drawn group's terraces solved against each other, then placed so the ground
 * either side of every seam meets (3DGBA, GPLv3). Pure C. Spec: docs/phase33-romgen/SPEC-S3.md sections 1.5, 2.2-2.4, 6 (S3.5). */
#ifndef RG_RWORLD_H
#define RG_RWORLD_H

#include "rg_rprep.h"
#include "rg_rtables.h"

#define RG_SEAM_WEIGHT 16            /* per cell of seam (rel:795) */
#define RG_HARD_WEIGHT 1e6           /* ground walked from one to the other (rel:796) */
#define RG_LOOSE_WEIGHT 0.01         /* a block nothing places (rel:797) */
#define RG_GS_ITER 4000              /* Gauss-Seidel iteration cap (rel:851) */
#define RG_GS_DELTA 0.001            /* stop when the largest change is below this (rel:870) */
#define RG_GROUND_SPREAD 0.05        /* a group is trusted when at most this share of its ground is a massif (rel:762) */
#define RG_MASSIF 160                /* pixels (rel:763) */
#define RG_FOOTPRINT 0.6             /* share of a cell drawn as top (or ground) that makes it a footprint (rel:591) */
#define RG_HALF_LOG_PX 1.0           /* R2 log: a pre-rounding level within this many pixels of a half level */
#define RG_LEVELS_MAX_BROKEN 512u

/* CPython >= 3.12 sum() is Neumaier-compensated, 3.11 naive (SPEC-S2 A1, spec Q4 = compensated). */
typedef struct RgWorldOpts { bool naiveSum; } RgWorldOpts;
RgWorldOpts rg_world_opts_default(void);        /* naiveSum = !RG_PYSUM_COMPENSATED */

/* One pre-rounding level that lies within RG_HALF_LOG_PX of a half level (R2). kind 0: terrace of `group`, `idx` =
 * region; kind 1: a group's fallback level (idx = -1); kind 2: plain map `idx` = layout id. */
typedef struct RgNearHalf { uint8_t kind; uint16_t group; int32_t idx; double value, distPx; } RgNearHalf;
/* A seam given up, counted in cells: names are (kind 0 group / kind 1 layout) as upstream's Counter key. */
typedef struct RgBroken { uint8_t kindA, kindB; uint16_t idA, idB; unsigned cells; } RgBroken;

typedef struct RgLevels {
    unsigned nGroups;
    uint8_t ok[RG_DRAWN_MAX_GROUPS];            /* drawn_ok: the group survived (the world_levels()["regions"] membership) */
    uint8_t dropped[RG_DRAWN_MAX_GROUPS];       /* removed by the GROUND_SPREAD rule (not the excluded one) */
    double quality[RG_DRAWN_MAX_GROUPS];        /* ground-spread share, last measured */
    unsigned nLevel[RG_DRAWN_MAX_GROUPS];
    int32_t *level[RG_DRAWN_MAX_GROUPS];        /* absolute level (pixels, multiples of 16) of each region; NULL if !ok */
    int32_t base[512];                          /* layout id -> pixels of a map not drawn (plain map) */
    uint8_t hasBase[512];
    unsigned nBroken;
    RgBroken broken[RG_LEVELS_MAX_BROKEN];      /* Counter insertion order */
    unsigned brokenCells;                       /* seam cells given up */
    unsigned nNear;
    RgNearHalf *near_;                          /* the R2 log of the final solve (value within RG_HALF_LOG_PX of a half level) */
    double minHalfDistPx;                       /* the smallest distance seen over every rounded value */
    unsigned solves, nodes, samples, seamsDropped;  /* diagnostics of the final solve; solves = ground-spread rounds */
} RgLevels;

/* canvas + drawn_prepare for every group that is not DRAWN_EXCLUDED, then trimmed to what world_levels reads (sizes,
 * big, runs, ties, stats, edges, member list). preps has d->nGroups entries; the excluded group's stays zeroed. */
RgErr rg_world_prep_all(RgRCtx *c, const RgDrawn *d, RgPrep *preps);
void rg_world_prep_free(RgPrep *preps, unsigned n);

/* world_levels (rel:959-1086): the levels of every surviving group's regions and the bases of plain maps. */
RgErr rg_world_levels(const RgWorld *w, const RgDrawn *d, const RgPrep *preps, const RgWorldOpts *o, RgLevels *out);
void rg_levels_free(RgLevels *lv);

/* ---- the solver primitives, exposed so the host test can check them on synthetic graphs (SPEC-S3 O3) ---- */

/* One sample per entry: pair (a, b) means h[a] - h[b] = d with weight w (wi = 1 when upstream's weight is a Python int).
 * Entries with the same (a, b) form one key's list, keys in first-appearance order. */
typedef struct RgWlSample { int32_t a, b; double d, w; uint8_t wi; } RgWlSample;
/* sum() of weights with CPython's typing (ints exact until the first float, then Neumaier / plain adds). */
double rg_wl_wsum(const double *w, const uint8_t *wi, unsigned n, bool naive);
/* _robust over `nSamples` entries, the nodes with fx[] held at fv[]; `ties` (in set order) are one level. Returns n
 * doubles (free()) or NULL. */
double *rg_wl_robust(unsigned n, const RgWlSample *s, unsigned nSamples, const uint8_t *fx, const double *fv,
                     const RgTie *ties, unsigned nTies, bool naive);
/* _give_up_seams: the offsets with the worst disagreeing pairs given up one at a time; *dropped counts them. */
double *rg_wl_give_up(unsigned n, const RgWlSample *s, unsigned nSamples, const uint8_t *fx, const double *fv, bool naive,
                      unsigned *dropped);
/* _gauss_seidel over single-sample pairs (no median, no reduction). */
double *rg_wl_gauss(unsigned n, const RgWlSample *s, unsigned nSamples, const uint8_t *fx, const double *fv, bool naive);

#endif
