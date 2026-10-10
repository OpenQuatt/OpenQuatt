#include <assert.h>
#include <string.h>

#include <vector>

#include "../../openquatt/includes/learning/oq_ph_learning_journal.h"
#include "../../openquatt/includes/learning/oq_ph_learning_live_logic.h"
#include "../../openquatt/includes/learning/oq_ph_passive_runtime_logic.h"

namespace {
using namespace oq_power_house::learning;
using namespace oq_sources;
using namespace learning_journal_detail;
constexpr uint32_t kEpoch = 20000U * 86400U;

std::vector<uint8_t> context_bytes(bool legacy, unsigned source, bool duo = true, bool q = true, float offset = 0.0f,
                                   const char* generation = "V2", float pulses = 450.0f,
                                   const char* meter = "Huba Control") {
  uint8_t bytes[kMaxPassiveContextBytes];
  Writer writer{bytes, sizeof(bytes)};
  const auto str = [&](const char* text) {
    write_u32(writer, strlen(text));
    memcpy(bytes + writer.position, text, strlen(text));
    writer.position += strlen(text);
  };
  write_u32(writer, kLearningAlgorithmVersion);
  write_u32(writer, duo ? 2 : 1);
  write_u32(writer, q);
  if (legacy) {
    for (unsigned index = 0; index < 12; ++index) write_u32(writer, source);
    for (unsigned index = 0; index < 5; ++index) str(source ? "HA input" : "OT thermostat");
  } else {
    write_u32(writer, kLearningMeasurementContextMarker);
  }
  str(generation);
  if (legacy) str(source ? "http://new-cic.example" : "http://old-cic.example");
  if (q) {
    if (legacy) {
      str(source ? "Outdoor unit" : "Controller");
    }
    str(meter);
    write_float(writer, pulses);
  }
  write_float(writer, offset);
  write_float(writer, 0);
  if (duo) {
    write_float(writer, 0);
    write_float(writer, 0);
  }
  assert(writer.ok);
  return {bytes, bytes + writer.position};
}

PassiveTickInput tick(const std::vector<uint8_t>& bytes, uint32_t second, float outside = 7.0f) {
  PassiveTickInput input;
  input.context = {bytes.data(), bytes.size(), 1};
  input.now_monotonic_ms = 1000ULL + second * 1000ULL;
  input.now_epoch_s = kEpoch + second;
  input.opted_in = input.context_valid = input.active_line_valid = true;
  input.active_line = {150, 15};
  input.batch_snapshot_available = input.dynamic_snapshot_available = true;
  auto& snapshot = input.batch_snapshot;
  snapshot.monotonic_ms = input.now_monotonic_ms;
  snapshot.epoch_s = input.now_epoch_s;
  snapshot.context_revision = 1;
  snapshot.room_c = snapshot.setpoint_c = 20;
  snapshot.outside_c = outside;
  snapshot.heat_to_water_w = second / 3600 % 4 == 2 ? 0 : 2200;
  snapshot.mean_water_c = 30;
  input.dynamic_snapshot = snapshot;
  return input;
}

void test_normal_source_changes_finish_a_day_and_keep_thermal_intervals() {
  PassiveRuntimeStorage state;
  PassiveRuntimeConfig config;
  config.thermal_model.initial_heat_loss_w_per_k = 150;
  auto bytes = context_bytes(false, 0);
  assert(initialize_passive_runtime(state, {bytes.data(), bytes.size(), 1}, config, true) ==
         PassiveRuntimeStatus::COLLECTING);
  SourceConfigurationGeneration owner;
  unsigned route_changes = 0;
  uint32_t previous = 0;
  oq_input_source::HoldState hold;
  for (uint32_t minute = 0; minute <= 28 * 60; ++minute) {
    const unsigned source = minute / 180 % 2;
    oq_input_source::NumericSources values;
    values.ha = {minute / 60 % 2 ? 8.0f : 6.0f, true};
    values.outdoor = {7, true};
    // Manual HA/Auto selection changes every three hours; Auto's lowest route
    // changes hourly. These still update normal resolver metadata.
    const auto selected = source ? oq_input_source::Source::HA : oq_input_source::Source::AUTO;
    owner.observe({static_cast<uint8_t>(selected), 0, 0, 0});
    const auto value = oq_input_source::select_outside(selected, values, 1000U + minute * 60000U, 120000, hold);
    auto resolved = selected_source(value.value, value.valid,
                                    value.route == oq_input_source::Source::HA ? LearningSourceRoute::HA_OUTSIDE
                                                                               : LearningSourceRoute::OUTSIDE_AGGREGATE,
                                    owner.current());
    if (minute % 120 == 119) resolved.provenance = LearningSourceProvenance::HELD;
    const auto current = owner.observe_resolution(resolved);
    if (previous && previous != current) ++route_changes;
    previous = current;
    const auto next_context = context_bytes(false, source);
    assert(next_context == bytes);
    const auto input = tick(next_context, minute * 60U, value.value);
    tick_passive_runtime(state, input);
    assert(state.context_revision == 1 && !state.blocked);
    assert(state.diagnostics.last_batch_rejection == LearningStatus::OK);
  }
  assert(route_changes > 20);
  assert(state.record_count == 1 && state.records[0].start_epoch_s == kEpoch);
  assert(state.records[0].duration_s == 86400 && state.records[0].mean_heat_w < 2200);
  assert(state.thermal_state.accepted_samples >= 48);
  assert(segment_elapsed_ms(state.batch_accumulator) == 4ULL * 3600 * 1000);
  assert(!passive_runtime_summary(state, state.last_monotonic_ms).auto_apply_allowed);
}

void test_legacy_checkpoint_restores_after_manual_source_change() {
  for (bool duo : {false, true}) {
    for (bool q : {false, true}) {
      const auto legacy = context_bytes(true, 0, duo, q);
      const auto current = context_bytes(false, 1, duo, q);
      const auto changed_sources = context_bytes(true, 1, duo, q);
      assert(legacy != changed_sources);
      assert(daily_context_matches(legacy.data(), legacy.size(), current.data(), current.size()));
      assert(daily_context_matches(changed_sources.data(), changed_sources.size(), current.data(), current.size()));
      PassiveRuntimeStorage state;
      PassiveRuntimeConfig config;
      config.thermal_model.initial_heat_loss_w_per_k = 150;
      initialize_passive_runtime(state, {legacy.data(), legacy.size(), 1}, config, true);
      for (uint32_t second = 0; second <= 18 * 3600; second += 60) tick_passive_runtime(state, tick(legacy, second));
      uint8_t journal[kLearningJournalMaxBytes];
      size_t size = 0;
      constexpr uint32_t saved = kEpoch + 18 * 3600;
      assert(encode_learning_journal(passive_runtime_dataset(state), config.quality, 2, saved, journal, sizeof(journal),
                                     size, &state.thermal_state, saved,
                                     &state.batch_accumulator) == LearningJournalStatus::OK);
      const auto metadata =
          inspect_learning_journal({journal, size}, current.data(), current.size(), saved + 90, config.quality);
      assert(metadata.status == LearningJournalStatus::OK);
      LearningJournalRecords records{{journal, size}, metadata.context_size, metadata.record_count};
      PassiveRuntimeStorage restored;
      initialize_passive_runtime(restored, {current.data(), current.size(), 1}, config, true);
      uint32_t thermal_epoch = 0;
      assert(records.restore_thermal(restored.thermal_state, config.thermal_model, 1000, 1, thermal_epoch));
      assert(restored.thermal_state.accepted_samples == state.thermal_state.accepted_samples);
      assert(records.restore_daily(restored.batch_accumulator, {current.data(), current.size(), 1}, config.quality));
      for (uint32_t second = 18 * 3600 + 90; second <= 86430; second += 60)
        tick_passive_runtime(restored, tick(current, second));
      assert(restored.record_count == 1 && restored.records[0].start_epoch_s == kEpoch);
      assert(restored.records[0].duration_s == 86400);
      // A real calibration/hardware change still refuses that daily checkpoint.
      for (const auto& changed : {context_bytes(false, 1, duo, q, 0.5f), context_bytes(false, 1, duo, q, 0, "V1"),
                                  context_bytes(false, 1, !duo, q), context_bytes(false, 1, duo, !q)}) {
        SegmentAccumulator refused;
        assert(!records.restore_daily(refused, {changed.data(), changed.size(), 1}, config.quality));
      }
      if (q) {
        const auto changed = context_bytes(false, 1, duo, q, 0, "V2", 1000);
        assert(!daily_context_matches(legacy.data(), legacy.size(), changed.data(), changed.size()));
        const auto changed_meter = context_bytes(false, 1, duo, q, 0, "V2", 450, "Custom");
        assert(!daily_context_matches(legacy.data(), legacy.size(), changed_meter.data(), changed_meter.size()));
      }
      for (size_t truncated = 0; truncated < legacy.size(); ++truncated)
        assert(!daily_context_matches(legacy.data(), truncated, current.data(), current.size()));
      auto malformed = legacy;
      memset(malformed.data() + 60, 0xff, 4);  // Overflowing legacy string length.
      assert(!daily_context_matches(malformed.data(), malformed.size(), current.data(), current.size()));
    }
  }
}

void test_invalid_input_and_explicit_reset_still_interrupt_collection() {
  const auto bytes = context_bytes(false, 0);
  PassiveRuntimeStorage state;
  PassiveRuntimeConfig config;
  initialize_passive_runtime(state, {bytes.data(), bytes.size(), 1}, config, true);
  tick_passive_runtime(state, tick(bytes, 0));
  tick_passive_runtime(state, tick(bytes, 60));
  auto invalid = tick(bytes, 120);
  invalid.batch_snapshot.invalid_reasons = INVALID_ESSENTIAL_SOURCE;
  invalid.dynamic_snapshot = invalid.batch_snapshot;
  tick_passive_runtime(state, invalid);
  assert(!state.batch_accumulator.active);
  assert(state.diagnostics.last_batch_rejection == LearningStatus::INVALID_MEASUREMENT);
  tick_passive_runtime(state, tick(bytes, 180));
  assert(state.batch_accumulator.start_epoch_s == kEpoch + 180);
  reset_passive_runtime(state);
  assert(!state.initialized && state.record_count == 0 && state.thermal_state.accepted_samples == 0);
}
}  // namespace

int main() {
  test_normal_source_changes_finish_a_day_and_keep_thermal_intervals();
  test_legacy_checkpoint_restores_after_manual_source_change();
  test_invalid_input_and_explicit_reset_still_interrupt_collection();
}
