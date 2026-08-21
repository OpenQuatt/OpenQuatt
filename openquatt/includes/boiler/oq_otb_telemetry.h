#pragma once

#include <stdint.h>
#include <string.h>

namespace oq_otb {

// OpenTherm message types used by the ESPHome master component.
constexpr uint8_t MESSAGE_TYPE_INVALID_DATA = 2;
constexpr uint8_t MESSAGE_TYPE_READ_ACK = 4;
constexpr uint8_t MESSAGE_TYPE_WRITE_ACK = 5;
constexpr uint8_t MESSAGE_TYPE_DATA_INVALID = 6;
constexpr uint8_t MESSAGE_TYPE_UNKNOWN_DATA_ID = 7;

enum Field : uint8_t {
  FIELD_STATUS = 0,
  FIELD_DEVICE_CONFIG,
  FIELD_FAULT_FLAGS,
  FIELD_MAX_BOILER_CAPACITY,
  FIELD_RELATIVE_MODULATION,
  FIELD_CH_WATER_PRESSURE,
  FIELD_BOILER_WATER_TEMPERATURE,
  FIELD_DHW_TEMPERATURE,
  FIELD_RETURN_WATER_TEMPERATURE,
  FIELD_OEM_DIAGNOSTIC,
  FIELD_OT_VERSION_DEVICE,
  FIELD_VERSION_DEVICE,
  FIELD_COUNT,
};

enum TransportError : uint8_t {
  TRANSPORT_ERROR_NONE = 0,
  TRANSPORT_ERROR_NO_TRANSITION,
  TRANSPORT_ERROR_INVALID_STOP_BIT,
  TRANSPORT_ERROR_PARITY,
  TRANSPORT_ERROR_NO_CHANGE_TOO_LONG,
  TRANSPORT_ERROR_RESPONSE_TIMEOUT,
  TRANSPORT_ERROR_HUB_SEND_TIMEOUT,
  TRANSPORT_ERROR_HUB_RECEIVE_TIMEOUT,
  TRANSPORT_ERROR_TIMER,
};

struct FieldState {
  uint32_t last_valid_ms{0};
  bool valid{false};
};

inline bool field_is_session_scoped(Field field) {
  switch (field) {
    case FIELD_DEVICE_CONFIG:
    case FIELD_MAX_BOILER_CAPACITY:
    case FIELD_OT_VERSION_DEVICE:
    case FIELD_VERSION_DEVICE:
      return true;
    default:
      return false;
  }
}

class TelemetryState {
 public:
  void reset_link_session() {
    this->session_has_response_ = false;
    this->last_response_ms_ = 0;
    this->last_response_id_ = -1;
    this->last_response_type_ = -1;
    for (auto& field : this->fields_) {
      field = {};
    }
  }

  void record_response(uint32_t now_ms, uint8_t message_id, uint8_t message_type) {
    this->session_has_response_ = true;
    this->last_response_ms_ = now_ms;
    this->last_response_id_ = message_id;
    this->last_response_type_ = message_type;
    this->accepted_response_count_++;

    const bool acknowledged = message_type == MESSAGE_TYPE_READ_ACK || message_type == MESSAGE_TYPE_WRITE_ACK;
    if (acknowledged) {
      this->acknowledged_response_count_++;
    } else if (message_type == MESSAGE_TYPE_DATA_INVALID) {
      this->data_invalid_response_count_++;
    } else if (message_type == MESSAGE_TYPE_UNKNOWN_DATA_ID) {
      this->unknown_data_id_response_count_++;
    } else {
      this->unexpected_response_type_count_++;
    }

    const int field_index = field_index_for_message_id(message_id);
    if (field_index < 0) return;

    auto& field = this->fields_[field_index];
    // All tracked telemetry messages are read requests. A WRITE_ACK for one of
    // them is therefore not a valid sample, even though it is a valid frame.
    if (message_type == MESSAGE_TYPE_READ_ACK) {
      field.last_valid_ms = now_ms;
      field.valid = true;
    } else {
      field.valid = false;
    }
  }

  bool field_is_valid(Field field) const { return this->fields_[field].valid; }

  bool field_is_fresh(Field field, uint32_t now_ms, uint32_t max_age_ms) const {
    const auto& state = this->fields_[field];
    if (!state.valid) return false;
    // Initial/static OpenTherm values are requested once when polling starts.
    // They remain valid for the lifetime of that link session; applying the
    // repeating-field timeout to them would erase valid ID15/capability data
    // a few seconds after boot even though no refresh is scheduled.
    if (field_is_session_scoped(field)) return true;
    return (uint32_t)(now_ms - state.last_valid_ms) <= max_age_ms;
  }

