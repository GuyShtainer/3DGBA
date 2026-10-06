/* vx_host.h -- the 3DS-side glue between main.c and the voxel overworld (phase 32, 3DGBA, GPLv3).
 * Everything libctru/citro-dependent about the voxel path lives here so main.c carries only calls.
 * Pure parts are elsewhere: vx_overlay.c (key pass + state machine), vx_gate.c voxel_gate().
 * Threading: vx_host_candidate/snapshot/mask/overlay_upload run on the main thread in the PARKED window
 * (both workers waited); everything else runs on the main thread inside the render. Voxel off => none
 * of these is called with a core that was ever masked, and no state here is touched. */
#ifndef VX_HOST_H
#define VX_HOST_H

#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>
#include <stdint.h>

#include "gbacore.h"
#include "voxel/vx_overlay.h"

/* Frame-stable part of the gate (SPEC 5.1 voxCandidate): setting on, N3DS, no link, the top game is
 * Pokemon Emerald (BPEE) and the data is present. Binds the ROM (interning + SHA-1) on a core change. */
bool vx_host_candidate(GbaCore *top, bool userOn, bool isN3DS, bool linkAny);
/* Parked window: copy + decode the top core's game state. True when every A1-A15 guard passed. */
bool vx_host_snapshot(GbaCore *top);
unsigned vx_host_cb2(void);
bool vx_host_sb1_valid(void);
/* Lazy CtrVoxel_Init with a VRAM threshold; cached (a failure is not retried until vx_host_reset). */
bool vx_host_init_ok(void);
/* Idempotent per core: BG1-3 + OBJ off and backdrop key on (parked window only). */
void vx_host_mask(GbaCore *core, bool on);
bool vx_host_masked(const GbaCore *core);
/* Builds + uploads the BG0 overlay from the core's frame buffer (parked window, after the worker). */
void vx_host_overlay_upload(const uint16_t *fb565, unsigned stride);

/* Render side (inside C3D_FrameBegin..End). */
bool vx_host_frame_update(int pitchIdx, int zoomIdx);   /* after C3D_FrameBegin; true = a world is ready */
bool vx_host_warming_up(void);
/* World into the logical surface, then surface -> t with the HD-2D tilt-shift (blur) and bloom (Emerald3DS
 * compositor); citro2d state restored. eye: signed 3D slider (left < 0), 0 = mono. */
void vx_host_draw_world(C3D_RenderTarget *t, float eye, bool blur, bool bloomOn);
void vx_host_draw_overlay(C3D_RenderTarget *t, float x, float y, float sx, float sy, bool smooth);
void vx_host_after_submit(uint64_t frameBeginTick);
const char *vx_host_status(bool userOn, bool isN3DS, bool linkAny, GbaCore *top);
/* Call once when the top core goes away / the session ends. */
void vx_host_reset(void);

/* Dev switch: -DVX_DEV_FORCE_OVERLAY=1 forces the candidate/gate path on without data so the mask +
 * overlay chain can be seen in the emulator without a voxel pak (the frame is then overlay over black). */
/* Dev switch: -DVX_DEV_ALLOW_O3DS=1 lifts only the New-3DS gate, so the emulator harness (which pins
 * an Old 3DS for movie sync) renders the real world. Never on in a release build. */
#ifndef VX_DEV_ALLOW_O3DS
#define VX_DEV_ALLOW_O3DS 0
#endif
#ifndef VX_DEV_FORCE_OVERLAY
#define VX_DEV_FORCE_OVERLAY 0
#endif

#endif
