// test_romgen_frlg_relief.c -- Phase 34 slice L1 (SPEC-P34 section 7.1): Kanto ledges in relief.bin (LEDGES mode).
//  1. synthetic: a 3x3 layout with one row of FRLG south ledges (jump set 0x38-0x3B) gives the hand-computed lattice
//     heights; 0x3C (a diagonal jump on Emerald) is not a ledge under the FRLG profile; ENABLED never applies; a layout
//     whose maps are all indoor/underground has no ledge row; outdoor = map types 1, 2, 3, 5, 6.
//  2. real FireRed / LeafGreen rev 1 (ROMGEN_ROM_FR / _LG, else firered.gba / leafgreen.gba beside ROMGEN_ROM):
//     Route 1 (layout 89) ledge cell count and its row, whole-file counts, the SHA-1 pin; every row is an outdoor
//     layout; FULL on FRLG is the LEDGES file; OFF writes nothing; FR and LG files are byte-identical; the vendored
//     consumer (VoxelRelief_Init) reads every cell back.
//
//   ROMGEN_ROM=$PWD/roms/emerald.gba make -C tools/romgen test T=frlg_relief
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rg_ledge.h"
#include "rg_relief.h"
#include "rg_rtables.h"
#include "rg_run.h"
#include "rg_fixture.h"
#include "voxel_relief.h"

static int sChecks, sFails, sSkipped;
#define CHECK(c) do { ++sChecks; if (!(c)) { ++sFails; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

/* ---- pins (measured on FR rev 1 after looking at the Route 1 / Route 4 / Route 25 ledge art; LG is byte-identical) ---- */
#define ROUTE1_LAYOUT 89u
#define ROUTE1_ROW_CELLS 61u            /* cells of Route 1's relief row */
#define ROUTE1_LEDGE_CELLS 61u          /* jump cells (no junction on Route 1) = the ledge cells of the row */
#define RELIEF_SIZE 26320u
#define RELIEF_ROWS 31u
#define RELIEF_CELLS 958u
#define RELIEF_LEDGE_CELLS 960u
#define RELIEF_SHA1 "32c24146974e720620e97906024d8a60d2f6e09c"

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

/* ---- 1. synthetic ---- */
enum { MT_GRASS = 0, MT_LEDGE_S = 1, MT_DIAG = 2, MT_COUNT = 3 };
static uint8_t sTiles[4 * 32];
static uint16_t sPal[256], sMt[MT_COUNT * 8], sAtt[MT_COUNT];

static void BuildTileset(void)
{
    unsigned r, b;

    memset(sTiles, 0, sizeof(sTiles));
    memset(sTiles + 32, 0x11, 32);                       /* tile 1: green */
    memset(sTiles + 64, 0x22, 32);                       /* tile 2: brown */
    for (r = 0; r < 8; r++)                              /* tile 3: rows 0-5 brown, rows 6-7 green */
        for (b = 0; b < 4; b++) sTiles[96 + r * 4 + b] = r < 6 ? 0x22 : 0x11;
    sPal[1] = 0x03E0;
    sPal[2] = 0x1D6B;
    sMt[0] = sMt[1] = sMt[2] = sMt[3] = 1;
    sMt[8] = sMt[9] = 1; sMt[10] = sMt[11] = 3;          /* ledge: top subtiles green, bottom subtiles the lip */
    sMt[16] = sMt[17] = 1; sMt[18] = sMt[19] = 3;        /* the diagonal twin, same drawing */
    sAtt[MT_GRASS] = 0; sAtt[MT_LEDGE_S] = 0x3B;         /* FRLG MB_JUMP_SOUTH, the same number as Emerald's */
    sAtt[MT_DIAG] = 0x3C;                                /* Emerald's MB_JUMP_SOUTHEAST: not a ledge in Kanto */
}

