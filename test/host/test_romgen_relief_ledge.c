// test_romgen_relief_ledge.c -- host test for phase 33 S3.1 (SPEC-S3 section 6): ledge cells, berms on a flat lattice,
// the VXL4 writer, rg_relief_build (LEDGES), rg_run's relief output and the consumer round trip (O1 I1-I4, O4).
// Synthetic checks always run; the real-ROM checks run with ROMGEN_ROM=/path/emerald.gba (exported, else SKIP).
//
//   clang -std=c11 -Wall -Wextra -O2 -ffp-contract=off -fsanitize=address,undefined -DVOXEL_HOST_FILES \
//         -DCTR_VOXEL_LIGHTING=1 -I source/romgen -I source/voxel -I test/host test/host/test_romgen_relief_ledge.c \
//         source/romgen/rg_*.c \
//         source/voxel/voxel_world.c source/voxel/voxel_regions.c source/voxel/voxel_relief.c \
//         source/voxel/voxel_building.c source/voxel/voxel_sign.c source/voxel/voxel_arena.c \
//         source/voxel/voxel_atlas.c source/voxel/voxel_mesh_builder.c source/voxel/voxel_tree.c \
//         source/voxel/voxel_lighting.c source/voxel/voxel_grade.c source/voxel/voxel_entities.c \
//         source/voxel/voxel_camera.c source/voxel/vx_adapter.c source/voxel/vx_snapshot.c \
//         source/voxel/vx_lz77.c source/voxel/vx_behavior.c source/voxel/vx_data.c \
//         source/voxel/ctr_shims_pure.c source/voxel/vx_battle_stub.c -lm -o /tmp/trl && /tmp/trl
// (`make -C tools/romgen test` runs every romgen suite with this line.)
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rg_ledge.h"
#include "rg_relief.h"
#include "rg_relief_write.h"
#include "rg_roles.h"
#include "rg_rtables.h"
#include "rg_run.h"
#include "rg_fixture.h"
#include "voxel_relief.h"

static int sChecks, sFails, sSkips;
#define CHECK(c) do { ++sChecks; if (!(c)) { ++sFails; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

