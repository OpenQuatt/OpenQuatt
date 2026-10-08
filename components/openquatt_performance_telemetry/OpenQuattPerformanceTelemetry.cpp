#include "OpenQuattPerformanceTelemetry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "esp_timer.h"
#include "OpenQuattPerformanceTelemetryPolicy.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::openquatt_performance_telemetry {
namespace {

static const char* const TAG = "openquatt.performance_telemetry";
static const uint32_t STORAGE_KEY = fnv1_hash("openquatt_performance_telemetry_store");

void append_float(FixedBufferWriter& output, float value, unsigned decimals) {
  if (!std::isfinite(value)) {
    output += "null";
    return;
  }
  char buffer[32];
  const int length = std::snprintf(buffer, sizeof(buffer), "%.*f", static_cast<int>(decimals), value);
  if (length <= 0 || static_cast<size_t>(length) >= sizeof(buffer)) {
    output += "null";
    return;
  }
  output += buffer;
}

void append_hp(FixedBufferWriter& output, const OpenQuattPerformanceTelemetry::MinuteRecord& record, size_t index) {
  if ((record.active_mask & (1U << index)) == 0U) {
    output += "null";
    return;
  }
  const auto& hp = record.hp[index];
  output += R"({"l":)";
  output.append_uint(static_cast<uint64_t>(hp.level));
  output += R"(,"hz":)";
  append_float(output, hp.frequency_sum / 6.0f, 1U);
  output += R"(,"ti":)";
  append_float(output, hp.water_in_sum / 6.0f, 2U);
  output += R"(,"to":)";
  append_float(output, hp.water_out_sum / 6.0f, 2U);
  output += R"(,"el":)";
  append_float(output, hp.electrical_wh, 2U);
  output += R"(,"et":)";
  append_float(output, hp.thermal_wh, 2U);
  output += R"(,"ep":)";
  append_float(output, hp.pump_wh, 2U);
  output += R"(,"b":)";
  output += hp.bottom_plate_heater ? "true}" : "false}";
}

}  // namespace

float OpenQuattPerformanceTelemetry::get_setup_priority() const { return setup_priority::LATE; }

void OpenQuattPerformanceTelemetry::setup() {
  // esp_timer_get_time() is device uptime from the ESP timer epoch. Keep the
  // first slot at exactly boot+15m; setup latency is caught up by loop().
  this->next_publish_us_ = PERFORMANCE_PUBLISH_INTERVAL_US;
  if (!this->records_.allocate()) {
    ESP_LOGE(TAG, "Could not allocate performance records in PSRAM; telemetry remains disabled");
    this->publish_state(false);
    this->mark_failed();
    return;
  }
  if (global_preferences == nullptr) {
    ESP_LOGE(TAG, "Preferences backend is unavailable; performance telemetry remains disabled");
    this->publish_state(false);
    return;
  }
  this->pref_ = global_preferences->make_preference<Storage>(STORAGE_KEY, true);
  Storage storage{};
  if (!this->load_storage_(&storage)) {
    storage = {STORAGE_MAGIC, STORAGE_VERSION, 0U, 0U};
    if (!this->save_storage_(storage)) {
      ESP_LOGE(TAG, "Could not initialize performance telemetry preference");
      this->publish_state(false);
      return;
    }
  }
  if (!this->apply_storage_(storage)) {
    ESP_LOGE(TAG, "Could not apply performance telemetry preference");
    this->publish_state(false);
    this->mark_failed();
  }
}

