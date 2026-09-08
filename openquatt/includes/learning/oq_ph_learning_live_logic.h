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

inline void apply_compile_time_topology(LearningSourceInput& input, bool topology_duo) {
  input.topology = topology_duo ? HydronicTopology::DUO_SERIES : HydronicTopology::SINGLE;
}

inline const char* snapshot_source_status_name(SnapshotSourceStatus status) {
  switch (status) {
    case SnapshotSourceStatus::OK:
      return "ok";
    case SnapshotSourceStatus::INVALID_CONFIGURATION:
      return "invalid_configuration";
    case SnapshotSourceStatus::INVALID_TOPOLOGY:
      return "invalid_topology";
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
    case SnapshotSourceStatus::CONTEXT_REVISION_MISMATCH:
      return "context_revision_mismatch";
    case SnapshotSourceStatus::FLOW_OUT_OF_RANGE:
      return "flow_out_of_range";
  }
  return "unknown";
}

template <typename T>
inline bool live_measurement_fresh(const PhysicalMeasurement<T>& measurement, uint64_t now_ms) {
  return measurement.valid && measurement.source_generation != 0U && measurement.received_monotonic_ms != 0U &&
         measurement.received_monotonic_ms <= now_ms && measurement.timing.max_age_ms != 0U &&
         now_ms - measurement.received_monotonic_ms <= measurement.timing.max_age_ms;
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
                                            bool heat_valid, uint32_t previous_context_revision,
                                            uint32_t context_revision, uint32_t max_interval_ms) {
  if (previous_epoch_s == 0U || epoch_s <= previous_epoch_s || !previous_heat_valid || !heat_valid ||
      previous_context_revision == 0U || previous_context_revision != context_revision || max_interval_ms == 0U)
    return 0U;
  const uint32_t elapsed_s = epoch_s - previous_epoch_s;
  const uint32_t max_interval_s = max_interval_ms / 1000U + (max_interval_ms % 1000U != 0U ? 1U : 0U);
  return elapsed_s <= max_interval_s ? elapsed_s : 0U;
}

namespace live_detail {

inline MeasurementTimingContract selected_timing(oq_sources::LearningSourceRoute route) {
  using oq_sources::LearningSourceRoute;
  switch (route) {
    case LearningSourceRoute::OPENTHERM_ROOM:
    case LearningSourceRoute::OPENTHERM_SETPOINT:
    case LearningSourceRoute::CIC_ROOM:
    case LearningSourceRoute::CIC_SETPOINT:
    case LearningSourceRoute::HA_ROOM:
    case LearningSourceRoute::HA_SETPOINT:
    case LearningSourceRoute::API_ROOM:
    case LearningSourceRoute::API_SETPOINT:
    case LearningSourceRoute::MQTT_ROOM:
    case LearningSourceRoute::MQTT_SETPOINT:
      return kRoomLearningTiming;
    default:
      return kHpLearningTiming;
  }
}

}  // namespace live_detail

// Control has already selected and validated one value per signal. Learning
// uses that value directly, regardless of the selected route.
inline PhysicalMeasurement<float> resolved_learning_measurement(const oq_sources::ResolvedLearningSource& source,
                                                                uint64_t now_ms) {
  PhysicalMeasurement<float> result;
  result.value = source.value;
  result.valid = source.valid && isfinite(source.value) && source.route != oq_sources::LearningSourceRoute::NONE &&
                 source.configuration_generation != 0U && now_ms != 0U;
  result.source = {PhysicalSourceKind::CONTROL_CONTRACT, static_cast<uint32_t>(source.route), PhysicalUnit::SYSTEM};
  result.source_generation = source.configuration_generation;
  result.received_monotonic_ms = now_ms;
  result.timing = live_detail::selected_timing(source.route);
  result.provenance = MeasurementProvenance::SELECTED_VALUE;
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
