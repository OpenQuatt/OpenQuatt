#pragma once

#include "esphome/core/component.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/openquatt_web_auth/OpenQuattWebAuth.h"
#include <mutex>
#include "PsramBuffer.h"
#include "esp_partition.h"
#include "MeasurementArchive.h"
#include "includes/learning/oq_ph_learning_source_logic.h"
#include "includes/learning/oq_ph_passive_runtime_logic.h"
#include "esphome/components/openquatt_weather/OpenQuattWeatherLogic.h"

namespace esphome::openquatt_measurement_archive {

class MeasurementArchive;
class ArchiveEnabledSwitch : public switch_::Switch {
 public:
  void set_parent(MeasurementArchive* archive) { archive_ = archive; }

 protected:
  void write_state(bool enabled) override;
  MeasurementArchive* archive_ = nullptr;
};

class MeasurementArchive : public Component, public oq_measurement_archive::Flash {
 public:
  void set_status(text_sensor::TextSensor* status) { status_ = status; }
  void set_web_auth(openquatt_web_auth::OpenQuattWebAuth* auth) { web_auth_ = auth; }
  void set_switch(ArchiveEnabledSwitch* enabled_switch) {
    enabled_switch_ = enabled_switch;
    enabled_switch_->set_parent(this);
  }
  void set_enabled(bool enabled);
  void setup() override;
  void setup_endpoint();
  void download(httpd_req_t* request, const char* date);
  openquatt_web_auth::OpenQuattWebAuth* web_auth() const { return web_auth_; }
  void dump_config() override;
  void observe(const oq_power_house::learning::LearningSourceInput& input,
               const oq_power_house::learning::CalorimetryResult& heat,
               const oq_power_house::learning::PassiveContextView& context, uint32_t cic_flow_endpoint,
               float supply_offset_c);
  void weather(const oq_weather::Series& series);
  bool healthy() const { return state_ && state_[0].healthy; }
  size_t measurements() const { return index_ready_ ? state_[0].measurements.count() : 0; }
  size_t forecasts() const { return index_ready_ ? state_[0].weather.count() : 0; }
  bool read(uint32_t offset, void* data, size_t size) override;
  bool write(uint32_t offset, const void* data, size_t size) override;
  bool erase(uint32_t offset) override;

 protected:
  struct State {
    oq_measurement_archive::Measurements measurements;
    oq_measurement_archive::WeatherHistory weather;
    oq_measurement_archive::Contexts contexts;
    oq_measurement_archive::Collector collector;
    oq_measurement_archive::SectorIndex measurement_index[oq_measurement_archive::kMeasurementSectors];
    oq_measurement_archive::SectorIndex weather_index[oq_measurement_archive::kWeatherSectors];
    uint8_t scratch[oq_measurement_archive::kSectorBytes];
    uint8_t context_bytes[sizeof(oq_measurement_archive::Context::bytes)]{};
    size_t context_size = 0;
    uint32_t flow_generation = 0, context = 0, source_revision = 0;
    uint32_t last_weather_epoch = 0;
    bool healthy = false, new_session = true;
  };
  openquatt_common::PsramObjectArray<State, 1> state_;
  const esp_partition_t* partition_ = nullptr;
  text_sensor::TextSensor* status_ = nullptr;
  ArchiveEnabledSwitch* enabled_switch_ = nullptr;
  openquatt_web_auth::OpenQuattWebAuth* web_auth_ = nullptr;
  // Index snapshots take this mutex for a bounded copy. Flash reads and
  // HTTP transmission after the copy never lock the main loop.
  std::mutex index_mutex_;
  bool enabled_ = false, stored_enabled_ = false, preference_error_ = false;
  bool index_ready_ = false, orphaned_ = false;
  void publish_(const char* code);
};

}  // namespace esphome::openquatt_measurement_archive
