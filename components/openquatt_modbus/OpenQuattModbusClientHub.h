#pragma once

#include <cstdint>

#include "esphome/components/modbus/modbus.h"

namespace esphome {
namespace openquatt_modbus {

class OpenQuattModbusClientHub : public modbus::ModbusClientHub {
 public:
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
};

}  // namespace openquatt_modbus
}  // namespace esphome
