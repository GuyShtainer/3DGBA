#!/usr/bin/env python3
"""sdmc.py — the virtual-SD bridge: control-file writer (D4 input channel) + netlog/CSV
readers (phase-16 slice E3; SPEC-harness H4, PHASE.md READ-STATE/PRESS channels b).

On Azahar the 3DS SD card is a plain host directory (probed 2026-08-08:
~/Library/Application Support/Azahar/sdmc). The 3DGBA app already ships a file-driven
input channel (phase 13 D4) and file diagnostics (D1-D3), so this tool is pure plumbing —
every protocol fact below cites the app source it comes from:

  INPUT  (write): sdmc:/cias/control/ — the app arms the whole channel iff this directory
    exists at SESSION start (one stat, main.c:2521; arm BEFORE the session begins).
    Files, read by ctl_path naming "<kind>_p<seat+1>.txt" (main.c:301-303):
      move_p{1,2}.txt      movement/button script; CONSUMED ON PICKUP via remove()
                           (main.c:460) => file disappearance is the filesystem ACK.
                           Body grammar (control.h token table, host-tested there):
                           walks L<n> R<n> U<n> D<n> (lowercase = sprint, adds B),
                           bare L/R/U/D tap-turn, buttons a b s c x y
                           (A B START SELECT L R), waits W<n>/w<n>, G (wait for go file,
                           token 0 only), first-byte '!' = abort file (honoured
                           MID-script, main.c:445-455). <= CTL_TOK_MAX 64 tokens.
      go_p{1,2}.txt        one-shot trigger for a G-held script (consumed; D4.10)
      record_p{1,2}.txt    D5 recorder arm marker (consumed; main.c:352-361)
      replay_p{1,2}.txt    D5 replay table, loaded when...
      replay_go_p{1,2}.txt ...this one-shot appears (consumed; main.c:363-390)
      diag_off.txt         D1-D3 kill switch (checked per session, main.c:232)
    Seats are FIXED: p1 = game A, p2 = game B (main.c:245-246). PICKUP NEEDS A LIVE CORE:
    the per-seat tick starts with `if (!coFor[sq]) continue;` (main.c:2947), so a ROM-less
    session NEVER consumes scripts — Tier B (ROM fixtures) is the live context (H3.6).

  OUTPUT (read): sdmc:/cias/netlogs/ — all files named
    "3DGBA_<kind>[_<ROLE>]_<MMDD>_<HHMMSS>.<ext>" (diag_log_path, main.c:195-210):
      control  the D4/D5 status log; FIRST line "# control p1=<code> p2=<code> dir=..."
               (main.c:2526-2528), then "[ctl pN] picked up <n> tokens" etc, fflush per
               line (main.c:276-285). Created lazily at the FIRST status line.
      gs       game-state timeline, dumped on Quit/Change-games (gs_dump, main.c:4147)
               and on link error — ONLY if something was captured (gamestate.c
               gamestate_log_dump returns early when s_gsLogN == 0, i.e. non-Pokemon or
               ROM-less runs write NO gs file — smoke must not expect one there).
      touch    touch-event log, same dump sites + same empty-skip (touch.c).
      csv      D3 per-frame telemetry — WIRELESS SESSIONS ONLY (opened at wl start,
               diag.h/main.c s_csvFile && wlOn); its absence in solo runs is normal (H4.4).
      net/wd/hang/rec  wireless round log, D1 watchdog, D2 hang dumps, D5 recordings.

  GUARD: every write goes through azctl.assert_sd_writable (H4.1) — only cias/control,
  3DGBA/ (fixtures) and, with the explicit flag, cias/netlogs are writable; dual-gba/
  (the user's real ROMs+saves) is untouchable. Writes into cias/control are
  tmp-then-rename in the same directory (atomic on APFS) so the app's 10-frame poll
  (control.h CTL_POLL_FRAMES) can never pick up a half-written script.

CLI (via the venv shim `tools/emutest/run sdmc ...`):
  sdmc paths                          resolved host paths + which exist
  sdmc arm-control                    mkdir the opt-in dir (BEFORE the session starts)
  sdmc drop KIND SEAT [BODY]          write <kind>_p<seat>.txt (BODY optional for markers)
  sdmc drop-abort SEAT                write the '!' abort file
  sdmc wait-consumed KIND SEAT [--timeout S] [--interval S]   pickup ACK (file gone)
  sdmc netlogs [--since EPOCH]        list netlog files (mtime-sorted)
  sdmc latest KIND                    newest 3DGBA_<kind>_* path (exit 1 if none)
  sdmc cat KIND|PATH [--tail N]       dump a netlog (KIND resolves via latest)
  sdmc control-status [--expect-pickup]   parse the newest control log
Exit codes (H2): 0 ok / condition met, 1 fail / not found / timeout. Never SKIPs.
"""

