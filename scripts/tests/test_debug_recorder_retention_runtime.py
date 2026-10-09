"""Exercise production ring ownership, range snapshot and HTTP JSON on the host.

PSRAM/HTTP stubs inject failures and interleavings; hardware heap margins and
real task timing still require HIL. Rows use the full default schema's stride.
"""

import json
import re
import unittest

import test_debug_recorder_configuration_runtime as runtime

HEADER, SOURCE, body = runtime.HEADER, runtime.SOURCE, runtime.body


PRELUDE = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
#define USE_SWITCH
#define USE_TEXT_SENSOR
#define ESP_LOGW(...) ((void)0)
bool locked = false, fail_allocation = false;
std::function<void()> allocation_hook, chunk_hook;
template<typename T> struct PsramBuffer {
  std::vector<T> storage;
  bool allocate_external(size_t count) {
    if (allocation_hook) allocation_hook();
    if (fail_allocation) { storage.clear(); return false; }
    storage.resize(count); return true;
  }
  T* data() { return storage.empty() ? nullptr : storage.data(); }
  const T* data() const { return storage.empty() ? nullptr : storage.data(); }
  size_t size() const { return storage.size(); }
  explicit operator bool() const { return !storage.empty(); }
  T& operator[](size_t index) { return storage.at(index); }
  const T& operator[](size_t index) const { return storage.at(index); }
};
struct httpd_req_t {
  std::string status, response;
  int chunks{0}, fail_chunk{-1};
};
constexpr int ESP_OK = 0;
constexpr const char* HTTPD_200 = "200 OK";
void httpd_resp_set_status(httpd_req_t* req, const char* text) { req->status = text; }
void httpd_resp_set_type(httpd_req_t*, const char*) {}
void httpd_resp_set_hdr(httpd_req_t*, const char*, const char*) {}
void httpd_resp_sendstr(httpd_req_t* req, const char* text) { req->response = text; }
int httpd_resp_send_chunk(httpd_req_t* req, const char* data, ssize_t size) {
  if (chunk_hook) { auto hook = std::move(chunk_hook); chunk_hook = {}; hook(); }
  if (req->fail_chunk >= 0 && req->chunks++ >= req->fail_chunk) return -1;
  if (data) req->response.append(data, static_cast<size_t>(size));
  return ESP_OK;
}
uint32_t now_ms = 0;
uint32_t millis() { return now_ms; }
constexpr int MALLOC_CAP_INTERNAL = 1, MALLOC_CAP_SPIRAM = 2;
uint32_t heap_caps_get_free_size(int) { return 0; }
uint32_t heap_caps_get_minimum_free_size(int) { return 0; }
uint32_t heap_caps_get_largest_free_block(int) { return 0; }
namespace text_sensor {
struct TextSensor { std::string state; bool valid{true}; bool has_state() const { return valid; } };
}
namespace switch_ { struct Switch { bool state{false}; }; }
'''

CLASS = r'''
class OpenQuattDebugRecorder {
 public:
  __CONSTANTS__
  __LAYOUTS__
  PsramBuffer<uint8_t> samples_;
  PsramBuffer<DebugField> fields_;
  PsramBuffer<StringEntry> string_entries_;
  PsramBuffer<uint16_t> string_buckets_, string_compaction_order_;
  PsramBuffer<char> string_data_;
  bool active_{true}, string_overflow_{false}, configuration_snapshot_overflow_{false};
  mutable bool export_in_progress_{false};
  uint64_t recording_id_{1}, started_monotonic_ms_{0}, last_sample_monotonic_ms_{0};
  uint32_t total_change_count_{0}, total_event_count_{0};
  size_t count_{0}, write_index_{0}, field_count_{4}, missing_field_count_{0};
  size_t sample_stride_{795}, sample_capacity_{BUFFER_BYTES / 795};
  size_t string_count_{0}, string_data_used_{0};
  text_sensor::TextSensor clock, diagnostic;
  switch_::Switch state;
  std::string configuration{"{\"v\":1,\"values\":{\"phRunExtension\":true}}"};
  OpenQuattDebugRecorder() {
    samples_.allocate_external(BUFFER_BYTES); fields_.allocate_external(field_count_);
    string_entries_.allocate_external(STRING_ENTRY_CAPACITY);
    string_buckets_.allocate_external(STRING_BUCKET_CAPACITY);
    string_compaction_order_.allocate_external(STRING_ENTRY_CAPACITY);
    string_data_.allocate_external(STRING_DATA_BYTES);
    const FieldType types[] = {FieldType::TIME_HHMM, FieldType::TEXT_SENSOR,
                               FieldType::SWITCH, FieldType::CONFIGURATION_SNAPSHOT};
    const char* keys[] = {"timeNowHhmm", "lowLoadDynamicThresholds", "flag", "configurationSnapshot"};
    size_t offset = SAMPLE_HEADER_BYTES;
    for (size_t i = 0; i < field_count_; ++i) {
      auto& f = fields_[i]; std::strcpy(f.key, keys[i]); f.type = types[i];
      f.value_offset = offset; f.value_size = value_size_for_type_(f.type); offset += f.value_size;
    }
    fields_[0].source = &clock; fields_[1].source = &diagnostic; fields_[2].source = &state;
    clear_();
  }
  bool available_() const { return true; }
  bool lock_state_() const { assert(!locked); locked = true; return true; }
  void unlock_state_() const { assert(locked); locked = false; }
  uint64_t monotonic_ms_(uint32_t value) const { return value; }
  uint64_t current_time_ms_() const { return 100000000ULL + now_ms; }
  uint64_t started_time_ms_() const { return 100000000ULL + started_monotonic_ms_; }
  uint64_t ended_time_ms_() const { return current_time_ms_(); }
  uint32_t retention_capacity_s_() const { return (sample_capacity_ - 1) * 10; }
  uint32_t capture_configuration_() { return intern_string_(configuration.data(), configuration.size()); }
  bool begin_export_() const;
  void end_export_() const;
  void clear_();
  void clear_strings_();
  bool compact_strings_();
  uint32_t intern_string_(const char*, size_t, bool preserve_unknown = false);
  void retain_string_(uint32_t);
  void release_sample_strings_(const uint8_t*);
  uint32_t capture_value_(const DebugField&);
  void capture_sample_();
  static uint8_t value_size_for_type_(FieldType);
  static bool string_type_(FieldType);
  static bool event_type_(FieldType);
  static uint32_t read_value_(const uint8_t*, const DebugField&);
  static void write_value_(uint8_t*, const DebugField&, uint32_t);
  static uint32_t sample_offset_(const uint8_t*);
  uint32_t sample_offset_s_(uint64_t) const;
  static uint16_t sample_change_count_(const uint8_t*);
  static uint16_t sample_event_count_(const uint8_t*);
  static void write_sample_header_(uint8_t*, uint32_t, uint16_t, uint16_t);
  uint8_t* writable_sample_at_(size_t);
  const uint8_t* sample_at_(size_t) const;
  bool capture_snapshot_(RecordingSnapshot*, uint32_t) const;
  void write_recording(httpd_req_t*, uint32_t) const;
  void write_recording_export_(httpd_req_t*, uint32_t) const;
  void tick(uint32_t seconds) {
    now_ms = seconds * 1000;
    clock.state = "12:34";
    diagnostic.state = "pmin=" + std::to_string(seconds) + "W off=1000W on=2000W (cached)";
    state.state = !state.state;
    capture_sample_();
  }
  void restart() { recording_id_++; started_monotonic_ms_ = now_ms; clear_(); tick(now_ms / 1000); }
};
'''

TEST = r'''
int main() {
  OpenQuattDebugRecorder r;
  assert(r.sample_capacity_ == 2637 && r.retention_capacity_s_() == 26360);
  // Exercise repeated eviction, string compaction/reuse and changing settings.
  for (uint32_t seconds = 0; seconds <= 16 * 3600; seconds += 10) {
    if (seconds == 15 * 3600) r.configuration = "{\"v\":1,\"values\":{\"phRunExtension\":false}}";
    r.tick(seconds);
  }
  assert(r.active_ && !r.string_overflow_ && r.count_ == r.sample_capacity_);
  assert(r.string_count_ < r.STRING_ENTRY_CAPACITY);
  OpenQuattDebugRecorder::RecordingSnapshot snapshot;
  assert(r.capture_snapshot_(&snapshot, 15));
  assert(snapshot.count == 91 && snapshot.samples.size() == 91 * r.sample_stride_);
  assert(snapshot.event_count == 90 && snapshot.string_data.size() == snapshot.string_data_used);
  const auto initial_offset = r.sample_offset_(snapshot.sample_at(0));
  r.tick(16 * 3600 + 10);
  assert(r.sample_offset_(snapshot.sample_at(0)) == initial_offset); // immutable copy
  for (uint32_t minutes : {0U, 15U, 120U, 360U, UINT32_MAX}) {
    httpd_req_t req; r.write_recording(&req, minutes);
    assert(req.status == HTTPD_200 && !r.export_in_progress_);
    std::cout << minutes << '\t' << req.response << '\n';
  }
  // A restart/eviction between plan and allocation forces a fresh plan.
  unsigned hooks = 0;
  allocation_hook = [&]() { assert(!locked); if (hooks++ == 0) r.restart(); };
  OpenQuattDebugRecorder::RecordingSnapshot restarted;
  assert(r.capture_snapshot_(&restarted, 360));
  allocation_hook = {};
  assert(restarted.recording_id == r.recording_id_ && restarted.count == 1);
  for (uint32_t seconds = 16 * 3600 + 20; seconds < 17 * 3600; seconds += 10) r.tick(seconds);
  hooks = 0;
  allocation_hook = [&]() { assert(!locked); if (hooks++ == 0) r.tick(now_ms / 1000 + 10); };
  OpenQuattDebugRecorder::RecordingSnapshot appended;
  assert(r.capture_snapshot_(&appended, 15));
  allocation_hook = {};
  assert(appended.count == 91 && r.sample_offset_(appended.sample_at(90)) == r.sample_offset_(r.sample_at_(r.count_ - 1)));
  // Continuous interference stops after three plans; no partial snapshot.
  hooks = 0;
  allocation_hook = [&]() { assert(!locked); hooks++; r.tick(now_ms / 1000 + 10); };
  OpenQuattDebugRecorder::RecordingSnapshot racing;
  assert(!r.capture_snapshot_(&racing, 0));
  allocation_hook = {};
  assert(!locked && hooks < 20);
  fail_allocation = true;
  httpd_req_t failed; r.write_recording(&failed, 0);
  assert(failed.status == "503 Service Unavailable" && !r.export_in_progress_);
  fail_allocation = false;
  for (uint32_t seconds = now_ms / 1000 + 10; seconds < 18 * 3600; seconds += 10) r.tick(seconds);
  // Source restart and a second export during HTTP streaming cannot change
  // the immutable snapshot or acquire the single-export guard.
  chunk_hook = [&]() {
    assert(!locked); r.restart(); httpd_req_t duplicate; r.write_recording(&duplicate, 0);
    assert(duplicate.status == "503 Service Unavailable");
  };
  httpd_req_t immutable; r.write_recording(&immutable, 15);
  assert(immutable.status == HTTPD_200 && !r.export_in_progress_);
  std::cout << "immutable\t" << immutable.response << '\n';
  for (unsigned i = 0; i < 100; ++i) r.tick(now_ms / 1000 + 10);
  httpd_req_t aborted; aborted.fail_chunk = 1; r.write_recording(&aborted, 0);
  assert(!r.export_in_progress_);
  std::cout << "aborted\t" << aborted.response << '\n';
  r.clear_();
  httpd_req_t empty; r.write_recording(&empty, 360);
  assert(empty.status == HTTPD_200 && !r.export_in_progress_);
  std::cout << "empty\t" << empty.response << '\n';
  // Range boundaries follow timestamps even when scheduling has jitter.
  r.started_monotonic_ms_ = 0; r.clear_();
  for (uint32_t seconds : {1000U, 1041U, 1059U, 1060U, 1085U, 1117U, 1120U}) r.tick(seconds);
  httpd_req_t jitter; r.write_recording(&jitter, 1);
  std::cout << "jitter\t" << jitter.response << '\n';
  // Missing/invalid local time is null; HH:MM never consumes string slots.
  const auto previous_strings = r.string_count_;
  for (const char* text : {"00:00", "23:59", "12:34"}) {
    r.clock.state = text; assert(r.capture_value_(r.fields_[0]) != r.MISSING_VALUE);
  }
  r.clock.state = "23:59"; assert(r.capture_value_(r.fields_[0]) == 1439);
  for (const char* text : {"24:00", "12:60", "9:00", "unknown", "12:3a", "12-34"}) {
    r.clock.state = text; assert(r.capture_value_(r.fields_[0]) == r.MISSING_VALUE);
  }
  r.clock.valid = false; assert(r.capture_value_(r.fields_[0]) == r.MISSING_VALUE);
  assert(r.string_count_ == previous_strings && !locked);
  // Export formatting keeps five characters through midnight/end-of-day;
  // invalid encoded minutes remain null rather than wrapping to a valid time.
  for (uint32_t minutes : {0U, 545U, 754U, 1439U, 1440U, 65534U}) {
    r.clear_(); r.clock.valid = true; r.tick(2000);
    r.write_value_(r.writable_sample_at_(0), r.fields_[0], minutes);
    httpd_req_t req; r.write_recording(&req, 0);
    assert(req.status == HTTPD_200 && !r.export_in_progress_);
    std::cout << "clock-" << minutes << '\t' << req.response << '\n';
  }
}
'''


class RecorderRetentionRuntimeTest(unittest.TestCase):
    def test_long_history_range_export_and_failure_boundaries(self):
        constants = HEADER[HEADER.index("  static constexpr uint32_t SAMPLE_INTERVAL_MS"):HEADER.index("  enum class FieldType")]
        layouts = "\n".join(re.search(pattern, HEADER, re.S).group(0) for pattern in [
            r"enum class FieldType.*?\n  };", r"struct DebugField.*?\n  };",
            r"struct StringEntry.*?\n  };", r"struct RecordingSnapshot.*?\n  };",
        ])
        writer = SOURCE[SOURCE.index("class ChunkedJsonWriter {"):SOURCE.index("class OpenQuattDebugRecorderRequestHandler")]
        helpers = "\n".join(body(signature) for signature in [
            "bool ascii_equals_ignore_case(", "bool string_is_missing(", "uint32_t hash_string(",
        ])
        methods = "\n".join(body(signature) for signature in [
            "bool OpenQuattDebugRecorder::RecordingSnapshot::allocate(",
            "const uint8_t* OpenQuattDebugRecorder::RecordingSnapshot::sample_at(",
            "const OpenQuattDebugRecorder::StringEntry* OpenQuattDebugRecorder::RecordingSnapshot::string_at(",
            "bool OpenQuattDebugRecorder::begin_export_(", "void OpenQuattDebugRecorder::end_export_(",
            "uint8_t OpenQuattDebugRecorder::value_size_for_type_(", "bool OpenQuattDebugRecorder::string_type_(",
            "bool OpenQuattDebugRecorder::event_type_(", "uint32_t OpenQuattDebugRecorder::read_value_(",
            "void OpenQuattDebugRecorder::write_value_(", "uint32_t OpenQuattDebugRecorder::sample_offset_(",
            "uint32_t OpenQuattDebugRecorder::sample_offset_s_(", "uint16_t OpenQuattDebugRecorder::sample_change_count_(",
            "uint16_t OpenQuattDebugRecorder::sample_event_count_(", "void OpenQuattDebugRecorder::write_sample_header_(",
            "uint8_t* OpenQuattDebugRecorder::writable_sample_at_(", "const uint8_t* OpenQuattDebugRecorder::sample_at_(",
            "void OpenQuattDebugRecorder::clear_(", "void OpenQuattDebugRecorder::clear_strings_(",
            "bool OpenQuattDebugRecorder::compact_strings_(", "uint32_t OpenQuattDebugRecorder::intern_string_(",
            "void OpenQuattDebugRecorder::retain_string_(", "void OpenQuattDebugRecorder::release_sample_strings_(",
            "uint32_t OpenQuattDebugRecorder::capture_value_(", "void OpenQuattDebugRecorder::capture_sample_(",
            "bool OpenQuattDebugRecorder::capture_snapshot_(", "void OpenQuattDebugRecorder::write_recording_export_(",
            "void OpenQuattDebugRecorder::write_recording(",
        ])
        fixture = PRELUDE + helpers + writer + CLASS.replace("__CONSTANTS__", constants).replace("__LAYOUTS__", layouts) + methods + TEST
        output = runtime.RecorderConfigurationRuntimeTest().compile_and_run(fixture)
        exports = dict(line.split("\t", 1) for line in output.splitlines())
        for minutes, count, span in [(0, 2637, 26360), (15, 91, 900), (120, 721, 7200), (360, 2161, 21600), (4294967295, 2637, 26360)]:
            payload = json.loads(exports[str(minutes)])
            self.assertEqual(payload["recording"]["sample_count"], count)
            self.assertEqual(payload["recording"]["duration_s"], span)
            self.assertEqual(payload["samples"][0], [0, []])
            self.assertEqual(payload["samples"][-1][0], span)
            self.assertEqual(payload["recording"]["event_count"], count - 1)
            self.assertEqual(payload["recording"]["mode"], "rolling")
            initial = dict(payload["initial"])
            self.assertEqual(initial[0], "12:34")
            self.assertEqual(json.loads(initial[3])["values"]["phRunExtension"], minutes != 15)
        jitter = json.loads(exports["jitter"])
        self.assertEqual([sample[0] for sample in jitter["samples"]], [0, 25, 57, 60])
        self.assertEqual(jitter["recording"]["event_count"], 3)
        self.assertEqual(json.loads(exports["immutable"])["recording"]["sample_count"], 91)
        with self.assertRaises(json.JSONDecodeError):
            json.loads(exports["aborted"])
        empty = json.loads(exports["empty"])
        self.assertEqual(empty["initial"], [])
        self.assertEqual(empty["samples"], [])
        self.assertEqual(empty["recording"]["duration_s"], 0)
        for minutes, expected in [(0, "00:00"), (545, "09:05"), (754, "12:34"),
                                  (1439, "23:59"), (1440, None), (65534, None)]:
            initial = dict(json.loads(exports[f"clock-{minutes}"])["initial"])
            self.assertEqual(initial[0], expected)


if __name__ == "__main__":
    unittest.main()
