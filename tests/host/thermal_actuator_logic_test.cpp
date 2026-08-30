#include <assert.h>
#include <stdint.h>

#include "../../openquatt/includes/control/oq_thermal_actuator_logic.h"
int main() {
  using namespace oq_thermal_actuator;
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
  input.mode_code = 3;
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
  using Block = PreflightBlock;
  assert(decide_preflight(5, 0, false, 2, 0U, false, false) == Block::NONE);
  assert(decide_preflight(0, 5, true, 3, 1000U, true, true) == Block::SAFE_ZERO);
  assert(decide_preflight(0, 0, false, 3, 1000U, false, true) == Block::SAFE_ZERO);
  assert(decide_preflight(0, 0, true, 2, 0U, false, true) == Block::DEFROST);
  assert(decide_preflight(1, 0, true, 3, 1000U, false, true) == Block::DEFROST);
  assert(decide_preflight(5, 0, false, 3, 1000U, false, true) == Block::COOLING_REST);
  assert(decide_preflight(5, 0, false, 3, 1000U, false, false) == Block::HP_REST);
  assert(decide_preflight(5, 0, false, 3, 0U, false, false) == Block::MODE);
  assert(minimum_off_remaining_ms(25U, UINT32_MAX - 50U, 1000U) == 924U);
  assert(minimum_off_remaining_ms(1000U, 500U, 500U) == 0U);
  assert(valid_level_command(5, 20) && !valid_level_command(0, 7) && !valid_level_command(5, 21));
  uint32_t accumulated_ms = 55000U;
  int runtime_minutes = 7;
  accumulate_runtime(125000U, accumulated_ms, runtime_minutes);
  assert(accumulated_ms == 0U);
  assert(runtime_minutes == 10);
  accumulated_ms = UINT32_MAX - 10U;
  accumulate_runtime(20U, accumulated_ms, runtime_minutes);
  assert(accumulated_ms == 0U);
  assert(runtime_minutes == 11);
  assert(topology_state(0, 4, 5, 6) == 4 && topology_state(1, 4, 5, 6) == 5 && topology_state(2, 4, 5, 6) == 6);
  return 0;
}
