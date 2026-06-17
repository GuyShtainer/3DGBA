// gamestate.c — see gamestate.h. Addresses verified vs pret's byte-matched sym maps (symbols branch),
// US v1.0/rev0. FRLG share one RAM map; LeafGreen's NEW symbols are FireRed-derived (see spec Risks).
#include <string.h>
#include <stdio.h>      // FILE / fprintf (the SD log dump)
#include <sys/stat.h>   // mkdir (ensure the netlogs dir exists)
#include "gamestate.h"

#define BMON_MOVES_OFF 0x0C   // BattlePokemon.moves[] offset (4x u16)
#define PM_TYPE_OFF    0x08   // gPartyMenu: low nibble menuType (0 field/1 battle), bits4-5 layout
#define PM_SLOT_OFF    0x09   // gPartyMenu.slotId (s8)

static const GameProfile PROFILES[] = {
  // code   sb1ptr      battleFlags  actionCur   moveCur     battleMons  bg0y
  //        partyMenu   partyCount  mainCb2     cb2Upd      cb2Init     newKeys
  //        ctrlFuncs   chooseTgt   multiCursor battlerPos  battlersCnt absentFlg   activeBat   mapLayout
  //        startCb     startCbInput sMenuBase  gWindows    startCursor gTasks      cb2BagRun   bagHandler  bagOpen
  { "BPEE", 0x03005D8Cu, 0x02022FECu, 0x020244ACu, 0x020244B0u, 0x02024084u, 0x02022E16u,
            0x0203CEC8u, 0x020244E9u, 0x030022C4u, 0x081B01B0u, 0x081B01E0u, 0x030022EEu,
            0x03005D60u, 0x08057824u, 0x03005D74u, 0x02024076u, 0x0202406Cu, 0x02024210u, 0x02024064u, 0x03005DC0u,
            0x03005DF4u, 0x0809FAC4u, 0x0203CD90u, 0x02020004u, 0x0203760Eu, 0x03005E00u, 0x081AAD5Cu, 0x081ABD28u, 0x00000000u,
            0x081B1370u, 0x080E215Cu, 0x080E2058u, 0x081B3730u, 0x0809FA34u, 0x02037318u,
            0x08038420u, 0x02037350u,
            0x020375BCu, 0x080D487Cu, 0x03005DD0u,
            /* link diag (EM): gLinkStatus gLinkErrorOccurred sLinkErrorBuffer gRemoteLinkPlayersNotReceived */
            0x030030E0u, 0x0300306Cu, 0x02022B00u, 0x03003078u },
  { "BPRE", 0x03005008u, 0x02022B4Cu, 0x02023FF8u, 0x02023FFCu, 0x02023BE4u, 0x02022976u,
            0x0203B0A0u, 0x02024029u, 0x030030F4u, 0x0811EBA0u, 0x0811EBD0u, 0x0303011Eu,
            0x03004FE0u, 0x0802E674u, 0x03004FF4u, 0x02023BD6u, 0x02023BCCu, 0x02023D70u, 0x02023BC4u, 0x03005040u,
            0x020370F0u, 0x0806F280u, 0x0203ADE4u, 0x020204B4u, 0x020370F4u, 0x03005090u, 0x08107EE0u, 0x08108F0Cu, 0x0203AD01u,
            0x0811FB28u, 0x0809CE54u, 0x0809CC98u, 0x08122C5Cu, 0x0806F1F0u, 0x00000000u,
            0x08011100u, 0x02036E38u,
            0x0203709Cu, 0x080981ACu, 0x03005050u,
            /* link diag (FRLG): gLinkStatus gLinkErrorOccurred sLinkErrorBuffer gRemoteLinkPlayersNotReceived */
            0x03003F20u, 0x03003EACu, 0x02022854u, 0x03003EB8u },
  { "BPGE", 0x03005008u, 0x02022B4Cu, 0x02023FF8u, 0x02023FFCu, 0x02023BE4u, 0x02022976u,
            0x0203B0A0u, 0x02024029u, 0x030030F4u, 0x0811EBA0u, 0x0811EBD0u, 0x0303011Eu,
            0x03004FE0u, 0x0802E674u, 0x03004FF4u, 0x02023BD6u, 0x02023BCCu, 0x02023D70u, 0x02023BC4u, 0x03005040u,
            0x020370F0u, 0x0806F280u, 0x0203ADE4u, 0x020204B4u, 0x020370F4u, 0x03005090u, 0x08107EE0u, 0x08108F0Cu, 0x0203AD01u,
            0x0811FB28u, 0x0809CE54u, 0x0809CC98u, 0x08122C5Cu, 0x0806F1F0u, 0x00000000u,
            0x08011100u, 0x02036E38u,
            0x0203709Cu, 0x080981ACu, 0x03005050u,
            /* link diag (FRLG): gLinkStatus gLinkErrorOccurred sLinkErrorBuffer gRemoteLinkPlayersNotReceived */
            0x03003F20u, 0x03003EACu, 0x02022854u, 0x03003EB8u },
};

