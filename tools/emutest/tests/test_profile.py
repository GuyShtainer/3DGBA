# test_profile.py — host-pure tests for azctl's qt-config editor + profile state machine
# (SPEC-harness H6.2 row test_profile). Run:
#   tools/emutest/.venv/bin/python -m unittest discover -s tools/emutest/tests -p 'test_*.py' -v
#
# Facts pinned here mirror the probed live qt-config.ini (2026-08-08): ASCII, LF endings,
# `key=value` line with an adjacent `key\default=<bool>` companion (S3.1: default=true means
# the stored value is IGNORED — config.cpp:1464-1472), Qt-escaped section names like
# [Data%20Storage].

import json
import os
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import azctl  # noqa: E402

# A miniature of the probed live file: managed keys, an unmanaged neighbour that shares a
# prefix with a managed key (init_time vs init_time_offset), and a %20 section.
FIXTURE = """[Data%20Storage]
use_custom_storage=true
use_custom_storage\\default=true

[Debugging]
gdbstub_port=24689
gdbstub_port\\default=true
use_gdbstub=false
use_gdbstub\\default=true

[Miscellaneous]
check_for_update_on_start=true
check_for_update_on_start\\default=true

[System]
init_clock=0
init_clock\\default=true
init_time=946681277
init_time\\default=true
init_time_offset=0
init_time_offset\\default=true

[UI]
Paths\\screenshotPath=
Paths\\screenshotPath\\default=true
confirmClose=true
confirmClose\\default=true
"""

PINS = [
    ("System", "init_clock", "1"),
    ("System", "init_time", "946684800"),
    ("Debugging", "use_gdbstub", "true"),
    ("Miscellaneous", "check_for_update_on_start", "false"),
    ("UI", "confirmClose", "false"),
    ("UI", "Paths\\screenshotPath", "/tmp/azshots"),
]


class TestProfileEditor(unittest.TestCase):
    def setUp(self):
        self.out = azctl.apply_profile_text(FIXTURE, PINS)

    def test_values_and_default_false(self):
        # S3.1: every pinned key gets value + \default=false, or the value is ignored.
        for want in ["init_clock=1\n", "init_clock\\default=false\n",
                     "init_time=946684800\n", "init_time\\default=false\n",
                     "use_gdbstub=true\n", "use_gdbstub\\default=false\n",
                     "check_for_update_on_start=false\n",
                     "check_for_update_on_start\\default=false\n",
                     "confirmClose=false\n", "confirmClose\\default=false\n",
                     "Paths\\screenshotPath=/tmp/azshots\n",
                     "Paths\\screenshotPath\\default=false\n"]:
            self.assertIn(want, self.out)

    def test_prefix_key_untouched(self):
        # init_time_offset must NOT be rewritten when pinning init_time (the '=' terminator
        # disambiguates the startswith match).
        self.assertIn("init_time_offset=0\n", self.out)
        self.assertIn("init_time_offset\\default=true\n", self.out)

    def test_untouched_lines_byte_identical(self):
        # Every line not belonging to a pinned key survives byte-identically, including the
        # %20 section header and its keys.
        managed = set()
        for _sec, key, _val in PINS:
            managed.add(key + "=")
            managed.add(key + "\\default=")
        def unmanaged(text):
            return [l for l in text.splitlines(keepends=True)
                    if not any(l.startswith(m) for m in managed)]
        self.assertEqual(unmanaged(FIXTURE), unmanaged(self.out))
        self.assertIn("[Data%20Storage]\n", self.out)
        self.assertIn("use_custom_storage=true\n", self.out)

    def test_idempotent(self):
        self.assertEqual(self.out, azctl.apply_profile_text(self.out, PINS))

    def test_missing_key_inserted_in_section(self):
        # A pin whose key is absent lands inside its existing section, not at EOF.
        out = azctl.apply_profile_text(FIXTURE, [("System", "init_ticks_type", "1")])
        sys_start = out.index("[System]")
        ui_start = out.index("[UI]")
        ins = out.index("init_ticks_type=1\n")
        self.assertTrue(sys_start < ins < ui_start)
        self.assertIn("init_ticks_type\\default=false\n", out)

    def test_missing_section_appended(self):
        out = azctl.apply_profile_text(FIXTURE, [("Layout", "layout_option", "0")])
        self.assertIn("[Layout]\n", out)
        self.assertLess(out.index("[UI]"), out.index("[Layout]"))
        self.assertIn("layout_option=0\n", out)


