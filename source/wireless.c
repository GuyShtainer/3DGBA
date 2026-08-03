// wireless.c — M1 wireless lobby: host a link / scan + join one / show the live seat map.
// Pure libctru + citro2d on top of the netlink UDS transport; no mGBA. The emulation link
// itself lands in a later milestone (M2.5+). See docs/kb/wireless-link-architecture.md.
//
// UI redesign v2 (1:1, screenshots 08 / wireless-connected / wireless-online):
// TOP = "Wireless Link" + a mono status line, a 2x2 bordered SEAT MAP (gold host / blue seat /
// dim open; name + role + game code + a match flag), and RTT/LOSS stat tiles (green when live).
// BOTTOM = "LOCAL · SAME ROOM (UDS)" Host/Scan, an "OR · OVER THE INTERNET" divider with the
// online button (the relay transport doesn't exist yet -> disabled), per-state screens, and
// "« back to menu". The session LOGIC (host/join/close, canStart, the APT-suspend fallback,
// the startLink return contract) is byte-for-byte the pre-redesign behaviour.
#include <string.h>
#include <stdio.h>
#include <3ds.h>
#include "wireless.h"
#include "netlink.h"
#include "theme.h"
#include "ui.h"
#include "assets.h"
#include "fingerprint.h"   // D6 link-surface fingerprint (pure C)
#include "celiolink.h"     // ...for CL_PROTO_REV only — the single source of truth for our FSM's
                           // wire revision. No FSM call is made from the lobby.

static const char* game_name(const char* code) {
	if (!strncmp(code, "BPEE", 4)) return "Emerald";
	if (!strncmp(code, "BPRE", 4)) return "FireRed";
	if (!strncmp(code, "BPGE", 4)) return "LeafGreen";
	if (!strncmp(code, "AXVE", 4)) return "Ruby";
	if (!strncmp(code, "AXPE", 4)) return "Sapphire";
	return code[0] ? code : "?";
}

