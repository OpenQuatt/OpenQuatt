#include "OpenQuattModbusClientHub.h"

#include "esphome/core/log.h"
#include "includes/protocol/oq_modbus_recovery.h"

namespace esphome {
namespace openquatt_modbus {
namespace {

static const char* const TAG = "openquatt.modbus";

}  // namespace

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
