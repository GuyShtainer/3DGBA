// test_romgen_shrubs.c -- look backlog L1: the profile's shrub tables (rg_gameprof.c) against the real ROMs.
// For every entry, on a General-primary outdoor layout of the entry's tileset pair: the id is used and blocked on at least
// half its uses (`romgen author ROM shrubs` lists it as a candidate), its UPPER layer is the bush (at least 60 drawn
// pixels, most of them leaf green) and its LOWER layer is opaque ground. That split is what the renderer relies on: the
// leaves slot keeps only the upper layer, the ground slot only the lower one.
// Emerald from ROMGEN_ROM; FireRed / LeafGreen from ROMGEN_ROM_FR / ROMGEN_ROM_LG or beside ROMGEN_ROM. A missing ROM
// prints SKIP. No game bytes are committed.
//
//   ROMGEN_ROM=$PWD/roms/emerald.gba make -C tools/romgen test T=shrubs
#define RG_AUTHOR_CONSUMER 1
#include "../../tools/romgen/rg_author.c"
#include "../../tools/romgen/rg_author_trees.c"
#include "../../tools/romgen/rg_png.c"

#include "rg_fixture.h"
#include "voxel_atlas.h"

static int sChecks, sFails, sSkipped;
#define CHECK(c) do { ++sChecks; if (!(c)) { ++sFails; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static bool LeafGreen(uint16_t c)
{
    unsigned r, g, b;

    rgb888(c, &r, &g, &b);
    return g >= r + 8u && g >= b + 24u;
}

static void RunShrubs(const uint8_t *rom, size_t n, GpGame game, const char *label)
{
    RgWorld w;
    char *buf = NULL;
    size_t sz = 0;
    FILE *mem;
    unsigned k, byEye = 0;

    CHECK(rg_world_open(&w, rom, n) == RG_OK && w.prof != NULL && w.prof->game == game);
    if (w.prof == NULL || w.prof->game != game)
        return;
    CHECK(w.prof->shrubs != NULL && w.prof->shrubCount > 0 && w.prof->shrubCount <= VOXEL_SHRUBS);
    mem = open_memstream(&buf, &sz);
    CHECK(rg_author_shrubs(&w, mem, NULL, 0) == 0);
    fclose(mem);
    for (k = 0; buf != NULL && k < w.prof->shrubCount; k++) {
        const GpShrub *e = &w.prof->shrubs[k];
        char key[64];
        const char *line;
        unsigned uses = 0, blocked = 0, l, x, y, upN = 0, upGreen = 0, lowDrawn = 0;
        RgPair *p = NULL;
        RgCellPx lo, up;

        /* a candidate of the scan: used, blocked on at least half its uses, mostly leaf green */
        snprintf(key, sizeof key, "ts 0x%08X 0x%03X uses ", (unsigned)e->tileset, (unsigned)e->metatile);
        line = strstr(buf, key);
        if (line != NULL && sscanf(line + strlen(key), "%u blocked %u", &uses, &blocked) == 2) {
            CHECK(uses > 0 && 2u * blocked >= uses);
        } else {
            /* or a member of a bush family picked by eye from the tileset's unfiltered listing: used outdoors */
            char *all = NULL;
            size_t asz = 0;
            FILE *m2 = open_memstream(&all, &asz);
            CHECK(rg_author_shrubs(&w, m2, NULL, e->tileset != 0 ? e->tileset : 1u) == 0);
            fclose(m2);
            line = all != NULL ? strstr(all, key) : NULL;
            CHECK(line != NULL && sscanf(line + strlen(key), "%u blocked %u", &uses, &blocked) == 2 && uses > 0);
            byEye++;
            free(all);
        }
        /* the id's own range: a primary id below nPrim with tileset 0, a secondary one at or past it */
        CHECK(e->tileset == 0 ? e->metatile < w.prof->nPrimMetatiles
                              : e->metatile >= w.prof->nPrimMetatiles && e->metatile < 1024u);
        for (l = 0; l < w.layoutCount && p == NULL; l++) {
            const RgLayout *L = &w.layouts[l];
            if (L->present && L->outdoor && L->ts[0]->addr == w.prof->tsGeneral
                && (e->tileset == 0 || L->ts[1]->addr == e->tileset))
                p = rg_pair_open(&w, L->pairIndex);
        }
        CHECK(p != NULL);
        if (p == NULL)
            continue;
        rg_cell_px(p, e->metatile, 0, &lo);
        rg_cell_px(p, e->metatile, 1, &up);
        rg_pair_close(p);
        for (y = 0; y < 16; y++)
            for (x = 0; x < 16; x++) {
                unsigned u = (up.drawn[y] >> x) & 1u;
                upN += u;
                upGreen += u && LeafGreen(up.c[y][x]);
                lowDrawn += (lo.drawn[y] >> x) & 1u;
            }
        printf("  %s shrub %u ts 0x%08X 0x%03X uses %u blocked %u upper %u green %u lower %u\n",
               label, k, (unsigned)e->tileset, (unsigned)e->metatile, uses, blocked, upN, upGreen, lowDrawn);
        CHECK(upN >= 60u && 2u * upGreen >= upN);     /* the bush is on the upper layer */
        CHECK(lowDrawn == 256u);                      /* the ground under it is opaque */
    }
    /* Emerald's Dewford family: 0x239 (the bush over the sand, walkable) and 0x23A (37 % leaf green over the whole cell, the scan wants 50)
     * do not pass the scan's filter and were picked by eye; every other entry does. */
    printf("  %s: %u entries picked by eye\n", label, byEye);
    CHECK(byEye == (game == GP_EMERALD ? 2u : 0u));
    free(buf);
    rg_world_close(&w);
}

int main(void)
{
    static const struct { const char *env; GpGame game; const char *name; } kGames[3] = {
        {"ROMGEN_ROM", GP_EMERALD, "Emerald"}, {FXR_ENV_FR, GP_FIRERED, "FireRed"}, {FXR_ENV_LG, GP_LEAFGREEN, "LeafGreen"}};
    int i;

    for (i = 0; i < 3; i++) {
        size_t n = 0;
        uint8_t *rom = NULL;

        if (i == 0) {
            const char *path = getenv("ROMGEN_ROM");
            FILE *fp = path != NULL ? fopen(path, "rb") : NULL;
            if (fp != NULL) {
                rom = (uint8_t *)malloc(0x2000000);
                n = rom != NULL ? fread(rom, 1, 0x2000000, fp) : 0;
                fclose(fp);
            }
        } else {
            rom = fxr_load_rom(kGames[i].env, &n);
        }
        if (rom == NULL || n == 0) {
            printf("SKIP %s: set %s\n", kGames[i].name, kGames[i].env);
            sSkipped++;
            free(rom);
            continue;
        }
        RunShrubs(rom, n, kGames[i].game, kGames[i].name);
        free(rom);
    }
    printf("test_romgen_shrubs: %d checks, %d failures, %d skipped\n", sChecks, sFails, sSkipped);
    return sFails != 0;
}
