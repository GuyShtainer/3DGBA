/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/gen_voxel_relief.py (export,
 * rel:3683-3808), MIT License - see source/voxel/NOTICE.md. Portions Copyright (c) Dust Zallax, MIT. */
#include "rg_relief_write.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define ROW_BYTES 14u
#define VARIANT_BYTES 36u
#define CUT_BYTES 14u

static void put16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, v & 0xFFFFu);
    put16(p + 2, v >> 16);
}

/* Python's sorted() over the 9-tuple (layout, x, y, variant, foot, behind, wall, sides, flags); foot is signed. */
static int cut_cmp(const void *a, const void *b)
{
    const RgCut *p = (const RgCut *)a, *q = (const RgCut *)b;

#define CMP(f) do { if (p->f != q->f) return p->f < q->f ? -1 : 1; } while (0)
    CMP(layout); CMP(x); CMP(y); CMP(variant); CMP(foot); CMP(behind); CMP(wall); CMP(sides); CMP(flags);
#undef CMP
    return 0;
}

static RgErr validate(const RgReliefRow *rows, unsigned nRows, unsigned nV, unsigned nCuts)
{
    unsigned i;

    if (nRows > 0xFFFFu || nV > 0xFFFFu || nCuts > 0xFFFFu)
        return RG_ERR_RELIEF;
    for (i = 0; i < nRows; i++) {
        const RgReliefRow *r = &rows[i];

        if (r->nCells > 0xFFFFu || r->w > 255u || (r->hFlags & 0x3FFFu) > 255u || (i > 0 && r->id <= rows[i - 1].id))
            return RG_ERR_RELIEF;
        if (r->nCells > 0 && r->cells == NULL)
            return RG_ERR_RELIEF;
    }
    return RG_OK;
}

RgErr rg_relief_write(const RgReliefRow *rows, unsigned nRows, const RgVariant *v, unsigned nV, RgCut *cuts,
                      unsigned nCuts, uint8_t *out, size_t cap, size_t *size)
{
    uint64_t cellBytes = 0, total, cutOff;
    unsigned i, k;
    uint8_t *p;
    RgErr e;

    assert(size != NULL);
    e = validate(rows, nRows, nV, nCuts);
    if (e != RG_OK)
        return e;
    assert((nRows == 0 || rows != NULL) && (nV == 0 || v != NULL) && (nCuts == 0 || cuts != NULL));
    for (i = 0; i < nRows; i++)
        cellBytes += (uint64_t)rows[i].nCells * RG_RELIEF_CELL_BYTES;
    cutOff = 8u + (uint64_t)ROW_BYTES * nRows + cellBytes;
    total = cutOff + 4u + (uint64_t)VARIANT_BYTES * nV + (uint64_t)CUT_BYTES * nCuts + 8u;
    if (cutOff > 0xFFFFFFFFu || total > 0xFFFFFFFFu)
        return RG_ERR_RELIEF;
    *size = (size_t)total;
    if (out == NULL)
        return RG_OK;
    if (cap < (size_t)total)
        return RG_ERR_TOO_BIG;
    p = out;
    memcpy(p, "VXL4", 4);
    put16(p + 4, nRows);
    put16(p + 6, 5);
    p += 8;
    cellBytes = 8u + (uint64_t)ROW_BYTES * nRows;       /* now the running cell offset */
    for (i = 0; i < nRows; i++, p += ROW_BYTES) {
        put16(p, rows[i].id);
        put16(p + 2, rows[i].nCells);
        put16(p + 4, rows[i].w);
        put16(p + 6, rows[i].hFlags);
        put32(p + 8, (uint32_t)cellBytes);
        put16(p + 12, (uint16_t)rows[i].base);
        cellBytes += (uint64_t)rows[i].nCells * RG_RELIEF_CELL_BYTES;
    }
    for (i = 0; i < nRows; i++) {
        size_t n = (size_t)rows[i].nCells * RG_RELIEF_CELL_BYTES;

        if (n > 0)
            memcpy(p, rows[i].cells, n);
        p += n;
    }
    assert((uint64_t)(p - out) == cutOff);
    if (nCuts > 1)
        qsort(cuts, nCuts, sizeof(RgCut), cut_cmp);
    put16(p, nV);
    put16(p + 2, nCuts);
    p += 4;
    for (i = 0; i < nV; i++, p += VARIANT_BYTES) {
        put16(p, v[i].firstLayout);
        put16(p + 2, v[i].metatile);
        for (k = 0; k < 16; k++)
            put16(p + 4 + 2 * k, v[i].mask[k]);
    }
    for (i = 0; i < nCuts; i++, p += CUT_BYTES) {
        put16(p, cuts[i].layout);
        p[2] = cuts[i].x;
        p[3] = cuts[i].y;
        put16(p + 4, cuts[i].variant);
        put16(p + 6, (uint16_t)cuts[i].foot);
        put16(p + 8, cuts[i].behind);
        put16(p + 10, cuts[i].wall);
        p[12] = cuts[i].sides;
        p[13] = cuts[i].flags;
    }
    put32(p, (uint32_t)cutOff);
    memcpy(p + 4, "CUTS", 4);
    return RG_OK;
}
