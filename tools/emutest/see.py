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

Live-calibrated constants (this machine, 2026-08-08, slice E4 — BUILDLOG E4 has the probe):
  - Window: kCGWindowOwnerName == 'Azahar' (capital A; the *process* is `azahar`),
    exactly ONE on-screen layer-0 window (probed with CGWindowListOptionAll: every other
    Azahar entry is an off-screen 0x0-ish helper). Azahar runs in single-window mode, so
    that same window shows the GAME LIST before boot / after the emulated app exits and
    the two 3DS screens while emulating — the crops are only meaningful while the app
    runs (verify with gdbio: g_renderSeq advancing).
  - Retina factor: screencapture -l returns exactly 2.0x the Quartz point bounds
    (probed: bounds 1280x568 pt -> image 2560x1136 px). Never assumed: recomputed per
    shot as img_w / bounds_w (S4.5 "calibrate, don't hardcode").
  - Title bar: 28 pt (56 px @2x — probed; macOS standard title-bar height; the S3 profile
    removes the status bar so the rest of the window IS the render client area).
    [M] TITLE_BAR_PT below.
  - Crop-math PROOF (live, slice E4): window 1280x568 pt -> client 2560x1080 px -> S4.1
    rects top (830,56)-(1730,596) and bottom (920,596)-(1640,1136) produced a 900x540 top
    crop showing exactly the app's "|| PAUSED / gameA <-> gameB / settings on the touch
    screen" pause screen and a 720x540 bottom crop showing exactly the ENHANCE settings
    tab with the TILT segmented control — no bleed, no chrome (900 = 400x2.25,
    720 = 320x2.25). Frames in runs/20260808-203721/rec-menu/{top,bottom}_00114.png.
  - Capture cost [M]: `screencapture -x -o -l<id>` = 0.08 s per shot (3 timed runs), so
    the loop sustains the requested rate up to ~6 fps (measured 6.04 fps at --fps 6).
    Each --with-state symbol read adds a gdbio halt->read->cont blink (~0.4 s on this
    release) — the measured 4 fps request with one state symbol delivered 1.53 fps.

Permission model (macOS TCC):
  - Screen Recording: preflighted via CGPreflightScreenCaptureAccess() (Quartz, macOS
    10.15+). Missing -> exit 75 with grant instructions. Granted + verified working on
    this machine 2026-08-08 (window contents visible in captures).
  - Quartz window METADATA (id/bounds) needs no permission; without Screen Recording the
    capture would show wallpaper only — hence the preflight, never a silent pass.

CLI (H2.4, via the venv shim):
  see shot top|bottom|both OUT.png [--raw-window FULL.png]   crop the live window
  see rec  [--seconds S] [--fps F] [--screen top|bottom|both] [--out DIR]
           [--with-state SYM,SYM] [--format mp4|gif|none] [--keep-window]
  see win                                                    print window id/bounds/title
Exit codes (H2): 0 ok, 1 fail (no window / capture error), 75 SKIP (permission missing).

