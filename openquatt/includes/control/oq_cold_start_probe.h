#pragma once

#include <cstdint>

namespace oq_cold_start {

// One persistent reader per HP. Only its single pending frame uses the existing
// bounded Modbus queue; retries never allocate additional command owners.
template <typename Device, typename Controller, typename Sensor, typename Bytes, typename Error>
class WaterProbeBase : public Device {
 public:
  void poll(uint32_t now_ms, uint32_t session_ms, bool allowed, Controller* controller, Sensor* sensor) {
    if (!allowed || session_ms == 0 || session_ms != this->session_ms_) {
      this->cancel();
      if (!allowed || session_ms == 0) return;
      this->session_ms_ = session_ms;
      this->set_parent(controller->hub());
      this->set_address(controller->device_address());
      this->sensor_ = sensor;
    }
    if (this->pending_ || (this->attempted_ && now_ms - this->last_attempt_ms_ < 10000UL)) return;
    this->attempted_ = true;
    this->last_attempt_ms_ = now_ms;
    this->pending_ = this->read_holding_registers(2134, 1);
  }

  void cancel() {
    // Device-scoped cancellation detaches even in-flight replies silently.
    // A late response cannot publish into the next circulation session.
    if (this->pending_) this->clear_tx_queue_for_device();
    this->pending_ = false;
    this->attempted_ = false;
    this->session_ms_ = 0;
    this->sensor_ = nullptr;
  }

  void on_response(Bytes request, Bytes response) override {
    if (!this->pending_) return;
    this->pending_ = false;
    if (request.size() != 5 || request[0] != 3 || request[1] != 0x08 || request[2] != 0x56 || request[3] != 0 ||
        request[4] != 1 || response.size() != 4 || response[0] != 3 || response[1] != 2)
      return;
    const uint16_t raw = (static_cast<uint16_t>(response[2]) << 8) | response[3];
    // Use the existing offset/scale/clamp filters and on_value timestamp.
    // A malformed or out-of-range reply must never make the sample fresh.
    if (raw < 2000 || raw > 13000) return;
    this->sensor_->publish_state(static_cast<float>(raw));
  }

  void on_error(Bytes, Error) override { this->pending_ = false; }
  void on_not_sent(Bytes) override { this->pending_ = false; }
  bool on_no_response(Bytes) override {
    this->pending_ = false;
    return false;  // The supervisory tick retries, at most once per 10 s.
  }

 private:
  Sensor* sensor_ = nullptr;
  uint32_t session_ms_ = 0;
  uint32_t last_attempt_ms_ = 0;
  bool attempted_ = false;
  bool pending_ = false;
};

}  // namespace oq_cold_start

#if defined(OQ_TOPOLOGY_DUO)
#include "esphome/components/modbus_controller/modbus_controller.h"
#include "esphome/components/sensor/sensor.h"
namespace oq_cold_start {
using WaterProbe = WaterProbeBase<esphome::modbus::ModbusClientDevice, esphome::modbus_controller::ModbusController,
                                  esphome::sensor::Sensor, std::span<const uint8_t>, esphome::modbus::ExceptionCode>;
}  // namespace oq_cold_start
#endif
