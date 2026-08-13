#!/usr/bin/env python3
"""azctl.py — Azahar launch/stop/status + managed config profile (phase-16 slice E1).

Design sources (house rule: every protocol fact cites where it came from):
  - SPEC-harness.md  H1.5 (profile+instance state machine), H1.6/S3.3 (profile keys),
    H2.1 (CLI), H2.8 (per-run dirs), H3.2 (readiness), H4 (sdmc bridge rules).
  - SPEC-protocols.md S2.1 (gdb stub enablement: INI route runs freely, CLI -g pauses the
    app at its first instruction — process.cpp:266-277), S3.1 (the `key\\default` write
    rule, config.cpp:1464-1472), S3.2 (Azahar saves the INI on boot AND exit,
    citra_qt.cpp:1543 — edit only while it is not running; restore only after exit),
    S3.4 (backup discipline), S3.5 (dialog inventory).
  - PokeDNA harness report: fresh boot per run; saves/ROMs copied, never edited in place.

LAUNCH RULE (probed on this machine 2026-08-08, non-negotiable): launch the .app BUNDLE via
`open -a <bundle> --args -w <rom>` — running Contents/MacOS/azahar directly pops a modal
"being run directly rather than via the Azahar.app bundle" dialog that blocks the instance
and once swallowed SIGTERM. `open` returns immediately, so the emulator is tracked by
`pgrep -x azahar` + the rotating log, never by the child pid of `open`.

Probed environment facts baked in below (all verified on THIS machine 2026-08-08):
  - Azahar 2125.1.2 bundle at ~/Applications/Azahar.app, data at
    ~/Library/Application Support/Azahar/.
  - The log rotates to `log/azahar_log.old.txt` per launch (NOT `.txt.old` — probed; the
    fresh `azahar_log.txt` can legitimately be 0 bytes for a while).
  - qt-config.ini is ASCII, LF line endings, `key=value` immediately followed by
    `key\\default=<bool>` (probed line pairs, e.g. 306/307 check_for_update_on_start).
  - `aspect_ratio` (SPEC-protocols S3.3) has NO key in 2125.1.2's file (probed grep) —
    absent key == built-in default AspectRatio::Default(0), which IS the pinned value, and
    inserting an unknown key would just be dropped on Azahar's own rewrite. Not pinned.

Timing constants measured live in slice E1 (BUILDLOG 2026-08-08) are marked [M].

Exit codes (SPEC-harness H2): 0 = ok, 1 = fail, 75 = skip (unused here; azctl never skips).
"""

import argparse
import contextlib
import difflib
import errno
import fcntl
import hashlib
import json
import os
import shutil
import signal
import subprocess
import sys
import time

# --------------------------------------------------------------------------- paths
# Defaults are the documented Azahar locations probed 2026-08-08 (SPEC-harness fixed-path
# table); every one is env-overridable (PHASE invariant 7 — no user paths beyond these).

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))


def az_bin():
    return os.environ.get(
        "EMUTEST_AZ_BIN",
        os.path.expanduser("~/Applications/Azahar.app/Contents/MacOS/azahar"))


def az_bundle():
    # open(1) wants the .app bundle; derive it from AZ_BIN (…/Azahar.app/Contents/MacOS/azahar).
    b = az_bin()
    marker = ".app/Contents/MacOS/"
    i = b.find(marker)
    if i >= 0:
        return b[: i + len(".app")]
    return b  # non-bundle override (tests) — used as-is


def az_data():
    return os.environ.get(
        "EMUTEST_AZ_DATA",
        os.path.expanduser("~/Library/Application Support/Azahar"))


def az_cfg():
    return os.path.join(az_data(), "config", "qt-config.ini")


def az_log():
    return os.path.join(az_data(), "log", "azahar_log.txt")


def az_log_old():
    # Probed 2026-08-08: the rotated name is azahar_log.old.txt (SPEC-harness H1.5 said
    # ".old" loosely; the real suffix goes before the extension).
    return os.path.join(az_data(), "log", "azahar_log.old.txt")


def az_sd():
    return os.path.join(az_data(), "sdmc")


def app_3dsx():
    return os.environ.get("EMUTEST_APP", os.path.join(REPO, "3DGBA.3dsx"))


def state_dir():
    return os.environ.get("EMUTEST_STATE_DIR", os.path.join(HERE, "state"))


def runs_dir():
    return os.environ.get("EMUTEST_RUNS_DIR", os.path.join(HERE, "runs"))


def backup_path():
    return os.path.join(state_dir(), "qt-config.ini.bak")


def pidfile_path():
    return os.path.join(state_dir(), "azahar.pid")


def lastrun_path():
    return os.path.join(state_dir(), "last_run")


def fixtures_manifest_path():
    return os.path.join(state_dir(), "fixtures.json")


def lock_path():
    return os.path.join(state_dir(), "azctl.lock")


def recent_backup_path():
    return os.path.join(state_dir(), "recent.bin.bak")


# --------------------------------------------------------------------------- state lock
# REVIEW FIX (2026-08-09): state/ is a SINGLE shared slot — one qt-config.ini.bak, one
# azahar.pid, one fixtures manifest — and nothing serialised access to it. Two concurrent
# azctl users could interleave backup_config() (user cfg -> .bak) with another process's
# restore_config() (.bak -> user cfg, then DELETE .bak), which permanently pins the
# harness profile in the user's qt-config.ini with no backup left to restore from
# (invariant 2 is a user-data invariant, so this is the blocker-grade half of the fix).
# An advisory flock around every mutating command makes those sections atomic; the
# `owner` stamp below makes the remaining cross-session case (one session stopping
# another's emulator — unavoidable, only one Azahar may run at a time) LOUD instead of
# silent. flock is advisory + released on process exit, so a crashed azctl never wedges
# the harness.
LOCK_WAIT_S = 60


