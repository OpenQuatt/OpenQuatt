#pragma once

#include "../boiler/oq_boiler_logic.h"

namespace oq_boiler_cm3_opentherm {

struct Inputs {
  uint32_t now_ms = 0;
  uint32_t command_updated_at_ms = 0;
  bool heat_requested = false;
  bool start_allowed = false;
  bool ch_applied = false;
  bool minimum_on_active = false;
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
  float ordinary_target_c = NAN;
  float room_c = NAN;
  float setpoint_c = NAN;
};

struct Decision {
  bool managed = false;
  float target_c = NAN;
  oq_boiler::BlockReason block_reason = oq_boiler::BLOCK_NONE;
  const char* stop_reason = nullptr;
};

class Control {
 public:
  bool running() const { return running_; }

  void withdraw_output();
  Decision tick(const Inputs& in);

 private:
  enum class Retry : uint8_t { Ready, AwaitFlowConfirmation, Rest };

  static float nominal_minimum_w(float capacity_kw, float modulation_pct) {
    if (!isfinite(capacity_kw) || !isfinite(modulation_pct) || capacity_kw <= 0.0f || modulation_pct <= 0.0f ||
        modulation_pct > 100.0f) {
      return NAN;
    }
    return capacity_kw * 10.0f * modulation_pct;
  }

  void discard_credit() { energy_ws_ = fminf(0.0f, energy_ws_); }

  Decision stop(const char* reason, bool account_residual_heat = false,
                oq_boiler::BlockReason block_reason = oq_boiler::BLOCK_NONE) {
    running_ = false;
    flame_seen_ = false;
    residual_heat_active_ = account_residual_heat;
    return {true, NAN, block_reason, reason};
  }

  Decision failed(uint32_t now_ms, const char* reason, bool early_flame_loss = false) {
    failed_at_ms_ = now_ms;
    // The flame receipt can precede the controller's filtered flow reading.
    // Only a confirmed flow interruption in the next few ticks can explain
    // this early flame loss. A timeout or thermal cutoff keeps its own rest.
    retry_ = early_flame_loss ? Retry::AwaitFlowConfirmation : Retry::Rest;
    discard_credit();
    return stop(reason);
  }

  bool managed_ = false;
  bool have_tick_ = false;
  bool running_ = false;
  bool flame_seen_ = false;
  Retry retry_ = Retry::Ready;
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
  float ordinary_target_c_ = NAN;
};

const char* apply(Control& control, bool cm3_context, const Inputs& in, oq_boiler::ControllerDecision& decision,
                  float& output_target_c);

}  // namespace oq_boiler_cm3_opentherm
