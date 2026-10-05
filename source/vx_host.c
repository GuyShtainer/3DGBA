/* vx_host.c -- see vx_host.h (phase 32, 3DGBA, GPLv3). */
#include "vx_host.h"

#include <stdio.h>
#include <sys/stat.h>
#include <string.h>

#include "voxel/ctr_shims.h"
#include "voxel/ctr_voxel.h"
#include "voxel/vx_adapter.h"
#include "voxel/vx_data.h"
#include "voxel/vx_snapshot.h"

#define VX_MIN_VRAM (2816u * 1024u)   /* chunk mesh 1536K + building pages 768K + one atlas, SPEC 4.7 */
#define OV_TEX 256u
#define OV_W 240u
#define OV_H 160u

static const int VX_PITCH_DEG[5] = { 34, 37, 40, 43, 46 };
static const int VX_ZOOM_PCT[4] = { 90, 100, 110, 120 };

static VxSnapshot sSnap;
static GbaCore *sBound;        /* the core whose ROM the adapter holds */
static bool sBpee, sDataOk;
static bool sInitTried, sInitOk;
static GbaCore *sMasked[2];    /* cores currently masked (parked-window state mirror) */
static bool sOvInit;
static C3D_Tex sOvTex;
static u16 *sOvBuf;
static FILE *sLog;
static bool sWorldReady;

static void LogSink(int channel, const char *line)
{
    (void)channel;
    if (sLog == NULL)
    {
        mkdir("sdmc:/3DGBA", 0777);
        sLog = fopen("sdmc:/3DGBA/voxel.log", "w");
    }
    if (sLog != NULL)
    {
        fputs(line, sLog);
        fputc('\n', sLog);
        fflush(sLog);
    }
}

static void Rebind(GbaCore *top)
{
    char code[5] = { 0 };
    size_t sz = 0;
    const uint8_t *rom;

    sBound = top;
    sBpee = sDataOk = false;
    vx_adapter_set_rom(NULL, 0);
    if (top == NULL)
        return;
    gbacore_game_code(top, code);
    sBpee = memcmp(code, "BPEE", 4) == 0;
#if VX_DEV_FORCE_OVERLAY
    sBpee = true;
#endif
    if (!sBpee)
        return;
    rom = gbacore_mem_block(top, 8, &sz);
    if (rom == NULL || sz == 0)
    {
        sBpee = false;
        return;
    }
    vx_log_set_sink(LogSink);
    vx_adapter_set_rom(rom, sz);
    {
        uint8_t sha[20];
        vx_sha1(rom, sz, sha);   /* once per ROM bind: ~0.5 s on ARM11, only when voxel is switched on */
        vx_data_set_rom_sha1(sha);
    }
    /* VXD_NONE is fine: the vendored world builds from the ROM alone (level terrain, extruded
     * houses). Only data that is present but wrong for this ROM or damaged blocks it. */
    sDataOk = vx_data_status() != VXD_WRONG_ROM && vx_data_status() != VXD_BAD_PAK;
#if VX_DEV_FORCE_OVERLAY
    sDataOk = true;
#endif
}

bool vx_host_candidate(GbaCore *top, bool userOn, bool isN3DS, bool linkAny)
{
    if (!userOn || (!isN3DS && !VX_DEV_FORCE_OVERLAY && !VX_DEV_ALLOW_O3DS) || linkAny || top == NULL)   /* dev switches: the emulator harness pins an Old 3DS */
        return false;
    if (top != sBound)
        Rebind(top);
    return sBpee && sDataOk;
}

bool vx_host_snapshot(GbaCore *top)
{
    VxMemSrc m;
    uint16_t io[4];
    size_t n;

    memset(&m, 0, sizeof m);
    m.ewram = gbacore_mem_block(top, 2, &n);
    m.iwram = gbacore_mem_block(top, 3, &n);
    m.pltt = gbacore_mem_block(top, 5, &n);
    m.vram = gbacore_mem_block(top, 6, &n);
    if (!gbacore_io_shadow(top, io) || vx_snapshot_take(&sSnap, &m) == false)
        return false;
    sSnap.dispcnt = io[0];
    sSnap.bldcnt = io[1];
    sSnap.bldalpha = io[2];
    sSnap.bldy = io[3];
    return vx_adapter_decode(&sSnap);
}

unsigned vx_host_cb2(void) { return sSnap.cb2; }
bool vx_host_sb1_valid(void) { return sSnap.sb1Valid; }

bool vx_host_init_ok(void)
{
    if (!sInitTried)
    {
        sInitTried = true;
        vx_gx_install_probe();
        if (vramSpaceFree() >= VX_MIN_VRAM || VX_DEV_FORCE_OVERLAY)
            sInitOk = CtrVoxel_Init();
#if VX_DEV_FORCE_OVERLAY
        sInitOk = true;
#endif
    }
    return sInitOk;
}

void vx_host_mask(GbaCore *core, bool on)
{
    for (int i = 0; i < 2; i++)
        if (sMasked[i] == core)
        {
            if (!on) { gbacore_set_overlay_mode(core, false); sMasked[i] = NULL; }
            return;
        }
    if (!on || core == NULL)
        return;
    for (int i = 0; i < 2; i++)
        if (sMasked[i] == NULL) { gbacore_set_overlay_mode(core, true); sMasked[i] = core; return; }
}

bool vx_host_masked(const GbaCore *core)
{
    return core != NULL && (sMasked[0] == core || sMasked[1] == core);
}

