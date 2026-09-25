#include <assert.h>

#include "../../openquatt/includes/boiler/oq_otb_telemetry.h"

int main() {
  oq_otb::TelemetryState state;
  const auto initial = state.field_receipt(oq_otb::FIELD_STATUS);
  assert(!initial.received && !initial.valid);

  // The legacy overload may still validate control telemetry, but it cannot
  // expose an invented zero payload as a physical receipt.
  state.record_request(0, oq_otb::MESSAGE_TYPE_READ_DATA);
  state.record_response(50U, 0, oq_otb::MESSAGE_TYPE_READ_ACK);
  const auto unknown = state.field_receipt(oq_otb::FIELD_STATUS);
  assert(unknown.received && !unknown.valid && unknown.value == 0U);
  assert(state.field_is_valid(oq_otb::FIELD_STATUS));

  state.record_request(0, oq_otb::MESSAGE_TYPE_READ_DATA);
  state.record_response(100U, 0, oq_otb::MESSAGE_TYPE_READ_ACK, 0x1234U);
  const auto accepted = state.field_receipt(oq_otb::FIELD_STATUS);
  assert(accepted.received && accepted.valid && accepted.value == 0x1234U);
  assert(accepted.received_ms == 100U);

  state.record_request(0, oq_otb::MESSAGE_TYPE_READ_DATA);
  state.record_response(200U, 0, oq_otb::MESSAGE_TYPE_DATA_INVALID, 0U);
  const auto rejected = state.field_receipt(oq_otb::FIELD_STATUS);
  assert(rejected.received && !rejected.valid && rejected.received_ms == 200U);

  // UNKNOWN_DATA_ID with a zero payload must remain unknown, never NO_HEAT.
  state.record_request(0, oq_otb::MESSAGE_TYPE_READ_DATA);
  state.record_response(300U, 0, oq_otb::MESSAGE_TYPE_UNKNOWN_DATA_ID, 0U);
  const auto unsupported = state.field_receipt(oq_otb::FIELD_STATUS);
  assert(unsupported.received && !unsupported.valid && unsupported.value == 0U);

  // A failed/mismatched exchange invalidates the requested field; a later
  // correlated retry must atomically replace its value and receipt timestamp.
  state.record_request(0, oq_otb::MESSAGE_TYPE_READ_DATA);
  state.record_response(400U, 25, oq_otb::MESSAGE_TYPE_READ_ACK, 0x4500U);
  const auto mismatch = state.field_receipt(oq_otb::FIELD_STATUS);
  assert(mismatch.received && !mismatch.valid && mismatch.received_ms == 300U && mismatch.value == 0U);
  state.record_request(0, oq_otb::MESSAGE_TYPE_READ_DATA);
  constexpr uint64_t receipt_after_32_bit_wrap = static_cast<uint64_t>(UINT32_MAX) + 5000U;
  state.record_response(UINT32_MAX - 2U, receipt_after_32_bit_wrap, 0, oq_otb::MESSAGE_TYPE_READ_ACK, 0x0008U);
  const auto retry = state.field_receipt(oq_otb::FIELD_STATUS);
  assert(retry.received && retry.valid && retry.value == 0x0008U);
  assert(retry.received_ms == receipt_after_32_bit_wrap);

  state.reset_link_session();
  const auto reset = state.field_receipt(oq_otb::FIELD_STATUS);
  assert(!reset.received && !reset.valid && reset.received_ms == 0U);
  return 0;
}
