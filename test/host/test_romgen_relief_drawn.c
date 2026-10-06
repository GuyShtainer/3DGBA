// test_romgen_relief_drawn.c -- host test for phase 33 S3.3 (SPEC-S3 section 6): rg_rdrawn (find_drawn seeds, links,
// BFS groups, alternate groups, drawn_group/excluded, map_links, _seam_cells).
// Synthetic checks always run; the real-ROM checks run with ROMGEN_ROM=/abs/path/emerald.gba (else SKIP).
//   make -C tools/romgen test T=relief_drawn
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rg_ralias.h"
#include "rg_rdrawn.h"
#include "rg_rrock.h"
#include "rg_rtables.h"
#include "rg_world.h"

static int sChecks, sFails, sSkips;
#define CHECK(c) do { ++sChecks; if (!(c)) { ++sFails; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

/* The groups on the user's ROM, in find_drawn's order (seed groups in A.3 name order, then alternate groups):
 * "key isAlt cellsWxcellsH: member@x,y ..." with members in BFS discovery order. Pinned: it fixes variant numbering. */
static const char *const kGroups[] = {
    "345 0 128x72: 345@56,0 265@0,0",
    "12 0 80x20: 12@0,0 23@20,0",
    "9 0 40x80: 9@0,0",
    "292 0 30x46: 292@0,0",
    "13 0 20x20: 13@0,0",
    "6 0 200x110: 6@120,70 37@40,80 36@0,0",
    "7 0 200x240: 7@80,40 41@80,0 43@80,80 40@0,0 42@0,80 44@80,160 45@80,200",
    "136 0 40x47: 136@0,0",
    "302 0 38x51: 302@0,0",
    "303 0 50x37: 303@0,0",
    "16 0 240x40: 16@80,0 48@0,0 47@100,0 263@160,0",
    "19 0 120x100: 19@0,60 26@80,0",
    "20 0 40x80: 20@0,0",
    "21 0 40x80: 21@0,0",
    "22 0 80x20: 22@0,0",
    "25 0 40x63: 25@0,0",
    "27 0 140x140: 27@100,0 29@0,0 28@60,20",
    "30 0 40x80: 30@0,0",
    "31 0 40x80: 31@0,0",
    "32 0 100x40: 32@0,0 15@80,20",
    "33 0 60x20: 33@0,0",
    "34 0 80x160: 34@0,140 35@40,0",
    "38 0 40x40: 38@0,0",
    "39 0 140x20: 39@0,0",
    "49 0 80x40: 49@0,0",
    "50 0 80x40: 50@0,0",
    "239 0 120x80: 239@40,0 238@0,0 241@40,40 394@80,0 240@0,40 395@80,40",
    "321 0 28x23: 321@0,0",
    "2 0 40x60: 2@0,0",
    "8 0 60x60: 8@0,0",
    "290 0 33x30: 290@0,0",
    "392 1 140x140: 392@100,0 29@0,0 28@60,20",
    "46 1 240x40: 16@80,0 48@0,0 47@100,0 46@160,0",
    "319 1 240x40: 16@80,0 48@0,0 319@100,0 263@160,0",
    "357 1 60x60: 357@0,0",
};
#define NGROUPS (sizeof(kGroups) / sizeof(kGroups[0]))

