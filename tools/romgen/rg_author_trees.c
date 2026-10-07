/* rg_author_trees.c -- `romgen author ROM trees`: which General-tileset metatiles are trees (3DGBA original work, GPLv3).
 * Host only. Numbers only: it prints metatile ids and counts and never embeds game text. The output is for a human or
 * agent to review and to turn into the profile's treePart / treeGround tables (Phase 34 T1); it is not committed.
 *
 * Rule credit: the "at least 50 % foliage pixels" test is Gummygamer's (gen1recomp-voxel-frlg, lib/Terrain.lua,
 * MIT): ignore the pixels that are the ground colour, then a metatile is foliage when it has at least 30 other pixels
 * and at least half of them are greenish (g >= r and g - b >= 0.04 * 255), tested on the lower layer and on the upper
 * layer separately; the ground colour is the one that is the commonest colour of the most metatiles (tolerance 14, summed
 * channel distance). Re-implemented here from that description; the reference was read, never executed. The older rg_foliage_ge_half() rule (Zallax: drawn pixels, g > 64 and g > r+16, g > b+16) is printed beside it.
 *
 * Output, for every layout whose primary tileset is the General tileset:
 *   1. every primary metatile that either rule calls foliage, with the cell count over those layouts;
 *   2. 2x2 blocks (greedy, row-major, non-overlapping) whose four cells are all Gummygamer-foliage AND collision-blocked
 *      (his rule only looks at wall cells: tall grass is green too, but it is walkable): tl tr / bl br, with counts;
 *   3. per metatile, how often it sat in each block corner, and the part it would get (0 tl, 1 tr, 2 bl, 3 br);
 *   4. foliage metatiles that never sat in a block (single bushes and fringes). */
#include "rg_author.h"

#include <stdlib.h>
#include <string.h>

#include "rg_art.h"
#include "rg_png.h"

#define TR_PRIM 1024u

typedef struct { uint16_t m[4]; unsigned n; } TrBlock;

static void rgb888(uint16_t c, unsigned *r, unsigned *g, unsigned *b)
{
    *r = (c & 31u) * 255u / 31u;
    *g = ((c >> 5) & 31u) * 255u / 31u;
    *b = ((c >> 10) & 31u) * 255u / 31u;
}

static bool near_key(uint16_t c, unsigned kr, unsigned kg, unsigned kb)
{
    unsigned r, g, b;

    rgb888(c, &r, &g, &b);
    return (r > kr ? r - kr : kr - r) + (g > kg ? g - kg : kg - g) + (b > kb ? b - kb : kb - b) <= 14u;   /* KEY_TOLERANCE */
}

/* Gummygamer's rule for one layer image (`drawn` rows mask the opaque pixels): the pixels near the ground colour are
 * ignored; foliage = at least 30 others and at least half of them green. */
static bool gg_layer(const uint16_t px[16][16], const uint16_t drawn[16], uint16_t key, unsigned *pct)
{
    unsigned x, y, other = 0, green = 0, kr, kg, kb;

    rgb888(key, &kr, &kg, &kb);
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++) {
            unsigned r, g, b;
            if (!((drawn[y] >> x) & 1u) || near_key(px[y][x], kr, kg, kb))
                continue;
            other++;
            rgb888(px[y][x], &r, &g, &b);
            green += (g >= r && g >= b + 11u);   /* g - b >= 0.04 * 255 = 10.2 */
        }
    *pct = other ? 100u * green / other : 0u;
    return other >= 30u && 2u * green >= other;
}

/* The ground colour: the colour that is the commonest of the MOST metatiles' lower layers (a metatile votes when its
 * commonest colour covers a quarter of it), not the colour with the most pixels. */
static uint16_t ground_key(RgPair *p, unsigned count)
{
    static unsigned votes[32768];
    unsigned m, best = 0, bestN = 0, x, y;

    memset(votes, 0, sizeof votes);
    for (m = 0; m < count; m++) {
        RgCellPx lo;
        unsigned top = 0, topN = 0;
        uint16_t seen[256];
        unsigned cnt[256], ns = 0, i;

        rg_cell_px(p, (uint16_t)m, 0, &lo);
        for (y = 0; y < 16; y++)
            for (x = 0; x < 16; x++) {
                uint16_t c = lo.c[y][x] & 0x7FFFu;
                for (i = 0; i < ns && seen[i] != c; i++) {}
                if (i == ns) { seen[ns] = c; cnt[ns] = 0; ns++; }
                if (++cnt[i] > topN) { topN = cnt[i]; top = c; }
            }
        if (topN >= 64u)
            votes[top]++;
    }
    for (m = 0; m < 32768u; m++)
        if (votes[m] > bestN) { bestN = votes[m]; best = m; }
    return (uint16_t)best;
}

