#!/usr/bin/env python3
"""gdbio.py — pure-python GDB RSP client + symbol lookup + base-offset verification
(phase-16 slice E2; SPEC-harness H2.2/H3.3/H3.4, SPEC-protocols S2).

PROTOCOL MODEL — the INSTALLED release, not master
==================================================
SPEC-protocols S2 was researched from Azahar master@5394625. Slice E2's live run proved the
installed release 2125.1.2 ships a MUCH older, vanilla-Citra gdbstub, so this file follows
the RELEASE source, fetched 2026-08-08 from
github.com/azahar-emu/azahar tag `2125.1.2` (`src/core/gdbstub/gdbstub.cpp`, 1281 lines —
cited below as `gdbstub.cpp`; also `src/core/core.cpp`, `src/citra_qt/citra_qt.cpp`).
Every deviation from S2 was ALSO confirmed live against the running emulator (BUILDLOG E2).

The release dialect (each fact: source line + how it was live-verified):

1. ONE gdb client per emulator boot, and boot BLOCKS until it connects.
   `System::Init` calls `GDBStub::DeferStart()` unconditionally (core.cpp:574); the first
   `RunLoop` iteration then runs `ToggleServer(true)` -> `Init(port)` which sets
   `halt_loop=true` (gdbstub.cpp:1157) and calls a BLOCKING `accept()` on the emu thread
   (gdbstub.cpp:1203). With the harness profile (`use_gdbstub=true`) EVERY boot therefore
   parks pre-first-instruction until a client connects, and stays halted until `c`.
   [live: g_renderSeq==0 a minute after boot; ran only after our `c`]
2. `-g PORT` adds NOTHING over the INI route: it just sets use_gdbstub+port
   (citra_qt.cpp:295-300). Master's pause-at-start flag does not exist here.
3. `?` does NOT attach or halt — it merely re-sends the latest stop reason
   (`SendSignal(current_thread, latest_signal)`, gdbstub.cpp:1069-1071; initial reply is
   `T00` with no thread suffix since current_thread is null, gdbstub.cpp:609-637).
   There is NO vAttach, NO qXfer:osdata (qSupported is
   `PacketSize=2000;qXfer:features:read+;qXfer:threads:read+`, gdbstub.cpp HandleQuery).
   [live: `?` -> `T00`]
4. HALT = the raw interrupt byte 0x03 (NOT a packet): sets halt_loop and stop-replies
   `T05...` (ReadCommand, gdbstub.cpp:648-652). Idempotent — safe when already halted.
   WIRE QUIRK (live transcript 2026-08-08): the T05 is followed by a spurious EMPTY
   packet `$#00` — halt() drains it or every later reply shifts off by one.
   Master's "0x03 null-deref" quirk does not apply (SendSignal null-guards its thread).
   RESUME = `c` (Continue() clears the flags, gdbstub.cpp:910-913; NO reply until the
   next stop). While halted the emu thread keeps polling HandlePacket once per RunLoop
   iteration (core.cpp:86-101), so reads work fine on a halted target.
5. `D` (detach) is UNSUPPORTED — the default switch arm replies EMPTY (gdbstub.cpp:1090).
   Closing the TCP socket triggers `Shutdown()` via the recv!=1 path (gdbstub.cpp:361-368):
   the client socket is shutdown()-but-never-close()d (lingers in CLOSE_WAIT),
   `gdbserver_socket=-1`, `defer_start=false` (gdbstub.cpp:1230) — the server NEVER accepts
   again this boot, and `halt_loop` keeps whatever value it had:
     * disconnect while HALTED  => emulation frozen FOREVER (the E2 probe-A wedge);
     * disconnect after `c`     => app runs on, gdb gone until the next boot.
   [live: both states reproduced 2026-08-08]
6. `k` = ToggleServer(false)+Continue (gdbstub.cpp:1072-1077) — it kills the STUB and
   resumes the app; it does NOT shut down the emulator (master's k does). There is no
   clean-quit channel via gdb on this release, so no `shutdown` command exists here.
7. Memory read `m<addr-hex>,<len-hex>`: invalid address -> `E00` (gdbstub.cpp:842-844;
   master replies empty), len*2 > 9996 -> `E01` (missing `return` after it, so keep reads
   FAR below the cap: MAX_READ_CHUNK=4096). Reads use the kernel's CURRENT process
   (gdbstub.cpp:842) — with a single .3dsx app that is the app once booted.

THE BROKER — living with "one client per boot"
==============================================
Because the stub accepts exactly one client per boot, per-command TCP connections (the
H2.2 sketch) are impossible: the first CLI command would consume the boot. Instead gdbio
keeps THE connection inside a tiny broker daemon (`gdbio serve`, auto-spawned on first
use) that holds the TCP session and serves JSON-lines requests on a unix socket at
state/gdbio.sock. Every other gdbio command talks to the broker; each read op is a
halt -> read -> resume cycle (observation, not interference — H2.2), with the halted
window kept to a few ms. `gdbio detach` resumes the app, closes the TCP cleanly and exits
the broker — after that the app runs client-less (fact 5) and only a fresh `azctl boot`
restores gdb access. The broker self-exits when the emulator dies (TCP EOF watch).

SYMBOLS + BASE VERIFICATION (unchanged from the S2 plan)
========================================================
nm PRIMARY / .map cross-check / stdlib-ELF ground truth — see build_symtab(),
map_crosscheck() (TLS exclusion: nm reports .tbss by TLS offset, probed live) and
ElfImage. S2.6: a .3dsx loads at PROCESS_IMAGE_VADDR 0x00100000 with link-time layout,
so map/nm addresses ARE gdb addresses; `verify-base` proves it per build by comparing
three anchors spanning .text/.rodata/.data against the ELF file bytes (H3.3):
  text   ctl_log_line  (nm t 0x001eb328, 0xc4 B, 2026-08-04 build) — immutable app code
  rodata AUDIO_NAMES   (nm R 0x00235000, audio.c) — first rodata bytes, immutable
  data   _deadbeef     (nm d 0x0049b4e4, 4 B) — mGBA's illegal-instruction marker
         `static uint8_t _deadbeef[4] = {0x10,0xB7,0x10,0xE7}` (external/mgba/src/gba/
         memory.c:28): a KNOWN compile-time word (LE 0xE710B710), pointed at but never
         written at runtime (memory.c:359). Fallback: g_appActive (main.c:85 `= true`).

Smoke/liveness symbols read by E2+ (S2.7 + source cites; resolved via nm per build):
  g_renderSeq   main.c:91, ++ per render-loop frame (main.c:2584) — ~60 Hz monotonic
  g_appActive   main.c:85  — 1 while foregrounded (bool, 1 byte)
  g_quit        main.c:74  — 0 until quit (bool, 1 byte)
  g_prefs       theme.c:21 — 8x int32 UiPrefs, compile-time {0,205,168,14,0,0,0,0}
                (settings_load may overwrite from sdmc:/3DGBA/settings.bin after boot)

CLI (via the venv shim: `tools/emutest/run gdbio <cmd>`):
  gdbio syms [--refresh] [--lookup NAME]
  gdbio resume                            # REQUIRED once after every azctl boot (fact 1)
  gdbio verify-base
  gdbio read <symbol|0xADDR> <len> [--out F.bin]
  gdbio read-u32 <symbol|0xADDR>
  gdbio read-u8  <symbol|0xADDR>          # g_quit/g_appActive are 1-byte bools
  gdbio poll <symbol|0xADDR> (--changed | --expect V) [--width N] [--timeout S] [--interval MS]
  gdbio status                            # broker + stub state (qSupported = drift detector)
  gdbio detach                            # resume + release the boot's one client + broker exit
  gdbio serve                             # the broker itself (auto-spawned; rarely run by hand)

Exit codes (H2): 0 = ok/condition met, 1 = fail/timeout. gdbio never SKIPs (75 unused).
"""

