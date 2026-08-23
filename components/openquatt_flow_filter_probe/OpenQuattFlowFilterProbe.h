#pragma once

#include <cstdint>

#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"

namespace esphome::openquatt_flow_filter_probe {

class OpenQuattFlowFilterProbe : public sensor::Sensor, public Component {
 public:
  void set_pin(InternalGPIOPin* pin) { this->pin_ = pin; }
  void set_edge_100_sensor(sensor::Sensor* sensor) { this->edge_100_sensor_ = sensor; }
  void set_pulse_13_sensor(sensor::Sensor* sensor) { this->pulse_13_sensor_ = sensor; }
  void set_pulse_20_sensor(sensor::Sensor* sensor) { this->pulse_20_sensor_ = sensor; }
  void set_pulse_50_sensor(sensor::Sensor* sensor) { this->pulse_50_sensor_ = sensor; }
  void set_raw_rising_hz_sensor(sensor::Sensor* sensor) { this->raw_rising_hz_sensor_ = sensor; }
  void set_raw_rising_count_sensor(sensor::Sensor* sensor) { this->raw_rising_count_sensor_ = sensor; }
  void set_pulse_width_min_sensor(sensor::Sensor* sensor) { this->pulse_width_min_sensor_ = sensor; }
  void set_pulse_width_avg_sensor(sensor::Sensor* sensor) { this->pulse_width_avg_sensor_ = sensor; }
  void set_pulse_width_max_sensor(sensor::Sensor* sensor) { this->pulse_width_max_sensor_ = sensor; }
  void set_pulse_width_lt20_sensor(sensor::Sensor* sensor) { this->pulse_width_lt20_sensor_ = sensor; }
  void set_pulse_width_20_50_sensor(sensor::Sensor* sensor) { this->pulse_width_20_50_sensor_ = sensor; }
  void set_pulse_width_50_100_sensor(sensor::Sensor* sensor) { this->pulse_width_50_100_sensor_ = sensor; }
  void set_pulse_width_ge100_sensor(sensor::Sensor* sensor) { this->pulse_width_ge100_sensor_ = sensor; }
  void set_kf_lpm_per_hz(float value) { this->kf_lpm_per_hz_ = value; }
  void set_q0_lpm(float value) { this->q0_lpm_ = value; }

  void setup() override;
  void loop() override;
  void dump_config() override;

 protected:
  struct ChannelState {
    uint32_t last_detected_edge_us{0};
    uint32_t last_rising_edge_us{0};
    uint32_t count{0};
  };

  struct PulseFilterState {
    uint32_t last_intr_us{0};
    bool latched{false};
  };

  struct RuntimeState {
    uint8_t meter_state{0};  // 0=initial, 1=running, 2=timed out
    bool peeked_edge{false};
    uint32_t last_processed_edge_us{0};
  };

  struct WidthStats {
    uint32_t count{0};
    uint64_t sum_us{0};
    uint32_t min_us{UINT32_MAX};
    uint32_t max_us{0};
    uint32_t lt20{0};
    uint32_t from20_to50{0};
    uint32_t from50_to100{0};
    uint32_t ge100{0};
  };

  static void IRAM_ATTR gpio_intr(OpenQuattFlowFilterProbe* probe);
  static void IRAM_ATTR apply_pulse_filter_(volatile ChannelState& channel, volatile PulseFilterState& filter,
                                            uint32_t filter_us, uint32_t now_us, bool pin_val, bool previous_pin_val);
  void process_channel_(sensor::Sensor* sensor, ChannelState state, RuntimeState& runtime, uint32_t filter_us,
                        bool pulse_mode, uint32_t now_us);
  void publish_raw_diagnostics_(uint32_t now_us);
  float pulses_per_minute_to_lph_(float ppm) const;

  InternalGPIOPin* pin_{nullptr};
  ISRInternalGPIOPin isr_pin_;
  sensor::Sensor* edge_100_sensor_{nullptr};
  sensor::Sensor* pulse_13_sensor_{nullptr};
  sensor::Sensor* pulse_20_sensor_{nullptr};
  sensor::Sensor* pulse_50_sensor_{nullptr};
  sensor::Sensor* raw_rising_hz_sensor_{nullptr};
  sensor::Sensor* raw_rising_count_sensor_{nullptr};
  sensor::Sensor* pulse_width_min_sensor_{nullptr};
  sensor::Sensor* pulse_width_avg_sensor_{nullptr};
  sensor::Sensor* pulse_width_max_sensor_{nullptr};
  sensor::Sensor* pulse_width_lt20_sensor_{nullptr};
  sensor::Sensor* pulse_width_20_50_sensor_{nullptr};
  sensor::Sensor* pulse_width_50_100_sensor_{nullptr};
  sensor::Sensor* pulse_width_ge100_sensor_{nullptr};

  float kf_lpm_per_hz_{0.05f};
  float q0_lpm_{0.0f};
  bool last_pin_val_{false};

  volatile ChannelState edge_100_state_{};
  volatile ChannelState pulse_13_state_{};
  volatile ChannelState pulse_20_state_{};
  volatile ChannelState pulse_50_state_{};
  volatile ChannelState pulse_100_state_{};
  volatile PulseFilterState pulse_13_filter_{};
  volatile PulseFilterState pulse_20_filter_{};
  volatile PulseFilterState pulse_50_filter_{};
  volatile PulseFilterState pulse_100_filter_{};
  volatile uint32_t edge_100_last_sent_us_{0};

  volatile uint32_t raw_rising_count_window_{0};
  volatile uint32_t raw_high_started_us_{0};
  volatile WidthStats width_stats_{};
  uint32_t diagnostics_window_started_us_{0};

  RuntimeState edge_100_runtime_{};
  RuntimeState pulse_13_runtime_{};
  RuntimeState pulse_20_runtime_{};
  RuntimeState pulse_50_runtime_{};
  RuntimeState pulse_100_runtime_{};
};

}  // namespace esphome::openquatt_flow_filter_probe
