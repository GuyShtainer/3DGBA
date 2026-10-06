/* ctr_shims.h -- the platform symbols the vendored voxel files call (3DGBA, GPLv3).
 *
 * Our own code, written against the call sites in the vendored files and SPEC-port section 4.
 * Upstream's implementations of these symbols are not vendored. The pure parts (texel index, RGBA
 * packing, OBJ tile maths, GX budget, log) live in ctr_shims_pure.c and build on the host; the
 * libctru parts (queue probe, timing) live in ctr_shims.c. */
#ifndef VX_CTR_SHIMS_H
#define VX_CTR_SHIMS_H

#include <stdbool.h>
#include <stdint.h>

#ifndef CTR_VOXEL_ENABLED
#define CTR_VOXEL_ENABLED 1
#endif
/* CTR_VOXEL_LIGHTING is set by the Makefile / host test build lines (-DCTR_VOXEL_LIGHTING=1). */

/* The logical surface the vendored renderer draws to: the top screen. */
#define CTR_GAME_WIDTH 400
#define CTR_GAME_HEIGHT 240
/* Battle scenery zoom (GBA pixel -> screen pixel). Only the battle path reads it; unused here. */
#define CTR_BATTLE_ZOOM (400.0f / 240.0f)

/* ---- log ----------------------------------------------------------------------------------- */
enum
{
    CTR_LOG_ERROR = 0,
    CTR_LOG_VIDEO,
    CTR_LOG_FS,
    CTR_LOG_VOXEL
};
typedef void (*VxLogSink)(int channel, const char *line);
void vx_log_set_sink(VxLogSink sink);
void CtrLog_Write(int channel, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#define PORT_LOG(...) CtrLog_Write(CTR_LOG_VOXEL, __VA_ARGS__)

/* ---- video ----------------------------------------------------------------------------------- */
typedef struct
{
    float cpuMs, gpuMs;
    uint32_t frames;
} CtrVideoStats;

/* Index of texel (x,y) in a PICA200 texture `width` texels wide: 8x8 tiles in row-major order,
 * Morton order inside a tile (x bits on the even positions). */
uint32_t CtrVideo_Texel(unsigned x, unsigned y, unsigned width);
/* GBA BGR555 -> PICA RGBA5551 with alpha set. */
uint16_t CtrVideo_RGBA5551(uint16_t bgr555);
/* OBJ character index of the 8x8 cell (x,y) (in cells) of a sprite `width` pixels wide. */
unsigned CtrVideo_ObjTile(unsigned base, unsigned x, unsigned y, unsigned width, bool color256,
                          bool mapping1d);
/* GX command budget: citro3d's queue holds 32 entries per frame and overflow is svcBreak. */
#define VX_GX_QUEUE_ENTRIES 32u
#define VX_GX_UPLOAD_COST 2u
#define VX_GX_RESERVE 16u
bool vx_gx_budget_try(unsigned *voxCmds, unsigned queueUsed, unsigned reserve);
unsigned vx_gx_budget_left(unsigned voxCmds, unsigned queueUsed, unsigned reserve);
/* The ARM side registers a probe that returns the live queue fill; default is 0. */
void vx_gx_set_probe(unsigned (*probe)(void));
void vx_gx_install_probe(void); /* ARM: probe = the GX_BindQueue wrapper's queue (ctr_shims.c) */
void vx_gx_frame_begin(void); /* zero the voxel command count (call after C3D_FrameBegin) */

/* Where a new voxel atlas page may live, tried in this order: VRAM (fastest to sample), then the
 * linear FCRAM heap (valid PICA200 texture memory, slower to sample). VRAM is shared with the
 * screen targets and the voxel mesh/page arenas and holds about one 256 KiB atlas; without the
 * linear tier a second tileset pair on screen (a town's edge) never got an atlas and the view
 * fell back to 2D. `allocated`: atlas textures held now; at `cap` neither (the caller evicts).
 * Linear only while `linearReserve` bytes would still be left free after the page. */
enum
{
    VX_ATLAS_MEM_VRAM = 1u,
    VX_ATLAS_MEM_LINEAR = 2u
};
#define VX_ATLAS_LINEAR_RESERVE (2ul * 1024ul * 1024ul)
unsigned vx_atlas_mem_allowed(unsigned allocated, unsigned cap, unsigned long linearFree,
                              unsigned long pageBytes, unsigned long linearReserve);

bool CtrVideo_TryVoxelUpload(void);
unsigned CtrVideo_VoxelUploadsLeft(void);
void CtrVideo_RequestPlaneRelease(void);
void CtrVideo_Present(void);
const CtrVideoStats *CtrVideo_GetStats(void);
const uint8_t *CtrVideo_GetBgVram(void);
void vx_video_set_bg_vram(const uint8_t *bg32k); /* the snapshot copy, set by the adapter */

/* ---- assets (prefetch is inert: every payload is already in the ROM buffer) ---------------------- */
typedef struct
{
    uint32_t index;
    uint32_t size;
    const char *path;
} CtrAssetPrefetch;
struct CtrAssetStats
{
    uint32_t evictions, misses;
};
bool CtrAssets_PrefetchFind(const void *ptr, CtrAssetPrefetch *out);
void *CtrAssets_PrefetchRead(const CtrAssetPrefetch *request);
void CtrAssets_PrefetchAdopt(const CtrAssetPrefetch *request, void *data);
const struct CtrAssetStats *CtrAssets_GetStats(void);

/* ---- platform timing --------------------------------------------------------------------------- */
typedef struct
{
    float frameMs, workMs, peakWorkMs, gameMs, vblankMs, bottomMs;
    uint32_t slowFrames;
} CtrTiming;
const CtrTiming *CtrPlatform_GetTiming(void);
CtrTiming *vx_timing_mut(void); /* the app fills it once per frame */

/* ---- settings ------------------------------------------------------------------------------------ */
/* Upstream ranges: pitch in degrees, zoom in percent. P3 maps g_settings onto these. */
void vx_settings_set(int pitchDegrees, int zoomPercent);
int CtrSettings_VoxelPitch(void);
int CtrSettings_VoxelZoom(void);

#endif /* VX_CTR_SHIMS_H */
