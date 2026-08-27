#pragma once

#include <math.h>
#include <stdint.h>

#include "oq_hp_fallback_logic.h"

namespace oq_hp_supervisory {

inline bool apply_heating_enable_gate(bool heating_request, bool heating_enable_valid, bool heating_enable_selected) {
  return heating_request && heating_enable_valid && heating_enable_selected;
}

inline int base_control_mode(bool cooling_request, bool heating_request, bool frost_request) {
  if (cooling_request) return 5;
  if (heating_request) return 2;
  if (frost_request) return 98;
  return 0;
}

struct ColdStartWaterSample {
  bool required = false;
  float temperature_c = NAN;
  uint32_t updated_at_ms = 0;
};

struct ColdStartDecision {
  bool samples_ready = false;
  bool hp_start_allowed = false;
  bool auxiliary_assist_recommended = false;
  bool released = false;
  float minimum_temperature_c = NAN;
};

inline bool cold_start_sample_is_new(const ColdStartWaterSample& sample, uint32_t sample_after_ms) {
  if (!sample.required) return true;
  if (sample_after_ms == 0 || sample.updated_at_ms == 0 || isnan(sample.temperature_c)) return false;
  return static_cast<int32_t>(sample.updated_at_ms - sample_after_ms) > 0;
}

inline ColdStartDecision evaluate_cold_start(uint32_t sample_after_ms, const ColdStartWaterSample& hp1,
                                             const ColdStartWaterSample& hp2, float minimum_start_c,
                                             float assist_release_c) {
  ColdStartDecision decision;
  if (isnan(minimum_start_c) || isnan(assist_release_c) || assist_release_c <= minimum_start_c ||
      !cold_start_sample_is_new(hp1, sample_after_ms) || !cold_start_sample_is_new(hp2, sample_after_ms)) {
    return decision;
  }

  float minimum_c = NAN;
  if (hp1.required) minimum_c = hp1.temperature_c;
  if (hp2.required) minimum_c = isnan(minimum_c) ? hp2.temperature_c : fminf(minimum_c, hp2.temperature_c);
  if (isnan(minimum_c)) return decision;

  decision.samples_ready = true;
  decision.minimum_temperature_c = minimum_c;
  decision.hp_start_allowed = minimum_c >= minimum_start_c;
  decision.auxiliary_assist_recommended = decision.hp_start_allowed && minimum_c < assist_release_c;
  decision.released = minimum_c >= assist_release_c;
  return decision;
}

inline bool fallback_availability_is_confirmed(bool raw_availability_complete,
                                               bool every_unavailable_hp_has_fallback_cause, bool all_hp_outputs_safe) {
  // Link-loss recovery is intentionally not "available" yet. It may still
  // establish a safe CM4 entry once every configured unavailable HP has an
  // explicit fallback cause and its stopped output has been freshly confirmed.
  return raw_availability_complete || (every_unavailable_hp_has_fallback_cause && all_hp_outputs_safe);
}

class Cm4ResumeTracker {
 public:
  void observe_fallback_request(bool requested, int current_mode) {
    if (requested && !request_active_ && !origin_valid_) {
      // Capture before the HP stop wait moves CM2/CM3 through CM1. A request
      // first observed in any other mode belongs to an HP-only origin.
      resume_mode_ = current_mode == 3 ? 3 : 2;
      origin_valid_ = true;
    }
    request_active_ = requested;
  }

  int resume_mode() const { return origin_valid_ && resume_mode_ == 3 ? 3 : 2; }

  void finish_after_decision(bool heating_demand, bool competing_owner, bool fallback_requested, bool hp_available) {
    if (!heating_demand || competing_owner || (!fallback_requested && hp_available)) {
      origin_valid_ = false;
      resume_mode_ = 2;
    }
    if (!fallback_requested) request_active_ = false;
  }

  bool origin_valid() const { return origin_valid_; }

