// test_romgen_frlg_buildings.c -- Phase 34 slice B0: the authoring toolchain and the FRLG buildings plumbing.
//  1. rg_png.c: the PNG writer's output is decoded by this file's own stored-deflate reader and compared pixel for pixel.
//  2. `romgen author ... census` on FireRed and LeafGreen rev 1: the M1 numbers, pinned (real ROMs, ROMGEN_ROM_FR / _LG
//     or firered.gba / leafgreen.gba beside ROMGEN_ROM; without them the real-ROM part prints SKIP).
//  3. the K1 Kanto table (Pallet Town: k_pallet_house x2, k_pallet_lab): gate, placements, a two-model buildings.bin
//     that the vendored consumer loads, and its SHA-1 pin (FR and LG give the identical file).
//  4. the art / preview / check / placements commands run end to end (on Emerald's own table, which has real models).
//
// rg_author.c and rg_png.c live in tools/romgen/ and are #included here so the suite needs no Makefile change.
//
//   ROMGEN_ROM=$PWD/roms/emerald.gba make -C tools/romgen test T=frlg_buildings
#define RG_AUTHOR_CONSUMER 1
#include "../../tools/romgen/rg_author.c"
#include "../../tools/romgen/rg_budget.c"
#include "../../tools/romgen/rg_author_trees.c"
#include "../../tools/romgen/rg_png.c"

#include "rg_fixture.h"
#include "rg_run.h"

static int sChecks, sFails, sSkipped;
#define CHECK(c) do { ++sChecks; if (!(c)) { ++sFails; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

/* ---- 1. a PNG decoder for the writer's own dialect: IHDR, IDAT (zlib, stored blocks only), IEND, filter 0 ---- */
static uint32_t Be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

/* 1 = ok. On success *w, *h and a malloc'd RGBA buffer. Every chunk's CRC and the stream's Adler-32 are verified. */
static int DecodePng(const uint8_t *b, size_t n, int *w, int *h, uint8_t **rgba)
{
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    size_t pos = 8, zn = 0, zcap = 0, rawLen = 0, rawCap, zi, row, rowBytes;
    uint8_t *z = NULL, *raw = NULL, *out;
    int sawIhdr = 0, sawIend = 0, y;

    *rgba = NULL;
    if (n < 8 || memcmp(b, sig, 8) != 0)
        return 0;
    while (pos + 12 <= n && !sawIend) {
        uint32_t len = Be32(b + pos);
        const uint8_t *type = b + pos + 4, *data = b + pos + 8;

        if (pos + 12u + len > n || Be32(data + len) != rg_png_crc32(type, 4u + len, 0)) { free(z); return 0; }
        if (memcmp(type, "IHDR", 4) == 0) {
            if (len != 13 || data[8] != 8 || data[9] != 6 || data[10] || data[11] || data[12]) { free(z); return 0; }
            *w = (int)Be32(data);
            *h = (int)Be32(data + 4);
            sawIhdr = 1;
        } else if (memcmp(type, "IDAT", 4) == 0) {
            if (zn + len > zcap) { zcap = (zn + len) * 2u; z = (uint8_t *)realloc(z, zcap); }
            memcpy(z + zn, data, len);
            zn += len;
        } else if (memcmp(type, "IEND", 4) == 0) {
            sawIend = len == 0;
        }
        pos += 12u + len;
    }
    if (!sawIhdr || !sawIend || pos != n || zn < 6 || *w <= 0 || *h <= 0) { free(z); return 0; }
    if (z[0] != 0x78 || ((z[0] << 8) | z[1]) % 31 != 0) { free(z); return 0; }
    rowBytes = 1u + 4u * (size_t)*w;
    rawCap = rowBytes * (size_t)*h;
    raw = (uint8_t *)malloc(rawCap);
    zi = 2;
    for (;;) {                                   /* stored blocks */
        unsigned final, len, nlen;

        if (zi + 5 > zn) { free(z); free(raw); return 0; }
        final = z[zi] & 1u;
        if ((z[zi] >> 1) != 0) { free(z); free(raw); return 0; }          /* BTYPE must be 00 */
        len = (unsigned)(z[zi + 1] | (z[zi + 2] << 8));
        nlen = (unsigned)(z[zi + 3] | (z[zi + 4] << 8));
        if ((len ^ 0xFFFFu) != nlen || zi + 5 + len > zn || rawLen + len > rawCap) { free(z); free(raw); return 0; }
        memcpy(raw + rawLen, z + zi + 5, len);
        rawLen += len;
        zi += 5u + len;
        if (final)
            break;
    }
    if (rawLen != rawCap || zi + 4 != zn || Be32(z + zi) != rg_png_adler32(raw, rawLen)) { free(z); free(raw); return 0; }
    out = (uint8_t *)malloc((size_t)*w * (size_t)*h * 4u);
    for (y = 0; y < *h; y++) {
        row = rowBytes * (size_t)y;
        if (raw[row] != 0) { free(z); free(raw); free(out); return 0; }       /* filter type 0 only */
        memcpy(out + 4u * (size_t)*w * (size_t)y, raw + row + 1, 4u * (size_t)*w);
    }
    free(z);
    free(raw);
    *rgba = out;
    return 1;
}

static uint8_t *Slurp(const char *path, size_t *n)
{
    FILE *fp = fopen(path, "rb");
    uint8_t *b;
    long len;

    if (fp == NULL) return NULL;
    fseek(fp, 0, SEEK_END); len = ftell(fp); fseek(fp, 0, SEEK_SET);
    b = (uint8_t *)malloc((size_t)len);
    if (fread(b, 1, (size_t)len, fp) != (size_t)len) { free(b); b = NULL; }
    fclose(fp);
    *n = (size_t)len;
    return b;
}

static void TestPng(void)
{
    static const struct { int w, h; } sizes[] = {{1, 1}, {37, 29}, {96, 64}, {200, 100}, {300, 301}};
    unsigned s;
    char dir[64] = "/tmp/rgpng.XXXXXX", path[128];

    /* known answers */
    CHECK(rg_png_crc32((const uint8_t *)"123456789", 9, 0) == 0xCBF43926u);
    CHECK(rg_png_adler32((const uint8_t *)"Wikipedia", 9) == 0x11E60398u);
    CHECK(mkdtemp(dir) != NULL);
    for (s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
        int w = sizes[s].w, h = sizes[s].h, w2, h2, x, y;
        uint8_t *img = (uint8_t *)malloc((size_t)w * (size_t)h * 4u), *buf, *dec = NULL, *file;
        size_t n, fn;

        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++) {
                uint8_t *p = img + 4u * ((size_t)y * (size_t)w + (size_t)x);

                p[0] = (uint8_t)(x * 7 + y); p[1] = (uint8_t)(y * 13 + (x ^ y)); p[2] = (uint8_t)(x * y);
                p[3] = (uint8_t)((x + y) % 5 == 0 ? 0 : (x * 3 + y) % 256);
            }
        n = rg_png_encode(img, w, h, &buf);
        CHECK(n > 0);
        CHECK(DecodePng(buf, n, &w2, &h2, &dec));
        CHECK(dec != NULL && w2 == w && h2 == h && memcmp(dec, img, (size_t)w * (size_t)h * 4u) == 0);
        /* the file route */
        snprintf(path, sizeof(path), "%s/t%u.png", dir, s);
        CHECK(rg_png_write_rgba(path, img, w, h));
        file = Slurp(path, &fn);
        CHECK(file != NULL && fn == n && memcmp(file, buf, n) == 0);
        /* a flipped byte is caught by the CRC / Adler checks of the decoder (the decoder is not vacuous) */
        if (n > 40) {
            uint8_t *bad = (uint8_t *)malloc(n), *d2 = NULL;

            memcpy(bad, buf, n);
            bad[n / 2] ^= 0x10;
            CHECK(!DecodePng(bad, n, &w2, &h2, &d2));
            free(bad); free(d2);
        }
        (void)unlink(path);
        free(img); free(buf); free(dec); free(file);
    }
    /* bad arguments */
    {
        uint8_t px[4] = {0}, *o = NULL;

        CHECK(rg_png_encode(px, 0, 1, &o) == 0 && o == NULL);
        CHECK(rg_png_encode(NULL, 1, 1, &o) == 0);
        CHECK(!rg_png_write_rgba("/nonexistent-dir-rg/x.png", px, 1, 1));
    }
    (void)rmdir(dir);
}