@contextlib.contextmanager
def state_lock(what):
    os.makedirs(state_dir(), exist_ok=True)
    f = open(lock_path(), "a+")
    try:
        deadline = time.time() + LOCK_WAIT_S
        announced = False
        while True:
            try:
                fcntl.flock(f, fcntl.LOCK_EX | fcntl.LOCK_NB)
                break
            except OSError as e:
                if e.errno not in (errno.EAGAIN, errno.EACCES):
                    raise
                if not announced:
                    f.seek(0)
                    holder = f.read().strip() or "?"
                    print("azctl: waiting for the state lock — another azctl holds it "
                          "({}); up to {}s".format(holder, LOCK_WAIT_S))
                    announced = True
                if time.time() >= deadline:
                    raise RuntimeError(
                        "azctl: could not take the state lock ({}) within {}s — another "
                        "harness session is mid-command. Wait for it, or remove {} if you "
                        "are certain no azctl is running.".format(
                            lock_path(), LOCK_WAIT_S, lock_path()))
                time.sleep(0.25)
        f.seek(0)
        f.truncate()
        f.write("pid={} cmd={} at={}\n".format(
            os.getpid(), what, time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())))
        f.flush()
        yield
    finally:
        try:
            fcntl.flock(f, fcntl.LOCK_UN)
        finally:
            f.close()


def session_owner():
    """Who owns a booted instance. EMUTEST_SESSION lets a caller (smoke.sh, a workflow)
    name itself; otherwise the parent pid is a decent proxy. Recorded in the pidfile so a
    cross-session stop/restart is reported instead of looking like our own instance."""
    return os.environ.get("EMUTEST_SESSION") or "ppid:{}".format(os.getppid())


GDB_PORT = 24689  # Azahar default (probed qt-config.ini line 215; gdbstub.cpp:162)

# [M] Measured live 2026-08-08 (BUILDLOG E1): cold boot on this machine — process visible in
# ~1 s, log rotated in ~1 s, gdb stub listening (= core booted) in ~4-7 s. 30 s keeps a wide
# margin for cold caches (SPEC-harness H3.2 default until E4 refines).
BOOT_TIMEOUT_S = 30
TERM_GRACE_S = 5  # SIGTERM → SIGKILL escalation window (SPEC-harness H1.5)

# --------------------------------------------------------------------------- profile
# The deterministic/automation profile — SPEC-protocols S3.3, each key probed present in
# the live qt-config.ini 2026-08-08 (line numbers in that file). Values are strings exactly
# as Qt writes them. THE write rule (S3.1, config.cpp:1464-1472): a pinned value needs its
# `key\default=false` companion or the stored value is IGNORED — apply_profile_text() forces
# `\default=false` for every pin unconditionally (uniform, idempotent).
PROFILE_PINS = [
    # section, key, value                         # why / probed line
    ("Layout", "layout_option", "0"),             # Default top-over-bottom layout (l.270)
    ("Layout", "screen_gap", "0"),                # S4 crop math input (l.288)
    ("Layout", "swap_screen", "false"),           # S4 assumes unswapped (l.300)
    ("Layout", "upright_screen", "false"),        # S4 assumes landscape (l.302)
    ("Layout", "screen_top_stretch", "false"),    # stretch replaces S4 rects (l.292)
    ("Layout", "screen_bottom_stretch", "false"), # (l.284)
    ("Layout", "use_integer_scaling", "false"),   # pins MaxRectangle branch (l.356)
    # PHASE 18. NEAREST screen filter. Without this the window upscale is bilinear, so a
    # capture can never answer a question about individual framebuffer pixels — which is
    # exactly what three phase-18 slices needed and went without. With it, the 2.25x upscale
    # is a verbatim pixel copy and the 400x240 / 320x240 framebuffer can be recovered
    # BYTE-EXACTLY from an ordinary `see shot` (tools/emutest/native.py --verify proves it by
    # re-expanding and diffing). This is what makes text-sharpness measurable off-hardware.
    ("Layout", "filter_mode", "false"),           # NEAREST window filter
    ("Layout", "render_3d", "0"),                 # stereo off — no touch remap (l.278)
    ("Layout", "factor_3d", "0"),                 # 3D slider off (l.264)
    # aspect_ratio: deliberately NOT pinned — no such key in 2125.1.2's file (probed);
    # absent == built-in default 0 (the pinned value); see module docstring.
    ("System", "init_clock", "1"),                # InitClock::FixedTime — RTC pin (l.370)
    ("System", "init_time", "946684800"),         # 2000-01-01T00:00:00Z, matches the
                                                  # SPEC-protocols S1.11 golden epoch (l.376)
    ("System", "init_ticks_type", "1"),           # InitTicks::Fixed (l.374)
    ("System", "init_ticks_override", "1000"),    # fixed tick seed (l.372)
    # CORRECTED 2026-08-09 (BUILDLOG E2 fact 1, live-proven; S2.1 was researched from
    # master and is WRONG for the installed release 2125.1.2): use_gdbstub=true does NOT
    # mean "runs freely while a stub listens". System::Init calls GDBStub::DeferStart()
    # unconditionally (release core.cpp:574) and Init(port) sets halt_loop=true then
    # BLOCKS in accept() on the emu thread (gdbstub.cpp:1157/1203), so EVERY boot with
    # this pin parks pre-first-instruction until a client connects — and stays halted
    # until `c`. `tools/emutest/run gdbio resume` is therefore MANDATORY after each boot.
    ("Debugging", "use_gdbstub", "true"),         # (l.224)
    ("Debugging", "gdbstub_port", str(GDB_PORT)), # explicit, never rely on default (l.215)
    ("Miscellaneous", "check_for_update_on_start", "false"),  # the update dialog hijacked a
                                                  # run + swallowed SIGTERM (l.306)
    ("UI", "confirmClose", "false"),              # close-confirm QMessageBox = the
                                                  # SIGTERM-swallower class (S3.5; l.629)
    ("UI", "fullscreen", "false"),                # windowed, deterministic geometry (l.637)
    ("UI", "showStatusBar", "false"),             # cleaner S4 crops (l.653)
    # ("UI", "Paths\\screenshotPath", <run dir>/azshots) is appended per-boot (H1.6).
]


