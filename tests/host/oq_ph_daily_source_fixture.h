#pragma once

#include "../../openquatt/includes/learning/oq_ph_learning_live_logic.h"
#include "../../openquatt/includes/learning/oq_ph_passive_runtime_logic.h"

namespace daily_source_fixture {
using namespace oq_power_house::learning;
constexpr uint32_t kEpoch = 20000U * 86400U;
constexpr uint8_t kContext[] = {'H', 'I', 'L', 1};

template <typename T>
inline PhysicalMeasurement<T> measured(T value, uint32_t field, PhysicalUnit unit, uint64_t now_ms) {
  return {value,
          true,
          {PhysicalSourceKind::MODBUS_REGISTER, field, unit},
          1,
          now_ms,
          {60000, 1000},
          MeasurementProvenance::PHYSICAL_RECEIPT};
}

inline HeatPumpRawMeasurements unit(PhysicalUnit identity, uint64_t now_ms, float inlet, float outlet) {
  return {true,
          measured(inlet, 2133, identity, now_ms),
          measured(outlet, 2134, identity, now_ms),
          measured(HeatPumpMode::HEATING, 2099, identity, now_ms),
          measured(true, 2108, identity, now_ms),
          measured(false, 2118, identity, now_ms),
          measured(false, 2120, identity, now_ms),
          measured(false, 2119, identity, now_ms)};
}

inline LearningSourceInput source(uint32_t second, float heat_w = 5000.0f) {
  LearningSourceInput raw;
  const uint64_t now_ms = 1000ULL + static_cast<uint64_t>(second) * 1000ULL;
  raw.monotonic_ms = now_ms;
  raw.epoch_s = kEpoch + second;
  raw.context_revision = 1;
  raw.topology = HydronicTopology::DUO_SERIES;
  raw.room_c = measured(20.0f, 1, PhysicalUnit::SYSTEM, now_ms);
  raw.setpoint_c = measured(20.0f, 2, PhysicalUnit::SYSTEM, now_ms);
  raw.outside_c = measured(5.0f, 2110, PhysicalUnit::HP1, now_ms);
  raw.flow_lph = measured(1000.0f, 2138, PhysicalUnit::HP1, now_ms);
  const float delta = heat_w * 3600.0f / (1000.0f * 4180.0f);
  raw.hp1 = unit(PhysicalUnit::HP1, now_ms, 30.0f - delta / 2, 30.0f);
  raw.hp2 = unit(PhysicalUnit::HP2, now_ms, 30.0f, 30.0f + delta / 2);
  raw.boiler_heat = measured(BoilerHeatState::NO_HEAT, 3, PhysicalUnit::SYSTEM, now_ms);
  raw.operation.control_mode_valid = raw.operation.active_limit_valid = raw.operation.service_or_ota_valid = true;
  raw.operation.captured_monotonic_ms = now_ms;
  raw.operation.captured_context_revision = 1;
  raw.operation.control_mode = LearningControlMode::HEATING;
  return raw;
}

inline PassiveTickInput production_tick(const LearningSourceInput& raw) {
  const auto calorimetry = evaluate_calorimetry(raw, QualityConfig{});
  const auto batch = build_learning_snapshot(raw, QualityConfig{}, SnapshotPurpose::STRUCTURAL_BATCH, &calorimetry);
  const auto dynamic = build_learning_snapshot(raw, QualityConfig{}, SnapshotPurpose::THERMAL_DYNAMIC, &calorimetry);
  PassiveTickInput input;
  input.context = {kContext, sizeof(kContext), raw.context_revision};
  input.now_monotonic_ms = raw.monotonic_ms;
  input.now_epoch_s = raw.epoch_s;
  input.opted_in = input.context_valid = input.active_line_valid = true;
  input.active_line = {150.0f, 15.0f};
  input.batch_snapshot_available = batch.has_snapshot;
  input.may_bridge_daily_gap = batch.may_bridge_daily_gap;
  input.batch_source_status = batch.status;
  input.batch_snapshot = batch.snapshot;
  input.dynamic_snapshot_available = dynamic.has_snapshot;
  input.dynamic_snapshot = dynamic.snapshot;
  return input;
}
}  // namespace daily_source_fixture
