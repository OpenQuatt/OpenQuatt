"""Exercise command results and real descendant cleanup on every exit path."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

from tests.support.process import run_process


class ProcessSupportTest(unittest.TestCase):
    def setUp(self):
        self.processes = []
        popen = subprocess.Popen
        self.popen_class = popen

        def record_process(*args, **kwargs):
            process = popen(*args, **kwargs)
            self.processes.append(process)
            return process

        self.spawn = patch("tests.support.process.subprocess.Popen", side_effect=record_process)
        self.spawn.start()
        self.addCleanup(self.spawn.stop)

    def assert_reaped(self):
        for process in self.processes:
            self.assertIsNotNone(process.returncode)
            with self.assertRaises(ChildProcessError):
                os.waitpid(process.pid, os.WNOHANG)

    def grandchild_command(self, heartbeat, *, wrapper_exits=False, child_captures=True):
        child = """
import pathlib, sys, time
path = pathlib.Path(sys.argv[1])
while True:
    with path.open("ab") as output:
        output.write(b".")
    time.sleep(0.01)
"""
        redirection = "" if child_captures else ", stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL"
        parent = f"""
import pathlib, subprocess, sys, time
subprocess.Popen([sys.executable, "-c", {child!r}, {str(heartbeat)!r}]{redirection})
path = pathlib.Path({str(heartbeat)!r})
while not path.exists() or path.stat().st_size < 2:
    time.sleep(0.01)