def apply_profile_text(text, pins):
    """Apply pins to Qt-INI text, preserving every untouched line byte-identically.

    pins: iterable of (section, key, value). For each pin the `key=` line is rewritten and
    the `key\\default=` line forced to false (S3.1); missing lines are inserted at the end
    of their section; missing sections are appended at EOF. Idempotent by construction.
    Qt-escaped section names like [Data%20Storage] pass through untouched (we only ever
    manage plain-ASCII sections). Host-tested by tests/test_profile.py (H6.2).
    """
    by_section = {}
    for sec, key, val in pins:
        by_section.setdefault(sec, {})[key] = val
    done_val = set()   # (sec, key) whose value line was rewritten
    done_def = set()   # (sec, key) whose \default line was rewritten

    lines = text.splitlines(keepends=True)
    out = []
    current = None

    def flush_section(sec):
        """Insert pins that had no existing line in section `sec` (before leaving it)."""
        if sec not in by_section:
            return
        for key, val in by_section[sec].items():
            if (sec, key) not in done_val:
                out.append("{}={}\n".format(key, val))
                done_val.add((sec, key))
            if (sec, key) not in done_def:
                out.append("{}\\default=false\n".format(key))
                done_def.add((sec, key))

    for line in lines:
        stripped = line.rstrip("\n")
        if stripped.startswith("[") and stripped.endswith("]"):
            flush_section(current)
            current = stripped[1:-1]
            out.append(line)
            continue
        replaced = False
        if current in by_section:
            for key, val in by_section[current].items():
                if stripped.startswith(key + "="):
                    out.append("{}={}\n".format(key, val))
                    done_val.add((current, key))
                    replaced = True
                    break
                if stripped.startswith(key + "\\default="):
                    out.append("{}\\default=false\n".format(key))
                    done_def.add((current, key))
                    replaced = True
                    break
        if not replaced:
            out.append(line)
    flush_section(current)

    # Entirely-missing sections go at EOF (none expected against the probed file).
    for sec in by_section:
        missing = [k for k in by_section[sec]
                   if (sec, k) not in done_val or (sec, k) not in done_def]
        if not missing:
            continue
        if out and not out[-1].endswith("\n"):
            out.append("\n")
        out.append("[{}]\n".format(sec))
        for key in missing:
            if (sec, key) not in done_val:
                out.append("{}={}\n".format(key, by_section[sec][key]))
                done_val.add((sec, key))
            if (sec, key) not in done_def:
                out.append("{}\\default=false\n".format(key))
                done_def.add((sec, key))
    return "".join(out)


# --------------------------------------------------------------------------- sd guard
def assert_sd_writable(path, allow_netlog_delete=False):
    """The single write gate for the virtual SD (SPEC-harness H4.1).

    Allowed: AZ_SD/cias/control/** (harness input channel), AZ_SD/3DGBA/** (fixture dir,
    rompicker.h:13), and AZ_SD/cias/netlogs/** ONLY when the caller holds the explicit
    --wipe-netlogs intent. Everything else on the SD — dual-gba/ (real ROMs + gameA.sav),
    Nintendo 3DS/, 3ds/ (dspfirm) — is user data: never written, renamed or deleted.
    """
    sd = os.path.realpath(az_sd())
    p = os.path.realpath(path)
    if not (p == sd or p.startswith(sd + os.sep)):
        raise RuntimeError("sd_guard: {} is not on the virtual SD {}".format(path, sd))
    rel = os.path.relpath(p, sd)
    allowed = [os.path.join("cias", "control"), "3DGBA"]
    if allow_netlog_delete:
        allowed.append(os.path.join("cias", "netlogs"))
    for a in allowed:
        if rel == a or rel.startswith(a + os.sep):
            return
    raise RuntimeError(
        "sd_guard: refusing to write {} — user data on the virtual SD "
        "(allowed: {})".format(rel, ", ".join(allowed)))


# --------------------------------------------------------------------------- process helpers
def all_azahar_pids():
    """pgrep -x azahar (the probed tracking method — `open` detaches, no child pid).

    REVIEW FIX (2026-08-09): scoped to OUR uid (`-U`). Another account's Azahar reads
    that account's own ~/Library/Application Support/Azahar, so it can neither be
    affected by our profile nor be killed by us — counting it only produced a boot we
    could not unblock and (under --force) an unkillable-pid spin in kill_pid()."""
    r = subprocess.run(["pgrep", "-x", "-U", str(os.getuid()), "azahar"],
                       capture_output=True, text=True)
    return [int(x) for x in r.stdout.split()] if r.returncode == 0 else []


def pid_alive(pid):
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        return True


def read_pidfile():
    try:
        with open(pidfile_path()) as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def tracked_live_pid():
    info = read_pidfile()
    if info and pid_alive(info.get("pid", -1)) and info["pid"] in all_azahar_pids():
        return info["pid"]
    return None


def kill_pid(pid, label, log=None):
    """SIGTERM → wait TERM_GRACE_S → SIGKILL (H1.5; a modal dialog swallowed SIGTERM once,
    hence the unconditional escalation). Returns True when the pid is gone.

    REVIEW FIX (2026-08-09): the post-SIGKILL wait is now BOUNDED. pid_alive() reports a
    PermissionError as alive (correct — the process exists), and both os.kill() calls
    swallow OSError, so an unsignalable pid used to spin here forever with no diagnostic.
    A pid that survives SIGKILL is reported and the caller decides."""
    if not pid_alive(pid):
        return True
    _event(log, "kill: SIGTERM {} pid {}".format(label, pid))
    try:
        os.kill(pid, signal.SIGTERM)
    except OSError as e:
        _event(log, "kill: SIGTERM {} pid {} failed: {}".format(label, pid, e))
    deadline = time.time() + TERM_GRACE_S
    while time.time() < deadline:
        if not pid_alive(pid):
            return True
        time.sleep(0.2)
    _event(log, "kill: SIGKILL {} pid {} (survived {}s grace)".format(
        label, pid, TERM_GRACE_S))
    try:
        os.kill(pid, signal.SIGKILL)
    except OSError as e:
        _event(log, "kill: SIGKILL {} pid {} failed: {}".format(label, pid, e))
    deadline = time.time() + TERM_GRACE_S
    while time.time() < deadline:
        if not pid_alive(pid):
            return True
        time.sleep(0.1)
    _event(log, "kill: FAILED — pid {} ({}) still alive after SIGKILL + {}s (not ours to "
                "signal? another user, or a stuck kernel wait)".format(
                    pid, label, TERM_GRACE_S))
    return False


