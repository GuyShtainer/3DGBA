// test_romgen_rs_foliage.c -- Phase 35 S3: the Ruby / Sapphire rev 2 foliage tables (rg_gameprof.c) against the real carts.
//  * trees: every id of the profile's treePart is foliage on collision-blocked cells only (`romgen author ROM trees`);
//    no other foliage metatile with 100 or more blocked uses is left out unless a table owns it (treeGround);
//    every treeGround pair keeps the cell's ground: the lower layer of the fringe metatile equals its replacement's
//    (0x040 is the one measured exception, a fence foot with 70 px of its own);
//  * shrubs: every bush and prop entry sits on the layer split the renderer relies on (bush / fence / rock / flower on the
//    UPPER layer, opaque ground on the LOWER one), used outdoors, blocked as the kind demands;
//  * grass: the behaviour-2 metatiles are the lower-layer blade tile 0x00D on the General tileset (upper layer empty),
//    the two canopy-fringe ids are tree-owned, the plain-grass ground metatile is opaque, and the one skipped tileset id
//    is the sandy one;
//  * Sapphire's table equals Ruby's with every secondary tileset address 0x70 lower.
// Real ROMs come from ROMGEN_ROM_RUBY / ROMGEN_ROM_SAPP (absolute paths) or ruby.gba / sapphire.gba beside ROMGEN_ROM; a
// missing ROM prints SKIP. No game bytes are committed.
//
//   ROMGEN_ROM=$PWD/roms/emerald.gba make -C tools/romgen test T=rs_foliage
#define RG_AUTHOR_CONSUMER 1
#include "../../tools/romgen/rg_author.c"
#include "../../tools/romgen/rg_budget.c"
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

static bool InTable(const int16_t *t, unsigned n, unsigned m)
{
    unsigned k;

    for (k = 0; k < n; k++)
        if ((unsigned)t[2 * k] == m)
            return true;
    return false;
}

static unsigned LowerDiff(RgPair *p, unsigned a, unsigned b)
{
    RgCellPx x, y;
    unsigned r, c, d = 0;

    rg_cell_px(p, a, 0, &x);
    rg_cell_px(p, b, 0, &y);
    for (r = 0; r < 16; r++)
        for (c = 0; c < 16; c++)
            d += x.c[r][c] != y.c[r][c];
    return d;
}

static RgPair *GeneralPair(const RgWorld *w, uint32_t secondary)
{
    unsigned l;

    for (l = 0; l < w->layoutCount; l++) {
        const RgLayout *L = &w->layouts[l];

        if (L->present && L->outdoor && L->ts[0]->addr == w->prof->tsGeneral && (secondary == 0 || L->ts[1]->addr == secondary))
            return rg_pair_open(w, L->pairIndex);
    }
    return NULL;
}

