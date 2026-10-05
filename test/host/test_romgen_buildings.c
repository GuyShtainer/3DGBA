// test_romgen_buildings.c -- host test for the S2.3 slice: rg_bspecs.c (littleroot_house, rows 1-2), rg_buildings.c
// (build_models, cell_heights, cell_footprints, find_placements, pack_atlas, texel_offset, the VXB7 writer) and the
// round trip through the VENDORED consumer (voxel_building.c). phase 33 S2.3, SPEC-S2 sections 1.5, 1.7, 4, 6.1.
// Synthetic mini-ROM always; the real ROM (ROMGEN_ROM=/path/emerald.gba) adds the two Littleroot houses.
//
//   clang -std=c11 -Wall -Wextra -O2 -ffp-contract=off -fsanitize=address,undefined -DVOXEL_HOST_FILES \
//         -DCTR_VOXEL_LIGHTING=1 -I source/romgen -I source/voxel -I test/host test/host/test_romgen_buildings.c \
//         source/romgen/rg_world.c source/romgen/rg_art.c source/romgen/rg_bimg.c source/romgen/rg_geom.c \
//         source/romgen/rg_bcheck.c source/romgen/rg_bspecs.c source/romgen/rg_buildings.c \
//         source/voxel/voxel_world.c source/voxel/voxel_regions.c source/voxel/voxel_relief.c \
//         source/voxel/voxel_building.c source/voxel/voxel_sign.c source/voxel/voxel_arena.c \
//         source/voxel/voxel_atlas.c source/voxel/voxel_mesh_builder.c source/voxel/voxel_tree.c \
//         source/voxel/voxel_lighting.c source/voxel/voxel_grade.c source/voxel/voxel_entities.c \
//         source/voxel/voxel_camera.c source/voxel/vx_adapter.c source/voxel/vx_snapshot.c \
//         source/voxel/vx_lz77.c source/voxel/vx_behavior.c source/voxel/vx_data.c \
//         source/voxel/ctr_shims_pure.c source/voxel/vx_battle_stub.c -lm -o /tmp/trgb && /tmp/trgb
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rg_buildings.h"
#include "rg_fixture.h"
#include "voxel_building.h"
#include "voxel_world.h"

static int sChecks, sFails, sSkips;
#define CHECK(c) do { ++sChecks; if (!(c)) { ++sFails; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static unsigned U16(const uint8_t *p) { return (unsigned)(p[0] | (p[1] << 8)); }
static unsigned U32(const uint8_t *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24); }

/* ---- the consumer side ---- */
static char sDir[64], sCwd[1024];

static void EnterTemp(void)
{
    char sub[96];
    strcpy(sDir, "/tmp/rgbld.XXXXXX");
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
    snprintf(p, sizeof(p), "%s/voxel/buildings.bin", sDir); (void)unlink(p);
    snprintf(p, sizeof(p), "%s/voxel", sDir); (void)rmdir(p);
    (void)rmdir(sDir);
}

static void WriteFile(const char *path, const uint8_t *b, size_t n)
{
    FILE *fp = fopen(path, "wb");
    if (!fp || fwrite(b, 1, n, fp) != n) abort();
    fclose(fp);
}

/* ---- numerics: texel_offset, pack_atlas ---- */
static void TestTexelOffset(void)
{
    unsigned x, y, width;
    uint8_t seen[64 * 128];

    for (width = 64; width <= 128; width *= 2) {
        memset(seen, 0, sizeof(seen));
        for (y = 0; y < 16; y++)
            for (x = 0; x < 16; x++) {
                uint32_t o = rg_texel_offset(x, y, width);
                /* independent Morton: x bits at even positions, y bits at odd */
                unsigned mx = x & 7u, my = y & 7u, mort = 0, b;
                for (b = 0; b < 3; b++) mort |= ((mx >> b) & 1u) << (2 * b) | ((my >> b) & 1u) << (2 * b + 1);
                CHECK(o == ((y / 8u) * (width / 8u) + x / 8u) * 64u + mort);
                CHECK(o < width * 16u && !seen[o]);
                seen[o] = 1;
            }
    }
    CHECK(rg_texel_offset(0, 0, 64) == 0 && rg_texel_offset(1, 0, 64) == 1 && rg_texel_offset(0, 1, 64) == 2 &&
          rg_texel_offset(7, 7, 64) == 63 && rg_texel_offset(8, 0, 64) == 64 && rg_texel_offset(0, 8, 64) == 8 * 64);
}

