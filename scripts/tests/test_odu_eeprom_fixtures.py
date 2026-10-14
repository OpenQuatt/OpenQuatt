"""Corruption checks for the existing EEPROM fixture headers."""
import importlib.util
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts/validate_odu_eeprom_fixtures.py"
SPEC = importlib.util.spec_from_file_location("odu_eeprom_fixtures", SCRIPT)
VALIDATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VALIDATOR)


def replace_array(text, name, values):
    return re.sub(r"(\b" + name + r"\s*=\s*\{)[^{}]*(\}\s*;)",
                  lambda match: match[1] + ", ".join(map(str, values)) + match[2], text)


class OduEepromFixtureTest(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.repo = Path(directory.name)
        self.fixtures = self.repo / "tests/fixtures"
        self.fixtures.mkdir(parents=True)
        for name, _, _ in VALIDATOR.FIXTURES:
            shutil.copyfile(ROOT / "tests/fixtures" / name, self.fixtures / name)

    def run_validator(self):
        report_path = self.repo / "report.json"
        process = subprocess.run([sys.executable, "-B", str(SCRIPT), "--repo-root", str(self.repo),
                                  "--json-report", str(report_path)], capture_output=True, text=True)
        self.assertNotIn("Traceback", process.stderr)
        report = json.loads(report_path.read_text())
        self.assertEqual(process.returncode, 0 if report["result"] == "PASS" else 1, process.stderr)
        return report

    def test_modbus_crc_known_answer(self):
        self.assertEqual(VALIDATOR.modbus(b"123456789"), 0x4B37)

    def test_cpp_literals_and_comments(self):
        text = "WORDS = {055U, 0x37U, 55, 0}; /* CRC = 9; */ CRC = 055U;"
        self.assertEqual(VALIDATOR.fixture_array(text, "WORDS", 4), [45, 55, 55, 0])
        self.assertEqual(VALIDATOR.fixture_scalar(text, "CRC"), 45)
        with self.assertRaises(ValueError):
            VALIDATOR.fixture_scalar("CRC = 089U;", "CRC")

    def test_all_existing_fixtures_and_both_runtime_captures_pass(self):
        report = self.run_validator()
        self.assertEqual(report["result"], "PASS", report)
        self.assertEqual(len(report["fixtures"]), 4)
        self.assertEqual(sum(item["runtime_capture"] == "CRC_MISMATCH_CONFIRMED"
                             for item in report["fixtures"]), 2)

    def test_baseline_corruption_fails(self):
        path = self.fixtures / "v1_eeprom.h"
        text = path.read_text()
        words = VALIDATOR.fixture_array(text, "EEPROM_WORDS", 512)
        words[100] ^= 1
        path.write_text(replace_array(text, "EEPROM_WORDS", words))
        self.assertIn("baseline EEPROM CRC mismatch", " ".join(self.run_validator()["errors"]))

    def test_another_valid_fixture_cannot_replace_either_v2_reference(self):
        for name, replacement in (("v2_old_amh6_eeprom.h", "v2_new_amh6_eeprom.h"),
                                  ("v2_new_amh6_eeprom.h", "v2_old_amh6_eeprom.h")):
            with self.subTest(fixture=name):
                shutil.copyfile(ROOT / "tests/fixtures" / replacement, self.fixtures / name)
                errors = self.run_validator()["errors"]
                self.assertTrue(any(error.startswith(name + ":") and
                                    "EEPROM payload differs from pinned fixture" in error for error in errors), errors)

    def test_changed_data_fails_even_with_valid_recalculated_crc(self):
        path = self.fixtures / "v2_old_amh6_eeprom.h"
        text = path.read_text()
        words = VALIDATOR.fixture_array(text, "EEPROM_WORDS", 512)
        words[100] ^= 1
        crc = VALIDATOR.modbus(bytes(words[:510]))
        words[510:512] = [crc & 255, crc >> 8]
        self.assertEqual(VALIDATOR.modbus(bytes(words)), 0)
        text = replace_array(text, "EEPROM_WORDS", words)
        text = re.sub(r"(\bEEPROM_CRC\s*=\s*)[^;]+;", lambda match: match[1] + str(crc) + "U;", text)
        path.write_text(text)
        self.assertIn("EEPROM payload differs from pinned fixture", " ".join(self.run_validator()["errors"]))

    def test_changed_capture_fails_even_with_updated_calculated_crc(self):
        for name in ("v1_eeprom.h", "v1_5_eeprom.h"):
            with self.subTest(fixture=name):
                path = self.fixtures / name
                text = path.read_text()
                captured = VALIDATOR.fixture_array(text, "CAPTURED_COOLING_WORDS", 11)
                captured[2] ^= 1
                runtime = VALIDATOR.fixture_array(text, "EEPROM_WORDS", 512)
                runtime[1:12] = captured
                crc = VALIDATOR.modbus(bytes(runtime[:510]))
                self.assertNotEqual(crc, runtime[510] | runtime[511] << 8)
                text = replace_array(text, "CAPTURED_COOLING_WORDS", captured)
                text = re.sub(r"(\bCAPTURED_CALCULATED_CRC\s*=\s*)[^;]+;",
                              lambda match: match[1] + str(crc) + "U;", text)
                path.write_text(text)
                errors = self.run_validator()["errors"]
                self.assertTrue(any(error.startswith(name + ":") and
                                    "runtime payload differs from pinned capture" in error for error in errors), errors)

    def test_comments_and_equivalent_cpp_literals_preserve_pinned_payload(self):
        path = self.fixtures / "v2_old_amh6_eeprom.h"
        text = path.read_text()
        words = VALIDATOR.fixture_array(text, "EEPROM_WORDS", 512)
        text = replace_array(text, "EEPROM_WORDS", ["0" + format(word, "o") + "U" for word in words])
        path.write_text(text + "\n// Comment-only updates do not change EEPROM data.\n")
        self.assertEqual(self.run_validator()["result"], "PASS")

    def test_high_byte_is_not_hidden_by_low_byte_crc(self):
        path = self.fixtures / "v1_eeprom.h"
        text = path.read_text()
        words = VALIDATOR.fixture_array(text, "EEPROM_WORDS", 512)
        words[100] |= 0x100
        path.write_text(replace_array(text, "EEPROM_WORDS", words))
        self.assertIn("EEPROM high byte is nonzero", " ".join(self.run_validator()["errors"]))

    def test_fixture_crc_constant_must_match_stored_crc(self):
        path = self.fixtures / "v1_eeprom.h"
        path.write_text(path.read_text().replace("EEPROM_CRC = 0x2A5DU;", "EEPROM_CRC = 0U;"))
        self.assertIn("baseline CRC constant differs", " ".join(self.run_validator()["errors"]))

    def test_both_retained_runtime_mismatches_are_required(self):
        for name in ("v1_eeprom.h", "v1_5_eeprom.h"):
            with self.subTest(fixture=name):
                path = self.fixtures / name
                text = path.read_text()
                words = VALIDATOR.fixture_array(text, "EEPROM_WORDS", 512)
                path.write_text(replace_array(text, "CAPTURED_COOLING_WORDS", words[1:12]))
                errors = self.run_validator()["errors"]
                self.assertTrue(any(error.startswith(name + ":") and
                                    "retained runtime CRC mismatch disappeared" in error for error in errors), errors)

    def test_invalid_or_missing_array_reports_failure(self):
        path = self.fixtures / "v1_eeprom.h"
        text = path.read_text()
        path.write_text(text.replace("EEPROM_WORDS = {", "OTHER_WORDS = {"))
        self.assertIn("missing or ambiguous declaration", " ".join(self.run_validator()["errors"]))

    def test_missing_fixture_reports_failure(self):
        (self.fixtures / "v1_eeprom.h").unlink()
        self.assertEqual(self.run_validator()["result"], "FAIL")


if __name__ == "__main__":
    unittest.main()
