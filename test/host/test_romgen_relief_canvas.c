// test_romgen_relief_canvas.c -- host test for phase 33 S3.4 (SPEC-S3 section 6): rg_rcanvas (drawn_canvas, drawn_role, the
// majority vote), rg_rprep + rg_rwrap (drawn_prepare: regions, split_wrapped, big, runs, ties, stats, edges).
// Synthetic checks always run; the real-ROM checks run with ROMGEN_ROM=/abs/path/emerald.gba (else SKIP).
//   make -C tools/romgen test T=relief_canvas        (RG_PIN_DUMP=1 prints the pin table instead of checking it)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rg_rcanvas.h"
#include "rg_rprep.h"
#include "rg_rtables.h"
#include "rg_world.h"

static int sChecks, sFails, sSkips;
#define CHECK(c) do { ++sChecks; if (!(c)) { ++sFails; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

/* ---- synthetic: the majority vote against a brute-force window ---- */

static uint32_t sRng = 12345u;
static uint32_t Rnd(void) { sRng = sRng * 1664525u + 1013904223u; return sRng >> 8; }

static void TestVote(void)
{
    enum { W = 37, H = 29 };
    uint8_t kind[W * H], voted[W * H];
    int x, y, i, j, bad = 0, flips = 0;

    for (i = 0; i < W * H; i++) kind[i] = (uint8_t)(Rnd() % 7u);
    CHECK(rg_canvas_vote(kind, W, H, voted));
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            uint8_t k = kind[y * W + x], want = k;
            int tops = 0, faces = 0;

            if (k == RG_K_TOP || k == RG_K_FACE || k == RG_K_FLECK) {
                for (j = y - 2; j <= y + 2; j++)
                    for (i = x - 2; i <= x + 2; i++) {
                        if (i < 0 || j < 0 || i >= W || j >= H) continue;
                        tops += kind[j * W + i] == RG_K_TOP || kind[j * W + i] == RG_K_RIM;   /* rim counts as top (upstream) */
                        faces += kind[j * W + i] == RG_K_FACE;
                    }
                want = tops > faces ? RG_K_TOP : RG_K_FACE;
                flips += want != k;
            }
            bad += voted[y * W + x] != want;
        }
    CHECK(bad == 0);
    CHECK(flips > 50);                     /* the random canvas really exercises the vote */
}

/* ---- synthetic canvases for drawn_prepare ---- */

/* A canvas of cw x ch cells with one member; every pixel row of `rows` is given as a kind (rows may be < ch*16). */
static RgCanvas MakeCanvas(int cw, int ch, const uint8_t *rowKind)
{
    RgCanvas cv;
    size_t cells = (size_t)cw * (size_t)ch;
    int x, y;

    memset(&cv, 0, sizeof cv);
    cv.nMem = 1;
    cv.mem = (RgCanvasMember *)calloc(1, sizeof *cv.mem);
    cv.mem[0].w = (uint16_t)cw; cv.mem[0].h = (uint16_t)ch;
    cv.cw = cw; cv.ch = ch;
    cv.kind = (uint8_t *)malloc(cells * 256u);
    cv.blocked = (uint8_t *)calloc(cells, 1);
    cv.side = (int8_t *)calloc(cells, 1);
    cv.flat = (uint8_t *)calloc(cells, 1);
    cv.faceLow = (uint8_t *)calloc(cells, 1);
    cv.pier = (uint8_t *)calloc(cells, 1);
    cv.meta = (uint16_t *)calloc(cells, sizeof(uint16_t));
    for (y = 0; y < ch * 16; y++)
        for (x = 0; x < cw * 16; x++) cv.kind[(size_t)y * (size_t)(cw * 16) + (size_t)x] = rowKind[y];
    return cv;
}

static unsigned CountDrops(const RgPrep *p, int32_t a, int32_t b, int32_t drop)
{
    unsigned i, j, n = 0;

    for (i = 0; i < p->nRuns; i++)
        if (p->runs[i].a == a && p->runs[i].b == b)
            for (j = 0; j < p->runs[i].n; j++) n += p->runs[i].v[j] == drop;
    return n;
}

