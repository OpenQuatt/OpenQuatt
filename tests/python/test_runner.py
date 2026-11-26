"""Failure boundaries of the shared test runner, independent of firmware."""

from contextlib import redirect_stderr, redirect_stdout
import io
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

from scripts import run_tests


class TestRunnerTest(unittest.TestCase):
    def test_skipped_required_test_fails_the_run(self):
        class RequiredIntegration(unittest.TestCase):
            def runTest(self):
                self.skipTest("missing integration dependency")

        output = io.StringIO()
        suite = unittest.TestSuite([RequiredIntegration()])
        self.assertFalse(run_tests.run_python_suite(suite, output))
        self.assertIn("missing integration dependency", output.getvalue())

    def test_empty_python_discovery_fails(self):
        with self.assertRaisesRegex(RuntimeError, "empty suite"):
            run_tests.run_python_suite(unittest.TestSuite(), io.StringIO())

    def test_failing_python_test_fails_the_run(self):
        class BrokenTest(unittest.TestCase):
            def runTest(self):
                self.fail("injected failure")

        self.assertFalse(run_tests.run_python_suite(unittest.TestSuite([BrokenTest()]), io.StringIO()))

    def test_list_does_not_require_a_compiler_or_integration_dependencies(self):
        output = io.StringIO()
        with mock.patch.dict(os.environ, {"CXX": "missing-test-compiler"}), redirect_stdout(output):
            self.assertEqual(run_tests.main(["host", "--list", "--pattern", "supervisory_safety_logic_test.cpp"]), 0)
        self.assertIn("host: 1 test files", output.getvalue())

    def test_pattern_typo_cannot_silently_pass(self):
        with redirect_stderr(io.StringIO()):
            self.assertEqual(run_tests.main(["host", "--pattern", "missing-test-file.cpp"]), 1)

    def test_legacy_test_directory_cannot_silently_lose_new_regressions(self):
        with tempfile.TemporaryDirectory(prefix="openquatt-legacy-tests-") as directory:
            root = Path(directory)
            legacy = root / "scripts/tests/test_legacy.py"
            legacy.parent.mkdir(parents=True)
            legacy.write_text("# Test added by a branch predating the test move.\n")
            host = root / "tests/host/probe_test.cpp"
            host.parent.mkdir(parents=True)
            host.write_text("int main() { return 0; }\n")
            output = io.StringIO()
            with mock.patch.object(run_tests, "ROOT", root), redirect_stderr(output):
                self.assertEqual(run_tests.main(["host", "--list"]), 1)
            self.assertIn("move them to tests/python", output.getvalue())

    def test_host_patterns_find_flat_and_nested_binaries(self):
        with tempfile.TemporaryDirectory(prefix="openquatt-host-discovery-") as directory:
            root = Path(directory)
            host = root / "tests/host"
            flat = host / "probe_test.cpp"
            nested = host / "power_house/probe_test.cpp"
            nested.parent.mkdir(parents=True)
            for path in (flat, nested):
                path.touch()
            (nested.parent / "fixture.cpp").touch()
            with mock.patch.object(run_tests, "ROOT", root):
                self.assertCountEqual(run_tests.test_files("host"), [flat, nested])
                self.assertCountEqual(run_tests.test_files("host", "probe_test.cpp"), [flat, nested])
                self.assertEqual(run_tests.test_files("host", "power_house/*_test.cpp"), [nested])

    def test_chain_test_generates_runtime_includes_at_any_depth(self):
        with tempfile.TemporaryDirectory(prefix="openquatt-chain-discovery-") as directory:
            root = Path(directory)
            source = root / "tests/host/thermal/scenarios/renamed_chain_test.cpp"
            source.parent.mkdir(parents=True)
            source.write_text('#include "runtime_fixture.inc"\nint main() { return 0; }\n')

            def write_runtime_includes(repo, build_directory):
                self.assertEqual(repo, root)
                (build_directory / "runtime_fixture.inc").touch()

            with mock.patch.object(run_tests, "ROOT", root), mock.patch.object(
                run_tests, "write_firmware_runtime_methods", side_effect=write_runtime_includes
            ):
                run_tests.run_host([source])

    def test_host_build_and_runtime_failures_propagate(self):
        with tempfile.TemporaryDirectory(prefix="openquatt-runner-test-") as directory:
            root = Path(directory)
            source = root / "tests/host/power_house/probe_test.cpp"
            source.parent.mkdir(parents=True)
            with mock.patch.object(run_tests, "ROOT", root), mock.patch.dict(os.environ, {"OPENQUATT_TEST_SANITIZERS": "none"}):
                source.write_text("int main() { return 0; }\n")
                run_tests.run_host([source])
                source.write_text("int main() { return 37; }\n")
                with self.assertRaises(subprocess.CalledProcessError) as failure:
                    run_tests.run_host([source])
                self.assertEqual(failure.exception.returncode, 37)
                source.write_text("#error injected build failure\n")
                with self.assertRaises(subprocess.CalledProcessError):
                    run_tests.run_host([source])

    def test_hung_host_test_is_terminated(self):
        with tempfile.TemporaryDirectory(prefix="openquatt-runner-timeout-") as directory:
            root = Path(directory)
            source = root / "tests/host/hung_test.cpp"
            source.parent.mkdir(parents=True)
            source.write_text("int main() { for (;;) {} }\n")
            with mock.patch.object(run_tests, "ROOT", root), mock.patch.object(run_tests, "RUN_TIMEOUT_SECONDS", 0.2), mock.patch.dict(os.environ, {"OPENQUATT_TEST_SANITIZERS": "none"}):
                with self.assertRaises(subprocess.TimeoutExpired):
                    run_tests.run_host([source])


if __name__ == "__main__":
    unittest.main()
