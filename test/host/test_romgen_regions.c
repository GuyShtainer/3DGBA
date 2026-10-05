// test_romgen_regions.c -- host test for source/romgen/rg_regions.c (phase 33 S1.2, SPEC-S0-S1 sections 4, 7.3):
// the VXR5 file written by romgen round-trips through the VENDORED consumer parser (voxel_regions.c).
// Synthetic mini-ROM always; the real ROM (ROMGEN_ROM=/path/emerald.gba) adds the full 442-layout file.
//
//   clang -std=c11 -Wall -Wextra -O2 -ffp-contract=off -fsanitize=address,undefined -DVOXEL_HOST_FILES \
//         -I source/romgen -I source/voxel -I test/host test/host/test_romgen_regions.c \
//         source/romgen/rg_world.c source/romgen/rg_art.c source/romgen/rg_roles.c source/romgen/rg_regions.c \
//         source/voxel/voxel_regions.c source/voxel/ctr_shims_pure.c source/voxel/vx_lz77.c -lm \
//         -o /tmp/trgg && /tmp/trgg
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rg_regions.h"
#include "rg_fixture.h"
#include "voxel_regions.h"

static int sChecks, sFails;
#define CHECK(c) do { ++sChecks; if (!(c)) { ++sFails; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static char sDir[64];
static char sCwd[1024];

static void EnterTemp(void)
{
    char sub[96];
    strcpy(sDir, "/tmp/rgreg.XXXXXX");
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
    snprintf(p, sizeof(p), "%s/voxel/regions.bin", sDir);
    (void)unlink(p);
    snprintf(p, sizeof(p), "%s/voxel", sDir);
    (void)rmdir(p);
    (void)rmdir(sDir);
}

static void WriteBin(const uint8_t *b, size_t n)
{
    FILE *fp = fopen("voxel/regions.bin", "wb");
    if (!fp || fwrite(b, 1, n, fp) != n) abort();
    fclose(fp);
}

/* Every cell of every present layout agrees through the vendored parser. Returns the cell count. */
static unsigned CompareAll(const RgWorld *w, const RgRoles *r)
{
    unsigned li, n = 0, x, y;
    for (li = 0; li < w->layoutCount; li++) {
        const RgLayout *L = &w->layouts[li];
        const uint8_t *ro;
        if (!L->present) { CHECK(VoxelRegions_RoleAt(L->id, 0, 0) == VOXEL_ROLE_FLOOR); continue; }
        ro = rg_roles_of(r, L->id);
        for (y = 0; y < L->h; y++)
            for (x = 0; x < L->w; x++) {
                if (VoxelRegions_RoleAt(L->id, (int)x, (int)y) != ro[y * L->w + x]) { CHECK(0); return n; }
                n++;
            }
        CHECK(VoxelRegions_RoleAt(L->id, -1, 0) == VOXEL_ROLE_FLOOR && VoxelRegions_RoleAt(L->id, (int)L->w, 0) == VOXEL_ROLE_FLOOR
              && VoxelRegions_RoleAt(L->id, 0, (int)L->h) == VOXEL_ROLE_FLOOR);
    }
    ++sChecks;   /* the all-cells comparison itself */
    return n;
}

static uint8_t *Serialise(const RgWorld *w, const RgRoles *r, size_t *n)
{
    size_t need = rg_regions_write(w, r, NULL, 0);
    uint8_t *b = (uint8_t *)malloc(need ? need : 1);
    CHECK(need > 8);
    CHECK(rg_regions_write(w, r, b, need) == need);
    CHECK(need == 0 || rg_regions_write(w, r, b, need - 1) == 0);   /* too small */
    *n = need;
    return b;
}

