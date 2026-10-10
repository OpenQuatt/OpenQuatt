import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SINGLE_TARGET = (ROOT / "configs" / "heatpump_controller_q" / "single.yaml").read_text()
DUO_TARGET = (ROOT / "configs" / "heatpump_controller_q" / "duo.yaml").read_text()
SINGLE_TOPOLOGY = (ROOT / "openquatt" / "topology" / "single.yaml").read_text()
DUO_TOPOLOGY = (ROOT / "openquatt" / "topology" / "duo.yaml").read_text()
Q_PROFILE = (ROOT / "openquatt" / "profiles" / "heatpump_controller_q.yaml").read_text()
NETWORK_PROFILE = (ROOT / "openquatt" / "connection" / "wifi_eth.yaml").read_text()
HIL_DUO = (ROOT / "configs" / "heatpump_controller_q" / "duo_hil.yaml").read_text()


def yaml_scalar(text: str, key: str) -> str:
    match = re.search(
        rf'(?m)^\s*{re.escape(key)}\s*:\s*"([^"]*)"\s*(?:#.*)?$',
        text,
    )
    if not match:
        raise AssertionError(f"missing YAML scalar {key}")
    return match.group(1)


def yaml_key_pattern(key: str) -> str:
    return rf'(?m)^\s*{re.escape(key)}\s*:'


