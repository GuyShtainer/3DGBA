# State-F mGBA driver-layer audit — does the driver deliver what the game reads?

Scope: `external/mgba/src/gba/sio.c` (490 lines, read in full), `external/mgba/include/mgba/internal/gba/sio.h`
(read in full), state-F parts of `source/gbacore.c` (NetDriver struct, net_start, net_celio_*, net_finishMulti,
net_poll_celio, gbacore_net_attach, net_wSIOCNT/net_wRCNT), `source/celiolink.h` (full) + targeted reads of
`celiolink.c` (cl_next_delay_us, cl_rearm_handshake, cl_reset_session, roomKeys), `main.c` worker loop,
`external/mgba/src/arm/arm.c` (ARMRunLoop), `external/mgba/src/gba/gba.c` (GBARaiseIRQ/GBATestIRQ),
`external/mgba/src/gba/sio/lockstep.c` (comparison driver). All paths below are relative to
`/Users/guyshtainer/VSCodeProjects/3ds-toolkit/projects/3DGBA/`.

Verdict up front: **the register/IRQ/seat plumbing is correct for both roles.** The audit found **no defect in
what SIOMULTI0..3 / SIOCNT id / SI / SD present to the game**. It found (a) one *by-construction cadence fact*
— the JOIN game is fed at most ~0.61 command frames per emulated game frame, ~60% of the real master's
1-per-frame design rate — and (b) two *code-level hazards* on the JOIN clock path (a no-ceiling wedge condition
and a stale-word window), plus several smaller latent items. Details and line proofs below.

---

## 0. How often the driver code even runs (granularity baseline)

`gbacore_net_poll` is called once per emulation *slice*, not per frame — main.c:196-200:

```c
while (e->netLinked && !g_quit) {
    gbacore_set_keys(e->core, (u16)e->keys);
    gbacore_net_poll(e->core);
    gbacore_run_loop(e->core);
```

`gbacore_run_loop` → `core->runLoop` → `_GBACoreRunLoop` (external/mgba/src/gba/core.c:857-859) →
`ARMRunLoop`, which runs only **until the next scheduled timing event** then returns
(external/mgba/src/arm/arm.c:243-255):

```c
void ARMRunLoop(struct ARMCore* cpu) {
    ...
    while (cpu->cycles < cpu->nextEvent) { ThumbStep(cpu); }
    ...
    cpu->irqh.processEvents(cpu);
}
```

GBA timing events (HBlank etc.) fire at ≲1232-cycle intervals, so `net_poll_celio` gets a chance roughly every
~73 µs of emulated time — far finer than any of the pacing gaps. **The poll granularity is NOT a rate
bottleneck**; the cadence below is set purely by the pacing arithmetic. Also note main.c:192-195: in net mode
there is deliberately **no real-time frame cap** — the JOIN core free-runs at whatever wall speed the 3DS
sustains (~40 fps per prior runs).

---

## 1. Slave-side self-clock → completeEvent → what the game reads (Q1)

### 1.1 The inject (JOIN, seat 1) — gbacore.c:909-968 `net_poll_celio`

Reached only via gbacore.c:989-990 (`if (nd->phase != NET_IDLE) return;` then
`if (s_netExp == 5) { net_poll_celio(nd, g); return; }`), so **strictly one transfer in flight**.

Gate (gbacore.c:919): `if (sio->mode != GBA_SIO_MULTI || !(sio->siocnt & 0x4000))` — 0x4000 is SIOCNT bit 14,
`DECL_BIT(GBASIOMultiplayer, Irq, 14)` (sio.h:54). Then pacing (935), edge gate (937-948), capture and inject:

```c
uint16_t w = gba->memory.io[IO_SIOMLT_SEND];     // gbacore.c:950; IO_SIOMLT_SEND = 0x95 = 0x0400012A>>1 (gbacore.c:325)
nd->clMyWord      = w;                            // :953
nd->clPartnerWord = cl_transfer(&nd->cl, w);      // :954  — partner reply computed AT INJECT
sio->siocnt |= 0x80;                              // :957  — Busy set manually (bit 7, sio.h:53)
...
int32_t cyc = GBASIOTransferCycles(GBA_SIO_MULTI, sio->siocnt, nd->peers);   // :963
mTimingSchedule(&gba->timing, &sio->completeEvent, cyc);                      // :965
```

