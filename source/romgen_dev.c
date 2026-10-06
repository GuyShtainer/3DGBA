/* romgen_dev.c -- see romgen_dev.h (3DGBA, GPLv3). Compiles to nothing unless ROMGEN_DEV_HOOK is 1. */
#include "romgen_dev.h"

#if ROMGEN_DEV_HOOK

#include <3ds.h>
#include <malloc.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "romgen/rg_run.h"

#define GO_PATH   "sdmc:/cias/control/romgen_go.txt"
#define OUT_DIR   "sdmc:/3ds/3DGBA/voxel"
#define STACK     (128 * 1024)
#define POLL_EVERY 120u          /* frames between stats of the go file */

typedef struct Job { const uint8_t *rom; size_t size; } Job;

static Job s_job;
static Thread s_thr;
static volatile int s_state;     /* 0 idle, 1 running, 2 finished (thread still to be joined) */
static volatile int s_cancel;
static unsigned s_tick;

static double NowMs(void) { return (double)osGetTime(); }

static bool WriteAtomic(const char *name, const void *b, size_t n)
{
    char tmp[128], dst[128];
    FILE *fp;
    bool ok;

    snprintf(tmp, sizeof tmp, "%s/%s.tmp", OUT_DIR, name);
    snprintf(dst, sizeof dst, "%s/%s", OUT_DIR, name);
    fp = fopen(tmp, "wb");
    if (fp == NULL)
        return false;
    ok = fwrite(b, 1, n, fp) == n;
    ok = (fclose(fp) == 0) && ok;
    if (ok) {
        remove(dst);
        ok = rename(tmp, dst) == 0;
    }
    return ok;
}

static void Report(const char *text) { (void)WriteAtomic("romgen_timings.txt", text, strlen(text)); }

static void MakeDirs(void)
{
    (void)mkdir("sdmc:/3ds", 0777);
    (void)mkdir("sdmc:/3ds/3DGBA", 0777);
    (void)mkdir(OUT_DIR, 0777);
}

/* Heap numbers are newlib's view (mallinfo): arena = bytes taken from the OS heap (it does not shrink), uordblks =
 * live. The arena delta around rg_run is the device's honest peak-heap figure (fragmentation included). */
static size_t Arena(void) { struct mallinfo mi = mallinfo(); return (size_t)mi.arena; }
static size_t Used(void)  { struct mallinfo mi = mallinfo(); return (size_t)mi.uordblks; }

static void Describe(char *t, size_t cap, RgErr e, const RgOutput *o, double totalMs, size_t a0, size_t a1, size_t u0, size_t u1,
                     size_t romSize)
{
    unsigned i, n = 0;

    n += (unsigned)snprintf(t + n, cap - n, "romgen device run: %s\nrom bytes %zu\n", e == RG_OK ? "OK" : rg_err_str(e), romSize);
    n += (unsigned)snprintf(t + n, cap - n, "ms total %.0f world %.0f roles %.0f signs %.0f serialise %.0f\n", totalMs, o->msWorld,
                            o->msRoles, o->msSigns, o->msWrite);
    n += (unsigned)snprintf(t + n, cap - n, "ms buildings models %.0f gates %.0f placements+write %.0f\n", o->msBuildModels,
                            o->msChecks, o->msWriteBuildings);
    n += (unsigned)snprintf(t + n, cap - n, "bytes regions %zu signposts %zu buildings %zu\n", o->regionsSize, o->signsSize,
                            o->buildingsSize);
    n += (unsigned)snprintf(t + n, cap - n, "counts models %u pages %u pageModels %u placements %u vertices %u masks %u variants %u\n",
                            o->bModels, o->bPages, o->bPageModels, o->bPlacements, o->bVertices, o->bMasks, o->bVariants);
    n += (unsigned)snprintf(t + n, cap - n, "gate failures %u\n", o->buildingsFailed);
    for (i = 0; i < o->buildingsFailed && i < RG_MAX_SKIPPED && n < cap; i++)
        n += (unsigned)snprintf(t + n, cap - n, "  failed: %s\n", o->failedNames[i]);
    n += (unsigned)snprintf(t + n, cap - n, "heap arena before %zu after %zu (peak delta ~%zu); live before %zu after %zu\n", a0, a1,
                            a1 > a0 ? a1 - a0 : 0u, u0, u1);
    n += (unsigned)snprintf(t + n, cap - n, "process heap size %u, linear free %u\n", (unsigned)envGetHeapSize(),
                            (unsigned)linearSpaceFree());
    (void)n;
}

static bool WriteOutputs(const RgOutput *o)
{
    bool ok = o->regions != NULL && WriteAtomic("regions.bin", o->regions, o->regionsSize);

    if (o->signs != NULL)
        ok = WriteAtomic("signposts.bin", o->signs, o->signsSize) && ok;
    if (o->buildings != NULL)
        ok = WriteAtomic("buildings.bin", o->buildings, o->buildingsSize) && ok;
    return ok;
}

static void WorkerMain(void *arg)
{
    static char text[1024];
    const Job *j = (const Job *)arg;
    RgRunOpts opts;
    RgOutput out;
    size_t a0, u0;
    bool wrote;
    double t0;
    RgErr e;

    MakeDirs();
    Report("romgen device run: STARTED (no result yet; if this stays, the app died or the run is still going)\n");
    memset(&opts, 0, sizeof opts);
    opts.nowMs = NowMs;
    opts.cancel = &s_cancel;
    opts.wantSigns = true;
    opts.wantBuildings = true;
    a0 = Arena();
    u0 = Used();
    t0 = NowMs();
    e = rg_run(j->rom, j->size, &opts, &out);
    t0 = NowMs() - t0;
    wrote = (e != RG_OK) || WriteOutputs(&out);
    Describe(text, sizeof text, e, &out, t0, a0, Arena(), u0, Used(), j->size);
    if (!wrote)
        (void)strncat(text, "WRITE FAILED: at least one output file could not be written to " OUT_DIR "\n", sizeof text - strlen(text) - 1u);
    if (e == RG_OK)
        rg_output_free(&out);
    Report(text);
    s_state = 2;
}

static void Start(const uint8_t *rom, size_t size)
{
    s32 prio = 0x30;

    s_job.rom = rom;
    s_job.size = size;
    s_cancel = 0;
    (void)svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    prio += 3;   /* below the emulator workers (main + 1): this never takes a core from a game */
    if (prio > 0x3F)
        prio = 0x3F;
    s_state = 1;
    s_thr = threadCreate(WorkerMain, &s_job, STACK, prio, 1, false);
    if (s_thr == NULL)
        s_thr = threadCreate(WorkerMain, &s_job, STACK, prio, -2, false);
    if (s_thr == NULL)
        s_state = 0;
}

static void Reap(void)
{
    threadJoin(s_thr, U64_MAX);
    threadFree(s_thr);
    s_thr = NULL;
    s_state = 0;
}

void romgen_dev_poll(const uint8_t *rom, size_t romSize)
{
    struct stat st;

    if (s_state == 2) {
        Reap();
        return;
    }
    if (s_state != 0 || rom == NULL || romSize == 0 || ++s_tick % POLL_EVERY != 0)
        return;
    if (stat(GO_PATH, &st) != 0)
        return;
    remove(GO_PATH);   /* consumed on pickup, like every control file */
    Start(rom, romSize);
}

void romgen_dev_stop(void)
{
    if (s_state == 0 || s_thr == NULL)
        return;
    s_cancel = 1;
    Reap();
}

#endif /* ROMGEN_DEV_HOOK */