`both` writes OUT.top.png + OUT.bottom.png (a single OUT.png cannot hold two screens
of different widths honestly; the two files feed sheet.py's dual-screen cells).

WHY `rec` exists instead of Azahar's own `--dump-video` (added scope, 2026-08-08): the
installed release loads libavutil dynamically with a STRICT major-version check and this
machine's Homebrew ships avutil.60 -> "Could not dynamically load libavutil" (probed).
So the harness records the way it screenshots: a timed window-capture loop whose PNG
frames ARE the deliverable (the model reads PNGs, not video), plus an OPTIONAL ffmpeg
assembly into .mp4/.gif for the user's eyes. If ffmpeg is missing or fails, the frames
still stand and the manifest says video: null with the reason — never claim a video
channel that did not work (PHASE inv. 4's honesty rule applied to the new channel).
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

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


def window_geometry(img_size, win):
    """(image size, Quartz window dict) -> dict(factor, title_px, client W/H, rects).
    Retina normalization (S4.5): the pt->px factor is RECOMPUTED per capture, never
    assumed 2x; the title bar is the only chrome left (the S3 profile removes the status
    bar), so client = image minus the title strip. Rects are already offset into IMAGE
    coordinates, ready for Image.crop(). Host-tested via tests/test_layout.py's math."""
    factor = img_size[0] / win["w"] if win.get("w") else 1.0
    title_px = int(round(TITLE_BAR_PT * factor))
    W, H = img_size[0], img_size[1] - title_px
    if H <= 0:
        raise ValueError("window too small ({}x{} px, title {} px)".format(
            img_size[0], img_size[1], title_px))
    top, bottom = layout_rects(W, H)
    off = lambda r: (r[0], r[1] + title_px, r[2], r[3] + title_px)  # noqa: E731
    return {"factor": factor, "title_px": title_px, "client": [W, H],
            "rects": {"top": off(top), "bottom": off(bottom)}}


def _grab(win):
    """Capture the window to a temp PNG and return it as a PIL RGB image (+ deletes tmp).
    Raises SystemExit(1) on capture failure (capture_window prints the reason)."""
    from PIL import Image
    tmp = tempfile.NamedTemporaryFile(suffix=".png", delete=False)
    tmp.close()
    try:
        capture_window(win, tmp.name)
        im = Image.open(tmp.name)
        im.load()                       # force the decode before the file disappears
        return im.convert("RGB")
    finally:
        os.unlink(tmp.name)


def _uniform(im):
    """True when the image has a single luminance value — what a capture WITHOUT the
    Screen Recording grant looks like (probed pre-grant: wallpaper/blank). Post-grant the
    app HUD is always drawn, so this is the silent-pass tripwire."""
    lo, hi = im.convert("L").getextrema()
    return lo == hi


def _uniform_verdict(im, where):
    """Decide what a flat capture MEANS, and exit accordingly.

    REVIEW FIX (2026-08-09): both call sites used to jump straight to
    skip_no_permission() — exit 75, "Screen Recording permission missing". But
    require_window() has already preflighted CGPreflightScreenCaptureAccess(), so
    reaching here normally PROVES the grant is effective, and the SKIP laundered a real
    defect into a non-failure. A black/blank screen is precisely the bug class this
    project keeps chasing (HANDOFF: the post-trade black screen), and PHASE invariant 4
    says a SKIP is never a pass — it must also never be a swallowed FAIL. The grant is
    re-checked here (it can be revoked mid-run) and only THEN does 75 apply."""
    if not screen_recording_granted():
        print("see: capture is a uniform image AND the Screen Recording grant is gone "
              "(revoked mid-run?)")
        skip_no_permission()
    if display_asleep():          # PHASE 18: the display can fall asleep MID-session too
        skip_display_asleep()
    lo, _hi = im.convert("L").getextrema()
    print("see: FAIL — {} is a single flat colour (luminance {}) while the Screen "
          "Recording grant IS effective. That is NOT a permission problem: the window is "
          "occluded/minimised/offscreen, or the app really is drawing a blank screen "
          "(a real defect — see HANDOFF's black-screen class).".format(where, lo),
          file=sys.stderr)
    raise SystemExit(1)


def display_asleep():
    """True when the main display is asleep — every capture is then uniformly black.

    PHASE 18. This is the real cause of the 'see returns pure black' defect that three
    phase-18 slices recorded as an unexplained environmental problem and shipped without
    pixel evidence for (SPEC-crisp Q1, SEVEN reproductions). It is not Azahar and not TCC:
    the sessions ran late on an idle machine, the display slept, and `screencapture` of the
    WHOLE display returns a 1-colour image in that state too. Nothing warned, because a
    black PNG is a perfectly valid PNG."""
    q = _quartz()
    try:
        return bool(q.CGDisplayIsAsleep(q.CGMainDisplayID()))
    except Exception:
        return False       # can't tell -> don't invent a failure


def skip_display_asleep():
    print("see: SKIP — the main display is ASLEEP, so every capture would be uniform black.")
    print("  Wake it and keep it awake for the capture session:")
    print("    nohup caffeinate -u -t 900 >/dev/null 2>&1 &")
    print("  (a SKIP is never a pass — PHASE invariant 4)")
    raise SystemExit(EXIT_SKIP)


def require_window(context=""):
    """Preflight the TCC grant + locate the window, or exit 75 / 1 with instructions."""
    if not screen_recording_granted():
        skip_no_permission()
    if display_asleep():
        skip_display_asleep()
    win = find_window()
    if win is None:
        print("see: FAIL — no Azahar window on screen{} (boot first: "
              "tools/emutest/run azctl boot && tools/emutest/run gdbio resume)".format(
                  context), file=sys.stderr)
        raise SystemExit(1)
    return win


def shot(which, out_path, raw_window=None):
    """Capture + crop. Returns {name: path} of the written crops."""
    win = require_window()
    im = _grab(win)
    try:
        geo = window_geometry(im.size, win)
    except ValueError as e:
        print("see: FAIL — {}".format(e), file=sys.stderr)
        raise SystemExit(1)
    # The raw window is saved FIRST so the evidence survives a flat-capture failure below.
    if raw_window:
        d = os.path.dirname(os.path.abspath(raw_window))
        if d:
            os.makedirs(d, exist_ok=True)     # E4 live bug: --raw-window into a fresh
        im.save(raw_window)                   # run-dir subdir crashed (crops mkdir'd, this didn't)
        print("see: raw window -> {} ({}x{} px, factor {:.2f})".format(
            raw_window, im.size[0], im.size[1], geo["factor"]))

    # Content check: never a silent pass — SKIP only if the grant is actually gone,
    # otherwise FAIL (see _uniform_verdict).
    if _uniform(im):
        _uniform_verdict(im, "the captured window")

    wrote = {}
    base, ext = os.path.splitext(out_path)
    plan = {"top": [("top", out_path)], "bottom": [("bottom", out_path)],
            "both": [("top", base + ".top" + ext), ("bottom", base + ".bottom" + ext)]}
    for name, path in plan[which]:
        rect = geo["rects"][name]
        crop = im.crop(rect)
        d = os.path.dirname(os.path.abspath(path))
        if d:
            os.makedirs(d, exist_ok=True)
        crop.save(path)
        wrote[name] = path
        print("see: {} screen {}x{} px (client rect {}) -> {}".format(
            name, crop.size[0], crop.size[1], rect, path))
    return wrote


# ================================================================================== rec
# The video channel (added scope 2026-08-08 — module doc explains why not --dump-video).
# Everything below is deliberately split into pure helpers (host-tested in
# tests/test_see_rec.py) + one live loop, so the naming/schedule/ffmpeg contract is
# provable without a running emulator.

FFMPEG_FALLBACK = "/opt/homebrew/bin/ffmpeg"    # probed on this machine (ffmpeg 8.1.2)


def rec_plan(seconds, fps):
    """-> (n_frames, interval_s). At least one frame; the loop paces off a fixed schedule
    (t0 + i*interval) so a slow capture steals from the NEXT sleep instead of drifting."""
    if seconds <= 0 or fps <= 0:
        raise ValueError("--seconds and --fps must be > 0")
    n = max(1, int(round(seconds * fps)))
    return n, 1.0 / fps


def frame_name(kind, idx):
    """Deterministic frame naming — 'top_00037.png' is addressable in a bug report
    ("look at frame 37") and is exactly what ffmpeg's %05d pattern consumes."""
    return "{}_{:05d}.png".format(kind, idx)


def frame_pattern(kind):
    return "{}_%05d.png".format(kind)


def ffmpeg_bin():
    """PATH first, then the probed Homebrew location, then EMUTEST_FFMPEG override."""
    env = os.environ.get("EMUTEST_FFMPEG")
    if env:
        return env if os.path.exists(env) else None
    found = shutil.which("ffmpeg")
    if found:
        return found
    return FFMPEG_FALLBACK if os.path.exists(FFMPEG_FALLBACK) else None


def ffmpeg_cmd(binary, directory, kind, fps, out_path, fmt="mp4"):
    """The assembly command. mp4: libx264 + yuv420p (QuickTime/Preview-friendly) and an
    even-dimension scale filter (h264 requires even width/height; a 320x240-scaled crop
    can land odd after the S4.1 truncations). gif: a palette-free single pass — small
    clips only, it is a convenience not a codec study."""
    src = os.path.join(directory, frame_pattern(kind))
    base = ["-y", "-framerate", "{:.4f}".format(fps), "-start_number", "0", "-i", src]
    if fmt == "gif":
        return [binary] + base + ["-vf", "scale=trunc(iw/2)*2:trunc(ih/2)*2:flags=neighbor",
                                  out_path]
    return [binary] + base + ["-vf", "scale=trunc(iw/2)*2:trunc(ih/2)*2",
                              "-c:v", "libx264", "-pix_fmt", "yuv420p", out_path]


def default_rec_dir():
    """Evidence goes in the run dir (H2.8): tools/emutest/state/last_run points at the
    newest azctl run dir; fall back to runs/ad-hoc-<stamp> when nothing was booted by us."""
    here = os.path.dirname(os.path.abspath(__file__))
    stamp = time.strftime("%Y%m%d-%H%M%S", time.gmtime())
    try:
        with open(os.path.join(here, "state", "last_run")) as f:
            last = f.read().strip()
        if last and os.path.isdir(last):
            return os.path.join(last, "rec-" + stamp)
    except OSError:
        pass
    return os.path.join(here, "runs", "adhoc-" + stamp, "rec")


class _StateReader:
    """--with-state: reads named globals through gdbio's broker at each frame. Each read
    is a halt->read->cont blink (~0.4 s on this release — gdbio module doc), so it COSTS
    frame rate; the manifest records the real timestamps either way. Any RSP error is
    recorded once and recording continues — a state hiccup must never lose the frames."""

    def __init__(self, specs, width=4):
        self.specs = specs
        self.width = width
        self.error = None
        self.broker = None
        self.addrs = {}
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import gdbio
        self.gdbio = gdbio
        try:
            cache = gdbio.build_symtab(quiet=True)
            for s in specs:
                self.addrs[s] = gdbio.lookup(cache, s)[0]
            self.broker = gdbio.Broker()
            self.broker.connect(autostart=True)
        except Exception as e:                        # noqa: BLE001 (never fail the rec)
            self.error = "{}: {}".format(type(e).__name__, e)
            self.broker = None

    def read(self):
        if self.broker is None:
            return None
        out = {}
        for s, addr in self.addrs.items():
            try:
                out[s] = int.from_bytes(self.broker.read_mem(addr, self.width), "little")
            except Exception as e:                    # noqa: BLE001
                self.error = self.error or "{}: {}".format(type(e).__name__, e)
                out[s] = None
        return out

    def close(self):
        if self.broker is not None:
            self.broker.close()
            self.broker = None


def rec(seconds, fps, which, out_dir, state_specs=None, fmt="mp4", keep_window=False,
        state_width=4):
    """The capture loop. Returns the manifest dict (also written as manifest.json)."""
    n_frames, interval = rec_plan(seconds, fps)
    win = require_window(" to record")
    os.makedirs(out_dir, exist_ok=True)
    kinds = ["top", "bottom"] if which == "both" else [which]

    reader = _StateReader(state_specs, width=state_width) if state_specs else None
    manifest = {
        "tool": "see.py rec", "format_version": 1,
        "started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "seconds_requested": seconds, "fps_requested": fps,
        "frames_planned": n_frames, "screens": kinds, "dir": os.path.abspath(out_dir),
        "state_symbols": list(state_specs or []), "state_error": None,
        "window": {"id": win["id"], "bounds": [win["x"], win["y"], win["w"], win["h"]],
                   "title": win["title"]},
        "frames": [], "video": None, "video_error": None, "ended_reason": "duration",
    }

    t0 = time.time()
    geo0 = None
    print("see rec: {} frames @ {} fps ({}s) of {} -> {}".format(
        n_frames, fps, seconds, "+".join(kinds), out_dir))
    for i in range(n_frames):
        target = t0 + i * interval
        now = time.time()
        if now < target:
            time.sleep(target - now)
        # The window can vanish mid-clip (the Tier-A movie ends with an in-app QUIT):
        # that is a legitimate end, not a failure — keep every frame captured so far.
        w = find_window()
        if w is None:
            manifest["ended_reason"] = "window-gone"
            print("see rec: window disappeared at frame {} (app quit?) — stopping".format(i))
            break
        im = _grab(w)
        if geo0 is None:
            if _uniform(im):
                _uniform_verdict(im, "the first recorded frame")
            geo0 = window_geometry(im.size, w)
            manifest["geometry"] = geo0
        elif im.size != tuple(
                [geo0["client"][0], geo0["client"][1] + geo0["title_px"]]):
            # A resize would change every crop size and break the video assembly; stop
            # and keep what we have (honest partial > silently ragged frames).
            manifest["ended_reason"] = "window-resized"
            print("see rec: window resized at frame {} — stopping".format(i))
            break
        rel = time.time() - t0
        # entry["i"] is BOTH the schedule index and the file index: the loop never skips a
        # frame silently (every abnormal case above `break`s), so manifest frame i is always
        # <kind>_%05d.png with the same i — that is what makes "look at frame 37" citable.
        entry = {"i": i, "t_rel": round(rel, 3),
                 "t_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), "files": {}}
        for kind in kinds:
            path = os.path.join(out_dir, frame_name(kind, len(manifest["frames"])))
            im.crop(geo0["rects"][kind]).save(path)
            entry["files"][kind] = os.path.basename(path)
        if keep_window:
            wpath = os.path.join(out_dir, frame_name("window", len(manifest["frames"])))
            im.save(wpath)
            entry["files"]["window"] = os.path.basename(wpath)
        if reader is not None:
            entry["state"] = reader.read()
        manifest["frames"].append(entry)

    if reader is not None:
        manifest["state_error"] = reader.error
        reader.close()

    n = len(manifest["frames"])
    span = manifest["frames"][-1]["t_rel"] - manifest["frames"][0]["t_rel"] if n > 1 else 0
    fps_actual = (n - 1) / span if span > 0 else float(fps)
    manifest["frames_captured"] = n
    manifest["fps_actual"] = round(fps_actual, 3)
    manifest["seconds_actual"] = round(span, 3)

    # --- optional ffmpeg assembly (frames are the deliverable; video is a bonus) -------
    if fmt != "none" and n >= 2:
        binary = ffmpeg_bin()
        if binary is None:
            manifest["video_error"] = ("ffmpeg not found (PATH / {} / $EMUTEST_FFMPEG) — "
                                       "frames kept, video SKIPPED".format(FFMPEG_FALLBACK))
        else:
            vids = {}
            for kind in kinds:
                out = os.path.join(out_dir, "rec_{}.{}".format(kind, fmt))
                cmd = ffmpeg_cmd(binary, out_dir, kind, fps_actual, out, fmt)
                r = subprocess.run(cmd, capture_output=True, text=True)
                if r.returncode != 0 or not os.path.exists(out) or os.path.getsize(out) == 0:
                    manifest["video_error"] = "ffmpeg rc={} — {}".format(
                        r.returncode, (r.stderr or "").strip().splitlines()[-1:] or "")
                    break
                vids[kind] = {"path": os.path.basename(out),
                              "bytes": os.path.getsize(out), "fps": round(fps_actual, 3)}
            if vids and manifest["video_error"] is None:
                manifest["video"] = vids
    elif fmt == "none":
        manifest["video_error"] = "assembly disabled (--format none) — frames only"
    else:
        manifest["video_error"] = "only {} frame(s) — nothing to assemble".format(n)

    with open(os.path.join(out_dir, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=1)

    print("see rec: {} frames in {:.1f}s = {:.2f} fps actual ({}); manifest.json written"
          .format(n, span, fps_actual, manifest["ended_reason"]))
    for kind in kinds:
        print("see rec:   {} frames: {}/{}".format(
            kind, out_dir, frame_pattern(kind)))
    if manifest["video"]:
        for kind, v in manifest["video"].items():
            print("see rec:   video: {}/{} ({} bytes)".format(out_dir, v["path"], v["bytes"]))
    else:
        print("see rec:   video: SKIPPED — {}".format(manifest["video_error"]))
    if manifest["state_error"]:
        print("see rec:   state: PARTIAL — {}".format(manifest["state_error"]))
    return manifest


def cmd_shot(args):
    shot(args.which, args.out, raw_window=args.raw_window)
    return 0


def cmd_rec(args):
    out_dir = args.out or default_rec_dir()
    specs = [s for s in (args.with_state or "").split(",") if s.strip()]
    m = rec(args.seconds, args.fps, args.screen, out_dir, state_specs=specs,
            fmt=args.format, keep_window=args.keep_window, state_width=args.state_width)
    # Exit 0 whenever FRAMES exist (the deliverable); the manifest's video/state fields
    # carry the honest sub-verdicts for smoke.sh to turn into PASS/SKIP rows.
    return 0 if m["frames_captured"] > 0 else 1


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
    s.add_argument("--wake", action="store_true",
                   help="wake a sleeping display first (synchronous `caffeinate -u -t 3`). A background caffeinate is NOT reliable — observed four live caffeinate processes with the display asleep anyway; a short synchronous one immediately before the grab is. Off by default: waking the user's display is a side effect they should ask for.")
    s.set_defaults(fn=cmd_shot)

    s = sub.add_parser("rec", help="timed capture loop -> PNG frames + manifest + video")
    s.add_argument("--seconds", type=float, default=8.0, help="clip length (default 8)")
    s.add_argument("--fps", type=float, default=5.0,
                   help="target capture rate (default 5; measured ceiling ~6 fps — a "
                        "window screencapture costs 0.08 s)")
    s.add_argument("--screen", choices=["top", "bottom", "both"], default="both")
    s.add_argument("--out", metavar="DIR",
                   help="output dir (default: <last azctl run dir>/rec-<UTC stamp>)")
    s.add_argument("--with-state", metavar="SYM[,SYM...]",
                   help="read these globals over gdbio at every frame into manifest.json "
                        "(costs frame rate AND emulated speed: each read is a "
                        "halt->read->cont blink, ~0.4 s — measured 4 fps -> 1.53 fps)")
    s.add_argument("--state-width", type=int, default=4,
                   help="bytes per --with-state read (default 4 = u32)")
    s.add_argument("--format", choices=["mp4", "gif", "none"], default="mp4",
                   help="ffmpeg assembly of the frame sequence (default mp4)")
    s.add_argument("--wake", action="store_true",
                   help="wake a sleeping display first (synchronous `caffeinate -u -t 3`). A background caffeinate is NOT reliable — observed four live caffeinate processes with the display asleep anyway; a short synchronous one immediately before the grab is. Off by default: waking the user's display is a side effect they should ask for.")
    s.add_argument("--keep-window", action="store_true",
                   help="also keep the uncropped window frames")
    s.set_defaults(fn=cmd_rec)

    s = sub.add_parser("win", help="print the Azahar window id/bounds/title")
    s.set_defaults(fn=cmd_win)

    args = ap.parse_args(argv)
    if getattr(args, "wake", False):
        try:
            subprocess.run(["caffeinate", "-u", "-t", "3"], timeout=10)
        except Exception as e:
            print("see: --wake failed (%s); continuing" % e, file=sys.stderr)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
