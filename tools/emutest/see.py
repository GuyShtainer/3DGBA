#!/usr/bin/env python3
"""see.py — the SEE channel: find the Azahar window, screencapture it, crop the 3DS
screens (phase-16 slice E4; SPEC-harness H2.4/H2.6, SPEC-protocols S4).

Design sources (house rule: every protocol fact cites where it came from):
  - SPEC-protocols S4.1: with the profile's layout_option=0 the render area is the 400:480
    emulation box aspect-fit + centered (Azahar release framebuffer_layout.cpp
    LargeFrameLayout — scale=min(W/400,H/480), int truncation, BelowLarge centering).
    The math is replicated exactly in layout_rects() and host-tested (tests/test_layout.py).
  - SPEC-protocols S4.5: do NOT forge the Qt geometry blob; measure the real window per
    shot via Quartz and parameterize the crop math (SPEC-harness H1.6 decision).
  - PHASE.md invariant 4: the permission-gated channel SKIPs loudly (exit 75), never fails
    the harness and never silently passes.

Live-calibrated constants (this machine, 2026-08-08, slice E4 — BUILDLOG has the probe):
  - Window: kCGWindowOwnerName == 'Azahar' (capital A; the *process* is `azahar`),
    single layer-0 window titled 'Azahar <version>'.
  - Retina factor: screencapture -l returns exactly 2.0x the Quartz point bounds
    (probed: bounds 1280x568 pt -> image 2560x1136 px). Never assumed: recomputed per
    shot as img_w / bounds_w (S4.5 "calibrate, don't hardcode").
  - Title bar: 28 pt (56 px @2x — probed: first render row at y=56; macOS standard
    title-bar height; the S3 profile removes the status bar so the rest of the window
    IS the render client area). [M] TITLE_BAR_PT below.
  - Crop-math proof: at client 2560x1080 the S4.1 rects (830,0)-(1730,540) /
    (920,540)-(1640,1080) cropped exactly the app's top HUD row and the bottom
    "tap screen / pause menu" line — no bleed (BUILDLOG E4, images in the run dir).

Permission model (macOS TCC):
  - Screen Recording: preflighted via CGPreflightScreenCaptureAccess() (Quartz, macOS
    10.15+). Missing -> exit 75 with grant instructions. Granted + verified working on
    this machine 2026-08-08 (window contents visible in captures).
  - Quartz window METADATA (id/bounds) needs no permission; without Screen Recording the
    capture would show wallpaper only — hence the preflight, never a silent pass.

CLI (H2.4, via the venv shim):
  see shot top|bottom|both OUT.png [--raw-window FULL.png]   crop the live window
  see win                                                    print window id/bounds/title
Exit codes (H2): 0 ok, 1 fail (no window / capture error), 75 SKIP (permission missing).

`both` writes OUT.top.png + OUT.bottom.png (a single OUT.png cannot hold two screens
of different widths honestly; the two files feed sheet.py's dual-screen cells).
"""

import argparse
import os
import subprocess
import sys
import tempfile

EXIT_SKIP = 75  # sysexits EX_TEMPFAIL (SPEC-harness H2)

# [M] measured 2026-08-08 (module doc): macOS title-bar height in points; subtracted from
# the top of the captured window to get the Qt render client area.
TITLE_BAR_PT = 28

# 3DS screen geometry (Azahar release core/3ds.h: kScreenTopWidth 400, height 240;
# kScreenBottomWidth 320, height 240; the S4.1 emulation box is 400x480).
TOP_W, TOP_H = 400, 240
BOT_W, BOT_H = 320, 240
BOX_W, BOX_H = 400, 480