static void TestSeamCells(void)
{
    RgSeamCell c[128];
    unsigned n;

    /* down, offset -3: a's x runs 0..2 (aw 10, bw 6): b index = x + 3 */
    n = rg_seam_cells(1, -3, 10, 10, 6, 10, c, 128);
    CHECK(n == 3 && c[0].aEdge == 1 && c[0].aIdx == 0 && c[0].bEdge == 2 && c[0].bIdx == 3 && c[2].aIdx == 2 && c[2].bIdx == 5);
    /* up, offset 4: x 4..9 (aw 10, bw 20 -> hi min(10, 24)) */
    n = rg_seam_cells(2, 4, 10, 10, 20, 10, c, 128);
    CHECK(n == 6 && c[0].aEdge == 2 && c[0].bEdge == 1 && c[0].aIdx == 4 && c[0].bIdx == 0 && c[5].aIdx == 9 && c[5].bIdx == 5);
    /* right, offset 0: y 0..min(ah, bh) */
    n = rg_seam_cells(4, 0, 7, 5, 9, 8, c, 128);
    CHECK(n == 5 && c[0].aEdge == 4 && c[0].bEdge == 3 && c[4].aIdx == 4 && c[4].bIdx == 4);
    /* left, offset 2: y 2..min(ah, 2 + bh) = 2..5 (ah 5, bh 8) */
    n = rg_seam_cells(3, 2, 7, 5, 9, 8, c, 128);
    CHECK(n == 3 && c[0].aEdge == 3 && c[0].bEdge == 4 && c[0].aIdx == 2 && c[0].bIdx == 0);
    /* no overlap */
    CHECK(rg_seam_cells(1, 10, 10, 10, 6, 10, c, 128) == 0);
    CHECK(rg_seam_cells(1, -9, 10, 10, 6, 10, c, 128) == 0);
}

