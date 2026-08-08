#!/usr/bin/env python3
"""ctm.py — CTM TAS-movie synthesizer + inspector for Azahar (phase-16 slice E3;
SPEC-harness H2.3 "tests as data", SPEC-protocols S1).

FORMAT SOURCE — verified against the INSTALLED release, not just master
=======================================================================
SPEC-protocols S1 was researched from Azahar master@5394625. Slice E2 proved the installed
release 2125.1.2 can diverge (its gdbstub does, badly), so E3 re-fetched the movie/HID code
from the RELEASE TAG `2125.1.2` (raw.githubusercontent.com/azahar-emu/azahar, 2026-08-08:
`src/core/movie.cpp` 715 lines, `src/core/hle/service/hid/hid.cpp` 729 lines) and compared
byte-relevant facts one by one. RESULT: the release matches S1 EXACTLY — every citation
below is release-verified (movie.cpp line numbers are the release file's):

  - magic {'C','T','M',0x1B} (movie.cpp:110), CTMHeader 256 B #pragma pack(1)
    (movie.cpp:114-129, static_assert 256), ControllerState 7 B (movie.cpp:39-107,
    static_assert 7). Field order/offsets == S1.1/S1.2.
  - Button bit order in pad_and_circle.hex: A0 B1 SELECT2 START3 RIGHT4 LEFT5 UP6 DOWN7
    R8 L9 X10 Y11 DEBUG12 GPIO14_13 (movie.cpp:47-61). ZL/ZR are NOT here (ir:rst).
  - Touch record: u16_le x, u16_le y, u8 valid, 1 pad byte — native BOTTOM-SCREEN pixels:
    hid.cpp:292-294 writes x*kScreenBottomWidth(320), y*kScreenBottomHeight(240) then
    Movie().HandleTouchStatus records exactly those values (hid.cpp:296).
  - Stream = strictly alternating [PadAndCircle][Touch] pairs at 234 Hz: the HID pad poll
    (pad_update_ticks = BASE_CLOCK_RATE_ARM11/234, hid.h:342) handles pad THEN touch in the
    same callback (hid.cpp:243 -> 296; artic path 163 -> 203); a wrong type logs
    "Expected to read type" and desyncs (movie.cpp:243-247, 274-278).
    Accel(104 Hz)/gyro records interleave ONLY if the game enables those sensors
    (hid.cpp:427-458, 488-519) — 3DGBA never does (no accelerometer/gyro/irrst init in
    source/, probed by grep), so pure pairs are correct for this app.
  - Playback: StartPlayback rejects ONLY a bad magic (ValidateHeader, movie.cpp:463-478:
    revision mismatch = warning "created on a different version of Citra", still plays;
    program_id is NEVER checked). CLI -p (citra_qt.cpp:1552-1555 path) calls no
    ValidateMovie and pops no dialog. Success/end log anchors: "Loaded Movie, ID:"
    (movie.cpp:551) and "Playback finished" (movie.cpp:231). Ends by byte exhaustion,
    then live input takes over.
  - Clock/ticks: PrepareForPlayback loads header.clock_init_time/timing_base_ticks
    (movie.cpp:597-604) and they OVERRIDE the INI [System] pins during playback — put the
    pinned epoch in the header (defaults below match azctl's profile pins).
  - Frame<->poll conversion for humans: input index = round(input / 234.0 *
    SCREEN_REFRESH_RATE) (movie.cpp:222-227); SCREEN_REFRESH_RATE = 268111856/4481136
    = 59.83122 Hz => 3.911001 polls per frame. (Corrected 2026-08-09: the decimals here
    and in SPEC-protocols S1.5 were off in the 4th significant digit; re-derived from the
    two constants this file already cites. The code path uses the EXACT Fractions below,
    so no synthesized bytes ever changed — tests/test_ctm.py pins both.)

TIMELINE SCRIPTS (tests as data — the PokeDNA JSON-script rule, H2.3)
=====================================================================
`ctm make script.json out.ctm` compiles a JSON list of ops into a movie; times are in
FRAMES at the 3DS refresh (59.83122 Hz) unless the op says polls. Deterministic: same JSON
-> same bytes (golden-tested, tests/test_ctm.py).

  ["wait", n]                   advance n frames, keeping whatever is currently held
  ["tap", KEYS]                 press KEYS for 8 frames, release, wait 8 (the PokeDNA
  ["tap", KEYS, hold, rel]        edge-latch rule: games latch on the 0->1 edge)
  ["hold", KEYS, n]             KEYS go down and STAY down; advances n frames
  ["release"]                   all keys up + touch up (0 duration — follow with a wait)
  ["touch", x, y]               touch-tap native bottom-screen (x,y) 8 frames + 8 release
  ["touch", x, y, hold, rel]      (x 0..319, y 0..239 — S1.4 range rule)
  ["touch_hold", x, y, n]       touch down / drag-move, stays down; advances n frames
  ["touch_release"]             touch up (0 duration)
  ["wait_polls", n]             POLL-exact variants (the S1.11 golden test is written in
  ["hold_polls", KEYS, n]         these; 1 poll = 1 [Pad][Touch] pair = 14 bytes; keys
  ["touch_polls", x, y, n]        auto-release after the exact poll count)

KEYS = "+"-joined names from the movie bit table: A B SELECT START RIGHT LEFT UP DOWN R L
X Y DEBUG GPIO14 (e.g. "START+SELECT"). ZL/ZR do not exist in this record type.

A held state shorter than one poll (~1/4 frame) emits 0 records — the console would never
see it; `make` fails loudly on such a segment instead of writing a dead press.

CLI:
  ctm make script.json out.ctm [--revision HEX40] [--author S] [--init-time N] [--ticks N]
  ctm inspect f.ctm [--json]
Exit codes (H2): 0 ok, 1 fail. Never SKIPs.
"""

