// gamestate.h — game-aware touch (v1.1): read live Gen-3 state from the running core so the
// touchscreen can act as a POINTER on the real game UI. All RAM addresses verified vs pret's
// byte-matched symbol maps; see docs/kb/gen3-ram-touch.md + gen3-touch-features-spec.md.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "gbacore.h"

// On-screen context the bottom game is in (drives how touch is interpreted).
typedef enum {
	GCTX_NONE = 0,
	GCTX_OVERWORLD,
	GCTX_BATTLE_ACTION,   // FIGHT / BAG / POKEMON / RUN
	GCTX_BATTLE_MOVE,     // move-selection menu
	GCTX_BATTLE_TARGET,   // double-battle "choose a target" (HandleInputChooseTarget live)
	GCTX_PARTY,           // party menu open (in-battle send-out)
	GCTX_FIELDMENU,       // overworld sMenu up (START / YES-NO / script multichoice)
	GCTX_BAG,             // bag menu open (field or battle "ITEM")
	GCTX_BATTLE_OTHER     // some other battle screen (dialog/animation) — tap = advance (A)
} GameCtx;

// Per-game RAM map (all absolute GBA bus addresses; EM=Emerald, FR=FireRed/LeafGreen).
typedef struct {
	char     code[5];
	uint32_t sb1ptr;        // gSaveBlock1Ptr (deref -> player x/y, first two s16)
	uint32_t battleFlags;   // gBattleTypeFlags (nonzero in battle)
	uint32_t actionCursor;  // gActionSelectionCursor[0]
	uint32_t moveCursor;    // gMoveSelectionCursor[0]
	uint32_t battleMons;    // gBattleMons[0] (moves[] at +0x0C)
	uint32_t bg0y;          // gBattle_BG0_Y (160 action / 320 move menu)
	// party menu
	uint32_t partyMenu;     // gPartyMenu base (+0x08 menuType|layout, +0x09 slotId)
	uint32_t partyCount;    // gPlayerPartyCount (u8)
	uint32_t mainCb2;       // gMain.callback2 (u32)
	uint32_t cb2UpdParty;   // CB2_UpdatePartyMenu (ROM, compare with Thumb bit masked)
	uint32_t cb2InitParty;  // CB2_InitPartyMenu (fade-in; not yet tappable)
	uint32_t newKeys;       // gMain.newKeys (u16) — unused (we inject via the returned key mask)
	// double-battle target select
	uint32_t ctrlFuncs;     // gBattlerControllerFuncs[4] (u32 ptrs)
	uint32_t chooseTarget;  // HandleInputChooseTarget (ROM, compare with Thumb bit masked)
	uint32_t multiCursor;   // gMultiUsePlayerCursor (u8) — targeted battler index
	uint32_t battlerPos;    // gBattlerPositions[4] (u8)
	uint32_t battlersCount; // gBattlersCount (u8)
	uint32_t absentFlags;   // gAbsentBattlerFlags (u8)
	uint32_t activeBattler; // gActiveBattler (u8)
	// overworld pathfinding
	uint32_t mapLayout;     // gBackupMapLayout / VMap (s32 width, s32 height, u16* map)
	// general field menu (sMenu) + bag
	uint32_t startCb;       // gMenuCallback / sStartMenuCallback (fn ptr; START active when == startCbInput)
	uint32_t startCbInput;  // HandleStartMenuInput / StartCB_HandleInput (ROM; compare Thumb-masked)
	uint32_t sMenuBase;     // struct Menu sMenu (+1 top, +2 cursorPos, +4 maxCursorPos, +5 windowId, +8 optHeight)
	uint32_t gWindowsBase;  // gWindows[] (12-byte stride: +0 bg, +1 left, +2 top, +3 width, +4 height)
	uint32_t startCursor;   // sStartMenuCursorPos (write too for the START menu)
	uint32_t gTasksBase;    // gTasks[] (40-byte stride: +0 func, +4 isActive, +8 data[])
	uint32_t cb2BagRun;     // CB2_BagMenuRun (ROM; gMain.callback2 == this when the bag is up)
	uint32_t bagHandler;    // Task_BagMenu_HandleInput (ROM; the live list-task owner)
	uint32_t bagOpen;       // FR gBagMenuState.bagOpen (bool8); 0 = unused (Emerald)
	uint32_t partyTask;     // Task_HandleChooseMonInput (party input handler — field OR battle)
	uint32_t yesNoTask;     // Task_HandleYesNoInput
	uint32_t multiTask;     // Task_HandleMultichoiceInput
	uint32_t selMenuTask;   // Task_HandleSelectionMenuInput (party SUMMARY/SWITCH/ITEM/CANCEL popup; uses sMenu)
	uint32_t startMenuTask; // Task_ShowStartMenu / Task_StartMenuHandleInput (active only while START menu is up)
	uint32_t mapHeader;     // gMapHeader (BPEE 0x02037318) for stereoscopic scenery depth; 0 = no M4
	uint32_t battleMainCb;  // BattleMainCB2 (callback2==this == interactive battle; battleFlags lingers post-battle)
	uint32_t mapObjects;    // gObjectEvents[16] (stride 0x24; +0 active:1, +0x10/+0x12 currentCoords x/y) -> NPC collision
	uint32_t fieldMsgMode;  // sFieldMessageBoxMode (EM) / sMessageBoxType (FRLG): u8, != 0 while a field textbox is up
	uint32_t mapNameTask;   // Task_MapNamePopUpWindow (EM) / Task_MapNamePopup (FRLG) — map-name banner task (ROM)
	uint32_t fieldCamera;   // gFieldCamera (+0x10 x, +0x14 y = sub-tile scroll, %%16) -> 3D depth scroll-align
	// --- link-error diagnostics (LOGGING ONLY; addresses verified vs pret symbols-branch rev0). When the
	// wireless trade dies the game enters CB2_PrintErrorMessage; these latch WHY (which watchdog / queue /
	// player-drop tripped) even though OUR transport was clean. 0 = not mapped for this game. ---
	uint32_t linkStatus;    // gLinkStatus (u32 bitfield: player count + LINK_STAT_* error bits, live)
	uint32_t linkErr;       // gLinkErrorOccurred (u8; 1 once the game's link layer flagged an error)
	uint32_t linkErrBuf;    // sLinkErrorBuffer (8B latched at error: status u32 + send/recv queue counts)
	uint32_t linkNotRecv;   // gRemoteLinkPlayersNotReceived (u32 player bitmask we stopped hearing from)
	// --- D2 game-heartbeat hang catcher (phase 13-prep, SPEC-firmware-diag D2.1; LOGGING ONLY) ---
	// gMain.vblankCounter1 (u32 @ gMain+0x20): incremented UNCONDITIONALLY in VBlankIntr (pret
	// pokeemerald src/main.c VBlankIntr(): "gMain.vblankCounter1++"; pokefirered same) — the game's
	// own IRQ-delivery heartbeat. Frozen while the core still produces video frames => the game's
	// interrupt path is dead (the D2 Tier-B watch; diag.h DiagHang). Derivation (no re-verified sym
	// map on this machine — SPEC D2.1): gMain base = the VERIFIED mainCb2 - 4 (gamestate.c game_read:
	// "gMain+0 = mainCb2-4"); offset 0x20 per pret include/main.h struct Main layout: callbacks
	// 0x00-0x18 (7 x MainCallback/IntrCallback ptrs), intrCheck u16 @0x1C (+2 pad), vblankCounter1
	// u32 @0x20, vblankCounter2 u32 @0x24, heldKeysRaw @0x28 ... newKeys u16 @0x2E — the layout
	// through 0x20 is cross-checked by the hw-exercised newKeys anchor (BPEE 0x030022EE = gMain+0x2E).
	// Status: derived-from-verified-anchors, VERIFY-ON-HW-PENDING (self-verifying once the D3 CSV
	// shows it ticking ~60/s); BPGE is FR-derived/unverified per the house rule (HANDOFF Gotchas).
	uint32_t vblankCtr;     // gMain.vblankCounter1 (u32); 0 = not mapped -> D2 Tier B stays disarmed
	// --- phase 15 co-op presence (SPEC-data.md D1). All VERIFIED-SYM against pret's byte-matched
	// `symbols` branch (re-read 2026-08-04); FR/LG values come from pokefirered.sym AND
	// pokeleafgreen.sym SEPARATELY (not FR-derived), and the rev0/rev1 maps agree on every one of
	// them — which matters because the user's FireRed is rev1 (MEMORY, run #12). 0 = not mapped,
	// and every 0 has a named graceful degradation in D1.9 (never a garbage read, never a draw at
	// a plausible-looking wrong position). Appending is the only safe edit: PROFILES[] in
	// gamestate.c is POSITIONAL-initialised (the D2 vblankCtr block above is the precedent). ---
	uint32_t sb2ptr;        // gSaveBlock2Ptr (deref -> +0x00 name[8] GBA charmap 0xFF-terminated,
	                        //   +0x08 playerGender 0=M/1=F, +0x0A visible TID = LE u16). D1.1/D1.2:
	                        //   EM sym:962 / FR sym:810 / LG sym:810; = sb1ptr+4 (a CONSEQUENCE of
	                        //   the read, not the derivation). Corroborated in-repo by
	                        //   docs/kb/gen3-ram-touch.md:48-49. VERIFIED-SYM.
	uint32_t spriteCoordOff;// gSpriteCoordOffsetX (s16); Y = this + 2. D1.4: EM sym:20-21,
	                        //   FR/LG sym:23-24. LOGGING/diagnostic ONLY in M0-M2 — the SHIPPED
	                        //   sub-tile source is fieldCamera+0x10/+0x14 (D1.3), which is already
	                        //   in this struct and already hardware-exercised by build_depth_grid.
	                        //   What this buys is the scripted CAMERA PAN term
	                        //   (gSpriteCoordOffset = gTotalCameraPixelOffset - sCameraPan,
	                        //   pokeemerald src/field_camera.c:459-462 == pokefirered :521-527);
	                        //   promote it into the anchor math only if a hardware run shows a
	                        //   pan-time misalignment (SPEC-data Open Q4). VERIFIED-SYM.
	uint32_t hbCtr;         // gMain.vblankCounter2 = gMain+0x24 — the presence liveness/wedge
	                        //   heartbeat (D1.5), incremented UNCONDITIONALLY in VBlankIntr in BOTH
	                        //   engines (pokeemerald src/main.c:355, pokefirered src/main.c:396).
	                        //   NOT vblankCtr: FR/LG's gMain+0x20 is declared `u32 *vblankCounter1`
	                        //   (pokefirered include/main.h:26) and is NULL unless SetVBlankCounter
	                        //   armed it, so the D2/D3 columns above are silently disarmed on the
	                        //   user's FireRed (a real, REPORTED-not-silently-changed defect in a
	                        //   LOGGING-ONLY path — SPEC-data D1.5.1 / Open Q1; presence routes
	                        //   around it instead of editing what a diagnostics column means
	                        //   mid-flight). gMain bases are themselves VERIFIED-SYM now: EM
	                        //   0x030022C0 (sym:894), FR/LG 0x030030F0 (sym:745) == mainCb2-4, which
	                        //   also retires the "derived" caveat on the vblankCtr ADDRESS above.
	                        //   VERIFIED-SYM base + VERIFIED-SRC offset (include/main.h:8-33).
	// --- phase 18 smart-touch warp routing (SPEC-door T4.1). gMapHeader again, but a SEPARATE
	// field from `mapHeader` above ON PURPOSE: main.c:885 gates the phase-14 HD-2D metatile-layer
	// depth path on `!p->mapHeader`, so filling that zero for FR/LG would silently switch on an
	// untested 3D path for two games — a render change this phase is forbidden to make. Values are
	// VERIFIED-SYM against pret's byte-matched `symbols` branch, re-read 2026-08-12:
	//   BPEE 0x02037318 (pokeemerald.sym "gMapHeader")
	//   BPRE 0x02036DFC (pokefirered.sym AND pokefirered_rev1.sym — identical; the user's FR is rev1)
	//   BPGE 0x02036DFC (pokeleafgreen.sym AND pokeleafgreen_rev1.sym, which live on the
	//                    pokefirered `symbols` branch — LG's OWN map, not FR-derived)
	// Note the trap the house rule catches: the plausible derivation `gObjectEvents - 0x38` holds
	// in Emerald and is WRONG for FRLG (it gives 0x02036E00). 0 = not mapped -> fieldpath.c
	// classifies nothing and the router keeps its pre-phase-18 behaviour. Appending is the only
	// safe edit here: PROFILES[] in gamestate.c is POSITIONAL-initialised.
	uint32_t mapHeaderPath; // gMapHeader for warp classification (MapLayout -> Tileset -> attrs)
	// --- phase 18 Ruby/Sapphire support (SPEC-coop P3.2). "RS is Emerald with different
	// addresses" is FALSE in exactly one structural way: Ruby/Sapphire have NO gSaveBlock1Ptr /
	// gSaveBlock2Ptr — the pointer indirection is an Emerald/FRLG-era change. pokeruby declares
	// `extern struct SaveBlock1 gSaveBlock1;` (include/global.h:668,756) and the symbol maps carry
	// the STRUCT, not a pointer to it: pokeruby.sym:106 `02025734 g 00003ac0 gSaveBlock1`,
	// :105 `02024ea4 g 00000890 gSaveBlock2`, and there is no *Ptr symbol in any of the four maps
	// (pokeruby / pokesapphire / pokeruby_rev1 / pokesapphire_rev1, all re-read + diffed
	// 2026-08-13: 727 RAM symbols, ZERO differences across all four).
	//   0 = sb1ptr/sb2ptr are POINTERS to deref (BPEE/BPRE/BPGE — unchanged behaviour)
	//   1 = sb1ptr/sb2ptr ARE the struct addresses (AXVE/AXPE)
	// Ignoring this is not a cosmetic bug: game_read's `(deref >> 24) == 0x02` validity test would
	// read SaveBlock1.pos (x | y<<16) as a pointer, sb1Valid would be false forever, and presence
	// would report OFF_FIELD on every frame. Exactly TWO call sites branch on it (game_read here
	// and ident_refresh in presence_read.c) — the only two `grep sb1ptr|sb2ptr source/` finds.
	// Appending is again the only safe edit: PROFILES[] is POSITIONAL-initialised.
	uint8_t  sbDirect;      // 1 = sb1ptr/sb2ptr are the structs themselves (RS), 0 = pointers
	// --- phase 20 peer sprite (docs/phase20-peersprite/SPEC.md S1.3). The three addresses that
	// turn `gObjectEvents[0].spriteId` into the peer's ACTUAL currently-displayed 16x32 trainer
	// frame. All VERIFIED-SYM against pret's byte-matched `symbols` branch, re-derived from the
	// nine maps on this machine on 2026-08-13 (the house rule: never ship someone else's word, not
	// even a spec's — the values below were re-read, not copied):
	//   gSprites            EM 0x02020630 | FR 0x0202063C = _rev1 | LG 0x0202063C = _rev1
	//                       RS 0x02020004 (pokeruby / pokesapphire / both rev1 — 4/4 AGREE)
	//   gPlttBufferUnfaded  EM 0x02037714 | FR 0x020371F8 = _rev1 | LG 0x020371F8 = _rev1
	//                       RS 0x0202EAC8 x4                        (4/4 AGREE)
	//   gPlayerAvatar       EM 0x02037590 (size 0x24) | FR/LG 0x02037078 (size 0x20, both revs)
	//                       RS 0x0202E858 (size 0x24) x4            (4/4 AGREE)
	// gSprites' symbol SIZE is 0x1144 == 65 * 0x44 (MAX_SPRITES + 1) in ALL NINE maps, which is
	// itself strong evidence `struct Sprite` is unchanged across RS / FRLG / Emerald.
	//
	// Every 0 is a NAMED degradation, never a guess: `sprites == 0` => that game falls back to the
	// phase-15 placeholder with reason PSPR_R_NOPROF; `plttUnfaded == 0` => the OBJ palette is read
	// from hardware PLTT 0x05000200 instead (which carries the peer's screen fades — a documented,
	// worse-but-correct source, SPEC S1.7); `playerAvatar == 0` => the spriteId cross-check is
	// SKIPPED rather than guessed. All five shipped profiles get real values, so no game degrades.
	//
	// Ruby/Sapphire stay VERIFIED-SYM / VERIFY-ON-HW for the same reason phase 18 gave: no RS ROM
	// exists on this machine. Appending is again the only safe edit — PROFILES[] is POSITIONAL.
	uint32_t sprites;       // gSprites[65], stride 0x44 (oam @+0x00, animNum/animCmdIndex @+0x2A,
	                        //   inUse/invisible @+0x3E, subspriteTableNum @+0x42)
	uint32_t plttUnfaded;   // gPlttBufferUnfaded u16[512]: BG banks 0-15 then OBJ banks 0-15, so
	                        //   OBJ bank n colour i is at +512 + 32*n + 2*i BYTES
	uint32_t playerAvatar;  // gPlayerAvatar (+0x00 flags, +0x04 spriteId) — the cross-check only
} GameProfile;

