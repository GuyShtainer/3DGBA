// test_romgen_signs.c -- host test for source/romgen/rg_signs.c (phase 33 S1.3, SPEC-S0-S1 sections 5, 7.4):
// signposts.bin (VXS2) and regions.bin round-trip through the VENDORED consumers (voxel_sign.c, voxel_regions.c).
// Synthetic mini-ROM always; the real ROM (ROMGEN_ROM=/path/emerald.gba) adds the full file.
//
//   clang -std=c11 -Wall -Wextra -O2 -ffp-contract=off -fsanitize=address,undefined -DVOXEL_HOST_FILES \
//         -DCTR_VOXEL_LIGHTING=1 -I source/romgen -I source/voxel -I test/host test/host/test_romgen_signs.c \
//         source/romgen/rg_world.c source/romgen/rg_art.c source/romgen/rg_roles.c source/romgen/rg_regions.c \
//         source/romgen/rg_signs.c \
//         source/voxel/voxel_world.c source/voxel/voxel_regions.c source/voxel/voxel_relief.c \
//         source/voxel/voxel_building.c source/voxel/voxel_sign.c source/voxel/voxel_arena.c \
//         source/voxel/voxel_atlas.c source/voxel/voxel_mesh_builder.c source/voxel/voxel_tree.c \
//         source/voxel/voxel_lighting.c source/voxel/voxel_daylight.c source/voxel/voxel_grade.c source/voxel/voxel_entities.c \
//         source/voxel/voxel_camera.c source/voxel/vx_adapter.c source/voxel/vx_snapshot.c \
//         source/voxel/vx_lz77.c source/voxel/vx_behavior.c source/voxel/vx_data.c \
//         source/voxel/ctr_shims_pure.c source/voxel/vx_battle_stub.c -lm -o /tmp/trgs && /tmp/trgs
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rg_regions.h"
#include "rg_signs.h"
#include "rg_fixture.h"
#include "voxel_regions.h"
#include "voxel_sign.h"
#include "voxel_world.h"

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
static unsigned PxBlank(unsigned x, unsigned y) { (void)x; (void)y; return 0; }
static unsigned PxGrey(unsigned x, unsigned y) { (void)x; (void)y; return 5; }
static unsigned PxA(unsigned x, unsigned y) { (void)x; (void)y; return 7; }
static unsigned PxB(unsigned x, unsigned y) { (void)x; (void)y; return 8; }
static unsigned PxPost(unsigned x, unsigned y) { (void)y; return (x == 3 || x == 4) ? 6 : 5; }
static unsigned PxFull(unsigned x, unsigned y) { (void)x; (void)y; return 2; }
static unsigned PxCol17(unsigned x, unsigned y) { (void)y; return x >= 1 ? 2 : 0; }
static unsigned PxCol05(unsigned x, unsigned y) { (void)y; return x <= 5 ? 2 : 0; }
static unsigned PxCol06(unsigned x, unsigned y) { (void)y; return x <= 6 ? 2 : 0; }

enum { T_BLANK, T_GREY, T_A, T_B, T_POST, T_FULL, T_C17, T_C05, T_C06, T_COUNT };
enum { M_VOID, M_FLOOR, M_SIGN, M_HEAD, M_A, M_B, M_WALL, M_HW15, M_HBIG, M_HOK, M_HFAR, M_COUNT };
#define ENT(tile) (uint16_t)((tile) | (1 << 12))

static uint16_t sPal[256], sMt[M_COUNT * 8], sAtt[M_COUNT];
static uint8_t sTiles[T_COUNT * 32];

static void Meta(unsigned m, const unsigned l0[4], const unsigned l1[4])
{
    unsigned q;
    for (q = 0; q < 4; q++) {
        sMt[m * 8 + q] = ENT(l0[q]);
        sMt[m * 8 + 4 + q] = ENT(l1[q]);
    }
}

