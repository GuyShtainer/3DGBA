// test_romgen_export.c -- host test for the S2.7 slice: the full buildings.bin through rg_run (wantBuildings), an
// independent parse of every SPEC-S2 section 4.2 extra check, the consumer round trip, the pinned counts and the
// two-pass sizing / size guards. phase 33 S2.7, SPEC-S2 sections 1.8, 4, 4.2, 6.1.
// Needs the ROM (ROMGEN_ROM=/path/emerald.gba, exported); without it only the synthetic guard check runs.
//
//   clang -std=c11 -Wall -Wextra -O2 -ffp-contract=off -fsanitize=address,undefined -DVOXEL_HOST_FILES \
//         -DCTR_VOXEL_LIGHTING=1 -I source/romgen -I source/voxel -I test/host test/host/test_romgen_export.c \
//         source/romgen/rg_*.c \
//         source/voxel/voxel_world.c source/voxel/voxel_regions.c source/voxel/voxel_relief.c \
//         source/voxel/voxel_building.c source/voxel/voxel_sign.c source/voxel/voxel_arena.c \
//         source/voxel/voxel_atlas.c source/voxel/voxel_mesh_builder.c source/voxel/voxel_tree.c \
//         source/voxel/voxel_lighting.c source/voxel/voxel_daylight.c source/voxel/voxel_grade.c source/voxel/voxel_entities.c \
//         source/voxel/voxel_camera.c source/voxel/vx_adapter.c source/voxel/vx_snapshot.c \
//         source/voxel/vx_lz77.c source/voxel/vx_behavior.c source/voxel/vx_data.c \
//         source/voxel/ctr_shims_pure.c source/voxel/vx_battle_stub.c -lm -o /tmp/trgx && /tmp/trgx
// (rg_run.c is included in rg_*.c; add -DRG_PYSUM_COMPENSATED=0 for the second summation mode.)
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "rg_run.h"
#include "voxel_building.h"
#include "voxel_world.h"

static int sChecks, sFails, sSkips;
#define CHECK(c) do { ++sChecks; if (!(c)) { ++sFails; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static double NowMs(void) { return (double)clock() * 1000.0 / CLOCKS_PER_SEC; }

static uint32_t U16(const uint8_t *b, size_t o) { return (uint32_t)b[o] | ((uint32_t)b[o + 1] << 8); }
static uint32_t U32(const uint8_t *b, size_t o) { return U16(b, o) | (U16(b, o + 2) << 16); }
static int32_t S16(const uint8_t *b, size_t o) { uint32_t v = U16(b, o); return (int32_t)(v >= 0x8000u ? (int32_t)v - 0x10000 : (int32_t)v); }
static float F32(const uint8_t *b, size_t o) { uint32_t v = U32(b, o); float f; memcpy(&f, &v, 4); return f; }