// One-pass snapshot of the live game.
typedef struct {
	bool    valid;
	GameCtx ctx;
	int     px, py;          // player tile (-1 if SaveBlock pointer not ready)
	int     actionCursor;    // 0..3 or -1
	int     moveCursor;      // 0..3 or -1
	bool    moveValid[4];
	// party
	int     partyCount;      // gPlayerPartyCount, -1 if N/A
	int     partyLayout;     // 0=SINGLE 1=DOUBLE 2=MULTI, -1 if N/A
	// double-battle target
	int     battlersCount;   // -1 if N/A
	uint8_t absentMask;      // gAbsentBattlerFlags
	uint8_t battlerPos[4];   // gBattlerPositions[0..3]
	// bag (live list-task base, computed in game_read; 0 if N/A)
	uint32_t bagListTaskBase; // gTasks + 40*listTaskId + 8 (+24 scroll, +26 row)
	bool     textDlg;         // overworld: a field textbox is up (sFieldMessageBoxMode != 0)
	bool     textBanner;      // overworld: the map-name banner task is live
	// --- instrumentation (LOGGING ONLY; never gate touch/3D/gameplay on these) ---
	uint32_t cb1, cb2;        // raw gMain.callback1/callback2 (Thumb bit stripped) = the screen fingerprint.
	                          // cb2 is THE value an undetected screen (pokedex/townmap/summary/card/keyboard/
	                          // title) reveals when you visit it on hw, to be promoted into a profile later.
	bool     sb1Valid;        // gSaveBlock1Ptr deref valid (save loaded; px/py/map*/obj* meaningful)
	bool     ctxResolved;     // ctx came from a POSITIVE battle/menu match (true) vs the bare overworld
	                          // fall-through where undetected screens hide (false -> inspect cb2)
	int      mapGroup, mapNum;// SaveBlock1.location (-1 if sb1 not ready) — which map (for the NPC overlay)
	int      objX, objY;      // gObjectEvents[0].currentCoords true avatar tile (-1 if slot inactive)
	int      facing;          // gObjectEvents[0] facing 1=D 2=U 3=L 4=R (-1 if N/A) — verify-on-hw offset
	uint8_t  nTask;           // count of active gTasks func ptrs captured in taskFp[]
	uint32_t taskFp[8];       // active task func pointers (Thumb stripped) — IDs ambiguous-callback2 screens
	// --- link-error diagnostics (LOGGING ONLY) — see GameProfile link* fields ---
	uint32_t linkStatus;      // gLinkStatus (live bitfield)
	uint32_t linkErrBuf0, linkErrBuf1;   // sLinkErrorBuffer[0..3] / [4..7] (latched status + queue counts)
	uint32_t linkNotRecv;     // gRemoteLinkPlayersNotReceived
	uint8_t  linkErr;         // gLinkErrorOccurred (1 = the game flagged a link error)
} GameState;