/* `trees LO HI`: a contact sheet of metatiles LO..HI-1 (8 per row, composite of both layers, 4x) for the reviewer. */
static int sheet(const RgWorld *w, const char *outDir, unsigned lo, unsigned hi, unsigned layout)
{
    unsigned l, m, x, y, n = hi - lo, rows = (n + 7u) / 8u;
    int W = 8 * 64, H = (int)rows * 64, rc = 1;
    uint8_t *img;
    RgPair *p = NULL;
    char path[1100];

    if (layout >= 1u && layout <= w->layoutCount && w->layouts[layout - 1u].present)
        p = rg_pair_open(w, w->layouts[layout - 1u].pairIndex);   /* L1: that layout's secondary tileset */
    for (l = 0; l < w->layoutCount && p == NULL; l++)
        if (w->layouts[l].present && w->layouts[l].ts[0]->addr == w->prof->tsGeneral)
            p = rg_pair_open(w, w->layouts[l].pairIndex);
    img = (uint8_t *)calloc((size_t)W * (size_t)H, 4);
    if (p == NULL || img == NULL || rg_author_mkdir_p(outDir) != 0 || hi <= lo) {
        free(img);
        if (p != NULL) rg_pair_close(p);
        return 1;
    }
    for (m = lo; m < hi; m++) {
        RgCellPx a, b;
        unsigned cx = ((m - lo) % 8u) * 64u, cy = ((m - lo) / 8u) * 64u;

        rg_cell_px(p, (uint16_t)m, 0, &a);
        rg_cell_px(p, (uint16_t)m, 1, &b);
        for (y = 0; y < 64; y++)
            for (x = 0; x < 64; x++) {
                unsigned sx = x / 4u, sy = y / 4u;
                uint16_t c = ((b.drawn[sy] >> sx) & 1u) ? b.c[sy][sx] : a.c[sy][sx];
                uint8_t *o = img + ((size_t)(cy + y) * (size_t)W + cx + x) * 4u;
                unsigned r, g, bl;
                rgb888(c, &r, &g, &bl);
                o[0] = (uint8_t)r; o[1] = (uint8_t)g; o[2] = (uint8_t)bl; o[3] = 255;
                if (x == 0 || y == 0) { o[0] = 255; o[1] = 0; o[2] = 255; }
            }
    }
    snprintf(path, sizeof path, "%s/trees_%03X_%03X.png", outDir, lo, hi);
    rc = rg_png_write_rgba(path, img, W, H) ? 0 : 1;
    fprintf(stderr, "sheet %s: ids 0x%03X.. row-major, 8 per row, magenta lines = cell edges\n", path, lo);
    free(img);
    rg_pair_close(p);
    return rc;
}

int rg_author_trees(const RgWorld *w, FILE *fp, const char *outDir, int sheetLo, int sheetHi, int layout)
{
    if (sheetHi > sheetLo)
        return sheet(w, outDir, (unsigned)sheetLo, (unsigned)sheetHi, layout > 0 ? (unsigned)layout : 0u);
    return rg_author_trees_list(w, fp);
}