static void TestRockSets(void)
{
    unsigned m, n = 0;

    for (m = 0; m < 1024; m++) n += rg_is_rock_tile(m) ? 1u : 0u;
    CHECK(n == 22);                      /* the ten ids, 11 more FACE_SOUTH (0x07C, 0x0A9 shared), 0x0A2 (rel:629-631) */
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

static void GroupString(const RgDrawn *d, unsigned g, char *buf, size_t cap)
{
    const RgDrawnGroup *G = &d->groups[g];
    size_t k;
    unsigned i;

    k = (size_t)snprintf(buf, cap, "%u %u %ux%u:", G->key, G->isAlt, G->cellsW, G->cellsH);
    for (i = 0; i < G->nMembers && k < cap; i++) {
        const RgDrawnMember *m = &d->pool[G->first + i];

        k += (size_t)snprintf(buf + k, cap - k, " %u@%d,%d", m->layout, m->x, m->y);
    }
}

/* An independent formulation of "rock meets rock at the seam": place b in a's frame and test global cells. */
static bool RockAt(const RgWorld *w, const RgAlias *al, uint16_t id, int x, int y)
{
    const RgLayout *L = &w->layouts[id - 1u];

    return !rg_off(L, x, y) && rg_is_rock_tile(rg_alias_metatile(al, L, x, y));
}

static bool SeamIndependent(const RgWorld *w, uint16_t a, uint16_t b, unsigned dir, int off, int *dxOut, int *dyOut)
{
    const RgLayout *A = &w->layouts[a - 1u], *B = &w->layouts[b - 1u];
    RgAlias *aa = NULL, *ab = NULL;
    int dx = 0, dy = 0, x, y;
    bool ra = false, rb = false;

    CHECK(rg_alias_of(w, a, &aa) == RG_OK && rg_alias_of(w, b, &ab) == RG_OK);
    switch (dir) {
    case 1: dx = off; dy = A->h; break;
    case 2: dx = off; dy = -(int)B->h; break;
    case 4: dx = A->w; dy = off; break;
    default: dx = -(int)B->w; dy = off; break;
    }
    /* cells of a within 2 of the shared edge and facing b's extent; cells of b within 2 of the edge and facing a */
    for (y = 0; y < (int)A->h; y++)
        for (x = 0; x < (int)A->w; x++) {
            bool near, facing;
            if (!RockAt(w, aa, a, x, y)) continue;
            if (dir == 1) { near = y >= (int)A->h - 2; facing = x >= dx && x < dx + (int)B->w; }
            else if (dir == 2) { near = y < 2; facing = x >= dx && x < dx + (int)B->w; }
            else if (dir == 4) { near = x >= (int)A->w - 2; facing = y >= dy && y < dy + (int)B->h; }
            else { near = x < 2; facing = y >= dy && y < dy + (int)B->h; }
            ra = ra || (near && facing);
        }
    for (y = 0; y < (int)B->h; y++)
        for (x = 0; x < (int)B->w; x++) {
            bool near, facing;
            if (!RockAt(w, ab, b, x, y)) continue;
            if (dir == 1) { near = y < 2; facing = x + dx >= 0 && x + dx < (int)A->w; }
            else if (dir == 2) { near = y >= (int)B->h - 2; facing = x + dx >= 0 && x + dx < (int)A->w; }
            else if (dir == 4) { near = x < 2; facing = y + dy >= 0 && y + dy < (int)A->h; }
            else { near = x >= (int)B->w - 2; facing = y + dy >= 0 && y + dy < (int)A->h; }
            rb = rb || (near && facing);
        }
    rg_alias_free(aa);
    rg_alias_free(ab);
    *dxOut = dx;
    *dyOut = dy;
    return ra && rb;
}

static unsigned CountLinks(const RgDrawn *d, uint16_t a, uint16_t b, int dx, int dy)
{
    unsigned i, n = 0;

    for (i = 0; i < d->nLinks; i++)
        n += (d->links[i].a == a && d->links[i].b == b && d->links[i].dx == dx && d->links[i].dy == dy) ? 1u : 0u;
    return n;
}

static void TestLinksIndependent(const RgWorld *w, const RgDrawn *d)
{
    unsigned i, k, n, expected = 0, pairs = 0;
    RgConn cn[16];

    for (i = 0; i < RG_OUTDOOR_MAP_COUNT; i++) {
        const RgMap *m = rg_world_map(w, RG_OUTDOOR_MAPS_BY_FOLDER[i].group, RG_OUTDOOR_MAPS_BY_FOLDER[i].num);

        n = rg_map_connections(w, m->group, m->num, cn, 16);
        for (k = 0; k < n; k++) {
            const RgMap *t = rg_world_map(w, cn[k].group, cn[k].num);
            int dx, dy;
            bool seam;

            if (!d->blockLayout[m->layoutId] || !d->blockLayout[t->layoutId] || t->layoutId == m->layoutId) continue;
            pairs++;
            seam = SeamIndependent(w, m->layoutId, t->layoutId, cn[k].dir, cn[k].offset, &dx, &dy);
            if (seam) {
                expected += 2;
                CHECK(CountLinks(d, m->layoutId, t->layoutId, dx, dy) >= 1);
                CHECK(CountLinks(d, t->layoutId, m->layoutId, -dx, -dy) >= 1);
            } else {
                CHECK(CountLinks(d, m->layoutId, t->layoutId, dx, dy) == 0);
            }
        }
    }
    CHECK(expected == d->nLinks);               /* nothing recorded that the independent formulation does not see */
    CHECK(pairs > 0 && expected > 0);
}

static void TestGroups(const RgWorld *w, const RgDrawn *d)
{
    unsigned g, i, nSeedGroups = 0, seedsInGroups = 0;
    char buf[512];
    static const uint16_t kKeys[] = {20, 21, 22, 38};
    uint8_t member[512];

    memset(member, 0, sizeof member);
    CHECK(d->nGroups == NGROUPS);
    for (g = 0; g < d->nGroups && g < NGROUPS; g++) {
        GroupString(d, g, buf, sizeof buf);
        if (strcmp(buf, kGroups[g]) != 0) printf("group %u: got [%s] want [%s]\n", g, buf, kGroups[g]);
        CHECK(strcmp(buf, kGroups[g]) == 0);
    }
    /* seeds: each seed lies in exactly one seed group; seed groups are named by a seed; ordered by A.3 rank */
    for (g = 0; g < d->nGroups; g++) {
        const RgDrawnGroup *G = &d->groups[g];

        if (G->isAlt) continue;
        nSeedGroups++;
        CHECK(d->seed[G->key] && d->pool[G->first].layout == G->key && d->pool[G->first].x >= 0);
        if (g > 0 && !d->groups[g - 1].isAlt) CHECK(rg_name_rank(d->groups[g - 1].key) < rg_name_rank(G->key));
        for (i = 0; i < G->nMembers; i++) {
            const RgDrawnMember *m = &d->pool[G->first + i];

            CHECK(!member[m->layout]);
            member[m->layout] = 1;
            seedsInGroups += d->seed[m->layout] ? 1u : 0u;
            CHECK(rg_relief_outdoor(m->layout) && rg_relief_alt_base(m->layout) == 0 && d->blockLayout[m->layout]);
            /* a member is a seed (>= DRAWN_MIN rock cells) or is reached through links */
            if (!d->seed[m->layout]) CHECK(d->rockCount[m->layout] > 0 && d->rockCount[m->layout] < RG_DRAWN_MIN && G->nMembers > 1);
            CHECK(m->x >= 0 && m->y >= 0 && (unsigned)m->x + w->layouts[m->layout - 1].w <= G->cellsW
                  && (unsigned)m->y + w->layouts[m->layout - 1].h <= G->cellsH);
        }
    }
    CHECK(seedsInGroups == d->nSeeds);
    CHECK(nSeedGroups == 31 && d->nGroups == 35 && d->nSeeds == 54 && d->nLinks == 96 && d->nPool == 66);
    /* the four tabled seeds name groups (S3.2 left this assertion for S3.3) */
    for (i = 0; i < 4; i++) {
        const RgDrawnGroup *G = NULL;

        for (g = 0; g < d->nGroups; g++)
            if (!d->groups[g].isAlt && d->groups[g].key == kKeys[i]) G = &d->groups[g];
        CHECK(G != NULL);
    }
    CHECK(rg_drawn_group(d, 38, false, NULL) != NULL && rg_drawn_excluded(rg_drawn_group(d, 38, false, NULL)));
    CHECK(rg_drawn_group(d, 38, true, NULL) == NULL);
    CHECK(rg_drawn_group(d, 20, true, NULL) != NULL && !rg_drawn_excluded(rg_drawn_group(d, 20, true, NULL)));
    CHECK(rg_drawn_group(d, 4, false, NULL) == NULL && rg_drawn_group(d, 10, false, NULL) == NULL);
    {
        uint8_t ok[RG_DRAWN_MAX_GROUPS];

        memset(ok, 1, sizeof ok);
        ok[rg_drawn_group(d, 21, false, NULL) - d->groups] = 0;
        CHECK(rg_drawn_group(d, 21, true, ok) == NULL && rg_drawn_group(d, 21, false, ok) != NULL);
        CHECK(rg_drawn_group(d, 22, true, ok) != NULL);
    }
    /* alias layouts are seeds only through alias_of (raw General rock count is 0) */
    for (i = 0; i < 3; i++) {
        const RgLayout *L = &w->layouts[RG_ALIAS_LAYOUTS[i] - 1u];
        unsigned x, y, raw = 0;

        for (y = 0; y < L->h; y++)
            for (x = 0; x < L->w; x++) raw += rg_is_rock_tile(rg_metatile(L, (int)x, (int)y)) ? 1u : 0u;
        CHECK(raw == 0 && d->seed[RG_ALIAS_LAYOUTS[i]] && d->rockCount[RG_ALIAS_LAYOUTS[i]] >= RG_DRAWN_MIN);
    }
    /* alternate groups: each is a copy of its base's group with the base replaced, same positions */
    for (g = 0; g < d->nGroups; g++) {
        const RgDrawnGroup *G = &d->groups[g], *B;
        uint16_t base = 0;

        if (!G->isAlt) continue;
        base = rg_relief_alt_base(G->key);
        B = rg_drawn_group(d, base, false, NULL);
        CHECK(base != 0 && B != NULL && !B->isAlt && B->nMembers == G->nMembers && B->cellsW == G->cellsW && B->cellsH == G->cellsH);
        for (i = 0; B != NULL && i < G->nMembers; i++) {
            const RgDrawnMember *a = &d->pool[G->first + i], *b = &d->pool[B->first + i];

            CHECK(a->x == b->x && a->y == b->y && a->layout == (b->layout == base ? G->key : b->layout));
        }
    }
}

static void TestLinkInvariants(const RgWorld *w, const RgDrawn *d)
{
    unsigned i, g, j;
    uint32_t h = 2166136261u;

    for (i = 0; i < d->nLinks; i++) {
        const RgDrawnLink *l = &d->links[i];

        CHECK(CountLinks(d, l->b, l->a, -l->dx, -l->dy) >= 1);               /* both sides recorded */
        CHECK(d->blockLayout[l->a] && d->blockLayout[l->b] && l->a != l->b);
        h = (h ^ l->a) * 16777619u; h = (h ^ l->b) * 16777619u;
        h = (h ^ (uint16_t)l->dx) * 16777619u; h = (h ^ (uint16_t)l->dy) * 16777619u;
    }
    if (h != 0xbca01a91u) printf("link hash %08x\n", (unsigned)h);
    CHECK(h == 0xbca01a91u);                     /* the full ordered link list (it fixes BFS order) */
    /* every member's position agrees with its links wherever both ends are in the group */
    for (g = 0; g < d->nGroups; g++) {
        const RgDrawnGroup *G = &d->groups[g];

        for (i = 0; i < G->nMembers; i++)
            for (j = 0; j < G->nMembers; j++) {
                const RgDrawnMember *a = &d->pool[G->first + i], *b = &d->pool[G->first + j];
                unsigned k;

                for (k = 0; k < d->nLinks; k++) {
                    const RgDrawnLink *l = &d->links[k];
                    uint16_t la = a->layout, lb = b->layout;

                    if (G->isAlt) {                       /* links are keyed by the base's id */
                        uint16_t base = rg_relief_alt_base(G->key);
                        la = (la == G->key) ? base : la;
                        lb = (lb == G->key) ? base : lb;
                    }
                    if (l->a == la && l->b == lb) { CHECK(b->x - a->x == l->dx && b->y - a->y == l->dy); }
                }
            }
    }
    /* spec O3: Route 104 / 105 / 106 (20, 21, 22) carry no link: their seams meet no rock; Rustboro (4) has no rock */
    for (i = 0; i < d->nLinks; i++)
        CHECK(d->links[i].a != 20 && d->links[i].a != 21 && d->links[i].a != 22 && d->links[i].a != 4);
    CHECK(!d->blockLayout[4] && d->rockCount[4] == 0);
    (void)w;
}

static void TestMapLinks(const RgWorld *w, const RgDrawn *d)
{
    RgMapLink *L = (RgMapLink *)malloc(RG_MAP_LINKS_MAX * sizeof *L);
    unsigned n, i, k, j, plain = 0, alt = 0;
    RgConn cn[16];

    CHECK(L != NULL);
    if (L == NULL) return;
    n = rg_map_links_sorted(w, L, RG_MAP_LINKS_MAX);
    CHECK(n == 148);
    for (i = 0; i < n; i++) {
        CHECK(rg_relief_outdoor(L[i].a) && rg_relief_outdoor(L[i].b) && L[i].a != L[i].b && L[i].dir >= 1 && L[i].dir <= 4);
        if (i > 0) {                                           /* strictly sorted by (rank a, rank b, dir key, offset) */
            uint16_t ra = rg_name_rank(L[i - 1].a), rb = rg_name_rank(L[i].a);
            bool lt = ra < rb;

            if (ra == rb) {
                ra = rg_name_rank(L[i - 1].b); rb = rg_name_rank(L[i].b);
                lt = ra < rb;
                if (ra == rb) {
                    unsigned ka = rg_dir_sort_key(L[i - 1].dir), kb = rg_dir_sort_key(L[i].dir);

                    lt = ka < kb;
                    if (ka == kb) lt = L[i - 1].offset < L[i].offset;
                }
            }
            CHECK(lt);
        }
    }
    /* independent set: every outdoor-to-outdoor connection of every map in the ROM (not via the A.5 table) */
    for (i = 0; i < w->mapCount; i++) {
        const RgMap *m = &w->maps[i];
        unsigned c = 0;

        if (!rg_relief_outdoor(m->layoutId)) continue;
        c = rg_map_connections(w, m->group, m->num, cn, 16);
        for (k = 0; k < c; k++) {
            const RgMap *t = rg_world_map(w, cn[k].group, cn[k].num);
            bool found = false;

            if (t == NULL || !rg_relief_outdoor(t->layoutId) || t->layoutId == m->layoutId) continue;
            for (j = 0; j < n; j++)
                found = found || (L[j].a == m->layoutId && L[j].b == t->layoutId && L[j].dir == cn[k].dir && L[j].offset == cn[k].offset);
            CHECK(found);
        }
    }
    for (i = 0; i < n; i++) {
        bool involvesAlt = rg_relief_alt_base(L[i].a) != 0 || rg_relief_alt_base(L[i].b) != 0;

        if (involvesAlt) alt++; else plain++;
        if (rg_relief_alt_base(L[i].a) != 0) {                /* an alternate's link has the base's twin */
            bool twin = false;

            for (j = 0; j < n; j++)
                twin = twin || (L[j].a == rg_relief_alt_base(L[i].a) && L[j].b == L[i].b && L[j].dir == L[i].dir && L[j].offset == L[i].offset);
            CHECK(twin || rg_relief_alt_base(L[i].b) != 0);
        }
    }
    CHECK(plain > 0 && alt > 0 && plain + alt == n);
    /* T8: Route 104 (layout 20) connects up to 4 (offset 0), down to 21 (offset 0), right to layout 1 (offset 50) */
    {
        bool up = false, down = false, right = false;

        for (i = 0; i < n; i++) {
            if (L[i].a != 20) continue;
            up = up || (L[i].b == 4 && L[i].dir == 2 && L[i].offset == 0);
            down = down || (L[i].b == 21 && L[i].dir == 1 && L[i].offset == 0);
            right = right || (L[i].b == 1 && L[i].dir == 4 && L[i].offset == 50);
        }
        CHECK(up && down && right);
    }
    (void)d;
    free(L);
}

static void TestRealRom(void)
{
    const char *path = getenv("ROMGEN_ROM");
    size_t n = 0;
    uint8_t *rom;
    RgWorld w;
    RgDrawn *d;

    if (path == NULL || (rom = ReadAll(path, &n)) == NULL) {
        printf("SKIP real-ROM checks (ROMGEN_ROM not set or unreadable)\n");
        sSkips++;
        return;
    }
    CHECK(rg_world_open(&w, rom, n) == RG_OK);
    d = (RgDrawn *)malloc(sizeof *d);
    CHECK(d != NULL && rg_drawn_find(&w, d) == RG_OK);
    if (d != NULL) {
        RgDrawn *e = (RgDrawn *)malloc(sizeof *e);

        TestGroups(&w, d);
        TestLinkInvariants(&w, d);
        TestLinksIndependent(&w, d);
        TestMapLinks(&w, d);
        CHECK(e != NULL && rg_drawn_find(&w, e) == RG_OK && memcmp(d, e, sizeof *d) == 0);   /* deterministic */
        free(e);
    }
    free(d);
    rg_world_close(&w);
    free(rom);
}

int main(void)
{
    TestSeamCells();
    TestRockSets();
    TestRealRom();
    printf("test_romgen_relief_drawn: %d checks, %d failures%s\n", sChecks, sFails, sSkips ? " (SKIPPED real-ROM parts)" : "");
    return sFails != 0;
}
