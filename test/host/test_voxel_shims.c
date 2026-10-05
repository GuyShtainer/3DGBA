// test_voxel_shims.c -- host test for the pure shims (ctr_shims_pure.c) and the voxel data layer
// (vx_data.c: hashes, pak reader, loose fallback). Phase 32 P2, SPEC-port section 9.2.
//
//   clang -std=c11 -Wall -Wextra -O2 -I source/voxel test/host/test_voxel_shims.c \
//         source/voxel/ctr_shims_pure.c source/voxel/vx_data.c -o /tmp/tvsh && /tmp/tvsh
//
// Texel order vs a brute-force Morton reference over the whole 512x256 plane, RGBA5551 over all 32768
// colours, ObjTile corners, the GX budget arithmetic, FNV-1a/CRC-32/SHA-1 known vectors, and a pak
// built in-process (good file; every header field, the index, ordering, flags, bounds and the ROM
// SHA-1 corrupted one at a time; loose fallback order).
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ctr_shims.h"
#include "vx_data.h"

static int sChecks, sFails;
#define CHECK(c) do { ++sChecks; if (!(c)) { ++sFails; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

/* ---------------------------------------------------------------- texel / colour / obj tile */
static uint32_t MortonRef(unsigned x, unsigned y, unsigned w)
{
    uint32_t tile = 0, in = 0;

    for (unsigned b = 0; b < 3; ++b)
    {
        in |= ((x >> b) & 1u) << (2 * b);
        in |= ((y >> b) & 1u) << (2 * b + 1);
    }
    tile = (y / 8) * (w / 8) + (x / 8);
    return tile * 64 + in;
}

static void TestTexel(void)
{
    static uint8_t seen[512 * 256];
    int bad = 0;

    for (unsigned y = 0; y < 256; ++y)
        for (unsigned x = 0; x < 512; ++x)
        {
            uint32_t t = CtrVideo_Texel(x, y, 512);
            if (t != MortonRef(x, y, 512) || t >= 512u * 256u || seen[t]++)
                ++bad;
        }
    CHECK(bad == 0);
    CHECK(CtrVideo_Texel(0, 0, 64) == 0);
    CHECK(CtrVideo_Texel(1, 0, 64) == 1);
    CHECK(CtrVideo_Texel(0, 1, 64) == 2);
    CHECK(CtrVideo_Texel(8, 0, 64) == 64);
    CHECK(CtrVideo_Texel(0, 8, 64) == 8 * 64);
    CHECK(CtrVideo_Texel(63, 63, 64) == 4095);
}

static void TestRgba(void)
{
    int bad = 0;

    for (uint32_t c = 0; c < 32768; ++c)
    {
        uint16_t want = (uint16_t)((((c >> 0) & 31) << 11) | (((c >> 5) & 31) << 6) | (((c >> 10) & 31) << 1) | 1);
        if (CtrVideo_RGBA5551((uint16_t)c) != want)
            ++bad;
    }
    CHECK(bad == 0);
}

static void TestObjTile(void)
{
    /* 1D mapping: row stride = width/8 tiles. */
    CHECK(CtrVideo_ObjTile(100, 0, 0, 32, false, true) == 100);
    CHECK(CtrVideo_ObjTile(100, 3, 0, 32, false, true) == 103);
    CHECK(CtrVideo_ObjTile(100, 0, 1, 32, false, true) == 104);
    CHECK(CtrVideo_ObjTile(100, 3, 3, 32, false, true) == 100 + 3 * 4 + 3);
    /* 2D mapping: row stride 32 tiles. */
    CHECK(CtrVideo_ObjTile(100, 0, 1, 32, false, false) == 132);
    CHECK(CtrVideo_ObjTile(100, 2, 2, 32, false, false) == 100 + 2 * 32 + 2);
    /* 8bpp: tile pairs, even base, and the 1024-tile wrap. */
    CHECK(CtrVideo_ObjTile(101, 0, 0, 16, true, true) == 100);
    CHECK(CtrVideo_ObjTile(100, 1, 0, 16, true, true) == 102);
    CHECK(CtrVideo_ObjTile(100, 0, 1, 16, true, true) == 100 + 2 * 2);
    CHECK(CtrVideo_ObjTile(1020, 3, 0, 32, false, true) == (1020 + 3) % 1024);
    CHECK(CtrVideo_ObjTile(1022, 4, 0, 64, false, true) == (1022 + 4) % 1024);
}

