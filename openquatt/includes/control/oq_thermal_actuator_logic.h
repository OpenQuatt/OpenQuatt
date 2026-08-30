#pragma once

#include <math.h>
#include <stdint.h>

#include <string>

namespace oq_thermal_actuator {

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
  if (in.mode_code <= 0) return "kies eerst verwarmen of koelen";
  if (in.mode_conflict) return "conflicterende werkmodus tussen HP1 en HP2";
  if (in.last_stop_ms > 0 && static_cast<uint32_t>(in.now_ms - in.last_stop_ms) < in.minimum_off_ms) {
    const uint32_t remaining_s =
        (in.minimum_off_ms - static_cast<uint32_t>(in.now_ms - in.last_stop_ms) + 999UL) / 1000UL;
    return "minimale uit-tijd: nog " + std::to_string(remaining_s) + " s";
  }
  return current_guard;
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
