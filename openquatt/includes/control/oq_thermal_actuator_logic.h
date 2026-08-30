#pragma once

#include <math.h>
#include <stdint.h>

#include <string>

namespace oq_thermal_actuator {

inline uint32_t minimum_off_remaining_ms(uint32_t now_ms, uint32_t last_stop_ms, uint32_t minimum_off_ms) {
  if (last_stop_ms == 0 || minimum_off_ms == 0) return 0;
  const uint32_t elapsed_ms = now_ms - last_stop_ms;
  return elapsed_ms < minimum_off_ms ? minimum_off_ms - elapsed_ms : 0;
}

constexpr bool valid_level_command(int control_level, int physical_level) {
  return control_level > 0 && control_level <= 10 && physical_level > 0 && physical_level <= 20;
}

struct ManualGuardInputs {
  int requested_level;
  int mode_code;
  uint32_t now_ms;
  uint32_t last_stop_ms;
  uint32_t minimum_off_ms;
  uint32_t startup_inhibit_remaining_s;
  bool stop_requested;
  bool water_temperature_trip;
  bool low_flow_fault;
  bool flow_valid;
  bool mode_conflict;
};

inline std::string manual_guard(const ManualGuardInputs& in, const std::string& current_guard) {
  if (in.requested_level <= 0 || current_guard != "Vrijgegeven") return current_guard;
  if (in.stop_requested) return "stopverzoek wordt veilig afgerond";
  if (in.water_temperature_trip) return "maximale watertemperatuur bereikt";
  if (in.low_flow_fault) return "low-flow-beveiliging actief";
  if (!in.flow_valid) return "onvoldoende flow voor compressorstart";
  if (in.startup_inhibit_remaining_s > 0) {
    return "opstartblokkering na reboot: nog " + std::to_string(in.startup_inhibit_remaining_s) + " s";
  }
  if (in.mode_code != 1 && in.mode_code != 2) return "kies eerst verwarmen of koelen";
  if (in.mode_conflict) return "conflicterende werkmodus tussen HP1 en HP2";
  const uint32_t remaining_ms = minimum_off_remaining_ms(in.now_ms, in.last_stop_ms, in.minimum_off_ms);
  if (remaining_ms > 0) {
    const uint32_t remaining_s = (remaining_ms + 999UL) / 1000UL;
    return "minimale uit-tijd: nog " + std::to_string(remaining_s) + " s";
  }
  return current_guard;
}

enum class PreflightBlock : uint8_t { NONE, SAFE_ZERO, DEFROST, COOLING_REST, HP_REST, MODE };

inline PreflightBlock decide_preflight(int guarded_level, int previous_level, bool retained_hold, int expected_mode,
                                       uint32_t hp_rest_remaining_ms, bool bypass_holds, bool cooling_start_blocked) {
  if (bypass_holds) return PreflightBlock::SAFE_ZERO;
  if (retained_hold) return PreflightBlock::DEFROST;
  if (guarded_level > 0 && cooling_start_blocked) return PreflightBlock::COOLING_REST;
  if (guarded_level > 0 && previous_level == 0 && hp_rest_remaining_ms > 0) return PreflightBlock::HP_REST;
  if (guarded_level > 0 && expected_mode != 1 && expected_mode != 2) return PreflightBlock::MODE;
  return guarded_level > 0 ? PreflightBlock::NONE : PreflightBlock::SAFE_ZERO;
}

inline void accumulate_runtime(uint32_t dt_ms, uint32_t& accumulated_ms, int& runtime_minutes) {
  const uint32_t sum_ms = accumulated_ms + dt_ms;
  accumulated_ms = sum_ms < accumulated_ms ? 60000UL : sum_ms;
  while (accumulated_ms >= 60000UL) {
    accumulated_ms -= 60000UL;
    ++runtime_minutes;
  }
}

constexpr uint8_t topology_state(int active_count, uint8_t idle, uint8_t single, uint8_t duo) {
  return active_count >= 2 ? duo : (active_count == 1 ? single : idle);
}

}  // namespace oq_thermal_actuator
