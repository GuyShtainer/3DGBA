// test_romgen_frlg_regions.c -- Phase 34 slice G2: FireRed / LeafGreen art, regions.bin and signposts.bin.
// Real-ROM suite on BOTH carts (SPEC-P34 section 4 "G2"): every Pallet cell's palette comes from the right tileset
// (0-6 primary, 7-12 secondary, re-derived here independently of rg_art.c), the Pallet house door metatile draws and its
// pixel SHA-1 is pinned, Pallet's regions (pond water, the three doors make house roles, bytes pinned after a visual check),
// Route 24's bridge deck is not water, signposts (Pallet has exactly 5 cut-outs at the census cells, the Kanto-wide count
// is pinned), and both files round-trip through the vendored consumers VoxelRegions_Init / VoxelSign_Init. FR and LG files
// are byte-identical. ROMs: ROMGEN_ROM_FR / ROMGEN_ROM_LG, else firered.gba / leafgreen.gba beside ROMGEN_ROM (rg_fixture.h).
//
//   ROMGEN_ROM=$PWD/roms/emerald.gba make -C tools/romgen test T=frlg_regions
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rg_bimg.h"
#include "rg_fixture.h"
#include "rg_regions.h"
#include "rg_roles.h"
#include "rg_run.h"
#include "rg_signs.h"
#include "voxel_regions.h"
#include "voxel_sign.h"
#include "voxel_world.h"
#include "vx_lz77.h"

static int sChecks, sFails, sSkipped;
#define CHECK(c) do { ++sChecks; if (!(c)) { ++sFails; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

/* ---- pins (measured on FR rev 1; LG is byte-identical) ---- */
#define PALLET_LAYOUT 78u
#define ROUTE24_LAYOUT 112u
#define PALLET_ROLES_SHA1 "f438cc30f0033f119e7bc4731722794311ba2739"
#define ROUTE24_ROLES_SHA1 "2d5f4d6cefd72d858956364f23e2ade5c7ca7229"
#define DOOR_IMAGE_SHA1 "f0e58e8686e7e54af622e5bfe3bb38953ed16430"
#define REGIONS_SHA1 "3716874d6ba477acbdaaecc079fd7dc77526cd1b"
#define SIGNPOSTS_SHA1 "ba2fde451aa9612c10f7a7a80cf0d9b06f007e06"
#define KANTO_SIGN_RECORDS 144u        /* outdoor 0x84 cells with a walkable cell south (147 outdoor 0x84 cells, 3 have none) */
#define KANTO_OUTDOOR_SIGN_EVENTS 151u
#define KANTO_SIGN_EVENTS_ON_RECORD 135u   /* events that land on a record; 138 events are on 0x84, 3 of them have no south cell */

/* ---- SHA-1 (same helper as test_romgen_frlg_buildings.c) ---- */
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

/* a pin is checked unless it is still the @placeholder@ (first run prints the measured value) */
static void CheckPin(const char *what, const char *pin, const char *got)
{
    printf("pin %s = %s\n", what, got);
    if (pin[0] == '@') { ++sChecks; ++sFails; printf("FAIL unpinned %s\n", what); return; }
    CHECK(strcmp(pin, got) == 0);
}

/* ---- the consumer side (temp dir holding voxel/regions.bin and voxel/signposts.bin) ---- */
static char sDir[64], sCwd[1024];

static void WriteFile(const char *path, const uint8_t *b, size_t n)
{
    FILE *fp = fopen(path, "wb");
    if (!fp || fwrite(b, 1, n, fp) != n) abort();
    fclose(fp);
}

static void EnterTemp(void)
{
    char sub[96];

    strcpy(sDir, "/tmp/rgfrr.XXXXXX");
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
    snprintf(p, sizeof(p), "%s/voxel/regions.bin", sDir); (void)unlink(p);
    snprintf(p, sizeof(p), "%s/voxel/signposts.bin", sDir); (void)unlink(p);
    snprintf(p, sizeof(p), "%s/voxel", sDir); (void)rmdir(p);
    (void)rmdir(sDir);
}

/* ---- one cart ---- */
typedef struct {
    const char *name, *env;
    GpGame game;
    uint8_t *rom;
    size_t n;
    RgOutput out;
    RgWorld w;
    char regionsSha[41], signsSha[41], palletSha[41], route24Sha[41], doorSha[41];
} Cart;

