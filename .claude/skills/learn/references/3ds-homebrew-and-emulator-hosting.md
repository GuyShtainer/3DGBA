# 3DS homebrew, the PICA200, and hosting an emulator core (live-RAM game control)

Hard-won, portable knowledge for Nintendo 3DS homebrew (libctru + citro2d/citro3d), and specifically
for **running an emulator core inside a 3DS app and driving/reading the guest game's live RAM**
(e.g. embedding mGBA to run GBA games). Concrete, reusable across any such 3DS tool.

## ARM11 cores, clocks, threads
- **Only 3 usable CPU lanes for a title.** Core 0 = main thread (+ one worker). Core 2 = a full extra
  core, but only after the `.cia` exheader grants `CanAccessCore2` (a `.3dsx` from the Homebrew Launcher
  can't reliably claim it → ship a `.cia` for multi-core work). Core 1 = a *slice* of the syscore you
  must request with `APT_SetAppCpuTimeLimit(N)` (good for an audio/IO thread). **Core 3 is OS-reserved —
  never available.**
- New 3DS only: call `osSetSpeedupEnable(true)` for 804 MHz + L2 (exheader must grant `804MHz`/`L2`).
  Self-detect the clock by timing a busy-loop before/after enabling speedup.
- `svcGetSystemTick()` runs at a **fixed** ~268 MHz regardless of CPU clock — so tick-based worst-frame
  timing stays correct even when the clock changes. Pace a real-time frame cap off it.

## PICA200 GPU realities (these bound every visual effect)
- **No programmable fragment/pixel shader.** You get: a programmable **vertex** (and geometry) shader,
  **6 fixed TEV combiner stages** (multiply/add/interpolate/subtract/dot3…), a fixed **fragment-lighting**
  unit with **hardware tangent-space normal mapping** (`GPU_BUMP_AS_TANG`), and a **fog/depth LUT**.
  Any "post-process shader" must be expressed as **multi-pass quad draws + TEV/fog/light** — not GLSL.
  A cheap blur = render to an offscreen target and **LINEAR down/up-sample**.
- The **top screen framebuffer is rotated**: 240×400 physical, 400×240 logical. citro2d draws in logical
  space, but `C3D_SetScissor` is in **rotated/physical** coords — the classic gotcha. A per-vertex-alpha
  gradient blend can avoid scissor entirely.
- Textures want **tiled, power-of-two**; back a 240×160 guest frame with a 256×256 texture (linear→tiled
  `GX_DisplayTransfer`). **One render thread owns the GPU**; do all GPU work between `C3D_FrameBegin/End`.
- Stereoscopic 3D: `gfxSet3D(true)` + a second `C2D_CreateScreenTarget(GFX_TOP, GFX_RIGHT)`; scale
  disparity by `osGet3DSliderState()`. **Crossed disparity (left-eye image shifted right, right-eye
  left) pops OUT toward the viewer**; the opposite recedes behind the screen.

## Hosting an emulator core + faux-3D from a 2D guest
- You generally only get the **already-composited frame** from the core — there is usually **no
  "re-rasterize this frame with a layer mask" hook** (mGBA rasterizes only as the PPU advances). So any
  *true* layer separation costs a **full extra emulation pass**, which is unaffordable once cores are
  saturated. Result: real volumetric 3D from a 2D game is a **budget wall, not an effort wall**.
- The feasible win is **2.5D**: overdraw shifted sub-rects of the single composited frame per eye —
  characters (from the guest's OAM/sprite list) and foreground scenery (from tile semantics). Convincing
  pop, with a faint edge "ghost" (keep disparity small). Depth-of-field / tilt-shift (blur graded by a
  coarse depth hint) + LDR bloom are the cheapest high-impact "diorama" cues.
- **AI depth vs normals for pixel art:** offline **monocular depth** (MiDaS/Depth-Anything) is *garbage*
  on 16-px tiles (no perspective cues; patch size ≈ whole sprite) and can't run at runtime on ARM11 — and
  an analytic per-tile depth from the game's own data is better *and* dynamic. Offline-baked **normal**
  maps, however, are robust on pixel art and the PICA200 lights them in hardware — the viable AI angle is
  normals-for-lighting, not depth.

## Reading/driving a running guest's RAM (game-aware control)
- **Detect on-screen context by scanning the guest's TASK list for the menu's active input-handler — it
  is FAIL-SAFE.** A wrong/absent address then just means "not detected" → safe default (e.g. keep
  walking). Heuristics that read a flag/pointer **fail UNSAFE**: many engine flags are **set but not
  cleared** (a "menu callback" left stale after the menu closes; a "battle type" flag zeroed at battle
  *setup*, not end → the field reads as a battle afterward). Prefer a **positive, self-clearing** signal
  (an active task, or the live `callback2`) over any lingering flag.
- **Deterministic menu selection: WRITE the game's own cursor variable, then pulse the confirm button
  the next frame.** Far more robust than reading the cursor back (read latency / index ambiguity).
- **Race rule:** reads/writes of a core a worker thread owns must happen at the **parked per-frame
  handshake**, never while the core free-runs (e.g. during a link).
- Verify every guest RAM address against **byte-matched symbol maps** of the exact build. Same-engine
  siblings (game variants) share the **RAM layout** but **ROM-function addresses differ per build** —
  treat derived sibling addresses as unverified, and make detection fail-safe so they no-op cleanly.
- Reusable guest-RAM facts worth caching per map: a tile-grid word often packs id + collision +
  elevation; an object-event array holds live NPC tile coords (route a pathfinder *around* them, but let
  the goal tile stay enterable so "tap an NPC" still interacts); the overworld camera typically centers
  the player at a fixed on-screen tile (makes tile↔screen math trivial).

## Embedding mGBA / linking guests
- **mGBA is MPL-2.0 = file-level copyleft.** Don't modify mGBA's own files; extend through its
  abstractions. A wireless/network link is best done as a **custom `GBASIODriver` peer** (its `start()`
  returning `false` hands *you* transfer completion → park the core for the network round-trip), **not**
  a fork of the lockstep code.
- The in-process two-core link is mGBA's `GBASIOLockstepCoordinator`: it parks/wakes each core at every
  SIO transfer. Drive it with fine-grained `core->runLoop` slices (not `runFrame`) so `earlyExit` returns
  a worker exactly at the transfer point.
- A **trade/battle is a link state-machine, not a callable function** — you **cannot** force-start one by
  poking `callback2`/RAM (near-certain crash). To trigger interactions, route through the game's **own**
  link UI (e.g. its lobby/cable-club specials) over the established link.

## Wireless link (UDS) over an emulated cable — the latency wall + diagnosing the guest
- **A synchronous link emulated over UDS is round-trip-LATENCY bound, not bandwidth.** A GBA MULTI link does
  ~9 SIO transfers per emulated VBlank, and each transfer is **one synchronous network round-trip** (the master
  clocks, the slave must reply before the round completes). The payload is tiny (~1 KB/s), but at one UDS
  round-trip per 16-bit word the guest runs at **emulated_fps ≈ (round-trips/sec) / 9**. Measured: a Gen-3
  cable-club trade *completes* but at **~4–5 fps**. Never bandwidth; it's **round-trip count × per-round latency**.
- **Split the round-trip before optimizing.** A pure ping (no guest in the loop) measures the radio; the
  per-round trade time conflates radio + your software turnaround. Measured: the UDS ping floor is **~5 ms but
  highly variable (5–25 ms) and lossy, RF-dependent**, while the host+slave per-round software turnaround was
  only **~2 ms**. So the wall is the **radio**, not your worker loop — tightening the loop buys ~nothing; the
  lever is the transport/channel. (Build a tiny standalone ping-flood bench to A/B transports without the
  emulator — the cleanest way to get the radio-only number.)
- **`udsCreateNetwork` defaults to wifi channel 0 = system auto-select.** You can PIN a 2.4 GHz channel (1/6/11)
  via `udsNetworkStruct.channel` *after* `udsGenerateDefaultNetworkStruct` (which `memset`s the struct, so the
  default is a clean 0 — **verify such library internals in the actual libctru source; don't trust a claim that
  a field is left uninitialized**). Worth A/B-ing: an auto channel can land on a congested one (~32 ms p50 /
  ~15 % loss vs a ~5 ms floor).
- **Pace the slave to the HOST's emulated rate.** Too slow / frozen → the slave can't run its main-loop link
  code in time and sends **stale words** → the master's running **checksum fails** (a CHECKSUM link error with
  byte-perfect transport == *desync*, not packet loss). Too fast / free-run → the slave's clock outruns the
  master → divergence / a watchdog trips. Sweet spot: the slave tracks the host's frame rate (advance ~1 frame
  per N adopted rounds, N = transfers/frame) — and **re-base that estimate at the start of each active burst**,
  because the guest free-runs frames-without-rounds during menus/navigation (a stale baseline then freezes it).
