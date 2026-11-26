#pragma once

#include "../boiler/oq_boiler_logic.h"

namespace oq_boiler_cm3_opentherm {

struct Inputs {
  uint32_t now_ms = 0;
  uint32_t command_updated_at_ms = 0;
  bool heat_requested = false;
  bool start_allowed = false;
  bool ch_applied = false;
  bool status_valid = false;
  bool flame_on = false;
  bool dhw_active = false;
  bool fault_active = false;
  bool defrost_active = false;
  bool flow_sufficient = false;
  bool flow_loss_confirmed = false;
  float capacity_kw = NAN;
  float minimum_modulation_pct = NAN;
  float requested_power_w = NAN;
  float inlet_c = NAN;
  float outlet_c = NAN;
  float flow_lph = NAN;
  float water_cp_j_per_kgk = NAN;
  float maximum_water_c = NAN;
  float room_c = NAN;
  float setpoint_c = NAN;
};

struct Decision {
  bool managed = false;
  float target_c = NAN;
};

class Control {
 public:
  bool running() const { return running_; }

  Decision tick(const Inputs& in);

 private:
  static float nominal_minimum_w(float capacity_kw, float modulation_pct) {
    if (!isfinite(capacity_kw) || !isfinite(modulation_pct) || capacity_kw <= 0.0f || modulation_pct <= 0.0f ||
        modulation_pct > 100.0f) {
      return NAN;
    }
    return capacity_kw * 10.0f * modulation_pct;
  }

  void discard_credit() { energy_ws_ = fminf(0.0f, energy_ws_); }

  Decision stop(bool account_residual_heat = false) {
    running_ = false;
    flame_seen_ = false;
    residual_heat_active_ = account_residual_heat;
    return {true};
  }

  Decision failed(uint32_t now_ms, bool early_flame_loss = false) {
    retry_wait_ = true;
    failed_at_ms_ = now_ms;
    // The flame receipt can precede the controller's filtered flow reading.
    // Only a confirmed flow interruption in the next few ticks can explain
    // this early flame loss. A timeout or thermal cutoff keeps its own rest.
    waiting_for_flow_confirmation_ = early_flame_loss;
    discard_credit();
    return stop();
  }

  bool managed_ = false;
  bool have_tick_ = false;
  bool running_ = false;
  bool flame_seen_ = false;
  bool retry_wait_ = false;
  bool waiting_for_flow_confirmation_ = false;
  bool waiting_for_setpoint_command_ = false;
  bool residual_heat_active_ = false;
  uint32_t previous_tick_ms_ = 0;
  uint32_t started_ms_ = 0;
  uint32_t flame_ms_ = 0;
  uint32_t failed_at_ms_ = 0;
  uint32_t setpoint_reduced_at_ms_ = 0;
  uint32_t residual_heat_started_ms_ = 0;
  float energy_ws_ = 0.0f;
  float setpoint_c_ = NAN;
  float target_c_ = NAN;
};

inline void apply(Control& control, bool cm3_context, const Inputs& in, oq_boiler::ControllerDecision& decision,
                  float& output_target_c) {
  if (!cm3_context) {
    // During an authorized CM3 -> CM4 handover, dispatch can still hold the
    // previous PH command. Preserve the ordinary target and the CH-on edge,
    // but never retain a boosted target through a minimum-on hold.
    if (control.running() && !decision.desired_active && decision.output_active) {
      decision.output_active = false;
      decision.block_reason = oq_boiler::BLOCK_NO_HEAT_REQUEST;
      output_target_c = NAN;
    }
    control = {};
    return;
  }

  const auto cm3 = control.tick(in);
  if (!cm3.managed || decision.force_off) return;
  output_target_c = cm3.target_c;
  decision.output_active = isfinite(output_target_c);
  if (!decision.output_active && decision.desired_active && decision.block_reason == oq_boiler::BLOCK_NONE) {
    decision.block_reason = oq_boiler::BLOCK_CM3_OPENTHERM_WAITING;
    decision.blocked = false;
  }
}

}  // namespace oq_boiler_cm3_opentherm