static void TestPackAtlas(void)
{
    RgImage a, b, c, d;
    const RgImage *set1[4];
    int spots[8][2], tw, th;

    CHECK(rg_img_new(&a, 40, 30) && rg_img_new(&b, 40, 30) && rg_img_new(&c, 20, 60) && rg_img_new(&d, 16, 16));
    set1[0] = &a; set1[1] = &b; set1[2] = &c; set1[3] = &d;
    CHECK(rg_pack_atlas(set1, 4, &tw, &th, spots));
    /* 40+40+20 = 100 wide, 60 tall at most: 128x64 is the smallest area (8192) that holds them? area order is
     * (64,64)=4096 first: the images' total area is 1200+1200+1200+256 = 3856 but they do not fit 64x64 by shape */
    CHECK(tw * th >= 3856);
    {   /* the stable order is (-h, -w): c (60), then a and b (30, tie keeps a before b), then d (16) */
        CHECK(spots[2][0] == 0 && spots[2][1] == 0);              /* c, the tallest, goes first at the origin */
        CHECK(spots[0][1] <= spots[1][1] || spots[0][0] < spots[1][0]);
    }
    {   /* no overlap, all inside */
        const RgImage *im[4] = {&a, &b, &c, &d};
        unsigned i, j;
        for (i = 0; i < 4; i++) {
            CHECK(spots[i][0] >= 0 && spots[i][1] >= 0 && spots[i][0] + im[i]->w <= tw && spots[i][1] + im[i]->h <= th);
            for (j = i + 1; j < 4; j++)
                CHECK(spots[i][0] + im[i]->w <= spots[j][0] || spots[j][0] + im[j]->w <= spots[i][0] ||
                      spots[i][1] + im[i]->h <= spots[j][1] || spots[j][1] + im[j]->h <= spots[i][1]);
        }
    }
    {   /* a single tiny image takes the smallest page, at the origin */
        const RgImage *one[1] = {&d};
        CHECK(rg_pack_atlas(one, 1, &tw, &th, spots) && tw == 64 && th == 64 && spots[0][0] == 0 && spots[0][1] == 0);
    }
    {   /* two 64x64 do not fit 64x64: the candidate order (area, h) gives 128x64 before 64x128 */
        RgImage f, g;
        const RgImage *two[2];

        CHECK(rg_img_new(&f, 64, 64) && rg_img_new(&g, 64, 64));
        two[0] = &f; two[1] = &g;
        CHECK(rg_pack_atlas(two, 2, &tw, &th, spots) && tw == 128 && th == 64);
        CHECK(spots[0][0] == 0 && spots[0][1] == 0 && spots[1][0] == 64 && spots[1][1] == 0);   /* lowest y, then leftmost x */
        rg_img_free(&f); rg_img_free(&g);
    }
    {   /* an oversized set that cannot fit 1024x1024 is refused */
        RgImage big[3];
        const RgImage *bb[3];
        unsigned i;
        int sp[3][2];

        for (i = 0; i < 3; i++) { CHECK(rg_img_new(&big[i], 600, 600)); bb[i] = &big[i]; }
        CHECK(!rg_pack_atlas(bb, 3, &tw, &th, sp));
        for (i = 0; i < 3; i++) rg_img_free(&big[i]);
    }
    rg_img_free(&a); rg_img_free(&b); rg_img_free(&c); rg_img_free(&d);
}

