# SPEC — D6 link-surface fingerprint + D7 PC-suite hardening (tools + ledger)

Phase-13-prep slices **D6** and **D7** (`PHASE.md` scope table). The PHASE.md **Invariants
bind every line of this spec** — repeated here where they bite:

- **The trade path is FROZEN** (Invariant 1). Nothing in this spec changes `cl_transfer`
  semantics, pacing constants, FSM transitions, netlink round/ack logic, or the gbacore SIO
  fill. Every deliverable is a read, a log, a test, a shell script, or a doc — plus exactly
  one new pre-link lobby packet type that the in-link path never sees.
- **Suites stay green and GROW** (Invariant 3): `test_celiolink.c` 417+ checks,
  `test_netlink_reliability.c` 66 checks, plus the new host binaries below.
- **Pure-C rule** (Invariant 4): the fingerprint module and every parser is header-free
  (`<stdint.h>`/`<string.h>` only) and dual-compiles on the PC.
- **Build proof** (Invariant 5) after each slice; **BUILDLOG.md entry** (Invariant 7).

Source designs (all cites verified against the checked-in docs):
`docs/kb/external/gen1-link.md` (Fingerprint/Handshake/transport),
`docs/kb/external/gen1-parity.md` (laggyPair fuzz, goldens+mutation, tri-ledger, verdict
honesty), `docs/kb/external/pm-rom-abi.md` (two-tier presence → the lobby-stage placement).

Files touched (summary — every change detailed in its slice):

| File | Change | Slice |
|---|---|---|
| `source/fingerprint.c` / `.h` | NEW pure-C module: surface struct, canonical bytes, two-lane FNV-1a, field-diff | D6 |
| `source/celiolink.h` | ONE line: `#define CL_PROTO_REV 1` (+ comment). No logic. | D6 |
| `source/gbacore.h`/`.c` | NEW read-only `gbacore_game_rev()` next to `gbacore_game_code()` | D6 |
| `source/netlink.h`/`.c` | NEW `PK_FPRINT` lobby packet + 3 API calls; lobby pump branch only | D6 |
| `source/wireless.c` | HUD mismatch line in the lobby phases; `net_fprint_set_local()` call | D6 |
| `source/gbacore.c` | ONE `# fprint` netlog header line (forward-decl getter, like `net_mono_ticks` at gbacore.c:314) | D6 |
| `test/test_celiolink.c` | ADDITIVE: TEST 16 golden, TEST 17 mutation, lag shim + lagged TEST-5/12 loops, mirror asserts | D6/D7a/D7b |
| `test/host/test_trace_replay.c` | NEW host harness (netlog parse + structural replay) | D7c |
| `test/fixtures/*.txt` | NEW: 4 real hw netlogs (list in D7c.2) | D7c |
| `tools/verdict.sh` | NEW post-run verdict grep tool | D7d |
| `docs/kb/celio/KNOWN-DIFFERENCES.md` | NEW tri-ledger, seeded (D7e.2) | D7e |

---

## D6 — link-surface fingerprint at UDS connect

### D6.1 The surface (what is hashed, what is deliberately excluded)