const GameProfile* profile_for(GbaCore* c) {
	if (!c) return NULL;
	char code[5]; gbacore_game_code(c, code);
	for (unsigned i = 0; i < sizeof PROFILES / sizeof PROFILES[0]; i++)
		if (!strncmp(code, PROFILES[i].code, 4)) return &PROFILES[i];
	return NULL;
}

// True if any active gTasks entry's func == `handler` (Thumb-masked). Robust menu detection.
static bool task_active(GbaCore* c, const GameProfile* p, uint32_t handler) {
	if (!handler) return false;
	for (int t = 0; t < 16; t++) {
		uint32_t task = p->gTasksBase + 40u * (uint32_t)t;
		if (gbacore_read8(c, task + 4) != 0 && (gbacore_read32(c, task + 0) & ~1u) == handler) return true;
	}
	return false;
}

// Find the live bag ListMenu task: scan gTasks (16 entries, 40-byte stride) for the active bag input
// handler, then return its list-task base (gTasks + 40*listTaskId + 8); 0 if none.
static uint32_t find_bag_list_task(GbaCore* c, const GameProfile* p) {
	for (int t = 0; t < 16; t++) {
		uint32_t task = p->gTasksBase + 40u * (uint32_t)t;
		if (gbacore_read8(c, task + 4) == 0) continue;                 // isActive
		if ((gbacore_read32(c, task + 0) & ~1u) != p->bagHandler) continue;
		int16_t listId = (int16_t)gbacore_read16(c, task + 8);          // data[0] = listTaskId
		if (listId < 0 || listId >= 16) return 0;
		return p->gTasksBase + 40u * (uint32_t)listId + 8u;
	}
	return 0;
}