import argparse
import json
import os
import re
import select
import shutil
import socket
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))

GDB_PORT = 24689          # Azahar default port (azctl pins it in the INI profile)
RSP_TIMEOUT_S = 5.0       # per-op timeout (SPEC-harness H3.4 "Timeout 5 s per read")
# Wall-clock default for `poll` — deliberately NOT the per-op timeout: BUILDLOG E2 measured
# resume -> first render-loop tick at 5-10 s on this machine, so any deadline at or below
# RSP_TIMEOUT_S fails a healthy boot (see main()'s poll parser comment).
POLL_TIMEOUT_S = 30.0
MAX_READ_CHUNK = 4096     # far below the 9996-hex-char reply cap (module doc fact 7)
BROKER_TIMEOUT_S = 12.0   # CLI->broker op timeout (covers autostart + one RSP op)


def elf_path():
    return os.environ.get("EMUTEST_ELF", os.path.join(REPO, "3DGBA.elf"))


def app_path():
    return os.environ.get("EMUTEST_APP", os.path.join(REPO, "3DGBA.3dsx"))


def map_path():
    # Probed 2026-08-08 (S2.5): the ld map lands at build/3DGBA.map, NOT the repo root.
    return os.environ.get("EMUTEST_MAP", os.path.join(REPO, "build", "3DGBA.map"))


def cache_path():
    return os.path.join(HERE, "emutest.mapsyms")  # gitignored (H1.4)


def state_dir():
    return os.environ.get("EMUTEST_STATE_DIR", os.path.join(HERE, "state"))


def broker_sock_path():
    return os.path.join(state_dir(), "gdbio.sock")


def broker_log_path():
    return os.path.join(state_dir(), "gdbio-broker.log")


MAKE_LINE = ("export DEVKITPRO=/opt/devkitpro DEVKITARM=$DEVKITPRO/devkitARM && "
             "make -j8   (in {})".format(REPO))


# =============================================================================== RSP layer
# Standard RSP framing ($<data>#<2-hex checksum>, +/- acks). Pure functions + PacketReader
# are host-tested against goldens in tests/test_rsp.py (H6 split).

class RspError(Exception):
    pass


def rsp_checksum(raw):
    """Modulo-256 sum of the raw (wire) payload bytes — computed BEFORE unescape/RLE."""
    return sum(raw) % 256


def rsp_escape(payload):
    """Escape '}' '$' '#' '*' as '}'+(c^0x20) (GDB RSP binary-data escaping). Our command
    packets never need it, but the builder is correct for arbitrary payloads."""
    out = bytearray()
    for c in payload:
        if c in (0x23, 0x24, 0x2A, 0x7D):  # '#' '$' '*' '}'
            out.append(0x7D)
            out.append(c ^ 0x20)
        else:
            out.append(c)
    return bytes(out)


def rsp_build(payload):
    """Frame a command packet: b'm49b49c,20' -> b'$m49b49c,20#9a'."""
    esc = rsp_escape(payload)
    return b"$" + esc + b"#" + ("%02x" % rsp_checksum(esc)).encode()


def rsp_decode_payload(raw):
    """Decode a received wire payload: single pass handling '}' escapes and '*' RLE.
    RLE (GDB spec): 'X','*',c => 'X' followed by (ord(c)-29) MORE copies of 'X'
    (so b'0* ' -> b'0000'). The release stub replies plain text in practice; this is the
    spec-complete decoder, golden-tested."""
    out = bytearray()
    i = 0
    while i < len(raw):
        c = raw[i]
        if c == 0x7D and i + 1 < len(raw):          # '}' escape
            out.append(raw[i + 1] ^ 0x20)
            i += 2
        elif c == 0x2A and out:                      # '*' run-length
            if i + 1 >= len(raw):
                raise RspError("truncated RLE at end of payload")
            out.extend(out[-1:] * (raw[i + 1] - 29))
            i += 2
        else:
            out.append(c)
            i += 1
    return bytes(out)


class PacketReader:
    """Incremental deframer: feed() wire bytes, next_event() yields
    ('ack',) | ('nak',) | ('pkt', decoded) | ('badsum', raw) | None (incomplete)."""

    def __init__(self):
        self._buf = bytearray()

    def feed(self, data):
        self._buf += data

    def next_event(self):
        while self._buf:
            c = self._buf[0]
            if c == 0x2B:                            # '+'
                del self._buf[0]
                return ("ack",)
            if c == 0x2D:                            # '-'
                del self._buf[0]
                return ("nak",)
            if c == 0x24:                            # '$'
                end = self._buf.find(b"#")
                if end < 0 or len(self._buf) < end + 3:
                    return None                      # incomplete packet
                raw = bytes(self._buf[1:end])
                csum = bytes(self._buf[end + 1:end + 3])
                del self._buf[:end + 3]
                try:
                    ok = int(csum, 16) == rsp_checksum(raw)
                except ValueError:
                    ok = False
                if not ok:
                    return ("badsum", raw)
                return ("pkt", rsp_decode_payload(raw))
            del self._buf[0]                         # stray byte between packets: skip
        return None