class QConfigCompositionContractTest(unittest.TestCase):
    def test_release_targets_only_pin_topology(self) -> None:
        for target, flag in (
            (SINGLE_TARGET, '-DOQ_TOPOLOGY_DUO=0'),
            (DUO_TARGET, '-DOQ_TOPOLOGY_DUO=1'),
        ):
            self.assertRegex(
                target,
                rf'(?m)^\s*oq_topology_build_flag\s*:\s*"{re.escape(flag)}"\s*$',
            )
            for key in (
                "oq_hardware_profile",
                "oq_hardware_build_flag",
                "oq_connection",
                "oq_connection_text_internal",
                "main_release_manifest_url",
                "alternate_topology",
            ):
                self.assertNotRegex(target, yaml_key_pattern(key))

        for package in (SINGLE_TOPOLOGY, DUO_TOPOLOGY):
            self.assertNotRegex(package, yaml_key_pattern("oq_topology_build_flag"))

    def test_hardware_and_network_have_single_owners(self) -> None:
        self.assertIn('oq_hardware_profile: "heatpump_controller_q"', Q_PROFILE)
        self.assertIn('oq_hardware_build_flag: "-DOQ_HARDWARE_HEATPUMP_CONTROLLER_Q=1"', Q_PROFILE)
        self.assertIn('oq_local_supply_temp_sensor_id: "water_supply_temp_pt1000"', Q_PROFILE)
        self.assertIn('oq_local_supply_temp_selector_id: "oq_local_supply_temp_source"', Q_PROFILE)
        self.assertIn('oq_connection: "auto"', NETWORK_PROFILE)
        self.assertIn('oq_connection_text_internal: "false"', NETWORK_PROFILE)

    def test_q_profile_owns_expanded_manifest_routing(self) -> None:
        for package, topology_config, topology, alternate in (
            (Q_PROFILE, SINGLE_TOPOLOGY, "single", "duo"),
            (Q_PROFILE, DUO_TOPOLOGY, "duo", "single"),
        ):
            self.assertEqual(yaml_scalar(topology_config, "oq_topology"), topology)
            self.assertEqual(yaml_scalar(topology_config, "alternate_topology"), alternate)

            main_url = yaml_scalar(package, "main_release_manifest_url").replace(
                "${oq_topology}", topology
            )
            dev_url = yaml_scalar(package, "dev_release_manifest_url").replace(
                "${oq_topology}", topology
            )
            alternate_main_url = yaml_scalar(
                package, "alternate_topology_main_release_manifest_url"
            ).replace("${alternate_topology}", alternate)
            alternate_dev_url = yaml_scalar(
                package, "alternate_topology_dev_release_manifest_url"
            ).replace("${alternate_topology}", alternate)

            self.assertEqual(
                main_url,
                f"https://github.com/OpenQuatt/OpenQuatt/releases/latest/download/openquatt-heatpump-controller-q-{topology}-ota.manifest.json",
            )
            self.assertEqual(
                dev_url,
                f"https://github.com/OpenQuatt/OpenQuatt/releases/download/dev-latest/openquatt-heatpump-controller-q-{topology}-ota.manifest.json",
            )
            self.assertEqual(
                alternate_main_url,
                f"https://github.com/OpenQuatt/OpenQuatt/releases/latest/download/openquatt-heatpump-controller-q-{alternate}-ota.manifest.json",
            )
            self.assertEqual(
                alternate_dev_url,
                f"https://github.com/OpenQuatt/OpenQuatt/releases/download/dev-latest/openquatt-heatpump-controller-q-{alternate}-ota.manifest.json",
            )
            self.assertEqual(
                yaml_scalar(package, "release_manifest_url"),
                "${main_release_manifest_url}",
            )

    def test_grouped_headers_are_not_also_included_as_files(self) -> None:
        grouped_header = re.compile(r"^\s+-\s+[^\n]*(?:\$\{openquatt_root\}|(?:\.\./)+openquatt)/includes/[^\n]+\.(?:h|hpp|tcc)\s*$", re.MULTILINE)
        for path in (ROOT / "openquatt").rglob("*.yaml"):
            self.assertNotRegex(path.read_text(), grouped_header, str(path.relative_to(ROOT)))

    def test_usage_telemetry_detects_hardware_header_in_generated_layout(self) -> None:
        telemetry = (ROOT / "components/openquatt_usage_telemetry/OpenQuattUsageTelemetry.cpp").read_text()
        conditional_include = re.search(
            r"#if __has_include\([^\n]+\)\n.*?#endif", telemetry, re.DOTALL
        )
        self.assertIsNotNone(conditional_include)
        compiler = os.environ.get("CXX", "c++")
        sdk_flags = (["-isystem", "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/c++/v1"]
                     if sys.platform == "darwin" else [])
        with tempfile.TemporaryDirectory() as directory:
            src = Path(directory)
            (src / "esp_efuse.h").write_text(
                "#pragma once\n#include <cstdint>\n"
                "using esp_err_t = int;\nconstexpr esp_err_t ESP_OK = 0;\n"
                "inline esp_err_t esp_efuse_read_field_blob(const void*, void*, unsigned) { return ESP_OK; }\n"
            )
            (src / "esp_efuse_table.h").write_text(
                "#pragma once\ninline constexpr const void* ESP_EFUSE_USER_DATA = nullptr;\n"
            )
            for available in (False, True):
                with self.subTest(header_available=available):
                    if available:
                        shutil.copytree(ROOT / "openquatt/includes/hardware", src / "includes/hardware")
                    expectation = (
                        "#ifndef OPENQUATT_HAS_Q_HARDWARE_REVISION\n#error Missing Q hardware revision\n#endif\n"
                        "int main() { return oq_hardware::read_hardware_revision_efuse().error; }\n"
                        if available else
                        "#ifdef OPENQUATT_HAS_Q_HARDWARE_REVISION\n#error Unexpected Q hardware revision\n#endif\n"
                        "int main() { return 0; }\n"
                    )
                    result = subprocess.run(
                        [compiler, *sdk_flags, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                         "-fsyntax-only", "-x", "c++", "-I", str(src), "-"],
                        input=conditional_include.group(0) + "\n" + expectation,
                        text=True, capture_output=True, check=False,
                    )
                    self.assertEqual(result.returncode, 0, result.stderr)

    def test_hil_build_has_canonical_unified_entrypoint(self) -> None:
        self.assertIn('openquatt_q_duo: !include duo.yaml', HIL_DUO)


if __name__ == "__main__":
    unittest.main()
