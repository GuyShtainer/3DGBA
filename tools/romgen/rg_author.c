/* rg_author.c -- the building-authoring toolchain: census, art, preview, check, placements (3DGBA original work, GPLv3).
 * Host only. `romgen author ROM <command> ...` (SPEC section 5.2). Every image is ROM-derived: it lands under
 * tools/romgen/out/ (git-ignored) and is never committed or sent anywhere. Numbers only: nothing here embeds game text.
 * Build: linked into build/romgen (tools/romgen/Makefile) together with rg_png.c; the consumer round trip of `check`
 * needs -DRG_AUTHOR_CONSUMER and the voxel consumer sources (the Makefile adds both). */
#include "rg_author.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rg_bcheck.h"
#include "rg_kspecs.h"
#include "rg_png.h"

#ifdef RG_AUTHOR_CONSUMER
#include "voxel_building.h"
#include "voxel_world.h"
#endif

/* ------------------------------------------------------------------------------------------------------------------ */
/* small helpers                                                                                                      */
/* ------------------------------------------------------------------------------------------------------------------ */

int rg_author_mkdir_p(const char *path)
{
    char buf[1024];
    size_t i, n = strlen(path);

    if (n == 0 || n >= sizeof(buf))
        return -1;
    memcpy(buf, path, n + 1u);
    for (i = 1; i <= n; i++) {
        if (buf[i] == '/' || buf[i] == '\0') {
            char c = buf[i];

            buf[i] = '\0';
            if (mkdir(buf, 0755) != 0 && errno != EEXIST)
                return -1;
            buf[i] = c;
        }
    }
    return 0;
}

static const uint8_t *rom_at(const RgWorld *w, uint32_t addr)
{
    return w->rom + (addr - 0x08000000u);
}

static uint32_t fnv32(uint32_t h, const uint8_t *p, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++)
        h = (h ^ p[i]) * 16777619u;
    return h;
}

