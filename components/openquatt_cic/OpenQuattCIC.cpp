#include "OpenQuattCIC.h"

#include <algorithm>
#include <cmath>

#include "esphome/core/application.h"
#include "esphome/core/log.h"

namespace esphome {
namespace openquatt_cic {

static const char* const TAG = "openquatt.cic";

static bool deadline_reached(uint32_t now_ms, uint32_t deadline_ms) {
  return static_cast<int32_t>(now_ms - deadline_ms) >= 0;
}

void OpenQuattCIC::setup() {
  this->backoff_ms_ = this->backoff_start_ms_;
  this->next_ms_ = millis() + this->backoff_start_ms_;
  if (this->url_text_ != nullptr) this->notify_url_changed(this->url_text_->state);
  if (this->fetch_mutex_ == nullptr) {
    this->fetch_mutex_ = xSemaphoreCreateMutex();
  }
  if (!this->response_buffer_.allocate(this->response_buffer_size_)) {
    ESP_LOGE(TAG, "Failed to allocate CIC response buffer");
  }
  this->publish_float_if_changed_(this->backoff_sensor_, this->backoff_start_ms_ / 1000.0f);
  this->publish_float_if_changed_(this->last_success_age_sensor_, NAN);
  this->publish_binary_if_changed_(this->feed_ok_, false);
  this->publish_binary_if_changed_(this->data_stale_, true);
  this->invalidate_feed_signals_();
  this->publish_diagnostics_if_due_(millis(), true);
}

void OpenQuattCIC::update() {
  const uint32_t now_ms = millis();
  if (this->url_text_ == nullptr) {
    this->notify_url_changed(nullptr, 0);
  } else {
    this->notify_url_changed(this->url_text_->state);
  }

  if (this->pause_sensor_ != nullptr && this->pause_sensor_->state) {
    return;
  }

  if (this->enabled_switch_ == nullptr || !this->enabled_switch_->state) {
    this->handle_disabled_();
    this->publish_diagnostics_if_due_(now_ms, false);
    return;
  }

  this->update_runtime_state_(now_ms);

  if (this->url_text_ == nullptr || this->url_text_->state.size() <= this->min_url_len_) {
    this->publish_diagnostics_if_due_(now_ms, false);
    return;
  }

  if (!deadline_reached(now_ms, this->next_ms_)) {
    this->publish_diagnostics_if_due_(now_ms, false);
    return;
  }

  (void)this->start_fetch_(this->url_text_->state);
  this->publish_diagnostics_if_due_(now_ms, false);
}

void OpenQuattCIC::notify_url_changed(const char* url, size_t length) {
  if (!this->url_state_.update(url, length)) return;

  this->last_success_ms_ = 0;
  this->consecutive_errors_ = 0;
  this->backoff_ms_ = this->backoff_start_ms_;
  this->next_ms_ = millis();
  this->publish_binary_if_changed_(this->data_stale_, true);
  this->publish_float_if_changed_(this->last_success_age_sensor_, NAN);
  this->invalidate_feed_signals_();
  this->publish_float_if_changed_(this->backoff_sensor_, this->backoff_start_ms_ / 1000.0f);
  this->publish_diagnostics_if_due_(millis(), true);
}

void OpenQuattCIC::loop() {
  if (this->pause_sensor_ != nullptr && this->pause_sensor_->state) {
    return;
  }

  if (this->fetch_result_ready_.load(std::memory_order_acquire)) this->finalize_fetch_();
}

void OpenQuattCIC::dump_config() {
  ESP_LOGCONFIG(TAG, "OpenQuatt CIC");
  LOG_UPDATE_INTERVAL(this);
  ESP_LOGCONFIG(TAG, "  Timeout: %u ms", static_cast<unsigned>(this->timeout_ms_));
  ESP_LOGCONFIG(TAG, "  Response buffer size: %u B", static_cast<unsigned>(this->response_buffer_size_));
  ESP_LOGCONFIG(TAG, "  Response buffer: %s",
                !this->response_buffer_ ? "missing" : (this->response_buffer_.is_external() ? "PSRAM" : "internal"));
  ESP_LOGCONFIG(TAG, "  Backoff start/max: %u / %u ms", static_cast<unsigned>(this->backoff_start_ms_),
                static_cast<unsigned>(this->backoff_max_ms_));
  ESP_LOGCONFIG(TAG, "  Stale after: %u ms", static_cast<unsigned>(this->stale_after_ms_));
  ESP_LOGCONFIG(TAG, "  Feed error trip: %u", static_cast<unsigned>(this->feed_error_trip_n_));
}

bool OpenQuattCIC::start_fetch_(const std::string& url) {
  if (this->fetch_mutex_ == nullptr) return false;

  if (xSemaphoreTake(this->fetch_mutex_, 0) != pdTRUE) {
    return false;
  }

  if (this->fetch_in_progress_ || this->fetch_result_ready_.load(std::memory_order_relaxed)) {
    xSemaphoreGive(this->fetch_mutex_);
    return false;
  }

  this->fetch_in_progress_ = true;
  this->fetch_result_ = FetchResult{};
  this->fetch_result_ready_.store(false, std::memory_order_relaxed);
  this->fetch_url_ = url;
  this->fetch_url_generation_ = this->url_state_.generation();
  xSemaphoreGive(this->fetch_mutex_);

  const BaseType_t created = xTaskCreate(&OpenQuattCIC::fetch_task_trampoline_, "openquatt_cic", 6144, this,
                                         tskIDLE_PRIORITY + 1, &this->fetch_task_handle_);
  if (created != pdPASS) {
    if (xSemaphoreTake(this->fetch_mutex_, portMAX_DELAY) == pdTRUE) {
      this->fetch_in_progress_ = false;
      this->fetch_result_ = FetchResult{
          .ready = true,
          .ok = false,
          .url_generation = this->fetch_url_generation_,
          .completed_at_ms = millis(),
          .duration_ms = 0,
          .status_code = -1,
          .stack_high_water_mark = 0,
          .error_status = "task_create",
      };
      this->fetch_result_ready_.store(true, std::memory_order_release);
      xSemaphoreGive(this->fetch_mutex_);
    }
    return false;
  }

  return true;
}

void OpenQuattCIC::fetch_task_trampoline_(void* arg) {
  auto* self = static_cast<OpenQuattCIC*>(arg);
  self->fetch_task_();
  vTaskDelete(nullptr);
}

void OpenQuattCIC::fetch_task_() {
  FetchResult result;
  std::string url;

  if (this->fetch_mutex_ != nullptr && xSemaphoreTake(this->fetch_mutex_, portMAX_DELAY) == pdTRUE) {
    url = this->fetch_url_;
    result.url_generation = this->fetch_url_generation_;
    xSemaphoreGive(this->fetch_mutex_);
  }

  (void)this->fetch_and_parse_(url, &result);
  result.stack_high_water_mark = static_cast<uint32_t>(uxTaskGetStackHighWaterMark(nullptr));
  result.ready = true;
  result.completed_at_ms = millis();

  if (this->fetch_mutex_ != nullptr && xSemaphoreTake(this->fetch_mutex_, portMAX_DELAY) == pdTRUE) {
    this->fetch_result_ = result;
    this->fetch_in_progress_ = false;
    this->fetch_task_handle_ = nullptr;
    this->fetch_result_ready_.store(true, std::memory_order_release);
    xSemaphoreGive(this->fetch_mutex_);
  }
}

void OpenQuattCIC::finalize_fetch_() {
  if (this->fetch_mutex_ == nullptr) {
    return;
  }

  FetchResult result;
  if (xSemaphoreTake(this->fetch_mutex_, 0) != pdTRUE) {
    return;
  }

  if (!this->fetch_result_.ready) {
    xSemaphoreGive(this->fetch_mutex_);
    return;
  }

  result = this->fetch_result_;
  this->fetch_result_ = FetchResult{};
  this->fetch_result_ready_.store(false, std::memory_order_release);
  xSemaphoreGive(this->fetch_mutex_);

  if (this->enabled_switch_ == nullptr || !this->enabled_switch_->state) {
    return;
  }

  this->request_count_++;
  this->last_duration_ms_ = result.duration_ms;
  this->last_status_code_ = result.status_code;
  if (result.stack_high_water_mark > 0 &&
      (this->fetch_stack_min_free_ == 0 || result.stack_high_water_mark < this->fetch_stack_min_free_)) {
    this->fetch_stack_min_free_ = result.stack_high_water_mark;
    ESP_LOGI(TAG, "CIC fetch task stack high-watermark: %u B free", static_cast<unsigned>(this->fetch_stack_min_free_));
  }
  if (this->last_duration_ms_ > this->max_duration_ms_) {
    this->max_duration_ms_ = this->last_duration_ms_;
  }

  if (!this->url_state_.accepts(result.url_generation)) {
    ESP_LOGI(TAG, "Discarding CIC response for an outdated feed URL");
    this->update_runtime_state_(millis());
    this->publish_diagnostics_if_due_(millis(), true);
    return;
  }

  if (result.ok) {
    if (!this->url_state_.mark_success(result.url_generation)) return;
    this->apply_payload_(result.payload);
    this->mark_success_(result.completed_at_ms);
  } else {
    if (result.error_status != nullptr) {
      this->status_momentary_error(result.error_status, 1000);
    }
    this->mark_failure_(result.completed_at_ms);
  }

  this->update_runtime_state_(millis());
  this->publish_diagnostics_if_due_(millis(), true);
}

bool OpenQuattCIC::fetch_and_parse_(const std::string& url, FetchResult* result) {
  if (result == nullptr) {
    return false;
  }

  if (!network::is_connected()) {
    result->duration_ms = 0;
    result->status_code = -1;
    result->error_status = "network";
    return false;
  }

  if (!this->response_buffer_) {
    result->duration_ms = 0;
    result->status_code = -1;
    result->error_status = "buffer";
    return false;
  }

  esp_http_client_config_t config = {};
  config.url = url.c_str();
  config.method = HTTP_METHOD_GET;
  config.timeout_ms = this->timeout_ms_;
  config.disable_auto_redirect = true;
  config.buffer_size = 512;
  config.buffer_size_tx = 256;

  const uint32_t start_ms = millis();
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    result->duration_ms = millis() - start_ms;
    result->status_code = -1;
    result->error_status = "http_init";
    return false;
  }