int rg_author_trees_list(const RgWorld *w, FILE *fp)
{
    const GameProfile *gp = w->prof;
    unsigned prim = gp->nPrimMetatiles, l, m, x, y, k, nLayouts = 0, nBlocks = 0;
    RgPair *p0 = NULL;
    uint16_t key = 0;
    static unsigned uses[TR_PRIM], bUses[TR_PRIM], corner[TR_PRIM][4], gpct[TR_PRIM];
    static bool zf[TR_PRIM], gf[TR_PRIM], inBlock[TR_PRIM];
    TrBlock *blk = NULL;
    unsigned cap = 0;

    if (prim > TR_PRIM)
        return 1;
    memset(uses, 0, sizeof uses); memset(bUses, 0, sizeof bUses); memset(corner, 0, sizeof corner); memset(gpct, 0, sizeof gpct);
    memset(zf, 0, sizeof zf); memset(gf, 0, sizeof gf); memset(inBlock, 0, sizeof inBlock);
    for (l = 0; l < w->layoutCount; l++) {
        const RgLayout *L = &w->layouts[l];
        if (L->present && L->ts[0]->addr == gp->tsGeneral) {
            p0 = rg_pair_open(w, L->pairIndex);
            break;
        }
    }
    if (p0 == NULL)
        return 1;
    key = ground_key(p0, prim);
    for (m = 0; m < prim; m++) {
        RgCellPx lo, up;
        unsigned pl, pu;
        uint16_t full[16];
        int yy;

        rg_cell_px(p0, (uint16_t)m, 0, &lo);
        rg_cell_px(p0, (uint16_t)m, 1, &up);
        for (yy = 0; yy < 16; yy++) full[yy] = 0xFFFFu;
        gf[m] = gg_layer((const uint16_t (*)[16])lo.c, full, key, &pl);
        gf[m] = gg_layer((const uint16_t (*)[16])up.c, up.drawn, key, &pu) || gf[m];
        gpct[m] = pl > pu ? pl : pu;
        zf[m] = rg_foliage_ge_half(p0, (uint16_t)m);
    }
    rg_pair_close(p0);
    fprintf(fp, "# trees: %s, primary metatiles %u, ground key colour BGR555 0x%04X\n", gp->dataSubdir, prim, key);

    for (l = 0; l < w->layoutCount; l++) {
        const RgLayout *L = &w->layouts[l];
        unsigned char *used;

        if (!L->present || L->ts[0]->addr != gp->tsGeneral || L->w == 0 || L->h == 0)
            continue;
        nLayouts++;
        used = (unsigned char *)calloc((size_t)L->w * L->h, 1);
        if (used == NULL)
            return 1;
        for (y = 0; y < L->h; y++)
            for (x = 0; x < L->w; x++) {
                unsigned a = rg_metatile(L, (int)x, (int)y);
                if (a < prim) { uses[a]++; bUses[a] += rg_blocked(L, (int)x, (int)y); }
            }
        for (y = 0; y + 1 < L->h; y++)
            for (x = 0; x + 1 < L->w; x++) {
                unsigned c[4];
                bool all = true;
                if (used[y * L->w + x])
                    continue;
                c[0] = rg_metatile(L, (int)x, (int)y);       c[1] = rg_metatile(L, (int)x + 1, (int)y);
                c[2] = rg_metatile(L, (int)x, (int)y + 1);   c[3] = rg_metatile(L, (int)x + 1, (int)y + 1);
                for (k = 0; k < 4; k++)
                    all = all && c[k] < prim && gf[c[k]] && rg_blocked(L, (int)(x + (k & 1u)), (int)(y + (k >> 1)));
                if (!all || used[y * L->w + x + 1] || used[(y + 1) * L->w + x] || used[(y + 1) * L->w + x + 1])
                    continue;
                used[y * L->w + x] = used[y * L->w + x + 1] = used[(y + 1) * L->w + x] = used[(y + 1) * L->w + x + 1] = 1;
                for (k = 0; k < 4; k++) { corner[c[k]][k]++; inBlock[c[k]] = true; }
                for (k = 0; k < nBlocks; k++)
                    if (blk[k].m[0] == c[0] && blk[k].m[1] == c[1] && blk[k].m[2] == c[2] && blk[k].m[3] == c[3]) {
                        blk[k].n++;
                        break;
                    }
                if (k == nBlocks) {
                    if (nBlocks == cap) {
                        TrBlock *nb;
                        cap = cap ? cap * 2u : 64u;
                        nb = (TrBlock *)realloc(blk, cap * sizeof *blk);
                        if (nb == NULL) { free(blk); free(used); return 1; }
                        blk = nb;
                    }
                    blk[nBlocks].m[0] = (uint16_t)c[0]; blk[nBlocks].m[1] = (uint16_t)c[1];
                    blk[nBlocks].m[2] = (uint16_t)c[2]; blk[nBlocks].m[3] = (uint16_t)c[3];
                    blk[nBlocks].n = 1;
                    nBlocks++;
                }
            }
        free(used);
    }
    fprintf(fp, "# %u layouts on the General tileset, %u distinct 2x2 foliage blocks\n", nLayouts, nBlocks);

    fprintf(fp, "# 1. foliage metatiles: id uses, uses on collision-blocked cells, zallax(>=50%% green) gummygamer(g, pct green)\n");
    for (m = 0; m < prim; m++)
        if (zf[m] || gf[m])
            fprintf(fp, "meta 0x%03X uses %u blocked %u z=%d g=%d gpct=%u\n", m, uses[m], bUses[m], zf[m], gf[m], gpct[m]);
    fprintf(fp, "# 2. blocks (tl tr bl br count), most common first\n");
    for (;;) {
        unsigned best = 0, bi = nBlocks;
        for (k = 0; k < nBlocks; k++)
            if (blk[k].n > best) { best = blk[k].n; bi = k; }
        if (bi == nBlocks)
            break;
        fprintf(fp, "block 0x%03X 0x%03X 0x%03X 0x%03X x%u\n", blk[bi].m[0], blk[bi].m[1], blk[bi].m[2], blk[bi].m[3], best);
        blk[bi].n = 0;
    }
    fprintf(fp, "# 3. corner votes (tl tr bl br) and proposed part (-1 = ambiguous across columns)\n");
    for (m = 0; m < prim; m++)
        if (inBlock[m]) {
            unsigned cl = corner[m][0] + corner[m][2], cr = corner[m][1] + corner[m][3];
            unsigned top = corner[m][0] + corner[m][1], bot = corner[m][2] + corner[m][3];
            int col = (cl && cr) ? -1 : (cr ? 1 : 0), row = (top && bot) ? -1 : (bot ? 1 : 0);
            fprintf(fp, "corner 0x%03X %u %u %u %u part %d\n", m, corner[m][0], corner[m][1], corner[m][2], corner[m][3],
                    (col < 0 || row < 0) ? -1 : row * 2 + col);
        }
    fprintf(fp, "# 4. foliage metatiles never in a block\n");
    for (m = 0; m < prim; m++)
        if (gf[m] && !inBlock[m])
            fprintf(fp, "single 0x%03X uses %u blocked %u z=%d g=%d gpct=%u\n", m, uses[m], bUses[m], zf[m], gf[m], gpct[m]);
    free(blk);
    return 0;
}