int wireless_lobby_run(C3D_RenderTarget* top, C3D_RenderTarget* bot, C2D_TextBuf txtBuf,
                       const char* myGameCode, uint8_t myGameRev) {
	char myCode[5] = { 0 };
	if (myGameCode) memcpy(myCode, myGameCode, 4);

	// D6 (SPEC-suite-hardening.md §D6): OUR link surface. Built once here, published to the
	// transport BEFORE every host/join so the lobby pump can start piggybacking it on the ping
	// cadence; compared against the peer's below. Nothing in it is refuse-grade in v0 — the whole
	// point is to turn "the hardware run just didn't work" into a named field on screen.
	DgbaFprint myFp;
	dgba_fprint_fill(&myFp, myCode, myGameRev, CL_PROTO_REV, DGBA_PROTO);
	u64 myFpHash = dgba_fprint_hash(&myFp);
	char fpLine[96] = "";      // the HUD verdict line ("" until a peer surface arrives)
	int  fpVerdict  = -1;      // -1 unknown, 0 match, >0 = number of differing fields
	int  fpShown    = -2;      // last verdict echoed into status[] (so we echo a DIFF once, not per frame)

	gfxSet3D(false);   // 2D lobby; restore stereo on exit so the games look right again

	int phase = 0;          // 0 = Host/Join menu, 1 = hosting, 2 = scan list, 3 = joined
	int sel = 0;
	DgbaLobby lobbies[8]; int nLob = 0; int rescan = 0;
	char status[64] = "";
	char peerCode[5] = { 0 };   // the joined host's game code (captured at join; "" when hosting)
	int  startLink = 0;     // 0 = none yet; 1 = start as host (seat 0); 2 = start as joiner (seat 1)
	bool canStart = false;  // true while phase 1/3 has 2 connected nodes (set from net_lobby_status below)

	while (aptMainLoop()) {
		hidScanInput();
		u32 kd = hidKeysDown();
		touchPosition tp = { 0, 0 };
		if (kd & KEY_TOUCH) hidTouchRead(&tp);
		bool avail = netlink_available();

		if (phase == 0) {                                   // ---- mode menu ----
			if (kd & (KEY_DDOWN | KEY_CPAD_DOWN)) sel = (sel + 1) % 3;
			if (kd & (KEY_DUP   | KEY_CPAD_UP))   sel = (sel + 2) % 3;
			int act = -1;
			if (kd & KEY_A) act = sel;
			if (kd & KEY_TOUCH) {
				// button rects mirror the phase-0 draw below
				if (tp.px >= 12 && tp.px < 308) {
					if      (tp.py >= 22  && tp.py < 56)  { sel = 0; act = 0; }   // Host
					else if (tp.py >= 64  && tp.py < 98)  { sel = 1; act = 1; }   // Scan
					else if (tp.py >= 126 && tp.py < 156)                          // Online (not yet)
						snprintf(status, sizeof status, "Online server: not yet — UDS local only");
					else if (tp.py >= 188 && tp.py < 210) { sel = 2; act = 2; }   // back to menu
				}
			}
			if (kd & KEY_B) break;
			if (act == 0) {                                  // Host
				if (!avail) snprintf(status, sizeof status, "Wireless off — install + run the .CIA");
				else {
					net_fprint_set_local(&myFp, myFpHash);   // D6: publish OUR surface BEFORE the session exists
					if (net_session_host(myCode, 0, 4)) {
						phase = 1; peerCode[0] = '\0'; status[0] = '\0';
						fpVerdict = -1; fpShown = -2; fpLine[0] = '\0';
					}
					else snprintf(status, sizeof status, "Host failed");
				}
			} else if (act == 1) {                           // Join -> scan
				if (!avail) snprintf(status, sizeof status, "Wireless off — install + run the .CIA");
				else { phase = 2; sel = 0; rescan = 0; nLob = 0; status[0] = '\0'; }
			} else if (act == 2) break;                      // Back
		} else if (phase == 1) {                            // ---- hosting ----
			if (kd & KEY_B) { net_session_close(); phase = 0; sel = 0; }
			else if ((kd & KEY_X) && canStart) { startLink = 1; break; }   // host = seat 0; leave session UP
			else if (kd & KEY_TOUCH) {
				if (canStart && tp.px >= 24 && tp.px < 296 && tp.py >= 96 && tp.py < 132) { startLink = 1; break; }
				if (tp.px >= 24 && tp.px < 296 && tp.py >= 140 && tp.py < 162) { net_session_close(); phase = 0; sel = 0; }
				else if (tp.px >= 12 && tp.px < 308 && tp.py >= 214 && tp.py < 236) break;   // back to menu
			}
		} else if (phase == 2) {                            // ---- scan list ----
			if (--rescan <= 0) { nLob = net_lobby_scan(lobbies, 8); rescan = 60; if (sel >= nLob) sel = nLob ? nLob - 1 : 0; }
			if (nLob > 0) {
				if (kd & (KEY_DDOWN | KEY_CPAD_DOWN)) sel = (sel + 1) % nLob;
				if (kd & (KEY_DUP   | KEY_CPAD_UP))   sel = (sel - 1 + nLob) % nLob;
			}
			int join = -1;
			if ((kd & KEY_A) && nLob > 0) join = sel;
			if (kd & KEY_TOUCH) {
				if (tp.px >= 12 && tp.px < 308 && tp.py >= 210 && tp.py < 236) { phase = 0; sel = 1; }   // back FIRST
				else if (tp.px >= 12 && tp.px < 308) {
					int visLob = nLob > 4 ? 4 : nLob;             // 4 cards fit above the back button
					for (int i = 0; i < visLob; i++) {
						float by = 40.0f + i * 38.0f;
						if (tp.py >= by && tp.py < by + 34.0f) { sel = i; join = i; }
					}
				}
			}
			if (kd & KEY_B) { phase = 0; sel = 1; }
			if (join >= 0) {
				memcpy(peerCode, lobbies[join].gameCode, 5);   // remember the host's code for the seat map
				net_fprint_set_local(&myFp, myFpHash);         // D6: publish OUR surface BEFORE connecting
				if (net_session_join(join)) {
					phase = 3; status[0] = '\0';
					fpVerdict = -1; fpShown = -2; fpLine[0] = '\0';
				}
				else snprintf(status, sizeof status, "Join failed");
			}
		} else {                                            // ---- joined ----
			if (kd & KEY_B) { net_session_close(); phase = 0; sel = 1; }
			else if ((kd & KEY_X) && canStart) { startLink = 2; break; }   // joiner = seat 1; leave session UP
			else if (kd & KEY_TOUCH) {
				if (canStart && tp.px >= 24 && tp.px < 296 && tp.py >= 96 && tp.py < 132) { startLink = 2; break; }
				if (tp.px >= 24 && tp.px < 296 && tp.py >= 140 && tp.py < 162) { net_session_close(); phase = 0; sel = 1; }
				else if (tp.px >= 12 && tp.px < 308 && tp.py >= 214 && tp.py < 236) break;   // back to menu
			}
		}

		DgbaConn conn2; bool haveConn = false;
		int rtt = -1, drops = 0, busy = 0;
		canStart = false;
		if (phase == 1 || phase == 3) {
			haveConn = net_lobby_status(&conn2);
			if (haveConn && conn2.totalNodes >= 2) { net_ping_update(&rtt, &drops, &busy); canStart = true; }   // M2 RTT + arm Start-link
			// D6: the peer's surface arrives on that same pump. Verdict = MATCH / the NAMED differing
			// fields / unknown-until-it-arrives (never a silent "assume compatible"). Allow-with-warning
			// in v0 (PHASE.md D6): nothing here blocks the link — EM<->FR and FR rev0<->rev1 trade for
			// real, so refusing on a diff would be the gen1recomp #511 false-incompatibility bug.
			u8 peer8[NET_FPRINT_BYTES]; u64 peerHash = 0;
			if (net_fprint_peer(peer8, &peerHash)) {
				DgbaFprint pf; memcpy(&pf, peer8, sizeof pf);
				char d[64];
				fpVerdict = dgba_fprint_diff(&myFp, &pf, d, (int)sizeof d);
				if (fpVerdict == 0) snprintf(fpLine, sizeof fpLine, "link-surface: MATCH");
				else                snprintf(fpLine, sizeof fpLine, "link-surface DIFF: %s", d);
				if (fpVerdict > 0 && fpShown != fpVerdict) {   // echo a DIFF into the status line ONCE
					snprintf(status, sizeof status, "Link surface DIFF: %.40s", d);   // status[] is 64B
					fpShown = fpVerdict;
				}
			}
		}
		// The APT suspend hook drops the UDS session on any HOME press; if it did, fall back to the menu so
		// a resumed lobby doesn't show a phantom HOSTING/JOINED for a dead link.
		if ((phase == 1 || phase == 3) && !net_session_active()) {
			phase = 0; sel = 0;
			snprintf(status, sizeof status, "Link closed (left for HOME). Re-host or re-join.");
		}

		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		C2D_TextBufClear(txtBuf);
		bool conn = (phase == 1 || phase == 3) && canStart;

		// =============== TOP: plate (title + RTT/LOSS tiles) + seat cards + values ===============
		C2D_TargetClear(top, g_ui.bg); C2D_SceneBegin(top);
		assets_draw_plate(conn ? "wless-conn-top" : "wless-idle-top");
		{	// transport + status, right-aligned (manifest ~x226..384 y16)
			const char* st = (phase == 1) ? (canStart ? "2 seats linked" : "hosting...")
			               : (phase == 3) ? (canStart ? "2 seats linked" : "connecting...")
			               : (phase == 2) ? "scanning" : "standalone";
			char line[64]; snprintf(line, sizeof line, "UDS · local · %s", st);
			float w = assets_text_w(txtBuf, FNT_JBM_MED, line, 9.0f);
			assets_draw_wgt(canStart ? "dot-green" : "dot-dim", 384.0f - w - 12.0f, 17.0f);
			assets_text_r(txtBuf, FNT_JBM_MED, line, 384.0f, 16.0f, 9.0f, g_ui.acc);
		}
		if (!avail) {
			assets_text(txtBuf, FNT_SG_MED, "Wireless unavailable — install + run the .CIA", 20.0f, 70.0f, 12.0f, g_ui.text);
		} else {	// 2x2 seat cards in the map region (x14 y41 w371 h113)
			int maxN = 4, myNode = -1; bool inLobby = (phase == 1 || phase == 3);
			DgbaConn* c = haveConn ? &conn2 : NULL;
			if (inLobby && c) { maxN = (c->maxNodes>=1 && c->maxNodes<=4)?c->maxNodes:4; myNode = c->myNode; }
			for (int i = 0; i < 4; i++) {
				float sx = 14.0f + (i % 2) * 190.0f, sy = 41.0f + (i / 2) * 61.0f;
				bool occ = inLobby && c && i < maxN && c->names[i][0];
				bool me  = occ && (myNode == i + 1);
				bool dis = i >= maxN;
				u32 bd = occ ? (i == 0 ? g_ui.acc : THEME_GAME_B) : g_ui.line;
				assets_fill9("fill-card-r8", sx, sy, 181.0f, 52.0f, 8.0f);
				ui_border(sx, sy, 181.0f, 52.0f, bd, 1.5f);
				assets_draw_wgt(occ ? (i==0?"dot-gold":"dot-blue") : "dot-dim", sx + 10.0f, sy + 9.0f);
				assets_text(txtBuf, FNT_SG_MED, occ ? (me?"You":c->names[i]) : "—", sx + 22.0f, sy + 6.0f, 12.0f, occ?g_ui.text:g_ui.dim);
				assets_text_r(txtBuf, FNT_JBM_MED, i==0?"HOST":(dis?"":(occ?"SEAT":"OPEN")), sx + 172.0f, sy + 8.0f, 8.0f, g_ui.dim);
				const char* code = occ ? (me ? myCode : (phase==3 && i==0 ? peerCode : NULL)) : NULL;
				assets_text(txtBuf, FNT_JBM_MED, (code&&code[0])?code:(occ?"—":"open"), sx + 10.0f, sy + 30.0f, 8.5f, g_ui.dim);
				if (occ && code && code[0] && myCode[0]) {
					bool m = !strncmp(code, myCode, 4);
					assets_text_r(txtBuf, FNT_JBM_MED, m?"○ match":"× diff", sx + 172.0f, sy + 30.0f, 8.5f, m?THEME_GAME_A:THEME_QUIT_TEXT);
				}
			}
			// RTT / LOSS values inside the baked tiles (manifest x25/x215 y182)
			bool live = inLobby && haveConn && conn2.totalNodes >= 2;
			char v[24];
			if (live) snprintf(v, sizeof v, "%d ms", rtt); else snprintf(v, sizeof v, "—");
			assets_text(txtBuf, FNT_JBM_BOLD, v, 25.0f, 184.0f, 13.0f, live?THEME_GAME_A:g_ui.dim);
			if (live) snprintf(v, sizeof v, "%d %%", drops); else snprintf(v, sizeof v, "—");
			assets_text(txtBuf, FNT_JBM_BOLD, v, 215.0f, 184.0f, 13.0f, live?THEME_GAME_A:g_ui.dim);
		}
		// D6 link-surface verdict (SPEC-suite-hardening.md §D6.6): dim when the two surfaces match,
		// RED and naming the field when they differ. Blank until the peer's surface arrives — an
		// unknown verdict is drawn as nothing here and logged as `unknown` in the netlog, never as a
		// match. Sits just above the status line (below the RTT/LOSS tiles at y182).
		if (fpLine[0] && (phase == 1 || phase == 3))
			assets_text(txtBuf, FNT_JBM_MED, fpLine, 20.0f, 209.0f, 8.5f,
			            fpVerdict > 0 ? THEME_QUIT_TEXT : g_ui.dim);
		if (status[0]) assets_text(txtBuf, FNT_JBM_MED, status, 20.0f, 226.0f, 8.5f, g_ui.acc);

		// =============== BOTTOM: idle actions / scan cards / connected ===============
		C2D_TargetClear(bot, g_ui.bg); C2D_SceneBegin(bot);
		if (phase == 0) {
			assets_draw_plate("wless-idle-bot");
			assets_button(txtBuf, "btn-primary",   13.0f, 33.0f,  293.0f, 40.0f, "Host a session",   FNT_SG_BOLD, 13.0f, g_ui.ink, sel==0);
			assets_button(txtBuf, "btn-secondary", 13.0f, 83.0f,  293.0f, 42.0f, "Scan for lobbies",    FNT_SG_BOLD, 13.0f, g_ui.text, sel==1);
			assets_button(txtBuf, "btn-secondary", 13.0f, 160.0f, 293.0f, 42.0f, "Connect online (soon)", FNT_SG_MED, 12.0f, g_ui.dim, 0);
		} else if (phase == 2) {
			assets_text(txtBuf, FNT_JBM_MED, "NEARBY SESSIONS", 13.0f, 10.0f, 9.0f, g_ui.dim);
			if (nLob == 0) assets_text_c(txtBuf, FNT_SG_MED, "scanning...", 160.0f, 60.0f, 12.0f, g_ui.dim);
			int vis = nLob > 4 ? 4 : nLob;
			for (int i = 0; i < vis; i++) {
				float by = 40.0f + i * 38.0f; bool s = (i == sel);
				bool m = !strncmp(lobbies[i].gameCode, myCode, 4);
				assets_fill9("fill-card-r8", 12.0f, by, 296.0f, 34.0f, 8.0f);
				if (s) ui_border(12.0f, by, 296.0f, 34.0f, g_ui.acc, 1.5f);
				C2D_DrawRectSolid(12.0f, by, 0.0f, 3.0f, 34.0f, m?THEME_GAME_A:THEME_QUIT);
				char line[64]; snprintf(line, sizeof line, "%s · %s", lobbies[i].host[0]?lobbies[i].host:"host", game_name(lobbies[i].gameCode));
				assets_text(txtBuf, FNT_SG_MED, line, 24.0f, by + 4.0f, 12.0f, g_ui.text);
				assets_text(txtBuf, FNT_JBM_MED, m?"○ match":"× different game", 24.0f, by + 20.0f, 8.0f, m?THEME_GAME_A:THEME_QUIT_TEXT);
			}
			assets_button(txtBuf, "btn-ghost", 13.0f, 210.0f, 293.0f, 26.0f, "« back", FNT_SG_MED, 12.0f, g_ui.dim, 0);
		} else {
			assets_draw_plate("wless-conn-bot");
			char line[48];
			if (canStart && rtt >= 0) snprintf(line, sizeof line, "RTT %d ms · loss %d %%", rtt, drops);
			else snprintf(line, sizeof line, "then open the in-game Cable Club to trade");
			assets_text_c(txtBuf, FNT_JBM_MED, line, 160.0f, 44.0f, 9.0f, g_ui.dim);
			assets_button(txtBuf, "btn-accent-outline", 13.0f, 73.0f,  293.0f, 42.0f, "Start linked trade", FNT_SG_BOLD, 13.0f, canStart?g_ui.acc:g_ui.dim, 0);
			assets_button(txtBuf, "btn-ghost",          13.0f, 124.0f, 293.0f, 32.0f, "Leave", FNT_SG_MED, 12.0f, g_ui.dim, 0);
		}

		C3D_FrameEnd(0);
	}

	if (!startLink) net_session_close();   // Start-link leaves the session UP for gameplay
	gfxSet3D(true);   // restore stereo for the game screens
	return startLink;
}