- **Capture the slave's outgoing SIO word LAST**, only after its main loop produced it. Gen-3 writes
  `SIOMLT_SEND` in a main-loop/VBlank link task *after* the serial ISR returns — so any "pre-send to save a hop"
  samples a stale previous-round word and poisons the stream. Sample at the proven post-ISR point, never earlier.
- **Diagnose *why the guest aborts a link* from the guest's OWN link-error state, not just your transport
  counters.** Gen-3 (verified pret symbols): `gLinkErrorOccurred` (bool), `gLinkStatus` (u32), `sLinkErrorBuffer`
  (latches the status + send/recv queue counts at error). Decode `(gLinkStatus & 0x7F000) >> 12`: HW / CHECKSUM /
  QUEUE_FULL / LAG_MASTER / INVALID_ID / LAG_SLAVE. The error screen's `callback2` (e.g. `CB2_PrintErrorMessage`)
  is a persistent terminal value seen only at a failed link's end — it doubles as a "the guest gave up" marker.
- **`RECEIVED_NOTHING` is a distinct failure from CHECKSUM/LAG — don't conflate them.** If the relay feeds the
  guest whole idle/all-zero commands (because the peer's real words weren't ready in time), the master keeps
  reading "nothing" → `gRemoteLinkPlayersNotReceived` → `CB2_LinkError`, with **`gLinkStatus & 0x7F000 == 0`**
  (no error bit set). That zero is the tell: it is NOT checksum, NOT lag, NOT queue-full — the fix is upstream
  (supply real frames / answer the protocol locally), not in the checksum path.
