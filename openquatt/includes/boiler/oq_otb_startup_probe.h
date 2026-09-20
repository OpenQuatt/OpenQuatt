#pragma once

#include <stdint.h>

namespace oq_otb {

constexpr uint8_t STARTUP_PROBE_TYPE_READ_DATA = 0;
constexpr uint8_t STARTUP_PROBE_TYPE_READ_ACK = 4;
constexpr uint8_t STARTUP_PROBE_TYPE_DATA_INVALID = 6;
constexpr uint8_t STARTUP_PROBE_TYPE_UNKNOWN_DATAID = 7;
constexpr uint8_t STARTUP_PROBE_ID_STATUS = 0;

enum StartupProbeResult : uint8_t {
  STARTUP_PROBE_IDLE = 0,
  STARTUP_PROBE_RUNNING,
  STARTUP_PROBE_OPENTHERM_DETECTED,
  STARTUP_PROBE_TIMED_OUT,
};

inline bool should_auto_select_opentherm(StartupProbeResult result, bool setup_complete) {
  return result == STARTUP_PROBE_OPENTHERM_DETECTED && !setup_complete;
}

inline bool should_keep_opentherm_polling(bool source_present, bool opentherm_selected, bool startup_probe_active,
                                          bool connection_mismatch) {
  return source_present && (opentherm_selected || startup_probe_active || connection_mismatch);
}

class StartupProbeState {
 public:
  void begin(uint32_t now_ms) {
    this->active_ = true;
    this->started_ms_ = now_ms;
    this->safe_status_sent_ = false;
    this->opentherm_detected_ = false;
  }

  void end() { this->active_ = false; }

  void record_request(uint8_t message_id, uint8_t message_type, uint8_t value_hb, uint8_t value_lb) {
    (void)value_lb;
    if (!this->active_) return;

    if (message_id == STARTUP_PROBE_ID_STATUS && message_type == STARTUP_PROBE_TYPE_READ_DATA &&
        (value_hb & 0x01U) == 0U) {
      this->safe_status_sent_ = true;
    }
  }

  void record_response(uint8_t message_id, uint8_t message_type) {
    if (!this->active_ || !this->safe_status_sent_) return;

    const bool valid_status_response = message_type == STARTUP_PROBE_TYPE_READ_ACK ||
                                       message_type == STARTUP_PROBE_TYPE_DATA_INVALID ||
                                       message_type == STARTUP_PROBE_TYPE_UNKNOWN_DATAID;
    if (message_id == STARTUP_PROBE_ID_STATUS && valid_status_response) {
      this->opentherm_detected_ = true;
    }
  }

  StartupProbeResult result(uint32_t now_ms, uint32_t timeout_ms) const {
    if (!this->active_) return STARTUP_PROBE_IDLE;
    if (this->opentherm_detected_) {
      return STARTUP_PROBE_OPENTHERM_DETECTED;
    }
    if (timeout_ms == 0 || (uint32_t)(now_ms - this->started_ms_) >= timeout_ms) {
      return STARTUP_PROBE_TIMED_OUT;
    }
    return STARTUP_PROBE_RUNNING;
  }

  bool active() const { return this->active_; }
  bool opentherm_detected() const { return this->opentherm_detected_; }

 private:
  bool active_{false};
  bool safe_status_sent_{false};
  bool opentherm_detected_{false};
  uint32_t started_ms_{0};
};

inline StartupProbeState startup_probe_state;

}  // namespace oq_otb