print("ready", flush=True)
print("waiting", file=sys.stderr, flush=True)
{"" if wrapper_exits else "time.sleep(60)"}
"""
        return [sys.executable, "-c", parent]

    def assert_grandchild_stopped(self, heartbeat):
        before = heartbeat.read_bytes()
        self.assertGreaterEqual(len(before), 2, "grandchild did not become active")
        time.sleep(0.15)
        self.assertEqual(heartbeat.read_bytes(), before, "grandchild is still running after cleanup")
        self.assert_reaped()

    def wait_for_grandchild(self, heartbeat):
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if heartbeat.exists() and heartbeat.stat().st_size >= 2:
                return
            time.sleep(0.01)
        self.fail("grandchild never became active")

    def test_text_input_cwd_env_and_nonzero_result(self):
        with tempfile.TemporaryDirectory() as directory:
            command = [sys.executable, "-c", (
                "import os, sys; print(sys.stdin.read() + os.environ['OQ_PROCESS_TEST']); "
                "print(os.getcwd(), file=sys.stderr); sys.exit(7)"
            )]
            result = run_process(
                command, timeout=5, input="input:", text=True, capture_output=True,
                cwd=directory, env={**os.environ, "OQ_PROCESS_TEST": "value"},
            )
            self.assertIsInstance(result, subprocess.CompletedProcess)
            self.assertEqual(result.args, command)
            self.assertEqual(result.returncode, 7)
            self.assertEqual(result.stdout, "input:value\n")
            self.assertEqual(Path(result.stderr.strip()).resolve(), Path(directory).resolve())
        self.assert_reaped()

    def test_check_raises_with_text_output(self):
        command = [sys.executable, "-c", "import sys; print('out'); print('err', file=sys.stderr); sys.exit(3)"]
        with self.assertRaises(subprocess.CalledProcessError) as raised:
            run_process(command, timeout=5, check=True, capture_output=True, text=True)
        self.assertEqual(raised.exception.returncode, 3)
        self.assertEqual(raised.exception.cmd, command)
        self.assertEqual(raised.exception.stdout, "out\n")
        self.assertEqual(raised.exception.stderr, "err\n")
        self.assert_reaped()

    def test_binary_input_and_explicit_merged_output(self):
        command = [sys.executable, "-c", (
            "import os, sys; os.write(1, sys.stdin.buffer.read()); os.write(2, b'error\\n')"
        )]
        result = run_process(
            command, timeout=5, input=b"input\n", stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        )
        self.assertEqual(result.stdout, b"input\nerror\n")
        self.assertIsNone(result.stderr)
        self.assert_reaped()

    def test_timeout_stops_active_grandchild_and_preserves_partial_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            heartbeat = Path(directory) / "heartbeat"
            command = self.grandchild_command(heartbeat)
            with self.assertRaises(subprocess.TimeoutExpired) as raised:
                run_process(command, timeout=1, capture_output=True, text=True)
            self.assertEqual(raised.exception.cmd, command)
            self.assertEqual(raised.exception.timeout, 1)
            self.assertIn(b"ready\n", raised.exception.stdout)
            self.assertIn(b"waiting\n", raised.exception.stderr)
            self.assert_grandchild_stopped(heartbeat)

    def test_timeout_cleans_descendants_after_wrapper_has_exited(self):
        with tempfile.TemporaryDirectory() as directory:
            heartbeat = Path(directory) / "heartbeat"
            command = self.grandchild_command(heartbeat, wrapper_exits=True)
            with self.assertRaises(subprocess.TimeoutExpired):
                run_process(command, timeout=1, capture_output=True)
            self.assert_grandchild_stopped(heartbeat)

    def test_success_cleans_descendants_with_closed_capture_pipes(self):
        with tempfile.TemporaryDirectory() as directory:
            heartbeat = Path(directory) / "heartbeat"
            command = self.grandchild_command(heartbeat, wrapper_exits=True, child_captures=False)
            result = run_process(command, timeout=5, capture_output=True, check=True)
            self.assertEqual(result.stdout, b"ready\n")
            self.assert_grandchild_stopped(heartbeat)

    def test_abrupt_runner_exit_closes_guardian_lease_and_stops_command(self):
        with tempfile.TemporaryDirectory() as directory:
            heartbeat = Path(directory) / "heartbeat"
            command = self.grandchild_command(heartbeat)
            parent_code = (
                "from tests.support.process import run_process; "
                f"run_process({command!r}, timeout=60, capture_output=True)"
            )
            parent = subprocess.Popen(
                [sys.executable, "-c", parent_code],
                cwd=Path(__file__).resolve().parents[2],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            )
            try:
                self.wait_for_grandchild(heartbeat)
            finally:
                parent.kill()
                parent.wait(timeout=5)
            # The guardian observes EOF even when the owner cannot run finally.
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                before = heartbeat.read_bytes()
                time.sleep(0.2)
                if heartbeat.read_bytes() == before:
                    break
            else:
                self.fail("guardian left its command running after abrupt owner exit")
            self.assert_grandchild_stopped(heartbeat)

    def test_interrupt_cleans_group_even_if_cleanup_is_interrupted_again(self):
        with tempfile.TemporaryDirectory() as directory:
            heartbeat = Path(directory) / "heartbeat"
            command = self.grandchild_command(heartbeat)

            def interrupt(process, *args, **kwargs):
                self.wait_for_grandchild(heartbeat)
                wait = process.wait
                calls = 0

                def interrupted_wait(*wait_args, **wait_kwargs):
                    nonlocal calls
                    calls += 1
                    if calls == 1:
                        raise KeyboardInterrupt
                    return wait(*wait_args, **wait_kwargs)

                process.wait = interrupted_wait
                raise KeyboardInterrupt

            killpg = os.killpg
            signals = 0

            def interrupted_killpg(pid, sig):
                nonlocal signals
                signals += 1
                if signals == 1:
                    raise KeyboardInterrupt
                killpg(pid, sig)

            with patch.object(self.popen_class, "communicate", autospec=True, side_effect=interrupt), patch(
                "tests.support.process.os.killpg", side_effect=interrupted_killpg
            ):
                with self.assertRaises(KeyboardInterrupt):
                    run_process(command, timeout=5, capture_output=True)
            self.assert_grandchild_stopped(heartbeat)

    def test_other_communication_error_cleans_group_and_preserves_exception(self):
        with tempfile.TemporaryDirectory() as directory:
            heartbeat = Path(directory) / "heartbeat"
            command = self.grandchild_command(heartbeat)

            def fail(process, *args, **kwargs):
                self.wait_for_grandchild(heartbeat)
                raise RuntimeError("injected communication failure")

            with patch.object(self.popen_class, "communicate", autospec=True, side_effect=fail):
                with self.assertRaisesRegex(RuntimeError, "injected communication failure"):
                    run_process(command, timeout=5, capture_output=True)
            self.assert_grandchild_stopped(heartbeat)

    def test_disappeared_group_does_not_hide_success(self):
        killpg = os.killpg

        def already_gone(pid, sig):
            try:
                killpg(pid, sig)
            except ProcessLookupError:
                pass
            raise ProcessLookupError

        with patch("tests.support.process.os.killpg", side_effect=already_gone):
            result = run_process([sys.executable, "-c", "pass"], timeout=5)
        self.assertEqual(result.returncode, 0)
        self.assert_reaped()

    def test_guardian_keeps_group_reserved_after_success_and_nonzero_command_exit(self):
        killpg = os.killpg
        for returncode in (0, 7):
            with self.subTest(returncode=returncode):
                self.processes.clear()
                signals = []

                def inspect_guardian(pgid, sig):
                    guardian, command = self.processes
                    try:
                        self.assertEqual(pgid, guardian.pid)
                        self.assertEqual(command.returncode, returncode)
                        self.assertIsNone(guardian.returncode)
                        # WNOWAIT observes child state without releasing its PID.
                        # None proves it is still alive when group cleanup starts.
                        state = os.waitid(os.P_PID, guardian.pid, os.WEXITED | os.WNOHANG | os.WNOWAIT)
                        self.assertIsNone(state, "guardian exited before the group's signal")
                        signals.append(pgid)
                    finally:
                        killpg(guardian.pid, sig)

                command = [sys.executable, "-c", f"import sys; sys.exit({returncode})"]
                with patch("tests.support.process.os.killpg", side_effect=inspect_guardian):
                    if returncode:
                        with self.assertRaises(subprocess.CalledProcessError):
                            run_process(command, timeout=5, check=True)
                    else:
                        result = run_process(command, timeout=5, check=True)
                        self.assertEqual(result.returncode, 0)
                self.assertEqual(len(signals), 1)
                self.assert_reaped()

    def test_shell_group_overrides_and_invalid_stream_combinations_are_rejected(self):
        for kwargs in (
            {"shell": True}, {"start_new_session": True}, {"process_group": 0},
            {"input": "value", "stdin": subprocess.PIPE},
            {"capture_output": True, "stdout": subprocess.PIPE},
        ):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                run_process([sys.executable, "-c", "pass"], timeout=5, **kwargs)
        self.assertEqual(self.processes, [])
        with patch("tests.support.process.os.name", "nt"), self.assertRaisesRegex(NotImplementedError, "POSIX"):
            run_process([sys.executable, "-c", "pass"], timeout=5)

    def test_missing_executable_keeps_file_not_found_error(self):
        with tempfile.TemporaryDirectory() as directory, self.assertRaises(FileNotFoundError):
            run_process([str(Path(directory) / "missing")], timeout=5)
        self.assert_reaped()


if __name__ == "__main__":
    unittest.main()