/* ---------------------------------------------------------------- GX budget */
static void TestBudget(void)
{
    unsigned vox = 0, n = 0;

    CHECK(vx_gx_budget_left(0, 0, 16) == 16);
    CHECK(vx_gx_budget_left(0, 20, 16) == 0);
    CHECK(vx_gx_budget_left(5, 14, 16) == 2);
    CHECK(vx_gx_budget_try(&vox, 0, 16) && vox == 2);
    while (vx_gx_budget_try(&vox, 0, 16)) ++n;
    CHECK(vox <= 32 - 16 && n == 7);               /* 8 uploads of 2 fit before the reserve */
    CHECK(!vx_gx_budget_try(&vox, 0, 16));
    vox = 0;
    CHECK(!vx_gx_budget_try(&vox, 15, 16));         /* 15 + 2 + 16 = 33 > 32 refuses */
    CHECK(vx_gx_budget_try(&vox, 14, 16) && vox == 16); /* 14 + 2 + 16 = 32 fits exactly */
    CHECK(!vx_gx_budget_try(NULL, 0, 16));
    /* the global counter */
    vx_gx_set_probe(NULL);
    vx_gx_frame_begin();
    n = 0;
    while (CtrVideo_TryVoxelUpload()) ++n;
    CHECK(n == 8 && CtrVideo_VoxelUploadsLeft() == 0);
    vx_gx_frame_begin();
    CHECK(CtrVideo_VoxelUploadsLeft() == 16);
}

/* ---------------------------------------------------------------- hashes */
static void TestHashes(void)
{
    uint8_t d[20];
    static const uint8_t abc[20] = {0xa9,0x99,0x3e,0x36,0x47,0x06,0x81,0x6a,0xba,0x3e,0x25,0x71,0x78,0x50,0xc2,0x6c,0x9c,0xd0,0xd8,0x9d};
    static const uint8_t empty[20] = {0xda,0x39,0xa3,0xee,0x5e,0x6b,0x4b,0x0d,0x32,0x55,0xbf,0xef,0x95,0x60,0x18,0x90,0xaf,0xd8,0x07,0x09};
    static const uint8_t mil[20] = {0x34,0xaa,0x97,0x3c,0xd4,0xc4,0xda,0xa4,0xf6,0x1e,0xeb,0x2b,0xdb,0xad,0x27,0x31,0x65,0x34,0x01,0x6f};
    char *big = malloc(1000000);

    CHECK(vx_crc32("123456789", 9) == 0xCBF43926u);
    CHECK(vx_crc32("", 0) == 0);
    CHECK(vx_fnv1a64("") == 0xcbf29ce484222325ull);
    CHECK(vx_fnv1a64("a") == 0xaf63dc4c8601ec8cull);
    CHECK(vx_fnv1a64("foobar") == 0x85944171f73967e8ull);
    vx_sha1("abc", 3, d); CHECK(memcmp(d, abc, 20) == 0);
    vx_sha1("", 0, d); CHECK(memcmp(d, empty, 20) == 0);
    memset(big, 'a', 1000000);
    vx_sha1(big, 1000000, d); CHECK(memcmp(d, mil, 20) == 0);
    {   /* padding boundaries 55/56/63/64/65 against a second implementation: self-consistency of
         * the incremental-free API is covered by the vectors above; here only that lengths differ */
        uint8_t a[20], b[20];
        vx_sha1(big, 55, a); vx_sha1(big, 56, b); CHECK(memcmp(a, b, 20) != 0);
        vx_sha1(big, 63, a); vx_sha1(big, 64, b); CHECK(memcmp(a, b, 20) != 0);
        vx_sha1(big, 64, a); vx_sha1(big, 65, b); CHECK(memcmp(a, b, 20) != 0);
    }
    free(big);
}