class RspClient:
    """THE one TCP session this emulator boot gets (module doc facts 1/5). Owned by the
    broker; direct use is for probes only. halt()/cont() track the stub's halt_loop —
    after connect the state is HALTED (Init sets halt_loop=true before its accept,
    gdbstub.cpp:1157) and halt() re-asserts it anyway to start from a known state."""

    def __init__(self, port=GDB_PORT, host="127.0.0.1", timeout=RSP_TIMEOUT_S):
        self.port = port
        self.host = host
        self.timeout = timeout
        self.sock = None
        self._reader = PacketReader()
        self.halted = None    # None = unknown (pre-connect)

    def connect(self):
        try:
            s = socket.create_connection((self.host, self.port), timeout=self.timeout)
        except OSError as e:
            raise RspError(
                "cannot connect to the gdb stub at {}:{} ({}) — boot Azahar first "
                "(tools/emutest/run azctl boot). If it IS running, this boot's one gdb "
                "client was already consumed (module doc fact 5): azctl boot again."
                .format(self.host, self.port, e))
        s.settimeout(0.25)              # short poll slices; per-op deadline in _wait_*
        self.sock = s
        # Fresh-boot state: emulator parked pre-first-instruction, halt_loop=true.
        self.halted = True

    def close(self):
        if self.sock is not None:
            try:
                self.sock.close()
            finally:
                self.sock = None

    def fileno(self):
        return self.sock.fileno()

    # ---- low-level ------------------------------------------------------------------
    def _recv_some(self, deadline):
        while time.time() < deadline:
            try:
                data = self.sock.recv(65536)
            except socket.timeout:
                continue
            if not data:
                raise RspError("stub closed the connection (emulator gone?)")
            return data
        raise RspError("timeout ({}s) waiting for stub data".format(self.timeout))

    def _wait_packet(self, deadline):
        """Consume acks/strays until a full packet arrives; ack it; return its payload."""
        while True:
            ev = self._reader.next_event()
            if ev is None:
                self._reader.feed(self._recv_some(deadline))
                continue
            if ev[0] in ("ack", "nak"):
                continue
            if ev[0] == "badsum":
                self.sock.sendall(b"-")              # ask the stub to re-send its reply
                continue
            self.sock.sendall(b"+")
            return ev[1]

    def _txn(self, payload):
        """Command packet -> reply packet. The release stub NAKs only on checksum errors
        (ReadCommand, gdbstub.cpp:672-684); we simply retry the send on 'nak' by looping —
        rare enough on localhost that a plain resend loop suffices."""
        for _attempt in range(5):
            self.sock.sendall(rsp_build(payload))
            deadline = time.time() + self.timeout
            got_nak = False
            while True:
                ev = self._reader.next_event()
                if ev is None:
                    self._reader.feed(self._recv_some(deadline))
                    continue
                if ev[0] == "ack":
                    continue
                if ev[0] == "nak":
                    got_nak = True
                    break
                if ev[0] == "badsum":
                    self.sock.sendall(b"-")
                    continue
                self.sock.sendall(b"+")
                return ev[1]
            if not got_nak:
                break
        raise RspError("packet {!r} NAKed 5 times".format(payload))

    # ---- protocol verbs (release dialect — module doc facts 3/4/7) -------------------
    def _drain(self, secs):
        """Consume the ONE spurious packet the release sends after a 0x03 halt.

        LIVE-PROBED NECESSITY (2026-08-08 wire transcript): the stub answers 0x03 with TWO
        packets — `$T05#b9` AND a spurious empty `$#00` — and an unconsumed empty packet
        shifts every later reply off by one. The stub never waits for reply-acks
        (ReadCommand ignores '+'), so the drained packet needs no ack.

        REVIEW FIX (2026-08-09): this was a FIXED `while time.time() < end` sleep — it
        always burned the whole 0.3 s even though the raw-socket probe shows the artifact
        arriving in the SAME TCP burst as the stop reply (`recv b'$T05#b9'` and
        `recv b'$#00'` both at +0.0007 s). That made the 0.3 s ~75% of every
        halt->read->cont blink (and is why `see rec --with-state` delivered 1.5 of 4
        requested fps). Now it RETURNS AS SOON AS the artifact packet is seen, with `secs`
        kept as the ceiling — so a late packet is still absorbed (the old code's real
        failure mode: a >0.3 s packet would be misread as the next `m` reply and surface
        as the bogus "empty reply … unsupported command?" diagnosis).
        Returns True when the artifact was consumed."""
        end = time.time() + secs
        while True:
            ev = self._reader.next_event()
            if ev is not None:
                if ev[0] in ("pkt", "badsum"):
                    return True                  # the artifact (empty $#00) — done
                continue                         # lone acks/naks: keep looking
            remaining = end - time.time()
            if remaining <= 0:
                return False
            r, _w, _x = select.select([self.sock], [], [], min(0.05, remaining))
            if not r:
                continue
            try:
                data = self.sock.recv(65536)
            except socket.timeout:
                continue
            if not data:
                raise RspError("stub closed the connection (emulator gone?)")
            self._reader.feed(data)

    def halt(self):
        """Raw 0x03 -> stop-reply T05 (ReadCommand, gdbstub.cpp:648-652). Idempotent.
        Drains the trailing spurious empty packet (see _drain)."""
        self.sock.sendall(b"\x03")
        r = self._wait_packet(time.time() + self.timeout)
        if not (r.startswith(b"T") or r.startswith(b"S")):
            raise RspError("unexpected stop-reply {!r} to interrupt".format(r))
        self._drain(0.3)
        self.halted = True
        return r.decode("ascii", "replace")

    def cont(self):
        """`c` clears halt/step flags (Continue, gdbstub.cpp:910-913). NO reply comes
        until a future stop — do not wait for one."""
        self.sock.sendall(rsp_build(b"c"))
        # The stub acks the packet ('+') but sends no reply; drain the ack lazily on the
        # next _wait_packet/_txn (PacketReader skips lone acks).
        self.halted = False

    def stop_reason(self):
        """`?` — report-only on this release (does NOT halt; module doc fact 3)."""
        return self._txn(b"?").decode("ascii", "replace")

    def qsupported(self):
        """Drift detector: the release replies
        PacketSize=2000;qXfer:features:read+;qXfer:threads:read+  (HandleQuery)."""
        return self._txn(b"qSupported").decode("ascii", "replace")

    def read_mem(self, addr, length):
        out = bytearray()
        while length > 0:
            k = min(length, MAX_READ_CHUNK)
            r = self._txn("m{:x},{:x}".format(addr, k).encode())
            if re.fullmatch(rb"E[0-9a-fA-F]{2}", r):
                # E00 = IsValidVirtualAddress failed (gdbstub.cpp:842-844); E01 = too long.
                raise RspError("stub error {} reading @0x{:08x} (E00 = address not mapped "
                               "in the current process)".format(r.decode(), addr))
            if r == b"":
                raise RspError("empty reply reading @0x{:08x} — unsupported command? "
                               "(release default arm, gdbstub.cpp:1090)".format(addr))
            try:
                chunk = bytes.fromhex(r.decode("ascii"))
            except ValueError:
                raise RspError("non-hex read reply {!r}".format(r[:64]))
            if len(chunk) != k:
                raise RspError("short read @0x{:08x}: {}/{} bytes".format(
                    addr, len(chunk), k))
            out += chunk
            addr += k
            length -= k
        return bytes(out)