static void TestSynthetic(void)
{
    RgFx f;
    RgWorld w;
    RgRoles r;
    uint16_t *blocks;
    unsigned seed = 12345, i, id;
    size_t n;
    uint8_t *bin;
    const int dims[4][2] = {{7, 5}, {20, 20}, {1, 9}, {13, 3}};
    uint8_t keep;

    fxr_init(&f);
    (void)fxr_layout_null(&f);   /* id 2: a NULL entry, absent from the file */
    for (i = 0; i < 4; i++) {
        blocks = (uint16_t *)malloc((size_t)(dims[i][0] * dims[i][1]) * 2);
        {
            int k;
            for (k = 0; k < dims[i][0] * dims[i][1]; k++) { seed = seed * 1103515245u + 12345u; blocks[k] = (uint16_t)((seed >> 16) & 0x0400); }
        }
        id = fxr_layout(&f, dims[i][0], dims[i][1], blocks, 0, 0);
        (void)fxr_map(&f, 0, i % 2 ? MAP_TYPE_ROUTE : MAP_TYPE_INDOOR, id, NULL, 0, NULL, 0);
        free(blocks);
    }
    fxr_finish(&f);
    CHECK(rg_world_open(&w, f.rom, FXR_ROM_SIZE) == RG_OK);
    CHECK(rg_roles_all(&w, &r, false, NULL, NULL) == RG_OK);
    /* force every role value into the data so all 11 round-trip */
    for (i = 0; i < r.total; i++) { seed = seed * 1103515245u + 12345u; r.data[i] = (uint8_t)(((seed >> 16) & 0xFFFF) % 11); }
    bin = Serialise(&w, &r, &n);
    CHECK(n == 8 + 12 * 5 + (1 + 35 + 400 + 9 + 39));   /* 5 present layouts: the filler (1x1) and four */
    CHECK(memcmp(bin, "VXR5", 4) == 0 && bin[4] == 5 && bin[5] == 0 && bin[6] == 0 && bin[7] == 0);
    EnterTemp();
    WriteBin(bin, n);
    CHECK(VoxelRegions_Init());
    CHECK(CompareAll(&w, &r) == 1 + 35 + 400 + 9 + 39);
    CHECK(VoxelRegions_RoleAt(2, 0, 0) == VOXEL_ROLE_FLOOR);   /* absent id */
    CHECK(VoxelRegions_RoleAt(999, 0, 0) == VOXEL_ROLE_FLOOR);
    VoxelRegions_Shutdown();
    /* negatives: the consumer must reject what our writer would never produce, proving both agree on validity */
    keep = bin[0]; bin[0] = 'X'; WriteBin(bin, n); CHECK(!VoxelRegions_Init()); bin[0] = keep;
    {   /* descending id */
        uint8_t a = bin[8], b = bin[8 + 12];
        bin[8] = b; bin[8 + 12] = a;
        WriteBin(bin, n); CHECK(!VoxelRegions_Init());
        bin[8] = a; bin[8 + 12] = b;
    }
    keep = bin[n - 1]; bin[n - 1] = 11; WriteBin(bin, n); CHECK(!VoxelRegions_Init()); bin[n - 1] = keep;   /* role byte 11 */
    WriteBin(bin, n - 1); CHECK(!VoxelRegions_Init());   /* truncated: the last layout's roles run past the end */
    WriteBin(bin, n); CHECK(VoxelRegions_Init());        /* restored file is valid again */
    VoxelRegions_Shutdown();
    LeaveTemp();
    free(bin);
    rg_roles_free(&r);
    rg_world_close(&w);
    free(f.rom);
}

static void TestRealRom(void)
{
    const char *path = getenv("ROMGEN_ROM");
    FILE *fp;
    uint8_t *rom, *bin;
    size_t n, size;
    RgWorld w;
    RgRoles r;

    if (!path || !(fp = fopen(path, "rb"))) { printf("SKIP real-ROM regions round trip (set ROMGEN_ROM)\n"); return; }
    rom = (uint8_t *)malloc(0x2000000);
    n = fread(rom, 1, 0x2000000, fp);
    fclose(fp);
    CHECK(rg_world_open(&w, rom, n) == RG_OK);
    CHECK(rg_roles_all(&w, &r, false, NULL, NULL) == RG_OK);
    bin = Serialise(&w, &r, &size);
    CHECK(size == 8 + 12 * 442 + 325479);
    EnterTemp();
    WriteBin(bin, size);
    CHECK(VoxelRegions_Init());
    CHECK(CompareAll(&w, &r) == 325479);
    VoxelRegions_Shutdown();
    LeaveTemp();
    printf("real ROM: regions.bin %zu bytes, 442 layouts, 325479 cells all agree through voxel_regions.c\n", size);
    free(bin);
    rg_roles_free(&r);
    rg_world_close(&w);
    free(rom);
}

int main(void)
{
    TestSynthetic();
    TestRealRom();
    printf("test_romgen_regions: %d checks, %d failures\n", sChecks, sFails);
    return sFails ? 1 : 0;
}
