#include <assert.h>
#include <stdint.h>

#include "openquatt/includes/boiler/oq_otb_startup_probe.h"

using oq_otb::StartupProbeState;

int main() {
  StartupProbeState state;

  assert(oq_otb::should_auto_select_opentherm(oq_otb::STARTUP_PROBE_OPENTHERM_DETECTED, false));
  assert(!oq_otb::should_auto_select_opentherm(oq_otb::STARTUP_PROBE_OPENTHERM_DETECTED, true));
  assert(!oq_otb::should_auto_select_opentherm(oq_otb::STARTUP_PROBE_TIMED_OUT, false));

  // Physical source absence is the top-level lifecycle gate. With a source
  // present, R1 stays idle outside the bounded probe while a proven mismatch
  // deliberately keeps safe polling active.
  assert(!oq_otb::should_keep_opentherm_polling(false, true, false, false));
  assert(!oq_otb::should_keep_opentherm_polling(false, false, true, false));
  assert(!oq_otb::should_keep_opentherm_polling(false, false, false, true));
  assert(!oq_otb::should_keep_opentherm_polling(true, false, false, false));
  assert(oq_otb::should_keep_opentherm_polling(true, true, false, false));
  assert(oq_otb::should_keep_opentherm_polling(true, false, true, false));
  assert(oq_otb::should_keep_opentherm_polling(true, false, false, true));

  assert(state.result(0, 8000) == oq_otb::STARTUP_PROBE_IDLE);
  state.begin(1000);
  assert(state.active());
  // The probe object is authoritative even if a diagnostic mirror is stale.
  assert(oq_otb::should_keep_opentherm_polling(true, false, state.active(), false));
  assert(state.result(1000, 8000) == oq_otb::STARTUP_PROBE_RUNNING);

  // A response cannot prove the link unless this probe first emitted its own
  // safe STATUS request.
  state.record_response(oq_otb::STARTUP_PROBE_ID_STATUS, oq_otb::STARTUP_PROBE_TYPE_READ_ACK);
  assert(!state.opentherm_detected());
  state.record_response(oq_otb::STARTUP_PROBE_ID_STATUS, oq_otb::STARTUP_PROBE_TYPE_DATA_INVALID);
  assert(!state.opentherm_detected());

  // STATUS with CH enabled is never accepted as a safe probe request.
  state.record_request(oq_otb::STARTUP_PROBE_ID_STATUS, oq_otb::STARTUP_PROBE_TYPE_READ_DATA, 1, 0);
  state.record_response(oq_otb::STARTUP_PROBE_ID_STATUS, oq_otb::STARTUP_PROBE_TYPE_READ_ACK);
  assert(!state.opentherm_detected());

  // Only a matching response after STATUS(CH=off) proves that an OpenTherm
  // boiler is physically responding.
  state.record_request(oq_otb::STARTUP_PROBE_ID_STATUS, oq_otb::STARTUP_PROBE_TYPE_READ_DATA, 0, 0);
  state.record_response(oq_otb::STARTUP_PROBE_ID_STATUS, oq_otb::STARTUP_PROBE_TYPE_READ_DATA);
  assert(!state.opentherm_detected());
  state.record_response(oq_otb::STARTUP_PROBE_ID_STATUS + 1, oq_otb::STARTUP_PROBE_TYPE_DATA_INVALID);
  assert(!state.opentherm_detected());
  state.record_response(oq_otb::STARTUP_PROBE_ID_STATUS, oq_otb::STARTUP_PROBE_TYPE_READ_ACK);
  assert(state.opentherm_detected());
  assert(state.result(1001, 8000) == oq_otb::STARTUP_PROBE_OPENTHERM_DETECTED);

  state.end();
  assert(!state.active());
  assert(!oq_otb::should_keep_opentherm_polling(true, false, state.active(), false));
  assert(state.result(1002, 8000) == oq_otb::STARTUP_PROBE_IDLE);

  // A valid negative acknowledgement still proves that a boiler is connected.
  state.begin(2000);
  state.record_request(oq_otb::STARTUP_PROBE_ID_STATUS, oq_otb::STARTUP_PROBE_TYPE_READ_DATA, 0, 0);
  state.record_response(oq_otb::STARTUP_PROBE_ID_STATUS, oq_otb::STARTUP_PROBE_TYPE_DATA_INVALID);
  assert(state.opentherm_detected());
  state.end();

  state.begin(3000);
  state.record_request(oq_otb::STARTUP_PROBE_ID_STATUS, oq_otb::STARTUP_PROBE_TYPE_READ_DATA, 0, 0);
  state.record_response(oq_otb::STARTUP_PROBE_ID_STATUS, oq_otb::STARTUP_PROBE_TYPE_UNKNOWN_DATAID);
  assert(state.opentherm_detected());
  state.end();

  // A new generation cannot reuse proof from the previous probe.
  state.begin(UINT32_MAX - 5);
  assert(!state.opentherm_detected());
  assert(state.result(1, 10) == oq_otb::STARTUP_PROBE_RUNNING);
  assert(state.result(5, 10) == oq_otb::STARTUP_PROBE_TIMED_OUT);
  state.end();

  return 0;
}