  bool ok = false;
  bool client_open = false;
  uint8_t* response_data = this->response_buffer_.data();
  const size_t response_size = this->response_buffer_.size();

  do {
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "HTTP open failed: %s", esp_err_to_name(err));
      result->error_status = "http_open";
      break;
    }
    client_open = true;

    const int content_length = esp_http_client_fetch_headers(client);

    result->status_code = esp_http_client_get_status_code(client);
    if (result->status_code != 200) {
      ESP_LOGW(TAG, "CIC HTTP status %d", result->status_code);
      result->error_status = "http_status";
      break;
    }

    if (content_length > 0 && static_cast<size_t>(content_length) >= response_size) {
      ESP_LOGW(TAG, "CIC payload %d exceeds buffer %u", content_length, static_cast<unsigned>(response_size));
      result->status_code = -2;
      result->error_status = "http_size";
      break;
    }

    size_t total = 0;
    while (total < response_size) {
      const int read_len =
          esp_http_client_read(client, reinterpret_cast<char*>(response_data + total), response_size - total);

      if (read_len < 0) {
        ESP_LOGW(TAG, "CIC HTTP read failed");
        result->status_code = -3;
        result->error_status = "http_read";
        total = 0;
        break;
      }

      if (read_len == 0) {
        break;
      }
      total += static_cast<size_t>(read_len);
    }