/* two terraces (TOP 0..15, TOP 36..47) joined by a 20-row `mid` kind, two cells wide: the drops of every column. */
static void TestRuns(void)
{
    uint8_t rows[48];
    RgCanvas cv;
    RgPrep p;
    unsigned i;

    for (i = 0; i < 48; i++) rows[i] = i < 16 ? RG_K_TOP : i < 36 ? RG_K_FACE : RG_K_TOP;
    cv = MakeCanvas(2, 3, rows);
    CHECK(rg_prep_run(&cv, false, &p) == RG_OK && cv.kind == NULL);       /* ownership moved */
    CHECK(p.nRegions == 2 && p.big[0] && p.big[1] && !p.wrapped && p.wrapCuts == 0);
    CHECK(p.sizes[0] == 512 && p.sizes[1] == 384);                        /* labels in first-pixel row-major order */
    CHECK(CountDrops(&p, 0, 1, 20) == 32);                                /* one FACE run of 20 per column */
    CHECK(p.nRuns == 1 && p.runs[0].n == 32 && p.nTies == 0);
    CHECK(p.region[0] == 0 && p.region[47 * 32] == 1 && p.region[20 * 32] == -1);
    /* edges: up/down of the one member land on the top and bottom terraces, left on both with the face between */
    CHECK(p.edges[p.edgeOff[RG_EDGE_UP]] == 0 && p.edges[p.edgeOff[RG_EDGE_DOWN] + 1] == 1);
    CHECK(p.edges[p.edgeOff[RG_EDGE_LEFT]] == 0 && p.edges[p.edgeOff[RG_EDGE_LEFT] + 1] == -1 &&
          p.edges[p.edgeOff[RG_EDGE_LEFT] + 2] == 1);
    CHECK(p.stats[0].n == 256 && p.stats[0].top == 256 && p.stats[0].ground == 0 && p.stats[0].nCounts == 1);
    rg_prep_free(&p);

    for (i = 16; i < 36; i++) rows[i] = RG_K_RIM;                          /* a rim: -RIM_RISE per column */
    cv = MakeCanvas(2, 3, rows);
    CHECK(rg_prep_run(&cv, false, &p) == RG_OK);
    CHECK(CountDrops(&p, 0, 1, -16) == 32 && p.nRuns == 1);
    rg_prep_free(&p);

    for (i = 16; i < 36; i++) rows[i] = RG_K_FACE;                         /* walked: 1000 zeros and a tie */
    cv = MakeCanvas(2, 3, rows);
    cv.flat[0] = cv.flat[2] = cv.flat[4] = RG_FLAT_FLOOR;                  /* cells (0,0) (0,1) (0,2) */
    CHECK(rg_prep_run(&cv, false, &p) == RG_OK);
    CHECK(CountDrops(&p, 0, 1, 0) == RG_WALKED && CountDrops(&p, 0, 1, 20) == 32);
    CHECK(p.nTies == 1 && p.ties[0].a == 0 && p.ties[0].b == 1);
    rg_prep_free(&p);

    for (i = 0; i < 48; i++) rows[i] = i < 16 ? RG_K_TOP : RG_K_FACE;      /* a thin terrace is not big */
    cv = MakeCanvas(2, 3, rows);
    CHECK(rg_prep_run(&cv, true, &p) == RG_OK && p.wrapped && p.nRegions == 1);   /* wrap flag only marks the pass */
    rg_prep_free(&p);
}

static void TestPierApart(void)
{
    uint8_t rows[32];
    RgCanvas cv;
    RgPrep p;
    unsigned i;

    for (i = 0; i < 32; i++) rows[i] = RG_K_GROUND;
    cv = MakeCanvas(2, 2, rows);                                           /* one ground; a pier cell splits it */
    cv.pier[1] = 1;                                                        /* (1,0) is pier, neighbours not floor/bridge */
    CHECK(rg_prep_run(&cv, false, &p) == RG_OK);
    CHECK(p.region[0] != p.region[16] && p.region[16] != -1);              /* the pier cell is its own region(s) */
    rg_prep_free(&p);

    cv = MakeCanvas(2, 2, rows);
    cv.pier[1] = 1; cv.flat[0] = RG_FLAT_FLOOR;                            /* floor beside a pier: joined, apart is false */
    CHECK(rg_prep_run(&cv, false, &p) == RG_OK);
    CHECK(p.region[0] == p.region[16]);
    rg_prep_free(&p);
}

