# Link cable — in-process lockstep between two mCore instances

**Verified against mGBA master source (2026-06-03).** Two GBA cores can be linked
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

## The concurrency catch (the one thing to prototype first)

mGBA ships **only a thread-based** `mLockstepUser` — `mLockstepThreadUser` whose
`sleep = mCoreThreadWaitFromThread` / `wake = mCoreThreadStopWaiting`, assuming each core
runs inside an `mCoreThread`. **Our skeleton uses raw `threadCreate` workers calling
`runFrame` directly — not `mCoreThread`.** Two ways to reconcile:

- **B1 — adopt mGBA's model.** Run each core via `mCoreThread` + `mLockstepThreadUser` and
  pace them from our coordinator. **Lowest risk** (it's the shipped, proven path) but it
  changes our worker model.
- **B2 — custom `mLockstepUser`.** Implement `sleep()`/`wake()` that bridge to our
  `LightEvent` handshake so a worker hitting a transfer barrier blocks on its own event
  until the coordinator advances the peer. **Fits our skeleton better but is unproven** —
  the primary calls `GBASIOLockstepCoordinatorWaitOnPlayers` expecting secondaries to
  advance and ack, so a single-pass-per-frame scheduler may need multiple coordinator
  turns per transfer.

**Prototype B2 against `src/gba/sio/lockstep.c`** (read
`GBASIOLockstepCoordinatorWaitOnPlayers`, `_advanceCycle`, `_untilNextSync`, `_hardSync`,
`GBASIOLockstepPlayerSleep/Wake`) before committing. If it resists, fall back to B1.

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
