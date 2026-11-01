#pragma once

#include <math.h>
#include <string>

#include "../../control/oq_thermal_request_logic.h"
#include "../oq_service_runtime.h"

namespace oq_manual_hp {

inline bool owns_control() {
  return id(oq_manual_hp_active) && id(oq_control_mode_code) == 100 &&
         id(oq_commissioning_task_code) == oq_commissioning::TASK_MANUAL_HP;
}

// Ownership includes the stop tail; only a current positive cooling intent
// may bypass an automatic dew latch during explicit manual service.
inline bool positive_cooling_request(bool hp1) {
  if (!owns_control() || id(oq_manual_hp_stop_requested)) return false;
  const int mode = hp1 ? id(oq_manual_hp1_mode_code) : id(oq_manual_hp2_mode_code);
  const float level = hp1 ? id(oq_manual_hp1_level).state : id(oq_manual_hp2_level).state;
  return mode == 1 && isfinite(level) && roundf(level) > 0.0f;
}

class ManualHpRuntime {
 public:
  void set_mode(int hp_index, const std::string& option, float min_flow_lph) {
    const int requested_mode = mode_code(option);
    const int current_mode = selected_mode_code(hp_index);

    if (requested_mode == current_mode) {
      publish_selected_mode(hp_index, option.c_str());
      return;
    }
    if (requested_mode == 0) {
      set_selected_mode_code(hp_index, 0);
      set_requested_level(hp_index, 0);
      publish_selected_mode(hp_index, "Standby");
      return;
    }
    if (!hp_available(hp_index)) {
      publish_guard("HP2: niet beschikbaar in single-opstelling");
      publish_selected_mode(hp_index, oq_request::request_mode_option(current_mode));
      return;
    }
    if (!owns_control()) {
      publish_guard(hp_index == 1 ? "HP1: start de bediening eerst" : "HP2: start de bediening eerst");
      publish_selected_mode(hp_index, oq_request::request_mode_option(current_mode));
      return;
    }
    if (!flow_ok(min_flow_lph)) {
      publish_guard(hp_index == 1 ? "HP1: wacht op voldoende flow" : "HP2: wacht op voldoende flow");
      publish_selected_mode(hp_index, oq_request::request_mode_option(current_mode));
      return;
    }
    if (other_mode_conflicts(hp_index, requested_mode)) {
      publish_guard(hp_index == 1 ? "HP1: conflicterende werkmodus met HP2" : "HP2: conflicterende werkmodus met HP1");
      publish_selected_mode(hp_index, oq_request::request_mode_option(current_mode));
      return;
    }
    if (last_applied_level(hp_index) > 0) {
      publish_guard(hp_index == 1 ? "HP1: zet eerst op Standby en wacht op stand 0"
                                  : "HP2: zet eerst op Standby en wacht op stand 0");
      publish_selected_mode(hp_index, oq_request::request_mode_option(current_mode));
      return;
    }

    set_selected_mode_code(hp_index, requested_mode);
    set_requested_level(hp_index, 0);
    publish_selected_mode(hp_index, option.c_str());
    publish_guard("Vrijgegeven");
  }

  void start() {
    if (id(oq_control_mode_code) != 100) {
      publish_status("REFUSED: CM100 required");
      return;
    }
    if (id(oq_commissioning_task_code) != oq_commissioning::TASK_NONE) {
      publish_status("REFUSED: BUSY");
      return;
    }

    reset_modes_and_levels();
    id(oq_commissioning_abort_requested) = false;
    id(oq_manual_hp_stop_requested) = false;
    id(oq_manual_hp_mode_allowed) = false;
    id(oq_commissioning_task_code) = oq_commissioning::TASK_MANUAL_HP;
    id(oq_commissioning_active) = true;
    id(oq_manual_hp_active) = true;
    publish_status("ACTIVE: select mode and compressor level");
    publish_guard("Vrijgegeven");
    oq_service_status::set_commissioning("MANUAL HP ACTIVE");
  }

  void abort_or_clear() {
    reset_modes_and_levels();
    id(oq_manual_hp_mode_allowed) = false;
    id(oq_manual_hp_stop_requested) = true;
    publish_status("STOPPING: waiting for compressor release");
  }