/* A lip 6 pixels deep (rows 8..13 of the cell), hand computation of S3.1's test: f = lip pixels from a point down.
 * j=0 py16 0; j=1 py20 f0 b0, lip starts 4 on: 0.75*6 = 4.5 * (1 - 4/8) = 2.25; j=2 py24 f6: 4.5; j=3 py28 f2: 1.5; j=4 0 */
static const double kCol[5] = {0.0, 2.25, 4.5, 1.5, 0.0};

static GameProfile sFr;      /* the Emerald row recoloured as FireRed: same attribute width, FRLG's jump set and game id */

static void MakeFrlgProfile(void)
{
    unsigned b;

    sFr = *gameprof_emerald();
    sFr.game = GP_FIRERED;
    memset(&sFr.jump, 0, sizeof sFr.jump);
    for (b = 0x38; b <= 0x3B; b++) sFr.jump.w[b >> 5] |= 1u << (b & 31u);
}

static void TestSynthetic(void)
{
    RgFx f;
    RgWorld w;
    uint32_t ts;
    uint16_t blocks[9], blocksDiag[9];
    unsigned idOut, idIn, idDiag, idUg, i;
    RgLedgeSet s;
    RgLat lat;
    RgPair *pair;
    RgLayout copy;
    uint16_t ids[16];
    int led, x, y;

    BuildTileset();
    MakeFrlgProfile();
    fxr_init(&f);
    ts = fxr_tileset(&f, 0, 0, sTiles, sizeof(sTiles), sPal, sMt, sAtt, MT_COUNT, 1);
    for (i = 0; i < 9; i++) blocks[i] = blocksDiag[i] = MT_GRASS;
    blocks[3] = blocks[4] = blocks[5] = MT_LEDGE_S;
    blocksDiag[4] = MT_DIAG;
    idIn = fxr_layout(&f, 3, 3, blocks, ts, 0);             /* referenced only by an indoor map: no ledge row */
    idOut = fxr_layout(&f, 3, 3, blocks, ts, 0);            /* a Route-type map */
    idDiag = fxr_layout(&f, 3, 3, blocksDiag, ts, 0);       /* 0x3C only: no jump cell under the FRLG profile */
    idUg = fxr_layout(&f, 3, 3, blocks, ts, 0);             /* underground (4): not outdoor */
    (void)fxr_map(&f, 0, 8, idIn, NULL, 0, NULL, 0);        /* MAP_TYPE_INDOOR */
    (void)fxr_map(&f, 0, 3, idOut, NULL, 0, NULL, 0);       /* MAP_TYPE_ROUTE */
    (void)fxr_map(&f, 0, 5, idDiag, NULL, 0, NULL, 0);      /* MAP_TYPE_UNDERWATER counts as outdoor */
    (void)fxr_map(&f, 0, 4, idUg, NULL, 0, NULL, 0);        /* MAP_TYPE_UNDERGROUND */
    fxr_finish(&f);
    CHECK(rg_world_open(&w, f.rom, FXR_ROM_SIZE) == RG_OK);

    /* the Emerald world first: rg_relief_outdoor_layout is rg_relief_outdoor there (the byte-identity guard) */
    CHECK(w.prof->game == GP_EMERALD);
    for (i = 1; i <= w.layoutCount; i++) CHECK(rg_relief_outdoor_layout(&w, (uint16_t)i) == rg_relief_outdoor((uint16_t)i));

    /* switch the whole world to the FRLG profile */
    w.prof = &sFr;
    for (i = 0; i < w.layoutCount; i++) w.layouts[i].prof = &sFr;
    CHECK(rg_relief_outdoor_layout(&w, (uint16_t)idOut) && rg_relief_outdoor_layout(&w, (uint16_t)idDiag));
    CHECK(!rg_relief_outdoor_layout(&w, (uint16_t)idIn) && !rg_relief_outdoor_layout(&w, (uint16_t)idUg));
    CHECK(!rg_relief_outdoor_layout(&w, 0) && !rg_relief_outdoor_layout(&w, (uint16_t)(w.layoutCount + 1u)));

    /* ledge layouts: only the route layout (the diagonal-only one has no jump cell, the indoor and underground ones are not outdoor) */
    CHECK(rg_ledge_layouts(&w, ids, 16) == 1 && ids[0] == idOut);
    CHECK(rg_ledge_layouts(&w, NULL, 0) == 1);

    /* cells: three south jump cells; 0x3C is not one */
    CHECK(rg_ledge_cells(&w.layouts[idOut - 1], true, &s) && s.n == 3 && s.nJump == 3);
    for (i = 0; i < s.n; i++) CHECK(s.c[i].y == 1 && s.c[i].x == (int)i && s.c[i].nDirs == 1 && s.c[i].dx[0] == 0 && s.c[i].dy[0] == 1);
    rg_ledge_set_free(&s);
    CHECK(rg_ledge_cells(&w.layouts[idDiag - 1], true, &s) && s.n == 0);
    rg_ledge_set_free(&s);

    /* ENABLED is an Emerald list: no Kanto layout skips the junction rule, whatever its id */
    copy = w.layouts[idOut - 1];
    copy.id = 20;
    CHECK(!rg_ledge_enabled(&copy));
    copy.prof = gameprof_emerald();
    CHECK(rg_ledge_enabled(&copy));
    copy.id = 17;
    CHECK(!rg_ledge_enabled(&copy));

    /* the berm: the hand-computed column on every cell of the ledge row, nothing elsewhere */
    CHECK(rg_lat_new(&lat, 3, 3));
    pair = rg_pair_open(&w, w.layouts[idOut - 1].pairIndex);
    CHECK(pair != NULL);
    led = pair != NULL ? rg_ledge_berms(&w.layouts[idOut - 1], pair, &lat) : -1;
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
    rg_lat_free(&lat);
    if (pair != NULL) rg_pair_close(pair);

    /* rg_relief_build under the FRLG profile: no T1-T9 tables needed, FULL asked = LEDGES built, one row, three cells */
    {
        uint8_t *out = NULL;
        size_t n = 0;
        RgReliefStats st;
        RgErr e = rg_relief_build(&w, NULL, RG_RELIEF_FULL, NULL, NULL, NULL, &out, &n, &st);

        CHECK(e == RG_OK && out != NULL && st.ledgeLayouts == 1 && st.rows == 1 && st.cells == 3 && st.ledgeCells == 3 && st.drawnRows == 0);
        CHECK(out != NULL && U16(out, 4) == 1 && U16(out, 8) == idOut && U16(out, 10) == 3);
        free(out);
        out = NULL;
        CHECK(rg_relief_build(&w, NULL, RG_RELIEF_OFF, NULL, NULL, NULL, &out, &n, &st) == RG_ERR_RELIEF);
    }
    rg_world_close(&w);
    free(f.rom);
}

