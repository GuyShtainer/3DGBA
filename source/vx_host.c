/* vx_host.c -- see vx_host.h (phase 32, 3DGBA, GPLv3). */
#include "vx_host.h"

#include <stdio.h>
#include <sys/stat.h>
#include <string.h>

#include "romgen/rg_anchor.h"
#include "romgen/rg_gameprof.h"
#include "voxel/ctr_shims.h"
#include "voxel/ctr_voxel.h"
#include "voxel/vx_adapter.h"
#include "voxel/vx_data.h"
#include "voxel/vx_snapshot.h"

#define VX_MIN_VRAM (1024u * 1024u)   /* after the 768K surface: CtrVoxel_Init sizes its atlases/mesh to what is left */
#define OV_TEX 256u
#define OV_W 240u
#define OV_H 160u

static const int VX_PITCH_DEG[5] = { 34, 37, 40, 43, 46 };
static const int VX_ZOOM_PCT[4] = { 90, 100, 110, 120 };

static VxSnapshot sSnap;
static GbaCore *sBound;        /* the core whose ROM the adapter holds */
static GpGame sGame;           /* the detected game profile row; GP_NONE = voxel off for this ROM */
static bool sDataOk;
static bool sInitTried, sInitOk;
static GbaCore *sMasked[2];    /* cores currently masked (parked-window state mirror) */
static bool sOvInit;
static C3D_Tex sOvTex;
static u16 *sOvBuf;
static FILE *sLog;
static bool sWorldReady;
/* Phase 34 R1: a FireRed / LeafGreen cartridge is NOT rendered yet (its row has rendererOn = false until R2), but its
 * anchors are self-checked and the result goes to voxel.log: the ROM checks once per bind, the RAM checks on an
 * overworld frame, once per map. sProbe is the row under test (NULL = none / the ROM checks failed). */
static const GameProfile *sProbe;
static const uint8_t *sProbeRom;
static size_t sProbeSize;
static uint32_t sProbeLoc;     /* (group << 8 | num) the RAM checks last ran for, 0xFFFFFFFF = not yet */
/* The world's logical surface and the bloom target, as Emerald3DS composes them (3ds_video.c:3804-3817):
 * the world is drawn into a 512x256 RGBA8 VRAM texture with a 16-bit depth buffer, then blitted to the
 * screen with the HD-2D tilt-shift and bloom on top. */
static C3D_Tex sSurface;
static C3D_RenderTarget *sLogical;
static C3D_Tex sBloomTex;
static C3D_RenderTarget *sBloom;

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

/* One numbered line per failed check (SPEC-P34 3.3). */
static void LogAnchorFail(const GameProfile *p, int check)
{
    PORT_LOG("vx: anchor %d (%s) failed: 0x%08X (%.4s rev %u)", check, vx_anchor_name(check),
             (unsigned)vx_anchor_last_value(), p->code, (unsigned)p->rev);
}

/* ROM half of the self-check, once per bind, for a FireRed / LeafGreen cartridge the renderer does not drive yet. */
static void ProbeRom(const uint8_t *rom, size_t sz)
{
    const GameProfile *p = gameprof_detect_romgen(rom, sz);
    int r;

    if (p == NULL || p->game == GP_EMERALD)
        return;
    vx_log_set_sink(LogSink);
    r = vx_anchor_check_rom(p, rom, sz);
    if (r != 0)
    {
        LogAnchorFail(p, r);
        return;
    }
    PORT_LOG("vx: rom anchors ok (%.4s rev %u)", p->code, (unsigned)p->rev);
    sProbe = p;
    sProbeRom = rom;
    sProbeSize = sz;
    sProbeLoc = 0xFFFFFFFFu;
}

static uint32_t Rd32At(const uint8_t *b, uint32_t off)
{
    return (uint32_t)b[off] | ((uint32_t)b[off + 1] << 8) | ((uint32_t)b[off + 2] << 16) | ((uint32_t)b[off + 3] << 24);
}

