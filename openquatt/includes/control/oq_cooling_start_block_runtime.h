#pragma once

#include <math.h>
#include <stdint.h>

#include <algorithm>

#include "oq_cooling_limiter_logic.h"
#include "oq_cooling_start_block_logic.h"
#include "oq_thermal_actuator_runtime.h"
#include "../service/tasks/oq_manual_hp_logic.h"

#if defined(OQ_TOPOLOGY_DUO)
namespace oq_cooling_start_block_runtime {

// Read-only diagnostics for the user display (issue #642). This runtime never
// commands HPs and never changes control timing; it only reports why a wanted
// cooling start is still held back, distinguishing the shared cooling restart
// setting from the general per-HP compressor restart protection and other
// blocks. Control behavior stays owned by oq_thermal_actuator_runtime,
// oq_cooling_runtime and the incident manager.
class Runtime {
 public:
  oq_cooling_start_block::Inputs inputs(uint32_t now_ms, int cooling_minimum_off_floor_s) {
    oq_cooling_start_block::Inputs in;
#if OQ_TOPOLOGY_DUO
    in.duo = true;
#else
    in.duo = false;
#endif
    const bool manual_service_active = oq_manual_hp::owns_control();
    const bool cooling_enabled = id(cooling_enable_selected).has_state() && id(cooling_enable_selected).state;
    const bool request_active = id(cooling_request_active).has_state() && id(cooling_request_active).state;
    const bool permitted = id(cooling_permitted).has_state() && id(cooling_permitted).state;
    const int control_mode = id(oq_control_mode_code);
    const int request_mode = id(oq_request_mode_code);
    const bool cooling_context = control_mode == 5 || request_mode == 1;
    in.cooling_demand_active =
        !manual_service_active && cooling_enabled && request_active && permitted && cooling_context;

    const int hp1_applied = id(hp1_last_applied_level);
#if OQ_TOPOLOGY_DUO
    const int hp2_applied = id(hp2_last_applied_level);
    in.any_hp_running = hp1_applied > 0 || hp2_applied > 0;
#else
    in.any_hp_running = hp1_applied > 0;
#endif
    if (!in.cooling_demand_active || in.any_hp_running) return in;

    const float cooling_off_state = id(cooling_minimum_off_time).state;
    int cooling_off_s = !isfinite(cooling_off_state) ? 600 : static_cast<int>(lroundf(cooling_off_state));
    const int cooling_floor_s = oq_cooling::cooling_minimum_off_floor_s(cooling_minimum_off_floor_s);
    cooling_off_s = std::max(cooling_floor_s, std::min(3600, cooling_off_s));
    const bool restart_by_minimum_off_time =
        id(cooling_restart_mode).has_state() && id(cooling_restart_mode).current_option() == "Minimum off time";
    in.cooling_remaining_ms = oq_cooling::global_minimum_off_time_remaining_ms(
        restart_by_minimum_off_time, now_ms, id(oq_cooling_confirmed_stop_seen), id(oq_cooling_last_confirmed_stop_ms),
        id(oq_cooling_boot_min_off_elapsed), static_cast<uint32_t>(cooling_off_s) * 1000UL);
    in.cooling_confirmation_pending = id(oq_cooling_stop_confirmation_pending_hp1)
#if OQ_TOPOLOGY_DUO
                                      || id(oq_cooling_stop_confirmation_pending_hp2)
#endif
        ;
    if (in.cooling_confirmation_pending) {
      // A pending stop confirmation holds the full configured delay; the exact
      // remaining time is unknown until the stop is confirmed.
      in.cooling_remaining_ms = std::max(in.cooling_remaining_ms, static_cast<uint32_t>(cooling_off_s) * 1000UL);
    }

    in.hp1_rest_remaining_ms = id(oq_incident_manager).minimum_off_remaining_ms(1U, now_ms);
    in.hp1_startup_inhibited = id(oq_incident_manager).startup_inhibited(1U);
    in.hp1_start_limit_remaining_ms = oq_thermal_actuator_runtime::runtime().start_limit_remaining_ms(true, now_ms);
#if OQ_TOPOLOGY_DUO
    in.hp2_rest_remaining_ms = id(oq_incident_manager).minimum_off_remaining_ms(2U, now_ms);
    in.hp2_startup_inhibited = id(oq_incident_manager).startup_inhibited(2U);
    in.hp2_start_limit_remaining_ms = oq_thermal_actuator_runtime::runtime().start_limit_remaining_ms(false, now_ms);
#endif

    const int demand_raw = id(oq_cooling_demand_raw);
    const int owner = id(oq_cooling_request_owner_hp);
    const int actuator_hp1 = id(oq_actuator_hp1_req);
#if OQ_TOPOLOGY_DUO
    const int actuator_hp2 = id(oq_actuator_hp2_req);
    const bool actuator_wants_start = actuator_hp1 > 0 || actuator_hp2 > 0;
#else
    const bool actuator_wants_start = actuator_hp1 > 0;
#endif
    in.dispatch_blocked_other = demand_raw > 0 && (owner == 0 || actuator_wants_start);
    return in;
  }

  const char* reason(int cooling_minimum_off_floor_s) {
    const uint32_t now_ms = static_cast<uint32_t>(millis());
    return oq_cooling_start_block::resolve(this->inputs(now_ms, cooling_minimum_off_floor_s)).reason;
  }

  float remaining_s(int cooling_minimum_off_floor_s) {
    const uint32_t now_ms = static_cast<uint32_t>(millis());
    const auto result = oq_cooling_start_block::resolve(this->inputs(now_ms, cooling_minimum_off_floor_s));
    if (!result.has_countdown || result.remaining_ms == 0) return 0.0f;
    return static_cast<float>((result.remaining_ms + 999UL) / 1000UL);
  }

  float hp_minimum_off_remaining_s(uint8_t hp_index) {
    const uint32_t now_ms = static_cast<uint32_t>(millis());
    if (hp_index != 1U && hp_index != 2U) return 0.0f;
#if !OQ_TOPOLOGY_DUO
    if (hp_index == 2U) return 0.0f;
#endif
    return static_cast<float>((id(oq_incident_manager).minimum_off_remaining_ms(hp_index, now_ms) + 999UL) / 1000UL);
  }

  bool stop_confirmation_pending() {
    return id(oq_cooling_stop_confirmation_pending_hp1)
#if OQ_TOPOLOGY_DUO
           || id(oq_cooling_stop_confirmation_pending_hp2)
#endif
        ;
  }
};

inline Runtime& runtime() {
  static Runtime instance;
  return instance;
}

}  // namespace oq_cooling_start_block_runtime
#endif
