// vx_gate.h -- phase 32: the VOXEL gate (SPEC-port §5.3). PURE C, host-tested.
// ============================================================================================
// One flat struct, one pure function, host-tested by test_voxel_gate.c (every rule flips the answer
// alone). Moved here verbatim from tilt.h when the phase-14 tilt was deleted (2026-10-06, see
// docs/REMOVED-3D-ATTEMPTS.md). All plain ints (pure-C rule).
// ctx values cross the boundary pinned at the call site: GCTX_OVERWORLD == FIELD_CTX_OVERWORLD,
// GCTX_FIELDMENU == VOX_CTX_FIELDMENU (main.c _Static_asserts).
#pragma once

#include "fieldgate.h"   // FIELD_CTX_OVERWORLD

#define VOX_CTX_FIELDMENU 6
#define VOX_CB2_OVERWORLD_A 0x08085E5Du   // CB2_Overworld (BPEE, thumb bit set), snapshot gMain.callback2
#define VOX_CB2_OVERWORLD_B 0x08085E51u   // CB2_OverworldBasic, the other callback VoxelWorld_IsMapAvailable accepts
typedef struct {
	int userOn;        // g_prefs.voxel
	int isBPEE;        // top game's ROM header code is "BPEE"
	int dataOk;        // pak or loose data present and validated
	int isN3DS;        // G2
	int menuOpen;      // G3: OUR pause menu
	int linkAny;       // G4 (+linkOn): no parked window under any link
	int ok;            // G5: game_read profile ok for the top game
	int ctx;           // G6: GCTX_OVERWORLD or GCTX_FIELDMENU
	int sb1Valid;      // G7
	int fsStarved;     // G11: fsOn && the focused screen is not the top one
	unsigned cb2;      // snapshot gMain.callback2
	int snapValid;     // snapshot + decode passed A1-A15 this frame
	int initOk;        // CtrVoxel_Init succeeded (VRAM/linear budget)
} VoxGateIn;
int voxel_gate(const VoxGateIn* in);   // 1 = draw the voxel world on the top screen this frame