# ============================================================================ ELF extract
# Minimal ELF32-LE program-header reader (H3.3: stdlib only, host-tested in
# tests/test_elf_extract.py). Layout offsets per the ELF32 spec (TIS ELF 1.2).

class ElfImage:
    PT_LOAD = 1

    def __init__(self, path):
        with open(path, "rb") as f:
            self.data = f.read()
        d = self.data
        if d[:4] != b"\x7fELF":
            raise ValueError("{}: not an ELF".format(path))
        if d[4] != 1 or d[5] != 1:
            raise ValueError("{}: not ELF32 little-endian".format(path))
        e_phoff = struct.unpack_from("<I", d, 0x1C)[0]
        e_phentsize = struct.unpack_from("<H", d, 0x2A)[0]
        e_phnum = struct.unpack_from("<H", d, 0x2C)[0]
        self.loads = []           # (vaddr, filesz, memsz, offset)
        for i in range(e_phnum):
            o = e_phoff + i * e_phentsize
            p_type, p_offset, p_vaddr, _p_paddr, p_filesz, p_memsz = \
                struct.unpack_from("<IIIIII", d, o)
            if p_type == self.PT_LOAD:
                self.loads.append((p_vaddr, p_filesz, p_memsz, p_offset))

    def bytes_at(self, vaddr, length):
        """File bytes for [vaddr, vaddr+length) — None if any part is not file-backed
        (bss, or unmapped). The compile-time ground truth for verify-base."""
        for v, filesz, _memsz, off in self.loads:
            if v <= vaddr and vaddr + length <= v + filesz:
                s = off + (vaddr - v)
                return self.data[s:s + length]
        return None


# ============================================================================== symbols
# nm PRIMARY (statics included), .map cross-check (S2.5/S2.6). Cached in
# emutest.mapsyms keyed on the ELF's mtime+size (SPEC-harness H2.2).

def nm_tool():
    cand = os.path.join(os.environ.get("DEVKITARM", "/opt/devkitpro/devkitARM"),
                        "bin", "arm-none-eabi-nm")
    if os.path.exists(cand):
        return cand
    found = shutil.which("arm-none-eabi-nm")
    if found:
        return found
    raise SystemExit("gdbio: arm-none-eabi-nm not found — install devkitARM "
                     "(sudo dkp-pacman -S 3ds-dev) or set DEVKITARM")


def nm_parse(text):
    """Parse `nm -S` output -> {name: [(addr, type, size|None), ...]}.
    Probed line shapes (2026-08-08, GNU nm 2.45.1 on 3DGBA.elf; tests/fixtures/nm_snippet.txt):
      `0049b49c 00000020 D g_prefs`   addr size type name
      `00491454 r .LC2`               addr type name (no size)
      `         w __cxa_type_match`   no address (weak/undefined) -> skipped
    Duplicate names (file-scope statics in different TUs, e.g. kHeld) all kept —
    lookup() refuses ambiguous names."""
    syms = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) == 4 and _is_hex(parts[0]) and _is_hex(parts[1]):
            addr, size, typ, name = int(parts[0], 16), int(parts[1], 16), parts[2], parts[3]
        elif len(parts) == 3 and _is_hex(parts[0]):
            addr, size, typ, name = int(parts[0], 16), None, parts[1], parts[2]
        else:
            continue
        syms.setdefault(name, []).append((addr, typ, size))
    return syms


def _is_hex(s):
    return len(s) == 8 and all(c in "0123456789abcdefABCDEF" for c in s)


# GNU ld map parsing (S2.5). Probed shapes (build/3DGBA.map 2026-08-04; fixture
# tests/fixtures/map_snippet.map):
#   output section:  `.rodata         0x00235000   0x2651e8`         (name at col 0)
#   symbol line:     `                0x00235000                AUDIO_NAMES` (2 tokens)
#   NOT symbols: input-section/`*fill*`/object lines (3 tokens), `PROVIDE (...)`,
#   `. = ALIGN (...)`, `sym = sym` assignments (all >2 tokens or non-0x first token).
_MAP_SECTION_RE = re.compile(r"^(\.[A-Za-z_][\w.]*)")
_MAP_SYMBOL_RE = re.compile(r"^ +(0x[0-9a-f]+) +(\S+)$")


def map_parse(text):
    """-> [(name, addr, section)] for plain symbol-definition lines."""
    out = []
    section = None
    for line in text.splitlines():
        m = _MAP_SECTION_RE.match(line)
        if m:
            section = m.group(1)
            continue
        m = _MAP_SYMBOL_RE.match(line)
        if m and not m.group(2).startswith("0x"):
            out.append((m.group(2), int(m.group(1), 16), section))
    return out


IMAGE_BASE = 0x00100000  # PROCESS_IMAGE_VADDR (S2.6: 3dsx.cpp:287, memory.h:192)


def map_crosscheck(nm_syms, map_syms):
    """Map addresses must equal nm addresses for every unambiguous shared symbol —
    the map is a cross-check of the nm table (SPEC-harness: nm primary).
    EXCLUDED: symbols whose nm address is below IMAGE_BASE — nm reports thread-local
    (.tbss) symbols by their offset WITHIN the TLS block, not a process vaddr (probed
    2026-08-08: libctru's `B __ctru_dev_utf16_buf` = 0x0 / `__ctru_dev_path_buf` = 0x804
    vs their .tbss map placement at 0x49d2d8+). Those offsets are not readable addresses;
    lookup() callers reading such a symbol get the same sub-image-base address and the
    stub's E00 error names the address, so the failure is loud, not silent."""
    checked = 0
    mismatches = []
    for name, addr, _sec in map_syms:
        entries = nm_syms.get(name)
        if entries and len(entries) == 1 and entries[0][0] >= IMAGE_BASE:
            checked += 1
            if entries[0][0] != addr:
                mismatches.append((name, addr, entries[0][0]))
    return {"checked": checked,
            "mismatches": [{"name": n, "map": m, "nm": e} for n, m, e in mismatches]}