/* RAM half: on an overworld frame, once per map. No retry loop: a failure is one line until the map changes. */
static void ProbeRam(GbaCore *top)
{
    size_t nEw = 0, nIw = 0;
    const uint8_t *ew = gbacore_mem_block(top, 2, &nEw);
    const uint8_t *iw = gbacore_mem_block(top, 3, &nIw);
    uint32_t cb2, sb1, loc;
    VxaRam ram;
    int r;

    if (ew == NULL || iw == NULL || nEw < 0x40000u || nIw < 0x8000u)
        return;
    cb2 = Rd32At(iw, sProbe->gMain - 0x03000000u + 4u);
    if (cb2 != sProbe->cb2Overworld && cb2 != sProbe->cb2OverworldBasic)
        return;   /* not an overworld frame (title, battle, a warp in progress) */
    sb1 = Rd32At(iw, sProbe->sb1Ptr - 0x03000000u);
    if (sb1 < 0x02000000u || sb1 - 0x02000000u > 0x3FFF0u)
        loc = 0xFFFFFFFEu;
    else
        loc = ((uint32_t)ew[sb1 - 0x02000000u + 4u] << 8) | ew[sb1 - 0x02000000u + 5u];
    if (loc == sProbeLoc)
        return;
    sProbeLoc = loc;
    ram.ewram = ew;
    ram.iwram = iw;
    r = vx_anchor_check_ram(sProbe, sProbeRom, sProbeSize, &ram);
    if (r != 0)
        LogAnchorFail(sProbe, r);
    else
        PORT_LOG("vx: anchors ok (%.4s rev %u) map %u.%u", sProbe->code, (unsigned)sProbe->rev,
                 (unsigned)(loc >> 8), (unsigned)(loc & 0xFFu));
}

