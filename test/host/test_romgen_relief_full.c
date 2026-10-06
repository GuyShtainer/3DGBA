// test_romgen_relief_full.c -- host test for phase 33 S3.7 (SPEC-S3 section 6): the cut tiles, cell shapes and the FULL
// relief.bin export (O1 I1, I5-I7, the consumer round trip, O4). Synthetic checks always run; the real-ROM checks run
// with ROMGEN_ROM=/path/emerald.gba (exported, else SKIP).
//
//   clang -std=c11 -Wall -Wextra -O2 -ffp-contract=off -fsanitize=address,undefined -DVOXEL_HOST_FILES \
//         -DCTR_VOXEL_LIGHTING=1 -I source/romgen -I source/voxel -I test/host test/host/test_romgen_relief_full.c \
//         source/romgen/rg_*.c source/voxel/<the VOXSRC list of tools/romgen/Makefile> -lm -o /tmp/trf && /tmp/trf
// (`make -C tools/romgen test T=relief_full` runs it.)
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "rg_ledge.h"
#include "rg_rcut.h"
#include "rg_rdrawn.h"
#include "rg_relief.h"
#include "rg_relief_write.h"
#include "rg_roles.h"
#include "rg_rshape.h"
#include "rg_rtables.h"
#include "rg_run.h"
#include "voxel_relief.h"

static int sChecks, sFails, sSkips;
#define CHECK(c) do { ++sChecks; if (!(c)) { ++sFails; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static uint32_t U16(const uint8_t *b, size_t o) { return (uint32_t)b[o] | ((uint32_t)b[o + 1] << 8); }
static uint32_t U32(const uint8_t *b, size_t o) { return U16(b, o) | (U16(b, o + 2) << 16); }

/* ---- synthetic: _lifted, spread, commonest, an empty shape input ---- */
static void TestLifted(void)
{
    RgGrid g;
    uint16_t full[16], none[16], half[16];
    double foot = -1.0;
    unsigned i, j, n;

    for (i = 0; i < 16; i++) { full[i] = 0xFFFFu; none[i] = 0; half[i] = 0x00FFu; }
    for (j = 0; j < RG_SIDE; j++) for (i = 0; i < RG_SIDE; i++) g.g[j][i] = 0.0;
    CHECK(rg_cut_lifted(&g, full, &foot) == 0 && foot == 0.0);
    for (j = 0; j < RG_SIDE; j++) for (i = 0; i < RG_SIDE; i++) g.g[j][i] = (j < 2 && i < 2) ? 0.0 : 10.0;
    n = rg_cut_lifted(&g, full, &foot);
    CHECK(foot == 0.0 && n > 100u && n < 256u);                 /* the corner quad stays on the foot, the rest is lifted */
    CHECK(rg_cut_lifted(&g, none, &foot) == 0);                 /* nothing of the background there: nothing lifted */
    CHECK(rg_cut_lifted(&g, half, &foot) < n);                  /* only the pixels the mask names count */
    for (j = 0; j < RG_SIDE; j++) for (i = 0; i < RG_SIDE; i++) g.g[j][i] = 2.0;
    CHECK(rg_cut_lifted(&g, full, &foot) == 0 && foot == 2.0);  /* lifted means MORE than CUT_LIFT over the foot */
    g.g[2][2] = 5.0;
    CHECK(rg_cut_lifted(&g, full, &foot) > 0 && foot == 2.0);
}

