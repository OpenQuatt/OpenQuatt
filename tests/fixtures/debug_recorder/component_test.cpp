#include "components/openquatt_debug_recorder/OpenQuattDebugRecorder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <thread>

using namespace esphome;
using namespace esphome::openquatt_debug_recorder;

namespace {
class Recorder : public OpenQuattDebugRecorder {
 public:
  using OpenQuattDebugRecorder::available_;
  using OpenQuattDebugRecorder::BUFFER_BYTES;
  using OpenQuattDebugRecorder::capture_configuration_;
  using OpenQuattDebugRecorder::capture_snapshot_;
  using OpenQuattDebugRecorder::clear_;
  using OpenQuattDebugRecorder::configuration_snapshot_overflow_;
  using OpenQuattDebugRecorder::count_;
  using OpenQuattDebugRecorder::export_in_progress_;
  using OpenQuattDebugRecorder::field_count_;
  using OpenQuattDebugRecorder::fields_;
  using OpenQuattDebugRecorder::last_sample_monotonic_ms_;
  using OpenQuattDebugRecorder::lock_state_;
  using OpenQuattDebugRecorder::MISSING_VALUE;
  using OpenQuattDebugRecorder::recording_id_;
  using OpenQuattDebugRecorder::RecordingSnapshot;
  using OpenQuattDebugRecorder::retention_capacity_s_;
  using OpenQuattDebugRecorder::sample_at_;
  using OpenQuattDebugRecorder::sample_capacity_;
  using OpenQuattDebugRecorder::sample_offset_;
  using OpenQuattDebugRecorder::sample_stride_;
  using OpenQuattDebugRecorder::string_count_;
  using OpenQuattDebugRecorder::STRING_ENTRY_CAPACITY;
  using OpenQuattDebugRecorder::string_overflow_;
  using OpenQuattDebugRecorder::unlock_state_;

  bool active() const { return active_; }
  bool buffers_empty() const {
    return !samples_ && !fields_ && !pending_fields_ && !string_entries_ && !string_buckets_ &&
           !string_compaction_order_ && !string_data_ && !configuration_fields_ && !configuration_scratch_;
  }
  size_t field_index(const char* key) const {
    for (size_t i = 0; i < field_count_; ++i)
      if (std::strcmp(fields_[i].key, key) == 0) return i;
    assert(false && "expected field missing from real default schema");
    return 0;
  }
  uint32_t capture(const char* key) { return capture_value_(fields_[field_index(key)]); }
  uint32_t sample_value(size_t row, const char* key) const {
    return read_value_(sample_at_(row), fields_[field_index(key)]);
  }
  std::string text(uint32_t index) const {
    assert(index < STRING_ENTRY_CAPACITY);
    const auto& entry = string_entries_[index];
    return {string_data_.data() + entry.offset, entry.length};
  }
  void retain(uint32_t index) { retain_string_(index); }
  void evict_configuration_reference(uint32_t index) {
    std::vector<uint8_t> sample(sample_stride_, 0xff);
    write_value_(sample.data(), fields_[field_index("configurationSnapshot")], index);
    release_sample_strings_(sample.data());
    assert(string_entries_[index].ref_count == 0);
  }
  void release_configuration_scratch() { configuration_scratch_.release(); }
  void overwrite_clock(uint32_t value) {
    write_value_(writable_sample_at_(0), fields_[field_index("timeNowHhmm")], value);
  }
};

struct Environment {
  web_server_base::WebServerBase web;
  ESPPreferences preferences;
  std::vector<std::unique_ptr<Entity>> entities;

