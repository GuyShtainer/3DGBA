// vx_fixture.h -- shared synthetic game state for the phase-32 voxel host suites (test_voxel_*.c).
// Builds, in plain heap buffers, a fake 8 MiB "ROM" holding hand-made map data at the real BPEE
// table addresses and a fake EWRAM/IWRAM/palette/VRAM holding a field state, then takes a VxSnapshot
// of it. No game data: every byte is invented here. Header-only, included by one .c per suite.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "vx_adapter.h"
#include "vx_snapshot.h"

#define FX_ROM_SIZE 0x800000u
#define FX_NUM_CELLS_W 20
#define FX_NUM_CELLS_H 20

static uint8_t *fxRom;
static uint8_t *fxEwram, *fxIwram, *fxPltt, *fxVram;
static VxSnapshot *fxSnap;
static size_t fxLz1Len; /* packed length of the compressed tileset's tiles */

static void P16(uint8_t *b, uint32_t off, uint32_t v) { b[off] = (uint8_t)v; b[off + 1] = (uint8_t)(v >> 8); }
static void P32(uint8_t *b, uint32_t off, uint32_t v) { P16(b, off, v & 0xFFFF); P16(b, off + 2, v >> 16); }
#define ROMOFF(a) ((a) - 0x08000000u)
static void R32(uint32_t addr, uint32_t v) { P32(fxRom, ROMOFF(addr), v); }
static void R16(uint32_t addr, uint32_t v) { P16(fxRom, ROMOFF(addr), v); }
static void E32(uint32_t addr, uint32_t v) { P32(fxEwram, addr - 0x02000000u, v); }
static void E16(uint32_t addr, uint32_t v) { P16(fxEwram, addr - 0x02000000u, v); }
static void E8(uint32_t addr, uint32_t v) { fxEwram[addr - 0x02000000u] = (uint8_t)v; }
static void I32(uint32_t addr, uint32_t v) { P32(fxIwram, addr - 0x03000000u, v); }

/* Fixture ROM addresses (data region from 0x08600000). */
enum
{
    FX_TS_GENERAL = 0x083DF704, FX_TS_FORTREE = 0x083DF7C4, FX_TS_BUILDING = 0x083DFB6C,
    FX_GROUP0 = 0x08600000,     /* u32 headers[2] */
    FX_HDR_A = 0x08600100, FX_HDR_B = 0x08600140,
    FX_LAYOUT_A = 0x08600200, FX_LAYOUT_B = 0x08600220,
    FX_EVENTS_A = 0x08600300, FX_OBJTPL = 0x08600340, FX_BGEV = 0x08600400,
    FX_CONNS_A = 0x08600440, FX_CONNLIST = 0x08600450,
    FX_GFXINFO0 = 0x08600500, FX_IMAGES0 = 0x08600540, FX_IMGDATA0 = 0x08600600,
    FX_BORDER = 0x08600800, FX_MAP_A = 0x08601000, FX_MAP_B = 0x08602000,
    FX_TILESET_DATA = 0x08610000 /* 3 x 0x8000 */
};

static uint32_t TsData(int i) { return FX_TILESET_DATA + 0x8000u * (uint32_t)i; }

static void BuildTileset(uint32_t addr, int idx, int compressed, int secondary)
{
    uint32_t d = TsData(idx);
    uint8_t *t = fxRom + ROMOFF(d);

    R32(addr + 0, (uint32_t)(compressed | (secondary << 8)));
    R32(addr + 4, d);
    R32(addr + 8, d + 0x7400);
    R32(addr + 12, d + 0x5000);
    R32(addr + 16, d + 0x7000);
    if (compressed)
    {   /* an all-literal GBA LZ77 stream of 16384 bytes */
        size_t o = 4;
        t[0] = 0x10; t[1] = 0x00; t[2] = 0x40; t[3] = 0x00;
        for (unsigned i = 0; i < 16384; i += 8)
        {
            t[o++] = 0;
            for (unsigned k = 0; k < 8; ++k) t[o++] = (uint8_t)((i + k) * 3 + idx);
        }
        fxLz1Len = o;
    }
    else
        for (unsigned i = 0; i < 16384; ++i) t[i] = (uint8_t)(i * 5 + idx);
    for (unsigned m = 0; m < 512; ++m)
    {
        for (unsigned q = 0; q < 8; ++q) P16(fxRom, ROMOFF(d + 0x5000 + m * 16 + q * 2), (m * 8 + q) & 0x3FF);
        P16(fxRom, ROMOFF(d + 0x7000 + m * 2), 0);
    }
    for (unsigned c = 0; c < 256; ++c) P16(fxRom, ROMOFF(d + 0x7400 + c * 2), c * 17 & 0x7FFF);
}

