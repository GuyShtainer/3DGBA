/* ctr_shims_pure.c -- the libctru-free half of the voxel platform shims (3DGBA, GPLv3).
 * Builds on the host. See ctr_shims.h. */
#include "ctr_shims.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---- log ---------------------------------------------------------------------------------- */
static VxLogSink sLogSink;

void vx_log_set_sink(VxLogSink sink)
{
    sLogSink = sink;
}

void CtrLog_Write(int channel, const char *fmt, ...)
{
    char line[256];
    va_list ap;

    if (sLogSink == NULL || fmt == NULL)
        return;
    va_start(ap, fmt);
    (void)vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    sLogSink(channel, line);
}

/* ---- texel maths -------------------------------------------------------------------------- */
/* Spread the low three bits of v onto the even bit positions: abc -> 0a0b0c. */
static uint32_t Spread3(uint32_t v)
{
    return (v & 1u) | ((v & 2u) << 1) | ((v & 4u) << 2);
}

uint32_t CtrVideo_Texel(unsigned x, unsigned y, unsigned width)
{
    uint32_t tile = (uint32_t)(y >> 3) * (uint32_t)(width >> 3) + (uint32_t)(x >> 3);

    return tile * 64u + (Spread3(x & 7u) | (Spread3(y & 7u) << 1));
}

uint16_t CtrVideo_RGBA5551(uint16_t bgr555)
{
    uint16_t r = (uint16_t)(bgr555 & 31u);
    uint16_t g = (uint16_t)((bgr555 >> 5) & 31u);
    uint16_t b = (uint16_t)((bgr555 >> 10) & 31u);

    return (uint16_t)((r << 11) | (g << 6) | (b << 1) | 1u);
}

unsigned CtrVideo_ObjTile(unsigned base, unsigned x, unsigned y, unsigned width, bool color256,
                          bool mapping1d)
{
    unsigned stride = mapping1d ? width / 8u : 32u;
    unsigned unit = color256 ? 2u : 1u;
    unsigned first = color256 ? (base & ~1u) : base;

    return (first + (y * stride + x) * unit) & 1023u;
}

/* ---- GX command budget ---------------------------------------------------------------------- */
static unsigned (*sProbe)(void);
static unsigned sVoxCmds;

void vx_gx_set_probe(unsigned (*probe)(void))
{
    sProbe = probe;
}

void vx_gx_frame_begin(void)
{
    sVoxCmds = 0;
}

static unsigned UsedNow(void)
{
    unsigned live = sProbe != NULL ? sProbe() : 0u;

    return live > sVoxCmds ? live : sVoxCmds;
}

unsigned vx_gx_budget_left(unsigned voxCmds, unsigned queueUsed, unsigned reserve)
{
    unsigned used = queueUsed > voxCmds ? queueUsed : voxCmds;

    if (used + reserve >= VX_GX_QUEUE_ENTRIES)
        return 0;
    return VX_GX_QUEUE_ENTRIES - used - reserve;
}

bool vx_gx_budget_try(unsigned *voxCmds, unsigned queueUsed, unsigned reserve)
{
    unsigned used;

    if (voxCmds == NULL)
        return false;
    used = queueUsed > *voxCmds ? queueUsed : *voxCmds;
    if (used + VX_GX_UPLOAD_COST + reserve > VX_GX_QUEUE_ENTRIES)
        return false;
    *voxCmds = used + VX_GX_UPLOAD_COST;
    return true;
}

bool CtrVideo_TryVoxelUpload(void)
{
    unsigned used = UsedNow();

    if (used + VX_GX_UPLOAD_COST + VX_GX_RESERVE > VX_GX_QUEUE_ENTRIES)
        return false;
    sVoxCmds = used + VX_GX_UPLOAD_COST;
    return true;
}

unsigned CtrVideo_VoxelUploadsLeft(void)
{
    return vx_gx_budget_left(sVoxCmds, UsedNow(), VX_GX_RESERVE);
}

/* ---- atlas memory policy -------------------------------------------------------------------- */
unsigned vx_atlas_mem_allowed(unsigned allocated, unsigned cap, unsigned long linearFree,
                              unsigned long pageBytes, unsigned long linearReserve)
{
    unsigned allowed = 0;

    if (allocated >= cap)
        return 0;
    allowed |= VX_ATLAS_MEM_VRAM;
    if (linearFree >= pageBytes && linearFree - pageBytes >= linearReserve)
        allowed |= VX_ATLAS_MEM_LINEAR;
    return allowed;
}

/* ---- video odds and ends ---------------------------------------------------------------------- */
static CtrVideoStats sVideoStats;
static const uint8_t *sBgVram;
static unsigned sPlaneReleaseLogged;

void CtrVideo_RequestPlaneRelease(void)
{
    /* No 3D depth planes in this build: nothing to free. Logged once. */
    if (sPlaneReleaseLogged++ == 0)
        CtrLog_Write(CTR_LOG_VIDEO, "VOX: atlas VRAM blocked; no compositor planes to release");
}

void CtrVideo_Present(void)
{
    /* Never reached: the call sites are in battle/warm-up paths this build does not enter. */
}

const CtrVideoStats *CtrVideo_GetStats(void)
{
    return &sVideoStats;
}

void vx_video_set_bg_vram(const uint8_t *bg32k)
{
    sBgVram = bg32k;
}

const uint8_t *CtrVideo_GetBgVram(void)
{
    static const uint8_t sZero[1];

    return sBgVram != NULL ? sBgVram : sZero;
}

/* ---- assets: inert ------------------------------------------------------------------------------ */
static const struct CtrAssetStats sAssetStats;

bool CtrAssets_PrefetchFind(const void *ptr, CtrAssetPrefetch *out)
{
    (void)ptr;
    (void)out;
    return false;
}

void *CtrAssets_PrefetchRead(const CtrAssetPrefetch *request)
{
    (void)request;
    return NULL;
}

void CtrAssets_PrefetchAdopt(const CtrAssetPrefetch *request, void *data)
{
    (void)request;
    (void)data;
}

const struct CtrAssetStats *CtrAssets_GetStats(void)
{
    return &sAssetStats;
}

/* ---- timing and settings ------------------------------------------------------------------------ */
static CtrTiming sTiming = {16.7f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0};

const CtrTiming *CtrPlatform_GetTiming(void)
{
    return &sTiming;
}

CtrTiming *vx_timing_mut(void)
{
    return &sTiming;
}

static int sPitch = 40, sZoom = 100;

void vx_settings_set(int pitchDegrees, int zoomPercent)
{
    sPitch = pitchDegrees;
    sZoom = zoomPercent > 0 ? zoomPercent : 100;
}

int CtrSettings_VoxelPitch(void)
{
    return sPitch;
}

int CtrSettings_VoxelZoom(void)
{
    return sZoom;
}
