#pragma once

#ifdef USE_OPENQUATT_WEATHER
#endif

#include <atomic>
#include "esphome/core/component.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text/text.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/components/ota/ota_backend.h"
#include "PsramBuffer.h"
#include "OpenQuattWeatherLogic.h"
#include "OpenQuattWeatherLocation.h"
#include "OpenQuattWeatherHttp.h"
#include "OpenQuattWeatherPolicy.h"
#include "WeatherTextSnapshot.h"
#include "WeatherForecastSnapshot.h"
#include "includes/learning/oq_ph_forecast_logic.h"
#include "lwip/ip_addr.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace esphome::openquatt_web_auth {
class OpenQuattWebAuth;
}

namespace esphome::openquatt_weather {

class OpenQuattWeather;
class WeatherLocationText : public text::Text {
 public:
  void set_parent(OpenQuattWeather* parent) { parent_ = parent; }

 protected:
  void control(const std::string& value) override;
  OpenQuattWeather* parent_ = nullptr;
};

class OpenQuattWeather : public PollingComponent, public ota::OTAGlobalStateListener {
 public:
  void set_clock(time::RealTimeClock* clock) { clock_ = clock; }
  void set_web_auth(openquatt_web_auth::OpenQuattWebAuth* auth) { web_auth_ = auth; }
  openquatt_web_auth::OpenQuattWebAuth* web_auth() const { return web_auth_; }
  ForecastSnapshotAccess& forecast_endpoint() { return forecast_snapshots_; }
  void set_enabled_switch(switch_::Switch* entity) { enabled_ = entity; }
  void set_pause_sensor(binary_sensor::BinarySensor* entity) { pause_ = entity; }
  void set_location_text(WeatherLocationText* entity) {
    location_text_ = entity;
    entity->set_parent(this);
  }
  void set_status(text_sensor::TextSensor* entity) { status_ = entity; }
  void set_resolved_location(text_sensor::TextSensor* entity) { resolved_ = entity; }
  void set_forecast_age(sensor::Sensor* entity) { age_ = entity; }
  void set_issued_epoch(text_sensor::TextSensor* entity) { issued_epoch_ = entity; }
  void set_request_count(sensor::Sensor* entity) { requests_ = entity; }
  void set_failure_count(sensor::Sensor* entity) { failures_ = entity; }
  void set_worker_stack_free(sensor::Sensor* entity) { stack_free_ = entity; }
  void set_last_duration(sensor::Sensor* entity) { duration_ = entity; }
  void set_temperature_30m(sensor::Sensor* entity) { temperatures_[0] = entity; }
  void set_temperature_1h(sensor::Sensor* entity) { temperatures_[1] = entity; }
  void set_temperature_3h(sensor::Sensor* entity) { temperatures_[2] = entity; }
  void setup() override;
  void update() override;
  void loop() override;
  void dump_config() override;
  void on_shutdown() override;
  void on_ota_global_state(ota::OTAState state, float, uint8_t, ota::OTAComponent*) override;
  void observe(const oq_power_house::forecast::Observation& observation);
  void set_location_query(const std::string& query, const std::string& request = {});
  bool worker_busy() const { return phase_.load(std::memory_order_acquire) != oq_weather::MailboxPhase::IDLE; }
  uint32_t cancelled_requests() const { return cancelled_requests_; }
  uint32_t worker_largest_low() const { return worker_largest_low_; }
  uint32_t forecast_samples(size_t horizon) const {
    return storage_ && horizon < 3 ? storage_[0].forecast.errors[horizon].issued.count : 0;
  }
  double forecast_replay_mae(size_t horizon) const {
    return storage_ && horizon < 3 ? storage_[0].forecast.errors[horizon].actual_inputs.mae() : NAN;
  }

