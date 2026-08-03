---
name: learn
description: >-
  Hard-won engineering knowledge for cartridge-resident homebrew (Game Boy Advance,
  with notes that carry to Nintendo DS / 3DS) and Pokemon save-format tools. Covers
  flashcart microSD sector I/O and the OS-mode rule, FatFs file ops + the never-corrupt
  verified-write pattern, Pokemon Gen-3 .sav parsing/encryption/genuine-trade mechanics
  plus save-code licensing, devkitARM/libtonc build and EWRAM/IWRAM memory budgeting,
  Mode-3 UI / input / PSG sound, and what is feasible vs hardware-impossible (with the
  real-hardware validation gate). Use this skill whenever working on a GBA/DS/3DS homebrew
  tool, anything that reads or writes a flashcart microSD, any Pokemon save (.sav) parse or
  edit, low-level cartridge I/O, on-cartridge UI/sound/logging, or build/memory problems on
  these consoles — even if the user never names the skill. Consult it BEFORE writing new
  SD-write, save-parse, RTC, or EWRAM-heavy code so you don't relearn the gotchas.
---

# Learn — homebrew & Pokemon save engineering

Accumulated, verified engineering knowledge for building tools that run *on the cartridge*
(Game Boy Advance primarily, with one-line notes where things carry to Nintendo DS / 3DS) and
that read/write a flashcart microSD or parse/edit Pokemon save files. The detail lives in the
focused reference files under `references/` — read the one matching your task; don't load them all.

## Pick the reference you need

| Your task touches… | Read |
|---|---|
| Low-level **SD sector I/O**, flashcart detection, the OS-mode constraint, IRQ/DMA, reboot-to-loader | `references/flashcart-sd-io.md` |
| **FatFs** `f_*` file listing/copy/move/delete, and any write that must not corrupt user data | `references/fatfs-and-safe-writes.md` |
| **Pokemon Gen-3 `.sav`** parse/decrypt/edit, "genuine" trading, IVs/EVs/stats, save-code **licensing** | `references/gen3-pokemon-saves-and-licensing.md` |
| **Build / Makefile** flow, or running out of **EWRAM/IWRAM**, placement attributes, pure-C cores | `references/gba-build-and-memory.md` |
| On-screen **UI, input, sound, logging** (incl. the key-repeat "stuck cursor" gotcha) | `references/gba-ui-input-sound.md` |
| Is a feature **feasible**? the **RTC**, and the **hardware-validation gate** + cross-platform notes | `references/capabilities-limits-and-cross-platform.md` |
| **3DS** homebrew: ARM11 cores/clocks, the **PICA200** GPU (no fragment shader), **hosting an emulator core** + driving/reading a guest game's **live RAM**, faux-3D from a 2D guest, embedding/linking **mGBA** | `references/3ds-homebrew-and-emulator-hosting.md` |
| The **Gen-3 Pokémon GBA link wire format** (handshake, `[CRC][8 cmd]` frames, `LINKCMD_*`/`LINKTYPE_*`, the trade flow), the **`RECEIVED_NOTHING`** failure vs checksum, and **relaying/terminating a link over a network** | `references/gen3-link-protocol.md` |

## Cross-cutting rules (hold across every tool on this hardware)

1. **OS-mode rule (GBA flashcarts).** During an EZ-Flash microSD transfer the game ROM is
   *unmapped*: no ROM reads, no rendering, no audio, IRQs off. Code and data live during a
   transfer must be EWRAM-resident, and any ROM-touching IRQ / soft-reset must be gated on an
   "is reading SD" flag. Never animate, play sound, or render mid-transfer — wrap those *around*
   the write instead.
2. **Never corrupt user data.** Every write goes through the verified-write pipeline — write to a
   `.tmp`, re-read and byte-compare, unlink the original, rename — with an immutable backup taken
   first. EZ-Flash writes have **no retry**, so always verify rather than trust the write.
3. **Big buffers live in EWRAM, never on the stack.** IWRAM (32 KiB) holds the stack; large or
   static buffers go in EWRAM (256 KiB) as `static EWRAM_BSS`, 4-byte aligned. Watch the EWRAM
   budget with `arm-none-eabi-size -A` (the `.sbss` section) before adding more.
4. **Keep algorithm/parse/file-op cores pure C.** No tonc/GBA headers in core logic — only
   `<stdint.h>`/`<string.h>` + explicit little-endian helpers — so a host test dual-compiles and
   runs the same code on a PC, where most save-format bugs are cheapest to catch.
5. **Not done until hardware-validated.** The SD / RTC / destructive-write path is not emulated;
   anything that writes the card, reads the RTC, or destroys user data stays "not done" until it
   runs on real hardware — first on disposable save copies.
6. **Clean-room every shipped save-format byte.** PKHeX (GPLv3/C#) and the pret decomps are
   reference-only; `savaughn/pksav` (MIT) is a safe shippable basis. Re-derive data (offsets,
   tables) cleanly and pick each tool's license early.

## Extending this skill

When you close out a research thread or hit a non-obvious gotcha on these platforms, append it to
the matching reference file (or add a new `references/<topic>.md` and a row above). Keep every entry
concrete, load-bearing, and **portable** — no project-specific paths — so any toolkit (GBA, DS, or
3DS) that installs this skill benefits. This skill is self-contained; copy the whole `learn/` folder
into any repo's `.claude/skills/` to version it per-project, or keep it in `~/.claude/skills/` to
share it across all of them.