void OpenQuattPerformanceTelemetry::loop() {
  this->handle_transport_result_();
  const int64_t monotonic_now_us = esp_timer_get_time();
  const bool publish_due = performance_publish_due(monotonic_now_us, this->next_publish_us_);
  if (publish_due) {
    this->next_publish_us_ = advance_performance_publish_deadline(this->next_publish_us_, monotonic_now_us);
  }
  if (!this->enabled_.load() || !this->setup_complete_() || this->clock_ == nullptr) return;

  const auto now = this->clock_->now();
  if (!now.is_valid() || now.timestamp <= 0) return;
  const uint32_t timestamp_s = static_cast<uint32_t>(now.timestamp);
  const uint32_t slot_s = timestamp_s - (timestamp_s % SAMPLE_SECONDS);
  if (slot_s != this->last_sample_slot_s_) {
    this->process_sample_(slot_s);
  }
  // The cadence is anchored to monotonic device uptime. Only complete minutes
  // already finalized by process_sample_ enter the batch; a half-filled minute
  // remains in minute_ and is finalized exactly once at the next UTC boundary.
  if (publish_due) {
    this->pending_publish_allowed_ = true;
    this->close_window_(true);
  }
  this->try_publish_pending_();
}

void OpenQuattPerformanceTelemetry::dump_config() {
  ESP_LOGCONFIG(TAG, "OpenQuatt performance model validation:");
  ESP_LOGCONFIG(TAG, "  Enabled: %s", YESNO(this->enabled_.load()));
  ESP_LOGCONFIG(TAG, "  Choice configured: %s", YESNO(this->choice_configured_.load()));
  ESP_LOGCONFIG(TAG, "  Prompt handled: %s", YESNO(this->prompt_handled_.load()));
  ESP_LOGCONFIG(TAG, "  Topology: %s", this->topology_.c_str());
  ESP_LOGCONFIG(TAG, "  Record storage: %u records in PSRAM", static_cast<unsigned>(RECORD_STORAGE_COUNT));
}

bool OpenQuattPerformanceTelemetry::load_storage_(Storage* storage) {
  if (storage == nullptr || !this->pref_.load(storage) || storage->magic != STORAGE_MAGIC || storage->enabled > 1U) {
    return false;
  }
  if (storage->version == 1U) {
    if (storage->choice_configured > 1U) return false;
    // Legacy choices retain their consent. The separate invitation has not
    // been handled yet; an existing opt-in suppresses it in the web UI.
    storage->version = STORAGE_VERSION;
  } else if (storage->version != STORAGE_VERSION || storage->choice_configured > (CHOICE_CONFIGURED | PROMPT_HANDLED)) {
    return false;
  }
  return storage->enabled == 0U || (storage->choice_configured & CHOICE_CONFIGURED) != 0U;
}

bool OpenQuattPerformanceTelemetry::save_storage_(const Storage& storage) {
  return this->pref_.save(&storage) && global_preferences != nullptr && global_preferences->sync();
}

bool OpenQuattPerformanceTelemetry::apply_storage_(const Storage& storage) {
  const bool enabled = storage.enabled != 0U;
  this->enabled_.store(enabled);
  this->choice_configured_.store((storage.choice_configured & CHOICE_CONFIGURED) != 0U);
  this->prompt_handled_.store((storage.choice_configured & PROMPT_HANDLED) != 0U);
  this->publish_state(enabled);
  if (this->choice_configured_sensor_ != nullptr) {
    this->choice_configured_sensor_->publish_state(this->choice_configured_.load());
  }
  if (this->prompt_handled_sensor_ != nullptr) {
    this->prompt_handled_sensor_->publish_state(this->prompt_handled_.load());
  }
  return true;
}

bool OpenQuattPerformanceTelemetry::setup_complete_() const {
  return this->setup_complete_sensor_ != nullptr && this->setup_complete_sensor_->has_state() &&
         this->setup_complete_sensor_->state;
}