/* ---- every SPEC-S2 4.2 extra check, from the bytes alone ---- */
static void Parse(const uint8_t *b, size_t sz)
{
    uint32_t P, M, PM, PL, HB, MK, V, VAR;
    size_t o, vtx, tex, pageT, modelT, pmT, plT, hT, fT, mT, qT, varT;
    uint32_t i, j, prevLay = 0, cellSum = 0, maxLay = 0, patchVerts = 0;
    uint64_t texTotal = 0;
    unsigned layouts442 = 0;

    CHECK(sz >= 24 && memcmp(b, "VXB7", 4) == 0 && sz % 4 == 0);
    P = U16(b, 4); M = U16(b, 6); PM = U16(b, 8); PL = U16(b, 10); HB = U16(b, 12); MK = U16(b, 14);
    V = U32(b, 16); VAR = U16(b, 20);
    CHECK(U16(b, 22) == 0);
    CHECK(P > 0 && P <= 256 && VAR <= 128);
    pageT = 24; modelT = pageT + 8u * P; pmT = modelT + 16u * M; plT = pmT + 8u * PM; hT = plT + 16u * PL;
    fT = hT + HB + (HB & 1u); mT = fT + 2u * HB; qT = mT + 32u * MK; varT = qT + HB + (HB & 1u);
    o = varT + 6u * VAR;
    vtx = o + ((4u - (o & 3u)) & 3u);
    tex = vtx + 24u * (size_t)V;
    CHECK(tex <= sz);
    if (tex > sz) return;
    /* pads are zero */
    CHECK(HB % 2 == 0 || (b[hT + HB] == 0 && b[qT + HB] == 0));
    for (i = (uint32_t)o; i < vtx; i++) CHECK(b[i] == 0);
    /* page table: POT within 512, offsets contiguous, every page inside the file, nothing after the last */
    for (i = 0; i < P; i++) {
        uint32_t w = U16(b, pageT + 8u * i), h = U16(b, pageT + 8u * i + 2), off = U32(b, pageT + 8u * i + 4);
        /* POT, atlas dims <= 1024 (gen:1216-1218), area <= 512x512 texels: page 3 is 1024x256 upstream-faithfully */
        CHECK(w >= 8 && h >= 8 && w <= 1024 && h <= 1024 && (w & (w - 1)) == 0 && (h & (h - 1)) == 0 && w * h <= 512u * 512u);
        CHECK(off == tex + texTotal);
        CHECK((uint64_t)off + 2u * w * h <= sz);
        texTotal += 2ull * w * h;
    }
    CHECK(tex + texTotal == sz);
    /* models */
    for (i = 0; i < M; i++) {
        size_t r = modelT + 16u * i;
        uint32_t w = b[r], h = b[r + 1], fv = U32(b, r + 4), vc = U32(b, r + 8), hs = U32(b, r + 12);
        CHECK(w > 0 && h > 0 && (uint64_t)fv + vc <= V && hs + w * h <= HB);
        CHECK(vc % 3 == 0);
        cellSum += w * h;
    }
    CHECK(cellSum == HB);
    /* page models */
    for (i = 0; i < PM; i++) {
        CHECK(U16(b, pmT + 8u * i) < M && U16(b, pmT + 8u * i + 2) < P);
        CHECK(abs(S16(b, pmT + 8u * i + 4)) < 4096 && abs(S16(b, pmT + 8u * i + 6)) < 4096);
    }
    /* placements: sorted by layout (the consumer binary-searches), indices and extra ranges in bounds */
    for (i = 0; i < PL; i++) {
        size_t r = plT + 16u * i;
        uint32_t lay = U16(b, r), pm = U16(b, r + 2), ec = U16(b, r + 10), ef = U32(b, r + 12);
        CHECK(lay >= prevLay && lay >= 1 && lay <= 442);
        CHECK(pm < PM && (uint64_t)ef + ec <= V && ec % 6 == 0);
        prevLay = lay; if (lay > maxLay) maxLay = lay; layouts442 += lay == 442;
        patchVerts += ec;
        for (j = 0; j < ec; j++) {   /* patch quads: cell units, y 0.01, finite uv, shade 1 */
            size_t v = vtx + 24u * (ef + j);
            CHECK(fabsf(F32(b, v + 4) - 0.01f) < 1e-6f && F32(b, v + 20) == 1.0f);
        }
    }
    CHECK(maxLay <= 442);
    /* footprints: 0xFFFF or a real mask */
    for (i = 0; i < HB; i++) { uint32_t f = U16(b, fT + 2u * i); CHECK(f == 0xFFFFu || f < MK); }
    /* masks: rows hold 16 bits; heights: 255 = unowned; quarters: a nibble worth of bits */
    for (i = 0; i < HB; i++) CHECK(b[qT + i] <= 0x0F);
    /* variants: sorted by (layout, metatile, quarters), unique, quarters 1..15 */
    for (i = 0; i < VAR; i++) {
        size_t r = varT + 6u * i;
        uint64_t key = ((uint64_t)U16(b, r) << 24) | ((uint64_t)U16(b, r + 2) << 8) | b[r + 4];
        CHECK(U16(b, r) >= 1 && U16(b, r) <= 442 && b[r + 4] >= 1 && b[r + 4] <= 15 && b[r + 5] == 0);
        if (i > 0) {
            size_t p = r - 6;
            uint64_t pk = ((uint64_t)U16(b, p) << 24) | ((uint64_t)U16(b, p + 2) << 8) | b[p + 4];
            CHECK(pk < key);
        }
    }
    /* vertices: finite, positive shade */
    for (i = 0; i < V; i++) {
        size_t v = vtx + 24u * i;
        unsigned k;
        for (k = 0; k < 6; k++) CHECK(isfinite(F32(b, v + 4u * k)));
        CHECK(F32(b, v + 20) > 0.0f);
    }
    printf("parse: pages %u models %u pageModels %u placements %u (layout 442: %u, max layout %u) heights %u masks %u variants %u vertices %u (patch %u)\n",
           P, M, PM, PL, layouts442, maxLay, HB, MK, VAR, V, patchVerts);
}

