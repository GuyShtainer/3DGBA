# Link cable — in-process lockstep between two mCore instances

**Verified against the pinned mGBA checkout, source-level (2026-06-04).** Two GBA cores can be linked
entirely in-process using mGBA's lockstep SIO — the exact mechanism mGBA's Qt
`MultiplayerController` uses for local multiplayer. This is what makes "two games at
once" worth doing: trade/battle between the two screens.

## The wiring sequence (do it on the coordinator thread)

Set up once, after both cores are created and reset — on the coordinator thread, not in a
worker, so the coordinator owns the shared link state:

1. One coordinator: `static struct GBASIOLockstepCoordinator coord;`
   `GBASIOLockstepCoordinatorInit(&coord);`
2. Per core, a driver + an `mLockstepUser`:
   `GBASIOLockstepDriverCreate(&driverA, &userA);` then
   `GBASIOLockstepCoordinatorAttach(&coord, &driverA);` (same for B).
3. Bind each driver to its core:
   `coreA->setPeripheral(coreA, mPERIPH_GBA_LINK_PORT, &driverA.d);` (same for B).

`mPERIPH_GBA_LINK_PORT == 0x1001` (right after `mPERIPH_GBA_LUMINANCE = 0x1000`); inside
the core that `setPeripheral` case routes to `GBASIOSetDriver(&gba->sio, …)`.
`GBASIOLockstepDriver`'s first member is `struct GBASIODriver d;`, so `&driver->d` is the
valid `GBASIODriver*` `setPeripheral` expects.

- **Player 0 is the primary "runner"** that starts transfers; a transfer is rejected if
  `nAttached < 2` ("no secondary players") or if a non-zero player tries to start one
  ("Secondary player attempted to start transfer"). → **attach both before running.**

## Spike results (2026-06-08): handshake works, free-running B2 races on the data exchange

The B2 spike got **far**: wiring confirmed (`n=2`, player ids `0`/`1`), full speed + responsive
(free-running, 59fps), and a Gen-3 trade handshake **starts and exchanges** ("flashes") — then
the game errors ("Sorry, we have a link error"). A 3-agent source-level audit (see
`docs/` history / commit) pinned the cause, and it is **structural, not a small bug**:

- mGBA's coordinator is built for a **cooperative scheduler**: a MULTI/NORMAL_32 transfer is only
  correct if, between player 0's transfer START and FINISH, player 1's `_lockstepEvent` has run,
  written `multiData[1]`, and `AckPlayer` has set `dataReceived=true` on both. mGBA guarantees
  this by truly suspending player 0 at `WaitOnPlayers`.
- **Free-running breaks two invariants.** (1) Our park is **frame-granular** (worker only checks
  the park flag after `runFrame` returns), but a trade does *many* transfers per frame, so a
  `completeEvent`/`_sioFinish` can fire before `dataReceived` is set → `FinishMultiplayer`/
  `FinishNormal32` return `0xFFFF`/`0xFFFFFFFF` (`lockstep.c:585-587,636-637`) → the partner reads
  garbage → link error. (2) `GBASIOLockstepPlayerWake` is a **no-op unless `player->asleep`**
  (`lockstep.c:1051`); the primary's `WaitOnPlayers` wakes the secondary while it's still
  mid-frame (not yet parked), so that wake is **dropped at the coordinator** before reaching our
  `mLockstepUser` → a transfer slips → bad word. The "flash" is the first few transfers that
  happen to line up; the first slipped one is the error.
- **Azahar can't fairly test this anyway** — it runs the two cores *serially* (time-sliced),
  while B2's whole premise is the two cores running *in parallel* on New-3DS cores 0+2. So Azahar
  is the worst bed for the link, and a green/red verdict here doesn't transfer to hardware.

**Verdict: free-running B2 cannot reliably complete a transfer** because it races mGBA's
cooperative sleep/wake. Two ways forward:

