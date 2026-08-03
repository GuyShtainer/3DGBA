// Host unit test for the REAL netlink.c PK_EVENT reliability layer (fragmentation / reassembly /
// in-order delivery / cumulative ACK / un-acked-tail resend), run against a simulated lossy /
// reordering / duplicating channel. Compiles netlink.c on the host behind test/host/3ds.h.
//
//   clang -std=c11 -Wall -Wextra -O2 -I test/host -I source test/host/test_netlink_reliability.c -o /tmp/tn && /tmp/tn
//
// This executes the actual transport code that ships on the 3DS — it does NOT replace the 2-console
// hardware test (no real UDS radio here), but it proves the reliability math is correct under loss.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../source/netlink.c"   // pulls in the mock <3ds.h>, all statics, and the event logic

// ClEvent (celiolink.h) is ABI-identical to netlink.c's NetEvent (both 260 B) — use NetEvent here.
enum { CL_EV_LINKPLAYER=1, CL_EV_PARTY_CHUNK=2, CL_EV_SELECT=3, CL_EV_CONFIRM=4 };

// ----- simulated radio channel: every udsSendTo lands here as a datagram "in the air" -----
#define CHAN_MAX 8192
typedef struct { u8 b[NET_PKT_BUF]; int len; } Dgram;
static Dgram g_air[CHAN_MAX];
static int   g_airN = 0;

Result udsSendTo(u16 dst, u8 chan, u8 flags, const void* buf, size_t size) {
    (void)dst; (void)chan; (void)flags;
    if (g_airN < CHAN_MAX && size <= NET_PKT_BUF) {
        memcpy(g_air[g_airN].b, buf, size);
        g_air[g_airN].len = (int)size;
        g_airN++;
    }
    return 0;
}

// replicate net_rx_thread's PK_EVENT_ACK handling (it is inline in the thread, not a callable fn)
static void apply_ack(int seat, u32 ackRound) {
    NetEvTx* tx = &s_evTx[seat];
    if ((s32)(ackRound - tx->peerAcked) > 0) tx->peerAcked = ackRound;
    u32 nb = tx->peerAcked + 1;
    if ((s32)(nb - tx->base) > 0) tx->base = (nb > tx->next) ? tx->next : nb;
}

static int g_checks = 0, g_fail = 0;
#define CHECK(c, ...) do { g_checks++; if (!(c)) { g_fail++; printf("  FAIL: "); printf(__VA_ARGS__); } } while (0)

// deterministic PRNG so a failure reproduces
static unsigned g_rng = 0x1234567u;
static unsigned rng(void){ g_rng ^= g_rng<<13; g_rng ^= g_rng>>17; g_rng ^= g_rng<<5; return g_rng; }
static int chance(int pct){ return (int)(rng()%100) < pct; }

// One reference event we sent (to verify exactly against what we receive).
typedef struct { u8 type, arg; u16 len; u8 data[256]; } Ref;