static void BuildTileset(void)
{
    static const unsigned bl[4] = {T_BLANK, T_BLANK, T_BLANK, T_BLANK};
#define Q4(t) {t, t, t, t}
    static const unsigned grey[4] = Q4(T_GREY), a[4] = Q4(T_A), b[4] = Q4(T_B), post[4] = Q4(T_POST);
    static const unsigned head[4] = {T_BLANK, T_BLANK, T_FULL, T_BLANK};
    static const unsigned hw15[4] = {T_BLANK, T_BLANK, T_FULL, T_C06};
    static const unsigned hbig[4] = {T_C17, T_C05, T_C17, T_C05};
    static const unsigned hok[4] = {T_BLANK, T_BLANK, T_C17, T_C05};
    static const unsigned hfar[4] = {T_BLANK, T_BLANK, T_BLANK, T_C05};

    memset(sPal, 0, sizeof(sPal));
    sPal[16 + 1] = 0x03E0; sPal[16 + 2] = 0x001F; sPal[16 + 3] = 0x7FFF; sPal[16 + 4] = 0x0000;
    sPal[16 + 5] = 0x5294; sPal[16 + 6] = 0x01D7; sPal[16 + 7] = 0x7C00; sPal[16 + 8] = 0x03FF;
    MakeTile(sTiles + T_BLANK * 32, PxBlank); MakeTile(sTiles + T_GREY * 32, PxGrey); MakeTile(sTiles + T_A * 32, PxA);
    MakeTile(sTiles + T_B * 32, PxB); MakeTile(sTiles + T_POST * 32, PxPost); MakeTile(sTiles + T_FULL * 32, PxFull);
    MakeTile(sTiles + T_C17 * 32, PxCol17); MakeTile(sTiles + T_C05 * 32, PxCol05); MakeTile(sTiles + T_C06 * 32, PxCol06);
    Meta(M_VOID, bl, bl); Meta(M_FLOOR, grey, bl); Meta(M_SIGN, post, bl); Meta(M_HEAD, grey, head);
    Meta(M_A, a, bl); Meta(M_B, b, bl); Meta(M_WALL, grey, bl);
    Meta(M_HW15, grey, hw15); Meta(M_HBIG, grey, hbig); Meta(M_HOK, grey, hok); Meta(M_HFAR, grey, hfar);
}

/* ---- the consumer side ------------------------------------------------------------------- */
static char sDir[64], sCwd[1024];

static void EnterTemp(void)
{
    char sub[96];
    strcpy(sDir, "/tmp/rgsig.XXXXXX");
    if (!mkdtemp(sDir)) abort();
    if (!getcwd(sCwd, sizeof(sCwd))) abort();
    snprintf(sub, sizeof(sub), "%s/voxel", sDir);
    if (mkdir(sub, 0755) != 0) abort();
    if (chdir(sDir) != 0) abort();
}

static void LeaveTemp(void)
{
    char p[128];
    if (chdir(sCwd) != 0) abort();
    snprintf(p, sizeof(p), "%s/voxel/regions.bin", sDir); (void)unlink(p);
    snprintf(p, sizeof(p), "%s/voxel/signposts.bin", sDir); (void)unlink(p);
    snprintf(p, sizeof(p), "%s/voxel", sDir); (void)rmdir(p);
    (void)rmdir(sDir);
}

static void WriteFile(const char *path, const uint8_t *b, size_t n)
{
    FILE *fp = fopen(path, "wb");
    if (!fp || fwrite(b, 1, n, fp) != n) abort();
    fclose(fp);
}

/* Roles + signs of the whole world, pair by pair, through the same calls rg_run will make. */
static void Generate(const RgWorld *w, RgRoles *r, RgSignList *list)
{
    unsigned pi, li;
    CHECK(rg_roles_init(w, r) == RG_OK);
    for (pi = 0; pi < w->pairCount; pi++) {
        RgPair *p = rg_pair_open(w, (uint16_t)pi);
        CHECK(p != NULL);
        for (li = 0; li < w->layoutCount; li++) {
            const RgLayout *L = &w->layouts[li];
            if (!L->present || L->pairIndex != pi) continue;
            CHECK(rg_roles_layout(w, r, p, L, r->data + r->off[li]));
            CHECK(rg_signs_layout(p, L, r->data + r->off[li], list));
        }
        rg_pair_close(p);
    }
    rg_signs_finish(list);
}