  Environment() {
    recorder_test::now_ms = 0;
    recorder_test::allow_entity_reads = false;
    recorder_test::entity_reads = 0;
    recorder_test::held_mutexes = 0;
    recorder_test::fail_mutex_creation = false;
    recorder_test::reset_allocation_faults();
    global_preferences = &preferences;
    web_server_base::global_web_server_base = &web;
#define OQ_DEBUG_RECORDER_DEFAULT_FIELD(key, domain, name) add(domain, name);
#define OQ_DEBUG_RECORDER_CONFIGURATION_FIELD(key, domain, name) add(domain, name);
#include "components/openquatt_debug_recorder/debug_recorder_default_fields.h"
#undef OQ_DEBUG_RECORDER_CONFIGURATION_FIELD
#undef OQ_DEBUG_RECORDER_DEFAULT_FIELD
    clock().state = "12:34";
    margin().state = 0.5f;
    cooldown().state = 0.2f;
    extension().state = true;
  }
  ~Environment() {
    assert(recorder_test::held_mutexes == 0);
    web_server_base::global_web_server_base = nullptr;
    global_preferences = nullptr;
    App = Application{};
    recorder_test::allocation_hook = recorder_test::chunk_hook = {};
  }
  template <typename T>
  void add_to(std::vector<T*>& registry, const char* name) {
    for (auto* entity : registry)
      if (entity->name == name) return;
    auto entity = std::make_unique<T>();
    entity->name = name;
    registry.push_back(entity.get());
    entities.emplace_back(std::move(entity));
  }
  void add(const char* domain, const char* name) {
    if (std::strcmp(domain, "number") == 0)
      add_to(App.numbers, name);
    else if (std::strcmp(domain, "sensor") == 0)
      add_to(App.sensors, name);
    else if (std::strcmp(domain, "binary_sensor") == 0)
      add_to(App.binary_sensors, name);
    else if (std::strcmp(domain, "switch") == 0)
      add_to(App.switches, name);
    else if (std::strcmp(domain, "text_sensor") == 0)
      add_to(App.text_sensors, name);
    else if (std::strcmp(domain, "select") == 0)
      add_to(App.selects, name);
    else
      assert(false && "unknown generated entity domain");
  }
  template <typename T>
  T& find(const std::vector<T*>& registry, const char* name) {
    for (auto* entity : registry)
      if (entity->name == name) return *entity;
    assert(false && "generated entity missing");
    std::abort();
  }
  text_sensor::TextSensor& clock() { return find(App.text_sensors, "Time now (HH:MM)"); }
  text_sensor::TextSensor& diagnostic() { return find(App.text_sensors, "Low-load dynamic thresholds"); }
  switch_::Switch& flag() { return find(App.switches, "OpenQuatt Enabled"); }
  switch_::Switch& extension() { return find(App.switches, "Power House run extension"); }
  number::Number& margin() { return find(App.numbers, "Power House run extension stop margin"); }
  number::Number& cooldown() { return find(App.numbers, "Power House run extension restart cooldown"); }
  void tick(Recorder& recorder, uint32_t seconds) {
    recorder_test::now_ms = seconds * 1000;
    clock().state = "12:34";
    diagnostic().state = "pmin=" + std::to_string(seconds) + "W off=1000W on=2000W (cached)";
    flag().state = !flag().state;
    recorder_test::allow_entity_reads = true;
    recorder.loop();
    recorder_test::allow_entity_reads = false;
  }
  void restart(Recorder& recorder) {
    assert(recorder.restart_rolling());
    assert(recorder.count_ == 0);
    tick(recorder, recorder_test::now_ms / 1000);
  }
  AsyncWebServerRequest request(const char* path, int method = HTTP_POST) {
    AsyncWebServerRequest request;
    request.path = std::string("/openquatt/debug-recording/") + path;
    request.verb = method;
    request.headers["Host"] = "openquatt.local";
    request.headers["Origin"] = "http://openquatt.local";
    return request;
  }
  void dispatch(AsyncWebServerRequest& request) {
    assert(web.handlers.size() == 1);
    assert(web.handlers[0]->canHandle(&request));
    web.handlers[0]->handleRequest(&request);
  }
};

void emit(const std::string& label, const httpd_req_t& request) {
  std::cout << label << '\t' << request.response << '\n';
}
void emit_status(const char* label, const Recorder& recorder) {
  httpd_req_t request;
  recorder.write_status(&request);
  assert(request.status == HTTPD_200 && request.finished);
  emit(label, request);
}

void setup_boundaries() {
  for (size_t failed = 1; failed <= 9; ++failed) {
    Environment env;
    Recorder recorder;
    recorder_test::fail_allocation_at = failed;
    recorder.setup();
    assert(recorder_test::allocation_calls == failed);
    assert(recorder_test::internal_allocations == 0);
    assert(recorder.buffers_empty() && !recorder.available_());
    assert(!recorder.is_failed());  // Recorder storage failure must not stop the controller.
    assert(env.web.handlers.size() == 1 && !recorder.get_csrf_token().empty());
    assert(!recorder.restart_rolling() && !recorder.start_rolling());
    recorder.loop();
    recorder_test::reset_allocation_faults();
    emit_status(("allocation-" + std::to_string(failed)).c_str(), recorder);
    httpd_req_t download;
    recorder.write_recording(&download, 0);
    assert(download.status == "503 Service Unavailable" && !recorder.export_in_progress_);
  }
  for (bool missing_web : {false, true}) {
    Environment env;
    Recorder recorder;
    if (missing_web)
      web_server_base::global_web_server_base = nullptr;
    else
      recorder_test::fail_mutex_creation = true;
    recorder.setup();
    assert(recorder.is_failed() && recorder.buffers_empty() && !recorder.available_());
    assert(recorder_test::allocation_calls == 0 && env.web.handlers.empty());
    assert(!recorder.start_rolling() && !recorder.restart_rolling());
    recorder.loop();
    httpd_req_t status;
    recorder.write_status(&status);
    assert(status.status == "503 Service Unavailable");
  }
  Environment env;
  Recorder recorder;
  recorder.setup();
  assert(recorder.available_() && !recorder.is_failed() && recorder.count_ == 0);
  assert(recorder_test::allocation_calls == 9 && recorder_test::internal_allocations == 0);
  assert(env.preferences.saves == 0 && env.preferences.syncs == 0);
  assert(recorder.sample_capacity_ == Recorder::BUFFER_BYTES / recorder.sample_stride_);
  assert(recorder.retention_capacity_s_() >= 6 * 3600);
  emit_status("ready", recorder);
}

void lifecycle() {
  Environment env;
  Recorder recorder;
  recorder.setup();
  assert(recorder_test::entity_reads == 0 && recorder.count_ == 0);
  env.tick(recorder, 0);
  assert(recorder.count_ == 1 && recorder_test::entity_reads > 0);
  const auto reads = recorder_test::entity_reads;
  recorder.loop();
  assert(recorder.count_ == 1 && recorder_test::entity_reads == reads);
  env.tick(recorder, 10);
  assert(recorder.count_ == 2);
  assert(recorder.restart_rolling() && recorder.count_ == 0);
  recorder.stop();
  env.tick(recorder, 20);
  assert(recorder.count_ == 0);
  assert(recorder.restart_rolling());
  assert(recorder.lock_state_());
  const auto before = recorder_test::entity_reads;
  std::thread contending([&]() { recorder.loop(); });
  contending.join();
  assert(recorder.count_ == 0 && recorder_test::entity_reads == before);
  recorder.unlock_state_();
  env.tick(recorder, 20);
  assert(recorder.count_ == 1);
  recorder.stop();
  assert(recorder.configure("openquattEnabled\tswitch\tOpenQuatt Enabled", true));
  assert(recorder.start_rolling() && recorder.count_ == 0);
  env.tick(recorder, 30);
  assert(recorder.count_ == 1 && recorder.sample_value(0, "openquattEnabled") == env.flag().state);
  assert(recorder.restart_rolling() && recorder.count_ == 0);
  const auto id = recorder.recording_id_;
  env.preferences.fail_save = true;
  assert(!recorder.set_enabled(false) && recorder.enabled() && recorder.recording_id_ == id);
  env.preferences.fail_save = false;
  env.preferences.fail_sync = true;
  assert(!recorder.set_enabled(false) && recorder.enabled() && recorder.recording_id_ == id);
  env.preferences.fail_sync = false;
  auto failed_sync_reboot = env.preferences.cold_boot();
  global_preferences = &failed_sync_reboot;
  Recorder after_failed_sync;
  after_failed_sync.setup();
  assert(after_failed_sync.enabled());
  global_preferences = &env.preferences;
  assert(recorder.set_enabled(false) && !recorder.enabled());
  env.tick(recorder, 40);
  assert(recorder.count_ == 0 && !recorder.start_rolling() && !recorder.restart_rolling());
  const auto saves = env.preferences.saves;
  assert(recorder.set_enabled(false) && env.preferences.saves == saves);
  auto reboot_preferences = env.preferences.cold_boot();
  global_preferences = &reboot_preferences;
  Recorder reboot;
  reboot.setup();
  assert(!reboot.enabled() && reboot.count_ == 0);
  assert(reboot.set_enabled(true) && reboot.enabled() && reboot.count_ == 0);
  env.tick(reboot, 50);
  assert(reboot.count_ == 1);
  assert(reboot.set_enabled(true) && reboot.count_ == 1);
  for (uint32_t seconds = 60; seconds <= 8 * 3600; seconds += 10) env.tick(reboot, seconds);
  emit_status("lifecycle", reboot);
  // The production wrap tracker, including disabled periods, is exercised.
  assert(reboot.set_enabled(false));
  recorder_test::now_ms = UINT32_MAX - 100;
  reboot.loop();
  recorder_test::now_ms = 10;
  reboot.loop();
  assert(reboot.set_enabled(true));
  env.tick(reboot, 1);
  assert(reboot.last_sample_monotonic_ms_ > UINT32_MAX);
  global_preferences = &env.preferences;
}

void sensor_sources() {
  Environment env;
  Recorder recorder;
  auto& source = env.find(App.selects, "Water Supply Source");
  auto& local = env.find(App.selects, "Local Water Supply Temp Source");
  auto& effective = env.find(App.text_sensors, "Water Supply Temp Effective Source");
  auto& fallback = env.find(App.binary_sensors, "Water Supply Temp Fallback Active");
  auto& fault = env.find(App.binary_sensors, "PT1000 read problem");
  auto& pt1000 = env.find(App.sensors, "Water Supply Temp (PT1000)");
  auto& proxy = env.find(App.sensors, "Water Supply Temp");
  auto& cic = env.find(App.sensors, "CIC - Water Supply Temp");
  auto& selected = env.find(App.sensors, "Water Supply Temp (Selected)");
  const char* fallback_label = OQ_TOPOLOGY_DUO ? "hp2 water out (fallback)" : "hp1 water out (fallback)";
  source.option = "CIC";
  local.option = "PT1000";
  effective.state = fallback_label;
  fallback.state = true;
  fault.state = false;
  pt1000.state = proxy.state = 21.9f;
  cic.valid = false;
  selected.state = 21.6f;
  recorder.setup();
  env.tick(recorder, 0);

  source.option = "Local";
  effective.state = "Local - PT1000";
  fallback.state = false;
  selected.state = pt1000.state;
  env.tick(recorder, 10);

  pt1000.valid = proxy.valid = false;
  fault.state = fallback.state = true;
  effective.state = fallback_label;
  selected.state = 21.6f;
  env.tick(recorder, 20);

  pt1000.valid = proxy.valid = true;
  pt1000.state = proxy.state = selected.state = 24.3f;
  fault.state = fallback.state = false;
  effective.state = "Local - PT1000";
  env.tick(recorder, 30);
  httpd_req_t download;
  recorder.write_recording(&download, 0);
  assert(download.status == HTTPD_200 && download.finished);
  emit("sources", download);
}

void configuration() {
  Environment env;
  Recorder recorder;
  recorder.setup();
  recorder_test::allow_entity_reads = true;
  const uint32_t first = recorder.capture("configurationSnapshot");
  assert(first != Recorder::MISSING_VALUE);
  const auto initial = recorder.text(first);
  assert(initial.find("\"phRunExtension\":true") != std::string::npos);
  assert(initial.find("\"phRunExtensionStopMargin\":0.5") != std::string::npos);
  assert(initial.find("\"phRunExtensionRestartCooldown\":0.2") != std::string::npos);
#if OQ_TOPOLOGY_DUO
  assert(initial.find("\"unavailable\":[]") != std::string::npos);
#else
  assert(initial.find("\"hp2ExcludeMinHz\":null") != std::string::npos);
  assert(initial.find("\"unavailable\":[\"hp2ExcludeMinHz\",\"hp2ExcludeMaxHz\"]") != std::string::npos);
#endif
  recorder.retain(first);
  env.extension().state = false;
  const uint32_t second = recorder.capture_configuration_();
  assert(second != first && second != Recorder::MISSING_VALUE);
  recorder.retain(second);
  const auto changed = recorder.text(second);
  assert(changed.find("\"phRunExtension\":false") != std::string::npos);
  assert(recorder.capture_configuration_() == second && recorder.text(first) == initial);
  env.margin().valid = false;
  const uint32_t third = recorder.capture_configuration_();
  recorder.retain(third);
  assert(recorder.text(third).find("\"phRunExtensionStopMargin\":null") != std::string::npos);
  env.margin().valid = true;
  std::vector<uint32_t> references;
  for (size_t i = 0; i <= Recorder::STRING_ENTRY_CAPACITY; ++i) {
    env.cooldown().state = static_cast<float>(10000 + i);
    const auto next = recorder.capture_configuration_();
    if (next == Recorder::MISSING_VALUE) break;
    recorder.retain(next);
    references.push_back(next);
  }
  assert(references.size() > 4 && references.size() < Recorder::STRING_ENTRY_CAPACITY);
  assert(recorder.string_overflow_ && !recorder.configuration_snapshot_overflow_);
  recorder.evict_configuration_reference(references[0]);
  assert(recorder.capture_configuration_() != Recorder::MISSING_VALUE);
  assert(recorder.text(first) == initial && recorder.text(second) == changed);
  assert(recorder.text(third).find("\"phRunExtensionStopMargin\":null") != std::string::npos);
  const auto count = recorder.string_count_;
  recorder.release_configuration_scratch();
  assert(!recorder.available_());
  assert(recorder.capture_configuration_() == Recorder::MISSING_VALUE);
  assert(recorder.configuration_snapshot_overflow_ && recorder.string_count_ == count);
  recorder_test::allow_entity_reads = false;
}

void retention() {
  Environment env;
  Recorder recorder;
  recorder.setup();
  for (uint32_t seconds = 0; seconds <= 16 * 3600; seconds += 10) {
    if (seconds == 15 * 3600) env.extension().state = false;
    env.tick(recorder, seconds);
  }
  assert(!recorder.string_overflow_ && recorder.count_ == recorder.sample_capacity_);
  assert(recorder.string_count_ < Recorder::STRING_ENTRY_CAPACITY);
  Recorder::RecordingSnapshot snapshot;
  assert(recorder.capture_snapshot_(&snapshot, 15));
  assert(snapshot.count == 91 && snapshot.samples.size() == 91 * recorder.sample_stride_);
  assert(snapshot.event_count == 90 && snapshot.string_data.size() == snapshot.string_data_used);
  const auto offset = Recorder::sample_offset_(snapshot.sample_at(0));
  env.tick(recorder, 16 * 3600 + 10);
  assert(Recorder::sample_offset_(snapshot.sample_at(0)) == offset);
  emit_status("retention-status", recorder);
  for (uint32_t minutes : {0U, 15U, 120U, 360U, UINT32_MAX}) {
    httpd_req_t request;
    recorder.write_recording(&request, minutes);
    assert(request.status == HTTPD_200 && request.finished && !recorder.export_in_progress_);
    emit(std::to_string(minutes), request);
  }
  unsigned hooks = 0;
  recorder_test::allocation_hook = [&]() {
    assert(recorder_test::held_mutexes == 0);
    if (hooks++ == 0) env.restart(recorder);
  };
  Recorder::RecordingSnapshot restarted;
  assert(recorder.capture_snapshot_(&restarted, 360));
  recorder_test::allocation_hook = {};
  assert(restarted.recording_id == recorder.recording_id_ && restarted.count == 1);
  for (uint32_t seconds = 16 * 3600 + 20; seconds < 17 * 3600; seconds += 10) env.tick(recorder, seconds);
  hooks = 0;
  recorder_test::allocation_hook = [&]() {
    assert(recorder_test::held_mutexes == 0);
    if (hooks++ == 0) env.tick(recorder, recorder_test::now_ms / 1000 + 10);
  };
  Recorder::RecordingSnapshot appended;
  assert(recorder.capture_snapshot_(&appended, 15));
  recorder_test::allocation_hook = {};
  assert(appended.count == 91 && Recorder::sample_offset_(appended.sample_at(90)) ==
                                     Recorder::sample_offset_(recorder.sample_at_(recorder.count_ - 1)));
  hooks = 0;
  recorder_test::allocation_hook = [&]() {
    assert(recorder_test::held_mutexes == 0);
    ++hooks;
    env.tick(recorder, recorder_test::now_ms / 1000 + 10);
  };
  Recorder::RecordingSnapshot racing;
  assert(!recorder.capture_snapshot_(&racing, 0));
  recorder_test::allocation_hook = {};
  assert(recorder_test::held_mutexes == 0 && hooks < 20 && !racing.available);
  // Every snapshot allocation plus the JSON writer can fail independently.
  for (size_t failure = 1; failure <= 5; ++failure) {
    recorder_test::reset_allocation_faults();
    recorder_test::fail_allocation_at = failure;
    httpd_req_t failed;
    recorder.write_recording(&failed, 0);
    assert(failed.status == "503 Service Unavailable" && !recorder.export_in_progress_);
  }
  recorder_test::reset_allocation_faults();
  for (uint32_t seconds = recorder_test::now_ms / 1000 + 10; seconds < 18 * 3600; seconds += 10) {
    env.tick(recorder, seconds);
  }
  recorder_test::chunk_hook = [&]() {
    assert(recorder_test::held_mutexes == 0);
    env.restart(recorder);
    httpd_req_t duplicate;
    recorder.write_recording(&duplicate, 0);
    assert(duplicate.status == "503 Service Unavailable");
  };
  httpd_req_t immutable;
  recorder.write_recording(&immutable, 15);
  assert(immutable.status == HTTPD_200 && immutable.finished && !recorder.export_in_progress_);
  emit("immutable", immutable);
  for (unsigned i = 0; i < 100; ++i) env.tick(recorder, recorder_test::now_ms / 1000 + 10);
  httpd_req_t aborted;
  aborted.fail_chunk = 1;
  recorder.write_recording(&aborted, 0);
  assert(!recorder.export_in_progress_ && !aborted.finished);
  emit("aborted", aborted);
  assert(recorder.restart_rolling());
  httpd_req_t empty;
  recorder.write_recording(&empty, 360);
  assert(empty.status == HTTPD_200 && empty.finished && !recorder.export_in_progress_);
  emit("empty", empty);
  for (uint32_t seconds : {1000U, 1041U, 1059U, 1070U, 1085U, 1117U, 1130U}) env.tick(recorder, seconds);
  httpd_req_t jitter;
  recorder.write_recording(&jitter, 1);
  emit("jitter", jitter);
  const auto strings = recorder.string_count_;
  recorder_test::allow_entity_reads = true;
  for (const char* value : {"00:00", "23:59", "12:34"}) {
    env.clock().state = value;
    assert(recorder.capture("timeNowHhmm") != Recorder::MISSING_VALUE);
  }
  env.clock().state = "23:59";
  assert(recorder.capture("timeNowHhmm") == 1439);
  for (const char* value : {"24:00", "12:60", "9:00", "unknown", "12:3a", "12-34"}) {
    env.clock().state = value;
    assert(recorder.capture("timeNowHhmm") == Recorder::MISSING_VALUE);
  }
  env.clock().valid = false;
  assert(recorder.capture("timeNowHhmm") == Recorder::MISSING_VALUE);
  assert(recorder.string_count_ == strings && recorder_test::held_mutexes == 0);
  recorder_test::allow_entity_reads = false;
  for (uint32_t minutes : {0U, 545U, 754U, 1439U, 1440U, 65534U}) {
    assert(recorder.restart_rolling());
    env.clock().valid = true;
    env.tick(recorder, 2000);
    recorder.overwrite_clock(minutes);
    httpd_req_t request;
    recorder.write_recording(&request, 0);
    assert(request.status == HTTPD_200 && !recorder.export_in_progress_);
    emit("clock-" + std::to_string(minutes), request);
  }
}

void preference_failures() {
  const uint32_t recorder_key = fnv1_hash("openquatt_debug_recorder_enabled");
  constexpr uint32_t other_key = 0x12345678;
  struct Storage {
    uint8_t magic, enabled;
  };
  for (bool previous : {false, true}) {
    for (bool own_key_failure : {false, true}) {
      Environment env;
      Recorder recorder;
      recorder.setup();
      if (previous) {
        assert(recorder.set_enabled(false));
        assert(recorder.set_enabled(true));
      }
      for (uint32_t seconds : {0U, 10U, 20U}) env.tick(recorder, seconds);
      if (!previous) assert(recorder.set_enabled(false));
      assert(recorder.count_ == 3 && recorder.enabled() == previous && recorder.active() == previous);
      const auto recording_id = recorder.recording_id_;
      const auto last_sample = recorder.last_sample_monotonic_ms_;
      std::vector<uint8_t> samples;
      for (size_t i = 0; i < recorder.count_; ++i) {
        const auto* row = recorder.sample_at_(i);
        samples.insert(samples.end(), row, row + recorder.sample_stride_);
      }
      const auto unchanged = [&]() {
        assert(recorder.enabled() == previous && recorder.active() == previous);
        assert(recorder.count_ == 3 && recorder.recording_id_ == recording_id);
        assert(recorder.last_sample_monotonic_ms_ == last_sample);
        for (size_t i = 0; i < recorder.count_; ++i) {
          assert(std::memcmp(samples.data() + i * recorder.sample_stride_, recorder.sample_at_(i),
                             recorder.sample_stride_) == 0);
        }
      };
      const auto request_enabled = [&](bool enabled) {
        auto request = env.request("enabled");
        request.arguments["csrf_token"] = recorder.get_csrf_token();
        request.arguments["enabled"] = enabled ? "1" : "0";
        env.dispatch(request);
        return request;
      };
      auto other = env.preferences.make_preference<uint32_t>(other_key, true);
      const uint32_t other_value = 42;
      const auto queue_failure = [&]() {
        env.preferences.failed_write_key = own_key_failure ? recorder_key : other_key;
        if (!own_key_failure) assert(other.save(&other_value));
      };
      queue_failure();
      auto failed = request_enabled(!previous);
      assert(failed.response.status == "500" && env.preferences.pending.empty());
      unchanged();
      // Aggregate sync failure is not rollback: our key may already be durable.
      assert(env.preferences.durable.at(recorder_key)[1] ==
             static_cast<uint8_t>(own_key_failure ? previous : !previous));
      for (unsigned retry = 0; retry < 2; ++retry) {
        queue_failure();  // Real ESP32 sync discards failed keys, so requeue the unrelated failure.
        auto repeated = request_enabled(own_key_failure ? !previous : previous);
        assert(repeated.response.status == "500" && env.preferences.pending.empty());
        unchanged();
      }
      env.preferences.failed_write_key.reset();
      const auto attempts = env.preferences.save_attempts;
      const auto syncs = env.preferences.syncs;
      auto recovered = request_enabled(previous);
      assert(recovered.response.status == HTTPD_200);
      assert(env.preferences.save_attempts == attempts + 1 && env.preferences.syncs == syncs + 1);
      unchanged();
      const auto clean_saves = env.preferences.saves;
      const auto clean_syncs = env.preferences.syncs;
      assert(request_enabled(previous).response.status == HTTPD_200);
      assert(env.preferences.saves == clean_saves && env.preferences.syncs == clean_syncs);
      unchanged();
      if (previous) assert(request_enabled(false).response.status == HTTPD_200);
      assert(env.preferences.make_counts.at(recorder_key) == 1);
      assert(!recorder.enabled() && !recorder.active() && recorder.count_ == 3);
      assert(recorder.recording_id_ == recording_id);
      emit_status(
          (std::string("preference-") + (previous ? "on-" : "off-") + (own_key_failure ? "own" : "other")).c_str(),
          recorder);
      auto reboot_preferences = env.preferences.cold_boot();
      assert(reboot_preferences.pending.empty());
      global_preferences = &reboot_preferences;
      Recorder reboot;
      reboot.setup();
      assert(!reboot.enabled() && !reboot.active() && reboot.count_ == 0);
      global_preferences = &env.preferences;
    }
  }
  // A second component in the same process sees pending values; a cold boot
  // must discard that queue and recover only what actually reached NVS.
  {
    Environment env;
    Recorder recorder;
    recorder.setup();
    assert(recorder.set_enabled(false));
    auto preference = env.preferences.make_preference<Storage>(recorder_key, true);
    const Storage pending{0xa7, 1};
    assert(preference.save(&pending));
    Recorder same_process;
    same_process.setup();
    assert(same_process.enabled());
    auto reboot_preferences = env.preferences.cold_boot();
    global_preferences = &reboot_preferences;
    Recorder reboot;
    reboot.setup();
    assert(!reboot.enabled());
    global_preferences = &env.preferences;
  }
  for (bool failed_save : {false, true}) {
    Environment env;
    App = Application{};  // A disabled cold boot with no resolved recording entities has no active schema.
    auto preference = env.preferences.make_preference<Storage>(recorder_key, true);
    const Storage disabled{0xa7, 0};
    assert(preference.save(&disabled) && env.preferences.sync());
    Recorder recorder;
    recorder.setup();
    assert(recorder.available_() && !recorder.enabled() && recorder.field_count_ == 0);
    if (failed_save)
      env.preferences.fail_save_at = env.preferences.save_attempts + 2;
    else
      env.preferences.fail_sync_at = env.preferences.syncs + 2;
    auto enable = env.request("enabled");
    enable.arguments["csrf_token"] = recorder.get_csrf_token();
    enable.arguments["enabled"] = "1";
    env.dispatch(enable);
    assert(enable.response.status == "500" && !recorder.enabled() && !recorder.active());
    // Enabling persisted, schema activation failed, and the compensating
    // disable failed too. Runtime rollback alone cannot undo the flash write.
    assert(env.preferences.durable.at(recorder_key)[1] == 1);
    env.preferences.fail_save_at = env.preferences.fail_sync_at = 0;
    auto disable = env.request("enabled");
    disable.arguments["csrf_token"] = recorder.get_csrf_token();
    disable.arguments["enabled"] = "0";
    const auto attempts = env.preferences.save_attempts;
    env.dispatch(disable);
    assert(disable.response.status == HTTPD_200 && env.preferences.save_attempts == attempts + 1);
    assert(recorder.recording_id_ == 0 && recorder.count_ == 0 && !recorder.active());
    assert(env.preferences.make_counts.at(recorder_key) == 2);  // Seed handle plus one component handle.
    emit_status(failed_save ? "schema-compensation-save" : "schema-compensation-sync", recorder);
    auto reboot_preferences = env.preferences.cold_boot();
    global_preferences = &reboot_preferences;
    Recorder reboot;
    reboot.setup();
    assert(!reboot.enabled() && !reboot.active());
    global_preferences = &env.preferences;
  }
  for (bool previous : {false, true}) {
    Environment env;
    auto preference = env.preferences.make_preference<Storage>(recorder_key, true);
    const Storage stored{0xa7, static_cast<uint8_t>(previous)};
    assert(preference.save(&stored) && env.preferences.sync());
    recorder_test::fail_allocation_at = 1;
    Recorder recorder;
    recorder.setup();
    assert(!recorder.available_() && recorder.enabled() == previous);
    assert(env.preferences.make_counts.at(recorder_key) == 2);
    for (bool enabled : {!previous, previous, true, false, true, false}) {
      assert(recorder.set_enabled(enabled) && recorder.enabled() == enabled);
      assert(!recorder.active() && recorder.count_ == 0 && recorder.recording_id_ == 0);
      assert(env.preferences.make_counts.at(recorder_key) == 2);
    }
    auto reboot_preferences = env.preferences.cold_boot();
    global_preferences = &reboot_preferences;
    recorder_test::reset_allocation_faults();
    recorder_test::fail_allocation_at = 1;
    Recorder reboot;
    reboot.setup();
    assert(!reboot.available_() && !reboot.enabled());
    assert(reboot_preferences.make_counts.at(recorder_key) == 1);
    global_preferences = &env.preferences;
  }
  {
    Environment env;
    global_preferences = nullptr;
    Recorder recorder;
    recorder.setup();
    assert(recorder.enabled() && env.preferences.make_counts.empty());
    assert(!recorder.set_enabled(true));  // A default with no storage is not confirmed persistence.
    global_preferences = &env.preferences;
    const auto recording_id = recorder.recording_id_;
    assert(recorder.set_enabled(true));  // First successful save initializes exactly one lazy handle.
    assert(env.preferences.make_counts.at(recorder_key) == 1 && recorder.recording_id_ == recording_id);
    assert(recorder.set_enabled(false) && recorder.set_enabled(true));
    assert(env.preferences.make_counts.at(recorder_key) == 1);
  }
  {
    Environment env;
    Recorder recorder;
    recorder.setup();
    env.tick(recorder, 0);
    const auto recording_id = recorder.recording_id_;
    global_preferences = nullptr;
    assert(!recorder.set_enabled(false));
    assert(!recorder.set_enabled(true));  // An unchanged dirty value also needs real storage.
    assert(recorder.enabled() && recorder.active() && recorder.count_ == 1 && recorder.recording_id_ == recording_id);
    global_preferences = &env.preferences;
    assert(recorder.set_enabled(true));
    assert(recorder.count_ == 1 && recorder.recording_id_ == recording_id);
    assert(env.preferences.make_counts.at(recorder_key) == 1);
  }
}

void http_boundaries() {
  Environment env;
  Recorder recorder;
  recorder.setup();
  assert(!recorder.get_csrf_token().empty());
  auto& handler = *env.web.handlers[0];
  for (const char* path :
       {"status", "download", "download-range", "start", "stop", "restart", "enabled", "configure"}) {
    const bool read = std::strcmp(path, "status") == 0 || std::strncmp(path, "download", 8) == 0;
    auto correct = env.request(path, read ? HTTP_GET : HTTP_POST);
    assert(handler.canHandle(&correct));
    correct.verb = read ? HTTP_POST : HTTP_GET;
    assert(!handler.canHandle(&correct));
    correct.verb = HTTP_PUT;
    assert(!handler.canHandle(&correct));
    correct = env.request((std::string(path) + "/extra").c_str(), read ? HTTP_GET : HTTP_POST);
    assert(!handler.canHandle(&correct));
  }
  const auto id = recorder.recording_id_;
  for (const char* path : {"start", "stop", "restart", "enabled", "configure"}) {
    for (int invalid = 0; invalid < 5; ++invalid) {
      auto request = env.request(path);
      request.arguments["csrf_token"] = recorder.get_csrf_token();
      if (invalid == 0) request.headers.erase("Host");
      if (invalid == 1) request.headers["Origin"] = "http://evil.example";
      if (invalid == 2) request.headers["Referer"] = "http://evil.example/path";
      if (invalid == 3) request.arguments.erase("csrf_token");
      if (invalid == 4) request.arguments["csrf_token"] = "wrong";
      env.dispatch(request);
      assert(request.response.status == "403 Forbidden" && request.response.finished);
      assert(recorder.recording_id_ == id && recorder.enabled() && recorder.count_ == 0);
      assert(env.preferences.saves == 0);
    }
  }
  auto legacy = env.request("start");
  legacy.arguments["csrf_token"] = recorder.get_csrf_token();
  env.dispatch(legacy);
  assert(legacy.response.status == "410 Gone");
  auto restart = env.request("restart");
  restart.arguments["csrf_token"] = recorder.get_csrf_token();
  env.dispatch(restart);
  assert(restart.response.status == HTTPD_200 && recorder.recording_id_ > id && recorder.count_ == 0);
  env.tick(recorder, 10);
  auto disable = env.request("enabled");
  disable.arguments["csrf_token"] = recorder.get_csrf_token();
  disable.arguments["enabled"] = "0";
  env.preferences.fail_sync = true;
  env.dispatch(disable);
  assert(disable.response.status == "500" && recorder.enabled() && recorder.count_ == 1);
  env.preferences.fail_sync = false;
  for (uint32_t seconds = 20; seconds <= 130; seconds += 10) env.tick(recorder, seconds);
  disable.response = httpd_req_t{};
  env.dispatch(disable);
  assert(disable.response.status == HTTPD_200 && !recorder.enabled() && recorder.count_ == 13);
  for (const char* minutes : {"1", "4294967297"}) {
    auto download = env.request("download-range?ignored=1", HTTP_GET);
    // Overflow must fall back to the full history. An unchecked uint32_t cast
    // wraps 4294967297 to 1, which would incorrectly match the shorter control.
    download.arguments["last_minutes"] = minutes;
    env.dispatch(download);
    assert(download.response.status == HTTPD_200 && download.response.finished);
    emit(std::string("http-range-") + minutes, download.response);
  }
  auto full = env.request("download", HTTP_GET);
  env.dispatch(full);
  assert(full.response.status == HTTPD_200 && full.response.finished);
  emit("http-full-download", full.response);
}
}  // namespace

int main(int argc, char** argv) {
  assert(argc == 2);
  const std::string scenario = argv[1];
  if (scenario == "setup")
    setup_boundaries();
  else if (scenario == "lifecycle")
    lifecycle();
  else if (scenario == "configuration")
    configuration();
  else if (scenario == "sensor-sources")
    sensor_sources();
  else if (scenario == "retention")
    retention();
  else if (scenario == "http")
    http_boundaries();
  else if (scenario == "preference-failures")
    preference_failures();
  else
    return 2;
}
