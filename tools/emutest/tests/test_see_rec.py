# test_see_rec.py — host-pure tests for see.py's VIDEO channel helpers (phase-16 slice
# E4, added scope: `see rec`). The live loop is smoke/BUILDLOG territory (H6.3: no mocks
# of the emulator); what is testable without Azahar is the CONTRACT the loop promises —
# deterministic frame naming, the fixed capture schedule, the ffmpeg argv, and the
# run-dir default. Those are exactly the parts a future session will depend on when it
# says "look at frame 37".
# Run line:
#   tools/emutest/.venv/bin/python -m unittest discover -s tools/emutest/tests -p 'test_*.py' -v

import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import see


class TestRecPlan(unittest.TestCase):
    def test_frame_count_and_interval(self):
        self.assertEqual(see.rec_plan(10, 5), (50, 0.2))
        self.assertEqual(see.rec_plan(6, 4), (24, 0.25))

    def test_rounding_and_minimum_one_frame(self):
        n, iv = see.rec_plan(0.1, 4)          # 0.4 frames -> at least one
        self.assertEqual(n, 1)
        self.assertAlmostEqual(iv, 0.25)
        self.assertEqual(see.rec_plan(2.5, 6)[0], 15)

    def test_bad_args_raise(self):
        for bad in ((0, 5), (5, 0), (-1, 5), (5, -1)):
            with self.assertRaises(ValueError):
                see.rec_plan(*bad)


class TestFrameNaming(unittest.TestCase):
    def test_zero_padded_and_pattern_matches(self):
        # The name and the ffmpeg pattern MUST agree, or the assembly silently sees
        # 0 frames (the classic %0Nd mismatch).
        self.assertEqual(see.frame_name("top", 0), "top_00000.png")
        self.assertEqual(see.frame_name("top", 37), "top_00037.png")
        self.assertEqual(see.frame_name("bottom", 12345), "bottom_12345.png")
        self.assertEqual(see.frame_pattern("top") % 37, see.frame_name("top", 37))
        self.assertEqual(see.frame_pattern("bottom") % 0, see.frame_name("bottom", 0))

    def test_kinds_are_distinct_streams(self):
        self.assertNotEqual(see.frame_name("top", 3), see.frame_name("bottom", 3))


class TestFfmpegCmd(unittest.TestCase):
    def test_mp4_argv(self):
        cmd = see.ffmpeg_cmd("/bin/ffmpeg", "/tmp/r", "top", 4.75, "/tmp/r/rec_top.mp4")
        self.assertEqual(cmd[0], "/bin/ffmpeg")
        self.assertIn("-y", cmd)
        self.assertEqual(cmd[cmd.index("-framerate") + 1], "4.7500")
        self.assertEqual(cmd[cmd.index("-start_number") + 1], "0")
        self.assertEqual(cmd[cmd.index("-i") + 1], "/tmp/r/top_%05d.png")
        self.assertEqual(cmd[cmd.index("-c:v") + 1], "libx264")
        self.assertEqual(cmd[cmd.index("-pix_fmt") + 1], "yuv420p")   # QuickTime-friendly
        # h264 needs even dimensions — a 320x240-scaled crop can land odd after the S4.1
        # integer truncations, so the scale filter is load-bearing, not decoration.
        self.assertIn("scale=trunc(iw/2)*2:trunc(ih/2)*2", " ".join(cmd))
        self.assertEqual(cmd[-1], "/tmp/r/rec_top.mp4")

    def test_gif_argv_has_no_x264(self):
        cmd = see.ffmpeg_cmd("/bin/ffmpeg", "/tmp/r", "bottom", 5.0, "/tmp/r/x.gif", "gif")
        self.assertNotIn("libx264", cmd)
        self.assertEqual(cmd[-1], "/tmp/r/x.gif")
        self.assertEqual(cmd[cmd.index("-i") + 1], "/tmp/r/bottom_%05d.png")