/* ---------------------------------------------------------------- pak */
typedef struct { const char *path; const char *body; } Ent;

static void Put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i)); }
static void Put64(uint8_t *p, uint64_t v) { for (int i = 0; i < 8; ++i) p[i] = (uint8_t)(v >> (8 * i)); }

typedef struct
{
    uint8_t *buf;
    size_t size;
    uint64_t indexOff, dataOff;
    unsigned count;
} Pak;

static int CmpId(const void *a, const void *b)
{
    uint64_t x = vx_fnv1a64(((const Ent *)a)->path), y = vx_fnv1a64(((const Ent *)b)->path);
    return x < y ? -1 : x > y;
}

static Pak BuildPak(const Ent *in, unsigned n, const uint8_t sha[20])
{
    Ent e[8];
    Pak p;
    size_t dataBytes = 0, off;
    uint8_t *idx;

    memcpy(e, in, n * sizeof(Ent));
    qsort(e, n, sizeof(Ent), CmpId);
    for (unsigned i = 0; i < n; ++i) dataBytes += strlen(e[i].body);
    p.count = n;
    p.indexOff = 64;
    p.dataOff = 64 + 40u * n;
    p.size = (size_t)p.dataOff + dataBytes;
    p.buf = calloc(1, p.size);
    memcpy(p.buf, "EM3DPAK\0", 8);
    Put32(p.buf + 8, 1);
    Put32(p.buf + 12, 7);
    memcpy(p.buf + 16, sha, 20);
    Put32(p.buf + 36, n);
    Put64(p.buf + 40, p.indexOff);
    Put64(p.buf + 48, p.dataOff);
    idx = p.buf + 64;
    off = (size_t)p.dataOff;
    for (unsigned i = 0; i < n; ++i)
    {
        size_t len = strlen(e[i].body);
        uint8_t *x = idx + 40u * i;
        Put64(x, vx_fnv1a64(e[i].path));
        Put64(x + 16, off);
        Put32(x + 24, (uint32_t)len);
        Put32(x + 28, (uint32_t)len);
        Put32(x + 32, vx_crc32(e[i].body, len));
        memcpy(p.buf + off, e[i].body, len);
        off += len;
    }
    Put32(p.buf + 56, vx_crc32(idx, 40u * n));
    Put32(p.buf + 60, vx_crc32(p.buf, 60));
    return p;
}

static void Reseal(Pak *p)
{
    Put32(p->buf + 56, vx_crc32(p->buf + p->indexOff, 40u * p->count));
    Put32(p->buf + 60, vx_crc32(p->buf, 60));
}

static void Write(const char *path, const void *d, size_t n)
{
    FILE *f = fopen(path, "wb");
    fwrite(d, 1, n, f);
    fclose(f);
}

static char sDir[128], sPakPath[160], sLoose[160];

static int ReadAll(FILE *f, char *out, size_t cap)
{
    size_t n;
    if (f == NULL) return -1;
    n = fread(out, 1, cap - 1, f);
    out[n] = 0;
    fclose(f);
    return (int)n;
}

static void Reset(const uint8_t *sha)
{
    vx_data_set_paths(sPakPath, sLoose);
    vx_data_set_rom_sha1(sha);
}