import argparse
import json
import struct
import sys
from fractions import Fraction

# ---- rates (release core/core_timing.h:37,42 — same values as master S1.5) -----------------
BASE_CLOCK_RATE_ARM11 = 268111856
FRAME_CYCLES = 4481136                     # SCREEN_REFRESH_RATE = BASE/FRAME_CYCLES = 59.83122
PAD_HZ = 234                               # hid.h:342 pad_update_ticks = BASE/234
# polls per frame = 234 / (BASE/FRAME_CYCLES) — kept EXACT as a Fraction so poll counts are
# deterministic (cumulative rounding, no float drift): = 65536614/16756991 ~= 3.911001
POLLS_PER_FRAME = Fraction(PAD_HZ * FRAME_CYCLES, BASE_CLOCK_RATE_ARM11)
# The refresh rate as an exact Fraction too, so the human-facing seconds figure printed by
# `ctm make` is derived from the same constants instead of a hand-typed decimal.
SCREEN_REFRESH_HZ = Fraction(BASE_CLOCK_RATE_ARM11, FRAME_CYCLES)

MAGIC = b"CTM\x1b"                         # movie.cpp:110 header_magic_bytes

# Button bit order — movie.cpp:47-61 (BitField positions), == HID PadState order.
KEY_BITS = {
    "A": 0, "B": 1, "SELECT": 2, "START": 3, "RIGHT": 4, "LEFT": 5, "UP": 6, "DOWN": 7,
    "R": 8, "L": 9, "X": 10, "Y": 11, "DEBUG": 12, "GPIO14": 13,
}

# Header defaults — chosen to match azctl's determinism profile (S3.3): the movie header
# OVERRIDES the INI during playback (movie.cpp:597-604 + core.cpp), so the same epoch/ticks
# keep movie and non-movie boots on the same clock.
DEF_INIT_TIME = 946684800                  # 2000-01-01T00:00:00Z (S1.11 golden epoch)
DEF_BASE_TICKS = 1000                      # any fixed value >= 0 overrides (core_timing.cpp)
DEF_ID = 0x0123456789ABCDEF               # only pairs savestates with movies — fixed is fine
DEF_AUTHOR = "3dgba-emutest"
DEF_REVISION = b"\x00" * 20                # zeros => one-line "different version" WARNING in
                                           # the log, playback unaffected (movie.cpp:469-475);
                                           # --revision stamps a harvested build hash instead.

TAP_HOLD_F = 8                             # the PokeDNA hold-8/release-8 edge-latch rule
TAP_REL_F = 8


class CtmError(Exception):
    pass


def parse_keys(spec):
    """'START+SELECT' -> 0x000C. Names are the movie bit table's, case-insensitive."""
    mask = 0
    for name in str(spec).split("+"):
        n = name.strip().upper()
        if n not in KEY_BITS:
            raise CtmError("unknown key '{}' (valid: {})".format(
                name, " ".join(sorted(KEY_BITS, key=KEY_BITS.get))))
        mask |= 1 << KEY_BITS[n]
    return mask


