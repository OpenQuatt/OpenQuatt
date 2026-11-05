"""Exercise the real CM adapter, boiler contract and collector across frost circulation."""
from pathlib import Path
import tempfile
import unittest

from tests.python.test_learning_status_json_runtime import production_function
from tests.support.cpp import compiler_command, COMPILE_TIMEOUT_SECONDS, RUN_TIMEOUT_SECONDS
from tests.support.process import run_process

ROOT = Path(__file__).resolve().parents[2]


def bounded_section(source, start, end):
    if source.count(start) != 1 or source.count(end) != 1:
        raise AssertionError("Production adapter boundaries changed or are ambiguous")
    first, last = source.index(start), source.index(end)
    if last <= first:
        raise AssertionError("Production adapter boundaries are reversed")
    return source[first:last]


class LearningControlModeRuntimeTest(unittest.TestCase):
    def test_actual_frost_adapter_collects_without_bypassing_independent_gates(self):
        source = (ROOT / "openquatt/includes/control/oq_ph_learning_runtime.h").read_text()
        capture = production_function(source, "void capture_(")
        sections = {
            "production_learning_mode.inc": bounded_section(
                capture, "const int cm = id(oq_control_mode_code);", "op.active_limit_valid ="),
            "production_learning_service.inc": bounded_section(
                capture, "op.service_or_ota_valid = true;", "const bool boiler_heat_observed ="),
        }
        with tempfile.TemporaryDirectory(prefix="oq-learning-control-mode-") as directory:
            temporary = Path(directory)
            for name, body in sections.items():
                (temporary / name).write_text(body)
            binary = temporary / "control-mode"
            built = run_process(compiler_command(
                "-fsanitize=undefined", "-fno-sanitize-recover=all", "-I", str(temporary),
                str(ROOT / "tests/fixtures/learning/control_mode_runtime.cpp"), "-o", str(binary)),
                timeout=COMPILE_TIMEOUT_SECONDS, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = run_process([str(binary)], timeout=RUN_TIMEOUT_SECONDS, capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main()
