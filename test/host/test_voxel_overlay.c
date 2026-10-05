// test_voxel_overlay.c -- host test for the BG0 overlay key pass and the one-frame-lag state
// machine (source/voxel/vx_overlay.c). Phase 32 P3, SPEC-port sections 6.3, 6.4, 6.6, 9.2.
//
//   clang -std=c11 -Wall -Wextra -O2 -I source/voxel test/host/test_voxel_overlay.c \
//         source/voxel/vx_overlay.c -o /tmp/tvov && /tmp/tvov
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vx_overlay.h"

static int sChecks, sFails;
#define CHECK(c) do { ++sChecks; if (!(c)) { ++sFails; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

/* mGBA's 555 -> 565 expansion (M_RGB5_TO_BGR565 under COLOR_5_6_5): r<<11 | g<<1 (in place) | b. */
static uint16_t Expand(unsigned c555)
{
    return (uint16_t)(((c555 & 0x1F) << 11) | ((c555 & 0x3E0) << 1) | ((c555 & 0x7C00) >> 10));
}

/* The renderer's brighten / darken exactly as software-private.h writes them for COLOR_5_6_5
 * (re-expressed here: per channel mask arithmetic, results masked back into the channel). */
static unsigned Brighten(unsigned color, int y)
{
    unsigned c = 0, a;
    a = color & 0x1F;   c |= (a + ((0x1F - a) * y) / 16) & 0x1F;
    a = color & 0x7C0;  c |= (a + ((0x7C0 - a) * y) / 16) & 0x7C0;
    a = color & 0xF800; c |= (a + ((0xF800 - a) * y) / 16) & 0xF800;
    return c;
}
static unsigned Darken(unsigned color, int y)
{
    unsigned c = 0, a;
    a = color & 0x1F;   c |= (a - (a * y) / 16) & 0x1F;
    a = color & 0x7C0;  c |= (a - (a * y) / 16) & 0x7C0;
    a = color & 0xF800; c |= (a - (a * y) / 16) & 0xF800;
    return c;
}

static void TestKey(void)
{
    /* key -> transparent */
    CHECK(vx_overlay_px(VX_KEY565, VX_KEY565) == 0);
    /* every real colour: alpha 1 and the right RGB */
    int bad = 0;
    for (unsigned c = 0; c < 0x8000; ++c)
    {
        uint16_t px = Expand(c);
        uint16_t o = vx_overlay_px(px, VX_KEY565);
        unsigned r = px >> 11, g6 = (px >> 5) & 0x3F, b = px & 0x1F;
        if (!(o & 1u) || (o >> 11) != r || ((o >> 6) & 0x1F) != (g6 >> 1) || ((o >> 1) & 0x1F) != b)
            ++bad;
    }
    CHECK(bad == 0);
    /* the key can never come out of the 555 expansion... */
    bad = 0;
    for (unsigned c = 0; c < 0x8000; ++c)
        bad += Expand(c) == VX_KEY565;
    CHECK(bad == 0);
    /* ...nor out of brighten/darken of any such colour at any BLDY level 0..16 */
    bad = 0;
    for (unsigned c = 0; c < 0x8000; ++c)
        for (int y = 0; y <= 16; ++y)
        {
            bad += Brighten(Expand(c), y) == VX_KEY565;
            bad += Darken(Expand(c), y) == VX_KEY565;
        }
    CHECK(bad == 0);
    /* and green bit 5 is clear in all of them (the invariant behind the choice) */
    bad = 0;
    for (unsigned c = 0; c < 0x8000; ++c)
        for (int y = 0; y <= 16; ++y)
            bad += ((Brighten(Expand(c), y) | Darken(Expand(c), y) | Expand(c)) & 0x20u) != 0;
    CHECK(bad == 0);
    /* the black pixel is real and stays opaque (the point of not keying on black) */
    CHECK(vx_overlay_px(0x0000, VX_KEY565) == 1u);
    /* fallback key: the backdrop colour itself becomes transparent */
    CHECK(vx_overlay_px(0x1234, 0x1234) == 0u);
    CHECK(vx_overlay_px(0x1235, 0x1234) != 0u);
}

