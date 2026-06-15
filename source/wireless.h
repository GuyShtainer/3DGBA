// wireless.h — M1 wireless multi-console lobby UI (host / scan+join / live seat map).
// Self-contained screen built on the netlink UDS transport + citro2d; zero mGBA types.
// Runs its own loop and returns when the user backs out. See docs/kb/wireless-link-architecture.md.
#pragma once
#include <citro2d.h>
#include <citro3d.h>

// Open the lobby. myGameCode = the focused game's 4-char GBA code ("BPEE" etc.), used for the
// advertisement and the per-seat identity (✓/✗) check. Renders on the passed screen targets.
// Returns: 0 = closed (session torn down), 1 = START LINK as host (seat 0), 2 = START LINK as
// joiner (seat 1). On a non-zero return the UDS session is left UP for gameplay; on 0 it is closed.
int wireless_lobby_run(C3D_RenderTarget* top, C3D_RenderTarget* bot, C2D_TextBuf txtBuf,
                       const char* myGameCode);
