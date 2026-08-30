#include <assert.h>
#include <stdint.h>

#include "../../openquatt/includes/control/oq_thermal_actuator_logic.h"

int main() {
  using oq_thermal_actuator::accumulate_runtime;
  using oq_thermal_actuator::manual_guard;
  using oq_thermal_actuator::ManualGuardInputs;
  using oq_thermal_actuator::topology_state;

  ManualGuardInputs input{5, 2, 10000U, 0U, 300000U, 0U, false, false, false, true, false};
  assert(manual_guard(input, "Vrijgegeven") == "Vrijgegeven");

  input.stop_requested = true;
  input.water_temperature_trip = true;
  assert(manual_guard(input, "Vrijgegeven") == "stopverzoek wordt veilig afgerond");
  input.stop_requested = false;
  assert(manual_guard(input, "Vrijgegeven") == "maximale watertemperatuur bereikt");
  input.water_temperature_trip = false;
  input.low_flow_fault = true;
  assert(manual_guard(input, "Vrijgegeven") == "low-flow-beveiliging actief");
  input.low_flow_fault = false;
  input.flow_valid = false;
  assert(manual_guard(input, "Vrijgegeven") == "onvoldoende flow voor compressorstart");

  input.flow_valid = true;
  input.startup_inhibit_remaining_s = 17;
  assert(manual_guard(input, "Vrijgegeven") == "opstartblokkering na reboot: nog 17 s");
  input.startup_inhibit_remaining_s = 0;
  input.mode_code = 0;
  assert(manual_guard(input, "Vrijgegeven") == "kies eerst verwarmen of koelen");
  input.mode_code = 2;
  input.mode_conflict = true;
  assert(manual_guard(input, "Vrijgegeven") == "conflicterende werkmodus tussen HP1 en HP2");

  input.mode_conflict = false;
  input.now_ms = 25U;
  input.last_stop_ms = UINT32_MAX - 50U;
  input.minimum_off_ms = 1000U;
  assert(manual_guard(input, "Vrijgegeven") == "minimale uit-tijd: nog 1 s");
  assert(manual_guard(input, "Bestaande blokkering") == "Bestaande blokkering");
  input.requested_level = 0;
  assert(manual_guard(input, "Vrijgegeven") == "Vrijgegeven");

  uint32_t accumulated_ms = 55000U;
  int runtime_minutes = 7;
  accumulate_runtime(125000U, accumulated_ms, runtime_minutes);
  assert(accumulated_ms == 0U);
  assert(runtime_minutes == 10);
  accumulated_ms = UINT32_MAX - 10U;
  accumulate_runtime(20U, accumulated_ms, runtime_minutes);
  assert(accumulated_ms == 0U);
  assert(runtime_minutes == 11);

  assert(topology_state(0, 4, 5, 6) == 4);
  assert(topology_state(1, 4, 5, 6) == 5);
  assert(topology_state(2, 4, 5, 6) == 6);
  return 0;
}