def layout_rects(W, H):
    """SPEC-protocols S4.1 exactly (release framebuffer_layout.cpp LargeFrameLayout,
    layout_option=0, gap=0, no swap/upright/stretch/integer-scale): -> (top, bottom)
    rects as (x0, y0, x1, y1) in client-area pixels. All truncations replicated —
    host-tested against the S4.2 worked examples."""
    if W <= 0 or H <= 0:
        raise ValueError("empty client area {}x{}".format(W, H))
    scale = min(W / BOX_W, H / BOX_H)              # MaxRectangle, float
    tw, th = int(BOX_W * scale), int(BOX_H * scale)
    ox, oy = (W - tw) // 2, (H - th) // 2          # centering
    sa = th / float(BOX_H)                          # scale_amount
    top = (ox, oy, ox + int(TOP_W * sa), oy + int(TOP_H * sa))
    bh, bw = int(BOT_H * sa), int(BOT_W * sa)
    bx = ox + int(TOP_W * sa) // 2 - int(BOT_W * sa) // 2   # BelowLarge centering
    by = oy + (top[3] - top[1])
    bottom = (bx, by, bx + bw, by + bh)
    return top, bottom


def _quartz():
    """Import pyobjc Quartz lazily (SPEC-harness H1.2: only see.py needs it; every other
    tool must run without the wheel)."""
    try:
        import Quartz  # noqa: F401
        return Quartz
    except ImportError as e:
        print("see: FAIL — pyobjc Quartz not importable ({}). Run tools/emutest/setup.sh "
              "to rebuild the venv.".format(e), file=sys.stderr)
        raise SystemExit(1)


def screen_recording_granted():
    q = _quartz()
    try:
        return bool(q.CGPreflightScreenCaptureAccess())
    except AttributeError:
        # macOS < 10.15 has no preflight — fall through and let the capture content
        # check (below) decide. Not expected on this machine (Darwin 24).
        return True


def skip_no_permission():
    print("see: SKIP — Screen Recording permission missing for this session's host app.")
    print("  Grant it: System Settings -> Privacy & Security -> Screen Recording ->")
    print("  enable the host app (Visual Studio Code), then QUIT AND REOPEN it —")
    print("  the grant only applies after a restart. A SKIP is never a pass (PHASE inv. 4).")
    raise SystemExit(EXIT_SKIP)


def find_window():
    """-> dict(id, x, y, w, h, title) of the Azahar render window, or None.
    Owner match is case-insensitive: Quartz reports owner 'Azahar' while the unix process
    is `azahar` (probed 2026-08-08). Largest layer-0 window wins (there is only one in
    practice: the Qt main window with the embedded render widget)."""
    q = _quartz()
    wins = q.CGWindowListCopyWindowInfo(
        q.kCGWindowListOptionOnScreenOnly | q.kCGWindowListExcludeDesktopElements,
        q.kCGNullWindowID)
    best = None
    for w in wins or []:
        if str(w.get("kCGWindowOwnerName", "")).lower() != "azahar":
            continue
        if int(w.get("kCGWindowLayer", 0)) != 0:
            continue
        b = w.get("kCGWindowBounds") or {}
        cand = {"id": int(w["kCGWindowNumber"]),
                "x": float(b.get("X", 0)), "y": float(b.get("Y", 0)),
                "w": float(b.get("Width", 0)), "h": float(b.get("Height", 0)),
                "title": str(w.get("kCGWindowName", "") or "")}
        if best is None or cand["w"] * cand["h"] > best["w"] * best["h"]:
            best = cand
    return best


def capture_window(win, out_path):
    """screencapture -x (no sound) -o (no shadow) -l<id>: the window image including its
    title bar, at device pixels (2x on this Mac — probed)."""
    r = subprocess.run(["screencapture", "-x", "-o", "-l", str(win["id"]), out_path],
                       capture_output=True, text=True)
    if r.returncode != 0 or not os.path.exists(out_path) or os.path.getsize(out_path) == 0:
        print("see: FAIL — screencapture rc={} stderr={!r}".format(
            r.returncode, r.stderr.strip()), file=sys.stderr)
        raise SystemExit(1)


