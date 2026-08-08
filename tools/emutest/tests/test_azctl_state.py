# test_azctl_state.py — regression tests for the 2026-08-09 review-fix pass on azctl.py.
# Host-pure (SPEC-harness H6.1): everything runs against a throwaway EMUTEST_AZ_DATA /
# EMUTEST_STATE_DIR / EMUTEST_RUNS_DIR tree with all_azahar_pids() stubbed — no Azahar.
#
# Each class pins ONE defect found by the adversarial review, described by the behaviour it
# forbids, so a regression fails here instead of on the user's machine:
#   BLOCKER  harvest() with an unknown spawn_ts copied EVERY netlog in the user's dir into
#            the run dir as that run's evidence (`azctl stop` after the pidfile was gone).
#   MAJOR    restore_config() ran while an Azahar was still alive: the live instance
#            rewrites the INI on exit (S3.2) and the backup had already been deleted, so
#            the user's qt-config.ini stayed pinned to the harness profile forever.
#   MINOR    clean-fixtures deleted sdmc:/3DGBA/recent.bin on existence alone, though
#            stage_fixtures never created it (PHASE invariant 2).
#   MAJOR    nothing serialised the single shared state/ slot between concurrent sessions.
#
# Run: tools/emutest/.venv/bin/python -m unittest discover -s tools/emutest/tests -v

import json
import os
import shutil
import subprocess
import sys
import tempfile
import textwrap
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
sys.path.insert(0, TOOLS)
import azctl  # noqa: E402

USER_CFG = "[UI]\nconfirmClose=true\nconfirmClose\\default=true\n"
PINNED_CFG = "[UI]\nconfirmClose=false\nconfirmClose\\default=false\n"


class _Sandbox(unittest.TestCase):
    """Fresh throwaway Azahar data dir + harness state per test."""

    def setUp(self):
        self.root = tempfile.mkdtemp(prefix="emutest-azctl-")
        az = os.path.join(self.root, "az")
        os.makedirs(os.path.join(az, "config"))
        os.makedirs(os.path.join(az, "log"))
        self.netlogs = os.path.join(az, "sdmc", "cias", "netlogs")
        os.makedirs(self.netlogs)
        with open(os.path.join(az, "config", "qt-config.ini"), "w") as f:
            f.write(USER_CFG)
        self._env = {}
        for k, v in (("EMUTEST_AZ_DATA", az),
                     ("EMUTEST_STATE_DIR", os.path.join(self.root, "state")),
                     ("EMUTEST_RUNS_DIR", os.path.join(self.root, "runs"))):
            self._env[k] = os.environ.get(k)
            os.environ[k] = v
        os.makedirs(azctl.state_dir())
        os.makedirs(azctl.runs_dir())
        self._pids = azctl.all_azahar_pids
        azctl.all_azahar_pids = lambda: []          # "no emulator running" by default

    def tearDown(self):
        azctl.all_azahar_pids = self._pids
        for k, v in self._env.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v
        shutil.rmtree(self.root, ignore_errors=True)

    # helpers -------------------------------------------------------------------------
    def _run_dir(self, name, spawn_ts=None):
        d = os.path.join(azctl.runs_dir(), name)
        os.makedirs(d)
        if spawn_ts is not None:
            with open(os.path.join(d, "boot.json"), "w") as f:
                json.dump({"pid": 1, "spawn_ts": spawn_ts}, f)
        with open(azctl.lastrun_path(), "w") as f:
            f.write(d + "\n")
        return d

    def _netlog(self, name, mtime):
        p = os.path.join(self.netlogs, name)
        with open(p, "w") as f:
            f.write("# " + name + "\n")
        os.utime(p, (mtime, mtime))
        return p


class TestHarvestProvenance(_Sandbox):
    """THE BLOCKER: a harvest must never claim files it cannot date to this run."""

    def test_only_this_runs_netlogs_are_harvested(self):
        spawn = time.time()
        run = self._run_dir("R1", spawn_ts=spawn)          # pidfile absent on purpose
        self._netlog("3DGBA_gs_OLD.txt", spawn - 3600)     # yesterday's session
        self._netlog("3DGBA_gs_NEW.txt", spawn + 5)        # written during this run
        azctl._do_stop(harvest_evidence=True)
        got = sorted(os.listdir(os.path.join(run, "netlogs")))
        self.assertEqual(got, ["3DGBA_gs_NEW.txt"])

    def test_unknown_spawn_ts_skips_netlogs_entirely(self):
        run = self._run_dir("R2")                          # no pidfile AND no boot.json
        self._netlog("3DGBA_gs_FOREIGN.txt", time.time() - 900)
        azctl._do_stop(harvest_evidence=True)
        self.assertFalse(os.path.isdir(os.path.join(run, "netlogs")),
                         "netlogs harvested without any way to date them")

    def test_spawn_ts_is_recovered_from_boot_json(self):
        spawn = time.time() - 10
        run = self._run_dir("R3", spawn_ts=spawn)
        self.assertAlmostEqual(azctl._boot_json_spawn_ts(run), spawn, places=3)


