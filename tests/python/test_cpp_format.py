from __future__ import annotations

import importlib.util
import subprocess
import unittest
from pathlib import Path
from unittest import mock


SCRIPT_PATH = Path(__file__).resolve().parents[2] / "scripts/cpp_format.py"
SPEC = importlib.util.spec_from_file_location("cpp_format", SCRIPT_PATH)
assert SPEC is not None and SPEC.loader is not None
cpp_format = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(cpp_format)


class CppFormatTests(unittest.TestCase):
    def test_parse_clang_format_version(self) -> None:
        self.assertEqual(
            cpp_format.parse_clang_format_version("clang-format version 22.1.8"),
            "22.1.8",
        )
        self.assertIsNone(cpp_format.parse_clang_format_version("unexpected output"))

    @mock.patch.object(cpp_format.subprocess, "run")
    def test_list_cpp_files_includes_tracked_and_untracked_files(self, run: mock.Mock) -> None:
        run.return_value = subprocess.CompletedProcess(
            args=[], returncode=0, stdout="tracked.cpp\nnew_header.h\ntracked.cpp\n", stderr=""
        )

        files = cpp_format.list_cpp_files(Path("/repo"))

        self.assertEqual(files, ["new_header.h", "tracked.cpp"])
        command = run.call_args.args[0]
        self.assertEqual(
            command[:6],
            ["git", "ls-files", "--cached", "--others", "--exclude-standard", "--"],
        )


if __name__ == "__main__":
    unittest.main()
