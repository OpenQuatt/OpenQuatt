"""Execute the production consent methods against a fallible preference backend."""

from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "components/openquatt_performance_telemetry/OpenQuattPerformanceTelemetry.cpp"
USAGE_SOURCE = ROOT / "components/openquatt_usage_telemetry/OpenQuattUsageTelemetry.cpp"


class PerformanceConsentFailureTest(unittest.TestCase):
    def test_persistence_failure_and_retry(self) -> None:
        compiler = shutil.which(os.environ.get("CXX", "c++"))
        self.assertIsNotNone(compiler, "A C++ compiler is required for consent failure injection")
        source = SOURCE.read_text()
        methods = []
        for signature in (
            "bool OpenQuattPerformanceTelemetry::load_storage_",
            "bool OpenQuattPerformanceTelemetry::save_storage_",
            "bool OpenQuattPerformanceTelemetry::apply_storage_",
            "void OpenQuattPerformanceTelemetry::write_state",
        ):
            start = source.index(signature)
            end = source.index("\n}", start) + 2
            methods.append(source[start:end])
        harness = r'''
#include <cassert>
#include <atomic>
#include <cstdint>
#include <initializer_list>
#define ESP_LOGE(...) do {} while (0)
struct Storage {
  uint32_t magic;
  uint16_t version;
  uint8_t enabled;
  uint8_t choice_configured;
};
struct Backend {
  Storage durable{1, 2, 1, 1};
  Storage queued = durable;
  bool fail_save{false}, fail_sync{false}, pending{false};
  int saves{0}, fail_sync_count{0};
  bool save(const Storage* value) {
    ++saves;
    if (fail_save) return false;
    queued = *value;
    pending = true;
    return true;
  }
  bool sync() {
    if (fail_sync || fail_sync_count-- > 0) return false;
    if (pending) durable = queued;
    pending = false;
    return true;
  }
} backend;
Backend* global_preferences = &backend;
struct Preference {
  bool save(const Storage* storage) { return backend.save(storage); }
  bool load(Storage* storage) {
    *storage = backend.pending ? backend.queued : backend.durable;
    return true;
  }
};
struct Sensor {
  bool state{true};
  void publish_state(bool value) { state = value; }
};
struct Transport {
  bool blocked{false}, id_available{true};
  bool ensure_installation_id_for_external() { return id_available; }
  void cancel_external_publish() { blocked = true; }
};
struct OpenQuattPerformanceTelemetry {
  static constexpr uint32_t STORAGE_MAGIC = 1;
  static constexpr uint16_t STORAGE_VERSION = 2;
  static constexpr uint8_t CHOICE_CONFIGURED = 1, PROMPT_HANDLED = 2;
  Preference pref_;
  Transport transport;
  Transport* transport_ = &transport;
  Sensor sensor;
  Sensor* choice_configured_sensor_ = &sensor;
  Sensor prompt_sensor;
  Sensor* prompt_handled_sensor_ = &prompt_sensor;
  std::atomic<bool> enabled_{true}, choice_configured_{true}, prompt_handled_{false};
  bool published{true};
  int resets{0};
  void publish_state(bool value) { published = value; }
  void reset_collection_() { ++resets; }
  bool load_storage_(Storage* value);
  bool save_storage_(const Storage& storage);
  bool apply_storage_(const Storage& storage);
  void write_state(bool state);
};
'''
        cases = r'''
int main() {
  static_assert(sizeof(Storage) == 8);
  // Every legacy OFF state migrates without consent or invitation acknowledgement.
  for (uint8_t enabled : {0, 1}) {
    for (uint8_t choice : {0, 1}) {
      backend = Backend{};
      backend.durable = backend.queued = {1, 1, enabled, choice};
      OpenQuattPerformanceTelemetry component;
      Storage storage{};
      assert(component.load_storage_(&storage) == (enabled == 0 || choice != 0));
      if (enabled != 0 && choice == 0) continue;
      assert(storage.version == 2 && storage.choice_configured == choice);
      assert(backend.saves == 0 && backend.durable.version == 1);
      component.apply_storage_(storage);
      assert(component.enabled_ == (enabled != 0));
      assert(component.choice_configured_ == (choice != 0));
      assert(!component.prompt_handled_ && !component.prompt_sensor.state);
    }
  }
  // Reject corruption and future formats; a popup bit alone is never consent.
  for (Storage invalid : {Storage{0, 2, 0, 0}, Storage{1, 3, 0, 0},
                          Storage{1, 2, 2, 1}, Storage{1, 2, 0, 4},
                          Storage{1, 1, 0, 3}, Storage{1, 2, 1, 2}}) {
    backend = Backend{};
    backend.durable = backend.queued = invalid;
    OpenQuattPerformanceTelemetry component;
    Storage storage{};
    assert(!component.load_storage_(&storage));
  }
  for (uint8_t choice : {0, 1, 2, 3}) {
    backend = Backend{};
    backend.durable = backend.queued = {1, 2, 0, choice};
    OpenQuattPerformanceTelemetry component;
    Storage storage{};
    assert(component.load_storage_(&storage));
    component.apply_storage_(storage);
    assert(component.choice_configured_ == ((choice & 1) != 0));
    assert(component.prompt_handled_ == ((choice & 2) != 0));
  }
  // An old firmware rejects v2 and resets OFF rather than misreading bit1 as consent.
  for (uint8_t enabled : {0, 1}) {
    const Storage updated{1, 2, enabled, 3};
    const bool legacy_accepts = updated.magic == 1 && updated.version == 1 &&
                                updated.enabled <= 1 && updated.choice_configured <= 1;
    assert(!legacy_accepts);
  }
  // Missing installation identity must not confirm either consent or the prompt.
  backend = Backend{};
  OpenQuattPerformanceTelemetry unavailable;
  unavailable.transport.id_available = false;
  unavailable.write_state(true);
  assert(!unavailable.enabled_ && !unavailable.choice_configured_ && !unavailable.prompt_handled_);
  assert(!unavailable.sensor.state && !unavailable.prompt_sensor.state);
  assert(unavailable.transport.blocked && backend.saves == 0);
  // A durable opt-in must not make a failed opt-out look confirmed.
  for (bool fail_save : {false, true}) {
    backend = Backend{};
    OpenQuattPerformanceTelemetry component;
    backend.fail_save = fail_save;
    backend.fail_sync = !fail_save;
    component.write_state(false);
    assert(!component.enabled_ && !component.published);
    assert(!component.choice_configured_ && !component.sensor.state);
    assert(!component.prompt_handled_ && !component.prompt_sensor.state);
    assert(component.transport.blocked && component.resets == 1);
    assert(backend.durable.enabled == 1);
    // The repeated OFF action must really write, even though RAM is OFF.
    const int previous_saves = backend.saves;
    backend.fail_save = backend.fail_sync = false;
    component.write_state(false);
    assert(backend.saves > previous_saves);
    assert(backend.durable.enabled == 0 && backend.durable.choice_configured == 3);
    assert(component.choice_configured_ && component.sensor.state);
    assert(component.prompt_handled_ && component.prompt_sensor.state);
    OpenQuattPerformanceTelemetry reboot;
    reboot.apply_storage_(backend.durable);
    assert(!reboot.enabled_ && reboot.choice_configured_ && reboot.prompt_handled_);
  }
  // An earlier refusal survives identity failure, failed saves and sync failure,
  // including a successful fallback and a later flush followed by reboot.
  for (int failure : {0, 1, 2, 3}) {
    backend = Backend{};
    backend.durable = backend.queued = {1, 2, 0, 3};
    OpenQuattPerformanceTelemetry component;
    component.apply_storage_(backend.durable);
    component.transport.id_available = failure != 0;
    backend.fail_save = failure == 1;
    backend.fail_sync = failure == 2;
    backend.fail_sync_count = failure == 3 ? 1 : 0;
    component.write_state(true);
    assert(!component.enabled_ && !component.choice_configured_);
    assert(component.prompt_handled_ && component.prompt_sensor.state);
    backend.fail_save = backend.fail_sync = false;
    assert(backend.sync());
    OpenQuattPerformanceTelemetry reboot;
    reboot.apply_storage_(backend.durable);
    assert(!reboot.enabled_ && reboot.prompt_handled_ && reboot.prompt_sensor.state);
    if (failure == 2 || failure == 3) assert(backend.durable.choice_configured == 2);
  }
  // Failed opt-in must not escape later via somebody else's global sync.
  backend = Backend{};
  backend.durable.enabled = backend.queued.enabled = 0;
  OpenQuattPerformanceTelemetry component;
  component.apply_storage_(backend.durable);
  backend.fail_sync = true;
  component.write_state(true);
  assert(!component.enabled_ && !component.choice_configured_ && !component.prompt_handled_);
  assert(backend.pending && backend.queued.enabled == 0);
  backend.fail_sync = false;
  assert(backend.sync());
  OpenQuattPerformanceTelemetry reboot;
  reboot.apply_storage_(backend.durable);
  assert(!reboot.enabled_ && !reboot.choice_configured_ && !reboot.prompt_handled_);
  component.write_state(true);
  assert(component.enabled_ && component.choice_configured_ && component.prompt_handled_);
  assert(backend.durable.enabled == 1 && backend.durable.choice_configured == 3);
}
'''
        self.compile_and_run(harness + "\n".join(methods) + cases)

    def compile_and_run(self, source: str) -> None:
        compiler = shutil.which(os.environ.get("CXX", "c++"))
        self.assertIsNotNone(compiler)
        args = [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT)]
        if os.uname().sysname == "Darwin":
            sdk = subprocess.check_output(
                ["xcrun", "--sdk", "macosx", "--show-sdk-path"], text=True
            ).strip()
            args += ["-isystem", str(Path(sdk) / "usr/include/c++/v1")]
        with tempfile.TemporaryDirectory(prefix="openquatt-consent-") as directory:
            path = Path(directory)
            unit = path / "test.cpp"
            unit.write_text(source)
            binary = path / "test"
            compiled = subprocess.run(args + [str(unit), "-o", str(binary)], capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            executed = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(executed.returncode, 0, executed.stderr)

    def test_transport_cooldown_survives_cancellation_and_delayed_sessions(self) -> None:
        source = USAGE_SOURCE.read_text()
        start_method = source.index("void OpenQuattUsageTelemetry::start_publish_session_")
        cooldown_assignment = next(
            line.strip()
            for line in source[start_method : source.index("\n}", start_method)].splitlines()
            if "external_next_publish_allowed_us_ = esp_timer_get_time()" in line
        )
        methods = []
        for signature in (
            "bool OpenQuattUsageTelemetry::request_external_publish",
            "void OpenQuattUsageTelemetry::cancel_external_publish",
            "void OpenQuattUsageTelemetry::complete_publish_session_",
        ):
            start = source.index(signature)
            methods.append(source[start:source.index("\n}", start) + 2])
        harness = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "components/openquatt_performance_telemetry/OpenQuattPerformanceTelemetryPolicy.h"
using esphome::openquatt_performance_telemetry::FixedBufferWriter;
#define ESP_LOGW(...) do {} while (0)
#define ESP_LOGD(...) do {} while (0)
constexpr int pdTRUE = 1, portMAX_DELAY = 1;
int xSemaphoreTake(void*, int) { return pdTRUE; }
void xSemaphoreGive(void*) {}
int64_t now_us = 0;
int64_t esp_timer_get_time() { return now_us; }
struct Application { void wake_loop_threadsafe() {} } App;
struct Buffer {
  std::vector<char> bytes;
  bool allocate_external(size_t size) { bytes.resize(size); return true; }
  char* data() { return bytes.data(); }
  size_t size() { return bytes.size(); }
  void release() { bytes.clear(); }
};
enum class SessionKind { NONE, USAGE, EXTERNAL };
enum class ExternalPublishResult { NONE, SUCCEEDED, FAILED, CANCELLED };
struct OpenQuattUsageTelemetry {
  static constexpr size_t EXTERNAL_PAYLOAD_MAX = 4096;
  static constexpr int64_t EXTERNAL_PUBLISH_INTERVAL_US = 900000000;
  int64_t external_next_publish_allowed_us_{0};
  std::string installation_id_{"id"}, topic_{"devices"}, payload_message_id_;
  void* consent_mutex_ = this;
  Buffer external_publish_topic_, external_payload_;
  size_t external_payload_size_{0};
  std::atomic<bool> external_publish_pending_{false}, external_publish_blocked_{false};
  std::atomic<bool> session_active_{false}, publish_succeeded_{false}, publish_failed_{false};
  std::atomic<bool> finishing_session_{false}, cleanup_succeeded_{false}, enabled_{false};
  std::atomic<int> pending_message_id_{-1};
  std::atomic<SessionKind> session_kind_{SessionKind::NONE};
  std::atomic<ExternalPublishResult> external_publish_result_{ExternalPublishResult::NONE};
  uint32_t next_publish_ms_{0}, consecutive_failures_{0};
  void start_external_session() { COOLDOWN_ASSIGNMENT }
  void clear_payload_() {}
  void schedule_regular_publish_() {}
  void schedule_retry_() {}
  void commit_modbus_counter_snapshot_() {}
  bool request_external_publish(const char*, const char*, size_t);
  void cancel_external_publish();
  void complete_publish_session_();
};
'''
        cases = r'''
int main() {
  for (auto result : {ExternalPublishResult::SUCCEEDED, ExternalPublishResult::FAILED,
                     ExternalPublishResult::CANCELLED}) {
    OpenQuattUsageTelemetry transport;
    now_us = 0;
    assert(transport.request_external_publish("/performance", "{}", 2));
    // Starting the external session, not acceptance or teardown, anchors the
    // fifteen-minute gate in the production implementation.
    transport.start_external_session();
    now_us = 10000000LL;
    transport.session_kind_ = SessionKind::EXTERNAL;
    transport.cleanup_succeeded_ = result == ExternalPublishResult::SUCCEEDED;
    if (result == ExternalPublishResult::CANCELLED) transport.cancel_external_publish();
    transport.complete_publish_session_();
    assert(transport.external_publish_result_ == result);
    // A subsequent session gets a fresh start-anchored gate; teardown and
    // consent cancellation cannot bypass that gate.
    transport.cancel_external_publish();
    assert(!transport.request_external_publish("/performance", "{}", 2));
    now_us = 899999999LL;
    assert(!transport.request_external_publish("/performance", "{}", 2));
    ++now_us;
    assert(transport.request_external_publish("/performance", "{}", 2));
    transport.start_external_session();
    transport.session_kind_ = SessionKind::EXTERNAL;
    transport.cancel_external_publish();
    transport.complete_publish_session_();
    assert(!transport.request_external_publish("/performance", "{}", 2));
    now_us += 899999999LL;
    assert(!transport.request_external_publish("/performance", "{}", 2));
    ++now_us;
    assert(transport.request_external_publish("/performance", "{}", 2));
  }
  // Cancelling before session start clears the request without sending data;
  // a delayed session start at +400s anchors its own +900s deadline.
  OpenQuattUsageTelemetry transport;
  now_us = 0;
  assert(transport.request_external_publish("/performance", "{}", 2));
  transport.cancel_external_publish();
  assert(!transport.external_publish_pending_);
  assert(transport.request_external_publish("/performance", "{}", 2));
  now_us = 400000000LL;
  transport.start_external_session();
  transport.session_kind_ = SessionKind::EXTERNAL;
  transport.cancel_external_publish();
  transport.complete_publish_session_();
  now_us = 1299999999LL;
  assert(!transport.request_external_publish("/performance", "{}", 2));
  ++now_us;
  assert(transport.request_external_publish("/performance", "{}", 2));
}
'''
        self.compile_and_run(
            harness.replace("COOLDOWN_ASSIGNMENT", cooldown_assignment.replace("this->", ""))
            + "\n".join(methods)
            + cases
        )


if __name__ == "__main__":
    unittest.main()
