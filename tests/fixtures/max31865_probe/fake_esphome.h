#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>
#include <vector>

inline uint32_t clock_us = 0;
inline uint32_t micros() { return clock_us; }
inline uint32_t millis() { return clock_us / 1000; }
inline std::vector<std::string> diagnostic_logs;
inline std::vector<bool> diagnostic_log_bias_states;
inline bool diagnostic_bias_on = false;
template <typename... Args>
void fake_log(const char* tag, const char* format, Args... args) {
  if (std::string(tag) != "oq.max31865_diag") return;
  char line[512];
  if constexpr (sizeof...(args) == 0) {
    std::snprintf(line, sizeof(line), "%s", format);
  } else {
    std::snprintf(line, sizeof(line), format, args...);
  }
  diagnostic_logs.emplace_back(line);
  diagnostic_log_bias_states.push_back(diagnostic_bias_on);
}
#define ESP_LOGI(...) fake_log(__VA_ARGS__)
#define ESP_LOGE(...) fake_log(__VA_ARGS__)
#define ESP_LOGW(...) fake_log(__VA_ARGS__)
#define ESP_LOGV(...) fake_log(__VA_ARGS__)
#define ESP_LOGVV(...) fake_log(__VA_ARGS__)
#define ESP_LOGCONFIG(...) fake_log(__VA_ARGS__)
#define LOG_SENSOR(...)
#define LOG_PIN(...)
#define LOG_UPDATE_INTERVAL(...)
#define LOG_STR_LITERAL(x) x

struct FakeChip {
  uint8_t config = 1;
  uint8_t faults = 0;
  uint8_t injected_faults = 0;
  uint8_t faults_on_power_off = 0;
  uint16_t rtd = 48000;
  bool stuck = false;
  bool strict_clear = false;
  bool ignore_clear = false;
  bool drop_manual_start = false;
  bool rtd_fault_only = false;
  bool contradict_repeat = false;
  bool powered_off = false;
  uint8_t fault_reads_after_off = 0;
  uint8_t address = 0;
  bool address_pending = true;
  uint32_t conversion_ready_us = 0;
  std::vector<std::pair<uint32_t, uint8_t>> writes;
  std::vector<uint8_t> reads;
  std::vector<bool> read_bias_states;
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
  uint32_t due = 0;
  std::function<void()> pending;
  void status_set_error() { error = true; }
  void status_clear_error() { error = false; }
  void status_set_warning() { warning = true; }
  void status_clear_warning() { warning = false; }
  void set_timeout(const char*, uint32_t ms, std::function<void()> callback) {
    due = clock_us + ms * 1000;
    pending = std::move(callback);
  }
  void next() {
    clock_us = due;
    auto callback = std::move(pending);
    pending = nullptr;
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
  void* cs_ = nullptr;
  void spi_setup() {}
  void enable() { chip.address_pending = true; }
  void disable() { clock_us += 20; }
  void write_byte(uint8_t value) {
    if (chip.address_pending) {
      chip.address = value;
      chip.address_pending = false;
    } else if (chip.address == 0x80) {
      chip.writes.emplace_back(clock_us, value);
      if (chip.drop_manual_start && (value & 0x0C) == 8) return;
      chip.config = value;
      // The permissive mode models observed recovery, not guaranteed silicon behavior.
      if ((value & 2) && !chip.ignore_clear && (!chip.strict_clear || !(value & 0x2C))) chip.faults = 0;
      if (!chip.powered_off && !(value & 0x80)) chip.faults |= chip.faults_on_power_off;
      chip.powered_off = !(value & 0x80);
      diagnostic_bias_on = !chip.powered_off;
      chip.fault_reads_after_off = 0;
      if (value & 0x20) chip.conversion_ready_us = clock_us + ((value & 1) ? 66000 : 55000);
    }
  }
  uint8_t read_byte() {
    const uint8_t address = chip.address++;
    chip.reads.push_back(address);
    chip.read_bias_states.push_back(!chip.powered_off);
    if (address == 0) {
      chip.config &= ~0x02;  // Fault-clear self-clears independently of conversion/detector state.
      if ((chip.config & 0x20) && clock_us >= chip.conversion_ready_us) chip.config &= ~0x20;
      const uint8_t detect = chip.config & 0x0C;
      if (!chip.stuck && (detect == 4 || detect == 12)) {
        chip.config &= ~0x0C;
        chip.faults |= chip.injected_faults;
      }
      return chip.config;
    }
    if (address == 1) return chip.powered_off && chip.contradict_repeat ? 0 : chip.rtd >> 8;
    if (address == 2)
      return chip.powered_off && chip.contradict_repeat ? 0
                                                        : (chip.rtd & 0xFE) | (chip.faults != 0 || chip.rtd_fault_only);
    if (address == 7) {
      if (chip.powered_off && chip.fault_reads_after_off++ > 0 && chip.contradict_repeat) return chip.faults ? 0 : 8;
      return chip.faults;
    }
    return 0;
  }
};
}  // namespace spi
}  // namespace esphome
