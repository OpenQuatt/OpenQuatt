#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "oq_otb_telemetry.h"

namespace oq_otb {

constexpr uint8_t START_HANDSHAKE_ID_STATUS = 0;
constexpr uint8_t START_HANDSHAKE_ID_CH_SETPOINT = 1;
constexpr uint32_t START_HANDSHAKE_TIMEOUT_MS = 10000;

enum StartHandshakeResult : uint8_t {
  START_HANDSHAKE_IDLE = 0,
  START_HANDSHAKE_WAIT_TSET_ACK,
  START_HANDSHAKE_WAIT_STATUS_ACK,
  START_HANDSHAKE_ACCEPTED,
  START_HANDSHAKE_TSET_REJECTED,
  START_HANDSHAKE_TSET_UNSUPPORTED,
  START_HANDSHAKE_STATUS_REJECTED,
  START_HANDSHAKE_STATUS_UNSUPPORTED,
  START_HANDSHAKE_REQUEST_MISMATCH,
  START_HANDSHAKE_RESPONSE_ID_MISMATCH,
  START_HANDSHAKE_RESPONSE_TYPE_MISMATCH,
  START_HANDSHAKE_CH_ENABLE_NOT_SENT,
  START_HANDSHAKE_CH_ENABLE_NOT_ECHOED,
  START_HANDSHAKE_TIMED_OUT,
  START_HANDSHAKE_CANCELLED,
};

class StartHandshakeState {
 public:
  void reset() { *this = {}; }

  void begin(uint32_t now_ms) {
    const uint32_t next_generation = this->generation_ + 1U;
    *this = {};
    this->generation_ = next_generation == 0U ? 1U : next_generation;
    this->started_ms_ = now_ms;
    this->result_ = START_HANDSHAKE_WAIT_TSET_ACK;
  }

  void cancel() {
    if (!this->waiting()) return;
    this->result_ = START_HANDSHAKE_CANCELLED;
    this->pending_request_ = false;
  }

  void update_timeout(uint32_t now_ms) {
    if (!this->waiting() || (uint32_t)(now_ms - this->started_ms_) <= START_HANDSHAKE_TIMEOUT_MS) return;
    this->result_ = START_HANDSHAKE_TIMED_OUT;
    this->pending_request_ = false;
  }

  void record_request(uint8_t message_id, uint8_t message_type, uint8_t value_hb, uint8_t value_lb) {
    if (!this->waiting()) return;

    const bool expected_tset = this->result_ == START_HANDSHAKE_WAIT_TSET_ACK &&
                               message_id == START_HANDSHAKE_ID_CH_SETPOINT && message_type == MESSAGE_TYPE_WRITE_DATA;
    const bool expected_status = this->result_ == START_HANDSHAKE_WAIT_STATUS_ACK &&
                                 message_id == START_HANDSHAKE_ID_STATUS && message_type == MESSAGE_TYPE_READ_DATA;
    if (!expected_tset && !expected_status) {
      this->last_request_id_ = message_id;
      this->last_request_type_ = message_type;
      this->result_ = START_HANDSHAKE_REQUEST_MISMATCH;
      this->pending_request_ = false;
      return;
    }

    this->pending_request_ = true;
    this->last_request_id_ = message_id;
    this->last_request_type_ = message_type;
    const uint16_t request_value = (static_cast<uint16_t>(value_hb) << 8U) | value_lb;

    if (expected_tset) {
      this->tset_request_seen_ = true;
      this->requested_tset_raw_ = request_value;
    } else {
      this->status_master_hb_sent_ = value_hb;
    }
  }

  void record_response(uint8_t message_id, uint8_t message_type, uint8_t value_hb, uint8_t value_lb) {
    if (!this->waiting() || !this->pending_request_) return;

    this->last_response_id_ = message_id;
    this->last_response_type_ = message_type;
    const uint16_t response_value = (static_cast<uint16_t>(value_hb) << 8U) | value_lb;
    const ResponseCorrelation correlation =
        classify_response(this->last_request_id_, this->last_request_type_, message_id, message_type);
    this->pending_request_ = false;

    if (correlation == RESPONSE_CORRELATION_ID_MISMATCH) {
      this->result_ = START_HANDSHAKE_RESPONSE_ID_MISMATCH;
      return;
    }
    if (correlation == RESPONSE_CORRELATION_TYPE_MISMATCH) {
      this->result_ = START_HANDSHAKE_RESPONSE_TYPE_MISMATCH;
      return;
    }
    if (correlation == RESPONSE_CORRELATION_NEGATIVE) {
      this->record_negative_response_(message_type);
      return;
    }

    if (this->result_ == START_HANDSHAKE_WAIT_TSET_ACK) {
      this->accepted_tset_raw_ = response_value;
      this->tset_ack_seen_ = true;
      this->result_ = START_HANDSHAKE_WAIT_STATUS_ACK;
      return;
    }

    this->status_master_hb_echoed_ = value_hb;
    this->status_slave_lb_ = value_lb;
    if ((this->status_master_hb_sent_ & 0x01U) == 0U) {
      this->result_ = START_HANDSHAKE_CH_ENABLE_NOT_SENT;
    } else if ((this->status_master_hb_echoed_ & 0x01U) == 0U) {
      this->result_ = START_HANDSHAKE_CH_ENABLE_NOT_ECHOED;
    } else {
      this->result_ = START_HANDSHAKE_ACCEPTED;
    }
  }