def build_symtab(refresh=False, quiet=False):
    """Load (or build) the cached symbol table. Cache keyed on ELF mtime+size (H2.2:
    'cache invalidated on 3DGBA.elf mtime change'). Returns the cache dict."""
    elf = elf_path()
    if not os.path.exists(elf):
        raise SystemExit("gdbio: {} missing — build it:\n  {}".format(elf, MAKE_LINE))
    st = os.stat(elf)
    if not refresh:
        try:
            with open(cache_path()) as f:
                cache = json.load(f)
            if (cache.get("schema") == 1 and cache.get("elf") == elf
                    and cache.get("elf_mtime") == st.st_mtime
                    and cache.get("elf_size") == st.st_size):
                return cache
        except (OSError, ValueError):
            pass
    tool = nm_tool()
    r = subprocess.run([tool, "-S", elf], capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit("gdbio: nm failed: {}".format(r.stderr.strip()))
    syms = nm_parse(r.stdout)
    cache = {"schema": 1, "elf": elf, "elf_mtime": st.st_mtime, "elf_size": st.st_size,
             "nm_tool": tool,
             "symbols": {k: [[a, t, s] for a, t, s in v] for k, v in syms.items()},
             "map": None}
    mp = map_path()
    if os.path.exists(mp):
        with open(mp) as f:
            xc = map_crosscheck(syms, map_parse(f.read()))
        cache["map"] = {"path": mp, "mtime": os.path.getmtime(mp), "crosscheck": xc}
        if xc["mismatches"] and not quiet:
            print("gdbio: WARNING — {} map/nm address mismatches (stale {}? rebuild: {})"
                  .format(len(xc["mismatches"]), mp, MAKE_LINE), file=sys.stderr)
    with open(cache_path(), "w") as f:
        json.dump(cache, f)
    return cache


def lookup(cache, spec):
    """Resolve `0xADDR`, a symbol name, or `SYM+OFF` (OFF hex/dec — struct-field reads
    like g_prefs+0x1c = UiPrefs.tiltLevel, theme.h:38-53) -> (addr, size|None, name).
    Ambiguous names (duplicate statics) are refused with the candidate list — use the
    address form. (+OFF added in slice E3 for the CTM closed loop's tilt observable.)"""
    if spec.lower().startswith("0x"):
        return int(spec, 16), None, spec
    if "+" in spec:
        base, off_s = spec.split("+", 1)
        addr, size, name = lookup(cache, base)
        try:
            off = int(off_s, 0)
        except ValueError:
            raise SystemExit("gdbio: bad offset '{}' in '{}'".format(off_s, spec))
        if size is not None and off >= size:
            print("gdbio: WARNING — offset 0x{:x} >= sizeof({})=0x{:x}".format(
                off, name, size), file=sys.stderr)
        return addr + off, None, spec
    entries = cache["symbols"].get(spec)
    if not entries:
        import difflib
        near = difflib.get_close_matches(spec, cache["symbols"].keys(), n=5)
        raise SystemExit("gdbio: symbol '{}' not in {} ({} symbols){}".format(
            spec, cache["elf"], len(cache["symbols"]),
            "; close: " + ", ".join(near) if near else ""))
    if len(entries) > 1:
        raise SystemExit("gdbio: '{}' is ambiguous ({} statics share the name): {} — "
                         "use the 0xADDR form".format(
                             spec, len(entries),
                             ", ".join("0x{:08x}[{}]".format(a, t) for a, t, _ in entries)))
    addr, _typ, size = entries[0]
    return addr, size, spec


# ================================================================================ broker
# `gdbio serve` owns THE RSP connection (one per boot — module doc). JSON-lines protocol
# on a unix socket; ops: status / halt / cont / read / detach. Auto-spawned by ensure_broker.

def _rsp_dead(rsp):
    """True when the emulator has closed the RSP socket. Non-blocking, halted or not:
    select for readability, then a recv of 0 bytes is EOF; anything else is a stray packet
    that goes into the reader (harmless — PacketReader skips acks and the halt drain /
    next _txn consume the rest)."""
    if rsp.sock is None:
        return True
    try:
        r, _w, _x = select.select([rsp.sock], [], [], 0)
    except OSError:
        return True
    if not r:
        return False
    try:
        data = rsp.sock.recv(4096)
    except socket.timeout:
        return False
    except OSError:
        return True
    if not data:
        return True
    rsp._reader.feed(data)
    return False


def _serve(args):
    sock_path = broker_sock_path()
    os.makedirs(state_dir(), exist_ok=True)
    if os.path.exists(sock_path):
        os.unlink(sock_path)         # stale socket from a dead broker (we are the spawner)
    rsp = RspClient(port=args.port, timeout=args.timeout)
    rsp.connect()
    # Known-state handshake: assert HALT once (idempotent; fresh boots are halted anyway)
    # and capture qSupported as the version-drift detector.
    stop = rsp.halt()
    qsup = rsp.qsupported()
    print("broker: connected to :{} stop={} qSupported={}".format(args.port, stop, qsup),
          flush=True)
    srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    srv.bind(sock_path)
    srv.listen(1)
    srv.settimeout(0.5)
    meta = {"port": args.port, "stop": stop, "qsupported": qsup,
            "connected_at": time.time(), "pid": os.getpid()}
    # Leave the app HALTED here: the caller decides when it runs (`gdbio resume`).
    try:
        while True:
            # EOF watch on the RSP socket while idle: emulator death ends the broker.
            # REVIEW FIX (2026-08-09): this used to be gated on `not rsp.halted`, so a
            # broker left HALTED (which is exactly what the handshake above, and
            # `verify-base` on an un-resumed boot, leave behind) NEVER noticed the
            # emulator dying. It outlived `azctl stop` — and the next boot's
            # `gdbio resume` then connected to the ZOMBIE, printed "resume: app running"
            # and returned 0 while the real emulator stayed parked. Reading here is
            # equivalent to leaving the bytes in the kernel buffer (the next _txn would
            # feed them to the same PacketReader), so the gate bought nothing.
            if _rsp_dead(rsp):
                print("broker: emulator closed the RSP socket — exiting", flush=True)
                return 0
            try:
                cli, _addr = srv.accept()
            except socket.timeout:
                continue
            with cli:
                f = cli.makefile("rwb")
                for line in f:
                    try:
                        req = json.loads(line.decode())
                        # Same EOF check per request: a client must never get a plausible
                        # answer (or a silent success) out of a broker whose emulator died
                        # between the idle check and this op.
                        if _rsp_dead(rsp):
                            f.write((json.dumps({
                                "ok": False,
                                "err": "emulator closed the RSP socket (azahar gone) — "
                                       "broker exiting; azctl boot to get a new session"
                            }) + "\n").encode())
                            f.flush()
                            print("broker: emulator closed the RSP socket — exiting",
                                  flush=True)
                            return 0
                        resp = _serve_one(rsp, meta, req)
                    except RspError as e:
                        resp = {"ok": False, "err": str(e)}
                    except (ValueError, KeyError) as e:
                        resp = {"ok": False, "err": "bad request: {}".format(e)}
                    f.write((json.dumps(resp) + "\n").encode())
                    f.flush()
                    if resp.get("bye"):
                        return 0
    finally:
        try:
            os.unlink(sock_path)
        except OSError:
            pass
        rsp.close()


def _serve_one(rsp, meta, req):
    op = req["op"]
    if op == "status":
        return {"ok": True, "halted": rsp.halted, **meta}
    if op == "halt":
        return {"ok": True, "stop": rsp.halt()}
    if op == "cont":
        rsp.cont()
        return {"ok": True}
    if op == "read":
        addr, n = int(req["addr"]), int(req["len"])
        was_halted = rsp.halted
        if not was_halted:
            rsp.halt()               # observation window: halt -> read -> restore
        try:
            data = rsp.read_mem(addr, n)
        finally:
            if not was_halted:
                rsp.cont()
        return {"ok": True, "hex": data.hex()}
    if op == "detach":
        # Resume FIRST — closing while halted freezes emulation forever (module doc
        # fact 5); after `c` the close just retires the stub for this boot.
        if rsp.halted:
            rsp.cont()
        rsp.close()
        return {"ok": True, "bye": True}
    return {"ok": False, "err": "unknown op {}".format(op)}


class Broker:
    """CLI-side handle: auto-spawns `gdbio serve` when needed, then JSON-lines ops."""

    def __init__(self, port=GDB_PORT, timeout=BROKER_TIMEOUT_S):
        self.port = port
        self.timeout = timeout
        self.sock = None

    def _try_connect(self):
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(self.timeout)
        s.connect(broker_sock_path())
        self.sock = s
        self.f = s.makefile("rwb")

    def connect(self, autostart=True):
        try:
            self._try_connect()
            return
        except OSError:
            pass
        if not autostart:
            raise RspError("no broker at {}".format(broker_sock_path()))
        try:
            os.unlink(broker_sock_path())     # stale socket from a dead broker
        except OSError:
            pass
        os.makedirs(state_dir(), exist_ok=True)
        with open(broker_log_path(), "a") as log:
            log.write("--- spawn {} ---\n".format(time.strftime("%Y-%m-%dT%H:%M:%SZ",
                                                                time.gmtime())))
            log.flush()
            subprocess.Popen(
                [sys.executable, os.path.abspath(__file__), "serve",
                 "--port", str(self.port)],
                stdout=log, stderr=log, start_new_session=True)
        deadline = time.time() + self.timeout
        last = None
        while time.time() < deadline:
            try:
                self._try_connect()
                return
            except OSError as e:
                last = e
                time.sleep(0.2)
        raise RspError(
            "broker did not come up in {:.0f}s ({}) — see {} (likely: no Azahar, or this "
            "boot's one gdb client is already spent -> azctl boot again)".format(
                self.timeout, last, broker_log_path()))

    def op(self, **req):
        self.f.write((json.dumps(req) + "\n").encode())
        self.f.flush()
        line = self.f.readline()
        if not line:
            raise RspError("broker closed the connection mid-op")
        resp = json.loads(line.decode())
        if not resp.get("ok"):
            raise RspError(resp.get("err", "broker error"))
        return resp

    def read_mem(self, addr, n):
        return bytes.fromhex(self.op(op="read", addr=addr, len=n)["hex"])

    def close(self):
        if self.sock is not None:
            try:
                self.sock.close()
            finally:
                self.sock = None


def broker_session(args, autostart=True):
    b = Broker(port=args.port)
    b.connect(autostart=autostart)
    return b


# ============================================================================ verify-base
# H3.3: three anchors spanning .text/.rodata/.data, each byte-compared against the ELF.
# Candidate lists tolerate build drift; the fallback scan picks the lowest-addressed
# symbol of the right type with a file-backed window. `.bss` never qualifies (no image).
VERIFY_ANCHORS = [
    # (label, preferred symbols, read length or None=symbol size, nm types)
    ("text",   ["ctl_log_line"],             64,   "tT"),
    ("rodata", ["AUDIO_NAMES"],              64,   "rR"),
    # data: mGBA's _deadbeef (memory.c:28) = known word 0xE710B710, never written at
    # runtime; g_appActive (main.c:85 `= true`) = 1 while foregrounded. Both hold on a
    # RUNNING app — most .data is runtime-mutated, so anchor choice matters here.
    ("data",   ["_deadbeef", "g_appActive"], None, "dD"),
]


def _pick_anchor(cache, elf, names, length, types):
    for name in names:
        entries = cache["symbols"].get(name)
        if not entries or len(entries) != 1:
            continue
        addr, typ, size = entries[0]
        if typ not in types:
            continue
        n = length or max(1, min(size or 4, 64))
        if elf.bytes_at(addr, n) is not None:
            return name, addr, n, False
    # Build drift fallback: lowest-addressed symbol of the right type, >=4 file-backed bytes.
    best = None
    for name, entries in cache["symbols"].items():
        if len(entries) != 1:
            continue
        addr, typ, size = entries[0]
        if typ in types and (size or 0) >= 4 and elf.bytes_at(addr, length or 4) is not None:
            if best is None or addr < best[1]:
                best = (name, addr, length or max(1, min(size, 64)))
    if best:
        return best[0], best[1], best[2], True
    raise SystemExit("gdbio: no usable anchor of type [{}] — ELF/nm mismatch?".format(types))


def cmd_verify_base(args):
    cache = build_symtab()
    elf = ElfImage(elf_path())
    # The .3dsx Azahar runs and the .elf we compare against must be the same build:
    try:
        drift = abs(os.path.getmtime(elf_path()) - os.path.getmtime(app_path()))
        if drift > 120:
            print("gdbio: WARNING — 3DGBA.elf and 3DGBA.3dsx mtimes differ by {:.0f}s; "
                  "a stale pair would fake a slide. Rebuild: {}".format(drift, MAKE_LINE),
                  file=sys.stderr)
    except OSError:
        pass
    b = broker_session(args)
    was_halted = b.op(op="status")["halted"]
    if not was_halted:
        b.op(op="halt")
    ok = True
    rows = []
    try:
        for label, names, length, types in VERIFY_ANCHORS:
            name, addr, n, fell_back = _pick_anchor(cache, elf, names, length, types)
            want = elf.bytes_at(addr, n)
            got = b.read_mem(addr, n)
            match = want == got
            ok &= match
            note = " (FALLBACK anchor — preferred symbol missing, build drift?)" \
                if fell_back else ""
            if match:
                detail = "MATCH"
                if label == "data" and n >= 4:
                    detail += "  word=0x{:08x}".format(struct.unpack_from("<I", got)[0])
            else:
                first = next(i for i in range(n) if want[i] != got[i])
                detail = ("MISMATCH @+0x{:x}: elf={} gdb={}".format(
                    first, want[first:first + 8].hex(), got[first:first + 8].hex()))
                if label == "data":
                    detail += ("  [note: a lone data mismatch can be runtime mutation, "
                               "not a slide — retry on a fresh, un-resumed boot]")
            rows.append("  {:<7} {:<24} 0x{:08x} {:>3}B  {}{}".format(
                label, name, addr, n, detail, note))
    finally:
        if not was_halted:
            b.op(op="cont")
        b.close()
    print("verify-base: map/nm addresses vs the RUNNING app (S2.6 offset-zero claim):")
    for r in rows:
        print(r)
    print("verify-base: {}".format("PASS — .3dsx loaded at link-time addresses, no slide"
                                   if ok else "FAIL — see rows above; do NOT trust symbol "
                                   "reads until resolved (H3.3)"))
    return 0 if ok else 1


# ============================================================================== commands
def cmd_syms(args):
    cache = build_symtab(refresh=args.refresh)
    by_type = {}
    for entries in cache["symbols"].values():
        for _a, t, _s in entries:
            by_type[t] = by_type.get(t, 0) + 1
    total = sum(by_type.values())
    print("elf: {} (mtime {})".format(
        cache["elf"], time.strftime("%Y-%m-%d %H:%M:%S",
                                    time.localtime(cache["elf_mtime"]))))
    print("symbols: {} ({})".format(
        total, ", ".join("{}:{}".format(t, n) for t, n in sorted(by_type.items()))))
    if cache.get("map"):
        xc = cache["map"]["crosscheck"]
        print("map cross-check: {} shared symbols checked, {} mismatches ({})".format(
            xc["checked"], len(xc["mismatches"]), cache["map"]["path"]))
        for m in xc["mismatches"][:10]:
            print("  MISMATCH {}: map=0x{:08x} nm=0x{:08x}".format(
                m["name"], m["map"], m["nm"]))
    else:
        print("map cross-check: {} absent (nm-only; regenerate: {})".format(
            map_path(), MAKE_LINE))
    if args.lookup:
        entries = cache["symbols"].get(args.lookup, [])
        for a, t, s in entries:
            print("{} = 0x{:08x}  type {}  size {}".format(
                args.lookup, a, t, "0x{:x}".format(s) if s is not None else "?"))
        if not entries:
            print("{}: not found".format(args.lookup))
            return 1
    return 0


HALTED_NOTE = ("[HALTED — the emulator has executed no instructions since this value was "
               "read; run `gdbio resume` first if you wanted LIVE state]")


def _halted_warning(b):
    """REVIEW FIX (2026-08-09): reading a HALTED target is legitimate (verify-base does it
    on purpose), but a read as the FIRST gdbio command after `azctl boot` used to be a
    silent trap: the broker handshake halts and deliberately leaves the app halted, and
    unlike cmd_poll the read commands had no guard — so `read-u32 g_renderSeq` printed
    `= 0` and `read-u8 g_appActive` printed `= 1` (main.c:85's ELF initialiser) with exit
    0, while the emulator stayed parked forever. Reads now carry the caveat in BOTH the
    stdout line and a stderr warning. Returns a suffix for the printed line."""
    try:
        if not b.op(op="status")["halted"]:
            return ""
    except RspError:
        return ""
    print("gdbio: WARNING — the app is HALTED. These bytes are whatever was last in "
          "memory (on a fresh boot: the ELF's compile-time initialisers, because the "
          "release parks every use_gdbstub boot pre-first-instruction — module doc fact "
          "1). Run `tools/emutest/run gdbio resume` for live state.", file=sys.stderr)
    return "  " + HALTED_NOTE


def _hexdump(addr, data):
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        hexs = " ".join("{:02x}".format(b) for b in chunk)
        asc = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        print("0x{:08x}  {:<47}  |{}|".format(addr + i, hexs, asc))


def cmd_read(args):
    cache = build_symtab()
    addr, _size, name = lookup(cache, args.what)
    b = broker_session(args)
    try:
        note = _halted_warning(b)
        data = b.read_mem(addr, args.length)
    finally:
        b.close()
    if args.out:
        with open(args.out, "wb") as f:
            f.write(data)
        print("{} @0x{:08x}: {} bytes -> {}".format(name, addr, len(data), args.out))
    if note:
        print("{} @0x{:08x}{}".format(name, addr, note))
    _hexdump(addr, data)
    return 0


def _read_int(args, spec, width):
    cache = build_symtab()
    addr, _size, name = lookup(cache, spec)
    b = broker_session(args)
    try:
        note = _halted_warning(b)
        data = b.read_mem(addr, width)
    finally:
        b.close()
    return name, addr, int.from_bytes(data, "little"), note


def cmd_read_u32(args):
    name, addr, val, note = _read_int(args, args.what, 4)
    print("{} @0x{:08x} = {} (0x{:08x}){}".format(name, addr, val, val, note))
    return 0


def cmd_read_u8(args):
    name, addr, val, note = _read_int(args, args.what, 1)
    print("{} @0x{:08x} = {} (0x{:02x}){}".format(name, addr, val, val, note))
    return 0


def cmd_poll(args):
    """H3.4's liveness primitive. Each sample is a broker halt->read->cont blink (a held
    halt would freeze the very counter we poll); the app runs between samples."""
    cache = build_symtab()
    addr, _size, name = lookup(cache, args.symbol)
    b = broker_session(args)
    try:
        if b.op(op="status")["halted"]:
            # A halted app cannot change memory — poll implies the app should be running.
            b.op(op="cont")
            print("poll: app was halted — resumed it first (a halted app can't change)")
        deadline = time.time() + args.timeout
        first = None
        n = 0
        while True:
            val = int.from_bytes(b.read_mem(addr, args.width), "little")
            n += 1
            if first is None:
                first = val
                print("poll {} @0x{:08x}: sample1 = {}".format(name, addr, val))
                if args.expect is not None and val == args.expect:
                    print("poll: PASS — expected value {} on first sample".format(val))
                    return 0
            else:
                if args.changed and val != first:
                    print("poll: PASS — changed {} -> {} after {} samples".format(
                        first, val, n))
                    return 0
                if args.expect is not None and val == args.expect:
                    print("poll: PASS — reached expected {} after {} samples".format(val, n))
                    return 0
            if time.time() >= deadline:
                print("poll: FAIL — {} still {} after {} samples / {}s (wanted {})".format(
                    name, val, n, args.timeout,
                    "change from {}".format(first) if args.changed else args.expect))
                return 1
            time.sleep(args.interval / 1000.0)
    finally:
        b.close()


def cmd_resume(args):
    """REQUIRED once after every `azctl boot`: with use_gdbstub=true the release parks
    the whole emulator pre-first-instruction until a client connects, and keeps it halted
    until `c` (module doc fact 1). Auto-spawns the broker (which connects + re-asserts
    the halt), then continues. Idempotent while running."""
    b = broker_session(args)
    try:
        st = b.op(op="status")
        if st["halted"]:
            b.op(op="cont")
            print("resume: app running (was halted; broker qSupported={})".format(
                st["qsupported"]))
        else:
            print("resume: already running")
    finally:
        b.close()
    return 0


def cmd_status(args):
    try:
        b = broker_session(args, autostart=False)
    except RspError:
        print("broker=none (no {} — first gdbio command will spawn it)".format(
            broker_sock_path()))
        return 0
    try:
        st = b.op(op="status")
        print("broker=up pid={} halted={} connected_for={:.0f}s".format(
            st["pid"], st["halted"], time.time() - st["connected_at"]))
        print("stub qSupported={}".format(st["qsupported"]))
        print("stub stop-reason-at-connect={}".format(st["stop"]))
    finally:
        b.close()
    return 0


def cmd_detach(args):
    """Resume the app and retire this boot's gdb access (broker exits, TCP closes).
    After this the app runs client-less; a fresh `azctl boot` restores gdb (fact 5)."""
    try:
        b = broker_session(args, autostart=False)
    except RspError:
        print("detach: no broker running — nothing to detach")
        return 0
    try:
        b.op(op="detach")
    finally:
        b.close()
    print("detach: app resumed, RSP socket closed, broker exited — gdb is now spent for "
          "this boot (release accepts one client per boot; azctl boot to re-arm)")
    return 0


def cmd_serve(args):
    return _serve(args)


# ================================================================================= main
def main(argv=None):
    ap = argparse.ArgumentParser(
        description="GDB RSP client + symbol lookup for the 3DGBA app under Azahar "
                    "2125.1.2 (SPEC-harness H2.2; release dialect — see module doc)")
    # `common` = the per-op RSP timeout; `port_only` exists because `poll`'s --timeout is a
    # WALL-CLOCK DEADLINE, not a per-op timeout (see below).
    port_only = argparse.ArgumentParser(add_help=False)
    port_only.add_argument("--port", type=int, default=GDB_PORT,
                           help="gdb stub TCP port (default {})".format(GDB_PORT))
    common = argparse.ArgumentParser(add_help=False, parents=[port_only])
    common.add_argument("--timeout", type=float, default=RSP_TIMEOUT_S,
                        help="per-op timeout seconds (default {})".format(RSP_TIMEOUT_S))
    sub = ap.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("syms", parents=[common],
                       help="(re)build the nm/.map symbol cache")
    s.add_argument("--refresh", action="store_true", help="force rebuild")
    s.add_argument("--lookup", metavar="NAME", help="print a symbol's entries")
    s.set_defaults(fn=cmd_syms)

    s = sub.add_parser("resume", parents=[common],
                       help="start/resume emulation (REQUIRED once after azctl boot)")
    s.set_defaults(fn=cmd_resume)

    s = sub.add_parser("verify-base", parents=[common],
                       help="prove map/nm addresses == gdb addresses (H3.3)")
    s.set_defaults(fn=cmd_verify_base)

    s = sub.add_parser("read", parents=[common], help="hex+ASCII dump of memory")
    s.add_argument("what", metavar="SYM|0xADDR")
    s.add_argument("length", type=lambda v: int(v, 0))
    s.add_argument("--out", metavar="F.bin", help="also write raw bytes")
    s.set_defaults(fn=cmd_read)

    s = sub.add_parser("read-u32", parents=[common], help="read a little-endian u32")
    s.add_argument("what", metavar="SYM|0xADDR")
    s.set_defaults(fn=cmd_read_u32)

    s = sub.add_parser("read-u8", parents=[common],
                       help="read one byte (g_quit/g_appActive are 1-byte bools)")
    s.add_argument("what", metavar="SYM|0xADDR")
    s.set_defaults(fn=cmd_read_u8)

    # REVIEW FIX (2026-08-09): poll used to inherit `common`'s --timeout, whose default is
    # the PER-RSP-OP timeout (5.0 s). That made the documented liveness one-liner
    # (`gdbio poll g_renderSeq --changed`) FAIL on a perfectly healthy boot: E2 measured
    # resume -> render loop at 5-10 s, so the deadline expired first (live: FAIL after 6
    # samples / 5.0 s, then PASS with --timeout 30 on the same boot). poll now owns its
    # --timeout as a WALL-CLOCK DEADLINE with a default that matches the measured boot.
    s = sub.add_parser("poll", parents=[port_only],
                       help="sample a value until it changes / matches (H3.4 liveness)")
    s.add_argument("symbol", metavar="SYM|0xADDR")
    g = s.add_mutually_exclusive_group(required=True)
    g.add_argument("--changed", action="store_true",
                   help="pass when the value differs from the first sample")
    g.add_argument("--expect", type=lambda v: int(v, 0), metavar="V",
                   help="pass when the value equals V")
    s.add_argument("--width", type=int, choices=[1, 2, 4], default=4)
    s.add_argument("--interval", type=int, default=500, metavar="MS")
    s.add_argument("--timeout", type=float, default=POLL_TIMEOUT_S, metavar="S",
                   help="WALL-CLOCK deadline for the whole poll, seconds (default {}; the "
                        "app needs ~5-10 s after `resume` to reach its render loop)"
                        .format(POLL_TIMEOUT_S))
    s.set_defaults(fn=cmd_poll)

    s = sub.add_parser("status", parents=[common], help="broker + stub state")
    s.set_defaults(fn=cmd_status)

    s = sub.add_parser("detach", parents=[common],
                       help="resume + close the RSP session (gdb spent until next boot)")
    s.set_defaults(fn=cmd_detach)

    s = sub.add_parser("serve", parents=[common],
                       help="the broker daemon (auto-spawned; manual runs for debugging)")
    s.set_defaults(fn=cmd_serve)

    args = ap.parse_args(argv)
    try:
        return args.fn(args)
    except RspError as e:
        print("gdbio: FAIL — {}".format(e), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
