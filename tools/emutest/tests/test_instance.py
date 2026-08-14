# test_instance.py — host-pure tests for the PHASE 21 S3 dual-instance upgrade:
# instance.py derivation, azctl's recent.bin builder + --stage-roms staging, and
# see.py's pid-scoped window selection seam.
# Run: tools/emutest/.venv/bin/python -m unittest discover -s tools/emutest/tests -v

import json
import os
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
sys.path.insert(0, TOOLS)
import azctl     # noqa: E402
import instance  # noqa: E402
import see       # noqa: E402


class _Env(unittest.TestCase):
    """Save/restore the EMUTEST_* env around each test."""

    KEYS = ["EMUTEST_INSTANCE", "EMUTEST_STATE_DIR", "EMUTEST_RUNS_DIR",
            "EMUTEST_AZ_DATA", "EMUTEST_AZ_BIN", "EMUTEST_GDB_PORT",
            "EMUTEST_ROMS_DIR", "EMUTEST_AZ_TEMPLATE_CFG"]

    def setUp(self):
        self._env = {k: os.environ.get(k) for k in self.KEYS}
        for k in self.KEYS:
            os.environ.pop(k, None)

    def tearDown(self):
        for k, v in self._env.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v


class TestInstanceDerivation(_Env):
    def test_default_instance_matches_pre_s3_values(self):
        # Instance "a" (implicit) must derive the EXACT legacy values, or every old
        # caller silently moves to a fresh state dir / port.
        self.assertEqual(instance.instance_id(), "a")
        self.assertTrue(instance.is_default())
        self.assertEqual(instance.gdb_port(), 24689)
        self.assertEqual(instance.state_dir(), os.path.join(TOOLS, "state"))
        self.assertEqual(instance.runs_dir(), os.path.join(TOOLS, "runs"))
        self.assertEqual(instance.az_data(),
                         os.path.expanduser("~/Library/Application Support/Azahar"))
        self.assertEqual(
            instance.az_bin(),
            os.path.expanduser("~/Applications/Azahar.app/Contents/MacOS/azahar"))

    def test_instance_b_derives_everything(self):
        os.environ["EMUTEST_INSTANCE"] = "b"
        self.assertFalse(instance.is_default())
        self.assertEqual(instance.gdb_port(), 24690)
        self.assertEqual(instance.state_dir(), os.path.join(TOOLS, "state-b"))
        self.assertEqual(instance.runs_dir(), os.path.join(TOOLS, "runs-b"))
        self.assertEqual(instance.az_home(), os.path.join(TOOLS, "az-b"))
        self.assertEqual(instance.az_data(), os.path.join(TOOLS, "az-b", "user"))
        self.assertEqual(instance.az_bin(),
                         os.path.join(TOOLS, "az-b", "Azahar.app", "Contents",
                                      "MacOS", "azahar"))

    def test_env_overrides_beat_instance_derivation(self):
        os.environ["EMUTEST_INSTANCE"] = "b"
        os.environ["EMUTEST_STATE_DIR"] = "/x/state"
        os.environ["EMUTEST_RUNS_DIR"] = "/x/runs"
        os.environ["EMUTEST_AZ_DATA"] = "/x/az"
        os.environ["EMUTEST_AZ_BIN"] = "/x/bin/azahar"
        os.environ["EMUTEST_GDB_PORT"] = "31337"
        self.assertEqual(instance.state_dir(), "/x/state")
        self.assertEqual(instance.runs_dir(), "/x/runs")
        self.assertEqual(instance.az_data(), "/x/az")
        self.assertEqual(instance.az_bin(), "/x/bin/azahar")
        self.assertEqual(instance.gdb_port(), 31337)

    def test_bad_instance_id_is_refused(self):
        for bad in ["ab", "1", "", "B B"]:
            os.environ["EMUTEST_INSTANCE"] = bad
            with self.assertRaises(SystemExit):
                instance.instance_id()

    def test_azctl_profile_pins_the_instance_port(self):
        # The INI pin and gdbio's dial must both follow the instance (import-time value
        # for THIS process = default instance in the test env).
        self.assertIn(("Debugging", "gdbstub_port", str(azctl.GDB_PORT)),
                      azctl.PROFILE_PINS)
        import gdbio
        self.assertEqual(gdbio.GDB_PORT, azctl.GDB_PORT)