  bool waiting() const {
    return this->result_ == START_HANDSHAKE_WAIT_TSET_ACK || this->result_ == START_HANDSHAKE_WAIT_STATUS_ACK;
  }
  StartHandshakeResult result() const { return this->result_; }
  uint32_t generation() const { return this->generation_; }
  bool tset_request_seen() const { return this->tset_request_seen_; }
  bool tset_ack_seen() const { return this->tset_ack_seen_; }
  float requested_tset_c() const { return this->tset_request_seen_ ? this->requested_tset_raw_ / 256.0f : 0.0f; }
  float accepted_tset_c() const { return this->tset_ack_seen_ ? this->accepted_tset_raw_ / 256.0f : 0.0f; }

  const char* result_name() const {
    switch (this->result_) {
      case START_HANDSHAKE_WAIT_TSET_ACK:
        return "Waiting for TSet ACK";
      case START_HANDSHAKE_WAIT_STATUS_ACK:
        return "Waiting for STATUS ACK";
      case START_HANDSHAKE_ACCEPTED:
        return "Accepted";
      case START_HANDSHAKE_TSET_REJECTED:
        return "TSet rejected";
      case START_HANDSHAKE_TSET_UNSUPPORTED:
        return "TSet unsupported";
      case START_HANDSHAKE_STATUS_REJECTED:
        return "STATUS rejected";
      case START_HANDSHAKE_STATUS_UNSUPPORTED:
        return "STATUS unsupported";
      case START_HANDSHAKE_REQUEST_MISMATCH:
        return "Request mismatch";
      case START_HANDSHAKE_RESPONSE_ID_MISMATCH:
        return "Response ID mismatch";
      case START_HANDSHAKE_RESPONSE_TYPE_MISMATCH:
        return "Response type mismatch";
      case START_HANDSHAKE_CH_ENABLE_NOT_SENT:
        return "CH enable not sent";
      case START_HANDSHAKE_CH_ENABLE_NOT_ECHOED:
        return "CH enable not echoed";
      case START_HANDSHAKE_TIMED_OUT:
        return "Timed out";
      case START_HANDSHAKE_CANCELLED:
        return "Cancelled";
      default:
        return "Idle";
    }
  }

  void format_detail(char* buffer, size_t size) const {
    if (buffer == nullptr || size == 0U) return;
    snprintf(buffer, size, "gen=%u TSet=0x%04X/0x%04X STATUS=0x%02X/0x%02X%02X last=%d/%d:%d/%d",
             static_cast<unsigned>(this->generation_), static_cast<unsigned>(this->requested_tset_raw_),
             static_cast<unsigned>(this->accepted_tset_raw_), static_cast<unsigned>(this->status_master_hb_sent_),
             static_cast<unsigned>(this->status_master_hb_echoed_), static_cast<unsigned>(this->status_slave_lb_),
             static_cast<int>(this->last_request_id_), static_cast<int>(this->last_request_type_),
             static_cast<int>(this->last_response_id_), static_cast<int>(this->last_response_type_));
  }

 private:
  void record_negative_response_(uint8_t message_type) {
    const bool unsupported = message_type == MESSAGE_TYPE_UNKNOWN_DATA_ID;
    if (this->result_ == START_HANDSHAKE_WAIT_TSET_ACK) {
      this->result_ = unsupported ? START_HANDSHAKE_TSET_UNSUPPORTED : START_HANDSHAKE_TSET_REJECTED;
    } else {
      this->result_ = unsupported ? START_HANDSHAKE_STATUS_UNSUPPORTED : START_HANDSHAKE_STATUS_REJECTED;
    }
  }

  StartHandshakeResult result_{START_HANDSHAKE_IDLE};
  uint32_t generation_{0};
  uint32_t started_ms_{0};
  bool pending_request_{false};
  int16_t last_request_id_{-1};
  int16_t last_request_type_{-1};
  int16_t last_response_id_{-1};
  int16_t last_response_type_{-1};
  bool tset_request_seen_{false};
  bool tset_ack_seen_{false};
  uint16_t requested_tset_raw_{0};
  uint16_t accepted_tset_raw_{0};
  uint8_t status_master_hb_sent_{0};
  uint8_t status_master_hb_echoed_{0};
  uint8_t status_slave_lb_{0};
};

inline StartHandshakeState start_handshake_state{};

}  // namespace oq_otb
