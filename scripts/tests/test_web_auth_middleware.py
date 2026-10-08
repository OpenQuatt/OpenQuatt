from pathlib import Path
import os
import importlib.util
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
FIXTURES = Path(__file__).parent / "fixtures" / "web_auth"


class WebAuthMiddlewareTest(unittest.TestCase):
    def test_runtime_auth_transitions_and_concurrent_credentials(self):
        self.compile_and_run("middleware_test", component=True)

    def test_component_state_and_storage_failures(self):
        self.compile_and_run("component_test", component=True)

    def test_recovery_capability_and_failure_boundaries(self):
        self.compile_and_run("recovery_test", component=True, recovery=True)

    def test_wifi_recovery_capability_and_failure_boundaries(self):
        self.compile_and_run("recovery_test", component=True, recovery=True, wifi=True)

    def compile_and_run(self, name, component=False, recovery=False, wifi=False):
        spec = importlib.util.find_spec("esphome")
        self.assertIsNotNone(
            spec,
            "Install the pinned test dependency: python3 -m pip install -r .github/requirements-esphome.txt",
        )
        upstream = Path(spec.submodule_search_locations[0]) / "components/web_server_base"
        with tempfile.TemporaryDirectory(prefix="openquatt-auth-test-") as directory:
            binary = Path(directory) / name
            command = [os.environ.get("CXX", "c++"), "-std=c++20", "-pthread",
                       "-Wall", "-Wextra", "-Werror"]
            if wifi:
                command.append("-DUSE_WIFI")
            if sys.platform == "darwin":
                sdk = subprocess.check_output(
                    ["xcrun", "--sdk", "macosx", "--show-sdk-path"], text=True
                ).strip()
                headers = Path(sdk) / "usr/include/c++/v1"
                if headers.exists():
                    command.extend(["-isystem", str(headers)])
            # Expose the real component at ESPHome's generated include location.
            include_root = Path(directory) / "esphome/components"
            include_root.mkdir(parents=True)
            (include_root / "web_server_base").symlink_to(upstream)
            (include_root / "openquatt_web_auth").symlink_to(ROOT / "components/openquatt_web_auth")
            command.extend([
                "-I", str(FIXTURES), "-I", str(upstream),
                "-I", directory, "-I", str(ROOT / "components/openquatt_web_auth"),
                str(FIXTURES / f"{name}.cpp"),
                str(upstream / "web_server_base.cpp"),
                "-o", str(binary),
            ])
            if component:
                command.append(str(ROOT / "components/openquatt_web_auth/OpenQuattWebAuth.cpp"))
            if recovery:
                command.extend(["-I", str(ROOT / "components/openquatt_recovery"),
                                str(ROOT / "components/openquatt_recovery/OpenQuattRecovery.cpp")])
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(binary)], check=True, timeout=15)


if __name__ == "__main__":
    unittest.main()
