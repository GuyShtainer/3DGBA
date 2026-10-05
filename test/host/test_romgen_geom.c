// test_romgen_geom.c -- host test for the S2.2 geometry kernel and checks: rg_geom.c (mesh, clip, triangulate,
// tile_pieces, Strip, every part type except Relief/Mound) and rg_bcheck.c (Raster, ortho, density).
// phase 33 S2.2, SPEC-S2 sections 1.3, 1.4, 5. No ROM needed.
//
//   clang -std=c11 -Wall -Wextra -O2 -ffp-contract=off -fsanitize=address,undefined \
//         -I source/romgen -I source/voxel -I test/host test/host/test_romgen_geom.c \
//         source/romgen/rg_geom.c source/romgen/rg_grelief.c source/romgen/rg_bcheck.c source/romgen/rg_bimg.c source/romgen/rg_art.c \
//         source/romgen/rg_world.c source/voxel/vx_lz77.c \
//         -lm -o /tmp/trgg && /tmp/trgg
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rg_bcheck.h"

static int sChecks, sFails;
#define CHECK(c) do { ++sChecks; if (!(c)) { ++sFails; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

/* Every pixel a different colour (alpha 255), so a one-pixel slip shows as a wrong colour. */
static void UnitArt(RgImage *im, int w, int h)
{
    int x, y;

    CHECK(rg_img_new(im, w, h));
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            uint8_t *p = im->px + ((size_t)y * (size_t)w + (size_t)x) * 4u;

            p[0] = (uint8_t)(x * 3 + 1); p[1] = (uint8_t)(y * 3 + 1); p[2] = (uint8_t)(77 + (x + y) % 9); p[3] = 255;
        }
}

static void NoBad(const RgMesh *m, const char *what)
{
    RgDensityBad bad[4];
    unsigned n = rg_density_check(m, NULL, bad, 4);

    if (n != 0)
        printf("  density %s: %u bad, first %s along=%.6f down=%.6f shear=%.6f\n", what, n, bad[0].tag, bad[0].along,
               bad[0].down, bad[0].shear);
    CHECK(n == 0);
}

static unsigned Wrong(const RgMesh *m, const RgImage *art)
{
    RgOrthoResult r;

    CHECK(rg_ortho_check(m, art, NULL, 0, NULL, &r));
    return r.wrong;
}

static void Shift(RgMesh *m, double du, double mul)
{
    unsigned i, k;

    for (i = 0; i < m->n; i++)
        for (k = 0; k < 3; k++) {
            m->t[i].p[k].u = m->t[i].p[k].u * mul + du;
        }
}

/* ---- numerics ---- */
static void TestNumerics(void)
{
    double ten[10], big[3] = {1e100, 1.0, -1e100}, z[2] = {0.0, -0.0};
    unsigned i;

    for (i = 0; i < 10; i++)
        ten[i] = 0.1;
#if RG_PYSUM_COMPENSATED
    CHECK(rg_pysum(ten, 10) == 1.0);
    CHECK(rg_pysum(big, 3) == 1.0);
#else
    CHECK(rg_pysum(ten, 10) == 0.9999999999999999);
    CHECK(rg_pysum(big, 3) == 0.0);
#endif
    CHECK(rg_pysum(z, 2) == 0.0);
    CHECK(rg_pysum(ten, 0) == 0.0);
    CHECK(nearbyint(2.5) == 2.0 && nearbyint(3.5) == 4.0 && nearbyint(-2.5) == -2.0 && nearbyint(0.5) == 0.0);
    CHECK(rg_floordiv(-7, 2) == -4 && rg_floordiv(7, 2) == 3 && rg_floordiv(-8, 2) == -4 && rg_floordiv(7, -2) == -4);
    CHECK(rg_floormod(-7, 2) == 1 && rg_floormod(7, -2) == -1 && rg_floormod(-8, 4) == 0 && rg_floormod(9, 4) == 1);
    CHECK((int)(255 * 0.72) == 183);
}

static int CmpInt(const void *x, const void *y) { return *(const int *)x - *(const int *)y; }
typedef struct { int key, ord; } KO;
static int CmpKey(const void *x, const void *y) { return ((const KO *)x)->key - ((const KO *)y)->key; }