/* ---- 2. the census ---- */
static const struct { unsigned mapsec, count; } kBySec[] = {
    {0x58, 3}, {0x59, 5}, {0x5A, 6}, {0x5B, 9}, {0x5C, 6}, {0x5D, 8}, {0x5E, 9}, {0x5F, 9}, {0x60, 5}, {0x61, 1},
    {0x62, 13}, {0x66, 4}, {0x68, 1}, {0x69, 3}, {0x6A, 2}, {0x6B, 2}, {0x6C, 2}, {0x6E, 2}, {0x6F, 1}, {0x70, 2},
    {0x73, 1}, {0x74, 2}, {0x76, 1}, {0x7A, 1}, {0x7B, 1}, {0x7D, 1}, {0x7E, 2}, {0x88, 6}, {0x8F, 4}, {0x90, 4},
    {0x91, 7}, {0x92, 7}, {0x93, 4}, {0x94, 4}, {0x95, 4}, {0x98, 1}, {0x9A, 1}, {0x9F, 1}, {0xA1, 1}, {0xA5, 2},
    {0xA7, 1}, {0xA9, 1}, {0xAE, 1}, {0xBB, 1}};

static RgCensus sCen[2];

static void TestCensus(const char *name, const char *env, int idx)
{
    size_t n = 0;
    uint8_t *rom = fxr_load_rom(env, &n);
    RgWorld w;
    RgCensus *c = &sCen[idx];
    unsigned cnt[256], i, towns = 0, others = 0, ms, nModel = 0;

    if (rom == NULL) {
        printf("SKIP %s: set ROMGEN_ROM (firered.gba / leafgreen.gba beside it) or %s\n", name, env);
        sSkipped++;
        return;
    }
    CHECK(rg_world_open(&w, rom, n) == RG_OK);
    {
        unsigned nk = 0;
        const RgSpec *k = rg_kspecs_table(w.prof, &nk);          /* K1: the Pallet recipes mark their placements covered */

        CHECK(k != NULL && rg_author_census(&w, k, nk, c));
    }
    /* the SPEC's section 5.1 numbers; "277 door warps" is every warp on an outdoor map (the SPEC's own rule, destination
     * type 8, gives 191 doors: 277 = 191 + 55 map-to-map + 31 cave). The 152 placements and 108 + 44 agree. */
    CHECK(c->outdoorMaps == 76);
    CHECK(c->outdoorWarps == 277);
    CHECK(c->doorWarps == 191 && c->mapWarps == 55 && c->caveWarps == 31);
    CHECK(c->n == 152);
    CHECK(c->mainland == 108 && c->sevii == 44);
    CHECK(c->covered == 151);                                              /* K1 Pallet 3 + K2 landmarks 36 + K3 Viridian, Route 2, Forest gates 8 + K4 Pewter 3 + K5 Cerulean 6, Route 25 1 + K6 Vermilion 4, Routes 5-8 9 + 3 reuse (Saffron x2, Route 15) + K7 Lavender 5 (Tower, three houses, Power Plant) + K8 Celadon 7 (Dept. Store, Mansion, Game Corner, Prize Room, house x3) + K9 Fuchsia, Safari Zone, Routes 11/12/16/18 18 (Fuchsia hall x4, house, Safari entrance, Safari halls, rest houses x4, two gates, two cottages, two Route 12/16 gates) + K10 Saffron 9 (Silph Co., Dojo, Gym, house x3 doors, two gates; map 3/10 only) + K11 5 (Cinnabar Mansion and Lab, Indigo League building, the Route 22 / 23 gate halves; every mainland row is now covered except the excluded S.S. Anne gangway) + KS1 14 (One Island: Network Center and harbor, two purple houses; Two Island: Game Corner, harbor, purple house; Three Island: four purple houses and the red house; Cape Brink: purple house; Three Isle Port) + KS2 10 (Four Island: orange house, lilac house x3, harbor; Five Island: lilac house, edge house, harbor; Lorelei's house; Rocket Warehouse) + KS3 10 (the lilac house on Seven Island, Six Island, Water Path x2 and Sevault Canyon; the Seven / Six Island harbors, the Navel Rock and Birth Island harbors; Trainer Tower; only the excluded S.S. Anne gangway stays uncovered) */
    memset(cnt, 0, sizeof(cnt));
    for (i = 0; i < c->n; i++) {
        const RgCensusRow *r = &c->row[i];
        const RgMap *dm = rg_world_map(&w, r->destGroup, r->destNum);

        cnt[r->mapsec]++;
        CHECK(dm != NULL && dm->mapType == 8);                          /* every destination is an indoor map */
        CHECK(r->nDoors >= 1 && r->model >= -1);
        if (r->model >= 0) nModel++;                                    /* K3: Pallet + landmarks + Viridian/Route 2/Forest cover 47 of 152 */
        CHECK(r->rect[2] >= 1 && r->rect[3] >= 1 && r->rect[0] >= 0 && r->rect[1] >= 0);
        CHECK(r->rect[0] + r->rect[2] <= (int)w.layouts[r->layout - 1].w && r->rect[1] + r->rect[3] <= (int)w.layouts[r->layout - 1].h);
        CHECK(r->sevii == rg_author_mapsec_is_sevii(r->mapsec));
    }
    CHECK(nModel == c->covered);
    for (i = 0; i < sizeof(kBySec) / sizeof(kBySec[0]); i++) {
        CHECK(cnt[kBySec[i].mapsec] == kBySec[i].count);                 /* per map section (towns, routes, Sevii islands) */
        cnt[kBySec[i].mapsec] = 0;
    }
    for (ms = 0; ms < 256; ms++)
        CHECK(cnt[ms] == 0);                                             /* nothing outside the pinned sections */
    for (i = 0; i < c->n; i++) {
        if (c->row[i].sevii) continue;
        if (c->row[i].mapsec >= 0x58 && c->row[i].mapsec <= 0x62) towns++; else others++;
    }
    CHECK(towns == 74 && others == 34);                                  /* "74 in towns, 34 on routes, the Forest and the Safari Zone" */
    /* Pallet Town: three placements; the two houses are the same cell contents (one recipe serves both), the Lab differs */
    {
        const RgCensusRow *p[3];
        unsigned k = 0;

        for (i = 0; i < c->n && k < 3; i++)
            if (c->row[i].group == 3 && c->row[i].num == 0)
                p[k++] = &c->row[i];
        CHECK(k == 3);
        if (k == 3) {
            CHECK(p[0]->sig == p[1]->sig && p[0]->sig != p[2]->sig);
            CHECK(p[0]->layout == 78 && p[0]->door[0][0] == 6 && p[0]->door[0][1] == 7);
        }
    }
    /* the printed table's last line is the coverage meter */
    {
        char *buf = NULL;
        size_t sz = 0;
        unsigned nkt = 0;
        FILE *fp = open_memstream(&buf, &sz);

        census_print(&w, c, rg_kspecs_table(w.prof, &nkt), -1, -1, fp);
        fclose(fp);
        CHECK(buf != NULL && strstr(buf, "covered 151 / 152\n") != NULL && strstr(buf, "152 placements (108 mainland, 44 sevii)") != NULL);
        free(buf);
    }
    rg_world_close(&w);
    free(rom);
    printf("%s census: %u placements (%u mainland, %u sevii), %u warps = %u doors + %u map + %u cave\n", name, c->n, c->mainland,
           c->sevii, c->outdoorWarps, c->doorWarps, c->mapWarps, c->caveWarps);
}

/* ---- 3. the K1 Kanto table: Pallet Town's two recipes, their gate, and a two-model buildings.bin the consumer loads ---- */
static char sDir[64], sCwd[1024];

static int EnterTemp(void)
{
    char sub[96];

    strcpy(sDir, "/tmp/rgfb.XXXXXX");
    if (!mkdtemp(sDir) || !getcwd(sCwd, sizeof(sCwd))) return 0;
    snprintf(sub, sizeof(sub), "%s/voxel", sDir);
    return mkdir(sub, 0755) == 0 && chdir(sDir) == 0;
}

static void LeaveTemp(void)
{
    char p[128];

    if (chdir(sCwd) != 0) abort();
    snprintf(p, sizeof(p), "%s/voxel/buildings.bin", sDir); (void)unlink(p);
    snprintf(p, sizeof(p), "%s/voxel", sDir); (void)rmdir(p);
    (void)rmdir(sDir);
}

