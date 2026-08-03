// netlink.c — UDS transport for the wireless dual-GBA link. M1: lobby + seat negotiation only
// (host/scan/join/status/close). The transfer plane (net_transfer_*) and the RX/TX thread land
// in M2/M2.5. Pure libctru; see docs/kb/wireless-link-architecture.md §2a.
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "netlink.h"
#include "diag.h"    // D1 breadcrumbs: pure-C header (no libctru leak) — one volatile store per
                     // wait-loop iteration names WHERE a thread is parked (SPEC-firmware-diag D1.1-D1.2)

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
#define PK_WORD 7   // M3: one seat's SIO word for a round (pk->seat / pk->round / pk->d.send)
#define PK_EVENT     8   // Celio EVENT channel: a fragment of a reliable, in-order ClEvent (DgbaEventPkt)
#define PK_EVENT_ACK 9   // cumulative ACK for the EVENT channel (reuses DgbaLinkPkt: seat + round=ackSeq)

// --- Celio EVENT channel wire layout --------------------------------------------------------------
// PK_WORD stays a dumb (seat,round)-keyed word pipe (untouched). The EVENT channel is a PARALLEL,
// reliable-ordered datagram service for the handful of SEMANTIC ClEvents the Celio FSM ships (party
// chunks + trainer block + tiny control events). A ClEvent (260 B) exceeds the 16-byte DgbaLinkPkt, so
// events get their OWN datagram, fragmented to fit one UDS packet. The RX read buffer (NET_PKT_BUF)
// caps a fragment; with a 12-byte header that leaves NET_EVENT_FRAG_MAX payload bytes per fragment.
//
// DgbaEventPkt header is a fixed 12 bytes; payload[] follows. Every fragment of an event carries the
// FULL descriptor (seq/evType/evArg/evLen/fragCount) so reassembly is idempotent and order-independent
// within an event. A fragment's wire size = NET_EVENT_HDR + (bytes in this fragment); we send only the
// used prefix, so an empty-payload control event is a 12-byte datagram.
#define NET_PKT_BUF        256   // RX/TX packet buffer (was a bare u8[64]); >= one PK_EVENT fragment.
#define NET_EVENT_HDR      12    // DgbaEventPkt header bytes (magic..evLen), before payload[]
#define NET_EVENT_FRAG_MAX (NET_PKT_BUF - NET_EVENT_HDR)   // 244 payload bytes / fragment
typedef struct __attribute__((packed)) {
	u8  magic;      // 'G'   (shared wire magic; type disambiguates from DgbaLinkPkt)
	u8  type;       // PK_EVENT
	u8  seat;       // SENDER's seat (the stream this event belongs to)
	u8  evType;     // ClEvent.type
	u32 seq;        // per-seat monotonically-increasing event sequence (1-based; 0 = none)
	u8  evArg;      // ClEvent.arg
	u8  fragIdx;    // 0..fragCount-1
	u8  fragCount;  // total fragments for this event (>=1)
	u8  rsv;        // reserved/align (0)
	// --- 12 bytes (NET_EVENT_HDR) above; evLen is the first 2 payload-area bytes ---
	u16 evLen;      // ClEvent.len (TOTAL payload length across all fragments)
	u8  payload[NET_EVENT_FRAG_MAX - 2];   // this fragment's payload bytes (evLen counts the WHOLE event)
} DgbaEventPkt;
// Wire offset of the per-fragment payload byte 0 = NET_EVENT_HDR + 2 (after evLen). A fragment carrying
// F payload bytes is (NET_EVENT_HDR + 2 + F) bytes on the wire; F <= NET_EVENT_PAY_MAX.
#define NET_EVENT_PAY_OFF  (NET_EVENT_HDR + 2)              // = 14
#define NET_EVENT_PAY_MAX  (NET_PKT_BUF - NET_EVENT_PAY_OFF)// 242 payload bytes / fragment

#define PING_RING 32
#define PING_EVERY_N 10   // send one ping every 10 frames (~6 Hz @60fps): ample for a latency HUD,
                          // and light enough that the UDS TX buffer never saturates (no "busy" sends).
static u32 s_pingSeq   = 0;
static u32 s_pingFrame = 0;         // frame tick for the ping cadence
static u64 s_pingTick[PING_RING];   // svcGetSystemTick when seq was sent; 0 = free/acked
static int s_pingRtt   = -1;        // last measured round-trip (ms), -1 = none yet
static int s_pingDrops = 0;         // pings whose pong never came back before the slot recycled
static int s_pingSendFails = 0;     // local udsSendTo refusals (TX buffer busy) — NOT an air-loss drop

// --- M3 wireless transport state ------------------------------------------------------------
static LightLock     s_txLock;             // serializes EVERY udsSendTo across main/RX/worker threads
static LightLock     s_rxLock;             // serializes the RX dispatch (one logical pull owner)
static bool          s_locksInit = false;  // one-time init guard (never re-init a held lock)
static Thread        s_rxThread = NULL;
static volatile bool s_rxRun     = false;
static u16           s_peerNode  = UDS_BROADCAST_NETWORKNODEID;   // resolved lone-peer node id
static bool          s_peerResolved = false;                     // false => no unicast target yet
static int           s_wordSendFails = 0;                        // WORD packets that failed to send (counts BUSY too)
static int           s_netBusyN      = 0;                        // cumulative TX-busy retries observed (diag)
static int           s_rxWordN       = 0;   // PK_WORD packets the RX thread received from the peer (0 => RX-empty)
static u32           s_rxMaxSeat0    = 0;   // highest round a SEAT-0 (parent/host) word was merged for
static bool          s_haveSeat0     = false; // any seat-0 word ever seen (on the JOINER: did the host's words arrive?)
// Reliable lockstep: THIS console's current outgoing word (the last one net_transfer_send_word sent),
// atomically packed so the RX thread can RE-SEND it every few ms until the peer responds — a dropped or
// reordered word (UDS is best-effort + out-of-order) thus always eventually arrives. The RX thread is the
// re-sender (NOT the collect loop): the side that completes a round locally isn't in collect, yet must keep
// re-sending its reply until the peer advances. The peer merges duplicates idempotently.
static volatile u64  s_curPacked = 0;   // 0 = none. bit63=valid | seat(bits48-50) | round(bits16-47) | word(bits0-15)
static volatile bool s_collectAbort = false;   // net_transfer_abort sets this so a polling collect returns at once
#define CUR_PACK(seat,round,word) ((1ull<<63) | ((u64)((seat)&7)<<48) | ((u64)(u32)(round)<<16) | (u16)(word))
#define NET_RX_STACK (16 * 1024)

