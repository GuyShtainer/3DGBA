# test_rsp.py — RSP packet encode/decode goldens for gdbio.py (phase-16 slice E2;
# SPEC-harness H6.1 host-pure split). Run:
#   tools/emutest/.venv/bin/python -m unittest discover -s tools/emutest/tests -p 'test_*.py' -v
#
# The framing facts pinned here are standard GDB RSP as implemented by Azahar's stub
# (SPEC-protocols S2.3: `$<data>#<2-hex mod-256 checksum>` with `+`/`-` acks; gdbstub.cpp).
# Checksum goldens below were computed independently (sum(bytes) % 256) and are literals on
# purpose — the test must not recompute them with the code under test.

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import gdbio  # noqa: E402


class TestChecksumAndBuild(unittest.TestCase):
    def test_checksum_goldens(self):
        # Independently computed literals (see file header).
        self.assertEqual(gdbio.rsp_checksum(b"m49b49c,20"), 0x9A)
        self.assertEqual(gdbio.rsp_checksum(b"?"), 0x3F)
        self.assertEqual(gdbio.rsp_checksum(b"OK"), 0x9A)
        self.assertEqual(gdbio.rsp_checksum(b"D"), 0x44)
        self.assertEqual(gdbio.rsp_checksum(b"k"), 0x6B)

    def test_build_goldens(self):
        # The exact wire bytes for the packets E2's live flow sends (S2.2/S2.3).
        self.assertEqual(gdbio.rsp_build(b"?"), b"$?#3f")
        self.assertEqual(gdbio.rsp_build(b"D"), b"$D#44")
        self.assertEqual(gdbio.rsp_build(b"m49b49c,20"), b"$m49b49c,20#9a")
        self.assertEqual(gdbio.rsp_build(b"m1eb328,40"), b"$m1eb328,40#92")

    def test_build_escapes_specials(self):
        # GDB binary-escape: '}' -> 0x7d,(c^0x20); '$','#','*' likewise. A payload of
        # b"}" must not appear raw on the wire (it would corrupt framing).
        pkt = gdbio.rsp_build(b"}")
        self.assertTrue(pkt.startswith(b"$\x7d\x5d#"), pkt)

    def test_escape_roundtrip(self):
        for payload in [b"}", b"$", b"#", b"*", b"a}b$c#d*e", bytes(range(256))]:
            esc = gdbio.rsp_escape(payload)
            # no raw specials on the wire...
            body = esc.replace(b"\x7d", b"")  # escape marker itself is allowed
            for c in b"$#":
                self.assertNotIn(bytes([c]), body)
            # ...and decode restores the original
            self.assertEqual(gdbio.rsp_decode_payload(esc), payload)


class TestDecode(unittest.TestCase):
    def test_rle_golden(self):
        # GDB spec example: '0* ' is a run of four '0's (' '=0x20, 0x20-29=3 more).
        self.assertEqual(gdbio.rsp_decode_payload(b"0* "), b"0000")

    def test_rle_mid_string(self):
        # 'x' + ('!'=0x21 -> 4 more) => "xxxxx"; surrounding text untouched.
        self.assertEqual(gdbio.rsp_decode_payload(b"ax*!b"), b"axxxxxb")

    def test_plain_hex_passthrough(self):
        # Azahar's m-read replies are plain hex — must pass through untouched.
        self.assertEqual(gdbio.rsp_decode_payload(b"30402de99c409fe5"),
                         b"30402de99c409fe5")

    def test_escape_decode(self):
        self.assertEqual(gdbio.rsp_decode_payload(b"\x7d\x5d"), b"\x7d")   # '}'
        self.assertEqual(gdbio.rsp_decode_payload(b"\x7d\x03"), b"#")
        self.assertEqual(gdbio.rsp_decode_payload(b"\x7d\x04"), b"$")


class TestPacketReader(unittest.TestCase):
    def feed_all(self, reader, data):
        reader.feed(data)
        out = []
        while True:
            ev = reader.next_event()
            if ev is None:
                return out
            out.append(ev)

    def test_ack_then_packet(self):
        # The stub acks our command with '+' then sends its reply — the exact byte
        # sequence a `?` transaction produces (S2.3). "T05thread:1;" checksum d7 (literal).
        r = gdbio.PacketReader()
        evs = self.feed_all(r, b"+$T05thread:1;#d7")
        self.assertEqual(evs, [("ack",), ("pkt", b"T05thread:1;")])

    def test_split_packet_across_feeds(self):
        # TCP may fragment anywhere, including inside the 2-char checksum.
        r = gdbio.PacketReader()
        self.assertEqual(self.feed_all(r, b"+$m49b49c"), [("ack",)])
        self.assertEqual(self.feed_all(r, b",20#9"), [])
        self.assertEqual(self.feed_all(r, b"a"), [("pkt", b"m49b49c,20")])

    def test_empty_reply_packet(self):
        # S2.3: an invalid m-read address gets the EMPTY packet `$#00` — decoded as b"".
        r = gdbio.PacketReader()
        self.assertEqual(self.feed_all(r, b"$#00"), [("pkt", b"")])

    def test_nak_event(self):
        r = gdbio.PacketReader()
        self.assertEqual(self.feed_all(r, b"-"), [("nak",)])

    def test_bad_checksum_flagged(self):
        r = gdbio.PacketReader()
        evs = self.feed_all(r, b"$OK#00")   # real checksum is 9a
        self.assertEqual(evs, [("badsum", b"OK")])

    def test_ok_golden(self):
        r = gdbio.PacketReader()
        self.assertEqual(self.feed_all(r, b"$OK#9a"), [("pkt", b"OK")])

    def test_stray_bytes_skipped(self):
        # Garbage between packets (e.g. a stray \0 on connect) must not derail framing.
        r = gdbio.PacketReader()
        self.assertEqual(self.feed_all(r, b"\x00\x0a$D#44"), [("pkt", b"D")])


if __name__ == "__main__":
    unittest.main()
