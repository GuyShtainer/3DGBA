/* romgen_cli.c -- host tool: reads a Pokemon Emerald (BPEE) .gba and writes the voxel data files
 * (3DGBA, GPLv3). Pure host code around the romgen cores in source/romgen/.
 *
 *   romgen ROM.gba OUTDIR [--time] [--only regions,signposts] [--dump-roles LAYOUT_ID]
 *
 * Writes OUTDIR/regions.bin and OUTDIR/signposts.bin. The output is derived from the user's ROM: write it
 * outside the repo or under an ignored path. --dump-roles prints a layout with upstream's letters
 * (. floor, ~ water, _ ledge, = stairs, W wall, T tree, o prop, % shelf, | fence, # cliff, S signpost). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rg_run.h"

static double NowMs(void)
{
    struct timespec ts;
    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static uint8_t *ReadFile(const char *path, size_t *n)
{
    FILE *fp = fopen(path, "rb");
    uint8_t *buf;
    long len;

    if (fp == NULL)
        return NULL;
    if (fseek(fp, 0, SEEK_END) != 0 || (len = ftell(fp)) <= 0 || len > 0x2000000L || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }
    buf = (uint8_t *)malloc((size_t)len);
    if (buf == NULL || fread(buf, 1, (size_t)len, fp) != (size_t)len) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    *n = (size_t)len;
    return buf;
}

static bool WriteFile(const char *dir, const char *name, const uint8_t *b, size_t n)
{
    char path[1024];
    FILE *fp;
    bool ok;

    snprintf(path, sizeof(path), "%s/%s", dir, name);
    fp = fopen(path, "wb");
    if (fp == NULL) {
        fprintf(stderr, "romgen: cannot write %s\n", path);
        return false;
    }
    ok = fwrite(b, 1, n, fp) == n;
    return fclose(fp) == 0 && ok;
}

static void DumpRoles(const RgOutput *o, unsigned id)
{
    static const char kLetters[11] = {'.', '~', '_', '=', 'W', 'T', 'o', '%', '|', '#', 'S'};
    const uint8_t *b = o->regions;
    unsigned count = (unsigned)(b[4] | (b[5] << 8)), i, x, y;

    for (i = 0; i < count; i++) {
        const uint8_t *row = b + 8 + 12u * i;
        unsigned lid = (unsigned)(row[0] | (row[1] << 8)), w = (unsigned)(row[2] | (row[3] << 8)), h = (unsigned)(row[4] | (row[5] << 8));
        uint32_t off = (uint32_t)row[8] | ((uint32_t)row[9] << 8) | ((uint32_t)row[10] << 16) | ((uint32_t)row[11] << 24);

        if (lid != id)
            continue;
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++)
                putchar(kLetters[b[off + y * w + x] % 11u]);
            putchar('\n');
        }
        return;
    }
    fprintf(stderr, "romgen: layout %u is not in the output\n", id);
}

int main(int argc, char **argv)
{
    const char *romPath = NULL, *outDir = NULL;
    bool timing = false, wantRegions = true, wantSigns = true;
    int dumpId = 0, i;
    size_t n = 0;
    uint8_t *rom;
    RgRunOpts opts;
    RgOutput out;
    RgErr e;
    double t0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--time") == 0) {
            timing = true;
        } else if (strcmp(argv[i], "--only") == 0 && i + 1 < argc) {
            const char *v = argv[++i];
            wantRegions = strstr(v, "regions") != NULL;
            wantSigns = strstr(v, "signposts") != NULL;
        } else if (strcmp(argv[i], "--dump-roles") == 0 && i + 1 < argc) {
            dumpId = atoi(argv[++i]);
        } else if (romPath == NULL) {
            romPath = argv[i];
        } else if (outDir == NULL) {
            outDir = argv[i];
        }
    }
    if (romPath == NULL || outDir == NULL) {
        fprintf(stderr, "usage: romgen ROM.gba OUTDIR [--time] [--only regions,signposts] [--dump-roles LAYOUT_ID]\n");
        return 2;
    }
    rom = ReadFile(romPath, &n);
    if (rom == NULL) {
        fprintf(stderr, "romgen: cannot read %s\n", romPath);
        return 1;
    }
    memset(&opts, 0, sizeof(opts));
    opts.nowMs = NowMs;
    opts.wantSigns = wantSigns;
    t0 = NowMs();
    e = rg_run(rom, n, &opts, &out);
    if (e != RG_OK) {
        fprintf(stderr, "romgen: %s\n", rg_err_str(e));
        free(rom);
        return 1;
    }
    printf("world: %u layouts, %u maps, %u tilesets, %u pairs, %u outdoor maps\n", out.layouts, out.maps, out.tilesets,
           out.pairs, out.outdoorMaps);
    printf("roles: floor %u water %u ledge %u stair %u wall %u tree %u prop %u shelf %u fence %u cliff %u signpost %u\n",
           out.roleCount[0], out.roleCount[1], out.roleCount[2], out.roleCount[3], out.roleCount[4], out.roleCount[5],
           out.roleCount[6], out.roleCount[7], out.roleCount[8], out.roleCount[9], out.roleCount[10]);
    if (wantRegions && WriteFile(outDir, "regions.bin", out.regions, out.regionsSize))
        printf("regions.bin: %zu bytes, %u layouts\n", out.regionsSize, out.layouts);
    if (wantSigns && out.signs != NULL && WriteFile(outDir, "signposts.bin", out.signs, out.signsSize))
        printf("signposts.bin: %zu bytes, %u records (%u with a head, %u with an empty mask)\n", out.signsSize, out.signCount,
               out.headCount, out.emptyMasks);
    if (timing)
        printf("time: total %.1f ms (world %.1f, roles %.1f, signs %.1f, serialise %.1f)\n", NowMs() - t0, out.msWorld,
               out.msRoles, out.msSigns, out.msWrite);
    if (dumpId > 0)
        DumpRoles(&out, (unsigned)dumpId);
    rg_output_free(&out);
    free(rom);
    return 0;
}
