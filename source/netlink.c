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
#define PK_WORD 7   // M3: one seat's SIO word for a round (pk->seat / pk->round / pk->d.send)

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

// WORD-path send: a TX-busy (0xC86113F0) means the frame did NOT leave the radio. For the SIO WORD
// path that IS link loss (it 0xFFFF-poisons the peer's Gen-3 handshake), so retry the busy a bounded
// number of times, releasing s_txLock between tries so the RX-thread PONG and the other seat's WORD
// are never starved. Bounded well under NET_DEADLINE_MS. s_netBusyN counts every busy waited out; the
// return is the LAST udsSendTo result (success, a fatal error, or busy if all retries were exhausted).
static Result net_send_word_retry(u16 dst, const void* p, size_t n) {
	if (!s_inited || !s_up) return (Result)0xD8E007FA;
	Result r = (Result)0xC86113F0;
	for (int t = 0; t < 64; t++) {                 // <=64 * 125us = ~8ms worst case, << 250ms deadline
		LightLock_Lock(&s_txLock);
		r = udsSendTo(dst, DGBA_DATACHAN, (u8)UDS_SENDFLAG_Default, p, n);
		LightLock_Unlock(&s_txLock);
		if (r != (Result)0xC86113F0) break;        // sent, or a real (fatal) error -> stop
		s_netBusyN++;                              // diag: a TX-busy we had to wait out
		svcSleepThread(125000LL);                  // 0.125ms: let nwm drain its TX ring
	}
	return r;
}

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
			u8 buf[64]; size_t got = 0; u16 src = 0;
			while (R_SUCCEEDED(udsPullPacket(&s_bind, buf, sizeof buf, &got, &src)) && got >= sizeof(DgbaLinkPkt)) {
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
	LightEvent_Signal(&r->ev);
	LightLock_Unlock(&r->lock);
}

// Put one WORD packet on the wire (unicast to the peer, MAC-ACKed, busy-retried). Used by the primary
// send AND the reliable re-send. Idempotent on the peer (net_round_merge overwrites the same slot).
static void net_word_tx(int seat, int mode, u32 round, u16 word) {
	if (s_loopback || !s_up || !s_peerResolved) return;
	DgbaLinkPkt pk; memset(&pk, 0, sizeof pk);
	pk.magic = 'G'; pk.type = PK_WORD; pk.seat = (u8)seat; pk.mode = (u8)mode;
	pk.round = round; pk.d.send = word;
	Result rc = net_send_word_retry(s_peerNode, &pk, sizeof pk);
	if (R_FAILED(rc)) s_wordSendFails++;             // a WORD that never left IS link loss — count BUSY too
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
	for (;;) {
		if (s_collectAbort || !s_up) return false;                          // teardown / link down
		if ((s64)deadlineTick - (s64)svcGetSystemTick() <= 0) return false; // genuine link-lost
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

// --- M3 RX thread: the ONE udsPullPacket owner. Drains to empty, dispatches every packet, THEN
// waits on the bind event (drain-first => a missed edge is harmless; the next pass re-drains). No
// svcClearEvent (it would drop a frame signal). net_link_stop wakes it via svcSignalEvent. --------
static void net_rx_thread(void* arg) {
	(void)arg;
	u8 buf[64]; size_t got; u16 src;
	int resendTick = 0;
	while (__atomic_load_n(&s_rxRun, __ATOMIC_ACQUIRE)) {
		LightLock_Lock(&s_rxLock);
		while (s_up && R_SUCCEEDED(udsPullPacket(&s_bind, buf, sizeof buf, &got, &src)) && got) {
			if (got < sizeof(DgbaLinkPkt)) continue;
			const DgbaLinkPkt* pk = (const DgbaLinkPkt*)buf;
			if (pk->magic != 'G') continue;
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
				net_round_merge(pk->seat, pk->round, pk->d.send);
			}
		}
		LightLock_Unlock(&s_rxLock);
		if (!__atomic_load_n(&s_rxRun, __ATOMIC_ACQUIRE)) break;
		// RELIABLE re-send: every ~4ms re-transmit our current outgoing word so a dropped/reordered word
		// reaches the peer even when neither side is blocked in collect (the side that completed its round
		// locally still must keep re-sending its reply until the peer advances).
		if (++resendTick >= 8) { resendTick = 0; net_resend_current(); }
		// Poll the radio at ~2 kHz instead of blocking on the bind event: re-drains promptly AND notices
		// s_rxRun==false within ~0.5ms on teardown — no event-wait means no lost-wake hang (the close-hang
		// we're fixing) and no sticky-event busy-spin. Runs on core 2 (freed when emuB pauses).
		svcSleepThread(500 * 1000LL);   // 0.5 ms
	}
}

bool net_link_start(int seat) {
	(void)seat;                            // role is decided in gbacore_net_attach(seat)
	if (!s_inited || !s_up) return false;
	net_rounds_init();                     // (also arms s_txLock/s_rxLock)
	net_transfer_reset();
	s_loopback = false;
	if (!net_resolve_peer()) return false; // REFUSE to start without a unicast peer (no lossy broadcast WORDs)
	if (!s_rxThread) {
		__atomic_store_n(&s_rxRun, true, __ATOMIC_RELEASE);
		s32 prio = 0x30; svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
		// Core 2 (freed when emuB pauses) so the RX poll never contends with emuA's trade worker on core 0.
		// Fall back to the default core if core 2 is unavailable (Old 3DS / no grant).
		s_rxThread = threadCreate(net_rx_thread, NULL, NET_RX_STACK, prio - 1, 2, false);
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
		threadJoin(s_rxThread, U64_MAX);                       // never held under s_txLock/s_rxLock -> no inversion
		threadFree(s_rxThread); s_rxThread = NULL;
	}
	net_transfer_abort();   // release any worker still parked in collect
}

void net_link_get_rtt(int* rttMs, int* drops) {
	if (rttMs) *rttMs = s_pingRtt;
	if (drops) *drops = s_pingDrops;
}

// M3 loss diag: cumulative WORD send failures (incl. busy that exhausted retries) and TX-busy retries seen.
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
