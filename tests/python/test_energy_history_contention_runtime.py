from pathlib import Path
import re
import tempfile
import unittest

from tests.support.cpp import COMPILE_TIMEOUT_SECONDS, RUN_TIMEOUT_SECONDS, compiler_command
from tests.support.process import run_process

ROOT = Path(__file__).resolve().parents[2]
COMPONENT = ROOT / "components/openquatt_energy_history/OpenQuattEnergyHistory"


class EnergyHistoryContentionRuntimeTest(unittest.TestCase):
    def test_control_loop_retries_without_moving_samples_across_boundaries(self):
        header = COMPONENT.with_suffix(".h").read_text()
        source = COMPONENT.with_suffix(".cpp").read_text()
        lock = source[source.index("constexpr uint32_t SHUTDOWN_LOCK_TIMEOUT_MS"):source.index("static std::string base64_encode_bytes_")]
        records = header[header.index("  struct __attribute__((packed)) EnergyHistoryValues"):
                         header.index("  struct ExternalState {")]
        methods = (
            "loop", "on_shutdown", "force_flush", "refresh_hourly_retention", "service_pending_history_",
            "process_pending_history_locked_", "flush_current_day_locked_", "capture_day_totals", "apply_sample_locked_",
            "clear_history", "clear_hour_records_", "pack_values_", "record_has_values_", "encode_kwh_", "delta_values_",
            "add_wh_", "delta_wh_", "capture_hour_delta_", "restore_current_day_hours_",
            "import_history_records", "parse_import_csrf_token", "clear_history_locked_", "write_record_", "read_record_", "record_valid_",
            "scan_archive_", "fnv1a_hash_", "record_crc_", "date_key_valid_", "date_bitmap_index_",
            "date_bitmap_get_", "date_bitmap_set_", "split_import_fields_", "parse_import_uint32_",
        )
        declarations = []
        definitions = []
        for name in methods:
            declarations.append(re.search(r"(?m)^  [^\n]*\b" + name + r"\([^;]*?;", header)[0].replace(" override", ""))
            start = re.search(r"(?m)^\S+ OpenQuattEnergyHistory::" + name + r"\(", source).start()
            line_end = source.index("\n", start)
            end = line_end if source[start:line_end].endswith("}") else source.index("\n}", line_end) + 2
            definitions.append(source[start:end])
        start = source.index("static std::string trim_copy(")
        definitions.insert(0, source[start:source.index("\n}", start) + 2])
        fixture = (ROOT / "tests/fixtures/energy_history/contention_runtime.cpp").read_text()
        program = (fixture.replace("// COMPONENT_LOCK", lock)
                   .replace("// COMPONENT_RECORDS", records)
                   .replace("// COMPONENT_DECLARATIONS", "\n".join(declarations))
                   .replace("// COMPONENT_DEFINITIONS", "\n".join(definitions)))
        with tempfile.TemporaryDirectory() as temp:
            binary = Path(temp) / "energy_history_contention"
            result = run_process(compiler_command("-x", "c++", "-", "-o", str(binary)), input=program,
                                 text=True, capture_output=True, timeout=COMPILE_TIMEOUT_SECONDS)
            self.assertEqual(result.returncode, 0, result.stderr[-6000:])
            result = run_process([str(binary)], text=True, capture_output=True, timeout=RUN_TIMEOUT_SECONDS)
            self.assertEqual(result.returncode, 0, result.stderr[-6000:])
