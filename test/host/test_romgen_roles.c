// test_romgen_roles.c -- host test for source/romgen/rg_roles.c (phase 33 S1.1, SPEC-S0-S1 sections 3, 7.2).
// Synthetic layouts (one per branch of role_at) always run; the real-ROM invariants 1-9 run with
// ROMGEN_ROM=/path/emerald.gba (otherwise SKIP, still passes).
//
//   clang -std=c11 -Wall -Wextra -O2 -ffp-contract=off -fsanitize=address,undefined \
//         -I source/romgen -I source/voxel -I test/host test/host/test_romgen_roles.c \
//         source/romgen/rg_world.c source/romgen/rg_art.c source/romgen/rg_roles.c \
//         source/voxel/vx_lz77.c -lm -o /tmp/trgr && /tmp/trgr
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rg_behavior.h"
#include "rg_roles.h"
#include "rg_fixture.h"
#include "voxel_regions.h"

static int sChecks, sFails;
#define CHECK(c) do { ++sChecks; if (!(c)) { ++sFails; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

/* ---- synthetic tileset ------------------------------------------------------------------- */
typedef unsigned (*PixFn)(unsigned x, unsigned y);
static void MakeTile(uint8_t *t, PixFn f)
{
    unsigned x, y;
    memset(t, 0, 32);
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++) {
            unsigned i = f(x, y) & 15u;
            t[y * 4 + x / 2] |= (uint8_t)((x & 1) ? i << 4 : i);
        }
}
static unsigned PixBlank(unsigned x, unsigned y) { (void)x; (void)y; return 0; }
static unsigned PixGreen(unsigned x, unsigned y) { (void)x; (void)y; return 1; }
static unsigned PixRed(unsigned x, unsigned y) { (void)x; (void)y; return 2; }
static unsigned PixGrey(unsigned x, unsigned y) { (void)x; (void)y; return 5; }
static unsigned PixBrown(unsigned x, unsigned y) { (void)x; (void)y; return 6; }
static unsigned PixBand(unsigned x, unsigned y) { (void)x; return (y & 1) ? 4 : 3; }

enum { T_BLANK, T_GREEN, T_RED, T_GREY, T_BROWN, T_BAND, T_COUNT };
#define ENT(tile, pal) (uint16_t)((tile) | ((pal) << 12))
enum { M_FLOOR, M_WALL, M_TREE, M_WATER, M_JUMP, M_STAIR, M_POST, M_DOOR, M_HEAD, M_LAMP, M_COVER, M_COUNT };

static uint16_t sPal[256], sMt[M_COUNT * 8], sAtt[M_COUNT];
static uint8_t sTiles[T_COUNT * 32];

static void Meta(unsigned m, unsigned t0, unsigned t1, unsigned attr)
{
    unsigned q;
    for (q = 0; q < 4; q++) {
        sMt[m * 8 + q] = ENT(t0, 1);
        sMt[m * 8 + 4 + q] = ENT(t1, 1);
    }
    sAtt[m] = (uint16_t)attr;
}

static void BuildTileset(void)
{
    memset(sPal, 0, sizeof(sPal));
    sPal[16 + 1] = 0x03E0;   /* green */
    sPal[16 + 2] = 0x001F;   /* red */
    sPal[16 + 3] = 0x7FFF;   /* white */
    sPal[16 + 4] = 0x0000;   /* black */
    sPal[16 + 5] = 0x5294;   /* grey */
    sPal[16 + 6] = 0x01D7;   /* brown-ish */
    MakeTile(sTiles + T_BLANK * 32, PixBlank); MakeTile(sTiles + T_GREEN * 32, PixGreen);
    MakeTile(sTiles + T_RED * 32, PixRed); MakeTile(sTiles + T_GREY * 32, PixGrey);
    MakeTile(sTiles + T_BROWN * 32, PixBrown); MakeTile(sTiles + T_BAND * 32, PixBand);
    Meta(M_FLOOR, T_GREY, T_BLANK, 0);
    Meta(M_WALL, T_RED, T_BLANK, 0);
    Meta(M_TREE, T_GREEN, T_BLANK, 0);
    Meta(M_WATER, T_GREY, T_BLANK, 0x10);
    Meta(M_JUMP, T_GREY, T_BLANK, 0x3B);
    Meta(M_STAIR, T_BAND, T_BLANK, 0);
    Meta(M_POST, T_BROWN, T_BLANK, 0);
    Meta(M_DOOR, T_RED, T_BLANK, 0x69);
    Meta(M_HEAD, T_GREY, T_RED, 0);          /* walkable cell with an upper-layer head */
    Meta(M_LAMP, T_BROWN, T_BLANK, 0);
    Meta(M_COVER, T_GREY, T_RED, 0);         /* upper layer fills the cell */
    sMt[M_HEAD * 8 + 4] = sMt[M_HEAD * 8 + 5] = ENT(T_RED, 1); sMt[M_HEAD * 8 + 6] = sMt[M_HEAD * 8 + 7] = ENT(T_BLANK, 1);
}