static const RgSpec *game_specs(const RgWorld *w, unsigned *n)
{
    if (w->prof->game == GP_EMERALD) {
        *n = rg_spec_count;
        return rg_specs;
    }
    return rg_kspecs_table(w->prof, n);
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* the census                                                                                                         */
/* ------------------------------------------------------------------------------------------------------------------ */

/* Map sections (the map header's byte 0x14) that belong to the Sevii Islands, Navel Rock and Birth Island: 0x8F up. Measured
 * on the census: 44 of the 152 placements (the Safari Zone is 0x88 and the last Kanto section is Route 25 at 0x7D). */
bool rg_author_mapsec_is_sevii(unsigned mapsec)
{
    return mapsec >= 0x8Fu;
}

const char *rg_author_mapsec_name(unsigned mapsec)
{
    static char route[16];
    static const char *const kanto[] = {"Pallet", "Viridian", "Pewter", "Cerulean", "Lavender", "Vermilion", "Celadon",
                                        "Fuchsia", "Cinnabar", "Indigo", "Saffron"};

    if (mapsec >= 0x58u && mapsec < 0x58u + sizeof(kanto) / sizeof(kanto[0]))
        return kanto[mapsec - 0x58u];
    if (mapsec >= 0x65u && mapsec <= 0x7Du) {       /* Route N = 0x64 + N */
        snprintf(route, sizeof(route), "Route %u", mapsec - 0x64u);
        return route;
    }
    if (mapsec == 0x7Eu)
        return "Viridian Forest";
    if (mapsec == 0x88u)
        return "Safari Zone";
    if (mapsec >= 0x8Fu)
        return "Sevii";
    return "";
}

static bool is_outdoor_type(unsigned t)
{
    return t == 1u || t == 2u || t == 3u || t == 5u || t == 6u;
}

/* The seed rect around one door: the 4-connected blocked cells of the secondary tileset (metatile >= nPrimMetatiles; the
 * door cell itself counts whether or not it is blocked) that touch it, limited to a 16 x 16 window. A door standing on a primary-tileset metatile (a gate over an arrow mat)
 * seeds a 3 x 3 box above it. The author refines the rect from the `art` image; this is only a starting point. */
static void seed_rect(const RgLayout *L, int dx, int dy, int bb[4])
{
    enum { WIN = 16 };
    uint8_t seen[WIN * WIN];
    int stackX[WIN * WIN], stackY[WIN * WIN], sp = 0;
    int x0 = dx - WIN / 2, y0 = dy - WIN / 2 - 2;
    unsigned nPrim = rg_lprof(L)->nPrimMetatiles;

    memset(seen, 0, sizeof(seen));
    bb[0] = dx; bb[1] = dy; bb[2] = dx + 1; bb[3] = dy + 1;
    if (rg_metatile(L, dx, dy) == RG_NONE || rg_metatile(L, dx, dy) < nPrim) {
        bb[0] = dx - 1; bb[1] = dy - 2; bb[2] = dx + 2; bb[3] = dy + 1;
        return;
    }
    stackX[sp] = dx; stackY[sp] = dy; sp++;
    seen[(dy - y0) * WIN + (dx - x0)] = 1;
    while (sp > 0) {
        int x, y, k;
        static const int dxs[4] = {1, -1, 0, 0}, dys[4] = {0, 0, 1, -1};

        sp--;
        x = stackX[sp]; y = stackY[sp];
        if (x < bb[0]) bb[0] = x;
        if (y < bb[1]) bb[1] = y;
        if (x + 1 > bb[2]) bb[2] = x + 1;
        if (y + 1 > bb[3]) bb[3] = y + 1;
        for (k = 0; k < 4; k++) {
            int nx = x + dxs[k], ny = y + dys[k];
            unsigned mt;

            if (nx < x0 || ny < y0 || nx >= x0 + WIN || ny >= y0 + WIN || rg_off(L, nx, ny) || seen[(ny - y0) * WIN + (nx - x0)])
                continue;
            mt = rg_metatile(L, nx, ny);
            if (mt == RG_NONE || mt < nPrim || (!rg_blocked(L, nx, ny) && !rg_has_warp(L, nx, ny)))
                continue;
            seen[(ny - y0) * WIN + (nx - x0)] = 1;
            stackX[sp] = nx; stackY[sp] = ny; sp++;
        }
    }
}

static uint32_t rect_signature(const RgLayout *L, const int16_t r[4])
{
    uint32_t h = 2166136261u;
    uint8_t hdr[4];
    int x, y;

    hdr[0] = (uint8_t)r[2]; hdr[1] = (uint8_t)(r[2] >> 8); hdr[2] = (uint8_t)r[3]; hdr[3] = (uint8_t)(r[3] >> 8);
    h = fnv32(h, hdr, 4);
    for (y = r[1]; y < r[1] + r[3]; y++)
        for (x = r[0]; x < r[0] + r[2]; x++) {
            uint8_t b[2] = {0xFF, 0xFF};

            if (!rg_off(L, x, y)) {
                b[0] = L->blocks[2u * ((size_t)y * L->w + (size_t)x)];
                b[1] = L->blocks[2u * ((size_t)y * L->w + (size_t)x) + 1u];
            }
            h = fnv32(h, b, 2);
        }
    return h;
}

bool rg_author_census(const RgWorld *w, const RgSpec *specs, unsigned nSpecs, RgCensus *out)
{
    unsigned mi, cap = 0;
    RgBuildModels ms;
    bool haveModels = false;

    memset(out, 0, sizeof(*out));
    for (mi = 0; mi < w->mapCount; mi++) {
        const RgMap *m = &w->maps[mi];
        const uint8_t *h, *ev;
        uint32_t evp, wp;
        unsigned wc, i;
        unsigned destSeen[64][2], nDest = 0;
        int destRow[64];

        if (!is_outdoor_type(m->mapType))
            continue;
        out->outdoorMaps++;
        h = rom_at(w, m->addr);
        evp = rg_rd32(h + 4);
        if (evp == 0)
            continue;
        ev = rom_at(w, evp);
        wc = ev[1];
        wp = rg_rd32(ev + 8);
        for (i = 0; i < wc && wc > 0; i++) {
            const uint8_t *e = rom_at(w, wp) + 8u * i;
            unsigned dn = e[6], dg = e[7], k;
            const RgMap *dm = (dg < w->prof->groupCount) ? rg_world_map(w, dg, dn) : NULL;
            RgCensusRow *r;

            if (dm != NULL && dm->mapType == 3u)
                out->mapWarps++;
            else if (dm != NULL && dm->mapType == 4u)
                out->caveWarps++;
            out->outdoorWarps++;
            if (dm == NULL || dm->mapType != 8u)
                continue;
            out->doorWarps++;
            for (k = 0; k < nDest; k++)
                if (destSeen[k][0] == dg && destSeen[k][1] == dn)
                    break;
            if (k == nDest) {
                if (nDest >= 64u)
                    continue;
                if (out->n == cap) {
                    RgCensusRow *nr;

                    cap = cap ? cap * 2u : 64u;
                    nr = (RgCensusRow *)realloc(out->row, cap * sizeof(RgCensusRow));
                    if (nr == NULL) {
                        rg_census_free(out);
                        return false;
                    }
                    out->row = nr;
                }
                r = &out->row[out->n];
                memset(r, 0, sizeof(*r));
                r->group = m->group; r->num = m->num; r->layout = m->layoutId; r->mapsec = h[0x14];
                r->destGroup = (uint8_t)dg; r->destNum = (uint8_t)dn;
                r->model = -1;
                r->sevii = rg_author_mapsec_is_sevii(r->mapsec);
                destSeen[nDest][0] = dg; destSeen[nDest][1] = dn;
                destRow[nDest] = (int)out->n;
                nDest++;
                out->n++;
                k = nDest - 1u;
            }
            r = &out->row[destRow[k]];
            if (r->nDoors < 8u) {
                r->door[r->nDoors][0] = (int16_t)rg_rd16(e);
                r->door[r->nDoors][1] = (int16_t)rg_rd16(e + 2);
            }
            r->nDoors++;
        }
    }
    /* seed rects (union over the doors) and signatures */
    for (mi = 0; mi < out->n; mi++) {
        RgCensusRow *r = &out->row[mi];
        const RgLayout *L = &w->layouts[r->layout - 1u];
        int bb[4] = {1 << 20, 1 << 20, -(1 << 20), -(1 << 20)}, k, nd = r->nDoors < 8u ? r->nDoors : 8;

        for (k = 0; k < nd; k++) {
            int b[4];

            seed_rect(L, r->door[k][0], r->door[k][1], b);
            if (b[0] < bb[0]) bb[0] = b[0];
            if (b[1] < bb[1]) bb[1] = b[1];
            if (b[2] > bb[2]) bb[2] = b[2];
            if (b[3] > bb[3]) bb[3] = b[3];
        }
        if (bb[0] < 0) bb[0] = 0;
        if (bb[1] < 0) bb[1] = 0;
        if (bb[2] > (int)L->w) bb[2] = L->w;
        if (bb[3] > (int)L->h) bb[3] = L->h;
        r->rect[0] = (int16_t)bb[0]; r->rect[1] = (int16_t)bb[1];
        r->rect[2] = (int16_t)(bb[2] - bb[0]); r->rect[3] = (int16_t)(bb[3] - bb[1]);
        r->sig = rect_signature(L, r->rect);
        if (r->sevii) out->sevii++; else out->mainland++;
    }
    /* coverage: which model stands over a door of the row */
    if (specs != NULL && nSpecs > 0 && rg_build_models(w, specs, nSpecs, &ms) == RG_OK) {
        haveModels = true;
        for (mi = 0; mi < ms.n; mi++) {
            RgPlacementList pl;
            unsigned pi, ri;

            memset(&pl, 0, sizeof(pl));
            if (rg_find_placements(w, &ms.m[mi], &pl) != RG_OK) {
                rg_placements_free(&pl);
                continue;
            }
            for (pi = 0; pi < pl.n; pi++)
                for (ri = 0; ri < out->n; ri++) {
                    RgCensusRow *r = &out->row[ri];
                    unsigned k, nd = r->nDoors < 8u ? r->nDoors : 8u;

                    if (r->layout != pl.p[pi].layout || r->model >= 0)
                        continue;
                    for (k = 0; k < nd; k++)
                        if (r->door[k][0] >= pl.p[pi].px && r->door[k][0] < pl.p[pi].px + ms.m[mi].w &&
                            r->door[k][1] >= pl.p[pi].py && r->door[k][1] < pl.p[pi].py + ms.m[mi].h) {
                            /* the index in the caller's table: expanded specs keep table order for direct rows */
                            const RgSpec *sp = ms.m[mi].spec;

                            r->model = (int)(sp - specs);
                            if (r->model < 0 || r->model >= (int)nSpecs)
                                r->model = (int)mi;
                            break;
                        }
                }
            rg_placements_free(&pl);
        }
    }
    (void)haveModels;
    for (mi = 0; mi < out->n; mi++)
        out->covered += out->row[mi].model >= 0;
    if (haveModels)
        rg_models_free(&ms);
    return true;
}

void rg_census_free(RgCensus *c)
{
    free(c->row);
    memset(c, 0, sizeof(*c));
}

static void census_print(const RgWorld *w, const RgCensus *c, const RgSpec *specs, int fg, int fn, FILE *fp)
{
    unsigned i, shown = 0;

    fprintf(fp, "# map(G/N) mapsec layout doors dest(G/N) seed(x,y,w,h) sig model\n");
    for (i = 0; i < c->n; i++) {
        const RgCensusRow *r = &c->row[i];
        unsigned k, nd = r->nDoors < 8u ? r->nDoors : 8u;

        if (fg >= 0 && (r->group != fg || r->num != fn))
            continue;
        shown++;
        fprintf(fp, "%u/%u 0x%02X L%u ", r->group, r->num, r->mapsec, r->layout);
        for (k = 0; k < nd; k++)
            fprintf(fp, "%s%d,%d", k ? ";" : "", r->door[k][0], r->door[k][1]);
        if (r->nDoors > nd)
            fprintf(fp, ";+%u", r->nDoors - nd);
        fprintf(fp, " -> %u/%u  %d,%d,%d,%d  %08X  %s\n", r->destGroup, r->destNum, r->rect[0], r->rect[1], r->rect[2],
                r->rect[3], r->sig, r->model >= 0 ? specs[r->model].name : "-");
    }
    fprintf(fp, "# %u outdoor maps, %u warps (%u doors to an indoor map, %u to another outdoor map, %u to a cave), %u placements (%u mainland, %u sevii)\n",
            c->outdoorMaps, c->outdoorWarps, c->doorWarps, c->mapWarps, c->caveWarps, c->n, c->mainland, c->sevii);
    {
        unsigned cnt[256], ms;

        memset(cnt, 0, sizeof(cnt));
        for (i = 0; i < c->n; i++)
            cnt[c->row[i].mapsec]++;
        for (ms = 0; ms < 256u; ms++)
            if (cnt[ms])
                fprintf(fp, "# mapsec 0x%02X %-16s %u\n", ms, rg_author_mapsec_name(ms), cnt[ms]);
    }
    (void)w;
    (void)shown;
    fprintf(fp, "covered %u / %u\n", c->covered, c->n);
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* images                                                                                                             */
/* ------------------------------------------------------------------------------------------------------------------ */

static void px_set(RgImage *im, int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    uint8_t *p;

    if (x < 0 || y < 0 || x >= im->w || y >= im->h)
        return;
    p = im->px + ((size_t)y * (size_t)im->w + (size_t)x) * 4u;
    p[0] = r; p[1] = g; p[2] = b; p[3] = a;
}

static const uint8_t kDigit[10][5] = {   /* 3 x 5, one row per byte, bit 2 = left */
    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1},
    {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7}};

static void draw_number(RgImage *im, int x, int y, int v, uint8_t shade)
{
    char s[16];
    size_t i;

    snprintf(s, sizeof(s), "%d", v);
    for (i = 0; s[i]; i++) {
        int d = s[i] - '0', r, c;

        for (r = 0; r < 5; r++)
            for (c = 0; c < 3; c++)
                if (kDigit[d][r] & (4 >> c))
                    px_set(im, x + (int)i * 4 + c, y + r, shade, shade, shade, 255);
    }
}

/* A x4 nearest-neighbour picture of `art` on a grey checkerboard (so transparency reads), with a one-pixel grid
 * every 16 art pixels (one metatile) and a ruler along the top and left edges: a tick every 8 art pixels, a number
 * every 16. `tint` (cw*ch RGB triples, or NULL) blends a colour over each metatile at 40 %. */
#define RULER_L 22
#define RULER_T 12
static bool render_4x(const RgImage *art, const uint8_t *tint, RgImage *out)
{
    int x, y, W = art->w * 4 + RULER_L, H = art->h * 4 + RULER_T;

    if (!rg_img_new(out, W, H))
        return false;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
            px_set(out, x, y, 255, 255, 255, 255);
    for (y = 0; y < art->h * 4; y++)
        for (x = 0; x < art->w * 4; x++) {
            const uint8_t *s = art->px + ((size_t)(y / 4) * (size_t)art->w + (size_t)(x / 4)) * 4u;
            uint8_t chk = (uint8_t)((((x / 8) + (y / 8)) & 1) ? 200 : 232), r = chk, g = chk, b = chk;

            if (s[3] >= 128) {
                r = s[0]; g = s[1]; b = s[2];
            }
            if (tint != NULL) {
                const uint8_t *t = tint + 3u * (size_t)((y / 64) * ((art->w + 15) / 16) + (x / 64));

                if (t[0] | t[1] | t[2]) {
                    r = (uint8_t)((r * 6 + t[0] * 4) / 10);
                    g = (uint8_t)((g * 6 + t[1] * 4) / 10);
                    b = (uint8_t)((b * 6 + t[2] * 4) / 10);
                }
            }
            px_set(out, RULER_L + x, RULER_T + y, r, g, b, 255);
        }
    for (x = 0; x <= art->w; x += 16)
        for (y = 0; y < art->h * 4; y++)
            px_set(out, RULER_L + x * 4 - (x == art->w), RULER_T + y, 255, 0, 255, 255);
    for (y = 0; y <= art->h; y += 16)
        for (x = 0; x < art->w * 4; x++)
            px_set(out, RULER_L + x, RULER_T + y * 4 - (y == art->h), 255, 0, 255, 255);
    for (x = 0; x <= art->w; x += 8) {
        int len = (x % 16 == 0) ? 5 : 3, k;

        for (k = 0; k < len; k++)
            px_set(out, RULER_L + x * 4, RULER_T - 1 - k, 0, 0, 0, 255);
        if (x % 16 == 0 && x < art->w)
            draw_number(out, RULER_L + x * 4 + 2, 0, x, 0);
    }
    for (y = 0; y <= art->h; y += 8) {
        int len = (y % 16 == 0) ? 5 : 3, k;

        for (k = 0; k < len; k++)
            px_set(out, RULER_L - 1 - k, RULER_T + y * 4, 0, 0, 0, 255);
        if (y % 16 == 0 && y < art->h)
            draw_number(out, 2, RULER_T + y * 4 + 2, y, 0);
    }
    return true;
}

static int write_png(const char *dir, const char *name, const RgImage *im)
{
    char path[1100];

    snprintf(path, sizeof(path), "%s/%s", dir, name);
    if (!rg_png_write_rgba(path, im->px, im->w, im->h)) {
        fprintf(stderr, "romgen author: cannot write %s\n", path);
        return 1;
    }
    printf("wrote %s (%dx%d)\n", path, im->w, im->h);
    return 0;
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* art                                                                                                                */
/* ------------------------------------------------------------------------------------------------------------------ */

int rg_author_art(const RgWorld *w, const char *outDir, unsigned layout, int x, int y, int cw, int ch)
{
    const RgLayout *L;
    const GameProfile *gp = w->prof;
    RgPair *p;
    RgImage art, upper, lower, big;
    uint8_t *tint = NULL;
    int cx, cy, rc = 0, tw;
    char name[96];

    if (layout < 1u || layout > w->layoutCount || !w->layouts[layout - 1u].present) {
        fprintf(stderr, "romgen author: layout %u is not present\n", layout);
        return 2;
    }
    L = &w->layouts[layout - 1u];
    if (cw < 1 || ch < 1 || cw > 32 || ch > 32 || x < 0 || y < 0 || x + cw > (int)L->w || y + ch > (int)L->h) {
        fprintf(stderr, "romgen author: rect %d,%d %dx%d is outside layout %u (%ux%u) or larger than 32x32\n", x, y, cw, ch,
                layout, L->w, L->h);
        return 2;
    }
    if (rg_author_mkdir_p(outDir) != 0) {
        fprintf(stderr, "romgen author: cannot create %s\n", outDir);
        return 1;
    }
    p = rg_pair_open(w, L->pairIndex);
    if (p == NULL)
        return 1;
    memset(&art, 0, sizeof(art)); memset(&upper, 0, sizeof(upper)); memset(&lower, 0, sizeof(lower)); memset(&big, 0, sizeof(big));
    tw = cw;
    tint = (uint8_t *)calloc((size_t)((cw * 16 + 15) / 16) * (size_t)((ch * 16 + 15) / 16) * 3u, 1);
    if (tint == NULL || !rg_building_art(w, p, L, x, y, cw, ch, NULL, 0, NULL, false, &art) ||
        !rg_building_art(w, p, L, x, y, cw, ch, NULL, 0, NULL, true, &upper) || !rg_img_new(&lower, cw * 16, ch * 16)) {
        rg_pair_close(p);
        free(tint);
        rg_img_free(&art); rg_img_free(&upper);
        return 1;
    }
    for (cy = 0; cy < ch; cy++)
        for (cx = 0; cx < cw; cx++) {
            unsigned mt = rg_metatile(L, x + cx, y + cy);
            unsigned beh = rg_behaviour(L, x + cx, y + cy);
            const uint16_t cell = (uint16_t)rg_rd16(L->blocks + 2u * ((size_t)(y + cy) * L->w + (size_t)(x + cx)));
            unsigned coll = (cell >> 10) & 3u;
            unsigned layerType = (unsigned)((rg_attr(L, (uint16_t)mt) & gp->layerMask) >> gp->layerShift);
            uint8_t *t = tint + 3u * (size_t)(cy * tw + cx);
            RgImage c16;

            printf("%d %d %u %u %u %u\n", x + cx, y + cy, mt, beh, coll, layerType);
            if (mt != RG_NONE && rg_cell_image_layers(p, (uint16_t)mt, true, &c16)) {
                rg_img_paste(&lower, &c16, cx * 16, cy * 16);
                rg_img_free(&c16);
            }
            if (gp_beh(&gp->water, beh)) { t[0] = 0; t[1] = 200; t[2] = 255; }
            if (gp_beh(&gp->signpost, beh)) { t[0] = 0; t[1] = 0; t[2] = 255; }
            if (rg_has_warp(L, x + cx, y + cy)) { t[0] = 0; t[1] = 200; t[2] = 0; }
            if (coll != 0u && !t[1] && !t[2]) { t[0] = 255; t[1] = 0; t[2] = 0; }
        }
    snprintf(name, sizeof(name), "L%u_%d_%d_1x.png", layout, x, y);
    rc |= write_png(outDir, name, &art);
    if (render_4x(&art, NULL, &big)) {
        snprintf(name, sizeof(name), "L%u_%d_%d_4x.png", layout, x, y);
        rc |= write_png(outDir, name, &big);
        rg_img_free(&big);
    }
    snprintf(name, sizeof(name), "L%u_%d_%d_lower.png", layout, x, y);
    rc |= write_png(outDir, name, &lower);
    snprintf(name, sizeof(name), "L%u_%d_%d_upper.png", layout, x, y);
    rc |= write_png(outDir, name, &upper);
    if (render_4x(&art, tint, &big)) {
        snprintf(name, sizeof(name), "L%u_%d_%d_coll.png", layout, x, y);
        rc |= write_png(outDir, name, &big);
        rg_img_free(&big);
    }
    rg_img_free(&art); rg_img_free(&upper); rg_img_free(&lower);
    free(tint);
    rg_pair_close(p);
    return rc;
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* the model of one spec                                                                                              */
/* ------------------------------------------------------------------------------------------------------------------ */

static const RgSpec *find_spec(const RgSpec *specs, unsigned n, const char *name)
{
    unsigned i;

    for (i = 0; i < n; i++)
        if (strcmp(specs[i].name, name) == 0)
            return &specs[i];
    return NULL;
}

/* The ortho_check transform, kept in one place so the preview matches the gate. */
#define ORTHO_M 32
static void ortho_raster(const RgMesh *m, const RgImage *art, RgRaster *ras)
{
    unsigned i, k;

    for (i = 0; i < m->n; i++) {
        const RgTri *t = &m->t[i];
        double vs[3][6];

        if (t->flags & (RG_TAG_DEPTH | RG_TAG_BEHIND))
            continue;
        for (k = 0; k < 3; k++) {
            vs[k][0] = t->p[k].x + ORTHO_M;
            vs[k][1] = t->p[k].z - t->p[k].y + ORTHO_M;
            vs[k][2] = t->p[k].y + t->p[k].z;
            vs[k][3] = 1.0;
            vs[k][4] = t->p[k].u;
            vs[k][5] = t->p[k].v;
        }
        rg_raster_draw(ras, vs, art, t->shade, (int16_t)t->tag);
    }
}

/* An oblique textured z-buffer view: azimuth about the vertical axis, pitch below the horizon, scale 3. */
static bool oblique(const RgMesh *m, const RgImage *art, double azDeg, double pitchDeg, RgImage *out)
{
    const double S = 3.0, az = azDeg * 3.14159265358979323846 / 180.0, ph = pitchDeg * 3.14159265358979323846 / 180.0;
    double minx = 1e30, maxx = -1e30, miny = 1e30, maxy = -1e30, cx = 0, cz = 0;
    unsigned i, k, n = 0;
    RgRaster ras;
    int W, H, pad = 8;
    size_t px;

    for (i = 0; i < m->n; i++)
        for (k = 0; k < 3; k++) {
            cx += m->t[i].p[k].x; cz += m->t[i].p[k].z; n++;
        }
    if (n == 0)
        return false;
    cx /= n; cz /= n;
    for (i = 0; i < m->n; i++)
        for (k = 0; k < 3; k++) {
            const RgVtx *v = &m->t[i].p[k];
            double dx = v->x - cx, dz = v->z - cz, xr = dx * cos(az) + dz * sin(az), zr = -dx * sin(az) + dz * cos(az);
            double sx = S * xr, sy = S * (zr * sin(ph) - v->y * cos(ph));

            if (sx < minx) minx = sx;
            if (sx > maxx) maxx = sx;
            if (sy < miny) miny = sy;
            if (sy > maxy) maxy = sy;
        }
    W = (int)ceil(maxx - minx) + 2 * pad;
    H = (int)ceil(maxy - miny) + 2 * pad;
    if (W < 8 || H < 8 || W > 4096 || H > 4096 || !rg_raster_init(&ras, W, H))
        return false;
    for (i = 0; i < m->n; i++) {
        const RgTri *t = &m->t[i];
        double vs[3][6];

        if (t->flags & (RG_TAG_DEPTH | RG_TAG_BEHIND))
            continue;
        for (k = 0; k < 3; k++) {
            double dx = t->p[k].x - cx, dz = t->p[k].z - cz, xr = dx * cos(az) + dz * sin(az), zr = -dx * sin(az) + dz * cos(az);

            vs[k][0] = S * xr - minx + pad;
            vs[k][1] = S * (zr * sin(ph) - t->p[k].y * cos(ph)) - miny + pad;
            vs[k][2] = zr * cos(ph) + t->p[k].y * sin(ph);
            vs[k][3] = 1.0;
            vs[k][4] = t->p[k].u;
            vs[k][5] = t->p[k].v;
        }
        rg_raster_draw(&ras, vs, art, t->shade, (int16_t)t->tag);
    }
    if (!rg_img_new(out, W, H)) {
        rg_raster_free(&ras);
        return false;
    }
    for (px = 0; px < (size_t)W * (size_t)H; px++) {
        uint8_t *o = out->px + px * 4u;

        if (ras.owner[px] >= 0) {
            o[0] = ras.rgb[px * 3u]; o[1] = ras.rgb[px * 3u + 1u]; o[2] = ras.rgb[px * 3u + 2u]; o[3] = 255;
        } else {
            o[0] = o[1] = o[2] = 40; o[3] = 255;
        }
    }
    rg_raster_free(&ras);
    return true;
}

/* Builds the one-spec model; NULL + message when the layout pin does not match. */
static bool build_one(const RgWorld *w, const RgSpec *spec, RgBuildModels *ms)
{
    RgErr e = rg_build_models(w, spec, 1, ms);

    if (e != RG_OK) {
        fprintf(stderr, "romgen author: building %s failed (%s)\n", spec->name, rg_err_str(e));
        return false;
    }
    if (ms->n == 0) {
        fprintf(stderr, "romgen author: %s: the layout %u fingerprint does not match (spec pin %08X, layout is %08X)\n",
                spec->name, spec->layoutId, spec->layoutFnv,
                spec->layoutId >= 1 && spec->layoutId <= w->layoutCount && w->layouts[spec->layoutId - 1].present
                    ? (unsigned)rg_layout_fnv(&w->layouts[spec->layoutId - 1]) : 0u);
        rg_models_free(ms);
        return false;
    }
    return true;
}

int rg_author_preview(const RgWorld *w, const char *outDir, const RgSpec *spec)
{
    RgBuildModels ms;
    const RgBuildModel *m;
    RgRaster ras;
    RgImage ortho, diff;
    unsigned ri;
    int x, y, rc = 0;
    char name[128];

    if (rg_author_mkdir_p(outDir) != 0)
        return 1;
    if (!build_one(w, spec, &ms))
        return 1;
    m = &ms.m[0];
    if (!rg_raster_init(&ras, m->art.w + 2 * ORTHO_M, m->art.h + ORTHO_M) || !rg_img_new(&ortho, ras.w, ras.h) ||
        !rg_img_new(&diff, ras.w, ras.h)) {
        rg_models_free(&ms);
        return 1;
    }
    ortho_raster(&m->mesh, &m->art, &ras);
    for (y = 0; y < ras.h; y++)
        for (x = 0; x < ras.w; x++) {
            size_t idx = (size_t)y * (size_t)ras.w + (size_t)x;

            if (ras.owner[idx] >= 0)
                px_set(&ortho, x, y, ras.rgb[idx * 3u], ras.rgb[idx * 3u + 1u], ras.rgb[idx * 3u + 2u], 255);
            else
                px_set(&ortho, x, y, 40, 40, 40, 255);
            px_set(&diff, x, y, 40, 40, 40, 255);
        }
    /* diff: the art faded, then the judged rects painted: wrong red, missing blue, extra yellow, right = the art */
    for (y = 0; y < m->art.h; y++)
        for (x = 0; x < m->art.w; x++) {
            const uint8_t *a = m->art.px + ((size_t)y * (size_t)m->art.w + (size_t)x) * 4u;

            if (a[3] >= 128)
                px_set(&diff, x + ORTHO_M, y + ORTHO_M, (uint8_t)(a[0] / 3 + 60), (uint8_t)(a[1] / 3 + 60), (uint8_t)(a[2] / 3 + 60), 255);
        }
    for (ri = 0; ri < spec->nExact; ri++) {
        const RgExact *e = &spec->exact[ri];

        for (y = e->y0; y < e->y1; y++)
            for (x = e->x0; x < e->x1; x++) {
                const uint8_t *a;
                size_t idx;

                if (x < 0 || y < 0 || x >= m->art.w || y >= m->art.h)
                    continue;
                a = m->art.px + ((size_t)y * (size_t)m->art.w + (size_t)x) * 4u;
                idx = (size_t)(y + ORTHO_M) * (size_t)ras.w + (size_t)(x + ORTHO_M);
                if (a[3] >= 128) {
                    if (ras.owner[idx] < 0)
                        px_set(&diff, x + ORTHO_M, y + ORTHO_M, 30, 80, 255, 255);
                    else if (ras.rgb[idx * 3u] != a[0] || ras.rgb[idx * 3u + 1u] != a[1] || ras.rgb[idx * 3u + 2u] != a[2])
                        px_set(&diff, x + ORTHO_M, y + ORTHO_M, 255, 30, 30, 255);
                    else
                        px_set(&diff, x + ORTHO_M, y + ORTHO_M, a[0], a[1], a[2], 255);
                } else if (ras.owner[idx] >= 0 && !e->behind) {
                    px_set(&diff, x + ORTHO_M, y + ORTHO_M, 255, 230, 0, 255);
                }
            }
    }
    /* extras outside the art's own box (the gate's rects stay inside it, but a stray part may stand outside) */
    for (y = 0; y < ras.h; y++)
        for (x = 0; x < ras.w; x++) {
            int ax = x - ORTHO_M, ay = y - ORTHO_M;
            size_t idx = (size_t)y * (size_t)ras.w + (size_t)x;

            if ((ax < 0 || ay < 0 || ax >= m->art.w || ay >= m->art.h) && ras.owner[idx] >= 0)
                px_set(&diff, x, y, 255, 140, 0, 255);
        }
    snprintf(name, sizeof(name), "%s_ortho.png", spec->name);
    rc |= write_png(outDir, name, &ortho);
    snprintf(name, sizeof(name), "%s_diff.png", spec->name);
    rc |= write_png(outDir, name, &diff);
    rg_img_free(&ortho); rg_img_free(&diff); rg_raster_free(&ras);
    {
        static const struct { const char *suffix; double az, pitch; } views[3] = {{"fl", -35.0, 40.0}, {"fr", 35.0, 40.0}, {"top", 0.0, 40.0}};
        unsigned v;

        for (v = 0; v < 3; v++) {
            RgImage ob;

            if (!oblique(&m->mesh, &m->art, views[v].az, views[v].pitch, &ob)) {
                fprintf(stderr, "romgen author: the %s view failed\n", views[v].suffix);
                rc = 1;
                continue;
            }
            snprintf(name, sizeof(name), "%s_%s.png", spec->name, views[v].suffix);
            rc |= write_png(outDir, name, &ob);
            rg_img_free(&ob);
        }
    }
    rg_models_free(&ms);
    return rc;
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* check and placements                                                                                               */
/* ------------------------------------------------------------------------------------------------------------------ */

#ifdef RG_AUTHOR_CONSUMER
/* Writes a one-model buildings.bin, loads it through the vendored consumer and resolves the first placement's cell. */
static bool consumer_roundtrip(const RgWorld *w, const RgBuildModels *one, const RgPlacementList *pl, char *why, size_t whySize)
{
    RgBuildStats st;
    size_t sz;
    uint8_t *buf;
    char dir[64] = "/tmp/rgauth.XXXXXX", cwd[1024], path[128];
    FILE *fp;
    bool ok = false;
    VoxelMapInstance inst;
    int g = -1;
    float top = 0.0f;

    if (pl->n == 0) {
        snprintf(why, whySize, "no placement to resolve");
        return false;
    }
    memset(&st, 0, sizeof(st));
    sz = rg_buildings_write(w, one, NULL, 0, &st);
    if (sz == 0 || st.err != RG_OK) {
        snprintf(why, whySize, "rg_buildings_write failed (%s)", rg_err_str(st.err));
        return false;
    }
    buf = (uint8_t *)malloc(sz);
    if (buf == NULL || rg_buildings_write(w, one, buf, sz, &st) != sz) {
        free(buf);
        snprintf(why, whySize, "rg_buildings_write (second pass) failed");
        return false;
    }
    if (mkdtemp(dir) == NULL || getcwd(cwd, sizeof(cwd)) == NULL) {
        free(buf);
        snprintf(why, whySize, "no temp directory");
        return false;
    }
    snprintf(path, sizeof(path), "%s/voxel", dir);
    if (mkdir(path, 0755) != 0 || chdir(dir) != 0) {
        free(buf);
        (void)rmdir(dir);
        snprintf(why, whySize, "cannot enter the temp directory");
        return false;
    }
    fp = fopen("voxel/buildings.bin", "wb");
    if (fp != NULL) {
        (void)fwrite(buf, 1, sz, fp);
        (void)fclose(fp);
    }
    memset(&inst, 0, sizeof(inst));
    if (!VoxelBuildings_Init()) {
        snprintf(why, whySize, "VoxelBuildings_Init refused the file");
    } else {
        const RgBuildModel *m = &one->m[0];

        inst.layoutId = pl->p[0].layout;
        if (VoxelBuildings_PageOf(&inst) < 0)
            snprintf(why, whySize, "the placement's layout has no page");
        else if (!VoxelBuildings_CellAt(&inst, pl->p[0].px + m->w / 2, pl->p[0].py + m->h - 1, &g, &top) || g < 0 || top <= 0.0f)
            snprintf(why, whySize, "the placement's cell does not resolve");
        else
            ok = true;
        VoxelBuildings_Shutdown();
    }
    if (chdir(cwd) != 0)
        ok = false;
    snprintf(path, sizeof(path), "%s/voxel/buildings.bin", dir); (void)unlink(path);
    snprintf(path, sizeof(path), "%s/voxel", dir); (void)rmdir(path);
    (void)rmdir(dir);
    free(buf);
    return ok;
}
#endif

int rg_author_check(const RgWorld *w, const RgSpec *spec, int expect, FILE *fp)
{
    RgBuildModels ms;
    const RgBuildModel *m;
    RgPlacementList pl;
    RgDensityBad bad[8];
    unsigned ri, nBad, k;
    bool pass = true;
    char why[160];

    fprintf(fp, "spec %s: layout %u pin %08X rect %d,%d,%d,%d\n", spec->name, spec->layoutId, spec->layoutFnv, spec->rect[0],
            spec->rect[1], spec->rect[2], spec->rect[3]);
    if (!build_one(w, spec, &ms)) {
        fprintf(fp, "  FAIL model: not built (see stderr)\n");
        return 1;
    }
    m = &ms.m[0];
    for (ri = 0; ri < spec->nExact; ri++) {
        RgOrthoResult r;

        if (!rg_ortho_check(&m->mesh, &m->art, &spec->exact[ri], 1, m->hasDrawing ? &m->drawing : NULL, &r)) {
            fprintf(fp, "  FAIL exact[%u]: ortho check ran out of memory\n", ri);
            pass = false;
            continue;
        }
        fprintf(fp, "  exact[%u] (%d,%d,%d,%d%s) wrong %u missing %u extra %u\n", ri, spec->exact[ri].x0, spec->exact[ri].y0,
                spec->exact[ri].x1, spec->exact[ri].y1, spec->exact[ri].behind ? " behind" : "", r.wrong, r.missing, r.extra);
        if (r.wrong || r.missing || r.extra)
            pass = false;
    }
    nBad = rg_density_check(&m->mesh, &m->art, bad, 8);
    fprintf(fp, "  density: %u bad triangle(s)\n", nBad);
    for (k = 0; k < nBad && k < 8u; k++)
        fprintf(fp, "    %s along %.4f down %.4f shear %.4f\n", bad[k].tag, bad[k].along, bad[k].down, bad[k].shear);
    if (nBad)
        pass = false;
    if ((w->prof->game == GP_FIRERED || w->prof->game == GP_LEAFGREEN) && spec->parts != NULL) {
        /* Phase 34: the Kanto table only. Emerald's builders are pinned bytes and never face this gate. */
        RgPartList parts;
        unsigned side;
        bool ran;

        rg_parts_init(&parts);
        ran = spec->parts(spec, spec->arg0, spec->arg1, &parts);
        for (side = 0; side < 2u; side++) {
            RgSideResult sr;
            const char *nm = side ? "east" : "west";

            if (!ran || !rg_side_check(&parts, &m->mesh, side == 1u, &sr)) {
                fprintf(fp, "  FAIL side %s: check could not run\n", nm);
                pass = false;
            } else if (!sr.applicable) {
                fprintf(fp, "  side %s: no prism at the edge (n/a)\n", nm);
            } else {
                bool bad = sr.open > RG_SIDE_TOL(sr.expected);

                fprintf(fp, "  side %s: %u of %u wall cells open (tolerance %u)%s\n", nm, sr.open, sr.expected,
                        RG_SIDE_TOL(sr.expected), bad ? "  FAIL: open side" : "");
                if (bad)
                    pass = false;
            }
        }
        rg_parts_free(&parts);
    }
    memset(&pl, 0, sizeof(pl));
    if (rg_find_placements(w, m, &pl) != RG_OK) {
        fprintf(fp, "  FAIL placements: search failed\n");
        rg_models_free(&ms);
        return 1;
    }
    fprintf(fp, "  placements: %u found", pl.n);
    if (expect >= 0) {
        fprintf(fp, ", expected %d: %s", expect, (int)pl.n == expect ? "ok" : "MISMATCH");
        if ((int)pl.n != expect)
            pass = false;
    }
    fprintf(fp, "\n");
#ifdef RG_AUTHOR_CONSUMER
    if (consumer_roundtrip(w, &ms, &pl, why, sizeof(why))) {
        fprintf(fp, "  round trip: ok\n");
    } else {
        fprintf(fp, "  FAIL round trip: %s\n", why);
        pass = false;
    }
#else
    (void)why;
    fprintf(fp, "  round trip: not built in (compile with -DRG_AUTHOR_CONSUMER)\n");
#endif
    fprintf(fp, "  RESULT %s\n", pass ? "PASS" : "FAIL");
    rg_placements_free(&pl);
    rg_models_free(&ms);
    return pass ? 0 : 1;
}

int rg_author_placements(const RgWorld *w, const RgSpec *spec, FILE *fp)
{
    RgBuildModels ms;
    RgPlacementList pl;
    unsigned i;

    if (!build_one(w, spec, &ms))
        return 1;
    memset(&pl, 0, sizeof(pl));
    if (rg_find_placements(w, &ms.m[0], &pl) != RG_OK) {
        rg_models_free(&ms);
        return 1;
    }
    for (i = 0; i < pl.n; i++) {
        unsigned mi;
        int g = -1, n = -1;

        for (mi = 0; mi < w->mapCount; mi++)
            if (w->maps[mi].layoutId == pl.p[i].layout) {
                g = w->maps[mi].group; n = w->maps[mi].num;
                break;
            }
        fprintf(fp, "L%u %d %d  (map %d/%d)\n", pl.p[i].layout, pl.p[i].px, pl.p[i].py, g, n);
    }
    fprintf(fp, "%s: %u placement(s) across %u layouts\n", spec->name, pl.n, w->layoutCount);
    rg_placements_free(&pl);
    rg_models_free(&ms);
    return 0;
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* the command line                                                                                                   */
/* ------------------------------------------------------------------------------------------------------------------ */

static uint8_t *read_file(const char *path, size_t *n)
{
    FILE *fp = fopen(path, "rb");
    long len;
    uint8_t *b;

    if (fp == NULL)
        return NULL;
    if (fseek(fp, 0, SEEK_END) != 0 || (len = ftell(fp)) <= 0 || len > 0x2000000L || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }
    b = (uint8_t *)malloc((size_t)len);
    if (b == NULL || fread(b, 1, (size_t)len, fp) != (size_t)len) {
        free(b);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    *n = (size_t)len;
    return b;
}

static void usage(void)
{
    fprintf(stderr,
            "usage: romgen author ROM.gba <command> [--out DIR]\n"
            "  census [--map G/N]\n"
            "  art LAYOUT X Y W H\n"
            "  preview SPEC\n"
            "  check [SPEC|TOWN|all] [--expect N]\n"
            "  placements SPEC\n"
            "  trees [LO HI [LAYOUT]]   (list the tree metatiles; or a contact sheet PNG of metatiles LO..HI-1, LAYOUT's tilesets)\n"
            "  shrubs [TS]     (one-cell foliage candidates, primary and secondary, + contact sheet shrubs.png; TS: every id of that tileset)\n"
            "images land in tools/romgen/out/author/<BPRE|BPGE|BPEE>/ (next to the build directory unless --out is given)\n");
}

/* Names matched by `check`: exact, "all", or a town prefix ("pallet" matches "k_pallet_*" and "pallet_*"). */
static bool spec_matches(const RgSpec *s, const char *key)
{
    char pre1[80], pre2[80];

    if (strcmp(key, "all") == 0 || strcmp(s->name, key) == 0)
        return true;
    snprintf(pre1, sizeof(pre1), "k_%s_", key);
    snprintf(pre2, sizeof(pre2), "%s_", key);
    return strncmp(s->name, pre1, strlen(pre1)) == 0 || strncmp(s->name, pre2, strlen(pre2)) == 0;
}

int rg_author_main(int argc, char **argv)
{
    const char *romPath, *cmd, *outOpt = NULL;
    char outDir[1100], base[1024];
    uint8_t *rom;
    size_t n = 0;
    RgWorld w;
    RgErr e;
    const RgSpec *specs;
    unsigned nSpecs = 0, i;
    int rc = 0, ai, nargs = 0;
    const char *args[16];
    int expect = -1, mapG = -1, mapN = -1;

    if (argc < 4) {
        usage();
        return 2;
    }
    romPath = argv[2];
    cmd = argv[3];
    for (ai = 4; ai < argc; ai++) {
        if (strcmp(argv[ai], "--out") == 0 && ai + 1 < argc) {
            outOpt = argv[++ai];
        } else if (strcmp(argv[ai], "--expect") == 0 && ai + 1 < argc) {
            expect = atoi(argv[++ai]);
        } else if (strcmp(argv[ai], "--map") == 0 && ai + 1 < argc) {
            if (sscanf(argv[++ai], "%d/%d", &mapG, &mapN) != 2) {
                usage();
                return 2;
            }
        } else if (nargs < 16) {
            args[nargs++] = argv[ai];
        }
    }
    rom = read_file(romPath, &n);
    if (rom == NULL) {
        fprintf(stderr, "romgen author: cannot read %s\n", romPath);
        return 1;
    }
    e = rg_world_open(&w, rom, n);
    if (e != RG_OK) {
        fprintf(stderr, "romgen author: %s\n", rg_err_str(e));
        free(rom);
        return 1;
    }
    {   /* the default output root sits next to the build directory: <exe dir>/../out/author */
        const char *slash = strrchr(argv[0], '/');
        size_t dl = slash != NULL ? (size_t)(slash - argv[0]) : 0;

        if (slash != NULL && dl < sizeof(base) - 32u) {
            memcpy(base, argv[0], dl);
            snprintf(base + dl, sizeof(base) - dl, "/../out/author");
        } else {
            snprintf(base, sizeof(base), "tools/romgen/out/author");
        }
    }
    snprintf(outDir, sizeof(outDir), "%s/%c%c%c%c", outOpt != NULL ? outOpt : base, w.prof->code[0], w.prof->code[1],
             w.prof->code[2], w.prof->code[3]);
    specs = game_specs(&w, &nSpecs);

    if (strcmp(cmd, "census") == 0) {
        RgCensus c;

        if (!rg_author_census(&w, specs, nSpecs, &c)) {
            rc = 1;
        } else {
            census_print(&w, &c, specs, mapG, mapN, stdout);
            rg_census_free(&c);
        }
    } else if (strcmp(cmd, "art") == 0 && nargs == 5) {
        rc = rg_author_art(&w, outDir, (unsigned)atoi(args[0]), atoi(args[1]), atoi(args[2]), atoi(args[3]), atoi(args[4]));
    } else if ((strcmp(cmd, "preview") == 0 || strcmp(cmd, "placements") == 0) && nargs == 1) {
        const RgSpec *s = find_spec(specs, nSpecs, args[0]);

        if (s == NULL) {
            fprintf(stderr, "romgen author: no spec named %s (%u in the table)\n", args[0], nSpecs);
            rc = 2;
        } else if (strcmp(cmd, "preview") == 0) {
            rc = rg_author_preview(&w, outDir, s);
        } else {
            rc = rg_author_placements(&w, s, stdout);
        }
    } else if (strcmp(cmd, "trees") == 0 && (nargs == 0 || nargs == 2 || nargs == 3)) {
        rc = rg_author_trees(&w, stdout, outDir, nargs ? (int)strtol(args[0], NULL, 0) : 0, nargs ? (int)strtol(args[1], NULL, 0) : 0,
                             nargs == 3 ? atoi(args[2]) : 0);
    } else if (strcmp(cmd, "shrubs") == 0 && nargs <= 1) {
        rc = rg_author_shrubs(&w, stdout, outDir, nargs ? (uint32_t)strtoul(args[0], NULL, 0) : 0u);
    } else if (strcmp(cmd, "grass") == 0) {
        unsigned vals[64], nv = 0, a;
        uint32_t gts = 0;
        bool byId = nargs >= 3 && strcmp(args[0], "ids") == 0;

        if (byId)
            gts = (uint32_t)strtoul(args[1], NULL, 0);
        for (a = byId ? 2u : 0u; a < (unsigned)nargs && nv < 64u; a++)
            vals[nv++] = (unsigned)strtoul(args[a], NULL, 0);
        if (!byId && nv == 0) { vals[0] = 2; vals[1] = 3; vals[2] = 7; vals[3] = 9; nv = 4; }
        rc = rg_author_grass(&w, stdout, outDir, vals, byId ? 0u : nv, gts, vals, byId ? nv : 0u);
    } else if (strcmp(cmd, "check") == 0 && nargs <= 1) {
        const char *key = nargs == 1 ? args[0] : "all";
        unsigned matched = 0, failed = 0;

        for (i = 0; i < nSpecs; i++)
            if (spec_matches(&specs[i], key)) {
                matched++;
                failed += (unsigned)(rg_author_check(&w, &specs[i], expect, stdout) != 0);
            }
        printf("check %s: %u spec(s), %u failed\n", key, matched, failed);
        if (matched == 0 && strcmp(key, "all") != 0) {
            fprintf(stderr, "romgen author: no spec matches %s\n", key);
            rc = 2;
        } else {
            rc = failed ? 1 : 0;
        }
    } else {
        usage();
        rc = 2;
    }
    rg_world_close(&w);
    free(rom);
    return rc;
}
