# test_censusguard.py — host-pure tests for the census capture-time novelty assertion
# (phase 29 lane E; EVIDENCE-INTEGRITY.md).
# Run line:
#   tools/emutest/.venv/bin/python -m unittest discover -s tools/emutest/tests -p 'test_*.py' -v
#
# The load-bearing expectations are MEASURED, not invented:
#   * the subject rect is the per-pixel varying bbox across 40 real captures of each
#     screen — (80,40)-(320,200) on the 400x240 top, (40,40)-(280,200) on the 320x240
#     bottom (censusguard module doc);
#   * TestRealEvidence replays the one defect this guard exists to catch —
#     firered/I4-slotmachine vs firered/I4c-slotspin, byte-identical GBA frames 32 s
#     apart — against the banked evidence itself, and asserts that the HUD-bearing top
#     screen is NOT what gets hashed.
# The evidence-backed tests SKIP loudly (never silently pass) when evidence/ is absent.

import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import censusguard  # noqa: E402

_HERE = os.path.abspath(__file__)                       # tools/emutest/tests/this file
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(_HERE))))
EV = os.path.join(REPO, "docs", "phase21-touch-census", "evidence")


def _pil():
    try:
        from PIL import Image
        return Image
    except ImportError:  # pragma: no cover - the venv always has it
        return None


class TestSubjectRect(unittest.TestCase):
    def test_top_screen(self):
        # 400x240 -> the measured varying bbox of the emerald census tops.
        self.assertEqual(censusguard.subject_rect((400, 240)), (80, 40, 320, 200))

    def test_bottom_screen(self):
        # 320x240 -> the measured varying bbox of the firered census bottoms.
        self.assertEqual(censusguard.subject_rect((320, 240)), (40, 40, 280, 200))

    def test_rect_is_always_the_gba_frame(self):
        for size in ((400, 240), (320, 240)):
            x0, y0, x1, y1 = censusguard.subject_rect(size)
            self.assertEqual((x1 - x0, y1 - y0), (censusguard.GBA_W, censusguard.GBA_H))

    def test_non_native_size_raises(self):
        # A 2.25x `see shot` crop or a zoomed impl capture is NOT a census subject; the
        # guard must refuse it rather than hash the wrong pixels.
        for bad in ((900, 540), (720, 480), (240, 160), (0, 0)):
            with self.assertRaises(ValueError):
                censusguard.subject_rect(bad)


class TestSubjectSelection(unittest.TestCase):
    def test_emerald_subject_is_top(self):
        self.assertTrue(censusguard.is_subject("/x/evidence/emerald/B1.top.png"))
        self.assertFalse(censusguard.is_subject("/x/evidence/emerald/B1.bottom.png"))

    def test_firered_subject_is_bottom(self):
        self.assertTrue(censusguard.is_subject("/x/evidence/firered/B1.bottom.png"))
        self.assertFalse(censusguard.is_subject("/x/evidence/firered/B1.top.png"))

    def test_lone_capture_is_its_own_subject(self):
        self.assertTrue(censusguard.is_subject("/x/evidence/ruby/A2-title.png"))
        self.assertTrue(censusguard.is_subject("/x/evidence/impl/EM-P24.bottom.png"))