    if (total == 0) {
      break;
    }
    if (total == response_size) {
      ESP_LOGW(TAG, "CIC payload filled the entire buffer");
      result->status_code = -4;
      result->error_status = "http_trunc";
      break;
    }

    ok = this->parse_payload_(response_data, total, &result->payload);
    if (!ok) {
      result->error_status = "json_parse";
    }
  } while (false);

  result->duration_ms = millis() - start_ms;

  if (client_open) {
    esp_http_client_close(client);
  }
  esp_http_client_cleanup(client);

  result->ok = ok;
  return ok;
}

bool OpenQuattCIC::parse_payload_(const uint8_t* data, size_t len, ParsedPayload* payload) {
  if (payload == nullptr) {
    return false;
  }

  JsonDocument doc = json::parse_json(data, len);
  if (doc.overflowed() || doc.isNull()) {
    return false;
  }

  JsonObject root = doc.as<JsonObject>();

  auto maybe_store_float = [&](JsonVariantConst value, float lo, float hi, MaybeFloat* target) {
    if (value.isNull()) {
      return;
    }
    if (!value.is<float>() && !value.is<int>()) {
      return;
    }
    const float x = value.as<float>();
    if (std::isnan(x) || x < lo || x > hi) {
      return;
    }
    target->present = true;
    target->value = x;
  };
  auto maybe_store_bool = [&](JsonVariantConst value, MaybeBool* target) {
    if (value.isNull() || !value.is<bool>()) {
      return;
    }
    target->present = true;
    target->value = value.as<bool>();
  };

  if (root["flowMeter"]) {
    JsonObject flow_meter = root["flowMeter"];
    maybe_store_float(flow_meter["waterSupplyTemperature"], -20.0f, 90.0f, &payload->water_supply_temp);
  }

  if (root["qc"]) {
    JsonObject qc = root["qc"];
    maybe_store_float(qc["flowRateFiltered"], 0.0f, 5000.0f, &payload->flow_rate);
  }

  if (root["boiler"]) {
    JsonObject boiler = root["boiler"];
    maybe_store_float(boiler["otFbWaterPressure"], 0.0f, 10.0f, &payload->cic_boiler_water_pressure);
  }

  if (root["thermostat"]) {
    JsonObject thermostat = root["thermostat"];
    maybe_store_bool(thermostat["otFtChEnabled"], &payload->cic_ch_enabled);
    maybe_store_bool(thermostat["otFtCoolingEnabled"], &payload->cic_cooling_enabled);
    maybe_store_float(thermostat["otFtControlSetpoint"], 5.0f, 80.0f, &payload->cic_control_setpoint);
    maybe_store_float(thermostat["otFtRoomSetpoint"], 5.0f, 35.0f, &payload->cic_room_setpoint);
    maybe_store_float(thermostat["otFtRoomTemperature"], -20.0f, 50.0f, &payload->cic_room_temp);
  }

  return true;
}

