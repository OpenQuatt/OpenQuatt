#include <assert.h>
#include <math.h>

#include "../../openquatt/includes/learning/oq_ph_learning_fit.h"
#include "../../openquatt/includes/learning/oq_ph_learning_source_logic.h"

namespace {
using namespace oq_power_house;
using namespace oq_power_house::learning;

// These are explicit synthetic physical observations, not a claim that live
// selected sensors already expose this provenance or uncertainty contract.
template <typename T>
PhysicalMeasurement<T> measured(T value, uint32_t field, PhysicalUnit unit, uint64_t now_ms) {
  return {value,
          true,
          {PhysicalSourceKind::MODBUS_REGISTER, field, unit},
          1,
          now_ms,
          {60000, 1000},
          MeasurementProvenance::PHYSICAL_RECEIPT};
}

HeatPumpRawMeasurements unit(PhysicalUnit identity, uint64_t now_ms, float water_in, float water_out) {
  return {true,
          measured(water_in, 2133, identity, now_ms),
          measured(water_out, 2134, identity, now_ms),
          measured(HeatPumpMode::HEATING, 2099, identity, now_ms),
          measured(true, 2108, identity, now_ms),
          measured(false, 2118, identity, now_ms),
          measured(false, 2120, identity, now_ms),
          measured(false, 2119, identity, now_ms)};
}

LearningSourceInput observation(uint64_t now_ms, uint32_t epoch_s, float outside_c) {
  LearningSourceInput input;
  input.monotonic_ms = now_ms;
  input.epoch_s = epoch_s;
  input.source_cohort_generation = 1;
  input.physical_context_generation = 1;
  input.control_generation = 1;
  input.topology = HydronicTopology::DUO_SERIES;
  input.calorimetry.meter_boundary = MeterBoundary::SHARED_DUO_SERIES_CIRCUIT;
  input.calorimetry.fluid_model = FluidHeatCapacityModel::WATER_CP_4180;
  input.calorimetry.calorimetry_generation = 1;
  input.calorimetry.uncertainty_proven = true;
  input.calorimetry.heat_uncertainty_w = 50.0f;
  input.calorimetry.max_flow_lph = 3000.0f;
  input.calorimetry.max_series_junction_delta_c = 1.0f;
  input.calorimetry.duo_series_order = DuoSeriesOrder::HP1_TO_HP2;
  input.room_c = measured(20.0f, 1, PhysicalUnit::SYSTEM, now_ms);
  input.setpoint_c = measured(20.0f, 2, PhysicalUnit::SYSTEM, now_ms);
  input.outside_c = measured(outside_c, 2110, PhysicalUnit::HP1, now_ms);
  input.flow_lph = measured(1000.0f, 2138, PhysicalUnit::HP1, now_ms);
  const float total_delta = 200.0f * (16.0f - outside_c) * 3600.0f / (1000.0f * 4180.0f);
  input.hp1 = unit(PhysicalUnit::HP1, now_ms, 30.0f, 30.0f + total_delta / 2.0f);
  input.hp2 = unit(PhysicalUnit::HP2, now_ms, 30.0f + total_delta / 2.0f, 30.0f + total_delta);
  input.boiler_heat = measured(BoilerHeatState::NO_HEAT, 3, PhysicalUnit::SYSTEM, now_ms);
  input.operation.control_mode_valid = true;
  input.operation.captured_monotonic_ms = now_ms;
  input.operation.captured_control_generation = input.control_generation;
  input.operation.control_mode = LearningControlMode::HEATING;
  input.operation.active_limit_valid = true;
  input.operation.service_or_ota_valid = true;
  input.operation.setpoint_recovery_valid = true;
  input.operation.comfort_acceptable_valid = true;
  input.operation.comfort_acceptable = true;
  return input;
}

void raw_series_observations_to_advice() {
  constexpr uint32_t start_epoch = 20000U * 86400U;
  SegmentRecord records[kMaxSegmentRecords];
  RecordBuffer buffer{records, 0, kMaxSegmentRecords};
  SegmentAccumulator aggregate;
  QualityConfig quality;
  for (uint32_t day = 0; day < 9; ++day) {
    for (uint32_t slot = 0; slot < 2; ++slot) {
      for (uint32_t minute = 0; minute <= 240; ++minute) {
        const uint32_t seconds = day * 86400U + slot * 28800U + minute * 60U;
        const auto raw = observation(1000ULL + seconds * 1000ULL, start_epoch + seconds, -5.0f + 2.0f * day);
        const auto event = observe_source_input(aggregate, raw, quality);
        assert(event.source.measurement_valid);
        if (event.aggregate.has_record)
          assert(append_record(buffer, event.aggregate.record, raw.epoch_s, quality) == LearningStatus::OK);
      }
    }
  }
  assert(buffer.count == 18);
  FitConfig config;
  config.reference_room_c = 20.0f;
  config.reference_setpoint_c = 20.0f;
  AdviceFitWorkspace workspace;
  const HouseLine active{150.0f, 15.0f};
  auto status = begin_advice_fit(records, buffer.count, start_epoch + 9U * 86400U, active, quality, config, workspace);
  size_t steps = 0;
  while (status == LearningStatus::FIT_IN_PROGRESS) {
    assert(++steps <= kMaxAdviceFitSteps);
    status = advance_advice_fit(workspace);
  }
  assert(status == LearningStatus::ADVICE_READY);
  assert(fabsf(workspace.result.candidate.heat_loss_w_per_k - 200.0f) < 0.01f);
  assert(fabsf(workspace.result.candidate.zero_power_temp_c - 16.0f) < 0.001f);
  assert(workspace.result.holdout_candidate_mae_w < 0.01f);
  assert(active.heat_loss_w_per_k == 150.0f && active.zero_power_temp_c == 15.0f);
}

// A host-side size guard for the pure data only. This is not an ESP32 footprint
// or a substitute for the future strict-PSRAM and internal-heap HIL checks.
static_assert(sizeof(SegmentRecord) * kMaxSegmentRecords + sizeof(AdviceFitWorkspace) + sizeof(SegmentAccumulator) <
              32U * 1024U);
}  // namespace

int main() { raw_series_observations_to_advice(); }