static void TestBuild(void)
{
    uint16_t src[6 * 4], dst[4 * 3];
    for (unsigned i = 0; i < 6 * 4; ++i)
        src[i] = VX_KEY565;
    src[1 * 6 + 2] = 0xFFFF; /* one opaque white pixel at (2,1) */
    src[2 * 6 + 0] = 0x0000; /* one opaque black pixel at (0,2) */
    memset(dst, 0xAA, sizeof dst);
    vx_overlay_build(src, 6, dst, 4, 4, 3, VX_KEY565);
    int opaque = 0;
    for (unsigned i = 0; i < 12; ++i)
        opaque += dst[i] != 0;
    CHECK(opaque == 2);
    CHECK(dst[1 * 4 + 2] == (uint16_t)(0xF800 | 0x07C0 | (0x1F << 1) | 1));
    CHECK(dst[2 * 4 + 0] == 1u);
}

static void TestStateMachine(void)
{
    /* the four rows of SPEC 6.6 */
    CHECK(vx_overlay_frame_kind(true, true) == VX_FRAME_WORLD_OVERLAY);
    CHECK(vx_overlay_frame_kind(true, false) == VX_FRAME_OVERLAY_BLACK);
    CHECK(vx_overlay_frame_kind(false, true) == VX_FRAME_FLAT);
    CHECK(vx_overlay_frame_kind(false, false) == VX_FRAME_FLAT);

    /* Engage = 2 frames, disengage = 1: simulate the pipeline. The mask requested in the parked
     * window of iteration i applies to the frame rendered during i and is seen (uploaded) in i+1. */
    int shown[8], voxTop[8], ready[8];
    bool maskReq = false, kickMask = false, upMasked = false, readyLast = false;
    for (int i = 0; i < 8; ++i)
    {
        upMasked = kickMask;             /* top of iteration: upload the frame kicked last time */
        voxTop[i] = i >= 2 && i < 6;     /* the gate is true on iterations 2..5 */
        maskReq = vx_overlay_want(voxTop[i], true, readyLast);
        kickMask = maskReq;              /* the kick records the mask in force for that frame */
        ready[i] = voxTop[i];            /* Update succeeds exactly while the gate is on (+ menu rule below) */
        shown[i] = vx_overlay_frame_kind(upMasked, ready[i] || (upMasked && i >= 6));
        readyLast = ready[i];
    }
    /* gate on at 2 -> world not ready last frame at 2 (no mask) -> mask requested at 3 -> that frame is
     * uploaded at 4 -> first world frame at 4: two frames after the gate opens. */
    CHECK(shown[2] == VX_FRAME_FLAT && shown[3] == VX_FRAME_FLAT);
    CHECK(shown[4] == VX_FRAME_WORLD_OVERLAY && shown[5] == VX_FRAME_WORLD_OVERLAY);
    /* gate closes at 6: the frame uploaded at 6 is still masked; a world is kept for it (menu/link
     * rule), and the frame uploaded at 7 is unmasked again: disengage = 1 frame. */
    CHECK(shown[6] == VX_FRAME_WORLD_OVERLAY);
    CHECK(shown[7] == VX_FRAME_FLAT);

    CHECK(!vx_overlay_want(false, true, true));
    CHECK(!vx_overlay_want(true, false, true));
    CHECK(!vx_overlay_want(true, true, false));
    CHECK(vx_overlay_want(true, true, true));
}

int main(void)
{
    TestKey();
    TestBuild();
    TestStateMachine();
    printf("test_voxel_overlay: %d checks, %d failures\n", sChecks, sFails);
    return sFails != 0;
}