class TestDigestIgnoresChrome(unittest.TestCase):
    """The whole point: the top screen's HUD clock changes every capture, so a whole-file
    hash calls two shots of one unchanged screen 'distinct'. The subject digest must not."""

    def setUp(self):
        self.Image = _pil()
        if self.Image is None:
            self.skipTest("SKIP: PIL not importable")

    def _synth(self, path, size, seed, hud):
        im = self.Image.new("RGB", size, (0, 0, 0))
        px = im.load()
        x0, y0, x1, y1 = censusguard.subject_rect(size)
        for y in range(y0, y1):
            for x in range(x0, x1):
                px[x, y] = ((x * 7 + y * 13 + seed) % 256, (x + seed) % 256, y % 256)
        for x in range(size[0]):          # a HUD strip above the frame, y<30
            px[x, 10] = (hud, hud, hud)
        im.save(path)

    def test_same_frame_different_hud_is_one_digest(self):
        with tempfile.TemporaryDirectory() as d:
            a, b = os.path.join(d, "a.png"), os.path.join(d, "b.png")
            self._synth(a, (400, 240), seed=3, hud=11)
            self._synth(b, (400, 240), seed=3, hud=250)   # clock ticked, game did not
            import hashlib
            with open(a, "rb") as fa, open(b, "rb") as fb:
                ha, hb = (hashlib.sha256(fa.read()).hexdigest(),
                          hashlib.sha256(fb.read()).hexdigest())
            self.assertNotEqual(ha, hb,
                                "the files must differ, else the test proves nothing")
            self.assertEqual(censusguard.frame_digest(a), censusguard.frame_digest(b))

    def test_different_frame_is_a_different_digest(self):
        with tempfile.TemporaryDirectory() as d:
            a, b = os.path.join(d, "a.png"), os.path.join(d, "b.png")
            self._synth(a, (400, 240), seed=3, hud=11)
            self._synth(b, (400, 240), seed=4, hud=11)
            self.assertNotEqual(censusguard.frame_digest(a), censusguard.frame_digest(b))

    def test_check_refuses_a_repeat_and_accepts_a_novel_frame(self):
        with tempfile.TemporaryDirectory() as d:
            banked = os.path.join(d, "I4-slotmachine.bottom.png")
            repeat = os.path.join(d, "new.png")
            novel = os.path.join(d, "novel.png")
            self._synth(banked, (320, 240), seed=9, hud=1)
            self._synth(repeat, (320, 240), seed=9, hud=200)
            self._synth(novel, (320, 240), seed=10, hud=200)
            self.assertEqual(censusguard.main(["check", repeat, "--against", banked]), 1)
            self.assertEqual(censusguard.main(["check", novel, "--against", banked]), 0)

    def test_bank_writes_a_novel_frame_and_refuses_a_repeat(self):
        # `bank` is the fix: the banked file is a PRODUCT of the check, so a repeat
        # leaves NOTHING on disk — the failure mode that banked I4c-slotspin cannot recur
        # through this path.
        with tempfile.TemporaryDirectory() as d:
            bankdir = os.path.join(d, "firered")     # the evidence dir being added to
            os.makedirs(bankdir)
            banked = os.path.join(bankdir, "I4-slotmachine.bottom.png")
            self._synth(banked, (320, 240), seed=9, hud=1)
            # a `see shot` bottom crop is the native screen at 2.25x (720x540)
            shot_repeat = os.path.join(d, "shot_repeat.png")
            shot_novel = os.path.join(d, "shot_novel.png")
            self.Image.open(banked).resize((720, 540), self.Image.NEAREST).save(shot_repeat)
            novel_src = os.path.join(d, "novel_src.png")
            self._synth(novel_src, (320, 240), seed=42, hud=1)
            self.Image.open(novel_src).resize((720, 540), self.Image.NEAREST).save(shot_novel)

            out_bad = os.path.join(bankdir, "I4c-slotspin.bottom.png")
            rc = censusguard.main(["bank", shot_repeat, out_bad,
                                   "--screen", "bottom", "--against", bankdir])
            self.assertEqual(rc, 1)
            self.assertFalse(os.path.exists(out_bad), "a refused capture must not be written")

            out_ok = os.path.join(bankdir, "I5-roulette.bottom.png")
            rc = censusguard.main(["bank", shot_novel, out_ok,
                                   "--screen", "bottom", "--against", bankdir])
            self.assertEqual(rc, 0)
            self.assertTrue(os.path.exists(out_ok))
            self.assertEqual(self.Image.open(out_ok).size, (320, 240))
            # and the reconstruction is exact, not a resample
            self.assertEqual(censusguard.frame_digest(out_ok),
                             censusguard.frame_digest(novel_src))

    def test_check_refuses_a_blank_frame(self):
        with tempfile.TemporaryDirectory() as d:
            blank = os.path.join(d, "blank.png")
            other = os.path.join(d, "other.png")
            self.Image.new("RGB", (320, 240), (0, 0, 0)).save(blank)
            self._synth(other, (320, 240), seed=1, hud=1)
            self.assertEqual(censusguard.main(["check", blank, "--against", other]), 1)