def keys_name(mask):
    names = [n for n, b in sorted(KEY_BITS.items(), key=lambda kv: kv[1]) if mask & (1 << b)]
    return "+".join(names) if names else "idle"


def _check_touch(x, y):
    # S1.4: native bottom-screen coordinates; 0..319/0..239 stays inside the
    # u16(norm*320/240) range the local-input path itself produces.
    if not (0 <= int(x) <= 319 and 0 <= int(y) <= 239):
        raise CtmError("touch ({},{}) out of native bottom-screen range 0..319/0..239".format(x, y))
    return int(x), int(y)


class Timeline:
    """Compiles ops into poll states. Time is tracked in frames as an exact Fraction; the
    emitted poll count always equals round(total_frames * POLLS_PER_FRAME) (cumulative
    rounding — segment lengths never drift from the ideal timeline)."""

    def __init__(self):
        self.frames = Fraction(0)          # total frame time consumed
        self.polls = []                    # list of (hex_mask, (tx, ty, tvalid))
        self.keys = 0
        self.touch = (0, 0, 0)

    def _target_polls(self):
        # round-half-up on an exact rational (deterministic; no banker's surprises)
        t = self.frames * POLLS_PER_FRAME
        return int(t + Fraction(1, 2))

    def advance_frames(self, n, what):
        if n < 0:
            raise CtmError("negative duration in {}".format(what))
        self.frames += Fraction(n)
        need = self._target_polls() - len(self.polls)
        self._emit(need, what)

    def advance_polls(self, n, what):
        if n < 0:
            raise CtmError("negative poll count in {}".format(what))
        self._emit(int(n), what)
        # keep the frame clock in sync so mixed frame/poll scripts stay monotonic
        self.frames = Fraction(len(self.polls)) / POLLS_PER_FRAME

    def _emit(self, n, what):
        if n <= 0 and (self.keys or self.touch[2]):
            # A press/touch shorter than one poll would write ZERO records — the console
            # would never see it. Loud failure instead of a silently dead input (H2.3).
            raise CtmError("segment '{}' lasts < 1 poll (~0.26 frames) — nothing would be "
                           "recorded; lengthen it".format(what))
        for _ in range(max(0, n)):
            self.polls.append((self.keys, self.touch))


def compile_script(script):
    """JSON op list -> Timeline. Raises CtmError with the op index on any bad op."""
    if not isinstance(script, list):
        raise CtmError("script must be a JSON list of ops")
    tl = Timeline()
    for i, op in enumerate(script):
        try:
            if not isinstance(op, list) or not op:
                raise CtmError("op must be a non-empty list")
            name = op[0]
            if name == "wait":
                tl.advance_frames(int(op[1]), "wait")
            elif name == "tap":
                hold = int(op[2]) if len(op) > 2 else TAP_HOLD_F
                rel = int(op[3]) if len(op) > 3 else TAP_REL_F
                m = parse_keys(op[1])
                tl.keys |= m
                tl.advance_frames(hold, "tap hold")
                tl.keys &= ~m
                tl.advance_frames(rel, "tap release")
            elif name == "hold":
                tl.keys |= parse_keys(op[1])
                tl.advance_frames(int(op[2]), "hold")
            elif name == "release":
                tl.keys = 0
                tl.touch = (0, 0, 0)
            elif name == "touch":
                x, y = _check_touch(op[1], op[2])
                hold = int(op[3]) if len(op) > 3 else TAP_HOLD_F
                rel = int(op[4]) if len(op) > 4 else TAP_REL_F
                tl.touch = (x, y, 1)
                tl.advance_frames(hold, "touch hold")
                tl.touch = (0, 0, 0)
                tl.advance_frames(rel, "touch release")
            elif name == "touch_hold":
                x, y = _check_touch(op[1], op[2])
                tl.touch = (x, y, 1)
                tl.advance_frames(int(op[3]), "touch_hold")
            elif name == "touch_release":
                tl.touch = (0, 0, 0)
            elif name == "wait_polls":
                tl.advance_polls(int(op[1]), "wait_polls")
            elif name == "hold_polls":
                m = parse_keys(op[1])
                tl.keys |= m
                tl.advance_polls(int(op[2]), "hold_polls")
                tl.keys &= ~m
            elif name == "touch_polls":
                x, y = _check_touch(op[1], op[2])
                tl.touch = (x, y, 1)
                tl.advance_polls(int(op[3]), "touch_polls")
                tl.touch = (0, 0, 0)
            else:
                raise CtmError("unknown op '{}'".format(name))
        except (IndexError, TypeError, ValueError) as e:
            raise CtmError("op {} {}: {}".format(i, op, e))
        except CtmError as e:
            raise CtmError("op {} {}: {}".format(i, op, e))
    if not tl.polls:
        raise CtmError("script produced an empty movie")
    return tl