static void TestSpread(void)
{
    enum { W = 2, H = 3 };
    uint8_t content[W * H];
    RgLat h, out;
    static const double col[5] = {20.0, 16.0, 4.0, 2.0, 0.0};
    unsigned k, same = 0;
    size_t n;

    memset(content, 1, sizeof(content));
    CHECK(rg_lat_new(&h, W, H));
    for (k = 0; k < 5; k++) *rg_lat_at(&h, 4, 1 + (int)k) = col[k];     /* a 20-pixel fall down a column of rock */
    CHECK(rg_spread(&h, content, &out) == RG_OK);
    CHECK(*rg_lat_at(&out, 4, 1) == 20.0 && *rg_lat_at(&out, 4, 2) == 15.0 && *rg_lat_at(&out, 4, 3) == 10.0);
    CHECK(*rg_lat_at(&out, 4, 4) == 5.0 && *rg_lat_at(&out, 4, 5) == 0.0);   /* spread evenly, ends kept */
    n = (size_t)(W * 4 + 1) * (size_t)(H * 4 + 1);
    for (k = 0; k < n; k++) same += out.v[k] == h.v[k];
    CHECK(same == n - 3u);                                              /* nothing else moved */
    rg_lat_free(&out);
    memset(content, 0, sizeof(content));                                /* no rock: no spread */
    CHECK(rg_spread(&h, content, &out) == RG_OK && memcmp(out.v, h.v, n * sizeof(double)) == 0);
    rg_lat_free(&out);
    /* a run steeper than SPREAD * STEP per step stays: a cliff is a cliff */
    memset(content, 1, sizeof(content));
    for (k = 0; k < 5; k++) *rg_lat_at(&h, 4, 1 + (int)k) = col[k] * 2.0;
    CHECK(rg_spread(&h, content, &out) == RG_OK && memcmp(out.v, h.v, n * sizeof(double)) == 0);
    rg_lat_free(&out);
    rg_lat_free(&h);
}

static void TestEmptyShapes(void)
{
    RgShapeIn in;
    RgShapes s;
    RgLayout L;

    memset(&in, 0, sizeof(in));
    memset(&L, 0, sizeof(L));
    L.w = 3;
    L.h = 2;
    in.L = &L;                                   /* no group: nothing drawn, nothing to shape (rel:3170) */
    CHECK(rg_cell_shapes(&in, &s) == RG_OK && s.nFills == 0 && s.w == 3 && s.h == 2);
    CHECK(s.has != NULL && s.has[0] == 0 && s.hasWall[5] == 0);
    rg_shapes_free(&s);
    CHECK(s.grid == NULL && s.fills == NULL);
    CHECK(rg_rim_cells(&in, NULL, &s) == RG_OK && s.nFills == 0);
    rg_shapes_free(&s);
}

/* ---- the reader of the full file ---- */
typedef struct { unsigned id, cells, w, h, flags; int base; uint32_t off; } Row;
typedef struct { unsigned layout, x, y, variant; int foot; unsigned behind, wall, sides, flags; } Cut;

typedef struct File {
    const uint8_t *b;
    size_t sz;
    unsigned nRows, nVar, nCut;
    Row rows[128];
    uint32_t cutOff;
    const uint8_t *var;           /* nVar x 36 */
    const uint8_t *cut;           /* nCut x 14 */
} File;

static void ReadCut(const File *f, unsigned i, Cut *c)
{
    const uint8_t *p = f->cut + 14u * i;

    c->layout = U16(p, 0); c->x = p[2]; c->y = p[3]; c->variant = U16(p, 4); c->foot = (int16_t)U16(p, 6);
    c->behind = U16(p, 8); c->wall = U16(p, 10); c->sides = p[12]; c->flags = p[13];
}

static int CutCmp(const Cut *a, const Cut *b)
{
#define CMP(f) do { if (a->f != b->f) return a->f < b->f ? -1 : 1; } while (0)
    CMP(layout); CMP(x); CMP(y); CMP(variant); CMP(foot); CMP(behind); CMP(wall); CMP(sides); CMP(flags);
#undef CMP
    return 0;
}

