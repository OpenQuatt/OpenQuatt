#pragma once

#include "oq_ph_learning_platform.h"

#if OQ_PH_LEARNING_CORE_AVAILABLE

#include "../control/oq_input_source_logic.h"
#include "../boiler/oq_boiler_logic.h"
#include "../sources/oq_resolved_learning_source.h"
#include "../sources/oq_source_receipt_runtime.h"
#include "oq_ph_learning_source_logic.h"

namespace oq_power_house::learning {

// Resolvers remember intermediate route changes, even if the selected route is
// back to A at the next learner tick. These counters are never persisted.
inline bool observe_source_revisions(uint32_t previous[4], const oq_sources::ResolvedLearningSource sources[4]) {
  bool changed = false;
  for (size_t index = 0; index < 4; ++index) {
    changed = changed || previous[index] != sources[index].configuration_generation;
    previous[index] = sources[index].configuration_generation;
  }
  return changed;
}

// These are measurement cadence limits, independent of selected-value holds.
constexpr MeasurementTimingContract kHpLearningTiming{45000, 30000};
constexpr MeasurementTimingContract kRoomLearningTiming{120000, 30000};

inline void apply_compile_time_installation_contract(LearningSourceInput& input, bool topology_duo) {
  input.topology = topology_duo ? HydronicTopology::DUO_SERIES : HydronicTopology::SINGLE;
  input.calorimetry.meter_boundary =
      topology_duo ? MeterBoundary::SHARED_DUO_SERIES_CIRCUIT : MeterBoundary::SINGLE_HEAT_PUMP_CIRCUIT;
  input.calorimetry.fluid_model = FluidHeatCapacityModel::WATER_CP_4180;
  input.calorimetry.duo_series_order = topology_duo ? DuoSeriesOrder::HP1_TO_HP2 : DuoSeriesOrder::UNKNOWN;
}

inline const char* snapshot_source_status_name(SnapshotSourceStatus status) {
  switch (status) {
    case SnapshotSourceStatus::OK:
      return "ok";
    case SnapshotSourceStatus::INVALID_CONFIGURATION:
      return "invalid_configuration";
    case SnapshotSourceStatus::INVALID_TOPOLOGY:
      return "invalid_topology";
    case SnapshotSourceStatus::INVALID_METER_BOUNDARY:
      return "invalid_meter_boundary";
    case SnapshotSourceStatus::INVALID_CALORIMETRY_CONTRACT:
      return "invalid_calorimetry_contract";
    case SnapshotSourceStatus::INVALID_UNCERTAINTY:
      return "invalid_uncertainty";
    case SnapshotSourceStatus::MISSING_REQUIRED_UNIT:
      return "missing_required_unit";
    case SnapshotSourceStatus::UNEXPECTED_UNIT:
      return "unexpected_unit";
    case SnapshotSourceStatus::MISSING_MEASUREMENT:
      return "missing_measurement";
    case SnapshotSourceStatus::INVALID_VALUE:
      return "invalid_value";
    case SnapshotSourceStatus::UNKNOWN_SOURCE:
      return "unknown_source";
    case SnapshotSourceStatus::SOURCE_UNIT_MISMATCH:
      return "source_unit_mismatch";
    case SnapshotSourceStatus::DUPLICATE_TEMPERATURE_SOURCE:
      return "duplicate_temperature_source";
    case SnapshotSourceStatus::UNTRUSTED_PROVENANCE:
      return "untrusted_provenance";
    case SnapshotSourceStatus::INVALID_TIMING_CONTRACT:
      return "invalid_timing_contract";
    case SnapshotSourceStatus::INVALID_TIMESTAMP:
      return "invalid_timestamp";
    case SnapshotSourceStatus::SOURCE_STALE:
      return "source_stale";
    case SnapshotSourceStatus::TIME_SKEW:
      return "time_skew";
    case SnapshotSourceStatus::INVALID_MODE:
      return "invalid_mode";
    case SnapshotSourceStatus::COOLING_ACTIVE:
      return "cooling_active";
    case SnapshotSourceStatus::INCONSISTENT_ACTIVITY:
      return "inconsistent_activity";
    case SnapshotSourceStatus::PROTECTION_ACTIVE:
      return "protection_active";
    case SnapshotSourceStatus::BOILER_UNKNOWN:
      return "boiler_unknown";
    case SnapshotSourceStatus::BOILER_ACTIVE:
      return "boiler_active";
    case SnapshotSourceStatus::ZERO_FLOW_NOT_PROVEN:
      return "zero_flow_not_proven";
    case SnapshotSourceStatus::OPERATIONAL_CONTEXT_UNKNOWN:
      return "operational_context_unknown";
    case SnapshotSourceStatus::CONTROL_MODE_BLOCKED:
      return "control_mode_blocked";
    case SnapshotSourceStatus::ACTIVE_LIMIT:
      return "active_limit";
    case SnapshotSourceStatus::SERVICE_OR_OTA_ACTIVE:
      return "service_or_ota_active";
    case SnapshotSourceStatus::SETPOINT_RECOVERY_ACTIVE:
      return "setpoint_recovery_active";
    case SnapshotSourceStatus::COMFORT_UNACCEPTABLE:
      return "comfort_unacceptable";
    case SnapshotSourceStatus::OPERATIONAL_CONTEXT_STALE:
      return "operational_context_stale";
    case SnapshotSourceStatus::CONTROL_GENERATION_MISMATCH:
      return "control_generation_mismatch";
    case SnapshotSourceStatus::FLOW_OUT_OF_RANGE:
      return "flow_out_of_range";
    case SnapshotSourceStatus::SERIES_JUNCTION_MISMATCH:
      return "series_junction_mismatch";
  }
  return "unknown";
}

template <typename T>
inline bool live_measurement_fresh(const PhysicalMeasurement<T>& measurement, uint64_t now_ms) {
  return measurement.valid && measurement.source_generation != 0U && measurement.received_monotonic_ms != 0U &&
         measurement.received_monotonic_ms <= now_ms && measurement.timing.max_age_ms != 0U &&
         now_ms - measurement.received_monotonic_ms <= measurement.timing.max_age_ms;
}

struct DiagnosticHeatMeasurement {
  float heat_to_water_w = NAN;
  bool valid = false;
};

inline DiagnosticHeatMeasurement diagnostic_signed_heat(const LearningSourceInput& input,
                                                        const QualityConfig& quality) {
  DiagnosticHeatMeasurement result;
  if (!valid_quality_config(quality) ||
      (input.topology != HydronicTopology::SINGLE && input.topology != HydronicTopology::DUO_SERIES) ||
      !input.hp1.present || (input.topology == HydronicTopology::DUO_SERIES && !input.hp2.present) ||
      (input.topology == HydronicTopology::SINGLE && input.hp2.present))
    return result;

  source_detail::MeasurementMeta measurements[5];
  size_t measurement_count = 0;
  SnapshotSourceStatus status =
      source_detail::append_measurement(input.flow_lph, input.monotonic_ms, measurements, measurement_count);
  if (status == SnapshotSourceStatus::OK)
    status = source_detail::append_unit_measurement(input.hp1.water_in_c, PhysicalUnit::HP1, input.monotonic_ms,
                                                    measurements, measurement_count);
  if (status == SnapshotSourceStatus::OK)
    status = source_detail::append_unit_measurement(input.hp1.water_out_c, PhysicalUnit::HP1, input.monotonic_ms,
                                                    measurements, measurement_count);
  if (status == SnapshotSourceStatus::OK && input.topology == HydronicTopology::DUO_SERIES)
    status = source_detail::append_unit_measurement(input.hp2.water_in_c, PhysicalUnit::HP2, input.monotonic_ms,
                                                    measurements, measurement_count);
  if (status == SnapshotSourceStatus::OK && input.topology == HydronicTopology::DUO_SERIES)
    status = source_detail::append_unit_measurement(input.hp2.water_out_c, PhysicalUnit::HP2, input.monotonic_ms,
                                                    measurements, measurement_count);
  if (status != SnapshotSourceStatus::OK ||
      source_detail::validate_skew(measurements, measurement_count) != SnapshotSourceStatus::OK)
    return result;
  const MeterBoundary expected_boundary = input.topology == HydronicTopology::SINGLE
                                              ? MeterBoundary::SINGLE_HEAT_PUMP_CIRCUIT
                                              : MeterBoundary::SHARED_DUO_SERIES_CIRCUIT;
  if (input.calorimetry.meter_boundary != expected_boundary ||
      input.calorimetry.fluid_model != FluidHeatCapacityModel::WATER_CP_4180 ||
      input.calorimetry.calorimetry_generation == 0U || !input.calorimetry.uncertainty_proven ||
      !isfinite(input.calorimetry.heat_uncertainty_w) || input.calorimetry.heat_uncertainty_w < 0.0f ||
      input.calorimetry.heat_uncertainty_w > quality.max_abs_heat_w || !isfinite(input.calorimetry.max_flow_lph) ||
      input.calorimetry.max_flow_lph <= 0.0f || input.calorimetry.max_flow_lph > kAbsoluteMaxFlowLph ||
      !isfinite(input.flow_lph.value) || input.flow_lph.value < 0.0f ||
      input.flow_lph.value > input.calorimetry.max_flow_lph)
    return result;
  const auto water_in_range = [&](float value) {
    return isfinite(value) && value >= quality.water_min_c && value <= quality.water_max_c;
  };
  if (!water_in_range(input.hp1.water_in_c.value) || !water_in_range(input.hp1.water_out_c.value) ||
      (input.topology == HydronicTopology::DUO_SERIES &&
       (!water_in_range(input.hp2.water_in_c.value) || !water_in_range(input.hp2.water_out_c.value))))
    return result;
  if (source_detail::same_source(input.hp1.water_in_c.source, input.hp1.water_out_c.source)) return result;
  if (input.topology == HydronicTopology::DUO_SERIES) {
    const SourceIdentity temperature_sources[] = {input.hp1.water_in_c.source, input.hp1.water_out_c.source,
                                                  input.hp2.water_in_c.source, input.hp2.water_out_c.source};
    for (size_t left = 0; left < 4; ++left)
      for (size_t right = left + 1; right < 4; ++right)
        if (source_detail::same_source(temperature_sources[left], temperature_sources[right])) return result;
  }
  if (input.flow_lph.value == 0.0f &&
      input.calorimetry.zero_flow_proof != ZeroFlowProof::PHYSICAL_METER_COVERS_BOUNDARY)
    return result;
  double rise_k = static_cast<double>(input.hp1.water_out_c.value) - input.hp1.water_in_c.value;
  if (input.topology == HydronicTopology::DUO_SERIES) {
    if ((input.calorimetry.duo_series_order != DuoSeriesOrder::HP1_TO_HP2 &&
         input.calorimetry.duo_series_order != DuoSeriesOrder::HP2_TO_HP1) ||
        !isfinite(input.calorimetry.max_series_junction_delta_c) ||
        input.calorimetry.max_series_junction_delta_c <= 0.0f ||
        input.calorimetry.max_series_junction_delta_c > kAbsoluteMaxSeriesJunctionDeltaC)
      return result;
    const float junction_delta = input.calorimetry.duo_series_order == DuoSeriesOrder::HP1_TO_HP2
                                     ? fabsf(input.hp1.water_out_c.value - input.hp2.water_in_c.value)
                                     : fabsf(input.hp2.water_out_c.value - input.hp1.water_in_c.value);
    if (junction_delta > input.calorimetry.max_series_junction_delta_c) return result;
    rise_k += static_cast<double>(input.hp2.water_out_c.value) - input.hp2.water_in_c.value;
  }
  const double heat_w =
      static_cast<double>(input.flow_lph.value) * kWaterVolumetricHeatCapacityJPerLiterK / 3600.0 * rise_k;
  if (!isfinite(heat_w) || !isfinite(static_cast<float>(heat_w)) || fabs(heat_w) > quality.max_abs_heat_w)
    return result;
  result.heat_to_water_w = static_cast<float>(heat_w);
  result.valid = true;
  return result;
}

template <typename T>
inline PhysicalMeasurement<T> receipt_measurement(const oq_sources::RawFloatReceipt& receipt, T value,
                                                  SourceIdentity identity, uint32_t generation,
                                                  MeasurementTimingContract timing, bool value_valid = true) {
  PhysicalMeasurement<T> result;
  result.value = value;
  result.valid = receipt.received && receipt.valid && isfinite(receipt.value) && value_valid;
  result.source = identity;
  result.source_generation = generation;
  result.received_monotonic_ms = receipt.received_ms;
  result.timing = timing;
  result.provenance = MeasurementProvenance::PHYSICAL_RECEIPT;
  return result;
}

inline bool valid_register_word(float value) {
  return isfinite(value) && value >= 0.0f && value <= 65535.0f && floorf(value) == value;
}

inline HeatPumpMode decode_learning_hp_mode(float value) {
  if (value == 0.0f) return HeatPumpMode::OFF;
  if (value == 2.0f) return HeatPumpMode::HEATING;
  if (value == 1.0f || value == 3.0f) return HeatPumpMode::COOLING;
  return HeatPumpMode::UNKNOWN;
}

inline HeatPumpRawMeasurements learning_hp_measurements(const oq_sources::HeatPumpReceipts& raw, PhysicalUnit unit,
                                                        float inlet_offset_c, float outlet_offset_c,
                                                        uint32_t calibration_generation) {
  HeatPumpRawMeasurements hp;
  hp.present = true;
  const auto source = [unit](uint32_t address) {
    return SourceIdentity{PhysicalSourceKind::MODBUS_REGISTER, address, unit};
  };
  hp.water_in_c = receipt_measurement(raw.water_in, raw.water_in.value + inlet_offset_c, source(2133),
                                      calibration_generation, kHpLearningTiming, isfinite(inlet_offset_c));
  hp.water_out_c = receipt_measurement(raw.water_out, raw.water_out.value + outlet_offset_c, source(2134),
                                       calibration_generation, kHpLearningTiming, isfinite(outlet_offset_c));
  hp.mode = receipt_measurement(raw.working_mode, decode_learning_hp_mode(raw.working_mode.value), source(2099), 1,
                                kHpLearningTiming, valid_register_word(raw.working_mode.value));
  hp.compressor_active = receipt_measurement(
      raw.compressor_frequency, raw.compressor_frequency.value > 0.0f, source(2103), 1, kHpLearningTiming,
      raw.compressor_frequency.value >= 0.0f && raw.compressor_frequency.value <= 120.0f);
  hp.defrost_active = receipt_measurement(raw.defrost, raw.defrost.value != 0.0f, source(2118), 1, kHpLearningTiming,
                                          raw.defrost.value == 0.0f || raw.defrost.value == 1.0f);
  const bool relay_valid = valid_register_word(raw.status_2108.value);
  const bool protection_valid = valid_register_word(raw.status_2119.value);
  hp.valve_transition_active = receipt_measurement(
      raw.status_2108, relay_valid && (static_cast<uint16_t>(raw.status_2108.value) & 0x0040U) != 0U, source(2108), 1,
      kHpLearningTiming, relay_valid);
  hp.oil_return_active = receipt_measurement(
      raw.status_2119, protection_valid && (static_cast<uint16_t>(raw.status_2119.value) & 0x0008U) != 0U, source(2119),
      1, kHpLearningTiming, protection_valid);
  return hp;
}

inline bool learning_hp_protection_clear(const oq_sources::HeatPumpReceipts& raw, uint64_t now_ms) {
  return raw.status_2119.fresh(now_ms, kHpLearningTiming.max_age_ms) && raw.status_2119.value == 0.0f;
}

inline bool learning_strategy_output_current(bool output_valid, uint8_t output_source_code, uint8_t active_source_code,
                                             uint32_t now_ms, uint32_t updated_ms, uint32_t max_age_ms = 15000U) {
  return output_valid && (active_source_code == 2U || active_source_code == 3U) &&
         output_source_code == active_source_code && updated_ms != 0U &&
         static_cast<uint32_t>(now_ms - updated_ms) <= max_age_ms;
}

struct DiagnosticCaptureGate {
  bool continuity_valid = false;
};

struct DiagnosticCaptureDecision {
  bool capture = false;
  bool use_previous = false;
};

inline DiagnosticCaptureDecision diagnostic_capture_decision(DiagnosticCaptureGate& gate, bool opted_in) {
  if (!opted_in) {
    gate.continuity_valid = false;
    return {};
  }
  const DiagnosticCaptureDecision decision{true, gate.continuity_valid};
  gate.continuity_valid = true;
  return decision;
}

inline void pause_diagnostic_capture(DiagnosticCaptureGate& gate) { gate.continuity_valid = false; }

inline void reset_diagnostic_capture(DiagnosticCaptureGate& gate) { gate = {}; }

inline uint32_t diagnostic_coverage_seconds(uint32_t previous_epoch_s, uint32_t epoch_s, bool previous_heat_valid,
                                            bool heat_valid, uint32_t previous_source_generation,
                                            uint32_t source_generation, uint32_t previous_control_generation,
                                            uint32_t control_generation, uint32_t max_interval_ms) {
  if (previous_epoch_s == 0U || epoch_s <= previous_epoch_s || !previous_heat_valid || !heat_valid ||
      previous_source_generation == 0U || previous_source_generation != source_generation ||
      previous_control_generation == 0U || previous_control_generation != control_generation || max_interval_ms == 0U)
    return 0U;
  const uint32_t elapsed_s = epoch_s - previous_epoch_s;
  const uint32_t max_interval_s = max_interval_ms / 1000U + (max_interval_ms % 1000U != 0U ? 1U : 0U);
  return elapsed_s <= max_interval_s ? elapsed_s : 0U;
}

namespace live_detail {

struct RoutePolicy {
  bool supported = false;
  SourceIdentity identity;
  MeasurementTimingContract timing;
};

inline RoutePolicy route_policy(oq_sources::LearningSourceRoute route) {
  using oq_sources::LearningSourceRoute;
  switch (route) {
    case LearningSourceRoute::OPENTHERM_ROOM:
      return {true, {PhysicalSourceKind::OPENTHERM_FRAME, 24U, PhysicalUnit::SYSTEM}, kRoomLearningTiming};
    case LearningSourceRoute::OPENTHERM_SETPOINT:
      return {true, {PhysicalSourceKind::OPENTHERM_FRAME, 16U, PhysicalUnit::SYSTEM}, kRoomLearningTiming};
    case LearningSourceRoute::CIC_ROOM:
      return {true, {PhysicalSourceKind::CIC_FIELD, 1U, PhysicalUnit::SYSTEM}, kRoomLearningTiming};
    case LearningSourceRoute::CIC_SETPOINT:
      return {true, {PhysicalSourceKind::CIC_FIELD, 2U, PhysicalUnit::SYSTEM}, kRoomLearningTiming};
    case LearningSourceRoute::CIC_FLOW:
      return {true, {PhysicalSourceKind::CIC_FIELD, 3U, PhysicalUnit::SYSTEM}, kHpLearningTiming};
    case LearningSourceRoute::HP1_OUTSIDE:
      return {true, {PhysicalSourceKind::MODBUS_REGISTER, 2110U, PhysicalUnit::HP1}, kHpLearningTiming};
    case LearningSourceRoute::HP2_OUTSIDE:
      return {true, {PhysicalSourceKind::MODBUS_REGISTER, 2110U, PhysicalUnit::HP2}, kHpLearningTiming};
    case LearningSourceRoute::CONTROLLER_FLOW:
      return {true, {PhysicalSourceKind::EXTERNAL_METER, 1U, PhysicalUnit::SYSTEM}, kHpLearningTiming};
    case LearningSourceRoute::HP1_FLOW:
      return {true, {PhysicalSourceKind::MODBUS_REGISTER, 2138U, PhysicalUnit::HP1}, kHpLearningTiming};
    case LearningSourceRoute::HP2_FLOW:
      return {true, {PhysicalSourceKind::MODBUS_REGISTER, 2138U, PhysicalUnit::HP2}, kHpLearningTiming};
    default:
      return {};
  }
}

inline bool valid_component_pair(const oq_sources::ResolvedLearningSource& source) {
  using oq_sources::LearningSourceRoute;
  if (source.component_route == source.secondary_route || source.component_route == LearningSourceRoute::NONE ||
      source.secondary_route == LearningSourceRoute::NONE)
    return false;
  if (source.route == LearningSourceRoute::OUTSIDE_AGGREGATE) {
    return source.component_route == LearningSourceRoute::HP1_OUTSIDE &&
           source.secondary_route == LearningSourceRoute::HP2_OUTSIDE;
  }
  if (source.route != LearningSourceRoute::FLOW_AGGREGATE) return false;
  const bool first = source.component_route == LearningSourceRoute::HP1_FLOW ||
                     source.component_route == LearningSourceRoute::CONTROLLER_FLOW;
  return first && source.secondary_route == LearningSourceRoute::HP2_FLOW;
}

inline uint32_t composition_identity(oq_sources::LearningCompositeOperation operation,
                                     oq_sources::LearningSourceRoute first, oq_sources::LearningSourceRoute second) {
  const uint32_t operation_id = static_cast<uint32_t>(operation);
  const uint32_t first_id = static_cast<uint32_t>(first);
  const uint32_t second_id = static_cast<uint32_t>(second);
  if (operation_id == 0U || operation_id > 0xFFU || first_id == 0U || first_id > 0xFFU || second_id == 0U ||
      second_id > 0xFFU)
    return 0U;
  return (operation_id << 16U) | (first_id << 8U) | second_id;
}

inline bool within_selected_tolerance(oq_sources::LearningSourceRoute route, float selected, float composed) {
  if (!isfinite(selected) || !isfinite(composed)) return false;
  const float tolerance = route == oq_sources::LearningSourceRoute::FLOW_AGGREGATE ? 0.5f : 0.0001f;
  return fabsf(selected - composed) <= tolerance;
}

inline bool configuration_matches_route(const oq_sources::ResolvedLearningSource& source) {
  using oq_input_source::Source;
  using oq_sources::LearningSourceRoute;
  const auto selected = static_cast<Source>(source.configuration.selected);
  switch (source.route) {
    case LearningSourceRoute::OPENTHERM_ROOM:
    case LearningSourceRoute::OPENTHERM_SETPOINT:
      return selected == Source::OPENTHERM && source.configuration.endpoint_generation == 0U;
    case LearningSourceRoute::CIC_ROOM:
    case LearningSourceRoute::CIC_SETPOINT:
    case LearningSourceRoute::CIC_FLOW:
      return selected == Source::CIC && source.configuration.endpoint_generation != 0U;
    case LearningSourceRoute::HP1_OUTSIDE:
    case LearningSourceRoute::HP2_OUTSIDE:
    case LearningSourceRoute::OUTSIDE_AGGREGATE:
      return (selected == Source::OUTDOOR || selected == Source::AUTO) &&
             source.configuration.endpoint_generation == 0U;
    case LearningSourceRoute::CONTROLLER_FLOW:
    case LearningSourceRoute::HP1_FLOW:
    case LearningSourceRoute::HP2_FLOW:
    case LearningSourceRoute::FLOW_AGGREGATE:
      return selected == Source::OUTDOOR && source.configuration.endpoint_generation == 0U;
    default:
      return false;
  }
}

}  // namespace live_detail

inline PhysicalMeasurement<float> resolved_learning_measurement(const oq_sources::ResolvedLearningSource& source,
                                                                uint64_t now_ms) {
  PhysicalMeasurement<float> result;
  const auto policy = live_detail::route_policy(source.route);
  if (policy.supported) {
    result = receipt_measurement(source.receipt, source.value, policy.identity, source.configuration_generation,
                                 policy.timing,
                                 source.valid && live_detail::configuration_matches_route(source) &&
                                     source.provenance == oq_sources::LearningSourceProvenance::PHYSICAL_RECEIPT &&
                                     source.value == source.receipt.value && source.configuration_generation != 0U &&
                                     source.receipt.fresh(now_ms, policy.timing.max_age_ms));
    return result;
  }

  using oq_sources::LearningCompositeOperation;
  using oq_sources::LearningSourceProvenance;
  using oq_sources::LearningSourceRoute;
  const bool composite_route =
      source.route == LearningSourceRoute::OUTSIDE_AGGREGATE || source.route == LearningSourceRoute::FLOW_AGGREGATE;
  if (!composite_route) return result;
  const bool operation_valid = source.composite_operation == LearningCompositeOperation::ARITHMETIC_MEAN ||
                               (source.route == LearningSourceRoute::FLOW_AGGREGATE &&
                                source.composite_operation == LearningCompositeOperation::MAXIMUM);
  const uint32_t identity =
      live_detail::composition_identity(source.composite_operation, source.component_route, source.secondary_route);
  const uint64_t first_ms = source.receipt.received_ms;
  const uint64_t second_ms = source.secondary_receipt.received_ms;
  const uint64_t receipt_skew_ms = first_ms >= second_ms ? first_ms - second_ms : second_ms - first_ms;
  const bool receipts_valid = source.receipt.fresh(now_ms, kHpLearningTiming.max_age_ms) &&
                              source.secondary_receipt.fresh(now_ms, kHpLearningTiming.max_age_ms) &&
                              receipt_skew_ms <= kHpLearningTiming.max_skew_ms;
  const float composed = source.composite_operation == LearningCompositeOperation::ARITHMETIC_MEAN
                             ? 0.5f * (source.receipt.value + source.secondary_receipt.value)
                             : fmaxf(source.receipt.value, source.secondary_receipt.value);
  result.value = composed;
  result.valid = operation_valid && identity != 0U && source.valid &&
                 live_detail::configuration_matches_route(source) &&
                 source.provenance == LearningSourceProvenance::UNSUPPORTED && source.configuration_generation != 0U &&
                 live_detail::valid_component_pair(source) && receipts_valid &&
                 live_detail::within_selected_tolerance(source.route, source.value, composed);
  result.source = {PhysicalSourceKind::PHYSICAL_COMPOSITION, identity, PhysicalUnit::SYSTEM};
  result.source_generation = source.configuration_generation;
  result.received_monotonic_ms = first_ms >= second_ms ? first_ms : second_ms;
  result.timing = kHpLearningTiming;
  result.provenance = MeasurementProvenance::PHYSICAL_COMPOSITION;
  return result;
}

// CM2 owns heat-pump-only operation. Require the dispatched, current NONE
// command and withdrawn outputs as well: a mode label or R1-off alone is not
// enough during startup, a mode transition or a paused/stale dispatcher.
// This is controller evidence, not a physical heat measurement or a statement
// about solar, internal or independently controlled heat gains.
inline PhysicalMeasurement<BoilerHeatState> learning_cm2_boiler_contract(bool runtime_available, int control_mode,
                                                                         const oq_boiler::BoilerCommand& command,
                                                                         bool output_active, bool relay_active,
                                                                         uint64_t now_ms,
                                                                         bool applied_ot_command_active) {
  PhysicalMeasurement<BoilerHeatState> result;
  result.value = BoilerHeatState::NO_HEAT;
  result.valid = runtime_available && control_mode == 2 && now_ms != 0U &&
                 oq_boiler::command_is_fresh(command, static_cast<uint32_t>(now_ms), 15000U) &&
                 command.source == oq_boiler::COMMAND_SOURCE_NONE && !command.demand_present && !command.heat_request &&
                 !output_active && !relay_active && !applied_ot_command_active;
  result.source = {PhysicalSourceKind::CONTROL_CONTRACT, 2U, PhysicalUnit::SYSTEM};
  result.source_generation = 1U;
  result.received_monotonic_ms = now_ms;
  result.timing = kHpLearningTiming;
  result.provenance = MeasurementProvenance::CONTROL_CONTRACT;
  return result;
}

// A physical OpenTherm Status READ_ACK proves no central-heating/flame activity
// only while its owning transport is active. R1 relay commands never enter here.
inline PhysicalMeasurement<BoilerHeatState> learning_boiler_status(float payload, uint64_t received_ms, bool valid,
                                                                   bool transport_active) {
  oq_sources::RawFloatReceipt receipt;
  const bool payload_valid = valid && transport_active && valid_register_word(payload);
  receipt.observe(payload, received_ms, payload_valid);
  const uint16_t status = payload_valid ? static_cast<uint16_t>(payload) : 0U;
  const bool heating = (status & 0x000AU) != 0U;  // Slave CH-active or flame-on.
  return receipt_measurement(receipt, heating ? BoilerHeatState::HEAT_ACTIVE : BoilerHeatState::NO_HEAT,
                             {PhysicalSourceKind::OPENTHERM_FRAME, 0x10000U, PhysicalUnit::SYSTEM}, 1, {10000, 30000});
}

// CM2 is heat-pump-only for either boiler transport. OpenTherm is an optional
// contradiction check, not a required source. Keep the contract identity stable
// when telemetry disappears or activity changes; invalidate only the sample.
inline PhysicalMeasurement<BoilerHeatState> learning_boiler_heat(
    bool opentherm_selected, const PhysicalMeasurement<BoilerHeatState>& contract,
    const PhysicalMeasurement<BoilerHeatState>& telemetry) {
  auto result = contract;
  if (opentherm_selected && live_measurement_fresh(telemetry, contract.received_monotonic_ms) &&
      telemetry.value == BoilerHeatState::HEAT_ACTIVE)
    result.valid = false;
  return result;
}

}  // namespace oq_power_house::learning

#endif  // OQ_PH_LEARNING_CORE_AVAILABLE
