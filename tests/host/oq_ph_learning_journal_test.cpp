#include <assert.h>
#include <initializer_list>
#include <math.h>
#include <string.h>

#include "../../openquatt/includes/learning/oq_ph_learning_journal.h"
#include "../../openquatt/includes/learning/oq_ph_passive_runtime_logic.h"

namespace {
using namespace oq_power_house::learning;

constexpr uint8_t kContext[] = {9, 8, 7, 6, 5, 4, 3, 2, 1};

PassiveContextView context(uint32_t revision = 1) { return {kContext, sizeof(kContext), revision}; }

PassiveRuntimeConfig config() {
  PassiveRuntimeConfig value;
  value.thermal_model.initial_heat_loss_w_per_k = 150.0;
  return value;
}

SegmentRecord record(uint32_t start_epoch_s, float outside_c) {
  SegmentRecord value;
  value.start_epoch_s = start_epoch_s;
  value.end_epoch_s = start_epoch_s + 4U * 3600U;
  value.duration_s = 4U * 3600U;
  value.context_revision = 1;
  value.mean_room_c = 20.0f;
  value.mean_setpoint_c = 20.0f;
  value.mean_outside_c = outside_c;
  value.mean_heat_w = 200.0f * (16.0f - outside_c);
  value.room_trend_k_per_h = 0.0f;
  value.room_range_k = 0.0f;
  value.setpoint_range_c = 0.0f;
  value.water_start_c = 30.0f;
  value.water_end_c = 30.0f;
  return value;
}

void write_u16(uint8_t* bytes, size_t offset, uint16_t value) {
  bytes[offset] = static_cast<uint8_t>(value);
  bytes[offset + 1U] = static_cast<uint8_t>(value >> 8U);
}

void write_u32(uint8_t* bytes, size_t offset, uint32_t value) {
  for (uint8_t shift = 0; shift < 32U; shift += 8U) bytes[offset++] = static_cast<uint8_t>(value >> shift);
}

void repair_crc(uint8_t* bytes, size_t size) {
  write_u32(bytes, size - kLearningJournalCrcBytes, learning_journal_crc32(bytes, size - kLearningJournalCrcBytes));
}

uint16_t load_u16(const uint8_t* bytes, size_t offset) {
  return static_cast<uint16_t>(bytes[offset]) | static_cast<uint16_t>(bytes[offset + 1U]) << 8U;
}

uint32_t load_u32(const uint8_t* bytes, size_t offset) {
  uint32_t value = 0;
  for (uint8_t shift = 0; shift < 32U; shift += 8U) value |= static_cast<uint32_t>(bytes[offset++]) << shift;
  return value;
}

// Convert the latest fixture to the actual earlier record layout and tail size.
// Only the redundant record revision/duration and new metadata change; all
// original scalar/profile/thermal/checkpoint payload fields remain byte exact.
void legacy_schema(uint8_t* bytes, size_t& size, uint16_t schema, uint16_t algorithm = 4) {
  assert(schema >= 4 && schema <= 8 && load_u16(bytes, 4) == 9);
  const size_t context_size = load_u16(bytes, 24);
  for (uint16_t index = 0; index < load_u16(bytes, 22); ++index) {
    const size_t offset = kLearningJournalHeaderBytes + context_size + index * kLearningJournalRecordBytes;
    const uint16_t duration = load_u16(bytes, offset + 8U);
    write_u32(bytes, offset + 8U, duration == 0U ? 86400U : duration);
    write_u32(bytes, offset + 12U, load_u32(bytes, 28));
  }
  size -= kLearningJournalDailyBytes - learning_journal_daily_bytes(schema);
  if (schema == 4) size -= kLearningJournalThermalBytes;
  write_u16(bytes, 4, schema);
  write_u16(bytes, 20, algorithm);
  write_u32(bytes, 8, static_cast<uint32_t>(size));
  repair_crc(bytes, size);
}

void legacy_schema_seven(uint8_t* bytes, size_t& size) { legacy_schema(bytes, size, 7); }

void unchanged_record_values_and_thermal(const uint8_t* old_bytes, const uint8_t* new_bytes, size_t count) {
  const size_t records_offset = kLearningJournalHeaderBytes + sizeof(kContext);
  for (size_t index = 0; index < count; ++index) {
    const size_t offset = records_offset + index * kLearningJournalRecordBytes;
    assert(memcmp(old_bytes + offset, new_bytes + offset, 8U) == 0);
    assert(memcmp(old_bytes + offset + 16U, new_bytes + offset + 16U, 36U) == 0);
    const uint16_t duration = load_u16(new_bytes, offset + 8U);
    assert(load_u32(old_bytes, offset + 8U) == (duration == 0U ? 86400U : duration));
    assert(load_u32(old_bytes, offset + 12U) == load_u32(new_bytes, 28));
  }
  const size_t thermal_offset = records_offset + count * kLearningJournalRecordBytes;
  assert(memcmp(old_bytes + thermal_offset, new_bytes + thermal_offset, kLearningJournalThermalBytes) == 0);
}

SegmentAccumulator daily_checkpoint(uint32_t start_epoch, uint32_t duration_s = 4200) {
  SegmentAccumulator state;
  LearningSnapshot snapshot;
  snapshot.monotonic_ms = 7000;
  snapshot.epoch_s = start_epoch;
  snapshot.context_revision = 1;
  snapshot.room_c = snapshot.setpoint_c = 20;
  snapshot.outside_c = -5;
  snapshot.heat_to_water_w = 4200;
  snapshot.mean_water_c = 31;
  for (uint32_t elapsed = 0; elapsed <= duration_s; elapsed += 60) {
    snapshot.monotonic_ms = 7000ULL + elapsed * 1000ULL;
    snapshot.epoch_s = start_epoch + elapsed;
    assert(!observe_snapshot(state, snapshot, QualityConfig{}).has_record);
  }
  assert(valid_daily_checkpoint(state, QualityConfig{}));
  return state;
}

PassiveRuntimeStorage populated(uint32_t now_epoch_s) {
  PassiveRuntimeStorage state;
  assert(initialize_passive_runtime(state, context(), config(), true) == PassiveRuntimeStatus::COLLECTING);
  const uint32_t day = now_epoch_s / 86400U;
  state.records[0] = record((day - 2U) * 86400U + 22U * 3600U, -4.0f);
  state.records[1] = record((day - 1U) * 86400U + 3U * 3600U, 4.0f);
  state.record_count = 2;
  return state;
}

void test_round_trip_and_reboot_remap_never_restores_readiness() {
  static_assert(kLearningJournalHeaderBytes == 32U);
  static_assert(kLearningJournalRecordBytes == 52U);
  constexpr uint32_t now_epoch = 20000U * 86400U + 12U * 3600U;
  auto original = populated(now_epoch);
  uint8_t bytes[kLearningJournalMaxBytes];
  size_t size = 0;
  assert(encode_learning_journal(passive_runtime_dataset(original), original.config.quality, 41, now_epoch, bytes,
                                 sizeof(bytes), size) == LearningJournalStatus::OK);
  assert(size < 8192U);
  const auto metadata =
      inspect_learning_journal({bytes, size}, kContext, sizeof(kContext), now_epoch, config().quality);
  assert(metadata.status == LearningJournalStatus::OK && metadata.sequence == 41 && metadata.record_count == 2);

  PassiveRuntimeStorage restored;
  assert(initialize_passive_runtime(restored, context(7), config(), true) == PassiveRuntimeStatus::COLLECTING);
  assert(restore_passive_records(restored,
                                 LearningJournalRecords{{bytes, size}, metadata.context_size, metadata.record_count}));
  assert(restored.record_count == 2 && restored.restored_records && restored.fit_pending);
  assert(restored.records[0].context_revision == 7);
  PassiveRuntimeStorage aged;
  initialize_passive_runtime(aged, context(8), config(), true);
  assert(restore_passive_records(aged,
                                 LearningJournalRecords{{bytes, size}, metadata.context_size, metadata.record_count},
                                 now_epoch + kMaxRecordAgeS + 1));
  assert(aged.record_count == 0);

  assert(restored.thermal_state.accepted_samples == 0);
  const auto summary = passive_runtime_summary(restored, 1);
  assert(!summary.batch_advice_ready && !summary.thermal_model_ready);
  assert(!summary.cross_validated_advice_ready && !summary.auto_apply_allowed);
}

void test_sparse_season_round_trip_and_reboot_remap() {
  constexpr uint32_t now_epoch = 20000U * 86400U + 12U * 3600U;
  PassiveRuntimeStorage original;
  assert(initialize_passive_runtime(original, context(), config(), true) == PassiveRuntimeStatus::COLLECTING);
  const uint32_t earliest_end_epoch = now_epoch - kMaxRecordAgeS;
  for (size_t index = 0; index < kMaxSegmentRecords; ++index) {
    const uint32_t end_epoch =
        earliest_end_epoch + static_cast<uint32_t>(index * 365U / (kMaxSegmentRecords - 1U)) * 86400U;
    original.records[index] = record(end_epoch - 4U * 3600U, -8.0f + static_cast<float>(index % 20U));
  }
  original.record_count = kMaxSegmentRecords;

  uint8_t bytes[kLearningJournalMaxBytes];
  size_t size = 0;
  assert(encode_learning_journal(passive_runtime_dataset(original), original.config.quality, 42, now_epoch, bytes,
                                 sizeof(bytes), size) == LearningJournalStatus::OK);
  assert(size < 8192U);
  const auto metadata =
      inspect_learning_journal({bytes, size}, kContext, sizeof(kContext), now_epoch, original.config.quality);
  assert(metadata.status == LearningJournalStatus::OK && metadata.record_count == kMaxSegmentRecords);

  PassiveRuntimeStorage restored;
  assert(initialize_passive_runtime(restored, context(7), config(), true) == PassiveRuntimeStatus::COLLECTING);
  assert(restore_passive_records(restored,
                                 LearningJournalRecords{{bytes, size}, metadata.context_size, metadata.record_count}));
  assert(restored.record_count == kMaxSegmentRecords && restored.restored_records && restored.fit_pending);
  assert(restored.records[0].end_epoch_s == earliest_end_epoch);
  assert(restored.records[kMaxSegmentRecords - 1U].end_epoch_s == now_epoch);
  for (size_t index = 0; index < restored.record_count; ++index) assert(restored.records[index].context_revision == 7U);
}

// Schema 5 stores the actual count, not the firmware array capacity.
void test_previous_64_record_capacity_restores_without_reset() {
  constexpr uint32_t now = 20000U * 86400U + 12U * 3600U;
  auto original = populated(now);
  for (size_t index = 0; index < 64U; ++index)
    original.records[index] = record(now - static_cast<uint32_t>(64U - index) * 86400U, -4.0f);
  original.record_count = 64U;
  uint8_t bytes[4540U];  // Previous firmware's maximum encoded journal size.
  size_t size = 0;
  assert(encode_learning_journal(passive_runtime_dataset(original), original.config.quality, 1, now, bytes,
                                 sizeof(bytes), size) == LearningJournalStatus::OK);
  legacy_schema(bytes, size, 5, 3);
  const auto metadata = inspect_learning_journal({bytes, size}, kContext, sizeof(kContext), now, config().quality);
  assert(metadata.status == LearningJournalStatus::OK && metadata.record_count == 64U);
  PassiveRuntimeStorage restored;
  assert(initialize_passive_runtime(restored, context(), config(), true) == PassiveRuntimeStatus::COLLECTING);
  assert(restore_passive_records(restored,
                                 LearningJournalRecords{{bytes, size}, metadata.context_size, metadata.record_count}));
  assert(restored.record_count == 64U && restored.fit_pending);
  for (size_t index = 0; index < 64U; ++index)
    assert(restored.records[index].end_epoch_s == original.records[index].end_epoch_s);
}

void test_previous_algorithm_journal_restores_as_data_for_refit() {
  constexpr uint32_t now_epoch = 20000U * 86400U + 12U * 3600U;
  auto original = populated(now_epoch);
  uint8_t bytes[kLearningJournalMaxBytes];
  size_t size = 0;
  assert(encode_learning_journal(passive_runtime_dataset(original), original.config.quality, 43, now_epoch, bytes,
                                 sizeof(bytes), size) == LearningJournalStatus::OK);
  write_u16(bytes, 20U, kEarliestRestorableLearningAlgorithmVersion);
  repair_crc(bytes, size);
  const auto metadata =
      inspect_learning_journal({bytes, size}, kContext, sizeof(kContext), now_epoch, original.config.quality);
  assert(metadata.status == LearningJournalStatus::OK &&
         metadata.algorithm_version == kEarliestRestorableLearningAlgorithmVersion);

  PassiveRuntimeStorage restored;
  assert(initialize_passive_runtime(restored, context(7), config(), true) == PassiveRuntimeStatus::COLLECTING);
  assert(restore_passive_records(restored,
                                 LearningJournalRecords{{bytes, size}, metadata.context_size, metadata.record_count}));
  assert(restored.record_count == 2U && restored.fit_pending && !restored.fit_running);
  assert(!passive_runtime_summary(restored, 1).auto_apply_allowed);
}

void test_corrupt_truncated_schema_count_context_and_record_fail_closed() {
  constexpr uint32_t now_epoch = 20000U * 86400U + 12U * 3600U;
  auto state = populated(now_epoch);
  uint8_t bytes[kLearningJournalMaxBytes];
  size_t size = 0;
  assert(encode_learning_journal(passive_runtime_dataset(state), state.config.quality, 7, now_epoch, bytes,
                                 sizeof(bytes), size) == LearningJournalStatus::OK);

  uint8_t changed[kLearningJournalMaxBytes];
  memcpy(changed, bytes, size);
  changed[kLearningJournalHeaderBytes + 1U] ^= 0x80U;
  assert(
      inspect_learning_journal({changed, size}, kContext, sizeof(kContext), now_epoch, state.config.quality).status ==
      LearningJournalStatus::CORRUPT);
  assert(inspect_learning_journal({bytes, size - 1U}, kContext, sizeof(kContext), now_epoch, state.config.quality)
             .status == LearningJournalStatus::INVALID_LENGTH);
  const auto early_clock =
      inspect_learning_journal({bytes, size}, kContext, sizeof(kContext), now_epoch - 1U, state.config.quality);
  assert(early_clock.status == LearningJournalStatus::TIME_DISCONTINUITY);
  memcpy(changed, bytes, size);
  write_u16(changed, 4, kLearningJournalSchemaVersion + 1U);
  repair_crc(changed, size);
  assert(
      inspect_learning_journal({changed, size}, kContext, sizeof(kContext), now_epoch, state.config.quality).status ==
      LearningJournalStatus::INVALID_SCHEMA);

  memcpy(changed, bytes, size);
  write_u16(changed, 22, kMaxSegmentRecords + 1U);
  repair_crc(changed, size);
  assert(
      inspect_learning_journal({changed, size}, kContext, sizeof(kContext), now_epoch, state.config.quality).status ==
      LearningJournalStatus::INVALID_COUNT);

  constexpr uint8_t wrong_context[] = {9, 8, 7, 6, 5, 4, 3, 2, 0};
  assert(inspect_learning_journal({bytes, size}, wrong_context, sizeof(wrong_context), now_epoch, state.config.quality)
             .status == LearningJournalStatus::OK);

  memcpy(changed, bytes, size);
  const size_t mean_room_offset = kLearningJournalHeaderBytes + sizeof(kContext) + 16U;
  write_u32(changed, mean_room_offset, 0x7FC00000U);
  repair_crc(changed, size);
  assert(
      inspect_learning_journal({changed, size}, kContext, sizeof(kContext), now_epoch, state.config.quality).status ==
      LearningJournalStatus::INVALID_RECORD);

  assert(inspect_learning_journal({bytes, size}, kContext, sizeof(kContext), now_epoch + kMaxRecordAgeS + 1U,
                                  state.config.quality)
             .status == LearningJournalStatus::OK);
}

void test_two_slot_selection_survives_torn_new_write_and_rejects_sequence_aba() {
  constexpr uint32_t now_epoch = 20000U * 86400U + 12U * 3600U;
  auto state = populated(now_epoch);
  uint8_t old_slot[kLearningJournalMaxBytes];
  uint8_t new_slot[kLearningJournalMaxBytes];
  size_t old_size = 0;
  size_t new_size = 0;
  assert(encode_learning_journal(passive_runtime_dataset(state), state.config.quality, 20, now_epoch, old_slot,
                                 sizeof(old_slot), old_size) == LearningJournalStatus::OK);
  state.records[1].mean_heat_w += 10.0f;
  assert(encode_learning_journal(passive_runtime_dataset(state), state.config.quality, 21, now_epoch, new_slot,
                                 sizeof(new_slot), new_size) == LearningJournalStatus::OK);
  new_slot[new_size - 1U] ^= 1U;
  auto selection = select_learning_journal_slot({old_slot, old_size}, {new_slot, new_size}, kContext, sizeof(kContext),
                                                now_epoch, state.config.quality);
  assert(selection.status == LearningJournalStatus::OK && selection.selected_slot == 0 &&
         selection.metadata.sequence == 20);

  assert(encode_learning_journal(passive_runtime_dataset(state), state.config.quality, 20, now_epoch, new_slot,
                                 sizeof(new_slot), new_size) == LearningJournalStatus::OK);
  selection = select_learning_journal_slot({old_slot, old_size}, {new_slot, new_size}, kContext, sizeof(kContext),
                                           now_epoch, state.config.quality);
  assert(selection.status == LearningJournalStatus::AMBIGUOUS_SEQUENCE && selection.selected_slot == -1);
}

SegmentRecord daily_record(uint32_t end_epoch_s) {
  auto value = record(end_epoch_s - 86400U, 0.0f);
  value.end_epoch_s = end_epoch_s;
  value.duration_s = 86400U;
  const int16_t profile[] = {-1500, -1200, -800, -100, 100, 700, 1200, 1600};
  memcpy(value.effective_outside_profile_centi, profile, sizeof(profile));
  return value;
}

ThermalModelState learned_thermal_model() {
  ThermalModelState model;
  const auto thermal_config = config().thermal_model;
  assert(initialize_thermal_model(model, thermal_config));
  ThermalInterval interval;
  interval.start_monotonic_ms = 1000;
  interval.end_monotonic_ms = 1801000;
  interval.context_revision = 1;
  interval.complete = interval.inputs_fresh = interval.generations_consistent = true;
  interval.operational_gates_passed = interval.hidden_heat_exclusion_valid = interval.hidden_heat_excluded = true;
  interval.indoor_start_c = 20;
  interval.indoor_end_c = 20.1;
  interval.mean_indoor_c = 20.05;
  interval.mean_outside_c = 5;
  interval.mean_heat_w = 4200;
  assert(observe_thermal_interval(model, interval, thermal_config).accepted);
  return model;
}

void test_schema_five_legacy_layout_and_thermal_survive_upgrade() {
  constexpr uint32_t now = 20000U * 86400U;
  auto legacy = record(now - 4U * 3600U, -4.0f);
  legacy.room_range_k = 0.1f;
  legacy.setpoint_range_c = 0.025f;
  legacy.water_start_c = 31;
  legacy.water_end_c = 30.5f;
  const auto thermal = learned_thermal_model();
  uint8_t bytes[kLearningJournalMaxBytes];
  size_t size = 0;
  assert(encode_learning_journal({&legacy, 1, context()}, config().quality, 3, now, bytes, sizeof(bytes), size,
                                 &thermal, now) == LearningJournalStatus::OK);
  // New encoding preserves every legacy field and the schema-5 thermal tail.
  legacy_schema(bytes, size, 5, 3);
  const auto metadata = inspect_learning_journal({bytes, size}, kContext, sizeof(kContext), now, config().quality);
  assert(metadata.status == LearningJournalStatus::OK && metadata.schema_version == 5);
  LearningJournalRecords view{{bytes, size}, metadata.context_size, metadata.record_count};
  const auto restored = view[0];
  assert(!is_daily_record(restored));
  assert(restored.room_range_k == legacy.room_range_k && restored.setpoint_range_c == legacy.setpoint_range_c);
  assert(restored.water_start_c == legacy.water_start_c && restored.water_end_c == legacy.water_end_c);
  ThermalModelState restored_thermal;
  uint32_t thermal_epoch = 0;
  assert(view.restore_thermal(restored_thermal, config().thermal_model, 500, 7, thermal_epoch));
  assert(thermal_epoch == now && restored_thermal.accepted_samples == thermal.accepted_samples);
  assert(restored_thermal.theta_loss_scaled == thermal.theta_loss_scaled);
  assert(restored_thermal.theta_heat_scaled == thermal.theta_heat_scaled);
  assert(restored_thermal.covariance_00 == thermal.covariance_00);
  assert(restored_thermal.information_11 == thermal.information_11);
  assert(restored_thermal.context_revision == 7 && !restored_thermal.recent_data_valid);

  uint8_t upgraded[kLearningJournalMaxBytes];
  size_t upgraded_size = 0;
  assert(encode_learning_journal({&restored, 1, context()}, config().quality, 4, now, upgraded, sizeof(upgraded),
                                 upgraded_size, &thermal, now) == LearningJournalStatus::OK);
  assert(upgraded_size == size + kLearningJournalDailyBytes && upgraded[4] == kLearningJournalSchemaVersion);
  unchanged_record_values_and_thermal(bytes, upgraded, 1);
}

void test_schema_six_mixed_records_signed_profile_and_thermal_round_trip() {
  constexpr uint32_t now = 20000U * 86400U;
  const SegmentRecord records[] = {record(now - 3U * 86400U, -4.0f), daily_record(now)};
  const auto thermal = learned_thermal_model();
  uint8_t bytes[kLearningJournalMaxBytes];
  size_t size = 0;
  assert(encode_learning_journal({records, 2, context()}, config().quality, 8, now, bytes, sizeof(bytes), size,
                                 &thermal, now) == LearningJournalStatus::OK);
  legacy_schema(bytes, size, 6);
  assert(size == kLearningJournalHeaderBytes + sizeof(kContext) + 2 * 52U + kLearningJournalThermalBytes + 4U);
  const auto metadata = inspect_learning_journal({bytes, size}, kContext, sizeof(kContext), now, config().quality);
  assert(metadata.status == LearningJournalStatus::OK && metadata.schema_version == 6);
  LearningJournalRecords view{{bytes, size}, metadata.context_size, metadata.record_count};
  assert(!is_daily_record(view[0]) && is_daily_record(view[1]));
  assert(view[0].water_start_c == records[0].water_start_c);
  assert(view[1].mean_room_c == records[1].mean_room_c && view[1].mean_heat_w == records[1].mean_heat_w);
  assert(memcmp(view[1].effective_outside_profile_centi, records[1].effective_outside_profile_centi,
                sizeof(records[1].effective_outside_profile_centi)) == 0);
  const size_t profile_offset = kLearningJournalHeaderBytes + sizeof(kContext) + kLearningJournalRecordBytes + 36U;
  assert(bytes[profile_offset] == static_cast<uint8_t>(-1500));
  assert(bytes[profile_offset + 1] == static_cast<uint8_t>(static_cast<uint16_t>(-1500) >> 8U));
  assert(bytes[profile_offset + 14] == static_cast<uint8_t>(1600));
  ThermalModelState restored;
  uint32_t thermal_epoch = 0;
  assert(view.restore_thermal(restored, config().thermal_model, 500, 9, thermal_epoch));
  assert(restored.accepted_samples == thermal.accepted_samples &&
         restored.theta_loss_scaled == thermal.theta_loss_scaled);
  assert(thermal_epoch == now && !restored.recent_data_valid);
  assert(!view.has_daily_checkpoint() && view.daily_checkpoint_epoch() == 0 && view.daily_checkpoint_elapsed_s() == 0);
  SegmentAccumulator absent;
  assert(!view.restore_daily(absent, context(), config().quality));
  uint8_t upgraded[kLearningJournalMaxBytes];
  size_t upgraded_size = 0;
  assert(encode_learning_journal({records, 2, context()}, config().quality, 9, now, upgraded, sizeof(upgraded),
                                 upgraded_size, &thermal, now) == LearningJournalStatus::OK);
  assert(upgraded_size == size + kLearningJournalDailyBytes && upgraded[4] == kLearningJournalSchemaVersion);
  unchanged_record_values_and_thermal(bytes, upgraded, 2);

  uint8_t changed[kLearningJournalMaxBytes];
  memcpy(changed, bytes, size);
  changed[profile_offset] ^= 1;
  assert(inspect_learning_journal({changed, size}, kContext, sizeof(kContext), now, config().quality).status ==
         LearningJournalStatus::CORRUPT);
  memcpy(changed, bytes, size);
  write_u16(changed, profile_offset, 0x8000U);
  repair_crc(changed, size);
  assert(inspect_learning_journal({changed, size}, kContext, sizeof(kContext), now, config().quality).status ==
         LearningJournalStatus::INVALID_RECORD);
  memcpy(changed, bytes, size);
  write_u16(changed, profile_offset, 1600U);  // Unsorted profile, still within the temperature limits.
  repair_crc(changed, size);
  assert(inspect_learning_journal({changed, size}, kContext, sizeof(kContext), now, config().quality).status ==
         LearningJournalStatus::INVALID_RECORD);
  // A schema-5 header cannot reinterpret a schema-6 daily profile as a legacy record.
  memcpy(changed, bytes, size);
  write_u16(changed, 4, 5);
  write_u16(changed, 20, 3);
  repair_crc(changed, size);
  assert(inspect_learning_journal({changed, size}, kContext, sizeof(kContext), now, config().quality).status ==
         LearningJournalStatus::INVALID_RECORD);
}

void test_schema_four_records_migrate_without_thermal_state() {
  constexpr uint32_t now = 20000U * 86400U + 12U * 3600U;
  auto state = populated(now);
  uint8_t bytes[kLearningJournalMaxBytes];
  size_t size = 0;
  assert(encode_learning_journal(passive_runtime_dataset(state), state.config.quality, 1, now, bytes, sizeof(bytes),
                                 size) == LearningJournalStatus::OK);
  // Schema4 has the earlier record layout and no thermal or checkpoint tail.
  legacy_schema(bytes, size, 4, 2);
  constexpr uint8_t new_source[] = {9};
  const auto metadata =
      inspect_learning_journal({bytes, size}, new_source, sizeof(new_source), now, state.config.quality);
  assert(metadata.status == LearningJournalStatus::OK && metadata.record_count == 2);
  LearningJournalRecords view{{bytes, size}, metadata.context_size, metadata.record_count};
  assert(view[0].mean_heat_w == state.records[0].mean_heat_w);
  ThermalModelState thermal;
  uint32_t epoch = 0;
  assert(!view.restore_thermal(thermal, ThermalModelConfig{}, 100, 1, epoch));
}

void test_daily_checkpoint_round_trip_requires_exact_context_and_remaps_clocks() {
  constexpr uint32_t now = 20000U * 86400U + 4200U;
  auto completed = record(now - 4200U - 4U * 3600U, -5);
  auto daily = daily_checkpoint(now - 4200U);
  daily.missing_energy_uncertainty_ws = 4567.5;
  const auto thermal = learned_thermal_model();
  uint8_t bytes[kLearningJournalMaxBytes];
  size_t size = 0;
  assert(encode_learning_journal({&completed, 1, context()}, config().quality, 2, now, bytes, sizeof(bytes), size,
                                 &thermal, now, &daily) == LearningJournalStatus::OK);
  const auto metadata = inspect_learning_journal({bytes, size}, kContext, sizeof(kContext), now, config().quality);
  assert(metadata.status == LearningJournalStatus::OK && metadata.schema_version == kLearningJournalSchemaVersion);
  LearningJournalRecords view{{bytes, size}, metadata.context_size, metadata.record_count};
  assert(view.has_daily_checkpoint() && view.daily_checkpoint_epoch() == now);
  SegmentAccumulator restored;
  assert(view.restore_daily(restored, context(7), config().quality));
  assert(restored.active && restored.restart_pending && restored.source_gap_pending);
  assert(restored.context_revision == 7 && restored.carried_duration_ms == 4200000);
  assert(restored.start_monotonic_ms == 1 && restored.last_monotonic_ms == 1 && restored.last_gap_observation_ms == 1);
  assert(restored.start_epoch_s == daily.start_epoch_s && restored.last_epoch_s == daily.last_epoch_s);
  assert(restored.room_start_c == daily.room_start_c && restored.last_room_c == daily.last_room_c);
  assert(restored.last_setpoint_c == daily.last_setpoint_c && restored.last_outside_c == daily.last_outside_c);
  assert(restored.last_heat_w == daily.last_heat_w && restored.water_start_c == daily.water_start_c);
  assert(restored.water_end_c == daily.water_end_c && restored.room_min_c == daily.room_min_c);
  assert(restored.room_max_c == daily.room_max_c && restored.setpoint_min_c == daily.setpoint_min_c);
  assert(restored.setpoint_max_c == daily.setpoint_max_c &&
         restored.integrated_duration_s == daily.integrated_duration_s);
  assert(restored.missing_energy_uncertainty_ws == daily.missing_energy_uncertainty_ws);
  assert(restored.room_integral == daily.room_integral && restored.setpoint_integral == daily.setpoint_integral);
  assert(restored.outside_integral == daily.outside_integral && restored.heat_integral == daily.heat_integral);
  assert(restored.hour_effective_integral == daily.hour_effective_integral && restored.trend_w == daily.trend_w);
  assert(restored.trend_wt == daily.trend_wt && restored.trend_wtt == daily.trend_wtt);
  assert(restored.trend_wr == daily.trend_wr && restored.trend_wtr == daily.trend_wtr);
  assert(memcmp(restored.hourly_effective_outside_c, daily.hourly_effective_outside_c,
                sizeof(daily.hourly_effective_outside_c)) == 0);
  constexpr uint8_t changed[] = {9, 8, 7, 6, 5, 4, 3, 2, 0};
  assert(inspect_learning_journal({bytes, size}, changed, sizeof(changed), now, config().quality).status ==
         LearningJournalStatus::OK);
  assert(!view.restore_daily(restored, {changed, sizeof(changed), 9}, config().quality));
  assert(!view.restore_daily(restored, {kContext, sizeof(kContext) - 1U, 9}, config().quality));
  assert(restored.context_revision == 7);  // Failure leaves the consumer unchanged.
  assert(view[0].mean_heat_w == completed.mean_heat_w);
  ThermalModelState restored_thermal;
  uint32_t thermal_epoch = 0;
  assert(view.restore_thermal(restored_thermal, config().thermal_model, 500, 9, thermal_epoch));
  assert(restored_thermal.accepted_samples == thermal.accepted_samples && thermal_epoch == now);
}

void test_max_capacity_checkpoint_fits_slot_and_old_layouts_cannot_read_new_tail() {
  constexpr uint32_t now = 20000U * 86400U + 4200U;
  uint8_t max_context[kMaxPassiveContextBytes]{};
  SegmentRecord records[kMaxSegmentRecords];
  for (size_t i = 0; i < kMaxSegmentRecords; ++i)
    records[i] = daily_record(now - 4200U - static_cast<uint32_t>(kMaxSegmentRecords - i - 1U) * 86400U);
  const auto daily = daily_checkpoint(now - 4200U);
  const auto thermal = learned_thermal_model();
  uint8_t bytes[8192];
  size_t size = 0;
  assert(encode_learning_journal({records, kMaxSegmentRecords, {max_context, sizeof(max_context), 1}}, config().quality,
                                 1, now, bytes, sizeof(bytes), size, &thermal, now,
                                 &daily) == LearningJournalStatus::OK);
  assert(size == kLearningJournalMaxBytes && size == 8168U);
  const auto metadata =
      inspect_learning_journal({bytes, size}, max_context, sizeof(max_context), now, config().quality);
  assert(metadata.status == LearningJournalStatus::OK && metadata.record_count == kMaxSegmentRecords);
  LearningJournalRecords view{{bytes, size}, metadata.context_size, metadata.record_count};
  SegmentAccumulator restored;
  assert(view.restore_daily(restored, {max_context, sizeof(max_context), 1}, config().quality));
  assert(restored.heat_integral == daily.heat_integral);
  // Earlier schema length contracts reject the unconverted new tail.
  for (uint16_t old_schema : {uint16_t{4}, uint16_t{5}, uint16_t{6}, uint16_t{7}, uint16_t{8}}) {
    write_u16(bytes, 4, old_schema);
    repair_crc(bytes, size);
    assert(inspect_learning_journal({bytes, size}, max_context, sizeof(max_context), now, config().quality).status ==
           LearningJournalStatus::INVALID_LENGTH);
  }
}

void test_invalid_day_never_encodes_as_valid_or_overlaps_completed_records() {
  constexpr uint32_t now = 20000U * 86400U + 4200U;
  auto completed = record(now - 4200U - 4U * 3600U, -5);
  auto daily = daily_checkpoint(now - 4200U);
  uint8_t bytes[kLearningJournalMaxBytes];
  size_t size = 123;
  auto encode = [&](const SegmentAccumulator& candidate, uint32_t created) {
    return encode_learning_journal({&completed, 1, context()}, config().quality, 1, created, bytes, sizeof(bytes), size,
                                   nullptr, 0, &candidate);
  };
  assert(encode(daily, now - 1U) == LearningJournalStatus::INVALID_ARGUMENT && size == 0);
  auto invalid = daily;
  invalid.context_revision = 2;
  assert(encode(invalid, now) == LearningJournalStatus::INVALID_ARGUMENT && size == 0);
  invalid = daily;
  invalid.heat_integral = NAN;
  assert(encode(invalid, now) == LearningJournalStatus::INVALID_ARGUMENT && size == 0);
  invalid = daily;
  --invalid.start_epoch_s;
  assert(encode(invalid, now) == LearningJournalStatus::INVALID_RECORD && size == 0);
  assert(encode(daily, now) == LearningJournalStatus::OK);
  const size_t daily_offset = size - kLearningJournalCrcBytes - kLearningJournalDailyBytes;
  write_u32(bytes, daily_offset, 2);  // Only 0/1 are valid serialized active flags.
  repair_crc(bytes, size);
  assert(inspect_learning_journal({bytes, size}, kContext, sizeof(kContext), now, config().quality).status ==
         LearningJournalStatus::INVALID_RECORD);
  SegmentAccumulator inactive;
  assert(encode(inactive, now) == LearningJournalStatus::OK);
  for (size_t i = daily_offset; i < daily_offset + kLearningJournalDailyBytes; ++i) assert(bytes[i] == 0);
  bytes[daily_offset + 4U] = 1;  // Inactive tails must be entirely zero.
  repair_crc(bytes, size);
  assert(inspect_learning_journal({bytes, size}, kContext, sizeof(kContext), now, config().quality).status ==
         LearningJournalStatus::INVALID_RECORD);
}

void test_current_quality_can_reject_day_without_discarding_records_or_thermal() {
  constexpr uint32_t now = 20000U * 86400U + 4200U;
  const auto completed = record(now - 4200U - 4U * 3600U, -5);
  auto daily = daily_checkpoint(now - 4200U);
  daily.last_heat_w = 5000;  // A valid endpoint under the original configuration.
  const auto thermal = learned_thermal_model();
  uint8_t bytes[kLearningJournalMaxBytes];
  size_t size = 0;
  assert(encode_learning_journal({&completed, 1, context()}, config().quality, 2, now, bytes, sizeof(bytes), size,
                                 &thermal, now, &daily) == LearningJournalStatus::OK);
  auto strict = config().quality;
  strict.max_abs_heat_w = 4500;
  const auto metadata = inspect_learning_journal({bytes, size}, kContext, sizeof(kContext), now, strict);
  assert(metadata.status == LearningJournalStatus::OK);
  LearningJournalRecords view{{bytes, size}, metadata.context_size, metadata.record_count};
  SegmentAccumulator restored;
  assert(!view.restore_daily(restored, context(), strict));
  assert(view.has_daily_checkpoint() && view.daily_checkpoint_start_epoch() == daily.start_epoch_s);
  assert(view[0].mean_heat_w == completed.mean_heat_w);
  ThermalModelState restored_thermal;
  uint32_t thermal_epoch = 0;
  assert(view.restore_thermal(restored_thermal, config().thermal_model, 500, 1, thermal_epoch));
  assert(restored_thermal.accepted_samples == thermal.accepted_samples && thermal_epoch == now);
  constexpr uint8_t changed[] = {1};
  assert(inspect_learning_journal({bytes, size}, changed, sizeof(changed), now, strict).status ==
         LearningJournalStatus::OK);
  assert(!view.restore_daily(restored, {changed, sizeof(changed), 1}, strict));
}

void test_daily_checkpoint_elapsed_reads_saved_progress_and_bounds_conversion() {
  constexpr uint32_t now = 20000U * 86400U + 3600U;
  const auto daily = daily_checkpoint(now - 3600U, 3600U);
  uint8_t bytes[kLearningJournalMaxBytes];
  size_t size = 0;
  assert(encode_learning_journal({nullptr, 0, context()}, config().quality, 1, now, bytes, sizeof(bytes), size, nullptr,
                                 0, &daily) == LearningJournalStatus::OK);
  const auto metadata = inspect_learning_journal({bytes, size}, kContext, sizeof(kContext), now, config().quality);
  assert(metadata.status == LearningJournalStatus::OK);
  LearningJournalRecords view{{bytes, size}, metadata.context_size, metadata.record_count};
  assert(view.daily_checkpoint_elapsed_s() == 3600U);
  const size_t duration_offset = size - kLearningJournalCrcBytes - kLearningJournalDailyBytes + 68U;
  for (double invalid : {-1.0, 86400.0, static_cast<double>(NAN), static_cast<double>(INFINITY)}) {
    learning_journal_detail::Writer writer{bytes, size};
    writer.position = duration_offset;
    learning_journal_detail::write_double(writer, invalid);
    assert(view.daily_checkpoint_elapsed_s() == 0);
  }
  learning_journal_detail::Writer writer{bytes, size};
  writer.position = duration_offset;
  learning_journal_detail::write_double(writer, 86399.999);
  assert(view.daily_checkpoint_elapsed_s() == 86399U);
  assert(encode_learning_journal({nullptr, 0, context()}, config().quality, 1, now, bytes, sizeof(bytes), size) ==
         LearningJournalStatus::OK);
  assert(view.daily_checkpoint_elapsed_s() == 0);
}

void test_daily_restore_diagnostics_preserve_output_and_history() {
  constexpr uint32_t now = 20000U * 86400U + 4200U;
  const auto completed = record(now - 4200U - 4U * 3600U, -5);
  const auto daily = daily_checkpoint(now - 4200U);
  uint8_t bytes[kLearningJournalMaxBytes];
  size_t size = 0;
  assert(encode_learning_journal({&completed, 1, context()}, config().quality, 2, now, bytes, sizeof(bytes), size,
                                 nullptr, 0, &daily) == LearningJournalStatus::OK);
  LearningJournalRecords view{{bytes, size}, sizeof(kContext), 1};
  SegmentAccumulator output = daily;
  // Rejection must never partially replace the caller's live accumulator.
  uint8_t unchanged[sizeof(output)];
  memcpy(unchanged, &output, sizeof(output));
  DailyCheckpointRestoreStatus status = DailyCheckpointRestoreStatus::OK;
  auto reject = [&](PassiveContextView expected, const QualityConfig& quality, DailyCheckpointRestoreStatus reason) {
    assert(!view.restore_daily(output, expected, quality, &status));
    assert(status == reason);
    assert(memcmp(&output, unchanged, sizeof(output)) == 0);
    assert(view[0].mean_heat_w == completed.mean_heat_w);
    assert(strcmp(daily_checkpoint_restore_reason(status), "none") != 0);
  };
  reject({nullptr, 0, 1}, config().quality, DailyCheckpointRestoreStatus::CONTEXT_UNAVAILABLE);
  reject(context(0), config().quality, DailyCheckpointRestoreStatus::CONTEXT_UNAVAILABLE);
  constexpr uint8_t changed[] = {1};
  reject({changed, sizeof(changed), 1}, config().quality, DailyCheckpointRestoreStatus::CONTEXT_MISMATCH);
  auto strict = config().quality;
  strict.max_abs_heat_w = 4100;
  reject(context(), strict, DailyCheckpointRestoreStatus::QUALITY_INVALID);
  // Deliberately fault-inject into the otherwise immutable validated view to
  // exercise defensive decode failure; ordinary owners may not mutate it.
  const size_t duration_offset = size - kLearningJournalCrcBytes - kLearningJournalDailyBytes + 68U;
  learning_journal_detail::Writer writer{bytes, size};
  writer.position = duration_offset;
  learning_journal_detail::write_double(writer, NAN);
  reject(context(), config().quality, DailyCheckpointRestoreStatus::DECODE_FAILED);
  writer.position = duration_offset;
  learning_journal_detail::write_double(writer, daily.integrated_duration_s);
  assert(view.restore_daily(output, context(7), config().quality, &status));
  assert(status == DailyCheckpointRestoreStatus::OK && output.context_revision == 7);
  assert(strcmp(daily_checkpoint_restore_reason(status), "none") == 0);
  // No sums or saved hours change on a successful round trip, only local clocks.
  auto expected = daily;
  expected.context_revision = 7;
  expected.heat_gap_pending = true;
  uint8_t expected_bytes[kLearningJournalDailyBytes], actual_bytes[kLearningJournalDailyBytes];
  learning_journal_detail::Writer expected_writer{expected_bytes, sizeof(expected_bytes)};
  learning_journal_detail::Writer actual_writer{actual_bytes, sizeof(actual_bytes)};
  learning_journal_detail::write_daily(expected_writer, &expected);
  learning_journal_detail::write_daily(actual_writer, &output);
  assert(expected_writer.ok && actual_writer.ok && memcmp(expected_bytes, actual_bytes, sizeof(actual_bytes)) == 0);
  assert(segment_elapsed_ms(output) == 4200000ULL);
  assert(encode_learning_journal({&completed, 1, context()}, config().quality, 3, now, bytes, sizeof(bytes), size) ==
         LearningJournalStatus::OK);
  memcpy(&output, unchanged, sizeof(output));
  reject(context(), config().quality, DailyCheckpointRestoreStatus::MISSING);
}

void test_current_floor_and_schema_seven_backward_reading() {
  constexpr uint32_t now = 20000U * 86400U + 4200U;
  const auto completed = record(now - 4200U - 4U * 3600U, -5);
  auto daily = daily_checkpoint(now - 4200U);
  daily.missing_energy_uncertainty_ws = 4567.5;
  daily.gap_heat_bound_floor_w = 20000.0f;
  auto q = config().quality;
  q.max_daily_missing_heat_w = 6115.28f;
  const auto thermal = learned_thermal_model();
  uint8_t current[kLearningJournalMaxBytes], legacy[kLearningJournalMaxBytes];
  size_t size = 0;
  assert(encode_learning_journal({&completed, 1, context()}, q, 9, now, current, sizeof(current), size, &thermal, now,
                                 &daily) == LearningJournalStatus::OK);
  const auto metadata = inspect_learning_journal({current, size}, kContext, sizeof(kContext), now, q);
  assert(metadata.status == LearningJournalStatus::OK && metadata.schema_version == kLearningJournalSchemaVersion);
  LearningJournalRecords view{{current, size}, sizeof(kContext), 1};
  SegmentAccumulator restored;
  assert(view.restore_daily(restored, context(), q));
  assert(restored.gap_heat_bound_floor_w == 20000.0f);
  assert(restored.missing_energy_uncertainty_ws == daily.missing_energy_uncertainty_ws);
  size_t old_size = size;
  std::memcpy(legacy, current, size);
  legacy_schema_seven(legacy, old_size);
  const auto old_metadata = inspect_learning_journal({legacy, old_size}, kContext, sizeof(kContext), now, q);
  assert(old_metadata.status == LearningJournalStatus::OK && old_metadata.schema_version == 7);
  LearningJournalRecords old{{legacy, old_size}, sizeof(kContext), 1};
  assert(old.has_daily_checkpoint() && old.daily_checkpoint_epoch() == now);
  assert(old.restore_daily(restored, context(), q));
  assert(restored.gap_heat_bound_floor_w == q.max_abs_heat_w);
  assert(restored.missing_energy_uncertainty_ws == daily.missing_energy_uncertainty_ws);
  assert(old[0].mean_heat_w == completed.mean_heat_w && old[0].start_epoch_s == completed.start_epoch_s);
  ThermalModelState restored_thermal;
  uint32_t thermal_epoch = 0;
  assert(old.restore_thermal(restored_thermal, config().thermal_model, 1000, 7, thermal_epoch));
  assert(restored_thermal.accepted_samples == thermal.accepted_samples);
  assert(restored_thermal.theta_loss_scaled == thermal.theta_loss_scaled);
  // New metadata corruption must not be reinterpreted as a known zero peak.
  const size_t floor_offset =
      size - kLearningJournalCrcBytes - kLearningJournalDailyBytes + kLearningJournalDailyV7Bytes;
  for (const float bad : {0.0f, -1.0f, NAN, INFINITY, 100001.0f}) {
    uint8_t changed[kLearningJournalMaxBytes];
    std::memcpy(changed, current, size);
    learning_journal_detail::Writer writer{changed, size};
    writer.position = floor_offset;
    learning_journal_detail::write_float(writer, bad);
    repair_crc(changed, size);
    assert(inspect_learning_journal({changed, size}, kContext, sizeof(kContext), now, q).status ==
           LearningJournalStatus::INVALID_RECORD);
    assert(select_learning_journal_slot({changed, size}, {legacy, old_size}, kContext, sizeof(kContext), now, q)
               .selected_slot == 1);
  }
  auto strict = q;
  strict.max_abs_heat_w = 10000.0f;
  // A current consumer limit may reject the unfinished day, not its history/RLS.
  assert(inspect_learning_journal({current, size}, kContext, sizeof(kContext), now, strict).status ==
         LearningJournalStatus::OK);
  assert(!view.restore_daily(restored, context(), strict));
}

void test_schema_nine_record_layout_and_canonical_quality_codes() {
  constexpr uint32_t now = 20000U * 86400U;
  auto value = daily_record(now);
  value.context_revision = 17;
  for (uint16_t coverage = 0; coverage <= kDailyCoverageScale; ++coverage) {
    value.daily_quality = {coverage, static_cast<uint16_t>(kDailyCoverageScale - coverage), coverage};
    uint8_t bytes[kLearningJournalRecordBytes], again[kLearningJournalRecordBytes];
    learning_journal_detail::Writer writer{bytes, sizeof(bytes)};
    learning_journal_detail::write_record(writer, value);
    assert(writer.ok && writer.position == 52U);
    assert(load_u32(bytes, 0) == value.start_epoch_s && load_u32(bytes, 4) == value.end_epoch_s);
    assert(load_u16(bytes, 8) == 0 && load_u16(bytes, 10) == coverage);
    assert(load_u16(bytes, 12) == kDailyCoverageScale - coverage && load_u16(bytes, 14) == coverage);
    learning_journal_detail::Reader reader{bytes, sizeof(bytes)};
    const auto decoded = learning_journal_detail::read_record(reader, 9, 17);
    assert(reader.ok && reader.position == 52U && decoded.duration_s == 86400U && decoded.context_revision == 17);
    assert(decoded.daily_quality.heat_coverage_bp == coverage);
    assert(decoded.daily_quality.temperature_coverage_bp == kDailyCoverageScale - coverage);
    assert(decoded.daily_quality.heat_uncertainty_mean_w == coverage);
    learning_journal_detail::Writer second{again, sizeof(again)};
    learning_journal_detail::write_record(second, decoded);
    assert(second.ok && memcmp(bytes, again, sizeof(bytes)) == 0);
  }
  for (uint16_t uncertainty :
       {uint16_t{0}, uint16_t{1}, uint16_t{65533}, kDailyUncertaintyOverflow, kDailyQualityUnknown}) {
    value.daily_quality = {kDailyQualityUnknown, kDailyQualityUnknown, uncertainty};
    uint8_t bytes[kLearningJournalRecordBytes], again[kLearningJournalRecordBytes];
    learning_journal_detail::Writer writer{bytes, sizeof(bytes)};
    learning_journal_detail::write_record(writer, value);
    learning_journal_detail::Reader reader{bytes, sizeof(bytes)};
    const auto decoded = learning_journal_detail::read_record(reader, 9, 23);
    assert(decoded.context_revision == 23);  // No stale per-record revision survives.
    assert(decoded.daily_quality.heat_coverage_bp == kDailyQualityUnknown);
    assert(decoded.daily_quality.temperature_coverage_bp == kDailyQualityUnknown);
    assert(decoded.daily_quality.heat_uncertainty_mean_w == uncertainty);
    learning_journal_detail::Writer second{again, sizeof(again)};
    learning_journal_detail::write_record(second, decoded);
    assert(writer.ok && reader.ok && second.ok && memcmp(bytes, again, sizeof(bytes)) == 0);
  }
  for (uint32_t duration = 14400; duration <= 15000; ++duration) {
    auto legacy = record(now - duration, -4);
    legacy.duration_s = duration;
    legacy.end_epoch_s = now;
    uint8_t bytes[kLearningJournalRecordBytes];
    learning_journal_detail::Writer writer{bytes, sizeof(bytes)};
    learning_journal_detail::write_record(writer, legacy);
    learning_journal_detail::Reader reader{bytes, sizeof(bytes)};
    const auto decoded = learning_journal_detail::read_record(reader, 9, 1);
    assert(writer.ok && reader.ok && load_u16(bytes, 8) == duration && decoded.duration_s == duration);
    assert(decoded.start_epoch_s == legacy.start_epoch_s && decoded.end_epoch_s == legacy.end_epoch_s);
    assert(decoded.water_start_c == legacy.water_start_c && decoded.water_end_c == legacy.water_end_c);
  }
}

void test_schema_nine_rejects_invalid_codes_and_retains_nonfit_days() {
  constexpr uint32_t now = 20000U * 86400U;
  auto value = daily_record(now);
  value.room_trend_k_per_h = 1.5f;
  value.daily_quality = {9000, 8000, kDailyUncertaintyOverflow};
  assert(validate_segment_record_structure(value, config().quality) == LearningStatus::OK);
  assert(validate_segment_record(value, config().quality) != LearningStatus::OK);
  uint8_t bytes[kLearningJournalMaxBytes];
  size_t size = 0;
  assert(encode_learning_journal({&value, 1, context()}, config().quality, 1, now, bytes, sizeof(bytes), size) ==
         LearningJournalStatus::OK);
  assert(inspect_learning_journal({bytes, size}, kContext, sizeof(kContext), now, config().quality).status ==
         LearningJournalStatus::OK);
  LearningJournalRecords view{{bytes, size}, sizeof(kContext), 1};
  assert(view[0].daily_quality.heat_uncertainty_mean_w == kDailyUncertaintyOverflow);
  const size_t record_offset = kLearningJournalHeaderBytes + sizeof(kContext);
  for (size_t offset : {10U, 12U}) {
    for (uint16_t bad : {uint16_t{10001}, uint16_t{65534}}) {
      uint8_t changed[kLearningJournalMaxBytes];
      memcpy(changed, bytes, size);
      write_u16(changed, record_offset + offset, bad);
      repair_crc(changed, size);
      assert(inspect_learning_journal({changed, size}, kContext, sizeof(kContext), now, config().quality).status ==
             LearningJournalStatus::INVALID_RECORD);
    }
  }
  for (uint16_t bad_duration : {uint16_t{1}, uint16_t{14399}, uint16_t{15001}, uint16_t{65535}}) {
    uint8_t changed[kLearningJournalMaxBytes];
    memcpy(changed, bytes, size);
    write_u16(changed, record_offset + 8U, bad_duration);
    repair_crc(changed, size);
    assert(inspect_learning_journal({changed, size}, kContext, sizeof(kContext), now, config().quality).status ==
           LearningJournalStatus::INVALID_RECORD);
  }
  value.daily_quality.heat_coverage_bp = 10001;
  assert(encode_learning_journal({&value, 1, context()}, config().quality, 1, now, bytes, sizeof(bytes), size) ==
         LearningJournalStatus::INVALID_RECORD);
  value.daily_quality.heat_coverage_bp = 9000;
  value.context_revision = 2;
  assert(encode_learning_journal({&value, 1, context()}, config().quality, 1, now, bytes, sizeof(bytes), size) ==
         LearningJournalStatus::INVALID_RECORD);
}

void test_every_legacy_schema_preserves_values_thermal_debt_and_unknown_coverage() {
  constexpr uint32_t now = 20000U * 86400U + 4200U;
  const auto thermal = learned_thermal_model();
  auto quality = config().quality;
  quality.max_daily_missing_heat_w = 6115.28f;
  for (uint16_t schema : {uint16_t{4}, uint16_t{5}, uint16_t{6}, uint16_t{7}, uint16_t{8}}) {
    SegmentRecord values[]{record(now - 4200U - 3U * 86400U, -4),
                           schema >= 6 ? daily_record(now - 4200U) : record(now - 4200U - 14400U, 5)};
    for (auto& value : values) value.daily_quality = {7000, 6000, 123};
    auto daily = daily_checkpoint(now - 4200U);
    daily.missing_energy_uncertainty_ws = 289.578 * 3600.0;
    daily.gap_heat_bound_floor_w = 20000.0f;
    uint8_t bytes[kLearningJournalMaxBytes];
    size_t size = 0;
    assert(encode_learning_journal({values, 2, context()}, quality, 7, now, bytes, sizeof(bytes), size, &thermal, now,
                                   &daily) == LearningJournalStatus::OK);
    legacy_schema(bytes, size, schema);
    const auto metadata = inspect_learning_journal({bytes, size}, kContext, sizeof(kContext), now, quality);
    assert(metadata.status == LearningJournalStatus::OK && metadata.schema_version == schema);
    uint8_t wrong_revision[kLearningJournalMaxBytes];
    memcpy(wrong_revision, bytes, size);
    write_u32(wrong_revision, 28, 2);
    repair_crc(wrong_revision, size);
    assert(inspect_learning_journal({wrong_revision, size}, kContext, sizeof(kContext), now, quality).status ==
           LearningJournalStatus::INVALID_RECORD);  // Old per-record revisions must not be overwritten.
    LearningJournalRecords view{{bytes, size}, sizeof(kContext), 2};
    SegmentRecord restored_records[]{view[0], view[1]};
    for (size_t index = 0; index < 2; ++index) {
      const auto& decoded = restored_records[index];
      assert(decoded.start_epoch_s == values[index].start_epoch_s && decoded.end_epoch_s == values[index].end_epoch_s);
      assert(decoded.duration_s == values[index].duration_s &&
             decoded.context_revision == values[index].context_revision);
      assert(decoded.daily_quality.heat_coverage_bp == kDailyQualityUnknown);
      assert(decoded.daily_quality.temperature_coverage_bp == kDailyQualityUnknown);
      assert(decoded.daily_quality.heat_uncertainty_mean_w == kDailyQualityUnknown);
    }
    ThermalModelState restored_thermal;
    uint32_t thermal_epoch = 0;
    assert(view.restore_thermal(restored_thermal, config().thermal_model, 500, 1, thermal_epoch) == (schema >= 5));
    if (schema >= 5) assert(restored_thermal.accepted_samples == thermal.accepted_samples);
    SegmentAccumulator restored_daily;
    assert(view.restore_daily(restored_daily, context(), quality) == (schema >= 7));
    if (schema >= 7) {
      assert(restored_daily.missing_energy_uncertainty_ws == daily.missing_energy_uncertainty_ws);
      assert(restored_daily.heat_last_w == daily.last_heat_w);
      assert(restored_daily.heat_last_epoch_s == daily.last_epoch_s);
      assert(restored_daily.heat_last_elapsed_ms == 4200000U);
      assert(restored_daily.heat_observed_ms == 0 && restored_daily.temperature_observed_ms == 0);
      assert(restored_daily.next_heat_observed_ms == 0 && restored_daily.next_heat_integral_ws == 0.0);
      assert(restored_daily.next_heat_uncertainty_ws == 0.0 && !restored_daily.daily_quality_known);
      assert(restored_daily.heat_gap_pending);
      assert(restored_daily.gap_heat_bound_floor_w == (schema == 7 ? 50000.0f : 20000.0f));
    }
    uint8_t upgraded[kLearningJournalMaxBytes];
    size_t upgraded_size = 0;
    assert(encode_learning_journal({restored_records, 2, context()}, quality, 8, now, upgraded, sizeof(upgraded),
                                   upgraded_size, schema >= 5 ? &restored_thermal : nullptr, thermal_epoch,
                                   schema >= 7 ? &restored_daily : nullptr) == LearningJournalStatus::OK);
    assert(upgraded[4] == 9 && upgraded_size > size);
    if (schema >= 5) unchanged_record_values_and_thermal(bytes, upgraded, 2);
    assert(inspect_learning_journal({upgraded, upgraded_size}, kContext, sizeof(kContext), now, quality).status ==
           LearningJournalStatus::OK);
  }
}

void test_schema_nine_checkpoint_preserves_independent_heat_cursor_and_flags() {
  constexpr uint32_t start = 20000U * 86400U;
  auto daily = daily_checkpoint(start, 86340U);
  daily.heat_last_w = 3000.0f;
  daily.heat_last_elapsed_ms = 86460000U;
  daily.heat_last_epoch_s = start + 86460U;
  daily.heat_observed_ms = 86400000U;
  daily.temperature_observed_ms = 86340000U;
  daily.next_heat_observed_ms = 60000U;
  daily.next_heat_integral_ws = 180000.0;
  daily.next_heat_uncertainty_ws = 1234.5;
  daily.heat_gap_pending = false;
  daily.daily_quality_known = true;
  assert(valid_daily_checkpoint(daily, config().quality));
  uint8_t bytes[kLearningJournalMaxBytes];
  size_t size = 0;
  assert(encode_learning_journal({nullptr, 0, context()}, config().quality, 1, start + 86459U, bytes, sizeof(bytes),
                                 size, nullptr, 0, &daily) == LearningJournalStatus::INVALID_ARGUMENT);
  assert(encode_learning_journal({nullptr, 0, context()}, config().quality, 1, start + 86460U, bytes, sizeof(bytes),
                                 size, nullptr, 0, &daily) == LearningJournalStatus::OK);
  const size_t daily_offset = size - kLearningJournalCrcBytes - kLearningJournalDailyBytes;
  const size_t flags_offset = size - kLearningJournalCrcBytes - sizeof(uint32_t);
  assert(load_u32(bytes, flags_offset) == 2U && kLearningJournalDailyBytes == 300U);
  learning_journal_detail::Reader reader{bytes, size};
  reader.position = daily_offset;
  SegmentAccumulator decoded;
  assert(learning_journal_detail::read_daily(reader, decoded, 9));
  assert(reader.position == size - kLearningJournalCrcBytes && !decoded.heat_gap_pending);
  LearningJournalRecords view{{bytes, size}, sizeof(kContext), 0};
  assert(view.restore_daily(decoded, context(), config().quality));
  assert(decoded.heat_gap_pending && decoded.daily_quality_known);
  assert(decoded.heat_last_w == daily.heat_last_w && decoded.heat_last_elapsed_ms == daily.heat_last_elapsed_ms);
  assert(decoded.heat_last_epoch_s == daily.heat_last_epoch_s && decoded.heat_observed_ms == daily.heat_observed_ms);
  assert(decoded.temperature_observed_ms == daily.temperature_observed_ms);
  assert(decoded.next_heat_observed_ms == daily.next_heat_observed_ms);
  assert(decoded.next_heat_integral_ws == daily.next_heat_integral_ws);
  assert(decoded.next_heat_uncertainty_ws == daily.next_heat_uncertainty_ws);
  write_u32(bytes, 16, start + 86459U);
  repair_crc(bytes, size);
  assert(inspect_learning_journal({bytes, size}, kContext, sizeof(kContext), start + 86460U, config().quality).status ==
         LearningJournalStatus::INVALID_RECORD);
  write_u32(bytes, 16, start + 86460U);
  repair_crc(bytes, size);
  bytes[daily_offset + kLearningJournalDailyV8Bytes] ^= 1U;
  assert(inspect_learning_journal({bytes, size}, kContext, sizeof(kContext), start + 86460U, config().quality).status ==
         LearningJournalStatus::CORRUPT);
  bytes[daily_offset + kLearningJournalDailyV8Bytes] ^= 1U;
  for (uint32_t bad_flags : {4U, 8U, UINT32_MAX}) {
    write_u32(bytes, flags_offset, bad_flags);
    repair_crc(bytes, size);
    assert(
        inspect_learning_journal({bytes, size}, kContext, sizeof(kContext), start + 86460U, config().quality).status ==
        LearningJournalStatus::INVALID_RECORD);
  }
}

}  // namespace

