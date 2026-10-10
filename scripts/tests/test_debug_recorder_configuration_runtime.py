"""Execute production recorder capture/string ownership with small host storage.

Entity stubs model published states; this does not replace hardware heap/concurrency
validation. Method bodies and field layouts come from the production sources.
"""

from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "components/openquatt_debug_recorder/OpenQuattDebugRecorder.cpp").read_text()
HEADER = (ROOT / "components/openquatt_debug_recorder/OpenQuattDebugRecorder.h").read_text()


def body(signature: str) -> str:
    start = SOURCE.index(signature)
    return SOURCE[start:SOURCE.index("\n}\n", start) + 3]


PRELUDE = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>
#include "components/openquatt_debug_recorder/debug_configuration_json.h"
#define USE_NUMBER
#define USE_SWITCH
#define USE_SELECT
namespace esphome {
namespace openquatt_debug_recorder {
using StringRef = std::string;
template<typename T> struct HostBuffer {
  std::vector<T> storage;
  void allocate(size_t count) { storage.resize(count); }
  T* data() { return storage.empty() ? nullptr : storage.data(); }
  const T* data() const { return storage.empty() ? nullptr : storage.data(); }
  size_t size() const { return storage.size(); }
  explicit operator bool() const { return !storage.empty(); }
  T& operator[](size_t index) { return storage.at(index); }
};
struct Entity {
  std::string name;
  bool valid{true};
  const std::string& get_name() const { return name; }
  bool has_state() const { return valid; }
};
namespace number { struct Number : Entity { float state{0}; }; }
namespace switch_ { struct Switch : Entity { bool state{false}; }; }
namespace select {
struct Select : Entity {
  std::string option{"Home Assistant"};
  StringRef current_option() const { return option; }
};
}
struct Application {
  std::vector<number::Number*> numbers;
  std::vector<switch_::Switch*> switches;
  std::vector<select::Select*> selects;
  const auto& get_numbers() const { return numbers; }
  const auto& get_switches() const { return switches; }
  const auto& get_selects() const { return selects; }
} App;
constexpr int MALLOC_CAP_INTERNAL = 1, MALLOC_CAP_SPIRAM = 2;
uint32_t millis() { return 0; }
uint32_t heap_caps_get_free_size(int) { return 0; }
uint32_t heap_caps_get_minimum_free_size(int) { return 0; }
uint32_t heap_caps_get_largest_free_block(int) { return 0; }
'''

CLASS = r'''
class OpenQuattDebugRecorder {
 public:
  static constexpr size_t FIELD_KEY_BYTES = 40, FIELD_NAME_BYTES = 48, FIELD_UNIT_BYTES = 24;
  static constexpr size_t STRING_ENTRY_CAPACITY = 4, STRING_BUCKET_CAPACITY = 4, STRING_DATA_BYTES = 8192;
  static constexpr size_t CONFIGURATION_FIELD_CAPACITY = 16, CONFIGURATION_SCRATCH_BYTES = 1024;
  static constexpr uint32_t MISSING_VALUE = UINT32_MAX;
  static constexpr uint16_t INVALID_STRING_INDEX = UINT16_MAX;
  __LAYOUTS__
  HostBuffer<DebugField> configuration_fields_, fields_;
  HostBuffer<char> configuration_scratch_, string_data_;
  HostBuffer<StringEntry> string_entries_;
  HostBuffer<uint16_t> string_buckets_, string_compaction_order_;
  size_t configuration_field_count_{0}, field_count_{1}, string_count_{0}, string_data_used_{0};
  bool string_overflow_{false}, configuration_snapshot_overflow_{false};
  OpenQuattDebugRecorder() {
    configuration_fields_.allocate(16); configuration_scratch_.allocate(1024);
    fields_.allocate(1); string_data_.allocate(STRING_DATA_BYTES);
    string_entries_.allocate(4); string_buckets_.allocate(4); string_compaction_order_.allocate(4);
    clear_strings_();
    fields_[0].type = FieldType::CONFIGURATION_SNAPSHOT;
    fields_[0].value_size = 2;
  }
  bool configure_configuration_fields_();
  uint32_t capture_configuration_();
  uint32_t capture_value_(const DebugField&);
  void clear_strings_();
  bool compact_strings_();
  uint32_t intern_string_(const char*, size_t, bool preserve_unknown = false);
  void retain_string_(uint32_t);
  void release_sample_strings_(const uint8_t*);
  static bool string_type_(FieldType);
  static uint32_t read_value_(const uint8_t*, const DebugField&);
  std::string text(uint32_t index) {
    const auto& entry = string_entries_[index];
    return std::string(string_data_.data() + entry.offset, entry.length);
  }
};
'''

TEST = r'''
void add_entity(const char* domain, const char* name) {
  if (std::strcmp(domain, "number") == 0) { auto* e = new number::Number; e->name = name; App.numbers.push_back(e); }
  if (std::strcmp(domain, "switch") == 0) { auto* e = new switch_::Switch; e->name = name; App.switches.push_back(e); }
  if (std::strcmp(domain, "select") == 0) { auto* e = new select::Select; e->name = name; App.selects.push_back(e); }
}
int test() {
#define OQ_DEBUG_RECORDER_CONFIGURATION_FIELD(key, domain, name) add_entity(domain, name);
#include "components/openquatt_debug_recorder/debug_recorder_default_fields.h"
#undef OQ_DEBUG_RECORDER_CONFIGURATION_FIELD
  auto* enabled = static_cast<switch_::Switch*>(find_entity_in(App.switches, "Power House run extension"));
  auto* margin = static_cast<number::Number*>(find_entity_in(App.numbers, "Power House run extension stop margin"));
  auto* cooldown = static_cast<number::Number*>(find_entity_in(App.numbers, "Power House run extension restart cooldown"));
  enabled->state = true; margin->state = 0.5f; cooldown->state = 0.2f;
  OpenQuattDebugRecorder r;
  assert(r.configure_configuration_fields_());
  const uint32_t first = r.capture_value_(r.fields_[0]);
  assert(first != r.MISSING_VALUE);
  const std::string initial = r.text(first);
  assert(initial.find("\"phRunExtension\":true") != std::string::npos);
  assert(initial.find("\"phRunExtensionStopMargin\":0.5") != std::string::npos);
  assert(initial.find("\"phRunExtensionRestartCooldown\":0.2") != std::string::npos);
#if OQ_TOPOLOGY_DUO
  assert(initial.find("\"unavailable\":[]") != std::string::npos);
#else
  assert(initial.find("\"hp2ExcludeMinHz\":null") != std::string::npos);
  assert(initial.find("\"unavailable\":[\"hp2ExcludeMinHz\",\"hp2ExcludeMaxHz\"]") != std::string::npos);
#endif
  r.retain_string_(first);
  enabled->state = false;
  const uint32_t second = r.capture_configuration_();
  assert(second != first);
  r.retain_string_(second);
  const std::string changed = r.text(second);
  assert(changed.find("\"phRunExtension\":false") != std::string::npos);
  assert(r.capture_configuration_() == second);  // unchanged settings deduplicate
  assert(r.text(first) == initial);  // old sample does not change with live sources
  margin->valid = false;
  const uint32_t third = r.capture_configuration_();
  assert(r.text(third).find("\"phRunExtensionStopMargin\":null") != std::string::npos);
  r.retain_string_(third);
  margin->valid = true;
  cooldown->state = 0.3f;
  const uint32_t fourth = r.capture_configuration_();
  r.retain_string_(fourth);
  cooldown->state = 0.4f;
  assert(r.capture_configuration_() == r.MISSING_VALUE);
  assert(r.string_overflow_ && !r.configuration_snapshot_overflow_);
  // Evict a sample with the new field type, compact, then reuse its slot.
  uint16_t reference = static_cast<uint16_t>(first);
  uint8_t sample[2]; std::memcpy(sample, &reference, sizeof(reference));
  r.release_sample_strings_(sample);
  assert(r.string_entries_[first].ref_count == 0);
  assert(r.capture_configuration_() != r.MISSING_VALUE);
  assert(r.text(second) == changed);
  assert(r.text(third).find("\"phRunExtensionStopMargin\":null") != std::string::npos);
  // Scratch failure must not return a stale snapshot or intern partial JSON.
  const size_t previous_count = r.string_count_;
  r.configuration_scratch_.storage.clear();
  assert(r.capture_configuration_() == r.MISSING_VALUE);
  assert(r.configuration_snapshot_overflow_);
  assert(r.string_count_ == previous_count);
  for (auto* e : App.numbers) delete e;
  for (auto* e : App.switches) delete e;
  for (auto* e : App.selects) delete e;
  return 0;
}
} }
int main() { return esphome::openquatt_debug_recorder::test(); }
'''


class RecorderConfigurationRuntimeTest(unittest.TestCase):
    def compile_and_run(self, fixture: str, duo: int = 1):
        compiler = shutil.which("c++")
        self.assertIsNotNone(compiler, "C++ compiler required for recorder runtime regression")
        compiler_flags = ["-std=c++17", "-Wall", "-Wextra", "-Werror"]
        if sys.platform == "darwin":
            sdk = subprocess.check_output(["xcrun", "--sdk", "macosx", "--show-sdk-path"], text=True).strip()
            headers = Path(sdk) / "usr/include/c++/v1"
            if (headers / "cstdint").is_file():
                compiler_flags += ["-isystem", str(headers)]
        with tempfile.TemporaryDirectory(prefix="oq-recorder-runtime-") as directory:
            source = Path(directory) / "fixture.cpp"
            source.write_text(fixture)
            binary = Path(directory) / "test"
            result = subprocess.run([compiler, *compiler_flags,
                                     f"-DOQ_TOPOLOGY_DUO={duo}", "-I", str(ROOT),
                                     "-I", str(ROOT / "components/openquatt_debug_recorder"),
                                     str(source), "-o", str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr + result.stdout[:1000])
            return result.stdout

    def test_capture_changes_missing_pool_pressure_and_eviction_single_and_duo(self):
        layouts = "\n".join(re.search(pattern, HEADER, re.S).group(0) for pattern in [
            r"enum class FieldType.*?\n  };",
            r"struct DebugField.*?\n  };",
            r"struct StringEntry.*?\n  };",
        ])
        helpers = "\n".join(body(signature) for signature in [
            "bool copy_text(", "bool ascii_equals_ignore_case(", "bool string_is_missing(",
            "uint32_t hash_string(", "template <typename EntitiesT>\nvoid* find_entity_in(",
        ])
        methods = "\n".join(body(signature) for signature in [
            "bool OpenQuattDebugRecorder::string_type_(", "uint32_t OpenQuattDebugRecorder::read_value_(",
            "void OpenQuattDebugRecorder::clear_strings_(", "bool OpenQuattDebugRecorder::compact_strings_(",
            "uint32_t OpenQuattDebugRecorder::intern_string_(", "void OpenQuattDebugRecorder::retain_string_(",
            "void OpenQuattDebugRecorder::release_sample_strings_(", "uint32_t OpenQuattDebugRecorder::capture_value_(",
            "bool OpenQuattDebugRecorder::configure_configuration_fields_(",
            "uint32_t OpenQuattDebugRecorder::capture_configuration_(",
        ])
        fixture = PRELUDE + helpers + CLASS.replace("__LAYOUTS__", layouts) + methods + TEST
        for duo in [0, 1]:
            self.compile_and_run(fixture, duo)

    def test_http_start_capture_is_deferred_to_loop_with_stop_restart_and_lock_contention(self):
        for signature in ["void OpenQuattDebugRecorder::start_rolling_locked_(",
                          "bool OpenQuattDebugRecorder::start_rolling(",
                          "bool OpenQuattDebugRecorder::restart_rolling(",
                          "bool OpenQuattDebugRecorder::set_enabled("]:
            self.assertNotIn("capture_sample_()", body(signature))
        # Only loop() may call capture, for every rolling lifecycle entry.
        loop = body("void OpenQuattDebugRecorder::loop(")
        self.assertEqual(SOURCE.count("this->capture_sample_();"), loop.count("this->capture_sample_();"))
        fixture = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#define ESP_LOGW(...) ((void)0)
uint32_t now = 0;
uint32_t millis() { return now; }
class OpenQuattDebugRecorder {
 public:
  bool active_{false}, enabled_{true}, lock_available{true}, in_loop{false};
  uint64_t recording_id_{0}, started_monotonic_ms_{0}, stopped_monotonic_ms_{0}, last_sample_monotonic_ms_{0};
  uint32_t count_{0};
  int source_state{0}, captured_state{-1}, reads{0};
  static constexpr uint32_t SAMPLE_INTERVAL_MS = 10000;
  bool lock_state_(int = 1) { return lock_available; }
  void unlock_state_() {}
  bool available_() { return true; }
  bool activate_pending_configuration_() { return true; }
  uint64_t current_time_ms_() { return now; }
  uint64_t monotonic_ms_(uint32_t value) { return value; }
  void track_millis_(uint32_t) {}
  void clear_() { count_ = 0; last_sample_monotonic_ms_ = 0; }
  void capture_sample_() {
    assert(in_loop); captured_state = source_state; reads++; count_++; last_sample_monotonic_ms_ = now;
  }
  bool start_rolling();
  void start_rolling_locked_();
  void loop();
  void stop();
  void tick() { in_loop = true; loop(); in_loop = false; }
};
'''
        fixture += "\n".join(body(signature) for signature in [
            "bool OpenQuattDebugRecorder::start_rolling(", "void OpenQuattDebugRecorder::start_rolling_locked_(",
            "void OpenQuattDebugRecorder::loop(", "void OpenQuattDebugRecorder::stop(",
        ])
        fixture += r'''
int main() {
  OpenQuattDebugRecorder r;
  r.start_rolling_locked_(); assert(r.reads == 0);
  r.source_state = 1; r.tick(); assert(r.reads == 1 && r.captured_state == 1);
  r.tick(); assert(r.reads == 1);  // uptime zero does not repeat the first sample
  now = 10000; r.tick(); assert(r.reads == 2);
  r.start_rolling_locked_(); r.stop(); r.tick(); assert(r.reads == 2 && r.count_ == 0);
  r.start_rolling_locked_(); r.lock_available = false; r.tick(); assert(r.reads == 2);
  r.source_state = 2; r.lock_available = true; r.tick(); assert(r.reads == 3 && r.captured_state == 2);
  r.stop(); assert(r.start_rolling()); assert(r.reads == 3);
  r.source_state = 3; r.tick(); assert(r.reads == 4 && r.captured_state == 3);
  // Rolling continues well beyond the removed one-hour timer.
  for (now = 20000; now <= 8 * 3600 * 1000; now += 10000) r.tick();
  assert(r.active_ && r.reads > 2800);
  r.stop(); const int before = r.reads;
  r.enabled_ = false; assert(!r.start_rolling()); r.tick(); assert(r.reads == before);

}
'''
        self.compile_and_run(fixture)


if __name__ == "__main__":
    unittest.main()