void OpenQuattPerformanceTelemetry::write_state(bool state) {
  const bool current = this->enabled_.load();
  if (!state && this->transport_ != nullptr) {
    // The transport gate is closed before data and preferences are touched.
    this->transport_->cancel_external_publish();
  }
  if (state && (this->transport_ == nullptr || !this->transport_->ensure_installation_id_for_external())) {
    ESP_LOGE(TAG, "Could not create an anonymous installation ID; performance telemetry remains disabled");
    this->enabled_.store(false);
    this->choice_configured_.store(false);
    this->prompt_handled_.store(false);
    if (this->choice_configured_sensor_ != nullptr) this->choice_configured_sensor_->publish_state(false);
    if (this->prompt_handled_sensor_ != nullptr) this->prompt_handled_sensor_->publish_state(false);
    if (this->transport_ != nullptr) this->transport_->cancel_external_publish();
    this->publish_state(false);
    this->reset_collection_();
    return;
  }

  Storage storage{};
  if (!this->load_storage_(&storage)) {
    storage = {STORAGE_MAGIC, STORAGE_VERSION, static_cast<uint8_t>(current ? 1U : 0U), 0U};
  }
  storage.enabled = state ? 1U : 0U;
  storage.choice_configured = CHOICE_CONFIGURED | PROMPT_HANDLED;
  if (!this->save_storage_(storage)) {
    ESP_LOGE(TAG, "Could not persist performance telemetry preference");
    // save() may have queued an opt-in even when sync() failed. Replace it
    // with a fail-closed value before another component can flush preferences.
    storage.enabled = 0U;
    storage.choice_configured = 0U;
    if (!this->save_storage_(storage)) {
      ESP_LOGE(TAG, "Could not persist fail-closed fallback; consent remains unconfirmed");
    }
    this->choice_configured_.store(false);
    this->prompt_handled_.store(false);
    if (this->prompt_handled_sensor_ != nullptr) this->prompt_handled_sensor_->publish_state(false);
    if (this->choice_configured_sensor_ != nullptr) this->choice_configured_sensor_->publish_state(false);
    if (this->transport_ != nullptr) this->transport_->cancel_external_publish();
    this->enabled_.store(false);
    this->publish_state(false);
    this->reset_collection_();
    return;
  }
  this->apply_storage_(storage);
  this->reset_collection_();
  if (!state && this->transport_ != nullptr) this->transport_->cancel_external_publish();
}

void OpenQuattPerformanceTelemetry::reset_collection_() {
  this->minute_ = {};
  this->last_sample_slot_s_ = 0U;
  this->active_window_s_ = 0U;
  this->active_record_count_ = 0U;
  this->clear_pending_();
  this->hp_runtime_ = {};
}

void OpenQuattPerformanceTelemetry::process_sample_(uint32_t timestamp_s) {
  if (this->last_sample_slot_s_ != 0U && timestamp_s < this->last_sample_slot_s_) {
    // Clock correction backwards: do not mix samples from different UTC windows.
    this->reset_collection_();
  }
  const uint32_t minute_s = timestamp_s - (timestamp_s % MINUTE_SECONDS);
  if (this->minute_.start_s != 0U && minute_s != this->minute_.start_s) {
    this->finish_minute_();
    this->minute_ = {};
  }
  if (this->minute_.start_s == 0U) this->minute_.start_s = minute_s;
  if (this->last_sample_slot_s_ != 0U && timestamp_s > this->last_sample_slot_s_ + SAMPLE_SECONDS) {
    this->minute_.invalid = true;
  }
  this->last_sample_slot_s_ = timestamp_s;
  this->sample_minute_(timestamp_s);
}