`nd->peers` = "number of OTHER GBAs" = 1 (gbacore.c:59, attach :717). `GBASIOTransferCycles` (sio.c:351-359)
indexes `GBASIOCyclesPerTransfer[baud][connected]`; Gen-3 uses baud 3 (115200 bps), row
`{ 3140, 5755, 8376, 10486 }` (sio.c:18) → **5755 cycles ≈ 343 µs per transfer** — identical to what the
host-side master path computes in `_startTransfer` (sio.c:150-155, `connected = driver->connectedDevices`).

### 1.2 Completion — sio.c `_sioFinish` → `net_finishMulti` → `GBASIOMultiplayerFinishTransfer`

completeEvent fires → `_sioFinish` (sio.c:408-421):

```c
case GBA_SIO_MULTI:
    if (sio->driver && sio->driver->finishMultiplayer) {
        sio->driver->finishMultiplayer(sio->driver, data.multi);   // = net_finishMulti → net_celio_fill
    }
    GBASIOMultiplayerFinishTransfer(sio, data.multi, cyclesLate);
```

`net_finishMulti` (gbacore.c:629-707) with `s_netExp == 5` calls `net_celio_fill` (gbacore.c:639), which for
seat 1 returns the **cached** inject-time words (gbacore.c:613-617):

```c
} else {                                             // SLAVE: words computed post-ISR in net_poll_celio
    data[0] = nd->clPartnerWord;
    data[1] = nd->clMyWord;
}
data[2] = data[3] = 0xFFFF;                          // seats 2/3 absent in a 2-player trade
```

(Correct: it must NOT re-read `io[SIOMLT_SEND]` here — the comment at gbacore.c:626-628 documents that
`_sioFinish` calls the driver *before* SIOMULTI/IRQ, so a late read would be stale.)

Then `GBASIOMultiplayerFinishTransfer` (sio.c:371-389) — the exact writes the game reads:

```c
int id = 0;
if (sio->driver && sio->driver->deviceId) { id = sio->driver->deviceId(sio->driver); }   // :372-375 → net_devId → nd->seat (gbacore.c:478)
sio->p->memory.io[GBA_REG(SIOMULTI0)] = data[0];    // :376  master's word
sio->p->memory.io[GBA_REG(SIOMULTI1)] = data[1];    // :377  our (slave) word echoed
sio->p->memory.io[GBA_REG(SIOMULTI2)] = data[2];    // :378  0xFFFF
sio->p->memory.io[GBA_REG(SIOMULTI3)] = data[3];    // :379  0xFFFF
sio->siocnt = GBASIOMultiplayerClearBusy(sio->siocnt);   // :381  Busy → 0
sio->siocnt = GBASIOMultiplayerSetId(sio->siocnt, id);   // :382  ID bits 4-5 ← 1 (JOIN) → GetMultiplayerId()==1 (player 2) ✓
sio->rcnt = GBASIORegisterRCNTFillSc(sio->rcnt);         // :384
if (GBASIOMultiplayerIsIrq(sio->siocnt)) {               // :386  bit 14 — guaranteed set (our gate required it)
    GBARaiseIRQ(sio->p, GBA_IRQ_SIO, cyclesLate);        // :387  IRQ raised AT COMPLETION (inject + 5755 cyc)
}
```

`GBARaiseIRQ` (gba.c:584-587) sets `io[IF] |= 1<<7` **unconditionally** and `GBATestIRQ` (gba.c:594-599)
schedules `gba->irqEvent` at `GBA_IRQ_DELAY` (7 cycles) **only if `IE & IF`**.

### 1.3 The SI (slave) bit and ID before/at the check — who sets them and from which callbacks

