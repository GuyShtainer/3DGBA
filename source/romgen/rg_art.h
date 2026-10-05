/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/voxel_art.py and the
 * art questions of voxel_cells.py, MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
/* rg_art.h -- a tileset pair's pixels and the per-metatile drawing features (3DGBA, GPLv3). Pure C.
 * Spec: docs/phase33-romgen/SPEC-S0-S1.md sections 1.4, 1.6, 3.2. */
#ifndef RG_ART_H
#define RG_ART_H

#include <stdbool.h>
#include <stdint.h>

#include "rg_world.h"

typedef struct {
    uint16_t drawn[16];          /* bit x of row y = pixel (x,y) drawn (palette index != 0) */
    uint16_t c[16][16];          /* BGR555 & 0x7FFF, valid where drawn */
    uint16_t count;              /* number of drawn pixels */
} RgLayer;

typedef struct RgPair RgPair;    /* decoded tiles of both tilesets + per-metatile memo */

/* Decodes both tile blocks. NULL on failure. */
RgPair *rg_pair_open(const RgWorld *w, uint16_t pairIndex);
void    rg_pair_close(RgPair *p);

/* One layer (0 = lower, 1 = upper) of a metatile, lazily built and memoised. Never NULL: an
 * unreadable metatile gives the shared empty layer. */
const RgLayer *rg_layer(RgPair *p, uint16_t metatile, int layer);
/* Layer 1 drawn over layer 0 (cells:210-211). */
void rg_merged(RgPair *p, uint16_t metatile, RgLayer *out);

/* Derived, memoised per metatile. foliage(m) >= 0.5 is `drawn > 0 && 2*green >= drawn`. */
bool rg_foliage_ge_half(RgPair *p, uint16_t metatile);
bool rg_treads(RgPair *p, uint16_t metatile);
bool rg_covers(RgPair *p, uint16_t metatile);

/* The treads rule on a merged drawing; reports the two measures (0 when it fails the 240-pixel fill). */
bool rg_treads_measure(const RgLayer *merged, double *across, double *down);
/* Same drawn pixels and same colours (dict equality of {(x,y): rgb}). */
bool rg_layer_equal(const RgLayer *a, const RgLayer *b);
static inline bool rg_layer_any(const RgLayer *a) { return a->count != 0; }
static inline bool rg_layer_has(const RgLayer *a, int x, int y) { return (a->drawn[y] >> x) & 1u; }

#endif
