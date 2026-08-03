// Minimal host mock of libctru's <3ds.h> — JUST enough to compile source/netlink.c on a PC so the
// PK_EVENT reliability logic can be unit-tested against a simulated lossy channel. NOT a real libctru.
// The session/lobby UDS calls are no-op stubs; only udsSendTo is test-provided (captures the wire).
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

typedef uint8_t  u8;  typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t   s8;  typedef int16_t  s16; typedef int32_t  s32; typedef int64_t  s64;
typedef u32 Result;   typedef u32 Handle;

#define SYSCLOCK_ARM11 268111856ull
#define CUR_THREAD_HANDLE ((Handle)0xFFFF8000)
#define U64_MAX 0xFFFFFFFFFFFFFFFFull

// --- sync primitives (no-op on the single-threaded host harness) ---
typedef s32 LightLock;
typedef struct { volatile s32 state; } LightEvent;
static inline void LightLock_Init(LightLock* l){ if(l) *l=0; }
static inline void LightLock_Lock(LightLock* l){ (void)l; }
static inline void LightLock_Unlock(LightLock* l){ (void)l; }
static inline void LightEvent_Init(LightEvent* e, int t){ (void)t; if(e) e->state=0; }
static inline void LightEvent_Signal(LightEvent* e){ if(e) e->state=1; }
static inline void LightEvent_Clear(LightEvent* e){ if(e) e->state=0; }
static inline void LightEvent_Wait(LightEvent* e){ (void)e; }
typedef enum { RESET_ONESHOT=0, RESET_STICKY=1, RESET_PULSE=2 } ResetType;

// --- threads (stubbed; the harness never starts the RX thread) ---
typedef void* Thread;
typedef void (*ThreadFunc)(void*);
static inline Thread threadCreate(ThreadFunc f, void* a, size_t s, int p, int c, bool d){
    (void)f;(void)a;(void)s;(void)p;(void)c;(void)d; return (Thread)1; }
static inline void threadJoin(Thread t, u64 ns){ (void)t;(void)ns; }
static inline void threadFree(Thread t){ (void)t; }

// --- svc ---
static inline u64 svcGetSystemTick(void){ static u64 t=1000; t+=4000; return t; }   // monotone
static inline void svcSleepThread(s64 ns){ (void)ns; }
static inline Result svcSignalEvent(Handle h){ (void)h; return 0; }
static inline Result svcClearEvent(Handle h){ (void)h; return 0; }
static inline Result svcGetThreadPriority(s32* o, Handle h){ (void)h; if(o)*o=0x30; return 0; }

#define R_SUCCEEDED(r) ((Result)(r)==0)
#define R_FAILED(r)    ((Result)(r)!=0)

// --- UDS: opaque structs (netlink.c passes these by pointer only) ---
typedef struct { u8 _[0x108]; } udsNetworkStruct;
typedef struct { u8 total_nodes; u8 max_nodes; u16 node_bitmask; u16 cur_NetworkNodeID; u8 _[0x20]; } udsConnectionStatus;
typedef struct { u64 uds_friendcodeseed; u16 username[12]; u8 _[0x20]; } udsNodeInfo;
typedef struct { Handle event; u32 BindNodeID; u8 _[0x10]; } udsBindContext;
typedef struct { udsNetworkStruct network; udsNodeInfo nodes[16]; u8 _[0x40]; } udsNetworkScanInfo;

#define UDS_BROADCAST_NETWORKNODEID   0xFFFF
#define UDS_DEFAULT_RECVBUFSIZE       0x4000
#define UDS_SENDFLAG_Default          0
#define UDS_SENDFLAG_Broadcast        1
#define UDSCONTYPE_Client             1
#define UDS_CHECK_SENDTO_FATALERROR(r) (R_FAILED(r))

// session/lobby calls — all no-op success stubs (not under test)
static inline Result udsInit(size_t s, const void* d){ (void)s;(void)d; return 0; }
static inline void   udsExit(void){ }
static inline Result udsCreateNetwork(const udsNetworkStruct* n, const void* p, size_t ps, udsBindContext* b, u8 ch, u32 rb){ (void)n;(void)p;(void)ps;(void)b;(void)ch;(void)rb; return 0; }
static inline Result udsConnectNetwork(const udsNetworkStruct* n, const void* p, size_t ps, udsBindContext* b, u16 node, int t, u8 ch, u32 rb){ (void)n;(void)p;(void)ps;(void)b;(void)node;(void)t;(void)ch;(void)rb; return 0; }
static inline Result udsDisconnectNetwork(void){ return 0; }
static inline Result udsDestroyNetwork(void){ return 0; }
static inline Result udsUnbind(udsBindContext* b){ (void)b; return 0; }
static inline Result udsScanBeacons(void* buf, size_t sz, udsNetworkScanInfo** o, size_t* c, u32 wlan, const void* h, const void* f, bool s){ (void)buf;(void)sz;(void)wlan;(void)h;(void)f;(void)s; if(o)*o=NULL; if(c)*c=0; return 0; }
static inline void   udsGenerateDefaultNetworkStruct(udsNetworkStruct* n, u32 id, u8 sub, u8 max){ (void)id;(void)sub;(void)max; if(n) memset(n,0,sizeof *n); }
static inline Result udsSetApplicationData(const void* d, size_t s){ (void)d;(void)s; return 0; }
static inline Result udsGetNetworkStructApplicationData(const udsNetworkStruct* n, void* d, size_t s, size_t* o){ (void)n;(void)d;(void)s; if(o)*o=0; return 0; }
static inline Result udsGetConnectionStatus(udsConnectionStatus* c){ if(c) memset(c,0,sizeof *c); return 0; }
static inline Result udsGetNodeInformation(u16 id, udsNodeInfo* o){ (void)id; if(o) memset(o,0,sizeof *o); return 0; }
static inline Result udsGetNodeInfoUsername(const udsNodeInfo* n, char* o){ (void)n; if(o)o[0]=0; return 0; }

// udsPullPacket: the harness never runs the RX thread; stub = "no packet".
static inline Result udsPullPacket(const udsBindContext* b, void* buf, size_t sz, size_t* got, u16* src){ (void)b;(void)buf;(void)sz; if(got)*got=0; if(src)*src=0; return 1; }

// udsSendTo: TEST-PROVIDED (defined in the harness) so it captures every datagram onto the sim channel.
Result udsSendTo(u16 dst, u8 chan, u8 flags, const void* buf, size_t size);
