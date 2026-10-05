/* vx_overlay.c -- see vx_overlay.h (3DGBA, GPLv3). */
#include "vx_overlay.h"

uint16_t vx_overlay_px(uint16_t px, uint16_t key)
{
    if (px == key)
        return 0u;
    /* RGB565 (r15-11 g10-5 b4-0) -> RGBA5551 (r15-11 g10-6 b5-1 a0), alpha 1 */
    return (uint16_t)((px & 0xF800u) | ((px & 0x07C0u)) | ((px & 0x001Fu) << 1) | 1u);
}

void vx_overlay_build(const uint16_t *fb565, unsigned srcStride, uint16_t *dst5551,
                      unsigned dstStride, unsigned w, unsigned h, uint16_t key)
{
    for (unsigned y = 0; y < h; ++y)
    {
        const uint16_t *s = fb565 + (size_t)y * srcStride;
        uint16_t *d = dst5551 + (size_t)y * dstStride;
        for (unsigned x = 0; x < w; ++x)
            d[x] = vx_overlay_px(s[x], key);
    }
}

VxFrameKind vx_overlay_frame_kind(bool uploadedMasked, bool worldReady)
{
    if (!uploadedMasked)
        return VX_FRAME_FLAT;
    return worldReady ? VX_FRAME_WORLD_OVERLAY : VX_FRAME_OVERLAY_BLACK;
}

bool vx_overlay_want(bool voxTop, bool available, bool worldReadyLastFrame)
{
    return voxTop && available && worldReadyLastFrame;
}
