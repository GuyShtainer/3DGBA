/* vx_data.c -- pak v0.2.0 reader + loose fallback (3DGBA, GPLv3). Written from the format
 * description in SPEC-port 4.5; no upstream code. */
#define _GNU_SOURCE
#include "vx_data.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define PAK_HEADER_BYTES 64u
#define PAK_ENTRY_BYTES 40u
#define PAK_MAX_ENTRIES (1u << 20)
#define PAK_CRC_LIMIT (1u << 20)

static char sPakPath[256] = "sdmc:/3ds/emerald3ds/emerald3ds.pak";
static char sLooseDir[256] = "sdmc:/3ds/3DGBA/voxel";
static uint8_t sRomSha1[20];
static bool sHaveSha1;
static VxDataStatus sStatus = VXD_NONE;
static bool sPakChecked;
static FILE *sPak;
static uint64_t sPakSize;
static uint8_t *sIndex;
static uint32_t sCount;
static uint64_t sDataOffset;

/* ---- hashes ------------------------------------------------------------------------------- */
uint32_t vx_crc32(const void *data, size_t len)
{
    const uint8_t *p = data;
    uint32_t crc = 0xFFFFFFFFu;

    for (size_t i = 0; i < len; ++i)
    {
        crc ^= p[i];
        for (int k = 0; k < 8; ++k)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

uint64_t vx_fnv1a64(const char *s)
{
    uint64_t h = 0xcbf29ce484222325ull;

    for (; *s != '\0'; ++s)
        h = (h ^ (uint8_t)*s) * 0x100000001b3ull;
    return h;
}

static uint32_t Rol(uint32_t v, unsigned n)
{
    return (v << n) | (v >> (32u - n));
}

static void Sha1Block(uint32_t h[5], const uint8_t *b)
{
    uint32_t w[80], a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4];

    for (unsigned i = 0; i < 16; ++i)
        w[i] = ((uint32_t)b[i * 4] << 24) | ((uint32_t)b[i * 4 + 1] << 16)
             | ((uint32_t)b[i * 4 + 2] << 8) | b[i * 4 + 3];
    for (unsigned i = 16; i < 80; ++i)
        w[i] = Rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    for (unsigned i = 0; i < 80; ++i)
    {
        uint32_t f, k, t;

        if (i < 20) { f = (bb & c) | (~bb & d); k = 0x5A827999u; }
        else if (i < 40) { f = bb ^ c ^ d; k = 0x6ED9EBA1u; }
        else if (i < 60) { f = (bb & c) | (bb & d) | (c & d); k = 0x8F1BBCDCu; }
        else { f = bb ^ c ^ d; k = 0xCA62C1D6u; }
        t = Rol(a, 5) + f + e + k + w[i];
        e = d; d = c; c = Rol(bb, 30); bb = a; a = t;
    }
    h[0] += a; h[1] += bb; h[2] += c; h[3] += d; h[4] += e;
}

void vx_sha1(const void *data, size_t len, uint8_t out[20])
{
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    const uint8_t *p = data;
    uint8_t tail[128];
    size_t full = len / 64u, rem = len % 64u, tailLen;
    uint64_t bits = (uint64_t)len * 8u;

    for (size_t i = 0; i < full; ++i)
        Sha1Block(h, p + i * 64u);
    memset(tail, 0, sizeof(tail));
    memcpy(tail, p + full * 64u, rem);
    tail[rem] = 0x80;
    tailLen = rem < 56u ? 64u : 128u;
    for (unsigned i = 0; i < 8; ++i)
        tail[tailLen - 1u - i] = (uint8_t)(bits >> (8u * i));
    Sha1Block(h, tail);
    if (tailLen == 128u)
        Sha1Block(h, tail + 64);
    for (unsigned i = 0; i < 5; ++i)
        for (unsigned k = 0; k < 4; ++k)
            out[i * 4 + k] = (uint8_t)(h[i] >> (24u - 8u * k));
}

/* ---- little-endian field reads ---------------------------------------------------------------- */
static uint32_t Rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t Rd64(const uint8_t *p)
{
    return (uint64_t)Rd32(p) | ((uint64_t)Rd32(p + 4) << 32);
}

/* ---- pak -------------------------------------------------------------------------------------- */
static void PakClose(void)
{
    if (sPak != NULL)
        fclose(sPak);
    sPak = NULL;
    free(sIndex);
    sIndex = NULL;
    sCount = 0;
    sPakChecked = false;
}

void vx_data_set_paths(const char *pakPath, const char *looseDir)
{
    PakClose();
    sStatus = VXD_NONE;
    if (pakPath != NULL)
    {
        strncpy(sPakPath, pakPath, sizeof(sPakPath) - 1u);
        sPakPath[sizeof(sPakPath) - 1u] = '\0';
    }
    if (looseDir != NULL)
    {
        strncpy(sLooseDir, looseDir, sizeof(sLooseDir) - 1u);
        sLooseDir[sizeof(sLooseDir) - 1u] = '\0';
    }
}

void vx_data_set_rom_sha1(const uint8_t sha1[20])
{
    PakClose();
    sStatus = VXD_NONE;
    sHaveSha1 = sha1 != NULL;
    if (sha1 != NULL)
        memcpy(sRomSha1, sha1, 20);
}

VxDataStatus vx_data_status(void)
{
    return sStatus;
}

/* Opens and validates the pak once. Returns true when entries can be looked up. */
static bool PakOpen(void)
{
    uint8_t hdr[PAK_HEADER_BYTES];
    uint64_t indexOff;
    size_t indexBytes;

    if (sPakChecked)
        return sPak != NULL;
    sPakChecked = true;
    sPak = fopen(sPakPath, "rb");
    if (sPak == NULL)
        return false;
    if (fseek(sPak, 0, SEEK_END) != 0 || fread(hdr, 0, 0, sPak) != 0)
        goto bad;
    sPakSize = (uint64_t)ftell(sPak);
    rewind(sPak);
    if (fread(hdr, 1, sizeof(hdr), sPak) != sizeof(hdr) || memcmp(hdr, "EM3DPAK\0", 8) != 0
     || vx_crc32(hdr, 60) != Rd32(hdr + 60) || Rd32(hdr + 8) != 1u)
        goto bad;
    if (sHaveSha1 && memcmp(hdr + 16, sRomSha1, 20) != 0)
    {
        PakClose();
        sPakChecked = true;
        sStatus = VXD_WRONG_ROM;
        return false;
    }
    sCount = Rd32(hdr + 36);
    indexOff = Rd64(hdr + 40);
    sDataOffset = Rd64(hdr + 48);
    if (sCount < 1u || sCount > PAK_MAX_ENTRIES || indexOff < PAK_HEADER_BYTES
     || indexOff > sPakSize || (uint64_t)sCount * PAK_ENTRY_BYTES > sPakSize - indexOff)
        goto bad;
    indexBytes = (size_t)sCount * PAK_ENTRY_BYTES;
    sIndex = malloc(indexBytes);
    if (sIndex == NULL || fseek(sPak, (long)indexOff, SEEK_SET) != 0
     || fread(sIndex, 1, indexBytes, sPak) != indexBytes || vx_crc32(sIndex, indexBytes) != Rd32(hdr + 56))
        goto bad;
    for (uint32_t i = 1; i < sCount; ++i)
        if (Rd64(sIndex + (size_t)i * PAK_ENTRY_BYTES) <= Rd64(sIndex + (size_t)(i - 1u) * PAK_ENTRY_BYTES))
            goto bad;
    return true;
bad:
    PakClose();
    sPakChecked = true;
    sStatus = VXD_BAD_PAK;
    return false;
}

/* Cookie for the FILE* window over one pak entry. */
typedef struct
{
    FILE *file;
    uint64_t base;
    uint64_t size;
    uint64_t pos;
} Window;

static ssize_t WinRead(void *cookie, char *buf, size_t n)
{
    Window *w = cookie;
    size_t want = n < w->size - w->pos ? n : (size_t)(w->size - w->pos);
    size_t got;

    if (want == 0)
        return 0;
    if (fseek(w->file, (long)(w->base + w->pos), SEEK_SET) != 0)
        return -1;
    got = fread(buf, 1, want, w->file);
    w->pos += got;
    return (ssize_t)got;
}

static int WinClose(void *cookie)
{
    free(cookie);
    return 0;
}

#ifdef __APPLE__
static int WinReadFn(void *c, char *b, int n)
{
    return (int)WinRead(c, b, (size_t)n);
}

static fpos_t WinSeekFn(void *c, fpos_t off, int whence)
{
    Window *w = c;
    int64_t np = whence == SEEK_SET ? off : whence == SEEK_CUR ? (int64_t)w->pos + off : (int64_t)w->size + off;

    if (np < 0 || (uint64_t)np > w->size)
        return -1;
    w->pos = (uint64_t)np;
    return np;
}
#else
static int WinSeek(void *c, off_t *off, int whence)
{
    Window *w = c;
    int64_t np = whence == SEEK_SET ? *off : whence == SEEK_CUR ? (int64_t)w->pos + *off : (int64_t)w->size + *off;

    if (np < 0 || (uint64_t)np > w->size)
        return -1;
    w->pos = (uint64_t)np;
    *off = (off_t)np;
    return 0;
}
#endif

static FILE *PakEntryOpen(const char *path)
{
    uint64_t id = vx_fnv1a64(path);
    uint32_t lo = 0, hi = sCount;
    const uint8_t *e = NULL;
    uint64_t off;
    uint32_t stored, raw, crc;
    Window *w;
    FILE *f;

    while (lo < hi)
    {
        uint32_t mid = lo + (hi - lo) / 2u;
        const uint8_t *cand = sIndex + (size_t)mid * PAK_ENTRY_BYTES;
        uint64_t cid = Rd64(cand);

        if (cid == id) { e = cand; break; }
        if (cid < id) lo = mid + 1u; else hi = mid;
    }
    if (e == NULL)
        return NULL;
    off = Rd64(e + 16);
    stored = Rd32(e + 24);
    raw = Rd32(e + 28);
    crc = Rd32(e + 32);
    if ((Rd32(e + 12) & 1u) != 0 || stored != raw || off < sDataOffset || off > sPakSize
     || raw > sPakSize - off)
    {
        sStatus = VXD_BAD_PAK;
        return NULL;
    }
    if (raw <= PAK_CRC_LIMIT)
    {
        uint8_t *tmp = malloc(raw != 0 ? raw : 1u);
        bool ok = tmp != NULL && fseek(sPak, (long)off, SEEK_SET) == 0
               && fread(tmp, 1, raw, sPak) == raw && vx_crc32(tmp, raw) == crc;

        free(tmp);
        if (!ok)
        {
            sStatus = VXD_BAD_PAK;
            return NULL;
        }
    }
    w = calloc(1, sizeof(*w));
    if (w == NULL)
        return NULL;
    w->file = sPak;
    w->base = off;
    w->size = raw;
#ifdef __APPLE__
    f = funopen(w, WinReadFn, NULL, WinSeekFn, WinClose);
#else
    {
        cookie_io_functions_t fn = {WinRead, NULL, WinSeek, WinClose};

        f = fopencookie(w, "rb", fn);
    }
#endif
    if (f == NULL)
        free(w);
    return f;
}

FILE *CtrData_Open(const char *path)
{
    char loose[320];
    const char *base;
    FILE *f;

    if (path == NULL)
        return NULL;
    if (PakOpen())
    {
        f = PakEntryOpen(path);
        if (f != NULL)
        {
            sStatus = VXD_OK_PAK;
            return f;
        }
    }
    base = strrchr(path, '/');
    base = base != NULL ? base + 1 : path;
    if (snprintf(loose, sizeof(loose), "%s/%s", sLooseDir, base) >= (int)sizeof(loose))
        return NULL;
    f = fopen(loose, "rb");
    if (f != NULL && sStatus != VXD_BAD_PAK && sStatus != VXD_WRONG_ROM)
        sStatus = VXD_OK_LOOSE;
    return f;
}