# ---- byte layer (offsets per S1.1/S1.2 == release movie.cpp structs) ------------------------
def pack_header(input_count, revision=DEF_REVISION, author=DEF_AUTHOR,
                init_time=DEF_INIT_TIME, base_ticks=DEF_BASE_TICKS,
                movie_id=DEF_ID, program_id=0, rerecord=1):
    if len(revision) != 20:
        raise CtmError("revision must be exactly 20 bytes (40 hex chars)")
    a = author.encode("ascii")
    if len(a) > 32:
        raise CtmError("author > 32 bytes")
    # program_id = 0 for a .3dsx: the 3DSX loader creates its CodeSet with program id 0 and
    # never overrides ReadProgramId (release 3dsx.cpp; S1.10) — and playback never checks it.
    return struct.pack("<4sQ20sQQ32sIQq156s", MAGIC, program_id, revision,
                       init_time, movie_id, a.ljust(32, b"\x00"), rerecord,
                       input_count, base_ticks, b"\x00" * 156)


def pack_pad(hex_mask, cpx=0, cpy=0):
    # type 0 + u16 hex + s16 circle x/y (movie.cpp pad_and_circle). Circle stays centered
    # (0) for button scripts — the live-input range is ~±0x9C (S1.3).
    return struct.pack("<BHhh", 0, hex_mask & 0xFFFF, cpx, cpy)


def pack_touch(x, y, valid):
    # type 1 + u16 x + u16 y + u8 valid + 1 explicit pad byte to 7 (movie.cpp touch)
    return struct.pack("<BHHBB", 1, x, y, 1 if valid else 0, 0)


def movie_bytes(tl, **hdr_kw):
    body = bytearray()
    for keys, (tx, ty, tv) in tl.polls:
        body += pack_pad(keys)             # pad FIRST, touch SECOND — hid.cpp:243->296
        body += pack_touch(tx, ty, tv)
    # input_count counts PadAndCircle records ONLY (GetInputCount, movie.cpp:131-149)
    return pack_header(len(tl.polls), **hdr_kw) + bytes(body)


# ---- inspect --------------------------------------------------------------------------------
def parse_movie(data):
    """-> (header dict, runs list, problems list). A run is a maximal span of identical
    poll state: {start_poll, polls, keys, keys_name, touch:[x,y,valid]}."""
    problems = []
    if len(data) < 256:
        raise CtmError("file shorter than the 256-byte header")
    magic, program_id, revision, init_time, movie_id, author, rerecord, input_count, \
        base_ticks, _res = struct.unpack("<4sQ20sQQ32sIQq156s", data[:256])
    if magic != MAGIC:
        problems.append("bad magic {!r} (want {!r}) — Azahar would reject this file".format(
            magic, MAGIC))
    hdr = {
        "program_id": program_id,
        "revision": revision.hex(),
        "clock_init_time": init_time,
        "id": "0x{:016X}".format(movie_id),
        "author": author.rstrip(b"\x00").decode("ascii", "replace"),
        "rerecord_count": rerecord,
        "input_count": input_count,
        "timing_base_ticks": base_ticks,
    }
    body = data[256:]
    if len(body) % 7:
        problems.append("{} trailing bytes (not a whole 7-byte record; Azahar ignores "
                        "them)".format(len(body) % 7))
    n = len(body) // 7
    pad_records = 0
    runs = []
    poll = 0
    i = 0
    while i + 1 < n or (i < n and n % 2):  # walk records; pairs expected
        off = 256 + i * 7
        t0 = body[i * 7]
        if t0 != 0:
            problems.append("record {} @0x{:x}: type {} where PadAndCircle(0) expected — "
                            "playback would desync here".format(i, off, t0))
            break
        hexm, _cpx, _cpy = struct.unpack_from("<Hhh", body, i * 7 + 1)
        pad_records += 1
        if i + 1 >= n:
            problems.append("dangling Pad record without its Touch partner at the end")
            break
        t1 = body[(i + 1) * 7]
        if t1 != 1:
            problems.append("record {} @0x{:x}: type {} where Touch(1) expected — "
                            "playback would desync here".format(i + 1, off + 7, t1))
            break
        tx, ty, tv = struct.unpack_from("<HHB", body, (i + 1) * 7 + 1)
        state = (hexm, tx, ty, 1 if tv else 0)
        if runs and (runs[-1]["keys"], runs[-1]["touch"][0], runs[-1]["touch"][1],
                     runs[-1]["touch"][2]) == state:
            runs[-1]["polls"] += 1
        else:
            runs.append({"start_poll": poll, "polls": 1, "keys": hexm,
                         "keys_name": keys_name(hexm), "touch": [tx, ty, 1 if tv else 0]})
        poll += 1
        i += 2
    if pad_records != input_count:
        problems.append("header input_count={} but {} Pad records in the body (cosmetic "
                        "on the CLI path — ValidateMovie never runs there)".format(
                            input_count, pad_records))
    return hdr, runs, problems


