#include <assert.h>
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
  write_u16(bytes, 4, 5);
  write_u16(bytes, 20, 3);
  repair_crc(bytes, size);
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
  // Schema 6 preserves each legacy record and the schema-5 thermal tail byte for byte.
  write_u16(bytes, 4, 5);
  write_u16(bytes, 20, 3);
  repair_crc(bytes, size);
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
  assert(upgraded_size == size && upgraded[4] == 6);
  const size_t records_offset = kLearningJournalHeaderBytes + sizeof(kContext);
  assert(memcmp(bytes + records_offset, upgraded + records_offset,
                kLearningJournalRecordBytes + kLearningJournalThermalBytes) == 0);
}

void test_schema_six_mixed_records_signed_profile_and_thermal_round_trip() {
  constexpr uint32_t now = 20000U * 86400U;
  const SegmentRecord records[] = {record(now - 3U * 86400U, -4.0f), daily_record(now)};
  const auto thermal = learned_thermal_model();
  uint8_t bytes[kLearningJournalMaxBytes];
  size_t size = 0;
  assert(encode_learning_journal({records, 2, context()}, config().quality, 8, now, bytes, sizeof(bytes), size,
                                 &thermal, now) == LearningJournalStatus::OK);
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
  // Schema 4 had the identical record layout and CRC, without the thermal tail.
  size -= kLearningJournalThermalBytes;
  write_u16(bytes, 4, 4);
  write_u16(bytes, 20, 2);
  write_u32(bytes, 8, size);
  repair_crc(bytes, size);
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

}  // namespace

int main() {
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
