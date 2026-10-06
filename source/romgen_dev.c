/* romgen_dev.c -- see romgen_dev.h (3DGBA, GPLv3). Compiles to nothing unless ROMGEN_DEV_HOOK is 1. */
#include "romgen_dev.h"

#if ROMGEN_DEV_HOOK

#include <3ds.h>
#include <malloc.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "romgen/rg_relief_write.h"
#include "romgen/rg_run.h"

#define GO_PATH   "sdmc:/cias/control/romgen_go.txt"
#define OUT_DIR   "sdmc:/3ds/3DGBA/voxel"
#define STACK     (128 * 1024)
#define POLL_EVERY 120u          /* frames between stats of the go file */
#define AUTO_AFTER 600u          /* frames with Emerald loaded before the one automatic run (~10 s: past boot) */
#define LOG_DIR   "sdmc:/cias/netlogs"

typedef struct Job { const uint8_t *rom; size_t size; } Job;

static Job s_job;
static Thread s_thr;
static volatile int s_state;     /* 0 idle, 1 running, 2 finished (thread still to be joined) */
static volatile int s_cancel;
static unsigned s_tick;
static unsigned s_romFrames;   /* consecutive frames an Emerald ROM has been loaded */
static bool s_keptRelief;      /* the last run left an existing FULL relief.bin alone */
static bool s_autoDone;        /* the automatic run happens once per app session */

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

/* The report goes to two places: the voxel dir (next to the files it describes) and the netlogs folder, so the usual
 * "copy all of sdmc:/cias/netlogs after a run" picks it up with the other logs. One timestamped netlog per run. */
static void ReportLog(const char *text)
{
    char path[96];
    time_t tt = time(NULL);
    struct tm *lt = localtime(&tt);
    FILE *fp;

    snprintf(path, sizeof path, LOG_DIR "/3DGBA_romgen_%02d%02d_%02d%02d%02d.txt", lt ? lt->tm_mon + 1 : 0,
             lt ? lt->tm_mday : 0, lt ? lt->tm_hour : 0, lt ? lt->tm_min : 0, lt ? lt->tm_sec : 0);
    fp = fopen(path, "w");
    if (fp == NULL)
        return;
    (void)fputs(text, fp);
    (void)fclose(fp);
}

static void Report(const char *text) { (void)WriteAtomic("romgen_timings.txt", text, strlen(text)); }

static void MakeDirs(void)
{
    (void)mkdir("sdmc:/3ds", 0777);
    (void)mkdir("sdmc:/3ds/3DGBA", 0777);
    (void)mkdir(OUT_DIR, 0777);
    (void)mkdir("sdmc:/cias", 0777);
    (void)mkdir(LOG_DIR, 0777);
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
    n += (unsigned)snprintf(t + n, cap - n, "relief ledges: %zu bytes, %u rows, %u cells, %u ms\n", o->reliefSize, o->rst.rows,
                            o->rst.cells, (unsigned)o->msRelief);
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

/* PC fallback (SPEC-S3 1.7): the device only builds ledges, so a FULL relief.bin the user copied from the PC CLI to
 * OUT_DIR is left alone. The check reads just the header and row table of the existing file. */
static bool KeepExistingRelief(const RgOutput *o)
{
    static uint8_t head[8 + 14 * 256];
    char path[128];
    FILE *fp;
    size_t n;

    snprintf(path, sizeof path, "%s/relief.bin", OUT_DIR);
    fp = fopen(path, "rb");
    if (fp == NULL)
        return false;
    n = fread(head, 1, sizeof head, fp);
    (void)fclose(fp);
    return rg_relief_keep_existing(head, n, o->rst.drawnRows) != 0;
}

static bool WriteOutputs(const RgOutput *o)
{
    bool ok = o->regions != NULL && WriteAtomic("regions.bin", o->regions, o->regionsSize);

    if (o->signs != NULL)
        ok = WriteAtomic("signposts.bin", o->signs, o->signsSize) && ok;
    if (o->buildings != NULL)
        ok = WriteAtomic("buildings.bin", o->buildings, o->buildingsSize) && ok;
    s_keptRelief = o->relief != NULL && KeepExistingRelief(o);
    if (o->relief != NULL && !s_keptRelief)
        ok = WriteAtomic("relief.bin", o->relief, o->reliefSize) && ok;
    return ok;
}

static void WorkerMain(void *arg)
{
    static char text[1536];
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
    opts.relief = RG_RELIEF_LEDGES;   /* S3a: the full relief stays host-only (SPEC-S3 1.7) */
    a0 = Arena();
    u0 = Used();
    t0 = NowMs();
    e = rg_run(j->rom, j->size, &opts, &out);
    t0 = NowMs() - t0;
    wrote = (e != RG_OK) || WriteOutputs(&out);
    Describe(text, sizeof text, e, &out, t0, a0, Arena(), u0, Used(), j->size);
    if (s_keptRelief)
        (void)strncat(text, "relief.bin: an existing FULL file (drawn rows > 0) was left in place, not overwritten with ledges\n", sizeof text - strlen(text) - 1u);
    if (!wrote)
        (void)strncat(text, "WRITE FAILED: at least one output file could not be written to " OUT_DIR "\n", sizeof text - strlen(text) - 1u);
    if (e == RG_OK)
        rg_output_free(&out);
    Report(text);
    ReportLog(text);
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
    if (rom == NULL || romSize == 0) {
        s_romFrames = 0;
        return;
    }
    if (s_state != 0)
        return;
    if (!s_autoDone && ++s_romFrames >= AUTO_AFTER) {
        s_autoDone = true;
        Start(rom, romSize);
        return;
    }
    if (++s_tick % POLL_EVERY != 0 || stat(GO_PATH, &st) != 0)
        return;
    remove(GO_PATH);   /* a manual re-run: consumed on pickup, like every control file */
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