static Cart sCart[2] = {{.name = "FireRed", .env = FXR_ENV_FR, .game = GP_FIRERED},
                        {.name = "LeafGreen", .env = FXR_ENV_LG, .game = GP_LEAFGREEN}};

/* the regions slab of one layout inside the file */
static const uint8_t *Slab(const RgOutput *o, unsigned id, unsigned *cells)
{
    unsigned count = (unsigned)(o->regions[4] | (o->regions[5] << 8)), i;

    for (i = 0; i < count; i++) {
        const uint8_t *row = o->regions + 8 + 12u * i;

        if ((unsigned)(row[0] | (row[1] << 8)) == id) {
            *cells = (unsigned)(row[2] | (row[3] << 8)) * (unsigned)(row[4] | (row[5] << 8));
            return o->regions + rg_rd32(row + 8);
        }
    }
    return NULL;
}

/* Re-derives a metatile layer's colours straight from the ROM tables: palette pal < 7 from the PRIMARY tileset's palette
 * block, pal >= 7 from the SECONDARY's; tile < 640 from the primary's tiles, else the secondary's. Returns the number of
 * pixels it compared; *wrong counts pixels where the other tileset's palette would have given a different colour. */
static unsigned CheckLayerPalettes(const RgWorld *w, const RgLayout *L, RgPair *pair, uint16_t metatile, int layer,
                                   uint8_t *tiles[2], unsigned *wrong, unsigned *secondaryPx)
{
    uint16_t ent[8];
    const RgLayer *lay = rg_layer(pair, metatile, layer);
    unsigned q, x, y, compared = 0;

    (void)w;
    if (!rg_metatile_entries(L, metatile, ent))
        return 0;
    for (q = 0; q < 4; q++) {
        uint16_t e = ent[layer * 4 + (int)q];
        unsigned tile = e & 0x3FFu, pal = e >> 12, tw = tile < 640u ? 0u : 1u, local = tile - tw * 640u;
        const RgTileset *own = L->ts[pal < 7u ? 0 : 1], *other = L->ts[pal < 7u ? 1 : 0];
        unsigned ox = (q & 1u) * 8u, oy = (q >> 1) * 8u;

        if (tiles[tw] == NULL || local * 32u + 32u > L->ts[tw]->tilesBytes)
            continue;
        for (y = 0; y < 8; y++) {
            for (x = 0; x < 8; x++) {
                unsigned sx = (e & 0x400u) ? 7u - x : x, sy = (e & 0x800u) ? 7u - y : y;
                uint8_t packed = tiles[tw][local * 32u + sy * 4u + sx / 2u];
                unsigned idx = (sx & 1u) ? (unsigned)(packed >> 4) : (unsigned)(packed & 0xFu);
                uint16_t want, alt;

                if (idx == 0) {
                    CHECK(!rg_layer_has(lay, (int)(ox + x), (int)(oy + y)));
                    continue;
                }
                want = (uint16_t)(rg_rd16(own->palettes + 32u * pal + 2u * idx) & 0x7FFFu);
                alt = (uint16_t)(rg_rd16(other->palettes + 32u * pal + 2u * idx) & 0x7FFFu);
                CHECK(rg_layer_has(lay, (int)(ox + x), (int)(oy + y)));
                CHECK(lay->c[oy + y][ox + x] == want);
                compared++;
                if (pal >= 7u) *secondaryPx += 1u;
                if (alt != want) ++*wrong;
            }
        }
    }
    return compared;
}