- **SIOCNT bit 2 (SI/Slave) and bits 4-5 (ID)** are set by mGBA on **every SIOCNT write** while the driver
  handles MULTI — sio.c:180-182, using our `deviceId`/`connectedDevices` callbacks:

  ```c
  value &= 0xFF83;
  value = GBASIOMultiplayerSetSlave(value, id || !connected);   // JOIN: id=1 → SI=1; HOST: id=0, connected=1 → SI=0
  value = GBASIOMultiplayerSetId(value, id);                     // JOIN: ID=1; HOST: ID=0
  value |= sio->siocnt & 0x00FC;
  ```

  (`Slave` = bit 2, `Id` = bits 4-5: sio.h:49,51. `net_devId` returns `nd->seat`, `net_devices` returns
  `nd->peers` — gbacore.c:477-478.)

- **RCNT bit 2 (SI)** is set once at the MULTI mode switch — sio.c:57-64 in `_switchMode`:

  ```c
  case GBA_SIO_MULTI:
      if (sio->driver && sio->driver->deviceId) { id = sio->driver->deviceId(sio->driver); }
      sio->rcnt = GBASIORegisterRCNTSetSi(sio->rcnt, !!id);
  ```

- **SD/Ready (SIOCNT bit 3, RCNT bit 1)** come from OUR driver: `net_wSIOCNT` returns
  `GBASIOMultiplayerSetReady(v, 1)` (gbacore.c:490), `net_wRCNT` returns `GBASIORegisterRCNTSetSd(v, 1)`
  (gbacore.c:493), and `net_setMode` pre-seeds both at the mode transition (gbacore.c:463-466) "before its
  next SIOCNT read".

- **ID is re-asserted at every transfer finish** (sio.c:382) from `deviceId`.

**No read gap:** a game *enters* MULTI by writing SIOCNT's mode bits — and in `GBASIOWriteSIOCNT` the
mode-change wipe (`sio->siocnt = value & 0x3000;` sio.c:160) and the Slave/Id computation (sio.c:180-181) and
our `writeSIOCNT` Ready-fill (sio.c:221-222 → gbacore.c:490) all happen **inside that same write** before
`sio->siocnt = value;` (sio.c:238). So from the very first instant the JOIN game is in MULTI it reads
SIOCNT = SI:1, SD:1, ID:1 → any `CheckMasterOrSlave`-style test (`(SIOCNT & (SI|SD)) == SD` ⇒ master) sees
**SLAVE**, and the HOST sees SI:0, SD:1, ID:0 → **MASTER**. ✓ (Matches HW runs: JOIN engaged as slave, host
clocked.)

**Answer Q1: correct.** SIOMULTI0=master word, SIOMULTI1=own word echoed, 2/3=0xFFFF, Busy cleared, ID=1,
SIO IRQ raised at inject+5755 cycles; SI/ID from `deviceId()`+`connectedDevices()` at every SIOCNT write;
Ready/SD from `net_wSIOCNT`/`net_wRCNT`/`net_setMode`.

### 1.4 Footnote — the sticky-status OR in sio.c:182 (not a JOIN bug)

`value |= sio->siocnt & 0x00FC;` ORs the *previous* SIOCNT bits 2-7 into every write (they're read-only
status). Because it's an OR, a previously-set SI bit can never be cleared by the computed `SetSlave(...,0)` on
the host. Two facts defuse it: (1) a mode *change* first wipes bits 0-11 (`sio->siocnt = value & 0x3000;`
sio.c:159-161), so nothing from NORMAL-mode probing (where dummy `GBASIONormalFillSi` sets bit 2, sio.c:228)
survives into MULTI; (2) within MULTI with the driver attached, host SI is computed 0 every time. The only
poisoning path is SIOCNT writes made **in MULTI before `gbacore_net_attach`** (driver NULL ⇒
`SetSlave(value, 0 || !0)` = 1, sio.c:163-180) — i.e. starting wireless *after* the game already sat in the
club. Latent host-side hazard only; the JOIN direction (needs SI=1) cannot be broken by it.

### 1.5 Comparison with the lockstep driver (requested)