/* ---- the consumer side ---- */
static void Consume(const uint8_t *b, size_t sz, unsigned variants)
{
    char dir[64] = "/tmp/rgexp.XXXXXX", sub[96], cwd[1024], p[128];
    FILE *fp;
    unsigned lay, mt, q, vi = 0;

    CHECK(mkdtemp(dir) != NULL);
    CHECK(getcwd(cwd, sizeof(cwd)) != NULL);
    snprintf(sub, sizeof(sub), "%s/voxel", dir);
    CHECK(mkdir(sub, 0755) == 0);
    CHECK(chdir(dir) == 0);
    fp = fopen("voxel/buildings.bin", "wb");
    CHECK(fp != NULL && fwrite(b, 1, sz, fp) == sz);
    if (fp) fclose(fp);
    CHECK(VoxelBuildings_Init());
    while (VoxelBuildings_Variant(vi, &lay, &mt, &q)) vi++;
    CHECK(vi == variants);
    CHECK(VoxelBuildings_MaxTop() > 0.0f);
    {   /* every placed layout answers PageOf, and every page reads back inside its own texels */
        unsigned l, pages = 0;
        for (l = 1; l <= 442; l++) {
            VoxelMapInstance inst;
            int pg;
            memset(&inst, 0, sizeof(inst));
            inst.layoutId = (uint16_t)l;
            pg = VoxelBuildings_PageOf(&inst);
            if (pg >= 0) {
                unsigned w, h;
                uint16_t one[4];
                pages++;
                CHECK(VoxelBuildings_PageSize((unsigned)pg, &w, &h) && w <= 1024 && h <= 1024 && w * h <= 512u * 512u);
                CHECK(VoxelBuildings_ReadPage((unsigned)pg, 0, 4, one));
                CHECK(VoxelBuildings_ReadPage((unsigned)pg, w * h - 4, 4, one));
                CHECK(!VoxelBuildings_ReadPage((unsigned)pg, w * h - 3, 4, one));
            }
        }
        printf("consumer: %u layouts have a page, %u variants\n", pages, vi);
        CHECK(pages > 0);
    }
    VoxelBuildings_Shutdown();
    CHECK(chdir(cwd) == 0);
    snprintf(p, sizeof(p), "%s/voxel/buildings.bin", dir); (void)unlink(p);
    (void)rmdir(sub); (void)rmdir(dir);
}