1. **B1 — `mCoreThread` + `mLockstepThreadUser` (recommended for a working link).** This is mGBA's
   *shipped, proven* model: each core runs in an `mCoreThread` whose run loop cooperatively honors
   sleep/wake exactly as the coordinator expects — scheduler-agnostic, so it should work under
   Azahar's time-slicing *and* on hardware. Cost: a real rewrite of our worker/render/audio model
   (mCoreThread owns the thread + frame callback; we lose the manual go/done handshake and likely
   explicit core-2 affinity), touching code that currently works for v0.6/v0.7. Big, but correct.
2. **Transfer-level barrier on top of B2.** Keep pinned workers but, while `coordinator->transferActive`,
   stop free-running and let the coordinator fully sequence the ack round (both park, resume between
   transfers). Smaller than B1 but still intricate and unproven; partially re-introduces the barrier.

The committed spike (free-running) stays as the responsive baseline (main is decoupled, so a
dropped wake never hangs the app — you can always toggle Link off). Completing a real trade is a
**hardware-final** milestone: validate B1 (or the barrier) on a real New 3DS, where the cores are
genuinely parallel, before calling the link done.

## The concurrency mechanism (source-verified 2026-06-04 — this is the crux)

mGBA ships **only a thread-based** `mLockstepUser` — `mLockstepThreadUser` whose
`sleep = mCoreThreadWaitFromThread` / `wake = mCoreThreadStopWaiting`, assuming each core
runs inside an `mCoreThread`. **Our skeleton uses raw `threadCreate` workers calling
`runFrame` directly.** The naïve fear was a deadlock: `GBASIOLockstepPlayerSleep` calls
`user->sleep()` **while the coordinator `Mutex` is held** (`sio/lockstep.c:907` lock →
`:1009` sleep → `:1012` unlock). If `sleep()` *blocked there*, the peer worker could never
take the mutex to reach `wake()`.

**It doesn't block there — `sleep` is cooperative.** `GBASIOLockstepPlayerSleep`
(`:1084`) does:
```c
player->driver->user->sleep(player->driver->user);
player->driver->d.p->p->cpu->nextEvent = 0;   // force the CPU to break out…
GBAInterrupt(player->driver->d.p->p);          // …of runFrame, right now
```
i.e. it asks the user to *arrange to wait* and then makes **`runFrame` return early**
(mid-video-frame). The real blocking is meant to happen in the thread's **outer loop, after
the mutex is released** — which is exactly what `mCoreThread` does. `wake()` only needs to
*signal*; it never blocks. So a correct `mLockstepUser` must **not** block inside `sleep()`
— it sets a flag; the worker blocks after `runFrame` returns.

This reshapes the decision — both options are viable; **B2 now looks preferable** because it
keeps our pinned, truly-parallel cores (the basis of the v0.4 GREEN result):

- **B1 — adopt `mCoreThread` + `mLockstepThreadUser`.** Proven/shipped. But `mCoreThread`
  owns thread creation (CPU-affinity/core-2 pinning is ours to lose), runs the core free
  with its own sync + frame callback, and supplants our per-frame `LightEvent` handshake and
  frameskip control. Big rewrite of the worker/render model. **Fallback.**
- **B2 — custom `mLockstepUser` + a run-until-frame loop in our workers (recommended).**
  Keep the two pinned workers. `sleep()` just sets a per-core `wantWait` flag (the
  coordinator already forces the early `runFrame` return); `wake()` `LightEvent_Signal`s that
  core's wait event. Deadlock-safe because `sleep()` never blocks under the mutex and
  `wake()` only signals. The one real change: a worker must call `runFrame` **repeatedly**
  until its video frame completes, blocking on the wait event whenever `wantWait` is set —
  because a single `runFrame` no longer equals one frame once transfers interrupt it.

## B2 implementation outline (the spike)

