// vx_gate.c -- phase 32: the VOXEL gate (SPEC-port §5.3). Pure C, host-tested (test_voxel_gate.c).
// Moved here verbatim from tilt.c when the phase-14 tilt was deleted (2026-10-06).
#include "vx_gate.h"

// Ordered ladder, first miss returns 0. Dropped on purpose vs the old tilt gate: G8 textDlg (text
// boxes are BG0, they overlay the world) and G10 stereo (the world is drawn in both eyes). No
// hysteresis here: the overlay state machine (vx_overlay.c) and the warm-up blank smooth transitions.
int voxel_gate(const VoxGateIn* in) {
	if (!in->userOn)                     return 0;   // VOXEL 3D off (the shipped default)
	if (!in->isBPEE)                     return 0;   // Emerald only: every address is BPEE's
	if (!in->dataOk)                     return 0;   // no pak / loose data
	if (!in->isN3DS)                     return 0;   // G2
	if (in->menuOpen)                    return 0;   // G3
	if (in->linkAny)                     return 0;   // G4: no parked window under a link
	if (!in->ok)                         return 0;   // G5
	if (in->ctx != FIELD_CTX_OVERWORLD && in->ctx != VOX_CTX_FIELDMENU) return 0;   // G6 (+START menu)
	if (!in->sb1Valid)                   return 0;   // G7
	if (in->fsStarved)                   return 0;   // G11
	if (in->cb2 != VOX_CB2_OVERWORLD_A && in->cb2 != VOX_CB2_OVERWORLD_B) return 0;   // battle/bag/title/warp
	if (!in->snapValid)                  return 0;
	if (!in->initOk)                     return 0;
	return 1;
}