static void TestPalettes(Cart *c)
{
    const RgLayout *L = &c->w.layouts[PALLET_LAYOUT - 1];
    RgPair *pair = rg_pair_open(&c->w, L->pairIndex);
    uint8_t *tiles[2] = {NULL, NULL};
    uint8_t seen[1024];
    unsigned k, x, y, metatiles = 0, compared = 0, wrong = 0, secondaryPx = 0;

    CHECK(pair != NULL);
    CHECK(L->ts[0] != NULL && L->ts[1] != NULL && L->ts[0]->addr == (c->game == GP_FIRERED ? 0x082D4B04u : 0x082D4AE4u));
    for (k = 0; k < 2; k++) {
        const RgTileset *t = L->ts[k];

        tiles[k] = (uint8_t *)malloc(t->tilesBytes);
        if (t->compressed)
            CHECK(vx_lz77_decode(t->tilesRom, c->w.romSize - (size_t)(t->tilesRom - c->w.rom), tiles[k], t->tilesBytes) == t->tilesBytes);
        else
            memcpy(tiles[k], t->tilesRom, t->tilesBytes);
    }
    memset(seen, 0, sizeof seen);
    for (y = 0; y < L->h; y++) {
        for (x = 0; x < L->w; x++) {
            uint16_t m = rg_metatile(L, (int)x, (int)y);

            if (m >= 1024u || seen[m])
                continue;
            seen[m] = 1;
            metatiles++;
            compared += CheckLayerPalettes(&c->w, L, pair, m, 0, tiles, &wrong, &secondaryPx);
            compared += CheckLayerPalettes(&c->w, L, pair, m, 1, tiles, &wrong, &secondaryPx);
        }
    }
    printf("%s Pallet palettes: %u distinct metatiles, %u pixels compared, %u from palettes 7-12, %u would differ from the wrong tileset\n",
           c->name, metatiles, compared, secondaryPx, wrong);
    CHECK(metatiles > 20 && compared > 1000);
    CHECK(secondaryPx > 0 && wrong > 0);   /* the test can fail: the secondary palettes are used, and the wrong source would show */
    free(tiles[0]);
    free(tiles[1]);
    rg_pair_close(pair);
}

static void TestDoorImage(Cart *c)
{
    const RgLayout *L = &c->w.layouts[PALLET_LAYOUT - 1];
    RgPair *pair = rg_pair_open(&c->w, L->pairIndex);
    RgImage im, im2;
    unsigned i, lit = 0;
    uint16_t door = rg_metatile(L, 6, 7);

    CHECK(door == 0x2A3 && rg_metatile(L, 15, 7) == 0x2A3 && rg_metatile(L, 16, 13) == 0x2AC);
    CHECK(pair != NULL && rg_cell_image(pair, door, &im) && im.w == 16 && im.h == 16);
    for (i = 0; i < 256; i++)
        lit += im.px[4 * i] || im.px[4 * i + 1] || im.px[4 * i + 2];
    CHECK(lit > 100);   /* a drawn door, not a black cell */
    CHECK(rg_cell_image(pair, 0x2AC, &im2) && !rg_img_equal(&im, &im2));   /* the lab door differs from the house door */
    Sha1(im.px, 16u * 16u * 4u, c->doorSha);
    rg_img_free(&im);
    rg_img_free(&im2);
    rg_pair_close(pair);
}

/* Role string of the whole of layout `id`, for the dump a human can read. */
static void DumpSlab(const uint8_t *s, unsigned w, unsigned h)
{
    static const char kLetters[11] = {'.', '~', '_', '=', 'W', 'T', 'o', '%', '|', '#', 'S'};
    unsigned x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) putchar(kLetters[s[y * w + x] % 11u]);
        putchar('\n');
    }
}