/* char -> (metatile, collision) */
static uint16_t Cell(char ch)
{
    switch (ch) {
    case '.': return M_FLOOR;
    case '#': return M_WALL | (1 << 10);
    case 'T': return M_TREE | (1 << 10);
    case '~': return M_WATER | (1 << 10);
    case '^': return M_JUMP | (1 << 10);
    case '=': return M_STAIR;
    case 'P': return M_POST | (1 << 10);
    case 'D': return M_DOOR;
    case 'h': return M_HEAD;
    case 'l': return M_LAMP | (1 << 10);
    case 'C': return M_COVER | (1 << 10);
    default: abort();
    }
}

static unsigned AddLayout(RgFx *f, uint32_t prim, int w, int h, const char *const *rows)
{
    uint16_t *b = (uint16_t *)malloc((size_t)(w * h) * 2);
    int x, y;
    unsigned id;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) b[y * w + x] = Cell(rows[y][x]);
    id = fxr_layout(f, w, h, b, prim, 0);
    free(b);
    return id;
}

/* a w x h map of '.' with edits */
typedef struct { int x, y; char ch; } Edit;
static void Fill(char *rows[], int w, int h)
{
    int y;
    for (y = 0; y < h; y++) { rows[y] = (char *)malloc((size_t)w + 1); memset(rows[y], '.', (size_t)w); rows[y][w] = 0; }
}
static void Put(char *rows[], const char *s, int x, int y) { memcpy(rows[y] + x, s, strlen(s)); }
static void Free(char *rows[], int h) { int y; for (y = 0; y < h; y++) free(rows[y]); }

static unsigned RoleAt(const RgRoles *r, const RgWorld *w, unsigned id, int x, int y)
{
    return rg_roles_of(r, (uint16_t)id)[y * w->layouts[id - 1].w + x];
}

