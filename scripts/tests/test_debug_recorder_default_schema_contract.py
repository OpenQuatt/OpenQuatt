from pathlib import Path
import re
import subprocess
import sys
import unittest


ROOT = Path(__file__).resolve().parents[2]
CONFIG_JS = (ROOT / "openquatt" / "web" / "js" / "src" / "core" / "config.js").read_text()
RECORDER_SOURCE = (
    ROOT / "components" / "openquatt_debug_recorder" / "OpenQuattDebugRecorder.cpp"
).read_text()
RECORDER_HEADER = (
    ROOT / "components" / "openquatt_debug_recorder" / "OpenQuattDebugRecorder.h"
).read_text()
GENERATED_INC = ROOT / "components" / "openquatt_debug_recorder" / "debug_recorder_default_fields.h"

SUPPORTED_DOMAINS = {
    "sensor",
    "number",
    "binary_sensor",
    "switch",
    "text_sensor",
    "select",
}


def debug_recording_keys() -> list[str]:
    match = re.search(r"export const DEBUG_RECORDING_KEYS = \[(.*?)\];", CONFIG_JS, re.S)
    assert match is not None
    return re.findall(r'"([^"]+)"', match.group(1))


class DebugRecorderDefaultSchemaContractTest(unittest.TestCase):
    def test_generated_schema_is_in_sync(self) -> None:
        result = subprocess.run(
            [sys.executable, "scripts/generate_debug_recorder_default_fields.py", "--check"],
            cwd=ROOT,
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_generated_fields_match_webapp_keys_in_order(self) -> None:
        keys = debug_recording_keys()
        generated = re.findall(
            r'OQ_DEBUG_RECORDER_DEFAULT_FIELD\("([^"]+)", "([^"]+)", "([^"]+)"\)',
            GENERATED_INC.read_text(),
        )
        self.assertEqual([entry[0] for entry in generated], keys)
        for _, domain, _ in generated:
            self.assertIn(domain, SUPPORTED_DOMAINS)

    def test_default_schema_fits_field_budget(self) -> None:
        keys = debug_recording_keys()
        header_capacity = int(re.search(r"FIELD_CAPACITY = (\d+)", RECORDER_HEADER).group(1))
        header_system = int(re.search(r"SYSTEM_FIELD_COUNT = (\d+)", RECORDER_HEADER).group(1))
        self.assertLessEqual(len(keys) + header_system, header_capacity)

    def test_firmware_uses_default_schema_without_webapp(self) -> None:
        self.assertIn("configure_default_schema_", RECORDER_SOURCE)
        self.assertIn("start_rolling_locked_", RECORDER_SOURCE)
        self.assertIn("load_enabled_preference_", RECORDER_SOURCE)
        # setup() must configure + start without any pending browser configuration
        setup = RECORDER_SOURCE[RECORDER_SOURCE.index("void OpenQuattDebugRecorder::setup()"):]
        setup = setup[: setup.index("void OpenQuattDebugRecorder::loop()")]
        self.assertIn("configure_default_schema_", setup)
        self.assertIn("start_rolling_locked_", setup)

    def test_restart_does_not_need_pending_configuration(self) -> None:
        restart = RECORDER_SOURCE[RECORDER_SOURCE.index("bool OpenQuattDebugRecorder::restart_rolling()"):]
        restart = restart[: restart.index("void OpenQuattDebugRecorder::stop()")]
        self.assertNotIn("activate_pending_configuration_", restart)
        self.assertIn("configure_default_schema_", restart)

    def test_status_reports_enabled_and_no_frozen(self) -> None:
        self.assertIn('"enabled"', RECORDER_SOURCE)
        self.assertNotIn("frozen", RECORDER_SOURCE)
        self.assertNotIn("frozen", RECORDER_HEADER)

    def test_range_export_rebases_window(self) -> None:
        self.assertIn("/openquatt/debug-recording/download-range", RECORDER_SOURCE)
        self.assertIn("export_start_index", RECORDER_SOURCE)
        self.assertIn("range_started_at_ms", RECORDER_SOURCE)
        self.assertIn("first_offset", RECORDER_SOURCE)

    def test_export_duration_describes_window_not_runtime(self) -> None:
        # Ook bij Alles (export_start_index == 0) is duration_s het venster.
        self.assertIn("window_duration_s", RECORDER_SOURCE)
        self.assertIn('writer.write_uint32(export_duration_s)', RECORDER_SOURCE)
        self.assertNotIn('writer.write_uint32(snapshot.duration_s)', RECORDER_SOURCE)

    def test_partial_event_count_skips_window_initial(self) -> None:
        self.assertIn("for (size_t index = 1; index < export_count; ++index)", RECORDER_SOURCE)

    def test_set_enabled_is_idempotent(self) -> None:
        setter = RECORDER_SOURCE[RECORDER_SOURCE.index("void OpenQuattDebugRecorder::set_enabled"):]
        setter = setter[: setter.index("void OpenQuattDebugRecorder::rotate_csrf_token_")]
        same_value_branch = setter[: setter.index("this->enabled_ = enabled;")]
        self.assertNotIn("start_rolling_locked_", same_value_branch)

    def test_restart_respects_opt_out(self) -> None:
        restart = RECORDER_SOURCE[RECORDER_SOURCE.index("bool OpenQuattDebugRecorder::restart_rolling()"):]
        restart = restart[: restart.index("void OpenQuattDebugRecorder::stop()")]
        self.assertIn("!this->enabled_", restart)


if __name__ == "__main__":
    unittest.main()
