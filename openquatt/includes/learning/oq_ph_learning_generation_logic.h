#pragma once

#include "oq_ph_learning_platform.h"

#if OQ_PH_LEARNING_CORE_AVAILABLE

#include <math.h>
#include <new>
#include <stddef.h>
#include <stdint.h>

#include "oq_ph_learning_source_logic.h"

namespace oq_power_house::learning {

struct ExactSourcePolicy {
  uint8_t field_tag = 0;
  SourceIdentity identity;
  uint32_t configuration_generation = 0;
  MeasurementTimingContract timing;
  MeasurementProvenance provenance = MeasurementProvenance::UNKNOWN;
};

struct ResolvedSourceConfiguration {
  ExactSourcePolicy fields[kMaxSourceMeasurements]{};
  size_t count = 0;
};

struct PhysicalContextTokens {
  uint32_t installation = 0;
  uint32_t sensor_calibration = 0;
  uint32_t hydraulic_configuration = 0;
};

struct ExactPhysicalContext {
  HydronicTopology topology = HydronicTopology::UNKNOWN;
  MeterBoundary meter_boundary = MeterBoundary::UNKNOWN;
  FluidHeatCapacityModel fluid_model = FluidHeatCapacityModel::UNKNOWN;
  uint32_t calorimetry_generation = 0;
  bool uncertainty_proven = false;
  float heat_uncertainty_w = NAN;
  float max_flow_lph = NAN;
  DuoSeriesOrder duo_series_order = DuoSeriesOrder::UNKNOWN;
  float max_series_junction_delta_c = NAN;
  ZeroFlowProof zero_flow_proof = ZeroFlowProof::NOT_PROVEN;
  PhysicalContextTokens tokens;
};

struct ControlContextTokens {
  uint32_t active_model = 0;
  uint32_t strategy = 0;
  uint32_t control_configuration = 0;
  uint32_t comfort_policy = 0;
};

struct ExactGenerationConfiguration {
  ResolvedSourceConfiguration sources;
  ExactPhysicalContext physical;
  ControlContextTokens control;
};

enum class GenerationStatus : uint8_t {
  READY = 0,
  INITIALIZED,
  OWNER_REINITIALIZED,
  OWNER_NOT_INITIALIZED,
  OWNER_MISMATCH,
  DATASET_SCOPE_NOT_CLEARED,
  NON_MONOTONIC_OWNER,
  INVALID_OWNER,
  INVALID_OWNER_STATE,
  INVALID_CONFIGURATION,
  GENERATION_OVERFLOW,
  OWNER_BLOCKED,
};

enum class DatasetScopeReset : uint8_t {
  NOT_CLEARED = 0,
  CLEARED,
};

enum GenerationChange : uint8_t {
  GENERATION_CHANGE_NONE = 0,
  GENERATION_CHANGE_SOURCE = 1U << 0,
  GENERATION_CHANGE_PHYSICAL = 1U << 1,
  GENERATION_CHANGE_CONTROL = 1U << 2,
  GENERATION_CHANGE_OWNER = 1U << 3,
};

struct GenerationOwnerState {
  // Supplied by the caller. Tokens must be nonzero, strictly increasing, and never reused across reboots.
  bool initialized = false;
  bool configuration_known = false;
  bool blocked = false;
  uint64_t owner_token = 0;
  uint32_t source_generation = 0;
  uint32_t physical_generation = 0;
  uint32_t control_generation = 0;
  ExactGenerationConfiguration configuration;
};

struct GenerationDecision {
  GenerationStatus status = GenerationStatus::INVALID_CONFIGURATION;
  bool accepted = false;
  bool reset_segment = true;
  bool invalidate_dataset = false;
  uint8_t changes = GENERATION_CHANGE_NONE;
  uint8_t invalidated_contexts = GENERATION_CHANGE_NONE;
  uint32_t source_generation = 0;
  uint32_t physical_generation = 0;
  uint32_t control_generation = 0;
};

struct OwnedSourceObserveResult {
  GenerationDecision generation;
  SourceObserveResult observation;
};

inline const char* generation_status_name(GenerationStatus status) {
  switch (status) {
    case GenerationStatus::READY:
      return "ready";
    case GenerationStatus::INITIALIZED:
      return "initialized";
    case GenerationStatus::OWNER_REINITIALIZED:
      return "owner_reinitialized";
    case GenerationStatus::OWNER_NOT_INITIALIZED:
      return "owner_not_initialized";
    case GenerationStatus::OWNER_MISMATCH:
      return "owner_mismatch";
    case GenerationStatus::DATASET_SCOPE_NOT_CLEARED:
      return "dataset_scope_not_cleared";
    case GenerationStatus::NON_MONOTONIC_OWNER:
      return "non_monotonic_owner";
    case GenerationStatus::INVALID_OWNER:
      return "invalid_owner";
    case GenerationStatus::INVALID_OWNER_STATE:
      return "invalid_owner_state";
    case GenerationStatus::INVALID_CONFIGURATION:
      return "invalid_configuration";
    case GenerationStatus::GENERATION_OVERFLOW:
      return "generation_overflow";
    case GenerationStatus::OWNER_BLOCKED:
      return "owner_blocked";
  }
  return "unknown";
}

namespace generation_detail {

inline bool known_provenance(MeasurementProvenance provenance) {
  return provenance == MeasurementProvenance::PHYSICAL_RECEIPT || provenance == MeasurementProvenance::HELD ||
         provenance == MeasurementProvenance::SYNTHESIZED || provenance == MeasurementProvenance::REPUBLISHED ||
         provenance == MeasurementProvenance::PHYSICAL_COMPOSITION ||
         provenance == MeasurementProvenance::CONTROL_CONTRACT;
}

inline bool same_identity(const SourceIdentity& lhs, const SourceIdentity& rhs) {
  return lhs.kind == rhs.kind && lhs.id == rhs.id && lhs.unit == rhs.unit;
}

inline bool same_timing(const MeasurementTimingContract& lhs, const MeasurementTimingContract& rhs) {
  return lhs.max_age_ms == rhs.max_age_ms && lhs.max_skew_ms == rhs.max_skew_ms;
}

inline bool same_source_policy(const ExactSourcePolicy& lhs, const ExactSourcePolicy& rhs) {
  return lhs.field_tag == rhs.field_tag && same_identity(lhs.identity, rhs.identity) &&
         lhs.configuration_generation == rhs.configuration_generation && same_timing(lhs.timing, rhs.timing) &&
         lhs.provenance == rhs.provenance;
}

inline bool same_source_configuration(const ResolvedSourceConfiguration& lhs, const ResolvedSourceConfiguration& rhs) {
  if (lhs.count != rhs.count) return false;
  for (size_t index = 0; index < lhs.count; ++index)
    if (!same_source_policy(lhs.fields[index], rhs.fields[index])) return false;
  return true;
}

inline bool same_physical_tokens(const PhysicalContextTokens& lhs, const PhysicalContextTokens& rhs) {
  return lhs.installation == rhs.installation && lhs.sensor_calibration == rhs.sensor_calibration &&
         lhs.hydraulic_configuration == rhs.hydraulic_configuration;
}

inline bool same_physical_context(const ExactPhysicalContext& lhs, const ExactPhysicalContext& rhs) {
  return lhs.topology == rhs.topology && lhs.meter_boundary == rhs.meter_boundary &&
         lhs.fluid_model == rhs.fluid_model && lhs.calorimetry_generation == rhs.calorimetry_generation &&
         lhs.uncertainty_proven == rhs.uncertainty_proven && lhs.heat_uncertainty_w == rhs.heat_uncertainty_w &&
         lhs.max_flow_lph == rhs.max_flow_lph && lhs.duo_series_order == rhs.duo_series_order &&
         lhs.max_series_junction_delta_c == rhs.max_series_junction_delta_c &&
         lhs.zero_flow_proof == rhs.zero_flow_proof && same_physical_tokens(lhs.tokens, rhs.tokens);
}

inline bool same_control_context(const ControlContextTokens& lhs, const ControlContextTokens& rhs) {
  return lhs.active_model == rhs.active_model && lhs.strategy == rhs.strategy &&
         lhs.control_configuration == rhs.control_configuration && lhs.comfort_policy == rhs.comfort_policy;
}

inline bool valid_physical_tokens(const PhysicalContextTokens& tokens) {
  return tokens.installation != 0 && tokens.sensor_calibration != 0 && tokens.hydraulic_configuration != 0;
}

inline bool valid_control_tokens(const ControlContextTokens& tokens) {
  return tokens.active_model != 0 && tokens.strategy != 0 && tokens.control_configuration != 0 &&
         tokens.comfort_policy != 0;
}

inline bool valid_stored_source_configuration(const ResolvedSourceConfiguration& sources, HydronicTopology topology) {
  const size_t expected_count = topology == HydronicTopology::SINGLE ? 12U : 19U;
  if (sources.count != expected_count) return false;
  for (size_t index = 0; index < sources.count; ++index) {
    const ExactSourcePolicy& field = sources.fields[index];
    const uint8_t expected_tag =
        topology == HydronicTopology::SINGLE && index == 11U ? 19U : static_cast<uint8_t>(index + 1U);
    if (field.field_tag != expected_tag || !source_detail::known_source_kind(field.identity.kind) ||
        !source_detail::known_physical_unit(field.identity.unit) || field.identity.id == 0 ||
        field.configuration_generation == 0 || !known_provenance(field.provenance) || field.timing.max_age_ms == 0 ||
        field.timing.max_age_ms > kAbsoluteMaxMeasurementAgeMs || field.timing.max_skew_ms == 0 ||
        field.timing.max_skew_ms > kAbsoluteMaxMeasurementSkewMs)
      return false;
    if (field.field_tag >= 5U && field.field_tag <= 11U && field.identity.unit != PhysicalUnit::HP1) return false;
    if (field.field_tag >= 12U && field.field_tag <= 18U && field.identity.unit != PhysicalUnit::HP2) return false;
  }
  return true;
}

inline bool valid_stored_physical_context(const ExactPhysicalContext& physical) {
  if ((physical.topology != HydronicTopology::SINGLE && physical.topology != HydronicTopology::DUO_SERIES) ||
      !valid_physical_tokens(physical.tokens) || physical.calorimetry_generation == 0 ||
      physical.fluid_model != FluidHeatCapacityModel::WATER_CP_4180 || !physical.uncertainty_proven ||
      !isfinite(physical.heat_uncertainty_w) || physical.heat_uncertainty_w < 0.0f ||
      !isfinite(physical.max_flow_lph) || physical.max_flow_lph <= 0.0f ||
      physical.max_flow_lph > kAbsoluteMaxFlowLph ||
      (physical.zero_flow_proof != ZeroFlowProof::NOT_PROVEN &&
       physical.zero_flow_proof != ZeroFlowProof::PHYSICAL_METER_COVERS_BOUNDARY))
    return false;
  if (physical.topology == HydronicTopology::SINGLE)
    return physical.meter_boundary == MeterBoundary::SINGLE_HEAT_PUMP_CIRCUIT &&
           physical.duo_series_order == DuoSeriesOrder::UNKNOWN && physical.max_series_junction_delta_c == 0.0f;
  return physical.meter_boundary == MeterBoundary::SHARED_DUO_SERIES_CIRCUIT &&
         (physical.duo_series_order == DuoSeriesOrder::HP1_TO_HP2 ||
          physical.duo_series_order == DuoSeriesOrder::HP2_TO_HP1) &&
         isfinite(physical.max_series_junction_delta_c) && physical.max_series_junction_delta_c > 0.0f &&
         physical.max_series_junction_delta_c <= kAbsoluteMaxSeriesJunctionDeltaC;
}

inline bool valid_owner_state(const GenerationOwnerState& state) {
  if (!state.initialized)
    return !state.configuration_known && !state.blocked && state.owner_token == 0 && state.source_generation == 0 &&
           state.physical_generation == 0 && state.control_generation == 0;
  if (state.owner_token == 0 || state.source_generation == 0 || state.physical_generation == 0 ||
      state.control_generation == 0 || (state.blocked && state.configuration_known))
    return false;
  if (!state.configuration_known) return true;
  return valid_stored_physical_context(state.configuration.physical) &&
         valid_stored_source_configuration(state.configuration.sources, state.configuration.physical.topology) &&
         valid_control_tokens(state.configuration.control);
}

inline GenerationDecision reject_invalid_owner_state(const GenerationOwnerState& state) {
  GenerationDecision decision;
  decision.status = GenerationStatus::INVALID_OWNER_STATE;
  decision.invalidate_dataset = true;
  decision.invalidated_contexts = GENERATION_CHANGE_SOURCE | GENERATION_CHANGE_PHYSICAL | GENERATION_CHANGE_CONTROL;
  if (state.initialized) {
    decision.source_generation = state.source_generation;
    decision.physical_generation = state.physical_generation;
    decision.control_generation = state.control_generation;
  }
  return decision;
}

template <typename T>
inline bool append_source_policy(ResolvedSourceConfiguration& output, uint8_t field_tag,
                                 const PhysicalMeasurement<T>& measurement, PhysicalUnit expected_unit,
                                 bool enforce_unit) {
  if (output.count >= kMaxSourceMeasurements || field_tag == 0 ||
      !source_detail::known_source_kind(measurement.source.kind) ||
      !source_detail::known_physical_unit(measurement.source.unit) || measurement.source.id == 0 ||
      measurement.source_generation == 0 || !known_provenance(measurement.provenance) ||
      measurement.timing.max_age_ms == 0 || measurement.timing.max_age_ms > kAbsoluteMaxMeasurementAgeMs ||
      measurement.timing.max_skew_ms == 0 || measurement.timing.max_skew_ms > kAbsoluteMaxMeasurementSkewMs ||
      (enforce_unit && measurement.source.unit != expected_unit))
    return false;
  output.fields[output.count++] = {field_tag, measurement.source, measurement.source_generation, measurement.timing,
                                   measurement.provenance};
  return true;
}

inline bool append_heat_pump_sources(ResolvedSourceConfiguration& output, const HeatPumpRawMeasurements& heat_pump,
                                     PhysicalUnit unit, uint8_t first_tag) {
  return append_source_policy(output, first_tag, heat_pump.water_in_c, unit, true) &&
         append_source_policy(output, first_tag + 1U, heat_pump.water_out_c, unit, true) &&
         append_source_policy(output, first_tag + 2U, heat_pump.mode, unit, true) &&
         append_source_policy(output, first_tag + 3U, heat_pump.compressor_active, unit, true) &&
         append_source_policy(output, first_tag + 4U, heat_pump.defrost_active, unit, true) &&
         append_source_policy(output, first_tag + 5U, heat_pump.valve_transition_active, unit, true) &&
         append_source_policy(output, first_tag + 6U, heat_pump.oil_return_active, unit, true);
}

inline bool capture_sources(const LearningSourceInput& input, ResolvedSourceConfiguration& output) {
  output.~ResolvedSourceConfiguration();
  new (&output) ResolvedSourceConfiguration();
  if (input.topology != HydronicTopology::SINGLE && input.topology != HydronicTopology::DUO_SERIES) return false;
  if (!input.hp1.present || (input.topology == HydronicTopology::DUO_SERIES && !input.hp2.present) ||
      (input.topology == HydronicTopology::SINGLE && input.hp2.present))
    return false;
  constexpr PhysicalUnit ignored_unit = PhysicalUnit::SYSTEM;
  if (!append_source_policy(output, 1U, input.room_c, ignored_unit, false) ||
      !append_source_policy(output, 2U, input.setpoint_c, ignored_unit, false) ||
      !append_source_policy(output, 3U, input.outside_c, ignored_unit, false) ||
      !append_source_policy(output, 4U, input.flow_lph, ignored_unit, false) ||
      !append_heat_pump_sources(output, input.hp1, PhysicalUnit::HP1, 5U))
    return false;
  if (input.topology == HydronicTopology::DUO_SERIES &&
      !append_heat_pump_sources(output, input.hp2, PhysicalUnit::HP2, 12U))
    return false;
  return append_source_policy(output, 19U, input.boiler_heat, ignored_unit, false);
}

inline bool capture_physical(const LearningSourceInput& input, const PhysicalContextTokens& tokens,
                             ExactPhysicalContext& output) {
  if (!valid_physical_tokens(tokens)) return false;
  const CalorimetryContract& calorimetry = input.calorimetry;
  if (input.topology != HydronicTopology::SINGLE && input.topology != HydronicTopology::DUO_SERIES) return false;
  const MeterBoundary expected_boundary = input.topology == HydronicTopology::SINGLE
                                              ? MeterBoundary::SINGLE_HEAT_PUMP_CIRCUIT
                                              : MeterBoundary::SHARED_DUO_SERIES_CIRCUIT;
  if (calorimetry.meter_boundary != expected_boundary ||
      calorimetry.fluid_model != FluidHeatCapacityModel::WATER_CP_4180 || calorimetry.calorimetry_generation == 0 ||
      !calorimetry.uncertainty_proven || !isfinite(calorimetry.heat_uncertainty_w) ||
      calorimetry.heat_uncertainty_w < 0.0f || !isfinite(calorimetry.max_flow_lph) ||
      calorimetry.max_flow_lph <= 0.0f || calorimetry.max_flow_lph > kAbsoluteMaxFlowLph ||
      (calorimetry.zero_flow_proof != ZeroFlowProof::NOT_PROVEN &&
       calorimetry.zero_flow_proof != ZeroFlowProof::PHYSICAL_METER_COVERS_BOUNDARY))
    return false;
  if (input.topology == HydronicTopology::DUO_SERIES) {
    if ((calorimetry.duo_series_order != DuoSeriesOrder::HP1_TO_HP2 &&
         calorimetry.duo_series_order != DuoSeriesOrder::HP2_TO_HP1) ||
        !isfinite(calorimetry.max_series_junction_delta_c) || calorimetry.max_series_junction_delta_c <= 0.0f ||
        calorimetry.max_series_junction_delta_c > kAbsoluteMaxSeriesJunctionDeltaC)
      return false;
  }
  output = {input.topology,
            calorimetry.meter_boundary,
            calorimetry.fluid_model,
            calorimetry.calorimetry_generation,
            calorimetry.uncertainty_proven,
            calorimetry.heat_uncertainty_w,
            calorimetry.max_flow_lph,
            input.topology == HydronicTopology::DUO_SERIES ? calorimetry.duo_series_order : DuoSeriesOrder::UNKNOWN,
            input.topology == HydronicTopology::DUO_SERIES ? calorimetry.max_series_junction_delta_c : 0.0f,
            calorimetry.zero_flow_proof,
            tokens};
  return true;
}

inline bool capture_configuration(const LearningSourceInput& input, const PhysicalContextTokens& physical_tokens,
                                  const ControlContextTokens& control_tokens, ExactGenerationConfiguration& output) {
  output.~ExactGenerationConfiguration();
  new (&output) ExactGenerationConfiguration();
  if (!valid_control_tokens(control_tokens) || !capture_sources(input, output.sources) ||
      !capture_physical(input, physical_tokens, output.physical))
    return false;
  output.control = control_tokens;
  return true;
}

inline void fill_generations(const GenerationOwnerState& state, GenerationDecision& decision) {
  decision.source_generation = state.source_generation;
  decision.physical_generation = state.physical_generation;
  decision.control_generation = state.control_generation;
}

inline bool increment_generations(GenerationOwnerState& state, uint8_t changes) {
  if (((changes & GENERATION_CHANGE_SOURCE) != 0 && state.source_generation == UINT32_MAX) ||
      ((changes & GENERATION_CHANGE_PHYSICAL) != 0 && state.physical_generation == UINT32_MAX) ||
      ((changes & GENERATION_CHANGE_CONTROL) != 0 && state.control_generation == UINT32_MAX))
    return false;
  if ((changes & GENERATION_CHANGE_SOURCE) != 0) ++state.source_generation;
  if ((changes & GENERATION_CHANGE_PHYSICAL) != 0) ++state.physical_generation;
  if ((changes & GENERATION_CHANGE_CONTROL) != 0) ++state.control_generation;
  return true;
}

inline GenerationDecision reject_unknown_configuration(GenerationOwnerState& state) {
  GenerationDecision decision;
  decision.status = GenerationStatus::INVALID_CONFIGURATION;
  decision.changes = GENERATION_CHANGE_SOURCE | GENERATION_CHANGE_PHYSICAL | GENERATION_CHANGE_CONTROL;
  decision.invalidated_contexts = decision.changes;
  decision.invalidate_dataset = true;
  if (!increment_generations(state, decision.changes)) {
    state.blocked = true;
    decision.status = GenerationStatus::GENERATION_OVERFLOW;
  }
  state.configuration_known = false;
  fill_generations(state, decision);
  return decision;
}

}  // namespace generation_detail

inline GenerationDecision start_generation_owner(GenerationOwnerState& state, uint64_t owner_token,
                                                 DatasetScopeReset dataset_scope_reset,
                                                 const LearningSourceInput& input,
                                                 const PhysicalContextTokens& physical_tokens,
                                                 const ControlContextTokens& control_tokens) {
  using namespace generation_detail;
  GenerationDecision decision;
  if (!valid_owner_state(state)) return reject_invalid_owner_state(state);
  if (owner_token == 0) {
    decision.status = GenerationStatus::INVALID_OWNER;
    if (state.initialized) {
      fill_generations(state, decision);
    } else {
      decision.invalidate_dataset = true;
      decision.invalidated_contexts = GENERATION_CHANGE_SOURCE | GENERATION_CHANGE_PHYSICAL | GENERATION_CHANGE_CONTROL;
    }
    return decision;
  }
  if (dataset_scope_reset != DatasetScopeReset::CLEARED) {
    decision.status = GenerationStatus::DATASET_SCOPE_NOT_CLEARED;
    if (state.initialized) fill_generations(state, decision);
    return decision;
  }
  if (state.initialized && owner_token <= state.owner_token) {
    decision.status = GenerationStatus::NON_MONOTONIC_OWNER;
    fill_generations(state, decision);
    return decision;
  }
  if (state.blocked) {
    decision.status = GenerationStatus::OWNER_BLOCKED;
    fill_generations(state, decision);
    return decision;
  }

  ExactGenerationConfiguration current;
  const bool valid_configuration = capture_configuration(input, physical_tokens, control_tokens, current);
  const bool owner_changed = state.initialized;
  const uint8_t changes = GENERATION_CHANGE_SOURCE | GENERATION_CHANGE_PHYSICAL | GENERATION_CHANGE_CONTROL;
  if (owner_changed) {
    if (!increment_generations(state, changes)) {
      state.blocked = true;
      state.configuration_known = false;
      decision.status = GenerationStatus::GENERATION_OVERFLOW;
      decision.changes = changes;
      decision.invalidated_contexts = changes;
      decision.invalidate_dataset = true;
      fill_generations(state, decision);
      return decision;
    }
  } else {
    state.source_generation = 1;
    state.physical_generation = 1;
    state.control_generation = 1;
  }
  state.initialized = true;
  state.owner_token = owner_token;
  state.configuration_known = valid_configuration;
  if (valid_configuration) state.configuration = current;
  decision.status = valid_configuration
                        ? (owner_changed ? GenerationStatus::OWNER_REINITIALIZED : GenerationStatus::INITIALIZED)
                        : GenerationStatus::INVALID_CONFIGURATION;
  decision.accepted = valid_configuration;
  decision.invalidate_dataset = true;
  decision.changes = GENERATION_CHANGE_OWNER | changes;
  decision.invalidated_contexts = changes;
  fill_generations(state, decision);
  return decision;
}

inline GenerationDecision observe_generation(GenerationOwnerState& state, uint64_t owner_token,
                                             const LearningSourceInput& input,
                                             const PhysicalContextTokens& physical_tokens,
                                             const ControlContextTokens& control_tokens) {
  using namespace generation_detail;
  GenerationDecision decision;
  if (!valid_owner_state(state)) return reject_invalid_owner_state(state);
  if (owner_token == 0) {
    decision.status = GenerationStatus::INVALID_OWNER;
    if (state.initialized) {
      fill_generations(state, decision);
    } else {
      decision.invalidate_dataset = true;
      decision.invalidated_contexts = GENERATION_CHANGE_SOURCE | GENERATION_CHANGE_PHYSICAL | GENERATION_CHANGE_CONTROL;
    }
    return decision;
  }
  if (!state.initialized) {
    decision.status = GenerationStatus::OWNER_NOT_INITIALIZED;
    decision.invalidate_dataset = true;
    decision.invalidated_contexts = GENERATION_CHANGE_SOURCE | GENERATION_CHANGE_PHYSICAL | GENERATION_CHANGE_CONTROL;
    return decision;
  }
  if (state.owner_token != owner_token) {
    decision.status = GenerationStatus::OWNER_MISMATCH;
    fill_generations(state, decision);
    return decision;
  }
  if (state.blocked) {
    decision.status = GenerationStatus::OWNER_BLOCKED;
    fill_generations(state, decision);
    return decision;
  }

  ExactGenerationConfiguration current;
  const bool valid_configuration = capture_configuration(input, physical_tokens, control_tokens, current);
  if (!valid_configuration) return reject_unknown_configuration(state);

  uint8_t changes = GENERATION_CHANGE_NONE;
  if (!state.configuration_known || !same_source_configuration(state.configuration.sources, current.sources))
    changes |= GENERATION_CHANGE_SOURCE;
  if (!state.configuration_known || !same_physical_context(state.configuration.physical, current.physical))
    changes |= GENERATION_CHANGE_PHYSICAL;
  if (!state.configuration_known || !same_control_context(state.configuration.control, current.control))
    changes |= GENERATION_CHANGE_CONTROL;
  if (!increment_generations(state, changes)) {
    state.blocked = true;
    state.configuration_known = false;
    decision.status = GenerationStatus::GENERATION_OVERFLOW;
    decision.changes = changes;
    decision.invalidated_contexts = changes;
    decision.invalidate_dataset =
        (changes & (GENERATION_CHANGE_SOURCE | GENERATION_CHANGE_PHYSICAL)) != GENERATION_CHANGE_NONE;
    fill_generations(state, decision);
    return decision;
  }
  state.configuration = current;
  state.configuration_known = true;
  decision.status = GenerationStatus::READY;
  decision.accepted = true;
  decision.changes = changes;
  decision.invalidated_contexts = changes;
  decision.reset_segment = changes != GENERATION_CHANGE_NONE;
  decision.invalidate_dataset =
      (changes & (GENERATION_CHANGE_SOURCE | GENERATION_CHANGE_PHYSICAL)) != GENERATION_CHANGE_NONE;
  fill_generations(state, decision);
  return decision;
}

inline OwnedSourceObserveResult start_owned_source_input(GenerationOwnerState& generation_state, uint64_t owner_token,
                                                         const PhysicalContextTokens& physical_tokens,
                                                         const ControlContextTokens& control_tokens,
                                                         SegmentAccumulator& accumulator, RecordBuffer& records,
                                                         const LearningSourceInput& input,
                                                         const QualityConfig& quality) {
  records.count = 0;
  reset_segment(accumulator);
  OwnedSourceObserveResult result;
  result.generation = start_generation_owner(generation_state, owner_token, DatasetScopeReset::CLEARED, input,
                                             physical_tokens, control_tokens);
  if (!result.generation.accepted) {
    result.observation.source.status = SnapshotSourceStatus::INVALID_CONFIGURATION;
    result.observation.aggregate.status = LearningStatus::INVALID_CONFIGURATION;
    return result;
  }
  LearningSourceInput stamped = input;
  stamped.source_cohort_generation = result.generation.source_generation;
  stamped.physical_context_generation = result.generation.physical_generation;
  stamped.control_generation = result.generation.control_generation;
  stamped.operation.captured_control_generation = stamped.control_generation;
  result.observation = observe_source_input(accumulator, stamped, quality);
  return result;
}

inline OwnedSourceObserveResult observe_owned_source_input(GenerationOwnerState& generation_state, uint64_t owner_token,
                                                           const PhysicalContextTokens& physical_tokens,
                                                           const ControlContextTokens& control_tokens,
                                                           SegmentAccumulator& accumulator, RecordBuffer& records,
                                                           const LearningSourceInput& input,
                                                           const QualityConfig& quality) {
  OwnedSourceObserveResult result;
  result.generation = observe_generation(generation_state, owner_token, input, physical_tokens, control_tokens);
  if (result.generation.invalidate_dataset) records.count = 0;
  if (result.generation.reset_segment) reset_segment(accumulator);
  if (!result.generation.accepted) {
    result.observation.source.status = SnapshotSourceStatus::INVALID_CONFIGURATION;
    result.observation.aggregate.status = LearningStatus::INVALID_CONFIGURATION;
    return result;
  }
  LearningSourceInput stamped = input;
  stamped.source_cohort_generation = result.generation.source_generation;
  stamped.physical_context_generation = result.generation.physical_generation;
  stamped.control_generation = result.generation.control_generation;
  stamped.operation.captured_control_generation = stamped.control_generation;
  result.observation = observe_source_input(accumulator, stamped, quality);
  return result;
}

}  // namespace oq_power_house::learning

#endif  // OQ_PH_LEARNING_CORE_AVAILABLE