def gdb_port_listening(port=GDB_PORT):
    """Zero-interference stub probe: check for a LISTEN socket via lsof, do NOT connect —
    the stub is single-client (gdbstub.cpp listen(_,1)) and E2 owns the actual dialect."""
    r = subprocess.run(
        ["lsof", "-nP", "-iTCP:{}".format(port), "-sTCP:LISTEN"],
        capture_output=True, text=True)
    return r.returncode == 0 and "LISTEN" in r.stdout


# --------------------------------------------------------------------------- run dirs / events
def _utcnow():
    return time.strftime("%Y%m%d-%H%M%S", time.gmtime())


def new_run_dir():
    d = os.path.join(runs_dir(), _utcnow())
    n = 1
    base = d
    while os.path.exists(d):  # two boots in the same second
        d = "{}-{}".format(base, n)
        n += 1
    os.makedirs(d)
    with open(lastrun_path(), "w") as f:
        f.write(d + "\n")
    return d


def last_run_dir():
    try:
        with open(lastrun_path()) as f:
            return f.read().strip() or None
    except OSError:
        return None


def _event(run_dir, msg):
    """One line per harness action into <run>/events.log (H2.8: append-only evidence)."""
    line = "{} {}".format(time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), msg)
    print("azctl: " + msg)
    if run_dir:
        try:
            with open(os.path.join(run_dir, "events.log"), "a") as f:
                f.write(line + "\n")
        except OSError:
            pass


# --------------------------------------------------------------------------- config backup/restore
def backup_config(log=None):
    """AZ_CFG → state/qt-config.ini.bak. Create-once per APPLIED period (S3.4): caller
    guarantees CLEAN state first — an existing backup is never overwritten."""
    if os.path.exists(backup_path()):
        raise RuntimeError("backup already exists (state APPLIED?) — refusing to overwrite")
    os.makedirs(state_dir(), exist_ok=True)
    shutil.copy2(az_cfg(), backup_path())
    _event(log, "profile: backed up qt-config.ini ({} bytes)".format(
        os.path.getsize(backup_path())))


def restore_config(log=None):
    """Restore the user's qt-config.ini byte-identically and delete the backup (→ CLEAN).
    Idempotent: no backup = already CLEAN = success. Only call with Azahar stopped —
    it saves the INI on exit (S3.2, citra_qt.cpp:1543) and would overwrite the restore."""
    if not os.path.exists(backup_path()):
        return False
    shutil.copy2(backup_path(), az_cfg())
    same = _sha256(backup_path()) == _sha256(az_cfg())
    if not same:  # cannot happen short of disk error, but the invariant is load-bearing
        raise RuntimeError("restore verification failed: qt-config.ini != backup")
    os.remove(backup_path())
    _event(log, "profile: restored qt-config.ini byte-identically, backup deleted (CLEAN)")
    return True


def restore_config_guarded(log=None):
    """restore_config(), but ONLY when no Azahar of ours is alive.

    REVIEW FIX (2026-08-09) — the hole this closes: restore_config() copies the backup
    back and DELETES it. Azahar saves the INI on exit (S3.2, citra_qt.cpp:1543), so
    restoring while ANY instance still lives means that instance rewrites the harness
    profile into the user's qt-config.ini on quit — permanently, because the backup is
    already gone and the next restore silently returns False. Two reachable triggers were
    proven in review: (a) cmd_boot's stale-backup recovery ran BEFORE the foreign-instance
    check, so a user-launched Azahar was live during the restore; (b) `azctl stop` with no
    pidfile (azctl killed mid-boot) restored while our own orphan was still running.
    Refusing and KEEPING the backup is always recoverable (`azctl restore` after the user
    closes Azahar); restoring into a live instance is not."""
    if not os.path.exists(backup_path()):
        return True                               # already CLEAN
    alive = all_azahar_pids()
    if alive:
        _event(log, "profile: NOT restoring — azahar still running (pid {}). The backup is "
                    "KEPT at {}; close Azahar then run: tools/emutest/run azctl restore"
                    .format(",".join(map(str, alive)), backup_path()))
        return False
    restore_config(log)
    return True


def _sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def apply_profile(run_dir, extra_pins=None):
    """Backup → rewrite AZ_CFG with the pins (+ per-run screenshotPath + any per-boot
    extras) → drop the diff into the run dir (H2.8 'applied profile diff')."""
    backup_config(run_dir)
    with open(az_cfg(), "r", encoding="utf-8") as f:
        before = f.read()
    shots = os.path.join(run_dir, "azshots")
    os.makedirs(shots, exist_ok=True)
    pins = list(PROFILE_PINS) + [("UI", "Paths\\screenshotPath", shots)] + list(extra_pins or [])
    after = apply_profile_text(before, pins)
    with open(az_cfg(), "w", encoding="utf-8") as f:
        f.write(after)
    diff = "".join(difflib.unified_diff(
        before.splitlines(keepends=True), after.splitlines(keepends=True),
        fromfile="qt-config.ini.user", tofile="qt-config.ini.harness"))
    with open(os.path.join(run_dir, "profile.diff"), "w") as f:
        f.write(diff)
    _event(run_dir, "profile: applied {} pins (+screenshotPath), diff -> profile.diff".format(
        len(pins)))


# --------------------------------------------------------------------------- netlogs / fixtures
def wipe_netlogs(run_dir):
    """Empty AZ_SD/cias/netlogs/ — ONLY under the explicit --wipe-netlogs flag (H4.3;
    default is no wipe: stale logs are the user's data)."""
    d = os.path.join(az_sd(), "cias", "netlogs")
    os.makedirs(d, exist_ok=True)
    n = 0
    for name in os.listdir(d):
        p = os.path.join(d, name)
        assert_sd_writable(p, allow_netlog_delete=True)
        if os.path.isdir(p):
            shutil.rmtree(p)
        else:
            os.remove(p)
        n += 1
    _event(run_dir, "netlogs: wiped {} entries from {}".format(n, d))


FIXTURE_NAMES = ["gameA.gba", "gameA.sav", "gameB.gba", "gameB.sav"]


def recent_path():
    # rompicker.c:104 RECENT_PATH = "sdmc:/3DGBA/recent.bin" — the app's recent-ROM list,
    # inside the same dir we stage fixtures into.
    return os.path.join(az_sd(), "3DGBA", "recent.bin")


