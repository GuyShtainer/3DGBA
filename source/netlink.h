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