  bool expire_response_session_if_stale(uint32_t now_ms, uint32_t max_age_ms) {
    if (!this->session_has_response_ || (uint32_t)(now_ms - this->last_response_ms_) <= max_age_ms) {
      return false;
    }

    // Latch expiry before millis() can make a full 32-bit revolution. Without
    // this state transition, an unattended disconnected bus could appear
    // fresh again for one timeout window after roughly 49.7 days.
    this->session_has_response_ = false;
    this->last_response_ms_ = 0;
    for (auto& field : this->fields_) {
      field.valid = false;
    }
    return true;
  }

  bool response_payload_is_usable(uint8_t message_id, uint8_t message_type) const {
    // Every field tracked by this helper is requested as READ_DATA. A
    // syntactically valid WRITE_ACK for such an ID must not reach ESPHome's
    // payload processors as if it were a valid measurement.
    if (field_index_for_message_id(message_id) >= 0) {
      return message_type == MESSAGE_TYPE_READ_ACK;
    }
    return message_type == MESSAGE_TYPE_READ_ACK || message_type == MESSAGE_TYPE_WRITE_ACK;
  }

  bool transport_is_available(uint32_t now_ms, uint32_t response_max_age_ms, uint32_t status_max_age_ms) const {
    return this->session_has_response_ && (uint32_t)(now_ms - this->last_response_ms_) <= response_max_age_ms &&
           this->field_is_fresh(FIELD_STATUS, now_ms, status_max_age_ms);
  }

  void record_log_message(uint32_t now_ms, const char* tag, const char* message) {
    if (tag == nullptr || message == nullptr || strcmp(tag, "opentherm") != 0) return;

    TransportError error = TRANSPORT_ERROR_NONE;
    if (strstr(message, "NO_TRANSITION") != nullptr) {
      error = TRANSPORT_ERROR_NO_TRANSITION;
      this->no_transition_count_++;
      this->protocol_error_count_++;
    } else if (strstr(message, "INVALID_STOP_BIT") != nullptr) {
      error = TRANSPORT_ERROR_INVALID_STOP_BIT;
      this->invalid_stop_bit_count_++;
      this->protocol_error_count_++;
    } else if (strstr(message, "PARITY_ERROR") != nullptr) {
      error = TRANSPORT_ERROR_PARITY;
      this->parity_error_count_++;
      this->protocol_error_count_++;
    } else if (strstr(message, "NO_CHANGE_TOO_LONG") != nullptr) {
      error = TRANSPORT_ERROR_NO_CHANGE_TOO_LONG;
      this->no_change_too_long_count_++;
      this->protocol_error_count_++;
    } else if (strstr(message, "Timeout while waiting for response from device") != nullptr) {
      error = TRANSPORT_ERROR_RESPONSE_TIMEOUT;
      this->response_timeout_count_++;
    } else if (strstr(message, "Hub timeout triggered during send") != nullptr) {
      error = TRANSPORT_ERROR_HUB_SEND_TIMEOUT;
      this->hub_send_timeout_count_++;
    } else if (strstr(message, "Hub timeout triggered during receive") != nullptr) {
      error = TRANSPORT_ERROR_HUB_RECEIVE_TIMEOUT;
      this->hub_receive_timeout_count_++;
    } else if (strstr(message, "Error occured while manipulating timer") != nullptr) {
      error = TRANSPORT_ERROR_TIMER;
      this->timer_error_count_++;
    }

    if (error == TRANSPORT_ERROR_NONE) return;
    this->transport_error_count_++;
    this->last_transport_error_ = error;
    this->last_transport_error_ms_ = now_ms;
  }

  uint32_t last_response_ms() const { return this->last_response_ms_; }
  bool session_has_response() const { return this->session_has_response_; }
  int last_response_id() const { return this->last_response_id_; }
  int last_response_type() const { return this->last_response_type_; }
  uint32_t accepted_response_count() const { return this->accepted_response_count_; }
  uint32_t acknowledged_response_count() const { return this->acknowledged_response_count_; }
  uint32_t data_invalid_response_count() const { return this->data_invalid_response_count_; }
  uint32_t unknown_data_id_response_count() const { return this->unknown_data_id_response_count_; }
  uint32_t unexpected_response_type_count() const { return this->unexpected_response_type_count_; }

