#include <assert.h>
#include <math.h>
#include <string.h>

#include "../../openquatt/includes/learning/oq_ph_learning_journal.h"
#include "../../openquatt/includes/learning/oq_ph_passive_runtime_logic.h"

namespace {
using namespace oq_power_house::learning;

constexpr uint8_t kContext[] = {9, 8, 7, 6, 5, 4, 3, 2, 1};

PassiveContextView context(uint32_t source = 1, uint32_t physical = 2, uint32_t control = 3) {
  return {kContext, sizeof(kContext), source, physical, control};
}

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
  value.source_generation = 1;
  value.physical_context_generation = 2;
  value.control_generation = 3;
  value.mean_room_c = 20.0f;
  value.mean_setpoint_c = 20.0f;
  value.mean_outside_c = outside_c;
  value.mean_heat_w = 200.0f * (16.0f - outside_c);
  value.mean_heat_uncertainty_w = 50.0f;
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
  state.records[0] = record((day - 2U) * 86400U + 3600U, -4.0f);
  state.records[1] = record((day - 1U) * 86400U + 3600U, 4.0f);
  state.record_count = 2;
  return state;
}

void test_round_trip_and_reboot_remap_never_restores_readiness() {
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
  assert(initialize_passive_runtime(restored, context(7, 8, 9), config(), true) == PassiveRuntimeStatus::COLLECTING);
  assert(restore_passive_records(restored,
                                 LearningJournalRecords{{bytes, size}, metadata.context_size, metadata.record_count}));
  assert(restored.record_count == 2 && restored.restored_records && restored.fit_pending);
  assert(restored.records[0].source_generation == 7);
  assert(restored.records[0].physical_context_generation == 8);
  assert(restored.records[0].control_generation == 9);
  assert(restored.thermal_state.accepted_samples == 0);
  const auto summary = passive_runtime_summary(restored, 1);
  assert(!summary.batch_advice_ready && !summary.thermal_model_ready);
  assert(!summary.cross_validated_advice_ready && !summary.auto_apply_allowed);
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
             .status == LearningJournalStatus::CONTEXT_MISMATCH);

  memcpy(changed, bytes, size);
  const size_t mean_room_offset = kLearningJournalHeaderBytes + sizeof(kContext) + 24U;
  write_u32(changed, mean_room_offset, 0x7FC00000U);
  repair_crc(changed, size);
  assert(
      inspect_learning_journal({changed, size}, kContext, sizeof(kContext), now_epoch, state.config.quality).status ==
      LearningJournalStatus::INVALID_RECORD);

  assert(inspect_learning_journal({bytes, size}, kContext, sizeof(kContext), now_epoch + kMaxRecordAgeS + 1U,
                                  state.config.quality)
             .status == LearningJournalStatus::STALE_RECORD);
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

}  // namespace

int main() {
  test_round_trip_and_reboot_remap_never_restores_readiness();
  test_corrupt_truncated_schema_count_context_and_record_fail_closed();
  test_two_slot_selection_survives_torn_new_write_and_rejects_sequence_aba();
}