static void TestSynthetic(void)
{
    RgFx f;
    RgWorld w;
    RgRoles r;
    uint32_t tsA, tsB;
    char *A[16], *B[8], *C[8], *D[12], *E[8], *F[10];
    unsigned idA, idB, idC, idD, idE, idF;
    int16_t wA[2][2] = {{20, 14}, {0, 0}};
    uint16_t sA[2][3] = {{14, 6, 0}, {0, 0, 0}};
    int16_t wD[1][2] = {{3, 10}};
    int16_t wF[1][2] = {{8, 5}};
    RgErr e;

    BuildTileset();
    fxr_init(&f);
    tsA = fxr_tileset(&f, 0, 0, sTiles, sizeof(sTiles), sPal, sMt, sAtt, M_COUNT, 1);
    tsB = fxr_tileset(&f, 0, 0, sTiles, sizeof(sTiles), sPal, sMt, sAtt, M_COUNT, 1);   /* a twin at another address */

    /* A: the outdoor branches (24 x 16) */
    Fill(A, 24, 16);
    Put(A, "~", 1, 1); Put(A, "^", 3, 1); Put(A, "=", 7, 1);
    Put(A, "TTT", 1, 3); Put(A, "T", 6, 3);
    Put(A, "###", 1, 6);
    Put(A, "#", 6, 5); Put(A, "#", 6, 6); Put(A, "#", 6, 7);
    Put(A, "###", 9, 5); Put(A, "###", 9, 6); Put(A, "###", 9, 7);
    Put(A, "#", 0, 10); Put(A, "#", 0, 11); Put(A, "#", 0, 12);
    Put(A, "#", 14, 2); Put(A, "C", 16, 2);
    Put(A, "P", 14, 6);                                   /* a sign event: open post */
    Put(A, "#", 13, 10); Put(A, "P", 14, 10); Put(A, "#", 15, 10); Put(A, "#", 14, 9);   /* wall-backed post */
    Put(A, "###", 19, 12); Put(A, "###", 19, 13); Put(A, "D", 20, 14);                   /* a house */
    Put(A, "P", 22, 4); Put(A, "#", 21, 4); Put(A, "#", 23, 4);                           /* un-evented post, walls both sides */
    idA = AddLayout(&f, tsA, 24, 16, (const char *const *)A);
    /* B: same tileset, no events: a wall-backed M_POST. C: twin tileset: the same, must NOT be a signpost. */
    Fill(B, 8, 8); Put(B, "#", 2, 4); Put(B, "P", 3, 4); Put(B, "#", 4, 4); Put(B, "#", 3, 3);
    idB = AddLayout(&f, tsA, 8, 8, (const char *const *)B);
    Fill(C, 8, 8); Put(C, "#", 2, 4); Put(C, "P", 3, 4); Put(C, "#", 4, 4); Put(C, "#", 3, 3);
    idC = AddLayout(&f, tsB, 8, 8, (const char *const *)C);
    /* D: house flood limits (30 x 12) */
    Fill(D, 30, 12);
    { int y; for (y = 0; y <= 9; y++) Put(D, "#", 3, y); }
    Put(D, "####", 0, 9); Put(D, "T", 4, 9); Put(D, "####", 5, 9); Put(D, "###############", 9, 9); Put(D, "D", 3, 10);
    Put(D, "#######", 3, 7);                                                   /* x = 3..9 on row 7: 5 columns right is the limit */
    Put(D, "#", 3, 9);
    idD = AddLayout(&f, tsA, 30, 12, (const char *const *)D);
    /* E: indoors copy of lone posts: no signposts at all */
    Fill(E, 8, 8); Put(E, "#", 3, 3);
    idE = AddLayout(&f, tsA, 8, 8, (const char *const *)E);
    /* F: lamps (20 x 10) */
    Fill(F, 20, 10);
    Put(F, "h", 3, 3); Put(F, "l", 3, 4);                                    /* a free-standing lamp */
    Put(F, "###", 7, 3); Put(F, "###", 7, 4); Put(F, "D", 8, 5);            /* a house */
    Put(F, "h", 10, 3); Put(F, "l", 10, 4);                                  /* a lamp against the house wall: signpost */
    Put(F, "#", 13, 4); Put(F, "h", 15, 3); Put(F, "l", 14, 4);            /* a lamp against a plain wall: not */
    Put(F, "h", 14, 3);
    idF = AddLayout(&f, tsA, 20, 10, (const char *const *)F);
    CHECK(idA == 2 && idB == 3 && idC == 4 && idD == 5 && idE == 6 && idF == 7);

    wA[0][0] = 20; wA[0][1] = 14;
    (void)fxr_map(&f, 0, MAP_TYPE_ROUTE, idA, wA, 1, sA, 1);
    (void)fxr_map(&f, 0, MAP_TYPE_TOWN, idB, NULL, 0, NULL, 0);
    (void)fxr_map(&f, 0, MAP_TYPE_CITY, idC, NULL, 0, NULL, 0);
    (void)fxr_map(&f, 0, MAP_TYPE_ROUTE, idD, wD, 1, NULL, 0);
    (void)fxr_map(&f, 0, MAP_TYPE_INDOOR, idE, NULL, 0, NULL, 0);
    (void)fxr_map(&f, 0, MAP_TYPE_ROUTE, idF, wF, 1, NULL, 0);
    fxr_finish(&f);

    e = rg_world_open(&w, f.rom, FXR_ROM_SIZE);
    CHECK(e == RG_OK);
    e = rg_roles_all(&w, &r, false, NULL, NULL);
    CHECK(e == RG_OK);
    CHECK(r.postCount == 1);                 /* only the evented, open-on-three-sides M_POST of tileset A */

    /* A */
    CHECK(RoleAt(&r, &w, idA, 1, 1) == VOXEL_ROLE_WATER);       /* water beats blocked */
    CHECK(RoleAt(&r, &w, idA, 3, 1) == VOXEL_ROLE_LEDGE);       /* jump beats blocked */
    CHECK(RoleAt(&r, &w, idA, 5, 1) == VOXEL_ROLE_FLOOR);
    CHECK(RoleAt(&r, &w, idA, 7, 1) == VOXEL_ROLE_STAIR);
    CHECK(RoleAt(&r, &w, idA, 2, 3) == VOXEL_ROLE_TREE && RoleAt(&r, &w, idA, 6, 3) == VOXEL_ROLE_TREE);   /* lone foliage is no post */
    CHECK(RoleAt(&r, &w, idA, 2, 6) == VOXEL_ROLE_FENCE && RoleAt(&r, &w, idA, 1, 6) == VOXEL_ROLE_FENCE
          && RoleAt(&r, &w, idA, 3, 6) == VOXEL_ROLE_FENCE);     /* horizontal run */
    CHECK(RoleAt(&r, &w, idA, 6, 6) == VOXEL_ROLE_FENCE && RoleAt(&r, &w, idA, 6, 5) == VOXEL_ROLE_FENCE
          && RoleAt(&r, &w, idA, 6, 7) == VOXEL_ROLE_FENCE);     /* vertical run, top/middle/bottom */
    CHECK(RoleAt(&r, &w, idA, 9, 5) == VOXEL_ROLE_CLIFF && RoleAt(&r, &w, idA, 10, 5) == VOXEL_ROLE_CLIFF
          && RoleAt(&r, &w, idA, 11, 7) == VOXEL_ROLE_CLIFF);
    CHECK(RoleAt(&r, &w, idA, 10, 6) == VOXEL_ROLE_SHELF);
    CHECK(RoleAt(&r, &w, idA, 0, 11) == VOXEL_ROLE_CLIFF);       /* fence needs the neighbours IN the map */
    CHECK(RoleAt(&r, &w, idA, 14, 2) == VOXEL_ROLE_SIGNPOST);    /* a lone drawing on open ground */
    CHECK(RoleAt(&r, &w, idA, 16, 2) == VOXEL_ROLE_FENCE);       /* a cover is no post */
    CHECK(RoleAt(&r, &w, idA, 14, 6) == VOXEL_ROLE_SIGNPOST);    /* the sign event */
    CHECK(RoleAt(&r, &w, idA, 14, 10) == VOXEL_ROLE_SIGNPOST);   /* wall-backed, carried by post_metatiles */
    CHECK(RoleAt(&r, &w, idA, 13, 10) != VOXEL_ROLE_SIGNPOST);
    CHECK(RoleAt(&r, &w, idA, 22, 4) == VOXEL_ROLE_SIGNPOST);    /* post_metatile, walls both sides, south open */
    CHECK(RoleAt(&r, &w, idA, 19, 12) == VOXEL_ROLE_WALL && RoleAt(&r, &w, idA, 20, 13) == VOXEL_ROLE_WALL
          && RoleAt(&r, &w, idA, 21, 12) == VOXEL_ROLE_WALL);
    CHECK(RoleAt(&r, &w, idA, 20, 14) == VOXEL_ROLE_FLOOR);      /* the door cell is walkable */
    /* B / C: the carried post. A twin tileset at another address does not share the key. */
    CHECK(RoleAt(&r, &w, idB, 3, 4) == VOXEL_ROLE_SIGNPOST);
    CHECK(RoleAt(&r, &w, idC, 3, 4) != VOXEL_ROLE_SIGNPOST);
    /* D: house flood limits */
    CHECK(RoleAt(&r, &w, idD, 3, 9) == VOXEL_ROLE_WALL && RoleAt(&r, &w, idD, 3, 3) == VOXEL_ROLE_WALL);   /* 7 rows above the door */
    CHECK(RoleAt(&r, &w, idD, 3, 2) != VOXEL_ROLE_WALL);                                                    /* 8 rows: out */
    CHECK(RoleAt(&r, &w, idD, 8, 7) == VOXEL_ROLE_WALL && RoleAt(&r, &w, idD, 9, 7) != VOXEL_ROLE_WALL);   /* |dx| <= 5 */
    CHECK(RoleAt(&r, &w, idD, 8, 9) != VOXEL_ROLE_WALL);                                                    /* only reachable through the tree */
    CHECK(RoleAt(&r, &w, idD, 0, 9) == VOXEL_ROLE_WALL && RoleAt(&r, &w, idD, 2, 9) == VOXEL_ROLE_WALL);
    CHECK(RoleAt(&r, &w, idD, 4, 9) == VOXEL_ROLE_TREE);
    CHECK(RoleAt(&r, &w, idD, 5, 9) != VOXEL_ROLE_WALL);                                                    /* only reachable through foliage */
    CHECK(RoleAt(&r, &w, idD, 20, 9) != VOXEL_ROLE_WALL && RoleAt(&r, &w, idD, 9, 9) != VOXEL_ROLE_WALL);
    /* E: indoors: no signposts, a lone drawing is just a fence */
    CHECK(RoleAt(&r, &w, idE, 3, 3) == VOXEL_ROLE_FENCE);
    /* F: lamps */
    CHECK(RoleAt(&r, &w, idF, 3, 4) == VOXEL_ROLE_SIGNPOST);     /* free-standing lamp */
    CHECK(RoleAt(&r, &w, idF, 10, 4) == VOXEL_ROLE_SIGNPOST);    /* lamp against the house wall (lamp branch) */
    CHECK(RoleAt(&r, &w, idF, 14, 4) == VOXEL_ROLE_FENCE);       /* against a plain wall: no */
    CHECK(RoleAt(&r, &w, idF, 7, 3) == VOXEL_ROLE_WALL && RoleAt(&r, &w, idF, 8, 4) == VOXEL_ROLE_WALL);
    /* determinism: reversed pair order gives identical bytes */
    {
        RgRoles r2;
        CHECK(rg_roles_all(&w, &r2, true, NULL, NULL) == RG_OK);
        CHECK(r2.total == r.total && memcmp(r2.data, r.data, r.total) == 0);
        rg_roles_free(&r2);
    }
    rg_roles_free(&r);
    rg_world_close(&w);
    Free(A, 16); Free(B, 8); Free(C, 8); Free(D, 12); Free(E, 8); Free(F, 10);
    free(f.rom);
}