class TestStateMachine(unittest.TestCase):
    """backup/restore/crash-recovery on temp dirs via the EMUTEST_* env overrides
    (SPEC-harness: AZ paths env-overridable; state dir override is test-only)."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="emutest-profile-")
        self.az = os.path.join(self.tmp, "azdata")
        os.makedirs(os.path.join(self.az, "config"))
        os.makedirs(os.path.join(self.az, "sdmc"))
        self.state = os.path.join(self.tmp, "state")
        os.makedirs(self.state)
        with open(os.path.join(self.az, "config", "qt-config.ini"), "w") as f:
            f.write(FIXTURE)
        os.environ["EMUTEST_AZ_DATA"] = self.az
        os.environ["EMUTEST_STATE_DIR"] = self.state

    def tearDown(self):
        del os.environ["EMUTEST_AZ_DATA"]
        del os.environ["EMUTEST_STATE_DIR"]
        shutil.rmtree(self.tmp)

    def test_backup_restore_roundtrip(self):
        azctl.backup_config()
        self.assertTrue(os.path.exists(azctl.backup_path()))          # -> APPLIED
        with open(azctl.az_cfg(), "w") as f:                          # harness profile
            f.write("MUTATED\n")
        self.assertTrue(azctl.restore_config())                       # -> CLEAN
        with open(azctl.az_cfg()) as f:
            self.assertEqual(f.read(), FIXTURE)                       # byte-identical
        self.assertFalse(os.path.exists(azctl.backup_path()))
        self.assertFalse(azctl.restore_config())                      # idempotent no-op

    def test_backup_never_overwrites(self):
        # S3.4: create-once. A second backup while APPLIED would clobber the user copy
        # with the harness profile — must raise.
        azctl.backup_config()
        with open(azctl.az_cfg(), "w") as f:
            f.write("HARNESS PROFILE\n")
        with self.assertRaises(RuntimeError):
            azctl.backup_config()
        azctl.restore_config()

    def test_crash_recovery_shape(self):
        # A dead run leaves backup + stale pidfile; recovery = restore + pidfile removal
        # (H1.5 boot step 1 — exercised here as the same calls boot makes).
        azctl.backup_config()
        with open(azctl.az_cfg(), "w") as f:
            f.write("HARNESS PROFILE\n")
        with open(azctl.pidfile_path(), "w") as f:
            json.dump({"pid": 99999999, "spawn_ts": 0, "run_dir": self.tmp}, f)
        self.assertIsNone(azctl.tracked_live_pid())                   # pid long dead
        azctl.restore_config()
        os.remove(azctl.pidfile_path())
        with open(azctl.az_cfg()) as f:
            self.assertEqual(f.read(), FIXTURE)

    def test_sd_guard(self):
        sd = os.path.join(self.az, "sdmc")
        for ok in [os.path.join(sd, "cias", "control", "move_p1.txt"),
                   os.path.join(sd, "3DGBA", "gameA.gba")]:
            azctl.assert_sd_writable(ok)  # must not raise
        azctl.assert_sd_writable(os.path.join(sd, "cias", "netlogs", "x.txt"),
                                 allow_netlog_delete=True)
        for bad in [os.path.join(sd, "dual-gba", "gameA.sav"),      # real user save
                    os.path.join(sd, "3ds", "dspfirm.cdc"),
                    os.path.join(sd, "cias", "netlogs", "x.txt"),   # without the flag
                    os.path.join(self.tmp, "outside.txt")]:
            with self.assertRaises(RuntimeError):
                azctl.assert_sd_writable(bad)


if __name__ == "__main__":
    unittest.main()
