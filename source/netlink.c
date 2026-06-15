// netlink.c — UDS transport for the wireless dual-GBA link. M1: lobby + seat negotiation only
// (host/scan/join/status/close). The transfer plane (net_transfer_*) and the RX/TX thread land
// in M2/M2.5. Pure libctru; see docs/kb/wireless-link-architecture.md §2a.
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "netlink.h"

// Our app's UDS identity — the scan filter, so we only ever see other 3DGBA lobbies.
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

// --- M2 transfer/ping plane ----------------------------------------------------------------
// The on-wire packet (the emulation link reuses types 0-4 later; M2 uses PING/PONG to measure RTT).
typedef struct __attribute__((packed)) {
	u8  magic;   // 'G'
	u8  type;    // 5 = PING, 6 = PONG
	u8  seat;
	u8  mode;
	u32 round;   // ping sequence
	union { u16 word[4]; u32 normal; u16 send; } d;
} DgbaLinkPkt;   // 16 bytes
#define PK_PING 5
#define PK_PONG 6

#define PING_RING 32
#define PING_EVERY_N 10   // send one ping every 10 frames (~6 Hz @60fps): ample for a latency HUD,
                          // and light enough that the UDS TX buffer never saturates (no "busy" sends).
static u32 s_pingSeq   = 0;
static u32 s_pingFrame = 0;         // frame tick for the ping cadence
static u64 s_pingTick[PING_RING];   // svcGetSystemTick when seq was sent; 0 = free/acked
static int s_pingRtt   = -1;        // last measured round-trip (ms), -1 = none yet
static int s_pingDrops = 0;         // pings whose pong never came back before the slot recycled
static int s_pingSendFails = 0;     // local udsSendTo refusals (TX buffer busy) — NOT an air-loss drop

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

bool net_session_active(void) { return s_up; }

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
	s_pingSeq = 0; s_pingFrame = 0; s_pingRtt = -1; s_pingDrops = 0; s_pingSendFails = 0; memset(s_pingTick, 0, sizeof s_pingTick);
}

// M2: call once per frame while connected. Echoes peers' pings, times our returned pongs, and
// (every PING_EVERY_N frames) fires a fresh ping — unicast to the lone peer when there is one.
// Reports the latest RTT (ms, -1 if none), the cumulative drop count, and local TX-busy refusals.
void net_ping_update(int* rttMs, int* drops, int* sendFails) {
	if (s_inited && s_up) {
		u8 buf[64]; size_t got = 0; u16 src = 0;
		while (R_SUCCEEDED(udsPullPacket(&s_bind, buf, sizeof buf, &got, &src)) && got >= sizeof(DgbaLinkPkt)) {
			const DgbaLinkPkt* pk = (const DgbaLinkPkt*)buf;
			if (pk->magic != 'G') continue;
			if (pk->type == PK_PING) {                       // a peer pinged us -> unicast a pong straight back
				DgbaLinkPkt pong; memset(&pong, 0, sizeof pong);
				pong.magic = 'G'; pong.type = PK_PONG; pong.round = pk->round;
				Result pr = udsSendTo(src, DGBA_DATACHAN, UDS_SENDFLAG_Default, &pong, sizeof pong);
				if (UDS_CHECK_SENDTO_FATALERROR(pr)) s_pingSendFails++;   // benign "TX busy" (0xC86113F0) ignored
			} else if (pk->type == PK_PONG) {                // our ping returned -> measure RTT
				u64 sent = s_pingTick[pk->round % PING_RING];
				if (sent) {
					s_pingRtt = (int)((svcGetSystemTick() - sent) * 1000ull / SYSCLOCK_ARM11);
					s_pingTick[pk->round % PING_RING] = 0;
				}
			}
		}
		// Throttle to ~6 Hz (every PING_EVERY_N frames): keeps the UDS TX buffer unpressured (so
		// udsSendTo stops returning "busy") and a ping's pong always returns long before the 32-slot
		// ring recycles it.
		if (++s_pingFrame % PING_EVERY_N == 0) {
			// Prefer UNICAST to the lone peer: unicast frames are MAC-ACKed + auto-retransmitted, so they
			// survive transient RF loss; UDS broadcast frames are never ACKed (best-effort, lossy) and a
			// client's broadcast double-hops via the host (the host/joined drop asymmetry we measured).
			u16 dst = UDS_BROADCAST_NETWORKNODEID;
			u32 flags = UDS_SENDFLAG_Default | UDS_SENDFLAG_Broadcast;
			udsConnectionStatus st;
			if (R_SUCCEEDED(udsGetConnectionStatus(&st)) && st.total_nodes == 2) {
				for (int node = 1; node <= st.max_nodes; node++)
					if ((st.node_bitmask & (1u << (node - 1))) && node != st.cur_NetworkNodeID) {
						dst = (u16)node; flags = UDS_SENDFLAG_Default; break;   // unicast to the single peer
					}
			}
			u32 seq = ++s_pingSeq;
			int slot = (int)(seq % PING_RING);
			DgbaLinkPkt ping; memset(&ping, 0, sizeof ping);
			ping.magic = 'G'; ping.type = PK_PING; ping.round = seq;
			Result rc = udsSendTo(dst, DGBA_DATACHAN, flags, &ping, sizeof ping);
			if (R_SUCCEEDED(rc)) {
				if (s_pingTick[slot]) s_pingDrops++;             // reusing a still-pending slot = a real lost ping
				s_pingTick[slot] = svcGetSystemTick();           // arm the slot only AFTER the send actually left
			} else {
				s_pingSendFails++;                               // TX busy/refused: nothing sent, don't arm a phantom drop
			}
		}
	}
	if (rttMs) *rttMs = s_pingRtt;
	if (drops) *drops = s_pingDrops;
	if (sendFails) *sendFails = s_pingSendFails;
}