class TestRecentBin(_Env):
    def test_pair_layout(self):
        # rompicker.c RecentPair: char a[256] + char b[256], NUL-terminated, 512 B.
        rb = azctl.build_recent_bin("sdmc:/3DGBA/gameA.gba", "sdmc:/3DGBA/gameB.gba")
        self.assertEqual(len(rb), 512)
        self.assertEqual(rb[:21], b"sdmc:/3DGBA/gameA.gba")
        self.assertEqual(rb[21], 0)
        self.assertEqual(rb[256:277], b"sdmc:/3DGBA/gameB.gba")
        self.assertEqual(rb[277], 0)

    def test_single_mode(self):
        rb = azctl.build_recent_bin("sdmc:/3DGBA/gameA.gba")
        self.assertEqual(len(rb), 512)
        self.assertEqual(rb[256], 0)          # b[0]=='\0' => single mode

    def test_overlong_path_refused(self):
        with self.assertRaises(ValueError):
            azctl.build_recent_bin("x" * 256)


class TestStageRoms(_Env):
    def setUp(self):
        super().setUp()
        self.tmp = tempfile.mkdtemp(prefix="emutest-inst-")
        az = os.path.join(self.tmp, "az")
        os.makedirs(os.path.join(az, "sdmc"))
        roms = os.path.join(self.tmp, "roms")
        os.makedirs(roms)
        for n in ["emerald", "firered"]:
            with open(os.path.join(roms, n + ".gba"), "wb") as f:
                f.write(b"ROM-" + n.encode())
            with open(os.path.join(roms, n + ".sav"), "wb") as f:
                f.write(b"SAV-" + n.encode())
        os.environ["EMUTEST_AZ_DATA"] = az
        os.environ["EMUTEST_STATE_DIR"] = os.path.join(self.tmp, "state")
        os.environ["EMUTEST_RUNS_DIR"] = os.path.join(self.tmp, "runs")
        os.environ["EMUTEST_ROMS_DIR"] = roms
        os.makedirs(azctl.state_dir())

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)
        super().tearDown()

    def test_solo_stage_writes_fixtures_and_single_recent(self):
        azctl.stage_roms(None, "firered")
        dst = os.path.join(azctl.az_sd(), "3DGBA")
        self.assertEqual(open(os.path.join(dst, "gameA.gba"), "rb").read(),
                         b"ROM-firered")
        self.assertEqual(open(os.path.join(dst, "gameA.sav"), "rb").read(),
                         b"SAV-firered")
        rb = open(azctl.recent_path(), "rb").read()
        self.assertEqual(len(rb), 512)
        self.assertEqual(rb[256], 0)                       # single mode
        st = azctl._fixture_state()
        self.assertFalse(st["recent_pre_existing"])        # we created recent.bin
        self.assertEqual(len(st["files"]), 2)

    def test_pair_stage_and_clean_roundtrip(self):
        azctl.stage_roms(None, "emerald,firered")
        dst = os.path.join(azctl.az_sd(), "3DGBA")
        self.assertEqual(open(os.path.join(dst, "gameB.gba"), "rb").read(),
                         b"ROM-firered")
        rb = open(azctl.recent_path(), "rb").read()
        self.assertNotEqual(rb[256], 0)                    # pair mode
        old_pids = azctl.all_azahar_pids
        azctl.all_azahar_pids = lambda: []
        try:
            self.assertEqual(azctl.cmd_clean_fixtures(None), 0)
        finally:
            azctl.all_azahar_pids = old_pids
        self.assertEqual(sorted(os.listdir(dst)), [])      # incl. our recent.bin
        # originals untouched
        self.assertEqual(open(os.path.join(instance.roms_dir(),
                                           "firered.gba"), "rb").read(), b"ROM-firered")

    def test_restage_does_not_reclassify_our_recent_bin(self):
        # Second stage in the SAME period must not back up the recent.bin WE wrote.
        azctl.stage_roms(None, "firered")
        azctl.stage_roms(None, "emerald")
        st = azctl._fixture_state()
        self.assertFalse(st["recent_pre_existing"])
        self.assertFalse(os.path.exists(azctl.recent_backup_path()))

    def test_user_recent_bin_backed_up_and_flagged(self):
        os.makedirs(os.path.join(azctl.az_sd(), "3DGBA"), exist_ok=True)
        with open(azctl.recent_path(), "wb") as f:
            f.write(b"the user's pairing")
        azctl.stage_roms(None, "firered")
        st = azctl._fixture_state()
        self.assertTrue(st["recent_pre_existing"])
        self.assertEqual(open(azctl.recent_backup_path(), "rb").read(),
                         b"the user's pairing")

    def test_unknown_rom_name_fails_loudly(self):
        with self.assertRaises(RuntimeError):
            azctl.stage_roms(None, "ruby-nonexistent")
        with self.assertRaises(RuntimeError):
            azctl.stage_roms(None, "a,b,c")