static void Parse(File *f, const uint8_t *b, size_t sz)
{
    unsigned i, k;
    uint32_t end;

    memset(f, 0, sizeof(*f));
    f->b = b;
    f->sz = sz;
    CHECK(sz >= 20 && memcmp(b, "VXL4", 4) == 0 && U16(b, 6) == 5 && memcmp(b + sz - 4, "CUTS", 4) == 0);   /* I1 */
    f->nRows = U16(b, 4);
    CHECK(f->nRows <= 128);
    f->cutOff = U32(b, sz - 8);
    end = 8 + 14u * f->nRows;
    for (i = 0; i < f->nRows && i < 128; i++) {
        const uint8_t *r = b + 8 + 14u * i;

        f->rows[i] = (Row){U16(r, 0), U16(r, 2), U16(r, 4), U16(r, 6) & 0x3FFFu, U16(r, 6) >> 14, (int16_t)U16(r, 12), U32(r, 8)};
        CHECK(i == 0 || f->rows[i].id > f->rows[i - 1].id);
        CHECK(f->rows[i].off == end);
        end += 27u * f->rows[i].cells;
        for (k = 0; k < f->rows[i].cells; k++) {
            const uint8_t *c = b + f->rows[i].off + 27u * k;

            CHECK(c[0] < f->rows[i].w && c[1] < f->rows[i].h);
            CHECK(k == 0 || c[1] > c[-27 + 1] || (c[1] == c[-27 + 1] && c[0] > c[-27]));
        }
    }
    CHECK(end == f->cutOff);
    f->nVar = U16(b, f->cutOff);
    f->nCut = U16(b, f->cutOff + 2u);
    f->var = b + f->cutOff + 4u;
    f->cut = f->var + 36u * f->nVar;
    CHECK(f->cut + 14u * f->nCut + 8u == b + sz);
}

static const Row *RowOf(const File *f, unsigned id)
{
    unsigned i;

    for (i = 0; i < f->nRows; i++)
        if (f->rows[i].id == id)
            return &f->rows[i];
    return NULL;
}

static const uint8_t *CellOf(const File *f, const Row *r, unsigned x, unsigned y)
{
    unsigned k;

    for (k = 0; k < r->cells; k++) {
        const uint8_t *c = f->b + r->off + 27u * k;

        if (c[0] == x && c[1] == y)
            return c;
    }
    return NULL;
}

/* I5: the cut table's own invariants; `order[id]` = position of a layout in the export order */
static void CheckCuts(const File *f, const unsigned *order)
{
    unsigned i, v, withVariant = 0, walls = 0, goes = 0, flat = 0;
    uint8_t *used = (uint8_t *)calloc(f->nVar ? f->nVar : 1u, 1);
    unsigned *first = (unsigned *)malloc((f->nVar ? f->nVar : 1u) * sizeof(unsigned));
    Cut prev, c;

    for (v = 0; v < f->nVar; v++) first[v] = 0xFFFFFFFFu;
    for (i = 0; i < f->nRows; i++) {
        CHECK(!(f->rows[i].flags & 1) || (f->rows[i].flags & 2));       /* bit 14 only with bit 15 */
    }
    for (i = 0; i < f->nCut; i++) {
        const Row *r;

        ReadCut(f, i, &c);
        if (i > 0) CHECK(CutCmp(&prev, &c) < 0);
        r = RowOf(f, c.layout);
        CHECK(r != NULL && (r->flags & 2));                            /* only drawn rows have cuts */
        CHECK(r != NULL && CellOf(f, r, c.x, c.y) != NULL);            /* every cut is a written cell */
        CHECK(c.variant == 0xFFFFu || c.variant < f->nVar);
        if (c.variant != 0xFFFFu && c.variant < f->nVar) {
            withVariant++;
            used[c.variant] = 1;
            if (first[c.variant] == 0xFFFFFFFFu || order[c.layout] < order[first[c.variant]]) first[c.variant] = c.layout;
        }
        walls += c.wall != 0xFFFFu;
        goes += (c.flags & 2u) != 0;
        flat += (c.flags & 1u) != 0;
        CHECK((c.flags & ~3u) == 0 && (c.sides == 0xFFu || c.wall != 0xFFFFu));
        prev = c;
    }
    for (v = 0; v < f->nVar; v++) {
        CHECK(used[v]);                                                 /* every variant is used */
        CHECK(first[v] == U16(f->var, 36u * v));                        /* firstLayout: the first in export order */
    }
    printf("  cut table: %u variants, %u cuts (%u cut tiles, %u with walls, %u go-on, %u flat-background)\n", f->nVar,
           f->nCut, withVariant, walls, goes, flat);
    CHECK(withVariant > 100 && walls > 100 && goes > 0 && flat > 0);
    free(used);
    free(first);
}

/* I6: across a link between two drawn members of one group the shared lattice edge agrees within 1 px */
static int Abs(const Row *r, const uint8_t *c, int i, int j)
{
    return (int8_t)c[2 + j * 5 + i] * ((r->flags & 1) ? 2 : 1) + r->base;
}

