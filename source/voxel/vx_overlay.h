/* vx_overlay.h -- the BG0 overlay of the GBA frame drawn over the voxel world (3DGBA, GPLv3).
 * SPEC-port section 6. Pure C, host-testable: the key-to-alpha pass and the one-frame-lag state
 * machine. The renderer side (mask request, texture upload) is vx_host.c. */
#ifndef VX_OVERLAY_H
#define VX_OVERLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* When 1 (default) the top core is told to key the backdrop (patches/mgba-backdrop-key.patch).
 * When 0 the patch is not in libmgba and the overlay falls back to the palette-0 colour key
 * (SPEC 6.2 option C): pixels equal to the frame's own backdrop colour become transparent. Lossy
 * where BG0 itself uses the backdrop colour (black indoor backdrop vs black text pixels). */
#ifndef VX_OVERLAY_KEY_PATCH
#define VX_OVERLAY_KEY_PATCH 1
#endif

/* RGB565 colour key the patched software renderer writes for "nothing drawn here". Green bit 5 of a
 * 565 word is always 0 after mGBA's 555->565 expansion and after every blend/brighten/darken mask,
 * so no real pixel can equal it (proved exhaustively by test_voxel_overlay). */
#define VX_KEY565 0x0020u

/* One pixel: key -> 0 (transparent), else RGB565 -> PICA RGBA5551 with alpha 1. `key` is the colour
 * treated as transparent (VX_KEY565 with the patch, the backdrop colour in the fallback). */
uint16_t vx_overlay_px(uint16_t px565, uint16_t key);

/* Builds the w x h overlay (stride-addressed source, tightly strided destination). */
void vx_overlay_build(const uint16_t *fb565, unsigned srcStride, uint16_t *dst5551,
                      unsigned dstStride, unsigned w, unsigned h, uint16_t key);

/* The one-frame-lag state machine (SPEC 6.6). uploadedMasked: the frame being drawn was rendered
 * with the overlay mask on; worldReady: CtrVoxel_Update succeeded this frame. */
typedef enum
{
    VX_FRAME_FLAT = 0,         /* draw the uploaded image as today (voxel off, opening frame, warm-up) */
    VX_FRAME_WORLD_OVERLAY,    /* world + BG0 overlay (the normal voxel frame) */
    VX_FRAME_OVERLAY_BLACK     /* closing frame without a world: overlay over black (rare) */
} VxFrameKind;
VxFrameKind vx_overlay_frame_kind(bool uploadedMasked, bool worldReady);

/* Mask request for the NEXT rendered frame, decided in the parked window. */
bool vx_overlay_want(bool voxTop, bool available, bool worldReadyLastFrame);

#endif
