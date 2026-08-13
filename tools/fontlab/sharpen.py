#!/usr/bin/env python3
"""fontlab/sharpen.py — the phase-18 STEM-SNAP pass: harden a baked .bcfnt's alpha.

WHY THIS EXISTS
---------------
Phase 18's S1 slice made every text draw land at texel scale EXACTLY 1.0 with an integer
origin (source/typography.h R1). That removed *resampling* blur — and the user still saw
soft text, because a second, independent cause was left standing: **the bake itself is
soft**. Measured over the alphabet of the seven shipped faces:

    face          texels at alpha=255      stranded 25%..75%
    jbm_med_7               0.0 %                 87.7 %
    jbm_med_9               0.0 %                 75.6 %
    sg_med_10               0.4 %                 66.1 %
    sg_med_12               2.0 %                 63.9 %

`mkbcfnt` (v2.3.0) rasterises the TTF with UNHINTED grayscale AA and offers no hinting,
gamma or contrast option (`--help` lists only -o/-s/-b/-w). At 7-12 px a Medium-weight
stem is ~0.5 device pixels wide, so it straddles two pixel columns and lands on neither:
`jbm_med_7` contains **not one fully opaque texel in the entire alphabet** — its modal ink
value is nibble 8 of 15, i.e. the text is literally 50 % grey mush. Drawing that at a
perfect 1.0 reproduces the mush perfectly.

WHAT THIS DOES
--------------
Hinting proper would move outlines, which changes advance widths and would desynchronise
the CWDH metrics from the sheet. This pass does the part of hinting that needs no outline
surgery — **stem snapping in the alpha domain**: a monotone transfer curve that pulls a
straddled stem up toward solid ink and pushes the antialiasing halo down toward clear.

The whole transform is a **16-entry lookup table per face**. A bcfnt glyph sheet is A4
(4-bit alpha), so there are only 16 possible values; remapping them is the *complete*
space of order-preserving alpha edits. Consequences worth stating, because they are what
make this safe to ship:

  * Nothing but alpha changes. FINF/TGLP/CWDH/CMAP, the cell grid, `lineFeed`, every
    advance width and the file size are byte-identical, so R1 (px == lineFeed) and every
    layout measurement in test_typography T10 still hold by construction.
  * LUT[0] == 0 always, so background stays background and glyph counters cannot fill in.
  * The LUT is non-decreasing, so no pixel gets darker than a pixel that was darker than
    it — the glyph's shape is preserved, only its edge profile changes.
  * It is applied to the sheet's raw nibbles, so it is independent of the 8x8 Morton
    tiling: no detile/retile round-trip, nothing to get wrong.

THE CURVE
---------
Two stages, both per face:

  1. GAIN. `ceil` = the alpha at the `--ceil-pct` percentile of the face's own lit texels
     (default 95). Everything at or above it is "a full stroke" and normalises to 1.0.
     This is per-face on purpose: `sg_bold_17` already reaches 255 on 30 % of its ink and
     needs almost no gain, while `jbm_med_7` tops out around 60 % and needs a lot.
  2. CONTRAST. A symmetric power S-curve about `--pivot` (default 0.5) with exponent
     `--gamma` (default 2.2): values above the pivot are driven toward opaque, values
     below it toward clear. Gamma 1.0 degenerates to pure gain, which is why the default
     is not 1.0 — gain alone lifts the halo along with the stem and spreads the blur.

Then quantise back to a nibble. `--report` prints the LUT and the before/after ink
statistics for every face; `--check` re-measures a directory and fails if a face is below
the solidity floor, which is what tools/build_assets.sh calls after baking.

Usage:
    sharpen.py apply  data/fnt_jbm_med_7.bin [--gamma 2.2] [--ceil-pct 95] [--pivot 0.5]
    sharpen.py report data/                  # before/after table, no writes
    sharpen.py check  data/ [--min-solid 30] # exit 1 if a face is still mush
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bcfnt import Bcfnt  # noqa: E402

# The alphabet the statistics are measured over. Deliberately letters+digits and not the
# whole sheet: the sheet is 1024x1024 and mostly empty cells, and punctuation/box glyphs
# have ink distributions that are not representative of running text.
ALPHABET = ("ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz"
            "0123456789")

DEF_CEIL_PCT = 95.0
DEF_GAMMA = 2.2
DEF_PIVOT = 0.5
# A face whose ink is at least this share >= 75% alpha counts as "snapped". 30 % is the
# floor the shipped ladder clears with margin; it is a tripwire against a silent mkbcfnt
# change, not a tuning target.
DEF_MIN_SOLID = 30.0

SOLID_NIB = 11.25   # 75 % of 15
MID_LO_NIB = 3.75   # 25 % of 15


def face_hist(font):
    """Histogram of alpha nibbles over ALPHABET. h[0] = clear texels."""
    h = [0] * 16
    for ch in ALPHABET:
        gi = font.glyph_index(ch)
        if gi is None:
            continue
        for row in font.glyph_bitmap(gi):
            for a in row:
                h[a // 17] += 1
    return h


def make_lut(hist, ceil_pct=DEF_CEIL_PCT, gamma=DEF_GAMMA, pivot=DEF_PIVOT):
    """-> (lut[16], ceil_nibble). Monotone, lut[0] == 0."""
    lit = sum(hist[1:])
    ceil_n = 15
    if lit:
        acc = 0
        for n in range(1, 16):
            acc += hist[n]
            if acc >= lit * ceil_pct / 100.0:
                ceil_n = n
                break
    ceil_n = max(1, ceil_n)
    lut = []
    for n in range(16):
        t = min(1.0, n / float(ceil_n))
        if t <= pivot:
            o = pivot * ((t / pivot) ** gamma) if pivot > 0 else 0.0
        else:
            o = 1.0 - (1.0 - pivot) * (((1.0 - t) / (1.0 - pivot)) ** gamma)
        lut.append(max(0, min(15, int(round(o * 15)))))
    lut[0] = 0
    # enforce monotonicity explicitly rather than trusting the algebra
    for n in range(1, 16):
        if lut[n] < lut[n - 1]:
            lut[n] = lut[n - 1]
    return lut, ceil_n


def hist_stats(hist, lut=None):
    """-> dict of solid%/mid%/lit-share, over the lit texels of the ORIGINAL."""
    lit = sum(hist[1:])
    if not lit:
        return dict(solid=0.0, mid=0.0, ink=0.0, opaque=0.0)
    solid = mid = opaque = ink = 0
    for n in range(1, 16):
        o = lut[n] if lut else n
        c = hist[n]
        ink += o * c
        if o >= 15:
            opaque += c
        if o >= SOLID_NIB:
            solid += c
        if MID_LO_NIB <= o <= SOLID_NIB:
            mid += c
    return dict(solid=100.0 * solid / lit, mid=100.0 * mid / lit,
                opaque=100.0 * opaque / lit, ink=float(ink))


def apply_lut_bytes(raw, font, lut):
    """Remap every alpha nibble of every glyph sheet. Returns a new bytes object.

    Works on the raw sheet bytes, so it is independent of the 8x8 Morton swizzle: a LUT
    applied to a permuted set is the same permutation of the LUT applied to the set.
    """
    out = bytearray(raw)
    base = font.sheet_data_off
    end = base + font.n_sheets * font.sheet_size
    if end > len(out):
        raise ValueError("sheet range %d..%d past EOF %d" % (base, end, len(out)))
    tbl = bytes(bytearray((lut[hi] << 4) | lut[lo]
                          for hi in range(16) for lo in range(16)))
    for i in range(base, end):
        out[i] = tbl[out[i]]
    return bytes(out)


def sharpen_file(path, out_path=None, ceil_pct=DEF_CEIL_PCT, gamma=DEF_GAMMA,
                 pivot=DEF_PIVOT, min_solid=DEF_MIN_SOLID, force=False):
    """Apply the stem-snap LUT in place (or to out_path).

    NOT IDEMPOTENT by construction — the gain stage renormalises against the face's OWN
    distribution, so a second pass would re-normalise an already-snapped face and drive it
    toward a 1-bit mask. `mkbcfnt` output is freshly rasterised on every bake, so
    build_assets.sh is safe; a hand-run is guarded here instead of being made a footgun.
    Returns None (and writes nothing) when the face is already above the solidity floor,
    unless `force`.
    """
    font = Bcfnt(path)
    hist = face_hist(font)
    if not force and hist_stats(hist)["solid"] >= min_solid:
        return None
    lut, ceil_n = make_lut(hist, ceil_pct, gamma, pivot)
    before = hist_stats(hist)
    after = hist_stats(hist, lut)
    data = apply_lut_bytes(font.raw, font, lut)
    if out_path is None:
        out_path = path
    with open(out_path, "wb") as f:
        f.write(data)
    return dict(path=path, lut=lut, ceil=ceil_n, before=before, after=after,
                line_feed=font.line_feed, size=len(data))


def _faces(target):
    if os.path.isdir(target):
        return [os.path.join(target, f) for f in sorted(os.listdir(target))
                if f.startswith("fnt_") and f.endswith(".bin")]
    return [target]


def _row(name, lf, b, a, ratio):
    return ("  %-14s lf=%-3d solid %5.1f%% -> %5.1f%%   opaque %5.1f%% -> %5.1f%%   "
            "stranded %5.1f%% -> %5.1f%%   ink x%.2f" %
            (name, lf, b["solid"], a["solid"], b["opaque"], a["opaque"],
             b["mid"], a["mid"], ratio))


def cmd_apply(args):
    rc = 0
    for p in _faces(args.target):
        name = os.path.basename(p)[4:-4]
        r = sharpen_file(p, None, args.ceil_pct, args.gamma, args.pivot,
                         args.min_solid, args.force)
        if r is None:
            print("  %-14s already snapped (solid >= %.0f%%), left alone"
                  % (name, args.min_solid))
            continue
        ratio = r["after"]["ink"] / r["before"]["ink"] if r["before"]["ink"] else 1.0
        print(_row(name, r["line_feed"], r["before"], r["after"], ratio))
        if args.verbose:
            print("        ceil=nibble %-2d  LUT %s" % (r["ceil"], r["lut"]))
        if r["after"]["solid"] < args.min_solid:
            print("  !! %s still below the %.0f%% solidity floor" % (name, args.min_solid),
                  file=sys.stderr)
            rc = 1
    return rc


def cmd_report(args):
    print("%-14s %4s %8s %8s %8s %8s" %
          ("face", "lf", "opaque", "solid", "stranded", "ceilNib"))
    for p in _faces(args.target):
        font = Bcfnt(p)
        h = face_hist(font)
        s = hist_stats(h)
        lut, ceil_n = make_lut(h, args.ceil_pct, args.gamma, args.pivot)
        print("%-14s %4d %7.1f%% %7.1f%% %7.1f%% %8d" %
              (os.path.basename(p)[4:-4], font.line_feed, s["opaque"], s["solid"],
               s["mid"], ceil_n))
        if args.verbose:
            print("     would-be LUT %s" % lut)
    return 0


def cmd_check(args):
    rc = 0
    for p in _faces(args.target):
        font = Bcfnt(p)
        s = hist_stats(face_hist(font))
        name = os.path.basename(p)[4:-4]
        ok = s["solid"] >= args.min_solid
        print("  %-14s solid %5.1f%%  %s" % (name, s["solid"], "OK" if ok else "SOFT"))
        if not ok:
            print("  !! %s: %.1f%% of its ink is >= 75%% alpha, floor is %.0f%%. "
                  "Was tools/fontlab/sharpen.py run after the bake?"
                  % (name, s["solid"], args.min_solid), file=sys.stderr)
            rc = 1
    return rc


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("cmd", choices=("apply", "report", "check"))
    ap.add_argument("target", help="a .bcfnt/.bin file, or a directory of fnt_*.bin")
    ap.add_argument("--ceil-pct", type=float, default=DEF_CEIL_PCT)
    ap.add_argument("--gamma", type=float, default=DEF_GAMMA)
    ap.add_argument("--pivot", type=float, default=DEF_PIVOT)
    ap.add_argument("--min-solid", type=float, default=DEF_MIN_SOLID)
    ap.add_argument("--force", action="store_true",
                    help="re-apply even to a face that already clears the floor")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args(argv)
    return dict(apply=cmd_apply, report=cmd_report, check=cmd_check)[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