bool game_read(GbaCore* c, const GameProfile* p, GameState* out) {
	memset(out, 0, sizeof *out);
	out->px = out->py = -1; out->actionCursor = out->moveCursor = -1; out->ctx = GCTX_NONE;
	out->partyCount = out->partyLayout = out->battlersCount = -1;
	out->mapGroup = out->mapNum = out->objX = out->objY = out->facing = -1;
	if (!c || !p) return false;
	out->valid = true;

	uint32_t sb1 = gbacore_read32(c, p->sb1ptr);
	out->sb1Valid = (sb1 >> 24) == 0x02;
	if (out->sb1Valid) {
		out->px = (int16_t)gbacore_read16(c, sb1);
		out->py = (int16_t)gbacore_read16(c, sb1 + 2);
		out->mapGroup = gbacore_read8(c, sb1 + 0x04);   // SaveBlock1.location.mapGroup (WarpData)
		out->mapNum   = gbacore_read8(c, sb1 + 0x05);   // SaveBlock1.location.mapNum
	}

	// --- instrumentation (LOGGING ONLY): raw callbacks, player-avatar geo, active-task fingerprint.
	// All read-only; NOTHING below gates touch/3D/gameplay on these — they only feed the SD log. ---
	out->cb2 = gbacore_read32(c, p->mainCb2) & ~1u;            // raw gMain.callback2 = the screen fingerprint
	out->cb1 = gbacore_read32(c, p->mainCb2 - 4u) & ~1u;       // gMain.callback1 (gMain+0 = mainCb2-4)
	out->ctxResolved = true;                                   // default; cleared in the overworld fall-through
	// link-error diagnostics: WHY the game's own link layer aborted (latched in sLinkErrorBuffer even after
	// it enters CB2_PrintErrorMessage). All read-only; never gates anything. 0 addr -> 0 (game not mapped).
	out->linkStatus  = p->linkStatus  ? gbacore_read32(c, p->linkStatus)      : 0;
	out->linkErr     = p->linkErr     ? (uint8_t)gbacore_read8(c, p->linkErr) : 0;
	out->linkErrBuf0 = p->linkErrBuf  ? gbacore_read32(c, p->linkErrBuf)      : 0;
	out->linkErrBuf1 = p->linkErrBuf  ? gbacore_read32(c, p->linkErrBuf + 4u) : 0;
	out->linkNotRecv = p->linkNotRecv ? gbacore_read32(c, p->linkNotRecv)     : 0;
	for (int t = 0; t < 16 && out->nTask < 8; t++) {           // up to 8 active gTasks func ptrs (screen ID)
		uint32_t task = p->gTasksBase + 40u * (uint32_t)t;
		if (gbacore_read8(c, task + 4) == 0) continue;         // isActive
		out->taskFp[out->nTask++] = gbacore_read32(c, task + 0) & ~1u;
	}
	// Sort the fingerprint ascending so the SAME screen produces the SAME (diffable) signature on every
	// visit — gTasks SLOT order is unstable across visits, which would make one screen fingerprint differently.
	for (int i = 1; i < out->nTask; i++) {
		uint32_t v = out->taskFp[i]; int j = i - 1;
		while (j >= 0 && out->taskFp[j] > v) { out->taskFp[j + 1] = out->taskFp[j]; j--; }
		out->taskFp[j + 1] = v;
	}
	if (p->mapObjects && (gbacore_read32(c, p->mapObjects) & 1u)) {     // gObjectEvents[0] active (slot 0 = player)
		out->objX   = (int16_t)gbacore_read16(c, p->mapObjects + 0x10); // currentCoords.x (grid, +7)
		out->objY   = (int16_t)gbacore_read16(c, p->mapObjects + 0x12); // currentCoords.y
		out->facing = gbacore_read8 (c, p->mapObjects + 0x18) & 0x0F;   // facingDirection — offset verify-on-hw (FR/LG base also suspect)
	}

	// 'In battle' = the battle main loop is the active callback2. (gBattleTypeFlags is zeroed at battle
	// SETUP, not end, so it lingers into the overworld -> the old battleFlags test made the whole
	// post-battle field read as a battle dialog = tap-anywhere-A. callback2 returns to the field cleanly.)
	bool inBattle = p->battleMainCb && out->cb2 == p->battleMainCb;

	// Menu detection is TASK-BASED (scan gTasks for the menu's active input handler): robust and
	// FAIL-SAFE — a wrong/absent address just means "not detected" (the overworld still WALKS), never a
	// false-positive that blocks walking. Bag + party run in field AND battle, so they're top-level.
	{ uint32_t lb = find_bag_list_task(c, p);                    // bag (field or battle "ITEM")
	  if (lb) { out->ctx = GCTX_BAG; out->bagListTaskBase = lb; return true; } }
	// sMenu-driven popups (party SUMMARY/SWITCH popup, YES-NO, multichoice) — field OR battle. These all
	// drive sMenu.cursorPos, so GCTX_FIELDMENU's hit-test handles them. Task-detected => fail-safe.
	if (task_active(c, p, p->selMenuTask) || task_active(c, p, p->yesNoTask) || task_active(c, p, p->multiTask)) {
		out->ctx = GCTX_FIELDMENU; return true;
	}
	if (task_active(c, p, p->partyTask)) {                       // party slot pick (field, or battle send-out/use)
		out->ctx = GCTX_PARTY;
		uint8_t mt8 = gbacore_read8(c, p->partyMenu + PM_TYPE_OFF);
		int lay = (mt8 >> 4) & 0x03, cnt = gbacore_read8(c, p->partyCount);
		out->partyLayout = (lay <= 2) ? lay : 0;
		out->partyCount  = (cnt >= 1 && cnt <= 6) ? cnt : 6;
		return true;
	}

	if (!inBattle) {                                            // overworld
		// START menu — detect by its TASK (active only while open). The old callback compare
		// (gMenuCallback==HandleStartMenuInput) false-positived: gMenuCallback isn't cleared on close,
		// so once you opened START, the overworld read as a menu forever -> walk stuck on A (Emerald).
		if (task_active(c, p, p->startMenuTask)) { out->ctx = GCTX_FIELDMENU; return true; }
		out->ctx = GCTX_OVERWORLD;
		out->ctxResolved = false;   // overworld OR an undetected screen that fell through here -> inspect cb2 in the log
		// On-screen field text — precise per-band kill signals for the renderer's DoF. The BG0
		// text-layer scan in main.c is the game-agnostic catch-all; these are exact backups.
		// Fail-safe: unknown address -> false (the BG0 scan still covers it).
		out->textDlg    = p->fieldMsgMode && gbacore_read8(c, p->fieldMsgMode) != 0;
		out->textBanner = task_active(c, p, p->mapNameTask);
		return true;
	}

	// In battle. Target-select is its own controller state — test it first (not by bg0y).
	int bc = gbacore_read8(c, p->battlersCount);
	bool targetSel = false;
	for (int b = 0; b < bc && b < 4; b++)
		if ((gbacore_read32(c, p->ctrlFuncs + 4u * b) & ~1u) == p->chooseTarget) { targetSel = true; break; }
	if (targetSel) {
		out->ctx = GCTX_BATTLE_TARGET;
		out->battlersCount = bc;
		out->absentMask = gbacore_read8(c, p->absentFlags);
		for (int i = 0; i < 4; i++) out->battlerPos[i] = (i < bc) ? gbacore_read8(c, p->battlerPos + i) : 0xFF;
		return true;
	}

	uint16_t bgy = gbacore_read16(c, p->bg0y);
	if (bgy == 160) {
		out->ctx = GCTX_BATTLE_ACTION;
		uint8_t a = gbacore_read8(c, p->actionCursor); out->actionCursor = (a < 4) ? a : -1;
	} else if (bgy == 320) {
		out->ctx = GCTX_BATTLE_MOVE;
		uint8_t m = gbacore_read8(c, p->moveCursor); out->moveCursor = (m < 4) ? m : -1;
		for (int i = 0; i < 4; i++) out->moveValid[i] = gbacore_read16(c, p->battleMons + BMON_MOVES_OFF + i * 2) != 0;
	} else {
		out->ctx = GCTX_BATTLE_OTHER;
	}
	return true;
}

