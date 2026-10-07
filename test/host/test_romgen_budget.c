// test_romgen_budget.c -- look L7 follow-up: no chunk of any map of any game overflows the device's vertex scratch.
// The device (ctr_voxel.c) builds a chunk into a fixed scratch of VOXEL_CHUNK_SCRATCH vertices and REFUSES whatever is
// emitted past it: models come last, so an overflow silently drops a building's tail parts (Pokemon Tower and the Power
// Plant lost theirs before the flat-cap change). rg_budget.c measures every chunk of every map with the device's own
// emitters in the device's order; this suite fails on
//   - any chunk over the scratch (all three games, relief FULL: the worst case),
//   - any model over RG_BUDGET_MODEL_VERTS (the per-model limit `romgen author check` also enforces),
//   - RG_BUDGET_SCRATCH drifting from ctr_voxel.c's lighting build (read from the source text).
// Emerald from ROMGEN_ROM; FireRed / LeafGreen from ROMGEN_ROM_FR / ROMGEN_ROM_LG or beside ROMGEN_ROM. A missing ROM
// prints SKIP. No game bytes are committed. Each game runs in its own child process: the consumer keeps per-process
// state (one game per boot on the device), and measuring FireRed after Emerald in one process under-counts it
// (2286 vs 2712 vertices for the worst chunk).
//
//   ROMGEN_ROM=$PWD/roms/emerald.gba make -C tools/romgen test T=budget
#define RG_AUTHOR_CONSUMER 1
#include "../../tools/romgen/rg_budget.c"

#include "rg_fixture.h"

#include <sys/wait.h>

static int sChecks, sFails, sSkipped;
#define CHECK(c) do { ++sChecks; if (!(c)) { ++sFails; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

/* The device constant, from the source: the CTR_VOXEL_LIGHTING branch's `#define VOXEL_CHUNK_SCRATCH (...)` line. */
static void TestScratchPin(void)
{
    char path[1024], line[256];
    const char *slash = strrchr(__FILE__, '/');
    FILE *fp;
    bool found = false;

    CHECK(RG_BUDGET_SCRATCH == 9344u);
    CHECK(RG_BUDGET_MODEL_VERTS < RG_BUDGET_SCRATCH);
    if (slash == NULL) { sSkipped++; printf("SKIP scratch pin (no source path)\n"); return; }
    snprintf(path, sizeof path, "%.*s/../../source/voxel/ctr_voxel.c", (int)(slash - __FILE__), __FILE__);
    if ((fp = fopen(path, "r")) == NULL) { sSkipped++; printf("SKIP scratch pin (%s not found)\n", path); return; }
    while (fgets(line, sizeof line, fp) != NULL) {
        if (strncmp(line, "#define VOXEL_CHUNK_SCRATCH (", 29) == 0) {   /* the lighting build's (the only parenthesised one) */
            CHECK(strncmp(line, "#define VOXEL_CHUNK_SCRATCH (8192u + 64u * 18u)", 47) == 0);
            found = true;
            break;
        }
    }
    fclose(fp);
    CHECK(found);
}

static void MeasureGame(const char *name, uint8_t *rom, size_t n)
{
    RgBudget b;
    char why[256];
    unsigned i, over = 0, worst = 0, worstModel = 0;

    memset(&b, 0, sizeof b);
    why[0] = 0;
    CHECK(rg_budget_run(rom, n, &b, why, sizeof why));
    if (b.nChunks == 0) { printf("  %s: no chunks measured: %s\n", name, why); CHECK(b.nChunks > 0); free(rom); return; }
    CHECK(b.nModels > 0);
    CHECK(b.layoutsSkipped == 0);
    for (i = 0; i < b.nChunks; i++) {
        const RgBudgetChunk *c = &b.chunk[i];

        CHECK(c->total == c->ground + c->trees + c->models);
        if (c->total > RG_BUDGET_SCRATCH) {
            over++;
            printf("FAIL %s: map %u/%u layout %u chunk %d,%d emits %u vertices, scratch %u (%s)\n", name, c->group, c->num,
                   c->layout, c->cx, c->cy, c->total, RG_BUDGET_SCRATCH, c->what);
        }
        if (c->total > worst) worst = c->total;
    }
    for (i = 0; i < b.nModels; i++) {
        if (b.model[i].verts > RG_BUDGET_MODEL_VERTS)
            printf("FAIL %s: model %s emits %u vertices, limit %u\n", name, b.model[i].name, b.model[i].verts,
                   RG_BUDGET_MODEL_VERTS);
        if (b.model[i].verts > worstModel) worstModel = b.model[i].verts;
    }
    CHECK(over == 0);
    CHECK(b.overChunks == 0);
    CHECK(b.overModels == 0);
    printf("  %s: %u chunks in %u layouts, worst chunk %u of %u vertices (%u%%), worst model %u of %u\n", name, b.nChunks,
           b.layoutsMeasured, worst, RG_BUDGET_SCRATCH, worst * 100u / RG_BUDGET_SCRATCH, worstModel, RG_BUDGET_MODEL_VERTS);
    rg_budget_free(&b);
    free(rom);
}

/* One game in a child process; the child's check and failure counts come back through a pipe. */
static void TestGame(const char *name, uint8_t *rom, size_t n)
{
    int fd[2], st = 0, got[2] = {0, 0};
    pid_t pid;

    if (rom == NULL) { sSkipped++; printf("SKIP %s (ROM not found)\n", name); return; }
    fflush(stdout);
    if (pipe(fd) != 0 || (pid = fork()) < 0) { CHECK(!"fork"); free(rom); return; }
    if (pid == 0) {
        close(fd[0]);
        sChecks = sFails = 0;
        MeasureGame(name, rom, n);   /* frees rom */
        got[0] = sChecks;
        got[1] = sFails;
        fflush(stdout);
        if (write(fd[1], got, sizeof got) != (ssize_t)sizeof got) _exit(2);
        _exit(0);
    }
    close(fd[1]);
    if (read(fd[0], got, sizeof got) != (ssize_t)sizeof got) got[0] = 0;
    close(fd[0]);
    waitpid(pid, &st, 0);
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0 && got[0] > 0);
    sChecks += got[0];
    sFails += got[1];
    free(rom);
}

static uint8_t *LoadEmerald(size_t *n)
{
    const char *p = getenv("ROMGEN_ROM");
    FILE *fp;
    long len;
    uint8_t *buf;

    if (p == NULL || p[0] == 0 || (fp = fopen(p, "rb")) == NULL)
        return NULL;
    if (fseek(fp, 0, SEEK_END) != 0 || (len = ftell(fp)) <= 0 || len > 0x2000000L || fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return NULL; }
    buf = (uint8_t *)malloc((size_t)len);
    if (buf == NULL || fread(buf, 1, (size_t)len, fp) != (size_t)len) { free(buf); fclose(fp); return NULL; }
    fclose(fp);
    *n = (size_t)len;
    return buf;
}

int main(void)
{
    size_t n = 0;
    uint8_t *rom;

    TestScratchPin();
    rom = LoadEmerald(&n);
    TestGame("Emerald", rom, n);
    rom = fxr_load_rom(FXR_ENV_FR, &n);
    TestGame("FireRed", rom, n);
    rom = fxr_load_rom(FXR_ENV_LG, &n);
    TestGame("LeafGreen", rom, n);
    printf("test_romgen_budget: %d checks, %d failures, %d skipped\n", sChecks, sFails, sSkipped);
    return sFails != 0;
}