static void RunTrees(const RgWorld *w, const char *label)
{
    const GameProfile *gp = w->prof;
    char *buf = NULL;
    size_t sz = 0;
    FILE *mem = open_memstream(&buf, &sz);
    unsigned id, nTable = 0, nBlocked = 0;
    const char *p;
    RgPair *pair;

    CHECK(rg_author_trees_list(w, mem) == 0);
    fclose(mem);
    CHECK(buf != NULL && strstr(buf, "# trees: ") == buf);
    CHECK(gp->treePart != NULL && gp->treePartCount == 24 && gp->treeGround != NULL && gp->treeGroundCount == 13);
    for (id = 0; id < gp->treePartCount; id++) {
        char key[32];
        unsigned uses = 0, blocked = 0;
        int z, g;
        const char *line;

        snprintf(key, sizeof key, "meta 0x%03X uses ", (unsigned)gp->treePart[2 * id]);
        line = strstr(buf, key);
        CHECK(line != NULL);
        CHECK(gp->treePart[2 * id + 1] >= 0 && gp->treePart[2 * id + 1] <= 4 && gp->treePart[2 * id] < (int)gp->nPrimMetatiles);
        if (line != NULL && sscanf(line + strlen(key), "%u blocked %u z=%d g=%d", &uses, &blocked, &z, &g) == 4) {
            CHECK(g == 1 && blocked == uses);            /* foliage, and only ever on blocked cells */
            nTable++;
            nBlocked += blocked;
        }
    }
    CHECK(nTable == 24);
    /* any other metatile that is foliage on at least 100 blocked cells must be owned by a table (a fringe) */
    for (p = buf; (p = strstr(p, "\nmeta 0x")) != NULL; p++) {
        unsigned m, uses, blocked;
        int z, g;

        if (sscanf(p + 1, "meta 0x%x uses %u blocked %u z=%d g=%d", &m, &uses, &blocked, &z, &g) != 5)
            continue;
        if (g == 1 && blocked >= 100u && !InTable(gp->treePart, gp->treePartCount, m))
            CHECK(InTable(gp->treeGround, gp->treeGroundCount, m));
    }
    /* each fringe keeps its cell's ground: lower layer equal to the replacement's, except the 0x040 fence foot */
    pair = GeneralPair(w, 0);
    CHECK(pair != NULL);
    if (pair != NULL) {
        for (id = 0; id < gp->treeGroundCount; id++) {
            unsigned a = (unsigned)gp->treeGround[2 * id], b = (unsigned)gp->treeGround[2 * id + 1];
            unsigned d = LowerDiff(pair, a, b);

            CHECK(a < gp->nPrimMetatiles && b < gp->nPrimMetatiles);
            CHECK(a == 0x040u ? d == 70u : d == 0u);
        }
        rg_pair_close(pair);
    }
    printf("  %s: tree list %zu bytes, %u table ids, %u blocked uses of them\n", label, sz, nTable, nBlocked);
    free(buf);
}