// ============================ game-state instrumentation logger ============================
// LOGGING ONLY — reads the snapshot game_read already produced and records it; never writes game RAM,
// never changes touch/3D/gameplay. Edge-triggered ring + one-shot SD dump (the netlog pattern, verbatim).

static const char* const GS_CTXN[] = {   // index = GameCtx; matches main.c's teal-line names
	"none", "field", "b.act", "b.move", "b.tgt", "party", "fmenu", "bag", "b.oth"
};
const char* gamestate_ctx_name(int ctx) {
	return (ctx >= 0 && ctx < (int)(sizeof GS_CTXN / sizeof GS_CTXN[0])) ? GS_CTXN[ctx] : "?";
}

#define GSLOG_N 1024
#define GS_HEARTBEAT_FRAMES 600u   // ~10s @ 60fps: a liveness/drift row even when nothing changed (EMULATED frames)
#define GS_HEARTBEAT_MS     2000u  // ~2s WALL-CLOCK: keeps logging a STUCK/FROZEN game (emulated clock stopped ->
                                   // the emulated-frame heartbeat can't fire). This is how we capture FireRed's
                                   // immediate hang and Emerald's post-error freeze (cb2 frozen at the error CB).
typedef struct {
	uint32_t frame; uint32_t cb1, cb2;
	uint16_t inj;
	int16_t  px, py, objX, objY;
	int16_t  mapG, mapN;   // u8 source (0..255) -> int16_t so FR/LG map numbers >=128 aren't sign-wrapped; -1 = N/A
	int8_t   face;
	uint8_t  scr, ctx, sb1V, resolved, nTask;
	uint32_t taskFp[8];
	uint8_t  dValid, dOw, dTT, dTB;            // 3D-effect health (top game only)
	int16_t  dNspr, dNui, dNfg, dCamX, dCamY;
	float    dMaxd;
	uint32_t lstat, lbuf0, lbuf1, lnotrecv;    // link-error diagnostics (gLinkStatus / sLinkErrorBuffer / notRecv)
	uint8_t  lerr;                             // gLinkErrorOccurred
} GsLogEntry;
static GsLogEntry s_gsLog[GSLOG_N];
static uint32_t   s_gsLogN = 0;
// per screen-slot (0=top,1=bottom) edge cache: log only when ctx or raw cb2 changes, or the heartbeat fires.
static uint8_t  s_lastCtx[2]   = { 0xFF, 0xFF };
static uint32_t s_lastCb2[2]   = { 0, 0 };
static uint32_t s_lastFrame[2] = { 0, 0 };
static uint8_t  s_lastLinkErr[2] = { 0xFF, 0xFF };   // edge on the link-error flag flipping -> always log the death
static uint32_t s_lastTickMs[2]  = { 0, 0 };          // wall-clock of the last logged row (frozen-game heartbeat)

