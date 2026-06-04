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
