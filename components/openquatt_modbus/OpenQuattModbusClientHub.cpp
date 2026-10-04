#include "OpenQuattModbusClientHub.h"

#include "esphome/core/log.h"
#include "includes/protocol/oq_modbus_recovery.h"

namespace esphome {
namespace openquatt_modbus {
namespace {

static const char* const TAG = "openquatt.modbus";
constexpr size_t MAX_LOGGED_NOISE_BYTES = 8U;

struct ModbusRequestContext {
  bool valid{false};
  uint8_t slave{0U};
  uint8_t function{0U};
  bool has_register{false};
  uint16_t register_address{0U};
  bool has_argument{false};
  uint16_t argument{0U};
};

ModbusRequestContext make_request_context(uint8_t slave, const uint8_t* pdu, size_t pdu_size) {
  ModbusRequestContext context{};
  context.slave = slave;
  if (pdu == nullptr || pdu_size == 0U) {
    return context;
  }

  context.valid = true;
  context.function = pdu[0];
  if (pdu_size >= 3U) {
    context.has_register = true;
    context.register_address = oq_modbus_recovery::read_be_u16(pdu + 1U);
  }
  if (pdu_size >= 5U) {
    context.has_argument = true;
    context.argument = oq_modbus_recovery::read_be_u16(pdu + 3U);
  }
  return context;
}

void format_noise_bytes(const uint8_t* data, size_t size, char* output, size_t output_size) {
  if (output == nullptr || output_size == 0U) {
    return;
  }
  output[0] = '\\0';
  if (data == nullptr || size == 0U) {
    return;
  }

  const size_t shown = size < MAX_LOGGED_NOISE_BYTES ? size : MAX_LOGGED_NOISE_BYTES;
  size_t used = 0U;
  for (size_t i = 0U; i < shown && used < output_size; i++) {
    const int written = snprintf(output + used, output_size - used, i == 0U ? "%02X" : " %02X", data[i]);
    if (written <= 0 || static_cast<size_t>(written) >= output_size - used) {
      output[output_size - 1U] = '\\0';
      return;
    }
    used += static_cast<size_t>(written);
  }
  if (size > shown && used < output_size) {
    snprintf(output + used, output_size - used, " ...");
  }
}

void log_modbus_anomaly(const char* event, const ModbusRequestContext& context, const char* bytes_label,
                        const uint8_t* bytes, size_t byte_count) {
  char bytes_hex[32];
  format_noise_bytes(bytes, byte_count, bytes_hex, sizeof(bytes_hex));

  if (context.valid && context.has_register && context.has_argument) {
    ESP_LOGD(TAG, "%s: slave=%u fc=0x%02X reg=%u arg=%u %s[%u]=%s", event,
             static_cast<unsigned>(context.slave), static_cast<unsigned>(context.function),
             static_cast<unsigned>(context.register_address), static_cast<unsigned>(context.argument), bytes_label,
             static_cast<unsigned>(byte_count), bytes_hex);
    return;
  }
  if (context.valid) {
    ESP_LOGD(TAG, "%s: slave=%u fc=0x%02X %s[%u]=%s", event, static_cast<unsigned>(context.slave),
             static_cast<unsigned>(context.function), bytes_label, static_cast<unsigned>(byte_count), bytes_hex);
    return;
  }
  ESP_LOGD(TAG, "%s: request=unknown %s[%u]=%s", event, bytes_label, static_cast<unsigned>(byte_count), bytes_hex);
}

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

  const ModbusRequestContext request_context =
      make_request_context(command->frame.address(), request_pdu.data(), request_pdu.size());
  log_modbus_anomaly("Resynchronizing expected Modbus response", request_context, "leading", this->rx_buffer_.data(),
                     offset);
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
    ModbusRequestContext expected_request{};
    if (this->waiting_for_response_) {
      auto* command = this->find_waiting_();
      expected_response_was_waiting = command != nullptr && command->state == modbus::FrameState::WAITING;
      if (expected_response_was_waiting) {
        const auto request_pdu = command->frame.pdu();
        expected_request = make_request_context(command->frame.address(), request_pdu.data(), request_pdu.size());
      }
    }

    if (!this->parse_modbus_server_frame_()) {
      if (!this->try_resync_expected_response_()) {
        this->response_recovery_pending_ = false;
        log_modbus_anomaly("Modbus parse failed", expected_request, "rx", this->rx_buffer_.data(),
                           this->rx_buffer_.size());
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
        log_modbus_anomaly("Recovered expected Modbus response", expected_request, "trailing", this->rx_buffer_.data(),
                           trailing_bytes);
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

    ModbusRequestContext expected_request{};
    if (this->waiting_for_response_) {
      auto* command = this->find_waiting_();
      if (command != nullptr && command->state == modbus::FrameState::WAITING) {
        const auto request_pdu = command->frame.pdu();
        expected_request = make_request_context(command->frame.address(), request_pdu.data(), request_pdu.size());
      }
    }
    this->response_recovery_pending_ = false;
    log_modbus_anomaly("Modbus partial response timeout", expected_request, "rx", this->rx_buffer_.data(),
                       this->rx_buffer_.size());
    increment_(this->partial_response_count_);
    this->clear_rx_buffer_(LOG_STR("timeout after partial response"), true);
  }
}

}  // namespace openquatt_modbus
}  // namespace esphome