/* ---- 2. the real carts ---- */
typedef struct {
    const char *name, *env;
    GpGame game;
    uint8_t *rom;
    size_t n;
    RgOutput out;
    char sha[41];
} Cart;
static Cart sCart[2] = {{.name = "FireRed", .env = FXR_ENV_FR, .game = GP_FIRERED},
                        {.name = "LeafGreen", .env = FXR_ENV_LG, .game = GP_LEAFGREEN}};

static void Consume(const uint8_t *b, size_t sz)
{
    char dir[64] = "/tmp/rgfrl.XXXXXX", sub[96], cwd[1024];
    FILE *fp;
    unsigned n, i, k, cells = 0;

    CHECK(mkdtemp(dir) != NULL);
    CHECK(getcwd(cwd, sizeof(cwd)) != NULL);
    snprintf(sub, sizeof(sub), "%s/voxel", dir);
    CHECK(mkdir(sub, 0755) == 0);
    CHECK(chdir(dir) == 0);
    fp = fopen("voxel/relief.bin", "wb");
    CHECK(fp != NULL && fwrite(b, 1, sz, fp) == sz);
    if (fp) fclose(fp);
    CHECK(VoxelRelief_Init());
    n = U16(b, 4);
    for (i = 0; i < n; i++) {
        const uint8_t *r = b + 8 + 14u * i;
        VoxelMapInstance inst;

        memset(&inst, 0, sizeof(inst));
        inst.layoutId = (int)U16(r, 0);
        CHECK(!VoxelRelief_IsDrawn(&inst) && VoxelRelief_Base(&inst) == 0.0f);
        for (k = 0; k < U16(r, 2); k++) {
            const uint8_t *c = b + U32(r, 8) + 27u * k;
            const int16_t *g = VoxelRelief_Cell(&inst, c[0], c[1]);
            unsigned j;

            CHECK(g != NULL);
            for (j = 0; g != NULL && j < 25; j++) CHECK(g[j] == (int8_t)c[2 + j]);
            cells++;
        }
    }
    CHECK(cells == RELIEF_CELLS);
    VoxelRelief_Shutdown();
    CHECK(chdir(cwd) == 0);
    snprintf(sub, sizeof(sub), "%s/voxel/relief.bin", dir); (void)unlink(sub);
    snprintf(sub, sizeof(sub), "%s/voxel", dir); (void)rmdir(sub);
    (void)rmdir(dir);
}