class TestPickWindow(unittest.TestCase):
    """see.py's pure selection seam: pid filter first, then largest area."""

    CANDS = [
        {"id": 1, "pid": 100, "w": 1280.0, "h": 568.0, "title": "Azahar A"},
        {"id": 2, "pid": 200, "w": 1280.0, "h": 568.0, "title": "Azahar B"},
        {"id": 3, "pid": 200, "w": 10.0, "h": 10.0, "title": "helper"},
    ]

    def test_pid_filter_selects_the_right_instance(self):
        self.assertEqual(see.pick_window(self.CANDS, 100)["id"], 1)
        self.assertEqual(see.pick_window(self.CANDS, 200)["id"], 2)

    def test_no_pid_falls_back_to_largest(self):
        self.assertIn(see.pick_window(self.CANDS, None)["id"], (1, 2))

    def test_unknown_pid_returns_none(self):
        self.assertIsNone(see.pick_window(self.CANDS, 999))

    def test_empty(self):
        self.assertIsNone(see.pick_window([], None))


class TestInstanceDataTemplate(_Env):
    """ensure_instance_data builds the non-default tree once, from the template cfg."""

    def setUp(self):
        super().setUp()
        self.tmp = tempfile.mkdtemp(prefix="emutest-instb-")
        # Fake template bundle: <tmp>/tpl/Azahar.app/Contents/MacOS/azahar
        tpl = os.path.join(self.tmp, "tpl", "Azahar.app", "Contents", "MacOS")
        os.makedirs(tpl)
        with open(os.path.join(tpl, "azahar"), "wb") as f:
            f.write(b"#!/bin/sh\n")
        with open(os.path.join(self.tmp, "template.ini"), "w") as f:
            f.write("[UI]\nconfirmClose=true\n")
        os.environ["EMUTEST_INSTANCE"] = "b"
        os.environ["EMUTEST_AZ_DATA"] = os.path.join(self.tmp, "b-user")
        os.environ["EMUTEST_STATE_DIR"] = os.path.join(self.tmp, "state-b")
        os.environ["EMUTEST_RUNS_DIR"] = os.path.join(self.tmp, "runs-b")
        os.environ["EMUTEST_AZ_TEMPLATE_CFG"] = os.path.join(self.tmp, "template.ini")
        # az_bin is derived from az_home() (under TOOLS) — point the BUNDLE work at tmp
        # by overriding az_home via EMUTEST_AZ_BIN (skips the copy branch entirely):
        os.environ["EMUTEST_AZ_BIN"] = os.path.join(
            self.tmp, "tpl", "Azahar.app", "Contents", "MacOS", "azahar")

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)
        super().tearDown()

    def test_builds_config_and_sdmc_skeleton(self):
        azctl.ensure_instance_data(None)
        data = azctl.az_data()
        self.assertEqual(open(os.path.join(data, "config", "qt-config.ini")).read(),
                         "[UI]\nconfirmClose=true\n")
        sd = azctl.az_sd()
        for d in ["3DGBA", os.path.join("cias", "control"),
                  os.path.join("cias", "netlogs"), "3ds", "dual-gba"]:
            self.assertTrue(os.path.isdir(os.path.join(sd, d)), d)
        dsp = os.path.join(sd, "3ds", "dspfirm.cdc")
        self.assertTrue(os.path.exists(dsp))
        self.assertEqual(os.path.getsize(dsp), 0)
        self.assertTrue(os.path.isdir(os.path.join(data, "log")))

    def test_idempotent_and_keeps_existing_config(self):
        azctl.ensure_instance_data(None)
        cfg = os.path.join(azctl.az_data(), "config", "qt-config.ini")
        with open(cfg, "w") as f:
            f.write("EDITED\n")
        azctl.ensure_instance_data(None)          # second run must not clobber
        self.assertEqual(open(cfg).read(), "EDITED\n")

    def test_default_instance_is_a_noop(self):
        os.environ["EMUTEST_INSTANCE"] = "a"
        azctl.ensure_instance_data(None)          # must not raise, must create nothing
        self.assertFalse(os.path.exists(os.path.join(self.tmp, "b-user")))


if __name__ == "__main__":
    unittest.main()