// Optional 3D-effect health, logged alongside the TOP game's row (pass NULL for the bottom game).
// Mirrors the main.c DepthSnap scalars so a wrong-looking 3D pop can be correlated with the screen.
// Newer per-sprite disparity-detail fields are APPENDED (size-tolerant, like the settings loader):
// callers that don't set them leave them 0, and the dump prints them in trailing columns so an old
// reader still parses the leading ones. Disparities are in px-at-FULL-slider (the pop_eye unit before
// it multiplies by eyeSl), so the values are slider-independent (a wrong pop is visible regardless).
typedef struct {
	uint8_t overworld, textTop, textBot;   // depth gating flags
	short   nspr, nui, nfg;                // on-screen sprite / BG0-panel / foreground-tile counts
	float   maxd;                          // strongest in-view stereoscopic depth
	short   camX, camY;                    // gFieldCamera sub-tile scroll
	// --- per-sprite stereoscopic-disparity detail (LOGGING ONLY) — proves the 3D EFFECT, not just counts.
	// All px @ full slider; mirror pop_eye's feet base = RAMP_AT(fy)+floorD and head = base+POP3D_STANDUP. ---
	float   feetMin, feetMax;              // min/max grounded-feet disparity across on-screen sprites
	float   headMin, headMax;              // min/max head disparity (feet + standup, clamped)
	short   tallOk, tallFail;              // sprites where head exceeds feet by ~POP3D_STANDUP (tall-is-taller) vs not
	uint8_t orderOk;                       // 1 = on-screen set is MONOTONIC in screen-y vs feet disparity (front-is-front)
	uint8_t s3d;                           // 1 = stereoscopic 3D engaged this frame (slider>thresh & not in menu); 0 = flat
	// --- phase 14 HD-2D tilt (LOGGING ONLY; SPEC-integration I6.4) — the level/angle actually in
	// force, so a hardware photo of a tilted screen can be read against the gate that allowed it.
	// BOTH screens ride the TOP row because gs_log_sample is only called with depth != NULL for
	// screen 0 (main.c). Appended, size-tolerantly, exactly as the header note above licenses.
	// Values are the previous frame's settled tween (the gs sample runs in the parked window, the
	// tween is stepped later in the render phase) — i.e. the angle of the frame just displayed.
	// NOTE: no tilt EDGE trigger was added (I6.6) — the ring is edge-triggered on ctx/cb2/link plus
	// a 600-frame and a 2 s heartbeat; tilt changes are already correlated with the ctx edges that
	// cause them, and a tween edge would emit ~15 rows per transition into a 1024-entry ring. ---
	uint8_t tiltLvl;                       // effective (clamped) level in force on the TOP screen, 0..3
	float   tiltAngTop, tiltAngBot;        // tweened angle in DEGREES per screen (0 = flat)
	// --- phase 15 co-op presence (LOGGING ONLY; SPEC-avatar A6.5.3) — the PEER of the TOP game,
	// appended size-tolerantly exactly as the phase-14 tilt block above was. This is the surface
	// that actually FIRES in a same-console phase: the D3 CSV's peer columns only write during a
	// wireless session, and gate P-G3 turns presence off for the whole of one (SPEC-data D4.8), so
	// the gs ring is where a "why can I not see my friend" run is read back from. Values are the
	// PREVIOUS frame's solve, like tiltLvl above: this block is stamped in the parked window and
	// presence_solve runs later, in the render phase — i.e. the state of the frame just displayed.
	// Nothing reads these back; they change no behaviour. ---
	uint8_t prLive;                        // presence_liveness: 0 none / 1 connected / 2 active
	uint8_t prDrawn;                       // 1 = the P-G ladder resolved to DRAW this game's peer
	uint8_t prReason;                      // PRES_OFF_* (0 = drawing) — "why not" without a rebuild
	int8_t  prFace;                        // peer facing 1=D 2=U 3=L 4=R (-1 = no record)
	int16_t prMapG, prMapN;                // peer map (-1 = n/a); != this row's mapG/mapN is THE
	                                       //   commonest reason the avatar is absent
	int16_t prPx, prPy;                    // peer tile (-1 = n/a)
	// --- phase 20 peer sprite (docs/phase20-peersprite/SPEC.md S4.3). Appended size-tolerantly
	// again, and for the same reason the block above exists: this is the surface a "the peer is
	// still magenta" run is read back from, and it must say WHY without a rebuild. LOGGING ONLY. ---
	uint8_t  prSprReason;                  // PSPR_R_* (0 = the peer's OWN frame is being drawn)
	uint8_t  prSprW, prSprH;               // the decoded cell size actually in the sheet
	uint16_t prSprGfx;                     // graphicsId — the FORM (normal / bike / surf / ...)
} GsDepth;

