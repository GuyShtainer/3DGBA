// test_romgen_buildings.c -- host test for the S2.3 slice: rg_bspecs.c (littleroot_house, rows 1-2), rg_buildings.c
// (build_models, cell_heights, cell_footprints, find_placements, pack_atlas, texel_offset, the VXB7 writer) and the
// round trip through the VENDORED consumer (voxel_building.c). phase 33 S2.3, SPEC-S2 sections 1.5, 1.7, 4, 6.1.
// Synthetic mini-ROM always; the real ROM (ROMGEN_ROM=/path/emerald.gba) adds the two Littleroot houses.
//
//   clang -std=c11 -Wall -Wextra -O2 -ffp-contract=off -fsanitize=address,undefined -DVOXEL_HOST_FILES \
//         -DCTR_VOXEL_LIGHTING=1 -I source/romgen -I source/voxel -I test/host test/host/test_romgen_buildings.c \
//         source/romgen/rg_world.c source/romgen/rg_art.c source/romgen/rg_bimg.c source/romgen/rg_geom.c source/romgen/rg_grelief.c \
//         source/romgen/rg_bcheck.c source/romgen/rg_bspecs.c source/romgen/rg_buildings.c source/romgen/rg_bexpand.c source/romgen/rg_binterior.c source/romgen/rg_brooms.c \
//         source/voxel/voxel_world.c source/voxel/voxel_regions.c source/voxel/voxel_relief.c \
//         source/voxel/voxel_building.c source/voxel/voxel_sign.c source/voxel/voxel_arena.c \
//         source/voxel/voxel_atlas.c source/voxel/voxel_mesh_builder.c source/voxel/voxel_tree.c \
//         source/voxel/voxel_lighting.c source/voxel/voxel_daylight.c source/voxel/voxel_grade.c source/voxel/voxel_entities.c \
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

