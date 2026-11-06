"""Exercise the production boot-recovery owner, including its verified-ticket gate."""
from pathlib import Path
import tempfile
import unittest

from tests.python.test_learning_status_json_runtime import production_function
from tests.support.cpp import compiler_command, COMPILE_TIMEOUT_SECONDS, RUN_TIMEOUT_SECONDS
from tests.support.process import run_process

ROOT = Path(__file__).resolve().parents[2]


class LearningRestartRuntimeTest(unittest.TestCase):
    def test_actual_owner_waits_restores_or_discards_without_touching_retained_learning(self):
        source = (ROOT / "openquatt/includes/control/oq_ph_learning_runtime.h").read_text()
        branch = production_function(source, "if (state.daily_restore_pending) {")
        with tempfile.TemporaryDirectory(prefix="oq-learning-restart-owner-") as directory:
            temporary = Path(directory)
            (temporary / "production_daily_restart.inc").write_text(branch)
            (temporary / "production_restart_interruption.inc").write_text(production_function(
                source, "if (const char* reason = daily_restart_interruption_reason("))
            (temporary / "production_prepared_restart.inc").write_text(production_function(
                source, "if (state.restart_prepared) {\n      if (!coherent_daily_restart_tick"))
            (temporary / "production_retry_prepare.inc").write_text(production_function(
                source, "if (state.restart_prepared) {\n      // OTA retry"))
            (temporary / "production_supply_calibration_change.inc").write_text("\n".join(
                production_function(source, signature) for signature in (
                    "static void clear_daily_resume_status_(",
                    "static void cancel_daily_resume_status_(",
                    "void supply_calibration_changed()",
                )))
            binary = temporary / "restart-owner"
            built = run_process(compiler_command(
                "-fsanitize=undefined", "-fno-sanitize-recover=all", "-I", str(temporary),
                str(ROOT / "tests/fixtures/learning/restart_runtime.cpp"), "-o", str(binary)),
                timeout=COMPILE_TIMEOUT_SECONDS, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = run_process([str(binary)], timeout=RUN_TIMEOUT_SECONDS, capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main()
