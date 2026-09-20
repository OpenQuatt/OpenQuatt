#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "openquatt/includes/boiler/oq_otb_connection_state.h"

namespace {

void test_selected_opentherm_requires_correlated_response() {
  oq_otb::BoilerConnectionVerificationState state;
  state.begin_opentherm(1000);
  assert(state.state() == oq_otb::BOILER_CONNECTION_OT_CHECKING);
  assert(!state.ever_verified());
  assert(!state.update_opentherm(8999, 8000, 10000));
  assert(state.update_opentherm(9000, 8000, 10000));
  assert(state.state() == oq_otb::BOILER_CONNECTION_OT_NO_RESPONSE);
  assert(!state.ever_verified());

  assert(state.record_correlated_response(9500));
  assert(state.state() == oq_otb::BOILER_CONNECTION_OT_VERIFIED);
  assert(state.ever_verified());
  assert(state.currently_verified());

  assert(!state.update_opentherm(19500, 8000, 10000));
  assert(state.update_opentherm(19501, 8000, 10000));
  assert(state.state() == oq_otb::BOILER_CONNECTION_OT_LINK_LOST);
  assert(state.ever_verified());
  assert(!state.currently_verified());

  assert(state.record_correlated_response(20000));
  assert(state.state() == oq_otb::BOILER_CONNECTION_OT_VERIFIED);
}

void test_r1_probe_states_are_separate() {
  oq_otb::BoilerConnectionVerificationState state;
  state.begin_r1_probe();
  assert(state.state() == oq_otb::BOILER_CONNECTION_R1_CHECKING);
  state.mark_r1_ready();
  assert(state.state() == oq_otb::BOILER_CONNECTION_R1_READY);
  state.begin_r1_probe();
  state.mark_r1_opentherm_detected();
  assert(state.state() == oq_otb::BOILER_CONNECTION_R1_OPENTHERM_DETECTED);
  assert(!state.record_correlated_response(1234));
}

void test_timeouts_are_rollover_safe() {
  oq_otb::BoilerConnectionVerificationState state;
  state.begin_opentherm(UINT32_MAX - 3U);
  assert(!state.update_opentherm(2U, 7U, 10U));
  assert(state.update_opentherm(3U, 7U, 10U));
  assert(state.state() == oq_otb::BOILER_CONNECTION_OT_NO_RESPONSE);

  state.record_correlated_response(UINT32_MAX - 3U);
  assert(!state.update_opentherm(6U, 7U, 10U));
  assert(state.update_opentherm(7U, 7U, 10U));
  assert(state.state() == oq_otb::BOILER_CONNECTION_OT_LINK_LOST);
}

void test_stable_wire_values() {
  assert(strcmp(oq_otb::boiler_connection_state_text(oq_otb::BOILER_CONNECTION_OT_CHECKING), "ot_checking") == 0);
  assert(strcmp(oq_otb::boiler_connection_state_text(oq_otb::BOILER_CONNECTION_OT_VERIFIED), "ot_verified") == 0);
  assert(strcmp(oq_otb::boiler_connection_state_text(oq_otb::BOILER_CONNECTION_OT_NO_RESPONSE), "ot_no_response") == 0);
  assert(strcmp(oq_otb::boiler_connection_state_text(oq_otb::BOILER_CONNECTION_OT_LINK_LOST), "ot_link_lost") == 0);
}

}  // namespace

int main() {
  test_selected_opentherm_requires_correlated_response();
  test_r1_probe_states_are_separate();
  test_timeouts_are_rollover_safe();
  test_stable_wire_values();
  return 0;
}
