/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/voxel_building.py
 * (Raster, ortho_check, density_check), MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
/* rg_bcheck.h -- the software raster and the two model checks (3DGBA, GPLv3). Pure C. SPEC-S2 section 1.4, 5. */
#ifndef RG_BCHECK_H
#define RG_BCHECK_H

#include "rg_geom.h"

/* The depth buffer is double, as Python's: a float one would merge near-ties Python keeps apart. */
typedef struct RgRaster { int w, h; uint8_t *rgb; double *depth; int16_t *owner; } RgRaster;

bool rg_raster_init(RgRaster *r, int w, int h);          /* bg (0,0,0), depth -1e30, owner -1 */
void rg_raster_free(RgRaster *r);
/* vs[k] = sx, sy, depth, invw, u, v (u, v already multiplied by invw as upstream). Larger depth is nearer; on a
 * tie the first triangle wins. A pixel is written when the texel's alpha >= 128; colour is (int)(c * shade). */
void rg_raster_draw(RgRaster *r, const double vs[3][6], const RgImage *tex, double shade, int16_t owner);

/* An art rectangle the model must reproduce pixel for pixel; `behind`: geometry may show where the art has ground. */
typedef struct RgExact { int16_t x0, y0, x1, y1; bool behind; } RgExact;
typedef struct RgOrthoResult { unsigned wrong, missing, extra; } RgOrthoResult;

/* vb:1595-1662. `exact` NULL (nExact 0) = the whole art. `reference` NULL = art. False on no memory. */
bool rg_ortho_check(const RgMesh *m, const RgImage *art, const RgExact *exact, unsigned nExact,
                    const RgImage *reference, RgOrthoResult *out);

typedef struct RgDensityBad { char tag[RG_TAG_LEN]; double along, down, shear; } RgDensityBad;
/* vb:1665-1716. Returns how many triangles broke the property; the first maxBad are stored. `art` is unused
 * (kept for the spec's signature). */
unsigned rg_density_check(const RgMesh *m, const RgImage *art, RgDensityBad *bad, unsigned maxBad);

/* Side closure (Phase 34, Kanto table only). Seen from due west or due east, every prism end the camera can see
 * (rg_prism_exposed: the prism's (z, y) cross-section, eroded by 0.75 px at its border, minus what a prism reaching
 * further out hides) must be a solid wall: each such cell has to be covered by some triangle projected along x. Prism
 * side faces and edge-on faces project to lines and cover nothing, so a missing end face (a prism without a cap band,
 * or ends=false on the outermost slice) leaves its section open. A hip roof or a chamfered block (no prism) counts
 * its own outline (under its upper edge, from its lowest point) when it reaches the model's edge. `applicable` is false
 * when nothing on that side is a wall to judge. */
typedef struct RgSideResult { bool applicable; unsigned expected, open; } RgSideResult;
/* `parts` is the builder's part list, `m` its emitted mesh. False on no memory. Fails (pass) when
 * open <= RG_SIDE_TOL(expected). */
bool rg_side_check(const RgPartList *parts, const RgMesh *m, bool east, RgSideResult *out);
#define RG_SIDE_TOL(expected) ((expected) / 50u > 8u ? (expected) / 50u : 8u)

#endif