static unsigned RunShrubs(const RgWorld *w, const char *label, unsigned *nBush, unsigned *nProp)
{
    const GameProfile *gp = w->prof;
    char *buf = NULL, *pbuf = NULL;
    size_t sz = 0, psz = 0;
    FILE *mem, *pm;
    unsigned k, byEye = 0, fences = 0, rocks = 0, flowers = 0;

    *nBush = *nProp = 0;
    CHECK(gp->shrubs != NULL && gp->shrubCount == 16 && gp->shrubCount <= VOXEL_SHRUBS);
    mem = open_memstream(&buf, &sz);
    CHECK(rg_author_shrubs(w, mem, NULL, 0) == 0);
    fclose(mem);
    pm = open_memstream(&pbuf, &psz);
    CHECK(rg_author_props(w, pm, NULL, 1) == 0);
    fclose(pm);
    for (k = 0; buf != NULL && pbuf != NULL && k < gp->shrubCount; k++) {
        const GpShrub *e = &gp->shrubs[k];
        char key[64];
        const char *line;
        unsigned uses = 0, blocked = 0, upper = 0, beh, x, y, upN = 0, upGreen = 0, lowDrawn = 0, kept = 0, keyed = 0;
        RgPair *p;
        RgCellPx lo, up;

        CHECK(e->tileset == 0 ? e->metatile < gp->nPrimMetatiles : e->metatile >= gp->nPrimMetatiles && e->metatile < 1024u);
        if (e->kind == GP_PROP_BUSH) {
            ++*nBush;
            snprintf(key, sizeof key, "ts 0x%08X 0x%03X uses ", (unsigned)e->tileset, (unsigned)e->metatile);
            line = strstr(buf, key);
            if (line != NULL && sscanf(line + strlen(key), "%u blocked %u", &uses, &blocked) == 2) {
                CHECK(uses > 0 && 2u * blocked >= uses);
            } else {
                /* a member of a bush family picked by eye from the tileset's unfiltered listing */
                char *all = NULL;
                size_t asz = 0;
                FILE *m2 = open_memstream(&all, &asz);

                CHECK(rg_author_shrubs(w, m2, NULL, e->tileset != 0 ? e->tileset : 1u) == 0);
                fclose(m2);
                line = all != NULL ? strstr(all, key) : NULL;
                CHECK(line != NULL && sscanf(line + strlen(key), "%u blocked %u", &uses, &blocked) == 2 && uses > 0);
                byEye++;
                free(all);
            }
        } else {
            ++*nProp;
            fences += e->kind == GP_PROP_FENCE_EW || e->kind == GP_PROP_FENCE_NS;
            rocks += e->kind == GP_PROP_ROCK;
            flowers += e->kind == GP_PROP_FLOWER;
            CHECK(e->tileset == 0);
            snprintf(key, sizeof key, "ts 0x%08X 0x%03X beh ", (unsigned)e->tileset, (unsigned)e->metatile);
            line = strstr(pbuf, key);
            CHECK(line != NULL && sscanf(line + strlen(key), "0x%x uses %u blocked %u upper %u", &beh, &uses, &blocked, &upper) == 4);
            if (e->kind == GP_PROP_FLOWER)
                CHECK(50u * blocked <= uses && upper == 256u);   /* a bed is walkable (4 of 293 uses sit on a blocked cell) */
            else
                CHECK(2u * blocked >= uses);
        }
        p = GeneralPair(w, e->tileset);
        CHECK(p != NULL);
        if (p == NULL)
            continue;
        rg_cell_px(p, e->metatile, 0, &lo);
        rg_cell_px(p, e->metatile, 1, &up);
        rg_pair_close(p);
        for (y = 0; y < 16; y++)
            for (x = 0; x < 16; x++) {
                unsigned u = (up.drawn[y] >> x) & 1u, c, same = 0;

                upN += u;
                upGreen += u && LeafGreen(up.c[y][x]);
                lowDrawn += (lo.drawn[y] >> x) & 1u;
                if (!u)
                    continue;
                for (c = 0; c < 256u; c++)
                    same += lo.c[c / 16u][c % 16u] == up.c[y][x];
                if (same) keyed++; else kept++;
            }
        printf("  %s entry %u kind %u ts 0x%08X 0x%03X uses %u blocked %u upper %u green %u lower %u keyed %u kept %u\n", label, k,
               (unsigned)e->kind, (unsigned)e->tileset, (unsigned)e->metatile, uses, blocked, upN, upGreen, lowDrawn, keyed, kept);
        CHECK(lowDrawn == 256u);                          /* the ground under it is opaque */
        if (e->kind == GP_PROP_BUSH)
            CHECK(upN >= 60u && 2u * upGreen >= upN);     /* the bush is on the upper layer */
        else
            CHECK(upN >= 20u && upN == kept + keyed);
        if (e->kind == GP_PROP_FENCE_EW || e->kind == GP_PROP_FENCE_NS)
            CHECK(upN >= 40u && keyed == 0);              /* a fence is its own colours */
        if (e->kind == GP_PROP_ROCK)
            CHECK(kept >= 20u && (keyed == 0u || keyed == 108u));   /* 0x0E1 stands on the cliff colour: 108 px share it, measured */
        if (e->kind == GP_PROP_FLOWER)
            CHECK(kept >= 20u && keyed >= 20u);           /* the bed's grass goes, the flowers stay */
    }
    CHECK(fences == 3u && rocks == 3u && flowers == 1u && *nBush == 9u && *nProp == 7u);
    free(buf);
    free(pbuf);
    return byEye;
}