int main() {
  test_schema_nine_record_layout_and_canonical_quality_codes();
  test_schema_nine_rejects_invalid_codes_and_retains_nonfit_days();
  test_every_legacy_schema_preserves_values_thermal_debt_and_unknown_coverage();
  test_schema_nine_checkpoint_preserves_independent_heat_cursor_and_flags();
  test_current_floor_and_schema_seven_backward_reading();
  test_daily_restore_diagnostics_preserve_output_and_history();
  test_daily_checkpoint_elapsed_reads_saved_progress_and_bounds_conversion();
  test_current_quality_can_reject_day_without_discarding_records_or_thermal();
  test_daily_checkpoint_round_trip_requires_exact_context_and_remaps_clocks();
  test_max_capacity_checkpoint_fits_slot_and_old_layouts_cannot_read_new_tail();
  test_invalid_day_never_encodes_as_valid_or_overlaps_completed_records();
  test_schema_five_legacy_layout_and_thermal_survive_upgrade();
  test_schema_six_mixed_records_signed_profile_and_thermal_round_trip();
  test_previous_64_record_capacity_restores_without_reset();
  test_schema_four_records_migrate_without_thermal_state();
  test_round_trip_and_reboot_remap_never_restores_readiness();
  test_sparse_season_round_trip_and_reboot_remap();
  test_previous_algorithm_journal_restores_as_data_for_refit();
  test_corrupt_truncated_schema_count_context_and_record_fail_closed();
  test_two_slot_selection_survives_torn_new_write_and_rejects_sequence_aba();
}