def shot(which, out_path, raw_window=None):
    """Capture + crop. Returns {name: path} of the written crops."""
    from PIL import Image

    if not screen_recording_granted():
        skip_no_permission()
    win = find_window()
    if win is None:
        print("see: FAIL — no Azahar window on screen (boot first: "
              "tools/emutest/run azctl boot && tools/emutest/run gdbio resume)",
              file=sys.stderr)
        raise SystemExit(1)

    tmp = tempfile.NamedTemporaryFile(suffix=".png", delete=False)
    tmp.close()
    try:
        capture_window(win, tmp.name)
        im = Image.open(tmp.name).convert("RGB")
    finally:
        os.unlink(tmp.name)

    # Retina normalization (S4.5): recompute the pt->px factor per shot, never assume 2x.
    factor = im.size[0] / win["w"] if win["w"] else 1.0
    title_px = int(round(TITLE_BAR_PT * factor))
    W, H = im.size[0], im.size[1] - title_px
    if H <= 0:
        print("see: FAIL — window too small ({}x{} px, title {} px)".format(
            im.size[0], im.size[1], title_px), file=sys.stderr)
        raise SystemExit(1)
    # Content check: a permissionless/foreign capture yields a uniform image (wallpaper
    # was probed pre-grant; post-grant the app HUD is always visible). extrema equal ==
    # nothing captured -> treat as the permission SKIP, never a silent pass.
    if im.convert("L").getextrema()[0] == im.convert("L").getextrema()[1]:
        print("see: capture is a uniform image — Screen Recording grant not effective?")
        skip_no_permission()

    if raw_window:
        im.save(raw_window)
        print("see: raw window -> {} ({}x{} px, factor {:.2f})".format(
            raw_window, im.size[0], im.size[1], factor))

    top, bottom = layout_rects(W, H)
    off = lambda r: (r[0], r[1] + title_px, r[2], r[3] + title_px)  # noqa: E731
    wrote = {}
    base, ext = os.path.splitext(out_path)
    plan = {"top": [("top", out_path)], "bottom": [("bottom", out_path)],
            "both": [("top", base + ".top" + ext), ("bottom", base + ".bottom" + ext)]}
    for name, path in plan[which]:
        rect = off(top if name == "top" else bottom)
        crop = im.crop(rect)
        d = os.path.dirname(os.path.abspath(path))
        if d:
            os.makedirs(d, exist_ok=True)
        crop.save(path)
        wrote[name] = path
        print("see: {} screen {}x{} px (client rect {}) -> {}".format(
            name, crop.size[0], crop.size[1], rect, path))
    return wrote


def cmd_shot(args):
    shot(args.which, args.out, raw_window=args.raw_window)
    return 0


def cmd_win(args):
    if not screen_recording_granted():
        # Metadata itself needs no permission, but a `win` that succeeds while `shot`
        # would SKIP invites confusion — report both facts.
        print("see: note — Screen Recording NOT granted (shot would SKIP)")
    w = find_window()
    if w is None:
        print("see: no Azahar window on screen", file=sys.stderr)
        return 1
    print("id={id} bounds=({x:.0f},{y:.0f} {w:.0f}x{h:.0f} pt) title={title!r}".format(**w))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="SEE channel: capture the Azahar window and crop the 3DS screens "
                    "(SPEC-protocols S4; exit 75 = Screen Recording missing)")
    sub = ap.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("shot", help="capture + crop the live window")
    s.add_argument("which", choices=["top", "bottom", "both"])
    s.add_argument("out", help="output PNG ('both' writes OUT.top.png + OUT.bottom.png)")
    s.add_argument("--raw-window", metavar="FULL.png",
                   help="also save the uncropped window capture")
    s.set_defaults(fn=cmd_shot)

    s = sub.add_parser("win", help="print the Azahar window id/bounds/title")
    s.set_defaults(fn=cmd_win)

    args = ap.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