static void RunGrass(const RgWorld *w, const char *label)
{
    const GameProfile *gp = w->prof;
    static const unsigned kBeh[1] = {2};
    char *buf = NULL;
    size_t sz = 0;
    FILE *mem = open_memstream(&buf, &sz);
    const char *line;
    unsigned uses = 0, upper = 99, nBlade = 0;
    RgPair *p;
    RgCellPx g;
    unsigned x, y, drawn = 0;

    CHECK(rg_author_grass(w, mem, NULL, kBeh, 1, 0, NULL, 0) == 0);
    fclose(mem);
    CHECK(buf != NULL && gp->grassGround == 0x001u && gp_beh(&gp->bladeGrass, 0x02) && !gp_beh(&gp->bladeGrass, 0x03));
    /* the behaviour-2 General tile is the plain blade tile 0x00D, upper layer empty */
    line = buf != NULL ? strstr(buf, "ts 0x00000000 0x00D beh 0x02 uses ") : NULL;
    CHECK(line != NULL && sscanf(line + strlen("ts 0x00000000 0x00D beh 0x02 uses "), "%u upper %u", &uses, &upper) == 2);
    CHECK(uses > 2000u && upper == 0u);
    /* the canopy fringes 0x1C6 / 0x1C7 and 0x025 carry blades under canopy: tree-owned, so the grass list drops them */
    CHECK(InTable(gp->treeGround, gp->treeGroundCount, 0x1C6) && InTable(gp->treeGround, gp->treeGroundCount, 0x1C7)
          && InTable(gp->treeGround, gp->treeGroundCount, 0x025));
    for (line = buf; line != NULL && (line = strstr(line, "\ngrass ")) != NULL; line++)
        nBlade++;
    /* the skipped id is behaviour 2 with no upper layer on its own tileset, over sand */
    CHECK(gp->grassSkip != NULL && gp->grassSkipCount == 1 && gp->grassSkip[0].metatile == 0x206u && gp->grassSkip[0].tileset != 0);
    {
        char key[48];

        snprintf(key, sizeof key, "ts 0x%08X 0x206 beh 0x02 uses ", (unsigned)gp->grassSkip[0].tileset);
        CHECK(buf != NULL && strstr(buf, key) != NULL);
    }
    /* the plain grass ground is opaque on the lower layer */
    p = GeneralPair(w, 0);
    CHECK(p != NULL);
    if (p != NULL) {
        rg_cell_px(p, gp->grassGround, 0, &g);
        rg_pair_close(p);
        for (y = 0; y < 16; y++)
            for (x = 0; x < 16; x++)
                drawn += (g.drawn[y] >> x) & 1u;
        CHECK(drawn == 256u);
    }
    printf("  %s: %u behaviour-2 metatiles, 0x00D x%u, upper %u\n", label, nBlade, uses, upper);
    free(buf);
}

int main(void)
{
    static const struct { const char *env; GpGame game; const char *name; } kGames[2] = {
        {FXR_ENV_RUBY, GP_RUBY, "Ruby"}, {FXR_ENV_SAPP, GP_SAPPHIRE, "Sapphire"}};
    GpShrub ruby[16];
    unsigned nRuby = 0, i;
    bool haveRuby = false;

    for (i = 0; i < 2; i++) {
        size_t n = 0;
        uint8_t *rom = fxr_load_rom(kGames[i].env, &n);
        RgWorld w;
        unsigned nBush, nProp, byEye;

        if (rom == NULL) {
            printf("SKIP %s: set %s\n", kGames[i].name, kGames[i].env);
            sSkipped++;
            continue;
        }
        CHECK(rg_world_open(&w, rom, n) == RG_OK && w.prof != NULL && w.prof->game == kGames[i].game);
        if (w.prof != NULL && w.prof->game == kGames[i].game) {
            RunTrees(&w, kGames[i].name);
            byEye = RunShrubs(&w, kGames[i].name, &nBush, &nProp);
            printf("  %s: %u bushes + %u props, %u picked by eye\n", kGames[i].name, nBush, nProp, byEye);
            CHECK(byEye == 0u);
            RunGrass(&w, kGames[i].name);
            if (i == 0) {                     /* remember Ruby's table for the Sapphire comparison */
                for (nRuby = 0; nRuby < w.prof->shrubCount && nRuby < 16; nRuby++)
                    ruby[nRuby] = w.prof->shrubs[nRuby];
                haveRuby = true;
            } else if (haveRuby) {
                /* Sapphire = Ruby with every secondary tileset address 0x70 lower, ids and kinds equal */
                CHECK(nRuby == w.prof->shrubCount);
                for (unsigned k = 0; k < nRuby && k < w.prof->shrubCount; k++) {
                    CHECK(ruby[k].metatile == w.prof->shrubs[k].metatile && ruby[k].kind == w.prof->shrubs[k].kind);
                    CHECK(ruby[k].tileset == (w.prof->shrubs[k].tileset == 0 ? 0u : w.prof->shrubs[k].tileset + 0x70u));
                }
            }
            rg_world_close(&w);
        }
        free(rom);
    }
    printf("test_romgen_rs_foliage: %d checks, %d failures, %d skipped\n", sChecks, sFails, sSkipped);
    return sFails != 0;
}
