#include "OpenQuattModbusClientHub.h"
#include "OpenQuattModbusCommandGuard.h"

#include "esphome/core/log.h"
#include "includes/protocol/oq_modbus_recovery.h"

namespace esphome {
namespace openquatt_modbus {
namespace {

static const char* const TAG = "openquatt.modbus";

}  // namespace

bool OpenQuattModbusClientHub::tx_blocked() {
  // The upstream send_frame_ may otherwise delay/yield after our guard. Wait
  // for a completely idle bus so dispatch has no deliberate delay afterwards.
  return this->ModbusClientHub::tx_blocked() || this->tx_delay_remaining() > 0;
}

void OpenQuattModbusClientHub::loop() {
  // ESPHome 2026.9.0 ModbusClientHub lifecycle. send_next_frame_ is not virtual,
  // so preserve the upstream RX/watchdog/sweep order and gate its final dispatch.
  // A granted retry becomes READY in sweep_ and passes the same live guard.
  this->sweep_();
  this->Modbus::loop();
  if (this->waiting_for_response_ &&
      this->last_receive_check_ - this->last_send_ > this->last_send_tx_offset_ + this->send_wait_time_us_) {
    this->expire_waiting_();
  }
  this->sweep_();
  if (this->tx_blocked()) {
    return;
  }
  if (this->command_guard_ && !detail::guard_next_command(
                                  this->tx_buffer_.size(), [this]() { return this->select_next_ready_(); },
                                  [this](const modbus::ModbusDeviceCommand& command) {
                                    const auto pdu = command.frame.pdu();
                                    return this->command_guard_(command.frame.address(), pdu.data(), pdu.size());
                                  },
                                  [this](modbus::ModbusDeviceCommand& command) {
                                    // No timeout/offline transition: normal on_not_sent cleanup retires
                                    // this one frame and keeps unrelated requests and in-flight RX alive.
                                    command.retire();
                                    this->sweep_needed_ = true;
                                    this->sweep_();
                                  })) {
    return;
  }
  this->send_next_frame_();
}

bool OpenQuattModbusClientHub::try_resync_expected_response_() {
  if (!this->waiting_for_response_) {
    return false;
  }

  auto* command = this->find_waiting_();
  if (command == nullptr || command->state != modbus::FrameState::WAITING) {
    return false;
  }

  const auto request_pdu = command->frame.pdu();
  const size_t offset = oq_modbus_recovery::find_expected_response_offset(
      this->rx_buffer_.data(), this->rx_buffer_.size(), command->frame.address(), request_pdu.data(),
      request_pdu.size());
  if (offset == 0U) {
    return false;
  }

  ESP_LOGD(TAG, "Resynchronizing expected Modbus response after dropping %u leading byte%s",
           static_cast<unsigned>(offset), offset == 1U ? "" : "s");
  this->rx_buffer_.erase(this->rx_buffer_.begin(), this->rx_buffer_.begin() + offset);
  this->response_recovery_pending_ = true;
  return true;
}

void OpenQuattModbusClientHub::parse_modbus_frames() {
  if (this->rx_buffer_.empty()) {
    return;
  }

  size_t size;
  do {
    size = this->rx_buffer_.size();
    bool expected_response_was_waiting = false;
    if (this->waiting_for_response_) {
      auto* command = this->find_waiting_();
      expected_response_was_waiting = command != nullptr && command->state == modbus::FrameState::WAITING;
    }

    if (!this->parse_modbus_server_frame_()) {
      if (!this->try_resync_expected_response_()) {
        this->response_recovery_pending_ = false;
        increment_(this->parse_failed_count_);
        this->clear_rx_buffer_(LOG_STR("parse failed"), true);
      }
    } else if (expected_response_was_waiting && !this->waiting_for_response_ && size > this->rx_buffer_.size()) {
      // The expected transaction is complete. No later bytes in this RX batch
      // can belong to another response because the client sends only one
      // request at a time. Treat them as trailing line noise instead of
      // carrying them into the next transaction and logging a partial frame.
      if (!this->rx_buffer_.empty()) {
        const size_t trailing_bytes = this->rx_buffer_.size();
        ESP_LOGD(TAG, "Recovered expected Modbus response and discarded %u trailing byte%s",
                 static_cast<unsigned>(trailing_bytes), trailing_bytes == 1U ? "" : "s");
        this->response_recovery_pending_ = true;
        this->clear_rx_buffer_(LOG_STR("trailing bytes after expected response"), false);
      }

      if (this->response_recovery_pending_) {
        increment_(this->recovered_response_count_);
        this->response_recovery_pending_ = false;
      }
    }
  } while (!this->rx_buffer_.empty() && size > this->rx_buffer_.size());

  if (!this->rx_buffer_.empty() && this->timeout_()) {
    // Leading noise can make the upstream parser believe a complete valid
    // response is merely an incomplete frame (for example FF + an FC06 echo).
    // Give the bounded request-aware resync one last chance before classifying
    // the buffer as a genuine partial response.
    if (this->try_resync_expected_response_()) {
      this->parse_modbus_frames();
      return;
    }

    this->response_recovery_pending_ = false;
    increment_(this->partial_response_count_);
    this->clear_rx_buffer_(LOG_STR("timeout after partial response"), true);
  }
}

}  // namespace openquatt_modbus
}  // namespace esphome
