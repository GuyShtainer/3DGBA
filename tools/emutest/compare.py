#!/usr/bin/env python3
"""compare.py — pixel-diff two screenshots; exit code = verdict (phase-16 slice E4).

Port of the PokeDNA harness compare.py (gba-toolkit scratchpad, read 2026-08-08 — the
sibling report's DECIDE mechanic: "ref-vs-new heat-map diff with --tolerance/--max-diff,
exit code = verdict"). Two deliberate changes from the original:
  1. pillow-only — the numpy dependency is dropped per SPEC-harness H1.2 (the pixel
     metrics here are exact with ImageChops.difference + histogram/point on images of our
     size; PokeDNA needed numpy only for libmgba-py's raw C buffer, which does not exist
     on the Azahar path).
  2. size-agnostic — GBA shots were always 240x160; here top crops are 400x240-scaled and
     bottom 320x240-scaled (SPEC-protocols S4), so the diff takes whatever matching pair
     it is given and only insists ref and candidate agree with each other.

Diff image layout (unchanged from the original): 3 panels [reference | candidate | heat],
1px green separators; heat = dimmed greyscale reference with changed pixels bright red —
you see WHERE it moved, not just that it did.

CLI (SPEC-harness H2.5):
    compare ref.png new.png [--tolerance N] [--max-diff N] [--heatmap out.png] [-o out.png]
    exit 0  <= max-diff differing pixels (default 0 = identical)
    exit 1  more differing pixels than allowed
    exit 2  operator error (missing file, size mismatch)

Library:
    from compare import compare_png
    d = compare_png("ref.png", "new.png", diff_path="diff.png")
    assert d.identical, d.summary()
"""

import argparse
import os
import sys

from PIL import Image, ImageChops


class PngDiff(object):
    def __init__(self, ref_path, new_path, width, height, diff_pixels,
                 max_channel_delta, mean_channel_delta, bbox, diff_path, tolerance):
        self.ref_path = ref_path
        self.new_path = new_path
        self.width = width
        self.height = height
        self.total_pixels = width * height
        self.diff_pixels = diff_pixels
        self.max_channel_delta = max_channel_delta
        self.mean_channel_delta = mean_channel_delta
        self.bbox = bbox                    # (x0, y0, x1, y1) inclusive, or None
        self.diff_path = diff_path
        self.tolerance = tolerance

    @property
    def identical(self):
        return self.diff_pixels == 0

    @property
    def percent(self):
        return 100.0 * self.diff_pixels / self.total_pixels if self.total_pixels else 0.0

    def summary(self):
        if self.identical:
            s = ("IDENTICAL — {}x{}, 0 differing pixels".format(self.width, self.height)
                 + (" (tolerance {})".format(self.tolerance) if self.tolerance else ""))
        else:
            s = ("DIFFERENT — {}/{} pixels ({:.3f}%), max channel delta {}, mean {:.3f}"
                 .format(self.diff_pixels, self.total_pixels, self.percent,
                         self.max_channel_delta, self.mean_channel_delta))
            if self.bbox:
                x0, y0, x1, y1 = self.bbox
                s += ", changed bbox x{}-{} y{}-{}".format(x0, x1, y0, y1)
        if self.diff_path:
            s += "\n  diff image: {}".format(self.diff_path)
        return s


def _load_rgb(path):
    if not os.path.isfile(path):
        raise FileNotFoundError("image not found: {}".format(path))
    return Image.open(path).convert("RGB")


def _mask(delta, tolerance):
    """Changed-pixel mask (mode L, 255 = changed): a pixel counts as changed when ANY
    channel's |delta| exceeds tolerance — same predicate as the numpy original's
    `(delta > tolerance).any(axis=2)`."""
    thresholded = delta.point(lambda v: 255 if v > tolerance else 0)
    r, g, b = thresholded.split()
    return ImageChops.lighter(ImageChops.lighter(r, g), b)