void gs_log_reset(void) {
	s_gsLogN = 0;
	s_lastCtx[0] = s_lastCtx[1] = 0xFF;
	s_lastCb2[0] = s_lastCb2[1] = 0;
	s_lastFrame[0] = s_lastFrame[1] = 0;
	s_lastLinkErr[0] = s_lastLinkErr[1] = 0xFF;
	s_lastTickMs[0] = s_lastTickMs[1] = 0;
}

void gs_log_sample(GbaCore* c, const GameProfile* p, const GameState* gs,
                   int screen, uint16_t injKeys, const GsDepth* depth, uint32_t nowMs) {
	if (!c || !p || !gs || !gs->valid || screen < 0 || screen > 1) return;
	uint32_t frame = gbacore_frame_counter(c);
	bool edge = (gs->ctx != s_lastCtx[screen]) || (gs->cb2 != s_lastCb2[screen])
	            || (gs->linkErr != s_lastLinkErr[screen])   // capture the exact frame the link error flips
	            || (frame - s_lastFrame[screen] >= GS_HEARTBEAT_FRAMES)
	            || (nowMs - s_lastTickMs[screen] >= GS_HEARTBEAT_MS);   // WALL-CLOCK: keep logging a frozen game
	if (!edge) return;                                 // cheap no-op on the common (unchanged) frame
	s_lastCtx[screen] = (uint8_t)gs->ctx; s_lastCb2[screen] = gs->cb2; s_lastFrame[screen] = frame;
	s_lastLinkErr[screen] = gs->linkErr; s_lastTickMs[screen] = nowMs;

	GsLogEntry* e = &s_gsLog[s_gsLogN % GSLOG_N];
	memset(e, 0, sizeof *e);
	e->frame = frame; e->scr = (uint8_t)screen; e->ctx = (uint8_t)gs->ctx;
	e->sb1V = gs->sb1Valid ? 1 : 0; e->resolved = gs->ctxResolved ? 1 : 0; e->nTask = gs->nTask;
	e->cb1 = gs->cb1; e->cb2 = gs->cb2; e->inj = injKeys;
	e->px = (int16_t)gs->px; e->py = (int16_t)gs->py;
	e->objX = (int16_t)gs->objX; e->objY = (int16_t)gs->objY;
	e->mapG = (int16_t)gs->mapGroup; e->mapN = (int16_t)gs->mapNum; e->face = (int8_t)gs->facing;
	for (int i = 0; i < 8; i++) e->taskFp[i] = gs->taskFp[i];
	if (depth) {
		e->dValid = 1; e->dOw = depth->overworld; e->dTT = depth->textTop; e->dTB = depth->textBot;
		e->dNspr = depth->nspr; e->dNui = depth->nui; e->dNfg = depth->nfg;
		e->dMaxd = depth->maxd; e->dCamX = depth->camX; e->dCamY = depth->camY;
	}
	e->lstat = gs->linkStatus; e->lbuf0 = gs->linkErrBuf0; e->lbuf1 = gs->linkErrBuf1;
	e->lnotrecv = gs->linkNotRecv; e->lerr = gs->linkErr;
	s_gsLogN++;
}

// Decode a GBA key mask (bit order A0 B1 Sel2 St3 Right4 Left5 Up6 Down7 R8 L9; see GBAKEY_* in gbacore.h).
static void gs_keystr(uint16_t k, char* out, int cap) {
	static const char* const N[] = { "A","B","s","S",">","<","^","v","R","L" };
	int n = 0;
	for (int i = 0; i < 10 && n < cap - 2; i++) if (k & (1u << i)) { const char* t = N[i]; while (*t && n < cap - 1) out[n++] = *t++; }
	if (n == 0) out[n++] = '-';
	out[n] = '\0';
}

