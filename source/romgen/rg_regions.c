/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/gen_voxel_regions.py,
 * MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
#include "rg_regions.h"

#include <string.h>

#define VXR_MAX_BYTES (2u * 1024u * 1024u)   /* the consumer's limit (voxel_regions.c) */

static void put16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, v & 0xFFFFu);
    put16(p + 2, v >> 16);
}

size_t rg_regions_write(const RgWorld *w, const RgRoles *r, uint8_t *out, size_t cap)
{
    unsigned li, count = 0;
    size_t cells = 0, size, rowAt = 8, dataAt;

    if (w == NULL || r == NULL || r->data == NULL)
        return 0;
    for (li = 0; li < w->layoutCount; li++) {
        if (!w->layouts[li].present)
            continue;
        count++;
        cells += (size_t)w->layouts[li].w * w->layouts[li].h;
    }
    size = 8u + 12u * (size_t)count + cells;
    if (count == 0 || count > 65535u || size > VXR_MAX_BYTES)
        return 0;
    if (out == NULL)
        return size;
    if (cap < size)
        return 0;
    memcpy(out, "VXR5", 4);
    put16(out + 4, count);
    put16(out + 6, 0);
    dataAt = 8u + 12u * (size_t)count;
    for (li = 0; li < w->layoutCount; li++) {
        const RgLayout *L = &w->layouts[li];
        size_t n = (size_t)L->w * L->h;

        if (!L->present)
            continue;
        put16(out + rowAt, L->id);
        put16(out + rowAt + 2, L->w);
        put16(out + rowAt + 4, L->h);
        put16(out + rowAt + 6, 0);
        put32(out + rowAt + 8, (uint32_t)dataAt);
        memcpy(out + dataAt, r->data + r->off[li], n);
        rowAt += 12;
        dataAt += n;
    }
    return size;
}
