#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include "esphome.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/json/json_util.h"
#include "esphome/components/network/util.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text/text.h"
#include "esp_http_client.h"
#include "PsramBuffer.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "OpenQuattCICUrlState.h"

namespace esphome {
namespace openquatt_cic {

using openquatt_common::PsramBuffer;

class OpenQuattCIC : public PollingComponent {
 public:
  void set_enabled_switch(switch_::Switch* enabled_switch) { this->enabled_switch_ = enabled_switch; }
  void set_url_text(text::Text* url_text) { this->url_text_ = url_text; }

  void set_backoff_start_ms(uint32_t backoff_start_ms) { this->backoff_start_ms_ = backoff_start_ms; }
  void set_backoff_max_ms(uint32_t backoff_max_ms) { this->backoff_max_ms_ = backoff_max_ms; }
  void set_min_url_len(uint32_t min_url_len) { this->min_url_len_ = min_url_len; }
  void set_stale_after_ms(uint32_t stale_after_ms) { this->stale_after_ms_ = stale_after_ms; }
  void set_feed_error_trip_n(uint32_t feed_error_trip_n) { this->feed_error_trip_n_ = feed_error_trip_n; }
  void set_timeout_ms(uint32_t timeout_ms) { this->timeout_ms_ = timeout_ms; }
  void set_response_buffer_size(size_t response_buffer_size) { this->response_buffer_size_ = response_buffer_size; }
  void set_diagnostic_publish_interval_ms(uint32_t interval_ms) { this->diagnostic_publish_interval_ms_ = interval_ms; }

  void set_water_supply_temp(sensor::Sensor* sensor) { this->water_supply_temp_ = sensor; }
  void set_flow_rate(sensor::Sensor* sensor) { this->flow_rate_ = sensor; }
  void set_cic_boiler_water_pressure(sensor::Sensor* sensor) { this->cic_boiler_water_pressure_ = sensor; }
  void set_cic_control_setpoint(sensor::Sensor* sensor) { this->cic_control_setpoint_ = sensor; }
  void set_cic_room_setpoint(sensor::Sensor* sensor) { this->cic_room_setpoint_ = sensor; }
  void set_cic_room_temp(sensor::Sensor* sensor) { this->cic_room_temp_ = sensor; }
  void set_cic_ch_enabled(binary_sensor::BinarySensor* binary_sensor) { this->cic_ch_enabled_ = binary_sensor; }
  void set_cic_ch_enable_valid(binary_sensor::BinarySensor* binary_sensor) {
    this->cic_ch_enable_valid_ = binary_sensor;
  }
  void set_cic_cooling_enabled(binary_sensor::BinarySensor* binary_sensor) {
    this->cic_cooling_enabled_ = binary_sensor;
  }
  void set_feed_ok(binary_sensor::BinarySensor* binary_sensor) { this->feed_ok_ = binary_sensor; }
  void set_data_stale(binary_sensor::BinarySensor* binary_sensor) { this->data_stale_ = binary_sensor; }
  void set_pause_sensor(binary_sensor::BinarySensor* binary_sensor) { this->pause_sensor_ = binary_sensor; }
  void set_backoff_sensor(sensor::Sensor* sensor) { this->backoff_sensor_ = sensor; }
  void set_last_success_age_sensor(sensor::Sensor* sensor) { this->last_success_age_sensor_ = sensor; }
  void set_request_count_sensor(sensor::Sensor* sensor) { this->request_count_sensor_ = sensor; }
  void set_success_count_sensor(sensor::Sensor* sensor) { this->success_count_sensor_ = sensor; }
  void set_failure_count_sensor(sensor::Sensor* sensor) { this->failure_count_sensor_ = sensor; }
  void set_last_duration_sensor(sensor::Sensor* sensor) { this->last_duration_sensor_ = sensor; }
  void set_max_duration_sensor(sensor::Sensor* sensor) { this->max_duration_sensor_ = sensor; }
  void set_last_status_code_sensor(sensor::Sensor* sensor) { this->last_status_code_sensor_ = sensor; }

  void setup() override;
  void update() override;
  void loop() override;
  void dump_config() override;

  void notify_url_changed(const std::string& url) { this->notify_url_changed(url.data(), url.size()); }
  bool is_url_ready(const std::string& url) const { return this->url_state_.ready(url.data(), url.size()); }

 protected:
  struct MaybeFloat {
    bool present{false};
    float value{NAN};
  };

  struct MaybeBool {
    bool present{false};
    bool value{false};
  };

