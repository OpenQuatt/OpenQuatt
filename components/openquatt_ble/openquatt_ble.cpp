#include "openquatt_ble.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include <esp_heap_caps.h>
#include <nvs.h>
#include <algorithm>
#include <array>

namespace esphome::openquatt_ble {
namespace {
OpenQuattBle* receiver{};
using Record = std::array<uint8_t, 13 + CAPACITY * 41>;
constexpr char STORE[] = "oq_ble";
constexpr char KEY[] = "config";
constexpr uint32_t RADIO_START_RETRY_MS = 30000;

void put(uint8_t* dest, uint64_t value, size_t width) {
  for (size_t i = 0; i < width; ++i) {
    dest[i] = uint8_t(value);
    value >>= 8;
  }
}
uint64_t get(const uint8_t* src, size_t width) {
  uint64_t v{};
  while (width) v = (v << 8) | src[--width];
  return v;
}

Record encode(const BleConfig& c) {
  Record r{};
  r[0] = 1;
  r[1] = c.enabled;
  r[2] = c.count;
  put(&r[3], c.revision, 4);
  put(&r[7], c.room_mac, 6);
  for (size_t i = 0; i < c.count; ++i) {
    auto* p = &r[13 + 41 * i];
    const auto& s = c.paired[i];
    put(p, s.mac, 6);
    p[6] = uint8_t(s.protocol);
    p[7] = s.use_dew_point;
    std::memcpy(p + 8, s.name, sizeof(s.name));
  }
  return r;
}

bool load(BleConfig& c) {
  nvs_handle_t handle{};
  auto err = nvs_open(STORE, NVS_READONLY, &handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) return true;
  if (err != ESP_OK) return false;
  Record r{};
  size_t n = r.size();
  err = nvs_get_blob(handle, KEY, r.data(), &n);
  nvs_close(handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) return true;
  if (err != ESP_OK || n != r.size() || r[0] != 1 || r[1] > 1 || r[2] > CAPACITY) return false;
  c.enabled = r[1];
  c.count = r[2];
  c.revision = get(&r[3], 4);
  c.room_mac = get(&r[7], 6);
  for (size_t i = 0; i < c.count; ++i) {
    auto* p = &r[13 + 41 * i];
    if (p[7] > 1) return false;
    auto& s = c.paired[i];
    s.mac = get(p, 6);
    s.protocol = Protocol(p[6]);
    s.use_dew_point = p[7];
    std::memcpy(s.name, p + 8, sizeof(s.name));
  }
  return valid_config(c);
}

bool save(const BleConfig& c) {
  auto record = encode(c);
  nvs_handle_t handle{};
  if (nvs_open(STORE, NVS_READWRITE, &handle) != ESP_OK) return false;
  const bool committed =
      nvs_set_blob(handle, KEY, record.data(), record.size()) == ESP_OK && nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  if (!committed || nvs_open(STORE, NVS_READONLY, &handle) != ESP_OK) return false;
  Record verify{};
  size_t size = verify.size();
  const auto status = nvs_get_blob(handle, KEY, verify.data(), &size);
  nvs_close(handle);
  return status == ESP_OK && size == verify.size() && record == verify;
}

template <typename T>
const T* find(const T* slots, size_t size, uint64_t mac, Protocol protocol) {
  for (size_t i = 0; i < size; ++i)
    if (slots[i].mac == mac && slots[i].protocol == protocol) return slots + i;
  return nullptr;
}
}  // namespace

void OpenQuattBle::setup() {
  receiver = this;
  if (state_.allocate()) {
    auto& s = state_[0];
    s.storage_unknown = !load(s.candidate);
    if (!s.storage_unknown) s.config = s.candidate;
    s.storage_error = s.storage_unknown;
    if (!s.storage_unknown) radio_(s.config.enabled);
#ifndef USE_OPENQUATT_BLE_NIMBLE
    tracker_->set_raw_advertisement_callback(
        {this, [](void* self, const ble_device_base::RawAdvertisement& adv) {
           static_cast<OpenQuattBle*>(self)->observe_(
               {adv.address, adv.data, adv.data_len, adv.rssi, millis(), AdvertisementKind::COMBINED});
         }});
#endif
  }
  register_http_handlers();
}

void OpenQuattBle::radio_(bool enabled) {
  auto& s = state_[0];
  s.radio_start_pending = false;
#ifndef USE_OPENQUATT_BLE_NIMBLE
  auto* ble = esp32_ble::global_ble;
  if (!ble || ble->is_failed() || tracker_->is_failed()) return;
#endif
  if (enabled) {
    // The S3 controller may assert during init if this contiguous DMA allocation fails.
    // Include the 2 KiB startup margin used by OpenAmber; verify margin on Q hardware.
    constexpr size_t CONTROLLER_BLOCK = 0x7800 + 2048;
    const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    ESP_LOGI("openquatt_ble", "Bluetooth start: largest internal DMA block %u bytes", unsigned(largest));
    if (largest < CONTROLLER_BLOCK) {
      ESP_LOGW("openquatt_ble", "BLE start skipped: largest internal DMA block %u bytes", unsigned(largest));
      s.last_radio_start_attempt = millis();
      s.radio_start_pending = true;
      return;
    }
  }
#ifdef USE_OPENQUATT_BLE_NIMBLE
  scanner_.set_enabled(enabled);
#else
  if (enabled)
    ble->enable();
  else
    ble->disable();
#endif
}

bool OpenQuattBle::ready_() const {
#ifdef USE_OPENQUATT_BLE_NIMBLE
  return scanner_.ready();
#else
  auto* ble = esp32_ble::global_ble;
  return ble && !ble->is_failed() && ble->is_active() && !tracker_->is_failed() && tracker_->scan_running();
#endif
}

bool OpenQuattBle::radio_failed_() const {
#ifdef USE_OPENQUATT_BLE_NIMBLE
  return scanner_.failed();
#else
  return !esp32_ble::global_ble || esp32_ble::global_ble->is_failed() || tracker_->is_failed();
#endif
}

void OpenQuattBle::observe_(const Advertisement& adv) {
  auto& s = state_[0];
  const auto now = adv.received_at;
  if (!s.config.enabled || s.storage_unknown || s.driver_failed) return;
  size_t index = 0;
  while (index < s.config.count && s.config.paired[index].mac != adv.address) ++index;
  const bool paired = index < s.config.count;
  if (!paired && (!s.searching || now - s.search_start >= SEARCH_MS)) return;
  Frame f{};
  if (!decode_advertisement(adv.address, adv.data, adv.length, f)) return;
  std::lock_guard guard(lock_);
  Measurement* m;
  if (paired) {
    if (s.config.paired[index].protocol != f.protocol) return;
    m = &s.paired[index];
  } else {
    size_t i = 0;
    while (i < s.discoveries && s.discovered[i].mac != adv.address) ++i;
    if (i == s.discoveries) {
      if (i == CAPACITY) return;
      ++s.discoveries;
    } else if (s.discovered[i].protocol != f.protocol)
      return;
    m = &s.discovered[i];
  }
  m->mac = adv.address;
  m->protocol = f.protocol;
  m->ingest(f, adv.rssi, now);
}

Submit OpenQuattBle::admission_(uint32_t expected) {
  auto* web = web_server_base::global_web_server_base;
  if (!state_ || !web || web->is_recovery_active()) return Submit::UNAVAILABLE;
  auto& s = state_[0];
  if (s.storage_unknown) return Submit::UNAVAILABLE;
  if (s.command != Command::NONE) return Submit::BUSY;
  if (expected != s.config.revision) return Submit::CONFLICT;
  s.recovery_epoch = web->recovery_epoch();
  return Submit::ACCEPTED;
}
bool OpenQuattBle::observed_(const BleConfig& candidate, uint32_t now) const {
  const auto& s = state_.data()[0];
  for (size_t i = 0; i < candidate.count; ++i) {
    const auto& p = candidate.paired[i];
    const auto* old = find(s.config.paired, s.config.count, p.mac, p.protocol);
    if (old && (!p.use_dew_point || old->use_dew_point)) continue;
    const auto* d = old ? &s.paired[old - s.config.paired] : find(s.discovered, s.discoveries, p.mac, p.protocol);
    if (!s.config.enabled || !d) return false;
    const auto reading = d->read(now);
    if (!std::isfinite(reading.temperature) || (p.use_dew_point && !std::isfinite(reading.dew_point))) return false;
  }
  return true;
}

Submit OpenQuattBle::submit_config(const BleConfig& candidate, uint32_t expected_revision) {
  std::unique_lock guard(lock_, std::try_to_lock);
  if (!guard) return Submit::BUSY;
  auto admitted = admission_(expected_revision);
  if (admitted != Submit::ACCEPTED) return admitted;
  if (!valid_config(candidate) || !observed_(candidate, millis()) || expected_revision == UINT32_MAX)
    return Submit::INVALID;
  auto& s = state_[0];
  s.candidate = candidate;
  s.candidate.revision = expected_revision + 1;
  s.command = Command::CONFIG;
  return Submit::ACCEPTED;
}
Submit OpenQuattBle::submit_scan(bool active, uint32_t expected_revision) {
  std::unique_lock guard(lock_, std::try_to_lock);
  if (!guard) return Submit::BUSY;
  const auto admitted = admission_(expected_revision);
  if (admitted != Submit::ACCEPTED) return admitted;
  auto& s = state_[0];
  if (!s.config.enabled || (active && !s.ready)) return Submit::UNAVAILABLE;
  s.command = active ? Command::START : Command::STOP;
  return Submit::ACCEPTED;
}

void OpenQuattBle::apply_(uint32_t now) {
  auto& s = state_[0];
  const auto command = s.command;
  s.command = Command::NONE;
  auto* web = web_server_base::global_web_server_base;
  if (!web || web->recovery_epoch() != s.recovery_epoch || web->is_recovery_active()) return;
  if (command == Command::START) {
    if (!s.ready || s.searching) return;
    s.searching = true;
    s.search_start = now;
    s.discoveries = 0;
    for (auto& entry : s.discovered) entry = {};
    return;
  }
  if (command == Command::STOP) {
    s.searching = false;
    return;
  }
  if (!observed_(s.candidate, now)) return;
  const auto persisted = persist_config(s.config, s.candidate, save, [&] {
    return !web->is_recovery_active() && web->recovery_epoch() == s.recovery_epoch;
  });
  if (persisted != Persist::APPLIED) {
    s.storage_error = true;
    if (persisted == Persist::UNKNOWN) {
      s.storage_unknown = true;
      ++s.room_generation;
      s.ready = s.searching = false;
      radio_(false);
    }
    return;
  }
  s.storage_error = false;
  Measurement next[CAPACITY]{};
  if (s.config.enabled && s.candidate.enabled) {
    for (size_t i = 0; i < s.candidate.count; ++i) {
      auto& p = s.candidate.paired[i];
      auto* old = find(s.paired, s.config.count, p.mac, p.protocol);
      if (!old) old = find(s.discovered, s.discoveries, p.mac, p.protocol);
      if (old) next[i] = *old;
    }
  }
  std::copy(std::begin(next), std::end(next), s.paired);
  if (s.candidate.enabled != s.config.enabled) {
    s.searching = s.ready = false;
    s.discoveries = 0;
    radio_(s.candidate.enabled);
  }
  if (s.candidate.room_mac != s.config.room_mac || (!s.candidate.enabled && s.config.enabled)) ++s.room_generation;
  s.config = s.candidate;
}

void OpenQuattBle::loop() {
  if (!state_) return;
#ifdef USE_OPENQUATT_BLE_NIMBLE
  scanner_.service(millis());
  NimbleScanner::Packet packet;
  for (size_t i = 0; i < NimbleScanner::QUEUE_CAPACITY && scanner_.pop(packet); ++i) observe_(packet.view());
#endif
  std::lock_guard guard(lock_);
  auto& s = state_[0];
  const uint32_t now = millis();
  s.ready = !s.storage_unknown && s.config.enabled && ready_();
  if (!s.driver_failed && radio_failed_()) {
    s.driver_failed = true;
    s.searching = false;
    s.discoveries = 0;
    for (auto& entry : s.paired) entry = {};
  }
  if (s.searching && now - s.search_start >= SEARCH_MS) s.searching = false;
  for (size_t i = 0; i < s.config.count; ++i) s.paired[i].expire(now);
  for (size_t i = 0; i < s.discoveries; ++i) s.discovered[i].expire(now);
  if (s.command != Command::NONE) apply_(now);
  if (s.radio_start_pending && !s.driver_failed && millis() - s.last_radio_start_attempt >= RADIO_START_RETRY_MS)
    radio_(true);
}

uint32_t room_generation() { return receiver && receiver->state_ ? receiver->state_[0].room_generation : 0; }
float room_temperature(uint32_t now_ms) {
  if (!receiver || !receiver->state_) return NAN;
  auto& s = receiver->state_[0];
  if (!s.config.enabled || s.storage_unknown) return NAN;
  for (size_t i = 0; i < s.config.count; ++i) {
    if (s.config.paired[i].mac == s.config.room_mac) return s.paired[i].read(now_ms).temperature;
  }
  return NAN;
}
DewPointInput dew_point(uint32_t now_ms) {
  if (!receiver || !receiver->state_) return {NAN, true};
  auto& s = receiver->state_[0];
  if (s.storage_unknown) return {NAN, true};
  return highest_dew_point(s.config, s.paired, now_ms);
}
bool OpenQuattBle::snapshot(BleSnapshot& out) {
  std::unique_lock guard(lock_, std::try_to_lock);
  if (!guard || !state_) return false;
  auto& s = state_[0];
  out.config = s.config;
  out.pending = s.command != Command::NONE;
  out.storage_error = s.storage_error;
  out.ready = s.ready;
  out.searching = s.searching;
  const uint32_t now = millis();
  const auto dew = dew_point(now);
  out.dew_required = dew.required;
  out.dew_point = dew.value;
  for (size_t i = 0; i < s.config.count; ++i) {
    out.paired[i] = s.paired[i].read(now);
    out.paired[i].mac = s.config.paired[i].mac;
    out.paired[i].protocol = s.config.paired[i].protocol;
  }
  out.discovered_count = 0;
  for (size_t i = 0; i < s.discoveries; ++i) {
    const auto& d = s.discovered[i];
    if (!find(s.config.paired, s.config.count, d.mac, d.protocol)) out.discovered[out.discovered_count++] = d.read(now);
  }
  return true;
}
}  // namespace esphome::openquatt_ble
