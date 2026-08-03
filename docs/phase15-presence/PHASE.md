# Phase 15 — Co-op presence: see your friend on your map

The 3DGBA answer to Project PM's headline feature. Queued 2026-08-03; launches after phase 14
(tilt) is verified and committed, because both phases edit `main.c`'s render path and the
per-frame parked window.

Design basis: `docs/kb/coop-shared-overworld.md` (our own feasibility study — its §2
"Recommended Architecture" is binding), corrected and reinforced by
`docs/kb/external/pm-rom-abi.md` §"The crux" and `docs/kb/external/gen1-render.md` finding 2.

## The one-paragraph version

Both games already run in our process, so the peer's map, tile, facing and identity are a plain
RAM read in the parked window. We draw our own avatar sprite over the frame at the computed
screen tile. Project PM needed a ROM hack for the equivalent because a stock emulator has no
game knowledge; **we have that knowledge already** (`gamestate.c` profiles), so the entire
export half of their ABI is redundant for us.

## Scope — IN (this phase)

| # | Milestone | Deliverable |
|---|---|---|
| M0 | Profile groundwork | `sb2ptr` (peer name / trainer ID / gender) and `gSpriteCoordOffsetX/Y` (sub-tile scroll) added to `GameProfile` for **BPEE/BPRE/BPGE**, pret-cited, marked verified vs verify-on-hw-pending |
| M1 | Same-map detection + readout | Cross-read both games in the parked window; when `(mapGroup, mapNum)` match, a HUD line naming the peer + their tile. No art needed — this proves the data before anything is drawn |
| M2 | The avatar | A real walking overlay sprite at the computed screen tile, correct facing, animated, interpolated tile-to-tile, tracking BG scroll; disappears off-screen and on map change |
| M3 | Identity + interaction trigger | "Card" readout (peer name/TID/gender from SaveBlock2 — a pure read, no link); adjacency+facing+A detection raising a prompt. Trade/battle themselves ride the **existing** link/Union-Room path — never a poked `callback2` |

## Scope — OUT (deliberate, with reasons)

- **M4 wireless (the two-console version).** This is the part that makes it "like the Platinum
  hack" over the radio, and it is a *small* follow-on phase: per `coop-shared-overworld.md` §2 it
  is a **data-source swap** — the render/gating layer built here is identical. It is out of THIS
  phase only because it adds a second UDS data channel serviced by `netlink.c`, which is adjacent
  to the frozen trade path while **HW run #13 is still pending**. Design for it (Invariant 3);
  build it once #13 rules. **User-confirmed 2026-08-03: "we will wait on M4 then, after 13."**
  So M4 is the first thing to queue once run #13 reports — do not start it before.
- **Native in-game avatars** (occlusion behind buildings, collision, talking to them). That needs
  the game engine to own the entity, i.e. our own FR/EM ROM patch — round 2 proved the
  distribution model (pret decomp → build → ship an xdelta) but it is a project plus an
  `ip-legal` gate. Not now.
- **Object-event injection** (`gObjectEvents[]` / `gLinkPlayerObjectEvents[]`). Rejected with
  evidence in `coop-shared-overworld.md` §2 and re-confirmed by round 2: a bare struct write
  yields an invisible inert slot, the real spawners are `static` ROM functions we cannot call,
  `RemoveObjectEventsOutsideView` culls untracked objects every camera tick, and `gbacore.h`
  exposes no `write32` or ROM-call trampoline. Do not attempt it.

## The honest ceiling (state it in the README/HANDOFF, do not oversell)

The GBA engine does not know the avatar exists. **No collision, no z-ordering behind buildings,
no talking to them.** You see your friend walk around your map in real time; you cannot bump
them. Project PM gets the rest because they patched the ROM. This is the documented boundary,
not a bug to be fixed later by tweaking.

## Invariants (binding)

1. **Read-only with respect to both games.** Presence never writes emulated RAM. It cannot
   corrupt either save or either game's state — that property is the reason this design was
   chosen over injection, so preserve it absolutely.
2. **All cross-game reads happen in the existing parked window**, where both workers are blocked
   (`main.c` ~2181, "Workers are parked here -> touch RAM access safe"), alongside the existing
   depth-grid/touch/gate reads. No new synchronization primitive, no read from a worker thread.
3. **Transport-agnostic peer state.** Define ONE `PeerPresence` struct (map, tile, facing,
   sub-tile offset, sprite/gender, name, TID, a monotonic `round`/frame counter for staleness)
   and populate it behind a source abstraction. Same-console fills it from sibling RAM; M4 later
   fills it from a UDS beacon. The draw/gate layer must not know which. Mirror Project PM's
   48-byte overworld record shape (`pm-rom-abi.md`) — including the frame counter, which doubles
   as a liveness/wedge signal.
4. **Composes with phase 14 tilt.** When tilt is active the avatar must ride the ground plane:
   project its **foot anchor** through the same tilt projection and translate the upright sprite
   so the feet pin to the projected point — **pure translation, never depth-scaled**
   (`gen1-render.md` finding 2; scaling is what makes pixel art mushy). Y-sort by foot Y. Read
   phase 14's `tilt.h` and use it; do not duplicate the math.
5. **Gate honestly.** Draw only when both games are on the same `(mapGroup, mapNum)`, both
   snapshots are valid, and the peer is in a state where a position read is meaningful (overworld —
   reuse phase 14's ctx gate work). No profile for a game ⇒ presence silently OFF, never garbage.
6. **Presentation-only and flag-gated**, default sensible; costs nothing measurable when off.
   The frame budget rule from phase 14 applies: the render thread feeds two saturated workers.
7. **Our own sprite art.** Ship a generic 4-direction overworld sprite as our own asset (the
   existing `.t3x`/assets pipeline). Do NOT rip the peer's VRAM frames — `coop-shared-overworld.md`
   §4 judged it not worth the swapped-VRAM/CPU-DMA complexity, and it is also the cleaner IP
   posture.
8. **Pure-C, host-tested** (CLAUDE.md #4): the screen-tile math, staleness/liveness rules,
   interpolation and the same-map predicate live in a header-free module with golden tests.
9. Suites stay green and grow; `make` clean with zero warnings in new files; dated `BUILDLOG.md`
   entry per slice; HANDOFF gains the hardware checklist items. Hardware-final (CLAUDE.md #6).

## Decisions already made (do not re-open)

- **Start on Emerald + FireRed, not Ruby + Sapphire.** `coop-shared-overworld.md` §3 M0 opens
  with "author the missing AXVE/AXPE rows" — that was written before we had a working
  three-profile table and it is the single biggest upfront cost in that plan. We already have
  **verified BPEE/BPRE/BPGE** profiles, the user's own carts are Emerald and FireRed **rev1**,
  and phase 13's fingerprint records the revision per run. Ruby/Sapphire rows become an optional
  later addition, not a prerequisite.
- **The screen-tile formula is already derived and checked** (`coop-shared-overworld.md` §1a):
  the player sits at screen tile (7,5), `gSaveBlock1Ptr->pos` is the top-left visible tile for
  both games identically, so peer A appears on host B's screen at
  `(7 + (Apx − Bpx), 5 + (Apy − Bpy))` with the MAP_OFFSET bias cancelling. Verify it, do not
  re-derive it.
- **Interaction rides the game's own link path** (Union Room / cable club specials), never a
  forced `callback2` — a trade/battle is a live link state machine, not a callable function.