class TestRealEvidence(unittest.TestCase):
    """Replays the actual defect against the actual banked census."""

    def setUp(self):
        if _pil() is None:
            self.skipTest("SKIP: PIL not importable")
        if not os.path.isdir(EV):
            self.skipTest("SKIP: {} not present (bare worktree)".format(EV))

    def _p(self, *a):
        p = os.path.join(EV, *a)
        if not os.path.exists(p):
            self.skipTest("SKIP: missing {}".format(p))
        return p

    def test_the_slot_pair_is_one_frame(self):
        # THE defect, kept as a live regression fixture: I4c-slotspin is labelled
        # "mid-spin" and is a re-shot of the idle machine 32 s earlier. Same cb2
        # (CB2_RunSlotMachine) on both, so the census's own state check could not separate
        # them; the frame digest can. The capture now sits in firered/withdrawn/ — kept,
        # not deleted, and excluded from the audit.
        a = self._p("firered", "I4-slotmachine.bottom.png")
        b = self._p("firered", "withdrawn", "I4c-slotspin.bottom.png")
        self.assertEqual(censusguard.frame_digest(a), censusguard.frame_digest(b))
        self.assertEqual(censusguard.main(["check", b, "--against", a]), 1)
        self.assertTrue(censusguard.is_aux(b), "withdrawn/ must not count as a claim")

    def test_four_unrelated_emerald_screens_are_four_frames(self):
        # The alarm that started the investigation: these four share ONE bottom-screen
        # hash. Their SUBJECT frames (top) are four distinct screens; the shared image is
        # the companion screen photographing an idle second game.
        ids = ["C3-battlemove", "D1b-frontiermap", "E2b-partysub",
               "G3a-pokenav-condition-menu"]
        tops = {censusguard.frame_digest(self._p("emerald", i + ".top.png")) for i in ids}
        self.assertEqual(len(tops), 4)
        bots = {censusguard.frame_digest(self._p("emerald", i + ".bottom.png")) for i in ids}
        self.assertEqual(len(bots), 1)
        for i in ids:
            self.assertFalse(censusguard.is_subject(self._p("emerald", i + ".bottom.png")))

    def test_no_subject_frame_is_blank(self):
        for d in ("emerald", "firered"):
            root = os.path.join(EV, d)
            if not os.path.isdir(root):
                continue
            for f in sorted(os.listdir(root)):
                p = os.path.join(root, f)
                if not f.endswith(".png") or not censusguard.is_subject(p):
                    continue
                self.assertLess(censusguard.dark_fraction(p), 0.99,
                                "{} is a blank subject frame".format(p))

    def test_no_two_census_rows_share_a_subject_frame(self):
        # The corrected state of the census: every remaining row's picture is its own.
        # aux/ and withdrawn/ are excluded — a scratch pad legitimately repeats a frame
        # while a nav step is retried, and a withdrawn capture is kept precisely BECAUSE
        # it repeats one.
        dups, _skipped = censusguard.duplicate_map(
            [os.path.join(EV, "emerald"), os.path.join(EV, "firered")], subject_only=True)
        rowdups = {k: v for k, v in dups.items()
                   if sum(1 for p in v if not censusguard.is_aux(p)) > 1}
        self.assertEqual(rowdups, {},
                         "two census rows share one frame: {}".format(rowdups))
        self.assertEqual(censusguard.main(
            ["audit", os.path.join(EV, "emerald"), os.path.join(EV, "firered")]), 0)


if __name__ == "__main__":
    unittest.main()
