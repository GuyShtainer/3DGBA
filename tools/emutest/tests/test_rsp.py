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


class TestHaltDrain(unittest.TestCase):
    """REVIEW FIX 2026-08-09 (F16): _drain() was a fixed `while time.time() < end` sleep,
    so every halt paid the full 0.3 s even though the live wire transcript shows the
    spurious empty packet arriving in the SAME burst as the T05 (+0.0007 s). It now
    returns as soon as that packet is consumed, with `secs` kept as the ceiling for a late
    one. Driven here over a real socketpair, no emulator involved."""

    def _client(self):
        import socket
        a, b = socket.socketpair()
        a.settimeout(0.25)
        c = gdbio.RspClient()
        c.sock = a
        c.halted = True
        return c, b

    def test_returns_immediately_once_the_artifact_arrives(self):
        import time
        c, peer = self._client()
        try:
            peer.sendall(b"$#00")                 # the release's spurious empty packet
            t0 = time.time()
            self.assertTrue(c._drain(0.3))
            self.assertLess(time.time() - t0, 0.1,
                            "drain still burns the whole ceiling")
        finally:
            peer.close(); c.close()

    def test_ceiling_still_bounds_a_silent_socket(self):
        import time
        c, peer = self._client()
        try:
            t0 = time.time()
            self.assertFalse(c._drain(0.2))       # nothing ever arrives
            self.assertGreaterEqual(time.time() - t0, 0.19)
        finally:
            peer.close(); c.close()

    def test_a_late_artifact_is_still_absorbed(self):
        # The old fixed sleep would have left a >0.3 s packet in the stream, where the next
        # _txn consumed it as the `m` reply and read_mem raised the wrong diagnosis.
        import threading
        c, peer = self._client()
        try:
            threading.Timer(0.15, lambda: peer.sendall(b"$#00")).start()
            self.assertTrue(c._drain(1.0))
        finally:
            peer.close(); c.close()


class TestPollTimeoutIsWallClock(unittest.TestCase):
    """REVIEW FIX 2026-08-09 (F13): `poll` inherited the shared --timeout whose default is
    the PER-RSP-OP timeout (5.0 s), so the documented liveness one-liner failed on a
    healthy boot (the app needs ~5-10 s after resume to reach its render loop)."""

    def test_constants_are_distinct(self):
        self.assertNotEqual(gdbio.POLL_TIMEOUT_S, gdbio.RSP_TIMEOUT_S)
        self.assertGreaterEqual(gdbio.POLL_TIMEOUT_S, 30.0)

    def test_poll_help_advertises_the_wall_clock_default(self):
        import subprocess
        out = subprocess.run(
            [sys.executable, os.path.join(os.path.dirname(os.path.dirname(
                os.path.abspath(__file__))), "gdbio.py"), "poll", "--help"],
            capture_output=True, text=True).stdout
        self.assertIn("WALL-CLOCK", out)
        self.assertIn(str(gdbio.POLL_TIMEOUT_S), out)


if __name__ == "__main__":
    unittest.main()
