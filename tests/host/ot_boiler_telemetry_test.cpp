#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "openquatt/includes/boiler/oq_otb_telemetry.h"

int main() {
  oq_otb::TelemetryState state;

  assert(state.accepted_response_count() == 0);
  assert(state.last_response_id() == -1);
  assert(strcmp(state.last_response_type_name(), "Never") == 0);
  assert(!state.field_is_valid(oq_otb::FIELD_CH_WATER_PRESSURE));
  assert(state.response_payload_is_usable(18, oq_otb::MESSAGE_TYPE_READ_ACK));
  assert(!state.response_payload_is_usable(18, oq_otb::MESSAGE_TYPE_WRITE_ACK));
  assert(state.response_payload_is_usable(1, oq_otb::MESSAGE_TYPE_WRITE_ACK));

  // Optional negative acknowledgements do not take down the transport, but
  // the mandatory STATUS field must be valid and fresh for safe actuation.
  oq_otb::TelemetryState link_state;
  assert(!link_state.transport_is_available(0, 1000, 1000));
  link_state.record_response(100, 18, oq_otb::MESSAGE_TYPE_READ_ACK);
  assert(!link_state.transport_is_available(100, 1000, 1000));
  link_state.record_response(150, 0, oq_otb::MESSAGE_TYPE_READ_ACK);
  assert(link_state.transport_is_available(150, 1000, 1000));
  link_state.record_response(200, 18, oq_otb::MESSAGE_TYPE_DATA_INVALID);
  assert(link_state.transport_is_available(200, 1000, 1000));
  link_state.record_response(250, 0, oq_otb::MESSAGE_TYPE_DATA_INVALID);
  assert(!link_state.transport_is_available(250, 1000, 1000));
  link_state.record_response(260, 0, oq_otb::MESSAGE_TYPE_READ_ACK);
  assert(link_state.transport_is_available(260, 1000, 1000));
  const uint32_t accepted_before_reset = link_state.accepted_response_count();
  link_state.reset_link_session();
  assert(!link_state.session_has_response());
  assert(link_state.accepted_response_count() == accepted_before_reset);
  assert(link_state.last_response_id() == -1);
  assert(strcmp(link_state.last_response_type_name(), "Never") == 0);
  assert(!link_state.field_is_valid(oq_otb::FIELD_STATUS));
  assert(!link_state.transport_is_available(260, 1000, 1000));
  link_state.record_response(270, 0, oq_otb::MESSAGE_TYPE_READ_ACK);
  assert(link_state.transport_is_available(270, 1000, 1000));
  assert(!link_state.transport_is_available(1271, 1000, 1000));
  assert(link_state.expire_response_session_if_stale(1271, 1000));
  assert(!link_state.session_has_response());
  assert(!link_state.field_is_valid(oq_otb::FIELD_STATUS));
  // Once observed, a stale session remains unavailable across a full millis
  // rollover until a genuinely new response starts a new session.
  assert(!link_state.transport_is_available(271, 1000, 1000));
  assert(!link_state.expire_response_session_if_stale(271, 1000));
  link_state.record_response(272, 0, oq_otb::MESSAGE_TYPE_READ_ACK);
  assert(link_state.transport_is_available(272, 1000, 1000));

  // A READ_ACK makes only the matching field valid. Zero-valued payloads are
  // still legitimate samples; validity comes from the response type, not data.
  state.record_response(100, 18, oq_otb::MESSAGE_TYPE_READ_ACK);
  assert(state.accepted_response_count() == 1);
  assert(state.acknowledged_response_count() == 1);
  assert(state.last_response_id() == 18);
  assert(strcmp(state.last_response_type_name(), "READ_ACK") == 0);
  assert(state.field_is_valid(oq_otb::FIELD_CH_WATER_PRESSURE));
  assert(state.field_is_fresh(oq_otb::FIELD_CH_WATER_PRESSURE, 1100, 1000));
  assert(!state.field_is_fresh(oq_otb::FIELD_CH_WATER_PRESSURE, 1101, 1000));
  assert(!state.field_is_valid(oq_otb::FIELD_BOILER_WATER_TEMPERATURE));

  // A semantic negative acknowledgement must invalidate the old sample
  // immediately while remaining a complete response for link supervision.
  state.record_response(200, 18, oq_otb::MESSAGE_TYPE_DATA_INVALID);
  assert(state.accepted_response_count() == 2);
  assert(state.data_invalid_response_count() == 1);
  assert(!state.field_is_valid(oq_otb::FIELD_CH_WATER_PRESSURE));
  assert(strcmp(state.last_response_type_name(), "DATA_INVALID") == 0);

  state.record_response(300, 18, oq_otb::MESSAGE_TYPE_UNKNOWN_DATA_ID);
  assert(state.unknown_data_id_response_count() == 1);
  assert(!state.field_is_valid(oq_otb::FIELD_CH_WATER_PRESSURE));

  // A WRITE_ACK is a valid frame for commands, but never a valid sample for
  // the read-only telemetry fields tracked here.
  state.record_response(400, 18, oq_otb::MESSAGE_TYPE_WRITE_ACK);
  assert(state.acknowledged_response_count() == 2);
  assert(!state.field_is_valid(oq_otb::FIELD_CH_WATER_PRESSURE));

  state.record_response(500, 18, oq_otb::MESSAGE_TYPE_INVALID_DATA);
  assert(state.unexpected_response_type_count() == 1);
  assert(!state.field_is_valid(oq_otb::FIELD_CH_WATER_PRESSURE));

  // Freshness calculations must remain correct across millis() wraparound.
  state.record_response(UINT32_MAX - 5, 25, oq_otb::MESSAGE_TYPE_READ_ACK);
  assert(state.field_is_fresh(oq_otb::FIELD_BOILER_WATER_TEMPERATURE, 4, 10));
  assert(!state.field_is_fresh(oq_otb::FIELD_BOILER_WATER_TEMPERATURE, 5, 10));

  // Only warnings emitted by the ESPHome OpenTherm component are counted.
  state.record_log_message(1000, "wifi", "NO_TRANSITION");
  state.record_log_message(1001, "opentherm", "Unrelated warning");
  assert(state.transport_error_count() == 0);

  state.record_log_message(1010, "opentherm", "Protocol error occured while receiving response: NO_TRANSITION");
  state.record_log_message(1020, "opentherm", "Protocol error occured while receiving response: NO_CHANGE_TOO_LONG");
  state.record_log_message(1030, "opentherm", "Protocol error occured while receiving response: INVALID_STOP_BIT");
  state.record_log_message(1040, "opentherm", "Protocol error occured while receiving response: PARITY_ERROR");
  state.record_log_message(1050, "opentherm",
                           "Timeout while waiting for response from device: no frame captured before the receive "
                           "deadline");
  state.record_log_message(1060, "opentherm", "Hub timeout triggered during send");
  state.record_log_message(1070, "opentherm", "Hub timeout triggered during receive");
  state.record_log_message(1080, "opentherm", "Error occured while manipulating timer (TIMER_START_ERROR): ESP_FAIL");

  assert(state.transport_error_count() == 8);
  assert(state.protocol_error_count() == 4);
  assert(state.no_transition_count() == 1);
  assert(state.no_change_too_long_count() == 1);
  assert(state.invalid_stop_bit_count() == 1);
  assert(state.parity_error_count() == 1);
  assert(state.response_timeout_count() == 1);
  assert(state.hub_send_timeout_count() == 1);
  assert(state.hub_receive_timeout_count() == 1);
  assert(state.timer_error_count() == 1);
  assert(state.last_transport_error_ms() == 1080);
  assert(strcmp(state.last_transport_error_name(), "TIMER_ERROR") == 0);

  return 0;
}