static void TestPallet(Cart *c)
{
    const RgLayout *L = &c->w.layouts[PALLET_LAYOUT - 1];
    unsigned cells = 0, x, y, hist[11] = {0};
    const uint8_t *s = Slab(&c->out, PALLET_LAYOUT, &cells);
    static const int sign[5][2] = {{4, 7}, {13, 7}, {9, 11}, {5, 14}, {16, 16}};   /* the census cells: 2 mailboxes, 3 fence/post signs */
    static const int door[3][2] = {{6, 7}, {15, 7}, {16, 13}};
    unsigned i, records = 0, pond = 0;

    CHECK(s != NULL && L->w == 24 && L->h == 20 && cells == 480);
    if (s == NULL) return;
    for (y = 0; y < L->h; y++) for (x = 0; x < L->w; x++) hist[s[y * L->w + x] % 11u]++;
    /* the pond: every cell of the water behaviour (0x15) is water, nothing else is */
    for (y = 0; y < L->h; y++)
        for (x = 0; x < L->w; x++) {
            unsigned b = rg_behaviour(L, (int)x, (int)y);

            CHECK((s[y * L->w + x] == VOXEL_ROLE_WATER) == (b == 0x15));
            pond += b == 0x15;
        }
    CHECK(pond == 12);   /* the 4x3 pond south-west of Oak's lab */
    CHECK(s[17 * L->w + 7] == VOXEL_ROLE_WATER && s[19 * L->w + 10] == VOXEL_ROLE_WATER);
    /* the three doors make house roles (the doors are collision-blocked: SPEC section 2) */
    for (i = 0; i < 3; i++) {
        CHECK(rg_blocked(L, door[i][0], door[i][1]) && rg_behaviour(L, door[i][0], door[i][1]) == 0x69);
        CHECK(s[door[i][1] * L->w + door[i][0]] == VOXEL_ROLE_WALL);
        CHECK(s[(door[i][1] - 1) * L->w + door[i][0]] == VOXEL_ROLE_WALL);   /* and the mass above it */
    }
    /* five signs exactly, at the census cells */
    for (y = 0; y < L->h; y++) for (x = 0; x < L->w; x++) records += s[y * L->w + x] == VOXEL_ROLE_SIGNPOST;
    CHECK(records == 5);
    for (i = 0; i < 5; i++) CHECK(s[sign[i][1] * L->w + sign[i][0]] == VOXEL_ROLE_SIGNPOST && rg_has_sign(L, sign[i][0], sign[i][1]));
    CHECK(hist[VOXEL_ROLE_WALL] == 68 && hist[VOXEL_ROLE_WATER] == 12 && hist[VOXEL_ROLE_SIGNPOST] == 5);
    Sha1(s, cells, c->palletSha);
    printf("%s Pallet roles: floor %u water %u ledge %u stair %u wall %u tree %u prop %u shelf %u fence %u cliff %u sign %u\n", c->name,
           hist[0], hist[1], hist[2], hist[3], hist[4], hist[5], hist[6], hist[7], hist[8], hist[9], hist[10]);
    if (getenv("G2_DUMP")) DumpSlab(s, L->w, L->h);
}

static void TestRoute24(Cart *c)
{
    const RgLayout *L = &c->w.layouts[ROUTE24_LAYOUT - 1];
    const RgMap *m = rg_world_map(&c->w, 3, 43);
    unsigned cells = 0, x, y, deck = 0, water = 0, walkOnWater = 0;
    const uint8_t *s = Slab(&c->out, ROUTE24_LAYOUT, &cells);

    CHECK(m != NULL && m->layoutId == ROUTE24_LAYOUT && L->w == 24 && L->h == 40 && cells == 960);
    if (s == NULL) return;
    for (y = 0; y < L->h; y++)
        for (x = 0; x < L->w; x++) {
            unsigned b = rg_behaviour(L, (int)x, (int)y), role = s[y * L->w + x];

            CHECK((role == VOXEL_ROLE_WATER) == (b == 0x15));   /* water is the sea behaviour and nothing else: no bridge/log behaviour in Kanto */
            water += role == VOXEL_ROLE_WATER;
            /* the Nugget Bridge deck (x 10-12, y 17-39): the entry row y 17 is behaviour 0x2A (rock stairs, never water in FRLG), the
             * deck proper y 18-39 is plain behaviour 0 over the sea, between rails; none of it is water */
            if (x >= 10 && x <= 12 && y >= 17) {
                CHECK(!rg_blocked(L, (int)x, (int)y) && role != VOXEL_ROLE_WATER);
                CHECK(y == 17 ? b == 0x2A : b == 0);
                deck++;
            }
            if (role == VOXEL_ROLE_WATER && !rg_blocked(L, (int)x, (int)y)) walkOnWater++;
        }
    CHECK(deck == 3 * 23);
    CHECK(water > 100 && walkOnWater == water);   /* sea is collision-walkable (surf) and is water */
    Sha1(s, cells, c->route24Sha);
}