/* ---- real ROM invariants 1-9 ------------------------------------------------------------- */
static bool OpenPostCell(const RgLayout *L, int x, int y)
{
    return L->outdoor && rg_blocked(L, x, y) && !rg_blocked(L, x + 1, y) && !rg_blocked(L, x - 1, y) && !rg_blocked(L, x, y + 1);
}

static void TestRealRom(void)
{
    const char *path = getenv("ROMGEN_ROM");
    FILE *fp;
    uint8_t *rom;
    size_t n;
    RgWorld w;
    RgRoles r, r2;
    unsigned li, x, y, counts[11] = {0};
    unsigned rustSigns = 0, rustLampBranch = 0, rustNoEvent = 0, openSignChecked = 0;
    static const int kR104[5][2] = {{20, 50}, {27, 66}, {23, 5}, {7, 20}, {17, 23}};
    static const int kLit[4][2] = {{15, 13}, {6, 17}, {7, 8}, {12, 8}};

    if (!path || !(fp = fopen(path, "rb"))) { printf("SKIP real-ROM role invariants (set ROMGEN_ROM)\n"); return; }
    rom = (uint8_t *)malloc(0x2000000);
    n = fread(rom, 1, 0x2000000, fp);
    fclose(fp);
    CHECK(rg_world_open(&w, rom, n) == RG_OK);
    CHECK(rg_roles_all(&w, &r, false, NULL, NULL) == RG_OK);
    for (li = 0; li < w.layoutCount; li++) {
        const RgLayout *L = &w.layouts[li];
        const uint8_t *ro = rg_roles_of(&r, L->id);
        for (y = 0; y < L->h; y++) {
            for (x = 0; x < L->w; x++) {
                unsigned role = ro[y * L->w + x];
                bool blocked = rg_blocked(L, (int)x, (int)y);
                unsigned b = rg_behaviour(L, (int)x, (int)y);
                CHECK(role < 11 && role != VOXEL_ROLE_PROP);                                  /* 1 */
                counts[role]++;
                if (role == VOXEL_ROLE_FLOOR || role == VOXEL_ROLE_STAIR) CHECK(!blocked);   /* 2 */
                if (role == VOXEL_ROLE_SIGNPOST || role == VOXEL_ROLE_WALL || role == VOXEL_ROLE_TREE || role == VOXEL_ROLE_FENCE
                    || role == VOXEL_ROLE_CLIFF || role == VOXEL_ROLE_SHELF) CHECK(blocked);
                if (rg_is_water(b)) CHECK(role == VOXEL_ROLE_WATER);
                if (rg_is_jump(b) && !rg_is_water(b)) CHECK(role == VOXEL_ROLE_LEDGE);
                if (role == VOXEL_ROLE_SIGNPOST) CHECK(L->outdoor);                          /* 3 */
                if (role == VOXEL_ROLE_WALL) {                                               /* 8 */
                    unsigned k, ok = 0;
                    for (k = 0; k < L->warpCount; k++) {
                        int dx = L->warps[k].x, dy = L->warps[k].y;
                        if (rg_is_house_door(rg_behaviour(L, dx, dy)) && abs((int)x - dx) <= 5 && (int)y <= dy && (int)y >= dy - 7) ok = 1;
                    }
                    CHECK(ok);
                }
            }
        }
    }
    {   /* 4: Route 104 (layout 20): signs open on three sides are SIGNPOST at their event cells */
        const RgLayout *L = &w.layouts[19];
        unsigned k;
        for (k = 0; k < 5; k++) {
            if (OpenPostCell(L, kR104[k][0], kR104[k][1])) {
                CHECK(rg_roles_of(&r, 20)[kR104[k][1] * L->w + kR104[k][0]] == VOXEL_ROLE_SIGNPOST);
                openSignChecked++;
            }
        }
        CHECK(openSignChecked >= 1);
    }
    {   /* 5: Littleroot (layout 10): all four signs are SIGNPOST, the wall-backed (7,8) and (12,8) included */
        const RgLayout *L = &w.layouts[9];
        unsigned k;
        for (k = 0; k < 4; k++) CHECK(rg_roles_of(&r, 10)[kLit[k][1] * L->w + kLit[k][0]] == VOXEL_ROLE_SIGNPOST);
    }
    {   /* 6: Rustboro (layout 4): signposts without a sign event; one from the lamp branch */
        const RgLayout *L = &w.layouts[3];
        const uint8_t *ro = rg_roles_of(&r, 4);
        for (y = 0; y < L->h; y++)
            for (x = 0; x < L->w; x++) {
                if (ro[y * L->w + x] != VOXEL_ROLE_SIGNPOST) continue;
                rustSigns++;
                if (!rg_has_sign(L, (int)x, (int)y)) {
                    int wb = rg_blocked(L, (int)x - 1, (int)y), eb = rg_blocked(L, (int)x + 1, (int)y);
                    rustNoEvent++;
                    if (wb != eb && !rg_blocked(L, (int)x, (int)y + 1) && (wb ? ro[y * L->w + x - 1] : ro[y * L->w + x + 1]) == VOXEL_ROLE_WALL)
                        rustLampBranch++;
                }
            }
        CHECK(rustNoEvent >= 1);
        CHECK(rustLampBranch >= 1);
    }
    CHECK(rg_roles_all(&w, &r2, true, NULL, NULL) == RG_OK);   /* 9 */
    CHECK(r2.total == r.total && memcmp(r2.data, r.data, r.total) == 0);
    printf("real ROM roles: floor %u water %u ledge %u stair %u wall %u tree %u prop %u shelf %u fence %u cliff %u signpost %u\n",
           counts[0], counts[1], counts[2], counts[3], counts[4], counts[5], counts[6], counts[7], counts[8], counts[9], counts[10]);
    printf("  Rustboro: %u signposts, %u without an event, %u via the lamp branch; Route 104 open signs checked: %u\n",
           rustSigns, rustNoEvent, rustLampBranch, openSignChecked);
    rg_roles_free(&r);
    rg_roles_free(&r2);
    rg_world_close(&w);
    free(rom);
}

int main(void)
{
    TestSynthetic();
    TestRealRom();
    printf("test_romgen_roles: %d checks, %d failures\n", sChecks, sFails);
    return sFails ? 1 : 0;
}
