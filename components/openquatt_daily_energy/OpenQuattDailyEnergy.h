#pragma once

#include "../openquatt_energy_history/OpenQuattEnergyHistory.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"

#include "oq_daily_energy_state.h"

namespace esphome::openquatt_daily_energy {
class OpenQuattDailyEnergy : public Component {
 public:
  void set_clock(time::RealTimeClock* clock) { clock_ = clock; }
  void set_archive(openquatt_energy_history::OpenQuattEnergyHistory* archive) { archive_ = archive; }
  void set_channel(size_t index, sensor::Sensor* power, sensor::Sensor* total) {
    powers_[index] = power;
    outputs_[index] = total;
  }
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }
  void setup() override;
  void capture_history();

 private:
  bool prepare_(const ESPTime& now, uint32_t now_ms);
  void on_power_(size_t index, float watts);

  std::array<sensor::Sensor*, CHANNEL_COUNT> powers_{};
  std::array<sensor::Sensor*, CHANNEL_COUNT> outputs_{};
  time::RealTimeClock* clock_{};
  openquatt_energy_history::OpenQuattEnergyHistory* archive_{};
  ESPPreferenceObject pref_;
  DatedTotals saved_{};
  DailyEnergyState state_;
};
}  // namespace esphome::openquatt_daily_energy