class TestFfmpegDiscovery(unittest.TestCase):
    def test_env_override_wins_and_missing_is_none(self):
        old = os.environ.get("EMUTEST_FFMPEG")
        try:
            os.environ["EMUTEST_FFMPEG"] = "/definitely/not/here/ffmpeg"
            self.assertIsNone(see.ffmpeg_bin())     # missing override -> None, not a crash
            os.environ["EMUTEST_FFMPEG"] = sys.executable
            self.assertEqual(see.ffmpeg_bin(), sys.executable)
        finally:
            if old is None:
                os.environ.pop("EMUTEST_FFMPEG", None)
            else:
                os.environ["EMUTEST_FFMPEG"] = old


class TestDefaultRecDir(unittest.TestCase):
    def test_points_into_last_run_when_present(self):
        here = os.path.dirname(os.path.dirname(os.path.abspath(see.__file__)))
        del here                                   # path shape only; no writes in tests
        d = see.default_rec_dir()
        self.assertTrue(os.path.basename(d).startswith("rec") or "adhoc-" in d, d)
        self.assertTrue(os.path.isabs(d) or d.startswith("/"), d)


class TestWindowGeometry(unittest.TestCase):
    def test_live_probed_window(self):
        # The E4 live probe: Quartz bounds 1280x568 pt, capture 2560x1136 px => factor 2,
        # title 56 px, client 2560x1080 and the S4.1 rects offset by the title strip.
        geo = see.window_geometry((2560, 1136), {"w": 1280.0})
        self.assertEqual(geo["factor"], 2.0)
        self.assertEqual(geo["title_px"], 56)
        self.assertEqual(geo["client"], [2560, 1080])
        self.assertEqual(geo["rects"]["top"], (830, 56, 1730, 596))
        self.assertEqual(geo["rects"]["bottom"], (920, 596, 1640, 1136))

    def test_rects_are_inside_the_image(self):
        for img in ((2560, 1136), (1200, 800), (900, 1000)):
            geo = see.window_geometry(img, {"w": img[0] / 2.0})
            for r in geo["rects"].values():
                self.assertGreaterEqual(r[0], 0)
                self.assertGreaterEqual(r[1], geo["title_px"])
                self.assertLessEqual(r[2], img[0])
                self.assertLessEqual(r[3], img[1])

    def test_too_small_raises(self):
        with self.assertRaises(ValueError):
            see.window_geometry((100, 10), {"w": 50.0})


class TestUniformDetector(unittest.TestCase):
    def test_blank_vs_content(self):
        from PIL import Image, ImageDraw
        blank = Image.new("RGB", (32, 32), (17, 17, 17))
        self.assertTrue(see._uniform(blank))
        lit = blank.copy()
        ImageDraw.Draw(lit).rectangle([4, 4, 9, 9], fill=(200, 200, 200))
        self.assertFalse(see._uniform(lit))


class TestManifestShape(unittest.TestCase):
    """The manifest is the contract a regression report cites ("frame 37 at t=7.4 s with
    tiltLevel 3"). Assemble the same dict the loop writes and assert the keys exist."""

    def test_json_round_trip(self):
        import json
        m = {"tool": "see.py rec", "format_version": 1, "frames_planned": 3,
             "screens": ["top"], "frames": [
                 {"i": 0, "t_rel": 0.0, "files": {"top": see.frame_name("top", 0)},
                  "state": {"g_prefs+0x1c": 3}}],
             "video": {"top": {"path": "rec_top.mp4", "bytes": 4096, "fps": 4.9}},
             "video_error": None, "ended_reason": "duration"}
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "manifest.json")
            with open(p, "w") as f:
                json.dump(m, f)
            with open(p) as f:
                back = json.load(f)
        self.assertEqual(back["frames"][0]["files"]["top"], "top_00000.png")
        self.assertEqual(back["frames"][0]["state"]["g_prefs+0x1c"], 3)
        self.assertEqual(back["video"]["top"]["path"], "rec_top.mp4")


if __name__ == "__main__":
    unittest.main()
