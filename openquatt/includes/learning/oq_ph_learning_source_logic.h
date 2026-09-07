#pragma once

#include "oq_ph_learning_platform.h"

#if OQ_PH_LEARNING_CORE_AVAILABLE

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "oq_ph_learning_aggregate.h"
#include "../performance/oq_energy_logic.h"

namespace oq_power_house::learning {

constexpr float kWaterVolumetricHeatCapacityJPerLiterK = 4180.0f;
constexpr uint32_t kAbsoluteMaxMeasurementAgeMs = 60U * 60U * 1000U;
constexpr uint32_t kAbsoluteMaxMeasurementSkewMs = 60U * 60U * 1000U;
// Phase 1 uses one fixed water-side contract for the supported profiles. A
// later apply phase must establish measurement uncertainty independently.
constexpr float kPassiveMaximumFlowLph = 3000.0f;
constexpr float kPassiveSeriesJunctionToleranceC = 1.0f;
constexpr size_t kMaxSourceMeasurements = 19;
constexpr size_t kSystemSourceMeasurements = 5;
constexpr size_t kSourceMeasurementsPerHeatPump = 7;
static_assert(kSystemSourceMeasurements + 2U * kSourceMeasurementsPerHeatPump == kMaxSourceMeasurements,
              "Duo source metadata capacity must cover every required field");

inline bool power_cap_binds_filtered_demand(int filtered_demand, int power_cap) {
  return filtered_demand > 0 && power_cap < filtered_demand;
}

enum class PhysicalSourceKind : uint8_t {
  UNKNOWN = 0,
  DIRECT_SENSOR,
  MODBUS_REGISTER,
  OPENTHERM_FRAME,
  CIC_FIELD,
  HA_ENTITY,
  API_INGRESS,
  MQTT_INGRESS,
  EXTERNAL_METER,
  PHYSICAL_COMPOSITION,
  CONTROL_CONTRACT,
};

enum class PhysicalUnit : uint8_t {
  SYSTEM = 0,
  HP1 = 1,
  HP2 = 2,
};

enum class MeasurementProvenance : uint8_t {
  UNKNOWN = 0,
  PHYSICAL_RECEIPT,
  HELD,
  SYNTHESIZED,
  REPUBLISHED,
  PHYSICAL_COMPOSITION,
  CONTROL_CONTRACT,
};

struct SourceIdentity {
  PhysicalSourceKind kind = PhysicalSourceKind::UNKNOWN;
  uint32_t id = 0;  // Caller-owned stable identity for the exact endpoint or field.
  PhysicalUnit unit = PhysicalUnit::SYSTEM;
};

struct MeasurementTimingContract {
  uint32_t max_age_ms = 0;
  uint32_t max_skew_ms = 0;
};

template <typename T>
struct PhysicalMeasurement {
  T value{};
  bool valid = false;
  SourceIdentity source;
  uint32_t source_generation = 0;
  uint64_t received_monotonic_ms = 0;
  MeasurementTimingContract timing;
  MeasurementProvenance provenance = MeasurementProvenance::UNKNOWN;
};

enum class HeatPumpMode : uint8_t {
  UNKNOWN = 0,
  OFF,
  HEATING,
  COOLING,
  OTHER,
};

enum class BoilerHeatState : uint8_t {
  UNKNOWN = 0,
  NO_HEAT,
  HEAT_ACTIVE,
};

enum class HydronicTopology : uint8_t {
  UNKNOWN = 0,
  SINGLE,
  DUO_SERIES,
  DUO_PARALLEL,
};

struct HeatPumpRawMeasurements {
  bool present = false;
  PhysicalMeasurement<float> water_in_c;
  PhysicalMeasurement<float> water_out_c;
  PhysicalMeasurement<HeatPumpMode> mode;
  PhysicalMeasurement<bool> compressor_active;
  PhysicalMeasurement<bool> defrost_active;
  PhysicalMeasurement<bool> valve_transition_active;
  PhysicalMeasurement<bool> oil_return_active;
};

enum class LearningControlMode : uint8_t {
  UNKNOWN = 0,
  HEATING,
  OTHER,
};

struct LearningOperationalContext {
  uint64_t captured_monotonic_ms = 0;
  uint32_t captured_context_revision = 0;
  bool control_mode_valid = false;
  LearningControlMode control_mode = LearningControlMode::UNKNOWN;
  bool active_limit_valid = false;
  bool active_limit = false;
  bool service_or_ota_valid = false;
  bool service_or_ota = false;
  bool setpoint_recovery_valid = false;
  bool setpoint_recovery = false;
  bool comfort_acceptable_valid = false;
  bool comfort_acceptable = false;
};

struct LearningSourceInput {
  uint64_t monotonic_ms = 0;
  uint32_t epoch_s = 0;
  uint32_t context_revision = 0;
  HydronicTopology topology = HydronicTopology::UNKNOWN;
  PhysicalMeasurement<float> room_c;
  PhysicalMeasurement<float> setpoint_c;
  PhysicalMeasurement<float> outside_c;
  PhysicalMeasurement<float> flow_lph;
  HeatPumpRawMeasurements hp1;
  HeatPumpRawMeasurements hp2;
  PhysicalMeasurement<BoilerHeatState> boiler_heat;
  LearningOperationalContext operation;
};

enum class SnapshotSourceStatus : uint8_t {
  OK = 0,
  INVALID_CONFIGURATION,
  INVALID_TOPOLOGY,
  MISSING_REQUIRED_UNIT,
  UNEXPECTED_UNIT,
  MISSING_MEASUREMENT,
  INVALID_VALUE,
  UNKNOWN_SOURCE,
  SOURCE_UNIT_MISMATCH,
  DUPLICATE_TEMPERATURE_SOURCE,
  UNTRUSTED_PROVENANCE,
  INVALID_TIMING_CONTRACT,
  INVALID_TIMESTAMP,
  SOURCE_STALE,
  TIME_SKEW,
  INVALID_MODE,
  COOLING_ACTIVE,
  INCONSISTENT_ACTIVITY,
  PROTECTION_ACTIVE,
  BOILER_UNKNOWN,
  BOILER_ACTIVE,
  OPERATIONAL_CONTEXT_UNKNOWN,
  CONTROL_MODE_BLOCKED,
  ACTIVE_LIMIT,
  SERVICE_OR_OTA_ACTIVE,
  SETPOINT_RECOVERY_ACTIVE,
  COMFORT_UNACCEPTABLE,
  OPERATIONAL_CONTEXT_STALE,
  CONTEXT_REVISION_MISMATCH,
  FLOW_OUT_OF_RANGE,
  SERIES_JUNCTION_MISMATCH,
};

struct SnapshotBuildResult {
  SnapshotSourceStatus status = SnapshotSourceStatus::INVALID_CONFIGURATION;
  // A timestamped invalid snapshot is still an observation and must be passed to the aggregate.
  bool has_snapshot = false;
  bool measurement_valid = false;
  uint32_t invalid_reasons = INVALID_ESSENTIAL_SOURCE;
  LearningSnapshot snapshot;
};

struct SnapshotDiagnostics {
  SnapshotSourceStatus status = SnapshotSourceStatus::INVALID_CONFIGURATION;
  uint32_t invalid_reasons = INVALID_ESSENTIAL_SOURCE;
};

inline SnapshotDiagnostics combined_snapshot_diagnostics(const SnapshotBuildResult& batch,
                                                         const SnapshotBuildResult& dynamic) {
  // Both paths contribute to current_observation_valid. Dynamic learning may
  // accept recovery/comfort periods that the structural batch must exclude.
  return {dynamic.status != SnapshotSourceStatus::OK ? dynamic.status : batch.status,
          batch.invalid_reasons | dynamic.invalid_reasons};
}

struct SourceObserveResult {
  SnapshotBuildResult source;
  ObserveResult aggregate;
};

enum class SnapshotPurpose : uint8_t { STRUCTURAL_BATCH = 0, THERMAL_DYNAMIC };

namespace source_detail {

struct MeasurementMeta {
  SourceIdentity source;
  uint32_t source_generation = 0;
  uint64_t received_monotonic_ms = 0;
  MeasurementTimingContract timing;
};

inline bool same_source(const SourceIdentity& lhs, const SourceIdentity& rhs) {
  return lhs.kind == rhs.kind && lhs.id == rhs.id && lhs.unit == rhs.unit;
}

inline SnapshotBuildResult failure(const LearningSourceInput& input, SnapshotSourceStatus status, uint32_t reasons) {
  SnapshotBuildResult result;
  result.status = status;
  result.invalid_reasons = reasons == INVALID_NONE ? INVALID_ESSENTIAL_SOURCE : reasons;
  result.has_snapshot = input.monotonic_ms != 0 && input.epoch_s != 0;
  result.snapshot.monotonic_ms = input.monotonic_ms;
  result.snapshot.epoch_s = input.epoch_s;
  result.snapshot.context_revision = input.context_revision;
  result.snapshot.invalid_reasons = result.invalid_reasons;
  return result;
}

inline bool known_source_kind(PhysicalSourceKind kind) {
  switch (kind) {
    case PhysicalSourceKind::DIRECT_SENSOR:
    case PhysicalSourceKind::MODBUS_REGISTER:
    case PhysicalSourceKind::OPENTHERM_FRAME:
    case PhysicalSourceKind::CIC_FIELD:
    case PhysicalSourceKind::HA_ENTITY:
    case PhysicalSourceKind::API_INGRESS:
    case PhysicalSourceKind::MQTT_INGRESS:
    case PhysicalSourceKind::EXTERNAL_METER:
    case PhysicalSourceKind::PHYSICAL_COMPOSITION:
    case PhysicalSourceKind::CONTROL_CONTRACT:
      return true;
    case PhysicalSourceKind::UNKNOWN:
      return false;
  }
  return false;
}

inline bool known_physical_unit(PhysicalUnit unit) {
  return unit == PhysicalUnit::SYSTEM || unit == PhysicalUnit::HP1 || unit == PhysicalUnit::HP2;
}

template <typename T>
inline SnapshotSourceStatus append_measurement(const PhysicalMeasurement<T>& measurement, uint64_t now_ms,
                                               MeasurementMeta* measurements, size_t& count,
                                               bool allow_control_contract = false) {
  if (!measurement.valid) return SnapshotSourceStatus::MISSING_MEASUREMENT;
  if (!known_source_kind(measurement.source.kind) || !known_physical_unit(measurement.source.unit) ||
      measurement.source.id == 0 || measurement.source_generation == 0)
    return SnapshotSourceStatus::UNKNOWN_SOURCE;
  const bool composition = measurement.provenance == MeasurementProvenance::PHYSICAL_COMPOSITION &&
                           measurement.source.kind == PhysicalSourceKind::PHYSICAL_COMPOSITION;
  const bool declaration = allow_control_contract &&
                           measurement.provenance == MeasurementProvenance::CONTROL_CONTRACT &&
                           measurement.source.kind == PhysicalSourceKind::CONTROL_CONTRACT &&
                           measurement.source.unit == PhysicalUnit::SYSTEM;
  if (!composition && !declaration &&
      (measurement.provenance != MeasurementProvenance::PHYSICAL_RECEIPT ||
       measurement.source.kind == PhysicalSourceKind::CONTROL_CONTRACT ||
       measurement.source.kind == PhysicalSourceKind::PHYSICAL_COMPOSITION))
    return SnapshotSourceStatus::UNTRUSTED_PROVENANCE;
  if (measurement.timing.max_age_ms == 0 || measurement.timing.max_age_ms > kAbsoluteMaxMeasurementAgeMs ||
      measurement.timing.max_skew_ms == 0 || measurement.timing.max_skew_ms > kAbsoluteMaxMeasurementSkewMs)
    return SnapshotSourceStatus::INVALID_TIMING_CONTRACT;
  if (measurement.received_monotonic_ms == 0 || measurement.received_monotonic_ms > now_ms)
    return SnapshotSourceStatus::INVALID_TIMESTAMP;
  if (now_ms - measurement.received_monotonic_ms > measurement.timing.max_age_ms)
    return SnapshotSourceStatus::SOURCE_STALE;
  if (count >= kMaxSourceMeasurements) return SnapshotSourceStatus::INVALID_CONFIGURATION;
  measurements[count++] = {measurement.source, measurement.source_generation, measurement.received_monotonic_ms,
                           measurement.timing};
  return SnapshotSourceStatus::OK;
}

template <typename T>
inline SnapshotSourceStatus append_unit_measurement(const PhysicalMeasurement<T>& measurement,
                                                    PhysicalUnit expected_unit, uint64_t now_ms,
                                                    MeasurementMeta* measurements, size_t& count) {
  const SnapshotSourceStatus status = append_measurement(measurement, now_ms, measurements, count);
  if (status != SnapshotSourceStatus::OK) return status;
  return measurement.source.unit == expected_unit ? SnapshotSourceStatus::OK
                                                  : SnapshotSourceStatus::SOURCE_UNIT_MISMATCH;
}

inline SnapshotSourceStatus validate_skew(const MeasurementMeta* measurements, size_t count) {
  uint64_t newest_ms = 0;
  for (size_t index = 0; index < count; ++index)
    if (measurements[index].received_monotonic_ms > newest_ms) newest_ms = measurements[index].received_monotonic_ms;
  for (size_t index = 0; index < count; ++index)
    if (newest_ms - measurements[index].received_monotonic_ms > measurements[index].timing.max_skew_ms)
      return SnapshotSourceStatus::TIME_SKEW;
  return SnapshotSourceStatus::OK;
}

inline bool finite_measurement(const PhysicalMeasurement<float>& measurement) { return isfinite(measurement.value); }

inline SnapshotSourceStatus append_heat_pump(const HeatPumpRawMeasurements& heat_pump, PhysicalUnit expected_unit,
                                             uint64_t now_ms, MeasurementMeta* measurements, size_t& count) {
  SnapshotSourceStatus status =
      append_unit_measurement(heat_pump.water_in_c, expected_unit, now_ms, measurements, count);
  if (status != SnapshotSourceStatus::OK) return status;
  status = append_unit_measurement(heat_pump.water_out_c, expected_unit, now_ms, measurements, count);
  if (status != SnapshotSourceStatus::OK) return status;
  status = append_unit_measurement(heat_pump.mode, expected_unit, now_ms, measurements, count);
  if (status != SnapshotSourceStatus::OK) return status;
  status = append_unit_measurement(heat_pump.compressor_active, expected_unit, now_ms, measurements, count);
  if (status != SnapshotSourceStatus::OK) return status;
  status = append_unit_measurement(heat_pump.defrost_active, expected_unit, now_ms, measurements, count);
  if (status != SnapshotSourceStatus::OK) return status;
  status = append_unit_measurement(heat_pump.valve_transition_active, expected_unit, now_ms, measurements, count);
  if (status != SnapshotSourceStatus::OK) return status;
  return append_unit_measurement(heat_pump.oil_return_active, expected_unit, now_ms, measurements, count);
}

inline SnapshotSourceStatus validate_heat_pump_state(const HeatPumpRawMeasurements& heat_pump) {
  if (!finite_measurement(heat_pump.water_in_c) || !finite_measurement(heat_pump.water_out_c))
    return SnapshotSourceStatus::INVALID_VALUE;
  if (heat_pump.mode.value == HeatPumpMode::COOLING) return SnapshotSourceStatus::COOLING_ACTIVE;
  if (heat_pump.mode.value != HeatPumpMode::OFF && heat_pump.mode.value != HeatPumpMode::HEATING)
    return SnapshotSourceStatus::INVALID_MODE;
  if (heat_pump.mode.value == HeatPumpMode::OFF && heat_pump.compressor_active.value)
    return SnapshotSourceStatus::INCONSISTENT_ACTIVITY;
  if (heat_pump.defrost_active.value || heat_pump.valve_transition_active.value || heat_pump.oil_return_active.value)
    return SnapshotSourceStatus::PROTECTION_ACTIVE;
  return SnapshotSourceStatus::OK;
}

inline uint32_t reasons_for_status(SnapshotSourceStatus status) {
  switch (status) {
    case SnapshotSourceStatus::SOURCE_STALE:
    case SnapshotSourceStatus::TIME_SKEW:
      return INVALID_SOURCE_STALE;
    case SnapshotSourceStatus::INVALID_MODE:
    case SnapshotSourceStatus::INCONSISTENT_ACTIVITY:
      return INVALID_CONTROL_MODE;
    case SnapshotSourceStatus::COOLING_ACTIVE:
      return INVALID_COOLING;
    case SnapshotSourceStatus::PROTECTION_ACTIVE:
      return INVALID_DEFROST_OR_OIL_RETURN;
    case SnapshotSourceStatus::BOILER_ACTIVE:
      return INVALID_BOILER_HEAT;
    case SnapshotSourceStatus::FLOW_OUT_OF_RANGE:
    case SnapshotSourceStatus::SERIES_JUNCTION_MISMATCH:
      return INVALID_SOURCE_UNCERTAIN;
    default:
      return INVALID_ESSENTIAL_SOURCE;
  }
}

}  // namespace source_detail

struct CalorimetryResult {
  SnapshotSourceStatus status = SnapshotSourceStatus::INVALID_CONFIGURATION;
  float heat_to_water_w = NAN;
  float mean_water_c = NAN;
  bool valid = false;
};

// The same primitive feeds learner snapshots and diagnostic rows. It validates
// the fixed water / HP1->HP2 measurement boundary once and never infers heat
// from control demand or room response.
inline CalorimetryResult evaluate_calorimetry(const LearningSourceInput& input, const QualityConfig& quality) {
  CalorimetryResult result;
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
  if (status != SnapshotSourceStatus::OK) {
    result.status = status;
    return result;
  }
  status = source_detail::validate_skew(measurements, measurement_count);
  if (status != SnapshotSourceStatus::OK) {
    result.status = status;
    return result;
  }
  if (!isfinite(input.flow_lph.value) || input.flow_lph.value < 0.0f) {
    result.status = SnapshotSourceStatus::INVALID_VALUE;
    return result;
  }
  if (input.flow_lph.value > kPassiveMaximumFlowLph) {
    result.status = SnapshotSourceStatus::FLOW_OUT_OF_RANGE;
    return result;
  }
  const auto water_in_range = [&](float value) {
    return isfinite(value) && value >= quality.water_min_c && value <= quality.water_max_c;
  };
  if (!water_in_range(input.hp1.water_in_c.value) || !water_in_range(input.hp1.water_out_c.value) ||
      (input.topology == HydronicTopology::DUO_SERIES &&
       (!water_in_range(input.hp2.water_in_c.value) || !water_in_range(input.hp2.water_out_c.value)))) {
    result.status = SnapshotSourceStatus::INVALID_VALUE;
    return result;
  }
  const SourceIdentity temperature_sources[] = {input.hp1.water_in_c.source, input.hp1.water_out_c.source,
                                                input.hp2.water_in_c.source, input.hp2.water_out_c.source};
  const size_t temperature_count = input.topology == HydronicTopology::DUO_SERIES ? 4 : 2;
  for (size_t left = 0; left < temperature_count; ++left)
    for (size_t right = left + 1; right < temperature_count; ++right)
      if (source_detail::same_source(temperature_sources[left], temperature_sources[right])) {
        result.status = SnapshotSourceStatus::DUPLICATE_TEMPERATURE_SOURCE;
        return result;
      }
  double water_sum_c = static_cast<double>(input.hp1.water_in_c.value) + input.hp1.water_out_c.value;
  size_t water_count = 2;
  if (input.topology == HydronicTopology::DUO_SERIES) {
    if (fabsf(input.hp1.water_out_c.value - input.hp2.water_in_c.value) > kPassiveSeriesJunctionToleranceC) {
      result.status = SnapshotSourceStatus::SERIES_JUNCTION_MISMATCH;
      return result;
    }
    water_sum_c += static_cast<double>(input.hp2.water_in_c.value) + input.hp2.water_out_c.value;
    water_count = 4;
  }
  float heat_w = oq_energy::hydronic_heat_power(input.hp1.water_in_c.value, input.hp1.water_out_c.value,
                                                input.flow_lph.value, kWaterVolumetricHeatCapacityJPerLiterK);
  if (input.topology == HydronicTopology::DUO_SERIES)
    heat_w += oq_energy::hydronic_heat_power(input.hp2.water_in_c.value, input.hp2.water_out_c.value,
                                             input.flow_lph.value, kWaterVolumetricHeatCapacityJPerLiterK);
  const double mean_water_c = water_sum_c / static_cast<double>(water_count);
  if (!isfinite(heat_w) || !isfinite(mean_water_c) || !isfinite(static_cast<float>(mean_water_c)) ||
      fabsf(heat_w) > quality.max_abs_heat_w) {
    result.status = SnapshotSourceStatus::INVALID_VALUE;
    return result;
  }
  result.status = SnapshotSourceStatus::OK;
  result.heat_to_water_w = heat_w;
  result.mean_water_c = static_cast<float>(mean_water_c);
  result.valid = true;
  return result;
}

inline SnapshotBuildResult build_learning_snapshot(const LearningSourceInput& input, const QualityConfig& quality,
                                                   SnapshotPurpose purpose = SnapshotPurpose::STRUCTURAL_BATCH,
                                                   const CalorimetryResult* prepared_calorimetry = nullptr) {
  using namespace source_detail;
  if (!valid_quality_config(quality) ||
      (purpose != SnapshotPurpose::STRUCTURAL_BATCH && purpose != SnapshotPurpose::THERMAL_DYNAMIC))
    return failure(input, SnapshotSourceStatus::INVALID_CONFIGURATION, INVALID_ESSENTIAL_SOURCE);
  if (input.monotonic_ms == 0 || input.epoch_s == 0 || input.context_revision == 0)
    return failure(input, SnapshotSourceStatus::INVALID_CONFIGURATION, INVALID_ESSENTIAL_SOURCE);
  if (input.topology != HydronicTopology::SINGLE && input.topology != HydronicTopology::DUO_SERIES)
    return failure(input, SnapshotSourceStatus::INVALID_TOPOLOGY, INVALID_SOURCE_UNCERTAIN);
  if (!input.hp1.present) return failure(input, SnapshotSourceStatus::MISSING_REQUIRED_UNIT, INVALID_ESSENTIAL_SOURCE);
  if (input.topology == HydronicTopology::DUO_SERIES && !input.hp2.present)
    return failure(input, SnapshotSourceStatus::MISSING_REQUIRED_UNIT, INVALID_ESSENTIAL_SOURCE);
  if (input.topology == HydronicTopology::SINGLE && input.hp2.present)
    return failure(input, SnapshotSourceStatus::UNEXPECTED_UNIT, INVALID_SOURCE_UNCERTAIN);

  uint32_t unknown_operation_reasons = INVALID_NONE;
  if (!input.operation.control_mode_valid) unknown_operation_reasons |= INVALID_CONTROL_MODE;
  if (!input.operation.active_limit_valid) unknown_operation_reasons |= INVALID_ACTIVE_LIMIT;
  if (!input.operation.service_or_ota_valid) unknown_operation_reasons |= INVALID_SERVICE_OR_OTA;
  if (purpose == SnapshotPurpose::STRUCTURAL_BATCH) {
    if (!input.operation.setpoint_recovery_valid) unknown_operation_reasons |= INVALID_SETPOINT_RECOVERY;
    if (!input.operation.comfort_acceptable_valid) unknown_operation_reasons |= INVALID_CONTROL_MODE;
  }
  if (unknown_operation_reasons != INVALID_NONE)
    return failure(input, SnapshotSourceStatus::OPERATIONAL_CONTEXT_UNKNOWN, unknown_operation_reasons);
  const uint32_t all_operation_reasons =
      INVALID_CONTROL_MODE | INVALID_ACTIVE_LIMIT | INVALID_SERVICE_OR_OTA | INVALID_SETPOINT_RECOVERY;
  if (input.operation.captured_monotonic_ms == 0 || input.operation.captured_monotonic_ms > input.monotonic_ms ||
      input.monotonic_ms - input.operation.captured_monotonic_ms > quality.max_interval_ms)
    return failure(input, SnapshotSourceStatus::OPERATIONAL_CONTEXT_STALE, all_operation_reasons);
  if (input.operation.captured_context_revision != input.context_revision)
    return failure(input, SnapshotSourceStatus::CONTEXT_REVISION_MISMATCH, all_operation_reasons);
  if (input.operation.control_mode != LearningControlMode::HEATING)
    return failure(input, SnapshotSourceStatus::CONTROL_MODE_BLOCKED, INVALID_CONTROL_MODE);
  if (input.operation.active_limit) return failure(input, SnapshotSourceStatus::ACTIVE_LIMIT, INVALID_ACTIVE_LIMIT);
  if (input.operation.service_or_ota)
    return failure(input, SnapshotSourceStatus::SERVICE_OR_OTA_ACTIVE, INVALID_SERVICE_OR_OTA);
  if (purpose == SnapshotPurpose::STRUCTURAL_BATCH && input.operation.setpoint_recovery)
    return failure(input, SnapshotSourceStatus::SETPOINT_RECOVERY_ACTIVE, INVALID_SETPOINT_RECOVERY);
  if (purpose == SnapshotPurpose::STRUCTURAL_BATCH && !input.operation.comfort_acceptable)
    return failure(input, SnapshotSourceStatus::COMFORT_UNACCEPTABLE, INVALID_CONTROL_MODE);

  MeasurementMeta measurements[kMaxSourceMeasurements];
  size_t measurement_count = 0;
  SnapshotSourceStatus status = append_measurement(input.room_c, input.monotonic_ms, measurements, measurement_count);
  if (status == SnapshotSourceStatus::OK)
    status = append_measurement(input.setpoint_c, input.monotonic_ms, measurements, measurement_count);
  if (status == SnapshotSourceStatus::OK)
    status = append_measurement(input.outside_c, input.monotonic_ms, measurements, measurement_count);
  if (status == SnapshotSourceStatus::OK)
    status = append_measurement(input.flow_lph, input.monotonic_ms, measurements, measurement_count);
  if (status == SnapshotSourceStatus::OK)
    status = append_heat_pump(input.hp1, PhysicalUnit::HP1, input.monotonic_ms, measurements, measurement_count);
  if (status == SnapshotSourceStatus::OK && input.topology == HydronicTopology::DUO_SERIES)
    status = append_heat_pump(input.hp2, PhysicalUnit::HP2, input.monotonic_ms, measurements, measurement_count);
  if (status == SnapshotSourceStatus::OK)
    status = append_measurement(input.boiler_heat, input.monotonic_ms, measurements, measurement_count,
                                input.boiler_heat.value == BoilerHeatState::NO_HEAT);
  if (status != SnapshotSourceStatus::OK) return failure(input, status, reasons_for_status(status));

  if (!finite_measurement(input.room_c) || !finite_measurement(input.setpoint_c) ||
      !finite_measurement(input.outside_c) || input.room_c.value < quality.room_min_c ||
      input.room_c.value > quality.room_max_c || input.setpoint_c.value < quality.setpoint_min_c ||
      input.setpoint_c.value > quality.setpoint_max_c || input.outside_c.value < quality.outside_min_c ||
      input.outside_c.value > quality.outside_max_c)
    return failure(input, SnapshotSourceStatus::INVALID_VALUE, INVALID_ESSENTIAL_SOURCE);
  status = validate_heat_pump_state(input.hp1);
  if (status == SnapshotSourceStatus::OK && input.topology == HydronicTopology::DUO_SERIES)
    status = validate_heat_pump_state(input.hp2);
  if (status != SnapshotSourceStatus::OK) return failure(input, status, reasons_for_status(status));
  if (input.boiler_heat.value == BoilerHeatState::HEAT_ACTIVE)
    return failure(input, SnapshotSourceStatus::BOILER_ACTIVE, INVALID_BOILER_HEAT);
  if (input.boiler_heat.value != BoilerHeatState::NO_HEAT)
    return failure(input, SnapshotSourceStatus::BOILER_UNKNOWN, INVALID_ESSENTIAL_SOURCE | INVALID_BOILER_HEAT);

  status = validate_skew(measurements, measurement_count);
  if (status != SnapshotSourceStatus::OK) return failure(input, status, reasons_for_status(status));
  const CalorimetryResult calorimetry =
      prepared_calorimetry == nullptr ? evaluate_calorimetry(input, quality) : *prepared_calorimetry;
  if (!calorimetry.valid) return failure(input, calorimetry.status, reasons_for_status(calorimetry.status));

  SnapshotBuildResult result;
  result.status = SnapshotSourceStatus::OK;
  result.has_snapshot = true;
  result.measurement_valid = true;
  result.invalid_reasons = INVALID_NONE;
  result.snapshot.monotonic_ms = input.monotonic_ms;
  result.snapshot.epoch_s = input.epoch_s;
  result.snapshot.context_revision = input.context_revision;
  result.snapshot.invalid_reasons = INVALID_NONE;
  result.snapshot.room_c = input.room_c.value;
  result.snapshot.setpoint_c = input.setpoint_c.value;
  result.snapshot.outside_c = input.outside_c.value;
  result.snapshot.heat_to_water_w = calorimetry.heat_to_water_w;
  result.snapshot.mean_water_c = calorimetry.mean_water_c;
  return result;
}

inline SourceObserveResult observe_source_input(SegmentAccumulator& state, const LearningSourceInput& input,
                                                const QualityConfig& quality) {
  SourceObserveResult result;
  result.source = build_learning_snapshot(input, quality);
  if (!result.source.has_snapshot) {
    reset_segment(state);
    result.aggregate.status = LearningStatus::TIME_DISCONTINUITY;
    return result;
  }
  result.aggregate = observe_snapshot(state, result.source.snapshot, quality);
  return result;
}

}  // namespace oq_power_house::learning

#endif  // OQ_PH_LEARNING_CORE_AVAILABLE
