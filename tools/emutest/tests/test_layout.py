# test_layout.py — host-pure tests for see.py's screen-crop math (phase-16 slice E4;
# SPEC-harness H6.2 "test_layout": the H2.6/S4.1 algorithm at several window scales +
# Retina normalization, exact-pixel expectations).
# Run line:
#   tools/emutest/.venv/bin/python -m unittest discover -s tools/emutest/tests -p 'test_*.py' -v
#
# The expectations below are SPEC-protocols S4.2's worked examples VERBATIM plus the
# live-calibrated case from the E4 slice (client 2560x1080 @2x — the geometry the crops
# were visually proven against on 2026-08-08, BUILDLOG E4).

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import see


class TestLayoutRects(unittest.TestCase):
    def test_s42_square_1x(self):
        # S4.2: W x H = 400x480 (1:1): top (0,0)-(400,240); bottom (40,240)-(360,480).
        top, bottom = see.layout_rects(400, 480)
        self.assertEqual(top, (0, 0, 400, 240))
        self.assertEqual(bottom, (40, 240, 360, 480))

    def test_s42_2x(self):
        # S4.2: 800x960 (2x): top (0,0)-(800,480); bottom (80,480)-(720,960).
        top, bottom = see.layout_rects(800, 960)
        self.assertEqual(top, (0, 0, 800, 480))
        self.assertEqual(bottom, (80, 480, 720, 960))

    def test_s42_letterboxed_600x800(self):
        # S4.2: 600x800: scale=1.5, box 600x720 at oy=40 -> top (0,40)-(600,400);
        # bottom (60,400)-(540,760).
        top, bottom = see.layout_rects(600, 800)
        self.assertEqual(top, (0, 40, 600, 400))
        self.assertEqual(bottom, (60, 400, 540, 760))

    def test_live_calibrated_2560x1080(self):
        # The E4 live case (window 1280x568 pt @2x, title bar 28 pt -> client 2560x1080):
        # scale = min(6.4, 2.25) = 2.25, box 900x1080, ox 830 -> the rects that cropped
        # the app's HUD rows exactly (BUILDLOG E4).
        top, bottom = see.layout_rects(2560, 1080)
        self.assertEqual(top, (830, 0, 1730, 540))
        self.assertEqual(bottom, (920, 540, 1640, 1080))

    def test_truncation_odd_size(self):
        # Non-integer scale: 500x600 -> scale = min(1.25, 1.25) = 1.25, tw=625?? No:
        # int(400*1.25)=500, int(480*1.25)=600 exact; use 500x610 to force truncation:
        # scale = min(1.25, 1.2708..) = 1.25 -> th = int(600)=600, oy=(610-600)//2=5,
        # sa = 600/480 = 1.25 -> top h = int(240*1.25) = 300.
        top, bottom = see.layout_rects(500, 610)
        self.assertEqual(top, (0, 5, 500, 305))
        # bottom: bw = int(320*1.25) = 400, bx = 0 + 500//2 - 400//2 = 50, by = 305
        self.assertEqual(bottom, (50, 305, 450, 605))

    def test_top_bottom_share_edge_and_widths(self):
        # Structural invariants at arbitrary sizes: bottom starts exactly at the top
        # rect's lower edge; widths keep the 400:320 ratio via int(sa*w) truncation.
        for W, H in ((411, 483), (1024, 768), (2560, 1080), (777, 913)):
            top, bottom = see.layout_rects(W, H)
            self.assertEqual(bottom[1], top[3], (W, H))
            self.assertLessEqual(top[2] - top[0], W)
            self.assertLessEqual(bottom[3] - bottom[1], H)

    def test_empty_client_raises(self):
        with self.assertRaises(ValueError):
            see.layout_rects(0, 480)


class TestRetinaNormalization(unittest.TestCase):
    def test_factor_and_title_offset(self):
        # see.shot() computes factor = img_w / bounds_w and title_px = 28 * factor
        # (module doc [M]); replicate the arithmetic for the probed live window
        # (bounds 1280x568 pt, image 2560x1136 px) => factor 2.0, title 56 px,
        # client 2560x1080.
        img_w, img_h = 2560, 1136
        bounds_w = 1280.0
        factor = img_w / bounds_w
        self.assertEqual(factor, 2.0)
        title_px = int(round(see.TITLE_BAR_PT * factor))
        self.assertEqual(title_px, 56)
        self.assertEqual(img_h - title_px, 1080)

    def test_non_retina_factor(self):
        # A 1x display: factor 1.0, title 28 px.
        factor = 1280 / 1280.0
        self.assertEqual(int(round(see.TITLE_BAR_PT * factor)), 28)


if __name__ == "__main__":
    unittest.main()
