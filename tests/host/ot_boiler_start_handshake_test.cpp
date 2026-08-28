#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "openquatt/includes/boiler/oq_otb_start_handshake.h"

namespace {

using namespace oq_otb;

void send_tset(StartHandshakeState& state, uint16_t raw_tset) {
  state.record_request(START_HANDSHAKE_ID_CH_SETPOINT, MESSAGE_TYPE_WRITE_DATA, static_cast<uint8_t>(raw_tset >> 8U),
                       static_cast<uint8_t>(raw_tset));
}

void acknowledge_tset(StartHandshakeState& state, uint16_t raw_tset) {
  state.record_response(START_HANDSHAKE_ID_CH_SETPOINT, MESSAGE_TYPE_WRITE_ACK, static_cast<uint8_t>(raw_tset >> 8U),
                        static_cast<uint8_t>(raw_tset));
}

void send_status(StartHandshakeState& state, uint8_t master_hb) {
  state.record_request(START_HANDSHAKE_ID_STATUS, MESSAGE_TYPE_READ_DATA, master_hb, 0);
}

void test_happy_path_records_requested_and_accepted_values() {
  StartHandshakeState state;
  state.begin(1000);
  assert(state.generation() == 1);
  assert(state.result() == START_HANDSHAKE_WAIT_TSET_ACK);

  send_tset(state, 0x3700);
  acknowledge_tset(state, 0x3600);
  assert(state.result() == START_HANDSHAKE_WAIT_STATUS_ACK);
  assert(fabsf(state.requested_tset_c() - 55.0f) < 0.001f);
  assert(fabsf(state.accepted_tset_c() - 54.0f) < 0.001f);

  send_status(state, 0x01);
  state.record_response(START_HANDSHAKE_ID_STATUS, MESSAGE_TYPE_READ_ACK, 0x01, 0x0A);
  assert(state.result() == START_HANDSHAKE_ACCEPTED);

  char detail[128];
  state.format_detail(detail, sizeof(detail));
  assert(strstr(detail, "TSet=0x3700/0x3600") != nullptr);
  assert(strstr(detail, "STATUS=0x01/0x010A") != nullptr);
}

void test_negative_tset_responses_are_specific() {
  StartHandshakeState rejected;
  rejected.begin(0);
  send_tset(rejected, 0x3700);
  rejected.record_response(START_HANDSHAKE_ID_CH_SETPOINT, MESSAGE_TYPE_DATA_INVALID, 0, 0);
  assert(rejected.result() == START_HANDSHAKE_TSET_REJECTED);

  StartHandshakeState unsupported;
  unsupported.begin(0);
  send_tset(unsupported, 0x3700);
  unsupported.record_response(START_HANDSHAKE_ID_CH_SETPOINT, MESSAGE_TYPE_UNKNOWN_DATA_ID, 0, 0);
  assert(unsupported.result() == START_HANDSHAKE_TSET_UNSUPPORTED);
}

void test_response_id_and_type_mismatches_fail_closed() {
  StartHandshakeState wrong_id;
  wrong_id.begin(0);
  send_tset(wrong_id, 0x3700);
  wrong_id.record_response(START_HANDSHAKE_ID_STATUS, MESSAGE_TYPE_WRITE_ACK, 0x37, 0);
  assert(wrong_id.result() == START_HANDSHAKE_RESPONSE_ID_MISMATCH);

  StartHandshakeState wrong_type;
  wrong_type.begin(0);
  send_tset(wrong_type, 0x3700);
  wrong_type.record_response(START_HANDSHAKE_ID_CH_SETPOINT, MESSAGE_TYPE_READ_ACK, 0x37, 0);
  assert(wrong_type.result() == START_HANDSHAKE_RESPONSE_TYPE_MISMATCH);
}

void test_status_requires_ch_enable_echo() {
  StartHandshakeState state;
  state.begin(0);
  send_tset(state, 0x3700);
  acknowledge_tset(state, 0x3700);
  send_status(state, 0x01);
  state.record_response(START_HANDSHAKE_ID_STATUS, MESSAGE_TYPE_READ_ACK, 0x00, 0x00);
  assert(state.result() == START_HANDSHAKE_CH_ENABLE_NOT_ECHOED);
}

void test_timeout_cancel_and_generation_are_bounded() {
  StartHandshakeState state;
  state.begin(UINT32_MAX - 5000U);
  state.update_timeout(4999U);
  assert(state.waiting());
  state.update_timeout(5000U);
  assert(state.result() == START_HANDSHAKE_TIMED_OUT);

  state.begin(6000U);
  assert(state.generation() == 2);
  state.cancel();
  assert(state.result() == START_HANDSHAKE_CANCELLED);
}

void test_response_from_preexisting_exchange_is_ignored() {
  StartHandshakeState state;
  state.begin(100);
  state.record_response(START_HANDSHAKE_ID_STATUS, MESSAGE_TYPE_READ_ACK, 0, 0);
  assert(state.result() == START_HANDSHAKE_WAIT_TSET_ACK);
}

}  // namespace

int main() {
  test_happy_path_records_requested_and_accepted_values();
  test_negative_tset_responses_are_specific();
  test_response_id_and_type_mismatches_fail_closed();
  test_status_requires_ch_enable_echo();
  test_timeout_cancel_and_generation_are_bounded();
  test_response_from_preexisting_exchange_is_ignored();
  return 0;
}
