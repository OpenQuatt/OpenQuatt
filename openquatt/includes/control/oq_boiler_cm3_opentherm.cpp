#include "oq_boiler_cm3_opentherm.h"

namespace oq_boiler_cm3_opentherm {
namespace {

// The startup headroom and timing are provisional until checked against real
// boilers. ID15 describes steady modulation, not the ignition power.
constexpr float kPreferredStartExtraRiseC = 12.0f;
constexpr float kMinimumStartExtraRiseC = 3.0f;
constexpr float kStartAboveOutletC = 4.0f;
constexpr float kWaterLimitReserveC = 2.0f;
constexpr float kOutletCutoffReserveC = 1.0f;
constexpr uint32_t kIgnitionTimeoutMs = 150000;
constexpr uint32_t kStableFlameMs = 60000;
constexpr uint32_t kDoseMinimumMs = 180000;
constexpr uint32_t kFailedStartRestMs = 360000;
constexpr uint32_t kFlowConfirmationMs = 15000;
constexpr uint32_t kResidualHeatMs = 90000;
constexpr uint32_t kMaximumTickGapMs = 15000;
constexpr float kTargetRampCPerSecond = 0.1f;
constexpr float kDoseTargetRampCPerSecond = 0.3f;
constexpr float kDoseTargetBelowOutletC = 2.0f;

}  // namespace

Decision Control::tick(const Inputs& in) {
  const float minimum_w = nominal_minimum_w(in.capacity_kw, in.minimum_modulation_pct);
  const bool thermal_data_valid = isfinite(in.inlet_c) && isfinite(in.outlet_c) && isfinite(in.flow_lph) &&
                                  isfinite(in.maximum_water_c) && isfinite(in.water_cp_j_per_kgk);
  // Keep the normal target on boilers without the telemetry needed for this policy.
  managed_ |= in.heat_requested && in.status_valid && thermal_data_valid && isfinite(minimum_w);
  if (!managed_) return {};

  const uint32_t elapsed_ms = have_tick_ ? in.now_ms - previous_tick_ms_ : 0;
  previous_tick_ms_ = in.now_ms;
  have_tick_ = true;
  if (waiting_for_flow_confirmation_) {
    const bool within_window = in.now_ms - failed_at_ms_ <= kFlowConfirmationMs;
    if (within_window && in.flow_loss_confirmed) retry_wait_ = false;
    waiting_for_flow_confirmation_ = within_window && !in.flow_loss_confirmed;
  }
  if (isfinite(in.setpoint_c)) {
    if (isfinite(setpoint_c_) && in.setpoint_c < setpoint_c_ - 0.05f) {
      waiting_for_setpoint_command_ = true;
      // Dispatch may still publish a pre-reduction strategy frame on its
      // next tick. Require a fresh calculation after observing the change.
      setpoint_reduced_at_ms_ = in.now_ms;
    }
    setpoint_c_ = in.setpoint_c;
  }
  if (waiting_for_setpoint_command_ &&
      oq_boiler::timestamp_is_strictly_newer(in.command_updated_at_ms, setpoint_reduced_at_ms_)) {
    waiting_for_setpoint_command_ = false;
  }
  const bool room_below_target = isfinite(in.room_c) && isfinite(in.setpoint_c) && in.room_c < in.setpoint_c;
  const bool dose = in.requested_power_w < minimum_w;

  if (!in.heat_requested || !in.flow_sufficient || waiting_for_setpoint_command_ || (dose && !room_below_target)) {
    energy_ws_ = 0.0f;
    return stop();
  }
  if (elapsed_ms > kMaximumTickGapMs || !thermal_data_valid || !isfinite(minimum_w) || !in.status_valid ||
      in.dhw_active || in.fault_active) {
    discard_credit();
    return stop();
  }

  const float ceiling_c = in.maximum_water_c - kWaterLimitReserveC;
  const auto target = oq_boiler::target_from_power(fmaxf(in.requested_power_w, minimum_w), in.capacity_kw * 1000.0f,
                                                   in.inlet_c, in.flow_lph, in.water_cp_j_per_kgk, ceiling_c);
  if (running_ && in.outlet_c >= in.maximum_water_c - kOutletCutoffReserveC) return failed(in.now_ms);
  if (!isfinite(in.requested_power_w) || in.requested_power_w <= 0.0f || !target.valid ||
      target.requested_power_w < minimum_w) {
    discard_credit();
    return stop();
  }

  const float seconds = elapsed_ms / 1000.0f;
  const float conductance_w_per_k = in.flow_lph * in.water_cp_j_per_kgk / 3600.0f;
  const float reserve_ws = minimum_w * (kDoseMinimumMs / 1000.0f);
  const float measured_w = conductance_w_per_k * (in.outlet_c - in.inlet_c);
  residual_heat_active_ &= in.now_ms - residual_heat_started_ms_ < kResidualHeatMs;
  const bool firing = running_ && in.ch_applied && in.flame_on;
  const float delivered_w = firing                                  ? fmaxf(minimum_w, measured_w)
                            : residual_heat_active_ && !in.flame_on ? fmaxf(0.0f, measured_w)
                                                                    : 0.0f;
  // Count heat carried out after an intentional dose stops, as well as heat
  // during ignition and continuous runs that later return to a small request.
  if (!in.defrost_active || delivered_w > 0.0f) {
    energy_ws_ =
        fmaxf(-5.0f * reserve_ws, fminf(reserve_ws, energy_ws_ + (in.requested_power_w - delivered_w) * seconds));
  }

  if (running_) {
    if (!in.ch_applied) return stop();
    if (flame_seen_ && !in.flame_on) {
      // A stable burn ended. The normal CH minimum-off time and the
      // boiler's own anti-cycle timer still guard the next start.
      if (in.now_ms - flame_ms_ >= kStableFlameMs) return stop();
      return failed(in.now_ms, true);
    }
    if (!flame_seen_ && !in.flame_on && in.now_ms - started_ms_ >= kIgnitionTimeoutMs) return failed(in.now_ms);
    if (!flame_seen_ && in.flame_on) {
      flame_seen_ = true;
      flame_ms_ = in.now_ms;
    }
    if (!flame_seen_ && in.defrost_active) return stop();

    const uint32_t flame_age_ms = in.now_ms - flame_ms_;
    if (dose && flame_seen_ && flame_age_ms >= kDoseMinimumMs && energy_ws_ <= 0.0f) {
      residual_heat_started_ms_ = in.now_ms;
      return stop(true);
    }
    if (flame_seen_ && flame_age_ms >= kStableFlameMs) {
      const float ramp_rate = dose ? kDoseTargetRampCPerSecond : kTargetRampCPerSecond;
      const float ramp_target_c = target_c_ - ramp_rate * seconds;
      const float outlet_floor_c = dose ? fminf(target_c_, in.outlet_c - kDoseTargetBelowOutletC) : ramp_target_c;
      target_c_ = fmaxf(target.target_temperature_c, fmaxf(ramp_target_c, outlet_floor_c));
    } else if (target_c_ > ceiling_c) {
      return stop();
    }
    target_c_ = fminf(target_c_, ceiling_c);
    return {true, target_c_};
  }

  if (retry_wait_ && in.now_ms - failed_at_ms_ >= kFailedStartRestMs) retry_wait_ = false;
  if (retry_wait_ || !in.start_allowed || in.defrost_active || (dose && energy_ws_ < reserve_ws)) return {true};
  if (in.flame_on) {
    // A flame inherited at the CM3 handover cannot receive another boost.
    if (!in.ch_applied) return {true};
    flame_seen_ = true;
    flame_ms_ = in.now_ms - kStableFlameMs;
    target_c_ = target.target_temperature_c;
  } else {
    const float stable_minimum_c = in.inlet_c + minimum_w / conductance_w_per_k;
    const float minimum_ignition_c =
        fmaxf(stable_minimum_c + kMinimumStartExtraRiseC, in.outlet_c + kStartAboveOutletC);
    if (minimum_ignition_c > ceiling_c) return {true};
    target_c_ = fminf(ceiling_c, fmaxf(fmaxf(minimum_ignition_c, stable_minimum_c + kPreferredStartExtraRiseC),
                                       target.target_temperature_c));
  }
  started_ms_ = in.now_ms;
  running_ = true;
  residual_heat_active_ = false;
  return {true, target_c_};
}

}  // namespace oq_boiler_cm3_opentherm