  uint32_t transport_error_count() const { return this->transport_error_count_; }
  uint32_t protocol_error_count() const { return this->protocol_error_count_; }
  uint32_t response_timeout_count() const { return this->response_timeout_count_; }
  uint32_t no_transition_count() const { return this->no_transition_count_; }
  uint32_t invalid_stop_bit_count() const { return this->invalid_stop_bit_count_; }
  uint32_t parity_error_count() const { return this->parity_error_count_; }
  uint32_t no_change_too_long_count() const { return this->no_change_too_long_count_; }
  uint32_t hub_send_timeout_count() const { return this->hub_send_timeout_count_; }
  uint32_t hub_receive_timeout_count() const { return this->hub_receive_timeout_count_; }
  uint32_t timer_error_count() const { return this->timer_error_count_; }
  uint32_t last_transport_error_ms() const { return this->last_transport_error_ms_; }
  TransportError last_transport_error() const { return this->last_transport_error_; }

  const char* last_response_type_name() const {
    switch (this->last_response_type_) {
      case MESSAGE_TYPE_READ_ACK:
        return "READ_ACK";
      case MESSAGE_TYPE_WRITE_ACK:
        return "WRITE_ACK";
      case MESSAGE_TYPE_INVALID_DATA:
        return "INVALID_DATA";
      case MESSAGE_TYPE_DATA_INVALID:
        return "DATA_INVALID";
      case MESSAGE_TYPE_UNKNOWN_DATA_ID:
        return "UNKNOWN_DATAID";
      default:
        return this->last_response_type_ < 0 ? "Never" : "Unexpected";
    }
  }

  const char* last_transport_error_name() const {
    switch (this->last_transport_error_) {
      case TRANSPORT_ERROR_NO_TRANSITION:
        return "NO_TRANSITION";
      case TRANSPORT_ERROR_INVALID_STOP_BIT:
        return "INVALID_STOP_BIT";
      case TRANSPORT_ERROR_PARITY:
        return "PARITY_ERROR";
      case TRANSPORT_ERROR_NO_CHANGE_TOO_LONG:
        return "NO_CHANGE_TOO_LONG";
      case TRANSPORT_ERROR_RESPONSE_TIMEOUT:
        return "RESPONSE_TIMEOUT";
      case TRANSPORT_ERROR_HUB_SEND_TIMEOUT:
        return "HUB_SEND_TIMEOUT";
      case TRANSPORT_ERROR_HUB_RECEIVE_TIMEOUT:
        return "HUB_RECEIVE_TIMEOUT";
      case TRANSPORT_ERROR_TIMER:
        return "TIMER_ERROR";
      default:
        return "None";
    }
  }

 private:
  static int field_index_for_message_id(uint8_t message_id) {
    switch (message_id) {
      case 0:
        return FIELD_STATUS;
      case 3:
        return FIELD_DEVICE_CONFIG;
      case 5:
        return FIELD_FAULT_FLAGS;
      case 15:
        return FIELD_MAX_BOILER_CAPACITY;
      case 17:
        return FIELD_RELATIVE_MODULATION;
      case 18:
        return FIELD_CH_WATER_PRESSURE;
      case 25:
        return FIELD_BOILER_WATER_TEMPERATURE;
      case 26:
        return FIELD_DHW_TEMPERATURE;
      case 28:
        return FIELD_RETURN_WATER_TEMPERATURE;
      case 115:
        return FIELD_OEM_DIAGNOSTIC;
      case 125:
        return FIELD_OT_VERSION_DEVICE;
      case 127:
        return FIELD_VERSION_DEVICE;
      default:
        return -1;
    }
  }

  FieldState fields_[FIELD_COUNT]{};

  bool session_has_response_{false};
  uint32_t last_response_ms_{0};
  int last_response_id_{-1};
  int last_response_type_{-1};
  uint32_t accepted_response_count_{0};
  uint32_t acknowledged_response_count_{0};
  uint32_t data_invalid_response_count_{0};
  uint32_t unknown_data_id_response_count_{0};
  uint32_t unexpected_response_type_count_{0};

  uint32_t transport_error_count_{0};
  uint32_t protocol_error_count_{0};
  uint32_t response_timeout_count_{0};
  uint32_t no_transition_count_{0};
  uint32_t invalid_stop_bit_count_{0};
  uint32_t parity_error_count_{0};
  uint32_t no_change_too_long_count_{0};
  uint32_t hub_send_timeout_count_{0};
  uint32_t hub_receive_timeout_count_{0};
  uint32_t timer_error_count_{0};
  uint32_t last_transport_error_ms_{0};
  TransportError last_transport_error_{TRANSPORT_ERROR_NONE};
};

inline TelemetryState telemetry_state{};

}  // namespace oq_otb