/* Writes both files, loads them in the vendored consumers and checks every record. Returns records checked. */
static unsigned RoundTrip(const RgWorld *w, const RgRoles *r, const RgSignList *list)
{
    size_t rs = rg_regions_write(w, r, NULL, 0), ss = rg_signs_write(list, NULL, 0);
    uint8_t *rb = (uint8_t *)malloc(rs), *sb = (uint8_t *)malloc(ss);
    unsigned i, checked = 0, k;

    CHECK(rs > 0 && ss == 8 + 72 * list->count);
    CHECK(rg_regions_write(w, r, rb, rs) == rs && rg_signs_write(list, sb, ss) == ss);
    CHECK(ss == 0 || rg_signs_write(list, sb, ss - 1) == 0);
    EnterTemp();
    WriteFile("voxel/regions.bin", rb, rs);
    WriteFile("voxel/signposts.bin", sb, ss);
    CHECK(VoxelRegions_Init());
    VoxelSign_Init();
    for (i = 0; i < list->count; i++) {
        const RgSignRec *rec = &list->rec[i];
        VoxelMapInstance inst;
        unsigned any = 0;
        int m = -1;
        memset(&inst, 0, sizeof(inst));
        inst.layoutId = rec->layout;
        CHECK(VoxelSign_IsCell(&inst, rec->x, rec->y));
        for (k = 0; k < 16; k++) any |= rec->head[k];
        if (any) {
            CHECK(VoxelSign_HeadGround(&inst, rec->x, rec->y - 1, &m) && m == rec->headGround);
        } else {
            CHECK(!VoxelSign_HeadGround(&inst, rec->x, rec->y - 1, &m));
        }
        checked++;
    }
    /* a cell that is not a record is not a sign cell */
    {
        VoxelMapInstance inst;
        memset(&inst, 0, sizeof(inst));
        inst.layoutId = list->count ? list->rec[0].layout : 1;
        CHECK(!VoxelSign_IsCell(&inst, 0, 0) || (list->rec[0].x == 0 && list->rec[0].y == 0));
        CHECK(!VoxelSign_IsCell(&inst, 5000, 5000));
    }
    VoxelSign_Shutdown();
    VoxelRegions_Shutdown();
    LeaveTemp();
    free(rb);
    free(sb);
    return checked;
}

static void TestUnits(RgPair *p, const RgLayout *L)
{
    uint16_t solid[16], out[16], rows[16], head[16];
    unsigned y;
    int x;

    /* cutout_mask: a ring with a hole is filled; a solid border pixel stays; empty gives 0; an open ring leaks */
    memset(solid, 0, sizeof(solid));
    for (x = 4; x <= 11; x++) { solid[4] |= (uint16_t)(1u << x); solid[11] |= (uint16_t)(1u << x); }
    for (y = 4; y <= 11; y++) { solid[y] |= (uint16_t)((1u << 4) | (1u << 11)); }
    rg_cutout_mask(solid, out);
    CHECK(out[7] == 0x0FF0 && out[4] == 0x0FF0 && out[3] == 0 && out[12] == 0);   /* hole filled: x 4..11 */
    solid[4] &= (uint16_t)~(1u << 7);                                              /* open the ring */
    rg_cutout_mask(solid, out);
    CHECK(out[7] == 0x0810 && out[5] == 0x0810);                                   /* interior leaked outside: only the walls */
    memset(solid, 0, sizeof(solid));
    rg_cutout_mask(solid, out);
    { unsigned any = 0; for (y = 0; y < 16; y++) any |= out[y]; CHECK(any == 0); }
    solid[0] = 0x0001; solid[15] = 0x8000;                                         /* solid border pixels */
    rg_cutout_mask(solid, out);
    CHECK(out[0] == 0x0001 && out[15] == 0x8000 && out[1] == 0);

    /* head_mask rejections and the accepted case */
    for (y = 0; y < 16; y++) rows[y] = 0;
    rows[0] = 0x0018;   /* bits 3,4 */
    rg_head_mask(p, rows, M_HEAD, head);
    CHECK(head[8] == 0x00FF && head[15] == 0x00FF && head[7] == 0);                /* accepted: quad 2 */
    rows[0] = 0xFFFF;                                                              /* popcount 16 > 14 */
    rg_head_mask(p, rows, M_HEAD, head);
    { unsigned any = 0; for (y = 0; y < 16; y++) any |= head[y]; CHECK(any == 0); }
    rows[0] = 0x3FFF;                                                              /* popcount 14: allowed */
    rg_head_mask(p, rows, M_HEAD, head);
    CHECK(head[15] == 0x00FF);
    rows[0] = 0x00F0;
    rg_head_mask(p, rows, M_HW15, head);                                           /* width 15 */
    { unsigned any = 0; for (y = 0; y < 16; y++) any |= head[y]; CHECK(any == 0); }
    rg_head_mask(p, rows, M_HBIG, head);                                           /* 208 pixels, width 13 */
    { unsigned any = 0; for (y = 0; y < 16; y++) any |= head[y]; CHECK(any == 0); }
    rg_head_mask(p, rows, M_HOK, head);                                            /* 104 pixels, width 13: accepted */
    CHECK(head[8] == 0x3FFE && head[15] == 0x3FFE && head[0] == 0);
    rows[0] = 0x0001;                                                              /* seed needs bit at x-1..x+1: x 0..1 not drawn */
    rg_head_mask(p, rows, M_HFAR, head);
    { unsigned any = 0; for (y = 0; y < 16; y++) any |= head[y]; CHECK(any == 0); }
    rg_head_mask(p, rows, M_FLOOR, head);                                          /* nothing on the upper layer */
    { unsigned any = 0; for (y = 0; y < 16; y++) any |= head[y]; CHECK(any == 0); }
    (void)L;
}