/* Every layout of the cart: water role iff a water behaviour; ledge role iff a jump behaviour (nothing else is a role of the behaviour sets). */
static void TestBehaviourRoles(Cart *c)
{
    const GameProfile *gp = c->w.prof;
    unsigned li, cellsChecked = 0, lastSigns = 0;

    for (li = 0; li < c->w.layoutCount; li++) {
        const RgLayout *L = &c->w.layouts[li];
        unsigned cells = 0, x, y;
        const uint8_t *s;

        if (!L->present) continue;
        s = Slab(&c->out, L->id, &cells);
        CHECK(s != NULL && cells == (unsigned)L->w * L->h);
        if (s == NULL) continue;
        for (y = 0; y < L->h; y++)
            for (x = 0; x < L->w; x++) {
                unsigned b = rg_behaviour(L, (int)x, (int)y), role = s[y * L->w + x];

                CHECK(role < 11);
                CHECK((role == VOXEL_ROLE_WATER) == gp_beh(&gp->water, b));
                if (role == VOXEL_ROLE_LEDGE) CHECK(gp_beh(&gp->jump, b));
                if (role == VOXEL_ROLE_SIGNPOST) { CHECK(L->outdoor && gp_beh(&gp->signpost, b) && rg_blocked(L, (int)x, (int)y)); lastSigns++; }
                cellsChecked++;
            }
    }
    CHECK(cellsChecked > 200000);
    CHECK(lastSigns == KANTO_SIGN_RECORDS);
}

/* Signs: records, census agreement, and the round trip through the vendored consumers. */
static void TestSignsAndRoundTrip(Cart *c)
{
    const RgOutput *o = &c->out;
    unsigned li, i, k, onRecord = 0, outdoorEvents = 0, events84 = 0, pallet = 0, any = 0;
    unsigned count = o->signs != NULL ? (unsigned)(o->signs[4] | (o->signs[5] << 8)) : 0;
    unsigned nCells = 0;

    CHECK(o->signs != NULL && o->signsSize == 8u + 72u * count && memcmp(o->signs, "VXS2", 4) == 0);
    CHECK(count == KANTO_SIGN_RECORDS && o->signCount == KANTO_SIGN_RECORDS);
    for (li = 0; li < c->w.layoutCount; li++) {
        const RgLayout *L = &c->w.layouts[li];
        unsigned cells = 0;
        const uint8_t *s;

        if (!L->present || !L->outdoor) continue;
        s = Slab(o, L->id, &cells);
        for (i = 0; s != NULL && i < L->signCount; i++) {
            int x = L->signs[i].x, y = L->signs[i].y;

            if (rg_off(L, x, y)) continue;
            outdoorEvents++;
            events84 += rg_behaviour(L, x, y) == 0x84;
            onRecord += s[y * L->w + x] == VOXEL_ROLE_SIGNPOST;
        }
    }
    printf("%s signs: %u records, outdoor sign events %u (%u on 0x84), %u land on a record (rate %.1f %%)\n", c->name, count,
           outdoorEvents, events84, onRecord, outdoorEvents ? 100.0 * onRecord / outdoorEvents : 0.0);
    CHECK(outdoorEvents == KANTO_OUTDOOR_SIGN_EVENTS && onRecord == KANTO_SIGN_EVENTS_ON_RECORD);
    /* records: sorted, Pallet has 5, the cut-out masks are not empty */
    for (i = 0; i < count; i++) {
        const uint8_t *rec = o->signs + 8 + 72u * i;
        unsigned layout = (unsigned)(rec[0] | (rec[1] << 8));

        any = 0;
        for (k = 0; k < 16; k++) any |= (unsigned)(rec[6 + 2 * k] | (rec[7 + 2 * k] << 8));
        CHECK(any != 0);
        if (layout == PALLET_LAYOUT) pallet++;
    }
    CHECK(pallet == 5);
    CHECK(o->emptyMasks == 0);

    EnterTemp();
    WriteFile("voxel/regions.bin", o->regions, o->regionsSize);
    WriteFile("voxel/signposts.bin", o->signs, o->signsSize);
    CHECK(VoxelRegions_Init());
    VoxelSign_Init();
    for (li = 0; li < c->w.layoutCount; li++) {
        const RgLayout *L = &c->w.layouts[li];
        unsigned cells = 0, x, y;
        const uint8_t *s;

        if (!L->present) continue;
        s = Slab(o, L->id, &cells);
        for (y = 0; s != NULL && y < L->h; y++)
            for (x = 0; x < L->w; x++) {
                VoxelMapInstance inst;
                unsigned role = VoxelRegions_RoleAt(L->id, (int)x, (int)y);

                CHECK(role == s[y * L->w + x]);
                nCells++;
                memset(&inst, 0, sizeof(inst));
                inst.layoutId = L->id;
                if (role == VOXEL_ROLE_SIGNPOST) CHECK(VoxelSign_IsCell(&inst, (int)x, (int)y));
            }
    }
    for (i = 0; i < count; i++) {
        const uint8_t *rec = o->signs + 8 + 72u * i;
        VoxelMapInstance inst;

        memset(&inst, 0, sizeof(inst));
        inst.layoutId = (uint16_t)(rec[0] | (rec[1] << 8));
        CHECK(VoxelSign_IsCell(&inst, rec[2] | (rec[3] << 8), rec[4] | (rec[5] << 8)));
        CHECK(VoxelRegions_RoleAt(inst.layoutId, rec[2] | (rec[3] << 8), rec[4] | (rec[5] << 8)) == VOXEL_ROLE_SIGNPOST);
    }
    CHECK(nCells == 246995u - 8u - 12u * 366u);
    VoxelSign_Shutdown();
    VoxelRegions_Shutdown();
    LeaveTemp();
}