def stage_fixtures(run_dir):
    """--fresh-sd-fixtures (H4.2): copy the user's dual-gba ROMs+saves into the app's
    ROM_DIR sdmc:/3DGBA (rompicker.h:13) as harness-created fixtures; manifest with
    SHA-256 so stop() can prove the originals were never touched. Copies only — the
    originals in dual-gba/ are READ-ONLY user data (H4.1).

    REVIEW FIX (2026-08-09): a PRE-EXISTING recent.bin is now backed up here and restored
    by clean-fixtures. Before, clean-fixtures deleted recent.bin on existence alone even
    though stage_fixtures never created it — a user who had ever picked ROMs from
    sdmc:/3DGBA lost their recent list to any `smoke.sh --rom` (invariant 2: the harness
    never deletes anything it did not create). The app rewrites the file during a fixture
    session either way, so a copy is the only honest way to give it back."""
    src_dir = os.path.join(az_sd(), "dual-gba")
    dst_dir = os.path.join(az_sd(), "3DGBA")
    os.makedirs(dst_dir, exist_ok=True)
    old = load_fixture_manifest()
    old_dsts = {e["dst"] for e in old}
    # recent.bin: snapshot the user's copy if it exists and we have not already snapshotted
    # it for this staging period (a re-stage without a clean must not clobber the backup).
    recent_pre = os.path.exists(recent_path())
    if recent_pre and not os.path.exists(recent_backup_path()):
        os.makedirs(state_dir(), exist_ok=True)
        shutil.copy2(recent_path(), recent_backup_path())
        _event(run_dir, "fixtures: backed up the pre-existing recent.bin ({} bytes) -> {}"
               .format(os.path.getsize(recent_backup_path()), recent_backup_path()))
    manifest = []
    for name in FIXTURE_NAMES:
        src = os.path.join(src_dir, name)
        if not os.path.exists(src):
            _event(run_dir, "fixtures: {} absent in dual-gba/, skipped".format(name))
            continue
        dst = os.path.join(dst_dir, name)
        assert_sd_writable(dst)
        if os.path.exists(dst) and dst not in old_dsts:
            raise RuntimeError(
                "fixtures: {} exists and is not in the fixtures manifest — refusing to "
                "overwrite a file the harness did not create (H4.2)".format(dst))
        shutil.copy2(src, dst)
        manifest.append({"src": src, "dst": dst, "sha256": _sha256(src),
                         "src_mtime": os.path.getmtime(src)})
        _event(run_dir, "fixtures: {} -> {} ({} bytes)".format(
            name, dst, os.path.getsize(dst)))
    if not any(e["dst"].endswith("gameA.gba") for e in manifest):
        raise RuntimeError("fixtures: dual-gba/gameA.gba not found — nothing to stage")
    with open(fixtures_manifest_path(), "w") as f:
        json.dump({"files": manifest, "recent_pre_existing": recent_pre}, f, indent=1)


def _fixture_state():
    """-> {"files": [...], "recent_pre_existing": bool}. Tolerates the legacy bare-list
    manifest written before the 2026-08-09 review fix."""
    try:
        with open(fixtures_manifest_path()) as f:
            data = json.load(f)
    except (OSError, ValueError):
        return {"files": [], "recent_pre_existing": False}
    if isinstance(data, list):                    # legacy shape
        return {"files": data, "recent_pre_existing": False}
    return {"files": data.get("files", []),
            "recent_pre_existing": bool(data.get("recent_pre_existing"))}


def load_fixture_manifest():
    return _fixture_state()["files"]


def verify_fixture_originals(run_dir):
    """Post-run: re-hash the dual-gba originals against the manifest (H4.2 — a mismatch is
    a harness bug and fails the run loudly). Returns ok?"""
    ok = True
    for e in load_fixture_manifest():
        if not os.path.exists(e["src"]):
            _event(run_dir, "FIXTURE ORIGINAL MISSING: {}".format(e["src"]))
            ok = False
            continue
        if _sha256(e["src"]) != e["sha256"]:
            _event(run_dir, "FIXTURE ORIGINAL CHANGED: {} (sha mismatch) — HARNESS BUG, "
                            "user data may be affected".format(e["src"]))
            ok = False
    if ok and load_fixture_manifest():
        _event(run_dir, "fixtures: originals re-hashed, all untouched")
    return ok


# --------------------------------------------------------------------------- harvest
def harvest(run_dir, spawn_ts):
    """Copy evidence into the run dir (H2.8/H4.3): azahar_log.txt + the rotated
    azahar_log.old.txt (rotation happens at OUR launch, so the .old file's mtime is always
    pre-spawn — it is the pre-run log, harvested as-is for context, no mtime filter) and
    every netlogs file newer than the boot. Copy, never move (S3.4).

    REVIEW FIX (2026-08-09) — THE BLOCKER: spawn_ts used to default to 0 when the pidfile
    was missing, and the netlog filter `mtime >= spawn_ts - 1` is then true for EVERY file
    in the user's netlogs dir. A `azctl stop` after the app self-quit therefore copied
    foreign logs (from earlier sessions, other runs, real hardware pulls) into the run dir
    and wrote them into events.log as this run's evidence — a ROM-less boot "producing" a
    4.3 KB gs log with real rows. spawn_ts is now recovered from the run dir's boot.json,
    and when it is genuinely unknown the netlog harvest is SKIPPED loudly rather than
    fabricating provenance."""
    if not run_dir:
        return
    for src, name in [(az_log(), "azahar_log.txt"), (az_log_old(), "azahar_log.old.txt")]:
        try:
            if os.path.exists(src):
                shutil.copy2(src, os.path.join(run_dir, name))
                _event(run_dir, "harvest: {} ({} bytes)".format(name, os.path.getsize(src)))
        except OSError as e:
            _event(run_dir, "harvest: {} failed: {}".format(name, e))
    nl = os.path.join(az_sd(), "cias", "netlogs")
    if not spawn_ts:
        _event(run_dir, "harvest: netlogs SKIPPED — this run's spawn_ts is unknown (no "
                        "pidfile and no boot.json), so newer-than-boot cannot be decided "
                        "and copying everything would plant foreign logs as evidence")
    elif os.path.isdir(nl):
        out = os.path.join(run_dir, "netlogs")
        for root, _dirs, files in os.walk(nl):
            for fn in files:
                p = os.path.join(root, fn)
                try:
                    if os.path.getmtime(p) >= spawn_ts - 1:
                        rel = os.path.relpath(p, nl)
                        dst = os.path.join(out, rel)
                        os.makedirs(os.path.dirname(dst), exist_ok=True)
                        shutil.copy2(p, dst)
                        _event(run_dir, "harvest: netlogs/{}".format(rel))
                except OSError:
                    pass