// Transfer-plane ring state (declared here so net_session_close, above the transfer-plane functions,
// can reference it). The ring of rounds each gathers every seat's word; a waiter blocks until the
// needed seats arrive. LOOPBACK = two local cores rendezvous in-memory; M3 = real UDS via the RX thread.
#define NET_ROUNDS 32   // an uncapped WORD burst can lead the slowest collect by several rounds; 8 let
                        // round R+8 evict round R's still-awaited slot (forced 50ms timeout). 32 covers
                        // the worst observed burst lead. NetRound is small, so this is cheap.
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

static void net_locks_init(void) {        // call from net_rounds_init so locks are ALWAYS valid
	if (s_locksInit) return;
	LightLock_Init(&s_txLock);
	LightLock_Init(&s_rxLock);
	s_locksInit = true;
}

// The ONLY caller of udsSendTo anywhere. Serializes the three send sites (main ping, RX pong,
// worker WORD). Returns a non-success sentinel when the link is down so a skipped send is never
// counted as delivered.
static Result net_send_locked(u16 dst, u32 flags, const void* p, size_t n) {
	if (!s_inited || !s_up) return (Result)0xD8E007FA;   // "not sent" sentinel (link down)
	LightLock_Lock(&s_txLock);
	Result r = udsSendTo(dst, DGBA_DATACHAN, (u8)flags, p, n);
	LightLock_Unlock(&s_txLock);
	return r;
}

// (net_send_word_retry removed: its on-WORKER busy-retry (~8ms) starved run_loop under UDS contention and
// stuck the link — net_word_tx now does a single non-blocking try and relies on the RX-thread re-send.)

// Resolve the lone peer's unicast node id. Sets s_peerResolved only when a real (non-broadcast)
// peer node is found — WORD packets MUST go unicast (MAC-ACKed); a broadcast fallback is a hard
// "not ready", never a silent lossy degrade.
static bool net_resolve_peer(void) {
	s_peerNode = UDS_BROADCAST_NETWORKNODEID;
	s_peerResolved = false;
	udsConnectionStatus st;
	if (R_SUCCEEDED(udsGetConnectionStatus(&st)) && st.total_nodes == 2) {
		for (int node = 1; node <= st.max_nodes; node++)
			if ((st.node_bitmask & (1u << (node - 1))) && node != st.cur_NetworkNodeID) {
				s_peerNode = (u16)node; s_peerResolved = true; break;
			}
	}
	return s_peerResolved;
}

bool netlink_available(void) { return s_inited; }

bool netlink_init(void) {
	if (s_inited) return true;
	s_inited = R_SUCCEEDED(udsInit(DGBA_SHMEM_SZ, NULL));   // NULL => system username
	return s_inited;
}

void netlink_exit(void) {
	if (!s_inited) return;
	net_link_stop();        // join RX thread + abort pending rounds: NO thread is in a UDS call now
	net_session_close();    // udsUnbind (s_up flips false first)
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
	s_up = false;                         // flip FIRST: net_send_locked + the RX pump now early-return
	if (s_host) udsDestroyNetwork(); else udsDisconnectNetwork();
	udsUnbind(&s_bind);
	s_host = false;
	s_peerNode = UDS_BROADCAST_NETWORKNODEID; s_peerResolved = false;
	s_loopback = false;                   // restore a known transport state for the next link
	if (s_roundsInit)                     // unblock any worker still parked in collect (belt-and-suspenders)
		for (int i = 0; i < NET_ROUNDS; i++) LightEvent_Signal(&s_rounds[i].ev);
	s_pingSeq = 0; s_pingFrame = 0; s_pingRtt = -1; s_pingDrops = 0; s_pingSendFails = 0; s_wordSendFails = 0; s_netBusyN = 0;
	memset(s_pingTick, 0, sizeof s_pingTick);
}