static void TestPak(void)
{
    static const Ent ents[] = {{"voxel/relief.bin", "RELIEFDATA"}, {"voxel/regions.bin", "regions-bytes!"},
                               {"voxel/buildings.bin", "B"}};
    uint8_t sha[20], other[20];
    char buf[64];
    Pak p;

    for (int i = 0; i < 20; ++i) { sha[i] = (uint8_t)(i * 7 + 1); other[i] = (uint8_t)(i * 7 + 2); }
    snprintf(sDir, sizeof sDir, "/tmp/tvsh.%d", (int)getpid());
    snprintf(sPakPath, sizeof sPakPath, "%s.pak", sDir);
    snprintf(sLoose, sizeof sLoose, "%s.loose", sDir);
    { char c[200]; snprintf(c, sizeof c, "rm -rf %s && mkdir -p %s", sLoose, sLoose); CHECK(system(c) == 0); }

    /* nothing at all */
    Reset(sha);
    CHECK(CtrData_Open("voxel/relief.bin") == NULL && vx_data_status() == VXD_NONE);

    /* good pak */
    p = BuildPak(ents, 3, sha);
    Write(sPakPath, p.buf, p.size);
    Reset(sha);
    CHECK(ReadAll(CtrData_Open("voxel/relief.bin"), buf, sizeof buf) == 10 && strcmp(buf, "RELIEFDATA") == 0);
    CHECK(vx_data_status() == VXD_OK_PAK);
    CHECK(ReadAll(CtrData_Open("voxel/regions.bin"), buf, sizeof buf) == 14 && strcmp(buf, "regions-bytes!") == 0);
    CHECK(ReadAll(CtrData_Open("voxel/buildings.bin"), buf, sizeof buf) == 1 && buf[0] == 'B');
    {   /* seek + read stay inside the entry window */
        FILE *f = CtrData_Open("voxel/regions.bin");
        CHECK(f != NULL && fseek(f, 8, SEEK_SET) == 0 && fread(buf, 1, 32, f) == 6 && memcmp(buf, "bytes!", 6) == 0);
        CHECK(fseek(f, 0, SEEK_END) == 0 && ftell(f) == 14);
        fclose(f);
    }
    CHECK(CtrData_Open("voxel/missing.bin") == NULL);

    /* no ROM hash supplied: the SHA check is skipped */
    Reset(NULL);
    CHECK(ReadAll(CtrData_Open("voxel/relief.bin"), buf, sizeof buf) == 10);

    /* ROM mismatch */
    Reset(other);
    CHECK(CtrData_Open("voxel/relief.bin") == NULL && vx_data_status() == VXD_WRONG_ROM);

    /* corrupted header fields, one at a time (header CRC re-sealed where the field is not the CRC) */
    static const struct { size_t off; const char *what; } hdr[] = {{0, "magic"}, {8, "schema"}, {36, "count"}, {40, "indexOff"}};
    for (unsigned i = 0; i < sizeof(hdr) / sizeof(hdr[0]); ++i)
    {
        Pak q = BuildPak(ents, 3, sha);
        q.buf[hdr[i].off] ^= 0x55;
        if (i != 0) Put32(q.buf + 60, vx_crc32(q.buf, 60)); /* magic stays caught by the magic check */
        Write(sPakPath, q.buf, q.size);
        Reset(sha);
        CHECK(CtrData_Open("voxel/relief.bin") == NULL && vx_data_status() == VXD_BAD_PAK);
        free(q.buf);
    }
    {   Pak q = BuildPak(ents, 3, sha);                 /* header CRC itself */
        q.buf[60] ^= 1;
        Write(sPakPath, q.buf, q.size); Reset(sha);
        CHECK(CtrData_Open("voxel/relief.bin") == NULL && vx_data_status() == VXD_BAD_PAK);
        free(q.buf); }
    {   Pak q = BuildPak(ents, 3, sha);                 /* index byte flipped, CRC stale */
        q.buf[64 + 17] ^= 1;
        Write(sPakPath, q.buf, q.size); Reset(sha);
        CHECK(CtrData_Open("voxel/relief.bin") == NULL && vx_data_status() == VXD_BAD_PAK);
        free(q.buf); }
    {   Pak q = BuildPak(ents, 3, sha);                 /* ids out of order, CRCs re-sealed */
        uint8_t t[40];
        memcpy(t, q.buf + 64, 40); memcpy(q.buf + 64, q.buf + 104, 40); memcpy(q.buf + 104, t, 40);
        Reseal(&q);
        Write(sPakPath, q.buf, q.size); Reset(sha);
        CHECK(CtrData_Open("voxel/relief.bin") == NULL && vx_data_status() == VXD_BAD_PAK);
        free(q.buf); }
    {   Pak q = BuildPak(ents, 3, sha);                 /* every entry compressed-flagged */
        for (int k = 0; k < 3; ++k) q.buf[64 + 40 * k + 12] = 1;
        Reseal(&q);
        Write(sPakPath, q.buf, q.size); Reset(sha);
        CHECK(CtrData_Open("voxel/relief.bin") == NULL && vx_data_status() == VXD_BAD_PAK);
        CHECK(CtrData_Open("voxel/regions.bin") == NULL);
        free(q.buf); }
    {   Pak q = BuildPak(ents, 3, sha);                 /* entry runs past the end of the file */
        for (int k = 0; k < 3; ++k) Put32(q.buf + 64 + 40 * k + 24, 5000), Put32(q.buf + 64 + 40 * k + 28, 5000);
        Reseal(&q);
        Write(sPakPath, q.buf, q.size); Reset(sha);
        CHECK(CtrData_Open("voxel/relief.bin") == NULL && vx_data_status() == VXD_BAD_PAK);
        free(q.buf); }
    {   Pak q = BuildPak(ents, 3, sha);                 /* payload byte flipped: entry CRC */
        q.buf[q.size - 1] ^= 1;
        Write(sPakPath, q.buf, q.size); Reset(sha);
        int hit = 0;
        const char *names[] = {"voxel/relief.bin", "voxel/regions.bin", "voxel/buildings.bin"};
        for (int k = 0; k < 3; ++k) hit += CtrData_Open(names[k]) == NULL;
        CHECK(hit == 1);                                /* exactly the last entry fails its CRC */
        free(q.buf); }
    {   Pak q = BuildPak(ents, 3, sha);                 /* truncated file */
        Write(sPakPath, q.buf, 40); Reset(sha);
        CHECK(CtrData_Open("voxel/relief.bin") == NULL && vx_data_status() == VXD_BAD_PAK);
        free(q.buf); }

    /* loose fallback: a bad pak falls through to loose files; order is pak first when good */
    {   char lp[200];
        snprintf(lp, sizeof lp, "%s/relief.bin", sLoose);
        Write(lp, "LOOSE", 5);
        Reset(sha);                                     /* the pak on disk is the truncated one */
        CHECK(ReadAll(CtrData_Open("voxel/relief.bin"), buf, sizeof buf) == 5 && strcmp(buf, "LOOSE") == 0);
        CHECK(vx_data_status() == VXD_BAD_PAK);          /* damage stays reported */
        Write(sPakPath, p.buf, p.size);
        Reset(sha);
        CHECK(ReadAll(CtrData_Open("voxel/relief.bin"), buf, sizeof buf) == 10 && strcmp(buf, "RELIEFDATA") == 0);
        remove(sPakPath);                                /* no pak: loose wins */
        Reset(sha);
        CHECK(ReadAll(CtrData_Open("voxel/relief.bin"), buf, sizeof buf) == 5 && vx_data_status() == VXD_OK_LOOSE);
        CHECK(CtrData_Open("voxel/regions.bin") == NULL);
        remove(lp);
    }
    free(p.buf);
    { char c[200]; snprintf(c, sizeof c, "rm -rf %s", sLoose); (void)!system(c); }
}

int main(void)
{
    TestTexel();
    TestRgba();
    TestObjTile();
    TestBudget();
    TestHashes();
    TestPak();
    printf("test_voxel_shims: %d checks, %d failures\n", sChecks, sFails);
    return sFails != 0;
}
