/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/voxel_cells.py (Layout.role_at
 * and its helpers), MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
/* rg_roles.h -- the per-cell role of every layout (3DGBA, GPLv3). Pure C.
 * Spec: docs/phase33-romgen/SPEC-S0-S1.md section 3. Roles are voxel_regions.h's VOXEL_ROLE_*. */
#ifndef RG_ROLES_H
#define RG_ROLES_H

#include "rg_art.h"
#include "rg_world.h"

typedef struct RgRoles {
    uint8_t *data;               /* one byte per cell of every present layout, concatenated */
    uint32_t *off;               /* [id-1] byte offset of a layout's roles in data */
    uint16_t layoutCount;
    uint32_t total;
    uint64_t *posts;             /* sorted (tilesetAddr << 16 | metatile): cells post_metatiles */
    size_t postCount;
} RgRoles;

/* Allocates the role buffer and computes post_metatiles (cells:372-394). */
RgErr rg_roles_init(const RgWorld *w, RgRoles *r);
void  rg_roles_free(RgRoles *r);
const uint8_t *rg_roles_of(const RgRoles *r, uint16_t layoutId);   /* NULL when not present */

/* Fills the roles of layout L (w*h bytes, row major) using the open pair of L. Returns false on
 * allocation failure. */
bool rg_roles_layout(const RgWorld *w, const RgRoles *r, RgPair *pair, const RgLayout *L, uint8_t *out);

/* Every present layout, grouped by pair (one pair's art open at a time), progress per layout.
 * `reverse` walks the pairs backwards (the output must not depend on it: determinism test). */
RgErr rg_roles_all(const RgWorld *w, RgRoles *r, bool reverse, RgProgressFn progress, void *ctx);

#endif