static bool OvInit(void)
{
    if (sOvInit)
        return true;
    sOvBuf = linearAlloc(OV_TEX * OV_H * sizeof(u16));
    if (sOvBuf == NULL || !C3D_TexInit(&sOvTex, OV_TEX, OV_TEX, GPU_RGBA5551))
        return false;
    memset(sOvTex.data, 0, sOvTex.size);
    C3D_TexFlush(&sOvTex);
    C3D_TexSetFilter(&sOvTex, GPU_NEAREST, GPU_NEAREST);
    sOvInit = true;
    return true;
}

void vx_host_overlay_upload(const uint16_t *fb565, unsigned stride)
{
    if (!OvInit())
        return;
    vx_overlay_build(fb565, stride, sOvBuf, OV_TEX, OV_W, OV_H, VX_OVERLAY_KEY_PATCH ? VX_KEY565 : fb565[0]);
    GSPGPU_FlushDataCache(sOvBuf, OV_TEX * OV_H * sizeof(u16));
    C3D_SyncDisplayTransfer((u32 *)sOvBuf, GX_BUFFER_DIM(OV_TEX, OV_H), (u32 *)sOvTex.data,
                            GX_BUFFER_DIM(OV_TEX, OV_TEX),
                            GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB5A1) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB5A1)
                                | GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_FLIP_VERT(0));
}

bool vx_host_frame_update(int pitchIdx, int zoomIdx)
{
#if VX_DEV_FORCE_OVERLAY
    (void)pitchIdx; (void)zoomIdx;
    sWorldReady = false;
    return false;
#else
    float bg = 0.0f, spr = 0.0f;
    unsigned eff = (sSnap.bldcnt >> 6) & 3u;
    CtrTiming *tm = vx_timing_mut();

    if (!sInitOk)
        return false;
    vx_settings_set(VX_PITCH_DEG[(unsigned)pitchIdx % 5u], VX_ZOOM_PCT[(unsigned)zoomIdx % 4u]);
    vx_gx_frame_begin();
    if (eff >= 2u)
    {
        unsigned lvl = sSnap.bldy & 31u;
        float f = (lvl > 16u ? 16u : lvl) / 16.0f;

        bg = (sSnap.bldcnt & 0x0Eu) ? f : 0.0f;
        spr = (sSnap.bldcnt & 0x10u) ? f : 0.0f;
        CtrVoxel_SetBrightness(bg, spr, eff == 2u);
    }
    else
        CtrVoxel_SetBrightness(0.0f, 0.0f, false);
    tm->frameMs = 16.7f;
    sWorldReady = CtrVoxel_Update();
    return sWorldReady;
#endif
}

bool vx_host_warming_up(void)
{
    return sInitOk && CtrVoxel_IsWarmingUp();
}

void vx_host_draw_world(C3D_RenderTarget *t, float eye)
{
    C2D_TargetClear(t, C2D_Color32(0, 0, 0, 0xFF));
    if (sWorldReady)
    {
        C2D_Flush();
        CtrVoxel_Draw(t, eye);
    }
    /* citro2d restore (the tilt_draw_image escape pattern): the world binds its own program,
     * attributes, texenv, depth test and cull; hand the pipeline back the way citro2d expects it. */
    C2D_Prepare();
    C3D_DepthTest(false, GPU_GEQUAL, GPU_WRITE_ALL);
    C3D_CullFace(GPU_CULL_NONE);
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
                   GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
    C3D_FragOpMode(GPU_FRAGOPMODE_GL);
    C3D_FogGasMode(GPU_NO_FOG, GPU_PLAIN_DENSITY, false);
    C2D_SceneBegin(t);
}

void vx_host_draw_overlay(C3D_RenderTarget *t, float x, float y, float sx, float sy, bool smooth)
{
    Tex3DS_SubTexture s = { OV_W, OV_H, 0.0f, 1.0f, (float)OV_W / (float)OV_TEX, 1.0f - (float)OV_H / (float)OV_TEX };
    C2D_Image img = { &sOvTex, &s };
    GPU_TEXTURE_FILTER_PARAM f = smooth ? GPU_LINEAR : GPU_NEAREST;

    (void)t;
    if (!sOvInit)
        return;
    C3D_TexSetFilter(&sOvTex, f, f);
    C2D_DrawImageAt(img, x, y, 0.0f, NULL, sx, sy);
}

void vx_host_after_submit(uint64_t frameBeginTick)
{
#if !VX_DEV_FORCE_OVERLAY
    if (sWorldReady)
        CtrVoxel_AfterSubmit(frameBeginTick);
#else
    (void)frameBeginTick;
#endif
}

const char *vx_host_status(bool userOn, bool isN3DS, bool linkAny, GbaCore *top)
{
    static char buf[64];

    if (top != sBound && userOn)
        Rebind(top);
    if (!isN3DS && !VX_DEV_ALLOW_O3DS) return "Voxel 3D: needs a New 3DS";
    if (top == NULL || !sBpee) return "Voxel 3D: Emerald only";
    if (linkAny) return "Voxel 3D: paused during link";
    switch (vx_data_status())
    {
    case VXD_WRONG_ROM: return "Voxel 3D: data is for a different ROM";
    case VXD_BAD_PAK: return "Voxel 3D: data damaged";
    default: break;
    }
    if (sInitTried && !sInitOk) return "Voxel 3D: not enough video memory";
    snprintf(buf, sizeof buf, "Voxel 3D: %s%s", sInitOk ? CtrVoxel_Status() : "ready",
             vx_data_status() == VXD_NONE ? " (basic, no data pak)" : "");
    return buf;
}

void vx_host_reset(void)
{
    sBound = NULL;
    sBpee = sDataOk = false;
    sMasked[0] = sMasked[1] = NULL;
    vx_adapter_set_rom(NULL, 0);
}