/* SHA-1 of the file, for the pin (the same helper as test_romgen_relief_ledge.c) */
static uint32_t Rol(uint32_t v, unsigned n) { return (v << n) | (v >> (32 - n)); }
static void Sha1(const uint8_t *d, size_t n, char hex[41])
{
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    size_t total = ((n + 8) / 64 + 1) * 64, i;
    uint8_t *m = (uint8_t *)calloc(total, 1);
    unsigned k;

    memcpy(m, d, n);
    m[n] = 0x80;
    for (k = 0; k < 8; k++) m[total - 1 - k] = (uint8_t)(((uint64_t)n * 8u) >> (8 * k));
    for (i = 0; i < total; i += 64) {
        uint32_t w[80], a = h[0], b = h[1], c = h[2], dd = h[3], e = h[4];
        for (k = 0; k < 16; k++) w[k] = ((uint32_t)m[i + 4 * k] << 24) | ((uint32_t)m[i + 4 * k + 1] << 16) | ((uint32_t)m[i + 4 * k + 2] << 8) | m[i + 4 * k + 3];
        for (k = 16; k < 80; k++) w[k] = Rol(w[k - 3] ^ w[k - 8] ^ w[k - 14] ^ w[k - 16], 1);
        for (k = 0; k < 80; k++) {
            uint32_t f, kk, t;
            if (k < 20) { f = (b & c) | (~b & dd); kk = 0x5A827999u; }
            else if (k < 40) { f = b ^ c ^ dd; kk = 0x6ED9EBA1u; }
            else if (k < 60) { f = (b & c) | (b & dd) | (c & dd); kk = 0x8F1BBCDCu; }
            else { f = b ^ c ^ dd; kk = 0xCA62C1D6u; }
            t = Rol(a, 5) + f + e + kk + w[k];
            e = dd; dd = c; c = Rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += dd; h[4] += e;
    }
    for (k = 0; k < 5; k++) snprintf(hex + 8 * k, 9, "%08x", h[k]);
    free(m);
}

/* The FR buildings.bin of the Kanto models so far (K1 Pallet, K2 landmarks, K3 Viridian, K4 Pewter, K5 Cerulean, K6 Vermilion and Routes 5-8, K7 Lavender and Route 10, K8 Celadon, K9 Fuchsia and Safari Zone, K10 Saffron, K11 Cinnabar, Indigo Plateau and Routes 22/23, KS1 Sevii One/Two/Three + Cape Brink + Three Isle Port, KS2 Four/Five Island + Resort Gorgeous + Five Isle Meadow, KS3 Six/Seven Island + Water Path + Sevault Canyon + Trainer Tower + Navel Rock + Birth Island); FR = LG. Re-pinned for Phase 34 side walls (end faces closed); re-pinned 459645ee -> b0f63cdc by look L7 (the Trainer Tower and Silph Co. end-cap patches enlarged: 4148 -> 744 and 3488 -> 968 triangles so their platform, porch and entrance fit a chunk); re-pinned b0f63cdc -> e9f54cdd by the L7 follow-up flat-cap merge (a cap piece on a one-colour patch is one polygon, not one per tile repeat: 56151 -> 5186 triangles, Pokemon Tower 20464 -> 176, Power Plant 9292 -> 282; previews pixel-identical); re-pinned e9f54cdd -> 55cbd83c by look L6 (closed backs: rg_close_backs mirrors each gable's front slope about its ridge with a back wall, draws the skipped back walls and tops of flat blocks, a 45-degree back where a flat face would show through the art; the hip ridge gets its back face; ortho 0/0/0 unchanged on every model, heaviest model 996 -> 1200 vertices); re-pinned 55cbd83c -> 51a921db by look L6b (each mirrored roof is laid down from 45 to 15 degrees before the mirror, same art rows, so the rear slope shows from the in-game camera; ortho 0/0/0 unchanged, Emerald unchanged); re-pinned 51a921db -> 5ba2cc16 by look L5 (each closed end face is dressed from the building's own front art: a facade column for the wall, a same-facade window where the depth allows, wall colour in a mirrored gable's triangle; one projected polygon per piece, ortho 0/0/0 unchanged, Emerald unchanged, Rocket warehouse 652 -> 84 triangles); re-pinned 5ba2cc16 -> 23ed0dd9 by Phase 36 H2 (when the walkable ring round a building is mostly water, its footprint ground is the ring's commonest dry metatile instead: 7 placements in Vermilion, Cinnabar, Five Island and Route 12; sizes and counts unchanged). */
#define PALLET_BUILDINGS_SHA1 "23ed0dd9b33caa554fa24fb9f94eb058ff27a4c5"

static uint8_t *sPalletBin[2];
static size_t sPalletBinSize[2];

/* one check output must say "wrong 0 missing 0 extra 0" once per exact rect, an empty density list and a good round trip */
static void CheckSpecText(const RgWorld *w, const RgSpec *sp, int expect)
{
    char *buf = NULL;
    size_t sz = 0, found = 0;
    const char *p;
    FILE *mem = open_memstream(&buf, &sz);

    CHECK(rg_author_check(w, sp, expect, mem) == 0);
    fclose(mem);
    for (p = buf; (p = strstr(p, "wrong 0 missing 0 extra 0")) != NULL; p++)
        found++;
    CHECK(found == sp->nExact);                                           /* ortho 0/0/0 on every exact rect */
    CHECK(strstr(buf, "density: 0 bad triangle(s)") != NULL);             /* density empty */
    CHECK(strstr(buf, "round trip: ok") != NULL && strstr(buf, "RESULT PASS") != NULL);
    CHECK(strstr(buf, "FAIL: open side") == NULL && strstr(buf, "FAIL side") == NULL);   /* both ends closed */
    CHECK(strstr(buf, "  side west:") != NULL && strstr(buf, "  side east:") != NULL);
    free(buf);
}

static void TestPallet(const char *name, const char *env, GpGame game, int idx)
{
    size_t n = 0;
    uint8_t *rom = fxr_load_rom(env, &n);
    RgRunOpts opts;
    RgOutput out;
    RgWorld w;
    unsigned nk = 99, i;
    const RgSpec *k;

    if (rom == NULL) {
        sSkipped++;
        return;
    }
    CHECK(rg_world_open(&w, rom, n) == RG_OK && w.prof->game == game);
    k = rg_kspecs_table(w.prof, &nk);
    CHECK(k != NULL && nk == 85);                                          /* K1-K11: 66 Kanto models + KS1: 7 + KS2: 7 + KS3: 5 Sevii models */
    if (k != NULL && nk == 85) {
        char *buf = NULL;
        size_t sz = 0;
        FILE *mem;

        CHECK(strcmp(k[0].name, "k_pallet_house") == 0 && strcmp(k[1].name, "k_pallet_lab") == 0);
        CHECK(k[0].layoutId == 78 && k[1].layoutId == 78 && k[0].layoutFnv == k[1].layoutFnv);
        CHECK(k[0].rect[0] == 5 && k[0].rect[1] == 4 && k[0].rect[2] == 5 && k[0].rect[3] == 4);        /* 80x64 art */
        CHECK(k[1].rect[0] == 13 && k[1].rect[1] == 10 && k[1].rect[2] == 7 && k[1].rect[3] == 4);      /* 112x64 art */
        CheckSpecText(&w, &k[0], 2);                                      /* both houses */
        CheckSpecText(&w, &k[1], 1);                                      /* Oak's Lab */
        {
            FILE *sink = fopen("/dev/null", "w");

            CHECK(sink != NULL && rg_author_check(&w, &k[0], 1, sink) == 1);    /* a wrong expectation fails */
            if (sink) fclose(sink);
        }
        /* the placements are exactly these three */
        mem = open_memstream(&buf, &sz);
        CHECK(rg_author_placements(&w, &k[0], mem) == 0);
        fclose(mem);
        CHECK(strstr(buf, "L78 5 4  (map 3/0)\n") != NULL && strstr(buf, "L78 14 4  (map 3/0)\n") != NULL);
        CHECK(strstr(buf, "k_pallet_house: 2 placement(s) across 384 layouts") != NULL);
        free(buf);
        buf = NULL; sz = 0;
        mem = open_memstream(&buf, &sz);
        CHECK(rg_author_placements(&w, &k[1], mem) == 0);
        fclose(mem);
        CHECK(strstr(buf, "L78 13 10  (map 3/0)\n") != NULL && strstr(buf, "k_pallet_lab: 1 placement(s) across 384 layouts") != NULL);
        free(buf);
        /* K2: the landmarks. Names, check results (ortho 0/0/0 on every exact rect, density empty, round trip) and the
         * placement counts: Center 18 (16 census + Saffron L207 and One Island L88, pixel-identical on owned pixels),
         * Mart 13 (11 + the same two), Gym 2 + 4 + 1 = 7 by width class. */
        {
            static const char *const nm[5] = {"k_center", "k_mart", "k_gym", "k_gym_7", "k_gym_8"};
            static const int want[5] = {18, 13, 2, 4, 1};
            static const char *const first[5] = {"L79 24 23  (map ", "L79 34 16  (map ", "L79 33 6  (map ", "L80 12 12  (map ", "L84 8 26  (map "};

            for (i = 0; i < 5; i++) {
                char line[96];

                CHECK(strcmp(k[2 + i].name, nm[i]) == 0);
                CheckSpecText(&w, &k[2 + i], want[i]);
                buf = NULL; sz = 0;
                mem = open_memstream(&buf, &sz);
                CHECK(rg_author_placements(&w, &k[2 + i], mem) == 0);
                fclose(mem);
                snprintf(line, sizeof(line), "%s: %d placement(s) across 384 layouts", nm[i], want[i]);
                CHECK(strstr(buf, line) != NULL && strstr(buf, first[i]) != NULL);
                free(buf);
            }
        }
        /* K3: Viridian City, Route 2 and the Viridian Forest gates: eight models, one placement each (a gate half is a
         * partial building at the map edge; the Route 2 east building is the walk-through gatehouse). */
        {
            static const char *const nm[8] = {"k_viridian_house", "k_viridian_house2", "k_route2_house", "k_route2_gate",
                                              "k_route2_gate_s", "k_route2_gate_n", "k_forest_gate_n", "k_forest_gate_s"};
            static const char *const first[8] = {"L79 24 8  (map 3/1)\n", "L79 24 15  (map 3/1)\n", "L90 14 20  (map 3/20)\n",
                                                 "L90 16 41  (map 3/20)\n", "L90 2 45  (map 3/20)\n", "L90 2 13  (map 3/20)\n",
                                                 "L117 0 4  (map 1/0)\n", "L117 24 62  (map 1/0)\n"};

            for (i = 0; i < 8; i++) {
                char line[96];

                CHECK(strcmp(k[7 + i].name, nm[i]) == 0);
                CheckSpecText(&w, &k[7 + i], 1);
                buf = NULL; sz = 0;
                mem = open_memstream(&buf, &sz);
                CHECK(rg_author_placements(&w, &k[7 + i], mem) == 0);
                fclose(mem);
                snprintf(line, sizeof(line), "%s: 1 placement(s) across 384 layouts", nm[i]);
                CHECK(strstr(buf, line) != NULL && strstr(buf, first[i]) != NULL);
                free(buf);
            }
            CHECK(k[9].layoutFnv == 0x5E505C50u && k[12].layoutFnv == 0x5E505C50u && k[13].layoutFnv == 0x1DED0623u && k[14].layoutFnv == 0x1DED0623u);
        }
        /* K4: Pewter City: the house (two placements, one signature) and the Museum (one). The Gym, Mart and Center are K2's. */
        {
            static const char *const nm[2] = {"k_pewter_house", "k_pewter_museum"};
            static const int want[2] = {2, 1};
            static const char *const first[2] = {"L80 32 8  (map 3/2)\nL80 8 27  (map 3/2)\n", "L80 12 0  (map 3/2)\n"};

            for (i = 0; i < 2; i++) {
                char line[96];

                CHECK(strcmp(k[15 + i].name, nm[i]) == 0);
                CHECK(k[15 + i].layoutFnv == 0xAAB0C96Cu);
                CheckSpecText(&w, &k[15 + i], want[i]);
                buf = NULL; sz = 0;
                mem = open_memstream(&buf, &sz);
                CHECK(rg_author_placements(&w, &k[15 + i], mem) == 0);
                fclose(mem);
                snprintf(line, sizeof(line), "%s: %d placement(s) across 384 layouts", nm[i], want[i]);
                CHECK(strstr(buf, line) != NULL && strstr(buf, first[i]) != NULL);
                free(buf);
            }
        }
        /* K5: Cerulean City (five blue-roofed houses, the Bike Shop) and the Route 25 Sea Cottage. Route 4 has only the Center. */
        {
            static const char *const nm[7] = {"k_cerulean_house_a", "k_cerulean_house_b", "k_cerulean_house_c", "k_cerulean_house_d",
                                              "k_cerulean_house_e", "k_cerulean_bike", "k_route25_cottage"};
            static const char *const first[7] = {"L81 8 8  (map 3/3)\n", "L81 15 8  (map 3/3)\n", "L81 28 8  (map 3/3)\n",
                                                 "L81 13 14  (map 3/3)\n", "L81 21 25  (map 3/3)\n", "L81 12 23  (map 3/3)\n",
                                                 "L113 49 1  (map 3/44)\n"};

            for (i = 0; i < 7; i++) {
                char line[96];

                CHECK(strcmp(k[17 + i].name, nm[i]) == 0);
                CHECK(k[17 + i].layoutFnv == (i == 6 ? 0xBBAC050Au : 0xB952CCF4u));
                CheckSpecText(&w, &k[17 + i], 1);
                buf = NULL; sz = 0;
                mem = open_memstream(&buf, &sz);
                CHECK(rg_author_placements(&w, &k[17 + i], mem) == 0);
                fclose(mem);
                snprintf(line, sizeof(line), "%s: 1 placement(s) across 384 layouts", nm[i]);
                CHECK(strstr(buf, line) != NULL && strstr(buf, first[i]) != NULL);
                free(buf);
            }
        }
        /* K6: Vermilion City (Fan Club, two house models, the green-roofed building), the Underground Path hut x4 across
         * Routes 5-8, the Day Care, and the four Saffron gatehouse halves. Placements: the hut lands on all four routes; the
         * Route 7 and 8 gates also land on Saffron (L207) and Route 15 (L103), a deliberate reuse of the same cells. */
        {
            static const char *const nm[9] = {"k_vermilion_fanclub", "k_vermilion_house", "k_vermilion_green", "k_path_hut",
                                              "k_daycare", "k_route5_gate", "k_route6_gate", "k_route7_gate", "k_route8_gate"};
            static const unsigned cnt[9] = {1, 2, 1, 4, 1, 1, 1, 2, 3};
            static const char *const first[9] = {"L83 8 3  (map 3/5)\n", "L83 18 14  (map 3/5)\n", "L83 11 14  (map 3/5)\n",
                                                 "L93 30 28  (map 3/23)\n", "L93 21 21  (map 3/23)\n", "L93 22 32  (map 3/23)\n",
                                                 "L94 9 0  (map 3/24)\n", "L95 15 7  (map 3/25)\n", "L96 0 7  (map 3/26)\n"};
            static const uint32_t fnv[9] = {0x82FF5FADu, 0x82FF5FADu, 0x82FF5FADu, 0xA0C68725u, 0xA0C68725u, 0xA0C68725u,
                                            0xD03FD324u, 0x4C1E067Eu, 0x87638ADFu};

            for (i = 0; i < 9; i++) {
                char line[96];

                CHECK(strcmp(k[24 + i].name, nm[i]) == 0);
                CHECK(k[24 + i].layoutFnv == fnv[i]);
                CheckSpecText(&w, &k[24 + i], (int)cnt[i]);
                buf = NULL; sz = 0;
                mem = open_memstream(&buf, &sz);
                CHECK(rg_author_placements(&w, &k[24 + i], mem) == 0);
                fclose(mem);
                snprintf(line, sizeof(line), "%s: %u placement(s) across 384 layouts", nm[i], cnt[i]);
                CHECK(strstr(buf, line) != NULL && strstr(buf, first[i]) != NULL);
                if (i == 3)                                                 /* the hut: all four routes, and nothing else */
                    CHECK(strstr(buf, "L94 18 10  (map 3/24)\n") != NULL && strstr(buf, "L95 6 11  (map 3/25)\n") != NULL &&
                          strstr(buf, "L96 12 1  (map 3/26)\n") != NULL);
                if (i == 7)
                    CHECK(strstr(buf, "L207 1 24  (map 3/10)\n") != NULL);
                if (i == 8)
                    CHECK(strstr(buf, "L103 9 8  (map 3/33)\n") != NULL && strstr(buf, "L207 58 24  (map 3/10)\n") != NULL);
                free(buf);
            }
        }
        /* K7: Lavender Town (the Pokemon Tower, the purple-roofed house x3: one signature, three placements) and the Route 10
         * Power Plant. The Center and Mart are K2's. Row order in the table: tower, power plant, house. */
        {
            static const char *const nm[3] = {"k_pokemon_tower", "k_power_plant", "k_lavender_house"};
            static const unsigned cnt[3] = {1, 1, 3};
            static const char *const first[3] = {"L82 14 0  (map 3/4)\n", "L98 2 34  (map 3/28)\n", "L82 8 8  (map 3/4)\n"};
            static const uint32_t fnv[3] = {0xB6187344u, 0xEB232F5Au, 0xB6187344u};

            for (i = 0; i < 3; i++) {
                char line[96];

                CHECK(strcmp(k[33 + i].name, nm[i]) == 0);
                CHECK(k[33 + i].layoutFnv == fnv[i]);
                CheckSpecText(&w, &k[33 + i], (int)cnt[i]);
                buf = NULL; sz = 0;
                mem = open_memstream(&buf, &sz);
                CHECK(rg_author_placements(&w, &k[33 + i], mem) == 0);
                fclose(mem);
                snprintf(line, sizeof(line), "%s: %u placement(s) across 384 layouts", nm[i], cnt[i]);
                CHECK(strstr(buf, line) != NULL && strstr(buf, first[i]) != NULL);
                if (i == 2)                                                 /* the side-by-side pair */
                    CHECK(strstr(buf, "L82 3 13  (map 3/4)\n") != NULL && strstr(buf, "L82 8 13  (map 3/4)\n") != NULL);
                free(buf);
            }
        }
        /* K8: Celadon City (L84, map 3/6): the green house x3 (one signature, three placements), the Game Corner, the Prize
         * Room, the Dept. Store with its two wings and the Mansion. The Center and Gym are K2's. Table order: house, game
         * corner, prize, dept, mansion. All five sit on layout 84 (FNV 6B8BA7E4). */
        {
            static const char *const nm[5] = {"k_celadon_house", "k_celadon_game_corner", "k_celadon_prize", "k_celadon_dept",
                                              "k_celadon_mansion"};
            static const unsigned cnt[5] = {3, 1, 1, 1, 1};
            static const char *const first[5] = {"L84 36 25  (map 3/6)\n", "L84 31 17  (map 3/6)\n", "L84 38 17  (map 3/6)\n",
                                                 "L84 4 4  (map 3/6)\n", "L84 27 3  (map 3/6)\n"};

            for (i = 0; i < 5; i++) {
                char line[96];

                CHECK(strcmp(k[36 + i].name, nm[i]) == 0);
                CHECK(k[36 + i].layoutId == 84 && k[36 + i].layoutFnv == 0x6B8BA7E4u);
                CheckSpecText(&w, &k[36 + i], (int)cnt[i]);
                buf = NULL; sz = 0;
                mem = open_memstream(&buf, &sz);
                CHECK(rg_author_placements(&w, &k[36 + i], mem) == 0);
                fclose(mem);
                snprintf(line, sizeof(line), "%s: %u placement(s) across 384 layouts", nm[i], cnt[i]);
                CHECK(strstr(buf, line) != NULL && strstr(buf, first[i]) != NULL);
                if (i == 0)                                                 /* the three doors' houses */
                    CHECK(strstr(buf, "L84 40 25  (map 3/6)\n") != NULL && strstr(buf, "L84 48 25  (map 3/6)\n") != NULL);
                free(buf);
            }
        }
        /* K9: Fuchsia City, the Safari Zone, Routes 11 / 12 / 16 / 18. Table order from index 41. Every placement is checked by
         * its first cell ("L<layout> <x> <y> "), the four-placement models by all four. */
        {
            static const char *const nm[12] = {"k_fuchsia_hall", "k_fuchsia_house", "k_fuchsia_safari", "k_safari_west",
                                               "k_safari_hall", "k_route11_gate", "k_route18_gate", "k_route12_cottage",
                                               "k_route16_cottage", "k_route12_gate", "k_route16_gate", "k_safari_rest"};
            static const unsigned cnt[12] = {4, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 4};
            static const char *const first[12] = {"L85 13 28 ", "L85 26 12 ", "L85 22 0 ", "L150 10 2 ", "L147 22 30 ", "L99 58 7 ",
                                                  "L106 41 6 ", "L100 11 84 ", "L104 9 3 ", "L100 12 15 ", "L104 20 3 ", "L147 28 22 "};

            for (i = 0; i < 12; i++) {
                char line[96];

                CHECK(strcmp(k[41 + i].name, nm[i]) == 0);
                CheckSpecText(&w, &k[41 + i], (int)cnt[i]);
                buf = NULL; sz = 0;
                mem = open_memstream(&buf, &sz);
                CHECK(rg_author_placements(&w, &k[41 + i], mem) == 0);
                fclose(mem);
                snprintf(line, sizeof(line), "%s: %u placement(s) across 384 layouts", nm[i], cnt[i]);
                CHECK(strstr(buf, line) != NULL && strstr(buf, first[i]) != NULL);
                if (i == 0)
                    CHECK(strstr(buf, "L85 18 28 ") != NULL && strstr(buf, "L85 32 28 ") != NULL && strstr(buf, "L85 37 28 ") != NULL);
                if (i == 11)                                                /* the four rest houses over four layouts */
                    CHECK(strstr(buf, "L148 39 11 ") != NULL && strstr(buf, "L149 42 5 ") != NULL && strstr(buf, "L150 18 15 ") != NULL);
                free(buf);
            }
        }
        /* K10: Saffron City (L207, FNV 0730F5C6, map 3/10): Silph Co., the three house widths, the Dojo, the Gym and the two
         * gate halves. Table order from index 53. Several models also land on layout 88 (map 3/11, a second Saffron-like
         * layout without census rows), and the 3-wide house also lands on the 4-wide houses' cells (a pixel-identical
         * overlap); the placement counts below include those. */
        {
            static const char *const nm[8] = {"k_saffron_silph", "k_saffron_house", "k_saffron_house3", "k_saffron_house5",
                                              "k_saffron_dojo", "k_saffron_gym", "k_saffron_gate", "k_saffron_gate_s"};
            static const unsigned cnt[8] = {2, 4, 6, 2, 2, 2, 1, 1};
            static const char *const first[8] = {"L207 29 16 ", "L207 21 10 ", "L207 26 17 ", "L207 41 34 ", "L207 37 8 ",
                                                 "L207 43 8 ", "L207 31 0 ", "L207 31 46 "};

            for (i = 0; i < 8; i++) {
                char line[96];

                CHECK(strcmp(k[53 + i].name, nm[i]) == 0);
                CHECK(k[53 + i].layoutId == 207 && k[53 + i].layoutFnv == 0x0730F5C6u);
                CheckSpecText(&w, &k[53 + i], (int)cnt[i]);
                buf = NULL; sz = 0;
                mem = open_memstream(&buf, &sz);
                CHECK(rg_author_placements(&w, &k[53 + i], mem) == 0);
                fclose(mem);
                snprintf(line, sizeof(line), "%s: %u placement(s) across 384 layouts", nm[i], cnt[i]);
                CHECK(strstr(buf, line) != NULL && strstr(buf, first[i]) != NULL);
                if (i == 0)                                                 /* the same 9x15 cells on layout 88 */
                    CHECK(strstr(buf, "L88 19 9  (map 3/11)\n") != NULL);
                if (i == 1)                                                 /* the second 4-wide house and the layout-88 pair */
                    CHECK(strstr(buf, "L207 46 17 ") != NULL && strstr(buf, "L88 11 3 ") != NULL && strstr(buf, "L88 36 10 ") != NULL);
                if (i == 2)                                                 /* the 3-wide house on every 4-wide cell set too */
                    CHECK(strstr(buf, "L207 21 10 ") != NULL && strstr(buf, "L207 46 17 ") != NULL && strstr(buf, "L88 16 10 ") != NULL);
                free(buf);
            }
        }
        /* K11: Cinnabar Island (L86, map 3/8), the Indigo Plateau (L87, 3/9) and the two halves of the Route 22 / 23 gatehouse
         * (L110 on 3/41, L111 on 3/42). Table order from index 61, one placement each. Every check text must show both
         * side lines closed ("0 of N open"). */
        {
            static const char *const nm[5] = {"k_cinnabar_mansion", "k_cinnabar_lab", "k_indigo_league", "k_route22_gate",
                                              "k_route23_gate"};
            static const unsigned lay[5] = {86, 86, 87, 110, 111};
            static const uint32_t fnv[5] = {0xC8348D4Bu, 0xC8348D4Bu, 0x7014A55Cu, 0x5424564Fu, 0xC64404D8u};
            static const char *const first[5] = {"L86 5 0  (map 3/8)", "L86 5 6  (map 3/8)", "L87 6 0  (map 3/9)",
                                                 "L110 4 0  (map 3/41)", "L111 4 153  (map 3/42)"};

            for (i = 0; i < 5; i++) {
                char line[96];

                CHECK(strcmp(k[61 + i].name, nm[i]) == 0);
                CHECK(k[61 + i].layoutId == lay[i] && k[61 + i].layoutFnv == fnv[i]);
                CheckSpecText(&w, &k[61 + i], 1);
                buf = NULL; sz = 0;
                mem = open_memstream(&buf, &sz);
                CHECK(rg_author_check(&w, &k[61 + i], 1, mem) == 0);
                fclose(mem);
                CHECK(strstr(buf, "side west: 0 of ") != NULL && strstr(buf, "side east: 0 of ") != NULL);
                free(buf);
                buf = NULL; sz = 0;
                mem = open_memstream(&buf, &sz);
                CHECK(rg_author_placements(&w, &k[61 + i], mem) == 0);
                fclose(mem);
                snprintf(line, sizeof(line), "%s: 1 placement(s) across 384 layouts", nm[i]);
                CHECK(strstr(buf, line) != NULL && strstr(buf, first[i]) != NULL);
                free(buf);
            }
        }
        /* KS1: the Sevii Islands One, Two and Three (+ Cape Brink, Three Isle Port). Table order from index 66. The purple
         * house is one spec with 8 placements (One 2, Two 1, Three 4, Cape Brink 1); every other model has one. Every
         * check text must show both side lines closed ("0 of N open"). */
        {
            static const char *const nm[7] = {"k_sevii_house", "k_one_network", "k_one_harbor", "k_two_gamecorner",
                                              "k_two_harbor", "k_three_house_red", "k_three_port"};
            static const unsigned lay[7] = {230, 230, 230, 231, 231, 232, 241};
            static const uint32_t fnv[7] = {0x60EA96AFu, 0x60EA96AFu, 0x60EA96AFu, 0x9AB6DD1Fu, 0x9AB6DD1Fu, 0x76E27C1Eu,
                                            0x9A123C3Eu};
            static const unsigned np[7] = {8, 1, 1, 1, 1, 1, 1};
            static const char *const first[7] = {"L230 18 6  (map 3/12)", "L230 11 0  (map 3/12)", "L230 9 15  (map 3/12)",
                                                 "L231 37 6  (map 3/13)", "L231 7 7  (map 3/13)", "L232 2 28  (map 3/14)",
                                                 "L241 9 12  (map 3/49)"};

            for (i = 0; i < 7; i++) {
                char line[96];

                CHECK(strcmp(k[66 + i].name, nm[i]) == 0);
                CHECK(k[66 + i].layoutId == lay[i] && k[66 + i].layoutFnv == fnv[i]);
                CheckSpecText(&w, &k[66 + i], np[i]);
                buf = NULL; sz = 0;
                mem = open_memstream(&buf, &sz);
                CHECK(rg_author_check(&w, &k[66 + i], (int)np[i], mem) == 0);
                fclose(mem);
                CHECK(strstr(buf, "side west: 0 of ") != NULL && strstr(buf, "side east: 0 of ") != NULL);
                CHECK(strstr(buf, "FAIL: open side") == NULL && strstr(buf, "RESULT PASS") != NULL);
                free(buf);
                buf = NULL; sz = 0;
                mem = open_memstream(&buf, &sz);
                CHECK(rg_author_placements(&w, &k[66 + i], mem) == 0);
                fclose(mem);
                snprintf(line, sizeof(line), "%s: %u placement(s) across 384 layouts", nm[i], np[i]);
                CHECK(strstr(buf, line) != NULL && strstr(buf, first[i]) != NULL);
                free(buf);
            }
            /* the Cape Brink house is the 8th placement of the purple house (door in map 3/47) */
            buf = NULL; sz = 0;
            mem = open_memstream(&buf, &sz);
            CHECK(rg_author_placements(&w, &k[66], mem) == 0);
            fclose(mem);
            CHECK(strstr(buf, "L239 11 13  (map 3/47)\n") != NULL && strstr(buf, "L232 12 16  (map 3/14)\n") != NULL);
            free(buf);
        }
        /* KS2: Four Island, Five Island, Resort Gorgeous and Five Isle Meadow. Table order from index 73. The lilac house is
         * one spec with 4 placements (Four Island 3, Five Island 1); every other model has one. Every check text must show
         * both side lines closed ("0 of N open"). */
        {
            static const char *const nm[7] = {"k_four_house_orange", "k_four_house", "k_five_house_edge", "k_four_harbor",
                                              "k_five_harbor", "k_lorelei_house", "k_rocket_warehouse"};
            static const unsigned lay[7] = {233, 233, 234, 233, 234, 246, 248};
            static const uint32_t fnv[7] = {0xF927FC39u, 0xF927FC39u, 0x2AA31FFFu, 0xF927FC39u, 0x2AA31FFFu, 0x7C0F16BEu,
                                            0x70F9C5B3u};
            static const unsigned np[7] = {1, 4, 1, 1, 1, 1, 1};
            static const char *const first[7] = {"L233 11 10  (map 3/15)", "L233 24 11  (map 3/15)", "L234 21 6  (map 3/16)",
                                                 "L233 7 28  (map 3/15)", "L234 9 14  (map 3/16)", "L246 38 5  (map 3/54)",
                                                 "L248 9 17  (map 3/56)"};

            for (i = 0; i < 7; i++) {
                char line[96];

                CHECK(strcmp(k[73 + i].name, nm[i]) == 0);
                CHECK(k[73 + i].layoutId == lay[i] && k[73 + i].layoutFnv == fnv[i]);
                CheckSpecText(&w, &k[73 + i], np[i]);
                buf = NULL; sz = 0;
                mem = open_memstream(&buf, &sz);
                CHECK(rg_author_check(&w, &k[73 + i], (int)np[i], mem) == 0);
                fclose(mem);
                CHECK(strstr(buf, "side west: 0 of ") != NULL && strstr(buf, "side east: 0 of ") != NULL);
                CHECK(strstr(buf, "FAIL: open side") == NULL && strstr(buf, "RESULT PASS") != NULL);
                free(buf);
                buf = NULL; sz = 0;
                mem = open_memstream(&buf, &sz);
                CHECK(rg_author_placements(&w, &k[73 + i], mem) == 0);
                fclose(mem);
                snprintf(line, sizeof(line), "%s: %u placement(s) across 384 layouts", nm[i], np[i]);
                CHECK(strstr(buf, line) != NULL && strstr(buf, first[i]) != NULL);
                free(buf);
            }
            /* the lilac house also lands on Four Island twice more and on Five Island (door in map 3/16) */
            buf = NULL; sz = 0;
            mem = open_memstream(&buf, &sz);
            CHECK(rg_author_placements(&w, &k[74], mem) == 0);
            fclose(mem);
            CHECK(strstr(buf, "L233 32 20  (map 3/15)\n") != NULL && strstr(buf, "L233 24 23  (map 3/15)\n") != NULL &&
                  strstr(buf, "L234 11 3  (map 3/16)\n") != NULL);
            free(buf);
        }
        /* KS3: Seven Island, Six Island, Water Path, Sevault Canyon, Navel Rock, Birth Island and Trainer Tower. Table order
         * from index 80. The lilac house is one spec with 5 placements (Seven Island 1, Six Island 1, Water Path 2, Sevault
         * Canyon 1) and the Seven / Six Island harbor one with 2; every other model has one. Every check text must show both
         * side lines closed ("0 of N open"). */
        {
            static const char *const nm[5] = {"k_seven_house", "k_seven_harbor", "k_navel_harbor", "k_birth_harbor",
                                              "k_trainer_tower"};
            static const unsigned lay[5] = {235, 235, 343, 342, 254};
            static const uint32_t fnv[5] = {0x64A249C1u, 0x64A249C1u, 0x45EADA9Bu, 0xA2E502C5u, 0xD2E7C700u};
            static const unsigned np[5] = {5, 2, 1, 1, 1};
            static const char *const first[5] = {"L235 10 6  (map 3/17)", "L235 13 13  (map 3/17)", "L343 6 16  (map 2/0)",
                                                 "L342 12 24  (map 2/56)", "L254 54 0  (map 3/62)"};

            for (i = 0; i < 5; i++) {
                char line[96];

                CHECK(strcmp(k[80 + i].name, nm[i]) == 0);
                CHECK(k[80 + i].layoutId == lay[i] && k[80 + i].layoutFnv == fnv[i]);
                CheckSpecText(&w, &k[80 + i], np[i]);
                buf = NULL; sz = 0;
                mem = open_memstream(&buf, &sz);
                CHECK(rg_author_check(&w, &k[80 + i], (int)np[i], mem) == 0);
                fclose(mem);
                CHECK(strstr(buf, "side west: 0 of ") != NULL && strstr(buf, "side east: 0 of ") != NULL);
                CHECK(strstr(buf, "FAIL: open side") == NULL && strstr(buf, "RESULT PASS") != NULL);
                free(buf);
                buf = NULL; sz = 0;
                mem = open_memstream(&buf, &sz);
                CHECK(rg_author_placements(&w, &k[80 + i], mem) == 0);
                fclose(mem);
                snprintf(line, sizeof(line), "%s: %u placement(s) across 384 layouts", nm[i], np[i]);
                CHECK(strstr(buf, line) != NULL && strstr(buf, first[i]) != NULL);
                free(buf);
            }
            /* the lilac house also lands on Six Island, twice on Water Path (3/60) and on Sevault Canyon (3/64); the
             * Seven Island harbor model also lands on Six Island's (3/18) */
            buf = NULL; sz = 0;
            mem = open_memstream(&buf, &sz);
            CHECK(rg_author_placements(&w, &k[80], mem) == 0);
            fclose(mem);
            CHECK(strstr(buf, "L236 15 14  (map 3/18)\n") != NULL && strstr(buf, "L252 4 10  (map 3/60)\n") != NULL &&
                  strstr(buf, "L252 10 16  (map 3/60)\n") != NULL && strstr(buf, "L256 13 58  (map 3/64)\n") != NULL);
            free(buf);
            buf = NULL; sz = 0;
            mem = open_memstream(&buf, &sz);
            CHECK(rg_author_placements(&w, &k[81], mem) == 0);
            fclose(mem);
            CHECK(strstr(buf, "L236 8 23  (map 3/18)\n") != NULL);
            free(buf);
        }
        /* Look L7: the Trainer Tower's platform and porch (its LAST parts) were never drawn in-game: the model was 4148
         * triangles (12444 vertices) because its end caps were one quad per 4x4 patch, and a map chunk's vertex scratch
         * (ctr_voxel.c VOXEL_CHUNK_SCRATCH, 9344) refused the tail. Pin: the model stays far under that budget, and the
         * platform front (z 96-110, y <= 6) and the porch face (z 128, y <= 18, inside the 9x8-cell rect) are in the mesh. */
        {
            RgBuildModels ms;
            unsigned t, plat = 0, porch = 0, over = 0;

            memset(&ms, 0, sizeof(ms));
            CHECK(rg_build_models(&w, &k[84], 1, &ms) == RG_OK && ms.n == 1 && strcmp(ms.m[0].spec->name, "k_trainer_tower") == 0);
            if (ms.n == 1) {
                const RgMesh *mesh = &ms.m[0].mesh;

                CHECK(mesh->n * 3u <= 4000u);               /* was 12444; the chunk scratch is 9344 with the ground in it */
                for (t = 0; t < mesh->n; t++) {
                    const RgTri *tr = &mesh->t[t];
                    double zmax = tr->p[0].z, ymax = tr->p[0].y, zmin = tr->p[0].z;
                    unsigned j;

                    for (j = 1; j < 3; j++) {
                        if (tr->p[j].z > zmax) zmax = tr->p[j].z;
                        if (tr->p[j].z < zmin) zmin = tr->p[j].z;
                        if (tr->p[j].y > ymax) ymax = tr->p[j].y;
                    }
                    if (zmax > 128.0 + 1e-6 || zmin < 0.0)
                        over++;                             /* nothing outside the art rect rows 0-7 */
                    if (zmax >= 110.0 - 1e-6 && zmax <= 110.0 + 1e-6 && ymax <= 6.0 + 1e-6)
                        plat++;                             /* the platform's front edge */
                    if (zmax >= 128.0 - 1e-6 && ymax <= 18.0 + 1e-6 && ymax > 6.0)
                        porch++;                            /* the porch's front face, the door */
                }
                CHECK(over == 0 && plat > 0 && porch > 0);
            }
            rg_models_free(&ms);
        }
        /* The same cause on Silph Co. (9342 of 9344 scratch vertices, 824 triangles refused: its entrance canopy was never
         * drawn): its end-cap patches are now the large flat ones, 968 triangles. */
        for (i = 0; i < nk; i++) {
            RgBuildModels ms;

            if (strcmp(k[i].name, "k_saffron_silph") != 0)
                continue;
            memset(&ms, 0, sizeof(ms));
            CHECK(rg_build_models(&w, &k[i], 1, &ms) == RG_OK && ms.n == 1 && ms.m[0].mesh.n * 3u <= 4000u);
            rg_models_free(&ms);
        }
    }
    rg_world_close(&w);
    memset(&opts, 0, sizeof(opts));
    opts.wantBuildings = true;
    CHECK(rg_run(rom, n, &opts, &out) == RG_OK);
    CHECK(out.buildings != NULL && out.buildingsSize > 24 && memcmp(out.buildings, "VXB7", 4) == 0);
    CHECK(out.bModels == 85 && out.bPlacements == 164 && out.buildingsFailed == 0);
    CHECK(out.regions != NULL && out.signs == NULL && out.relief == NULL);   /* G2: regions are always made; signposts only on wantSigns; relief stays off for FRLG */
    CHECK(out.layouts == 384 && out.maps == 425 && out.outdoorMaps == 76);
    if (out.buildings != NULL) {
        sPalletBin[idx] = (uint8_t *)malloc(out.buildingsSize);
        if (sPalletBin[idx] != NULL) {
            memcpy(sPalletBin[idx], out.buildings, out.buildingsSize);
            sPalletBinSize[idx] = out.buildingsSize;
        }
    }
    if (out.buildings != NULL && EnterTemp()) {
        static const int in[6][2] = {{5, 4}, {9, 7}, {14, 4}, {18, 7}, {13, 10}, {19, 13}};     /* corners of the 3 rects */
        static const int outside[6][2] = {{4, 4}, {10, 4}, {12, 10}, {20, 13}, {13, 9}, {5, 8}};
        VoxelMapInstance inst;
        FILE *fp = fopen("voxel/buildings.bin", "wb");
        int g = -1;
        float top = 0.0f;

        CHECK(fp != NULL && fwrite(out.buildings, 1, out.buildingsSize, fp) == out.buildingsSize);
        if (fp) fclose(fp);
        CHECK(VoxelBuildings_Init());                                      /* the vendored consumer loads the two-model file */
        memset(&inst, 0, sizeof(inst));
        inst.layoutId = 78;                                                /* Pallet Town */
        CHECK(VoxelBuildings_PageOf(&inst) >= 0);
        CHECK(VoxelBuildings_MaxTop() > 0.0f);
        for (i = 0; i < 6; i++) {
            CHECK(VoxelBuildings_CellAt(&inst, in[i][0], in[i][1], &g, &top));
            CHECK(i % 2 == 0 || top > 1.0f);                               /* the bottom corners stand under the model */
            CHECK(!VoxelBuildings_CellAt(&inst, outside[i][0], outside[i][1], &g, &top));
        }
        inst.layoutId = 79;                                                /* K2: Viridian has the Center, Mart and Gym */
        CHECK(VoxelBuildings_PageOf(&inst) >= 0);
        CHECK(VoxelBuildings_CellAt(&inst, 24, 23, &g, &top) && VoxelBuildings_CellAt(&inst, 33, 6, &g, &top));
        inst.layoutId = 1;                                                 /* a layout without a recipe places nothing */
        CHECK(VoxelBuildings_PageOf(&inst) < 0);
        VoxelBuildings_Shutdown();
        LeaveTemp();
    }
    rg_output_free(&out);
    free(rom);
    printf("%s: Kanto K1-K11 + Sevii KS1-KS3 -> 85 models, 164 placements, buildings.bin %zu bytes\n", name, sPalletBinSize[idx]);
}

/* ---- 3b. the side-closure check on synthetic models (no ROM): an open-sided prism fails, a capped one passes ---- */
static void SideBox(RgPartList *pl, bool capped)
{
    static const double pts[4][2] = {{24, 0}, {24, 24}, {0, 24}, {0, 0}};
    RgPart *pt = rg_parts_add(pl, RG_P_PRISM, "box");
    RgPrism *pr;
    unsigned i;

    CHECK(pt != NULL);
    if (pt == NULL)
        return;
    pr = &pt->u.prism;
    pr->x0 = 0; pr->x1 = 40;
    pr->nPoly = 4;
    for (i = 0; i < 4; i++) {
        pr->poly[i][0] = pts[i][0]; pr->poly[i][1] = pts[i][1];
        if (i == 0) {
            pr->edges[i].kind = RG_EM_PROJ;
            pr->edges[i].proj = rg_proj_rows(0, 24);
        } else {
            pr->skip |= 1u << i;
        }
    }
    pr->west = pr->east = capped;       /* an end face is drawn only through a cap band */
    if (capped) {
        pr->hasCaps = true;
        pr->nCaps = 1;
        pr->caps[0] = rg_band(-1, 25, rg_tile_top(0, 0, 4, 4, 24), 24);
    }
}

static void TestSideCheck(void)
{
    unsigned c, side;

    for (c = 0; c < 2; c++) {
        RgPartList pl;
        RgMesh m;

        rg_mesh_init(&m);
        rg_parts_init(&pl);
        SideBox(&pl, c == 1);
        CHECK(rg_parts_emit(&pl, &m) && m.n > 0);
        for (side = 0; side < 2; side++) {
            RgSideResult sr;

            CHECK(rg_side_check(&pl, &m, side == 1, &sr) && sr.applicable && sr.expected > 0);
            if (c == 0)
                CHECK(sr.open > RG_SIDE_TOL(sr.expected));      /* the open-sided model must FAIL */
            else
                CHECK(sr.open <= RG_SIDE_TOL(sr.expected));     /* closing both ends passes */
        }
        rg_mesh_free(&m);
        rg_parts_free(&pl);
    }
}

/* ---- 4. the commands end to end, on Emerald (its table has real models) ---- */
static void TestCommands(void)
{
    const char *path = getenv("ROMGEN_ROM");
    size_t n = 0, i;
    FILE *fp = path ? fopen(path, "rb") : NULL;
    uint8_t *rom;
    RgWorld w;
    char dir[64] = "/tmp/rgcmd.XXXXXX", file[160];
    const RgSpec *sp = NULL;
    FILE *sink;

    if (fp == NULL) {
        printf("SKIP commands: ROMGEN_ROM not set\n");
        sSkipped++;
        return;
    }
    fseek(fp, 0, SEEK_END); n = (size_t)ftell(fp); fseek(fp, 0, SEEK_SET);
    rom = (uint8_t *)malloc(n);
    CHECK(fread(rom, 1, n, fp) == n);
    fclose(fp);
    CHECK(rg_world_open(&w, rom, n) == RG_OK && w.prof->game == GP_EMERALD);
    for (i = 0; i < rg_spec_count; i++)
        if (strcmp(rg_specs[i].name, "littleroot_house_w") == 0)
            sp = &rg_specs[i];
    CHECK(sp != NULL);
    CHECK(mkdtemp(dir) != NULL);
    sink = fopen("/dev/null", "w");
    if (sp != NULL && sink != NULL) {
        uint8_t *png, *dec = NULL;
        size_t pn;
        int pw, ph;
        RgPair *pair;
        RgImage art;
        const RgLayout *L = &w.layouts[sp->layoutId - 1];
        char *buf = NULL;
        size_t bsz = 0;
        FILE *mem;

        /* check: the shipped Emerald spec passes, and --expect 1 matches its one placement; a wrong expectation fails */
        CHECK(rg_author_check(&w, sp, 1, sink) == 0);
        CHECK(rg_author_check(&w, sp, 2, sink) == 1);
        mem = open_memstream(&buf, &bsz);
        CHECK(rg_author_check(&w, sp, -1, mem) == 0);
        fclose(mem);
        CHECK(strstr(buf, "round trip: ok") != NULL && strstr(buf, "RESULT PASS") != NULL && strstr(buf, "wrong 0 missing 0 extra 0") != NULL);
        free(buf);
        /* a spec whose layout pin is wrong is refused (fingerprint mismatch), not rendered */
        {
            RgSpec bad = *sp;

            bad.layoutFnv ^= 1u;
            CHECK(rg_author_check(&w, &bad, -1, sink) == 1);
        }
        /* placements */
        mem = open_memstream(&buf, &bsz);
        CHECK(rg_author_placements(&w, sp, mem) == 0);
        fclose(mem);
        CHECK(strstr(buf, "littleroot_house_w: 1 placement(s)") != NULL);
        free(buf);
        /* preview writes the five pictures */
        CHECK(rg_author_preview(&w, dir, sp) == 0);
        {
            static const char *const suffix[5] = {"ortho", "diff", "fl", "fr", "top"};
            unsigned s;

            for (s = 0; s < 5; s++) {
                snprintf(file, sizeof(file), "%s/%s_%s.png", dir, sp->name, suffix[s]);
                png = Slurp(file, &pn);
                CHECK(png != NULL && DecodePng(png, pn, &pw, &ph, &dec) && pw > 8 && ph > 8);
                free(png); free(dec); dec = NULL;
                (void)unlink(file);
            }
        }
        /* art: the _1x picture is exactly what rg_building_art returns, pixel for pixel; the 4x one carries the ruler */
        pair = rg_pair_open(&w, L->pairIndex);
        CHECK(pair != NULL);
        CHECK(rg_building_art(&w, pair, L, 2, 4, 5, 5, NULL, 0, NULL, false, &art));
        CHECK(rg_author_art(&w, dir, sp->layoutId, 2, 4, 5, 5) == 0);
        snprintf(file, sizeof(file), "%s/L%u_2_4_1x.png", dir, sp->layoutId);
        png = Slurp(file, &pn);
        CHECK(png != NULL && DecodePng(png, pn, &pw, &ph, &dec));
        CHECK(pw == art.w && ph == art.h && dec != NULL && memcmp(dec, art.px, (size_t)pw * (size_t)ph * 4u) == 0);
        free(png); free(dec); dec = NULL;
        (void)unlink(file);
        {
            static const char *const suffix[4] = {"4x", "lower", "upper", "coll"};
            unsigned s;

            for (s = 0; s < 4; s++) {
                snprintf(file, sizeof(file), "%s/L%u_2_4_%s.png", dir, sp->layoutId, suffix[s]);
                png = Slurp(file, &pn);
                CHECK(png != NULL && DecodePng(png, pn, &pw, &ph, &dec));
                CHECK(s == 0 || s == 3 ? (pw == art.w * 4 + RULER_L && ph == art.h * 4 + RULER_T) : (pw == art.w && ph == art.h));
                free(png); free(dec); dec = NULL;
                (void)unlink(file);
            }
        }
        rg_img_free(&art);
        rg_pair_close(pair);
        /* bad rects are refused */
        CHECK(rg_author_art(&w, dir, sp->layoutId, 100, 100, 5, 5) == 2);
        CHECK(rg_author_art(&w, dir, 9999, 0, 0, 1, 1) == 2);
        fclose(sink);
    }
    (void)rmdir(dir);
    rg_world_close(&w);
    free(rom);
}

int main(void)
{
    TestPng();
    TestCensus("FireRed", FXR_ENV_FR, 0);
    TestCensus("LeafGreen", FXR_ENV_LG, 1);
    if (sCen[0].row != NULL && sCen[1].row != NULL) {       /* FR and LG are the same census, row for row (one recipe table serves both) */
        unsigned i;

        CHECK(sCen[0].n == sCen[1].n);
        for (i = 0; i < sCen[0].n && i < sCen[1].n; i++)
            CHECK(memcmp(&sCen[0].row[i], &sCen[1].row[i], sizeof(RgCensusRow)) == 0);
    }
    rg_census_free(&sCen[0]);
    rg_census_free(&sCen[1]);
    TestPallet("FireRed", FXR_ENV_FR, GP_FIRERED, 0);
    TestPallet("LeafGreen", FXR_ENV_LG, GP_LEAFGREEN, 1);
    if (sPalletBin[0] != NULL && sPalletBin[1] != NULL) {   /* the pin, and FR and LG produce the identical file */
        char hex[2][41];

        CHECK(sPalletBinSize[0] == sPalletBinSize[1] && memcmp(sPalletBin[0], sPalletBin[1], sPalletBinSize[0]) == 0);
        Sha1(sPalletBin[0], sPalletBinSize[0], hex[0]);
        Sha1(sPalletBin[1], sPalletBinSize[1], hex[1]);
        CHECK(strcmp(hex[0], hex[1]) == 0);
        printf("Kanto buildings.bin sha1 %s\n", hex[0]);
        CHECK(strcmp(hex[0], PALLET_BUILDINGS_SHA1) == 0);
    }
    free(sPalletBin[0]);
    free(sPalletBin[1]);
    {   /* Emerald has no Kanto table */
        unsigned nk = 5;

        CHECK(rg_kspecs_table(gameprof_emerald(), &nk) == NULL && nk == 0);
    }
    TestSideCheck();
    TestCommands();
    printf("test_romgen_frlg_buildings: %d checks, %d failures, %d skipped\n", sChecks, sFails, sSkipped);
    return sFails != 0;
}
