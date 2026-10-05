// test_voxel_gate.c -- host test for voxel_gate() (source/tilt.c). Phase 32 P3, SPEC-port 5.3, 9.2.
//
//   clang -std=c11 -Wall -Wextra -O2 -I source test/host/test_voxel_gate.c -o /tmp/tvg && /tmp/tvg
//
// Every rule of the ladder flips the answer alone; the dropped rules (textDlg, stereo) are not in
// the struct at all; the result is checked against an independent positive-conjunction reference
// over a sweep of the whole input space.
#include <stdio.h>
#include <string.h>

#include "../../source/tilt.c"

static int sChecks, sFails;
#define CHECK(c) do { ++sChecks; if (!(c)) { ++sFails; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static VoxGateIn Clear(void)
{
    VoxGateIn in;
    memset(&in, 0, sizeof in);
    in.userOn = 1; in.isBPEE = 1; in.dataOk = 1; in.isN3DS = 1; in.menuOpen = 0; in.linkAny = 0;
    in.ok = 1; in.ctx = FIELD_CTX_OVERWORLD; in.sb1Valid = 1; in.fsStarved = 0;
    in.cb2 = VOX_CB2_OVERWORLD_A; in.snapValid = 1; in.initOk = 1;
    return in;
}

static int Ref(const VoxGateIn *i)
{
    return i->userOn && i->isBPEE && i->dataOk && i->isN3DS && !i->menuOpen && !i->linkAny && i->ok
        && (i->ctx == 1 || i->ctx == 6) && i->sb1Valid && !i->fsStarved
        && (i->cb2 == 0x08085E5Du || i->cb2 == 0x08085E51u) && i->snapValid && i->initOk;
}

int main(void)
{
    VoxGateIn in = Clear();
    CHECK(voxel_gate(&in) == 1);

    /* each rule alone */
    { VoxGateIn x = Clear(); x.userOn = 0;    CHECK(voxel_gate(&x) == 0); }   /* setting off */
    { VoxGateIn x = Clear(); x.isBPEE = 0;    CHECK(voxel_gate(&x) == 0); }   /* not Emerald */
    { VoxGateIn x = Clear(); x.dataOk = 0;    CHECK(voxel_gate(&x) == 0); }   /* no data */
    { VoxGateIn x = Clear(); x.isN3DS = 0;    CHECK(voxel_gate(&x) == 0); }   /* G2 */
    { VoxGateIn x = Clear(); x.menuOpen = 1;  CHECK(voxel_gate(&x) == 0); }   /* G3 */
    { VoxGateIn x = Clear(); x.linkAny = 1;   CHECK(voxel_gate(&x) == 0); }   /* G4 + linkOn */
    { VoxGateIn x = Clear(); x.ok = 0;        CHECK(voxel_gate(&x) == 0); }   /* G5 */
    { VoxGateIn x = Clear(); x.sb1Valid = 0;  CHECK(voxel_gate(&x) == 0); }   /* G7 */
    { VoxGateIn x = Clear(); x.fsStarved = 1; CHECK(voxel_gate(&x) == 0); }   /* G11 */
    { VoxGateIn x = Clear(); x.snapValid = 0; CHECK(voxel_gate(&x) == 0); }
    { VoxGateIn x = Clear(); x.initOk = 0;    CHECK(voxel_gate(&x) == 0); }

    /* G6: OVERWORLD and FIELDMENU on (the START menu overlays the world), every other ctx off */
    for (int ctx = 0; ctx < 16; ++ctx)
    {
        VoxGateIn x = Clear(); x.ctx = ctx;
        CHECK(voxel_gate(&x) == (ctx == 1 || ctx == 6));
    }
    /* callback: both overworld callbacks on, anything else off (battle, bag, title, warp) */
    { VoxGateIn x = Clear(); x.cb2 = VOX_CB2_OVERWORLD_B; CHECK(voxel_gate(&x) == 1); }
    { VoxGateIn x = Clear(); x.cb2 = 0x08085E5Cu; CHECK(voxel_gate(&x) == 0); }
    { VoxGateIn x = Clear(); x.cb2 = 0;           CHECK(voxel_gate(&x) == 0); }
    { VoxGateIn x = Clear(); x.cb2 = 0x0803E9E9u; CHECK(voxel_gate(&x) == 0); }

    /* the constants are the ones gba_game.h names (CB2_Overworld / CB2_OverworldBasic) */
    CHECK(VOX_CB2_OVERWORLD_A == 0x08085E5Du && VOX_CB2_OVERWORLD_B == 0x08085E51u);
    CHECK(VOX_CTX_FIELDMENU == 6 && FIELD_CTX_OVERWORLD == 1);

    /* exhaustive sweep of the boolean inputs x ctx x cb2 against the reference */
    const unsigned cbs[3] = { VOX_CB2_OVERWORLD_A, VOX_CB2_OVERWORLD_B, 0x08000001u };
    int mismatch = 0, total = 0;
    for (unsigned m = 0; m < (1u << 10); ++m)
        for (int ctx = 0; ctx < 9; ++ctx)
            for (int c = 0; c < 3; ++c)
            {
                VoxGateIn x;
                x.userOn = m & 1; x.isBPEE = (m >> 1) & 1; x.dataOk = (m >> 2) & 1; x.isN3DS = (m >> 3) & 1;
                x.menuOpen = (m >> 4) & 1; x.linkAny = (m >> 5) & 1; x.ok = (m >> 6) & 1; x.sb1Valid = (m >> 7) & 1;
                x.fsStarved = (m >> 8) & 1; x.snapValid = (m >> 9) & 1; x.initOk = 1;
                x.ctx = ctx; x.cb2 = cbs[c];
                ++total;
                mismatch += voxel_gate(&x) != Ref(&x);
            }
    CHECK(total == 1024 * 9 * 3);
    CHECK(mismatch == 0);

    /* pure: the gate does not mutate its input */
    VoxGateIn a = Clear(), b = Clear();
    (void)voxel_gate(&a);
    CHECK(memcmp(&a, &b, sizeof a) == 0);

    printf("test_voxel_gate: %d checks, %d failures\n", sChecks, sFails);
    return sFails != 0;
}