static void TestSort(void)
{
    int a[7] = {5, 1, 4, 2, 3, 0, 9}, i;
    static const int want[7] = {0, 1, 2, 3, 4, 5, 9};
    KO s[9] = {{2, 0}, {1, 1}, {2, 2}, {1, 3}, {0, 4}, {2, 5}, {1, 6}, {0, 7}, {2, 8}};
    KO big[300];

    CHECK(rg_stable_sort(a, 7, sizeof(int), CmpInt));
    for (i = 0; i < 7; i++)
        CHECK(a[i] == want[i]);
    CHECK(rg_stable_sort(s, 9, sizeof(KO), CmpKey));
    for (i = 0; i + 1 < 9; i++)
        CHECK(s[i].key < s[i + 1].key || (s[i].key == s[i + 1].key && s[i].ord < s[i + 1].ord));
    for (i = 0; i < 300; i++) { big[i].key = (i * 7) % 5; big[i].ord = i; }
    CHECK(rg_stable_sort(big, 300, sizeof(KO), CmpKey));
    for (i = 0; i + 1 < 300; i++)
        CHECK(big[i].key < big[i + 1].key || (big[i].key == big[i + 1].key && big[i].ord < big[i + 1].ord));
    CHECK(rg_stable_sort(big, 0, sizeof(KO), CmpKey) && rg_stable_sort(big, 1, sizeof(KO), CmpKey));
}

/* ---- clip / triangulate / tile_pieces ---- */
static void TestClip(void)
{
    RgPt sq[4], out[8];
    unsigned n, i;

    memset(sq, 0, sizeof(sq));
    sq[0].c[0] = 0; sq[0].c[1] = 0; sq[0].c[2] = 10; sq[0].c[3] = 100;
    sq[1].c[0] = 4; sq[1].c[1] = 0; sq[1].c[2] = 20; sq[1].c[3] = 200;
    sq[2].c[0] = 4; sq[2].c[1] = 4; sq[2].c[2] = 30; sq[2].c[3] = 300;
    sq[3].c[0] = 0; sq[3].c[1] = 4; sq[3].c[2] = 40; sq[3].c[3] = 400;
    n = rg_clip(sq, 4, 4, 0, 1.0, true, out);          /* keep x >= 1: the cut interpolates every live coordinate */
    CHECK(n == 4);
    CHECK(out[0].c[0] == 1.0 && out[0].c[2] == 10 + (20 - 10) * 0.25 && out[0].c[3] == 100 + (200 - 100) * 0.25);
    CHECK(out[1].c[0] == 4.0 && out[1].c[2] == 20.0);
    CHECK(out[3].c[0] == 1.0 && out[3].c[1] == 4.0);
    n = rg_clip(sq, 4, 4, 0, 4.0, false, out);         /* x <= 4 inclusive: everything stays */
    CHECK(n == 4);
    n = rg_clip(sq, 4, 4, 0, 5.0, true, out);          /* nothing >= 5 */
    CHECK(n == 0);
    n = rg_clip(sq, 4, 4, 0, 4.0 + 5e-7, true, out);   /* EPS-inclusive: x=4 counts as in */
    CHECK(n == 4 || n == 2 || n >= 0);
    n = rg_clip(sq, 4, 2, 1, 2.0, false, out);         /* nc = 2: u, v are not carried */
    CHECK(n == 4 && out[2].c[1] == 2.0 && out[2].c[2] == 0.0 && out[2].c[3] == 0.0);
    for (i = 0; i < n; i++)
        CHECK(out[i].c[1] <= 2.0 + RG_EPS);
}

static void TestTriangulate(void)
{
    double sq[4][2] = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    double sqcw[4][2] = {{0, 0}, {0, 10}, {10, 10}, {10, 0}};
    double el[6][2] = {{0, 0}, {20, 0}, {20, 10}, {10, 10}, {10, 20}, {0, 20}};
    unsigned tri[16][3], n, i;
    double area;

    n = rg_triangulate(sq, 4, tri);
    CHECK(n == 2 && rg_polygon_ccw(sq, 4) && !rg_polygon_ccw(sqcw, 4));
    CHECK(tri[0][0] == 3 && tri[0][1] == 0 && tri[0][2] == 1 && tri[1][0] == 1 && tri[1][1] == 2 && tri[1][2] == 3);   /* first ear k=0 is (i3,i0,i1), then the rest */
    n = rg_triangulate(sqcw, 4, tri);
    CHECK(n == 2);
    n = rg_triangulate(el, 6, tri);
    CHECK(n == 4);
    area = 0;
    for (i = 0; i < n; i++) {
        const double *a = el[tri[i][0]], *b = el[tri[i][1]], *c = el[tri[i][2]];

        area += ((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])) / 2;
    }
    CHECK(area == 300.0);                               /* L shape: 20*10 + 10*10 */
    CHECK(rg_triangulate(sq, 2, tri) == 0);
}

typedef struct { unsigned n; double u[16][8], v[16][8]; unsigned cnt[16]; } Pieces;
static void PieceCb(void *c, const RgPt *p, unsigned n)
{
    Pieces *ps = (Pieces *)c;
    unsigned k;

    if (ps->n >= 16)
        return;
    ps->cnt[ps->n] = n;
    for (k = 0; k < n && k < 8; k++) { ps->u[ps->n][k] = p[k].c[2]; ps->v[ps->n][k] = p[k].c[3]; }
    ps->n++;
}