static void CheckSeams(const File *f, const RgDrawn *d, const RgLevels *lv)
{
    unsigned k, pairs = 0, points = 0, bad = 0, soft = 0;

    for (k = 0; k < d->nLinks; k++) {
        const RgDrawnLink *l = &d->links[k];
        const Row *ra = RowOf(f, l->a), *rb = RowOf(f, l->b);
        const RgDrawnGroup *ga = rg_drawn_group(d, l->a, true, lv->ok), *gb = rg_drawn_group(d, l->b, true, lv->ok);
        unsigned q;

        if (ra == NULL || rb == NULL || ga == NULL || ga != gb || !(ra->flags & 2) || !(rb->flags & 2))
            continue;
        pairs++;
        for (q = 0; q < rb->cells; q++) {
            const uint8_t *cb = f->b + rb->off + 27u * q;
            int gx = cb[0] + l->dx, gy = cb[1] + l->dy, ax, ay;

            for (ay = gy - 1; ay <= gy + 1; ay++) for (ax = gx - 1; ax <= gx + 1; ax++) {
                const uint8_t *ca;
                int X, Y;

                if (ax < 0 || ay < 0 || ax >= (int)ra->w || ay >= (int)ra->h || (ca = CellOf(f, ra, (unsigned)ax, (unsigned)ay)) == NULL) continue;
                for (Y = (ay > gy ? ay : gy) * 4; Y <= (ay < gy ? ay : gy) * 4 + 4; Y++)
                    for (X = (ax > gx ? ax : gx) * 4; X <= (ax < gx ? ax : gx) * 4 + 4; X++) {
                        int va = Abs(ra, ca, X - ax * 4, Y - ay * 4), vb = Abs(rb, cb, X - gx * 4, Y - gy * 4);

                        points++;
                        if (abs(va - vb) > 1) { if (abs(va - vb) < 8) soft++; if (bad < 3) printf("  seam %u|%u d(%d,%d) at (%d,%d): %d vs %d  A cell (%d,%d) pt (%d,%d) B cell (%d,%d) pt (%d,%d)\n", l->a, l->b, l->dx, l->dy, X, Y, va, vb, ax, ay, X - ax * 4, Y - ay * 4, cb[0], cb[1], X - gx * 4, Y - gy * 4); bad++; }
                    }
            }
        }
    }
    printf("  seams: %u linked drawn pairs, %u shared lattice points compared, %u apart by more than 1 px\n", pairs, points, bad);
    /* upstream raises a shared point between two side-by-side rock cells to the higher one, on the map edge too (rel:3386-3406
     * has no seam guard): that is where the two sides part, by a cliff top each time. Pinned as the port reproduces it. */
    CHECK(pairs > 20 && points > 500 && soft == 0 && bad == 104);
}

/* I7: the ledge rows of layouts that are neither drawn nor ENABLED are the S3a rows, byte for byte (base aside) */
static void CheckLedgesSame(const File *full, const File *led)
{
    unsigned i, same = 0;

    for (i = 0; i < led->nRows; i++) {
        const Row *lr = &led->rows[i], *fr = RowOf(full, lr->id);

        CHECK(fr != NULL);
        if (fr == NULL || (fr->flags & 2) || lr->id == RG_ENABLED[0] || lr->id == RG_ENABLED[1]) continue;
        CHECK(fr->cells == lr->cells && fr->w == lr->w && fr->h == lr->h && fr->flags == 0);
        CHECK(fr->cells == lr->cells && memcmp(full->b + fr->off, led->b + lr->off, 27u * lr->cells) == 0);
        same++;
    }
    printf("  ledge rows identical to S3a: %u of %u\n", same, led->nRows);
    CHECK(same >= 1);                                           /* most ledge layouts are drawn (their rows are the drawn rows) */
}

