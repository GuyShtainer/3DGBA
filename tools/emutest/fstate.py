#!/usr/bin/env python3
"""fstate.py — read `g_fieldDbg` (and the tap geometry) as NAMED FIELDS over the emutest gdb
channel, and bank the answer.

WHY THIS FILE EXISTS. The phase-28 adversarial audit, finding O3:

    "'caught in two reads' is the one W1 claim with no artefact behind it. Every g_fieldDbg value
     in this lane exists only as hand-typed lines ... the reader itself (st28.py, taptile.py)
     lives only in a session scratchpad and is not in the repo ... Bank the raw capture next time;
     it is one `tee`."

So: the reader is here, in the tree, next to the other harness tools, and it PRINTS a machine-
readable line that can be `tee`d straight into evidence/. Two jobs, both of which the phase-28
session did by hand:

  fstate.py read [--fields a,b,c] [--json]     one snapshot of g_fieldDbg, named
  fstate.py watch --seconds N [--hz H]         repeated snapshots, one line per sample, with the
                                               emulated frame counter, so a state DELTA is a
                                               diff of two lines rather than a memory
  fstate.py tap X Y [--mode M]                 the bottom-screen touch coordinate that lands on
                                               GBA pixel (X,Y) under the live scale mode, with
                                               the app's OWN integer inverse asserted
  fstate.py tile DDX DDY [--mode M]            ...for a tile offset from the player (the walk
                                               gesture's own ddx/ddy), which is the form every
                                               live tap in this project is actually written in

THE LANDMINE THE TAP HALVES EXIST FOR (phase 28 §1.2). LANE-A's banked formula
`screen = (40 + (7+ddx)*16 + 8, 40 + (5+ddy)*16 + 8)` is the SCALE_1X case. The emutest
instances ship `scaleMode[1] = 1 = Aspect-fit`, where main.c's `touch_to_gba` maps
    f = min(320/240, 240/160) = 4/3,  ox = 0,  oy = (240 - 160*4/3)/2 = 40/3
    gba = ( (int)(px/f), (int)((py - oy)/f) )
i.e. **ddy -4 is screen y 45, not 64**. Computing the tap from the live scale mode is the fix,
and `--mode` defaults to reading `g_prefs`/settings rather than assuming — pass it explicitly
only when no emulator is up.

The field table is generated from source/touch.h's FieldDbg comment ladder, but it is written out
here so this tool needs no build: if a field is APPENDED to FieldDbg, add it at the end here too.
Offsets are checked against the struct size read back from the ELF when `nm` is available.
"""
import argparse
import json
import struct
import socket
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
RUN = HERE / "run"

# --- source/touch.h FieldDbg, in declaration order. All int32_t, so index*4 == byte offset. ---
FIELDS = [
    "planSeq", "px", "py", "mapGroup", "mapNum", "goalX", "goalY", "approachX", "approachY",
    "kind", "termDir", "pathLen", "pElev", "behaviour", "outcome", "warpGroup", "warpNum",
    "headRetarget", "routeEnd", "endSeq",
    "curMapGroup", "curMapNum", "curPx", "curPy", "curKeys", "curFrame", "walking",
    "progSeq", "progOutcome", "progMoves", "progInteracts", "progStep", "progHm", "progPhase",
    "progEnd", "progEndSeq", "progUsable", "progEdges", "progSurf", "progMapSeq",
    "progAKeys", "progAnswers", "progFacing",
    "ownStarts", "ownSelects", "runLeg", "runElig", "runFrames",
    "progRetarget", "progGoalX", "progGoalY",
    "progBeh", "progLatch", "progObjSlot", "progObjX", "progObjY",
    # --- phase 29 / lane F ---
    "planForced", "planForcedN", "progFaceEnter",
]

PHASE = {0: "WALK", 1: "FACE", 2: "A", 3: "DLG", 4: "YESNO", 5: "ANSWER", 6: "DONE"}
END = {0: "NONE", 1: "ARRIVED", 2: "HANDOFF", 3: "CANCEL", 4: "KEY", 5: "MAPCHANGE", 6: "CTX",
       7: "TIMEOUT", 8: "STALL", 9: "UNEXPECTED", 10: "ELIG", 11: "REPLAN"}
OUTCOME = {0: "TIER0", 1: "PLANNED", 2: "UNREACHABLE", 3: "NOEDGE", 4: "BADMAP", 5: "CAP",
           6: "NOEXC", 7: "NODIVE"}
HM = {0: "NONE", 1: "CUT", 2: "SURF", 3: "SMASH", 4: "FALLS", 5: "STRENGTH", 6: "DIVE"}


