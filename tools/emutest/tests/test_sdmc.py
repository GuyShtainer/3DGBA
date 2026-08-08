# test_sdmc.py — host-pure tests for sdmc.py (phase-16 slice E3; SPEC-harness H4 + the
# H6.2 "test_ctlscript" idea: the move-file bodies we drop are valid per control.h's
# grammar, string-level only — the app's own parser is tested in test/host/test_control.c).
# Run line:
#   tools/emutest/.venv/bin/python -m unittest discover -s tools/emutest/tests -p 'test_*.py' -v
#
# Every test points EMUTEST_AZ_DATA at a temp dir — the real Azahar data dir is never
# touched (PHASE invariant 2).

import os
import sys
import tempfile
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import sdmc


class Base(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self._old = os.environ.get("EMUTEST_AZ_DATA")
        os.environ["EMUTEST_AZ_DATA"] = self._tmp.name
        os.makedirs(os.path.join(self._tmp.name, "sdmc"), exist_ok=True)

    def tearDown(self):
        if self._old is None:
            os.environ.pop("EMUTEST_AZ_DATA", None)
        else:
            os.environ["EMUTEST_AZ_DATA"] = self._old
        self._tmp.cleanup()

    def netlog(self, name, body="", mtime=None):
        d = sdmc.netlogs_dir()
        os.makedirs(d, exist_ok=True)
        p = os.path.join(d, name)
        with open(p, "w") as f:
            f.write(body)
        if mtime is not None:
            os.utime(p, (mtime, mtime))
        return p


class TestMoveGrammar(unittest.TestCase):
    """String-level pre-flight vs the control.h token table (walks/taps/buttons/waits/G/!)."""

    def ok(self, body):
        self.assertEqual(sdmc.validate_move_body(body), [], body)

    def bad(self, body):
        self.assertNotEqual(sdmc.validate_move_body(body), [], body)

    def test_valid_bodies(self):
        self.ok("U2 a W60")            # walk 2 up, press A, wait 60
        self.ok("L12 r3 d")            # walk, sprint-walk, tap-turn
        self.ok("a b s c x y")         # all buttons (A B START SELECT L R)
        self.ok("W1 w999")             # waits, either case
        self.ok("G U2 a")              # G as token 0 = hold for the go file
        self.ok("  !anything at all")  # abort file: first non-ws '!' wins (D4.4)
        self.ok(" ".join(["a"] * 64))  # exactly CTL_TOK_MAX

    def test_invalid_bodies(self):
        self.bad("")                   # empty
        self.bad("   \n\t ")           # whitespace only
        self.bad("U2 G a")             # G not token 0
        self.bad("q")                  # no such button
        self.bad("W")                  # wait needs a count
        self.bad("A1")                 # buttons take no count (and are lowercase)
        self.bad("START")              # 3DS-style names are ctm.py's, not the app's
        self.bad(" ".join(["a"] * 65)) # over CTL_TOK_MAX


class TestControlWrites(Base):
    def test_arm_control_creates_dir(self):
        self.assertFalse(os.path.isdir(sdmc.control_dir()))
        rc = sdmc.main(["arm-control"])
        self.assertEqual(rc, 0)
        self.assertTrue(os.path.isdir(sdmc.control_dir()))
        # idempotent
        self.assertEqual(sdmc.main(["arm-control"]), 0)

    def test_drop_move_valid(self):
        rc = sdmc.main(["drop", "move", "1", "U2 a W60"])
        self.assertEqual(rc, 0)
        p = os.path.join(sdmc.control_dir(), "move_p1.txt")
        with open(p) as f:
            self.assertEqual(f.read(), "U2 a W60\n")   # newline appended
        # tmp-then-rename left no droppings
        leftovers = [n for n in os.listdir(sdmc.control_dir()) if n.startswith(".sdmc-")]
        self.assertEqual(leftovers, [])

    def test_drop_move_invalid_refused_force_overrides(self):
        rc = sdmc.main(["drop", "move", "2", "NOPE"])
        self.assertEqual(rc, 1)
        p = os.path.join(sdmc.control_dir(), "move_p2.txt")
        self.assertFalse(os.path.exists(p))
        rc = sdmc.main(["drop", "move", "2", "NOPE", "--force"])
        self.assertEqual(rc, 0)
        self.assertTrue(os.path.exists(p))

    def test_drop_marker_and_abort(self):
        self.assertEqual(sdmc.main(["drop", "go", "1"]), 0)
        self.assertTrue(os.path.exists(os.path.join(sdmc.control_dir(), "go_p1.txt")))
        self.assertEqual(sdmc.main(["drop-abort", "1"]), 0)
        with open(os.path.join(sdmc.control_dir(), "move_p1.txt")) as f:
            self.assertEqual(f.read(), "!\n")

    def test_seat_naming_fixed(self):
        # p1 = game A, p2 = game B (main.c:245-246) — never focus/screen dependent.
        self.assertTrue(sdmc.control_path("move", 1).endswith("/move_p1.txt"))
        self.assertTrue(sdmc.control_path("replay_go", 2).endswith("/replay_go_p2.txt"))
        with self.assertRaises(SystemExit):
            sdmc.control_path("evil/../..", 1)
        with self.assertRaises(SystemExit):
            sdmc.control_path("move", 3)

    def test_guard_blocks_outside_allowlist(self):
        # The write gate itself (azctl.assert_sd_writable) — user data stays untouchable.
        import azctl
        with self.assertRaises(RuntimeError):
            azctl.assert_sd_writable(os.path.join(azctl.az_sd(), "dual-gba", "x.txt"))

    def test_wait_consumed(self):
        # Absent = already consumed = PASS (the semantics IS "file gone").
        os.makedirs(sdmc.control_dir(), exist_ok=True)
        self.assertEqual(sdmc.main(["wait-consumed", "move", "1", "--timeout", "0.2"]), 0)
        # Present and nobody eats it -> timeout FAIL.
        sdmc.main(["drop", "move", "1", "a"])
        t0 = time.time()
        rc = sdmc.main(["wait-consumed", "move", "1", "--timeout", "0.4",
                        "--interval", "0.1"])
        self.assertEqual(rc, 1)
        self.assertGreaterEqual(time.time() - t0, 0.4)


class TestNetlogReads(Base):
    def test_netlogs_listing_and_since(self):
        now = time.time()
        self.netlog("3DGBA_control_0808_120000.txt", "x", mtime=now - 100)
        self.netlog("3DGBA_gs_0808_120100.txt", "y", mtime=now - 50)
        self.assertEqual(sdmc.main(["netlogs"]), 0)
        files = sdmc._netlog_files(since=now - 60)
        self.assertEqual([os.path.basename(p) for _m, p in files],
                         ["3DGBA_gs_0808_120100.txt"])

    def test_latest_picks_newest_and_matches_role_names(self):
        now = time.time()
        self.netlog("3DGBA_control_0808_120000.txt", mtime=now - 100)
        newest = self.netlog("3DGBA_control_0808_130000.txt", mtime=now - 10)
        self.netlog("3DGBA_gs_HOST_0808_120000.txt", mtime=now - 90)   # role-tagged
        self.netlog("3DGBA_rec_p1_0808_120000.txt", mtime=now - 80)    # seat-tagged
        self.netlog("3DGBA_gsX_0808_120000.txt", mtime=now - 1)        # wrong kind: no match
        self.assertEqual(sdmc._latest("control"), newest)
        self.assertTrue(sdmc._latest("gs").endswith("3DGBA_gs_HOST_0808_120000.txt"))
        self.assertTrue(sdmc._latest("rec").endswith("3DGBA_rec_p1_0808_120000.txt"))
        self.assertIsNone(sdmc._latest("csv"))
        self.assertEqual(sdmc.main(["latest", "csv"]), 1)   # absent = exit 1 (+ the
        # wireless-only note printed for csv — H4.4)

    def test_control_status_parse(self):
        hdr = ("# control p1=BPEE p2=BPRE dir=sdmc:/cias/control  (p1=game A, p2=game B, "
               "fixed; clock=emulated frames of that game)\n")
        self.netlog("3DGBA_control_0808_140000.txt",
                    hdr + "[ctl p1] picked up 3 tokens\n[ctl p1] done: 3 toks\n")
        self.assertEqual(sdmc.main(["control-status"]), 0)
        self.assertEqual(sdmc.main(["control-status", "--expect-pickup"]), 0)

    def test_control_status_no_pickup(self):
        self.netlog("3DGBA_control_0808_140001.txt",
                    "# control p1=---- p2=---- dir=sdmc:/cias/control\n")
        self.assertEqual(sdmc.main(["control-status"]), 0)
        self.assertEqual(sdmc.main(["control-status", "--expect-pickup"]), 1)

    def test_control_status_absent(self):
        self.assertEqual(sdmc.main(["control-status"]), 1)

    def test_cat_tail(self):
        p = self.netlog("3DGBA_wd_0808_150000.txt", "l1\nl2\nl3\n")
        self.assertEqual(sdmc.main(["cat", p, "--tail", "2"]), 0)
        self.assertEqual(sdmc.main(["cat", "wd"]), 0)
        self.assertEqual(sdmc.main(["cat", "hang"]), 1)   # none of that kind


if __name__ == "__main__":
    unittest.main()