import argparse
import os
import re
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import azctl  # paths + the sd_guard (single write gate, H4.1)

CONTROL_KINDS = ("move", "go", "record", "replay", "replay_go")
NETLOG_KINDS = ("control", "gs", "touch", "csv", "net", "wd", "hang", "rec")


def control_dir():
    return os.path.join(azctl.az_sd(), "cias", "control")


def netlogs_dir():
    return os.path.join(azctl.az_sd(), "cias", "netlogs")


def control_path(kind, seat):
    if kind not in CONTROL_KINDS:
        raise SystemExit("sdmc: unknown control kind '{}' (valid: {})".format(
            kind, " ".join(CONTROL_KINDS)))
    if seat not in (1, 2):
        raise SystemExit("sdmc: seat must be 1 (game A) or 2 (game B) — main.c:245-246")
    return os.path.join(control_dir(), "{}_p{}.txt".format(kind, seat))


# ---- move-body validation (string-level ONLY — the app's parser is the authority and is
# host-tested in test/host/test_control.c; this pre-flight just catches operator typos
# before they burn a 10-frame poll round-trip). Grammar per control.h CTOK_* + ctl_load.
_TOKEN_RE = re.compile(r"""^(
      [LRUDlrud][0-9]*      # walk L<n>/R<n>/U<n>/D<n> (lowercase sprint) or bare tap-turn
    | [abscxy]              # buttons: a=A b=B s=START c=SELECT x=L y=R
    | [Ww][0-9]+            # wait n emulated frames
    | G                     # wait-for-go (token 0 only; checked below)
)$""", re.VERBOSE)
CTL_TOK_MAX = 64  # control.h:62


def validate_move_body(body):
    """-> list of problems ([] = ok). Abort files ('!' first) are always valid."""
    stripped = body.lstrip(" \t\r\n")
    if stripped.startswith("!"):
        return []
    toks = stripped.split()
    problems = []
    if not toks:
        problems.append("empty script (no tokens)")
    if len(toks) > CTL_TOK_MAX:
        problems.append("{} tokens > CTL_TOK_MAX {}".format(len(toks), CTL_TOK_MAX))
    for i, t in enumerate(toks):
        if not _TOKEN_RE.match(t):
            problems.append("token {} '{}' not in the control.h grammar".format(i, t))
        elif t == "G" and i != 0:
            problems.append("'G' only allowed as token 0 (control.h CTL_WAIT_GO)")
    return problems


def _atomic_write(path, body):
    """tmp + rename in the SAME dir so the app's poll never sees a partial file."""
    azctl.assert_sd_writable(path)
    d = os.path.dirname(path)
    os.makedirs(d, exist_ok=True)
    fd, tmp = tempfile.mkstemp(prefix=".sdmc-", dir=d)
    try:
        with os.fdopen(fd, "w") as f:
            f.write(body)
        os.rename(tmp, path)
    except BaseException:
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise


# ---- commands -------------------------------------------------------------------------------
def cmd_paths(_args):
    rows = [("sd", azctl.az_sd()), ("control", control_dir()), ("netlogs", netlogs_dir())]
    for name, p in rows:
        print("{:<8} {}  ({})".format(name, p, "exists" if os.path.isdir(p) else "ABSENT"))
    return 0


def cmd_arm_control(_args):
    d = control_dir()
    azctl.assert_sd_writable(d)
    existed = os.path.isdir(d)
    os.makedirs(d, exist_ok=True)
    print("sdmc: {} {} — the app arms D4/D5 iff this exists at SESSION start "
          "(main.c:2521); arm before the session begins".format(
              "already had" if existed else "created", d))
    return 0