/* L1 (look backlog): `romgen author ROM shrubs`: one-cell foliage candidates on every General-primary layout, primary
 * AND secondary metatiles (a secondary id means nothing without its tileset, so each candidate is the pair
 * (tileset address, id)). A candidate is a metatile whose composite drawing has at least 60 pixels away from the ground
 * colour, at least 50 % of them leaf green (g >= r + 8 and g >= b + 24, stricter than T1: olive rock passes T1), collision-blocked on at least half of its uses (a bush is an
 * obstacle; grass, flowers and tall grass are walkable), that is not tall grass (profile behaviour set) and not already in the tree tables. The list is for a human to review against the
 * contact sheet <out>/shrubs.png (8 per row, in list order); it is not committed and decides nothing by itself.
 * `shrubs TS` lists every id used with tileset address TS instead (1 = the primary), unfiltered: a family's walkable
 * members (a bush drawn over the sand in front of a hedge) do not pass the collision rule. */
typedef struct { uint32_t ts; uint16_t m, pair, beh, lay, sx, sy; unsigned uses, blocked, pct, other; } ShCand;

static bool sh_in_tables(const GameProfile *gp, unsigned m)
{
    unsigned i;
    for (i = 0; gp->treePart != NULL && i < gp->treePartCount; i++)
        if ((unsigned)gp->treePart[2 * i] == m) return true;
    for (i = 0; gp->treeGround != NULL && i < gp->treeGroundCount; i++)
        if ((unsigned)gp->treeGround[2 * i] == m) return true;
    return false;
}

static void sh_composite(RgPair *p, uint16_t m, uint16_t out[16][16])
{
    RgCellPx a, b;
    unsigned x, y;
    rg_cell_px(p, m, 0, &a);
    rg_cell_px(p, m, 1, &b);
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++)
            out[y][x] = ((b.drawn[y] >> x) & 1u) ? b.c[y][x] : a.c[y][x];
}

