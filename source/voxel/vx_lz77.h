/* vx_lz77.h -- GBA BIOS LZ77 (type 0x10) decoder and packed-length scanner (3DGBA, GPLv3). */
#ifndef VX_LZ77_H
#define VX_LZ77_H

#include <stddef.h>
#include <stdint.h>

/* Decodes the stream at src (at most srcAvail readable bytes) into dst (cap bytes).
 * Returns the decoded size, or 0 on any error: bad header, decoded size 0 or over cap, a
 * back-reference before the start or a truncated stream. Never reads or writes out of range. */
size_t vx_lz77_decode(const uint8_t *src, size_t srcAvail, uint8_t *dst, size_t cap);

/* Walks the stream without producing output. Returns the packed length (bytes consumed, padding
 * excluded) and stores the decoded size in *decodedSize when it is non-NULL. 0 on error. */
size_t vx_lz77_scan(const uint8_t *src, size_t srcAvail, size_t *decodedSize);

#endif