bool OpenQuattPerformanceTelemetry::sample_minute_(uint32_t timestamp_s) {
  if (!this->records_ || this->minute_.samples >= 6U) {
    this->minute_.invalid = true;
    return false;
  }
  const int generation = generation_code_(this->generation_select_);
  if (generation == 0 || !valid_sensor_(this->outside_temp_sensor_) || !valid_sensor_(this->flow_sensor_)) {
    this->minute_.invalid = true;
    return false;
  }
  if (this->minute_.samples != 0U && this->minute_.generation != generation) {
    // Never combine measurements across a performance-map version change.
    this->minute_.invalid = true;
    return false;
  }

  const size_t hp_count = this->topology_ == "duo" ? 2U : 1U;
  uint8_t active_mask = 0U;
  for (size_t index = 0U; index < hp_count; ++index) {
    const auto& source = this->hp_[index];
    auto& runtime = this->hp_runtime_[index];
    if (!valid_sensor_(source.working_mode) || !valid_sensor_(source.frequency) || !valid_binary_(source.defrost) ||
        !valid_binary_(source.oil_return) || !valid_binary_(source.four_way_valve)) {
      this->minute_.invalid = true;
      return false;
    }
    const int mode = static_cast<int>(std::lround(source.working_mode->state));
    const bool active = mode == 2 && source.frequency->state > 0.0f;
    if (mode == 1) {
      this->minute_.invalid = true;  // Heating-only dataset: never sample cooling.
      return false;
    }
    if (!runtime.active_known || runtime.active != active) {
      runtime.active_known = true;
      runtime.active = active;
      runtime.settle_until_s = timestamp_s + START_SETTLE_SECONDS;
    }
    if (source.defrost->state || source.four_way_valve->state) {
      runtime.settle_until_s = std::max(runtime.settle_until_s, timestamp_s + DEFROST_SETTLE_SECONDS);
    }
    if (source.oil_return->state || timestamp_s < runtime.settle_until_s) {
      this->minute_.invalid = true;
      return false;
    }
    if (!active) continue;
    if (!valid_sensor_(source.level) || !valid_sensor_(source.input_power) || !valid_sensor_(source.heat_power) ||
        !valid_sensor_(source.pump_power) || !valid_sensor_(source.water_in) || !valid_sensor_(source.water_out) ||
        !valid_binary_(source.bottom_plate_heater)) {
      this->minute_.invalid = true;
      return false;
    }
    const int level = static_cast<int>(std::lround(source.level->state));
    if (level < 0) {
      this->minute_.invalid = true;
      return false;
    }
    if (runtime.level != level) {
      if (runtime.level >= 0)
        runtime.settle_until_s = std::max(runtime.settle_until_s, timestamp_s + LEVEL_SETTLE_SECONDS);
      runtime.level = level;
    }
    if (timestamp_s < runtime.settle_until_s ||
        !valid_active_measurement(source.input_power->state, source.heat_power->state, source.pump_power->state)) {
      this->minute_.invalid = true;
      return false;
    }
    active_mask |= static_cast<uint8_t>(1U << index);
    auto& hp = this->minute_.hp[index];
    if (this->minute_.samples == 0U) {
      hp.level = level;
      hp.bottom_plate_heater = source.bottom_plate_heater->state;
    } else if (hp.level != level || hp.bottom_plate_heater != source.bottom_plate_heater->state) {
      this->minute_.invalid = true;
      return false;
    }
    hp.frequency_sum += source.frequency->state;
    hp.water_in_sum += source.water_in->state;
    hp.water_out_sum += source.water_out->state;
    hp.electrical_wh += source.input_power->state * static_cast<float>(SAMPLE_SECONDS) / 3600.0f;
    hp.thermal_wh += source.heat_power->state * static_cast<float>(SAMPLE_SECONDS) / 3600.0f;
    hp.pump_wh += source.pump_power->state * static_cast<float>(SAMPLE_SECONDS) / 3600.0f;
  }
  if (active_mask == 0U || (this->minute_.samples != 0U && this->minute_.active_mask != active_mask)) {
    this->minute_.invalid = true;
    return false;
  }
  this->minute_.generation = generation;
  this->minute_.active_mask = active_mask;
  this->minute_.outside_sum += this->outside_temp_sensor_->state;
  this->minute_.flow_sum += this->flow_sensor_->state;
  ++this->minute_.samples;
  return true;
}

void OpenQuattPerformanceTelemetry::finish_minute_() {
  if (this->minute_.start_s == 0U ||
      !stable_minute(this->minute_.samples, this->minute_.invalid, this->minute_.active_mask)) {
    return;
  }
  MinuteRecord record{};
  record.start_s = this->minute_.start_s;
  record.active_mask = this->minute_.active_mask;
  record.generation = this->minute_.generation;
  record.outside_c = this->minute_.outside_sum / 6.0f;
  record.flow_lph = this->minute_.flow_sum / 6.0f;
  record.hp = this->minute_.hp;
  this->append_record_(record);
}

