/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/gen_voxel_sign_masks.py and
 * voxel_sign_mask.py, MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
/* rg_signs.h -- signpost pixel masks and the VXS2 signposts.bin serialiser (3DGBA, GPLv3). Pure C.
 * Spec: docs/phase33-romgen/SPEC-S0-S1.md section 5. */
#ifndef RG_SIGNS_H
#define RG_SIGNS_H

#include "rg_roles.h"

typedef struct RgSignRec {
    uint16_t layout, x, y;       /* 1-based layout id, layout-local cell */
    uint16_t rows[16];           /* sign mask, row 0 = top, bit 0 = left */
    uint16_t head[16];           /* lantern mask in the cell north; all 0 for none */
    uint16_t headGround;         /* metatile id (0 when head is all 0) */
} RgSignRec;

typedef struct RgSignList {
    RgSignRec *rec;
    size_t count, cap;
} RgSignList;

/* Pixels not reachable from the 16x16 border through empty ones (smask:28-44). solid and out are 16 row bitmasks. */
void rg_cutout_mask(const uint16_t solid[16], uint16_t out[16]);
/* smask:47-52: the sign's silhouette in cell (x, y) of L. */
void rg_metatile_mask(RgPair *p, const RgLayout *L, int x, int y, uint16_t rows[16]);
/* smask:55-87: the lantern drawn in `north`'s upper layer, or all zero. */
void rg_head_mask(RgPair *p, const uint16_t signRows[16], uint16_t north, uint16_t out[16]);
/* signs:22-44: the metatile the head cell's ground is drawn with, for head cell (hx, hy). */
uint16_t rg_head_ground(RgPair *p, const RgLayout *L, int hx, int hy);

/* Appends a record for every SIGNPOST cell of L (outdoor layouts only). roles = L's role bytes. */
bool rg_signs_layout(RgPair *p, const RgLayout *L, const uint8_t *roles, RgSignList *list);
/* Sorts by (layout, y, x). */
void rg_signs_finish(RgSignList *list);
void rg_signs_free(RgSignList *list);
unsigned rg_signs_with_head(const RgSignList *list);

/* VXS2 file: "VXS2", u32 count, 72 bytes per record. Returns the size; 0 when it does not fit or
 * there are no records or more than 65535 (the consumer rejects those). out == NULL sizes it. */
size_t rg_signs_write(const RgSignList *list, uint8_t *out, size_t cap);

#endif