int rg_author_shrubs(const RgWorld *w, FILE *fp, const char *outDir, uint32_t onlyTs)
{
    const GameProfile *gp = w->prof;
    unsigned prim = gp->nPrimMetatiles, l, x, y, i, n = 0, cap = 0, kept = 0;
    ShCand *c = NULL;
    RgPair *p0 = NULL;
    uint16_t key;
    unsigned kr, kg, kb;

    for (l = 0; l < w->layoutCount && p0 == NULL; l++)
        if (w->layouts[l].present && w->layouts[l].ts[0]->addr == gp->tsGeneral)
            p0 = rg_pair_open(w, w->layouts[l].pairIndex);
    if (p0 == NULL)
        return 1;
    key = ground_key(p0, prim);
    rg_pair_close(p0);
    rgb888(key, &kr, &kg, &kb);
    for (l = 0; l < w->layoutCount; l++) {
        const RgLayout *L = &w->layouts[l];
        if (!L->present || !L->outdoor || L->ts[0]->addr != gp->tsGeneral)
            continue;
        for (y = 0; y < L->h; y++)
            for (x = 0; x < L->w; x++) {
                unsigned m = rg_metatile(L, (int)x, (int)y);
                uint32_t ts = m < prim ? 0u : L->ts[1]->addr;
                for (i = 0; i < n && !(c[i].ts == ts && c[i].m == m); i++) {}
                if (i == n) {
                    if (n == cap) {
                        ShCand *nc;
                        cap = cap ? cap * 2u : 256u;
                        nc = (ShCand *)realloc(c, cap * sizeof *c);
                        if (nc == NULL) { free(c); return 1; }
                        c = nc;
                    }
                    memset(&c[n], 0, sizeof c[n]);
                    c[n].ts = ts; c[n].m = (uint16_t)m; c[n].pair = L->pairIndex;
                    c[n].beh = rg_behaviour(L, (int)x, (int)y);
                    c[n].lay = L->id; c[n].sx = (uint16_t)x; c[n].sy = (uint16_t)y;
                    n++;
                }
                c[i].uses++;
                c[i].blocked += rg_blocked(L, (int)x, (int)y);
            }
    }
    for (i = 0; i < n; i++) {
        RgPair *p = rg_pair_open(w, c[i].pair);
        uint16_t px[16][16];
        unsigned other = 0, green = 0;
        if (p == NULL) continue;
        sh_composite(p, c[i].m, px);
        rg_pair_close(p);
        for (y = 0; y < 16; y++)
            for (x = 0; x < 16; x++) {
                unsigned r, g, b;
                if (near_key(px[y][x], kr, kg, kb)) continue;
                other++;
                rgb888(px[y][x], &r, &g, &b);
                green += (g >= r + 8u && g >= b + 24u);   /* leaf green: olive rock and sand fail */
            }
        c[i].other = other;
        c[i].pct = other ? 100u * green / other : 0u;
    }
    /* keep the candidates, most used first; with onlyTs, every id of that tileset (0x1 = the primary), unfiltered */
    for (i = 0; i < n; i++)
        if (onlyTs != 0u ? c[i].ts == (onlyTs == 1u ? 0u : onlyTs) : c[i].other >= 60u && 2u * c[i].pct >= 100u && 2u * c[i].blocked >= c[i].uses && !gp_beh(&gp->tallGrass, c[i].beh)
            && !(c[i].ts == 0u && sh_in_tables(gp, c[i].m)))
            c[kept++] = c[i];
    for (i = 1; i < kept; i++) {
        ShCand t = c[i];
        unsigned j = i;
        while (j > 0 && c[j - 1].uses < t.uses) { c[j] = c[j - 1]; j--; }
        c[j] = t;
    }
    fprintf(fp, "# shrubs: %s, ground key 0x%04X, %u candidates (idx ts id uses blocked beh pct other)\n",
            gp->dataSubdir, key, kept);
    for (i = 0; i < kept; i++)
        fprintf(fp, "cand %3u ts 0x%08X 0x%03X uses %u blocked %u beh 0x%02X pct %u other %u at L%u %u,%u\n", i, c[i].ts,
                c[i].m, c[i].uses, c[i].blocked, c[i].beh, c[i].pct, c[i].other, c[i].lay, c[i].sx, c[i].sy);
    if (outDir != NULL && kept > 0) {
        unsigned rows = (kept + 7u) / 8u;
        int W = 8 * 64, H = (int)rows * 64;
        uint8_t *img = (uint8_t *)calloc((size_t)W * (size_t)H, 4);
        char path[1100];
        if (img == NULL || rg_author_mkdir_p(outDir) != 0) { free(img); free(c); return 1; }
        for (i = 0; i < kept; i++) {
            RgPair *p = rg_pair_open(w, c[i].pair);
            uint16_t px[16][16];
            unsigned cx = (i % 8u) * 64u, cy = (i / 8u) * 64u;
            if (p == NULL) continue;
            sh_composite(p, c[i].m, px);
            rg_pair_close(p);
            for (y = 0; y < 64; y++)
                for (x = 0; x < 64; x++) {
                    uint8_t *o = img + ((size_t)(cy + y) * (size_t)W + cx + x) * 4u;
                    unsigned r, g, b;
                    rgb888(px[y / 4u][x / 4u], &r, &g, &b);
                    o[0] = (uint8_t)r; o[1] = (uint8_t)g; o[2] = (uint8_t)b; o[3] = 255;
                    if (x == 0 || y == 0) { o[0] = 255; o[1] = 0; o[2] = 255; }
                }
        }
        snprintf(path, sizeof path, "%s/shrubs.png", outDir);
        if (!rg_png_write_rgba(path, img, W, H)) { free(img); free(c); return 1; }
        fprintf(stderr, "sheet %s: %u candidates, 8 per row, list order\n", path, kept);
        free(img);
    }
    free(c);
    return 0;
}

/* L2 (look backlog): `romgen author ROM grass [BEH...]`: every (tileset address, metatile id) whose behaviour is one of the
 * listed values (default 2 3 7 9 = the Emerald profile's tall-grass set), with its uses and the layer split of its drawing:
 * the pixels the upper layer draws (idx != 0; the lower layer always draws all 256), plus the contact sheet <out>/grass.png
 * (a row per metatile: lower layer | upper layer on magenta | composite). `grass ids TS ID...` lists the given ids of
 * tileset TS (1 = the primary) the same way, whatever their behaviour (flowers have none). Measuring only: decides nothing. */
typedef struct { uint32_t ts; uint16_t m, pair, beh, lay, sx, sy; unsigned uses, upper; } GrCand;

