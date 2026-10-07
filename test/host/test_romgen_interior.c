// test_romgen_interior.c -- host test for the S2.6 slice: rg_brooms.c (the 13 room tables), rg_binterior.c (_inside, the
// room expander, reuse_pieces / reuse_everywhere, bare twins, room_check) and the buildings.bin that results, read back by
// the VENDORED consumer (voxel_building.c). phase 33 S2.6, SPEC-S2 sections 1.6, 3.2, 5.4-5.5, 6.1.
// Synthetic checks always; the real ROM (ROMGEN_ROM=/path/emerald.gba) adds the whole-table build, the gate, room_check.
//
//   clang -std=c11 -Wall -Wextra -O2 -ffp-contract=off -fsanitize=address,undefined -DVOXEL_HOST_FILES \
//         -DCTR_VOXEL_LIGHTING=1 -I source/romgen -I source/voxel -I test/host test/host/test_romgen_interior.c \
//         source/romgen/rg_world.c source/romgen/rg_art.c source/romgen/rg_bimg.c source/romgen/rg_geom.c source/romgen/rg_grelief.c \
//         source/romgen/rg_bcheck.c source/romgen/rg_bspecs.c source/romgen/rg_buildings.c source/romgen/rg_bexpand.c source/romgen/rg_binterior.c source/romgen/rg_brooms.c \
//         source/voxel/voxel_world.c source/voxel/voxel_regions.c source/voxel/voxel_relief.c \
//         source/voxel/voxel_building.c source/voxel/voxel_sign.c source/voxel/voxel_arena.c \
//         source/voxel/voxel_atlas.c source/voxel/voxel_mesh_builder.c source/voxel/voxel_tree.c \
//         source/voxel/voxel_lighting.c source/voxel/voxel_grade.c source/voxel/voxel_entities.c \
//         source/voxel/voxel_camera.c source/voxel/vx_adapter.c source/voxel/vx_snapshot.c \
//         source/voxel/vx_lz77.c source/voxel/vx_behavior.c source/voxel/vx_data.c \
//         source/voxel/ctr_shims_pure.c source/voxel/vx_battle_stub.c -lm -o /tmp/trgi && /tmp/trgi
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "rg_binterior.h"
#include "voxel_building.h"
#include "voxel_world.h"

static int sChecks, sFails, sSkips;
#define CHECK(c) do { ++sChecks; if (!(c)) { ++sFails; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)


/* ---- the consumer side ---- */
static char sDir[64], sCwd[1024];