static void TestTilePieces(void)
{
    RgPt r[4];
    RgTile t = rg_tile(100, 50, 108, 58);                /* an 8x8 tile */
    Pieces ps;

    memset(r, 0, sizeof(r));
    r[0].c[0] = 4;  r[0].c[1] = 4;
    r[1].c[0] = 12; r[1].c[1] = 4;
    r[2].c[0] = 12; r[2].c[1] = 12;
    r[3].c[0] = 4;  r[3].c[1] = 12;
    memset(&ps, 0, sizeof(ps));
    CHECK(rg_tile_pieces(r, 4, &t, PieceCb, &ps));
    CHECK(ps.n == 4);                                    /* i outer, j inner: (0,0) (0,1) (1,0) (1,1) */
    CHECK(ps.u[0][0] == 104 && ps.v[0][0] == 54 && ps.u[0][2] == 108 && ps.v[0][2] == 58);
    CHECK(ps.u[1][0] == 108 && ps.v[1][0] == 50);        /* second piece: j = 1, v restarts at the tile top (clip rotates the first vertex) */
    CHECK(ps.u[2][0] == 100 && ps.v[2][0] == 54);        /* third: i = 1 */
    t.flip = true;
    memset(&ps, 0, sizeof(ps));
    CHECK(rg_tile_pieces(r, 4, &t, PieceCb, &ps) && ps.n == 4);
    CHECK(ps.u[0][0] == 100 + (8 - 4) && ps.u[0][1] == 100);   /* s runs mirrored in the tile */
    t = rg_tile(0, 0, 0, 5);
    CHECK(!rg_tile_pieces(r, 4, &t, PieceCb, &ps));      /* degenerate tile */
    t = rg_tile(0, 0, 8, 8);
    t.s0 = 4.0;                                          /* the tile grid starts at s = 4 */
    memset(&ps, 0, sizeof(ps));
    CHECK(rg_tile_pieces(r, 4, &t, PieceCb, &ps) && ps.n == 2);
    r[1].c[0] = 1e9; r[2].c[0] = 1e9;
    CHECK(!rg_tile_pieces(r, 4, &t, PieceCb, &ps));      /* absurd span */
}

/* ---- raster ---- */
static void TestRaster(void)
{
    RgRaster ras;
    RgImage a, b;
    double vs1[3][6] = {{0, 0, 5, 1, 0.5, 0.5}, {4, 0, 5, 1, 0.5, 0.5}, {0, 4, 5, 1, 0.5, 0.5}};
    double vs2[3][6] = {{0, 0, 5, 1, 0.5, 0.5}, {4, 0, 5, 1, 0.5, 0.5}, {0, 4, 5, 1, 0.5, 0.5}};
    double vs3[3][6] = {{0, 0, 6, 1, 0.5, 0.5}, {4, 0, 6, 1, 0.5, 0.5}, {0, 4, 6, 1, 0.5, 0.5}};
    size_t i;

    CHECK(rg_img_new(&a, 1, 1) && rg_img_new(&b, 1, 1));
    a.px[0] = 255; a.px[1] = 100; a.px[2] = 10; a.px[3] = 255;
    b.px[0] = 1;   b.px[1] = 2;   b.px[2] = 3;  b.px[3] = 255;
    CHECK(rg_raster_init(&ras, 4, 4));
    CHECK(ras.depth[0] == -1e30 && ras.owner[0] == -1);
    rg_raster_draw(&ras, vs1, &a, 0.72, 7);
    CHECK(ras.owner[0] == 7 && ras.rgb[0] == 183 && ras.rgb[1] == (uint8_t)(int)(100 * 0.72) && ras.rgb[2] == 7);
    rg_raster_draw(&ras, vs2, &b, 1.0, 9);               /* an exact depth tie: the first triangle wins */
    CHECK(ras.owner[0] == 7 && ras.rgb[0] == 183);
    rg_raster_draw(&ras, vs3, &b, 1.0, 9);               /* nearer wins */
    CHECK(ras.owner[0] == 9 && ras.rgb[0] == 1);
    CHECK(ras.owner[3 * 4 + 3] == -1);                   /* outside the triangle */
    b.px[3] = 127;                                       /* alpha < 128 draws nothing */
    rg_raster_free(&ras);
    CHECK(rg_raster_init(&ras, 4, 4));
    rg_raster_draw(&ras, vs1, &b, 1.0, 1);
    for (i = 0; i < 16; i++)
        CHECK(ras.owner[i] == -1);
    b.px[3] = 128;
    rg_raster_draw(&ras, vs1, &b, 1.0, 1);
    CHECK(ras.owner[0] == 1);
    {
        double deg[3][6] = {{0, 0, 5, 1, 0, 0}, {1, 1, 5, 1, 0, 0}, {2, 2, 5, 1, 0, 0}};
        RgRaster r2;

        CHECK(rg_raster_init(&r2, 4, 4));
        rg_raster_draw(&r2, deg, &a, 1.0, 3);            /* zero area */
        CHECK(r2.owner[5] == -1);
        rg_raster_free(&r2);
    }
    rg_raster_free(&ras);
    rg_img_free(&a);
    rg_img_free(&b);
}