static void TestWrapGate(void)
{
    RgDrawnGroup g;
    unsigned k;

    memset(&g, 0, sizeof g);
    for (k = 0; k < 400; k++) {
        g.key = (uint16_t)k;
        g.isAlt = 0;
        CHECK(rg_group_is_wrap(&g) == (k == 20 || k == 21 || k == 22));
        g.isAlt = 1;
        CHECK(!rg_group_is_wrap(&g));
    }
}

/* ---- real ROM ---- */

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

static uint64_t Fnv(uint64_t h, int64_t v)
{
    int i;

    for (i = 0; i < 8; i++) { h ^= (uint64_t)(v >> (8 * i)) & 0xFFu; h *= 1099511628211ULL; }
    return h;
}

/* A hash over everything drawn_prepare yields: sizes, big, runs (keys in insertion order, values), ties, stats, edges. */
static uint64_t PrepHash(const RgPrep *p)
{
    uint64_t h = 14695981039346656037ULL;
    size_t cells = (size_t)p->cv.cw * (size_t)p->cv.ch, i, j;

    for (i = 0; i < p->nRegions; i++) { h = Fnv(h, p->sizes[i]); h = Fnv(h, p->big[i]); h = Fnv(h, p->cutApart[i]); }
    for (i = 0; i < p->nRuns; i++) {
        h = Fnv(h, p->runs[i].a); h = Fnv(h, p->runs[i].b); h = Fnv(h, p->runs[i].n);
        for (j = 0; j < p->runs[i].n; j++) h = Fnv(h, p->runs[i].v[j]);
    }
    for (i = 0; i < p->nTies; i++) { h = Fnv(h, p->ties[i].a); h = Fnv(h, p->ties[i].b); }
    for (i = 0; i < cells; i++) {
        const RgCellStat *s = &p->stats[i];

        h = Fnv(h, s->n); h = Fnv(h, s->top); h = Fnv(h, s->ground); h = Fnv(h, s->nCounts);
        for (j = 0; j < s->nCounts; j++) { h = Fnv(h, p->counts[s->cOff + j].region); h = Fnv(h, p->counts[s->cOff + j].n); }
    }
    for (i = 0; i < p->edgeOff[p->cv.nMem * 4u]; i++) h = Fnv(h, p->edges[i]);
    return h;
}

typedef struct Pin {
    uint16_t key; uint8_t alt;
    uint32_t hist[7];
    uint32_t regions, big, cuts, runKeys, drops, ties;
    uint64_t hash;
} Pin;

#include "test_romgen_relief_canvas_pins.inc"
#define NPINS (sizeof(kPins) / sizeof(kPins[0]))

/* An independent labelling: FIFO BFS, 4-neighbours, TOP and GROUND each their own kind, "apart" and "band seam" from
 * the definitions. Returns the number of regions, labels in first-pixel row-major order. */
static bool IndApart(const RgCanvas *c, size_t a, size_t b)
{
    if (a == b || (c->pier[a] != 0) == (c->pier[b] != 0)) return false;
    return c->flat[c->pier[a] ? b : a] != RG_FLAT_FLOOR && c->flat[c->pier[a] ? b : a] != RG_FLAT_BRIDGE;
}

static bool IndSeam(const RgCanvas *c, int ax, int ay, int bx, int by)
{
    int sa, sb, cx, cy, s, low;

    if (ay == by) return false;
    sa = c->side[(size_t)ay * (size_t)c->cw + (size_t)ax];
    sb = c->side[(size_t)by * (size_t)c->cw + (size_t)bx];
    if ((sa != 0) == (sb != 0)) return false;
    cx = sa ? ax : bx; cy = sa ? ay : by;
    s = c->side[(size_t)cy * (size_t)c->cw + (size_t)cx];
    for (low = cx - s; low >= 0 && low < c->cw && c->side[(size_t)cy * (size_t)c->cw + (size_t)low] == s; low -= s) {}
    return low >= 0 && low < c->cw && c->flat[(size_t)cy * (size_t)c->cw + (size_t)low] == RG_FLAT_WATER;
}

