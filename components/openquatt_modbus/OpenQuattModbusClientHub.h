#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>

#include "esphome/components/modbus/modbus.h"

namespace esphome {
namespace openquatt_modbus {

class OpenQuattModbusClientHub : public modbus::ModbusClientHub {
 public:
  using CommandGuard = std::function<bool(uint8_t, const uint8_t*, size_t)>;

  // Install once during boot. The callback is synchronous and must
  // not queue or clear commands; captured state belongs to the ESPHome loop.
  void set_command_guard(CommandGuard guard) { this->command_guard_ = std::move(guard); }
  void loop() override;
  bool tx_blocked() override;

  uint32_t partial_response_count() const { return this->partial_response_count_; }
  uint32_t parse_failed_count() const { return this->parse_failed_count_; }
  uint32_t recovered_response_count() const { return this->recovered_response_count_; }
  uint32_t offline_count() const { return this->offline_count_; }

  void record_offline_transition() { increment_(this->offline_count_); }

 protected:
  void parse_modbus_frames() override;
  bool try_resync_expected_response_();

  static void increment_(uint32_t& counter) {
    if (counter != UINT32_MAX) {
      counter++;
    }
  }

  uint32_t partial_response_count_{0U};
  uint32_t parse_failed_count_{0U};
  uint32_t recovered_response_count_{0U};
  uint32_t offline_count_{0U};
  bool response_recovery_pending_{false};
  CommandGuard command_guard_;
};

}  // namespace openquatt_modbus
}  // namespace esphome