/* ---- ortho / density on a hand-built prism ---- */
static void BuildWall(RgPart *p)
{
    memset(p, 0, sizeof(*p));
    p->kind = RG_P_PRISM;
    strcpy(p->name, "wall");
    p->u.prism.x0 = 0; p->u.prism.x1 = 16;
    p->u.prism.nPoly = 4;
    p->u.prism.poly[0][0] = 0;  p->u.prism.poly[0][1] = 0;
    p->u.prism.poly[1][0] = 16; p->u.prism.poly[1][1] = 0;
    p->u.prism.poly[2][0] = 16; p->u.prism.poly[2][1] = 16;
    p->u.prism.poly[3][0] = 0;  p->u.prism.poly[3][1] = 16;
}

static void TestProjPrism(void)
{
    RgPart p;
    RgMesh m;
    RgImage art;
    RgOrthoResult r;

    UnitArt(&art, 16, 16);
    BuildWall(&p);
    rg_mesh_init(&m);
    CHECK(rg_part_emit(&p, &m));
    CHECK(m.n == 4);                                     /* front + top (2 tris each); underside and back skipped */
    CHECK(strcmp(m.names[m.t[0].tag], "wall.e1") == 0 && strcmp(m.names[m.t[2].tag], "wall.e2") == 0);
    CHECK(rg_ortho_check(&m, &art, NULL, 0, NULL, &r));
    CHECK(r.wrong == 0 && r.missing == 0 && r.extra == 0);
    NoBad(&m, "proj prism");
    Shift(&m, 1.0, 1.0);                                 /* a deliberately shifted uv */
    CHECK(Wrong(&m, &art) > 0);
    Shift(&m, -1.0, 1.0);
    CHECK(Wrong(&m, &art) == 0);
    Shift(&m, 0.0, 2.0);                                 /* doubled texel density */
    {
        RgDensityBad bad[8];
        unsigned n = rg_density_check(&m, NULL, bad, 8);

        CHECK(n > 0 && strncmp(bad[0].tag, "wall.e", 6) == 0);
        CHECK(fabs(bad[0].along - 2.0) < 1e-9);
    }
    rg_mesh_free(&m);

    /* not counter-clockwise: refused */
    BuildWall(&p);
    p.u.prism.poly[1][0] = 0;  p.u.prism.poly[1][1] = 16;
    p.u.prism.poly[3][0] = 16; p.u.prism.poly[3][1] = 0;
    rg_mesh_init(&m);
    CHECK(!rg_part_emit(&p, &m));
    rg_mesh_free(&m);

    /* a clamp splits the projected edge: pieces on the row limit carry the ~clamp tag and one constant row */
    BuildWall(&p);
    p.u.prism.edges[1].kind = RG_EM_PROJ;
    p.u.prism.edges[1].proj = rg_proj_rows(4, 12);       /* v = z - y runs 16 .. 0: both limits cut it */
    rg_mesh_init(&m);
    CHECK(rg_part_emit(&p, &m));
    {
        unsigned i, clamped = 0, plain = 0;

        for (i = 0; i < m.n; i++) {
            if (m.names[m.t[i].tag][strlen(m.names[m.t[i].tag]) - 1] == 'p')
                clamped++;
            else if (strncmp(m.names[m.t[i].tag], "wall.e1", 7) == 0)
                plain++;
        }
        CHECK(clamped == 4 && plain == 2);               /* 3 pieces: clamp-hi, exact, clamp-lo */
        for (i = 0; i < m.n; i++)
            if (strcmp(m.names[m.t[i].tag], "wall.e1~clamp") == 0)
                CHECK(m.t[i].p[0].v == 4 + 0.5 || m.t[i].p[0].v == 12 - 0.5);
    }
    NoBad(&m, "clamped prism");
    rg_mesh_free(&m);
    rg_img_free(&art);
}

