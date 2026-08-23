#pragma once

#include <cstdint>

#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"

namespace esphome::openquatt_flow_filter_probe {

class OpenQuattFlowFilterProbe : public sensor::Sensor, public Component {
 public:
  void set_pin(InternalGPIOPin *pin) { this->pin_ = pin; }
  void set_edge_100_sensor(sensor::Sensor *sensor) { this->edge_100_sensor_ = sensor; }
  void set_pulse_20_sensor(sensor::Sensor *sensor) { this->pulse_20_sensor_ = sensor; }
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
    bool running{false};
    bool peeked_edge{false};
    uint32_t last_processed_edge_us{0};
  };

  static void gpio_intr(OpenQuattFlowFilterProbe *probe);
  static void IRAM_ATTR apply_pulse_filter_(volatile ChannelState &channel, volatile PulseFilterState &filter,
                                            uint32_t filter_us, uint32_t now_us, bool pin_val, bool previous_pin_val);
  void process_channel_(sensor::Sensor *sensor, ChannelState state, RuntimeState &runtime, uint32_t filter_us,
                        bool pulse_mode, uint32_t now_us);
  float pulses_per_minute_to_lph_(float ppm) const;

  InternalGPIOPin *pin_{nullptr};
  ISRInternalGPIOPin isr_pin_;
  sensor::Sensor *edge_100_sensor_{nullptr};
  sensor::Sensor *pulse_20_sensor_{nullptr};

  float kf_lpm_per_hz_{0.05f};
  float q0_lpm_{0.0f};
  bool last_pin_val_{false};

  volatile ChannelState edge_100_state_{};
  volatile ChannelState pulse_100_state_{};
  volatile ChannelState pulse_20_state_{};
  volatile PulseFilterState pulse_100_filter_{};
  volatile PulseFilterState pulse_20_filter_{};
  volatile uint32_t edge_100_last_sent_us_{0};

  RuntimeState edge_100_runtime_{};
  RuntimeState pulse_100_runtime_{};
  RuntimeState pulse_20_runtime_{};
};

}  // namespace esphome::openquatt_flow_filter_probe