static void Rebind(GbaCore *top)
{
    size_t sz = 0;
    const uint8_t *rom;
    const GameProfile *prof;

    sBound = top;
    sGame = GP_NONE;
    sDataOk = false;
    gVxProf = NULL;
    sProbe = NULL;
    vx_adapter_set_rom(NULL, 0);
    if (top == NULL)
        return;
    rom = gbacore_mem_block(top, 8, &sz);
    if (rom == NULL || sz == 0)
        return;
    prof = gameprof_detect(rom, sz);
    if (prof == NULL)
        ProbeRom(rom, sz);
#if VX_DEV_FORCE_OVERLAY
    if (prof == NULL)
        prof = gameprof_emerald();
#endif
    if (prof == NULL)
        return;
    sGame = prof->game;
    gVxProf = prof;
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
    if (sProbe != NULL && sGame == GP_NONE)
        ProbeRam(top);
    return sGame != GP_NONE && sDataOk;
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
        /* The surface first, before the voxel atlases take VRAM (upstream order). */
        if (C3D_TexInitVRAM(&sSurface, 512, 256, GPU_RGBA8))
        {
            C3D_TexSetFilter(&sSurface, GPU_NEAREST, GPU_NEAREST);
            C3D_TexSetWrap(&sSurface, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
            sLogical = C3D_RenderTargetCreateFromTex(&sSurface, GPU_TEXFACE_2D, 0, GPU_RB_DEPTH16);
        }
        if (sLogical != NULL && C3D_TexInitVRAM(&sBloomTex, 128, 64, GPU_RGB565))   /* not fatal: no bloom */
        {
            C3D_TexSetFilter(&sBloomTex, GPU_LINEAR, GPU_LINEAR);
            C3D_TexSetWrap(&sBloomTex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
            sBloom = C3D_RenderTargetCreateFromTex(&sBloomTex, GPU_TEXFACE_2D, 0, -1);
        }
        if (sLogical != NULL && (vramSpaceFree() >= VX_MIN_VRAM || VX_DEV_FORCE_OVERLAY))
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

/* ---- the Emerald3DS voxel compositor (3ds_video.c:4356-4560, MIT, Copyright (c) Dust Zallax; adapted) ---- */
#define DIORAMA_TOP 100    /* rows blurred at the top */
#define DIORAMA_BOTTOM 56  /* ... and at the bottom */
#define BLOOM_W (CTR_GAME_WIDTH / 4)
#define BLOOM_H (CTR_GAME_HEIGHT / 4)
#define BLOOM_THRESHOLD 0.85f

static void SurfaceFilter(GPU_TEXTURE_FILTER_PARAM filter)
{
    /* Batched draws sample with the state at the flush: flush first, and rebind. */
    C2D_Flush();
    C3D_TexSetFilter(&sSurface, filter, filter);
    C3D_TexBind(0, &sSurface);
}

static void BlendNormal(void)
{
    C2D_Flush();
    C3D_AlphaTest(true, GPU_GREATER, 0);
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_ONE, GPU_ZERO);
}

/* HD-2D diorama: the tilt-shift of a miniature. The camera looks north, so the top of the screen is the
 * distance and the bottom the nearest ground; both bands are two copies of the surface drawn half a texel
 * off either way with bilinear filtering, fading from the focus band to the edge. */
static void DioramaTap(int y0, int rows, bool top, float dx, float dy, float alpha)
{
    const Tex3DS_SubTexture region = { CTR_GAME_WIDTH - 2, (u16)rows,
        (1.0f + dx) / 512.0f, 1.0f - (y0 + dy) / 256.0f,
        (1.0f + dx + CTR_GAME_WIDTH - 2) / 512.0f, 1.0f - (y0 + dy + rows) / 256.0f };
    C2D_ImageTint tint;

    C2D_AlphaImageTint(&tint, 0.0f);
    if (top)
        C2D_TopImageTint(&tint, C2D_Color32f(1.0f, 1.0f, 1.0f, alpha), 0.0f);
    else
        C2D_BottomImageTint(&tint, C2D_Color32f(1.0f, 1.0f, 1.0f, alpha), 0.0f);
    C2D_DrawImageAt((C2D_Image){ &sSurface, &region }, 1, y0, 0, &tint, 1, 1);
}

static void Diorama(void)
{
    SurfaceFilter(GPU_LINEAR);
    DioramaTap(0, DIORAMA_TOP, true, 0.5f, 0.5f, 0.67f);
    DioramaTap(0, DIORAMA_TOP, true, -0.5f, -0.5f, 0.50f);
    DioramaTap(CTR_GAME_HEIGHT - DIORAMA_BOTTOM, DIORAMA_BOTTOM, false, 0.5f, 0.5f, 0.60f);
    DioramaTap(CTR_GAME_HEIGHT - DIORAMA_BOTTOM, DIORAMA_BOTTOM, false, -0.5f, -0.5f, 0.45f);
    SurfaceFilter(GPU_NEAREST);
}

/* Bloom: the surface at a quarter size into a small target keeping only what is brighter than the
 * threshold ((colour - t) x 4 in texenv 4, which citro2d leaves free), then stretched back and added. */
static void BloomPrepare(void)
{
    const Tex3DS_SubTexture logical = { CTR_GAME_WIDTH, CTR_GAME_HEIGHT, 0, 1,
        CTR_GAME_WIDTH / 512.0f, 1 - CTR_GAME_HEIGHT / 256.0f };
    unsigned th = (unsigned)(BLOOM_THRESHOLD * 255.0f + 0.5f);
    C3D_TexEnv *env;

    C2D_TargetClear(sBloom, C2D_Color32(0, 0, 0, 255));
    C2D_SceneBegin(sBloom);
    C2D_ViewReset();
    SurfaceFilter(GPU_LINEAR);
    C3D_AlphaTest(false, GPU_ALWAYS, 0);
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
    env = C3D_GetTexEnv(4);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_RGB, GPU_PREVIOUS, GPU_CONSTANT, GPU_CONSTANT);
    C3D_TexEnvFunc(env, C3D_RGB, GPU_SUBTRACT);
    C3D_TexEnvScale(env, C3D_RGB, GPU_TEVSCALE_4);
    C3D_TexEnvColor(env, 0xFF000000u | th << 16 | th << 8 | th);
    C2D_DrawImageAt((C2D_Image){ &sSurface, &logical }, 0, 0, 0, NULL, 0.25f, 0.25f);
    C2D_Flush();
    C3D_TexEnvInit(C3D_GetTexEnv(4));
    SurfaceFilter(GPU_NEAREST);
}

static void BloomCompose(float strength)
{
    const Tex3DS_SubTexture region = { BLOOM_W, BLOOM_H, 0, 1, BLOOM_W / 128.0f, 1 - BLOOM_H / 64.0f };
    C2D_ImageTint tint;

    C2D_Flush();
    C3D_AlphaTest(false, GPU_ALWAYS, 0);
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE, GPU_ZERO, GPU_ONE);
    C2D_AlphaImageTint(&tint, strength);
    C2D_DrawImageAt((C2D_Image){ &sBloomTex, &region }, 0, 0, 0, &tint, 4.0f, 4.0f);
    C2D_Flush();
}