static uint32_t IndLabel(const RgCanvas *c, int32_t *lab, int32_t *queue)
{
    const int W = c->cw * 16, H = c->ch * 16;
    static const int dx[4] = {0, 1, 0, -1}, dy[4] = {-1, 0, 1, 0};
    uint32_t n = 0;
    int x, y;
    size_t i;

    for (i = 0; i < (size_t)W * (size_t)H; i++) lab[i] = -1;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            uint8_t k = c->kind[(size_t)y * W + x];
            size_t qh = 0, qt = 0;

            if ((k != RG_K_GROUND && k != RG_K_TOP) || lab[(size_t)y * W + x] >= 0) continue;
            lab[(size_t)y * W + x] = (int32_t)n;
            queue[qt++] = y * W + x;
            while (qh < qt) {
                int a = queue[qh] % W, b = queue[qh] / W, d;

                qh++;
                for (d = 0; d < 4; d++) {
                    int i2 = a + dx[d], j2 = b + dy[d];
                    size_t ca, cb;

                    if (i2 < 0 || j2 < 0 || i2 >= W || j2 >= H || lab[(size_t)j2 * W + i2] >= 0 ||
                        c->kind[(size_t)j2 * W + i2] != k)
                        continue;
                    ca = (size_t)(b / 16) * (size_t)c->cw + (size_t)(a / 16);
                    cb = (size_t)(j2 / 16) * (size_t)c->cw + (size_t)(i2 / 16);
                    if (ca != cb && (IndApart(c, ca, cb) || IndSeam(c, a / 16, b / 16, i2 / 16, j2 / 16))) continue;
                    lab[(size_t)j2 * W + i2] = (int32_t)n;
                    queue[qt++] = j2 * W + i2;
                }
            }
            n++;
        }
    return n;
}

static void Structural(const RgCanvas *c, uint32_t hist[7])
{
    const int W = c->cw * 16, H = c->ch * 16;
    int x, y;
    size_t cx;
    unsigned bad = 0;

    memset(hist, 0, 7 * sizeof hist[0]);
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            size_t cell = (size_t)(y / 16) * (size_t)c->cw + (size_t)(x / 16);
            uint8_t k = c->kind[(size_t)y * W + x];

            bad += k >= 7 || k == RG_K_FLECK;
            hist[k < 7 ? k : 0]++;
            bad += (k == RG_K_VOID) != (c->meta[cell] == 0xFFFF);        /* VOID exactly where no layout holds the cell */
        }
    CHECK(bad == 0);
    for (cx = 0; cx < (size_t)c->cw * (size_t)c->ch; cx++)
        if (c->flat[cx] == RG_FLAT_FALL) {
            int i;
            int allFace = 1;

            for (i = 0; i < 256; i++)
                if (c->kind[(size_t)((cx / (size_t)c->cw) * 16 + (size_t)(i / 16)) * W + (cx % (size_t)c->cw) * 16 + (size_t)(i % 16)] != RG_K_FACE)
                    allFace = 0;
            bad += !allFace;
        }
    CHECK(bad == 0);
}