def _panel(ref, new, mask):
    """The 3-panel diff image (port of the original _panel, PIL ops for numpy ones)."""
    w, h = ref.size
    grey = ref.convert("L").point(lambda v: v // 3)          # dimmed greyscale ref
    heat = Image.merge("RGB", (grey, grey, grey))
    red = Image.new("RGB", ref.size, (255, 0, 0))
    heat.paste(red, (0, 0), mask)                            # changed pixels bright red
    sep = Image.new("RGB", (1, h), (0, 255, 0))
    out = Image.new("RGB", (w * 3 + 2, h))
    out.paste(ref, (0, 0))
    out.paste(sep, (w, 0))
    out.paste(new, (w + 1, 0))
    out.paste(sep, (2 * w + 1, 0))
    out.paste(heat, (2 * w + 2, 0))
    return out


def compare_png(ref_path, new_path, diff_path=None, tolerance=0):
    """Compare two images; `tolerance` is per-channel absolute slack."""
    ref = _load_rgb(ref_path)
    new = _load_rgb(new_path)
    if ref.size != new.size:
        raise ValueError("size mismatch: {} is {}x{}, {} is {}x{}".format(
            os.path.basename(ref_path), ref.size[0], ref.size[1],
            os.path.basename(new_path), new.size[0], new.size[1]))
    w, h = ref.size

    delta = ImageChops.difference(ref, new)                  # per-channel |a-b|
    mask = _mask(delta, tolerance)
    n = mask.histogram()[255]                                # changed-pixel count (exact)
    bbox = None
    if n:
        bb = mask.getbbox()                                  # (x0,y0,x1,y1) exclusive end
        bbox = (bb[0], bb[1], bb[2] - 1, bb[3] - 1)          # inclusive, like the original

    # Channel-delta stats over ALL channels (exact ints from the histogram — the numpy
    # original's delta.max()/delta.mean()).
    hist = delta.histogram()                                 # 3 x 256 (R,G,B)
    per_value = [hist[i] + hist[256 + i] + hist[512 + i] for i in range(256)]
    total_ch = sum(per_value)
    max_delta = max((i for i, c in enumerate(per_value) if c), default=0)
    mean_delta = (sum(i * c for i, c in enumerate(per_value)) / total_ch) if total_ch else 0.0

    written = None
    if diff_path:
        d = os.path.dirname(os.path.abspath(diff_path))
        if d:
            os.makedirs(d, exist_ok=True)
        _panel(ref, new, mask).save(diff_path)
        written = str(diff_path)

    return PngDiff(str(ref_path), str(new_path), w, h, n, max_delta, mean_delta,
                   bbox, written, tolerance)


def assert_identical(ref_path, new_path, diff_path=None, tolerance=0, max_diff=0):
    """Raise AssertionError unless the two images match. Returns the PngDiff."""
    d = compare_png(ref_path, new_path, diff_path=diff_path, tolerance=tolerance)
    if d.diff_pixels > max_diff:
        raise AssertionError(d.summary())
    return d


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="pixel-diff two screenshots; exit code = verdict (H2.5)")
    ap.add_argument("reference")
    ap.add_argument("candidate")
    ap.add_argument("-o", "--out", "--heatmap", dest="out", metavar="DIFF.png",
                    help="write the 3-panel diff image (ref | new | heat)")
    ap.add_argument("--tolerance", type=int, default=0,
                    help="per-channel absolute slack (default 0 = exact)")
    ap.add_argument("--max-diff", type=int, default=0,
                    help="exit 0 if at most this many pixels differ (default 0)")
    a = ap.parse_args(argv)
    try:
        d = compare_png(a.reference, a.candidate, diff_path=a.out, tolerance=a.tolerance)
    except (FileNotFoundError, ValueError) as exc:
        print("ERROR: {}".format(exc), file=sys.stderr)
        return 2
    print(d.summary())
    return 0 if d.diff_pixels <= a.max_diff else 1


if __name__ == "__main__":
    sys.exit(main())