def cmd_make(args):
    with open(args.script) as f:
        try:
            script = json.load(f)
        except ValueError as e:
            print("ctm: FAIL — {} is not valid JSON: {}".format(args.script, e),
                  file=sys.stderr)
            return 1
    revision = DEF_REVISION
    if args.revision:
        try:
            revision = bytes.fromhex(args.revision)
        except ValueError:
            revision = b""
        if len(revision) != 20:
            print("ctm: FAIL — --revision wants exactly 40 hex chars", file=sys.stderr)
            return 1
    try:
        tl = compile_script(script)
        data = movie_bytes(tl, revision=revision, author=args.author,
                           init_time=args.init_time, base_ticks=args.ticks)
    except CtmError as e:
        print("ctm: FAIL — {}".format(e), file=sys.stderr)
        return 1
    with open(args.out, "wb") as f:
        f.write(data)
    frames = float(Fraction(len(tl.polls)) / POLLS_PER_FRAME)
    print("ctm: wrote {} — {} bytes, {} polls (~{:.1f} frames ~{:.1f} s emulated), "
          "revision {}".format(args.out, len(data), len(tl.polls), frames, frames / float(SCREEN_REFRESH_HZ),
                               "zeros (desync warning, plays fine)" if revision == DEF_REVISION
                               else revision.hex()[:12] + "…"))
    return 0


def cmd_inspect(args):
    with open(args.movie, "rb") as f:
        data = f.read()
    try:
        hdr, runs, problems = parse_movie(data)
    except CtmError as e:
        print("ctm: FAIL — {}".format(e), file=sys.stderr)
        return 1
    if args.json:
        print(json.dumps({"header": hdr, "runs": runs, "problems": problems}, indent=1))
        return 1 if problems else 0
    print("header:")
    for k, v in hdr.items():
        print("  {:<18} {}".format(k, v))
    print("timeline ({} runs; poll = 1/234 s, {:.6f} polls/frame):".format(
        len(runs), float(POLLS_PER_FRAME)))
    for r in runs:
        t = r["touch"]
        desc = r["keys_name"]
        if t[2]:
            desc += "  touch({},{})".format(t[0], t[1])
        print("  poll {:>6} +{:<6} (~{:>7.1f}f +{:<6.1f}) {}".format(
            r["start_poll"], r["polls"],
            float(Fraction(r["start_poll"]) / POLLS_PER_FRAME),
            float(Fraction(r["polls"]) / POLLS_PER_FRAME), desc))
    for p in problems:
        print("PROBLEM: {}".format(p))
    return 1 if problems else 0


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="CTM movie synthesizer/inspector for Azahar 2125.1.2 "
                    "(format release-verified; see module doc)")
    sub = ap.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("make", help="compile a JSON timeline into a .ctm")
    s.add_argument("script", help="JSON op list (see module doc)")
    s.add_argument("out", help="output .ctm path")
    s.add_argument("--revision", metavar="HEX40",
                   help="stamp a 40-hex build hash (default zeros: warning-only)")
    s.add_argument("--author", default=DEF_AUTHOR)
    s.add_argument("--init-time", type=int, default=DEF_INIT_TIME,
                   help="header clock_init_time, seconds since 1970 (overrides the INI "
                        "during playback; default {} = the profile pin)".format(DEF_INIT_TIME))
    s.add_argument("--ticks", type=int, default=DEF_BASE_TICKS,
                   help="header timing_base_ticks (default {})".format(DEF_BASE_TICKS))
    s.set_defaults(fn=cmd_make)

    s = sub.add_parser("inspect", help="decode a .ctm back to a timeline (round-trip proof)")
    s.add_argument("movie")
    s.add_argument("--json", action="store_true")
    s.set_defaults(fn=cmd_inspect)

    args = ap.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