# --- the broker, talked to DIRECTLY --------------------------------------------------------
# gdbio's CLI wraps its broker with a fixed 12 s socket deadline (BROKER_TIMEOUT_S), which is
# fine when a halt->read->continue round trip costs milliseconds. MEASURED on this machine while
# 3DGBA is running two GBA cores plus the HD-2D compositor, a round trip can exceed BOTH that and
# the broker's own 5 s per-op RSP deadline — every read comes back
# `FAIL — timeout (5.0s) waiting for stub data`, which reads exactly like a dead stub and is not
# one. Two consequences, and they are the reason this tool speaks the broker's JSON-lines
# protocol itself instead of shelling out to `gdbio read`:
#   1. START THE BROKER YOURSELF, with a per-op deadline that fits the app:
#        EMUTEST_INSTANCE=b tools/emutest/.venv/bin/python tools/emutest/gdbio.py serve \
#            --port 24690 --timeout 45 &
#      …BEFORE the first `gdbio resume` (the stub accepts one client per boot, so a broker
#      restart costs a reboot — gdbio.py module doc fact 5).
#   2. use a matching CLI-side deadline, which is what --op-timeout below is.
def broker_path():
    from instance import state_dir
    return str(Path(state_dir()) / "gdbio.sock")


class Broker:
    def __init__(self, timeout):
        import socket
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.settimeout(timeout)
        self.s.connect(broker_path())
        self.f = self.s.makefile("rwb")

    def op(self, **req):
        self.f.write((json.dumps(req) + "\n").encode())
        self.f.flush()
        line = self.f.readline()
        if not line:
            raise RuntimeError("broker closed the connection mid-op")
        r = json.loads(line.decode())
        if not r.get("ok"):
            raise RuntimeError(r.get("err", "broker error"))
        return r


_SYMS = {}


def sym_addr(name):
    """nm on 3DGBA.elf — the same primary source gdbio uses (its S2.6 note: a .3dsx loads at
    its link-time layout, so an nm address IS a gdb address)."""
    if not _SYMS:
        elf = HERE.parent.parent / "3DGBA.elf"
        out = subprocess.run(["nm", str(elf)], capture_output=True, text=True).stdout
        for ln in out.splitlines():
            p = ln.split()
            if len(p) == 3:
                _SYMS[p[2]] = int(p[0], 16)
    if name not in _SYMS:
        raise RuntimeError("no symbol %s in 3DGBA.elf" % name)
    return _SYMS[name]


_BROKER = None


def read_block():
    """One gdb read of the whole struct -> dict. ONE halt, not 58."""
    global _BROKER
    n = len(FIELDS) * 4
    if _BROKER is None:
        _BROKER = Broker(ARGS.op_timeout)
    r = _BROKER.op(op="read", addr=sym_addr("g_fieldDbg"), len=n)
    raw = bytes.fromhex(r["hex"])[:n]
    if len(raw) < n:
        sys.stderr.write("fstate: short read (%d bytes, want %d)\n" % (len(raw), n))
        return None
    vals = struct.unpack("<%di" % len(FIELDS), raw)
    return dict(zip(FIELDS, vals))


def annotate(d):
    d = dict(d)
    d["progPhase_"] = PHASE.get(d.get("progPhase"), "?")
    d["progEnd_"] = END.get(d.get("progEnd"), "?")
    d["progOutcome_"] = OUTCOME.get(d.get("progOutcome"), "?")
    d["progHm_"] = HM.get(d.get("progHm"), "?")
    return d


def line(d, fields):
    return " ".join("%s=%s" % (f, d.get(f, "?")) for f in fields)


DEFAULT_LINE = ["curFrame", "curMapGroup", "curMapNum", "curPx", "curPy", "progSeq",
                "progOutcome_", "progStep", "progPhase_", "progHm_", "progSurf", "progFacing",
                "progFaceEnter", "progBeh", "progAKeys", "progAnswers", "progEnd_",
                "progRetarget", "progGoalX", "progGoalY", "progLatch", "progObjSlot",
                "progObjX", "progObjY", "planForced", "planForcedN", "pathLen", "outcome",
                "routeEnd", "walking"]


# --- the tap geometry, mirroring main.c touch_to_gba (SCALE_1X 0 / Aspect-fit 1 / Stretch 2) ---
GBA_W, GBA_H, SW, SH = 240, 160, 320.0, 240.0


def scale_of(mode):
    if mode == 0:
        return 1.0, 1.0
    if mode == 2:
        return SW / GBA_W, SH / GBA_H
    f = min(SW / GBA_W, SH / GBA_H)
    return f, f


def touch_to_gba(px, py, mode):
    scx, scy = scale_of(mode)
    ox, oy = (SW - GBA_W * scx) / 2.0, (SH - GBA_H * scy) / 2.0
    return int((px - ox) / scx), int((py - oy) / scy)


