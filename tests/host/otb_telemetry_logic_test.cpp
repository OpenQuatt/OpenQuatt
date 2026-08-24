#include <assert.h>

#include "../../openquatt/includes/boiler/oq_otb_telemetry.h"

namespace {

using namespace oq_otb;

void record_read_response(TelemetryState& state, uint32_t now_ms, uint8_t message_id, uint8_t response_type) {
  state.record_request(message_id, MESSAGE_TYPE_READ_DATA);
  state.record_response(now_ms, message_id, response_type);
}

void test_repeating_field_expires_by_age() {
  TelemetryState state;
  record_read_response(state, 1000, 0, MESSAGE_TYPE_READ_ACK);
  assert(state.field_is_fresh(FIELD_STATUS, 10000, 10000));
  assert(!state.field_is_fresh(FIELD_STATUS, 11001, 10000));
}

void test_initial_fields_remain_valid_for_session() {
  TelemetryState state;

  record_read_response(state, 1000, 3, MESSAGE_TYPE_READ_ACK);
  record_read_response(state, 1100, 15, MESSAGE_TYPE_READ_ACK);
  record_read_response(state, 1200, 125, MESSAGE_TYPE_READ_ACK);
  record_read_response(state, 1300, 127, MESSAGE_TYPE_READ_ACK);

  constexpr uint32_t much_later_ms = 600000;
  constexpr uint32_t repeating_timeout_ms = 10000;
  assert(state.field_is_fresh(FIELD_DEVICE_CONFIG, much_later_ms, repeating_timeout_ms));
  assert(state.field_is_fresh(FIELD_MAX_BOILER_CAPACITY, much_later_ms, repeating_timeout_ms));
  assert(state.field_is_fresh(FIELD_OT_VERSION_DEVICE, much_later_ms, repeating_timeout_ms));
  assert(state.field_is_fresh(FIELD_VERSION_DEVICE, much_later_ms, repeating_timeout_ms));
}

void test_negative_ack_invalidates_initial_field() {
  TelemetryState state;
  record_read_response(state, 1000, 15, MESSAGE_TYPE_READ_ACK);
  assert(state.field_is_valid(FIELD_MAX_BOILER_CAPACITY));

  record_read_response(state, 2000, 15, MESSAGE_TYPE_UNKNOWN_DATA_ID);
  assert(!state.field_is_valid(FIELD_MAX_BOILER_CAPACITY));
  assert(!state.field_is_fresh(FIELD_MAX_BOILER_CAPACITY, 3000, 10000));
}

void test_session_reset_invalidates_initial_field() {
  TelemetryState state;
  record_read_response(state, 1000, 15, MESSAGE_TYPE_READ_ACK);
  assert(state.field_is_valid(FIELD_MAX_BOILER_CAPACITY));

  state.reset_link_session();
  assert(!state.field_is_valid(FIELD_MAX_BOILER_CAPACITY));
  assert(!state.field_is_fresh(FIELD_MAX_BOILER_CAPACITY, 2000, 10000));
}

void test_stale_link_expiry_invalidates_initial_field() {
  TelemetryState state;
  record_read_response(state, 1000, 15, MESSAGE_TYPE_READ_ACK);
  assert(state.field_is_valid(FIELD_MAX_BOILER_CAPACITY));

  assert(state.expire_response_session_if_stale(12001, 10000));
  assert(!state.field_is_valid(FIELD_MAX_BOILER_CAPACITY));
}

void test_response_correlation_rejects_mismatched_payloads() {
  TelemetryState state;
  record_read_response(state, 1000, 0, MESSAGE_TYPE_READ_ACK);
  assert(state.field_is_valid(FIELD_STATUS));
  assert(state.last_response_payload_is_usable());

  state.record_request(17, MESSAGE_TYPE_READ_DATA);
  state.record_response(1100, 0, MESSAGE_TYPE_READ_ACK);
  assert(state.response_id_mismatch_count() == 1);
  assert(!state.last_response_is_correlated());
  assert(!state.last_response_payload_is_usable());
  assert(!state.field_is_valid(FIELD_RELATIVE_MODULATION));

  state.record_request(0, MESSAGE_TYPE_READ_DATA);
  state.record_response(1200, 0, MESSAGE_TYPE_WRITE_ACK);
  assert(state.response_type_mismatch_count() == 1);
  assert(!state.field_is_valid(FIELD_STATUS));

  state.record_response(1300, 0, MESSAGE_TYPE_READ_ACK);
  assert(state.orphan_response_count() == 1);
  assert(!state.last_response_is_correlated());
}

}  // namespace

int main() {
  test_repeating_field_expires_by_age();
  test_initial_fields_remain_valid_for_session();
  test_negative_ack_invalidates_initial_field();
  test_session_reset_invalidates_initial_field();
  test_stale_link_expiry_invalidates_initial_field();
  test_response_correlation_rejects_mismatched_payloads();
  return 0;
}