void gamestate_log_dump(const char* path) {
	if (s_gsLogN == 0) return;           // nothing captured (e.g. non-Pokemon ROMs) -> don't litter SD with empty files
	mkdir("sdmc:/cias", 0777);           // ensure the parent dir exists (ignored if already present)
	mkdir("sdmc:/cias/netlogs", 0777);   // ...and the dedicated netlogs folder (same as the net logger)
	FILE* f = fopen(path, "w");
	if (!f) return;
	fprintf(f, "# 3DGBA game-state log  heartbeat=%u frames  scr: 0=top/3D 1=bottom/touch  (cb1/cb2 = raw gMain callbacks, Thumb-stripped)\n", GS_HEARTBEAT_FRAMES);
	fprintf(f, "# undetected screens (pokedex/townmap/summary/card/keyboard/title) fall through to ctx=field with resolved=0:\n");
	fprintf(f, "# read the cb2 column for each one you visit, then promote that value into a GameProfile later (logging only; no detection wired yet).\n");
	fprintf(f, "# geo: px,py=camera tile; objX,objY=true avatar tile; mapG,mapN=which map; face 1=D 2=U 3=L 4=R (NPC-overlay inputs). inj=injected touch key. d_*=3D-effect health (top rows).\n");
	fprintf(f, "# link: lerr=gLinkErrorOccurred (1=game flagged a link error); lstat=gLinkStatus (live); lbuf0/lbuf1=sLinkErrorBuffer 8B LATCHED at error (lbuf0=status word, lbuf1 low bytes=send/recv queue counts+disconnected); lnotrecv=gRemoteLinkPlayersNotReceived. cb2=0800B1A0(EM)/0800AF2C(FR) = CB2_PrintErrorMessage = the red error screen.\n");
	fprintf(f, "idx,frame,scr,ctx,ctxName,cb1,cb2,sb1V,resolved,px,py,objX,objY,mapG,mapN,face,inj,nTask,t0,t1,t2,t3,t4,t5,t6,t7,d_ow,d_nspr,d_nui,d_nfg,d_maxd,d_camX,d_camY,lerr,lstat,lbuf0,lbuf1,lnotrecv\n");
	uint32_t n    = (s_gsLogN < GSLOG_N) ? s_gsLogN : GSLOG_N;
	uint32_t base = (s_gsLogN < GSLOG_N) ? 0u : (s_gsLogN % GSLOG_N);   // oldest retained entry
	for (uint32_t i = 0; i < n; i++) {
		const GsLogEntry* e = &s_gsLog[(base + i) % GSLOG_N];
		char ks[12]; gs_keystr(e->inj, ks, sizeof ks);
		fprintf(f, "%lu,%lu,%u,%u,%s,%08lX,%08lX,%u,%u,%d,%d,%d,%d,%d,%d,%d,%s,%u,%08lX,%08lX,%08lX,%08lX,%08lX,%08lX,%08lX,%08lX",
		        (unsigned long)i, (unsigned long)e->frame, e->scr, e->ctx, gamestate_ctx_name(e->ctx),
		        (unsigned long)e->cb1, (unsigned long)e->cb2, e->sb1V, e->resolved,
		        e->px, e->py, e->objX, e->objY, e->mapG, e->mapN, e->face, ks, e->nTask,
		        (unsigned long)e->taskFp[0], (unsigned long)e->taskFp[1], (unsigned long)e->taskFp[2], (unsigned long)e->taskFp[3],
		        (unsigned long)e->taskFp[4], (unsigned long)e->taskFp[5], (unsigned long)e->taskFp[6], (unsigned long)e->taskFp[7]);
		if (e->dValid) fprintf(f, ",%u,%d,%d,%d,%.2f,%d,%d", e->dOw, e->dNspr, e->dNui, e->dNfg, e->dMaxd, e->dCamX, e->dCamY);
		else           fprintf(f, ",,,,,,,");   // 7 empty fields to match the 7 d_* header columns
		fprintf(f, ",%u,%08lX,%08lX,%08lX,%08lX\n", e->lerr,                       // link-error diagnostics
		        (unsigned long)e->lstat, (unsigned long)e->lbuf0, (unsigned long)e->lbuf1, (unsigned long)e->lnotrecv);
	}
	fclose(f);
}
