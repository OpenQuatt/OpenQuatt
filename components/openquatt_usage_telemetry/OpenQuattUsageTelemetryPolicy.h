#pragma once

#include <cinttypes>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>

namespace esphome {
namespace openquatt_usage_telemetry {

inline constexpr int MQTT_PUBLISH_RETAIN = 0;

enum class MqttCleanupDecision : uint8_t {
  DESTROY = 0U,
  FORCE_DISCONNECT = 1U,
  RETRY_STOP = 2U,
  DESTROY_ALREADY_STOPPED = 3U,
};

class FixedBufferWriter {
 public:
  FixedBufferWriter(char* data, size_t capacity) : data_(data), capacity_(capacity) {
    if (this->data_ == nullptr || this->capacity_ == 0U) {
      this->ok_ = false;
    } else {
      this->data_[0] = '\0';
    }
  }

  FixedBufferWriter& operator+=(const char* value) {
    if (value != nullptr) {
      this->append_(value, std::strlen(value));
    }
    return *this;
  }

  FixedBufferWriter& operator+=(const std::string& value) {
    this->append_(value.data(), value.size());
    return *this;
  }

  FixedBufferWriter& operator+=(char value) {
    this->append_(&value, 1U);
    return *this;
  }

  void append_uint(uint64_t value) {
    char buffer[24];
    const int length = std::snprintf(buffer, sizeof(buffer), "%" PRIu64, value);
    if (length <= 0) {
      this->ok_ = false;
      return;
    }
    this->append_(buffer, static_cast<size_t>(length));
  }

  bool ok() const { return this->ok_; }
  size_t size() const { return this->size_; }

 private:
  void append_(const char* value, size_t length) {
    if (!this->ok_ || value == nullptr || length >= this->capacity_ - this->size_) {
      this->ok_ = false;
      return;
    }
    std::memcpy(this->data_ + this->size_, value, length);
    this->size_ += length;
    this->data_[this->size_] = '\0';
  }

  char* data_{nullptr};
  size_t capacity_{0U};
  size_t size_{0U};
  bool ok_{true};
};

inline void append_json_escaped(FixedBufferWriter& output, const std::string& input) {
  for (char c : input) {
    switch (c) {
      case '"':
        output += "\\\"";
        break;
      case '\\':
        output += "\\\\";
        break;
      case '\b':
        output += "\\b";
        break;
      case '\f':
        output += "\\f";
        break;
      case '\n':
        output += "\\n";
        break;
      case '\r':
        output += "\\r";
        break;
      case '\t':
        output += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20U) {
          char buffer[7];
          std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned char>(c));
          output += buffer;
        } else {
          output += c;
        }
        break;
    }
  }
}

inline const char* quatt_hybrid_generation_wire_value(const std::string& option) {
  if (option == "V1") {
    return "v1";
  }
  if (option == "V1.5") {
    return "v1_5";
  }
  if (option == "V2") {
    return "v2";
  }
  return nullptr;
}

inline const char* heating_strategy_wire_value(const std::string& option) {
  if (option == "Power House") {
    return "power_house";
  }
  if (option == "Water Temperature Control (heating curve)") {
    return "heating_curve";
  }
  return nullptr;
}

inline const char* configured_source_wire_value(const std::string& option) {
  if (option == "Auto") {
    return "auto";
  }
  if (option == "Local") {
    return "local";
  }
  if (option == "Outdoor unit") {
    return "outdoor_unit";
  }
  if (option == "CIC") {
    return "cic";
  }
  if (option == "OT thermostat") {
    return "opentherm";
  }
  if (option == "HA input" || option == "Home Assistant") {
    return "home_assistant";
  }
  if (option == "API input") {
    return "api_input";
  }
  if (option == "MQTT") {
    return "mqtt";
  }
  if (option == "CIC or HA input") {
    return "cic_or_home_assistant";
  }
  if (option == "Schedule") {
    return "schedule";
  }
  if (option == "Disabled") {
    return "disabled";
  }
  return nullptr;
}

inline const char* flow_source_config_wire_value(const std::string& flow_source, bool q_source_available,
                                                 const std::string& q_flow_source) {
  if (flow_source == "CIC") {
    return "cic";
  }
  if (flow_source != "Outdoor unit") {
    return nullptr;
  }
  if (!q_source_available) {
    return "outdoor_unit";
  }
  if (q_flow_source == "Local") {
    return "controller_local";
  }
  if (q_flow_source == "Auto" || q_flow_source == "Outdoor unit") {
    return "outdoor_unit";
  }
  return nullptr;
}

inline MqttCleanupDecision mqtt_cleanup_decision(bool stop_succeeded, bool connected_seen, bool disconnected_seen,
                                                 uint8_t consecutive_stop_failures, bool disconnect_requested) {
  if (stop_succeeded) {
    return MqttCleanupDecision::DESTROY;
  }
  // esp_mqtt_client_disconnect() only sets DISCONNECT_BIT, which a
  // no-longer-running MQTT task never processes. Allow at most one
  // FORCE_DISCONNECT; a repeated ESP_FAIL afterwards means the task is gone
  // and destroy becomes safe.
  if (connected_seen && !disconnected_seen && !disconnect_requested) {
    return MqttCleanupDecision::FORCE_DISCONNECT;
  }
  if (consecutive_stop_failures < 2U) {
    return MqttCleanupDecision::RETRY_STOP;
  }
  return MqttCleanupDecision::DESTROY_ALREADY_STOPPED;
}

}  // namespace openquatt_usage_telemetry
}  // namespace esphome