def gba_to_touch(gx, gy, mode):
    """The inverse, then ASSERTED through the app's own integer forward map — a tap that does not
    round-trip is a tap aimed at the wrong tile, which is how phase 28 lost a whole run."""
    scx, scy = scale_of(mode)
    ox, oy = (SW - GBA_W * scx) / 2.0, (SH - GBA_H * scy) / 2.0
    best = None
    for px in range(0, 320):
        for py in range(0, 240):
            if touch_to_gba(px, py, mode) == (gx, gy):
                # prefer the middle of the admissible box
                if best is None:
                    best = [px, px, py, py]
                else:
                    best[1] = max(best[1], px)
                    best[2] = min(best[2], py)
                    best[3] = max(best[3], py)
    if best is None:
        return None
    px = (best[0] + best[1]) // 2
    ys = [y for y in range(240) if touch_to_gba(px, y, mode) == (gx, gy)]
    py = (min(ys) + max(ys)) // 2
    assert touch_to_gba(px, py, mode) == (gx, gy)
    return px, py


def live_mode():
    """settings.bin scaleMode[1] — the BOTTOM screen's mode, which is the one touch uses."""
    from instance import paths  # emutest's own instance resolver
    p = Path(paths().sdmc) / "3DGBA" / "settings.bin"
    if not p.exists():
        return None
    d = p.read_bytes()
    if len(d) < 12:
        return None
    return struct.unpack("<i", d[8:12])[0]


def main():
    global ARGS
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--instance", default=None, help="emutest instance id (default: env)")
    ap.add_argument("--op-timeout", type=float, default=300.0,
                    help="CLI-side deadline for one broker op (see the module doc: a busy 3DGBA "
                         "can make a halt->read->continue round trip take tens of seconds)")
    sub = ap.add_subparsers(dest="cmd", required=True)

    r = sub.add_parser("read", help="one named snapshot of g_fieldDbg")
    r.add_argument("--fields", default=None, help="comma list (default: the route/program set)")
    r.add_argument("--json", action="store_true")

    w = sub.add_parser("watch", help="repeated snapshots, one line each")
    w.add_argument("--seconds", type=float, default=10.0)
    w.add_argument("--hz", type=float, default=2.0)
    w.add_argument("--fields", default=None)
    w.add_argument("--until", default=None, help="stop when FIELD=VALUE (e.g. progEnd=7)")

    t = sub.add_parser("tap", help="touch coordinate for a GBA pixel")
    t.add_argument("gx", type=int)
    t.add_argument("gy", type=int)
    t.add_argument("--mode", type=int, default=None)

    tl = sub.add_parser("tile", help="touch coordinate for a tile offset from the player")
    tl.add_argument("ddx", type=int)
    tl.add_argument("ddy", type=int)
    tl.add_argument("--mode", type=int, default=None)

    ARGS = ap.parse_args()

    if ARGS.cmd in ("tap", "tile"):
        mode = ARGS.mode if ARGS.mode is not None else live_mode()
        if mode is None:
            sys.stderr.write("fstate: no settings.bin — pass --mode\n")
            return 2
        if ARGS.cmd == "tile":
            # touch.c: ddx = s_downGx/16 - 7, ddy = s_downGy/16 - 5 -> the tile CENTRE is
            # gba = (8 + 16*(7+ddx), 8 + 16*(5+ddy)).
            gx, gy = 8 + 16 * (7 + ARGS.ddx), 8 + 16 * (5 + ARGS.ddy)
        else:
            gx, gy = ARGS.gx, ARGS.gy
        if not (0 <= gx < GBA_W and 0 <= gy < GBA_H):
            sys.stderr.write("fstate: gba (%d,%d) is off-frame\n" % (gx, gy))
            return 1
        got = gba_to_touch(gx, gy, mode)
        if not got:
            sys.stderr.write("fstate: no touch pixel maps to gba (%d,%d) at mode %d\n" % (gx, gy, mode))
            return 1
        px, py = got
        print("mode=%d gba=(%d,%d) touch=(%d,%d)  # round-trip verified: t %d %d" %
              (mode, gx, gy, px, py, px, py))
        return 0

    fields = ARGS.fields.split(",") if getattr(ARGS, "fields", None) else DEFAULT_LINE
    if ARGS.cmd == "read":
        d = read_block()
        if d is None:
            return 1
        d = annotate(d)
        if ARGS.json:
            print(json.dumps(d))
        else:
            print(line(d, fields))
        return 0

    # watch
    until = None
    if ARGS.until:
        k, _, v = ARGS.until.partition("=")
        until = (k, int(v))
    t0 = time.time()
    period = 1.0 / max(ARGS.hz, 0.01)
    while time.time() - t0 < ARGS.seconds:
        d = read_block()
        if d is None:
            return 1
        d = annotate(d)
        print("%7.2f %s" % (time.time() - t0, line(d, fields)), flush=True)
        if until and d.get(until[0]) == until[1]:
            print("# until %s=%d met" % until)
            return 0
        time.sleep(period)
    return 0


if __name__ == "__main__":
    sys.exit(main())