static void BuildLayout(uint32_t addr, int w, int h, uint32_t map, uint32_t prim, uint32_t sec)
{
    R32(addr + 0, (uint32_t)w); R32(addr + 4, (uint32_t)h);
    R32(addr + 8, FX_BORDER); R32(addr + 12, map);
    R32(addr + 16, prim); R32(addr + 20, sec);
    for (int i = 0; i < w * h; ++i) R16(map + 2u * (uint32_t)i, ((uint32_t)(i % 97) & 0x3FF) | 0x3000u);
}

static void BuildHeader(uint32_t addr, uint32_t layout, uint32_t events, uint32_t conns, int layoutId, int weather, int type)
{
    R32(addr + 0, layout); R32(addr + 4, events); R32(addr + 12, conns);
    R16(addr + 0x12, (uint32_t)layoutId);
    fxRom[ROMOFF(addr) + 0x14] = 0x3A; fxRom[ROMOFF(addr) + 0x16] = (uint8_t)weather; fxRom[ROMOFF(addr) + 0x17] = (uint8_t)type;
}

static void BuildRom(void)
{
    fxRom = calloc(1, FX_ROM_SIZE);
    BuildTileset(FX_TS_GENERAL, 0, 0, 0);
    BuildTileset(FX_TS_FORTREE, 1, 1, 1);
    BuildTileset(FX_TS_BUILDING, 2, 0, 1);
    BuildLayout(FX_LAYOUT_A, FX_NUM_CELLS_W, FX_NUM_CELLS_H, FX_MAP_A, FX_TS_GENERAL, FX_TS_FORTREE);
    BuildLayout(FX_LAYOUT_B, 16, 16, FX_MAP_B, FX_TS_GENERAL, FX_TS_BUILDING);
    for (int i = 0; i < 4; ++i) R16(FX_BORDER + 2u * (uint32_t)i, 0x0001);
    R32(0x08481DD4 + 0, FX_LAYOUT_A);
    R32(0x08481DD4 + 4, FX_LAYOUT_B);
    R32(0x08486578 + 0, FX_GROUP0);
    R32(FX_GROUP0 + 0, FX_HDR_A);
    R32(FX_GROUP0 + 4, FX_HDR_B);
    /* events: 2 object templates (graphics ids 0x14 and 0x22), 1 bg event */
    fxRom[ROMOFF(FX_EVENTS_A) + 0] = 2; fxRom[ROMOFF(FX_EVENTS_A) + 3] = 1;
    R32(FX_EVENTS_A + 4, FX_OBJTPL); R32(FX_EVENTS_A + 0x10, FX_BGEV);
    fxRom[ROMOFF(FX_OBJTPL) + 1] = 0x14; fxRom[ROMOFF(FX_OBJTPL) + 0x18 + 1] = 0x22;
    R16(FX_BGEV + 0, 5); R16(FX_BGEV + 2, 9); fxRom[ROMOFF(FX_BGEV) + 4] = 3; fxRom[ROMOFF(FX_BGEV) + 5] = 1;
    /* connections: north (2) to B at offset 0, east (4) to B at offset -2 */
    R32(FX_CONNS_A + 0, 2); R32(FX_CONNS_A + 4, FX_CONNLIST);
    fxRom[ROMOFF(FX_CONNLIST) + 0] = 2; R32(FX_CONNLIST + 4, 0);
    fxRom[ROMOFF(FX_CONNLIST) + 8] = 0; fxRom[ROMOFF(FX_CONNLIST) + 9] = 1;
    fxRom[ROMOFF(FX_CONNLIST) + 12] = 4; R32(FX_CONNLIST + 16, (uint32_t)-2);
    fxRom[ROMOFF(FX_CONNLIST) + 20] = 0; fxRom[ROMOFF(FX_CONNLIST) + 21] = 1;
    BuildHeader(FX_HDR_A, FX_LAYOUT_A, FX_EVENTS_A, FX_CONNS_A, 1, 2, 3);
    BuildHeader(FX_HDR_B, FX_LAYOUT_B, 0, 0, 2, 0, 3);
    /* graphics info: every id points at one 0x24-byte record: size 512, 16x32, one image {data, 256} */
    for (unsigned i = 0; i < 239; ++i) R32(0x08505620 + 4 * i, FX_GFXINFO0);
    R16(FX_GFXINFO0 + 6, 512); R16(FX_GFXINFO0 + 8, 16); R16(FX_GFXINFO0 + 10, 32);
    R32(FX_GFXINFO0 + 0x1C, FX_IMAGES0);
    R32(FX_IMAGES0 + 0, FX_IMGDATA0); R16(FX_IMAGES0 + 4, 256);
    for (unsigned i = 0; i < 512; ++i) fxRom[ROMOFF(FX_IMGDATA0) + i] = (uint8_t)(i ^ 0x5A);
    for (unsigned i = 0; i < 37; ++i) R32(0x085059F8 + 4 * i, 0x08700000u + 0x40u * i);
}