int rg_author_grass(const RgWorld *w, FILE *fp, const char *outDir, const unsigned *behs, unsigned nBehs,
                    uint32_t ts, const unsigned *ids, unsigned nIds)
{
    const GameProfile *gp = w->prof;
    unsigned prim = gp->nPrimMetatiles, l, x, y, i, k, n = 0, cap = 0;
    GrCand *c = NULL;

    for (l = 0; l < w->layoutCount; l++) {
        const RgLayout *L = &w->layouts[l];
        if (!L->present || L->ts[0]->addr != gp->tsGeneral)   /* another primary tileset: the same ids mean other art */
            continue;
        for (y = 0; y < L->h; y++)
            for (x = 0; x < L->w; x++) {
                unsigned m = rg_metatile(L, (int)x, (int)y);
                uint32_t cts = m < prim ? 0u : L->ts[1]->addr;
                unsigned beh = rg_behaviour(L, (int)x, (int)y);
                bool want = false;
                if (nIds) {
                    for (k = 0; k < nIds; k++)
                        want = want || (ids[k] == m && cts == (ts == 1u ? 0u : ts));
                } else {
                    for (k = 0; k < nBehs; k++)
                        want = want || behs[k] == beh;
                }
                if (!want)
                    continue;
                for (i = 0; i < n && !(c[i].ts == cts && c[i].m == m); i++) {}
                if (i == n) {
                    if (n == cap) {
                        GrCand *nc;
                        cap = cap ? cap * 2u : 64u;
                        nc = (GrCand *)realloc(c, cap * sizeof *c);
                        if (nc == NULL) { free(c); return 1; }
                        c = nc;
                    }
                    memset(&c[n], 0, sizeof c[n]);
                    c[n].ts = cts; c[n].m = (uint16_t)m; c[n].pair = L->pairIndex; c[n].beh = (uint16_t)beh;
                    c[n].lay = L->id; c[n].sx = (uint16_t)x; c[n].sy = (uint16_t)y;
                    n++;
                }
                c[i].uses++;
            }
    }
    for (i = 0; i < n; i++) {
        RgPair *p = rg_pair_open(w, c[i].pair);
        RgCellPx b;
        if (p == NULL) continue;
        rg_cell_px(p, c[i].m, 1, &b);
        rg_pair_close(p);
        for (y = 0; y < 16; y++)
            for (x = 0; x < 16; x++)
                c[i].upper += (b.drawn[y] >> x) & 1u;
    }
    for (i = 1; i < n; i++) {
        GrCand t = c[i];
        unsigned j = i;
        while (j > 0 && c[j - 1].uses < t.uses) { c[j] = c[j - 1]; j--; }
        c[j] = t;
    }
    if (!nIds) {
        /* the densest screenful: the most cells of the listed behaviours in a 26 x 16 window (the view is about 25 x 15
         * tiles) of any General-primary layout, with a card budget (12 vertices a cell) */
        unsigned best = 0, bl = 0, bx = 0, by = 0, wy, wx;
        for (l = 0; l < w->layoutCount; l++) {
            const RgLayout *L = &w->layouts[l];
            if (!L->present || L->ts[0]->addr != gp->tsGeneral)
                continue;
            for (wy = 0; wy < L->h; wy++)
                for (wx = 0; wx < L->w; wx++) {
                    unsigned cnt = 0, yy, xx;
                    for (yy = wy; yy < wy + 16u && yy < L->h; yy++)
                        for (xx = wx; xx < wx + 26u && xx < L->w; xx++) {
                            unsigned b = rg_behaviour(L, (int)xx, (int)yy);
                            for (k = 0; k < nBehs; k++)
                                cnt += (behs[k] == b);
                        }
                    if (cnt > best) { best = cnt; bl = L->id; bx = wx; by = wy; }
                }
        }
        fprintf(fp, "# densest 26x16 window: %u cells at L%u %u,%u = %u grass-card vertices\n", best, bl, bx, by, best * 12u);
    }
    fprintf(fp, "# grass: %s, %u metatiles (idx ts id beh uses upperPx/256)\n", gp->dataSubdir, n);
    for (i = 0; i < n; i++)
        fprintf(fp, "grass %3u ts 0x%08X 0x%03X beh 0x%02X uses %u upper %u at L%u %u,%u\n", i, c[i].ts, c[i].m, c[i].beh,
                c[i].uses, c[i].upper, c[i].lay, c[i].sx, c[i].sy);
    if (outDir != NULL && n > 0) {
        int W = 3 * 64, H = (int)n * 64;
        uint8_t *img = (uint8_t *)calloc((size_t)W * (size_t)H, 4);
        char path[1100];
        if (img == NULL || rg_author_mkdir_p(outDir) != 0) { free(img); free(c); return 1; }
        for (i = 0; i < n; i++) {
            RgPair *p = rg_pair_open(w, c[i].pair);
            RgCellPx a, b;
            if (p == NULL) continue;
            rg_cell_px(p, c[i].m, 0, &a);
            rg_cell_px(p, c[i].m, 1, &b);
            rg_pair_close(p);
            for (y = 0; y < 64; y++)
                for (x = 0; x < 192; x++) {
                    unsigned col = x / 64u, px = (x % 64u) / 4u, py = y / 4u, r, g, bl;
                    bool up = ((b.drawn[py] >> px) & 1u) != 0u;
                    uint8_t *o = img + ((size_t)(i * 64u + y) * (size_t)W + x) * 4u;
                    if (col == 0)      rgb888(a.c[py][px], &r, &g, &bl);
                    else if (col == 1) { if (up) rgb888(b.c[py][px], &r, &g, &bl); else { r = 255; g = 0; bl = 255; } }
                    else               { if (up) rgb888(b.c[py][px], &r, &g, &bl); else rgb888(a.c[py][px], &r, &g, &bl); }
                    o[0] = (uint8_t)r; o[1] = (uint8_t)g; o[2] = (uint8_t)bl; o[3] = 255;
                }
        }
        snprintf(path, sizeof path, "%s/grass.png", outDir);
        if (!rg_png_write_rgba(path, img, W, H)) { free(img); free(c); return 1; }
        fprintf(stderr, "sheet %s: %u metatiles, lower | upper | composite\n", path, n);
        free(img);
    }
    free(c);
    return 0;
}

