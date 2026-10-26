#pragma once

#include <cmath>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

inline uint32_t clock_us = 0;
inline uint32_t micros() { return clock_us; }
// All log output is deliberately suppressed.
#define ESP_LOGE(...)
#define ESP_LOGW(...)
#define ESP_LOGV(...)
#define ESP_LOGVV(...)
#define ESP_LOGCONFIG(...)
#define LOG_SENSOR(...)
#define LOG_PIN(...)
#define LOG_UPDATE_INTERVAL(...)
#define LOG_STR_LITERAL(x) x

struct FakeChip {
  uint8_t config = 1;
  uint8_t faults = 0;
  uint8_t injected_faults = 0;
  uint16_t rtd = 51000;
  bool stuck = false;
  uint8_t address = 0;
  bool address_pending = true;
  std::vector<uint8_t> writes;
  std::vector<uint8_t> reads;
};
inline FakeChip chip;

namespace esphome {
class PollingComponent {
 public:
  virtual ~PollingComponent() = default;
  virtual void setup() {}
  virtual void dump_config() {}
  virtual void update() {}
  bool error = false;
  bool warning = false;
  uint32_t delay_ms = 0;
  std::function<void()> pending;
  void status_set_error() { error = true; }
  void status_clear_error() { error = false; }
  void status_set_warning() { warning = true; }
  void status_clear_warning() { warning = false; }
  void set_timeout(const char*, uint32_t ms, std::function<void()> callback) {
    delay_ms = ms;
    pending = std::move(callback);
  }
  void complete() {
    clock_us += delay_ms * 1000;
    auto callback = std::move(pending);
    callback();
  }
};
namespace sensor {
class Sensor {
 public:
  float state = NAN;
  void publish_state(float value) { state = value; }
};
}  // namespace sensor
namespace spi {
enum { BIT_ORDER_MSB_FIRST, CLOCK_POLARITY_LOW, CLOCK_PHASE_TRAILING, DATA_RATE_4MHZ };
template <int, int, int, int>
class SPIDevice {
 protected:
  void spi_setup() {}
  void enable() { chip.address_pending = true; }
  void disable() { clock_us += 20; }
  void write_byte(uint8_t value) {
    if (chip.address_pending) {
      chip.address = value;
      chip.address_pending = false;
    } else if (chip.address == 0x80) {
      chip.writes.push_back(value);
      chip.config = value;
      if (value & 2) chip.faults = 0;
      if (value & 0x0C) {
        chip.faults = chip.injected_faults;
        if (!chip.stuck) chip.config &= ~0x0C;
      }
    }
  }
  uint8_t read_byte() {
    chip.reads.push_back(chip.address);
    if (chip.address == 0) return chip.config;
    if (chip.address == 1) {
      chip.address = 2;
      return chip.rtd >> 8;
    }
    if (chip.address == 2) return chip.rtd & 0xFF;
    if (chip.address == 7) return chip.faults;
    return 0;
  }
};
}  // namespace spi
}  // namespace esphome