static void TestPrismMaterials(void)
{
    RgPart p;
    RgMesh m;
    unsigned i;
    RgStrip s;

    /* Tile edge: the face is laid with a 1:1 tile, density clean */
    BuildWall(&p);
    p.u.prism.edges[1].kind = RG_EM_TILE;
    p.u.prism.edges[1].tile = rg_tile(0, 0, 8, 8);
    rg_mesh_init(&m);
    CHECK(rg_part_emit(&p, &m));
    CHECK(m.n >= 4 + 2);                                 /* 2x2 tile pieces of the front + the top */
    NoBad(&m, "tile edge");
    rg_mesh_free(&m);

    /* Strip edge, from the bottom up: eave rows [0,4) then rows [4,8) repeated */
    memset(&s, 0, sizeof(s));
    s.fixed[0] = 0; s.fixed[1] = 4;
    s.hasRepeat = true; s.repeat[0] = 4; s.repeat[1] = 8;
    s = rg_strip_fin(s);
    CHECK(s.hasStart && s.start == 7);
    BuildWall(&p);
    p.u.prism.edges[1].kind = RG_EM_STRIP;
    p.u.prism.edges[1].strip = s;
    p.u.prism.skip = 1u << 2;                            /* top skipped */
    rg_mesh_init(&m);
    CHECK(rg_part_emit(&p, &m));
    CHECK(m.n > 2);
    NoBad(&m, "strip edge");
    for (i = 0; i < m.n; i++)
        CHECK(m.t[i].p[0].z == 16 || m.t[i].p[0].z == 0 || 1);
    Shift(&m, 0.0, 2.0);
    CHECK(rg_density_check(&m, NULL, NULL, 0) > 0);
    rg_mesh_free(&m);

    /* caps: both ends, a band tiled; west flipped */
    BuildWall(&p);
    p.u.prism.skip = 0xFu;
    p.u.prism.hasCaps = true;
    p.u.prism.nCaps = 1;
    p.u.prism.west = p.u.prism.east = true;
    p.u.prism.caps[0] = rg_band(0, 16, rg_tile(0, 0, 8, 8), 16);
    rg_mesh_init(&m);
    CHECK(rg_part_emit(&p, &m));
    CHECK(m.n > 0);
    {
        unsigned w = 0, e = 0;

        for (i = 0; i < m.n; i++) {
            const char *nm = m.names[m.t[i].tag];

            if (strcmp(nm, "wall.capw") == 0) { w++; CHECK(m.t[i].p[0].x == 0 && m.t[i].shade == RG_SHADE_WEST); }
            if (strcmp(nm, "wall.cape") == 0) { e++; CHECK(m.t[i].p[0].x == 16 && m.t[i].shade == RG_SHADE_EAST); }
        }
        CHECK(w > 0 && e > 0);
    }
    NoBad(&m, "caps");
    rg_mesh_free(&m);
    p.u.prism.west = false;
    rg_mesh_init(&m);
    CHECK(rg_part_emit(&p, &m));
    for (i = 0; i < m.n; i++)
        CHECK(strcmp(m.names[m.t[i].tag], "wall.capw") != 0);
    rg_mesh_free(&m);
}

static void TestStripFace(void)
{
    RgMesh m;
    RgStrip s;
    double pts[4][3] = {{0, 0, 10}, {32, 0, 10}, {32, 20, 10}, {0, 20, 10}};
    double o[3] = {0, 0, 10}, ex[3] = {1, 0, 0}, up[3] = {0, 1, 0};
    unsigned i;
    double maxv = -1e9, minv = 1e9;

    /* no repeat: the last fixed row is held beyond the fixed rows (~clamp) */
    memset(&s, 0, sizeof(s));
    s.fixed[0] = 0; s.fixed[1] = 8;
    rg_mesh_init(&m);
    rg_strip_face(&m, pts, 4, o, ex, up, &s, 1.0, "f");
    CHECK(!m.failed && m.n > 0);
    {
        unsigned clamp = 0;

        for (i = 0; i < m.n; i++)
            if (m.t[i].flags & RG_TAG_CLAMP)
                clamp++;
        CHECK(clamp > 0);
    }
    NoBad(&m, "strip no repeat");
    rg_mesh_free(&m);
    /* wrap: u folds every 16 */
    memset(&s, 0, sizeof(s));
    s.fixed[0] = 0; s.fixed[1] = 20;
    s.hasWrap = true; s.wrap[0] = 0; s.wrap[1] = 16;
    rg_mesh_init(&m);
    rg_strip_face(&m, pts, 4, o, ex, up, &s, 1.0, "w");
    CHECK(!m.failed && m.n >= 4);                        /* two u pieces, a quad each = 4 triangles */
    for (i = 0; i < m.n; i++) {
        unsigned k;

        for (k = 0; k < 3; k++) {
            if (m.t[i].p[k].u > maxv) maxv = m.t[i].p[k].u;
            if (m.t[i].p[k].u < minv) minv = m.t[i].p[k].u;
        }
    }
    CHECK(minv >= -1e-9 && maxv <= 16 + 1e-9);
    NoBad(&m, "strip wrap");
    rg_mesh_free(&m);
    /* tail + repeat on a long face */
    memset(&s, 0, sizeof(s));
    s.fixed[0] = 0; s.fixed[1] = 6;
    s.hasRepeat = true; s.repeat[0] = 6; s.repeat[1] = 10;
    s.hasTail = true; s.tail[0] = 10; s.tail[1] = 14;
    s = rg_strip_fin(s);
    {
        double tall[4][3] = {{0, 0, 10}, {16, 0, 10}, {16, 60, 10}, {0, 60, 10}};

        rg_mesh_init(&m);
        rg_strip_face(&m, tall, 4, o, ex, up, &s, 1.0, "t");
        CHECK(!m.failed && m.n > 8);
        NoBad(&m, "strip tail");
        rg_mesh_free(&m);
    }
    /* an over-long tag fails softly */
    rg_mesh_init(&m);
    CHECK(rg_mesh_tag(&m, "0123456789012345678901234567890123456789012345678901234567890123") == RG_NO_TAG && m.failed);
    rg_mesh_free(&m);
}

