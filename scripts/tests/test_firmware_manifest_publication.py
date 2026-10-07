from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class FirmwareManifestPublicationTest(unittest.TestCase):
    def test_ota_error_abort_and_progress_cannot_confirm_a_manual_check(self):
        common = (ROOT / "openquatt/oq_common.yaml").read_text()
        start = common.index("            // A successful manifest fetch")
        end = common.index("            const uint32_t request_generation", start)
        callback = common[start:end]
        # Execute the actual YAML callback gate with the upstream update lifecycle:
        # progress publishes INSTALLING; error/abort set an error before AVAILABLE;
        # successful fetch clears the error before publishing, even for the same version.
        source = r"""
#define assert(condition) do { if (!(condition)) return 1; } while (false)
namespace esphome {
namespace update { enum { UPDATE_STATE_INSTALLING, UPDATE_STATE_AVAILABLE, UPDATE_STATE_NO_UPDATE }; }
namespace web_server {
struct WebServer { static inline unsigned revision = 0;
static void record_firmware_manifest_publication() { ++revision; } };
}
}
struct Update { int state = esphome::update::UPDATE_STATE_NO_UPDATE; bool error = false;
bool status_has_error() const { return error; } } oq_firmware_update;
bool oq_firmware_target_install_active = false;
#define id(x) x
void publish() {
""" + callback + r"""
}
int main() {
using esphome::web_server::WebServer;
using namespace esphome::update;
publish(); assert(WebServer::revision == 1);
publish(); assert(WebServer::revision == 2); // unchanged successful manifest
oq_firmware_update.state = UPDATE_STATE_INSTALLING;
publish(); assert(WebServer::revision == 2);
for (int terminal = 0; terminal < 2; ++terminal) { // error and abort
oq_firmware_target_install_active = false; // YAML OTA listener has already run
oq_firmware_update.state = UPDATE_STATE_AVAILABLE;
oq_firmware_update.error = true;
publish(); assert(WebServer::revision == 2);
}
oq_firmware_update.error = false; // a later successful manifest clears the error
publish(); assert(WebServer::revision == 3);
oq_firmware_update.state = UPDATE_STATE_NO_UPDATE;
publish(); assert(WebServer::revision == 4);
oq_firmware_target_install_active = true;
publish(); assert(WebServer::revision == 4);
}
"""
        with tempfile.TemporaryDirectory() as directory:
            unit = Path(directory) / "publication.cpp"
            binary = Path(directory) / "publication"
            unit.write_text(source)
            subprocess.run(["c++", "-std=c++17", str(unit), "-o", str(binary)], check=True, capture_output=True)
            subprocess.run([str(binary)], check=True, capture_output=True)


if __name__ == "__main__":
    unittest.main()
