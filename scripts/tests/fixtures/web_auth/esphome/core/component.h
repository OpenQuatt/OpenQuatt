#pragma once
namespace esphome {
namespace setup_priority {
static constexpr float WIFI = 250;
}
class Component {
 public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void loop() {}
  virtual void dump_config() {}
  virtual float get_setup_priority() const { return 0; }
};
}  // namespace esphome
