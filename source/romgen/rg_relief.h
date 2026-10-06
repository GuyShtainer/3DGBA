/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/gen_voxel_relief.py (layout_heights,
 * relief_cells, cell_grid, export), MIT License - see source/voxel/NOTICE.md. Portions Copyright (c) Dust Zallax, MIT. */
/* rg_relief.h -- relief.bin orchestration (3DGBA, GPLv3). Pure C.
 * Spec: docs/phase33-romgen/SPEC-S3.md section 1.6. S3.1 builds the LEDGES product only. */
#ifndef RG_RELIEF_H
#define RG_RELIEF_H

#include "rg_roles.h"
#include "rg_world.h"

typedef enum { RG_RELIEF_OFF = 0, RG_RELIEF_LEDGES = 1, RG_RELIEF_FULL = 2 } RgReliefMode;

typedef struct RgReliefStats {
    unsigned rows, cells, ledgeLayouts, ledgeCells, drawnRows;     /* drawnRows = bit-15 rows */
    unsigned seeds, groups, groupsOk, excluded, spreadDropped;     /* find_drawn / world_levels (S3.3+) */
    unsigned variants, cuts, seamCellsGivenUp, nearHalfLevel;      /* S3.5+ */
    uint32_t maxCanvasPx;                                          /* largest group canvas, pixels (S3.4+) */
    double msLedges, msDrawnFind, msCanvas, msWorld, msSolve, msExport;
} RgReliefStats;

/* rel:2600-2615 relief_cells: the cells of a lattice with a point above 0.25 in magnitude, as 27-byte records
 * (x, y, 25 stored heights = clamp(-128, 127, nearbyint(v / unit))), sorted (y, x). Returns the cell count; *out is
 * malloc'd (NULL when 0) and freed by the caller. -1 on no memory. */
#include "rg_rlat.h"
int rg_relief_cells(const RgLat *lat, int unit, uint8_t **out);

/* Builds relief.bin into a malloc'd blob. roles may be NULL in LEDGES mode (FULL reads it). progress =
 * layouts done of total. FULL (S3.7) needs roles. */
RgErr rg_relief_build(const RgWorld *w, const RgRoles *r, RgReliefMode mode, RgProgressFn progress, void *ctx,
                      const volatile int *cancel, uint8_t **out, size_t *outSize, RgReliefStats *st);

#endif
