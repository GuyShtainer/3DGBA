# State-E "bridge" failure signature — 2026-06-28 hardware runs (PRESERVED before log deletion)

Source logs (sdmc:/cias/netlogs, matched pairs, gs read before net):
- Run A: `3DGBA_gs_HOST_0628_182418.txt` / `3DGBA_net_HOST_0628_182418.txt`
         `3DGBA_gs_JOIN_0628_182225.txt` / `3DGBA_net_JOIN_0628_182225.txt`
- Run B (more advanced, the real signature): `3DGBA_gs_HOST_0628_190938.txt` / `3DGBA_net_HOST_0628_190938.txt`
         `3DGBA_gs_JOIN_0628_191039.txt` / `3DGBA_net_JOIN_0628_191039.txt`

Both runs are the **KEY_Y state-E per-word bridge** (`exp=E` in every net row). Per-word relay over UDS:
each round carries one 16-bit SIO word per seat; `sub=1` means the peer's word was idle-substituted
(STATE-E filled in a default instead of a real peer word).

---

## (1) HOW FAR THE LINK GOT

**Run A (1824/1822) — stuck at the single-word HANDSHAKE, never entered the command stream.**
- Net words that ever crossed the wire are ONLY the handshake `B9A0` (seat0 on HOST, seat1 on JOIN) plus `0000` idle.
  HOST net distinct tuples: `B9A0,0000` x16, `0000,0000` x1, `FFFF,FFFF ok=0` x1 (final timeout). JOIN: `0000,B9A0` x19, `0000,0000` x2.
- **No 9-word `[CRC][8 cmd]` frame, no party payload — ever.** `realCmds=0`, `idleCmds=3` on both sides (bridge header).
- gs side: lstat only ever = `0x0`, `0x1`, `0x20`. Reached the trade/link-room callback `cb2=08056548` and idled there.
- This is the handshake-only floor: the two GBAs exchanged the `B9A0`/`8FFF` handshake region but the bridge
  never advanced to shipping real command frames.

**Run B (1909/1910) — got MUCH further: crossed real command words and reached the `LinkPlayer` block, then failed.**
- `realCmds=293` (HOST) / `173` (JOIN) — real (non-idle) command words DID cross the wire.
- Peak real words seen on the wire (net header `peakSent*/peakRx*` + tail rows): `B9A0` handshake, then
  `8FFF` (round 352), and at the very end genuine multi-byte command/party-ish words:
  `2222`, `1133` (HOST rounds 363-364), `3355`, `BBBB`, `003C`, `0081` (HOST rounds 371-374; net header `peakRxC=0081`, `peakSentP=3355`).
- gs side reached **`gLinkStatus=0x368 = CONN_ESTABLISHED(0x40) | RECEIVED_NOTHING(0x100) | 0x228 low bits`** at frames ~2230-2398,
  i.e. the connection was ESTABLISHED and the game entered the `LinkPlayer`/exchange block (cb2=`08085E04`, the FR link-room callback).
- So Run B = handshake PASSED → command frames flowing → `LinkPlayer` block entered → **then errored**.
  This matches the prior diagnosis "reaches the LinkPlayer block then errors."

---

## (2) THE PRECISE ERROR — decoded lstat / lbuf / lnotrecv

Bit masks (from gs header): ERRORS=0x7F000 (HW 0x1000 / CHECKSUM 0x2000 / QUEUE_FULL 0x4000 /
LAG_MASTER 0x10000 / INVALID_ID 0x20000 / LAG_SLAVE 0x40000), RECEIVED_NOTHING 0x100, CONN_ESTABLISHED 0x40.

**Every distinct gLinkStatus value across all four files has `gLinkStatus & 0x7F000 == 0`. CONFIRMED.**
Distinct lstat seen:
- `0x0`, `0x1`, `0x20`, `0x209`, `0x228`, `0x368`.
- Decode: `0x368` = CONN_ESTABLISHED | RECEIVED_NOTHING | 0x28; `0x228` = RECEIVED_NOTHING | 0x28; `0x209` = RECEIVED_NOTHING | 0x9.
- **The only error-class bit ever set is RECEIVED_NOTHING (0x100). NO checksum, NO lag, NO HW, NO queue-full, NO invalid-id bit is EVER set, in either run.**

**Latched error buffer is EMPTY at failure:** in every row `lbuf0 = 00000000` and `lbuf1 = 00000000`.
`sLinkErrorBuffer` (latched at error time) is all zeros → the game's link layer did NOT latch a hardware/protocol
error word. There is no checksum mismatch, no queue overflow, no disconnect recorded.

**The actual failure flag (Run B, both consoles, identical):**
```
idx=71  frame=2401  lerr=1  lstat=00000000  lbuf0=0  lbuf1=0  lnotrecv=01010101   (HOST 190938)
idx=71  frame=2401  lerr=1  lstat=00000000  lbuf0=0  lbuf1=0  lnotrecv=01010101   (JOIN 191039)
```
- `gLinkErrorOccurred (lerr) = 1` is raised.
- `gLinkStatus = 0` at the moment of error (`&0x7F000 == 0`).
- `gRemoteLinkPlayersNotReceived (lnotrecv) = 0x01010101` — **all four player slots flag "not received."**
  i.e. the game expected to receive each remote player's data block and got NONE for any slot.

