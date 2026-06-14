// wireless.c — M1 wireless lobby: host a link / scan + join one / show the live seat map.
// Pure libctru + citro2d on top of the netlink UDS transport; no mGBA. The emulation link
// itself lands in a later milestone (M2.5+). See docs/kb/wireless-link-architecture.md.
#include <string.h>
#include <stdio.h>
#include <3ds.h>
#include "wireless.h"
#include "netlink.h"
#include "theme.h"

static const char* game_name(const char* code) {
	if (!strncmp(code, "BPEE", 4)) return "Emerald";
	if (!strncmp(code, "BPRE", 4)) return "FireRed";
	if (!strncmp(code, "BPGE", 4)) return "LeafGreen";
	if (!strncmp(code, "AXVE", 4)) return "Ruby";
	if (!strncmp(code, "AXPE", 4)) return "Sapphire";
	return code[0] ? code : "?";
}

static void draw_text(C2D_TextBuf buf, const char* s, float x, float y, float sz, u32 col) {
	C2D_Text t; C2D_TextParse(&t, buf, s); C2D_TextOptimize(&t);
	C2D_DrawText(&t, C2D_WithColor, x, y, 0.0f, sz, sz, col);
}

void wireless_lobby_run(C3D_RenderTarget* top, C3D_RenderTarget* bot, C2D_TextBuf txtBuf,
                        const char* myGameCode) {
	char myCode[5] = { 0 };
	if (myGameCode) memcpy(myCode, myGameCode, 4);

	gfxSet3D(false);   // 2D lobby; restore stereo on exit so the games look right again

	int phase = 0;          // 0 = Host/Join menu, 1 = hosting, 2 = scan list, 3 = joined
	int sel = 0;
	DgbaLobby lobbies[8]; int nLob = 0; int rescan = 0;
	char status[64] = "";

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
			if (kd & KEY_TOUCH) for (int i = 0; i < 3; i++) {
				float by = 70.0f + i * 44.0f;
				if (tp.px >= 40 && tp.px < 280 && tp.py >= by && tp.py < by + 40) { sel = i; act = i; }
			}
			if (kd & KEY_B) break;
			if (act == 0) {                                  // Host
				if (!avail) snprintf(status, sizeof status, "Wireless off — install + run the .CIA");
				else if (net_session_host(myCode, 0, 4)) { phase = 1; status[0] = '\0'; }
				else snprintf(status, sizeof status, "Host failed");
			} else if (act == 1) {                           // Join -> scan
				if (!avail) snprintf(status, sizeof status, "Wireless off — install + run the .CIA");
				else { phase = 2; sel = 0; rescan = 0; nLob = 0; status[0] = '\0'; }
			} else if (act == 2) break;                      // Back
		} else if (phase == 1) {                            // ---- hosting ----
			if (kd & KEY_B) { net_session_close(); phase = 0; sel = 0; }
		} else if (phase == 2) {                            // ---- scan list ----
			if (--rescan <= 0) { nLob = net_lobby_scan(lobbies, 8); rescan = 60; if (sel >= nLob) sel = nLob ? nLob - 1 : 0; }
			if (nLob > 0) {
				if (kd & (KEY_DDOWN | KEY_CPAD_DOWN)) sel = (sel + 1) % nLob;
				if (kd & (KEY_DUP   | KEY_CPAD_UP))   sel = (sel - 1 + nLob) % nLob;
			}
			int join = -1;
			if ((kd & KEY_A) && nLob > 0) join = sel;
			if (kd & KEY_TOUCH) for (int i = 0; i < nLob; i++) {
				float by = 46.0f + i * 30.0f;
				if (tp.py >= by && tp.py < by + 28) { sel = i; join = i; }
			}
			if (kd & KEY_B) { phase = 0; sel = 1; }
			if (join >= 0) {
				if (net_session_join(join)) { phase = 3; status[0] = '\0'; }
				else snprintf(status, sizeof status, "Join failed");
			}
		} else {                                            // ---- joined ----
			if (kd & KEY_B) { net_session_close(); phase = 0; sel = 1; }
		}

		DgbaConn conn; bool haveConn = false;
		if (phase == 1 || phase == 3) haveConn = net_lobby_status(&conn);

		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

		// ---- top screen: title + seat map ----
		C2D_TargetClear(top, THEME_BG); C2D_SceneBegin(top);
		draw_text(txtBuf, "WIRELESS LINK", 16.0f, 12.0f, 0.8f, THEME_GOLD);
		char line[80];
		if (!avail) {
			draw_text(txtBuf, "Wireless unavailable.", 16.0f, 62.0f, 0.6f, THEME_TEXT);
			draw_text(txtBuf, "Install + run the .CIA (needs nwm::UDS).", 16.0f, 88.0f, 0.46f, THEME_DIM);
		} else if (phase == 1 || phase == 3) {
			int maxN = haveConn ? conn.maxNodes : 4, tot = haveConn ? conn.totalNodes : 1;
			snprintf(line, sizeof line, "%s  %s  -  %d/%d players", phase == 1 ? "HOSTING" : "JOINED",
			         game_name(myCode), tot, maxN);
			draw_text(txtBuf, line, 16.0f, 48.0f, 0.55f, THEME_TEXT);
			if (maxN < 1 || maxN > 4) maxN = 4;
			for (int i = 0; i < maxN; i++) {
				bool occ = haveConn && conn.names[i][0];
				bool me  = haveConn && (conn.myNode == i + 1);
				snprintf(line, sizeof line, "P%d  %s%s", i + 1, occ ? conn.names[i] : "(open)",
				         me ? "  <- you" : (i == 0 ? "  (host)" : ""));
				draw_text(txtBuf, line, 28.0f, 80.0f + i * 26.0f, 0.5f, occ ? THEME_TEXT : THEME_DIM);
			}
		} else {
			draw_text(txtBuf, game_name(myCode), 16.0f, 48.0f, 0.55f, THEME_DIM);
			draw_text(txtBuf, "Host a link, or join one nearby.", 16.0f, 76.0f, 0.5f, THEME_DIM);
			draw_text(txtBuf, "Both consoles must run the same game.", 16.0f, 100.0f, 0.44f, THEME_DIM);
		}
		if (status[0]) draw_text(txtBuf, status, 16.0f, 214.0f, 0.45f, THEME_GOLD);

		// ---- bottom screen: actions / scan list ----
		C2D_TargetClear(bot, THEME_BG); C2D_SceneBegin(bot);
		if (phase == 0) {
			static const char* const opts[3] = { "Host a game", "Join a game", "Back" };
			for (int i = 0; i < 3; i++) {
				float by = 70.0f + i * 44.0f; bool s = (i == sel);
				C2D_DrawRectSolid(40.0f, by, 0.0f, 240.0f, 40.0f, s ? THEME_GOLD : THEME_PANEL);
				draw_text(txtBuf, opts[i], 54.0f, by + 12.0f, 0.55f, s ? THEME_SELTXT : THEME_TEXT);
			}
			draw_text(txtBuf, "A select   B back", 8.0f, 224.0f, 0.42f, THEME_DIM);
		} else if (phase == 2) {
			draw_text(txtBuf, "Nearby games:", 8.0f, 8.0f, 0.5f, THEME_GOLD);
			if (nLob == 0) draw_text(txtBuf, "scanning...", 16.0f, 44.0f, 0.5f, THEME_DIM);
			for (int i = 0; i < nLob; i++) {
				float by = 46.0f + i * 30.0f; bool s = (i == sel);
				bool match = !strncmp(lobbies[i].gameCode, myCode, 4);
				C2D_DrawRectSolid(8.0f, by, 0.0f, 304.0f, 28.0f, s ? THEME_GOLD : THEME_PANEL);
				snprintf(line, sizeof line, "%s  -  %s  %s", lobbies[i].host[0] ? lobbies[i].host : "host",
				         game_name(lobbies[i].gameCode), match ? "[ok]" : "[x]");
				draw_text(txtBuf, line, 14.0f, by + 7.0f, 0.46f, s ? THEME_SELTXT : THEME_TEXT);
			}
			draw_text(txtBuf, "A join   B back", 8.0f, 224.0f, 0.42f, THEME_DIM);
		} else {   // hosting / joined
			draw_text(txtBuf, phase == 1 ? "Waiting for players..." : "Connected.", 8.0f, 8.0f, 0.5f, THEME_GOLD);
			draw_text(txtBuf, "Emulation link comes in a later step;", 8.0f, 44.0f, 0.44f, THEME_DIM);
			draw_text(txtBuf, "this proves the lobby + seats.", 8.0f, 66.0f, 0.44f, THEME_DIM);
			draw_text(txtBuf, "B leave", 8.0f, 224.0f, 0.42f, THEME_DIM);
		}

		C3D_FrameEnd(0);
	}

	net_session_close();
	gfxSet3D(true);   // restore stereo for the game screens
}