# --------------------------------------------------------------------------- commands
def cmd_boot(args):
    os.makedirs(state_dir(), exist_ok=True)
    os.makedirs(runs_dir(), exist_ok=True)

    # H1.5 step 1+2, reordered kill-before-restore (deviation recorded in BUILDLOG E1):
    # a still-live tracked instance saves the INI on its SIGTERM exit (S3.2), which would
    # overwrite a just-restored user config — so any tracked instance dies FIRST.
    tracked = tracked_live_pid()
    if tracked is not None:
        prev = (read_pidfile() or {}).get("owner")
        if prev and prev != session_owner():
            # REVIEW FIX (2026-08-09): state/ is one shared slot. A second harness session
            # restarting "the" instance is legal (only one Azahar may run) but must never
            # look like our own instance — say whose it was.
            print("azctl: NOTE — the tracked instance was booted by a DIFFERENT harness "
                  "session (owner {}; we are {}). Restarting it.".format(
                      prev, session_owner()))
        print("azctl: already booted (ours) — restarting (H2.1: boot while booted = restart)")
        _do_stop(harvest_evidence=True)

    # H1.5 step 2 — foreign instances: never silently kill an azahar the harness did not
    # start (the user may be running it). --force extends the kill.
    # REVIEW FIX (2026-08-09): this check now runs BEFORE the stale-backup recovery below.
    # It used to run after, so a stale backup + a user-launched Azahar meant we restored
    # the user's qt-config.ini and deleted the only backup while that Azahar was live —
    # and it then wrote the harness profile back on exit, permanently (see
    # restore_config_guarded).
    foreign = all_azahar_pids()
    if foreign:
        if not args.force:
            print("azctl: FAIL — azahar already running (pid {}) and not started by the "
                  "harness. Close Azahar or re-run with --force.".format(foreign))
            if os.path.exists(backup_path()):
                print("azctl: NOTE — a profile backup is still present ({}); it is being "
                      "KEPT, not restored, because that live instance would overwrite the "
                      "restore on exit. Close Azahar, then: azctl restore (or boot again)."
                      .format(backup_path()))
            return 1
        for pid in foreign:
            kill_pid(pid, "foreign azahar (--force)")

    if os.path.exists(backup_path()):
        print("azctl: recovered stale profile backup (previous run died before restore)")
        if not restore_config_guarded(None):
            return 1                      # a live azahar remains — refuse rather than lose it
        try:
            os.remove(pidfile_path())
        except OSError:
            pass

    run_dir = new_run_dir()
    _event(run_dir, "boot: run dir {}".format(run_dir))

    if not os.path.exists(app_3dsx()):
        _event(run_dir, "FAIL: app missing: {} — build with: export DEVKITPRO=/opt/devkitpro "
               "DEVKITARM=$DEVKITPRO/devkitARM && make -j8".format(app_3dsx()))
        return 1
    if not os.path.exists(az_cfg()):
        _event(run_dir, "FAIL: qt-config.ini missing at {} (run Azahar once "
               "manually to create it)".format(az_cfg()))
        return 1

    # THE MOVIE-MODE MODEL PIN (E3 live finding, log evidence in BUILDLOG E3): with the
    # emulated model = New 3DS, libctru's hidInit calls irrstInit (weak hidShouldUseIrrst =
    # APT_CheckNew3DS, disassembled from 3DGBA.elf @0x213618) -> the ir:rst service starts a
    # SECOND movie-consuming callback (release ir_rst.cpp:70-144, game-chosen update_period)
    # whose interleave with the 234 Hz pad stream depends on app-boot phase — unsynthesizable,
    # and it desynced a pure [Pad][Touch] movie ("Expected to read type 4"). Azahar's
    # APT::CheckNew3DS -> PTM::CheckNew3DS -> Settings::values.is_new_3ds (release
    # ptm.cpp:121-128), so pinning is_new_3ds=false makes the app skip irrstInit and keeps
    # the stream pure. Cost: movie runs emulate an Old 3DS (no ZL/ZR/c-stick — which CTM pad
    # records cannot press anyway; N3DS perf/core claims are hardware territory, invariant 5).
    extra_pins = []
    if (args.movie or args.record) and not args.keep_n3ds:
        extra_pins.append(("System", "is_new_3ds", "false"))
        _event(run_dir, "profile: movie mode — pinning is_new_3ds=false (ir:rst desync "
                        "guard; --keep-n3ds overrides)")
    apply_profile(run_dir, extra_pins)

    if args.wipe_netlogs:
        wipe_netlogs(run_dir)
    if args.fresh_sd_fixtures:
        stage_fixtures(run_dir)

    # Build the launch command. LAUNCH RULE (module docstring): bundle via open(1).
    extra = []
    if args.gdb is not None:
        # CORRECTED 2026-08-09 (BUILDLOG E2 fact 2): on release 2125.1.2 `-g PORT` adds
        # NOTHING over the INI route — it only sets use_gdbstub+gdbstub_port
        # (citra_qt.cpp:295-300). Master's pause-at-start flag (process.cpp:266-277) does
        # not exist here, and the profile already parks the boot. Kept as an explicit
        # port override / self-documentation for movie runs.
        port = args.gdb if args.gdb > 0 else GDB_PORT
        extra += ["-g", str(port)]
    if args.movie:
        extra += ["-p", os.path.abspath(args.movie)]
    if args.record:
        extra += ["-r", os.path.abspath(args.record), "-a", "emutest"]
    cmd = ["open", "-a", az_bundle(), "--args", "-w"] + extra + [app_3dsx()]
    with open(os.path.join(run_dir, "cmdline.txt"), "w") as f:
        f.write(" ".join(cmd) + "\n")

    spawn_ts = time.time()
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        _event(run_dir, "FAIL: open(1) rc={} stderr={}".format(r.returncode, r.stderr.strip()))
        restore_config_guarded(run_dir)
        return 1
    _event(run_dir, "boot: launched via {}".format(" ".join(cmd)))

    # Track by pgrep (open returns immediately — probed launch rule).
    pid = None
    t_pid = t_log = t_port = None
    deadline = spawn_ts + BOOT_TIMEOUT_S
    while time.time() < deadline:
        if pid is None:
            pids = all_azahar_pids()
            if pids:
                pid = pids[0]
                t_pid = time.time() - spawn_ts
                with open(pidfile_path(), "w") as f:
                    json.dump({"pid": pid, "spawn_ts": spawn_ts, "run_dir": run_dir,
                               "owner": session_owner()}, f)
                _event(run_dir, "boot: pid {} after {:.1f}s".format(pid, t_pid))
        if t_log is None:
            try:
                if os.path.getmtime(az_log()) >= spawn_ts:
                    t_log = time.time() - spawn_ts
                    _event(run_dir, "boot: log rotated after {:.1f}s".format(t_log))
            except OSError:
                pass
        if t_port is None and gdb_port_listening():
            t_port = time.time() - spawn_ts
            _event(run_dir, "boot: gdb stub listening on {} after {:.1f}s "
                            "(core booted)".format(GDB_PORT, t_port))
        if pid is not None and t_log is not None and t_port is not None:
            break
        if pid is not None and not pid_alive(pid):
            _event(run_dir, "FAIL: azahar pid {} exited during boot".format(pid))
            harvest(run_dir, spawn_ts)
            restore_config_guarded(run_dir)
            try:
                os.remove(pidfile_path())
            except OSError:
                pass
            return 1
        time.sleep(0.5)

    # Readiness verdict (H3.2): process + rotated log + (unless a movie-record run ever
    # disables it) the gdb stub listening. Any miss inside BOOT_TIMEOUT_S = loud fail +
    # full teardown, never a half-booted limbo.
    if pid is None or t_log is None or t_port is None:
        _event(run_dir, "FAIL: boot readiness timeout after {}s (pid={} log={} port={})".format(
            BOOT_TIMEOUT_S, pid, t_log, t_port))
        if pid is not None:
            kill_pid(pid, "half-booted azahar", run_dir)
        harvest(run_dir, spawn_ts)
        restore_config_guarded(run_dir)
        try:
            os.remove(pidfile_path())
        except OSError:
            pass
        return 1

    with open(os.path.join(run_dir, "boot.json"), "w") as f:
        json.dump({"pid": pid, "spawn_ts": spawn_ts, "t_pid_s": round(t_pid, 2),
                   "t_log_s": round(t_log, 2), "t_gdb_port_s": round(t_port, 2),
                   "azahar_version": azahar_version()}, f, indent=1)
    _event(run_dir, "boot: READY pid={} t_pid={:.1f}s t_log={:.1f}s t_gdb={:.1f}s".format(
        pid, t_pid, t_log, t_port))
    print("run_dir={}".format(run_dir))
    return 0