/* The dark of a cave: a soft ring of shadow round the player, over the world, under the game's text. */
static void Gloom(void)
{
    float x, y, size, amount;
    const C3D_Tex *tex = CtrVoxel_Gloom(&x, &y, &size, &amount);
    C2D_ImageTint tint;
    unsigned alpha;

    if (tex == NULL || amount <= 0.0f)
        return;
    {
        const Tex3DS_SubTexture whole = { tex->width, tex->height, 0.0f, 1.0f, 1.0f, 0.0f };
        alpha = (unsigned)(amount * 255.0f + 0.5f);
        BlendNormal();
        C2D_PlainImageTint(&tint, C2D_Color32(0, 0, 0, alpha > 255 ? 255 : alpha), 1.0f);
        C2D_DrawImageAt((C2D_Image){ (C3D_Tex *)tex, &whole }, x - size * 0.5f, y - size * 0.5f, 0,
                        &tint, size / tex->width, size / tex->height);
        C2D_Flush();
    }
}

void vx_host_draw_world(C3D_RenderTarget *t, float eye, bool blur, bool bloomOn)
{
    const Tex3DS_SubTexture logical = { CTR_GAME_WIDTH, CTR_GAME_HEIGHT, 0, 1,
        CTR_GAME_WIDTH / 512.0f, 1 - CTR_GAME_HEIGHT / 256.0f };
    float bloom;

    if (!sWorldReady || sLogical == NULL)
    {
        C2D_TargetClear(t, C2D_Color32(0, 0, 0, 0xFF));
        C2D_SceneBegin(t);
        return;
    }
    /* C2D_TargetClear clears colour and depth, which the 3D pass needs. */
    C2D_Flush();
    C2D_TargetClear(sLogical, C2D_Color32(0, 0, 0, 0xFF));
    CtrVoxel_Draw(sLogical, eye);
    C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
    C3D_FrameSplit(0);

    /* Back to the 2D compositor, which assumes its own program and no depth. */
    C2D_Prepare();
    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
    C3D_CullFace(GPU_CULL_NONE);
    C3D_FragOpMode(GPU_FRAGOPMODE_GL);
    C3D_FogGasMode(GPU_NO_FOG, GPU_PLAIN_DENSITY, false);
    bloom = (bloomOn && sBloom != NULL) ? CtrVoxel_Bloom() : 0.0f;
    if (bloom > 0.005f)
        BloomPrepare();
    C2D_TargetClear(t, C2D_Color32(0, 0, 0, 0xFF));
    C2D_SceneBegin(t);
    C2D_ViewReset();
    BlendNormal();
    C2D_DrawImageAt((C2D_Image){ &sSurface, &logical }, 0, 0, 0, NULL, 1, 1);
    if (blur)
        Diorama();
    if (bloom > 0.005f)
        BloomCompose(bloom);
    Gloom();
    C2D_Flush();
    /* Hand citro2d back its usual blend for the overlay and HUD. */
    C3D_AlphaTest(false, GPU_ALWAYS, 0);
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
                   GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
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
    if (top == NULL || sGame == GP_NONE) return "Voxel 3D: Emerald only";
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
    sGame = GP_NONE;
    sDataOk = false;
    gVxProf = NULL;
    sProbe = NULL;
    sMasked[0] = sMasked[1] = NULL;
    vx_adapter_set_rom(NULL, 0);
}