bool OpenQuattPerformanceTelemetry::append_record_(const MinuteRecord& record) {
  if (this->active_record_count_ != 0U && this->records_[0U].generation != record.generation) {
    // Never combine measurements across a performance-map version change.
    this->close_window_(false);
  }
  if (this->active_window_s_ == 0U) this->active_window_s_ = record.start_s;
  if (this->active_record_count_ >= RECORDS_PER_BATCH) return false;
  this->records_[this->active_record_count_++] = record;
  return true;
}

void OpenQuattPerformanceTelemetry::close_window_(bool allow_publish) {
  if (this->active_record_count_ != 0U) {
    if (this->pending_record_count_ == 0U) {
      for (size_t index = 0U; index < this->active_record_count_; ++index) {
        this->records_[RECORDS_PER_BATCH + index] = this->records_[index];
      }
      this->pending_window_s_ = this->active_window_s_;
      this->pending_record_count_ = this->active_record_count_;
      this->pending_batch_id_ = random_uuid_();
      this->pending_publish_allowed_ = allow_publish;
      this->next_retry_ms_ = 0U;
      this->consecutive_failures_ = 0U;
    } else {
      ESP_LOGW(TAG, "Dropping one completed performance window while an earlier batch is retried");
    }
  }
  this->active_window_s_ = 0U;
  this->active_record_count_ = 0U;
}

bool OpenQuattPerformanceTelemetry::build_pending_payload_() {
  if (this->pending_record_count_ == 0U || this->pending_batch_id_.empty() || this->transport_ == nullptr ||
      this->transport_->external_installation_id()[0] == '\0') {
    return false;
  }
  this->payload_.release();
  if (!this->payload_.allocate_external(PAYLOAD_CAPACITY + 1U)) {
    ESP_LOGW(TAG, "Could not allocate performance payload in PSRAM");
    return false;
  }
  const MinuteRecord& first = this->records_[RECORDS_PER_BATCH];
  FixedBufferWriter output(this->payload_.data(), this->payload_.size());
  output += R"({"v":1,"iid":")";
  output += this->transport_->external_installation_id();
  output += R"(","bid":")";
  output += this->pending_batch_id_;
  output += R"(","ws":)";
  output.append_uint(this->pending_window_s_);
  output += R"(,"fw":)";
  append_json_string(output, this->firmware_version_);
  output += R"(,"top":)";
  append_json_string(output, this->topology_);
  output += R"(,"gen":)";
  append_json_string(output, generation_name_(first.generation));
  output += R"(,"map":)";
  append_json_string(output, map_id_(first.generation));
  output += R"(,"m":[)";
  for (size_t index = 0U; index < this->pending_record_count_; ++index) {
    if (index != 0U) output += ',';
    const MinuteRecord& record = this->records_[RECORDS_PER_BATCH + index];
    output += R"({"t":)";
    output.append_uint(record.start_s);
    output += R"(,"a":)";
    output.append_uint(record.active_mask);
    output += R"(,"o":)";
    append_float(output, record.outside_c, 2U);
    output += R"(,"f":)";
    append_float(output, record.flow_lph, 1U);
    output += R"(,"h":[)";
    append_hp(output, record, 0U);
    if (this->topology_ == "duo") {
      output += ',';
      append_hp(output, record, 1U);
    }
    output += "]}";
  }
  output += "]}";
  if (!output.ok()) {
    ESP_LOGW(TAG, "Performance payload exceeded its 4096-byte PSRAM budget");
    this->payload_.release();
    return false;
  }
  return true;
}

