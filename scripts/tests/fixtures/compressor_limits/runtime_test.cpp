#include <cassert>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "components/openquatt_compressor_limits/openquatt_compressor_limits.h"
#include "nvs.h"

using namespace esphome::openquatt_compressor_limits;

uint32_t fake_millis{0U};
struct Blob {
  std::vector<uint8_t> bytes;
  bool wrong_type{false};
};
static std::map<std::string, Blob> entries;
static bool initialized{true}, open_error{false}, write_error{false}, commit_error{false}, readback_error{false};
static bool erase_error{false};
static unsigned writes{0U}, erasures{0U}, handles{0U};

esp_err_t nvs_open(const char* ns, int, nvs_handle_t* handle) {
  assert(std::string(ns) == "esphome");
  if (!initialized) return ESP_ERR_NVS_NOT_INITIALIZED;
  if (open_error) return ESP_FAIL;
  *handle = ++handles;
  return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t, const char* key, void* data, size_t* bytes) {
  auto it = entries.find(key);
  if (it == entries.end()) return ESP_ERR_NVS_NOT_FOUND;
  if (it->second.wrong_type) return ESP_ERR_NVS_TYPE_MISMATCH;
  if (readback_error && writes > 0U && std::string(key) == "oq_cycle_limits") return ESP_FAIL;
  if (data) {
    assert(*bytes >= it->second.bytes.size());
    std::memcpy(data, it->second.bytes.data(), it->second.bytes.size());
  }
  *bytes = it->second.bytes.size();
  return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t, const char* key, const void* data, size_t bytes) {
  ++writes;
  if (write_error) return ESP_ERR_NVS_NOT_ENOUGH_SPACE;
  const auto* first = static_cast<const uint8_t*>(data);
  entries[key] = Blob{{first, first + bytes}, false};
  return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t, const char* key) {
  ++erasures;
  if (erase_error) return ESP_FAIL;
  return entries.erase(key) ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_commit(nvs_handle_t) { return commit_error ? ESP_FAIL : ESP_OK; }
void nvs_close(nvs_handle_t) {
  assert(handles > 0U);
  --handles;
}
template <typename T>
static void put(const char* key, const T& value) {
  const auto* first = reinterpret_cast<const uint8_t*>(&value);
  entries[key] = Blob{{first, first + sizeof(value)}, false};
}
static void reset() {
  entries.clear();
  initialized = true;
  open_error = write_error = commit_error = readback_error = erase_error = false;
  fake_millis = writes = erasures = handles = 0U;
}
static LimitsRecord bundle() {
  LimitsRecord record{};
  assert(entries.at("oq_cycle_limits").bytes.size() == sizeof(record));
  std::memcpy(&record, entries.at("oq_cycle_limits").bytes.data(), sizeof(record));
  return record;
}
struct Controller {
  CompressorLimits component;
  WarningLimitNumber short_limit, long_limit;
  Controller() {
    short_limit.key = 123U;
    long_limit.key = 456U;
    component.set_limit(0U, &short_limit);
    component.set_limit(1U, &long_limit);
  }
};

int main() {
  // Both requests survive shutdown without an intervening loop/scheduler.
  reset();
  put("123", 9.0f);
  put("456", 75.0f);
  {
    Controller controller;
    controller.component.setup();
    assert(controller.short_limit.state == 9.0f && controller.long_limit.state == 75.0f);
    controller.short_limit.set(10.0f);
    assert(controller.short_limit.state == 10.0f);
    controller.long_limit.set(90.0f);
    controller.short_limit.set(12.0f);
    controller.component.on_shutdown();
    assert(bundle().limit_2h == 12.0f && bundle().limit_72h == 90.0f);
    assert(entries.count("123") == 0U && entries.count("456") == 0U);
    controller.short_limit.set(15.0f);
    fake_millis = 60000U;
    controller.component.loop();
    controller.component.on_shutdown();
    assert(bundle().limit_2h == 12.0f && writes == 1U);
  }
  {
    Controller reboot;
    reboot.component.setup();
    assert(reboot.short_limit.state == 12.0f && reboot.long_limit.state == 90.0f);
  }

  // Off-loop bursts coalesce in two slots, update RAM on loop, and respect
  // the write interval. Concurrent writers never access NVS or live state.
  reset();
  {
    Controller controller;
    controller.component.setup();
    std::thread first([&]() {
      for (int n = 0; n < 1000; ++n) controller.short_limit.set(11.0f);
    });
    std::thread second([&]() {
      for (int n = 0; n < 1000; ++n) controller.long_limit.set(85.0f);
    });
    first.join();
    second.join();
    assert(writes == 0U);
    controller.component.loop();
    assert(controller.short_limit.state == 11.0f && controller.long_limit.state == 85.0f);
    assert(writes == 0U);
    fake_millis = 59999U;
    controller.component.loop();
    assert(writes == 0U);
    fake_millis = 60000U;
    controller.component.loop();
    assert(writes == 1U);
    assert(bundle().limit_2h == 11.0f && bundle().limit_72h == 85.0f);
  }

  // Full NVS, failed commit, and failed raw readback retain both legacy keys.
  reset();
  {
    Controller controller;
    controller.component.setup();
    controller.short_limit.callback = [&](float) { controller.long_limit.set(95.0f); };
    std::thread writer([&]() {
      controller.short_limit.set(10.0f);
      controller.long_limit.set(80.0f);
    });
    writer.join();
    controller.component.loop();
    assert(controller.long_limit.state == 95.0f);  // Callback beats old slot1.
    controller.short_limit.callback = nullptr;
    controller.component.on_shutdown();
    assert(bundle().limit_72h == 95.0f);
  }

  // Off-task requests also survive shutdown without any intermediate loop.
  reset();
  {
    Controller controller;
    controller.component.setup();
    std::thread writer([&]() {
      controller.short_limit.set(14.0f);
      controller.long_limit.set(95.0f);
    });
    writer.join();
    controller.component.on_shutdown();
    assert(bundle().limit_2h == 14.0f && bundle().limit_72h == 95.0f);
  }

  // Full NVS, failed commit, and failed raw readback retain both legacy keys.
  for (unsigned fault = 0U; fault < 3U; ++fault) {
    reset();
    put("123", 9.0f);
    put("456", 75.0f);
    Controller controller;
    controller.component.setup();
    write_error = fault == 0U;
    commit_error = fault == 1U;
    readback_error = fault == 2U;
    controller.component.on_shutdown();
    assert(entries.count("123") && entries.count("456"));
    assert(erasures == 0U && controller.component.warning);
    write_error = commit_error = readback_error = false;
    controller.component.on_shutdown();
    assert(entries.count("123") == 0U && entries.count("456") == 0U);
    assert(!controller.component.warning);
  }

  // Wrong type/length/open failure cannot trigger migration or erasure.
  for (unsigned fault = 0U; fault < 4U; ++fault) {
    reset();
    put("123", 9.0f);
    put("456", 75.0f);
    if (fault == 0U) entries["123"].wrong_type = true;
    if (fault == 1U) entries["123"].bytes.resize(8U);
    if (fault == 2U) open_error = true;
    if (fault == 3U) entries["oq_cycle_limits"] = Blob{{0U}, false};
    Controller controller;
    controller.component.setup();
    controller.short_limit.set(10.0f);
    controller.component.on_shutdown();
    assert(writes == 0U && erasures == 0U && controller.component.warning);
    assert(controller.short_limit.state == 6.0f);
  }

  // Valid new record wins over malformed stale keys; cleanup failure retries
  // without rewriting the record and never erases an unexpected blob.
  reset();
  LimitsRecord current{};
  current.limit_2h = 13.0f;
  put("oq_cycle_limits", current);
  entries["123"] = Blob{{0U}, true};
  {
    Controller controller;
    controller.component.setup();
    assert(controller.short_limit.state == 13.0f);
    controller.component.on_shutdown();
    assert(writes == 0U && erasures == 0U && entries.count("123"));
  }
  reset();
  put("123", 9.0f);
  {
    Controller controller;
    controller.component.setup();
    erase_error = true;
    controller.component.on_shutdown();
    assert(writes == 1U && entries.count("123"));
    erase_error = false;
    controller.component.on_shutdown();
    assert(writes == 1U && entries.count("123") == 0U);
  }

  // Factory reset deinitializes/erases NVS before safe_reboot. Shutdown must
  // not recreate a just-erased bundle, even with accepted pending changes.
  reset();
  {
    Controller controller;
    controller.component.setup();
    controller.short_limit.set(10.0f);
    entries.clear();
    initialized = false;
    controller.component.on_shutdown();
    assert(entries.empty() && writes == 0U);
  }
  assert(handles == 0U);
}
