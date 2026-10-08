# 3DGBA — status & roadmap

An honest snapshot of what's solid, what's half-baked, and what's still a dream. This is a
solo fan project, so **everything is experimental**, and my free time sets the pace of both
development and hardware testing. The bar for "done" is *runs on a real New 3DS*, not
*compiles* or *works in an emulator*.

**What this project is:** a general GBA emulator whose *heart* is the **Pokémon**
experience. The emulator and the emulated link cable work with **any** GBA game; the
**touch** and **3D** enhancements are **Gen-3-Pokémon-specific** and do nothing in other
games (Sonic, Kirby, etc.).

**Legend:** ✅ works on real hardware · 🟡 built & experimental, actively being tuned on-device
· 🔜 in progress / next up · 🔮 planned / aspirational (designed, not built)

---

## New since the last push (2026-06-15 → 2026-10-08)

> **Most of what's below has NOT been tested on a real 3DS yet.** It builds, passes the
> PC test suites, and much of it was checked in the Azahar emulator, but Azahar doesn't
> prove timing, touch feel, wireless, or 3D on real hardware. Each item says where it stands.

| What | Hardware status |
|---|---|
| **Wireless trade between two consoles** (cable-club trade over local wireless, via Celio-style local link termination) | ✅ A trade completed and saved on both consoles (2026-07-06). ⚠️ Known bug: the joining console can black-screen when leaving the trade room afterwards. A fix is built but **not hardware-tested**. Wireless **battles are not supported** yet. |
| **New UI** (device-native fonts and art, all screens), plus later fix passes for blur, text size, touch drag-scroll and d-pad navigation | 🟡 The first redesign and the early fix passes were tried on hardware. The latest passes (bigger text, the phase-17–19 fixes) are emulator-checked only, **not hardware-tested**. |
| **Touch "smart pointer" fixes** (tap-vs-drag thresholds and other fixes after the first hardware test failed in six ways) | ❌ Four of the six defects are fixed, but **none of the fixes is hardware-tested**. Touch events are logged to the SD card for the next test. |
| **Diagnostics layer** (crash dumps, on-device logs, game-state logger) | ❌ **Not hardware-tested** |
| **Co-op presence** (see the other game's player walking on your map; same console only) | ❌ **Not hardware-tested** |
| **Voxel 3D overworld** for FireRed/LeafGreen, Emerald and Ruby/Sapphire: buildings generated on the device from your own ROM (no game art is shipped), Hoenn landmark models, stereo 3D, depth of field and bloom | ❌ **Not hardware-tested.** Checked in Azahar only. Known visual bugs are listed in `docs/phase36-hoenn/LOOK-BUGS.md`. Builds from this repo draw no trees: the tree art is the game's own, so it isn't shipped. |
| **Removed:** the older 2.5D depth-pop, tilt and HD-2D post effects (replaced by the voxel world) | — see `docs/REMOVED-3D-ATTEMPTS.md` |
| **Emulator self-test harness** (`tools/emutest/`, drives Azahar for automated checks) | Dev tooling only, runs on a Mac |

---

## ✅ Working — verified on a real New 3DS

- **Two GBA games at once**, one per screen — each a full [mGBA](https://mgba.io/) core
  pinned to its own ARM11 CPU core, running in genuine parallel. (It runs — but *not*
  always at full speed; see the performance note under Limitations.)
- **General GBA emulation** — it runs any GBA ROM you supply (it's a real emulator, not a
  Pokémon-only app).
- **Emulated link cable between the two local games** — works with **any** GBA game's link
  features (it's general); a full Gen-3 Pokémon **trade** completes and persists across the
  two screens (the tested case), no second console.
- **Wireless lobby** — host / scan / join over local wireless (UDS), with a live seat map,
  per-seat game-match check, and a live **RTT / packet-loss** link probe. Verified across
  two consoles (~1-frame RTT, near-zero loss).
- **Quality-of-life** — ROM picker with recent-pairing resume, per-game `.sav` load/save,
  save states, per-screen scaling/filter, audio mix modes (solo / mixed / split + per-game
  volume), settings persistence.

## 🟡 Experimental — built, works, still being tuned on-device

- **Touchscreen "smart pointer"** — instead of an on-screen gamepad, the touch screen
  drives the *real in-game UI*: tap a tile to pathfind-walk there, tap menu/bag/party rows,
  pick battle targets, tap-to-advance dialog. **Gen-3 Pokémon only.** Being hardened one
  flow at a time on hardware; some detection is heuristic and version-specific (LeafGreen
  addresses are FireRed-derived and may be inert).
- **Experimental voxel 3D overworld** *(New 3DS, hardware-unproven)* — on supported Gen-3 Pokémon
  overworld maps the top screen can draw the map as a voxel 3D world, in stereo with the 3D slider,
  with optional depth-of-field and bloom (pause menu). Everywhere else the game is plain 2D and the
  3D slider has no effect. (The older 2.5D depth-pop, tilt and HD-2D post effects were removed
  2026-10-06 — see `docs/REMOVED-3D-ATTEMPTS.md`.)

## 🔜 In progress — the wireless emulation link

Playing a real **trade or battle between two consoles over local wireless**. A trade now works;
battles don't yet. Milestones, each independently testable:

| Step | What | Status |
|---|---|---|
| M1 | UDS lobby + seat negotiation | ✅ on 2 consoles |
| M2 | RTT / packet-loss probe | ✅ (loss ≈ 0, RTT ≈ 1 frame) |
| M2.5 | Net SIO driver, testable on **one** console via in-memory loopback | ✅ |
| M3 | Real 2-console link: a Pokémon trade over local wireless | 🟡 trade + save work on 2 consoles; the post-trade room-exit fix is not hardware-tested; battles not supported yet |
| M4 | 4 seats / mixed topologies (2+2, 2+1+1) | 🔮 planned |

## 🔮 Planned / aspirational

- **Pokémon co-op "shared overworld" (pokeMMO-style)** — see other players walking around
  in your overworld. A design study is done; the honest verdict: drawing other players as
  **overlay avatars is feasible**, but *native* interaction (forcing a trade/battle to
  start from the overworld) isn't reachable without ROM-call machinery the project doesn't
  have — so any co-op interaction would route through the emulated link, not the games'
  own MMO-style triggers. Designed, not built.
- **Online play beyond the same room** — the link uses UDS (local, device-to-device, no
  internet). A future `soc:U` (sockets) backend in `netlink.c` could carry the same link
  over the internet via a relay server.
- **AI-baked normal maps for HD-2D lighting** — highest effort, lowest certainty; only if
  the effects above land well.

---

## Honest limitations

- **Performance isn't always full speed — even on a New 3DS.** Two interpreted mGBA cores
  plus audio is a heavy load for the ARM11, so it **often dips below 60fps** in busy scenes
  (audio is the single biggest cost). It's genuinely playable, but smoothing this out is
  active, ongoing work. (Old 3DS is best-effort and runs slow — the two cores need core 2
  at 804 MHz + L2 cache, so a New 3DS / New 2DS XL is required in practice.)
- **The touch & 3D layers are Gen-3-Pokémon-specific.** The *emulator* plays any GBA game;
  those extra layers read Pokémon RAM and won't do anything in other games.
- **Wireless latency** over UDS is ~1 frame RTT — fine for poll-based trades/battles;
  continuous per-frame-link titles would hitch.
- **"Done" means hardware-verified.** Azahar/Citra don't model core-2 contention, the
  804 MHz budget, or wireless RF, so timing-sensitive features are only trusted once
  they've run on a real console.

See [`docs/`](docs/) for the design studies (stereoscopic 3D, HD-2D, wireless link, co-op
overworld) and [`docs/ROADMAP.md`](docs/ROADMAP.md) for the detailed internal version ladder.