  void tick(float min_flow_lph) {
    if (!id(oq_manual_hp_active)) return;

    const bool owns_task = id(oq_commissioning_task_code) == oq_commissioning::TASK_MANUAL_HP;
    const bool cm100_exited = id(oq_control_mode_code) != 100;
    if (!owns_task || cm100_exited) {
      reset_modes_and_levels();
      id(oq_manual_hp_active) = false;
      id(oq_manual_hp_stop_requested) = false;
      id(oq_manual_hp_mode_allowed) = false;
      if (!owns_task) {
        publish_status("ABORTED: task ownership lost");
        return;
      }

      oq_commissioning::clear_container(false);
      publish_status("ABORTED: CM100 exited");
      publish_guard("Vrijgegeven");
      oq_service_status::set_commissioning("IDLE");
      return;
    }

    const bool safety_stop = any_thermal_level() && (id(oq_water_temp_hard_trip_active) ||
                                                     id(oq_lowflow_fault_active) || !flow_ok(min_flow_lph));
    if (safety_stop) {
      reset_modes_and_levels();
      id(oq_manual_hp_mode_allowed) = false;
      publish_status((id(oq_lowflow_fault_active) || !flow_ok(min_flow_lph)) ? "SAFETY STOP: LOW FLOW"
                                                                             : "SAFETY STOP: WATER TEMPERATURE");
    }

    if (id(oq_commissioning_abort_requested)) {
      id(oq_commissioning_abort_requested) = false;
      reset_modes_and_levels();
      id(oq_manual_hp_mode_allowed) = false;
      id(oq_manual_hp_stop_requested) = true;
    }

    if (!id(oq_manual_hp_stop_requested)) return;

    set_requested_level(1, 0);
    set_requested_level(2, 0);
    if (!all_stopped()) {
      publish_status("STOPPING: minimum runtime hold");
      return;
    }

    id(oq_manual_hp_active) = false;
    id(oq_manual_hp_stop_requested) = false;
    id(oq_manual_hp_mode_allowed) = false;
    oq_commissioning::clear_container(true);
    publish_status("STOPPED");
    publish_guard("Vrijgegeven");
    oq_service_status::set_commissioning("CM100 READY");
  }

 private:
  int mode_code(const std::string& option) const {
    if (option == "Cooling") return 1;
    if (option == "Heating") return 2;
    return 0;
  }

  bool hp_available(int hp_index) const {
    if (hp_index == 1) return true;
#if OQ_TOPOLOGY_DUO
    return hp_index == 2;
#else
    return false;
#endif
  }

  bool flow_ok(float min_flow_lph) const {
    return id(flow_rate_selected).has_state() && !isnan(id(flow_rate_selected).state) &&
           id(flow_rate_selected).state >= min_flow_lph;
  }

  int selected_mode_code(int hp_index) const {
    return hp_index == 1 ? id(oq_manual_hp1_mode_code) : id(oq_manual_hp2_mode_code);
  }

  void set_selected_mode_code(int hp_index, int mode) {
    if (hp_index == 1) {
      id(oq_manual_hp1_mode_code) = mode;
    } else {
      id(oq_manual_hp2_mode_code) = mode;
    }
  }

  bool other_mode_conflicts(int hp_index, int requested_mode) const {
    const int other_mode = hp_index == 1 ? id(oq_manual_hp2_mode_code) : id(oq_manual_hp1_mode_code);
    return other_mode > 0 && other_mode != requested_mode;
  }

  int last_applied_level(int hp_index) const {
    if (hp_index == 1) return id(hp1_last_applied_level);
#if OQ_TOPOLOGY_DUO
    return id(hp2_last_applied_level);
#else
    return 0;
#endif
  }

  void set_requested_level(int hp_index, float level) {
    if (hp_index == 1) {
      auto call = id(oq_manual_hp1_level).make_call();
      call.set_value(level);
      call.perform();
    } else {
      auto call = id(oq_manual_hp2_level).make_call();
      call.set_value(level);
      call.perform();
    }
  }

  void publish_selected_mode(int hp_index, const char* mode) {
    if (hp_index == 1) {
      id(oq_manual_hp1_mode).publish_state(mode);
    } else {
      id(oq_manual_hp2_mode).publish_state(mode);
    }
  }

  void reset_modes_and_levels() {
    set_requested_level(1, 0);
    set_requested_level(2, 0);
    id(oq_manual_hp1_mode_code) = 0;
    id(oq_manual_hp2_mode_code) = 0;
    publish_selected_mode(1, "Standby");
    publish_selected_mode(2, "Standby");
  }

  bool any_thermal_level() const {
    if ((int)roundf(id(oq_manual_hp1_level).state) > 0 || (int)roundf(id(oq_manual_hp2_level).state) > 0 ||
        id(hp1_last_applied_level) > 0) {
      return true;
    }
#if OQ_TOPOLOGY_DUO
    return id(hp2_last_applied_level) > 0;
#else
    return false;
#endif
  }

  bool all_stopped() const {
    if (id(hp1_last_applied_level) > 0) return false;
#if OQ_TOPOLOGY_DUO
    return id(hp2_last_applied_level) <= 0;
#else
    return true;
#endif
  }

  void publish_status(const char* status) { oq_service_status::set_manual_hp(status); }

  void publish_guard(const char* status) { oq_service_status::set_manual_hp_guard(status); }
};

inline ManualHpRuntime& runtime() {
  static ManualHpRuntime instance;
  return instance;
}

}  // namespace oq_manual_hp