 private:
  bool request_active_{false};
  bool origin_valid_{false};
  int resume_mode_{2};
};

inline int recovered_heating_mode(int stored_resume_mode, bool hp_available, bool power_house_active,
                                  bool boiler_assist_enabled) {
  if (!hp_available) return 1;
  return stored_resume_mode == 3 && power_house_active && boiler_assist_enabled ? 3 : 2;
}

struct FallbackEvaluationInputs {
  int current_mode = 0;
  bool heating_demand = false;
  bool fallback_enabled = false;
  uint8_t available_hp_count = 0;
  bool raw_availability_complete = false;
  bool every_unavailable_hp_has_fallback_cause = false;
  bool all_hp_outputs_safe = false;
  bool cold_start_blocked = false;
  bool flow_valid = false;
  bool flow_sufficient = false;
  bool supply_temperature_valid = false;
  bool boiler_guards_clear = false;
  bool cooling_active = false;
  bool frost_active = false;
  bool commissioning_active = false;
  bool override_active = false;
};

struct FallbackEvaluation {
  oq_hp_fallback::FallbackDecision decision;
  bool availability_complete = false;
  bool no_hp_available_confirmed = false;
  bool fallback_requested = false;
  bool cm3_handover_wait = false;
};

inline FallbackEvaluation evaluate_fallback(const FallbackEvaluationInputs& inputs) {
  FallbackEvaluation evaluation;
  const uint8_t effective_available_hp_count = inputs.cold_start_blocked ? 0 : inputs.available_hp_count;
  const bool effective_fallback_cause = inputs.cold_start_blocked || inputs.every_unavailable_hp_has_fallback_cause;
  evaluation.availability_complete =
      inputs.cold_start_blocked ? inputs.all_hp_outputs_safe
                                : fallback_availability_is_confirmed(inputs.raw_availability_complete,
                                                                     inputs.every_unavailable_hp_has_fallback_cause,
                                                                     inputs.all_hp_outputs_safe);
  evaluation.no_hp_available_confirmed = effective_available_hp_count == 0 && evaluation.availability_complete;
  evaluation.fallback_requested =
      inputs.heating_demand && effective_available_hp_count == 0 && effective_fallback_cause;

  oq_hp_fallback::FallbackInputs fallback_inputs;
  fallback_inputs.current_mode = static_cast<oq_hp_fallback::ControlMode>(inputs.current_mode);
  fallback_inputs.heating_demand = inputs.heating_demand;
  fallback_inputs.fallback_enabled = inputs.fallback_enabled;
  fallback_inputs.available_hp_count = effective_available_hp_count;
  fallback_inputs.hp_availability_complete = evaluation.availability_complete;
  fallback_inputs.confirmed_fallback_cause = effective_fallback_cause;
  fallback_inputs.hp_output_state_safe = inputs.all_hp_outputs_safe;
  fallback_inputs.flow_valid = inputs.flow_valid;
  fallback_inputs.flow_sufficient = inputs.flow_sufficient;
  fallback_inputs.supply_temperature_valid = inputs.supply_temperature_valid;
  fallback_inputs.boiler_guards_clear = inputs.boiler_guards_clear;
  fallback_inputs.cooling_active = inputs.cooling_active;
  fallback_inputs.frost_active = inputs.frost_active;
  fallback_inputs.commissioning_active = inputs.commissioning_active;
  fallback_inputs.override_active = inputs.override_active;
  evaluation.decision = oq_hp_fallback::decide_cm4(fallback_inputs);

  if (evaluation.fallback_requested && inputs.current_mode == 3) {
    // CM3 already owns the boiler. Keep that output enabled while only the
    // fresh HP stop confirmation is missing; every other CM4 guard must pass.
    // RECOVERING is incomplete until that confirmation, so evaluate the
    // prospective CM4 state with both coupled entry guards satisfied.
    fallback_inputs.hp_availability_complete = true;
    fallback_inputs.hp_output_state_safe = true;
    evaluation.cm3_handover_wait =
        oq_hp_fallback::decide_cm4(fallback_inputs).cm4_allowed && !evaluation.decision.cm4_allowed;
  }
  return evaluation;
}

struct HeatingModeInputs {
  int current_mode = 0;
  int base_target = 0;
  bool cm3_fallback_handover_wait = false;
  int cm4_resume_mode = 2;
  bool hp_available = false;
  bool power_house_active = false;
  bool boiler_assist_enabled = false;
};

struct HeatingModeDecision {
  int desired_mode = 1;
  int start_cm1_for_mode = -1;
  bool flow_interlock_hold = false;
  const char* transition_reason = "heating request held by flow interlock";
};

inline HeatingModeDecision decide_heating_mode(const HeatingModeInputs& inputs) {
  if (inputs.cm3_fallback_handover_wait) {
    return HeatingModeDecision{
        3,
        -1,
        false,
        "CM3 held until HP stop confirmation for CM4 handover",
    };
  }

  if (inputs.base_target == 4) {
    if (inputs.current_mode == 4) {
      return HeatingModeDecision{4, -1, false, "boiler fallback already active"};
    }
    if (inputs.current_mode == 3) {
      return HeatingModeDecision{
          4,
          -1,
          false,
          "CM3 -> CM4 continuous boiler fallback handover",
      };
    }
    return HeatingModeDecision{1, 4, false, "boiler fallback waiting for CM1 preflow"};
  }

  if (inputs.base_target == 2) {
    if (inputs.current_mode == 2 || inputs.current_mode == 3) {
      return HeatingModeDecision{2, -1, false, "heating already active"};
    }
    if (inputs.current_mode == 4) {
      const int recovered_mode = recovered_heating_mode(inputs.cm4_resume_mode, inputs.hp_available,
                                                        inputs.power_house_active, inputs.boiler_assist_enabled);
      return HeatingModeDecision{
          recovered_mode,
          -1,
          false,
          recovered_mode == 3 ? "CM4 -> CM3 continuous boiler handback" : "CM4 -> CM2 HP recovery handback",
      };
    }
    return HeatingModeDecision{1, 2, false, "heating request waiting for CM1 hold"};
  }

  return HeatingModeDecision{1, -1, true, "heating request held by flow interlock"};
}

}  // namespace oq_hp_supervisory