/* L8 (look backlog): romgen author ROM props [MINUSES] -- see rg_author.h. Every (tileset address, metatile id) of the
 * General-primary outdoor layouts used at least MINUSES times (default 20): behaviour, collision-blocked uses, the pixels
 * the upper layer draws (of 256), how often the east / south neighbour is the same id (hz / vt: a fence run's orientation),
 * and the metatile most often found beside it (nbr, +0x800 = a secondary id: what a prop cell stands on). Contact sheets
 * <out>/props-N.png, 48 metatiles per page (8 per row, composite, list order). The human picks fences, rocks and flowers
 * from it; it decides nothing. */
typedef struct { uint32_t ts; uint16_t m, pair, beh, lay, sx, sy, nbr; unsigned uses, blocked, upper, hz, vt, nbrN; } PrCand;

static void pr_rgb(uint16_t c, unsigned *r, unsigned *g, unsigned *b)
{
    *r = (c & 31u) * 255u / 31u;
    *g = ((c >> 5) & 31u) * 255u / 31u;
    *b = ((c >> 10) & 31u) * 255u / 31u;
}

static void pr_composite(RgPair *p, uint16_t m, uint16_t out[16][16])
{
    RgCellPx a, b;
    unsigned x, y;

    rg_cell_px(p, m, 0, &a);
    rg_cell_px(p, m, 1, &b);
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++)
            out[y][x] = ((b.drawn[y] >> x) & 1u) ? b.c[y][x] : a.c[y][x];
}

static bool pr_layout_ok(const GameProfile *gp, const RgLayout *L)
{
    return L->present && L->outdoor && L->ts[0]->addr == gp->tsGeneral;
}