static void RunCart(Cart *c)
{
    RgRunOpts opts;
    RgWorld w;
    RgOutput other;
    const RgLayout *L;
    RgLedgeSet s;
    unsigned i, n, k;

    c->rom = fxr_load_rom(c->env, &c->n);
    if (c->rom == NULL) {
        printf("SKIP %s: ROM not found (set ROMGEN_ROM_%s or ROMGEN_ROM)\n", c->name, c->game == GP_FIRERED ? "FR" : "LG");
        sSkipped++;
        return;
    }
    CHECK(rg_world_open(&w, c->rom, c->n) == RG_OK && w.prof->game == c->game);
    memset(&opts, 0, sizeof(opts));
    opts.relief = RG_RELIEF_LEDGES;
    CHECK(rg_run(c->rom, c->n, &opts, &c->out) == RG_OK);
    CHECK(c->out.relief != NULL && c->out.reliefSize == RELIEF_SIZE);
    CHECK(c->out.rst.rows == RELIEF_ROWS && c->out.rst.cells == RELIEF_CELLS && c->out.rst.ledgeLayouts == RELIEF_ROWS
          && c->out.rst.ledgeCells == RELIEF_LEDGE_CELLS && c->out.rst.drawnRows == 0);
    Sha1(c->out.relief, c->out.reliefSize, c->sha);
    printf("pin %s relief.bin = %s\n", c->name, c->sha);
    CHECK(strcmp(c->sha, RELIEF_SHA1) == 0);

    /* FULL on Kanto has no drawn relief to add: the same bytes; OFF writes nothing */
    memset(&opts, 0, sizeof(opts));
    opts.relief = RG_RELIEF_FULL;
    CHECK(rg_run(c->rom, c->n, &opts, &other) == RG_OK);
    CHECK(other.relief != NULL && other.reliefSize == c->out.reliefSize && memcmp(other.relief, c->out.relief, other.reliefSize) == 0);
    rg_output_free(&other);
    memset(&opts, 0, sizeof(opts));
    CHECK(rg_run(c->rom, c->n, &opts, &other) == RG_OK && other.relief == NULL && other.reliefSize == 0);
    rg_output_free(&other);

    /* every row is an outdoor layout; every outdoor layout with a jump cell has a row, no other layout has one */
    n = U16(c->out.relief, 4);
    CHECK(n == RELIEF_ROWS);
    for (i = 0; i < n; i++) {
        const uint8_t *r = c->out.relief + 8 + 14u * i;
        unsigned id = U16(r, 0);

        L = &w.layouts[id - 1u];
        CHECK(L->present && L->outdoor && rg_relief_outdoor_layout(&w, (uint16_t)id));
        CHECK(U16(r, 4) == L->w && (U16(r, 6) & 0x3FFFu) == L->h);
        CHECK((U16(r, 6) >> 14) == 0 && (int16_t)U16(r, 12) == 0);   /* not drawn, base 0 */
        CHECK(i == 0 || id > U16(r - 14, 0));
    }
    for (i = 0; i < w.layoutCount; i++) {
        bool hasJump = false, hasRow = false;
        int x, y;

        L = &w.layouts[i];
        if (!L->present) continue;
        for (y = 0; y < (int)L->h && !hasJump; y++)
            for (x = 0; x < (int)L->w && !hasJump; x++)
                hasJump = gp_beh(&w.prof->jump, rg_behaviour(L, x, y));
        for (k = 0; k < n; k++) hasRow = hasRow || U16(c->out.relief, 8 + 14u * k) == L->id;
        CHECK(hasRow == (hasJump && L->outdoor != 0));          /* a jump cell on a non-outdoor layout never gets a row */
        CHECK(!rg_ledge_enabled(L));
    }

    /* Route 1 */
    L = &w.layouts[ROUTE1_LAYOUT - 1u];
    CHECK(L->w == 24 && L->h == 40 && L->outdoor);
    CHECK(rg_ledge_cells(L, true, &s));
    printf("Route 1: %u ledge cells (%u jump)\n", s.n, s.nJump);
    CHECK(s.n == ROUTE1_LEDGE_CELLS);
    rg_ledge_set_free(&s);
    for (i = 0; i < n; i++)
        if (U16(c->out.relief, 8 + 14u * i) == ROUTE1_LAYOUT) {
            const uint8_t *r = c->out.relief + U32(c->out.relief, 8 + 14u * i + 8);
            unsigned nc = U16(c->out.relief, 8 + 14u * i + 2), j;
            int peak = 0;

            printf("Route 1 relief row: %u cells\n", nc);
            CHECK(nc == ROUTE1_ROW_CELLS);
            for (k = 0; k < nc; k++)
                for (j = 0; j < 25; j++)
                    if ((int8_t)r[27u * k + 2u + j] > peak) peak = (int8_t)r[27u * k + 2u + j];
            CHECK(peak == 6);                                    /* the lip's top edge is the same 6 pixels as Emerald's */
            /* the first cell of the row, (2,5): the measured south-ledge lattice (row j=2 peaks at 6, j=1 and 3 at 3) */
            CHECK(r[0] == 2 && r[1] == 5);
            {
                static const int8_t want[25] = {0, 0, 0, 0, 0, 0, 2, 3, 2, 3, 0, 5, 6, 5, 6, 0, 3, 3, 3, 3, 0, 0, 0, 0, 0};
                for (j = 0; j < 25; j++) CHECK((int8_t)r[2 + j] == want[j]);
            }
        }
    Consume(c->out.relief, c->out.reliefSize);
    rg_world_close(&w);
}

int main(void)
{
    unsigned k;

    TestSynthetic();
    for (k = 0; k < 2; k++) RunCart(&sCart[k]);
    if (sCart[0].rom != NULL && sCart[1].rom != NULL)
        CHECK(sCart[0].out.reliefSize == sCart[1].out.reliefSize && memcmp(sCart[0].out.relief, sCart[1].out.relief, sCart[0].out.reliefSize) == 0);
    for (k = 0; k < 2; k++) {
        if (sCart[k].rom == NULL) continue;
        rg_output_free(&sCart[k].out);
        free(sCart[k].rom);
    }
    printf("test_romgen_frlg_relief: %d checks, %d failures, %d skipped\n", sChecks, sFails, sSkipped);
    return sFails != 0;
}
