#pragma once
namespace esphome::binary_sensor {
class BinarySensor {
 public:
  bool state{false};
  bool sampled{true};
  bool has_state() const { return sampled; }
};
}  // namespace esphome::binary_sensor
