/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/gen_voxel_relief.py (ledge_cells,
 * ledge_berms, ledge_layouts, JUMPS/LIP constants), MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
/* rg_ledge.h -- ledge cells and their berms on a flat lattice (3DGBA, GPLv3). Pure C.
 * Spec: docs/phase33-romgen/SPEC-S3.md sections 1.5, 6 (S3.1). */
#ifndef RG_LEDGE_H
#define RG_LEDGE_H

#include "rg_art.h"
#include "rg_rlat.h"
#include "rg_world.h"

#define RG_LIP 6                 /* height of the lip's top edge, pixels (rel:184) */
#define RG_LIP_WIDTH 8           /* the lip is drawn half a cell deep (rel:185) */
#define RG_LIP_BACK 8            /* the ground behind the lip rises to it over this many pixels (rel:186) */

/* One ledge cell: its jump directions (1 or 2 from the behaviour; a junction takes the neighbours' union, <= 4). */
typedef struct RgLedgeCell {
    int16_t x, y;
    uint8_t nDirs;
    int8_t dx[4], dy[4];
} RgLedgeCell;

/* rel:2333-2364 ledge_cells, in upstream's dict order: the jump cells row-major, then the junction cells in the
 * order they were first joined. `at` maps (y*w + x) to the index in c[], or -1. */
typedef struct RgLedgeSet {
    RgLedgeCell *c;
    unsigned n, nJump;           /* nJump: how many of c[] are jump cells (the rest are junctions) */
    int32_t *at;
    int w, h;
} RgLedgeSet;

/* The two layouts upstream's ENABLED names (A.1): they skip the junction rule (rel:2353). */
bool rg_ledge_enabled(uint16_t layoutId);

/* junctions=false: only the cells whose behaviour is a jump. False on no memory. Free with rg_ledge_set_free. */
bool rg_ledge_cells(const RgLayout *L, bool junctions, RgLedgeSet *out);
void rg_ledge_set_free(RgLedgeSet *s);

/* rel:2565-2577 ledge_layouts: ids ascending of every present layout that rg_relief_outdoor names and that has a
 * jump behaviour cell (no junctions). Writes up to cap ids; returns the total. */
unsigned rg_ledge_layouts(const RgWorld *w, uint16_t *out, unsigned cap);

/* rel:2367-2464 ledge_berms: adds every ledge's lip to the lattice h (a layout's own lattice). `pair` is the open
 * tileset pair of L. Returns the ledge cell count, or -1 on no memory. */
int rg_ledge_berms(const RgLayout *L, RgPair *pair, RgLat *h);

#endif
