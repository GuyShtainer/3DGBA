// netlink.c — UDS transport for the wireless dual-GBA link. M1: lobby + seat negotiation only
// (host/scan/join/status/close). The transfer plane (net_transfer_*) and the RX/TX thread land
// in M2/M2.5. Pure libctru; see docs/kb/wireless-link-architecture.md §2a.
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "netlink.h"

// Our app's UDS identity — the scan filter, so we only ever see other dual-gba lobbies.
#define DGBA_WLANCOMMID 0x44474241u   // 'DGBA'
#define DGBA_ID8        0x00
#define DGBA_DATACHAN   0x01          // must be non-zero
#define DGBA_PROTO      1
#define DGBA_SHMEM_SZ   0x3000        // udsInit shared-mem (0x1000-aligned; verify headroom on hw)
#define SCAN_BUFSZ      0x4000

// The advertisement broadcast in the host's beacon (read by clients pre-join). <= 0xC8 bytes.
typedef struct __attribute__((packed)) {
	u8   proto;
	char gameCode[4];
	u32  romCrc;
	u8   seatsTotal;
	u8   seatsOpen;
} DgbaAdv;

static bool s_inited = false;     // udsInit succeeded
static bool s_up     = false;     // a network is hosted/joined
static bool s_host   = false;
static udsBindContext s_bind;
static udsNetworkStruct s_scanNet[8];   // cached scanned networks (for net_session_join)
static int  s_scanN = 0;

bool netlink_available(void) { return s_inited; }

bool netlink_init(void) {
	if (s_inited) return true;
	s_inited = R_SUCCEEDED(udsInit(DGBA_SHMEM_SZ, NULL));   // NULL => system username
	return s_inited;
}

void netlink_exit(void) {
	if (!s_inited) return;
	net_session_close();
	udsExit();
	s_inited = false;
}

bool net_session_host(const char* gameCode, u32 romCrc, int seatsTotal) {
	if (!s_inited || s_up) return false;
	if (seatsTotal < 2) seatsTotal = 2; else if (seatsTotal > DGBA_MAX_SEATS) seatsTotal = DGBA_MAX_SEATS;

	udsNetworkStruct net;
	udsGenerateDefaultNetworkStruct(&net, DGBA_WLANCOMMID, DGBA_ID8, (u8)seatsTotal);
	if (R_FAILED(udsCreateNetwork(&net, NULL, 0, &s_bind, DGBA_DATACHAN, UDS_DEFAULT_RECVBUFSIZE)))
		return false;

	DgbaAdv adv = { DGBA_PROTO, { 0, 0, 0, 0 }, romCrc, (u8)seatsTotal, (u8)(seatsTotal - 1) };
	if (gameCode) memcpy(adv.gameCode, gameCode, 4);
	udsSetApplicationData(&adv, sizeof adv);   // best-effort; lobby still works without it

	s_up = true; s_host = true;
	return true;
}

int net_lobby_scan(DgbaLobby* out, int max) {
	if (!s_inited || !out || max <= 0) return 0;
	if (max > (int)(sizeof s_scanNet / sizeof s_scanNet[0])) max = sizeof s_scanNet / sizeof s_scanNet[0];

	void* scanbuf = malloc(SCAN_BUFSZ);
	if (!scanbuf) return 0;
	udsNetworkScanInfo* nets = NULL;
	size_t total = 0;
	Result r = udsScanBeacons(scanbuf, SCAN_BUFSZ, &nets, &total, DGBA_WLANCOMMID, DGBA_ID8, NULL, false);
	if (R_FAILED(r) || !nets) { free(scanbuf); s_scanN = 0; return 0; }

	int n = (total > (size_t)max) ? max : (int)total;
	for (int i = 0; i < n; i++) {
		s_scanNet[i] = nets[i].network;                       // cache for net_session_join(i)
		memset(&out[i], 0, sizeof out[i]);
		DgbaAdv adv; size_t asz = 0;
		if (R_SUCCEEDED(udsGetNetworkStructApplicationData(&nets[i].network, &adv, sizeof adv, &asz))
		    && asz >= sizeof adv) {
			out[i].proto = adv.proto;
			memcpy(out[i].gameCode, adv.gameCode, 4);
			out[i].romCrc = adv.romCrc;
			out[i].seatsTotal = adv.seatsTotal;
			out[i].seatsOpen = adv.seatsOpen;
		}
		char uname[40] = { 0 };                               // host = node[0]
		if (R_SUCCEEDED(udsGetNodeInfoUsername(&nets[i].nodes[0], uname)))
			snprintf(out[i].host, DGBA_NAME_LEN, "%s", uname);
	}
	s_scanN = n;
	free(nets);
	free(scanbuf);
	return n;
}

bool net_session_join(int sel) {
	if (!s_inited || s_up || sel < 0 || sel >= s_scanN) return false;
	if (R_FAILED(udsConnectNetwork(&s_scanNet[sel], NULL, 0, &s_bind,
	             UDS_BROADCAST_NETWORKNODEID, UDSCONTYPE_Client, DGBA_DATACHAN, UDS_DEFAULT_RECVBUFSIZE)))
		return false;
	s_up = true; s_host = false;
	return true;
}

void net_session_close(void) {
	if (!s_up) return;
	if (s_host) udsDestroyNetwork(); else udsDisconnectNetwork();
	udsUnbind(&s_bind);
	s_up = false; s_host = false;
}

bool net_lobby_status(DgbaConn* out) {
	if (!out) return false;
	memset(out, 0, sizeof *out);
	if (!s_inited || !s_up) return false;

	udsConnectionStatus st;
	if (R_FAILED(udsGetConnectionStatus(&st))) return false;
	out->up = true;
	out->host = s_host;
	out->totalNodes = st.total_nodes;
	out->maxNodes = st.max_nodes;
	out->nodeMask = st.node_bitmask;
	out->myNode = st.cur_NetworkNodeID;

	for (int node = 1; node <= DGBA_MAX_SEATS; node++) {
		if (!(st.node_bitmask & (1u << (node - 1)))) continue;
		udsNodeInfo ni; char uname[40] = { 0 };
		if (R_SUCCEEDED(udsGetNodeInformation((u16)node, &ni))
		    && R_SUCCEEDED(udsGetNodeInfoUsername(&ni, uname)))
			snprintf(out->names[node - 1], DGBA_NAME_LEN, "%s", uname);
	}
	return true;
}
