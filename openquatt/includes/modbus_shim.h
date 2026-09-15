#pragma once

#include <functional>
#include <list>
#include <memory>
#include <span>

#include "esphome/components/modbus/modbus.h"
#include "esphome/components/modbus/modbus_helpers.h"
#include "esphome/components/modbus_controller/modbus_controller.h"

namespace openquatt_modbus_shim {

using ReadCallback = std::function<void(modbus::EntityType, uint16_t, std::span<const uint8_t>)>;

class ShimDevice : public modbus::ModbusClientDevice {
 public:
  ShimDevice(modbus::ModbusClientHub* hub, uint8_t address, modbus_controller::ModbusController* controller)
      : modbus::ModbusClientDevice(hub, address), controller_(controller) {}

  void set_callback(ReadCallback cb, modbus::EntityType type, uint16_t start) {
    this->callback_ = std::move(cb);
    this->type_ = type;
    this->start_ = start;
  }

  static std::list<std::unique_ptr<ShimDevice>>* get_list() {
    static std::list<std::unique_ptr<ShimDevice>> list;
    return &list;
  }

 protected:
  void on_response(std::span<const uint8_t> request_pdu, std::span<const uint8_t> response_pdu) override {
    if (this->controller_ != nullptr) {
      auto addr_opt = modbus::helpers::client_pdu_start_address(request_pdu);
      const uint16_t addr = addr_opt.has_value() ? *addr_opt : this->start_;
      const uint8_t fc = modbus::helpers::pdu_function_code(request_pdu);
      this->controller_->set_online(true, static_cast<int>(fc), static_cast<int>(addr));
    }
    auto payload = modbus::helpers::server_pdu_payload(response_pdu);
    if (this->callback_) this->callback_(this->type_, this->start_, payload);
    this->self_erase_();
  }

  void on_error(std::span<const uint8_t> request_pdu, modbus::ExceptionCode ec) override {
    if (this->controller_ != nullptr) {
      auto addr_opt = modbus::helpers::client_pdu_start_address(request_pdu);
      const uint16_t addr = addr_opt.has_value() ? *addr_opt : this->start_;
      const uint8_t fc = modbus::helpers::pdu_function_code(request_pdu);
      this->controller_->set_online(true, static_cast<int>(fc), static_cast<int>(addr));
    }
    // For reads, surface as empty payload so the caller's token/start check can decide to keep or discard.
    // The caller's timeout will eventually fire if this was a genuine failure.
    if (this->callback_) {
      // Do not call callback with empty for now; let timeout handle it to avoid spurious empty-data handling.
      // Instead, just erase and let the caller's delay timeout log.
    }
    this->self_erase_();
  }

  bool on_no_response(std::span<const uint8_t> request_pdu) override {
    if (this->controller_ == nullptr) return false;
    auto addr_opt = modbus::helpers::client_pdu_start_address(request_pdu);
    const uint16_t addr = addr_opt.has_value() ? *addr_opt : this->start_;
    const uint8_t fc = modbus::helpers::pdu_function_code(request_pdu);
    this->controller_->increment_non_response_count();
    if (this->controller_->can_send()) return true;
    this->controller_->set_online(false, static_cast<int>(fc), static_cast<int>(addr));
    this->self_erase_();
    return false;
  }

  void on_not_sent(std::span<const uint8_t> request_pdu) override { this->self_erase_(); }

 private:
  void self_erase_() {
    auto* list = get_list();
    for (auto it = list->begin(); it != list->end(); ++it) {
      if (it->get() == this) {
        list->erase(it);
        break;
      }
    }
  }
  modbus_controller::ModbusController* controller_{nullptr};
  ReadCallback callback_{nullptr};
  modbus::EntityType type_{modbus::EntityType::HOLDING};
  uint16_t start_{0};
};

inline bool queue_modbus_read(modbus_controller::ModbusController* controller, modbus::EntityType type,
                              uint16_t start_address, uint16_t count, ReadCallback callback) {
  if (controller == nullptr || controller->hub() == nullptr) return false;
  auto dev = std::make_unique<ShimDevice>(controller->hub(), controller->device_address(), controller);
  dev->set_callback(std::move(callback), type, start_address);
  ShimDevice* raw = dev.get();
  bool accepted = false;
  if (type == modbus::EntityType::HOLDING) {
    accepted = raw->read_holding_registers(start_address, count);
  } else {
    accepted = raw->read_entities(type, start_address, count);
  }
  if (!accepted) return false;
  ShimDevice::get_list()->push_back(std::move(dev));
  return true;
}

}  // namespace openquatt_modbus_shim
