/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/gen_voxel_relief.py (export,
 * rel:3683-3808), MIT License - see source/voxel/NOTICE.md. Portions Copyright (c) Dust Zallax, MIT. */
/* rg_relief_write.h -- the relief.bin (VXL4) serialiser (3DGBA, GPLv3). Pure C.
 * Spec: docs/phase33-romgen/SPEC-S3.md section 4. Layout, little endian:
 *   "VXL4", u16 rows, u16 side(5); rows x 14 (u16 id, u16 cells, u16 w, u16 h|flags, u32 offset, s16 base);
 *   cells x 27 (u8 x, u8 y, 25 x s8); u16 variants, u16 cuts; variants x 36; cuts x 14; u32 cutTableOffset, "CUTS". */
#ifndef RG_RELIEF_WRITE_H
#define RG_RELIEF_WRITE_H

#include <stddef.h>
#include <stdint.h>

#include "rg_world.h"

#define RG_RELIEF_CELL_BYTES 27u

typedef struct RgReliefRow {
    uint16_t id, w, hFlags;      /* hFlags: height | 0x8000 drawn | 0x4000 unit 2 */
    int16_t base;
    uint32_t nCells;
    const uint8_t *cells;        /* nCells x 27 B: x, y, 25 heights; sorted (y, x) */
} RgReliefRow;

typedef struct RgCut {
    uint16_t layout;
    uint8_t x, y;
    uint16_t variant;            /* 0xFFFF none */
    int16_t foot;
    uint16_t behind, wall;       /* 0xFFFF none; wall 0xFFFE no face */
    uint8_t sides, flags;        /* sides 0xFF none */
} RgCut;

typedef struct RgVariant {
    uint16_t firstLayout, metatile, mask[16];
} RgVariant;

/* Rows must already be in strictly ascending id. Cuts are sorted here (full-tuple order, foot signed). out == NULL
 * (cap ignored): size only. RG_ERR_RELIEF when a field would overflow or a row is malformed; RG_ERR_TOO_BIG when
 * cap is too small for a non-NULL out. *size gets the byte count on success. */
RgErr rg_relief_write(const RgReliefRow *rows, unsigned nRows, const RgVariant *v, unsigned nV, RgCut *cuts,
                      unsigned nCuts, uint8_t *out, size_t cap, size_t *size);

/* PC-fallback guard (S3.8). `head` = the first `n` bytes of an existing relief.bin. Returns the number of drawn rows
 * (bit 15 of the row heights), or -1 when the head is not a VXL4 file with its whole row table. */
int rg_relief_head_drawn_rows(const uint8_t *head, size_t n);

/* 1 when a generator that would write `newDrawnRows` drawn rows must leave the existing file alone: the existing head
 * is a FULL file (drawnRows > 0) and the new output is ledges-only (newDrawnRows == 0). An absent (NULL), malformed or
 * ledges-only existing file is replaced. */
int rg_relief_keep_existing(const uint8_t *head, size_t n, unsigned newDrawnRows);

#endif