int rg_author_props(const RgWorld *w, FILE *fp, const char *outDir, unsigned minUses)
{
    const GameProfile *gp = w->prof;
    unsigned prim = gp->nPrimMetatiles, l, x, y, i, n = 0, cap = 0, kept = 0;
    PrCand *c = NULL;

    for (l = 0; l < w->layoutCount; l++) {
        const RgLayout *L = &w->layouts[l];
        if (!pr_layout_ok(gp, L))
            continue;
        for (y = 0; y < L->h; y++)
            for (x = 0; x < L->w; x++) {
                unsigned m = rg_metatile(L, (int)x, (int)y);
                uint32_t ts = m < prim ? 0u : L->ts[1]->addr;
                for (i = 0; i < n && !(c[i].ts == ts && c[i].m == m); i++) {}
                if (i == n) {
                    if (n == cap) {
                        PrCand *nc;
                        cap = cap ? cap * 2u : 256u;
                        nc = (PrCand *)realloc(c, cap * sizeof *c);
                        if (nc == NULL) { free(c); return 1; }
                        c = nc;
                    }
                    memset(&c[n], 0, sizeof c[n]);
                    c[n].ts = ts; c[n].m = (uint16_t)m; c[n].pair = L->pairIndex;
                    c[n].beh = (uint16_t)rg_behaviour(L, (int)x, (int)y);
                    c[n].lay = L->id; c[n].sx = (uint16_t)x; c[n].sy = (uint16_t)y;
                    n++;
                }
                c[i].uses++;
                c[i].blocked += rg_blocked(L, (int)x, (int)y);
                if (x + 1u < L->w && rg_metatile(L, (int)x + 1, (int)y) == m) c[i].hz++;
                if (y + 1u < L->h && rg_metatile(L, (int)x, (int)y + 1) == m) c[i].vt++;
            }
    }
    for (i = 0; i < n; i++)
        if (c[i].uses >= minUses)
            c[kept++] = c[i];
    n = kept;
    for (i = 0; i < n; i++) {
        unsigned ids[512], cnt[512], nid = 0, k;
        RgPair *p = rg_pair_open(w, c[i].pair);
        RgCellPx b;

        for (l = 0; l < w->layoutCount; l++) {
            const RgLayout *L = &w->layouts[l];
            if (!pr_layout_ok(gp, L))
                continue;
            for (y = 0; y < L->h; y++)
                for (x = 0; x < L->w; x++) {
                    unsigned m = rg_metatile(L, (int)x, (int)y), d;
                    if (m != c[i].m || (m < prim ? 0u : L->ts[1]->addr) != c[i].ts)
                        continue;
                    for (d = 0; d < 4; d++) {
                        int nx = (int)x + (d == 0) - (d == 1), ny = (int)y + (d == 2) - (d == 3);
                        unsigned nm;
                        if (nx < 0 || ny < 0 || nx >= (int)L->w || ny >= (int)L->h)
                            continue;
                        nm = rg_metatile(L, nx, ny);
                        if (nm == m)
                            continue;
                        if (nm >= prim)
                            nm |= 0x800u;
                        for (k = 0; k < nid && ids[k] != nm; k++) {}
                        if (k == nid && nid < 512u) { ids[nid] = nm; cnt[nid] = 0; nid++; }
                        if (k < nid) cnt[k]++;
                    }
                }
        }
        for (k = 0; k < nid; k++)
            if (cnt[k] > c[i].nbrN) { c[i].nbrN = cnt[k]; c[i].nbr = (uint16_t)ids[k]; }
        if (p == NULL)
            continue;
        rg_cell_px(p, c[i].m, 1, &b);
        rg_pair_close(p);
        for (y = 0; y < 16; y++)
            for (x = 0; x < 16; x++)
                c[i].upper += (b.drawn[y] >> x) & 1u;
    }
    for (i = 1; i < n; i++) {
        PrCand t = c[i];
        unsigned j = i;
        while (j > 0 && c[j - 1].uses < t.uses) { c[j] = c[j - 1]; j--; }
        c[j] = t;
    }
    fprintf(fp, "# props: %s, %u metatiles with >= %u uses (idx ts id beh uses blocked upperPx hz vt nbr x)\n",
            gp->dataSubdir, n, minUses);
    for (i = 0; i < n; i++)
        fprintf(fp, "prop %3u ts 0x%08X 0x%03X beh 0x%02X uses %u blocked %u upper %u hz %u vt %u nbr 0x%03X x%u at L%u %u,%u\n",
                i, c[i].ts, c[i].m, c[i].beh, c[i].uses, c[i].blocked, c[i].upper, c[i].hz, c[i].vt, c[i].nbr, c[i].nbrN,
                c[i].lay, c[i].sx, c[i].sy);
    if (outDir != NULL && n > 0) {
        unsigned page, per = 48;

        if (rg_author_mkdir_p(outDir) != 0) { free(c); return 1; }
        for (page = 0; page * per < n; page++) {
            int W = 8 * 64, H = 6 * 64;
            uint8_t *img = (uint8_t *)calloc((size_t)W * (size_t)H, 4);
            char path[1100];

            if (img == NULL) { free(c); return 1; }
            for (i = page * per; i < n && i < (page + 1) * per; i++) {
                RgPair *p = rg_pair_open(w, c[i].pair);
                uint16_t px[16][16];
                unsigned q = i - page * per, cx = (q % 8u) * 64u, cy = (q / 8u) * 64u;

                if (p == NULL) continue;
                pr_composite(p, c[i].m, px);
                rg_pair_close(p);
                for (y = 0; y < 64; y++)
                    for (x = 0; x < 64; x++) {
                        uint8_t *o = img + ((size_t)(cy + y) * (size_t)W + cx + x) * 4u;
                        unsigned r, g, bl;

                        pr_rgb(px[y / 4u][x / 4u], &r, &g, &bl);
                        o[0] = (uint8_t)r; o[1] = (uint8_t)g; o[2] = (uint8_t)bl; o[3] = 255;
                        if (x == 0 || y == 0) { o[0] = 255; o[1] = 0; o[2] = 255; }
                    }
            }
            snprintf(path, sizeof path, "%s/props-%u.png", outDir, page);
            if (!rg_png_write_rgba(path, img, W, H)) { free(img); free(c); return 1; }
            free(img);
        }
        fprintf(stderr, "sheets %s/props-N.png: %u metatiles, 48 per page, 8 per row, list order\n", outDir, n);
    }
    free(c);
    return 0;
}