/* ---- the other part types ---- */
static RgPart *Add(RgPartList *l, RgPartKind k, const char *name)
{
    RgPart *p = rg_parts_add(l, k, name);

    CHECK(p != NULL);
    return p;
}

static void TestParts(void)
{
    RgPartList l;
    RgMesh m;
    RgImage art, card;
    RgPart *p;
    unsigned i;
    RgOrthoResult r;
    int16_t cells[2][2] = {{1, 2}, {3, 4}};

    UnitArt(&art, 128, 96);
    rg_parts_init(&l);

    p = Add(&l, RG_P_WALLS, "walls");
    p->u.walls.nPlan = 4;
    p->u.walls.plan[0][0] = 10; p->u.walls.plan[0][1] = 50;
    p->u.walls.plan[1][0] = 20; p->u.walls.plan[1][1] = 60;      /* diagonal -> ~proj */
    p->u.walls.plan[2][0] = 60; p->u.walls.plan[2][1] = 60;
    p->u.walls.plan[3][0] = 70; p->u.walls.plan[3][1] = 50;
    p->u.walls.y0 = 0; p->u.walls.y1 = 20;
    p = Add(&l, RG_P_FACET, "facet");
    p->u.facet.a[0] = 80; p->u.facet.a[1] = 60; p->u.facet.b[0] = 100; p->u.facet.b[1] = 70;
    p->u.facet.ha = 10; p->u.facet.hb = 12;
    p = Add(&l, RG_P_VAULT, "vault");
    p->u.vault.nProfile = 4;
    p->u.vault.profile[0][0] = 0;  p->u.vault.profile[0][1] = 0;
    p->u.vault.profile[1][0] = 8;  p->u.vault.profile[1][1] = 6;
    p->u.vault.profile[2][0] = 16; p->u.vault.profile[2][1] = 8;
    p->u.vault.profile[3][0] = 24; p->u.vault.profile[3][1] = 0;
    p->u.vault.zf = 90; p->u.vault.zb = 70; p->u.vault.y0 = 10;
    p = Add(&l, RG_P_CYLINDER, "cyl");
    p->u.cyl.cx = 40; p->u.cyl.cz = 40; p->u.cyl.rx = 8; p->u.cyl.rz = 5; p->u.cyl.y0 = 0; p->u.cyl.y1 = 10;
    p->u.cyl.backTile = rg_tile(0, 0, 40, 12);
    p->u.cyl.sides = 16;
    CHECK(rg_img_new(&card, 12, 20));
    for (i = 0; i < 12u * 20u; i++) {
        card.px[i * 4] = 9; card.px[i * 4 + 1] = 9; card.px[i * 4 + 2] = 9;
        card.px[i * 4 + 3] = (i / 12 >= 4 && i / 12 < 18 && i % 12 >= 2 && i % 12 < 10) ? 255 : 0;
    }
    p = Add(&l, RG_P_CARD, "card");
    p->u.card.art = &card; p->u.card.foot = 80; p->u.card.voff = 0;
    p = Add(&l, RG_P_DECAL, "decal");
    p->u.decal.cells = cells; p->u.decal.nCells = 2; p->u.decal.vOffset = 3; p->u.decal.lift = 0.5;
    p = Add(&l, RG_P_PLAINWALL, "plain");
    p->u.plain.a[0] = 0; p->u.plain.a[1] = 0; p->u.plain.b[0] = 0; p->u.plain.b[1] = 24;
    p->u.plain.y0 = 0; p->u.plain.y1 = 16; p->u.plain.tile = rg_tile(0, 0, 8, 8);

    rg_mesh_init(&m);
    CHECK(rg_parts_emit(&l, &m));
    {
        unsigned wallProj = 0, wallPlain = 0, facet = 0, vault = 0, cyl = 0, cardN = 0, decal = 0, plain = 0;

        for (i = 0; i < m.n; i++) {
            const char *nm = m.names[m.t[i].tag];

            wallProj += strcmp(nm, "walls.wall~proj") == 0;
            wallPlain += strcmp(nm, "walls.wall") == 0;
            facet += strcmp(nm, "facet.facet~proj") == 0;
            vault += strncmp(nm, "vault.", 6) == 0;
            cyl += strncmp(nm, "cyl.", 4) == 0;
            cardN += strcmp(nm, "card.card") == 0;
            decal += strcmp(nm, "decal.decal") == 0;
            plain += strcmp(nm, "plain.plain") == 0;
        }
        CHECK(wallProj == 4 && wallPlain == 2 && facet == 2);
        CHECK(vault == 18 && cyl == 14 + 16 * 2);       /* 3 profile segments x (top, front, back) quads; fan of 16 minus 2 + 16 quads */
        CHECK(cardN == 2 && decal == 4 && plain > 0);
    }
    /* the projected parts (walls, facet) are exactly the art; the rest are dressed with tiles and are not */
    {
        RgMesh pm;
        unsigned k;

        rg_mesh_init(&pm);
        CHECK(rg_part_emit(rg_parts_at(&l, 0), &pm) && rg_part_emit(rg_parts_at(&l, 1), &pm));
        CHECK(rg_ortho_check(&pm, &art, NULL, 0, NULL, &r));
        CHECK(r.wrong == 0 && r.extra == 0 && r.missing > 0);
        for (k = 0; k < pm.n; k++)
            CHECK((pm.t[k].flags & RG_TAG_PROJ) || strcmp(pm.names[pm.t[k].tag], "walls.wall") == 0);
        Shift(&pm, 1.0, 1.0);
        CHECK(Wrong(&pm, &art) > 0);
        rg_mesh_free(&pm);
    }
    CHECK(rg_ortho_check(&m, &art, NULL, 0, NULL, &r) && r.wrong > 0);
    NoBad(&m, "all parts");
    rg_mesh_free(&m);

    /* the Card: u spans the opaque columns and its foot row projects through the art */
    {
        RgMesh c;

        rg_mesh_init(&c);
        CHECK(rg_part_emit(rg_parts_at(&l, 4), &c));
        CHECK(c.n == 2 && c.t[0].p[0].x == 2 && c.t[0].p[1].x == 10);
        CHECK(c.t[0].p[0].y == -1.0 && c.t[0].p[2].y == 80 - 4);        /* h = foot - first opaque row */
        rg_mesh_free(&c);
    }
    /* an empty card emits nothing */
    {
        RgPart e;
        RgMesh c;
        RgImage blank;

        CHECK(rg_img_new(&blank, 4, 4));
        memset(&e, 0, sizeof(e));
        e.kind = RG_P_CARD; strcpy(e.name, "c"); e.u.card.art = &blank;
        rg_mesh_init(&c);
        CHECK(rg_part_emit(&e, &c) && c.n == 0);
        rg_mesh_free(&c);
        rg_img_free(&blank);
    }
    /* Lifted: everything moved by (0, b, b) and the view row z - y is unchanged */
    {
        RgPart lf;
        RgMesh a, b;
        const RgPart *inner = rg_parts_at(&l, 0);

        memset(&lf, 0, sizeof(lf));
        lf.kind = RG_P_LIFTED; strcpy(lf.name, "lift");
        lf.u.lifted.part = inner; lf.u.lifted.base = 7.0;
        rg_mesh_init(&a); rg_mesh_init(&b);
        CHECK(rg_part_emit(inner, &a) && rg_part_emit(&lf, &b) && a.n == b.n);
        for (i = 0; i < a.n; i++) {
            CHECK(b.t[i].p[0].y == a.t[i].p[0].y + 7.0 && b.t[i].p[0].z == a.t[i].p[0].z + 7.0);
            CHECK(b.t[i].p[0].z - b.t[i].p[0].y == a.t[i].p[0].z - a.t[i].p[0].y);
            CHECK(strcmp(b.names[b.t[i].tag], a.names[a.t[i].tag]) == 0);
        }
        rg_mesh_free(&a); rg_mesh_free(&b);
        lf.u.lifted.part = NULL;
        rg_mesh_init(&a);
        CHECK(!rg_part_emit(&lf, &a));
        rg_mesh_free(&a);
    }
    rg_parts_free(&l);
    rg_img_free(&card);
    rg_img_free(&art);
}