void OpenQuattCIC::apply_payload_(const ParsedPayload& payload) {
  if (payload.water_supply_temp.present) {
    this->publish_float_if_changed_(this->water_supply_temp_, payload.water_supply_temp.value);
  } else {
    // A successful payload without this optional field must not keep exposing
    // the previous value as a fresh CIC supply-temperature sample.
    this->publish_float_if_changed_(this->water_supply_temp_, NAN);
  }
  if (payload.flow_rate.present) {
    this->publish_float_if_changed_(this->flow_rate_, payload.flow_rate.value);
  }
  if (payload.cic_boiler_water_pressure.present) {
    this->publish_float_if_changed_(this->cic_boiler_water_pressure_, payload.cic_boiler_water_pressure.value);
  } else {
    // Boiler OpenTherm feedback is optional in the CiC feed. Never keep a
    // previous pressure value when a fresh payload no longer contains it.
    this->publish_float_if_changed_(this->cic_boiler_water_pressure_, NAN);
  }
  if (payload.cic_control_setpoint.present) {
    this->publish_float_if_changed_(this->cic_control_setpoint_, payload.cic_control_setpoint.value);
  }
  if (payload.cic_room_setpoint.present) {
    this->publish_float_if_changed_(this->cic_room_setpoint_, payload.cic_room_setpoint.value);
  }
  if (payload.cic_room_temp.present) {
    this->publish_float_if_changed_(this->cic_room_temp_, payload.cic_room_temp.value);
  }
  if (payload.cic_ch_enabled.present) {
    this->publish_binary_if_changed_(this->cic_ch_enabled_, payload.cic_ch_enabled.value);
  }
  this->publish_binary_if_changed_(this->cic_ch_enable_valid_, payload.cic_ch_enabled.present);
  if (payload.cic_cooling_enabled.present) {
    this->publish_binary_if_changed_(this->cic_cooling_enabled_, payload.cic_cooling_enabled.value);
  }
}

void OpenQuattCIC::mark_success_(uint32_t now_ms) {
  this->success_count_++;
  this->consecutive_errors_ = 0;
  this->last_success_ms_ = now_ms;
  this->backoff_ms_ = this->backoff_start_ms_;
  this->next_ms_ = now_ms + this->backoff_ms_;
  this->publish_binary_if_changed_(this->feed_ok_, true);
  this->publish_binary_if_changed_(this->data_stale_, false);
  this->publish_float_if_changed_(this->backoff_sensor_, this->backoff_ms_ / 1000.0f);
  this->publish_float_if_changed_(this->last_success_age_sensor_, 0.0f);
}

