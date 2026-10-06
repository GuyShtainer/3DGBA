// test_romgen_frlg_trees.c -- Phase 34 slice T1: `romgen author ROM trees` on the real FireRed / LeafGreen rev 1 ROMs.
// The tool lists the General-tileset metatiles that are foliage (Gummygamer's rule, tools/romgen/rg_author_trees.c) and
// the 2x2 blocks they form on collision-blocked cells. This suite pins what the profile's Kanto tree table rests on:
// the twelve table ids are foliage on blocked cells everywhere they occur, the Pallet border block 1C 1D / 14 15 is the
// most common block, the only other heavily used blocked foliage metatile is the round bush 0x005 (kept flat by decision),
// and FireRed and LeafGreen give the same list. Real ROMs come from ROMGEN_ROM_FR / ROMGEN_ROM_LG (absolute paths) or
// firered.gba / leafgreen.gba beside ROMGEN_ROM; a missing ROM prints SKIP. No game bytes are committed.
//
//   ROMGEN_ROM=$PWD/roms/emerald.gba make -C tools/romgen test T=frlg_trees
#define RG_AUTHOR_CONSUMER 1
#include "../../tools/romgen/rg_author.c"
#include "../../tools/romgen/rg_author_trees.c"
#include "../../tools/romgen/rg_png.c"

#include "rg_fixture.h"

static int sChecks, sFails, sSkipped;
#define CHECK(c) do { ++sChecks; if (!(c)) { ++sFails; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static char *RunTrees(const uint8_t *rom, size_t n, GpGame game, const char *label, int idx)
{
    RgWorld w;
    char *buf = NULL;
    size_t sz = 0;
    FILE *mem;
    unsigned id, nTable = 0;
    const char *p;

    CHECK(rg_world_open(&w, rom, n) == RG_OK && w.prof->game == game);
    mem = open_memstream(&buf, &sz);
    CHECK(rg_author_trees_list(&w, mem) == 0);
    fclose(mem);
    CHECK(buf != NULL && strstr(buf, "# trees: ") == buf);
    /* the first block listed is the most common one: Pallet's border block */
    p = strstr(buf, "block ");
    CHECK(p != NULL && strncmp(p, "block 0x01C 0x01D 0x014 0x015 x", 31) == 0);
    /* every table id: a foliage metatile (g=1) used only on blocked cells, with a real count */
    for (id = 0; id < w.prof->treePartCount; id++) {
        char key[32];
        unsigned uses = 0, blocked = 0;
        int z, g;
        const char *line;

        snprintf(key, sizeof key, "meta 0x%03X uses ", (unsigned)w.prof->treePart[2 * id]);
        line = strstr(buf, key);
        CHECK(line != NULL);
        if (line != NULL && sscanf(line + strlen(key), "%u blocked %u z=%d g=%d", &uses, &blocked, &z, &g) == 4) {
            CHECK(g == 1 && uses > 0 && blocked == uses);
            nTable++;
        }
    }
    CHECK(nTable == 12);
    /* any other metatile that is foliage on at least 100 blocked cells must be a decided exception: the round bush 0x005 */
    for (p = buf; (p = strstr(p, "\nmeta 0x")) != NULL; p++) {
        unsigned m, uses, blocked;
        int z, g, inTable = 0;
        unsigned k;

        if (sscanf(p + 1, "meta 0x%x uses %u blocked %u z=%d g=%d", &m, &uses, &blocked, &z, &g) != 5)
            continue;
        for (k = 0; k < w.prof->treePartCount; k++)
            inTable |= (unsigned)w.prof->treePart[2 * k] == m;
        if (g == 1 && blocked >= 100u && !inTable)
            CHECK(m == 0x005u && blocked == 193u);
    }
    printf("  %s: tree list %zu bytes\n", label, sz);
    (void)idx;
    rg_world_close(&w);
    return buf;
}

int main(void)
{
    static const struct { const char *env; GpGame game; const char *name; } kGames[2] = {
        {FXR_ENV_FR, GP_FIRERED, "FireRed"}, {FXR_ENV_LG, GP_LEAFGREEN, "LeafGreen"}};
    char *out[2] = {NULL, NULL};
    int i;

    for (i = 0; i < 2; i++) {
        size_t n = 0;
        uint8_t *rom = fxr_load_rom(kGames[i].env, &n);

        if (rom == NULL) {
            printf("SKIP %s: set ROMGEN_ROM (firered.gba / leafgreen.gba beside it) or %s\n", kGames[i].name, kGames[i].env);
            sSkipped++;
            continue;
        }
        out[i] = RunTrees(rom, n, kGames[i].game, kGames[i].name, i);
        free(rom);
    }
    if (out[0] != NULL && out[1] != NULL) {      /* the same list, apart from the "# trees: BPRE / BPGE" header line */
        const char *a = strchr(out[0], '\n'), *b = strchr(out[1], '\n');

        CHECK(a != NULL && b != NULL && strcmp(a, b) == 0);
    }
    free(out[0]);
    free(out[1]);
    printf("test_romgen_frlg_trees: %d checks, %d failures, %d skipped\n", sChecks, sFails, sSkipped);
    return sFails != 0;
}