static void EnterTemp(void)
{
    char sub[96];
    strcpy(sDir, "/tmp/rgint.XXXXXX");
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

/* ---- synthetic: _inside / _inside_grid, the colour tables ---- */
static RgShape Rect(double x0, double y0, double x1, double y1)
{
    RgShape s;

    memset(&s, 0, sizeof(s));
    s.n = 1;
    s.p[0].kind = RG_SH_RECT;
    s.p[0].v[0] = x0; s.p[0].v[1] = y0; s.p[0].v[2] = x1; s.p[0].v[3] = y1;
    return s;
}

static void TestShapes(void)
{
    RgShape r = Rect(2, 3, 5, 6), e, p;
    uint8_t grid[10 * 10];
    int x, y, nIn = 0, agree = 1;

    CHECK(rg_inside(&r, 2, 3) && rg_inside(&r, 4, 5) && !rg_inside(&r, 5, 5) && !rg_inside(&r, 1, 3));
    memset(&e, 0, sizeof(e));
    e.n = 1; e.p[0].kind = RG_SH_ELLIPSE;
    e.p[0].v[0] = 5; e.p[0].v[1] = 5; e.p[0].v[2] = 3; e.p[0].v[3] = 2;
    CHECK(rg_inside(&e, 5, 5) && rg_inside(&e, 3, 5) && !rg_inside(&e, 0, 0) && !rg_inside(&e, 5, 8));
    memset(&p, 0, sizeof(p));
    p.n = 1; p.p[0].kind = RG_SH_POLY; p.p[0].n = 3;
    p.p[0].v[0] = 0; p.p[0].v[1] = 0; p.p[0].v[2] = 8; p.p[0].v[3] = 0; p.p[0].v[4] = 0; p.p[0].v[5] = 8;
    CHECK(rg_inside(&p, 1, 1) && rg_inside(&p, 3, 3) && !rg_inside(&p, 7, 7) && !rg_inside(&p, 9, 1));
    rg_inside_grid(&r, 10, 10, grid);
    for (y = 0; y < 10; y++)
        for (x = 0; x < 10; x++) {
            nIn += grid[y * 10 + x] != 0;
            agree = agree && (grid[y * 10 + x] != 0) == rg_inside(&r, x, y);
        }
    CHECK(nIn == 9 && agree);
    rg_inside_grid(&e, 10, 10, grid);
    agree = 1;
    for (y = 0; y < 10; y++)
        for (x = 0; x < 10; x++)
            agree = agree && (grid[y * 10 + x] != 0) == rg_inside(&e, x, y);
    CHECK(agree);
}

/* every colour the tables name must come from a 15-bit palette entry, or no ROM pixel could ever match it */
static void TestColours(void)
{
    const uint32_t *c;
    unsigned n = rg_room_colours(&c), i, ok = 0;

    CHECK(n > 0);
    for (i = 0; i < n; i++) {
        char hex[8];
        uint16_t c5;

        snprintf(hex, sizeof(hex), "%06x", (unsigned)c[i]);
        ok += rg_hex_to_c5(hex, &c5) ? 1u : 0u;
    }
    CHECK(ok == n);
}

static void TestTables(void)
{
    static const RgRoomDef *defs[13] = {&rg_room_pc1f, &rg_room_pc2f, &rg_room_mart, &rg_room_brendan_1f,
        &rg_room_brendan_2f, &rg_room_may_1f, &rg_room_may_2f, &rg_room_lab, &rg_room_lab_table,
        &rg_room_lavaridge_pc1f, &rg_room_house1, &rg_room_house2, &rg_room_rustboro_gym};
    unsigned i, k, interior = 0, withOpen = 0;

    for (i = 0; i < rg_spec_count; i++)
        interior += rg_specs[i].kind == RG_SPEC_INTERIOR;
    CHECK(interior == 13);
    for (i = 0; i < 13; i++) {
        RgPieceList pl;
        bool named = true;

        memset(&pl, 0, sizeof(pl));
        CHECK(defs[i]->pieces(&pl) && !pl.failed && pl.n > 0);
        for (k = 0; k < pl.n; k++)
            named = named && pl.p[k].name[0] != 0 && pl.p[k].height > 0;
        CHECK(named);
        withOpen += defs[i]->open.n > 0;
        rg_pl_free(&pl);
    }
    CHECK(withOpen > 0);
    for (i = 0; i < rg_spec_count; i++)
        if (rg_specs[i].kind == RG_SPEC_INTERIOR)
            CHECK(rg_room_of_layout(rg_specs[i].layoutId) == (const RgRoomDef *)rg_specs[i].ext);
    CHECK(rg_room_of_layout(1) == NULL);
}

static double Now(void) { return (double)clock() / CLOCKS_PER_SEC; }


static void WriteAndRead(const RgWorld *w, const RgBuildModels *ms)
{
    RgBuildStats st;
    size_t sz;
    uint8_t *buf;
    unsigned i;
    double t0 = Now();

    memset(&st, 0, sizeof(st));
    sz = rg_buildings_write(w, ms, NULL, 0, &st);
    printf("write size: %zu (err %d field %s) in %.1fs\n", sz, (int)st.err, st.errField ? st.errField : "-", Now() - t0);
    CHECK(sz > 0);
    if (sz == 0)
        return;
    buf = (uint8_t *)malloc(sz);
    CHECK(buf && rg_buildings_write(w, ms, buf, sz, &st) == sz);
    printf("buildings.bin: %zu bytes, %u pages, %u models, %u pageModels, %u vertices, %u placements, %u masks, %u variants\n",
           sz, st.pages, st.models, st.pageModels, st.vertices, st.placements, st.masks, st.variants);
    CHECK(st.err == RG_OK && st.variants <= RG_MAX_VARIANTS && st.pages <= RG_MAX_PAGES);
    /* pins (this ROM, S2.6): 67 S2.5 models + 200 interior pieces + 19 bare twins */
    CHECK(st.models == 286 && st.variants == 66 && st.pages == 118 && st.pageModels == 630 && st.placements == 2894u);
    /* 7898476 B / 80520 vertices -> 7870828 / 79368: the look-L7 flat-cap merge (outdoor models; interiors unchanged);
     * -> 7873564 / 79482: look L6, the hip-roof ridge's back face (outdoor models; interiors unchanged) */
    CHECK(sz == 7873564u && st.vertices == 79482u && st.masks == 56u);
    CHECK(memcmp(buf, "VXB7", 4) == 0 && sz % 4 == 0);
    {
        VoxelMapInstance inst;
        unsigned lay, mt, q, vi = 0;

        EnterTemp();
        WriteFile("voxel/buildings.bin", buf, sz);
        CHECK(VoxelBuildings_Init());
        while (VoxelBuildings_Variant(vi, &lay, &mt, &q)) { CHECK(q < 16); vi++; }
        CHECK(vi == st.variants);
        for (i = 0; i < ms->n; i++) {
            const RgBuildModel *m = &ms->m[i];

            if (m->spec->kind != RG_SPEC_INTERIOR || m->own != NULL) continue;
            memset(&inst, 0, sizeof(inst));
            inst.layoutId = m->spec->layoutId;
            CHECK(VoxelBuildings_PageOf(&inst) >= 0);
        }
        CHECK(VoxelBuildings_MaxTop() > 0.0f);
        VoxelBuildings_Shutdown();
        LeaveTemp();
    }
    free(buf);
}

static void RealChecks(const RgWorld *w, RgBuildModels *ms, const uint8_t *rom)
{
    unsigned i, nInt = 0, nTwin = 0, gateBad = 0, bareTotal = 0, reusedTotal = 0;
    RgPlacementList *pls = (RgPlacementList *)calloc(ms->n ? ms->n : 1u, sizeof(RgPlacementList));
    static const uint16_t rooms[13] = {54, 55, 56, 57, 58, 432, 59, 60, 61, 62, 63, 71, 94};
    unsigned r;

    (void)rom;
    CHECK(pls != NULL);
    printf("interior pieces %u, twins %u, exact reuse placements %u, bare (own-pixel) placements %u, rooms reused into %u, skipped %u\n",
           ms->nInterior, ms->nTwin, ms->nReuseExact, ms->nReuseBare, ms->nReuseRooms, ms->skipped);
    for (i = 0; i < ms->n; i++) {
        const RgBuildModel *m = &ms->m[i];
        RgOrthoResult o;
        unsigned bad = 99;

        if (m->spec->kind != RG_SPEC_INTERIOR)
            continue;
        if (m->own != NULL) nTwin++; else nInt++;
        bareTotal += m->nBareAt;
        reusedTotal += m->nReused;
        CHECK(rg_model_gate(m, &o, &bad));
        if (o.wrong || o.missing || o.extra || bad) {
            gateBad++;
            printf("GATE %-34s wrong %u missing %u extra %u dens %u (%ux%u, %u tris)\n", m->spec->name, o.wrong,
                   o.missing, o.extra, bad, m->w, m->h, m->mesh.n);
        }
    }
    printf("interior models %u, twins %u, gate failures %u, bareAt %u, reusedAt %u\n", nInt, nTwin, gateBad, bareTotal,
           reusedTotal);
    CHECK(nInt == 200 && nTwin == 19 && gateBad == 0 && bareTotal == 159 && reusedTotal == 332);
    CHECK(ms->nInterior == 200 && ms->nTwin == 19 && ms->nReuseExact == 173 && ms->nReuseBare == 159 && ms->nReuseRooms == 37);
    for (i = 0; i < ms->n; i++)
        CHECK(rg_find_placements(w, &ms->m[i], &pls[i]) == RG_OK);
    {   /* the rooms nobody modelled: a reused piece stands on the commonest floor with its whole rect patched */
        unsigned k, whole = 0, patched = 0, perRoom[13];

        memset(perRoom, 0, sizeof(perRoom));
        for (i = 0; i < ms->n; i++) {
            if (ms->m[i].spec->kind != RG_SPEC_INTERIOR)
                continue;
            for (r = 0; r < 13; r++)
                perRoom[r] += ms->m[i].own == NULL && ms->m[i].spec->layoutId == rooms[r];
            for (k = 0; k < pls[i].n; k++)
                if (pls[i].p[k].patchAll) {
                    whole += pls[i].p[k].nOdd == (unsigned)ms->m[i].w * ms->m[i].h;
                    patched += pls[i].p[k].nOdd > 0;
                }
        }
        printf("whole-room patched placements %u of %u; pieces per room:", whole, patched);
        for (r = 0; r < 13; r++)
            printf(" %u:%u", rooms[r], perRoom[r]);
        printf("\n");
        CHECK(whole > 0 && whole == patched);
    }
    for (r = 0; r < 13; r++) {
        RgRoomResult rr = rg_room_check(w, ms, pls, rooms[r]);

        printf("room_check layout %3u: bad %u first (%d,%d) owner %d err %d\n", rooms[r], rr.bad, rr.firstX, rr.firstY,
               rr.firstOwner, (int)rr.err);
        CHECK(rr.err == RG_OK && rr.bad == 0);
    }
    {   /* the negative control: the floor laid under every piece with the pieces' triangles taken out is NOT the room */
        RgBuildModels blank = *ms;
        unsigned bad;

        blank.m = (RgBuildModel *)malloc((size_t)ms->n * sizeof(RgBuildModel));
        CHECK(blank.m != NULL);
        memcpy(blank.m, ms->m, (size_t)ms->n * sizeof(RgBuildModel));
        for (i = 0; i < ms->n; i++)
            blank.m[i].mesh.n = 0;
        bad = rg_room_check(w, &blank, pls, 61).bad;
        printf("negative control (no triangles) room 61: %u pixels differ\n", bad);
        CHECK(bad > 1000);
        free(blank.m);
    }
    WriteAndRead(w, ms);
    for (i = 0; i < ms->n; i++)
        rg_placements_free(&pls[i]);
    free(pls);
}

static void TestRealRom(void)
{
    const char *path = getenv("ROMGEN_ROM");
    FILE *fp;
    long n;
    uint8_t *rom;
    RgWorld w;
    RgBuildModels ms;
    RgErr e;
    double t0;

    if (!path || !(fp = fopen(path, "rb"))) { sSkips++; printf("SKIP real-ROM interiors (set ROMGEN_ROM)\n"); return; }
    fseek(fp, 0, SEEK_END); n = ftell(fp); fseek(fp, 0, SEEK_SET);
    rom = (uint8_t *)malloc((size_t)n);
    CHECK(rom && fread(rom, 1, (size_t)n, fp) == (size_t)n);
    fclose(fp);
    CHECK(rg_world_open(&w, rom, (size_t)n) == RG_OK);
    t0 = Now();
    memset(&ms, 0, sizeof(ms));
    e = rg_build_models(&w, rg_specs, rg_spec_count, &ms);
    printf("build_models: err %d (%s) in %.1fs, models %u\n", (int)e, rg_err_str(e), Now() - t0, ms.n);
    CHECK(e == RG_OK);
    if (e == RG_OK)
        RealChecks(&w, &ms, rom);
    if (e == RG_OK)
        rg_models_free(&ms);
    rg_world_close(&w);
    free(rom);
}

int main(void)
{
    TestShapes();
    TestColours();
    TestTables();
    TestRealRom();
    printf("test_romgen_interior: %d checks, %d failures, %d skipped\n", sChecks, sFails, sSkips);
    return sFails ? 1 : 0;
}