static void TestSynthetic(void)
{
    RgFx f;
    RgWorld w;
    RgRoles r;
    RgSignList list;
    uint32_t ts;
    uint16_t *b;
    int x, y;
    unsigned idS, idI, idT, idU, idV;
    int16_t none[1][2] = {{0, 0}};
    uint16_t sS[2][3] = {{5, 5, 0}, {8, 6, 0}};
    RgPair *p;
    const RgSignRec *r0, *r1;

    BuildTileset();
    fxr_init(&f);
    ts = fxr_tileset(&f, 0, 0, sTiles, sizeof(sTiles), sPal, sMt, sAtt, M_COUNT, 1);

    /* S: 12x10 floor, two signs with events: a plain post and a lamp (head cell (8,5)) */
    b = (uint16_t *)malloc(12 * 10 * 2);
    for (y = 0; y < 10; y++) for (x = 0; x < 12; x++) b[y * 12 + x] = M_FLOOR;
    b[5 * 12 + 5] = M_SIGN | (1 << 10);
    b[6 * 12 + 8] = M_SIGN | (1 << 10);
    b[5 * 12 + 8] = M_HEAD;
    idS = fxr_layout(&f, 12, 10, b, ts, 0);
    (void)fxr_map(&f, 0, MAP_TYPE_ROUTE, idS, none, 0, sS, 2);
    /* I: the same cells indoors: no records at all */
    idI = fxr_layout(&f, 12, 10, b, ts, 0);
    (void)fxr_map(&f, 0, MAP_TYPE_INDOOR, idI, none, 0, sS, 2);
    free(b);
    /* T: head_ground cases (7x7): head cell (3,3) over the sign (3,4); a neighbour pattern with A:2 B:2, A first */
    b = (uint16_t *)malloc(7 * 7 * 2);
    for (y = 0; y < 7; y++) for (x = 0; x < 7; x++) b[y * 7 + x] = M_WALL | (1 << 10);
    b[3 * 7 + 3] = M_HEAD; b[4 * 7 + 3] = M_SIGN | (1 << 10);
    b[2 * 7 + 2] = M_A; b[2 * 7 + 3] = M_B; b[2 * 7 + 4] = M_A; b[3 * 7 + 2] = M_B;   /* scan order: A, B, A, B */
    idT = fxr_layout(&f, 7, 7, b, ts, 0);
    /* U: B wins by count (A:1, B:3) although A is inserted first */
    b[2 * 7 + 2] = M_A; b[2 * 7 + 3] = M_B; b[2 * 7 + 4] = M_B; b[3 * 7 + 2] = M_B;
    idU = fxr_layout(&f, 7, 7, b, ts, 0);
    /* V: nothing usable around the head: fallback to the cell two below it; head at the bottom edge gives 0 */
    for (y = 0; y < 7; y++) for (x = 0; x < 7; x++) b[y * 7 + x] = M_WALL | (1 << 10);
    b[3 * 7 + 3] = M_HEAD; b[4 * 7 + 3] = M_SIGN | (1 << 10); b[5 * 7 + 3] = M_A | (1 << 10);
    idV = fxr_layout(&f, 7, 7, b, ts, 0);
    free(b);
    (void)fxr_map(&f, 0, MAP_TYPE_ROUTE, idT, none, 0, NULL, 0);
    (void)fxr_map(&f, 0, MAP_TYPE_ROUTE, idU, none, 0, NULL, 0);
    (void)fxr_map(&f, 0, MAP_TYPE_ROUTE, idV, none, 0, NULL, 0);
    fxr_finish(&f);
    CHECK(rg_world_open(&w, f.rom, FXR_ROM_SIZE) == RG_OK);

    p = rg_pair_open(&w, w.layouts[idS - 1].pairIndex);
    TestUnits(p, &w.layouts[idS - 1]);
    /* head_ground */
    CHECK(rg_head_ground(p, &w.layouts[idT - 1], 3, 3) == M_A);    /* tie A:2 B:2: the first inserted (A) wins */
    CHECK(rg_head_ground(p, &w.layouts[idU - 1], 3, 3) == M_B);    /* most frequent wins */
    CHECK(rg_head_ground(p, &w.layouts[idV - 1], 3, 3) == M_A);    /* fallback: metatile of the cell two below (3,5) */
    CHECK(rg_head_ground(p, &w.layouts[idV - 1], 3, 5) == 0);      /* two below is off the map? (3,7): yes -> 0 */
    {   /* an exact match beats counting: own layer 0 equals the first neighbour that has no layer 1 */
        CHECK(rg_head_ground(p, &w.layouts[idS - 1], 8, 5) == M_FLOOR);
    }
    rg_pair_close(p);

    memset(&list, 0, sizeof(list));
    Generate(&w, &r, &list);
    CHECK(list.count == 2);
    r0 = &list.rec[0]; r1 = &list.rec[1];
    CHECK(r0->layout == idS && r0->x == 5 && r0->y == 5 && r1->layout == idS && r1->x == 8 && r1->y == 6);   /* sorted (layout, y, x) */
    { unsigned k, ok = 1; for (k = 0; k < 16; k++) ok &= r0->rows[k] == 0x1818 && r1->rows[k] == 0x1818; CHECK(ok); }   /* post columns 3,4 / 11,12 */
    { unsigned k, ok = 1; for (k = 0; k < 16; k++) ok &= r0->head[k] == 0; CHECK(ok && r0->headGround == 0); }
    CHECK(r1->head[8] == 0x00FF && r1->head[15] == 0x00FF && r1->head[7] == 0 && r1->headGround == M_FLOOR);
    CHECK(rg_signs_with_head(&list) == 1);
    CHECK(RoundTrip(&w, &r, &list) == 2);
    rg_signs_free(&list);
    rg_roles_free(&r);
    CHECK(rg_signs_write(&list, NULL, 0) == 0);   /* no records: no file */
    rg_world_close(&w);
    free(f.rom);
}