Lockstep's `writeSIOCNT` is a pure passthrough (lockstep.c:530-534: `return value;`) — it relies on the same
generic sio.c:180-182 Slave/Id logic, driven by `GBASIOLockstepDriverDeviceId` (lockstep.c:517-528). Ready is
set by `_setReady` only when **all attached players' modes agree** (lockstep.c:849-861:
`sio->siocnt = GBASIOMultiplayerSetReady(sio->siocnt, ready); sio->rcnt = GBASIORegisterRCNTSetSd(...)`), and
player 0 additionally gets `ClearSlave` at SIO_EV_ATTACH (lockstep.c:952-956). Our net driver presents the
same bits but asserts Ready **unconditionally** — deliberate (both seats always "present" in state F) and
strictly more permissive, not less. Same-shaped inject too: lockstep's secondary also does
`siocnt |= 0x80` + `mTimingSchedule(completeEvent)` on SIO_EV_TRANSFER_START (lockstep.c:963-970).

---

## 2. data[0]/data[1] seat mapping — both roles (Q2)

`GBASIOMultiplayerFinishTransfer` maps `data[i]` → `SIOMULTI_i` verbatim (sio.c:376-379). The Gen-3 game at
multi-ID *i* expects its own transmitted word echoed in SIOMULTI*i* and the ID-0 master's word in SIOMULTI0.

- **HOST (seat 0, game = master, partner = CL_SLAVE)** — gbacore.c:610-612:

  ```c
  if (nd->seat == 0) {                                 // MASTER: our game word was captured in net_start
      data[0] = s_netPWord;
      data[1] = cl_transfer(&nd->cl, s_netPWord);      // the synthesized partner reply (local, full-speed)
  ```

  `s_netPWord` is latched in `net_start` at the Busy write — the hardware-correct latch point
  (gbacore.c:502-503: `uint16_t w = gba->memory.io[IO_SIOMLT_SEND]; s_netPWord = w;`), and `net_start` runs
  only for seat 0 (gbacore.c:500 `if (nd->seat != 0) return false;`). Game word → SIOMULTI0 ✓, partner
  (ID 1) → SIOMULTI1 ✓.

- **JOIN (seat 1, game = slave, partner = CL_MASTER)** — gbacore.c:613-616: `data[0] = nd->clPartnerWord`
  (master partner → SIOMULTI0 ✓), `data[1] = nd->clMyWord` (own word echoed → SIOMULTI1 ✓), captured at
  inject (gbacore.c:950-954), not re-read at finish.

Role map (gbacore.c:725-728) matches the memory-verified polarity:
`cl_init(&nd->cl, (seat == 0) ? CL_SLAVE : CL_MASTER, LINKTYPE_TRADE);`

**Answer Q2: correct for both roles.** One semantic nuance, not a bug: `cl_transfer(cl, local_word)` computes
the partner's word for transfer N *from* the game's transfer-N word — on real hardware both units' words are
armed before the clock runs, so the partner has half-a-transfer "prescience." Real Celio has the same property
and the game cannot observe it except through protocol echoes (PC suite covers the framing), so it is benign —
but it means a *stale-captured* game word (see §3.2) is immediately parsed by the partner FSM rather than
merely mirrored.

---

## 3. Edge-strict ISR gate + pacing on the JOIN path (Q3)

### 3.1 The gate — what it proves and when

After a round completes, `net_finishMulti` (seat ≠ 0 branch, gbacore.c:701-706) stamps:

```c
nd->isrWaitRound = nd->pendingRound;        // the ISR about to be armed
nd->irqArmTime   = (uint32_t)mTimingCurrentTime(&gba->timing);
nd->phase        = NET_IDLE;
```

(Note `irqArmTime` is taken **before** `GBASIOMultiplayerFinishTransfer` actually sets IF — sio.c:419 calls
the driver, :421 raises the IRQ — a few cycles of skew, absorbed by the 3000-cycle guard.)

Next inject requires (gbacore.c:937-943):

```c
bool acked   = (gba->memory.io[IO_IF] & SIO_IRQ_BIT) == 0;
bool fired   = !mTimingIsScheduled(&gba->timing, &gba->irqEvent);
bool guarded = elapsed >= NET_ISR_GUARD_CYCLES;               // 3000 cycles (gbacore.c:334)
if (!((acked && fired) && guarded)) return;                    // edge-strict: no ceiling
```

### 3.2 Stale-word window — CONFIRMED (narrow, pre-existing, but sharper in state F)

