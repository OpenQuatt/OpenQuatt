"""Execute the complete production learning status body against its 4 KiB cache."""
from pathlib import Path
import json
import re
import tempfile
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
            self.assertFalse(payload["auto_apply_allowed"])
            self.assertEqual(payload["collection"]["batch_missing_energy_budget_wh"], 2400)
            self.assertEqual(payload["collection"]["batch_resume_reason"], "checkpoint_context_unavailable")


if __name__ == "__main__":
    unittest.main()
