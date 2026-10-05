/* vx_lz77.c -- our own GBA LZ77 (type 0x10) decoder (3DGBA, GPLv3).
 *
 * Format (public BIOS documentation): u32 header, type 0x10 in the low byte and the decoded size
 * in the upper 24 bits; then groups of one flag byte and up to eight items, MSB first. Flag 0 =
 * one literal byte. Flag 1 = two bytes b1,b2: length (b1 >> 4) + 3, distance ((b1 & 15) << 8 |
 * b2) + 1 bytes back from the write position. */
#include "vx_lz77.h"

#define LZ_TYPE 0x10u
#define LZ_MAX_SIZE 0x1000000u

/* Shared walker: dst == NULL scans only. Returns consumed bytes or 0. */
static size_t Walk(const uint8_t *src, size_t srcAvail, uint8_t *dst, size_t cap, size_t *outSize)
{
    size_t size, pos = 4, written = 0;
    unsigned flags = 0, bits = 0;

    if (src == NULL || srcAvail < 4 || src[0] != LZ_TYPE)
        return 0;
    size = (size_t)src[1] | ((size_t)src[2] << 8) | ((size_t)src[3] << 16);
    if (size == 0 || size >= LZ_MAX_SIZE || (dst != NULL && size > cap))
        return 0;
    while (written < size)
    {
        if (bits == 0)
        {
            if (pos >= srcAvail)
                return 0;
            flags = src[pos++];
            bits = 8;
        }
        if ((flags & 0x80u) == 0)
        {
            if (pos >= srcAvail)
                return 0;
            if (dst != NULL)
                dst[written] = src[pos];
            ++pos;
            ++written;
        }
        else
        {
            size_t len, dist, k;

            if (srcAvail - pos < 2)
                return 0;
            len = (size_t)(src[pos] >> 4) + 3u;
            dist = ((size_t)(src[pos] & 15u) << 8 | (size_t)src[pos + 1]) + 1u;
            pos += 2;
            if (dist > written)
                return 0;
            for (k = 0; k < len && written < size; ++k, ++written)
                if (dst != NULL)
                    dst[written] = dst[written - dist];
        }
        flags = (flags << 1) & 0xFFu;
        --bits;
    }
    if (outSize != NULL)
        *outSize = size;
    return pos;
}

size_t vx_lz77_decode(const uint8_t *src, size_t srcAvail, uint8_t *dst, size_t cap)
{
    size_t size = 0;

    if (dst == NULL)
        return 0;
    if (Walk(src, srcAvail, dst, cap, &size) == 0)
        return 0;
    return size;
}

size_t vx_lz77_scan(const uint8_t *src, size_t srcAvail, size_t *decodedSize)
{
    return Walk(src, srcAvail, NULL, 0, decodedSize);
}