// Profile for a core's ROM (by header game code), or NULL if unknown.
const GameProfile* profile_for(GbaCore* c);

// Read the running game's live state into `out`. Returns false (out->valid=false) if no profile.
bool game_read(GbaCore* c, const GameProfile* p, GameState* out);

// --- Game-state instrumentation logger (LOGGING ONLY — never changes gameplay/touch/3D) -----------
// Edge-triggered in-memory ring: gs_log_sample appends a row ONLY when the screen (ctx) or raw
// gMain.callback2 changes, or a ~10s heartbeat elapses, per screen-slot. Flushed to one SD text file
// on gamestate_log_dump. Mirrors the netlog ring + one-shot-dump pattern (no per-frame file I/O).
void        gs_log_reset(void);   // clear the ring + per-slot edge cache (call once when a session starts)
// Phase 14 / SPEC-integration I5.3: stamp the CONSOLE MODEL and whether the 804 MHz + L2 speedup
// actually engaged into the dump header. Tilt is deliberately NOT clamped on the speedup probe (a
// .3dsx from the Homebrew Launcher can never claim it, so clamping would make the effect
// un-iterable on the whole make-and-3dslink dev loop) — instead the fact is recorded, so a slow
// hardware photo is never mistaken for a tilt cost. Call once at startup; header-only, no ring row.
void        gs_log_set_env(int isN3DS, int speedupActive);
// nowMs = a wall-clock millisecond stamp (osGetTime). Drives a WALL-CLOCK heartbeat so a FROZEN/STUCK game
// (emulated clock stopped — e.g. FireRed hanging on connect) keeps emitting rows; an emulated-frame heartbeat
// alone goes silent the instant the game stops advancing, hiding exactly the stuck state we want to capture.
void        gs_log_sample(GbaCore* c, const GameProfile* p, const GameState* gs,
                          int screen, uint16_t injKeys, const GsDepth* depth, uint32_t nowMs);  // screen: 0=top/3D, 1=bottom/touch
void        gamestate_log_dump(const char* path);   // flush the ring to an SD file (mkdir's sdmc:/cias/netlogs)
const char* gamestate_ctx_name(int ctx);            // GameCtx -> short name (for the log + HUD)