// ---------------------------------------------------------------------------------------------
// M2.5 transfer plane. The net SIO driver (gbacore.c) calls these to exchange one SIO word per
// "round" with the other seat(s). A small ring of rounds each gathers every seat's word; a waiter
// blocks (off the worker's run path) until the needed seats arrive. In LOOPBACK mode the two LOCAL
// cores rendezvous here in-memory with no radio (the one-console M2.5 test); M3 swaps in real UDS.
// ---------------------------------------------------------------------------------------------
#define NET_ROUNDS 8
typedef struct {
	LightLock  lock;
	LightEvent ev;                       // RESET_STICKY: stays signaled so every waiter wakes
	u32  round;
	u32  arrivedMask;
	u16  words[DGBA_MAX_SEATS];
	bool used;
} NetRound;
static NetRound s_rounds[NET_ROUNDS];
static bool s_roundsInit = false;
static bool s_loopback   = false;

static void net_rounds_init(void) {
	if (s_roundsInit) return;
	for (int i = 0; i < NET_ROUNDS; i++) {
		LightLock_Init(&s_rounds[i].lock);
		LightEvent_Init(&s_rounds[i].ev, RESET_STICKY);
		s_rounds[i].used = false;
	}
	s_roundsInit = true;
}

void net_link_set_loopback(bool on) { net_rounds_init(); s_loopback = on; }

void net_transfer_reset(void) {
	net_rounds_init();
	for (int i = 0; i < NET_ROUNDS; i++) {
		LightLock_Lock(&s_rounds[i].lock);
		s_rounds[i].used = false;
		s_rounds[i].arrivedMask = 0;
		LightEvent_Clear(&s_rounds[i].ev);
		LightLock_Unlock(&s_rounds[i].lock);
	}
}

// Merge one seat's word into its round slot and wake any waiter. Shared by the local send path and
// (in M3) the UDS RX path.
static void net_round_merge(int seat, u32 round, u16 word) {
	if (seat < 0 || seat >= DGBA_MAX_SEATS) return;
	NetRound* r = &s_rounds[round % NET_ROUNDS];
	LightLock_Lock(&r->lock);
	if (!r->used || r->round != round) {             // (re)claim the slot for this round
		r->round = round;
		r->arrivedMask = 0;
		r->used = true;
		memset(r->words, 0xFF, sizeof r->words);   // absent seats must read 0xFFFF (matches mGBA lockstep)
		LightEvent_Clear(&r->ev);
	}
	r->words[seat] = word;
	r->arrivedMask |= (1u << seat);
	LightEvent_Signal(&r->ev);
	LightLock_Unlock(&r->lock);
}

void net_transfer_send_word(int seat, int mode, u32 round, u16 send) {
	(void)mode;
	net_rounds_init();
	net_round_merge(seat, round, send);              // loopback: the peer core merges its own word
	if (!s_loopback) {
		// M3: also udsSendTo the WORD packet to the peer(s) over the radio here.
	}
}

bool net_transfer_collect(u32 round, int mode, u16 out[4], u32 needMask, u64 deadline_ms) {
	(void)mode;
	net_rounds_init();
	NetRound* r = &s_rounds[round % NET_ROUNDS];
	u64 deadlineTick = svcGetSystemTick() + (u64)deadline_ms * (SYSCLOCK_ARM11 / 1000ull);
	for (;;) {
		bool done = false;
		LightLock_Lock(&r->lock);
		if (r->round == round && (r->arrivedMask & needMask) == needMask) {
			for (int s = 0; s < 4; s++) out[s] = r->words[s];
			done = true;
		}
		LightLock_Unlock(&r->lock);
		if (done) return true;
		s64 remain = (s64)deadlineTick - (s64)svcGetSystemTick();
		if (remain <= 0) return false;
		LightEvent_WaitTimeout(&r->ev, remain * 1000000000ll / (s64)SYSCLOCK_ARM11);
	}
}

// Non-blocking readiness peek (the child poll uses this so it never blocks): true if the slot
// currently holds `round` and every needMask seat has arrived.
bool net_round_ready(u32 round, u32 needMask) {
	net_rounds_init();
	NetRound* r = &s_rounds[round % NET_ROUNDS];
	LightLock_Lock(&r->lock);
	bool ready = (r->used && r->round == round) && ((r->arrivedMask & needMask) == needMask);
	LightLock_Unlock(&r->lock);
	return ready;
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