The gate proves *the ISR ran*, but the true requirement is *the ISR's SIOMLT_SEND write happened*. `guarded`
is anchored to `irqArmTime`, **not to the ack edge**. Gen-3's interrupt dispatcher acks IF at handler *entry*,
then the serial handler writes REG_SIOMLT_SEND some hundreds of cycles later. Normally ack lands well inside
the 3000-cycle guard, so the guard covers the ack→write gap. But when the SIO completion collides with a long
higher-priority handler (the Gen-3 VBlank ISR runs many thousands of cycles) or an IME-off critical section,
the ack occurs at `elapsed >> 3000` — `guarded` is already true, and a poll landing in the ~200-800-cycle
ack→SIOMLT_SEND-write window captures the **previous** word. The word stream then runs one transfer late for
that frame; in state F the CL_MASTER partner *parses* that word same-transfer (§2 nuance), so one collision
can corrupt one command frame (the protocol's CRC/idle handling must absorb it). This exact gate shipped in
states A-D and D completed a trade, so the collision is rare/survivable — but it is the one concrete
mechanism by which "the game's SIOMLT_SEND write is missed" on the JOIN path. Hardening (if ever needed):
latch the first `acked` observation time and require `now - ackTime >= guard` before capture.

Round-0 note: at the listen edge the first captured word is whatever the game pre-seeded in SIOMLT_SEND
(gbacore.c:946-948 "the pre-seeded io[SIOMLT_SEND] is correct"). If the game enables SIOCNT.14 before seeding
0xB9A0 the first word is garbage; the partner's handshake FSM idles until B9A0 (run #3 log shows `w1=B9A0`
arriving), so this is self-healing.

### 3.3 Pacing conversion — sound

gbacore.c:966: `nd->nextClockCyc = nowCyc + (uint32_t)cyc + cl_next_delay_us(&nd->cl) * 17u;   // ~16.78 cycles/us`

True rate is 2^24 Hz = 16.777 cycles/µs; 17 runs the delays **1.33% long** — direction-safe, negligible.
Wrap-safe: the compare is `(int32_t)(nowCyc - nd->nextClockCyc) < 0` (gbacore.c:935). The delay is read
**after** `cl_transfer` advanced the FSM (966 after 954), so it reflects the *next* word's state — matches
`cl_next_delay_us` (celiolink.c:746-753): HANDSHAKE→30097 µs, CRC (= frame boundary, next word is the CRC)
→12953 µs, otherwise 1378 µs (constants celiolink.h:162-164, "in emulated cycles" per the port).

### 3.4 Resulting cadence — THE load-bearing number

Per transfer: 5755 cycles (343 µs) + paced gap. Steady COMMAND-frame cycle = 9 words (CRC + 8 cmd):

- 8 gaps at word pace: 5755 + 1378·17 = 29 181 cyc = **1.739 ms** each
- 1 gap at frame pace (after the 8th cmd word, state→CRC): 5755 + 12953·17 = 225 956 cyc = **13.47 ms**
- Total ≈ 8·1.739 + 13.47 ≈ **27.4 ms of EMULATED time per 9-word command frame ≈ 1.64 emulated frames**
  → **≈ 0.61 command frames per game frame**, vs the real Gen-3 master's design cadence of **1 command frame
  per game frame** (~16.7 ms). Handshake words: 5755 + 30097·17 ≈ 30.8 ms ≈ 1.84 frames/word (safe — slower
  handshaking is tolerated).

Because pacing is in **emulated** cycles, this ratio is invariant to wall speed: at the JOIN's ~40 fps
free-run (main.c:192-195 explicitly uncaps net mode) the wall cadence is ≈ 0.61 × 40 ≈ 24 command frames/s,
but what the game *experiences per its own frame* stays 0.61. Fidelity note: Celio's real dongle with the same
numbers against a real-time GBA yields the same ≈ 28 ms/frame ratio, so the constants themselves are
Celio-faithful — but if the room walk needs closer to 1 frame/frame, `CL_TIMING_FRAME_US` (12 953 µs) is the
dominant term and the single lever (dropping it to ~3-4 ms brings the cycle to ≈ 17 ms ≈ 1/frame).

