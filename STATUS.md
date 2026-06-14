# 3DGBA — status & roadmap

An honest snapshot of what's solid, what's half-baked, and what's still a dream. This is a
solo hobby project built and tested on a real New 3DS, so the bar for "done" is *runs on
hardware* — not *compiles* or *works in an emulator*.

**Legend:** ✅ works on real hardware · 🟡 built & experimental, actively being tuned on-device
· 🔜 in progress / next up · 🔮 planned / aspirational (designed, not built)

---

## ✅ Working — verified on a real New 3DS

- **Two GBA games at once**, one per screen — each a full [mGBA](https://mgba.io/) core
  pinned to its own ARM11 CPU core, running in genuine parallel. Full speed on a New 3DS.
- **General GBA emulation** — it runs any GBA ROM you supply (it's a real emulator, not a
  Pokémon-only app).
- **Emulated link cable between the two local games** — a full Gen-3 Pokémon **trade**
  completes and persists across the two screens, no second console.
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
- **Stereoscopic 3D depth** — uses the 3DS 3D slider to pop the overworld into a 2.5D
  diorama: characters and scenery stand up off a ground plane, with elevation/collision-
  aware depth. A walking "wobble" is the current tuning target. Gen-3 Pokémon tuned.
- **HD-2D-style post effects** — optional tilt-shift depth-of-field, LDR bloom, and a
  time-of-day color grade, all toggleable in the pause menu. Tuning ongoing.

## 🔜 In progress — the wireless emulation link

Playing a real **trade or battle between two consoles over WiFi** (today the wireless part
is only the lobby + a latency probe). Milestones, each independently testable:

| Step | What | Status |
|---|---|---|
| M1 | UDS lobby + seat negotiation | ✅ on 2 consoles |
| M2 | RTT / packet-loss probe | ✅ (loss ≈ 0, RTT ≈ 1 frame) |
| M2.5 | Net SIO driver, testable on **one** console via in-memory loopback | 🔜 in progress |
| M3 | Real 2-console NORMAL/MULTI link — a Pokémon trade over WiFi + "link lost" handling | 🔜 next |
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

- **New 3DS / New 2DS XL only** in practice — two software GBA cores need core 2 at
  804 MHz + L2 cache. Old 3DS is best-effort and will run slow.
- **The touch & 3D layers are Gen-3-Pokémon-specific.** The *emulator* plays any GBA game;
  those extra layers read Pokémon RAM and won't do anything in other games.
- **Wireless latency** over UDS is ~1 frame RTT — fine for poll-based trades/battles;
  continuous per-frame-link titles would hitch.
- **"Done" means hardware-verified.** Azahar/Citra don't model core-2 contention, the
  804 MHz budget, or wireless RF, so timing-sensitive features are only trusted once
  they've run on a real console.

See [`docs/`](docs/) for the design studies (stereoscopic 3D, HD-2D, wireless link, co-op
overworld) and [`docs/ROADMAP.md`](docs/ROADMAP.md) for the detailed internal version ladder.
