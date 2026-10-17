from pathlib import Path
import re
import tempfile
import unittest

from tests.support.cpp import COMPILE_TIMEOUT_SECONDS, compiler_command
from tests.support.process import run_process

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/fixtures/http_status"


class HttpStatusRuntimeTest(unittest.TestCase):
    def test_status_headers_body_and_failure_boundaries(self):
        with tempfile.TemporaryDirectory(prefix="openquatt-http-status-") as directory:
            binary = Path(directory) / "http-status"
            command = compiler_command()
            command.extend(["-I", str(FIXTURES), "-I", str(ROOT / "components/openquatt_common"),
                            str(FIXTURES / "http_status.cpp"), "-o", str(binary)])
            result = run_process(command, capture_output=True, text=True, timeout=COMPILE_TIMEOUT_SECONDS)
            self.assertEqual(result.returncode, 0, result.stderr)
            run_process([str(binary)], check=True, timeout=15)

    def test_direct_responses_preserve_current_default_headers(self):
        header = (ROOT / "components/web_server_base/web_server_base.h").read_text()
        defaults = re.findall(r'DefaultHeaders::Instance\(\).addHeader\((.*?)\);', header)
        self.assertEqual(defaults, ['ESPHOME_F("Access-Control-Allow-Origin"), ESPHOME_F("*")'])
        codegen = (ROOT / "components/web_server_base/__init__.py").read_text()
        self.assertIn('cg.add_define("WEB_SERVER_DEFAULT_HEADERS_COUNT", 1)', codegen)

    def test_custom_handlers_do_not_use_unmapped_numeric_statuses(self):
        for source in (ROOT / "components").glob("openquatt_*/*.cpp"):
            with self.subTest(source=source.relative_to(ROOT)):
                self.assertNotRegex(source.read_text(), r'request->send\((202|403|410|429|503|504)\b')

    def test_common_header_is_copied_to_generated_firmware(self):
        self.assertIn("${oq_component_http_status_header}", (ROOT / "openquatt/base/common.yaml").read_text())
        self.assertIn('oq_component_http_status_header: "${components_root}/openquatt_common/OpenQuattHttpStatus.h"',
                      (ROOT / "openquatt/oq_substitutions_common.yaml").read_text())


if __name__ == "__main__":
    unittest.main()
