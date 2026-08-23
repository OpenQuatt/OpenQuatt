#pragma once

#include <cstddef>
#include <cstdint>

namespace oq_pump_ipwm {

enum class Status : uint8_t {
  UNKNOWN = 0U,
  PWM_SHORT = 1U,
  STANDBY = 2U,
  RUNNING = 3U,
  PUMP_ON_ABNORMAL = 4U,
  PUMP_OFF_ABNORMAL = 5U,
  PUMP_OFF_FAILURE = 6U,
  PWM_OPEN = 7U,
};

struct DecodedFeedback {
  Status status{Status::UNKNOWN};
  bool power_valid{false};
  float power_w{0.0F};
};

inline constexpr std::size_t kContextRawObservationCount = 4U;
inline constexpr uint32_t kDiagnosticContextFreshnessMs = 20000U;

constexpr std::size_t context_raw_observation_index(uint16_t register_address) {
  switch (register_address) {
    case 2010U:
      return 0U;
    case 2108U:
      return 1U;
    case 2115U:
      return 2U;
    case 2137U:
      return 3U;
    default:
      return kContextRawObservationCount;
  }
}

struct ContextRawObservation {
  bool valid{false};
  uint16_t raw{0U};
  uint32_t observed_at_ms{0U};

  void observe(uint16_t value, uint32_t now_ms) {
    this->valid = true;
    this->raw = value;
    this->observed_at_ms = now_ms;
  }

  bool read_if_fresh(uint32_t now_ms, uint16_t& value) const {
    value = this->raw;
    return this->valid && now_ms - this->observed_at_ms < kDiagnosticContextFreshnessMs;
  }
};

constexpr const char* status_name(Status status) {
  switch (status) {
    case Status::PWM_SHORT:
      return "pwm_short";
    case Status::STANDBY:
      return "standby";
    case Status::RUNNING:
      return "running";
    case Status::PUMP_ON_ABNORMAL:
      return "pump_on_abnormal";
    case Status::PUMP_OFF_ABNORMAL:
      return "pump_off_abnormal";
    case Status::PUMP_OFF_FAILURE:
      return "pump_off_failure";
    case Status::PWM_OPEN:
      return "pwm_open";
    case Status::UNKNOWN:
      break;
  }
  return "unknown";
}

// R2137 stores tenths. Raw 50-750 means 5-75 W pump power. Known values
// outside that running band are status/fault codes and never represent power.
constexpr DecodedFeedback decode(uint16_t raw) {
  DecodedFeedback result;
  if (raw == 0U) {
    result.status = Status::PWM_SHORT;
  } else if (raw == 20U) {
    result.status = Status::STANDBY;
  } else if (raw >= 50U && raw <= 750U) {
    result.status = Status::RUNNING;
    result.power_valid = true;
    result.power_w = static_cast<float>(raw) * 0.1F;
  } else if (raw == 800U) {
    result.status = Status::PUMP_ON_ABNORMAL;
  } else if (raw >= 850U && raw <= 900U) {
    result.status = Status::PUMP_OFF_ABNORMAL;
  } else if (raw == 950U) {
    result.status = Status::PUMP_OFF_FAILURE;
  } else if (raw == 1000U) {
    result.status = Status::PWM_OPEN;
  }
  return result;
}

constexpr float power_contribution_w(const DecodedFeedback& feedback, bool relay_valid, bool relay_on) {
  return relay_valid && relay_on && feedback.power_valid ? feedback.power_w : 0.0F;
}

}  // namespace oq_pump_ipwm