Per-core state (in `gbacore`, or a small `linkcore` beside it): `LightEvent waitEv;`
`volatile bool wantWait;` plus the `GBASIOLockstepDriver` and an `mLockstepUser` whose
`sleep`/`wake` are:
```c
static void link_sleep(struct mLockstepUser* u){ Node* n=(Node*)u; n->wantWait = true; }      // no block!
static void link_wake (struct mLockstepUser* u){ Node* n=(Node*)u; LightEvent_Signal(&n->waitEv); }
```
Worker loop changes from "one `runFrame` per `go`" to **run-until-frame-complete**:
```c
LightEvent_Wait(&e->go);
uint32_t f0 = core->frameCounter(core);          // core.h:116
do {
    core->runFrame(core);                        // may return early when a transfer interrupts
    if (e->node.wantWait) { e->node.wantWait=false; LightEvent_Wait(&e->node.waitEv); }
} while (core->frameCounter(core) == f0 && !g_quit);
LightEvent_Signal(&e->done);
```
Use a **latching** event (`RESET_ONESHOT`) so a `wake` that arrives before the `Wait` isn't
lost. Both workers run concurrently on cores 0 and 2, so while A is parked on `waitEv`, B is
genuinely advancing and will drive the coordinator to `wake` A. Setup (coordinator init, two
`GBASIOLockstepDriverCreate` + `…CoordinatorAttach` + `setPeripheral`) happens **once on the
main thread after both ROMs load**, before the workers start stepping.

**Spike acceptance:** a Pokémon Gen-3 trade *or* a 2-player battle completes across the two
screens, on real New 3DS hardware, with no desync/hang. Read
`GBASIOLockstepCoordinatorWaitOnPlayers`, `_advanceCycle`, `_untilNextSync`, `_hardSync`,
`GBASIOLockstepPlayer{Sleep,Wake}` in `sio/lockstep.c` while building it. If B2's
frame-completion/wait edges resist a timebox, fall back to B1.

## Cadence vs. frameskip — they fight each other

While linked, both cores must stay within **`LOCKSTEP_INTERVAL = 4096` cycles** of each
other during a transfer (`HARD_SYNC_INTERVAL = 0x80000`). So you **cannot let the focused
game run ahead of a frameskipped unfocused game mid-transfer** — the main performance
mitigation (frameskip the unfocused core) directly tensions the link requirement.
Source shows "MULTI did not receive data. Are we running behind?" warnings → limited
drift tolerance. **Characterize empirically with a real link title** (Pokémon trade/battle
or a multiboot link demo).

## API-version warning

This wiring is the **master** Coordinator/Driver API. The older
`GBASIOLockstepNode`/`GBASIOLockstepInit`/`NodeCreate`/`AttachNode` API was **removed for
GBA** pre-0.11 (it survives only for Game Boy). If the build pins an older tag the link
wiring differs entirely — **pin a known master commit and record it** (cited line numbers
drift).

## References (mGBA master, fetched 2026-06-03)

- `include/mgba/internal/gba/sio/lockstep.h` — `GBASIOLockstepCoordinator`,
  `GBASIOLockstepDriver`, `…Init/Attach/Detach/Attached`, `GBASIOLockstepDriverCreate`.
- `src/gba/sio/lockstep.c` — `DRIVER_ID 0x6B636F4C` ('Lock'), `LOCKSTEP_INTERVAL 4096`,
  `HARD_SYNC_INTERVAL 0x80000`; the primary/secondary start checks; `user->sleep/wake`.
- `include/mgba/core/lockstep.h` — `struct mLockstepUser {sleep,wake,requestedId,playerIdChanged}`;
  `mLockstepThreadUser` + `mLockstepThreadUserInit` (the only shipped, thread-based user).
- `include/mgba/gba/interface.h` — `mPERIPH_GBA_LINK_PORT` (= 0x1001).
- `src/platform/qt/MultiplayerController.cpp` — working in-process reference (verbatim:
  `GBASIOLockstepCoordinatorInit` → `GBASIOLockstepDriverCreate` →
  `GBASIOLockstepCoordinatorAttach` → `setPeripheral(core, mPERIPH_GBA_LINK_PORT, …)`).

Core lifecycle belongs to [mgba-integration.md](mgba-integration.md); audio to
[audio-mixer.md](audio-mixer.md); CPU/thread placement to the toolkit `n3ds-systems` agent.
