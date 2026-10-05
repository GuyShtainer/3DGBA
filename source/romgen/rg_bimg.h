/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/voxel_building.py
 * (LayoutArt.building_art, LayoutArt.cell_image) and gen_voxel_buildings.py (the PIL image helpers),
 * MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
/* rg_bimg.h -- RGBA images for the building generator (3DGBA, GPLv3). Pure C. SPEC-S2 section 1.2. */
#ifndef RG_BIMG_H
#define RG_BIMG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rg_art.h"

typedef struct RgImage {
    int w, h;
    uint8_t *px;                 /* RGBA8, row major, malloc'd */
} RgImage;

/* Zeroed = (0,0,0,0). False on a bad size or no memory. */
bool rg_img_new(RgImage *im, int w, int h);
void rg_img_free(RgImage *im);
/* PIL paste without a mask: copy RGBA. Parts outside dst are clipped. */
void rg_img_paste(RgImage *dst, const RgImage *src, int x, int y);
/* alpha != 0, half-open [x0,y0,x1,y1); false = None (no such pixel). Every alpha in these images is 0 or 255
 * and every transparent pixel is (0,0,0,0), so old (any channel) and new (alpha only) Pillow agree. */
bool rg_img_bbox(const RgImage *im, int bb[4]);
bool rg_img_crop(const RgImage *im, const int bb[4], RgImage *out);
/* FNV-1a of w, h, px. Equality of two images = equal hash + memcmp. */
uint64_t rg_img_hash(const RgImage *im);
bool rg_img_equal(const RgImage *a, const RgImage *b);

/* RGBA of a BGR555 colour the way voxel_art does it: each channel c5*255//31, alpha as given. */
void rg_c5_rgba(uint16_t c, uint8_t a, uint8_t out[4]);

/* "rrggbb" (the specs' colour constants) -> BGR555 by the exact reverse of c5*255//31. False when a channel has
 * no 5-bit preimage or the string is not six hex digits. */
bool rg_hex_to_c5(const char *hex, uint16_t *c5);

/* vb:198-216 cell_image: a whole metatile as the ground renderer draws it, opaque over black, alpha 255
 * everywhere (upper layer where idx != 0, else the lower layer's colour). Allocates a 16x16 image. */
bool rg_cell_image(RgPair *p, uint16_t metatile, RgImage *out16);

/* vb:161-196 building_art: RGBA image (cw*16 x ch*16) of the cells (x, y, cw, ch) of L, every ground pixel
 * transparent. A lower-layer 8x8 block equal (as 64 colours) to one of the ground metatiles' lower blocks is
 * ground whole; when `owned` (cw*ch flags, row major) is given, only owned cells are drawn and a lower pixel equal
 * to a ground metatile's lower pixel at the same place is ground too (upstream ground_px). With upperOnly only the
 * upper layer is drawn. False on a bad argument, an off-map cell or no memory. */
bool rg_building_art(const RgWorld *w, RgPair *p, const RgLayout *L, int x, int y, int cw, int ch,
                     const uint16_t *groundTiles, unsigned nGround, const uint8_t *owned,
                     bool upperOnly, RgImage *out);

#endif
