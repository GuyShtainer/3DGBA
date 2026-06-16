// netlink.h — wireless multi-console transport for the dual-GBA link (M1: UDS lobby + seats).
// PURE libctru (UDS), ZERO mGBA types — the translation-unit boundary the architecture requires
// (see docs/kb/wireless-link-architecture.md §2a). The mGBA-side network SIO driver (M2.5+) will
// call this module's C API; this header never leaks a udsNetworkStruct to callers.
#pragma once
#include <3ds.h>
#include <stdbool.h>

#define DGBA_MAX_SEATS 4
#define DGBA_NAME_LEN  24   // UTF-8 username buffer (10 UTF-16 chars -> <=30B; we clamp)

// One lobby as seen in a scan: the host's advertisement + identity, for the join list.
typedef struct {
	char host[DGBA_NAME_LEN];     // host username (UTF-8, NUL-terminated)
	char gameCode[5];             // host's GBA game code ("BPEE" etc.), NUL-terminated
	u32  romCrc;                  // host's ROM CRC32 (0 until computed) — region/rev guard
	u8   proto;                   // protocol version
	u8   seatsTotal;              // 2..4
	u8   seatsOpen;               // remaining free seats
} DgbaLobby;

// Live connection status (host or client).
typedef struct {
	bool up;                      // session active
	bool host;                    // are we the host (parent)
	int  totalNodes;              // connected consoles
	int  maxNodes;
	u16  nodeMask;                // UDS node bitmask (bit0 = host node 0x1)
	int  myNode;                  // our UDS node id (1 = host)
	char names[DGBA_MAX_SEATS][DGBA_NAME_LEN];   // per-node usernames (index = nodeId-1)
} DgbaConn;

// Lifecycle. netlink_init brings UDS up (needs the .cia's nwm::UDS grant; no-ops/false on a
// .3dsx or a console without it, so the app keeps running). Safe to call once at boot.
bool netlink_init(void);
void netlink_exit(void);
bool netlink_available(void);   // true once udsInit succeeded
bool net_session_active(void);  // true while a network is hosted/joined (false after a HOME-suspend drop)

// Session (M1). gameCode/romCrc identify this host's ROM in the advertisement; seatsTotal 2..4.
bool net_session_host(const char* gameCode, u32 romCrc, int seatsTotal);
int  net_lobby_scan(DgbaLobby* out, int max);    // returns lobby count found (caches nets for join)
bool net_session_join(int sel);                  // connect to the sel'th scanned lobby
void net_session_close(void);                    // leave/destroy, back to standalone
bool net_lobby_status(DgbaConn* out);            // poll connection status + usernames

// M2 latency probe: call once per frame while connected. Echoes peers' pings, times our pongs,
// fires a fresh ping (throttled, unicast to the lone peer). *rttMs = last round-trip (ms, -1 = none
// yet); *drops = cumulative lost pings; *sendFails = cumulative local TX-busy refusals (not air loss).
// Any out-param may be NULL.
void net_ping_update(int* rttMs, int* drops, int* sendFails);

// --- M2.5 transfer plane: the net SIO driver (gbacore.c) exchanges one SIO word per "round" with
// the other seat(s) through these. In LOOPBACK mode the two LOCAL cores rendezvous in-memory (no
// radio) for the one-console M2.5 test; M3 swaps in real UDS. NEVER call inline from a worker that
// runs a core — collect() blocks. seat = GBA playerId 0..3; needMask = bitmask of seats required.
void net_link_set_loopback(bool on);
void net_transfer_reset(void);
void net_transfer_send_word(int seat, int mode, u32 round, u16 send);
bool net_transfer_collect(u32 round, int mode, u16 out[4], u32 needMask, u64 deadline_ms);
bool net_round_ready(u32 round, u32 needMask);   // non-blocking: is `round` present with needMask seats in?
// JOINER PACING BARRIER: block until `round` is present (needMask seats) or an escape fires
// (s_collectAbort / link-down / deadline_ms link-lost). Copies no words — gates the joiner's emulated
// clock to the parent's transfer pace so the Gen-3 SLAVE VBlank watchdog can't trip. Returns readiness.
bool net_round_wait(u32 round, u32 needMask, u64 deadline_ms);
// Monotonic tick + ticks->us, so the libctru-free mGBA TU (gbacore.c netlog) can stamp per-round wall-clock.
u64  net_mono_ticks(void);
u32  net_ticks_to_us(u64 dticks);

// --- M3 wireless transport (real UDS) -------------------------------------------------------
// net_link_start: arm wireless gameplay AFTER the lobby session is up. loopback=false, reset the
// ring, resolve the lone peer (REFUSING if it can't get a unicast node id), and spin the one RX
// thread that owns every udsPullPacket. Returns false if the session/peer isn't ready.
bool net_link_start(int seat);
// net_link_stop: stop+join the RX thread and release any worker blocked in collect. Idempotent.
void net_link_stop(void);
// net_transfer_abort: signal every round slot so a blocked net_transfer_collect returns at once
// (collect then reports timeout -> the worker's net free-run loop checks netLinked and exits).
void net_transfer_abort(void);
// net_link_get_rtt: the RX thread writes RTT(ms,-1=none)/cumulative drops; the HUD reads them.
void net_link_get_rtt(int* rttMs, int* drops);
// net_link_get_loss: cumulative WORD send failures (incl. busy that exhausted retries) + TX-busy retries (HUD).
void net_link_get_loss(int* wordSendFails, int* busyN);
// CHILD round-from-wire: the lowest parent-stamped round in the ring that is > afterRound and has
// the parent's word present. Returns true + *round when one exists (the child injects THAT exact
// wire round). Transport-agnostic: loopback's local parent-merge satisfies it the same way.
bool net_round_next_parent(u32 afterRound, u32* outRound);
