// rg_fixture.h -- synthetic mini-ROM builder for the phase-33 romgen host suites (test_romgen_*.c).
// Builds, in a heap buffer, a fake BPEE ROM holding hand-made layouts, tilesets, map headers and events
// at the real table addresses (gMapLayouts, gMapGroups). No game data: every byte is invented here.
// Layout id 1 is a 1x1 filler (NULL tilesets) that fills the 34 groups; tests add layouts from id 2.
// Header-only, included by one .c per suite.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "gba_game.h"

#define FXR_ROM_SIZE 0x490000u
#define FXR_MAX_MAPS 256

typedef struct {
    uint8_t *rom;
    uint32_t bump;                 /* next free ROM offset */
    unsigned nLayouts;
    unsigned nMaps;
    uint8_t mapGroup[FXR_MAX_MAPS];
    uint32_t mapHdr[FXR_MAX_MAPS];
} RgFx;

static void fxr_p16(RgFx *f, uint32_t off, uint32_t v) { f->rom[off] = (uint8_t)v; f->rom[off + 1] = (uint8_t)(v >> 8); }
static void fxr_p32(RgFx *f, uint32_t off, uint32_t v) { fxr_p16(f, off, v & 0xFFFF); fxr_p16(f, off + 2, v >> 16); }
#define FXR_ADDR(off) (0x08000000u + (off))
#define FXR_OFF(addr) ((addr) - 0x08000000u)

static uint32_t fxr_alloc(RgFx *f, uint32_t n)
{
    uint32_t o = (f->bump + 3u) & ~3u;
    f->bump = o + n;
    if (f->bump >= 0x480000u) abort();
    return o;
}

/* LZ77 type 0x10, all literals (flag bytes 0): valid, uncompressed-in-effect. Returns packed length. */
static uint32_t fxr_lz77_literal(uint8_t *dst, const uint8_t *src, uint32_t n)
{
    uint32_t o = 4, i = 0;
    dst[0] = 0x10; dst[1] = (uint8_t)n; dst[2] = (uint8_t)(n >> 8); dst[3] = (uint8_t)(n >> 16);
    while (i < n) {
        uint32_t k;
        dst[o++] = 0;
        for (k = 0; k < 8 && i < n; k++) dst[o++] = src[i++];
    }
    return o;
}

/* Sets the table slot of layout `id` (1-based) and the sentinel after the table. */
static void fxr_table_set(RgFx *f, unsigned id, uint32_t addr)
{
    fxr_p32(f, FXR_OFF(GBA_ADDR_MAP_LAYOUTS) + 4u * (id - 1u), addr);
    fxr_p32(f, FXR_OFF(GBA_ADDR_MAP_LAYOUTS) + 4u * id, 0xFFFFFFFFu);
}

/* A tileset. tiles: tileBytes of 4bpp (compressed if `compressed`); pal: 16*16 u16 (or NULL = zeros);
 * mt: count*8 u16; attr: count u16. adjacent: attrs follow metatiles directly (count recoverable);
 * else a 4-byte gap is left (count falls back to 512). Returns the GBA address. */
static uint32_t fxr_tileset(RgFx *f, int compressed, int secondary, const uint8_t *tiles, uint32_t tileBytes,
                            const uint16_t *pal, const uint16_t *mt, const uint16_t *attr, unsigned count,
                            int adjacent)
{
    uint32_t ts = fxr_alloc(f, 24), t, p, m, a, i;
    t = fxr_alloc(f, tileBytes * 2u + 64u);
    if (compressed) (void)fxr_lz77_literal(f->rom + t, tiles, tileBytes);
    else memcpy(f->rom + t, tiles, tileBytes);
    p = fxr_alloc(f, 512);
    if (pal) for (i = 0; i < 256; i++) fxr_p16(f, p + 2 * i, pal[i]);
    m = fxr_alloc(f, count * 16u + (adjacent ? 0u : 4u) + count * 2u + 1024u);   /* slack so the 512 fallback is in range */
    for (i = 0; i < count * 8u; i++) fxr_p16(f, m + 2 * i, mt[i]);
    a = m + count * 16u + (adjacent ? 0u : 4u);
    for (i = 0; i < count; i++) fxr_p16(f, a + 2 * i, attr[i]);
    f->rom[ts] = (uint8_t)compressed;
    f->rom[ts + 1] = (uint8_t)secondary;
    fxr_p32(f, ts + 4, FXR_ADDR(t));
    fxr_p32(f, ts + 8, FXR_ADDR(p));
    fxr_p32(f, ts + 12, FXR_ADDR(m));
    fxr_p32(f, ts + 16, FXR_ADDR(a));
    return FXR_ADDR(ts);
}

/* Appends a layout (next id). blocks: w*h u16. Returns the id. */
static unsigned fxr_layout(RgFx *f, int w, int h, const uint16_t *blocks, uint32_t prim, uint32_t sec)
{
    uint32_t s = fxr_alloc(f, 24), b = fxr_alloc(f, 16), d = fxr_alloc(f, (uint32_t)(w * h) * 2u);
    int i;
    for (i = 0; i < w * h; i++) fxr_p16(f, d + 2u * (uint32_t)i, blocks ? blocks[i] : 0);
    fxr_p32(f, s, (uint32_t)w); fxr_p32(f, s + 4, (uint32_t)h);
    fxr_p32(f, s + 8, FXR_ADDR(b)); fxr_p32(f, s + 12, FXR_ADDR(d));
    fxr_p32(f, s + 16, prim); fxr_p32(f, s + 20, sec);
    f->nLayouts++;
    fxr_table_set(f, f->nLayouts, FXR_ADDR(s));
    return f->nLayouts;
}