void OpenQuattCIC::mark_failure_(uint32_t now_ms) {
  this->failure_count_++;
  this->consecutive_errors_++;
  if (this->consecutive_errors_ >= this->feed_error_trip_n_) {
    this->publish_binary_if_changed_(this->feed_ok_, false);
  }

  uint32_t next_backoff_ms = this->backoff_ms_;
  if (next_backoff_ms < this->backoff_max_ms_) {
    next_backoff_ms = std::min(next_backoff_ms * 2U, this->backoff_max_ms_);
  }
  this->backoff_ms_ = next_backoff_ms;
  this->next_ms_ = now_ms + next_backoff_ms;
  this->publish_float_if_changed_(this->backoff_sensor_, next_backoff_ms / 1000.0f);
}

void OpenQuattCIC::update_runtime_state_(uint32_t now_ms) {
  const bool has_success = this->last_success_ms_ != 0;
  const bool stale = !has_success || static_cast<int32_t>(now_ms - this->last_success_ms_) >=
                                         static_cast<int32_t>(this->stale_after_ms_);

  if (has_success) {
    this->publish_float_if_changed_(this->last_success_age_sensor_, (now_ms - this->last_success_ms_) / 1000.0f);
  } else {
    this->publish_float_if_changed_(this->last_success_age_sensor_, NAN);
  }

  this->publish_binary_if_changed_(this->data_stale_, stale);
  if (stale) {
    this->invalidate_feed_signals_();
  }
}

void OpenQuattCIC::handle_disabled_() {
  this->publish_binary_if_changed_(this->data_stale_, true);
  this->publish_float_if_changed_(this->last_success_age_sensor_, NAN);
  this->invalidate_feed_signals_();
}

void OpenQuattCIC::invalidate_feed_signals_() {
  this->publish_binary_if_changed_(this->feed_ok_, false);
  this->publish_float_if_changed_(this->water_supply_temp_, NAN);
  this->publish_float_if_changed_(this->flow_rate_, NAN);
  this->publish_float_if_changed_(this->cic_boiler_water_pressure_, NAN);
  this->publish_float_if_changed_(this->cic_control_setpoint_, NAN);
  this->publish_float_if_changed_(this->cic_room_setpoint_, NAN);
  this->publish_float_if_changed_(this->cic_room_temp_, NAN);
  this->publish_binary_if_changed_(this->cic_ch_enabled_, false);
  this->publish_binary_if_changed_(this->cic_ch_enable_valid_, false);
  this->publish_binary_if_changed_(this->cic_cooling_enabled_, false);
}

void OpenQuattCIC::publish_diagnostics_if_due_(uint32_t now_ms, bool force) {
  if (!force && this->diagnostic_publish_interval_ms_ > 0 &&
      !deadline_reached(now_ms, this->last_diag_publish_ms_ + this->diagnostic_publish_interval_ms_)) {
    return;
  }
  this->last_diag_publish_ms_ = now_ms;
  this->publish_float_if_changed_(this->request_count_sensor_, static_cast<float>(this->request_count_));
  this->publish_float_if_changed_(this->success_count_sensor_, static_cast<float>(this->success_count_));
  this->publish_float_if_changed_(this->failure_count_sensor_, static_cast<float>(this->failure_count_));
  this->publish_float_if_changed_(this->last_duration_sensor_, static_cast<float>(this->last_duration_ms_));
  this->publish_float_if_changed_(this->max_duration_sensor_, static_cast<float>(this->max_duration_ms_));
  this->publish_float_if_changed_(this->last_status_code_sensor_, static_cast<float>(this->last_status_code_));
}

void OpenQuattCIC::publish_float_if_changed_(sensor::Sensor* sensor, float value) {
  if (sensor == nullptr) {
    return;
  }
  const float current = sensor->state;
  const bool current_nan = std::isnan(current);
  const bool value_nan = std::isnan(value);
  if (current_nan != value_nan || (!value_nan && std::fabs(current - value) > 0.001f)) {
    sensor->publish_state(value);
  }
}

void OpenQuattCIC::publish_binary_if_changed_(binary_sensor::BinarySensor* binary_sensor, bool value) {
  if (binary_sensor == nullptr) {
    return;
  }
  if (binary_sensor->state != value) {
    binary_sensor->publish_state(value);
  }
}

}  // namespace openquatt_cic
}  // namespace esphome
