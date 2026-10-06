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
    unsigned cnt[256], i, towns = 0, others = 0, ms;

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
    CHECK(c->covered == 3);                                              /* K1: the two Pallet recipes cover the three Pallet placements */
    memset(cnt, 0, sizeof(cnt));
    for (i = 0; i < c->n; i++) {
        const RgCensusRow *r = &c->row[i];
        const RgMap *dm = rg_world_map(&w, r->destGroup, r->destNum);

        cnt[r->mapsec]++;
        CHECK(dm != NULL && dm->mapType == 8);                          /* every destination is an indoor map */
        CHECK(r->nDoors >= 1 && (r->mapsec == 0x58 ? r->model >= 0 : r->model == -1));   /* only Pallet Town is authored so far */
        CHECK(r->rect[2] >= 1 && r->rect[3] >= 1 && r->rect[0] >= 0 && r->rect[1] >= 0);
        CHECK(r->rect[0] + r->rect[2] <= (int)w.layouts[r->layout - 1].w && r->rect[1] + r->rect[3] <= (int)w.layouts[r->layout - 1].h);
        CHECK(r->sevii == rg_author_mapsec_is_sevii(r->mapsec));
    }
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
        CHECK(buf != NULL && strstr(buf, "covered 3 / 152\n") != NULL && strstr(buf, "152 placements (108 mainland, 44 sevii)") != NULL);
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

/* The FR buildings.bin of the two Pallet models (K1). Re-pinned by the lead after the visual check, if the look changes. */
#define PALLET_BUILDINGS_SHA1 "66b63ede1e7eb54828654bf4a86f7a899b6c1aa7"

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
    CHECK(k != NULL && nk == 2);                                          /* K1: the two Pallet Town recipes */
    if (k != NULL && nk == 2) {
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
    }
    rg_world_close(&w);
    memset(&opts, 0, sizeof(opts));
    opts.wantBuildings = true;
    CHECK(rg_run(rom, n, &opts, &out) == RG_OK);
    CHECK(out.buildings != NULL && out.buildingsSize > 24 && memcmp(out.buildings, "VXB7", 4) == 0);
    CHECK(out.bModels == 2 && out.bPlacements == 3 && out.buildingsFailed == 0);
    CHECK(out.regions == NULL && out.signs == NULL && out.relief == NULL);   /* regions, signposts, relief stay off for FRLG */
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
        inst.layoutId = 79;                                                /* any other layout places nothing */
        CHECK(VoxelBuildings_PageOf(&inst) < 0);
        VoxelBuildings_Shutdown();
        LeaveTemp();
    }
    rg_output_free(&out);
    free(rom);
    printf("%s: Pallet Town -> 2 models, 3 placements, buildings.bin %zu bytes\n", name, sPalletBinSize[idx]);
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
        printf("Pallet buildings.bin sha1 %s\n", hex[0]);
        CHECK(strcmp(hex[0], PALLET_BUILDINGS_SHA1) == 0);
    }
    free(sPalletBin[0]);
    free(sPalletBin[1]);
    {   /* Emerald has no Kanto table */
        unsigned nk = 5;

        CHECK(rg_kspecs_table(gameprof_emerald(), &nk) == NULL && nk == 0);
    }
    TestCommands();
    printf("test_romgen_frlg_buildings: %d checks, %d failures, %d skipped\n", sChecks, sFails, sSkipped);
    return sFails != 0;
}