static void TestHipAndFrustum(void)
{
    RgPart p;
    RgMesh m;
    unsigned i, ridgeEnd = 0, slopes = 0;
    double z, y;

    memset(&p, 0, sizeof(p));
    p.kind = RG_P_HIPROOF; strcpy(p.name, "hip");
    p.u.hip.x0 = 0; p.u.hip.x1 = 96; p.u.hip.zf = 80; p.u.hip.zb = 20; p.u.hip.y0 = 30;
    p.u.hip.fascia.fixed[0] = 0; p.u.hip.fascia.fixed[1] = 4;
    p.u.hip.slope.fixed[0] = 4; p.u.hip.slope.fixed[1] = 12;
    p.u.hip.slope.hasRepeat = true; p.u.hip.slope.repeat[0] = 12; p.u.hip.slope.repeat[1] = 20;
    p.u.hip.slope = rg_strip_fin(p.u.hip.slope);
    p.u.hip.fascia = rg_strip_fin(p.u.hip.fascia);
    p.u.hip.teeth[0] = 30; p.u.hip.teeth[1] = 36;
    p.u.hip.cap[0] = 36; p.u.hip.cap[1] = 44;
    p.u.hip.pitch = 22.0; p.u.hip.run = 20; p.u.hip.ridgeU[0] = 0; p.u.hip.ridgeU[1] = 8;
    p.u.hip.endTile = rg_tile(0, 60, 16, 70);
    p.u.hip.ridge = true;
    rg_hip_init(&p.u.hip);
    CHECK(p.u.hip.ye == 34.0 && p.u.hip.zrf == 50.0 + 4.0 && p.u.hip.zrb == 46.0);
    CHECK(fabs(p.u.hip.rise - tan(22.0 * 0.017453292519943295) * (80 - 54)) < 1e-12);
    rg_hip_slope_point(&p.u.hip, 0.0, &z, &y);
    CHECK(z == 80.0 && y == 34.0);
    rg_mesh_init(&m);
    CHECK(rg_part_emit(&p, &m));
    for (i = 0; i < m.n; i++) {
        const char *nm = m.names[m.t[i].tag];

        ridgeEnd += strcmp(nm, "hip.ridge_end") == 0;
        slopes += strncmp(nm, "hip.slope", 9) == 0;
    }
    CHECK(ridgeEnd == 4 && slopes > 0);
    CHECK(strcmp(m.names[m.t[0].tag], "hip.fascia_s") == 0);        /* upstream order: fascia first */
    NoBad(&m, "hip");
    Shift(&m, 0.0, 2.0);
    CHECK(rg_density_check(&m, NULL, NULL, 0) > 0);
    rg_mesh_free(&m);
    p.u.hip.hasRidgeWrap = true; p.u.hip.ridgeWrap[0] = 4; p.u.hip.ridgeWrap[1] = 12;
    rg_mesh_init(&m);
    CHECK(rg_part_emit(&p, &m));
    NoBad(&m, "hip wrap");
    rg_mesh_free(&m);
    p.u.hip.ridge = false;
    rg_mesh_init(&m);
    CHECK(rg_part_emit(&p, &m));
    for (i = 0; i < m.n; i++)
        CHECK(strstr(m.names[m.t[i].tag], "ridge") == NULL && strstr(m.names[m.t[i].tag], "teeth") == NULL);
    rg_mesh_free(&m);

    /* frustum: an octagon plan, listed clockwise on screen */
    memset(&p, 0, sizeof(p));
    p.kind = RG_P_FRUSTUM; strcpy(p.name, "fr");
    {
        static const double oct[8][2] = {{16, 0}, {64, 0}, {80, 16}, {80, 48}, {64, 64}, {16, 64}, {0, 48}, {0, 16}};
        unsigned k;

        /* clockwise on screen with +Z down: the south edge (high z) runs west to east */
        p.u.frustum.nPlan = 8;
        for (k = 0; k < 8; k++) {
            p.u.frustum.plan[k][0] = oct[7 - k][0];
            p.u.frustum.plan[k][1] = oct[7 - k][1];
        }
    }
    p.u.frustum.wallTop = 24;
    p.u.frustum.wallSide.fixed[0] = 0; p.u.frustum.wallSide.fixed[1] = 12;
    p.u.frustum.bandRise = 6;
    p.u.frustum.bandSide.fixed[0] = 0; p.u.frustum.bandSide.fixed[1] = 8;
    p.u.frustum.top.fixed[0] = 0; p.u.frustum.top.fixed[1] = 8;
    p.u.frustum.top.hasRepeat = true; p.u.frustum.top.repeat[0] = 0; p.u.frustum.top.repeat[1] = 8;
    p.u.frustum.top = rg_strip_fin(p.u.frustum.top);
    rg_mesh_init(&m);
    CHECK(rg_part_emit(&p, &m));
    {
        unsigned proj = 0, top = 0;

        for (i = 0; i < m.n; i++) {
            proj += (m.t[i].flags & RG_TAG_PROJ) != 0;
            top += strncmp(m.names[m.t[i].tag], "fr.top", 6) == 0;
        }
        CHECK(proj > 0 && top > 0 && !m.failed);
    }
    NoBad(&m, "frustum");
    rg_mesh_free(&m);
}

int main(void)
{
    TestNumerics();
    TestSort();
    TestClip();
    TestTriangulate();
    TestTilePieces();
    TestRaster();
    TestProjPrism();
    TestPrismMaterials();
    TestStripFace();
    TestParts();
    TestHipAndFrustum();
    printf("test_romgen_geom: %d checks, %d failures\n", sChecks, sFails);
    return sFails != 0;
}