// Call once per frame while connected. In the LOBBY (no RX thread yet) this owns the pull/echo/RTT
// exactly as M2 did. IN-GAME the RX thread owns the pull, so here we only SEND the throttled ping
// (gated on s_rxRun => no double-pull). All sends go through net_send_locked (s_txLock) so they're
// safe against the RX-thread pong and the worker-thread WORD sends.
void net_ping_update(int* rttMs, int* drops, int* sendFails) {
	if (s_inited && s_up) {
		if (!s_rxRun) {   // LOBBY only: the RX thread isn't pulling, so we do (sole UDS user here)
			u8 buf[NET_PKT_BUF]; size_t got = 0; u16 src = 0;   // sized for a PK_EVENT fragment too (EVENT flows in-game)
			uint32_t wdIt = 0;   // D1 crumb iteration (lobby drain depth this frame)
			while (R_SUCCEEDED(udsPullPacket(&s_bind, buf, sizeof buf, &got, &src)) && got >= sizeof(DgbaLinkPkt)) {
				DIAG_CRUMB(g_diagNetCrumb, DIAG_SITE_NET_LOBBY_DRAIN, wdIt++);   // D1 site 1400 (main thread, lobby only)
				const DgbaLinkPkt* pk = (const DgbaLinkPkt*)buf;
				if (pk->magic != 'G') continue;
				if (pk->type == PK_PING) {                       // a peer pinged us -> unicast a pong straight back
					DgbaLinkPkt pong; memset(&pong, 0, sizeof pong);
					pong.magic = 'G'; pong.type = PK_PONG; pong.round = pk->round;
					Result pr = net_send_locked(src, UDS_SENDFLAG_Default, &pong, sizeof pong);
					if (UDS_CHECK_SENDTO_FATALERROR(pr)) s_pingSendFails++;   // benign "TX busy" (0xC86113F0) ignored
				} else if (pk->type == PK_PONG) {                // our ping returned -> measure RTT
					u64 sent = s_pingTick[pk->round % PING_RING];
					if (sent) {
						s_pingRtt = (int)((svcGetSystemTick() - sent) * 1000ull / SYSCLOCK_ARM11);
						s_pingTick[pk->round % PING_RING] = 0;
					}
				}
			}
			net_resolve_peer();   // keep M2's unicast-once-2-nodes lobby behavior (cheap; lobby has no workers)
		}
		// Throttle to ~6 Hz: keeps the UDS TX buffer unpressured. Unicast to the resolved peer (MAC-ACKed)
		// once known; broadcast (lossy, best-effort) only before the peer is resolved.
		if (++s_pingFrame % PING_EVERY_N == 0) {
			u16 dst   = s_peerResolved ? s_peerNode : UDS_BROADCAST_NETWORKNODEID;
			u32 flags = s_peerResolved ? UDS_SENDFLAG_Default
			                           : (UDS_SENDFLAG_Default | UDS_SENDFLAG_Broadcast);
			u32 seq = ++s_pingSeq;
			int slot = (int)(seq % PING_RING);
			DgbaLinkPkt ping; memset(&ping, 0, sizeof ping);
			ping.magic = 'G'; ping.type = PK_PING; ping.round = seq;
			Result rc = net_send_locked(dst, flags, &ping, sizeof ping);
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
// (The NetRound ring state is declared up top — before net_session_close, which references it.)

static void net_rounds_init(void) {
	net_locks_init();              // M3: locks valid on EVERY entry path (loopback never sends, but harmless)
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
	s_curPacked = 0; s_collectAbort = false;   // fresh link: no pending re-send, not aborting
	s_rxWordN = 0; s_rxMaxSeat0 = 0; s_haveSeat0 = false;   // fresh link transport stats
}

// Merge one seat's word into its round slot and wake any waiter. Shared by the local send path and
// (in M3) the UDS RX path.
static void net_round_merge(int seat, u32 round, u16 word) {
	if (seat < 0 || seat >= DGBA_MAX_SEATS) return;
	NetRound* r = &s_rounds[round % NET_ROUNDS];
	LightLock_Lock(&r->lock);
	if (r->used && (s32)(round - r->round) < 0) {    // a late/reordered word for an OLDER round than the
		LightLock_Unlock(&r->lock); return;          // slot already holds -> ignore (don't clobber a newer round)
	}
	if (!r->used || r->round != round) {             // (re)claim the slot for this round (same or newer)
		r->round = round;
		r->arrivedMask = 0;
		r->used = true;
		memset(r->words, 0xFF, sizeof r->words);   // absent seats must read 0xFFFF (matches mGBA lockstep)
		LightEvent_Clear(&r->ev);
	}
	r->words[seat] = word;
	r->arrivedMask |= (1u << seat);
	if (seat == 0 && (!s_haveSeat0 || (s32)(round - s_rxMaxSeat0) > 0)) { s_rxMaxSeat0 = round; s_haveSeat0 = true; }  // host-word progress (diag)
	LightEvent_Signal(&r->ev);
	LightLock_Unlock(&r->lock);
}

// Put one WORD packet on the wire (unicast to the peer, MAC-ACKed). SINGLE non-blocking try — used by the
// primary send (worker thread) AND the periodic RX-thread re-send. A TX-busy (0xC86113F0) here does NOT
// block: the previous busy-RETRY (up to 64*125us=~8ms) ran on the WORKER thread, so under UDS contention it
// starved gbacore_run_loop and the round's completeEvent never fired -> the link stuck (hardware 0617:
// busy=236, JOIN established=0, stuck at round 1). A dropped/busy word is harmless: s_curPacked holds it and
// the RX thread re-sends every ~4ms (OFF the worker), and the peer's collect waits for the genuine word (no
// 0xFFFF poison before the deadline). So delivery is preserved without ever blocking the emulated clock.
static void net_word_tx(int seat, int mode, u32 round, u16 word) {
	if (s_loopback || !s_up || !s_peerResolved) return;
	DgbaLinkPkt pk; memset(&pk, 0, sizeof pk);
	pk.magic = 'G'; pk.type = PK_WORD; pk.seat = (u8)seat; pk.mode = (u8)mode;
	pk.round = round; pk.d.send = word;
	Result rc = net_send_locked(s_peerNode, UDS_SENDFLAG_Default, &pk, sizeof pk);
	if (rc == (Result)0xC86113F0) s_netBusyN++;      // TX-busy: dropped this try; the RX re-send retries it (not loss)
	else if (R_FAILED(rc)) s_wordSendFails++;        // a real (non-busy) send failure
}

void net_transfer_send_word(int seat, int mode, u32 round, u16 send) {
	net_rounds_init();
	net_round_merge(seat, round, send);              // OUR seat in the ring first (radio-ordering-independent)
	__atomic_store_n(&s_curPacked, CUR_PACK(seat, round, send), __ATOMIC_RELEASE);  // current word for RX re-send
	net_word_tx(seat, mode, round, send);
}

// Re-send THIS console's current outgoing word (called periodically by the RX thread) so a dropped/reordered
// word eventually reaches the peer. Reliable lockstep via repetition; idempotent on the peer. mode is cosmetic.
static void net_resend_current(void) {
	u64 p = __atomic_load_n(&s_curPacked, __ATOMIC_ACQUIRE);
	if (p >> 63) net_word_tx((int)((p >> 48) & 7), 0, (u32)((p >> 16) & 0xFFFFFFFFull), (u16)(p & 0xFFFF));
}

// RELIABLE rendezvous: poll the ring for this exact round's words, RE-SENDING our own word every ~4ms so a
// dropped/reordered peer word eventually arrives (UDS is best-effort + out-of-order). Never fabricates a
// word — it blocks (to a long link-lost deadline) for the GENUINE peer word so the lockstep stays exact and
// the Gen-3 checksum is never poisoned. Poll (not the sticky LightEvent) because our own merge keeps that
// signaled. Abortable via s_collectAbort (teardown). Returns false only on a real link-lost / abort.
bool net_transfer_collect(u32 round, int mode, u16 out[4], u32 needMask, u64 deadline_ms) {
	(void)mode;
	net_rounds_init();
	NetRound* r = &s_rounds[round % NET_ROUNDS];
	u64 deadlineTick = svcGetSystemTick() + (u64)deadline_ms * (SYSCLOCK_ARM11 / 1000ull);
	uint32_t wdIt = 0;   // D1 crumb iteration (wait-loop passes; only stamped when NOT done)
	for (;;) {
		bool done = false;
		LightLock_Lock(&r->lock);
		if (r->round == round && (r->arrivedMask & needMask) == needMask) {
			for (int s = 0; s < 4; s++) out[s] = r->words[s];
			done = true;
		}
		LightLock_Unlock(&r->lock);
		if (done) return true;
		if (s_collectAbort || !s_up) return false;                         // teardown / link down
		if ((s64)deadlineTick - (s64)svcGetSystemTick() <= 0) return false; // genuine link-lost
		DIAG_CRUMB(g_diagNetCrumb, DIAG_SITE_NET_COLLECT, wdIt++);         // D1 site 1000: parked in collect
		svcSleepThread(1000000ll);                                         // 1ms poll (the RX thread re-sends our word)
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

// BRIDGE (state E) non-blocking single-seat read: if the ring slot currently holds `round` and
// `seat`'s word has arrived, copy it to *out and return true; else leave *out untouched, return
// false. Lets the bridge answer a transfer from already-arrived words without parking the worker
// (the collect-block is only used for a REAL round whose peer word hasn't landed yet).
bool net_round_peek_word(u32 round, int seat, u16* out) {
	net_rounds_init();
	if (seat < 0 || seat >= DGBA_MAX_SEATS) return false;
	NetRound* r = &s_rounds[round % NET_ROUNDS];
	bool ok = false;
	LightLock_Lock(&r->lock);
	if (r->used && r->round == round && (r->arrivedMask & (1u << seat))) {
		if (out) *out = r->words[seat];
		ok = true;
	}
	LightLock_Unlock(&r->lock);
	return ok;
}

// BRIDGE (state E) COMMAND collect: a Gen-3 link command is 8 words (one per MULTI transfer) protected
// by a checksum, so the bridge must deliver it ATOMICALLY — a half-real/half-idle command poisons the
// sum (desync-fatal). This waits (bounded) until all 8 rounds [base..base+7] hold `seat`'s word, then
// copies them to out[8]. Returns true on a fully-assembled command, false on timeout (the caller then
// substitutes a whole IDLE command = 8 zeros, which the game reads as receivedNothing and skips —
// recoverable, because the RX thread keeps re-sending our word so a genuinely-late command still lands).
// Polls (the RX thread merges + re-sends our word meanwhile). Abortable via s_collectAbort (teardown).
bool net_cmd_collect(int seat, u32 base, u16 out[8], u64 deadline_ms) {
	net_rounds_init();
	if (seat < 0 || seat >= DGBA_MAX_SEATS) return false;
	u64 deadlineTick = svcGetSystemTick() + (u64)deadline_ms * (SYSCLOCK_ARM11 / 1000ull);
	uint32_t wdIt = 0;   // D1 crumb iteration
	for (;;) {
		int present = 0;
		for (int i = 0; i < 8; i++) { out[i] = 0; if (net_round_peek_word(base + (u32)i, seat, &out[i])) present++; }
		if (present == 8) return true;
		if (s_collectAbort || !s_up) return false;                          // teardown / link down
		if ((s64)deadlineTick - (s64)svcGetSystemTick() <= 0) return false; // timeout -> caller idle-substitutes
		DIAG_CRUMB(g_diagNetCrumb, DIAG_SITE_NET_CMD_COLLECT, wdIt++);      // D1 site 1100: parked in cmd-collect (state E)
		svcSleepThread(1000000ll);                                          // 1ms poll (RX thread merges/re-sends)
	}
}

// BRIDGE (state E) lead-bound probe: the highest round for which `seat`'s word is present in the ring,
// + whether any is (the bridge caps how far it free-runs ahead of the peer so the ring/FIFO never
// overflows — the 26df69a "joiner raced 90s ahead -> desync" lesson, bounded). *out unchanged if none.
bool net_seat_max_round(int seat, u32* out) {
	net_rounds_init();
	if (seat < 0 || seat >= DGBA_MAX_SEATS) return false;
	bool found = false; u32 best = 0;
	for (int i = 0; i < NET_ROUNDS; i++) {
		NetRound* r = &s_rounds[i];
		LightLock_Lock(&r->lock);
		if (r->used && (r->arrivedMask & (1u << seat)) && (!found || (s32)(r->round - best) > 0)) {
			best = r->round; found = true;
		}
		LightLock_Unlock(&r->lock);
	}
	if (found && out) *out = best;
	return found;
}

// PACING BARRIER (joiner side): block until `round` is present with needMask seats, OR an escape
// fires. Mirrors net_transfer_collect's escapes exactly (s_collectAbort, !s_up, deadline) so a gone
// peer / HOME-close never hangs and the RX thread's re-send delivers a dropped word while we wait.
// Copies NO words — the subsequent net_finishMulti->collect reads them; this only gates the joiner's
// emulated clock to the parent's transfer pace (freezing the emulated clock keeps the Gen-3 SLAVE
// VBlank watchdog satisfied). Returns true if the round became ready, false on abort/link-down/
// deadline (the caller then free-runs and net_transfer_collect's link-lost machinery reports the loss).
bool net_round_wait(u32 round, u32 needMask, u64 deadline_ms) {
	net_rounds_init();
	if (net_round_ready(round, needMask)) return true;
	u64 deadlineTick = svcGetSystemTick() + (u64)deadline_ms * (SYSCLOCK_ARM11 / 1000ull);
	uint32_t wdIt = 0;   // D1 crumb iteration
	for (;;) {
		if (s_collectAbort || !s_up) return false;                          // teardown / link down
		if ((s64)deadlineTick - (s64)svcGetSystemTick() <= 0) return false; // genuine link-lost
		DIAG_CRUMB(g_diagNetCrumb, DIAG_SITE_NET_ROUND_WAIT, wdIt++);       // D1 site 1200: parked at the pacing barrier
		svcSleepThread(1000000ll);                                          // 1ms poll (RX thread merges/re-sends)
		if (net_round_ready(round, needMask)) return true;
	}
}

// Monotonic system tick + a ticks->microseconds helper, exposed so the mGBA-side netlog (gbacore.c, which
// stays libctru-free) can stamp each round's WALL-CLOCK and emit a dt_us column without including <3ds.h>.
// Per-round wall time vs the emulated-VBlank (dvbl) stamp separates emulated-clock divergence (the LAG
// hypothesis) from UDS air latency, and HOST dt_us vs JOIN dt_us shows which side is the slow one.
u64 net_mono_ticks(void)        { return svcGetSystemTick(); }
u32 net_ticks_to_us(u64 dticks) { return (u32)(dticks * 1000000ull / SYSCLOCK_ARM11); }
// True if more than `ms` of wall-clock has elapsed since `sinceTick`. Compared in u64 ticks (no u32-microsecond
// overflow — net_ticks_to_us wraps after ~71min), so the joiner pacing gate stays correct over any session.
bool net_older_than_ms(u64 sinceTick, u32 ms) {
	return (svcGetSystemTick() - sinceTick) > (u64)ms * (SYSCLOCK_ARM11 / 1000ull);
}

// CHILD round-from-wire: scan the ring for the lowest parent-stamped (bit0 set) round strictly
// greater than afterRound. The child injects exactly the round the PARENT stamped, so "round N" is
// one wire-defined identity on both consoles; a dropped/extra parent round is skipped cleanly
// instead of gating forever on a private counter the parent never matches. (afterRound==UINT32_MAX
// sentinel: nothing injected yet -> any round whose signed delta is > 0 qualifies.)
bool net_round_next_parent(u32 afterRound, u32* outRound) {
	net_rounds_init();
	bool found = false; u32 best = 0;
	for (int i = 0; i < NET_ROUNDS; i++) {
		NetRound* r = &s_rounds[i];
		LightLock_Lock(&r->lock);
		if (r->used && (r->arrivedMask & (1u << 0)) &&
		    (s32)(r->round - afterRound) > 0 &&
		    (!found || (s32)(r->round - best) < 0)) {
			best = r->round; found = true;
		}
		LightLock_Unlock(&r->lock);
	}
	if (found && outRound) *outRound = best;
	return found;
}

// ==================================================================================================
// Celio EVENT channel — reliable, in-order, exactly-once SEMANTIC events (a PARALLEL plane to PK_WORD).
// ==================================================================================================
// The transport carries opaque ClEvent {u8 type; u8 arg; u16 len; u8 data[256]} blobs (NET_CLEVENT_SIZE
// bytes) — netlink.c never includes celiolink.h; a local NetEvent mirror IS the ClEvent ABI.
//
// GUARANTEES (per seat / per sender stream):
//   * exactly-once  — each event is assigned a strictly-increasing seq; the receiver delivers a given
//                     seq AT MOST once (a duplicate fragment/full event for an already-delivered or
//                     already-buffered seq is ignored).
//   * in-order      — the receiver exposes events ONLY in contiguous seq order; a gap blocks delivery
//                     (net_event_recv returns 0) until the missing seq's re-send arrives. Never reorder.
//   * reliable      — the sender re-transmits the un-acked tail on the RX thread's cadence until the
//                     peer's cumulative PK_EVENT_ACK (highest contiguous seq received) clears it.
// The whole machinery lives ABOVE the datagram, so a future soc:U backend inherits it unchanged.
//
// CONCURRENCY: all state is guarded by s_evLock. net_event_send (caller thread) appends to the TX
// queue; the RX thread transmits/re-transmits + reassembles + ACKs; net_event_recv (caller thread)
// pops the in-order delivery ring. The single RX thread remains the only udsPullPacket owner.

// One event in transit — a local mirror of celiolink.h's ClEvent (ABI-identical; see the size assert).
// Kept here (not via celiolink.h) so netlink.c stays free of FSM types — the public API takes a void*.
typedef struct {
	u8  type;
	u8  arg;
	u16 len;            // bytes valid in data[]
	u8  data[256];
} NetEvent;             // 260 bytes == NET_CLEVENT_SIZE (the ClEvent ABI)
_Static_assert(sizeof(NetEvent) == NET_CLEVENT_SIZE, "NetEvent must match ClEvent (NET_CLEVENT_SIZE)");

#define NET_EV_TXQ    64    // outbound queue depth (events are few; 64 is ample headroom)
#define NET_EV_RXWIN  64    // inbound reassembly + delivery window (>= the bounded inbound ring)
#define NET_EV_SEATS  DGBA_MAX_SEATS

// Per-seat outbound stream: a ring of pending events [txBase..txNext). txBase = lowest un-acked seq;
// everything < txBase has been peer-ACKed and freed. seq is 1-based (0 = "none").
typedef struct {
	NetEvent q[NET_EV_TXQ];   // q[seq % NET_EV_TXQ] holds the event with that seq while base<=seq<next
	u32      base;            // lowest un-acked seq (== peerAcked+1)
	u32      next;            // next seq to assign (highest queued = next-1)
	u32      peerAcked;       // highest seq the peer has cumulatively ACKed (0 = none)
} NetEvTx;

// Reassembly slot: one per (seq % NET_EV_RXWIN); collects fragments until fragGotMask is complete.
typedef struct {
	u32  seq;             // 0 = empty slot
	u8   evType, evArg;
	u16  evLen;
	u8   fragCount;
	u32  fragGotMask;     // bit i set once fragment i landed (<=32 frags; 256B/242B-per-frag => <=2)
	u8   data[256];
} NetEvAsm;

// Per-seat inbound stream: reassembly slots keyed by seq, a contiguous-delivery cursor, and a small
// in-order delivery ring drained by net_event_recv.
typedef struct {
	NetEvAsm asm_[NET_EV_RXWIN];
	NetEvent deliver[NET_EV_RXWIN];   // fully-reassembled, NOT-yet-popped events keyed by seq
	u32      deliverSeq[NET_EV_RXWIN];// the seq held in deliver[i] (0 = empty)
	u32      expect;         // next seq to DELIVER in order (1-based; the contiguous cursor)
	u32      rxContig;       // highest contiguous seq fully RECEIVED (drives the cumulative ACK)
} NetEvRx;

static NetEvTx s_evTx[NET_EV_SEATS];
static NetEvRx s_evRx[NET_EV_SEATS];
static LightLock s_evLock;
static bool s_evLockInit = false;
// diagnostics (read by net_event_get_stats for the netlog)
static int s_evOverflow    = 0;   // inbound events that could NOT be buffered (window full) — must stay 0
static int s_evRetransmits = 0;   // cumulative fragment re-sends
static int s_evResendTick  = 0;   // RX-thread cadence divider for the un-acked-tail re-send

static void net_event_locks_init(void) {
	if (s_evLockInit) return;
	LightLock_Init(&s_evLock);
	s_evLockInit = true;
}

void net_event_reset(void) {
	net_event_locks_init();
	LightLock_Lock(&s_evLock);
	memset(s_evTx, 0, sizeof s_evTx);
	memset(s_evRx, 0, sizeof s_evRx);
	for (int s = 0; s < NET_EV_SEATS; s++) {
		s_evTx[s].base = s_evTx[s].next = 1;   // seq is 1-based; nothing queued yet
		s_evRx[s].expect = 1;                  // waiting for the first event (seq 1)
		s_evRx[s].rxContig = 0;                // nothing received contiguously yet
	}
	s_evOverflow = 0; s_evRetransmits = 0; s_evResendTick = 0;
	LightLock_Unlock(&s_evLock);
}

// EVENT-channel diagnostics for the netlog (declared in netlink.h). Reports the highest sent/acked/
// delivered seq across seats + the overflow/retransmit counters. rxDelivered>0 => peer events arrived.
void net_event_get_stats(int* txSeq, int* txAcked, int* rxDelivered, int* overflow, int* retransmits) {
	net_event_locks_init();
	int ts = 0, ta = 0, rd = 0;
	LightLock_Lock(&s_evLock);
	for (int s = 0; s < NET_EV_SEATS; s++) {
		int sent = (int)s_evTx[s].next - 1;       if (sent > ts) ts = sent;
		int ack  = (int)s_evTx[s].peerAcked;       if (ack  > ta) ta = ack;
		int del  = (int)s_evRx[s].expect - 1;      if (del  > rd) rd = del;
	}
	LightLock_Unlock(&s_evLock);
	if (txSeq)       *txSeq       = ts;
	if (txAcked)     *txAcked     = ta;
	if (rxDelivered) *rxDelivered = rd;
	if (overflow)    *overflow    = s_evOverflow;
	if (retransmits) *retransmits = s_evRetransmits;
}

// D3 per-frame CSV (SPEC-firmware-diag D3.2/D3.3 `evTxQ`): the EVENT-channel OUTBOUND queue depth
// — max un-ACKed backlog (next - base) across seats. This is the live form of the run-#6
// send-queue-overflow X-ray (98 frames of key 0x1C before the FR error): a climbing evTxQ column
// shows the drain falling behind the fill LONG before the game errors. Same s_evLock discipline
// as net_event_get_stats; called at ~60 Hz from the render thread — negligible.
int net_event_get_queue(void) {
	net_event_locks_init();
	int q = 0;
	LightLock_Lock(&s_evLock);
	for (int s = 0; s < NET_EV_SEATS; s++) {
		int d = (int)(s_evTx[s].next - s_evTx[s].base);
		if (d > q) q = d;
	}
	LightLock_Unlock(&s_evLock);
	return q;
}

// Transmit ONE event (all fragments) to the peer. Caller holds s_evLock. isResend bumps the diag.
static void net_event_tx_one(int seat, u32 seq, const NetEvent* ev, bool isResend) {
	int total = (int)ev->len;
	int frags = (total + NET_EVENT_PAY_MAX - 1) / NET_EVENT_PAY_MAX;
	if (frags < 1) frags = 1;   // a zero-payload control event is still ONE fragment
	for (int f = 0; f < frags; f++) {
		DgbaEventPkt pk;
		memset(&pk, 0, sizeof pk);
		pk.magic = 'G'; pk.type = PK_EVENT; pk.seat = (u8)seat;
		pk.evType = ev->type; pk.evArg = ev->arg;
		pk.seq = seq; pk.fragIdx = (u8)f; pk.fragCount = (u8)frags; pk.evLen = ev->len;
		int off = f * NET_EVENT_PAY_MAX;
		int n   = total - off; if (n > NET_EVENT_PAY_MAX) n = NET_EVENT_PAY_MAX; if (n < 0) n = 0;
		if (n > 0) memcpy(pk.payload, ev->data + off, (size_t)n);
		size_t wire = (size_t)NET_EVENT_PAY_OFF + (size_t)n;
		// Unicast to the resolved peer (MAC-ACKed). Single non-blocking try; loss is covered by the
		// RX-thread re-send of the whole un-acked tail until the peer's cumulative ACK clears it.
		u16 dst = s_peerResolved ? s_peerNode : UDS_BROADCAST_NETWORKNODEID;
		u32 fl  = s_peerResolved ? UDS_SENDFLAG_Default : (UDS_SENDFLAG_Default | UDS_SENDFLAG_Broadcast);
		net_send_locked(dst, fl, &pk, wire);   // benign TX-busy: re-send covers it; never counted as delivered
		if (isResend) s_evRetransmits++;
	}
}

// Send the cumulative ACK for `seat`'s stream (highest contiguous seq received). Caller holds s_evLock.
static void net_event_send_ack(int seat) {
	DgbaLinkPkt ack; memset(&ack, 0, sizeof ack);
	ack.magic = 'G'; ack.type = PK_EVENT_ACK; ack.seat = (u8)seat; ack.round = s_evRx[seat].rxContig;
	u16 dst = s_peerResolved ? s_peerNode : UDS_BROADCAST_NETWORKNODEID;
	u32 fl  = s_peerResolved ? UDS_SENDFLAG_Default : (UDS_SENDFLAG_Default | UDS_SENDFLAG_Broadcast);
	net_send_locked(dst, fl, &ack, sizeof ack);
}

// PUBLIC: enqueue a ClEvent for reliable send. Returns 0 ok / <0 backpressure (queue full).
int net_event_send(int seat, const void* clEvent) {
	if (seat < 0 || seat >= NET_EV_SEATS || !clEvent) return -1;
	net_event_locks_init();
	const NetEvent* ev = (const NetEvent*)clEvent;
	int rc = 0;
	LightLock_Lock(&s_evLock);
	NetEvTx* tx = &s_evTx[seat];
	if (tx->next - tx->base >= NET_EV_TXQ) {        // bounded queue full -> backpressure (never drop)
		rc = -1;
	} else {
		u32 seq = tx->next++;
		NetEvent* slot = &tx->q[seq % NET_EV_TXQ];
		*slot = *ev;
		if (slot->len > 256) slot->len = 256;       // clamp to the carried buffer (defensive)
		net_event_tx_one(seat, seq, slot, false);   // first transmit now; the RX thread re-sends until ACKed
	}
	LightLock_Unlock(&s_evLock);
	return rc;
}

// PUBLIC: pop the next in-order received ClEvent for `seat`. Returns 1 if delivered, 0 if none ready.
int net_event_recv(int seat, void* clEventOut) {
	if (seat < 0 || seat >= NET_EV_SEATS || !clEventOut) return 0;
	net_event_locks_init();
	int got = 0;
	LightLock_Lock(&s_evLock);
	NetEvRx* rx = &s_evRx[seat];
	u32 want = rx->expect;
	int idx  = (int)(want % NET_EV_RXWIN);
	if (rx->deliverSeq[idx] == want && want != 0) {   // the in-order next event is reassembled+ready
		memcpy(clEventOut, &rx->deliver[idx], sizeof(NetEvent));
		rx->deliverSeq[idx] = 0;                      // consume (exactly-once: cleared so a re-send is ignored)
		rx->expect = want + 1;
		got = 1;
	}
	LightLock_Unlock(&s_evLock);
	return got;
}

// RX-side: ingest one PK_EVENT fragment. Caller holds s_evLock (called from the RX thread). Reassembles
// into the (seq%RXWIN) slot; on completion, stores the event in the delivery ring and advances rxContig
// over any newly-contiguous run. A fragment for an already-delivered/old seq is ignored (exactly-once).
static void net_event_rx_fragment(const DgbaEventPkt* pk, size_t got) {
	int seat = pk->seat;
	if (seat < 0 || seat >= NET_EV_SEATS) return;
	if (got < (size_t)NET_EVENT_PAY_OFF) return;            // malformed (shorter than header+evLen)
	NetEvRx* rx = &s_evRx[seat];
	u32 seq = pk->seq;
	if (seq == 0) return;
	if ((s32)(seq - rx->expect) < 0) {                      // already delivered -> just (re)ACK, drop frag
		return;
	}
	if (seq - rx->expect >= NET_EV_RXWIN) { s_evOverflow++; return; }  // beyond the window: never silently lose
	int frags = pk->fragCount ? pk->fragCount : 1;
	if (frags > 32) return;                                 // mask is 32-bit; >242*32B events can't occur (max 2)
	int fi = pk->fragIdx;
	if (fi < 0 || fi >= frags) return;
	int slot = (int)(seq % NET_EV_RXWIN);
	NetEvAsm* as = &rx->asm_[slot];
	if (as->seq != seq) {                                   // (re)claim the reassembly slot for this seq
		// If the slot is occupied by a DIFFERENT live seq, the window invariant (seq-expect<RXWIN) means
		// it must be the same residue from an older expect window already delivered -> safe to overwrite.
		memset(as, 0, sizeof *as);
		as->seq = seq; as->evType = pk->evType; as->evArg = pk->evArg;
		as->evLen = pk->evLen; as->fragCount = (u8)frags;
	}
	int payN = (int)got - NET_EVENT_PAY_OFF;
	if (payN > NET_EVENT_PAY_MAX) payN = NET_EVENT_PAY_MAX;
	int off = fi * NET_EVENT_PAY_MAX;
	if (off + payN > 256) payN = 256 - off; if (payN < 0) payN = 0;
	if (payN > 0) memcpy(as->data + off, pk->payload, (size_t)payN);
	as->fragGotMask |= (1u << fi);
	u32 fullMask = (frags >= 32) ? 0xFFFFFFFFu : ((1u << frags) - 1u);
	if ((as->fragGotMask & fullMask) != fullMask) return;   // not all fragments yet
	// Event fully reassembled -> publish into the delivery ring (idempotent if already present).
	if (rx->deliverSeq[slot] != seq) {
		NetEvent* d = &rx->deliver[slot];
		d->type = as->evType; d->arg = as->evArg;
		d->len  = as->evLen > 256 ? 256 : as->evLen;
		memset(d->data, 0, sizeof d->data);
		if (d->len) memcpy(d->data, as->data, d->len);
		rx->deliverSeq[slot] = seq;
	}
	// Recompute the contiguous-RECEIVED high-water mark (drives the cumulative ACK). Everything below
	// `expect` was already delivered+consumed (definitely received); then extend over the contiguous run
	// of reassembled-but-not-yet-popped events sitting in the delivery ring.
	u32 contig = rx->expect - 1;   // expect>=1, so this is >=0; [1..expect-1] are delivered = received
	for (;;) {
		u32 nxt = contig + 1;
		if (nxt - rx->expect >= NET_EV_RXWIN) break;                 // past the window
		if (rx->deliverSeq[(int)(nxt % NET_EV_RXWIN)] == nxt) contig = nxt;
		else break;
	}
	rx->rxContig = contig;
}

// --- M3 RX thread: the ONE udsPullPacket owner. Drains to empty, dispatches every packet, THEN
// waits on the bind event (drain-first => a missed edge is harmless; the next pass re-drains). No
// svcClearEvent (it would drop a frame signal). net_link_stop wakes it via svcSignalEvent. --------
static void net_rx_thread(void* arg) {
	(void)arg;
	u8 buf[NET_PKT_BUF]; size_t got; u16 src;
	int resendTick = 0;
	while (__atomic_load_n(&s_rxRun, __ATOMIC_ACQUIRE)) {
		// D1: RX-thread loop-seq (the radio heartbeat the watchdog watches) + site-1300 crumb.
		// One pass = one drain + one re-send tick; g_diagRxSeq frozen while workers live = the
		// RX thread wedged (inside udsPullPacket / a send) — SPEC D1.3/D1.1 site 4.
		g_diagRxSeq++;
		DIAG_CRUMB(g_diagNetCrumb, DIAG_SITE_NET_RX_PASS, g_diagRxSeq);
		LightLock_Lock(&s_rxLock);
		while (s_up && R_SUCCEEDED(udsPullPacket(&s_bind, buf, sizeof buf, &got, &src)) && got) {
			if (got < 2 || buf[0] != 'G') continue;          // need at least magic+type; 'G' wire-magic
			u8 ptype = buf[1];
			if (ptype == PK_EVENT) {                         // EVENT channel fragment (own datagram layout)
				if (got < (size_t)NET_EVENT_PAY_OFF) continue;
				const DgbaEventPkt* ev = (const DgbaEventPkt*)buf;
				int evseat = ev->seat;
				LightLock_Lock(&s_evLock);
				net_event_rx_fragment(ev, got);              // reassemble + advance rxContig
				if (evseat >= 0 && evseat < NET_EV_SEATS) net_event_send_ack(evseat);  // cumulative ACK back
				LightLock_Unlock(&s_evLock);
				continue;
			}
			if (got < sizeof(DgbaLinkPkt)) continue;         // PING/PONG/WORD/EVENT_ACK ride the 16-B pkt
			const DgbaLinkPkt* pk = (const DgbaLinkPkt*)buf;
			if (pk->type == PK_PING) {                       // echo a pong straight back (unicast to sender)
				DgbaLinkPkt pong; memset(&pong, 0, sizeof pong);
				pong.magic = 'G'; pong.type = PK_PONG; pong.round = pk->round;
				Result pr = net_send_locked(src, UDS_SENDFLAG_Default, &pong, sizeof pong);
				if (UDS_CHECK_SENDTO_FATALERROR(pr)) s_pingSendFails++;
			} else if (pk->type == PK_PONG) {                // our ping returned -> RTT
				u64 sent = s_pingTick[pk->round % PING_RING];
				if (sent) { s_pingRtt = (int)((svcGetSystemTick() - sent) * 1000ull / SYSCLOCK_ARM11);
				            s_pingTick[pk->round % PING_RING] = 0; }
			} else if (pk->type == PK_WORD) {                // peer's SIO word -> merge + wake any collect
				s_rxWordN++;                                 // diag: a peer WORD actually arrived (RX-empty if this stays 0)
				net_round_merge(pk->seat, pk->round, pk->d.send);
			} else if (pk->type == PK_EVENT_ACK) {           // peer's cumulative ACK -> free our un-acked tail
				int aseat = pk->seat;
				if (aseat >= 0 && aseat < NET_EV_SEATS) {
					LightLock_Lock(&s_evLock);
					NetEvTx* tx = &s_evTx[aseat];
					if ((s32)(pk->round - tx->peerAcked) > 0) tx->peerAcked = pk->round;   // monotonic
					// Cumulative: everything <= peerAcked is delivered -> base = peerAcked+1 (capped at next).
					u32 nb = tx->peerAcked + 1;
					if ((s32)(nb - tx->base) > 0) tx->base = (nb > tx->next) ? tx->next : nb;
					LightLock_Unlock(&s_evLock);
				}
			}
		}
		LightLock_Unlock(&s_rxLock);
		if (!__atomic_load_n(&s_rxRun, __ATOMIC_ACQUIRE)) break;
		// RELIABLE re-send: every ~4ms re-transmit our current outgoing word so a dropped/reordered word
		// reaches the peer even when neither side is blocked in collect (the side that completed its round
		// locally still must keep re-sending its reply until the peer advances).
		if (++resendTick >= 8) { resendTick = 0; net_resend_current(); }
		// EVENT channel: re-transmit the whole un-acked tail every ~16ms (resend cadence) so a dropped
		// fragment/event eventually lands; the peer's cumulative PK_EVENT_ACK frees it. Re-ACK the last
		// contiguous seq too, so a dropped ACK doesn't strand the sender (idempotent).
		if (++s_evResendTick >= 32) {
			s_evResendTick = 0;
			LightLock_Lock(&s_evLock);
			for (int s = 0; s < NET_EV_SEATS; s++) {
				NetEvTx* tx = &s_evTx[s];
				for (u32 seq = tx->base; seq < tx->next; seq++)
					net_event_tx_one(s, seq, &tx->q[seq % NET_EV_TXQ], true);   // un-acked tail re-send
				if (s_evRx[s].rxContig != 0) net_event_send_ack(s);             // re-ACK (covers a lost ACK)
			}
			LightLock_Unlock(&s_evLock);
		}
		// Poll the radio at ~2 kHz instead of blocking on the bind event: re-drains promptly AND notices
		// s_rxRun==false within ~0.5ms on teardown — no event-wait means no lost-wake hang (the close-hang
		// we're fixing) and no sticky-event busy-spin. Runs on core 2 (freed when emuB pauses).
		svcSleepThread(500 * 1000LL);   // 0.5 ms
	}
}

bool net_link_start(int seat, int rxCore) {
	(void)seat;                            // role is decided in gbacore_net_attach(seat)
	if (!s_inited || !s_up) return false;
	net_rounds_init();                     // (also arms s_txLock/s_rxLock)
	net_transfer_reset();
	net_event_reset();                     // EVENT channel: fresh seq/ack + queues for this link
	s_loopback = false;
	if (!net_resolve_peer()) return false; // REFUSE to start without a unicast peer (no lossy broadcast WORDs)
	if (!s_rxThread) {
		__atomic_store_n(&s_rxRun, true, __ATOMIC_RELEASE);
		s32 prio = 0x30; svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
		// Pin the RX poll to rxCore = the core the participating game does NOT run on (the freed core when the
		// other game pauses), so the radio never contends with the trade worker. Fall back to the libctru
		// default core if rxCore is unavailable (Old 3DS / no core-2 grant).
		s_rxThread = threadCreate(net_rx_thread, NULL, NET_RX_STACK, prio - 1, rxCore, false);
		if (!s_rxThread) s_rxThread = threadCreate(net_rx_thread, NULL, NET_RX_STACK, prio - 1, -2, false);
		if (!s_rxThread) { __atomic_store_n(&s_rxRun, false, __ATOMIC_RELEASE); return false; }
	}
	return true;
}

void net_transfer_abort(void) {
	s_collectAbort = true;        // a polling collect checks this and returns at once
	if (!s_roundsInit) return;
	for (int i = 0; i < NET_ROUNDS; i++) {
		LightLock_Lock(&s_rounds[i].lock);
		LightEvent_Signal(&s_rounds[i].ev);   // (also wake any LightEvent waiter, belt-and-suspenders)
		LightLock_Unlock(&s_rounds[i].lock);
	}
}

void net_link_stop(void) {
	if (s_rxThread) {
		__atomic_store_n(&s_rxRun, false, __ATOMIC_RELEASE);   // the RX poll notices this within ~0.5ms
		DIAG_CRUMB(g_diagMainCrumb, DIAG_SITE_MAIN_RX_JOIN, 0);   // D1 site 4200: render thread parked in threadJoin
		threadJoin(s_rxThread, U64_MAX);                       // never held under s_txLock/s_rxLock -> no inversion
		g_diagMainCrumb = 0;                                   // bracket exit (nonzero == currently parked)
		threadFree(s_rxThread); s_rxThread = NULL;
	}
	net_transfer_abort();   // release any worker still parked in collect
}

void net_link_get_rtt(int* rttMs, int* drops) {
	if (rttMs) *rttMs = s_pingRtt;
	if (drops) *drops = s_pingDrops;
}

// M3 loss diag: cumulative WORD send failures (incl. busy that exhausted retries) and TX-busy retries seen.
// Transport-level establishment diagnostics (for the netlog header): did our WORDs leave (txFails/busyN),
// did the peer's WORDs arrive (rxWordN; 0 => RX-empty = nothing coming back), is the unicast peer resolved
// (peerUp), and the furthest SEAT-0/host round we've seen a word for (maxSeat0Round; -1 = none). On the JOINER
// these answer "why didn't round 0 cross": host not TXing vs joiner RX-empty vs round-number desync.
void net_link_get_stats(int* rxWordN, int* wordSendFails, int* busyN, int* peerUp, int* maxSeat0Round) {
	if (rxWordN)       *rxWordN       = s_rxWordN;
	if (wordSendFails) *wordSendFails = s_wordSendFails;
	if (busyN)         *busyN         = s_netBusyN;
	if (peerUp)        *peerUp        = s_peerResolved ? 1 : 0;
	if (maxSeat0Round) *maxSeat0Round = s_haveSeat0 ? (int)s_rxMaxSeat0 : -1;
}

void net_link_get_loss(int* wordSendFails, int* busyN) {
	if (wordSendFails) *wordSendFails = s_wordSendFails;
	if (busyN)         *busyN         = s_netBusyN;
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