class TestRestoreGuard(_Sandbox):
    """MAJOR: never restore (nor delete the backup) while an emulator can still rewrite
    the INI on exit."""

    def _apply(self):
        shutil.copy2(azctl.az_cfg(), azctl.backup_path())
        with open(azctl.az_cfg(), "w") as f:
            f.write(PINNED_CFG)

    def test_refuses_and_keeps_backup_while_azahar_lives(self):
        self._apply()
        azctl.all_azahar_pids = lambda: [4242]
        self.assertFalse(azctl.restore_config_guarded(None))
        self.assertTrue(os.path.exists(azctl.backup_path()), "backup was destroyed")
        self.assertEqual(open(azctl.az_cfg()).read(), PINNED_CFG)

    def test_restores_once_the_emulator_is_gone(self):
        self._apply()
        self.assertTrue(azctl.restore_config_guarded(None))
        self.assertEqual(open(azctl.az_cfg()).read(), USER_CFG)
        self.assertFalse(os.path.exists(azctl.backup_path()))

    def test_boot_checks_for_a_foreign_instance_before_touching_the_backup(self):
        self._apply()
        azctl.all_azahar_pids = lambda: [4242]

        class A:
            force = gdb = movie = record = None
            keep_n3ds = fresh_sd_fixtures = wipe_netlogs = False

        A.force = False
        self.assertEqual(azctl.cmd_boot(A()), 1)
        self.assertTrue(os.path.exists(azctl.backup_path()),
                        "cmd_boot restored+deleted the backup with an azahar still alive")

    def test_cmd_restore_refuses_for_a_foreign_instance_too(self):
        self._apply()
        azctl.all_azahar_pids = lambda: [4242]      # untracked: no pidfile written
        self.assertEqual(azctl.cmd_restore(None), 1)
        self.assertTrue(os.path.exists(azctl.backup_path()))


class TestFixtureRecentBin(_Sandbox):
    """MINOR (user data): recent.bin is only ours to delete when we caused it to exist."""

    def _stage_sources(self):
        sd = azctl.az_sd()
        os.makedirs(os.path.join(sd, "dual-gba"), exist_ok=True)
        os.makedirs(os.path.join(sd, "3DGBA"), exist_ok=True)
        for n in azctl.FIXTURE_NAMES:
            with open(os.path.join(sd, "dual-gba", n), "wb") as f:
                f.write(b"content-of-" + n.encode())

    def test_pre_existing_recent_bin_is_restored_not_deleted(self):
        self._stage_sources()
        with open(azctl.recent_path(), "wb") as f:
            f.write(b"the user's recent-ROM list")
        azctl.stage_fixtures(None)
        with open(azctl.recent_path(), "wb") as f:      # the app rewrites it in-session
            f.write(b"rewritten during the harness session")
        azctl.cmd_clean_fixtures(None)
        self.assertTrue(os.path.exists(azctl.recent_path()))
        self.assertEqual(open(azctl.recent_path(), "rb").read(),
                         b"the user's recent-ROM list")

    def test_session_created_recent_bin_is_removed(self):
        self._stage_sources()
        azctl.stage_fixtures(None)
        with open(azctl.recent_path(), "wb") as f:
            f.write(b"created by the app during our session")
        azctl.cmd_clean_fixtures(None)
        self.assertFalse(os.path.exists(azctl.recent_path()))

    def test_fixtures_are_removed_and_originals_untouched(self):
        self._stage_sources()
        azctl.stage_fixtures(None)
        azctl.cmd_clean_fixtures(None)
        left = sorted(os.listdir(os.path.join(azctl.az_sd(), "3DGBA")))
        self.assertEqual(left, [])
        for n in azctl.FIXTURE_NAMES:
            src = os.path.join(azctl.az_sd(), "dual-gba", n)
            self.assertEqual(open(src, "rb").read(), b"content-of-" + n.encode())

    def test_legacy_list_manifest_still_loads(self):
        # Manifests written before the fix were a bare list; _fixture_state must cope.
        with open(azctl.fixtures_manifest_path(), "w") as f:
            json.dump([{"src": "/a", "dst": "/b", "sha256": "x"}], f)
        st = azctl._fixture_state()
        self.assertEqual(len(st["files"]), 1)
        self.assertFalse(st["recent_pre_existing"])


class TestStateLock(_Sandbox):
    """MAJOR: the single shared state/ slot needs an exclusive holder."""

    def test_second_holder_is_excluded_then_admitted(self):
        holder = subprocess.Popen(
            [sys.executable, "-c", textwrap.dedent("""
                import os, sys, time
                sys.path.insert(0, {tools!r})
                import azctl
                with azctl.state_lock("holder"):
                    print("held", flush=True)
                    time.sleep(2)
            """).format(tools=TOOLS)],
            env=dict(os.environ), stdout=subprocess.PIPE, text=True)
        try:
            self.assertEqual(holder.stdout.readline().strip(), "held")
            old = azctl.LOCK_WAIT_S
            azctl.LOCK_WAIT_S = 0.5
            try:
                with self.assertRaises(RuntimeError):
                    with azctl.state_lock("second"):
                        pass
            finally:
                azctl.LOCK_WAIT_S = old
        finally:
            holder.wait(timeout=10)
        with azctl.state_lock("after"):          # free again once the holder exits
            pass


class TestKillPidIsBounded(_Sandbox):
    """MINOR: an unsignalable pid must not spin forever inside kill_pid."""

    def test_unkillable_pid_returns_false_instead_of_hanging(self):
        old_alive, old_grace = azctl.pid_alive, azctl.TERM_GRACE_S
        azctl.pid_alive = lambda pid: True        # PermissionError-style "always alive"
        azctl.TERM_GRACE_S = 0.2
        try:
            t0 = time.time()
            self.assertFalse(azctl.kill_pid(999999, "unkillable"))
            self.assertLess(time.time() - t0, 5.0)
        finally:
            azctl.pid_alive, azctl.TERM_GRACE_S = old_alive, old_grace


if __name__ == "__main__":
    unittest.main()
