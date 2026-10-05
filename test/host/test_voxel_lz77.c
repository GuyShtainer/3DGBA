// test_voxel_lz77.c -- host test for source/voxel/vx_lz77.c (phase 32 P2, SPEC-port section 9.2).
//
//   clang -std=c11 -Wall -Wextra -O2 -I source/voxel test/host/test_voxel_lz77.c \
//         source/voxel/vx_lz77.c -o /tmp/tvlz && /tmp/tvlz
//
// Our decoder against a reference ENCODER written here: round-trip on random and repetitive data
// (0..64 KiB), scan length = encoder output length, hostile streams rejected without leaving the
// buffers (the guard-page style check: every call gets exactly-sized heap buffers under ASan when
// built with -fsanitize=address).
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vx_lz77.h"

static int sChecks, sFails;
#define CHECK(c) do { ++sChecks; if (!(c)) { ++sFails; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static uint32_t sSeed = 12345u;
static uint32_t Rnd(void) { sSeed = sSeed * 1664525u + 1013904223u; return sSeed >> 8; }

/* Reference encoder: greedy longest match in a 4096 window, min match 3, max 18. Returns the length
 * of the stream before the 4-byte padding. */
static size_t Encode(const uint8_t *in, size_t n, uint8_t *out)
{
    size_t ip = 0, op = 4;

    out[0] = 0x10; out[1] = (uint8_t)n; out[2] = (uint8_t)(n >> 8); out[3] = (uint8_t)(n >> 16);
    while (ip < n)
    {
        size_t flagPos = op++;
        uint8_t flags = 0;

        for (int bit = 0; bit < 8 && ip < n; ++bit)
        {
            size_t bestLen = 0, bestDist = 0;

            for (size_t d = 1; d <= 4096 && d <= ip; ++d)
            {
                size_t l = 0;
                while (l < 18 && ip + l < n && in[ip + l - d] == in[ip + l]) ++l;
                if (l > bestLen) { bestLen = l; bestDist = d; }
            }
            if (bestLen >= 3)
            {
                flags |= (uint8_t)(0x80u >> bit);
                out[op++] = (uint8_t)(((bestLen - 3) << 4) | ((bestDist - 1) >> 8));
                out[op++] = (uint8_t)((bestDist - 1) & 0xFF);
                ip += bestLen;
            }
            else
                out[op++] = in[ip++];
        }
        out[flagPos] = flags;
    }
    return op;
}

static void RoundTrip(size_t n, int mode)
{
    uint8_t *raw = malloc(n ? n : 1), *enc = malloc(n * 2 + 64), *dec = malloc(n ? n : 1);
    size_t el, got, dsz = 0;

    for (size_t i = 0; i < n; ++i)
        raw[i] = mode == 0 ? (uint8_t)Rnd() : mode == 1 ? (uint8_t)(i % 7) : (uint8_t)((i / 50) & 3);
    el = Encode(raw, n, enc);
    got = vx_lz77_decode(enc, el, dec, n);
    CHECK(n == 0 ? got == 0 : (got == n && memcmp(raw, dec, n) == 0));
    if (n)
    {
        CHECK(vx_lz77_scan(enc, el, &dsz) == el && dsz == n);
        /* exact-size source: the walker must not need the padding */
        uint8_t *tight = malloc(el);
        memcpy(tight, enc, el);
        CHECK(vx_lz77_decode(tight, el, dec, n) == n);
        free(tight);
    }
    free(raw); free(enc); free(dec);
}

int main(void)
{
    static const size_t sizes[] = {1, 2, 3, 7, 8, 9, 17, 18, 19, 255, 256, 4095, 4096, 4097, 20000, 65536};

    for (unsigned s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s)
        for (int m = 0; m < 3; ++m)
            RoundTrip(sizes[s], m);
    RoundTrip(0, 0);

    {   /* hostile streams */
        uint8_t dec[64];
        const uint8_t badType[] = {0x20, 4, 0, 0, 0, 1, 2, 3, 4};
        const uint8_t zeroLen[] = {0x10, 0, 0, 0};
        const uint8_t backRefFirst[] = {0x10, 4, 0, 0, 0x80, 0x00, 0x00};   /* reference at output start */
        const uint8_t trunc[] = {0x10, 8, 0, 0, 0x00, 1, 2, 3};            /* 8 literals promised, 3 given */
        const uint8_t big[] = {0x10, 0xFF, 0xFF, 0xFF, 0x00, 1};           /* 16 MiB claimed, cap 64 */
        const uint8_t truncRef[] = {0x10, 8, 0, 0, 0x40, 1, 0x10};         /* 2nd block: ref missing 2nd byte */

        CHECK(vx_lz77_decode(badType, sizeof(badType), dec, sizeof(dec)) == 0);
        CHECK(vx_lz77_decode(zeroLen, sizeof(zeroLen), dec, sizeof(dec)) == 0);
        CHECK(vx_lz77_decode(backRefFirst, sizeof(backRefFirst), dec, sizeof(dec)) == 0);
        CHECK(vx_lz77_decode(trunc, sizeof(trunc), dec, sizeof(dec)) == 0);
        CHECK(vx_lz77_decode(big, sizeof(big), dec, sizeof(dec)) == 0);
        CHECK(vx_lz77_decode(truncRef, sizeof(truncRef), dec, sizeof(dec)) == 0);
        CHECK(vx_lz77_decode(NULL, 0, dec, sizeof(dec)) == 0);
        CHECK(vx_lz77_decode(badType, 3, dec, sizeof(dec)) == 0);
        CHECK(vx_lz77_scan(trunc, sizeof(trunc), NULL) == 0);
        CHECK(vx_lz77_scan(big, sizeof(big), NULL) != 1u); /* may be rejected or counted; must not crash */
        /* decoded size larger than the destination cap */
        uint8_t src[64], enc[256];
        for (int i = 0; i < 64; ++i) src[i] = (uint8_t)(i & 3);
        size_t el = Encode(src, 64, enc);
        CHECK(vx_lz77_decode(enc, el, dec, 32) == 0);
        /* every truncation of a valid stream is rejected, never a crash */
        for (size_t cut = 0; cut < el; ++cut)
            CHECK(vx_lz77_decode(enc, cut, dec, sizeof(dec)) == 0);
    }
    printf("test_voxel_lz77: %d checks, %d failures\n", sChecks, sFails);
    return sFails != 0;
}
