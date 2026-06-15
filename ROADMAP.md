# 3DGBA — Roadmap & Known Issues

The running list of what's planned and what's currently rough. 3DGBA is an early, actively-developed
hobby project — this is an honest backlog, not a promise of dates or order. The **core** (two GBA games
at once on one New 3DS, joined by an emulated link cable) runs on real hardware; the **wireless, 3D, and
touch** layers are experimental. If something here matters to you, feel free to open an issue or a PR.

## Known issues / current bugs

- **Closing from the wireless phase hangs the system.** After using the wireless lobby/link, closing
  3DGBA (HOME → Close, or launching another app and confirming) hangs on "closing the software" — the
  rest of the 3DS stays responsive, but the app won't exit and needs a Luma reboot. Cause: UDS teardown
  blocks during the close transition. *Workaround:* back out of wireless (press **B**) **before** closing.
- **Performance isn't always full speed.** Two interpreted mGBA cores plus audio is a heavy load for the
  ARM11, so busy scenes dip below 60fps (audio is the biggest single cost). New 3DS / New 2DS XL only;
  Old 3DS runs slow.
- **Wireless emulation link is experimental.** The one-console "Net link (loopback)" self-test now reaches
  the trade screen, but the trade itself is still being debugged (a word-alignment fix is in hardware
  testing). Real two-console wireless trades (M3) aren't done yet.
- **Stereoscopic-3D "wobble" while walking.** The per-eye depth shifts oddly during overworld movement;
  being tuned. Gen-3 Pokémon only.
- **Touch "smart pointer" is Gen-3-Pokémon-specific and partly heuristic.** Some flows (field-menu
  detection; LeafGreen addresses derived from FireRed) may misfire or be inert; being hardened one flow
  at a time, with on-device diagnostics.

## Planned — wireless link

- **M2.5** — the net SIO driver against a one-console loopback (no radio). *In progress* — trade-data fix
  under test.
- **M3** — the simplest real **two-console** link over UDS: a Pokémon trade/battle between two nearby
  consoles, plus the timeout → "link lost" path.
- **M4** — 4 seats and mixed topologies (2+2, 2+1+1), integrated with the local two-core link.
- **Online play (beyond local).** UDS is local-only; internet play would be an *optional, user-configured*
  `soc:U` socket backend to a relay server, kept entirely separate from the zero-config local path.

## Planned — Pokémon co-op

- **Shared overworld (pokeMMO-style)** — see other players walking around your overworld. A design study
  is done; drawing other players as overlay avatars is feasible, but *native* trade/battle triggers aren't
  reachable from the overworld, so any interaction would route through the emulated link.

## Planned — visuals

- **In-app 3D-strength control ("tilt bar")** — dial the stereoscopic pop in software, decoupled from the
  hardware 3D slider.
- More HD-2D depth refinement (Octopath-style); broaden where the effect applies.
- *(Optional, low certainty)* AI-baked normal maps for sprite/tile lighting — only if the above land well.

## Planned — performance

- Unfocused-game frameskip tuning; lighten the per-frame audio mix/feed (the documented heavy-scene cost);
  confirm the New-3DS **core 2 / 804 MHz / L2** budget is actually fully used.

## Planned — distribution

- GitHub Releases of the `.cia` / `.3dsx`; aim to get listed in **Universal-DB** / the **Homebrew App
  Store** (needs a real unique title ID and the mGBA source + local patch published, per GPL/MPL).

## Testing / validation

- The wireless trade — loopback (one console), then a real two-console trade — on **real hardware**.
- The close-from-wireless fix.
- **General emulation of non-Pokémon GBA games** (the emulator is general; only the touch/3D layers are
  Pokémon-specific).
- Anything timing-sensitive is "done" only after it runs on a real New 3DS — Azahar/Citra don't model
  core-2 contention, the 804 MHz budget, or wireless RF.

---

*3DGBA is unofficial and not affiliated with, authorized by, or endorsed by Nintendo. It ships **no games
or BIOS** — bring your own legally-dumped GBA ROM. Built on [mGBA](https://mgba.io/) (MPL-2.0). See the
[README](README.md) for the full disclaimer and license (**GPLv3**); the detailed internal version ladder
lives in [docs/ROADMAP.md](docs/ROADMAP.md).*
