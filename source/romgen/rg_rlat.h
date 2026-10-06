/* rg_rlat.h -- the relief lattice types (3DGBA, GPLv3). Pure C, header only.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/gen_voxel_relief.py (PER_CELL, STEP,
 * flat_lattice, cell_grid), MIT License - see source/voxel/NOTICE.md. Portions Copyright (c) Dust Zallax, MIT.
 * Modified for 3DGBA (GPLv3), 2026. Spec: docs/phase33-romgen/SPEC-S3.md section 1.1. */
#ifndef RG_RLAT_H
#define RG_RLAT_H

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

#define RG_P 4                   /* PER_CELL: lattice steps per cell (rel:156) */
#define RG_SIDE 5                /* RG_P + 1: points per cell side, the file's `side` */
#define RG_UNSET NAN             /* upstream None in solve_drawn's lattice; S3.1 never produces it */

/* (W*4+1) x (H*4+1) points, row major, malloc'd by rg_lat_new. */
typedef struct RgLat { int w, h; double *v; } RgLat;
/* cell_grid (rel:2600-2602): the 5x5 points of one cell, [j][i]. */
typedef struct RgGrid { double g[RG_SIDE][RG_SIDE]; } RgGrid;

/* rel:2540 flat_lattice: all 0.0, for a layout of w x h cells. False on a bad size or no memory. */
static inline bool rg_lat_new(RgLat *L, int w, int h)
{
    assert(L != NULL);
    if (w <= 0 || h <= 0)
        return false;
    L->w = w;
    L->h = h;
    L->v = (double *)calloc((size_t)(w * RG_P + 1) * (size_t)(h * RG_P + 1), sizeof(double));
    return L->v != NULL;
}

static inline void rg_lat_free(RgLat *L)
{
    assert(L != NULL);
    free(L->v);
    L->v = NULL;
}

/* Point (i, j) of the lattice: i in 0..w*4, j in 0..h*4. Asserts bounds. */
static inline double *rg_lat_at(RgLat *L, int i, int j)
{
    assert(L != NULL && L->v != NULL);
    assert(i >= 0 && i <= L->w * RG_P && j >= 0 && j <= L->h * RG_P);
    return &L->v[(size_t)j * (size_t)(L->w * RG_P + 1) + (size_t)i];
}

static inline void rg_cell_grid(const RgLat *L, int x, int y, RgGrid *out)
{
    int i, j;

    assert(L != NULL && out != NULL && x >= 0 && y >= 0 && x < L->w && y < L->h);
    for (j = 0; j < RG_SIDE; j++)
        for (i = 0; i < RG_SIDE; i++)
            out->g[j][i] = L->v[(size_t)(y * RG_P + j) * (size_t)(L->w * RG_P + 1) + (size_t)(x * RG_P + i)];
}

/* Python list equality: exact ==. */
static inline bool rg_grid_equal(const RgGrid *a, const RgGrid *b)
{
    int i, j;

    for (j = 0; j < RG_SIDE; j++)
        for (i = 0; i < RG_SIDE; i++)
            if (a->g[j][i] != b->g[j][i])
                return false;
    return true;
}

#endif
