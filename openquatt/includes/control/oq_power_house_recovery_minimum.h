#pragma once

#include "oq_compressor_feedback_logic.h"
#include "oq_power_house_dispatch_logic.h"
#include "oq_power_house_run_extension_logic.h"
#include "../performance/oq_energy_logic.h"
#include "../sources/oq_source_receipt_runtime.h"

namespace oq_power_house_recovery {

inline float delivered_heat(const oq_sources::HeatPumpReceipts& raw, uint64_t now, uint32_t since_valve_clear_ms,
                            uint32_t control_interval_ms, float inlet, float outlet, float flow) {
  constexpr uint64_t fresh_ms = 30000;
  if (!raw.outside.fresh(now, fresh_ms) || !raw.water_in.fresh(now, fresh_ms) || !raw.water_out.fresh(now, fresh_ms) ||
      !raw.flow.fresh(now, fresh_ms) || !raw.working_mode.fresh(now, fresh_ms) || raw.working_mode.value != 2 ||
      !raw.compressor_frequency.fresh(now, fresh_ms) || raw.compressor_frequency.value <= 0 ||
      !raw.defrost.fresh(now, fresh_ms) || raw.defrost.value != 0 || !raw.status_2108.fresh(now, fresh_ms) ||
      (static_cast<uint16_t>(std::lround(raw.status_2108.value)) & 0x0040U) || !raw.status_2119.fresh(now, fresh_ms) ||
      (static_cast<uint16_t>(std::lround(raw.status_2119.value)) & 0x0048U))
    return NAN;
  // Receipt timestamps and control millis have different epochs. Compare elapsed durations.
  const auto first = std::min({raw.water_in.received_ms, raw.water_out.received_ms, raw.flow.received_ms,
                               raw.compressor_frequency.received_ms});
  const auto last = std::max({raw.water_in.received_ms, raw.water_out.received_ms, raw.flow.received_ms,
                              raw.compressor_frequency.received_ms});
  const auto oldest = std::min({first, raw.working_mode.received_ms, raw.defrost.received_ms,
                                raw.status_2108.received_ms, raw.status_2119.received_ms});
  if (now - oldest >= since_valve_clear_ms || last - first >= control_interval_ms) return NAN;
  return oq_energy::hydronic_heat_power(inlet, outlet, flow, 4186.0f);
}

// During the post-defrost tail the observer cannot learn a stable point. An already running HP can therefore
// cross the room stop threshold solely because its held level is being estimated at a higher measured frequency.
inline void use_structural_minimum(oq_power_house_run_extension::Input& run,
                                   const oq_power_house_run_extension::State& previous,
                                   const oq_power_house_dispatch::DispatchInput& dispatch,
                                   const oq_power_house_dispatch::DispatchTuning& tuning,
                                   const oq_power_house_dispatch::DispatchState& protection,
                                   const oq_compressor_feedback::Snapshot& response, float delivered_w,
                                   float request_limit_w) {
  namespace ext = oq_power_house_run_extension;
  namespace dsp = oq_power_house_dispatch;
  const auto& hp = dispatch.hp1;
  if (dispatch.duo || !dispatch.performance_valid || !run.cycle_active || !run.actual_heating_active ||
      !run.inputs_valid || !run.heating_allowed || !ext::water_floor_allowed(run.water_limit_factor) ||
      previous.phase != ext::Phase::RUNNING || !previous.cycle_armed ||
      ext::setpoint_dropped(run.setpoint_c, previous.last_setpoint_c) || !std::isfinite(previous.last_setpoint_c) ||
      !(request_limit_w > run.base_requested_w) || !std::isfinite(run.base_requested_w) ||
      !(delivered_w > 0 && delivered_w < run.base_requested_w) || !(run.minimum_viable_w > run.base_requested_w) ||
      run.minimum_viable_level <= 1 || hp.feedback_hold_level != run.minimum_viable_level ||
      hp.feedback_hold_level != dsp::applied_level(hp) || !response.settling || response.missing ||
      response.actual_hz == 0 || response.requested_hz == 0 || hp.candidate.must_stop || hp.candidate.link_suspect ||
      !hp.candidate.available_for_start || hp.defrost || hp.valve_defrost || protection.hp1_defrost.previous_active ||
      !dsp::tail_active(protection.hp1_defrost, dispatch.now_ms, dsp::kDefrostHoldMs) ||
      dsp::tail_active(protection.oil_return, dispatch.now_ms, dsp::kOilReturnHoldMs))
    return;

  // Test only F1 on the already running unit. Fixed levels, frequency exclusions, model validity and
  // the electrical peak limit remain authoritative. Dispatch and the actuator still see the original hold.
  const auto f1 =
      dsp::make_candidate(dispatch, tuning, 1, 0, run.base_requested_w, dsp::FeedbackHold::IGNORE_FOR_COMFORT);
  if (!f1.valid || !(f1.thermal_w > 0 && f1.thermal_w <= run.base_requested_w)) return;
  run.minimum_viable_w = f1.thermal_w;
  run.minimum_viable_level = 1;
}

}  // namespace oq_power_house_recovery
