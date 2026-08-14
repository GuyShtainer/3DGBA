#!/usr/bin/env python3
"""instance.py — instance profiles for the dual-emulator harness (phase-21 S3 upgrade).

ONE id derives EVERYTHING, so two Azahar instances can run concurrently with zero shared
state. Instance "a" is the legacy default: every derived value equals what the harness
used before this module existed (state/, runs/, gdb 24689, the user's real Azahar bundle
and data dir), so old callers see no change. Any other id (b, c, …) gets:

  state dir   tools/emutest/state-<id>/      (lock, pidfile, backups — per-instance locks
  runs dir    tools/emutest/runs-<id>/        come free with the per-instance state dir)
  gdb port    24689 + (id - 'a')             (azctl pins it in the INI, gdbio dials it)
  az bundle   tools/emutest/az-<id>/Azahar.app   (private APFS-clone copy — see below)
  az data     tools/emutest/az-<id>/user/        (config/, sdmc/, log/ — full isolation)

WHY a private bundle copy (probed 2026-08-14 against the installed release 2125.1.2 +
its source at tag `2125.1.2`):
  - Azahar has NO user-dir CLI flag (the binary's full option help-string dump: only
    movie/dump-video/fullscreen/gdbport/install/help/movie-play/movie-record/version/
    windowed). `%CITRA_USER_DIR%` in `strings` output is a shortcut-template placeholder.
  - On macOS the frontend forces cwd = <dir containing Azahar.app> at startup
    (citra_qt.cpp:4394-4397, `SetCurrentDir(GetBundleDirectory() + "..")`), and
    common/file_util.cpp:964-969 then selects `<cwd>/user/` as a PORTABLE user dir when
    that directory exists (USERDATA_DIR "user", common_paths.h:21; GetCurrentDir returns
    a trailing '/', file_util.cpp:806-808). Otherwise the fixed macOS default
    ~/Library/Application Support/Azahar (EMU_APPLE_DATA_DIR, common_paths.h:36) is used.
  => a copy of the bundle at az-<id>/Azahar.app with a sibling az-<id>/user/ gives a
    fully isolated config+sdmc+log tree. A SYMLINKED bundle does NOT work:
    CFBundleCopyBundleURL resolves to the real location, cwd lands next to the user's
    real Azahar.app, and the instance silently reuses the user's data dir.

Env overrides always win (the test suite depends on this): EMUTEST_STATE_DIR,
EMUTEST_RUNS_DIR, EMUTEST_AZ_DATA, EMUTEST_AZ_BIN, EMUTEST_GDB_PORT.

Selection: EMUTEST_INSTANCE=<id> (a single letter, default "a"), or the venv shim's
`tools/emutest/run --instance <id> <tool> …` which exports it before exec.
"""

import os

HERE = os.path.dirname(os.path.abspath(__file__))

BASE_GDB_PORT = 24689   # Azahar's own default (qt-config.ini gdbstub_port; gdbstub.cpp:162)

# The default instance's fixed locations (probed 2026-08-08, azctl module doc).
DEFAULT_AZ_BIN = os.path.expanduser("~/Applications/Azahar.app/Contents/MacOS/azahar")
DEFAULT_AZ_DATA = os.path.expanduser("~/Library/Application Support/Azahar")


def instance_id():
    iid = os.environ.get("EMUTEST_INSTANCE", "a").strip().lower()
    if len(iid) != 1 or not ("a" <= iid <= "z"):
        raise SystemExit(
            "emutest: bad EMUTEST_INSTANCE {!r} — want a single letter a-z "
            "(a = the default instance)".format(iid))
    return iid


def is_default(iid=None):
    return (iid or instance_id()) == "a"


def _suffix(iid=None):
    iid = iid or instance_id()
    return "" if iid == "a" else "-" + iid


def state_dir():
    env = os.environ.get("EMUTEST_STATE_DIR")
    return env if env else os.path.join(HERE, "state" + _suffix())


def runs_dir():
    env = os.environ.get("EMUTEST_RUNS_DIR")
    return env if env else os.path.join(HERE, "runs" + _suffix())


def gdb_port():
    env = os.environ.get("EMUTEST_GDB_PORT")
    if env:
        return int(env)
    return BASE_GDB_PORT + (ord(instance_id()) - ord("a"))


def az_home(iid=None):
    """The private per-instance Azahar root (bundle copy + user dir). Non-default only."""
    iid = iid or instance_id()
    return os.path.join(HERE, "az-" + iid)


def az_bin():
    env = os.environ.get("EMUTEST_AZ_BIN")
    if env:
        return env
    if is_default():
        return DEFAULT_AZ_BIN
    return os.path.join(az_home(), "Azahar.app", "Contents", "MacOS", "azahar")


def az_data():
    env = os.environ.get("EMUTEST_AZ_DATA")
    if env:
        return env
    if is_default():
        return DEFAULT_AZ_DATA
    return os.path.join(az_home(), "user")


def template_az_bin():
    """The bundle the non-default instances are cloned FROM: the default instance's
    binary (which is itself EMUTEST_AZ_BIN-overridable for tests)."""
    return os.environ.get("EMUTEST_AZ_BIN", DEFAULT_AZ_BIN)


def roms_dir():
    """Source dir for `azctl boot --stage-roms` (the project's gitignored ROM library)."""
    repo = os.path.dirname(os.path.dirname(HERE))
    return os.environ.get("EMUTEST_ROMS_DIR", os.path.join(repo, "roms"))
