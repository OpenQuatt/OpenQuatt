from __future__ import annotations

import hashlib
import importlib.util
import sys
import tempfile
import unittest
from unittest import mock
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "scripts"
sys.path.insert(0, str(SCRIPTS))
MODULE_PATH = SCRIPTS / "symbolize_firmware_crash.py"
SPEC = importlib.util.spec_from_file_location("symbolize_firmware_crash", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class SymbolizeFirmwareCrashTests(unittest.TestCase):
    def test_addresses_are_strictly_validated(self) -> None:
        self.assertEqual(
            ["0x42001234", "0x40370000"],
            MODULE.parse_addresses(["0x42001234, 0x40370000"]),
        )
        for value in ("42001234", "0x1234;uname", "-1"):
            with self.subTest(value=value), self.assertRaises(MODULE.SymbolizationError):
                MODULE.parse_addresses([value])

    def test_hash_mismatch_aborts_before_addr2line(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            elf = root / "firmware.elf"
            cache = root / "CMakeCache.txt"
            elf.write_bytes(b"different")
            cache.write_text("CMAKE_ADDR2LINE:FILEPATH=/missing\n", encoding="utf-8")
            with (
                mock.patch.object(MODULE.subprocess, "run") as run,
                self.assertRaisesRegex(MODULE.SymbolizationError, "refusing symbolization"),
            ):
                MODULE.symbolize(
                    elf=elf,
                    expected_build_id="f" * 64,
                    addresses=["0x42000000"],
                    cmake_cache=cache,
                )
            run.assert_not_called()

    def test_verified_elf_is_passed_to_addr2line_without_shell(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            elf = root / "firmware.elf"
            tool = root / "xtensa-esp32s3-elf-addr2line"
            cache = root / "CMakeCache.txt"
            elf.write_bytes(b"exact elf")
            tool.write_text("", encoding="utf-8")
            cache.write_text(f"CMAKE_ADDR2LINE:FILEPATH={tool}\n", encoding="utf-8")
            build_id = hashlib.sha256(elf.read_bytes()).hexdigest()
            completed = mock.Mock(returncode=0, stdout="decoded\n", stderr="")

            with mock.patch.object(MODULE.subprocess, "run", return_value=completed) as run:
                output = MODULE.symbolize(
                    elf=elf,
                    expected_build_id=build_id,
                    addresses=["0x42000000"],
                    cmake_cache=cache,
                )

            self.assertEqual("decoded\n", output)
            command = run.call_args.args[0]
            self.assertEqual(str(tool.resolve()), command[0])
            self.assertIn("0x42000000", command)
            self.assertNotIn("shell", run.call_args.kwargs)


if __name__ == "__main__":
    unittest.main()