static void TestRealRom(void)
{
    const char *path = getenv("ROMGEN_ROM");
    FILE *fp;
    long n;
    uint8_t *rom;
    RgRunOpts opts;
    RgOutput a, b;
    RgBuildModels ms;
    RgBuildStats st;
    RgWorld w;
    uint8_t *tiny;
    size_t sz;

    if (!path || !(fp = fopen(path, "rb"))) { sSkips++; printf("SKIP real-ROM export (set ROMGEN_ROM)\n"); return; }
    fseek(fp, 0, SEEK_END); n = ftell(fp); fseek(fp, 0, SEEK_SET);
    rom = (uint8_t *)malloc((size_t)n);
    CHECK(rom && fread(rom, 1, (size_t)n, fp) == (size_t)n);
    fclose(fp);
    memset(&opts, 0, sizeof(opts));
    opts.nowMs = NowMs; opts.wantSigns = true; opts.wantBuildings = true;
    CHECK(rg_run(rom, (size_t)n, &opts, &a) == RG_OK);
    CHECK(a.regions && a.signs && a.buildings);
    printf("rg_run: buildings.bin %zu B; %u models %u pages %u pageModels %u placements %u vertices %u masks %u variants; gate failures %u\n"
           "        regions %zu B, signposts %zu B; ms: models %.0f gates %.0f placements+write %.0f\n",
           a.buildingsSize, a.bModels, a.bPages, a.bPageModels, a.bPlacements, a.bVertices, a.bMasks, a.bVariants,
           a.buildingsFailed, a.regionsSize, a.signsSize, a.msBuildModels, a.msChecks, a.msWriteBuildings);
    /* pinned counts (S2.6 values: nothing was added in S2.7; 7898476 B / 80520 vertices -> 7870828 / 79368 by the
     * look-L7 flat-cap merge: six flat-capped models emit 64 triangles fewer each, same pixels; -> 7873564 / 79482 by
     * look L6: the hip-roof ridge gets its back face, 38 triangles) */
    CHECK(a.buildingsSize == 7873564u && a.bModels == 286 && a.bPages == 118 && a.bPageModels == 630 && a.bPlacements == 2894 &&
          a.bVertices == 79482 && a.bMasks == 56 && a.bVariants == 66 && a.buildingsFailed == 0);
    CHECK(a.regionsSize == 330791u && a.signsSize == 26144u);   /* S0/S1 outputs unchanged by wantBuildings */
    Parse(a.buildings, a.buildingsSize);
    Consume(a.buildings, a.buildingsSize, a.bVariants);
    /* determinism: a second full run is byte identical */
    CHECK(rg_run(rom, (size_t)n, &opts, &b) == RG_OK);
    CHECK(b.buildingsSize == a.buildingsSize && memcmp(a.buildings, b.buildings, a.buildingsSize) == 0);
    rg_output_free(&b);
    /* wantBuildings off: no buildings output, the others unchanged */
    opts.wantBuildings = false;
    CHECK(rg_run(rom, (size_t)n, &opts, &b) == RG_OK && b.buildings == NULL && b.buildingsSize == 0);
    CHECK(b.regionsSize == a.regionsSize && memcmp(a.regions, b.regions, a.regionsSize) == 0);
    rg_output_free(&b);
    /* two-pass sizing and the output-buffer guard */
    CHECK(rg_world_open(&w, rom, (size_t)n) == RG_OK);
    CHECK(rg_build_models(&w, rg_specs, rg_spec_count, &ms) == RG_OK);
    memset(&st, 0, sizeof(st));
    sz = rg_buildings_write(&w, &ms, NULL, 0, &st);
    CHECK(sz == a.buildingsSize && st.err == RG_OK);
    tiny = (uint8_t *)malloc(sz);
    CHECK(tiny != NULL);
    memset(&st, 0, sizeof(st));
    CHECK(rg_buildings_write(&w, &ms, tiny, sz - 1, &st) == 0 && st.err == RG_ERR_TOO_BIG && st.errField != NULL);
    memset(&st, 0, sizeof(st));
    CHECK(rg_buildings_write(&w, &ms, tiny, sz, &st) == sz && memcmp(tiny, a.buildings, sz) == 0);
    free(tiny);
    rg_models_free(&ms);
    rg_world_close(&w);
    rg_output_free(&a);
    free(rom);
}

int main(void)
{
    TestRealRom();
    printf("test_romgen_export: %d checks, %d failures, %d skipped\n", sChecks, sFails, sSkips);
    return sFails != 0;
}