/* Field state: the player at map tile (5,6) on map A (backup dims 35x34), sprite 5 a weather sprite. */
static void BuildRam(void)
{
    fxEwram = calloc(1, 0x40000); fxIwram = calloc(1, 0x8000);
    fxPltt = calloc(1, 0x400); fxVram = calloc(1, 0x18000);
    I32(0x030022C4, 0x08085E5D);                         /* gMain.callback2 = CB2_Overworld */
    I32(0x03005D8C, 0x02025A58);                         /* gSaveBlock1Ptr */
    E16(0x02025A58, 5); E16(0x02025A5A, 6); E8(0x02025A5C, 0); E8(0x02025A5D, 0);
    I32(0x03005DC0, FX_NUM_CELLS_W + 15); I32(0x03005DC4, FX_NUM_CELLS_H + 14); I32(0x03005DC8, 0x02032318);
    for (int i = 0; i < (FX_NUM_CELLS_W + 15) * (FX_NUM_CELLS_H + 14); ++i) E16(0x02032318u + 2u * (uint32_t)i, (uint32_t)(i % 89) | 0x3000u);
    memcpy(fxEwram + (0x02037318u - 0x02000000u), fxRom + ROMOFF(FX_HDR_A), 28);
    /* player object 0 */
    E8(0x02037350, 0x01); E8(0x02037352, 0x01); E8(0x02037354, 0); E8(0x02037355, 0x59);
    E8(0x0203735B, 3); E16(0x02037360, 12); E16(0x02037362, 13); E16(0x02037364, 12); E16(0x02037366, 13);
    E8(0x02037368, 1);
    /* a second object: an NPC */
    E8(0x02037350 + 0x24, 0x01); E8(0x02037350 + 0x24 + 5, 0x14); E16(0x02037350 + 0x24 + 0x10, 15); E16(0x02037350 + 0x24 + 0x12, 9);
    E8(0x02037590 + 0, 0x21); E8(0x02037590 + 4, 0); E8(0x02037590 + 5, 0);
    /* sprite 0: the player's, RAM template; sprite 5: a rain weather sprite (ROM template) */
    E8(0x02020630 + 0x3E, 1); E32(0x02020630 + 0x14, 0x03007DB0);
    E8(0x02020630 + 5 * 0x44 + 0x3E, 1); E32(0x02020630 + 5 * 0x44 + 0x14, 0x0854FC2C);
    /* OAM words of sprite 0: y 0x50, 16x32 vertical shape, tile 0x123, prio 2, pal 7, x 0x1AB, matrix 9, size 2 */
    E16(0x02020630, 0x0050 | (2u << 14)); E16(0x02020632, 0x1AB | (9u << 9) | (2u << 14));
    E16(0x02020634, 0x123 | (2u << 10) | (7u << 12));
    E16(0x02020630 + 0x20, 120); E16(0x02020630 + 0x22, 80);
    /* weather block bytes at the measured offsets: rain, idle, EVA 9, fogs off */
    E8(0x02038454 + 0x6D0, 3); E8(0x02038454 + 0x6C6, 3); E8(0x02038454 + 0x730, 9);
    /* palette fade: y = 5, active */
    E16(0x02037FD4 + 4, 5u << 6); E16(0x02037FD4 + 6, 0x8000);
    for (int i = 0; i < 512; ++i) { E16(0x02037714u + 2u * (uint32_t)i, (uint32_t)i); P16(fxPltt, 2u * (uint32_t)i, (uint32_t)i ^ 0x7FFF); }
    for (unsigned i = 0; i < 0x18000; ++i) fxVram[i] = (uint8_t)(i * 7);
}

static VxMemSrc fxSrc(void)
{
    VxMemSrc s = {fxEwram, fxIwram, fxPltt, fxVram, 0x1040, 0x3F44, 0x0810, 0};
    return s;
}

static int fxInit(void)
{
    BuildRom();
    BuildRam();
    fxSnap = calloc(1, sizeof(*fxSnap));
    vx_adapter_set_rom(fxRom, FX_ROM_SIZE);
    VxMemSrc s = fxSrc();
    return vx_snapshot_take(fxSnap, &s) ? 0 : -1;
}
