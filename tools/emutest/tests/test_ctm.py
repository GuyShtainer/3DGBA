# test_ctm.py — host-pure tests for ctm.py (phase-16 slice E3; SPEC-harness H6.2
# "test_ctm_golden": golden CTM bytes vs the SPEC-protocols S1.11 example + round-trip).
# Run line:
#   tools/emutest/.venv/bin/python -m unittest discover -s tools/emutest/tests -p 'test_*.py' -v
#
# The golden below is built IN THE TEST from the S1.11 spec table (header field-by-field,
# body pair-by-pair) — independent of ctm.py's own pack helpers, so a synthesizer bug can't
# hide inside a shared constant. S1.11 was verified against the RELEASE 2125.1.2 movie.cpp
# (structs byte-identical to master; see ctm.py module doc).

import json
import os
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import ctm

IDLE_PAIR = bytes.fromhex("00000000000000" + "01000000000000")
START_PAIR = bytes.fromhex("00080000000000" + "01000000000000")   # hex=0x0008 bit3=Start
TOUCH_PAIR = bytes.fromhex("00000000000000" + "01a00078000100")   # touch 160,120 valid


def golden_s111_header():
    """The S1.11 header, field by field (offsets per S1.1 == release CTMHeader)."""
    h = bytearray(256)
    h[0x00:0x04] = b"CTM\x1b"                       # magic
    # 0x04 program_id = 0 (a .3dsx; S1.10)          (already zero)
    # 0x0C revision[20] = zeros (desync-warning variant)
    h[0x20:0x28] = struct.pack("<Q", 946684800)     # clock_init_time 2000-01-01
    h[0x28:0x30] = struct.pack("<Q", 0x0123456789ABCDEF)  # id
    h[0x30:0x3D] = b"3dgba-emutest"                 # author (NUL-padded to 32)
    h[0x50:0x54] = struct.pack("<I", 1)             # rerecord_count
    h[0x54:0x5C] = struct.pack("<Q", 12)            # input_count = PAD records only
    h[0x5C:0x64] = struct.pack("<q", 1000)          # timing_base_ticks
    return bytes(h)


class TestGoldenS111(unittest.TestCase):
    """ctm make on the poll-exact script == the 424-byte S1.11 example, byte for byte."""

    def make(self, script, **kw):
        tl = ctm.compile_script(script)
        return ctm.movie_bytes(tl, **kw)

    def test_golden_bytes(self):
        data = self.make([["wait_polls", 4], ["hold_polls", "START", 4],
                          ["wait_polls", 4]])
        expected = golden_s111_header() + 4 * IDLE_PAIR + 4 * START_PAIR + 4 * IDLE_PAIR
        self.assertEqual(len(expected), 424)
        self.assertEqual(data, expected)

    def test_touch_pair_golden(self):
        # S1.11's touch reference pair: tap at native (160,120) -> 01 A0 00 78 00 01 00
        data = self.make([["touch_polls", 160, 120, 1]])
        self.assertEqual(data[256:270], TOUCH_PAIR)

    def test_cli_make_matches(self):
        # Through the real CLI (file in, file out) — same bytes.
        with tempfile.TemporaryDirectory() as d:
            sp = os.path.join(d, "s.json")
            out = os.path.join(d, "o.ctm")
            with open(sp, "w") as f:
                json.dump([["wait_polls", 4], ["hold_polls", "START", 4],
                           ["wait_polls", 4]], f)
            rc = ctm.main(["make", sp, out])
            self.assertEqual(rc, 0)
            with open(out, "rb") as f:
                self.assertEqual(f.read(),
                                 golden_s111_header() + 4 * IDLE_PAIR + 4 * START_PAIR
                                 + 4 * IDLE_PAIR)


class TestFrameConversion(unittest.TestCase):
    """Frame times -> poll counts by cumulative rounding of the EXACT ratio
    234*4481136/268111856 (~3.9110/frame). Expected numbers computed independently
    with Fraction in the dev shell (BUILDLOG E3)."""

    def test_wait_tap_runs(self):
        tl = ctm.compile_script([["wait", 120], ["tap", "START"]])   # 8 hold + 8 release
        _hdr, runs, problems = ctm.parse_movie(ctm.movie_bytes(tl))
        self.assertEqual(problems, [])
        self.assertEqual([(r["keys_name"], r["polls"]) for r in runs],
                         [("idle", 469), ("START", 32), ("idle", 31)])
        self.assertEqual(len(tl.polls), 532)   # round(136 * PPF)

    def test_input_count_counts_pads_only(self):
        tl = ctm.compile_script([["wait", 120]])
        data = ctm.movie_bytes(tl)
        input_count = struct.unpack_from("<Q", data, 0x54)[0]
        self.assertEqual(input_count, 469)
        self.assertEqual(len(data), 256 + 469 * 14)

    def test_sub_poll_press_fails_loudly(self):
        # A held state that emits zero polls would be a press the console never sees.
        with self.assertRaises(ctm.CtmError):
            ctm.compile_script([["tap", "A", 0, 8]])

    def test_empty_script_fails(self):
        with self.assertRaises(ctm.CtmError):
            ctm.compile_script([])