// Run the full reliable exchange of `n` events on seat 0 over a lossy channel; verify exactly-once,
// in-order delivery with byte-identical content. Returns rounds taken (or -1 if it never converged).
static int run_scenario(const char* name, Ref* refs, int n, int lossPct, int dupPct, int reorder) {
    net_event_reset();
    s_inited = true; s_up = true;              // bring the (mock) link up so net_send_locked actually sends
    s_peerResolved = true; s_peerNode = 1;    // unicast to the (single) peer
    g_airN = 0;

    for (int i = 0; i < n; i++) {
        NetEvent ev; memset(&ev, 0, sizeof ev);
        ev.type = refs[i].type; ev.arg = refs[i].arg; ev.len = refs[i].len;
        memcpy(ev.data, refs[i].data, refs[i].len);
        CHECK(net_event_send(0, &ev) == 0, "%s: net_event_send(%d) backpressured unexpectedly\n", name, i);
    }

    Ref got[64]; int gotN = 0;
    int rounds = 0;
    const int MAX_ROUNDS = 5000;
    for (; rounds < MAX_ROUNDS; rounds++) {
        // snapshot the in-air datagrams, clear the channel for this round's new sends
        Dgram air[CHAN_MAX]; int airN = g_airN;
        memcpy(air, g_air, sizeof(Dgram) * (size_t)airN);
        g_airN = 0;

        // build the delivery set: drop (loss), duplicate (dup), then reorder (shuffle)
        Dgram del[CHAN_MAX * 2]; int delN = 0;
        for (int i = 0; i < airN; i++) {
            if (chance(lossPct)) continue;              // dropped in the air
            del[delN++] = air[i];
            if (chance(dupPct)) del[delN++] = air[i];   // duplicated
        }
        if (reorder) for (int i = delN - 1; i > 0; i--) { int j = (int)(rng() % (unsigned)(i + 1)); Dgram t = del[i]; del[i] = del[j]; del[j] = t; }

        // deliver each surviving datagram to the right endpoint
        for (int i = 0; i < delN; i++) {
            u8 ptype = del[i].len >= 2 ? del[i].b[1] : 0;
            if (ptype == PK_EVENT) {
                const DgbaEventPkt* pk = (const DgbaEventPkt*)del[i].b;
                int seat = pk->seat;
                net_event_rx_fragment(pk, (size_t)del[i].len);   // REAL reassembly
                net_event_send_ack(seat);                        // REAL cumulative ACK (pushes into g_air)
            } else if (ptype == PK_EVENT_ACK && del[i].len >= (int)sizeof(DgbaLinkPkt)) {
                const DgbaLinkPkt* pk = (const DgbaLinkPkt*)del[i].b;
                apply_ack(pk->seat, pk->round);                  // REAL un-acked-tail free
            }
        }

        // resend the un-acked tail (what net_rx_thread does every ~4ms) — REAL net_event_tx_one
        NetEvTx* tx = &s_evTx[0];
        for (u32 seq = tx->base; seq != tx->next; seq++)
            net_event_tx_one(0, seq, &tx->q[seq % NET_EV_TXQ], true);

        // drain everything deliverable in-order
        NetEvent out;
        while (net_event_recv(0, &out)) {
            if (gotN < 64) { got[gotN].type = out.type; got[gotN].arg = out.arg; got[gotN].len = out.len; memcpy(got[gotN].data, out.data, out.len); }
            gotN++;
        }

        if (gotN >= n && tx->base == tx->next) break;   // all delivered AND all acked -> converged
    }

    // ---- verify ----
    CHECK(rounds < MAX_ROUNDS, "%s: did NOT converge in %d rounds (gotN=%d/%d)\n", name, MAX_ROUNDS, gotN, n);
    CHECK(gotN == n, "%s: delivered %d events, expected EXACTLY %d (dup or loss!)\n", name, gotN, n);
    CHECK(s_evOverflow == 0, "%s: s_evOverflow=%d (must be 0 — never silently lose)\n", name, s_evOverflow);
    int okBytes = 1;
    for (int i = 0; i < n && i < gotN; i++) {
        if (got[i].type != refs[i].type || got[i].arg != refs[i].arg || got[i].len != refs[i].len ||
            memcmp(got[i].data, refs[i].data, refs[i].len) != 0) { okBytes = 0;
            printf("  FAIL: %s: event %d mismatch (in-order/content) type %u/%u len %u/%u\n",
                   name, i, got[i].type, refs[i].type, got[i].len, refs[i].len); }
    }
    CHECK(okBytes, "%s: all events delivered IN ORDER, byte-identical\n", name);
    printf("  %-22s loss=%2d%% dup=%2d%% reorder=%d -> %d events, %d rounds %s\n",
           name, lossPct, dupPct, reorder, gotN, rounds, (g_fail ? "" : "ok"));
    return rounds;
}

// Build the realistic trade event set: LinkPlayer(60) + 3x party chunk(200) + tiny controls, plus a
// synthetic 256-byte event to FORCE multi-fragment reassembly (real events are <=200B = 1 fragment).
static int make_trade_events(Ref* r) {
    int n = 0;
    r[n].type = CL_EV_LINKPLAYER; r[n].arg = 0;  r[n].len = 60;  for (int i=0;i<60;i++)  r[n].data[i]=(u8)(0xA0+i);  n++;
    for (int c = 0; c < 3; c++) { r[n].type = CL_EV_PARTY_CHUNK; r[n].arg=(u8)c; r[n].len=200; for(int i=0;i<200;i++) r[n].data[i]=(u8)(c*200+i); n++; }
    r[n].type = CL_EV_SELECT;  r[n].arg = 3;  r[n].len = 0;   n++;
    r[n].type = CL_EV_CONFIRM; r[n].arg = 0;  r[n].len = 0;   n++;
    r[n].type = 99;            r[n].arg = 7;  r[n].len = 256; for (int i=0;i<256;i++) r[n].data[i]=(u8)(0x33+i); n++;  // 2-fragment
    return n;
}

int main(void) {
    printf("==== netlink PK_EVENT reliability test (REAL transport code, simulated lossy channel) ====\n");
    Ref refs[16]; int n = make_trade_events(refs);
    printf("trade event set: %d events (incl. one 256-byte 2-fragment event)\n", n);

    run_scenario("clean",        refs, n,  0,  0, 0);   // sanity: no loss, no reorder
    run_scenario("reorder-only", refs, n,  0,  0, 1);
    run_scenario("loss-30",      refs, n, 30,  0, 1);
    run_scenario("loss-50",      refs, n, 50,  0, 1);
    run_scenario("dup-heavy",    refs, n,  0, 80, 1);   // exactly-once under massive duplication
    run_scenario("hostile",      refs, n, 40, 40, 1);   // loss + dup + reorder together

    printf("=========================================================================\n");
    printf("checks: %d   failures: %d\n", g_checks, g_fail);
    if (g_fail == 0) { printf("RESULT: PASS\n"); return 0; }
    printf("RESULT: FAIL\n"); return 1;
}