def _boot_json_spawn_ts(run_dir):
    """spawn_ts recovered from the run dir's own boot.json — the pidfile is not the only
    record of when this run started, and it is the one that goes missing (azctl killed
    mid-run, a concurrent session's stop, a manual rm)."""
    if not run_dir:
        return None
    try:
        with open(os.path.join(run_dir, "boot.json")) as f:
            ts = json.load(f).get("spawn_ts")
        return float(ts) if ts else None
    except (OSError, ValueError, TypeError):
        return None


def _do_stop(harvest_evidence=True):
    """Shared stop path: kill tracked → harvest → verify fixtures → restore config.
    Every step tolerant of already-done (H1.5 'idempotent')."""
    info = read_pidfile()
    run_dir = (info or {}).get("run_dir") or last_run_dir()
    # REVIEW FIX (2026-08-09, the blocker): spawn_ts=0 made harvest() copy EVERY netlog in
    # the user's dir into this run dir as if it were this run's evidence. Recover it from
    # boot.json; harvest() itself now skips netlogs when it is still unknown.
    spawn_ts = (info or {}).get("spawn_ts") or _boot_json_spawn_ts(run_dir)
    rc = 0
    if info and pid_alive(info["pid"]):
        owner = info.get("owner")
        if owner and owner != session_owner():
            _event(run_dir, "stop: NOTE — pid {} was booted by a different harness session "
                            "(owner {}; we are {})".format(info["pid"], owner,
                                                           session_owner()))
        kill_pid(info["pid"], "tracked azahar", run_dir)
        _event(run_dir, "stop: pid {} terminated".format(info["pid"]))
    if harvest_evidence and run_dir:
        harvest(run_dir, spawn_ts)
    if not verify_fixture_originals(run_dir):
        rc = 1  # loud: user data hash mismatch is a harness bug (H4.2)
    # Guarded: never restore into a live instance (it would rewrite the profile on exit
    # and the backup would already be deleted) — e.g. `azctl stop` with no pidfile while
    # our own orphan is still running.
    if not restore_config_guarded(run_dir):
        rc = 1
    try:
        os.remove(pidfile_path())
    except OSError:
        pass
    return rc


def cmd_stop(_args):
    return _do_stop()


