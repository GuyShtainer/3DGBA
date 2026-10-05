/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/gen_voxel_regions.py,
 * MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
/* rg_regions.h -- the VXR5 regions.bin serialiser (3DGBA, GPLv3). Pure C. SPEC-S0-S1 section 4. */
#ifndef RG_REGIONS_H
#define RG_REGIONS_H

#include "rg_roles.h"

/* Writes regions.bin into out (capacity cap) and returns its size; 0 when it does not fit or there
 * is nothing to write. out == NULL returns the size without writing. */
size_t rg_regions_write(const RgWorld *w, const RgRoles *r, uint8_t *out, size_t cap);

#endif