static void RunCart(Cart *c)
{
    RgRunOpts opts;

    c->rom = fxr_load_rom(c->env, &c->n);
    if (c->rom == NULL) {
        printf("SKIP %s: ROM not found (set ROMGEN_ROM_%s or ROMGEN_ROM)\n", c->name, c->game == GP_FIRERED ? "FR" : "LG");
        sSkipped++;
        return;
    }
    CHECK(rg_world_open(&c->w, c->rom, c->n) == RG_OK && c->w.prof->game == c->game);
    memset(&opts, 0, sizeof(opts));
    opts.wantSigns = true;
    opts.relief = RG_RELIEF_FULL;   /* L1: on FRLG FULL is built as LEDGES (see test_romgen_frlg_relief) */
    CHECK(rg_run(c->rom, c->n, &opts, &c->out) == RG_OK);
    CHECK(c->out.regions != NULL && c->out.regionsSize == 246995u && c->out.relief != NULL && c->out.reliefSize == 26320u);
    CHECK(c->out.layouts == 384 && memcmp(c->out.regions, "VXR5", 4) == 0);
    CHECK(c->out.regions[4] == (366 & 0xFF) && c->out.regions[5] == (366 >> 8));   /* 384 slots - 18 NULL */
    Sha1(c->out.regions, c->out.regionsSize, c->regionsSha);
    if (c->out.signs != NULL) Sha1(c->out.signs, c->out.signsSize, c->signsSha);
    TestPalettes(c);
    TestDoorImage(c);
    TestPallet(c);
    TestRoute24(c);
    TestBehaviourRoles(c);
    TestSignsAndRoundTrip(c);
}

int main(void)
{
    unsigned k;

    for (k = 0; k < 2; k++) RunCart(&sCart[k]);
    if (sCart[0].rom != NULL) {
        CheckPin("Pallet roles", PALLET_ROLES_SHA1, sCart[0].palletSha);
        CheckPin("Route 24 roles", ROUTE24_ROLES_SHA1, sCart[0].route24Sha);
        CheckPin("Pallet house door image", DOOR_IMAGE_SHA1, sCart[0].doorSha);
        CheckPin("regions.bin", REGIONS_SHA1, sCart[0].regionsSha);
        CheckPin("signposts.bin", SIGNPOSTS_SHA1, sCart[0].signsSha);
    }
    if (sCart[0].rom != NULL && sCart[1].rom != NULL) {   /* one blockdata, one set of roles: the two carts give identical files */
        CHECK(sCart[0].out.regionsSize == sCart[1].out.regionsSize && memcmp(sCart[0].out.regions, sCart[1].out.regions, sCart[0].out.regionsSize) == 0);
        CHECK(sCart[0].out.signsSize == sCart[1].out.signsSize && memcmp(sCart[0].out.signs, sCart[1].out.signs, sCart[0].out.signsSize) == 0);
        CHECK(strcmp(sCart[0].doorSha, sCart[1].doorSha) == 0);
    }
    for (k = 0; k < 2; k++) {
        if (sCart[k].rom == NULL) continue;
        rg_output_free(&sCart[k].out);
        rg_world_close(&sCart[k].w);
        free(sCart[k].rom);
    }
    printf("test_romgen_frlg_regions: %d checks, %d failures, %d skipped\n", sChecks, sFails, sSkipped);
    return sFails != 0;
}
