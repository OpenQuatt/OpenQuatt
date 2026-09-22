#pragma once

#ifndef OPENQUATT_OQ_OTB_CONNECTION_STATE_H_
#define OPENQUATT_OQ_OTB_CONNECTION_STATE_H_

#include <stdint.h>

namespace oq_otb {

enum BoilerConnectionState : uint8_t {
  BOILER_CONNECTION_UNKNOWN = 0,
  BOILER_CONNECTION_R1_CHECKING,
  BOILER_CONNECTION_R1_READY,
  BOILER_CONNECTION_R1_OPENTHERM_DETECTED,
  BOILER_CONNECTION_OT_CHECKING,
  BOILER_CONNECTION_OT_VERIFIED,
  BOILER_CONNECTION_OT_NO_RESPONSE,
  BOILER_CONNECTION_OT_LINK_LOST,
};

inline const char* boiler_connection_state_text(BoilerConnectionState state) {
  switch (state) {
    case BOILER_CONNECTION_R1_CHECKING:
      return "r1_checking";
    case BOILER_CONNECTION_R1_READY:
      return "r1_ready";
    case BOILER_CONNECTION_R1_OPENTHERM_DETECTED:
      return "r1_opentherm_detected";
    case BOILER_CONNECTION_OT_CHECKING:
      return "ot_checking";
    case BOILER_CONNECTION_OT_VERIFIED:
      return "ot_verified";
    case BOILER_CONNECTION_OT_NO_RESPONSE:
      return "ot_no_response";
    case BOILER_CONNECTION_OT_LINK_LOST:
      return "ot_link_lost";
    default:
      return "unknown";
  }
}

class BoilerConnectionVerificationState {
 public:
  void reset() {
    this->state_ = BOILER_CONNECTION_UNKNOWN;
    this->opentherm_selected_ = false;
    this->ever_verified_ = false;
    this->verification_started_ms_ = 0;
    this->last_correlated_response_ms_ = 0;
  }

  void begin_opentherm(uint32_t now_ms) {
    this->state_ = BOILER_CONNECTION_OT_CHECKING;
    this->opentherm_selected_ = true;
    this->ever_verified_ = false;
    this->verification_started_ms_ = now_ms;
    this->last_correlated_response_ms_ = 0;
  }

  void begin_r1_probe() {
    this->state_ = BOILER_CONNECTION_R1_CHECKING;
    this->opentherm_selected_ = false;
    this->ever_verified_ = false;
    this->verification_started_ms_ = 0;
    this->last_correlated_response_ms_ = 0;
  }

  void mark_r1_ready() {
    if (this->opentherm_selected_) return;
    this->state_ = BOILER_CONNECTION_R1_READY;
  }

  void mark_r1_opentherm_detected() {
    if (this->opentherm_selected_) return;
    this->state_ = BOILER_CONNECTION_R1_OPENTHERM_DETECTED;
  }

  bool record_correlated_response(uint32_t now_ms) {
    if (!this->opentherm_selected_) return false;
    const bool changed = this->state_ != BOILER_CONNECTION_OT_VERIFIED;
    this->ever_verified_ = true;
    this->last_correlated_response_ms_ = now_ms;
    this->state_ = BOILER_CONNECTION_OT_VERIFIED;
    return changed;
  }

  bool update_opentherm(uint32_t now_ms, uint32_t verification_timeout_ms, uint32_t link_timeout_ms) {
    if (!this->opentherm_selected_) return false;
    const BoilerConnectionState previous = this->state_;
    if (!this->ever_verified_) {
      if (verification_timeout_ms == 0 ||
          (uint32_t)(now_ms - this->verification_started_ms_) >= verification_timeout_ms) {
        this->state_ = BOILER_CONNECTION_OT_NO_RESPONSE;
      } else {
        this->state_ = BOILER_CONNECTION_OT_CHECKING;
      }
    } else {
      const bool response_fresh =
          link_timeout_ms == 0 || (uint32_t)(now_ms - this->last_correlated_response_ms_) <= link_timeout_ms;
      this->state_ = response_fresh ? BOILER_CONNECTION_OT_VERIFIED : BOILER_CONNECTION_OT_LINK_LOST;
    }
    return previous != this->state_;
  }

  BoilerConnectionState state() const { return this->state_; }
  bool opentherm_selected() const { return this->opentherm_selected_; }
  bool ever_verified() const { return this->ever_verified_; }
  bool currently_verified() const { return this->state_ == BOILER_CONNECTION_OT_VERIFIED; }

 private:
  BoilerConnectionState state_{BOILER_CONNECTION_UNKNOWN};
  bool opentherm_selected_{false};
  bool ever_verified_{false};
  uint32_t verification_started_ms_{0};
  uint32_t last_correlated_response_ms_{0};
};

inline BoilerConnectionVerificationState connection_verification_state{};

}  // namespace oq_otb

#endif  // OPENQUATT_OQ_OTB_CONNECTION_STATE_H_
