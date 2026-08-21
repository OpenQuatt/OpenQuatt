#include <assert.h>

#include "../../openquatt/includes/boiler/oq_otb_telemetry.h"

namespace {

using namespace oq_otb;

void test_repeating_field_expires_by_age() {
  TelemetryState state;
  state.record_response(1000, 0, MESSAGE_TYPE_READ_ACK);
  assert(state.field_is_fresh(FIELD_STATUS, 10000, 10000));
  assert(!state.field_is_fresh(FIELD_STATUS, 11001, 10000));
}

void test_initial_fields_remain_valid_for_session() {
  TelemetryState state;

  state.record_response(1000, 3, MESSAGE_TYPE_READ_ACK);
  state.record_response(1100, 15, MESSAGE_TYPE_READ_ACK);
  state.record_response(1200, 125, MESSAGE_TYPE_READ_ACK);
  state.record_response(1300, 127, MESSAGE_TYPE_READ_ACK);

  constexpr uint32_t much_later_ms = 600000;
  constexpr uint32_t repeating_timeout_ms = 10000;
  assert(state.field_is_fresh(FIELD_DEVICE_CONFIG, much_later_ms, repeating_timeout_ms));
  assert(state.field_is_fresh(FIELD_MAX_BOILER_CAPACITY, much_later_ms, repeating_timeout_ms));
  assert(state.field_is_fresh(FIELD_OT_VERSION_DEVICE, much_later_ms, repeating_timeout_ms));
  assert(state.field_is_fresh(FIELD_VERSION_DEVICE, much_later_ms, repeating_timeout_ms));
}

void test_negative_ack_invalidates_initial_field() {
  TelemetryState state;
  state.record_response(1000, 15, MESSAGE_TYPE_READ_ACK);
  assert(state.field_is_valid(FIELD_MAX_BOILER_CAPACITY));

  state.record_response(2000, 15, MESSAGE_TYPE_UNKNOWN_DATA_ID);
  assert(!state.field_is_valid(FIELD_MAX_BOILER_CAPACITY));
  assert(!state.field_is_fresh(FIELD_MAX_BOILER_CAPACITY, 3000, 10000));
}

void test_session_reset_invalidates_initial_field() {
  TelemetryState state;
  state.record_response(1000, 15, MESSAGE_TYPE_READ_ACK);
  assert(state.field_is_valid(FIELD_MAX_BOILER_CAPACITY));

  state.reset_link_session();
  assert(!state.field_is_valid(FIELD_MAX_BOILER_CAPACITY));
  assert(!state.field_is_fresh(FIELD_MAX_BOILER_CAPACITY, 2000, 10000));
}

void test_stale_link_expiry_invalidates_initial_field() {
  TelemetryState state;
  state.record_response(1000, 15, MESSAGE_TYPE_READ_ACK);
  assert(state.field_is_valid(FIELD_MAX_BOILER_CAPACITY));

  assert(state.expire_response_session_if_stale(12001, 10000));
  assert(!state.field_is_valid(FIELD_MAX_BOILER_CAPACITY));
}

}  // namespace

int main() {
  test_repeating_field_expires_by_age();
  test_initial_fields_remain_valid_for_session();
  test_negative_ack_invalidates_initial_field();
  test_session_reset_invalidates_initial_field();
  test_stale_link_expiry_invalidates_initial_field();
  return 0;
}