static unsigned fxr_layout_null(RgFx *f)
{
    f->nLayouts++;
    fxr_table_set(f, f->nLayouts, 0);
    return f->nLayouts;
}

static void fxr_init(RgFx *f)
{
    memset(f, 0, sizeof(*f));
    f->rom = (uint8_t *)calloc(1, FXR_ROM_SIZE);
    if (!f->rom) abort();
    memcpy(f->rom + 0xAC, "BPEE", 4);
    f->bump = 0x1000;
    (void)fxr_layout(f, 1, 1, NULL, 0, 0);   /* id 1: the filler layout */
}

/* A map in `group` with `layoutId`. warps: n*(x,y) s16; bgs: n*(x,y,kind). Returns the header's ROM
 * offset (tests may poke it). */
static uint32_t fxr_map(RgFx *f, unsigned group, unsigned mapType, unsigned layoutId,
                        const int16_t (*warps)[2], unsigned nw, const uint16_t (*bgs)[3], unsigned nb)
{
    uint32_t h = fxr_alloc(f, GBA_MAP_HEADER_BYTES), i;
    uint32_t lay = layoutId ? (uint32_t)f->rom[0x481DD4 + 4 * (layoutId - 1)] | ((uint32_t)f->rom[0x481DD4 + 4 * (layoutId - 1) + 1] << 8)
                  | ((uint32_t)f->rom[0x481DD4 + 4 * (layoutId - 1) + 2] << 16) | ((uint32_t)f->rom[0x481DD4 + 4 * (layoutId - 1) + 3] << 24) : 0;
    fxr_p32(f, h, lay);
    fxr_p16(f, h + GBA_OFF_MH_LAYOUT_ID, layoutId);
    f->rom[h + GBA_OFF_MH_MAPTYPE] = (uint8_t)mapType;
    if (nw || nb) {
        uint32_t ev = fxr_alloc(f, 0x14), wp = 0, bp = 0;
        if (nw) {
            wp = fxr_alloc(f, nw * 8u);
            for (i = 0; i < nw; i++) { fxr_p16(f, wp + 8 * i, (uint16_t)warps[i][0]); fxr_p16(f, wp + 8 * i + 2, (uint16_t)warps[i][1]); }
        }
        if (nb) {
            bp = fxr_alloc(f, nb * 12u);
            for (i = 0; i < nb; i++) { fxr_p16(f, bp + 12 * i, bgs[i][0]); fxr_p16(f, bp + 12 * i + 2, bgs[i][1]); f->rom[bp + 12 * i + 5] = (uint8_t)bgs[i][2]; }
        }
        f->rom[ev + 1] = (uint8_t)nw; f->rom[ev + 3] = (uint8_t)nb;
        if (nw) fxr_p32(f, ev + 8, FXR_ADDR(wp));
        if (nb) fxr_p32(f, ev + 0x10, FXR_ADDR(bp));
        fxr_p32(f, h + GBA_OFF_MH_EVENTS, FXR_ADDR(ev));
    }
    f->mapGroup[f->nMaps] = (uint8_t)group;
    f->mapHdr[f->nMaps] = FXR_ADDR(h);
    f->nMaps++;
    return h;
}

/* conns: n*(dir, offset, group, num) */
static void fxr_conns(RgFx *f, uint32_t hdrOff, const int32_t (*c)[4], unsigned n)
{
    uint32_t blk = fxr_alloc(f, 8), l = fxr_alloc(f, n * 12u), i;
    fxr_p32(f, blk, n); fxr_p32(f, blk + 4, FXR_ADDR(l));
    for (i = 0; i < n; i++) {
        f->rom[l + 12 * i] = (uint8_t)c[i][0];
        fxr_p32(f, l + 12 * i + 4, (uint32_t)c[i][1]);
        f->rom[l + 12 * i + 8] = (uint8_t)c[i][2];
        f->rom[l + 12 * i + 9] = (uint8_t)c[i][3];
    }
    fxr_p32(f, hdrOff + GBA_OFF_MH_CONNECTIONS, FXR_ADDR(blk));
}

/* Writes the group arrays (back to back, ending at gMapGroups) and the group table. Empty groups get
 * one filler map (layout 1, type 0). */
static void fxr_finish(RgFx *f)
{
    unsigned g, i, total = 0, counts[34];
    uint32_t pos;
    uint32_t filler;

    for (g = 0; g < 34; g++) {
        counts[g] = 0;
        for (i = 0; i < f->nMaps; i++) counts[g] += f->mapGroup[i] == g;
    }
    for (g = 0; g < 34; g++)
        if (counts[g] == 0) {
            (void)fxr_map(f, g, 0, 1, NULL, 0, NULL, 0);
            counts[g] = 1;
        }
    for (g = 0; g < 34; g++) total += counts[g];
    filler = 0; (void)filler;
    pos = FXR_OFF(GBA_ADDR_MAP_GROUPS) - 4u * total;
    for (g = 0; g < 34; g++) {
        unsigned k = 0;
        fxr_p32(f, FXR_OFF(GBA_ADDR_MAP_GROUPS) + 4u * g, FXR_ADDR(pos));
        for (i = 0; i < f->nMaps; i++)
            if (f->mapGroup[i] == g) { fxr_p32(f, pos + 4u * k, f->mapHdr[i]); k++; }
        pos += 4u * counts[g];
    }
}

