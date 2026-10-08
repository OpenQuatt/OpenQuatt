"""Execute the production forced-write lifecycle with bounded Modbus adapters."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CPP = (ROOT / "components/openquatt_odu_defrost/OpenQuattOduDefrost.cpp").read_text()


def method(signature):
    start = CPP.index(signature)
    end = CPP.index("{", start)
    depth = 1
    while depth:
        end += 1
        depth += (CPP[end] == "{") - (CPP[end] == "}")
    return CPP[start:end + 1]


class DefrostRecoveryGuardTest(unittest.TestCase):
    def test_production_forced_write_ack_failures_and_restart_blocker(self):
        methods = "\n".join(method(signature) for signature in (
            "bool OpenQuattOduDefrost::send_forced_once(",
            "void OpenQuattOduDefrost::on_response(",
            "void OpenQuattOduDefrost::on_not_sent(",
            "bool OpenQuattOduDefrost::on_no_response(",
            "void OpenQuattOduDefrost::on_error(",
        ))
        setup = method("void OpenQuattOduDefrost::setup()")
        registration = setup[setup.index("  if (auth_"):setup.index("  set_parent(")]
        timeout = method("if (forced_write_pending_ &&")
        fixture = (Path(__file__).parent / "fixtures/defrost_recovery_guard.cpp").read_text()
        fixture = fixture.replace("// PRODUCTION_METHODS", methods)
        fixture = fixture.replace("// PRODUCTION_REGISTRATION", registration)
        fixture = fixture.replace("// PRODUCTION_TIMEOUT", timeout)
        with tempfile.TemporaryDirectory(prefix="oq-defrost-guard-") as directory:
            source = Path(directory) / "test.cpp"
            source.write_text(fixture)
            command = [os.environ.get("CXX", "c++"), "-std=c++20", "-Wall", "-Wextra", "-Werror"]
            if sys.platform == "darwin":
                sdk = subprocess.check_output(["xcrun", "--sdk", "macosx", "--show-sdk-path"], text=True).strip()
                command.extend(["-isystem", str(Path(sdk) / "usr/include/c++/v1")])
            binary = Path(directory) / "test"
            result = subprocess.run([*command, "-I", str(ROOT / "openquatt"), str(source), "-o", str(binary)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(binary)], check=True, timeout=15)


if __name__ == "__main__":
    unittest.main()