def cmd_drop(args):
    path = control_path(args.kind, args.seat)
    body = args.body if args.body is not None else ""
    if args.kind == "move":
        problems = validate_move_body(body)
        if problems and not args.force:
            for p in problems:
                print("sdmc: move body problem: {}".format(p), file=sys.stderr)
            print("sdmc: FAIL — not dropped (--force overrides; the app parser is the "
                  "authority)", file=sys.stderr)
            return 1
    if body and not body.endswith("\n"):
        body += "\n"
    _atomic_write(path, body)
    print("sdmc: wrote {} ({} bytes){}".format(
        path, os.path.getsize(path),
        " — marker" if not body.strip() else ""))
    return 0


def cmd_drop_abort(args):
    path = control_path("move", args.seat)
    _atomic_write(path, "!\n")   # first non-ws byte '!' = abort, honoured mid-script
    print("sdmc: wrote abort file {}".format(path))
    return 0


def cmd_wait_consumed(args):
    path = control_path(args.kind, args.seat)
    deadline = time.time() + args.timeout
    while True:
        if not os.path.exists(path):
            print("sdmc: PASS — {} consumed (pickup ACK, main.c:460)".format(
                os.path.basename(path)))
            return 0
        if time.time() >= deadline:
            print("sdmc: FAIL — {} still present after {}s (no pickup: dead seat core? "
                  "channel not armed at session start? app paused?)".format(
                      os.path.basename(path), args.timeout))
            return 1
        time.sleep(args.interval)


def _netlog_files(since=None):
    d = netlogs_dir()
    if not os.path.isdir(d):
        return []
    out = []
    for name in sorted(os.listdir(d)):
        p = os.path.join(d, name)
        if not os.path.isfile(p):
            continue
        m = os.path.getmtime(p)
        if since is not None and m < since:
            continue
        out.append((m, p))
    out.sort()
    return out


def cmd_netlogs(args):
    files = _netlog_files(args.since)
    if not files:
        print("sdmc: no netlog files{} in {}".format(
            " newer than --since" if args.since else "", netlogs_dir()))
        return 0
    for m, p in files:
        print("{}  {:>9}  {}".format(
            time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(m)),
            os.path.getsize(p), os.path.basename(p)))
    return 0


def _latest(kind, since=None):
    """Newest netlog of `kind`. `since` (an epoch, e.g. a boot's spawn_ts) restricts the
    search to files this run could have written — REVIEW FIX 2026-08-09: without it,
    `control-status --expect-pickup` happily asserted against a PREVIOUS run's control
    log when the current session wrote none, so the row (and its quoted header) could
    pass on stale evidence. The gs-log check in smoke.sh already did the mtime filter;
    this makes the control channel match."""
    if kind not in NETLOG_KINDS:
        raise SystemExit("sdmc: unknown netlog kind '{}' (valid: {})".format(
            kind, " ".join(NETLOG_KINDS)))
    pat = re.compile(r"^3DGBA_{}(_p\d|_HOST|_JOIN)?_\d".format(re.escape(kind)))
    # -1 s of slack: the same tolerance azctl.harvest() uses for filesystem mtime
    # granularity vs the recorded spawn timestamp.
    cands = [(m, p) for m, p in _netlog_files(None if since is None else since - 1)
             if pat.match(os.path.basename(p))]
    return cands[-1][1] if cands else None


def cmd_latest(args):
    p = _latest(args.kind, getattr(args, "since", None))
    if not p:
        extra = (" (D3 CSV is WIRELESS-SESSIONS-ONLY — absence in a solo run is normal, "
                 "H4.4)" if args.kind == "csv" else "")
        print("sdmc: no 3DGBA_{}_* in {}{}".format(args.kind, netlogs_dir(), extra))
        return 1
    print(p)
    return 0


def cmd_cat(args):
    p = args.what
    if p in NETLOG_KINDS:
        p = _latest(p)
        if not p:
            print("sdmc: no 3DGBA_{}_* netlog yet".format(args.what), file=sys.stderr)
            return 1
    if not os.path.isfile(p):
        print("sdmc: no such file {}".format(p), file=sys.stderr)
        return 1
    with open(p, "r", errors="replace") as f:
        lines = f.readlines()
    if args.tail:
        lines = lines[-args.tail:]
    sys.stdout.write("".join(lines))
    return 0


_CTL_HDR_RE = re.compile(r"^# control p1=(\S+) p2=(\S+) dir=(\S+)")