The gen1recomp lesson that governs everything here: **fingerprint over-coverage manifests
as FALSE incompatibility between actually-compatible peers** — hashing `catchRate` split
Red/Blue from Yellow even though the real cable links them freely, because no link mode
ever reads it (gen1recomp #511; gen1-parity.md §9, Fingerprint.lua:131-137). "Hash what
the protocol reads, not the ROM." Our surface is therefore five bytes-scale fields, no ROM
hashing:

| # | Field | Type | Source (existing code anchor) | In-hash? | Why |
|---|---|---|---|---|---|
| 1 | `gameCode[4]` | 4cc | ROM header 0xAC. Live: `gbacore_game_code()` (gbacore.c:1160-1163, `busRead8(0x080000AC+i)`); pre-boot file read precedent `rom_game_code()` (rompicker.c:65-71). Read from the **focused/participating** game — precedent main.c:1796 (HUD) and wireless.c:32-33 (`myCode` from the `myGameCode` param). | yes | Carried + hashed, but **never refuse-grade**: EM↔FR↔LG trades are legitimate on real hardware; a mismatch is at most the in-game "chose differently" linkType refusal. The existing scan-time "× different game" flag (wireless.c:195-201) stays as-is. |
| 2 | `gameRev` | u8 | ROM header **0xBC** ("software version", GBATEK header layout). **NEW read**: add `gbacore_game_rev()` beside `gbacore_game_code()` — one `busRead8(0x080000BC)`. The FR rev0/rev1 lesson: run #12 forensics proved the user's FR is **rev1** by exact cb2 matches (HANDOFF session 2026-07-15/16); revs share the RAM map and trade freely, so this field is **diagnostic, never refuse-grade** — its job is to make the netlog say which rev each console ran, so symbol-map decoding is never guessed again. | yes | |
| 3 | `clProtoRev` | u8 | **NEW** `#define CL_PROTO_REV 1` in celiolink.h (see D6.2). | yes | The one field that tracks OUR FSM's wire behavior. |
| 4 | `netProto` | u8 | `DGBA_PROTO` (netlink.c:13). Already broadcast in the beacon (`DgbaAdv.proto`, netlink.c:17-21, set at :197) and copied out at scan (:223) — but **never compared anywhere today**. The fingerprint exchange makes it finally checked. | yes | |
| 5 | `modeFlags` | u8 | Low nibble = the session-entry link strategy. **NOT** a live read of `s_netExp` (gbacore.c:388) — at lobby time that static is 0/stale; it is only set to 5 (state F) at link attach (gbacore.c:748). Instead: a new shared `#define DGBA_NET_EXP_DEFAULT 5` used by BOTH gbacore.c:748 and the fingerprint fill (single source of truth). Bit 7 reserved 0. | yes | KEY_Y mid-link re-toggles are NOT re-exchanged — mid-link KEY_Y is already forbidden (HANDOFF run-#11 note: "DON'T press Y mid-link"). Documented limitation, see Open Questions. |

**Deliberately excluded** (each with the #511 justification):
- **Full ROM CRC**: would split revisions/languages/trainer-hacked ROMs that trade
  perfectly — the exact catchRate failure mode. (The dead `DgbaAdv.romCrc` field —
  always passed 0 at wireless.c:70 — stays dead; do NOT wire it up.)
- **Trainer identity / party data**: crosses later via the existing `CL_EV_LINKPLAYER` /
  `CL_EV_TRAINERCARD` events (celiolink.h:126-135); identity is per-session data, not
  link-surface compat.
- **App build id / netlog format rev**: changes nothing about whether two consoles can
  trade; the netlog header already self-describes.
- Language registries excluded by construction (there are none in our surface) — noted
  because gen1 excluded them explicitly so two languages interoperate
  (gen1-link.md, Handshake.lua:71-84): our analog is exactly the ROM-CRC exclusion.

### D6.2 `CL_PROTO_REV` in celiolink.h

One line, next to the pacing constants block (celiolink.h:163-171):

```c
// Protocol revision of THIS FSM's wire-visible behavior + CL_EV_* semantic contract.
// BUMP whenever a change alters what the synthesized partner puts on the (virtual) wire
// or what a CL_EV_* event means — and NEVER without, in the same commit:
//   (1) an entry in docs/kb/celio/KNOWN-DIFFERENCES.md (or BUILDLOG) describing the change,
//   (2) re-pinning the fingerprint golden in test_celiolink.c TEST 16.
// (gen1recomp bless discipline: "moving the fingerprint breaks linking between every
//  existing build and every new one — that is a parity change", gen1-parity.md §9.)
#define CL_PROTO_REV 1
```

No other celiolink.h/.c change. The FSM never reads it; only fingerprint.c does.

### D6.3 Pure-C hash module — `source/fingerprint.c` / `.h`

Header-free (Invariant 4). Contents:

```c
typedef struct {            // canonical wire layout — packed, fixed order, no padding
    uint8_t gameCode[4];    // ROM 0xAC, raw bytes
    uint8_t gameRev;        // ROM 0xBC
    uint8_t clProtoRev;     // CL_PROTO_REV
    uint8_t netProto;       // DGBA_PROTO
    uint8_t modeFlags;      // DGBA_NET_EXP_DEFAULT in low nibble
} DgbaFprint;               // 8 bytes — this byte sequence IS the canonical serialization

uint64_t dgba_fprint_hash(const DgbaFprint* f);          // two-lane FNV-1a (below)
int      dgba_fprint_diff(const DgbaFprint* a, const DgbaFprint* b,
                          char* out, int outMax);        // 0 = match; else names fields
```

- **Canonical serialization**: the packed struct bytes in declared order. gen1 needed
  sorted-key discipline because Lua `pairs()` order is unstable ("a digest that inherited
  it would reject identical peers at random", Fingerprint.lua:7-10); a fixed C struct of
  u8s has no such instability — but the rule survives as: *the struct layout is the wire
  format; never hash in-memory padding; fields are appended only, never reordered*
  (additive-versioning discipline, Handshake.lua:1-8: absence of a field decodes to
  legacy behavior).
- **Hash: FNV-1a, two 32-bit lanes with different offset bases → one u64** — the exact
  gen1 construction (Fingerprint.lua:21-63), trivial in C:
  lane A: offset 2166136261u, lane B: offset 0x811C9DC5 ^ 0xDEADBEEF (any second
  documented base), both prime 16777619, folded `((u64)laneA << 32) | laneB`.
- **`dgba_fprint_diff` names what differs** — the gen1 modDiff lesson: show "exactly what
  differs on the incompat screen instead of a silent mid-battle draw"
  (Handshake.lua:215-236, via gen1-link.md). Output like `"clProtoRev(1!=2) gameCode"`.
  The HUD and netlog print THIS, not just hashes.

### D6.4 Where it crosses: the lobby pump — strictly BEFORE `net_link_start`

**Placement rationale** (pm-rom-abi.md §7.4 two-tier presence: lobby-stage facts belong
to the lobby tier, and nothing game-active may depend on them): the fingerprint is a
**lobby-stage** exchange. The two candidate channels and the decision:

- The `net_event_*` channel (netlink.h:99-139, the run-#12 trainer-card precedent) is
  reliable/ordered — but it only exists after `net_link_start` spins the RX thread
  (netlink.h:74-83) and `net_event_reset` runs at link start. Using it would put the
  exchange INSIDE the link window. **Rejected.**
- The **lobby pump** `net_ping_update()` (netlink.c:266-300) is the sole packet path
  while `!s_rxRun` — it already owns pull/echo/RTT pre-link, and the trade path never
  runs concurrently with it. **Chosen.** This satisfies the charter's "strictly BEFORE
  cl link start": gbacore/celiolink are untouched; by the time the SIO driver attaches,
  the exchange is already done (or reported unknown).

**Wire format** — new packet type + struct in netlink.c (precedent: `DgbaEventPkt`,
netlink.c:60-70, which already established that non-16-byte packets ride the same
`NET_PKT_BUF` pull):

```c
#define PK_FPRINT 10
typedef struct __attribute__((packed)) {
    u8  magic;        // 'G'
    u8  type;         // PK_FPRINT
    u8  seat;         // sender's lobby role: 1 = host, 0 = client (myNode==1 test)
    u8  pad;
    u8  surface[8];   // the DgbaFprint canonical bytes (opaque to netlink — no
                      //  celiolink/fingerprint include; same opacity rule as ClEvent,
                      //  netlink.h:107-110)
    u64 hash;         // dgba_fprint_hash of surface (LE)
} DgbaFprintPkt;      // 20 bytes; passes the got >= sizeof(DgbaLinkPkt)=16 gate at :270
```

**Exchange protocol** (loss-tolerant by repetition, no new reliability machinery):
- `net_fprint_set_local(const void* surface8, u64 hash)` — called by wireless.c BEFORE
  `net_session_host` (:70) / `net_session_join`, from a `DgbaFprint` filled via
  `gbacore_game_code` + the new `gbacore_game_rev` on the focused game (fall back to the
  rompicker file-read path when no core is loaded yet — same fallback the advertisement
  gameCode already uses).
- In the lobby pump's send half (netlink.c:288-300), piggyback one `PK_FPRINT` on the
  existing ~6 Hz ping cadence **while the peer's fingerprint has not arrived, plus 3
  extra sends after it has** (so both sides converge even under loss; idempotent —
  receiving a duplicate overwrites with identical bytes).
- In the lobby pull loop (netlink.c:270-285), add the `PK_FPRINT` branch: stash
  `s_fprintPeer` + `s_fprintPeerHash`, set `s_fprintPeerValid`.
- `int net_fprint_peer(void* out8, u64* hash)` → 1 once received.
- `int net_fprint_log(char* buf, int max)` → formats the `# fprint` netlog line (D6.6);
  gbacore.c calls it via forward declaration (precedent: `net_mono_ticks` fwd-decl at
  gbacore.c:314 keeps that TU libctru-free).
- All state cleared in `net_session_close` alongside the ping state (netlink.c:250-260).

Multi-seat note: today's flow is 2-console; the packet carries `seat` so a future 3-4
player lobby (HANDOFF Next steps #3) compares per-peer. v0 stores ONE peer fingerprint
(the resolved peer — `net_resolve_peer`, netlink.c:157).

### D6.5 Mismatch behavior: **allow-with-warning** (v0) — and why

PHASE.md's D6 row is binding and already decides this: "mismatch = loud lobby-stage
warning + netlog line (narrow surface — over-hashing rejects compatible peers,
gen1recomp #511)". Per-field justification:

| Field differs | Verdict | Rationale |
|---|---|---|
| gameCode | warn (existing "× different game" UX kept) | EM↔FR trade is a REAL feature; refusal would be a false rejection. In-game linkType mismatch already fails safe ("chose differently" — HANDOFF Next steps #4). |
| gameRev | warn (info-grade) | FR rev0↔rev1 trade freely (run-#12 forensics); the value's real job is forensic. |
| netProto | warn, **strongly worded** | A framing change would break everything — but a refusal path is new behavior next to the frozen transport; and today both consoles always run the same build. See Open Questions for promotion to refuse-grade. |
| clProtoRev | warn, **strongly worded** | Same reasoning; the warning names the field so the user knows to update the other console. |
| modeFlags | warn | A/…/F mismatch at session entry would produce the known state-mix failure modes; loudly naming it turns a mystery hardware run into a one-line diagnosis. |

The **verdict ladder** concept (full / subset / refused, Handshake.lua:170-189 via
gen1-link.md) is intentionally NOT implemented in v0 — we have no subset semantics yet.
The diff string is the whole verdict. UNKNOWN discipline: if the peer fingerprint never
arrived, the verdict is `unknown`, displayed and logged as such — never treated as a
match (the run-#11 lost-log lesson generalized: absence of evidence ≠ PASS).

### D6.6 Surfaces of the warning

- **HUD (lobby)**: in wireless.c's connected phase (the seat map / status area,
  wireless.c:160-200), one line: `link-surface: MATCH` (dim) or
  `link-surface DIFF: clProtoRev(1!=2)` (drawn in `THEME_QUIT_TEXT` red, both screens'
  text budget permitting — same styling as the existing "× diff" flag at :169). Also
  echo into the existing `status[]` line so it is visible without squinting.
- **Netlog**: one header line in the gbacore dump (next to `# transport`,
  gbacore.c:869), via `net_fprint_log`:
  `# fprint local=BPEE r1 cl1 np1 m5 h=1A2B3C4D5E6F7788 peer=BPRE r0 cl1 np1 m5 h=... verdict=DIFF:gameCode,gameRev`
  (or `peer=none verdict=unknown`). Note the netlog only exists when a link ran; the HUD
  is the only surface for a lobby that never starts — acceptable, documented.

### D6.7 Golden + mutation tests (TEST 16 / TEST 17 in test_celiolink.c)

`fingerprint.c` is `#include`d into the test TU the same way celiolink.c is
(test_celiolink.c:19).

- **TEST 16 — GOLDEN**: a fixed surface `{"BPEE",0,1,1,5}` hashes to a **pinned hex
  constant written into the test**. Rationale verbatim from gen1
  (gate_fingerprint.lua:1-13): an accidental edit "flips one hex string and fails"
  where no self-consistent check would notice. Pin BOTH 32-bit lanes separately AND the
  folded u64 (lane-independence visibility). Also pin a SECOND golden for
  `{"BPRE",1,1,1,5}` (the user's real FR-rev1) so the pair proves gameCode and rev both
  participate.
- **TEST 17 — MUTATION**: flip each of the 5 fields one at a time from the TEST-16 base;
  each mutation MUST move the u64 ("If any of these pass unchanged the gate is
  decorative", gate_fingerprint.lua:50-110). Plus: determinism (two computes equal);
  mutation isolation (restoring the field restores the golden); `dgba_fprint_diff`
  names exactly the mutated field.
- **BLESS discipline**: the golden constant may be changed ONLY in a commit that also
  carries the KNOWN-DIFFERENCES/BUILDLOG entry for the surface change (D6.2 comment;
  gen1-parity.md §9). There is no `--bless` tooling in v0 — the pinned-constant-in-test
  + the comment IS the gate.

Estimated new checks: ~25.

---

## D7a — laggyPair delay shim over the two-instance tests

### D7a.1 How events are delivered today (evidence)

The two-instance harness (TEST 5) shuttles semantic events **synchronously and
instantly** through `relay_events()` / `relay_until_event()`
(test_celiolink.c:497-516): a `while (cl_take_outgoing(from,&ev)) cl_put_incoming(to,&ev)`
loop — zero latency, zero reordering. TEST 12 feeds peer events by direct
`cl_put_incoming` at scripted moments (test_celiolink.c:1070-1072). gen1's verdict on
exactly this shape: "loopbackPair delivers instantly, which is the one thing the real
relay never does" (link_desync_fuzz.lua:65-91, via gen1-parity.md §7). Our real channel
is the UDS event plane at measured ~23 ms RTT (HANDOFF: rtt~24 ms, pure-ping ~5-24 ms)
≈ **1-2 game frames** of one-way delay.

### D7a.2 The shim (additive, test-file-only)

```c
typedef struct { ClEvent ev; int due; } LagSlot;
typedef struct { LagSlot q[32]; int n; int now; uint32_t rng; } LagQ;   // rng = Park-Miller state
static void lag_put(LagQ* q, const ClEvent* ev, int delay);   // due = now + delay
static int  lag_pump(LagQ* q, CelioLink* to);                 // now++; deliver all due (in order)
static int  relay_events_lagged(CelioLink* from, LagQ* q, int delay);   // take_outgoing -> lag_put
```

- One `LagQ` per direction (A→B, B→A). `lag_pump` is called once per "pump" (one
  frame_drive step of the receiving side) — 1 pump ≈ 1 game frame ≈ 16.7 ms, so
  delay 1-2 models the measured radio and the sweep runs **0..4** (charter).
- Delay per event is either fixed (the sweep) or drawn from a **Park-Miller PRNG whose
  seed is printed on any failure** — "a failure replays from its seed alone"
  (link_desync_fuzz.lua:44-54); `main(argc,argv)` grows optional `[runs] [firstSeed]`
  (default 3 randomized runs after the fixed sweep). In-order delivery is preserved
  (the real channel is reliable-ordered, netlink.h:117-128 — we model LATENCY, not
  reorder; reorder/loss are the netlink host test's job, test_netlink_reliability.c).

### D7a.3 Which TESTs run under lag (honest resolution of the charter's "5/6/7/12")

- **TEST 5** — the only true two-instance relay test — is wrapped:
  `for lagAB in 0..4: for lagBA in {0, lagAB}:` re-run the whole coordination flow with
  `relay_events`/`relay_until_event` replaced by the lagged versions, `lag_pump` invoked
  inside every frame-drive loop that awaits a release. This directly exercises the
  run-#8 defect class (edge- vs LEVEL-triggered hold releases: holds must re-assert
  every idle frame until the delayed event lands).
- **TEST 12** — single-instance, but its peer-event feed (test_celiolink.c:1070-1072)
  gains a lag variant: delay the `CL_EV_PARTY_CHUNK` delivery by k=0..4 extra idle
  `frame_drive`s and assert the PARTY0 hold persists for EXACTLY those k frames and
  releases on the next INIT after delivery (level-trigger proof).
- **TESTs 6 (a/b/c) and 7** — **no lagged variant, on purpose.** TEST 6 is the
  game↔dongle handshake (local, no radio component: test_celiolink.c:746-855); TEST 7
  asserts `cl_next_delay_us` pacing constants (celiolink.c:1036-1041). Neither has a
  cross-console event to delay; a "lagged" run would be fake coverage. The gen1 CI
  lesson applies: reporting exercised-but-actually-unwired coverage "is what made the
  whole pipeline look delivered" (gen1-parity.md §12, test.sh:144-159) — we document
  the exclusion instead.

Estimated new checks: the TEST-5 body (~40 checks) × 6 lag combos + TEST-12 lag loop
(~10) ⇒ ~250 additional check executions (the counter counts executions; the printed
total will jump well past 417 — that is fine and expected, Invariant 3 says GROW).

---

## D7b — per-round agreement across both synthesized sides

### D7b.1 What must NOT be compared (resolved against the source)

The charter suggests "the mutual-CRC words already computed". Evidence says: the
additive mutual CRC (celiolink.c:1113-1146 — CRC slot emits the running accumulator
:1115, command words fold rx at :1129 and tx at :1134) covers **each console's OWN
game↔dongle word stream**. Under local termination the two consoles run *different
conversations by design* (celiolink.h:21-27) — e.g. run #9's HOST ring shows its game
streaming `BBBB/00C8/0080` while the JOIN was elsewhere entirely. **Cross-instance CRC
equality is a FALSE invariant** and would institutionalize a #511-style false rejection
inside our own suite. Per-instance CRC correctness is already asserted every frame vs an
independent sum (`CrcTracker`/`frame_drive`, test_celiolink.c:322-346) — that stays the
CRC gate.

### D7b.2 What MUST agree: the mirrored coordination state

The real cross-console invariant is the **mirror**: A's "local" facts must equal B's
"partner" facts once the relay settles — the exact shape of gen1's final-HP mirror check
(`A.player.hp == B.enemy.hp`, link_desync_fuzz.lua:216-240 via gen1-parity.md §7) and
per-turn `battleA.localHashes[turn] == battleB.localHashes[turn]`
(run_link_tests.lua:386-391 via §6). The mirror set (all fields exist today —
celiolink.h:316-337 + ClStatus :208-229):

| A-side | must equal | B-side (and symmetrically) |
|---|---|---|
| `localSelectSlot` | == | `partnerSelectSlot` |
| `localConfirmed` | == | `partnerConfirmed` |
| `localParty` bytes emitted (per chunk window) | byte-exact | `partnerParty` bytes captured |
| `tradeComplete` | converges == | `tradeComplete` |
| section pair | ∈ legal-pair table | (one side may lead by a bounded margin during transitions) |
| holds (`partnerPartyHeld`/`selectHeld`/`confirmHeld`) | all 0 at convergence points | same |

### D7b.3 The assertion

```c
// assert_mirrored(A, B, pumpIdx, settled): field-by-field, NOT a single hash.
```

- Called **every pump** of the lagged TEST-5 loop, with a `settled` flag: while events
  are legitimately in flight (LagQ non-empty) only the *monotone* facts are asserted
  (slots never change once set; tradeComplete never reverts); full equality is asserted
  after each relay drain — the drain-before-compare rule ("let it drain… or the check
  reads a mid-match frame", gen1-parity.md §8).
- Field-by-field beats a digest here **deliberately**: gen1's v1 single-hash "ended in a
  draw that explained nothing"; the 3-part split exists so a failure NAMES the component
  (LinkBattle.lua:93-97/118-127 via gen1-link.md). With six fields we print the field
  name + pump index + lag seed directly — maximal localization, no hash needed.
- Section-pair legality table (initial): {SETUP,SETUP} {SETUP,CONNECTION}
  {CONNECTION,CONNECTION} {CONNECTION,DISCONNECT} {DISCONNECT,DISCONNECT}
  {CONNECTION,LOUNGE} {LOUNGE,LOUNGE} {DISCONNECT,CONNECTION} — derived from the
  dispatcher's transitions (celiolink.c section switches; cancel→LOUNGE proven by
  TEST 11, reset→SETUP by TEST 9). Any other pair observed = FAIL naming both sections.

Estimated new checks: ~15 per TEST-5 lag combo (~90 executions).

---

## D7c — golden-trace replay harness (`test/host/test_trace_replay.c`)

### D7c.1 Corpus correction (evidence — this supersedes the charter's file pointer)

The charter names `netlogs-archive-2026-07-06.tar.gz` and "e.g. run-#9 complete trade,
run-#10 host". **The archive does not contain them.** Verified: the archive's 99 files
end at `*_0706_1057xx` — its latest HOST log (`3DGBA_net_HOST_0706_105724.txt`) carries
the run-#7 signature (`txSeq=4 txAcked=4 rxDelivered=0` — the run-#7 own-stream-poll
bug), i.e. the archive covers runs #1–#7 only. The completed-trade evidence lives in the
**working-tree `netlogs/` folder** (54 files, runs #9–#12), which — unlike the archive —
also has logs from builds WITH the `# celio-trace` feature (added post-run-#9). Two of
those are complete-from-round-0 streams. Fixtures come from BOTH sources.

### D7c.2 Fixture list (copy verbatim into `test/fixtures/`, names kept)

| Fixture | Source | Why (evidence) | Tier (D7c.4) |
|---|---|---|---|
| `3DGBA_net_JOIN_0703_075344.txt` | archive | Complete stream from round 0 (656 rows, `startN=0`); the pre-gate era: dongle drives `B9A0`×8→`8FFF` at a silent (0000) game — the recorded shape of TEST 6a. Header `frames=0` = FSM never framed. | S (parse + framing + header) |
| `3DGBA_net_HOST_0706_114305.txt` | netlogs/ | Complete from round 0 (412 rows) **with 11 `# celio-trace` lines** walking SETUP→CONNECTION (f=1 `0000` … f=16 `5FFF` … f=17 sec=1). | S + trace-legality |
| `3DGBA_net_HOST_0715_162959.txt` | netlogs/ | Run #12 HOST: `tradeComplete=1`, 32 trace lines, ring mid-session (first=51860). | header + trace-legality (ring not replayable) |
| `3DGBA_net_JOIN_0715_162349.txt` | netlogs/ | Run #12 JOIN: `tradeComplete=1 rxDelivered=10`, 32 trace lines. | header + trace-legality |

(Fixture bytes are user gameplay words/counters — no ROM code; flag them to the
`release-legal-audit` gate as save-derived user data, same class as the netlogs already
in-tree.)

### D7c.3 Line formats the parser must handle (cite = the writer)

All from the single netlog writer in gbacore.c (Do NOT invent a new format — charter):
- `# 3DGBA netlog role=%s seat=%d startN=… toN=…` (gbacore.c:838-839) — role + seat.
- `# celio section=%d state=%d blk=%d frames=%d partnerPartyB=%d tradeComplete=%d gateN=… resetN=… forceN=… sioMode=… siocnt=… [exitP=… sessEnd=… pCard=… idReal=…]` (:896-899; the bracketed fields absent on pre-run-#12 builds — parse both).
- `# celio-trace f=%lu cmd=%04X sec=%u st=%u blk=%u` (:904).
- `# event txSeq=%d txAcked=%d rxDelivered=%d overflow=%d retransmits=%d` (:908).
- CSV header + rows `idx,round,frame,dvbl,dt_us,rtt_us,paceus,turnus,exp,w0,w1,ok,sub` (:909-919).
- **Column-role resolution (verified against real logs)**: `w0`=seat-0 word, `w1`=seat-1
  word. On a `role=HOST` log the LOCAL GAME is seat 0 → game word = `w0`, dongle reply =
  `w1` (HOST_0706_114305 row 0: `w0=0000,w1=B9A0` — the dongle advertising). On
  `role=JOIN` the game is seat 1 → game = `w1`, dongle = `w0` (JOIN_0703_075344:
  `w0=B9A0…8FFF, w1=0000`). The parser derives (input,expected) columns from the header
  role.

### D7c.4 What is honestly replayable without the radio — the two tiers

- **Tier S — STRUCTURAL (build-independent; ships enabled).** For round-0 fixtures,
  walk a framing-only mirror of the PacketLayer (handshake words until the 8FFF exit,
  then repeating [1 CRC slot + 8 command words], with re-handshake runs — the state walk
  of celiolink.c:1090-1146, framing only, no section synthesis) and assert over the
  RECORDED stream:
  1. every dongle word in handshake phase ∈ {B9A0, 8FFF, D15E} (celiolink.h:55-59);
  2. **CRC-slot arithmetic**: each recorded dongle CRC-slot word equals the additive sum
     of the PREVIOUS recorded 16-word frame (seed 0xB9A0 spent as the first CRC word —
     the TEST-2 timeline, test_celiolink.c:113-171) — pure math over recorded bytes,
     true on every build, and it proves the parser's frame alignment is right;
  3. header consistency (e.g. `frames=0` ⇔ no completed command frame found);
  4. every `# celio-trace` (sec,st,blk) edge ∈ the legal-transition table (D7b.3's
     section pairs + blockSeq ordering CL_BLK_LINKPLAYER→…→CL_BLK_LINKCMD,
     celiolink.h:193-205).
- **Tier R — EXACT REPLAY (ships present but SKIPPING, loudly).** Feed the game column
  into a fresh `cl_init`'d FSM (`#include ../../source/celiolink.c`, the TEST-harness
  pattern) and diff every output word against the recorded dongle column, plus the
  replayed trace tail vs the recorded `# celio-trace` lines. **Valid only for logs
  recorded on the current FSM build** — every existing fixture predates run-#12's fixes
  (HOST_0706_114305 = the 0706 11:28 build; the 0715 logs = the pre-fix run-#12 build),
  so word-exact equality WOULD legitimately fail where behavior intentionally changed.
  Tier R therefore prints `SKIP: no current-build golden traces yet (bless from run #13)`
  — never a vacuous pass (the gen1 screenshot-differ lesson: an unwired tier reports
  itself unwired, gen1-parity.md §12, ci.yml:157-211). **Run #13's round-0 logs, recorded
  on the current build, become the first tier-R goldens** — the harness accepts any log
  path as argv so blessing = copying a run-#13 log into fixtures and removing the skip.
- **Never replayable, stated in the file header**: radio timing (`dt_us/rtt_us`), UDS
  loss/reorder, and the PEER console's semantic events (the ring records no ClEvent
  payloads) — event-driven holds are exercised only insofar as the recorded word stream
  shows their effects. Full two-console replay would need paired event logs (Open
  Questions).

### D7c.5 Build & run

```
clang -std=c11 -Wall -Wextra -O2 -I source test/host/test_trace_replay.c -o /tmp/tr && /tmp/tr            # all fixtures
/tmp/tr netlogs/3DGBA_net_HOST_<new>.txt                                                                  # ad-hoc / bless preview
```

Pure C, no 3ds.h needed (it parses text + includes celiolink.c only). One PASS/FAIL line
per fixture per tier; exit nonzero on any FAIL (SKIP ≠ FAIL).

---

## D7d — `tools/verdict.sh` — post-run netlog verdict

### D7d.1 Contract

`tools/verdict.sh <run-folder>` — a folder holding ONE run's copied logs (the
established workflow: wipe `sdmc:/cias/netlogs/` before, copy all after; one run = one
folder). Output: one line per run-#13 checklist item (HANDOFF Next steps #2 items a-d)
plus health lines:

```
13a trade-completes        PASS|FAIL|UNKNOWN   <evidence>
13b trainer-card           PASS|INFO|UNKNOWN   <evidence>          (see below)
13c room-exit              PASS|FAIL|UNKNOWN   <evidence>
13d re-link-after-exit     PASS|FAIL|UNKNOWN   <evidence>
hp  event-channel          PASS|FAIL|UNKNOWN   overflow=…
hp  wedge-escape           PASS|WARN|UNKNOWN   forceN=…
hp  link-error             PASS|FAIL|UNKNOWN   lerr/cb2
logs host-net|join-net|host-gs|join-gs  PRESENT|MISSING
```

**The prime directive: missing log ⇒ UNKNOWN, never PASS** — the run-#11 lesson (the
JOIN log was lost to a stray Quit and the fix went unproven for two more runs). Any item
whose evidence file is absent, or whose pattern's feature postdates the log's build
(e.g. no `exitP=` field ⇒ pre-run-#12 build), reports UNKNOWN with the reason.

### D7d.2 Exact patterns (each cites the writer that produces it)

Role files (main.c:100 `3DGBA_net_%s_MMDD_HHMMSS.txt`, :115-121 gs, :127-132 touch):
newest `3DGBA_net_HOST_*` / `3DGBA_net_JOIN_*` / `3DGBA_gs_HOST_*` / `3DGBA_gs_JOIN_*`
per folder (there can be several per run — sessions re-attach; take the LAST by name,
report the count).

| Item | Pattern (grep -E over the named file) | Writer cite | Verdict rule |
|---|---|---|---|
| 13a | `tradeComplete=1` in `# celio ` line, BOTH net logs; supporting `partnerPartyB=600` and `# event .*rxDelivered=([1-9])` | gbacore.c:896-899, :908 | both=PASS; one=FAIL naming the side; header absent=UNKNOWN |
| 13b | `pCard=1` / `idReal=1` in `# celio ` | gbacore.c:896 | idReal=1 ⇒ PASS(real card served); idReal=0+pCard-any on a FIRST-EVER linkup ⇒ INFO "canned card expected — verify render by eye"; the RENDER itself is not log-provable ⇒ never auto-PASS the checklist's "card renders", print `needs-eyes` |
| 13c | `exitP=1` AND `sessEnd=1` in `# celio `, BOTH net logs; AND NOT red-screen (below) | gbacore.c:896-899 | both+no-red=PASS; exitP=1&sessEnd=0=FAIL(close never answered); absent field=UNKNOWN(pre-#12 build) |
| 13d | `resetN=([1-9])` in `# celio `; supporting `# celio-trace .*sec=0` AFTER a `sec=2|3` entry (fresh SETUP) | gbacore.c:896, :904 | present=PASS; resetN=0 but user reports retrying=FAIL; no retry attempted=UNKNOWN (the tool cannot know — print "if re-link was attempted") |
| hp event | `overflow=0` in `# event ` (must); `retransmits=` reported informationally | gbacore.c:908 | overflow>0=FAIL (the never-silent-drop invariant, netlink.h:137) |
| hp wedge | `forceN=0` PASS; `forceN=[1-9][0-9]*` WARN "gate wedged but escaped n×" | gbacore.c:896 | run-#6 precedent: forceN=21 with a working session ⇒ WARN not FAIL |
| hp link-error | gs log: any data row with `lerr` column = 1, or cb2 ∈ {`0800B1A0`,`0800AF2C`} (CB2_PrintErrorMessage EM/FR — documented in the gs header itself) | gamestate.c:306-311 (header writer; :311 names the red-screen cb2 values) | any hit=FAIL quoting the row |

### D7d.3 Implementation rules

POSIX sh + grep/awk only (runs on macOS out of the box). Every verdict line ends with
the literal pattern used (auditable: a wrong verdict is diagnosable from its own
output). Exit code: nonzero iff any FAIL (UNKNOWN exits 0 but is impossible to miss —
it prints in the summary count). The script never writes anything.

---

## D7e — `docs/kb/celio/KNOWN-DIFFERENCES.md` — the tri-ledger

### D7e.1 Discipline (imported from gen1-parity.md §3, adapted)

gen1's three-way split: faithfully-ported (citations) / known-differences (ONLY genuine
divergences, each labeled intentional + why) / new-features. Our mapping: the
faithful-port record already exists (`docs/kb/celio/PORT-SPEC.md` + the 0706 audit in
`docs/kb/celio/audit/`); the new file is the middle ledger. Rules, stated in its header:
- Every entry: **what Celio/pret does** (file:line cite), **what we do** (file:line),
  **why the divergence exists**, **status** ∈ {`hw-validated (run #N)` /
  `PC-only (TEST N)` / `unmeasured`}, and the literal label "intentional divergence".
- "unmeasured = remaining work, not known-good" (gen1-parity.md §1 delta-column
  discipline).
- Entries are never deleted; status only moves forward (the DEBT-ratchet spirit,
  gen1-parity.md §13: "the ledger cannot rot into a permanent excuse").
- **Coupling rule (binds D6)**: any change to the FSM's wire-visible behavior requires,
  in ONE commit: the ledger entry + the `CL_PROTO_REV` bump + the re-pinned fingerprint
  golden (D6.2/D6.7).

### D7e.2 Seed entries (the charter's six, plus documented extras)

| # | Divergence | Celio/pret side (cite) | Our side (cite) | Status |
|---|---|---|---|---|
| 1 | **Proactive room heartbeat** — the partner sends key frames UNPROMPTED while the room phase is live | Celio's dongle is the SIO slave of a real game; a slave game self-heartbeats via its own echo. pret link.c:1793: RECEIVED_NOTHING + IsSendingKeysOverCable skips the WHOLE game frame — a frozen slave can never send the frame that would wake it | `roomKeys` flag + unconditional idle key frames (celiolink.h:260, celiolink.c:754-796); runs #4/#5 chicken-and-egg history | hw-validated (run #6: JOIN room live key exchange; runs #8-#10 trades) |
| 2 | **Whole-idle holds only on data-exchange waits** — peer-gated sections answer EMPTY frames and do not advance | Celio synthesizes from a ONE-SHOT uploaded party file — it never waits on a live peer at all (celiolink.h:119-122 port note) | the M1 hold gates (celiolink.h:311-325); holds are LEVEL-triggered every idle frame (run-#8 fix) | hw-validated (run #2 hold benign on hw; runs #8-#10) |
| 3 | **Wedge-escape ceiling** on the edge-strict capture gate (IE.SIO-masked or 60k-cycle ceiling; `forceN` diag) | no Celio analog — the gate itself is a 3DGBA/mGBA construct | gbacore.c edge gate + F1 escape (run-#6 audit fix; `forceN=` in `# celio`, gbacore.c:896) | hw-validated (run #6: forceN=21, session healthy) |
| 4 | **Over-drain pacing** — WORD 350 µs / FRAME 2500 µs ≈ 2× nominal drain | Celio packetLayer.hpp:51-53: 30097 / 1378 / 12953 µs (kb `celio/packetLayer.md:351-359`); real-master nominal ≈ 752/7637 µs | CL_TIMING_* (celiolink.h:166-171, with the run-#6 measurement rationale in the comment); per-word ISR-ack waits make effective ≈1.2-1.5×; under-drain fatal, over-drain absorbed by catch-up | hw-validated (run #7+: send-queue overflow gone) |
| 5 | **LOUNGE = full room re-establishment** (READY_EXIT_STANDBY answered, heartbeat gated on roomKeys) instead of a passive lounge | Celio tradeLounge.cpp is a thin idle section (kb `celio/trade-fsm.md`) — sufficient when the dongle's game is the master | the run-#10 rebuild (celiolink.c LOUNGE section; TEST 13, test_celiolink.c:951-998) — the slave-game console blacks out on an unestablished room | partially hw-validated (re-entry proven run #12; the EXIT half superseded by entry 6) |
| 6 | **Exit-fade silence + 5FFF session-end + CL_EV_EXIT_ROOM relay** (both games leave together) | pret link.c:1319: LinkCB_ReadyCloseLink hard-gated on `gLastRecvQueueCount==0`; a real exiting partner just goes silent. Celio has no cross-console "both leave" concept (one game) | exitPending / exitZeroRun / sessionEnded (celiolink.h:261-270, CL_EXIT_ZERO_RUN=4 :237-240); CL_EV_EXIT_ROOM (celiolink.h:132-133); TEST 14 (test_celiolink.c:1082-1192) | **PC-only (TEST 14) — awaiting HW run #13** |
| 7 | CRC-word-after-fresh-handshake gets the 13 ms frame gap instead of Celio's 1.4 ms | packetLayer word timing (kb packetLayer.md:98-101) | celiolink.h:397-399 — the comment literally says "Divergence from Celio … slower = safe" | unmeasured (never isolated on hw) |
| 8 | **MASTER partner escalates to driving 0x8FFF** after 8 advertisements; re-handshake detector on a B9A0/D15E run | Celio's EMU master drives per its own thread timing; no re-arm-on-run detector (the dongle never misses the start) | CL_HS_ESCALATE_AFTER / CL_HS_REARM_RUN (celiolink.h:163-164); TESTs 6a/6b/6c | hw-validated (runs #2/#3 fixes; every later trade) |
| 9 | **Capture-under-hold pipeline** (pull the local party DURING the hold; blockSeq-derived windows; per-chunk serve holds) | Celio: party preloaded, nothing to capture (one-shot file) | cl_capture_feed_* + window filing (run-#6/#7/#8 fixes; TEST 12, test_celiolink.c:999-1080) | hw-validated (run #8: parties crossed; runs #9/#10 trades) |
| 10 | **Identity latch + real trainer-card serve with version-consistency guard** (canned only as fallback) | Celio's partner identity IS canned (its use-case has no live peer) | identityLatched/shippedIdentity/partnerCard (celiolink.h:277-287); TEST 15 | card path PC-only (TEST 15, awaiting run #13); identity latch hw-validated (runs #9-#12) |

(Entries 8-10 exceed the charter's six — they are already-documented divergences pulled
from celiolink.h comments and the HANDOFF; seeding them now keeps the ledger honest from
day one.)

### D7e.3 Status upgrades from run #13

Running `tools/verdict.sh` on run #13's folder and copying its PASS lines into the
ledger's status column (entries 6 and 10 → hw-validated on success) is part of the
run-#13 post-mortem checklist — add this line to the HANDOFF checklist when this spec
lands (PHASE.md Invariant 8).

---

## Acceptance gates (exit criteria for both slices)

1. `clang -std=c11 -Wall -Wextra -O0 -g -I source test/test_celiolink.c -o /tmp/tc && /tmp/tc`
   — PASS, check count **strictly > 417** (expect ≈700+ executions with the lag sweep).
2. `clang -std=c11 -Wall -Wextra -O2 -I test/host -I source test/host/test_netlink_reliability.c -o /tmp/tn && /tmp/tn`
   — 66 PASS, byte-untouched by this work.
3. New: `clang -std=c11 -Wall -Wextra -O2 -I source test/host/test_trace_replay.c -o /tmp/tr && /tmp/tr`
   — all fixtures PASS tier S; tier R prints its SKIP line.
4. `tools/verdict.sh netlogs/` (the in-tree run-#9..#12 mixed folder) runs and emits
   UNKNOWNs where features postdate builds — a smoke test of the UNKNOWN discipline.
5. `make` → `3DGBA.3dsx` clean; `git diff source/celiolink.c` is EMPTY;
   `git diff source/celiolink.h` is the one `#define` block; gbacore.c diff = the
   `# fprint` line + `gbacore_game_rev` + the shared exp-default define only.
6. A dated `docs/phase13-diagnostics/BUILDLOG.md` entry per slice (D6, D7a+b, D7c, D7d,
   D7e can land as separate banked increments — Invariant 7: a killed session must lose
   nothing).

---

## Open Questions (implementer must decide / user must rule)

1. **Promote `netProto`/`clProtoRev` mismatch to refuse-at-lobby at first public
   release?** v0 is allow-with-warning per PHASE.md. Once builds diverge in the wild a
   hard gate becomes defensible (gen1's `refused` verdict for engine-major mismatch,
   Handshake.lua:170-189) — but it is NEW behavior next to the frozen path, so it needs
   its own decision + test pass. Tied to the `3dgba-github-release-prep` checklist.
2. **Extend the beacon (`DgbaAdv`) with rev/clProtoRev/hash too?** Would let the joiner
   see incompatibility BEFORE joining (scan-card UX). Risk: older builds read the
   appdata with `sizeof(oldAdv)` buffers (netlink.c:221-222) and libctru's behavior on a
   larger-than-buffer appdata read is unverified — could break old↔new scanning
   entirely. v0 keeps the beacon byte-identical; verify libctru's
   `udsGetNetworkStructApplicationData` truncation semantics before ever touching it.
3. **Re-exchange the fingerprint when KEY_Y changes the link state mid-session?**
   Currently: no (mid-link KEY_Y is forbidden anyway, HANDOFF). If the A-E fallback ever
   becomes a supported pre-link choice in the lobby UI, the fingerprint send must re-arm
   on that change — small, but it changes `modeFlags` from a constant to live state.
4. **Tier-R bless mechanics**: after run #13, WHICH log becomes the golden (a round-0
   HOST log is only guaranteed if the netlog ring hasn't wrapped — 1024 rounds ≈ the
   first ~30-60 s of a session)? Option: raise `NETLOG_N` for diagnostics builds
   (gbacore.c ring) — but that is a memory/perf decision on the frozen-adjacent TU;
   needs its own sign-off. Until then tier R may only ever see short sessions.
5. **Should the mirror assert (D7b) also run inside TEST 14's exit-relay flow?** The
   exit path is two-instance-shaped (CL_EV_EXIT_ROOM relay) but TEST 14 currently drives
   one instance with scripted peer events. Extending it to a true two-instance lagged
   exit is the natural next hardening step — deferred here because the exit path is
   exactly what run #13 must rule on first (don't grow tests around a path that hardware
   may still redesign).
6. **verdict.sh item 13d** cannot distinguish "re-link not attempted" from "re-link
   failed" without user input — acceptable? (Alternative: a `# session` counter line in
   the netlog header counting establishments per app-run — additive, one line in
   gbacore.c, but it is another diff on the hot-adjacent TU.)
7. **Paired event-log capture for future full replay** (D7c.4's "never replayable"
   gap): a flag-gated ClEvent tap writing `# clev DIR type arg len` lines would make
   two-console replay complete. It touches `net_celio_pump` (gbacore.c:614-620) — one
   line, but on the frozen path's pump; deferred to a post-#13 decision.