**Answer Q3:** conversion sound (+1.3% long); one narrow stale-word window (ack-late collision, §3.2); no path
by which a *timely* SIOMLT_SEND write is missed (capture always happens after the proven ISR edge, and the
inject latches exactly once per transfer); steady-state supply ≈ 0.61 command frames per emulated game frame
vs the expected 1.

---

## 4. writeSIOCNT/writeRCNT before the first transfer (Q4)

Covered in §1.3/§1.5. Summary with quotes:

- `net_wSIOCNT` (gbacore.c:483-491): in MULTI, captures baud for diag only and returns
  `GBASIOMultiplayerSetReady(v, 1)` — SD/Ready (bit 3) always 1. Baud deliberately untouched (the baud-force
  broke Gen-3 connection-verify; comment gbacore.c:485-488).
- `net_wRCNT` (gbacore.c:492-494): `GBASIORegisterRCNTSetSd(v, 1)` in MULTI.
- `net_setMode` (gbacore.c:459-466) seeds Ready + RCNT.SD **at the mode transition**, mirroring lockstep's
  `_setReady`, "else the game polls SIOCNT in MULTI, sees Ready==0, and never starts a transfer".
- SI (bit 2) and ID (bits 4-5) are not ours to set — generic sio.c:180-181 computes them from our
  `deviceId()`/`connectedDevices()` on **every** SIOCNT write, inside the same write that enters MULTI, so
  there is **no window** in which the JOIN game can read SI=0/ID=0 while in MULTI mode (post-attach).
- Versus lockstep: identical bit sources; lockstep's Ready is conditional on mode agreement
  (lockstep.c:849-861), ours unconditional — more permissive, correct for a 2-seat link whose partner is
  always present.

**Answer Q4: correct.** A slave game reading SIOCNT before any transfer sees SI=1, SD=1, ID=1 (and the host
SI=0, SD=1, ID=0) from its first MULTI instant.

---

## 5. Driver-level defects that could explain the JOIN room stall (Q5)

Ranked by how well each fits "reaches the room, main loop stalls / input not registering":

### 5.1 CONFIRMED-by-construction: 0.61 command frames per game frame (§3.4)
The slave's overworld advances only on received real frames (project-established). The driver supplies at most
~0.61 per frame in steady state — the room can only ever run at ≈ 60% speed *even with the proactive
heartbeat working perfectly*, and any frame the game classifies RECEIVED_NOTHING compounds on top. This is not
a freeze by itself, but it is the driver-level ceiling, and `CL_TIMING_FRAME_US` is the lever if run #6 shows
sluggish-but-alive movement.

### 5.2 CONFIRMED hazard: the edge gate has NO ceiling — a single unacked SIO IF wedges the JOIN clock forever
gbacore.c:943 (`if (!((acked && fired) && guarded)) return;`) has no `NET_ISR_GUARD_CEIL` escape, unlike the
A-D path (gbacore.c:1074). `GBARaiseIRQ` sets IF **regardless of IE** (gba.c:585) and schedules `irqEvent` only
when `IE & IF` (gba.c:595-597). So if the game has IE.SIO masked at the instant a transfer completes:
IF.SIO=1 sticks, `fired`=true, `acked`=false **forever** → `net_poll_celio` never injects again → total link
silence → a slave whose main loop waits on serial data freezes exactly as observed. Escapes: the game clearing
SIOCNT.14/leaving MULTI (gate at :919-923 resets `isrWaitRound`), the game unmasking IE.SIO (the IE write
retests and delivers the IRQ), or the game writing IF directly. Gen-3 does mask/unmask serial IE around link
re-init; if a completion lands in that window and the re-enable path doesn't dispatch, this is a permanent,
freeze-shaped wedge. **Diagnosable from the run-#6 netlog: if `# celio frames=` stops growing on JOIN while
`sioMode/siocnt` still show MULTI + bit14, this is it.** Fix shape: add a bounded ceiling (like :1074) or also
accept `acked==false && !(IE & (1<<7))` as "ISR can't run — treat armed word as final".

### 5.3 Stale-word window on ack-late collisions (§3.2) — corrupts, doesn't freeze
Same gate as A-D (survived a full D trade), but state F parses the game word same-transfer, so a collision
costs a command frame. Recurring collisions in the room would look like intermittent dropped key frames, not a
hard freeze.

