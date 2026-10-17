"""Exercise compiler configuration and the assertion/sanitizer safety gates."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from tests.support.cpp import COMPILE_TIMEOUT_SECONDS, RUN_TIMEOUT_SECONDS, compiler_command
from tests.support.process import run_process


class CompilerCommandTest(unittest.TestCase):
    def setUp(self):
        self.environment = patch.dict(os.environ, {}, clear=True)
        self.environment.start()
        self.addCleanup(self.environment.stop)
        self.platform = patch("tests.support.cpp.sys.platform", "linux")
        self.platform.start()
        self.addCleanup(self.platform.stop)
        self.which = patch("tests.support.cpp.shutil.which", return_value="/usr/bin/c++")
        self.which_mock = self.which.start()
        self.addCleanup(self.which.stop)

    def test_default_warning_policy_and_explicit_flags(self):
        command = compiler_command("-pthread", "-DNDEBUG")
        self.assertEqual(command[0], "c++")
        for flag in ("-std=c++17", "-Wall", "-Wextra", "-Werror", "-pthread"):
            self.assertIn(flag, command)
        self.assertGreater(command.index("-UNDEBUG"), command.index("-DNDEBUG"))
        self.assertFalse(any(flag.startswith("-fsanitize=") for flag in command))

    def test_cxx_quoted_path_and_wrapper_arguments_are_not_a_shell(self):
        os.environ["CXX"] = '"/opt/compiler tools/ccache" clang++ -DVALUE=3'
        command = compiler_command()
        self.assertEqual(command[:3], ["/opt/compiler tools/ccache", "clang++", "-DVALUE=3"])
        self.which_mock.assert_called_once_with("/opt/compiler tools/ccache")

    def test_missing_or_invalid_cxx_fails_clearly(self):
        for value in ("", "missing-cxx"):
            with self.subTest(cxx=value), patch("tests.support.cpp.shutil.which", return_value=None):
                os.environ["CXX"] = value
                with self.assertRaisesRegex(RuntimeError, r"C\+\+ compiler executable not found"):
                    compiler_command()
        os.environ["CXX"] = '"unterminated'
        with self.assertRaisesRegex(ValueError, "Invalid CXX command"):
            compiler_command()

    def test_sanitizers_are_explicit_and_nonrecovering(self):
        os.environ["OPENQUATT_TEST_SANITIZERS"] = "address,undefined"
        command = compiler_command()
        for flag in ("-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer", "-g"):
            self.assertIn(flag, command)
        os.environ["OPENQUATT_TEST_SANITIZERS"] = "none"
        self.assertFalse(any(flag.startswith("-fsanitize=") for flag in compiler_command()))
        for value in ("address", "undefined", "address,undefined,thread", "yes"):
            with self.subTest(value=value):
                os.environ["OPENQUATT_TEST_SANITIZERS"] = value
                with self.assertRaisesRegex(ValueError, "OPENQUATT_TEST_SANITIZERS"):
                    compiler_command()

    def test_sdkroot_takes_precedence_over_xcrun(self):
        with tempfile.TemporaryDirectory() as directory:
            headers = Path(directory) / "usr/include/c++/v1"
            headers.mkdir(parents=True)
            os.environ["SDKROOT"] = directory
            with patch("tests.support.cpp.sys.platform", "darwin"), patch(
                "tests.support.cpp.run_process"
            ) as xcrun:
                command = compiler_command()
            self.assertEqual(command[command.index("-isystem") + 1], str(headers))
            xcrun.assert_not_called()

    def test_xcrun_lookup_is_bounded_and_supplies_sdk_headers(self):
        with tempfile.TemporaryDirectory() as directory:
            headers = Path(directory) / "usr/include/c++/v1"
            headers.mkdir(parents=True)
            with patch("tests.support.cpp.sys.platform", "darwin"), patch(
                "tests.support.cpp.run_process", return_value=subprocess.CompletedProcess(["xcrun"], 0, directory + "\n", "")
            ) as xcrun:
                command = compiler_command()
            self.assertIn(str(headers), command)
            self.assertEqual(xcrun.call_args.args[0], ["xcrun", "--sdk", "macosx", "--show-sdk-path"])
            self.assertLessEqual(xcrun.call_args.kwargs["timeout"], 10)

    def test_sdk_without_libcpp_headers_leaves_compiler_default_search(self):
        with tempfile.TemporaryDirectory() as directory:
            os.environ["SDKROOT"] = directory
            with patch("tests.support.cpp.sys.platform", "darwin"):
                self.assertNotIn("-isystem", compiler_command())

    def test_missing_or_failing_xcrun_has_actionable_error(self):
        with patch("tests.support.cpp.sys.platform", "darwin"):
            with patch(
                "tests.support.cpp.shutil.which",
                side_effect=lambda tool: None if tool == "xcrun" else "/usr/bin/c++",
            ):
                with self.assertRaisesRegex(RuntimeError, "xcrun.*SDKROOT"):
                    compiler_command()
            with patch("tests.support.cpp.run_process", side_effect=subprocess.TimeoutExpired("xcrun", 10)):
                with self.assertRaisesRegex(RuntimeError, "macOS SDK.*SDKROOT"):
                    compiler_command()
            with patch("tests.support.cpp.run_process", return_value=subprocess.CompletedProcess(["xcrun"], 0, "\n", "")):
                with self.assertRaisesRegex(RuntimeError, "no macOS SDK"):
                    compiler_command()


class CompilerAssertionTest(unittest.TestCase):
    def test_real_assertion_still_fails_when_cxx_defines_ndebug(self):
        cxx = os.environ.get("CXX", "c++")
        with patch.dict(os.environ, {"CXX": cxx + " -DNDEBUG", "OPENQUATT_TEST_SANITIZERS": "none"}):
            command = compiler_command("-DNDEBUG")
        source = r"""
#include <cassert>
#include <csignal>
#include <cstdlib>
int main() {
  std::signal(SIGABRT, [](int) { std::_Exit(99); });
  assert(false && "negative control: assertions must stay enabled");
}
"""
        with tempfile.TemporaryDirectory(prefix="openquatt-cpp-helper-") as directory:
            binary = Path(directory) / "assertion"
            compiled = run_process(
                [*command, "-x", "c++", "-", "-o", str(binary)], input=source,
                text=True, capture_output=True, timeout=COMPILE_TIMEOUT_SECONDS,
            )
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            executed = run_process([str(binary)], capture_output=True, timeout=RUN_TIMEOUT_SECONDS)
            self.assertEqual(executed.returncode, 99, "CXX disabled the assertion regression")


if __name__ == "__main__":
    unittest.main()
