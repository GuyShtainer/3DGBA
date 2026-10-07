// test_romgen_expand.c -- host test for the S2.5 slice: rg_grelief.c (Relief, Mound, with_ring), rg_bexpand.c
// (component / kit / props expanders, seam_art, pick_side, flank_band, props find / cells_of / beyond), the S2.5
// branches of rg_buildings.c (owned / repeat_at / props placements, the variants table) and the round trip
// through the VENDORED consumer. phase 33 S2.5, SPEC-S2 sections 1.5-1.7, 4, 6.1.
// Synthetic checks always; the real ROM (ROMGEN_ROM=roms/emerald.gba, exported) adds the pinned counts.
//
//   clang -std=c11 -Wall -Wextra -O2 -ffp-contract=off -fsanitize=address,undefined -DVOXEL_HOST_FILES \
//         -DCTR_VOXEL_LIGHTING=1 -I source/romgen -I source/voxel -I test/host test/host/test_romgen_expand.c \
//         source/romgen/rg_world.c source/romgen/rg_art.c source/romgen/rg_bimg.c source/romgen/rg_geom.c source/romgen/rg_grelief.c \
//         source/romgen/rg_bcheck.c source/romgen/rg_bspecs.c source/romgen/rg_buildings.c source/romgen/rg_bexpand.c source/romgen/rg_binterior.c source/romgen/rg_brooms.c \
//         source/voxel/voxel_world.c source/voxel/voxel_regions.c source/voxel/voxel_relief.c \
//         source/voxel/voxel_building.c source/voxel/voxel_sign.c source/voxel/voxel_arena.c \
//         source/voxel/voxel_atlas.c source/voxel/voxel_mesh_builder.c source/voxel/voxel_tree.c \
//         source/voxel/voxel_lighting.c source/voxel/voxel_grade.c source/voxel/voxel_entities.c \
//         source/voxel/voxel_camera.c source/voxel/vx_adapter.c source/voxel/vx_snapshot.c \
//         source/voxel/vx_lz77.c source/voxel/vx_behavior.c source/voxel/vx_data.c \
//         source/voxel/ctr_shims_pure.c source/voxel/vx_battle_stub.c -lm -o /tmp/trgx && /tmp/trgx
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "rg_bexpand.h"
#include "rg_buildings.h"
#include "voxel_building.h"
#include "voxel_world.h"

static int sChecks, sFails, sSkips;
#define CHECK(c) do { ++sChecks; if (!(c)) { ++sFails; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static unsigned U16(const uint8_t *p) { return (unsigned)(p[0] | (p[1] << 8)); }

/* ---- the consumer side ---- */
static char sDir[64], sCwd[1024];