/* ---- synthetic: the S2.4 builders that need no ROM ---- */
static void TestSyntheticParts(void)
{
    RgPartList pl;
    RgMesh m;
    unsigned i, nproj = 0, njet = 0, ntop = 0;
    static const double roof[3][2] = {{7, 39}, {7, 11}, {0, 7}}, corn[2] = {39, 48};
    static const double unit[5] = {40, 60, 1, 16, 31};

    /* fountain: basin walls 3x2 + top fan 6 + sides 5x2 + bowl cylinder 46 + jet 2 = 70 triangles, all projected
     * except the basin's straight walls and the cylinder's far side */
    rg_parts_init(&pl);
    rg_mesh_init(&m);
    CHECK(rg_fountain(NULL, 0, 0, &pl) && pl.n == 4 && rg_parts_emit(&pl, &m) && !m.failed);
    CHECK(m.n == 70);
    for (i = 0; i < m.n; i++) {
        const char *nm = m.names[m.t[i].tag];
        if (!strcmp(nm, "jet~proj")) njet++;
        if (!strcmp(nm, "fountain.top")) ntop++;
        if (!strcmp(nm, "fountain.side~proj")) { nproj++; CHECK(m.t[i].shade == 0.7 && (m.t[i].flags & RG_TAG_PROJ)); }
    }
    CHECK(njet == 2 && ntop == 6 && nproj == 10);
    rg_mesh_free(&m);
    rg_parts_free(&pl);

    /* flat_block: a plain block is [body, roof]; with a roof unit [body, roof_w, roof_u, roof_e, unit] */
    rg_parts_init(&pl);
    rg_mesh_init(&m);
    CHECK(rg_flat_block(&pl, 64, 96, roof, corn, 48, NULL) && pl.n == 2 && rg_parts_emit(&pl, &m) && !m.failed && m.n > 0);
    rg_mesh_free(&m);
    rg_parts_free(&pl);
    rg_parts_init(&pl);
    rg_mesh_init(&m);
    CHECK(rg_flat_block(&pl, 96, 96, roof, corn, 48, unit) && pl.n == 5 && rg_parts_emit(&pl, &m) && !m.failed && m.n > 0);
    rg_mesh_free(&m);
    rg_parts_free(&pl);

    /* every direct row has a builder, a layout pin and exact rects inside its art */
    for (i = 0; i < rg_spec_count; i++) {
        const RgSpec *sp = &rg_specs[i];
        unsigned k;
        if (sp->kind != RG_SPEC_DIRECT) {                 /* S2.5 expander rows: pinned in test_romgen_expand.c */
            CHECK(sp->ext != NULL && sp->nGround > 0);
            continue;
        }
        CHECK(sp->kind == RG_SPEC_DIRECT && sp->parts && sp->layoutId && sp->layoutFnv && sp->nExact > 0 && sp->nGround > 0);
        for (k = 0; k < sp->nExact; k++)
            CHECK(sp->exact[k].x0 >= 0 && sp->exact[k].y0 >= 0 && sp->exact[k].x1 <= sp->rect[2] * 16 &&
                  sp->exact[k].y1 <= sp->rect[3] * 16 && sp->exact[k].x0 < sp->exact[k].x1 && sp->exact[k].y0 < sp->exact[k].y1);
    }
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
    uint8_t hts[256];

    if (!path || !(fp = fopen(path, "rb"))) { sSkips++; printf("SKIP real-ROM buildings (set ROMGEN_ROM)\n"); return; }
    fseek(fp, 0, SEEK_END); n = ftell(fp); fseek(fp, 0, SEEK_SET);
    rom = (uint8_t *)malloc((size_t)n);
    CHECK(rom && fread(rom, 1, (size_t)n, fp) == (size_t)n);
    fclose(fp);
    CHECK(rg_world_open(&w, rom, (size_t)n) == RG_OK);
    CHECK(rg_layout_fnv(&w.layouts[9]) == 0xEFE99674u);
    {   /* the 14 direct rows only: the S2.3-S2.4 byte pins below stay the regression for the direct path */
        static RgSpec direct[32];
        unsigned nd = 0;

        /* the Phase 36 Hoenn rows (rg_hspecs.h) follow these; their pins live in check, census and the world pins */
        for (i = 0; i < rg_spec_count && strcmp(rg_specs[i].name, "dewford_house_w") != 0; i++)
            if (rg_specs[i].kind == RG_SPEC_DIRECT && nd < 32)
                direct[nd++] = rg_specs[i];
        CHECK(nd == 14);
        CHECK(rg_build_models(&w, direct, nd, &ms) == RG_OK);
        CHECK(ms.n == nd && ms.n == 14 && ms.skipped == 0);
    }
    CHECK(rg_layout_fnv(&w.layouts[0]) == 0xCA6DFAA0u && rg_layout_fnv(&w.layouts[2]) == 0x6FFC5818u &&
          rg_layout_fnv(&w.layouts[3]) == 0xA55404CFu && rg_layout_fnv(&w.layouts[10]) == 0x52C922B6u &&
          rg_layout_fnv(&w.layouts[19]) == 0x157E3492u);
    /* the gate for all 14 direct models: ortho 0/0/0, density empty, heights in 1..255 over the rect */
    for (i = 0; i < ms.n; i++) {
        RgOrthoResult o;
        unsigned bad = 99, cells = (unsigned)ms.m[i].w * ms.m[i].h;
        CHECK(rg_model_gate(&ms.m[i], &o, &bad));
        CHECK(o.wrong == 0 && o.missing == 0 && o.extra == 0 && bad == 0);
        CHECK(ms.m[i].mesh.n > 0 && !ms.m[i].mesh.failed);
        if (i < 2) CHECK(ms.m[i].w == 5 && ms.m[i].h == 5);
        CHECK(cells <= sizeof(hts) && rg_cell_heights(&ms.m[i], hts));
        {   /* 0 = nothing stands over the cell (a back row the model does not reach); inside the spec's match rows
             * (and for the houses, everywhere) a cell must be covered */
            const RgSpec *sp = ms.m[i].spec;
            unsigned r0 = sp->matchRows[0], r1 = sp->matchRows[1] ? (unsigned)sp->matchRows[1] : ms.m[i].h, zeros = 0, any = 0;
            for (f = 0; f < cells; f++) {
                unsigned row = f / ms.m[i].w;
                if (hts[f] == 0) zeros++; else any++;
                if (i < 2 || (sp->matchRows[1] && row >= r0 && row < r1)) CHECK(hts[f] >= 1);
            }
            CHECK(any > 0);
            printf("%-20s zero-height cells %u of %u\n", sp->name, zeros, cells);
        }
        printf("%-20s tris %5u  gate %u/%u/%u dens %u  cells %ux%u\n", ms.m[i].spec->name, ms.m[i].mesh.n, o.wrong,
               o.missing, o.extra, bad, ms.m[i].w, ms.m[i].h);
        if (i < 2) {
            printf("house %u heights:", i);
            for (f = 0; f < 25; f++) printf(" %u", hts[f]);
            printf("\n");
        }
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

    /* placements of every model: its own reference position is always found; the Center also turns up in Oldale
     * (layout 11), the gym in Petalburg (1) and Rustboro (4) */
    {
        unsigned totalPl = 0;
        int centerPetal = 0, centerOldale = 0, centerPacif = 0, gymL1 = 0, gymL4 = 0, gymRL1 = 0, gymRL4 = 0;

        for (i = 0; i < ms.n; i++) {
            const RgSpec *sp = ms.m[i].spec;
            unsigned k;
            int own = 0;

            memset(&pl, 0, sizeof(pl));
            CHECK(rg_find_placements(&w, &ms.m[i], &pl) == RG_OK);
            printf("%-20s placements %u:", sp->name, pl.n);
            for (k = 0; k < pl.n; k++) {
                printf(" L%u(%d,%d)", pl.p[k].layout, pl.p[k].px, pl.p[k].py);
                if (pl.p[k].layout == sp->layoutId && pl.p[k].px == sp->rect[0] && pl.p[k].py == sp->rect[1]) own = 1;
                if (!strcmp(sp->name, "pokemon_center")) {
                    if (pl.p[k].layout == 1) centerPetal = 1;
                    if (pl.p[k].layout == 11) centerOldale = 1;
                    if (pl.p[k].layout == 16) {   /* Phase 36 H2: Pacifidlog's centre stands on the deck (0x221), not the sea */
                        centerPacif = 1;
                        CHECK(pl.p[k].ground == 0x221);
                    }
                }
                if (!strcmp(sp->name, "gym")) { if (pl.p[k].layout == 1) gymL1 = 1; if (pl.p[k].layout == 4) gymL4 = 1; }
                if (!strcmp(sp->name, "gym_rustboro")) { if (pl.p[k].layout == 1) gymRL1 = 1; if (pl.p[k].layout == 4) gymRL4 = 1; }
            }
            printf("\n");
            CHECK(own);
            totalPl += pl.n;
            rg_placements_free(&pl);
        }
        CHECK(centerPetal && centerOldale && centerPacif);
        /* the gym is placed in layout 1 (by `gym`) and layout 4 (by `gym_rustboro`): Rustboro's copy has its own roof and
         * flanks, so the Petalburg model does not match there and vice versa (separate refs) */
        CHECK(gymL1 && gymRL4 && !gymL4 && !gymRL1);
        printf("total placements over %u models: %u\n", ms.n, totalPl);
    }

    /* the whole direct-spec file, through the vendored consumer */
    memset(&st, 0, sizeof(st));
    sz = rg_buildings_write(&w, &ms, NULL, 0, &st);
    CHECK(sz > 0 && st.err == RG_OK);
    buf = (uint8_t *)malloc(sz);
    CHECK(buf && rg_buildings_write(&w, &ms, buf, sz, &st) == sz);
    printf("buildings.bin (%u models): %zu bytes, %u pages, %u vertices, %u placements, %u masks\n", ms.n, sz, st.pages,
           st.vertices, st.placements, st.masks);
    CHECK(memcmp(buf, "VXB7", 4) == 0 && st.models == ms.n && st.placements > 0);
    CHECK(sz % 4 == 0);
    {   /* every model's reference cell resolves through VoxelBuildings_PageOf / CellAt */
        VoxelMapInstance inst;
        int g = -1, ok;
        float top = 0.0f;

        EnterTemp();
        WriteFile("voxel/buildings.bin", buf, sz);
        CHECK(VoxelBuildings_Init());
        for (i = 0; i < ms.n; i++) {
            const RgSpec *sp = ms.m[i].spec;

            memset(&inst, 0, sizeof(inst));
            inst.layoutId = sp->layoutId;
            CHECK(VoxelBuildings_PageOf(&inst) >= 0);
            ok = VoxelBuildings_CellAt(&inst, sp->rect[0] + sp->rect[2] / 2, sp->rect[1] + sp->rect[3] - 1, &g, &top) ? 1 : 0;
            CHECK(ok && g >= 0 && top > 0.0f);
        }
        {   /* a cell far from any building */
            memset(&inst, 0, sizeof(inst));
            inst.layoutId = 10;
            CHECK(!VoxelBuildings_CellAt(&inst, 8, 10, &g, &top));
        }
        CHECK(VoxelBuildings_MaxTop() > 0.0f);
        VoxelBuildings_Shutdown();
        LeaveTemp();
    }

    one = ms; one.n = 1;
    memset(&st1, 0, sizeof(st1));
    sz1 = rg_buildings_write(&w, &one, NULL, 0, &st1);
    buf1 = (uint8_t *)malloc(sz1 ? sz1 : 1);
    CHECK(sz1 > 0 && buf1 && rg_buildings_write(&w, &one, buf1, sz1, &st1) == sz1);
    printf("buildings.bin (house 1 only): %zu bytes, %u placements\n", sz1, st1.placements);
    RoundTrip(buf1, sz1, 10, 2, 4, &cellOk, &page);
    CHECK(cellOk && page == 0);
    {   /* Phase 36 H2: Pacifidlog's centre has its roof row over the sea (water metatiles 0x230-0x233, layout 16). Those
         * cells get the whole upper layer cut (quarters 0xF, a variant per metatile): the sea under them, not the deck */
        unsigned P = U16(buf + 4), M = U16(buf + 6), PM = U16(buf + 8), PL = U16(buf + 10), HB = U16(buf + 12);
        unsigned MK = U16(buf + 14), VAR = U16(buf + 20), mt, v, pc = ms.n, k;
        size_t modelT = 24u + 8u * P, hT = modelT + 16u * M + 8u * PM + 16u * PL;
        size_t qT = hT + HB + (HB & 1u) + 2u * HB + 32u * MK, varT = qT + HB + (HB & 1u);

        for (i = 0; i < ms.n; i++)
            if (strcmp(ms.m[i].spec->name, "pokemon_center") == 0)
                pc = i;
        CHECK(pc < ms.n);
        if (pc < ms.n) {
            unsigned hs = U32(buf + modelT + 16u * pc + 12), w = ms.m[pc].w, h = ms.m[pc].h;

            for (k = 0; k < w; k++) {
                CHECK(buf[qT + hs + k] == 0x0F);                       /* the roof row: water at Pacifidlog */
                CHECK(buf[qT + hs + (h - 1u) * w + k] == 0);           /* the door row: the placement's ground */
            }
        }
        for (mt = 0x230; mt <= 0x233; mt++) {
            int found = 0;

            for (v = 0; v < VAR; v++)
                found |= U16(buf + varT + 6u * v) == 16 && U16(buf + varT + 6u * v + 2) == mt && buf[varT + 6u * v + 4] == 0x0F;
            CHECK(found);
        }
    }
    /* the structural parse of the file */
    CHECK(U16(buf + 4) >= 1 && U16(buf + 6) == ms.n);
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
    TestSyntheticParts();
    TestRealRom();
    printf("test_romgen_buildings: %d checks, %d failures, %d skipped\n", sChecks, sFails, sSkips);
    return sFails != 0;
}
