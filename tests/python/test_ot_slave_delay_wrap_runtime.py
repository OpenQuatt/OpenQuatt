"""Execute the native OT transport's production process body around cycle wrap."""

from pathlib import Path
import tempfile
import unittest

import re
from tests.support.cpp import COMPILE_TIMEOUT_SECONDS, RUN_TIMEOUT_SECONDS, compiler_command
from tests.support.process import run_process


ROOT = Path(__file__).resolve().parents[2]
COMPONENT = ROOT / "components/openquatt_ot_slave"
FIXTURE = ROOT / "tests/fixtures/ot_slave/delay_wrap.cpp"


def production_function(source: str, signature: str) -> str:
    """Copy one complete function verbatim, ignoring braces inside comments/strings."""
    if source.count(signature) != 1:
        raise AssertionError(f"Production signature changed or is ambiguous: {signature}")
    start = source.index(signature)
    opening = source.index("{", start)
    tokens = re.compile(r'''//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|[{}]''')
    depth = 0
    for token in tokens.finditer(source, opening):
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if depth == 0:
                return source[start:token.end()]
    raise AssertionError(f"Unterminated production function: {signature}")


class OtSlaveDelayWrapRuntimeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="oq-ot-delay-wrap-")
        cls.addClassCleanup(cls.directory.cleanup)
        temporary = Path(cls.directory.name)
        header = (COMPONENT / "OpenTherm.h").read_text()
        (temporary / "production_ot_enums.inc").write_text(
            "\n".join(
                production_function(header, f"enum {name} {{") + ";"
                for name in ("OpenThermStatus", "OpenThermResponseStatus", "OpenThermDriverError")
            )
        )
        source = (COMPONENT / "OpenTherm.cpp").read_text()
        (temporary / "production_ot_process.inc").write_text(
            "\n\n".join(
                production_function(source, signature)
                for signature in (
                    "void OpenTherm::process_rmt_frame_(",
                    "bool OpenTherm::sendResponse(",
                    "bool OpenTherm::process()",
                )
            )
        )
        cls.binary = temporary / "ot-delay-wrap"
        result = run_process(
            compiler_command(
                "-fsanitize=undefined", "-fno-sanitize-recover=all",
                "-fno-omit-frame-pointer", "-g", "-I", str(temporary),
                str(FIXTURE), "-o", str(cls.binary),
            ),
            timeout=COMPILE_TIMEOUT_SECONDS, capture_output=True, text=True,
        )
        if result.returncode != 0:
            raise AssertionError(result.stdout + result.stderr)

    def test_delay_boundaries_and_rx_transitions(self):
        for scenario in (
            "normal",
            "deadline_wrap",
            "poll_wrap",
            "zero_start",
            "zero_boundary",
            "rx_overwrite",
            "callback_tx",
        ):
            with self.subTest(scenario=scenario):
                result = run_process(
                    [str(self.binary), scenario], timeout=RUN_TIMEOUT_SECONDS,
                    capture_output=True, text=True,
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