### 5.4 `cl_rearm_handshake` on every not-ready→ready edge (gbacore.c:925-930)
Any transient observation of `mode != MULTI || !(siocnt & 0x4000)` at poll time restarts the partner's
PacketLayer handshake mid-room. `cl_rearm_handshake` (celiolink.c:705-719) preserves section/party/roomKeys
(roomKeys is cleared only in `cl_reset_session`, celiolink.c:727), so it recovers — but the partner answers
handshake words while the game expects commands until the game itself re-handshakes; a game that toggles
SIOCNT around a section boundary could ping-pong here. Frames drop; watch `gateN` in the log — a large gateN
*inside* the room = this path is being hit.

### 5.5 Events only pump during transfers (gbacore.c:603-607, called from fill at :618)
`net_celio_pump` runs only inside `net_celio_fill`, i.e. only when the local game is transferring. While the
JOIN game is gated (walking to the club), peer semantic events (host's LinkPlayer/party) sit in the netlink RX
path. The transport is ACKed + retransmitting + in-order (netlink.c:558-564), so nothing is lost — delivery is
just latency-bounded by transfer cadence. Not a room-freeze cause (transfers flow in the room), but it bounds
how fast SELECT/CONFIRM propagate: worst case one 27 ms command-frame cycle per event hop.

### 5.6 Latent, not implicated in this bug
- `s_netPWord` is a file-scope global read by the host fill (gbacore.c:611); safe while exactly one NetDriver
  per process attaches (wireless flow), unsafe if state F ever runs both seats in-process.
- `net_reset` (gbacore.c:447-455) does not reset `nd->cl`; a game soft-reset leaves the partner FSM stale
  until the ≥2 s gap detector fires (`net_celio_gap_check`, gbacore.c:595-602 → `cl_reset_session`).
- `net_celio_gap_check` runs twice per JOIN transfer (poll :949 and fill :609) — benign double-stamp of
  `lastXferTick`.
- The gate return path (:919-923) skips `gap_check`, so a long gated stretch correctly ripens into
  `cl_reset_session` at the first transfer after it — intended (fresh club conversation).
- If the game clears SIOCNT.14 *during* the 5755-cycle in-flight window, the transfer completes without an IRQ
  (sio.c:386 checks bit 14 at finish); the gate then passes immediately (`IF` never set → `acked` true) — no
  wedge, at worst one stale word.

---

## 6. Direct answers

1. **Q1:** Correct. Inject (gbacore.c:950-966) → completeEvent (+5755 cyc, sio.c:18 via
   GBASIOTransferCycles) → `_sioFinish` (sio.c:417-421) → `net_celio_fill` words → SIOMULTI0..3 written
   (sio.c:376-379), Busy cleared + ID←deviceId()=1 (sio.c:381-382), SIO IRQ raised iff SIOCNT.14
   (sio.c:386-388), which the gate guarantees. SI/ID set from `deviceId()`+`connectedDevices()` at every
   SIOCNT write (sio.c:180-181) and RCNT.SI at the mode switch (sio.c:63); Ready/SD from our
   net_wSIOCNT/net_wRCNT/net_setMode. JOIN reads SLAVE + player-2 ID at all times.
2. **Q2:** Correct both roles (gbacore.c:610-616 vs sio.c:376-379); own word always echoed in the own seat.
3. **Q3:** Conversion sound (17 vs 16.777, +1.3% long, wrap-safe); one narrow ack-late stale-word window
   (§3.2); cadence = 0.61 command frames per emulated game frame (27.4 ms/frame) vs the expected 1/frame;
   wall cadence scales with emu speed but the game-relative ratio is invariant.
4. **Q4:** Correct and lockstep-equivalent; Ready unconditional by design; no SI/ID read gap.
5. **Q5:** Two freeze-shaped driver facts: the **no-ceiling edge-gate wedge** (5.2 — hard freeze, permanent,
   log-diagnosable) and the **0.61×/frame supply ceiling** (5.1 — slow-motion, `CL_TIMING_FRAME_US` is the
   lever). Everything else audited is correct or recoverable.