**Conclusion: the failure is `gRemoteLinkPlayersNotReceived → CB2_LinkError`, NOT a checksum/CRC/LAG fault.**
`gLinkStatus & 0x7F000 == 0` is the smoking gun. The two earlier diagnoses (checksum mismatch; LAG) are WRONG.
The game aborts because the *higher-level player-data exchange* never delivered each peer's `LinkPlayer` block,
not because any low-level SIO word was malformed.

Run A shows the precursor: it never even left handshake, and there too `lnotrecv=01010101` once it sat in the
link room (idx 198+) with lstat oscillating 0/1/0x20 — same "received nothing from anyone" state, one phase earlier.

---

## (3) WHERE IDLE SUBSTITUTION (sub=1) HAPPENED RELATIVE TO THE FAILURE

- **Run A: `sub=1` on EVERY net round, from round 0 to the end, on both consoles.** The per-word relay was
  idle-substituting the peer word the entire time — it never carried a true peer word, consistent with "stuck at handshake."
- **Run B: `sub` is mostly 0 (real peer words crossing) but flips to `sub=1` intermittently throughout**, e.g. HOST rounds
  2, 9, 24, 27, 29, 40-41, 45-46, 51-53, 63-65, 70, 81, 86-87, 90-92, 94-95, ... and densely in the final
  command-frame tail (rounds 361, 366-371). I.e. **whenever a real command frame should have been mid-flight, the
  bridge dropped a peer word and substituted idle.** The `w1` column on HOST repeatedly reads `0000` (idle) where the
  joiner's real reply word belonged (e.g. rounds 9, 24, 27, 29 ...: `B9A0,0000 sub=1`).
- The failure (`lerr=1` at frame 2401) lands **right in the window where real command words start flowing AND idle
  substitution is interleaving with them** (HOST tail rounds 352-374 carry `8FFF`,`2222`,`1133`,`3355`,`BBBB`,`003C`,`0081`
  mixed with `sub=1` idle rounds). So idle substitution is co-located with — and immediately precedes — the abort.

The HOST-vs-JOIN `sub` columns also DISAGREE per round (the header calls this "the prime desync suspect"): the two
consoles do not idle-substitute on the same rounds, so each side sees a *different* word sequence for the same logical
exchange. That asymmetry is exactly what corrupts the per-player block reassembly.

---

## (4) WHAT THIS PROVES — why per-word relay fails, why local termination fixes it

**Why the per-word relay fails:**
The Gen-3 `LinkPlayer` exchange is a *framed, lock-stepped, position-sensitive* protocol: each console must emit a
specific 8-word command frame (`[CRC][8 cmd]`) in lock-step, and every word's *position in the stream* is significant
(the receiver reassembles per-player blocks by word index). The state-E bridge relays SIO words one at a time over a
high-latency, lossy UDS link (per-round RTT ~19-24 ms; `drops` 6-26 per run). Whenever a peer word isn't ready in time,
the bridge **substitutes an idle word (`sub=1`) to keep the local SIO from stalling.** That idle injection:
1. desynchronizes the two streams (HOST and JOIN substitute on *different* rounds — their `sub` columns differ), and
2. inserts wrong words at significant positions, so neither game ever assembles a complete, correctly-ordered
   `LinkPlayer` block for any remote slot.
The result is **not** a detectable low-level error (no checksum/queue/lag bit, `lbuf=0`) — the words are individually
well-formed, they're just the *wrong words in the wrong slots*. The game's only symptom is "I expected each remote
player's block and received none" → `gRemoteLinkPlayersNotReceived = 0x01010101` → `gLinkErrorOccurred=1` →
`CB2_LinkError`. The bridge can keep the handshake alive (Run A) and can even push real command frames for a while
(Run B), but it can never deliver a coherent, lock-stepped player-data exchange across the latency.

**Why local termination (Celio) fixes it:**
With local termination, each console's emulated game does NOT wait on the wire for the peer's raw SIO words. The dongle
**answers SIO locally** — it terminates the Gen-3 link state machine on-device and feeds the local game the exact
8-word command frames it expects, in lock-step, with no idle substitution. Only *semantic* commands / completed
player-data blocks cross the network (latency-tolerant, position-independent at the SIO layer), instead of raw,
position-sensitive, latency-fragile per-word SIO traffic. That removes both failure causes at once:
- no idle-word substitution (the local SIO is always answered on time → `sub` desync impossible), and
- the lock-step framing is reconstructed locally, so `gRemoteLinkPlayersNotReceived` can actually go to 0 because each
  side hands the game a complete, correctly-ordered remote `LinkPlayer` block.

This is precisely why the fix is "port Celio LOCAL TERMINATION (answer SIO locally, ship 8-word commands)" rather than
trying to harden the per-word relay — the per-word relay is structurally incapable of meeting Gen-3 lock-step timing
over UDS, and the logs prove the failure is timing/position desync (RECEIVED_NOTHING), not data corruption (no checksum/lag bit).