void OpenQuattPerformanceTelemetry::try_publish_pending_() {
  if (this->pending_record_count_ == 0U || !this->pending_publish_allowed_ || this->publish_in_flight_.load() ||
      this->transport_ == nullptr || !retry_due(millis(), this->next_retry_ms_)) {
    return;
  }
  if (!this->build_pending_payload_()) {
    this->schedule_retry_();
    return;
  }
  if (this->transport_->request_external_publish("/performance", this->payload_.data(),
                                                 std::strlen(this->payload_.data()))) {
    this->publish_in_flight_.store(true);
    this->payload_.release();
  } else {
    this->payload_.release();
    // A transport cooldown is measured from session start and may end just
    // after the fixed uptime slot. Retry at that boundary (not five minutes
    // later); other failures retain exponential backoff.
    const int64_t now_us = esp_timer_get_time();
    const int64_t allowed_us = this->transport_->external_publish_next_allowed_us();
    if (allowed_us > now_us) {
      const uint64_t delay_ms = static_cast<uint64_t>((allowed_us - now_us + 999) / 1000);
      this->next_retry_ms_ = millis() + static_cast<uint32_t>(std::min<uint64_t>(delay_ms, RETRY_MAX_MS));
    } else {
      this->schedule_retry_();
    }
  }
}

void OpenQuattPerformanceTelemetry::handle_transport_result_() {
  if (this->transport_ == nullptr || !this->publish_in_flight_.load()) return;
  const auto result = this->transport_->take_external_publish_result();
  if (result == openquatt_common::ExternalTelemetryPublishResult::NONE) return;
  this->publish_in_flight_.store(false);
  if (result == openquatt_common::ExternalTelemetryPublishResult::SUCCEEDED) {
    this->clear_pending_();
  } else if (result == openquatt_common::ExternalTelemetryPublishResult::FAILED && this->enabled_.load()) {
    this->schedule_retry_();
  } else {
    this->clear_pending_();
  }
}

void OpenQuattPerformanceTelemetry::schedule_retry_() {
  this->consecutive_failures_ = std::min<uint8_t>(this->consecutive_failures_ + 1U, 8U);
  uint32_t delay_ms = RETRY_MIN_MS;
  for (uint8_t index = 1U; index < this->consecutive_failures_ && delay_ms < RETRY_MAX_MS; ++index) {
    delay_ms = std::min<uint32_t>(delay_ms * 2U, RETRY_MAX_MS);
  }
  this->next_retry_ms_ = millis() + delay_ms;
}

void OpenQuattPerformanceTelemetry::clear_pending_() {
  this->payload_.release();
  this->pending_window_s_ = 0U;
  this->pending_record_count_ = 0U;
  this->pending_batch_id_.clear();
  this->next_retry_ms_ = 0U;
  this->consecutive_failures_ = 0U;
  this->pending_publish_allowed_ = false;
  this->publish_in_flight_.store(false);
}

bool OpenQuattPerformanceTelemetry::valid_sensor_(const sensor::Sensor* value) {
  return value != nullptr && value->has_state() && std::isfinite(value->state);
}

bool OpenQuattPerformanceTelemetry::valid_binary_(const binary_sensor::BinarySensor* value) {
  return value != nullptr && value->has_state();
}

int OpenQuattPerformanceTelemetry::generation_code_(const select::Select* source) {
  if (source == nullptr || !source->has_state()) return 0;
  const std::string& value = source->current_option();
  if (value == "V1") return 1;
  if (value == "V1.5") return 2;
  if (value == "V2") return 3;
  return 0;
}

const char* OpenQuattPerformanceTelemetry::generation_name_(int generation) {
  switch (generation) {
    case 1:
      return "v1";
    case 2:
      return "v1_5";
    case 3:
      return "v2";
    default:
      return "unknown";
  }
}

const char* OpenQuattPerformanceTelemetry::map_id_(int generation) {
  return generation == 3 ? "v2-2026-09-a" : "v1-2026-09-a";
}

std::string OpenQuattPerformanceTelemetry::random_uuid_() {
  std::array<uint8_t, 16U> bytes{};
  esp_fill_random(bytes.data(), bytes.size());
  bytes[6] = static_cast<uint8_t>((bytes[6] & 0x0FU) | 0x40U);
  bytes[8] = static_cast<uint8_t>((bytes[8] & 0x3FU) | 0x80U);
  char value[37];
  std::snprintf(value, sizeof(value), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", bytes[0],
                bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7], bytes[8], bytes[9], bytes[10],
                bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
  return value;
}

}  // namespace esphome::openquatt_performance_telemetry
