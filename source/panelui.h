// panelui.h — phase 32 track T: the device half of the single-game touch PANEL
// (docs/phase32-voxel/SPEC-touch-panel.md). panel.{c,h} is the pure-C half (geometry, sequencer,
// battle view model); this module binds it to a live GbaCore, routes the stylus, applies the
// sequencer's cursor writes exactly as smart touch's fmenu_select does, and draws the screen.
// Our own code (legal condition 5): only the IDEA of the layout comes from Zallax's demo.
#pragma once
#include <3ds.h>
#include <citro2d.h>
#include "gbacore.h"
#include "gamestate.h"
#include "touch.h"
#include "panel.h"

enum { PREG_NONE = 0, PREG_VIEW, PREG_COL, PREG_BATTLE, PREG_CHIP };

typedef struct {
	bool        ok;            // a supported Gen-3 game is bound (BPEE / BPRE / BPGE)
	GbaCore*    core;          // the core `ok` was decided for (rebinds when it changes)
	PanelAddrs  addrs;
	PanelCtx    ctx;           // this frame's panel context
	GameCtx     gctx;          // this frame's raw game context
	bool        inBattle;
	bool        battlePanel;   // the battle command panel replaces the game view this frame
	PanelSeq    seq;
	PanelStartList list;       // last START list read while the START menu was up
	PanelBattle battle;
	// the current stylus contact
	bool        wasTouching;
	int         age;           // frames of contact
	int         region;        // PREG_* decided on the first contact frame
	int         item;          // row / cell latched on the settled frame, -1 none
	int         x0, y0;        // the settled point
	int         lx, ly;        // the last contact point (hidTouchRead is 0 on the release frame)
	bool        drag;          // moved past the tap slop: buttons cancel
	// synthetic replay into SMART (battle cells) and the BACK B pulse
	int         replayKind, replaySlot, replayFrames;
	int         bPulse;
	// feedback
	int         fbRegion, fbItem, fbTimer;
	int         lastAbort;     // PABORT_* of the last ended sequence (on-device diagnostics)
} PanelUi;

#define PANELUI_SETTLE   2     // == touch.c TOUCH_SETTLE: hit-test the settled point, never the edge
#define PANELUI_SLOP    12     // finger tap-vs-drag threshold (learn: touch-ui-on-real-hardware)
#define PANELUI_FB       6     // pressed-state frames after an accepted tap

// The "MENU" chip in the band above the game view (the pause menu's touch route).
#define PANELUI_CHIP_X   4
#define PANELUI_CHIP_Y   8
#define PANELUI_CHIP_W  64
#define PANELUI_CHIP_H  24

// Binds to `core` (cached per core pointer). false = not a supported game: the caller keeps PAD.
bool panelui_bind(PanelUi* u, GbaCore* core);

// One gameplay frame, cores parked. `sm` is the TouchSmart filled from the same game (the view
// taps and the battle replay go through touch_update(TOUCH_SMART) with it). Returns the GBA keys
// to OR into the game; *menuReq is set when the MENU chip was tapped.
u16  panelui_update(PanelUi* u, const GameProfile* gp, const GameState* gs, const TouchSmart* sm,
                    bool touching, int px, int py, u16 padKeys, bool link, bool* menuReq);

// Top screen holds the last field frame: a full-screen menu is open outside battle.
bool panelui_hold_top(const PanelUi* u);
// The field frame may be captured into the hold texture this frame.
bool panelui_is_field(const PanelUi* u);

// Draws the whole bottom screen (the scene is already begun and cleared). `frame` = the game's
// 256x256 RGB565 texture, NULL = no frame yet.
void panelui_draw(const PanelUi* u, C3D_Tex* frame, C2D_TextBuf buf);