def cmd_clean_fixtures(_args):
    """E4: remove the staged ROM fixtures (everything in state/fixtures.json) plus the
    app-written sdmc:/3DGBA/recent.bin, and drop the manifest. Needed because Tier-A
    movies REQUIRE the ROM-less state — staged fixtures flip the app's boot flow into the
    ROM picker and a from-boot movie's taps land on the wrong screen (E3 follow-up 2;
    rompicker.c: scan_roms()>0). Originals are re-hashed FIRST (H4.2); only files the
    manifest lists — i.e. files the harness itself created — are ever deleted (H4.1).

    REVIEW FIX (2026-08-09): recent.bin is no longer deleted on existence alone. If the
    user already had sdmc:/3DGBA/recent.bin, stage_fixtures backed it up and we RESTORE
    it here; only a recent.bin that did not exist before staging is removed (that one the
    app wrote during our session, so it is ours to clean)."""
    if tracked_live_pid() is not None:
        print("azctl: FAIL — tracked azahar still running; `azctl stop` first "
              "(the app may hold the files open / rewrite recent.bin on exit)")
        return 1
    st = _fixture_state()
    manifest = st["files"]
    if not manifest:
        print("azctl: no fixtures manifest — nothing staged (already clean)")
        return 0
    if not verify_fixture_originals(None):
        print("azctl: FAIL — original hash mismatch (see above); NOT cleaning so the "
              "evidence stays on disk")
        return 1
    n = 0
    for e in manifest:
        dst = e["dst"]
        if os.path.exists(dst):
            assert_sd_writable(dst)
            os.remove(dst)
            n += 1
    # recent.bin (rompicker.c:104 RECENT_PATH) — user data if it pre-dated the staging.
    recent = recent_path()
    note = ""
    if st["recent_pre_existing"] and os.path.exists(recent_backup_path()):
        assert_sd_writable(recent)
        shutil.copy2(recent_backup_path(), recent)
        os.remove(recent_backup_path())
        note = "; restored the user's pre-existing recent.bin"
    elif st["recent_pre_existing"]:
        note = "; NOTE: recent.bin pre-existed but its backup is missing — left as-is"
    elif os.path.exists(recent):
        assert_sd_writable(recent)
        os.remove(recent)
        n += 1
        note = "; removed the recent.bin the app created during the fixture session"
    os.remove(fixtures_manifest_path())
    print("azctl: cleaned {} fixture files (originals verified untouched); "
          "sdmc:/3DGBA is ROM-less again{}".format(n, note))
    return 0


def cmd_restore(_args):
    # Manual escape hatch (H1.5): restore + delete backup only; no-op in CLEAN.
    # Refuse while a tracked instance lives — its exit would overwrite the restore (S3.2).
    if tracked_live_pid() is not None:
        print("azctl: FAIL — tracked azahar still running; use `azctl stop`")
        return 1
    had_backup = os.path.exists(backup_path())
    # REVIEW FIX (2026-08-09): guarded — a FOREIGN (untracked) instance is just as fatal to
    # a restore as a tracked one, and the old code only checked the tracked pid.
    if not restore_config_guarded(None):
        print("azctl: FAIL — azahar is running; the backup is kept. Close it and retry.")
        return 1
    print("azctl: restored (CLEAN)" if had_backup else "azctl: already CLEAN (no backup)")
    return 0


def azahar_version():
    plist = os.path.join(az_bundle(), "Contents", "Info.plist")
    r = subprocess.run(["plutil", "-extract", "CFBundleShortVersionString", "raw", plist],
                      capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else "unknown"


def cmd_status(_args):
    """Greppable status report (smoke parses the key=value lines)."""
    ours = tracked_live_pid()
    pids = all_azahar_pids()
    if ours is not None:
        info = read_pidfile()
        print("process=ours pid={} uptime={:.0f}s owner={}".format(
            ours, time.time() - info["spawn_ts"], info.get("owner", "?")))
    elif pids:
        print("process=foreign pids={}".format(",".join(map(str, pids))))
    else:
        print("process=none")
    print("profile={}".format("APPLIED" if os.path.exists(backup_path()) else "CLEAN"))
    print("gdb_port={}".format("open" if gdb_port_listening() else "closed"))
    print("last_run={}".format(last_run_dir() or "-"))
    print("azahar_version={}".format(azahar_version()))
    return 0


# --------------------------------------------------------------------------- main
def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Azahar launch/stop/status with the managed emutest profile "
                    "(SPEC-harness H1.5/H2.1)")
    sub = ap.add_subparsers(dest="cmd", required=True)

    b = sub.add_parser("boot", help="apply profile + launch the .3dsx windowed")
    b.add_argument("--gdb", nargs="?", const=GDB_PORT, type=int, default=None,
                   metavar="PORT",
                   help="also pass -g PORT on the CLI. NOTE: on release 2125.1.2 this is "
                        "redundant (it only sets use_gdbstub+port, citra_qt.cpp:295-300). "
                        "EVERY boot is parked pre-first-instruction by the profile's "
                        "use_gdbstub pin and needs `gdbio resume` (BUILDLOG E2 fact 1/2)")
    b.add_argument("--movie", metavar="F.ctm", help="play a CTM movie (-p); implies the "
                   "is_new_3ds=false pin (ir:rst desync guard — see cmd_boot comment)")
    b.add_argument("--record", metavar="F.ctm", help="record a CTM movie (-r); same pin")
    b.add_argument("--keep-n3ds", action="store_true",
                   help="movie/record run WITHOUT the is_new_3ds=false pin (the ir:rst "
                        "stream WILL desync a synthesized movie — experiments only)")
    b.add_argument("--fresh-sd-fixtures", action="store_true",
                   help="copy dual-gba ROMs+saves into sdmc:/3DGBA as fixtures (H4.2)")
    b.add_argument("--wipe-netlogs", action="store_true",
                   help="empty sdmc:/cias/netlogs before boot (H4.3)")
    b.add_argument("--force", action="store_true",
                   help="also kill azahar instances the harness did not start")
    b.set_defaults(fn=cmd_boot)

    sub.add_parser("stop", help="kill tracked instance, harvest evidence, restore config"
                   ).set_defaults(fn=cmd_stop)
    sub.add_parser("status", help="process / profile / gdb-port / last-run report"
                   ).set_defaults(fn=cmd_status)
    sub.add_parser("restore", help="restore the user's qt-config.ini (escape hatch)"
                   ).set_defaults(fn=cmd_restore)
    sub.add_parser("clean-fixtures", help="remove staged ROM fixtures + recent.bin "
                   "(back to the ROM-less state Tier-A movies need)"
                   ).set_defaults(fn=cmd_clean_fixtures)

    args = ap.parse_args(argv)
    # Every command that MUTATES shared state (the config backup, the pidfile, the fixture
    # manifest) runs under the advisory state lock; `status` is read-only and stays lock-
    # free so it can always report, even while a boot is in flight.
    if args.fn is cmd_status:
        return args.fn(args)
    try:
        with state_lock(args.cmd):
            return args.fn(args)
    except RuntimeError as e:
        print(str(e))
        return 1


if __name__ == "__main__":
    sys.exit(main())
