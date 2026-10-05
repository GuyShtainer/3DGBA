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

/* ---- S2.1 additions (SPEC-S2 section 1.1, gaps G1/G2/G4): full pixels, for building art ---- */

/* G1: one metatile layer as the GBA draws it, every pixel. c = BGR555, idx = palette index (0..15).
 * drawn bit = the pixel is opaque for this layer: lower layer always 1 (an idx-0 pixel carries palette
 * slot 0's colour, voxel_art Tilesets.subtile); upper layer idx != 0. A tile number past the tile data
 * is magenta (31,0,31) with idx 0. An unreadable metatile is all magenta idx 0 (lower) / undrawn (upper). */
typedef struct RgCellPx {
    uint16_t c[16][16];
    uint8_t idx[16][16];
    uint16_t drawn[16];
} RgCellPx;
void rg_cell_px(RgPair *p, uint16_t metatile, int layer, RgCellPx *out);

/* G2: one 8x8 subtile by (tile, palette), no flips, row major (voxel_art Tilesets.subtile). Past the tile
 * data: magenta with idx 0. */
void rg_subtile_px(RgPair *p, uint16_t tile, uint8_t pal, uint16_t c[64], uint8_t idx[64]);

/* G4: BGR555 channel -> 8 bit the way voxel_art does (c5*255//31), for the specs' hex colour constants. */
static inline uint8_t rg_c5_to_8(unsigned c5) { return (uint8_t)(c5 * 255u / 31u); }

#endif