class TestKeysAndTouch(unittest.TestCase):
    def test_key_bits(self):
        # movie.cpp:47-61 bit order.
        self.assertEqual(ctm.parse_keys("START"), 0x0008)
        self.assertEqual(ctm.parse_keys("START+SELECT"), 0x000C)
        self.assertEqual(ctm.parse_keys("A+B+X+Y"), 0x0C03)
        self.assertEqual(ctm.parse_keys("GPIO14"), 0x2000)

    def test_zl_zr_rejected(self):
        # ZL/ZR live in IrRst records, not PadAndCircle (S1.3) — must not parse.
        with self.assertRaises(ctm.CtmError):
            ctm.parse_keys("ZR")

    def test_touch_range(self):
        with self.assertRaises(ctm.CtmError):
            ctm.compile_script([["touch", 320, 0]])    # S1.4: 0..319
        with self.assertRaises(ctm.CtmError):
            ctm.compile_script([["touch", 0, 240]])    # S1.4: 0..239
        ctm.compile_script([["touch", 319, 239]])       # in range

    def test_hold_persists_until_release(self):
        tl = ctm.compile_script([["hold", "A", 4], ["wait", 4], ["release"],
                                 ["wait", 4]])
        _h, runs, problems = ctm.parse_movie(ctm.movie_bytes(tl))
        self.assertEqual(problems, [])
        self.assertEqual([r["keys_name"] for r in runs], ["A", "idle"])
        self.assertEqual(runs[0]["polls"], 31)   # round(8*PPF) — hold spans wait too


class TestInspect(unittest.TestCase):
    def test_round_trip(self):
        script = [["wait", 30], ["tap", "START"], ["touch", 200, 125, 10],
                  ["hold", "SELECT+START", 12], ["release"], ["wait", 10]]
        tl = ctm.compile_script(script)
        hdr, runs, problems = ctm.parse_movie(ctm.movie_bytes(tl))
        self.assertEqual(problems, [])
        self.assertEqual(hdr["input_count"], len(tl.polls))
        self.assertEqual(hdr["author"], "3dgba-emutest")
        names = [r["keys_name"] + (":touch" if r["touch"][2] else "") for r in runs]
        self.assertEqual(names, ["idle", "START", "idle", "idle:touch", "idle",
                                 "SELECT+START", "idle"])
        self.assertEqual(sum(r["polls"] for r in runs), len(tl.polls))

    def test_alternation_violation_detected(self):
        # Two Pad records in a row = the desync Azahar logs "Expected to read type 1".
        data = ctm.movie_bytes(ctm.compile_script([["wait_polls", 2]]))
        bad = bytearray(data)
        bad[256 + 7] = 0    # overwrite the first Touch record's type with Pad
        _h, _r, problems = ctm.parse_movie(bytes(bad))
        self.assertTrue(any("Touch(1) expected" in p for p in problems))

    def test_bad_magic_flagged(self):
        data = bytearray(ctm.movie_bytes(ctm.compile_script([["wait_polls", 1]])))
        data[0] = 0x58
        _h, _r, problems = ctm.parse_movie(bytes(data))
        self.assertTrue(any("bad magic" in p for p in problems))

    def test_input_count_mismatch_flagged(self):
        data = bytearray(ctm.movie_bytes(ctm.compile_script([["wait_polls", 2]])))
        struct.pack_into("<Q", data, 0x54, 99)
        _h, _r, problems = ctm.parse_movie(bytes(data))
        self.assertTrue(any("input_count=99" in p for p in problems))


class TestHeaderKnobs(unittest.TestCase):
    def test_revision_stamp(self):
        rev = bytes(range(20))
        data = ctm.movie_bytes(ctm.compile_script([["wait_polls", 1]]), revision=rev)
        self.assertEqual(data[0x0C:0x20], rev)

    def test_reserved_zeros_and_author_padding(self):
        data = ctm.movie_bytes(ctm.compile_script([["wait_polls", 1]]), author="ab")
        self.assertEqual(data[0x30:0x50], b"ab" + b"\x00" * 30)
        self.assertEqual(data[0x64:0x100], b"\x00" * 156)


if __name__ == "__main__":
    unittest.main()