  struct ParsedPayload {
    MaybeFloat water_supply_temp;
    MaybeFloat flow_rate;
    MaybeFloat cic_boiler_water_pressure;
    MaybeFloat cic_control_setpoint;
    MaybeFloat cic_room_setpoint;
    MaybeFloat cic_room_temp;
    MaybeBool cic_ch_enabled;
    MaybeBool cic_cooling_enabled;
  };

  struct FetchResult {
    bool ready{false};
    bool ok{false};
    uint32_t url_generation{0};
    uint32_t completed_at_ms{0};
    uint32_t duration_ms{0};
    int status_code{0};
    uint32_t stack_high_water_mark{0};
    const char* error_status{nullptr};
    ParsedPayload payload;
  };

  bool start_fetch_(const std::string& url);
  static void fetch_task_trampoline_(void* arg);
  void fetch_task_();
  void finalize_fetch_();
  bool fetch_and_parse_(const std::string& url, FetchResult* result);
  bool parse_payload_(const uint8_t* data, size_t len, ParsedPayload* payload);
  void apply_payload_(const ParsedPayload& payload);
  void mark_success_(uint32_t now_ms);
  void mark_failure_(uint32_t now_ms);
  void update_runtime_state_(uint32_t now_ms);
  void notify_url_changed(const char* url, size_t length);
  void handle_disabled_();
  void invalidate_feed_signals_();
  void publish_diagnostics_if_due_(uint32_t now_ms, bool force);

  void publish_float_if_changed_(sensor::Sensor* sensor, float value);
  void publish_binary_if_changed_(binary_sensor::BinarySensor* binary_sensor, bool value);

  switch_::Switch* enabled_switch_{nullptr};
  text::Text* url_text_{nullptr};

  sensor::Sensor* water_supply_temp_{nullptr};
  sensor::Sensor* flow_rate_{nullptr};
  sensor::Sensor* cic_boiler_water_pressure_{nullptr};
  sensor::Sensor* cic_control_setpoint_{nullptr};
  sensor::Sensor* cic_room_setpoint_{nullptr};
  sensor::Sensor* cic_room_temp_{nullptr};
  binary_sensor::BinarySensor* cic_ch_enabled_{nullptr};
  binary_sensor::BinarySensor* cic_ch_enable_valid_{nullptr};
  binary_sensor::BinarySensor* cic_cooling_enabled_{nullptr};
  binary_sensor::BinarySensor* feed_ok_{nullptr};
  binary_sensor::BinarySensor* data_stale_{nullptr};
  binary_sensor::BinarySensor* pause_sensor_{nullptr};
  sensor::Sensor* backoff_sensor_{nullptr};
  sensor::Sensor* last_success_age_sensor_{nullptr};
  sensor::Sensor* request_count_sensor_{nullptr};
  sensor::Sensor* success_count_sensor_{nullptr};
  sensor::Sensor* failure_count_sensor_{nullptr};
  sensor::Sensor* last_duration_sensor_{nullptr};
  sensor::Sensor* max_duration_sensor_{nullptr};
  sensor::Sensor* last_status_code_sensor_{nullptr};

  uint32_t backoff_start_ms_{5000};
  uint32_t backoff_max_ms_{120000};
  uint32_t min_url_len_{10};
  uint32_t stale_after_ms_{600000};
  uint32_t feed_error_trip_n_{3};
  uint32_t timeout_ms_{2000};
  size_t response_buffer_size_{2048};
  uint32_t diagnostic_publish_interval_ms_{60000};

  uint32_t next_ms_{0};
  uint32_t backoff_ms_{5000};
  uint32_t last_success_ms_{0};
  uint32_t consecutive_errors_{0};
  uint32_t request_count_{0};
  uint32_t success_count_{0};
  uint32_t failure_count_{0};
  uint32_t last_duration_ms_{0};
  uint32_t max_duration_ms_{0};
  int last_status_code_{0};
  uint32_t last_diag_publish_ms_{0};
  uint32_t fetch_stack_min_free_{0};

  SemaphoreHandle_t fetch_mutex_{nullptr};
  TaskHandle_t fetch_task_handle_{nullptr};
  bool fetch_in_progress_{false};
  std::string fetch_url_{};
  uint32_t fetch_url_generation_{0};
  FetchResult fetch_result_{};
  std::atomic<bool> fetch_result_ready_{false};
  OpenQuattCICUrlState url_state_{};
  PsramBuffer<uint8_t> response_buffer_{};
};

}  // namespace openquatt_cic
}  // namespace esphome