/* the consumer: write the file where VoxelRelief_Init reads it and compare through its public queries */
static void Consume(const File *f)
{
    char dir[64] = "/tmp/rgfull.XXXXXX", sub[96], cwd[1024], p[128];
    FILE *fp;
    unsigned i, k, seen = 0, cutsSeen = 0;

    CHECK(mkdtemp(dir) != NULL);
    CHECK(getcwd(cwd, sizeof(cwd)) != NULL);
    snprintf(sub, sizeof(sub), "%s/voxel", dir);
    CHECK(mkdir(sub, 0755) == 0);
    CHECK(chdir(dir) == 0);
    fp = fopen("voxel/relief.bin", "wb");
    CHECK(fp != NULL && fwrite(f->b, 1, f->sz, fp) == f->sz);
    if (fp) fclose(fp);
    CHECK(VoxelRelief_Init());
    CHECK(VoxelRelief_CutCount() == f->nVar);
    for (i = 0; i < f->nVar && i < VoxelRelief_CutCount(); i++) {
        unsigned lay = 0, mt = 0;
        const uint8_t *rows = NULL;

        CHECK(VoxelRelief_CutVariant(i, &lay, &mt, &rows) || i >= f->nVar);
        (void)lay; (void)mt; (void)rows;
    }
    for (i = 0; i < f->nRows; i++) {
        const Row *r = &f->rows[i];
        VoxelMapInstance inst;

        memset(&inst, 0, sizeof(inst));
        inst.layoutId = (int)r->id;
        CHECK(VoxelRelief_IsDrawn(&inst) == ((r->flags & 2) != 0));
        for (k = 0; k < r->cells; k++) {
            const uint8_t *c = f->b + r->off + 27u * k;
            const int16_t *g = VoxelRelief_Cell(&inst, c[0], c[1]);
            unsigned j;

            CHECK(g != NULL);
            for (j = 0; g != NULL && j < 25; j++) CHECK(g[j] == (int8_t)c[2 + j] * (int)((r->flags & 1) ? 2 : 1));
            seen++;
        }
    }
    for (i = 0; i < f->nCut; i++) {
        Cut c;
        VoxelMapInstance inst;
        float foot = 0;
        int ground = 0, wall = 0, v;
        unsigned sides = 0;

        ReadCut(f, i, &c);
        memset(&inst, 0, sizeof(inst));
        inst.layoutId = (int)c.layout;
        v = VoxelRelief_Cut(&inst, (int)c.x, (int)c.y, &foot, &ground, &wall, &sides);
        CHECK(v == (int)c.variant);
        CHECK(foot == (float)c.foot / 16.0f && sides == (c.sides | (c.flags << 8)));
        CHECK(ground == (c.behind == 0xFFFFu ? -1 : (int)c.behind));
        CHECK(wall == (c.wall == 0xFFFFu ? -1 : (int)c.wall));
        cutsSeen++;
    }
    CHECK(seen > 1000 && cutsSeen == f->nCut);
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

/* the export order of section 4: ENABLED, every group's members, the ledge layouts, then the rest by id (the world-lifted
 * rest is in rank order upstream; only the first three parts can fix a variant's firstLayout) */
static void ExportOrder(const RgWorld *w, const RgDrawn *d, unsigned *order)
{
    uint16_t ids[512];
    unsigned n = 0, i, nl;

    for (i = 0; i < 512; i++) order[i] = 0xFFFFFFFFu;
#define ADD(l) do { if (order[(l)] == 0xFFFFFFFFu) order[(l)] = n++; } while (0)
    ADD(RG_ENABLED[0]);
    ADD(RG_ENABLED[1]);
    for (i = 0; i < d->nPool; i++) ADD(d->pool[i].layout);
    nl = rg_ledge_layouts(w, ids, 512);
    for (i = 0; i < nl; i++) ADD(ids[i]);
    for (i = 1; i < 512; i++) ADD(i);
#undef ADD
}

static void TestRealRom(void)
{
    const char *path = getenv("ROMGEN_ROM");
    size_t n = 0, romSz = 0, ledSz = 0, again = 0;
    uint8_t *rom, *blob = NULL, *led = NULL, *blob2 = NULL;
    RgWorld w;
    RgRoles r;
    RgReliefStats st;
    RgOutput o;
    RgRunOpts opts;
    RgDrawn *d;
    RgLevels *lv;
    File f, fl;
    unsigned order[512], i, drawnRows = 0, unit2 = 0;
    clock_t t0;

    if (path == NULL) { printf("SKIP real-ROM checks (ROMGEN_ROM unset)\n"); sSkips++; return; }
    rom = ReadAll(path, &romSz);
    if (rom == NULL) { printf("SKIP cannot read %s\n", path); sSkips++; return; }
    CHECK(rg_world_open(&w, rom, romSz) == RG_OK);
    CHECK(rg_roles_init(&w, &r) == RG_OK && rg_roles_all(&w, &r, false, NULL, NULL) == RG_OK);
    d = (RgDrawn *)malloc(sizeof(*d));
    lv = (RgLevels *)calloc(1, sizeof(*lv));
    CHECK(d != NULL && lv != NULL && rg_drawn_find(&w, d) == RG_OK);
    memset(&st, 0, sizeof(st));
    t0 = clock();
    CHECK(rg_relief_build(&w, &r, RG_RELIEF_FULL, NULL, NULL, NULL, &blob, &n, &st) == RG_OK && blob != NULL);
    printf("  FULL build: %.0f ms\n", 1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC);
    CHECK(rg_relief_build(&w, &r, RG_RELIEF_LEDGES, NULL, NULL, NULL, &led, &ledSz, NULL) == RG_OK && led != NULL);
    if (blob == NULL || led == NULL) { free(rom); return; }
    Parse(&f, blob, n);
    Parse(&fl, led, ledSz);
    ExportOrder(&w, d, order);
    CHECK(st.rows == f.nRows && st.variants == f.nVar && st.cuts == f.nCut);
    for (i = 0; i < f.nRows; i++) { drawnRows += (f.rows[i].flags & 2) != 0; unit2 += (f.rows[i].flags & 1) != 0; }
    CHECK(st.drawnRows == drawnRows && drawnRows > 40 && unit2 > 0);
    printf("  relief.bin: %zu bytes, %u rows (%u drawn, %u at unit 2), %u cells, %u variants, %u cuts\n", n, f.nRows,
           drawnRows, unit2, st.cells, f.nVar, f.nCut);
    CheckCuts(&f, order);
    CheckLedgesSame(&f, &fl);
    {   /* the levels of the same world, for the seam check's group test */
        RgRCtx *ctx;
        RgPrep *preps = (RgPrep *)calloc(d->nGroups, sizeof(RgPrep));
        RgWorldOpts o2 = rg_world_opts_default();
        RgErr e = RG_OK;

        ctx = rg_rctx_new(&w, &r, &e);
        CHECK(ctx != NULL && preps != NULL && rg_world_prep_all(ctx, d, preps) == RG_OK);
        CHECK(rg_world_levels(&w, d, preps, &o2, lv) == RG_OK);
        CheckSeams(&f, d, lv);
        rg_levels_free(lv);
        rg_world_prep_free(preps, d->nGroups);
        free(preps);
        rg_rctx_free(ctx);
    }
    Consume(&f);
    /* O4: a second build, and rg_run's own relief output, are byte-identical */
    CHECK(rg_relief_build(&w, &r, RG_RELIEF_FULL, NULL, NULL, NULL, &blob2, &again, NULL) == RG_OK);
    CHECK(again == n && memcmp(blob2, blob, n) == 0);
    memset(&opts, 0, sizeof(opts));
    opts.relief = RG_RELIEF_FULL;
    CHECK(rg_run(rom, romSz, &opts, &o) == RG_OK);
    CHECK(o.relief != NULL && o.reliefSize == n && memcmp(o.relief, blob, n) == 0 && o.rst.cuts == st.cuts);
    rg_output_free(&o);
    {   /* FULL reads the roles: none given is refused */
        RgRoles none;
        uint8_t *x = NULL;
        size_t xs = 0;

        memset(&none, 0, sizeof(none));
        CHECK(rg_relief_build(&w, &none, RG_RELIEF_FULL, NULL, NULL, NULL, &x, &xs, NULL) == RG_ERR_RELIEF && x == NULL);
    }
    free(blob2);
    free(blob);
    free(led);
    free(d);
    free(lv);
    rg_roles_free(&r);
    rg_world_close(&w);
    free(rom);
}

int main(void)
{
    TestLifted();
    TestSpread();
    TestEmptyShapes();
    TestRealRom();
    printf("test_romgen_relief_full: %d checks, %d failures%s\n", sChecks, sFails, sSkips ? " (SKIPPED real-ROM parts)" : "");
    return sFails != 0;
}