static void EnterTemp(void)
{
    char sub[96];
    strcpy(sDir, "/tmp/rgexp.XXXXXX");
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

/* ---- synthetic: with_ring, pick_side, flank_band, Relief, Mound ---- */
static void Px(RgImage *im, int x, int y, int r, int g, int b)
{
    uint8_t *p = im->px + ((size_t)y * (size_t)im->w + (size_t)x) * 4u;
    p[0] = (uint8_t)r; p[1] = (uint8_t)g; p[2] = (uint8_t)b; p[3] = 255;
}

static bool IsPx(const RgImage *im, int x, int y, int r, int g, int b)
{
    const uint8_t *p = im->px + ((size_t)y * (size_t)im->w + (size_t)x) * 4u;
    return p[3] == 255 && p[0] == r && p[1] == g && p[2] == b;
}

static void TestWithRing(void)
{
    RgImage art, out;
    static const int ring[1][3] = {{222, 230, 238}};
    int x, y;

    /* rock A B C, foam under col 1 (below its foot) and col 2, and a ring-only column 3: hand-run BFS */
    CHECK(rg_img_new(&art, 4, 3));
    Px(&art, 1, 0, 10, 0, 0);  Px(&art, 2, 0, 0, 20, 0);  Px(&art, 1, 1, 0, 0, 30);
    Px(&art, 1, 2, 222, 230, 238);  Px(&art, 2, 1, 222, 230, 238);  Px(&art, 3, 1, 222, 230, 238);
    CHECK(rg_mound_with_ring(&art, ring, 1, &out) && out.w == 4 && out.h == 9);
    /* band 0: the rock only */
    CHECK(IsPx(&out, 1, 0, 10, 0, 0) && IsPx(&out, 2, 0, 0, 20, 0) && IsPx(&out, 1, 1, 0, 0, 30));
    CHECK(out.px[((size_t)1 * 4 + 2) * 4 + 3] == 0);                           /* (2,1) is foam, not rock */
    /* band 1: the foam alone, in place */
    CHECK(IsPx(&out, 1, 3 + 2, 222, 230, 238) && IsPx(&out, 2, 3 + 1, 222, 230, 238) && IsPx(&out, 3, 3 + 1, 222, 230, 238));
    CHECK(out.px[((size_t)3 * 4 + 1) * 4 + 3] == 0);
    /* band 2: grown, every pixel filled, first-come in FIFO order with neighbours +x -x +y -y */
    for (y = 0; y < 3; y++)
        for (x = 0; x < 4; x++)
            CHECK(out.px[((size_t)(6 + y) * 4 + (size_t)x) * 4 + 3] == 255);
    CHECK(IsPx(&out, 0, 6, 10, 0, 0) && IsPx(&out, 1, 6, 10, 0, 0) && IsPx(&out, 2, 6, 0, 20, 0) && IsPx(&out, 3, 6, 0, 20, 0));
    CHECK(IsPx(&out, 0, 7, 0, 0, 30) && IsPx(&out, 1, 7, 0, 0, 30) && IsPx(&out, 2, 7, 0, 20, 0) && IsPx(&out, 3, 7, 0, 20, 0));
    CHECK(IsPx(&out, 0, 8, 0, 0, 30) && IsPx(&out, 1, 8, 0, 0, 30) && IsPx(&out, 2, 8, 0, 20, 0) && IsPx(&out, 3, 8, 0, 20, 0));
    rg_img_free(&art); rg_img_free(&out);

    /* no ring colours: band 1 empty, a one-pixel rock grows over the whole drawing */
    CHECK(rg_img_new(&art, 3, 3));
    Px(&art, 1, 1, 7, 8, 9);
    CHECK(rg_mound_with_ring(&art, NULL, 0, &out));
    for (y = 0; y < 3; y++)
        for (x = 0; x < 3; x++) {
            CHECK(out.px[((size_t)(3 + y) * 3 + (size_t)x) * 4 + 3] == 0);
            CHECK(IsPx(&out, x, 6 + y, 7, 8, 9));
        }
    CHECK(IsPx(&out, 1, 1, 7, 8, 9) && out.px[3] == 0);
    rg_img_free(&art); rg_img_free(&out);
}

static void TestPickSide(void)
{
    RgImage art;
    RgTile t;
    int x, y;

    /* columns 0-7 drawn rows 5-19 (a front 15 tall ending on row 20), columns 8-15 rows 0-9 (too short) */
    CHECK(rg_img_new(&art, 16, 20));
    for (x = 0; x < 8; x++)
        for (y = 5; y < 20; y++)
            Px(&art, x, y, 1, 2, 3);
    for (x = 8; x < 16; x++)
        for (y = 0; y < 10; y++)
            Px(&art, x, y, 4, 5, 6);
    t = rg_pick_side(&art, 11);
    CHECK(t.rect[0] == 0 && t.rect[1] == 9 && t.rect[2] == 8 && t.rect[3] == 20);
    /* a second 8-wide stretch ending lower wins over a higher one; ties keep the first */
    for (x = 8; x < 16; x++)
        for (y = 0; y < 20; y++)
            Px(&art, x, y, 4, 5, 6);
    t = rg_pick_side(&art, 11);
    CHECK(t.rect[0] == 0 && t.rect[2] == 8 && t.rect[3] == 20);                 /* equal ends: the first, u = 0 */
    rg_img_free(&art);

    /* only widths 6 and 4 fit: a 5-wide stretch is a 4 wide tile at its first column */
    CHECK(rg_img_new(&art, 12, 16));
    for (x = 3; x < 8; x++)
        for (y = 4; y < 16; y++)
            Px(&art, x, y, 1, 1, 1);
    t = rg_pick_side(&art, 8);
    CHECK(t.rect[0] == 3 && t.rect[1] == 8 && t.rect[2] == 7 && t.rect[3] == 16);
    rg_img_free(&art);

    /* no straight front: the 4 x height window with the most drawn pixels, first by (v, u) */
    CHECK(rg_img_new(&art, 8, 12));
    Px(&art, 3, 5, 9, 9, 9);
    t = rg_pick_side(&art, 4);
    CHECK(t.rect[0] == 0 && t.rect[1] == 2 && t.rect[2] == 4 && t.rect[3] == 6);
    rg_img_free(&art);
}

static void TestFlankNone(void)
{
    RgImage art, out;
    RgComponentsCfg c;
    RgTile t;
    bool has = true;

    memset(&c, 0, sizeof(c));
    c.flank = -1;
    CHECK(rg_img_new(&art, 16, 16));
    Px(&art, 3, 4, 5, 6, 7);
    CHECK(rg_flank_band(NULL, &art, &c, 12, &out, &t, &has) && !has && rg_img_equal(&art, &out));
    rg_img_free(&art); rg_img_free(&out);
}

/* a hedge-like drawing: columns are runs from `top` down `len` rows (a top face then a front `height` tall) */
static void HedgeArt(RgImage *a, int w, int h, int top, int len)
{
    int x, y;

    CHECK(rg_img_new(a, w, h));
    for (x = 0; x < w; x++)
        for (y = top; y < top + len; y++)
            Px(a, x, y, (x * 7 + y * 3) & 255, (x * 5 + y * 11) & 255, (x * 13 + y * 17 + 40) & 255);
}

static void TestReliefGate(void)
{
    RgImage art;
    RgRelief R;
    RgMesh m;
    RgOrthoResult o;
    int height = 8;

    HedgeArt(&art, 32, 32, 4, 20);
    memset(&R, 0, sizeof(R));
    R.art = &art;
    R.height = height;
    R.side = rg_pick_side(&art, height);
    R.hasSeam = true;
    R.seamRows = 32;
    rg_mesh_init(&m);
    CHECK(rg_emit_relief(&R, "relief", &m) && !m.failed && m.n > 0);
    CHECK(rg_ortho_check(&m, &art, NULL, 0, NULL, &o));
    CHECK(o.wrong == 0 && o.missing == 0 && o.extra == 0);
    CHECK(rg_density_check(&m, &art, NULL, 0) == 0);
    /* a shifted uv is caught by the gate (it is a real check, not a rubber stamp) */
    if (m.n > 0) {
        unsigned k;
        for (k = 0; k < 3; k++) m.t[0].p[k].u += 3.0;
        CHECK(rg_ortho_check(&m, &art, NULL, 0, NULL, &o));
        CHECK(o.wrong > 0 || o.missing > 0 || o.extra > 0);
    }
    rg_mesh_free(&m);
    rg_img_free(&art);

    /* a run that ends in a notch (a railing's gaps, hull closing them): still exact */
    HedgeArt(&art, 24, 40, 2, 30);
    {
        int x, y;

        for (x = 8; x < 12; x++)
            for (y = 14; y < 18; y++)
                memset(art.px + ((size_t)y * 24u + (size_t)x) * 4u, 0, 4);        /* a hole in the middle */
    }
    memset(&R, 0, sizeof(R));
    R.art = &art; R.height = 10; R.side = rg_pick_side(&art, 10); R.hull = 6; R.bridge = 3;
    rg_mesh_init(&m);
    CHECK(rg_emit_relief(&R, "relief", &m) && !m.failed && m.n > 0);
    CHECK(rg_ortho_check(&m, &art, NULL, 0, NULL, &o));
    CHECK(o.wrong == 0 && o.missing == 0 && o.extra == 0);
    rg_mesh_free(&m);
    rg_img_free(&art);
}

static void TestMoundGate(void)
{
    RgImage draw, full;
    RgMound M;
    RgMesh m;
    RgOrthoResult o;
    static const int ring[1][3] = {{222, 230, 238}};
    int x, y;

    /* a dome, 24 wide, 20 tall, with a foam foot */
    CHECK(rg_img_new(&draw, 24, 20));
    for (x = 0; x < 24; x++) {
        double dx = (x + 0.5 - 12.0) / 12.0;
        int rows = (int)(14.0 * sqrt(1.0 - dx * dx > 0 ? 1.0 - dx * dx : 0.0));

        for (y = 15 - rows; y < 16; y++)
            Px(&draw, x, y, 90 + (x * 3) % 60, 110 + (y * 5) % 50, 120);
        if (rows > 0)
            for (y = 16; y < 19; y++)
                Px(&draw, x, y, 222, 230, 238);
    }
    CHECK(rg_mound_with_ring(&draw, ring, 1, &full));
    memset(&M, 0, sizeof(M));
    M.full = &full; M.rise = 1.0; M.step = 2; M.backSteps = 3; M.rows = 20; M.nRing = 1;
    M.ring[0][0] = 222; M.ring[0][1] = 230; M.ring[0][2] = 238;
    rg_mesh_init(&m);
    CHECK(rg_emit_mound(&M, "mound", &m) && !m.failed && m.n > 0);
    CHECK(rg_ortho_check(&m, &full, NULL, 0, &draw, &o));
    CHECK(o.wrong == 0 && o.missing == 0 && o.extra == 0);
    CHECK(rg_density_check(&m, &full, NULL, 0) == 0);
    rg_mesh_free(&m);
    rg_img_free(&draw); rg_img_free(&full);
}

static double Now(void) { return (double)clock() / CLOCKS_PER_SEC; }

static void TestRealRom(void)
{
    const char *path = getenv("ROMGEN_ROM");
    FILE *fp;
    long n;
    uint8_t *rom, *buf;
    RgWorld w;
    RgBuildModels ms;
    RgBuildStats st;
    unsigned i, kinds[4] = {0, 0, 0, 0}, gateBad = 0, nTris = 0, nPl = 0;
    size_t sz;
    double t0;

    if (!path || !(fp = fopen(path, "rb"))) { sSkips++; printf("SKIP real-ROM expanders (set ROMGEN_ROM)\n"); return; }
    fseek(fp, 0, SEEK_END); n = ftell(fp); fseek(fp, 0, SEEK_SET);
    rom = (uint8_t *)malloc((size_t)n);
    CHECK(rom && fread(rom, 1, (size_t)n, fp) == (size_t)n);
    fclose(fp);
    CHECK(rg_world_open(&w, rom, (size_t)n) == RG_OK);
    t0 = Now();
    CHECK(rg_build_models(&w, rg_specs, rg_spec_count, &ms) == RG_OK);
    printf("build_models: %u models in %.1fs: hedge %u railing %u kit %u props %u skipped %u seamClash %u conn %u ties %u\n",
           ms.n, Now() - t0, ms.nHedge, ms.nRailing, ms.nKit, ms.nProps, ms.skipped, ms.seamClash, ms.connAmbiguous,
           ms.propTies);
    for (i = 0; i < ms.n; i++) {
        RgOrthoResult o;
        unsigned bad = 99;
        const RgBuildModel *m = &ms.m[i];

        CHECK(rg_model_gate(m, &o, &bad));
        nTris += m->mesh.n;
        if (o.wrong || o.missing || o.extra || bad) {
            gateBad++;
            printf("GATE %-28s wrong %u missing %u extra %u dens %u  (%ux%u, %u tris)\n", m->spec->name, o.wrong,
                   o.missing, o.extra, bad, m->w, m->h, m->mesh.n);
        }
        (void)kinds;
    }
    for (i = 0; i < ms.n; i++) {   /* every owned model finds placements; props stand only by their own cells */
        const RgBuildModel *m = &ms.m[i];
        RgPlacementList pl;

        if (!m->xSpec) continue;
        memset(&pl, 0, sizeof(pl));
        CHECK(rg_find_placements(&w, m, &pl) == RG_OK);
        CHECK(pl.n > 0);
        nPl += pl.n;
        rg_placements_free(&pl);
    }
    CHECK(nPl > 0);
    printf("gate failures %u of %u models, %u triangles\n", gateBad, ms.n, nTris);
    /* S2.6: the 13 interior rows add 200 pieces + 19 bare twins to the S2.5 total of 67 models */
    CHECK(ms.n == 286 && ms.nInterior == 200 && ms.nTwin == 19 && ms.nHedge == 6 && ms.nRailing == 35 && ms.nKit == 7 && ms.nProps == 5 && ms.skipped == 0);
    /* 25514 -> 25130: flat-cap merge (look L7 follow-up): six Emerald models with flat-colour cap patches emit 64 triangles fewer each (littleroot_house_e/w, littleroot_lab, kit_house_4/5, oldale_house); previews pixel-identical.
     * 25130 -> 25168: look L6: the hip-roof ridge gets its back face, 38 triangles over the hip-roofed models (closed from behind; the ortho gate stays 0) */
    CHECK(gateBad == 0 && nTris == 25168);
    CHECK(ms.seamClash == 0 && ms.connAmbiguous == 0);   /* seam_art column assert, connections assert */
    printf("props tie-break divergence A2: %u tied adjacent groups (informational, model order / _n suffix only)\n", ms.propTies);
    t0 = Now();
    memset(&st, 0, sizeof(st));
    sz = rg_buildings_write(&w, &ms, NULL, 0, &st);
    printf("write size: %zu (err %d field %s) in %.1fs\n", sz, (int)st.err, st.errField ? st.errField : "-", Now() - t0);
    if (sz > 0) {
        buf = (uint8_t *)malloc(sz);
        CHECK(buf && rg_buildings_write(&w, &ms, buf, sz, &st) == sz);
        printf("buildings.bin: %zu bytes, %u pages, %u models, %u pageModels, %u vertices, %u placements, %u masks, %u variants\n",
               sz, st.pages, st.models, st.pageModels, st.vertices, st.placements, st.masks, st.variants);
        CHECK(st.err == RG_OK && st.models == 286 && st.variants == 66 && st.variants <= 128);
        /* 7898476 B / 80520 vertices -> 7870828 / 79368 (1152 vertices = 6 x 64 triangles): the flat-cap merge;
         * -> 7873564 / 79482 (114 vertices = 38 triangles): look L6: the hip-roof ridge gets its back face, 38 triangles over the hip-roofed models */
        CHECK(sz == 7873564u && st.pages == 118 && st.placements == 2894u && st.vertices == 79482u && st.masks == 56u);
        CHECK(memcmp(buf, "VXB7", 4) == 0 && sz % 4 == 0);
        {   /* the vendored consumer reads it back: every variant, every owned model's layout page */
            VoxelMapInstance inst;
            unsigned lay, mt, q, vi = 0;

            EnterTemp();
            WriteFile("voxel/buildings.bin", buf, sz);
            CHECK(VoxelBuildings_Init());
            while (VoxelBuildings_Variant(vi, &lay, &mt, &q)) { CHECK(q < 16); vi++; }
            CHECK(vi == st.variants);
            for (i = 0; i < ms.n; i++) {
                const RgBuildModel *m = &ms.m[i];

                if (!m->xSpec || m->prop) continue;
                memset(&inst, 0, sizeof(inst));
                inst.layoutId = m->spec->layoutId;
                CHECK(VoxelBuildings_PageOf(&inst) >= 0);
                CHECK(VoxelBuildings_LayoutTop(&inst) > 0.0f);
            }
            CHECK(VoxelBuildings_MaxTop() > 0.0f);
            VoxelBuildings_Shutdown();
            LeaveTemp();
        }
        free(buf);
    }
    rg_models_free(&ms);
    rg_world_close(&w);
    free(rom);
    (void)kinds; (void)U16;
}


int main(void)
{
    TestWithRing();
    TestPickSide();
    TestFlankNone();
    TestReliefGate();
    TestMoundGate();
    TestRealRom();
    printf("test_romgen_expand: %d checks, %d failures, %d skipped\n", sChecks, sFails, sSkips);
    return sFails != 0;
}