static void TestRealRom(void)
{
    const char *path = getenv("ROMGEN_ROM");
    FILE *fp;
    uint8_t *rom;
    size_t n;
    RgWorld w;
    RgRoles r;
    RgSignList list;
    unsigned i, rustHeads = 0, zeroRows = 0, ok = 1, outdoorRoles = 0;

    if (!path || !(fp = fopen(path, "rb"))) { printf("SKIP real-ROM signposts round trip (set ROMGEN_ROM)\n"); return; }
    rom = (uint8_t *)malloc(0x2000000);
    n = fread(rom, 1, 0x2000000, fp);
    fclose(fp);
    CHECK(rg_world_open(&w, rom, n) == RG_OK);
    memset(&list, 0, sizeof(list));
    Generate(&w, &r, &list);
    CHECK(list.count >= 1 && list.count <= 65535);
    for (i = 0; i < list.count; i++) {
        unsigned k, any = 0, anyRows = 0;
        const RgSignRec *rec = &list.rec[i];
        if (i > 0) { const RgSignRec *q = &list.rec[i - 1]; ok &= (q->layout < rec->layout) || (q->layout == rec->layout && (q->y < rec->y || (q->y == rec->y && q->x < rec->x))); }
        for (k = 0; k < 16; k++) { any |= rec->head[k]; anyRows |= rec->rows[k]; }
        zeroRows += anyRows == 0;
        if (rec->layout == 4 && any) rustHeads++;
        outdoorRoles += w.layouts[rec->layout - 1].outdoor;
    }
    CHECK(ok);                              /* strictly sorted by (layout, y, x) */
    CHECK(zeroRows == 4);                   /* SPEC 7.4 expected 0: 4 un-evented posts draw only ground colours (BUILDLOG, spec error) */
    CHECK(outdoorRoles == list.count);
    CHECK(rustHeads >= 1);                  /* Rustboro's lamps have lanterns */
    CHECK(RoundTrip(&w, &r, &list) == list.count);
    printf("real ROM: signposts.bin %zu bytes, %zu records (%u with a head, %u in Rustboro, %u with an empty mask)\n",
           rg_signs_write(&list, NULL, 0), list.count, rg_signs_with_head(&list), rustHeads, zeroRows);
    rg_signs_free(&list);
    rg_roles_free(&r);
    rg_world_close(&w);
    free(rom);
}

int main(void)
{
    TestSynthetic();
    TestRealRom();
    printf("test_romgen_signs: %d checks, %d failures\n", sChecks, sFails);
    return sFails ? 1 : 0;
}