def cmd_control_status(args):
    p = _latest("control", args.since)
    if not p:
        print("sdmc: no{} control log yet — it is created lazily at the FIRST status line "
              "(main.c:276-285): channel unarmed, or armed with nothing to say".format(
                  " NEW (mtime >= --since)" if args.since else ""))
        return 1
    with open(p, "r", errors="replace") as f:
        lines = f.read().splitlines()
    print("log: {}".format(p))
    ok_hdr = bool(lines) and _CTL_HDR_RE.match(lines[0])
    if ok_hdr:
        m = _CTL_HDR_RE.match(lines[0])
        print("header: p1={} p2={} dir={}".format(m.group(1), m.group(2), m.group(3)))
    else:
        print("header: MALFORMED first line: {!r}".format(lines[0] if lines else ""))
    pickups = [l for l in lines if "picked up" in l]
    for l in lines[1:]:
        print("  " + l)
    if args.expect_pickup:
        if pickups:
            print("sdmc: PASS — pickup line present: {}".format(pickups[-1].strip()))
            return 0
        print("sdmc: FAIL — no 'picked up' line in {}".format(os.path.basename(p)))
        return 1
    return 0 if ok_hdr else 1


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="virtual-SD bridge: D4 control files in, netlogs/CSV out "
                    "(SPEC-harness H4; app facts cited in the module doc)")
    sub = ap.add_subparsers(dest="cmd", required=True)

    sub.add_parser("paths", help="resolved host paths").set_defaults(fn=cmd_paths)
    sub.add_parser("arm-control", help="create sdmc:/cias/control (the app's D4 opt-in)"
                   ).set_defaults(fn=cmd_arm_control)

    s = sub.add_parser("drop", help="write a control file (tmp+rename, guarded)")
    s.add_argument("kind", choices=CONTROL_KINDS)
    s.add_argument("seat", type=int, choices=[1, 2], help="1 = game A, 2 = game B (fixed)")
    s.add_argument("body", nargs="?", default=None,
                   help="script text (markers like go/record need none)")
    s.add_argument("--force", action="store_true",
                   help="drop even if the move-grammar pre-flight complains")
    s.set_defaults(fn=cmd_drop)

    s = sub.add_parser("drop-abort", help="write the '!' abort move-file")
    s.add_argument("seat", type=int, choices=[1, 2])
    s.set_defaults(fn=cmd_drop_abort)

    s = sub.add_parser("wait-consumed", help="pass when the control file disappears")
    s.add_argument("kind", choices=CONTROL_KINDS)
    s.add_argument("seat", type=int, choices=[1, 2])
    s.add_argument("--timeout", type=float, default=15.0)
    s.add_argument("--interval", type=float, default=0.5)
    s.set_defaults(fn=cmd_wait_consumed)

    s = sub.add_parser("netlogs", help="list sdmc:/cias/netlogs files")
    s.add_argument("--since", type=float, metavar="EPOCH",
                   help="only files with mtime >= EPOCH (e.g. a boot spawn_ts)")
    s.set_defaults(fn=cmd_netlogs)

    s = sub.add_parser("latest", help="print newest 3DGBA_<kind>_* netlog path")
    s.add_argument("kind", choices=NETLOG_KINDS)
    s.add_argument("--since", type=float, metavar="EPOCH",
                   help="only consider files with mtime >= EPOCH (a boot spawn_ts) — "
                        "otherwise a previous run's log can answer for this one")
    s.set_defaults(fn=cmd_latest)

    s = sub.add_parser("cat", help="dump a netlog file (kind name resolves to newest)")
    s.add_argument("what", metavar="KIND|PATH")
    s.add_argument("--tail", type=int, metavar="N")
    s.set_defaults(fn=cmd_cat)

    s = sub.add_parser("control-status", help="parse the newest control log")
    s.add_argument("--expect-pickup", action="store_true",
                   help="exit 0 only if a 'picked up' status line exists")
    s.add_argument("--since", type=float, metavar="EPOCH",
                   help="require the log to be NEW (mtime >= EPOCH, e.g. the boot's "
                        "spawn_ts) — without it a stale log from an earlier run can "
                        "satisfy --expect-pickup")
    s.set_defaults(fn=cmd_control_status)

    args = ap.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
