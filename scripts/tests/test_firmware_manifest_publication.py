from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = Path(__file__).parent / "fixtures"


class FirmwareManifestPublicationTest(unittest.TestCase):
    def compile_and_run(self, source):
        with tempfile.TemporaryDirectory() as directory:
            unit = Path(directory) / "publication.cpp"
            binary = Path(directory) / "publication"
            unit.write_text(source)
            command = [os.environ.get("CXX", "c++"), "-std=c++17", "-pthread", "-Wall", "-Wextra", "-Werror"]
            if sys.platform == "darwin":
                sdk = subprocess.check_output(["xcrun", "--sdk", "macosx", "--show-sdk-path"], text=True).strip()
                headers = Path(sdk) / "usr/include/c++/v1"
                if headers.exists():
                    command.extend(["-isystem", str(headers)])
            command.extend([
                "-I", str(FIXTURES / "firmware_metadata"),
                "-I", str(FIXTURES / "web_auth"),
                "-I", str(ROOT / "components/openquatt_firmware_metadata"),
                str(unit), str(ROOT / "components/openquatt_firmware_metadata/OpenQuattFirmwareMetadata.cpp"),
                "-o", str(binary),
            ])
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(binary)], check=True, timeout=15)

    def test_production_metadata_auth_routing_and_concurrent_publication(self):
        self.compile_and_run((FIXTURES / "firmware_metadata/metadata_test.cpp").read_text())

    def test_ota_error_abort_and_progress_cannot_confirm_a_manual_check(self):
        common = (ROOT / "openquatt/oq_common.yaml").read_text()
        start = common.index("            // A successful manifest fetch")
        end = common.index("            const uint32_t request_generation", start)
        callback = common[start:end]
        # Execute the actual YAML callback gate with the upstream update lifecycle:
        # progress publishes INSTALLING; error/abort set an error before AVAILABLE;
        # successful fetch clears the error before publishing, even for the same version.
        source = r"""
#include "OpenQuattFirmwareMetadata.h"
#include <cstdlib>
#define assert(condition) do { if (!(condition)) std::abort(); } while (false)
namespace esphome {
namespace update { enum { UPDATE_STATE_INSTALLING, UPDATE_STATE_AVAILABLE, UPDATE_STATE_NO_UPDATE }; }
}
struct Metadata : esphome::openquatt_firmware_metadata::OpenQuattFirmwareMetadata {
unsigned revision() const { return manifest_revision_.load(); }
} oq_firmware_metadata;
struct Update { int state = esphome::update::UPDATE_STATE_NO_UPDATE; bool error = false;
bool status_has_error() const { return error; } } oq_firmware_update;
bool oq_firmware_target_install_active = false;
#define id(x) x
void publish() {
""" + callback + r"""
}
int main() {
using namespace esphome::update;
publish(); assert(oq_firmware_metadata.revision() == 1);
publish(); assert(oq_firmware_metadata.revision() == 2); // unchanged successful manifest
oq_firmware_update.state = UPDATE_STATE_INSTALLING;
publish(); assert(oq_firmware_metadata.revision() == 2);
for (int terminal = 0; terminal < 2; ++terminal) { // error and abort
oq_firmware_target_install_active = false; // YAML OTA listener has already run
oq_firmware_update.state = UPDATE_STATE_AVAILABLE;
oq_firmware_update.error = true;
publish(); assert(oq_firmware_metadata.revision() == 2);
}
oq_firmware_update.error = false; // a later successful manifest clears the error
publish(); assert(oq_firmware_metadata.revision() == 3);
oq_firmware_update.state = UPDATE_STATE_NO_UPDATE;
publish(); assert(oq_firmware_metadata.revision() == 4);
oq_firmware_target_install_active = true;
publish(); assert(oq_firmware_metadata.revision() == 4);
}
"""
        self.compile_and_run(source)


if __name__ == "__main__":
    unittest.main()