- **The real cure for an emulated-cable link is LOCAL TERMINATION, not transport tuning.** Relaying one raw SIO
  word per network round-trip is inherently latency-bound; instead make the driver *be* the link partner —
  answer every SIO word locally at full speed and ship only the **semantic commands** over the wire (the
  open-source Celio-Link model). A channel/transport swap only buys a constant factor; local termination removes
  the per-word round-trip entirely. **For the exact Gen-3 wire format this requires** (single-word handshake,
  `[CRC][8 cmd]` 9-word frames, the additive CRC, `LINKCMD_*`/`LINKTYPE_*`, the whole-idle-frame rule, the trade
  flow), see `references/gen3-link-protocol.md`.
- **Log a FROZEN/paced guest off a WALL-CLOCK heartbeat, not the emulated frame.** A paused/heavily-paced core's
  emulated frame counter stops, so an emulated-frame log heartbeat goes silent exactly at the stuck moment you
  need. Drive liveness off `svcGetSystemTick`; a frozen frame counter is itself the "core is paused/hung" tell.

## Text-console (`consoleInit`) gotcha
- A `consoleInit` text screen that you **clear + reprint every frame** flickers ("black stain over the text")
  because `consoleInit` binds the console to **one** framebuffer (it never follows a swap), while gfx
  double-buffering alternates the *displayed* buffer → text-buffer, stale-buffer, text-buffer… Fix:
  **`gfxSetDoubleBuffering(GFX_TOP, false)` before `consoleInit`** (verified: `gfxScreenSwapBuffers` does
  `gfxCurBuf ^= gfxIsDoubleBuf`, so off = no-op → the single console buffer is always shown). Keep `printf` +
  `gfxFlushBuffers` + `gspWaitForVBlank`.

## The 3DS hardware-validation gate
Azahar/Citra do **not** model: core-2 contention, the 804 MHz/L2 budget, ndsp audio latency, true frame
pacing, `uds` wireless latency, or stereoscopic 3D fusion/comfort. So anything threading-, frame-timing-,
audio-sync-, link-, wireless-, or 3D-related is **"not done" until it runs on a real New 3DS**. ndsp is
also **silent without DSP firmware** (`dspfirm.cdc` dumped to the SD).