static void RealGroup(RgRCtx *ctx, const RgDrawn *d, unsigned g, int dump, unsigned *seen)
{
    const RgDrawnGroup *G = &d->groups[g];
    RgPrep p;
    Pin got;
    int32_t *lab, *queue;
    size_t px;
    uint32_t ind, i;
    unsigned w;

    if (rg_drawn_excluded(G)) return;
    CHECK(rg_prepare(ctx, d, G, &p) == RG_OK);
    memset(&got, 0, sizeof got);
    got.key = G->key; got.alt = G->isAlt;
    Structural(&p.cv, got.hist);
    got.regions = p.nRegions; got.cuts = p.wrapCuts; got.runKeys = p.nRuns; got.ties = p.nTies;
    for (i = 0; i < p.nRegions; i++) got.big += p.big[i];
    for (i = 0; i < p.nRuns; i++) got.drops += p.runs[i].n;
    got.hash = PrepHash(&p);
    px = (size_t)p.cv.cw * 16u * (size_t)p.cv.ch * 16u;
    CHECK((uint64_t)got.hist[0] + got.hist[1] + got.hist[2] + got.hist[3] + got.hist[4] + got.hist[5] + got.hist[6] == px);
    CHECK(p.wrapped == (rg_group_is_wrap(G) ? true : false));
    CHECK(rg_group_is_wrap(G) || p.wrapCuts == 0);

    lab = (int32_t *)malloc(px * sizeof *lab);
    queue = (int32_t *)malloc(px * sizeof *queue);
    if (lab != NULL && queue != NULL) {
        int same = 1;

        ind = IndLabel(&p.cv, lab, queue);
        if (!p.wrapped) {
            same = ind == p.nRegions;
            for (i = 0; same && i < px; i++) same = lab[i] == p.region[i];
            CHECK(same);
        } else {
            CHECK(p.nRegions == ind + p.wrapCuts);        /* split_wrapped only adds labels */
        }
    } else CHECK(0);
    free(lab); free(queue);

    if (dump) {
        printf("    {%u, %u, {%u, %u, %u, %u, %u, %u, %u}, %u, %u, %u, %u, %u, %u, 0x%016llxULL},\n", got.key, got.alt,
               got.hist[0], got.hist[1], got.hist[2], got.hist[3], got.hist[4], got.hist[5], got.hist[6], got.regions,
               got.big, got.cuts, got.runKeys, got.drops, got.ties, (unsigned long long)got.hash);
    } else {
        for (w = 0; w < NPINS && !(kPins[w].key == got.key && kPins[w].alt == got.alt); w++) {}
        if (w == NPINS) CHECK(0);
        else {
            CHECK(memcmp(kPins[w].hist, got.hist, sizeof got.hist) == 0);
            CHECK(kPins[w].regions == got.regions && kPins[w].big == got.big && kPins[w].cuts == got.cuts);
            CHECK(kPins[w].runKeys == got.runKeys && kPins[w].drops == got.drops && kPins[w].ties == got.ties);
            CHECK(kPins[w].hash == got.hash);
            (*seen)++;
        }
    }
    rg_prep_free(&p);
}

static unsigned RunAll(const RgWorld *w, const RgRoles *r, const RgDrawn *d, int dump, uint64_t *sumHash)
{
    RgErr e = RG_OK;
    RgRCtx *ctx = rg_rctx_new(w, r, &e);
    unsigned g, seen = 0;

    CHECK(ctx != NULL);
    if (ctx == NULL) return 0;
    rg_rctx_verify(ctx, true);
    for (g = 0; g < d->nGroups; g++) RealGroup(ctx, d, g, dump, &seen);
    CHECK(rg_rctx_conflicts(ctx) == 118);      /* alias layouts 13/136/292 vs layout 28 share a role key: first wins, as upstream */
    *sumHash = seen;
    rg_rctx_free(ctx);
    return seen;
}

static void TestRealRom(void)
{
    const char *path = getenv("ROMGEN_ROM");
    size_t n = 0;
    uint8_t *rom;
    RgWorld w;
    RgRoles r;
    RgDrawn *d;
    int dump = getenv("RG_PIN_DUMP") != NULL;
    uint64_t dummy;

    if (path == NULL || (rom = ReadAll(path, &n)) == NULL) {
        printf("SKIP real-ROM checks (ROMGEN_ROM not set or unreadable)\n");
        sSkips++;
        return;
    }
    d = (RgDrawn *)malloc(sizeof *d);
    CHECK(rg_world_open(&w, rom, n) == RG_OK);
    CHECK(rg_roles_init(&w, &r) == RG_OK && rg_roles_all(&w, &r, false, NULL, NULL) == RG_OK);
    CHECK(d != NULL && rg_drawn_find(&w, d) == RG_OK);
    if (d != NULL) {
        unsigned seen = RunAll(&w, &r, d, dump, &dummy);

        if (!dump) CHECK(seen == NPINS && seen == 34);
        if (!dump) CHECK(RunAll(&w, &r, d, 0, &dummy) == seen);        /* deterministic: a second, fresh context agrees */
    }
    free(d);
    rg_roles_free(&r);
    rg_world_close(&w);
    free(rom);
}

int main(void)
{
    TestVote();
    TestRuns();
    TestPierApart();
    TestWrapGate();
    TestRealRom();
    printf("test_romgen_relief_canvas: %d checks, %d failures%s\n", sChecks, sFails, sSkips ? " (SKIPPED real-ROM parts)" : "");
    return sFails != 0;
}
