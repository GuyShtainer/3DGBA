/* romgen_cli.c -- host tool: reads a Pokemon Emerald (BPEE) .gba and writes the voxel data files
 * (3DGBA, GPLv3). Pure host code around the romgen cores in source/romgen/.
 *
 *   romgen ROM.gba OUTDIR [--time] [--only regions,signposts] [--dump-roles LAYOUT_ID]
 *
 * The output is derived from the user's ROM: write it outside the repo or under an ignored path. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rg_world.h"

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

int main(int argc, char **argv)
{
    const char *romPath = NULL, *outDir = NULL;
    bool timing = false;
    int i;
    size_t n = 0;
    uint8_t *rom;
    RgWorld w;
    RgErr e;
    double t0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--time") == 0)
            timing = true;
        else if (romPath == NULL)
            romPath = argv[i];
        else if (outDir == NULL)
            outDir = argv[i];
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
    t0 = NowMs();
    e = rg_world_open(&w, rom, n);
    if (e != RG_OK) {
        fprintf(stderr, "romgen: %s\n", rg_err_str(e));
        free(rom);
        return 1;
    }
    printf("world: %u layouts, %u maps, %u tilesets, %u pairs, %u outdoor maps, %u warp events, %u sign events\n",
           (unsigned)w.layoutCount, (unsigned)w.mapCount, (unsigned)w.tilesetCount - 1u, (unsigned)w.pairCount,
           (unsigned)w.outdoorMaps, (unsigned)w.warpEvents, (unsigned)w.signEvents);
    if (timing)
        printf("time: world %.1f ms\n", NowMs() - t0);
    rg_world_close(&w);
    free(rom);
    return 0;
}