 protected:
  enum class Job : uint8_t { SAVE_LOCATION, RESOLVE_LOCATION, FETCH };
  struct Storage {
    oq_weather::Series current, candidate;
    oq_power_house::forecast::State forecast;
    IssuedScenario issued_scenario;
    oq_weather::Location location, pending_location, request_location, result_location;
    oq_weather::HttpResponse http;
    alignas(std::max_align_t) uint8_t json_arena[8192]{};
    char response[4096]{}, path[384]{}, request_bytes[768]{}, tls_chunk[512]{}, resolved_name[65]{};
    char pending_save_request[33]{};  // Main-loop-owned; never read by worker.
    ForecastSnapshot public_snapshot, request_snapshot;
    Job job = Job::FETCH;
    uint32_t request_epoch = 0, request_generation = 0, request_configuration = 0;
    uint64_t request_ms = 0;
    bool result_ok = false;
    const char* error = "none";
    uint32_t duration_ms = 0, stack_free = 0;
    uint32_t internal_largest_low = 0;
  };
  static_assert(sizeof(Storage) <= 21U * 1024U, "weather data must fit its strict PSRAM budget");
  // Worker owns only request/result/parser fields while REQUESTED. Main loop
  // always owns location/current/forecast and copies results only after READY.
  openquatt_common::PsramObjectArray<Storage, 1> storage_;
  std::atomic<oq_weather::MailboxPhase> phase_{oq_weather::MailboxPhase::IDLE};
  std::atomic<bool> cancel_{false};
  std::atomic<oq_weather::WorkGate> work_gate_{oq_weather::WorkGate::CANCELLED};
  TaskHandle_t worker_ = nullptr;
  // DNS callbacks target this permanent component, never request-local memory.
  // An abandoned lookup keeps its one slot until its callback completes.
  std::atomic<const char*> dns_hostname_{nullptr};
  std::atomic<bool> dns_done_{false};
  std::atomic<uint32_t> dns_address_{0};
  bool dns_outstanding_ = false;  // Worker-only; callbacks publish dns_done_.
  time::RealTimeClock* clock_ = nullptr;
  openquatt_web_auth::OpenQuattWebAuth* web_auth_ = nullptr;
  switch_::Switch* enabled_ = nullptr;
  binary_sensor::BinarySensor* pause_ = nullptr;
  WeatherLocationText* location_text_ = nullptr;
  text_sensor::TextSensor* status_ = nullptr;
  text_sensor::TextSensor* resolved_ = nullptr;
  sensor::Sensor* age_ = nullptr;
  text_sensor::TextSensor* issued_epoch_ = nullptr;
  sensor::Sensor* requests_ = nullptr;
  sensor::Sensor* failures_ = nullptr;
  sensor::Sensor* stack_free_ = nullptr;
  sensor::Sensor* duration_ = nullptr;
  sensor::Sensor* temperatures_[3]{};
  oq_weather::OtaPause ota_pause_;
  WeatherTextSnapshots text_snapshots_;
  ForecastSnapshotAccess forecast_snapshots_;
  uint32_t generation_ = 1, configuration_ = 1, accepted_serial_ = 0;
  uint64_t next_fetch_ms_ = 0;
  uint32_t backoff_ms_ = 60000, last_report_ms_ = 0;
  uint32_t request_count_ = 0, failure_count_ = 0, worker_stack_min_ = 0, last_duration_ms_ = 0;
  uint32_t cancelled_requests_ = 0, worker_largest_low_ = 0;
  bool active_ = false, ota_paused_ = false, worker_attempted_ = false;
  bool pending_save_ = false, location_resolved_ = false, configuration_error_ = false;
  const char* last_error_ = "none";
  bool allowed_() const;
  void invalidate_();
  void request_(Job job);
  void publish_();
  void publish_forecast_();
  void setup_forecast_endpoint_();
  void publish_location_(const char* value, const char* request = "", SaveState state = SaveState::NONE);
  void publish_text_(text_sensor::TextSensor* entity, const char* value);
  void load_location_();
  bool save_location_();
  bool fetch_();
  bool cancelled_() const;
  bool resolve_dns_(const char* hostname, uint32_t& address);
  void sample_transport_memory_();
  static void dns_start_(void* argument);
  static void dns_complete_(const char*, const ip_addr_t* address, void* argument);
  static void worker_entry_(void* argument);
};

}  // namespace esphome::openquatt_weather