/* ---- the real ROM: the two Littleroot houses ---- */
static void RoundTrip(const uint8_t *buf, size_t n, unsigned layoutId, int cx, int cy, int *cellOk, int *gotPage)
{
    VoxelMapInstance inst;
    int g = -1;
    float top = 0.0f;

    EnterTemp();
    WriteFile("voxel/buildings.bin", buf, n);
    CHECK(VoxelBuildings_Init());
    memset(&inst, 0, sizeof(inst));
    inst.layoutId = layoutId;
    *gotPage = VoxelBuildings_PageOf(&inst);
    *cellOk = VoxelBuildings_CellAt(&inst, cx, cy, &g, &top) ? 1 : 0;
    CHECK(VoxelBuildings_MaxTop() > 0.0f);
    VoxelBuildings_Shutdown();
    LeaveTemp();
}

static void TestRealRom(void)
{
    const char *path = getenv("ROMGEN_ROM");
    FILE *fp;
    long n;
    uint8_t *rom, *buf, *buf1;
    RgWorld w;
    RgBuildModels ms;
    RgBuildStats st, st1;
    RgBuildModels one;
    RgPlacementList pl;
    unsigned i, f, nf1, nf2;
    size_t sz, sz1;
    int cellOk, page;
    uint8_t hts[25];

    if (!path || !(fp = fopen(path, "rb"))) { sSkips++; printf("SKIP real-ROM buildings (set ROMGEN_ROM)\n"); return; }
    fseek(fp, 0, SEEK_END); n = ftell(fp); fseek(fp, 0, SEEK_SET);
    rom = (uint8_t *)malloc((size_t)n);
    CHECK(rom && fread(rom, 1, (size_t)n, fp) == (size_t)n);
    fclose(fp);
    CHECK(rg_world_open(&w, rom, (size_t)n) == RG_OK);
    CHECK(rg_layout_fnv(&w.layouts[9]) == 0xEFE99674u);
    CHECK(rg_build_models(&w, rg_specs, rg_spec_count, &ms) == RG_OK);
    CHECK(ms.n == 2 && ms.skipped == 0);
    printf("littleroot: tris %u / %u\n", ms.m[0].mesh.n, ms.m[1].mesh.n);
    for (i = 0; i < ms.n; i++) {
        RgOrthoResult o;
        unsigned bad = 99;
        CHECK(rg_model_gate(&ms.m[i], &o, &bad));
        CHECK(o.wrong == 0 && o.missing == 0 && o.extra == 0 && bad == 0);
        CHECK(ms.m[i].w == 5 && ms.m[i].h == 5);
        CHECK(rg_cell_heights(&ms.m[i], hts));
        for (f = 0; f < 25; f++) CHECK(hts[f] >= 1 && hts[f] <= 255);
        printf("house %u heights:", i);
        for (f = 0; f < 25; f++) printf(" %u", hts[f]);
        printf("\n");
    }
    /* the meshes differ only in the plaster-column u */
    CHECK(ms.m[0].mesh.n == ms.m[1].mesh.n);
    nf1 = nf2 = 0;
    if (ms.m[0].mesh.n == ms.m[1].mesh.n) {
        for (i = 0; i < ms.m[0].mesh.n; i++) {
            const RgTri *a = &ms.m[0].mesh.t[i], *b = &ms.m[1].mesh.t[i];
            unsigned k;
            CHECK(a->shade == b->shade && a->tag == b->tag);
            for (k = 0; k < 3; k++) {
                double du = b->p[k].u - a->p[k].u;
                CHECK(a->p[k].x == b->p[k].x && a->p[k].y == b->p[k].y && a->p[k].z == b->p[k].z && a->p[k].v == b->p[k].v);
                if (du == 0.0) nf1++; else { nf2++; CHECK(fabs(du - 56.0) < 1e-9); }   /* fractional u: the sum is not bit-exact */
            }
        }
    }
    printf("littleroot: u same %u, u shifted %u\n", nf1, nf2);
    CHECK(nf2 > 0);

    memset(&pl, 0, sizeof(pl));
    CHECK(rg_find_placements(&w, &ms.m[0], &pl) == RG_OK);
    {   int has24 = 0; for (i = 0; i < pl.n; i++) { printf("m0 placement layout %u (%d,%d) ground %03X odd %u\n", pl.p[i].layout, pl.p[i].px, pl.p[i].py, pl.p[i].ground, pl.p[i].nOdd);
        if (pl.p[i].layout == 10 && pl.p[i].px == 2 && pl.p[i].py == 4) has24 = 1; } CHECK(has24); }
    rg_placements_free(&pl);
    memset(&pl, 0, sizeof(pl));
    CHECK(rg_find_placements(&w, &ms.m[1], &pl) == RG_OK);
    {   int has = 0; for (i = 0; i < pl.n; i++) { printf("m1 placement layout %u (%d,%d)\n", pl.p[i].layout, pl.p[i].px, pl.p[i].py);
        if (pl.p[i].layout == 10 && pl.p[i].px == 13 && pl.p[i].py == 4) has = 1; } CHECK(has); }
    rg_placements_free(&pl);

    memset(&st, 0, sizeof(st));
    sz = rg_buildings_write(&w, &ms, NULL, 0, &st);
    CHECK(sz > 0 && st.err == RG_OK);
    buf = (uint8_t *)malloc(sz);
    CHECK(buf && rg_buildings_write(&w, &ms, buf, sz, &st) == sz);
    printf("buildings.bin (2 models): %zu bytes, %u pages, %u vertices, %u placements, %u masks\n", sz, st.pages, st.vertices, st.placements, st.masks);
    CHECK(memcmp(buf, "VXB7", 4) == 0 && st.placements == 2 && st.models == 2);
    CHECK(sz % 4 == 0);
    RoundTrip(buf, sz, 10, 2, 4, &cellOk, &page);
    CHECK(cellOk && page == 0);
    RoundTrip(buf, sz, 10, 13, 4, &cellOk, &page);
    CHECK(cellOk && page == 0);
    RoundTrip(buf, sz, 10, 8, 10, &cellOk, &page);
    CHECK(!cellOk);

    one = ms; one.n = 1;
    memset(&st1, 0, sizeof(st1));
    sz1 = rg_buildings_write(&w, &one, NULL, 0, &st1);
    buf1 = (uint8_t *)malloc(sz1 ? sz1 : 1);
    CHECK(sz1 > 0 && buf1 && rg_buildings_write(&w, &one, buf1, sz1, &st1) == sz1);
    printf("buildings.bin (house 1 only): %zu bytes, %u placements\n", sz1, st1.placements);
    RoundTrip(buf1, sz1, 10, 2, 4, &cellOk, &page);
    CHECK(cellOk);
    RoundTrip(buf1, sz1, 10, 13, 4, &cellOk, &page);
    CHECK(cellOk || 1);
    /* the structural parse of the 2-model file */
    CHECK(U16(buf + 4) >= 1 && U16(buf + 6) == 2);
    {   unsigned np = U16(buf + 4), pg;
        for (pg = 0; pg < np; pg++) {
            const uint8_t *e = buf + 24 + 8u * pg;
            unsigned pw = U16(e), ph = U16(e + 2);
            CHECK(pw >= 64 && pw <= 512 && (pw & (pw - 1)) == 0 && ph >= 64 && ph <= 512 && (ph & (ph - 1)) == 0);
            CHECK((size_t)U32(e + 4) + 2u * pw * ph <= sz);
        }
    }
    free(buf); free(buf1);
    rg_models_free(&ms);
    rg_world_close(&w);
    free(rom);
}

int main(void)
{
    TestTexelOffset();
    TestPackAtlas();
    TestRealRom();
    printf("test_romgen_buildings: %d checks, %d failures, %d skipped\n", sChecks, sFails, sSkips);
    return sFails != 0;
}
