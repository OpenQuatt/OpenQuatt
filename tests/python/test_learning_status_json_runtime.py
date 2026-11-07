"""Execute the complete production learning status body against its 4 KiB cache."""
from pathlib import Path
import json
import re
import tempfile
from textwrap import dedent
import unittest

from tests.support.cpp import compiler_command, COMPILE_TIMEOUT_SECONDS, RUN_TIMEOUT_SECONDS
from tests.support.process import run_process

ROOT = Path(__file__).resolve().parents[2]


def production_function(source, signature):
    if source.count(signature) != 1:
        raise AssertionError(f"Production signature changed or is ambiguous: {signature}")
    start = source.index(signature)
    opening = source.index("{", start)
    tokens = re.compile(r"""//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|[{}]""")
    depth = 0
    for token in tokens.finditer(source, opening):
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if depth == 0:
                return source[start:token.end()]
    raise AssertionError(f"Unterminated production function: {signature}")


class LearningStatusJsonRuntimeTest(unittest.TestCase):
    def test_actual_export_aligns_all_quality_rows_and_preserves_unknowns(self):
        source = (ROOT / "openquatt/includes/control/oq_ph_learning_runtime.h").read_text()
        with tempfile.TemporaryDirectory(prefix="oq-learning-quality-export-") as directory:
            temporary = Path(directory)
            body = "\n".join(production_function(source, signature) for signature in (
                "static void clear_daily_resume_status_(", "static void cancel_daily_resume_status_(",
                "static void update_daily_resume_status_(", "void pause(LearningStatus",
                "static void publish_status_(", "static void publish_export_(",
            ))
            for name, flag, signature in (
                ("context_change", "context_changed", "if (context_changed) {"),
                ("reset", "reset_processed", "if (reset_processed) {\n      state.daily_restore_pending"),
            ):
                branch = production_function(source, signature)
                body += f"\nstatic void {name}(RuntimeStorage& state) {{ const bool {flag} = true; {branch} }}"
            (temporary / "production_learning_status.inc").write_text(body)
            (temporary / "production_diagnostic_row.inc").write_text(
                production_function(source, "struct DiagnosticRow {") + ";\n")
            binary = temporary / "quality-export"
            built = run_process(compiler_command(
                "-DOQ_TEST_EXPORT_JSON=1", "-fsanitize=undefined", "-fno-sanitize-recover=all",
                "-I", str(temporary), str(ROOT / "tests/fixtures/learning/status_json.cpp"), "-o", str(binary)),
                timeout=COMPILE_TIMEOUT_SECONDS, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = run_process([str(binary)], timeout=RUN_TIMEOUT_SECONDS, capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            payload = json.loads(run.stdout)
            self.assertLess(len(run.stdout.encode()), 37 * 1024)
            self.assertEqual(len(payload["record_columns"]), 10)
            self.assertEqual(len(payload["records"]), 128)
            self.assertEqual(len(payload["diagnostics"]), 60)
            self.assertEqual(len(payload["record_quality"]), 128)
            self.assertEqual(payload["record_quality"][0], [10000,10000,0,True,10000])
            self.assertEqual(payload["record_quality"][1], [10000,9999,100,True,4999])
            self.assertEqual(payload["record_quality"][2], [10000,10000,"overflow",False,0])
            self.assertEqual(payload["record_quality"][3], [None,None,None,True,5000])
            self.assertEqual(payload["collection_state"]["batch_budget_scope"], "model_advice_only")
            self.assertFalse(payload["auto_apply_allowed"])

    def test_cached_shadow_is_revoked_at_read_without_changing_learning_status(self):
        source = (ROOT / "components/openquatt_house_learning_status/OpenQuattHouseLearningStatus.cpp").read_text()
        learning = (ROOT / "openquatt/includes/control/oq_ph_learning_runtime.h").read_text()
        power_house = (ROOT / "openquatt/includes/control/oq_power_house_runtime.h").read_text()
        common = (ROOT / "openquatt/oq_common.yaml").read_text()
        with tempfile.TemporaryDirectory(prefix="oq-learning-shadow-cache-") as directory:
            temporary = Path(directory)
            functions = "\n".join(production_function(source, signature) for signature in (
                "void OpenQuattHouseLearningStatus::invalidate_shadow(",
                "bool OpenQuattHouseLearningStatus::publish_status_json(",
                "bool OpenQuattHouseLearningStatus::snapshot_status(",
            ))
            (temporary / "production_shadow_status.inc").write_text(functions)
            (temporary / "production_shadow_invalidation.inc").write_text(
                production_function(power_house, "void invalidate_demand_evaluation("))
            (temporary / "production_external_override.inc").write_text(
                production_function(learning, "id(external_heat_demand_selected).add_on_state_callback(") + ");")
            pause_action = common.split("id: oq_runtime_polling_paused\n", 1)[1].split("- lambda: |-\n", 1)[1]
            (temporary / "production_shadow_pause.inc").write_text(
                dedent(pause_action.split("            // This lifecycle edge", 1)[0]))
            binary = temporary / "shadow-cache"
            built = run_process(
                compiler_command("-DOQ_POWER_HOUSE_LEARNING_TARGET=1", "-fsanitize=undefined",
                                 "-fno-sanitize-recover=all",
                                 "-I", str(temporary), str(ROOT / "tests/fixtures/learning/shadow_cache.cpp"),
                                 "-o", str(binary)),
                timeout=COMPILE_TIMEOUT_SECONDS, capture_output=True, text=True,
            )
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = run_process([str(binary)], timeout=RUN_TIMEOUT_SECONDS, capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_full_production_status_and_all_enum_names_fit_existing_cache(self):
        with tempfile.TemporaryDirectory(prefix="oq-learning-status-json-") as directory:
            temporary = Path(directory)
            source = (ROOT / "openquatt/includes/control/oq_ph_learning_runtime.h").read_text()
            body = "\n".join(production_function(source, signature) for signature in (
                "static void clear_daily_resume_status_(", "static void cancel_daily_resume_status_(",
                "static void update_daily_resume_status_(",
                "void pause(LearningStatus", "static void publish_status_(",
            ))
            for name, flag, signature in (
                ("context_change", "context_changed", "if (context_changed) {"),
                ("reset", "reset_processed", "if (reset_processed) {\n      state.daily_restore_pending"),
            ):
                branch = production_function(source, signature)
                body += f"\nstatic void {name}(RuntimeStorage& state) {{ const bool {flag} = true; {branch} }}"
            (temporary / "production_learning_status.inc").write_text(body)
            binary = temporary / "status-json"
            built = run_process(
                compiler_command("-fsanitize=undefined", "-fno-sanitize-recover=all",
                                 "-I", str(temporary), str(ROOT / "tests/fixtures/learning/status_json.cpp"),
                                 "-o", str(binary)),
                timeout=COMPILE_TIMEOUT_SECONDS, capture_output=True, text=True,
            )
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = run_process([str(binary)], timeout=RUN_TIMEOUT_SECONDS, capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            payload = json.loads(run.stdout)
            self.assertLess(len(run.stdout.encode()), 4096)
            self.assertEqual(payload["shadow"]["tick_ms"], 2**64 - 1)
            self.assertEqual(payload["shadow"]["context_revision"], 2**32 - 1)
            self.assertTrue(payload["shadow"]["available"])
            self.assertFalse(payload["shadow"]["auto_apply_allowed"])
            self.assertEqual(payload["shadow"]["delta_w"], -350)
            self.assertFalse(payload["auto_apply_allowed"])
            self.assertEqual(payload["collection"]["batch_missing_energy_budget_wh"], 2400)
            self.assertEqual(payload["batch_preview"], {
                "h": 211.25, "t0": 17.125, "segments": 21, "days": 21, "min_c": 1.97, "max_c": 12.34,
            })
            self.assertEqual(payload["batch_fit"], {
                "train_segments": 18, "holdout_segments": 3, "train_days": 18, "holdout_days": 3,
                "train_p10_c": 8.28, "train_p90_c": 11.88, "train_min_c": 7.55, "train_max_c": 12.34,
            })
            self.assertEqual(payload["collection"]["batch_budget_scope"], "model_advice_only")
            self.assertAlmostEqual(payload["collection"]["batch_missing_heat_bound_w"], 18344.3, places=1)
            self.assertEqual(payload["collection"]["batch_resume_reason"], "checkpoint_context_unavailable")


if __name__ == "__main__":
    unittest.main()