/* ---- SHA-1 (test-side, for pinning the Route 101 cell bytes) ---- */
static uint32_t Rol(uint32_t v, unsigned n) { return (v << n) | (v >> (32 - n)); }
static void Sha1(const uint8_t *d, size_t n, char hex[41])
{
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    size_t total = ((n + 8) / 64 + 1) * 64, i;
    uint8_t *m = (uint8_t *)calloc(total, 1);
    unsigned k;

    memcpy(m, d, n);
    m[n] = 0x80;
    for (k = 0; k < 8; k++) m[total - 1 - k] = (uint8_t)(((uint64_t)n * 8u) >> (8 * k));
    for (i = 0; i < total; i += 64) {
        uint32_t w[80], a = h[0], b = h[1], c = h[2], dd = h[3], e = h[4];
        for (k = 0; k < 16; k++) w[k] = ((uint32_t)m[i + 4 * k] << 24) | ((uint32_t)m[i + 4 * k + 1] << 16) | ((uint32_t)m[i + 4 * k + 2] << 8) | m[i + 4 * k + 3];
        for (k = 16; k < 80; k++) w[k] = Rol(w[k - 3] ^ w[k - 8] ^ w[k - 14] ^ w[k - 16], 1);
        for (k = 0; k < 80; k++) {
            uint32_t f, kk, t;
            if (k < 20) { f = (b & c) | (~b & dd); kk = 0x5A827999u; }
            else if (k < 40) { f = b ^ c ^ dd; kk = 0x6ED9EBA1u; }
            else if (k < 60) { f = (b & c) | (b & dd) | (c & dd); kk = 0x8F1BBCDCu; }
            else { f = b ^ c ^ dd; kk = 0xCA62C1D6u; }
            t = Rol(a, 5) + f + e + kk + w[k];
            e = dd; dd = c; c = Rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += dd; h[4] += e;
    }
    for (k = 0; k < 5; k++) snprintf(hex + 8 * k, 9, "%08x", h[k]);
    free(m);
}

static uint32_t U16(const uint8_t *b, size_t o) { return (uint32_t)b[o] | ((uint32_t)b[o + 1] << 8); }
static uint32_t U32(const uint8_t *b, size_t o) { return U16(b, o) | (U16(b, o + 2) << 16); }

/* ---- synthetic world: a 3x3 layout with a row of south ledge cells whose lip is 6 pixels deep ---- */
enum { MT_GRASS = 0, MT_LEDGE_S = 1, MT_LEDGE_E = 2, MT_COUNT = 3 };
static uint8_t sTiles[4 * 32];
static uint16_t sPal[256], sMt[MT_COUNT * 8], sAtt[MT_COUNT];

static void BuildTileset(void)
{
    unsigned r, b;

    memset(sTiles, 0, sizeof(sTiles));
    memset(sTiles + 32, 0x11, 32);                       /* tile 1: all palette index 1 (green) */
    memset(sTiles + 64, 0x22, 32);                       /* tile 2: all index 2 (brown) */
    for (r = 0; r < 8; r++)                              /* tile 3: rows 0-5 brown, rows 6-7 green */
        for (b = 0; b < 4; b++) sTiles[96 + r * 4 + b] = r < 6 ? 0x22 : 0x11;
    sPal[1] = 0x03E0;                                    /* green */
    sPal[2] = 0x1D6B;                                    /* brown */
    sMt[0] = sMt[1] = sMt[2] = sMt[3] = 1;               /* grass: lower layer all tile 1, upper layer empty */
    sMt[8] = sMt[9] = 1; sMt[10] = sMt[11] = 3;          /* ledge: top subtiles green, bottom subtiles the lip */
    sMt[16] = sMt[17] = 1; sMt[18] = sMt[19] = 3;        /* the east-jump twin (same drawing) */
    sAtt[MT_GRASS] = 0; sAtt[MT_LEDGE_S] = 0x3B;         /* MB_JUMP_SOUTH */
    sAtt[MT_LEDGE_E] = 0x38;                             /* MB_JUMP_EAST */
}

static unsigned AddLayout(RgFx *f, uint32_t ts, int w, int h, const uint16_t *blocks)
{
    return fxr_layout(f, w, h, blocks, ts, 0);
}

/* ---- the lattice values the hand computation gives (SPEC-S3 6, S3.1 tests) ----
 * Ledge tile: lip pixels = metatile rows 8..13 (6 deep). A point at pixel row py of cell row 1 (py = 16 + 4j) reads
 * f = lip pixels from py down, b = lip pixels from py-1 up:
 *   j=0 py16: f0 b0, nothing within LIP_BACK (8): 0
 *   j=1 py20: f0 b0, lip starts 4 on, top = 0.75*6 = 4.5, 4.5 * (1 - 4/8) = 2.25
 *   j=2 py24: f6 -> 0.75*6 = 4.5            (half-even store: 4)
 *   j=3 py28: f2 -> 0.75*2 = 1.5            (half-even store: 2)
 *   j=4 py32: f0 b0, nothing below: 0 */
static const double kCol[5] = {0.0, 2.25, 4.5, 1.5, 0.0};

static void TestHalfEven(void)
{
    RgLat L;
    uint8_t *cells = NULL;
    int n, k;
    const double v[8] = {0.5, 1.5, 2.5, -2.5, 3.5, 200.0, -200.0, 0.75};
    const int8_t want[8] = {0, 2, 2, -2, 4, 127, -128, 1};

    CHECK(nearbyint(0.5) == 0.0 && nearbyint(1.5) == 2.0 && nearbyint(-2.5) == -2.0 && nearbyint(2.5) == 2.0);
    CHECK(rg_lat_new(&L, 1, 1));
    for (k = 0; k < 8; k++) *rg_lat_at(&L, k % 5, k / 5) = v[k];
    n = rg_relief_cells(&L, 1, &cells);
    CHECK(n == 1 && cells != NULL);
    for (k = 0; cells != NULL && k < 8; k++) CHECK((int8_t)cells[2 + k] == want[k]);
    free(cells);
    /* the 0.25 threshold: exactly 0.25 is not lifted, anything above is */
    rg_lat_free(&L);
    CHECK(rg_lat_new(&L, 2, 2));
    *rg_lat_at(&L, 2, 2) = 0.25;
    *rg_lat_at(&L, 6, 2) = -0.2501;
    n = rg_relief_cells(&L, 1, &cells);
    CHECK(n == 1 && cells != NULL && cells[0] == 1 && cells[1] == 0);   /* cell (1,0) only; (0,0) has 0.25 */
    free(cells);
    n = rg_relief_cells(&L, 2, &cells);                                   /* unit 2 halves, still > 0.25 */
    CHECK(n == 1);
    free(cells);
    rg_lat_free(&L);
    /* floor division, H16 */
    CHECK(rg_floordiv(-1, 4) == -1 && rg_floordiv(-4, 4) == -1 && rg_floordiv(-5, 4) == -2 && rg_floordiv(7, 4) == 1);
}

static void TestSynthetic(void)
{
    RgFx f;
    RgWorld w;
    uint32_t ts;
    uint16_t blocks[9], blocksJ[9];
    unsigned id, idJ, i;
    RgLedgeSet s;
    RgLat lat;
    RgPair *pair;
    uint8_t *cells = NULL;
    int n, led, x, y;

    BuildTileset();
    fxr_init(&f);
    ts = fxr_tileset(&f, 0, 0, sTiles, sizeof(sTiles), sPal, sMt, sAtt, MT_COUNT, 1);
    for (i = 0; i < 9; i++) blocks[i] = MT_GRASS;
    blocks[3] = blocks[4] = blocks[5] = MT_LEDGE_S;
    id = AddLayout(&f, ts, 3, 3, blocks);
    /* a junction layout: (1,0) jumps south, (0,1) jumps east, (1,1) is blocked grass between them */
    for (i = 0; i < 9; i++) blocksJ[i] = MT_GRASS;
    blocksJ[1] = MT_LEDGE_S;
    blocksJ[3] = MT_LEDGE_E;
    blocksJ[4] = (uint16_t)(MT_GRASS | (1u << 10));
    idJ = AddLayout(&f, ts, 3, 3, blocksJ);
    (void)fxr_map(&f, 0, MAP_TYPE_ROUTE, id, NULL, 0, NULL, 0);
    (void)fxr_map(&f, 0, MAP_TYPE_ROUTE, idJ, NULL, 0, NULL, 0);
    fxr_finish(&f);
    CHECK(rg_world_open(&w, f.rom, FXR_ROM_SIZE) == RG_OK);

    /* ledge_cells: three jump cells, all (0, 1) */
    CHECK(rg_ledge_cells(&w.layouts[id - 1], false, &s));
    CHECK(s.n == 3 && s.nJump == 3);
    for (i = 0; i < s.n; i++) CHECK(s.c[i].y == 1 && s.c[i].x == (int)i && s.c[i].nDirs == 1 && s.c[i].dx[0] == 0 && s.c[i].dy[0] == 1);
    CHECK(s.at[1 * 3 + 1] == 1 && s.at[0] == -1);
    rg_ledge_set_free(&s);

    /* the berm: every point of the three cells; all other lattice points stay 0 */
    CHECK(rg_lat_new(&lat, 3, 3));
    pair = rg_pair_open(&w, w.layouts[id - 1].pairIndex);
    CHECK(pair != NULL);
    led = pair != NULL ? rg_ledge_berms(&w.layouts[id - 1], pair, &lat) : -1;
    CHECK(led == 3);
    for (y = 0; y <= 12; y++)
        for (x = 0; x <= 12; x++) {
            double want = (y >= 4 && y <= 8) ? kCol[y - 4] : 0.0;
            if (*rg_lat_at(&lat, x, y) != want) {
                CHECK(*rg_lat_at(&lat, x, y) == want);
                printf("  lattice (%d,%d) = %g, want %g\n", x, y, *rg_lat_at(&lat, x, y), want);
            }
        }
    CHECK(*rg_lat_at(&lat, 6, 4) == 0.0 && *rg_lat_at(&lat, 6, 5) == 2.25 && *rg_lat_at(&lat, 6, 6) == 4.5 && *rg_lat_at(&lat, 6, 7) == 1.5);
    n = rg_relief_cells(&lat, 1, &cells);
    CHECK(n == 3);
    for (i = 0; cells != NULL && i < 3; i++) {
        static const int8_t want[25] = {0, 0, 0, 0, 0, 2, 2, 2, 2, 2, 4, 4, 4, 4, 4, 2, 2, 2, 2, 2, 0, 0, 0, 0, 0};
        const uint8_t *c = cells + i * RG_RELIEF_CELL_BYTES;
        unsigned k;
        CHECK(c[0] == i && c[1] == 1);
        for (k = 0; k < 25; k++) CHECK((int8_t)c[2 + k] == want[k]);   /* 2.25 -> 2, 4.5 -> 4, 1.5 -> 2 (half-even) */
    }
    free(cells);
    rg_lat_free(&lat);
    if (pair != NULL) rg_pair_close(pair);

    /* junctions: the blocked cell between two ledges at right angles joins, with both directions in join order */
    CHECK(rg_ledge_cells(&w.layouts[idJ - 1], false, &s) && s.n == 2);
    rg_ledge_set_free(&s);
    CHECK(rg_ledge_cells(&w.layouts[idJ - 1], true, &s));
    CHECK(s.n == 3 && s.nJump == 2 && s.c[2].x == 1 && s.c[2].y == 1 && s.c[2].nDirs == 2);
    CHECK(s.c[2].dx[0] == 0 && s.c[2].dy[0] == 1 && s.c[2].dx[1] == 1 && s.c[2].dy[1] == 0);
    rg_ledge_set_free(&s);
    CHECK(rg_is_ledge_junction(&w.layouts[idJ - 1], 1, 1));
    CHECK(!rg_is_ledge_junction(&w.layouts[idJ - 1], 0, 0) && !rg_is_ledge_junction(&w.layouts[id - 1], 1, 1));
    CHECK(!rg_is_ledge_junction(&w.layouts[idJ - 1], -1, 1));      /* off-map is not blocked */
    CHECK(rg_is_enabled_layout(20) && rg_is_enabled_layout(4) && !rg_is_enabled_layout(17));
    /* the fixture is not an Emerald: the tables refuse it */
    {
        const char *why = NULL;
        CHECK(!rg_rtables_check(&w, &why) && why != NULL);
    }
    rg_world_close(&w);
    free(f.rom);
}

/* ---- the writer ---- */
static void TestWriter(void)
{
    uint8_t cellsA[2 * 27], cellsB[27], *buf;
    RgReliefRow rows[2];
    RgVariant v;
    RgCut cuts[3];
    size_t sz = 0, sz2 = 0;
    unsigned i, trailer;

    memset(cellsA, 0, sizeof(cellsA));
    memset(cellsB, 0, sizeof(cellsB));
    cellsA[0] = 1; cellsA[1] = 0; cellsA[27] = 2; cellsA[28] = 0; cellsA[2] = 0x81;
    cellsB[0] = 7; cellsB[1] = 3; cellsB[26] = 0x7F;
    rows[0] = (RgReliefRow){17, 20, 20, 0, 2, cellsA};
    rows[1] = (RgReliefRow){40, 30, 0x8000 | 0x4000 | 12, -7, 1, cellsB};
    memset(&v, 0, sizeof(v));
    v.firstLayout = 4; v.metatile = 9; v.mask[0] = 0xABCD; v.mask[15] = 0x0001;
    cuts[0] = (RgCut){40, 5, 5, 0, 3, 0xFFFF, 0xFFFF, 0xFF, 0};
    cuts[1] = (RgCut){40, 5, 5, 0, -5, 0xFFFF, 0xFFFE, 0xFF, 1};
    cuts[2] = (RgCut){17, 9, 9, 0xFFFF, 0, 5, 6, 3, 2};
    CHECK(rg_relief_write(rows, 2, &v, 1, cuts, 3, NULL, 0, &sz) == RG_OK);
    CHECK(sz == 8 + 28 + 81 + 4 + 36 + 42 + 8);
    buf = (uint8_t *)malloc(sz);
    CHECK(rg_relief_write(rows, 2, &v, 1, cuts, 3, buf, sz, &sz2) == RG_OK && sz2 == sz);
    CHECK(rg_relief_write(rows, 2, &v, 1, cuts, 3, buf, sz - 1, &sz2) == RG_ERR_TOO_BIG);
    CHECK(memcmp(buf, "VXL4", 4) == 0 && U16(buf, 4) == 2 && U16(buf, 6) == 5);
    CHECK(U16(buf, 8) == 17 && U16(buf, 10) == 2 && U16(buf, 12) == 20 && U16(buf, 14) == 20 && U32(buf, 16) == 36 && U16(buf, 20) == 0);
    CHECK(U16(buf, 22) == 40 && U16(buf, 24) == 1 && U16(buf, 28) == (0xC000u | 12u) && U32(buf, 30) == 36u + 54u && U16(buf, 34) == 0xFFF9u);
    CHECK(buf[36 + 2] == 0x81 && buf[36 + 54 + 26] == 0x7F);
    trailer = U32(buf, sz - 8);
    CHECK(trailer == 36u + 81u && memcmp(buf + sz - 4, "CUTS", 4) == 0);
    CHECK(U16(buf, trailer) == 1 && U16(buf, trailer + 2) == 3);
    CHECK(U16(buf, trailer + 4) == 4 && U16(buf, trailer + 6) == 9 && U16(buf, trailer + 8) == 0xABCD && U16(buf, trailer + 4 + 4 + 30) == 1);
    /* cuts sorted as full tuples: layout 17 first, then (40,5,5,0) with foot -5 before foot 3 (signed) */
    {
        const uint8_t *c = buf + trailer + 4 + 36;
        CHECK(U16(c, 0) == 17 && c[2] == 9 && U16(c, 4) == 0xFFFF && U16(c, 8) == 5 && U16(c, 10) == 6 && c[12] == 3 && c[13] == 2);
        CHECK(U16(c, 14) == 40 && U16(c, 14 + 6) == 0xFFFBu && U16(c, 28 + 6) == 3);
    }
    free(buf);
    /* guards: unsorted/duplicate ids, a cell count or size that overflows, a cut/variant count past u16 */
    rows[1].id = 17;
    CHECK(rg_relief_write(rows, 2, NULL, 0, NULL, 0, NULL, 0, &sz) == RG_ERR_RELIEF);
    rows[1].id = 40; rows[1].nCells = 70000;
    CHECK(rg_relief_write(rows, 2, NULL, 0, NULL, 0, NULL, 0, &sz) == RG_ERR_RELIEF);
    rows[1].nCells = 1; rows[1].w = 256;
    CHECK(rg_relief_write(rows, 2, NULL, 0, NULL, 0, NULL, 0, &sz) == RG_ERR_RELIEF);
    CHECK(rg_relief_write(rows, 1, NULL, 0, NULL, 70000, NULL, 0, &sz) == RG_ERR_RELIEF);
    /* an empty file is valid: header + trailer */
    CHECK(rg_relief_write(NULL, 0, NULL, 0, NULL, 0, NULL, 0, &sz) == RG_OK && sz == 8 + 4 + 8);
    (void)i;
}

/* ---- independent decode (the I1-I4 reader) ---- */
typedef struct { unsigned id, cells, w, h, flags; int base; uint32_t off; } Row;

static unsigned Decode(const uint8_t *b, size_t sz, Row *rows, unsigned maxRows, const RgWorld *w)
{
    unsigned n, i, k;
    uint32_t cutOff, end;

    CHECK(sz >= 20 && memcmp(b, "VXL4", 4) == 0 && U16(b, 6) == 5 && memcmp(b + sz - 4, "CUTS", 4) == 0);
    n = U16(b, 4);
    CHECK(n <= maxRows);
    cutOff = U32(b, sz - 8);
    end = 8 + 14u * n;
    for (i = 0; i < n && i < maxRows; i++) {
        const uint8_t *r = b + 8 + 14u * i;
        const RgLayout *L;

        rows[i] = (Row){U16(r, 0), U16(r, 2), U16(r, 4), U16(r, 6) & 0x3FFFu, U16(r, 6) >> 14, (int16_t)U16(r, 12), U32(r, 8)};
        CHECK(i == 0 || rows[i].id > rows[i - 1].id);                       /* I2 ascending */
        CHECK(rg_relief_outdoor((uint16_t)rows[i].id));                     /* I2 outdoor */
        L = &w->layouts[rows[i].id - 1u];
        CHECK(rows[i].w == L->w && rows[i].h == L->h);                      /* I3 */
        CHECK(rows[i].flags == 0 && rows[i].base == 0 && rows[i].cells > 0);   /* I4 */
        CHECK(rows[i].off == end);
        end += 27u * rows[i].cells;
        for (k = 0; k < rows[i].cells; k++) {
            const uint8_t *c = b + rows[i].off + 27u * k;
            unsigned j;
            CHECK(c[0] < rows[i].w && c[1] < rows[i].h);
            CHECK(k == 0 || c[1] > c[-27 + 1] || (c[1] == c[-27 + 1] && c[0] > c[-27]));      /* strictly ascending (y, x) */
            for (j = 0; j < 25; j++) CHECK((int8_t)c[2 + j] >= 0 && (int8_t)c[2 + j] <= 6);      /* I4: 0..LIP */
        }
    }
    CHECK(end == cutOff);                                                   /* I1 */
    CHECK(U16(b, cutOff) == 0 && U16(b, cutOff + 2) == 0);                  /* S3a: empty cut table */
    CHECK(sz == (size_t)cutOff + 4 + 8);
    return n;
}

/* I4 geometry: every written cell is a ledge cell; a lattice point shared with an in-map non-ledge cell is 0. */
static void CheckLedgeGeometry(const uint8_t *b, const Row *rows, unsigned nRows, const RgWorld *w)
{
    unsigned i, k, gp;

    for (i = 0; i < nRows; i++) {
        const RgLayout *L = &w->layouts[rows[i].id - 1u];
        RgLedgeSet s;

        CHECK(rg_ledge_cells(L, true, &s));
        for (k = 0; k < rows[i].cells; k++) {
            const uint8_t *c = b + rows[i].off + 27u * k;
            int cx = c[0], cy = c[1];

            CHECK(s.at[cy * (int)L->w + cx] >= 0);
            for (gp = 0; gp < 25; gp++) {
                int gx = cx * 4 + (int)(gp % 5), gy = cy * 4 + (int)(gp / 5), a, bb;
                bool touchNonLedge = false;

                for (a = 0; a < 2; a++)
                    for (bb = 0; bb < 2; bb++) {
                        int tx = rg_floordiv(gx - 1 + a, 4), ty = rg_floordiv(gy - 1 + bb, 4);
                        if (tx >= 0 && tx < (int)L->w && ty >= 0 && ty < (int)L->h && s.at[ty * (int)L->w + tx] < 0)
                            touchNonLedge = true;
                    }
                if (touchNonLedge) CHECK((int8_t)c[2 + gp] == 0);
            }
        }
        rg_ledge_set_free(&s);
    }
}

/* the consumer: write the file where VoxelRelief_Init reads it, then compare every cell through its public queries */
static void Consume(const uint8_t *b, size_t sz, const Row *rows, unsigned nRows)
{
    char dir[64] = "/tmp/rgrel.XXXXXX", sub[96], cwd[1024], p[128];
    FILE *fp;
    unsigned i, k, seen = 0;

    CHECK(mkdtemp(dir) != NULL);
    CHECK(getcwd(cwd, sizeof(cwd)) != NULL);
    snprintf(sub, sizeof(sub), "%s/voxel", dir);
    CHECK(mkdir(sub, 0755) == 0);
    CHECK(chdir(dir) == 0);
    fp = fopen("voxel/relief.bin", "wb");
    CHECK(fp != NULL && fwrite(b, 1, sz, fp) == sz);
    if (fp) fclose(fp);
    CHECK(VoxelRelief_Init());
    CHECK(VoxelRelief_CutCount() == 0);
    for (i = 0; i < nRows; i++) {
        VoxelMapInstance inst;
        memset(&inst, 0, sizeof(inst));
        inst.layoutId = (int)rows[i].id;
        CHECK(!VoxelRelief_IsDrawn(&inst) && VoxelRelief_Base(&inst) == 0.0f);
        for (k = 0; k < rows[i].cells; k++) {
            const uint8_t *c = b + rows[i].off + 27u * k;
            const int16_t *g = VoxelRelief_Cell(&inst, c[0], c[1]);
            unsigned j;
            CHECK(g != NULL);
            for (j = 0; g != NULL && j < 25; j++) CHECK(g[j] == (int8_t)c[2 + j]);
            seen++;
        }
        CHECK(VoxelRelief_Cell(&inst, 0, 0) == NULL || rows[i].cells > 0);
    }
    CHECK(seen > 0);
    VoxelRelief_Shutdown();
    CHECK(chdir(cwd) == 0);
    snprintf(p, sizeof(p), "%s/voxel/relief.bin", dir); (void)unlink(p);
    (void)rmdir(sub); (void)rmdir(dir);
}

static uint8_t *ReadAll(const char *path, size_t *n)
{
    FILE *fp = fopen(path, "rb");
    long len;
    uint8_t *buf;

    if (fp == NULL) return NULL;
    fseek(fp, 0, SEEK_END); len = ftell(fp); fseek(fp, 0, SEEK_SET);
    buf = (uint8_t *)malloc((size_t)len);
    if (fread(buf, 1, (size_t)len, fp) != (size_t)len) { free(buf); fclose(fp); return NULL; }
    fclose(fp);
    *n = (size_t)len;
    return buf;
}

#define ROUTE101_CELLS 14u
#define ROUTE101_SHA1 "ec2cfdfe845f9e96f97884814164089fa33280c5"

static void TestRealRom(void)
{
    const char *path = getenv("ROMGEN_ROM");
    size_t n = 0;
    uint8_t *rom;
    RgWorld w;
    RgRoles r;
    RgOutput o1, o2;
    RgRunOpts opts;
    uint8_t *blob = NULL;
    size_t blobSz = 0;
    RgReliefStats st;
    Row rows[64];
    unsigned nRows, i, ids[512], nLedge;
    const char *why = NULL;

    if (path == NULL) { printf("SKIP real-ROM checks (ROMGEN_ROM unset)\n"); sSkips++; return; }
    rom = ReadAll(path, &n);
    if (rom == NULL) { printf("SKIP cannot read %s\n", path); sSkips++; return; }
    CHECK(rg_world_open(&w, rom, n) == RG_OK);
    CHECK(rg_rtables_check(&w, &why));
    if (why != NULL) printf("  tables: %s\n", why);
    memset(&r, 0, sizeof(r));
    CHECK(rg_relief_build(&w, &r, RG_RELIEF_OFF, NULL, NULL, NULL, &blob, &blobSz, &st) == RG_ERR_RELIEF);
    CHECK(rg_relief_build(&w, &r, RG_RELIEF_FULL, NULL, NULL, NULL, &blob, &blobSz, &st) == RG_ERR_RELIEF);
    CHECK(rg_relief_build(&w, &r, RG_RELIEF_LEDGES, NULL, NULL, NULL, &blob, &blobSz, &st) == RG_OK && blob != NULL);
    {   /* candidates: outdoor layouts with a jump cell */
        uint16_t id16[512];
        nLedge = rg_ledge_layouts(&w, id16, 512);
        for (i = 0; i < nLedge && i < 512; i++) ids[i] = id16[i];
    }
    nRows = Decode(blob, blobSz, rows, 64, &w);
    CHECK(nRows == st.rows && nRows > 0 && st.ledgeLayouts == nLedge);
    CheckLedgeGeometry(blob, rows, nRows, &w);
    {   /* the row set == candidates whose berm lifts some cell: recompute each, compare presence and bytes */
        unsigned present = 0;
        for (i = 0; i < nLedge; i++) {
            const RgLayout *L = &w.layouts[ids[i] - 1u];
            RgPair *p = rg_pair_open(&w, L->pairIndex);
            RgLat lat;
            uint8_t *cells = NULL;
            int c = -1, led = -1;
            unsigned k, row = 0;
            bool has = false;

            CHECK(p != NULL && rg_lat_new(&lat, L->w, L->h));
            if (p != NULL) { led = rg_ledge_berms(L, p, &lat); c = rg_relief_cells(&lat, 1, &cells); rg_pair_close(p); }
            CHECK(led > 0 && c >= 0);
            for (k = 0; k < nRows; k++) if (rows[k].id == ids[i]) { has = true; row = k; }
            CHECK(has == (c > 0));
            if (has && c > 0) {
                present++;
                CHECK(rows[row].cells == (unsigned)c && memcmp(blob + rows[row].off, cells, (size_t)c * 27u) == 0);
            }
            free(cells);
            rg_lat_free(&lat);
        }
        CHECK(present == nRows);
    }
    /* Route 101 (layout 17): row present, cell count + SHA-1 of its cell bytes pinned */
    {
        char hex[41];
        unsigned k;
        bool found = false;
        for (k = 0; k < nRows; k++)
            if (rows[k].id == 17) {
                found = true;
                Sha1(blob + rows[k].off, 27u * rows[k].cells, hex);
                printf("  Route 101 (layout 17): %u cells, SHA-1 of cell bytes %s\n", rows[k].cells, hex);
                CHECK(rows[k].cells == ROUTE101_CELLS);
                CHECK(strcmp(hex, ROUTE101_SHA1) == 0);
            }
        CHECK(found);
    }
    printf("  relief.bin: %zu bytes, %u rows, %u cells, %u ledge layouts, %u ledge cells\n", blobSz, nRows, st.cells,
           st.ledgeLayouts, st.ledgeCells);
    Consume(blob, blobSz, rows, nRows);

    /* determinism: a second build, and rg_run's own relief output, are byte-identical */
    {
        uint8_t *again = NULL;
        size_t againSz = 0;
        CHECK(rg_relief_build(&w, &r, RG_RELIEF_LEDGES, NULL, NULL, NULL, &again, &againSz, NULL) == RG_OK);
        CHECK(againSz == blobSz && memcmp(again, blob, blobSz) == 0);
        free(again);
    }
    memset(&opts, 0, sizeof(opts));
    opts.relief = RG_RELIEF_LEDGES;
    CHECK(rg_run(rom, n, &opts, &o1) == RG_OK);
    CHECK(rg_run(rom, n, &opts, &o2) == RG_OK);
    CHECK(o1.relief != NULL && o1.reliefSize == blobSz && memcmp(o1.relief, blob, blobSz) == 0);
    CHECK(o2.relief != NULL && o2.reliefSize == blobSz && memcmp(o1.relief, o2.relief, blobSz) == 0);
    CHECK(o1.rst.rows == nRows);
    CHECK(o1.regionsSize == o2.regionsSize && memcmp(o1.regions, o2.regions, o1.regionsSize) == 0);
    rg_output_free(&o1);
    rg_output_free(&o2);
    opts.relief = RG_RELIEF_OFF;                                  /* off leaves relief NULL and the other files alone */
    CHECK(rg_run(rom, n, &opts, &o1) == RG_OK && o1.relief == NULL && o1.reliefSize == 0 && o1.regions != NULL);
    rg_output_free(&o1);
    free(blob);
    rg_world_close(&w);
    free(rom);
}

int main(void)
{
    TestHalfEven();
    TestSynthetic();
    TestWriter();
    TestRealRom();
    printf("test_romgen_relief_ledge: %d checks, %d failures%s\n", sChecks, sFails, sSkips ? " (real-ROM part skipped)" : "");
    return sFails ? 1 : 0;
}
