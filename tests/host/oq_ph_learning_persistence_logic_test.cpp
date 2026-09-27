#include <assert.h>
#include <initializer_list>
#include <string.h>

#include "../../openquatt/includes/learning/oq_ph_learning_persistence_logic.h"

using namespace oq_power_house::learning;

namespace {
constexpr uint8_t kContext[] = {2, 1, 8};
constexpr uint32_t kEpoch = 20000U * 86400U;
constexpr uint64_t kLater = kJournalSaveIntervalMs + 1000;

enum class Fault { NONE, ERASE, TORN_WRITE, LOST_ACK, READ, FALSE_ERASE };
struct Flash {
  uint8_t bytes[2][8192];
  Fault fault = Fault::NONE;
  int writes = 0;
  Flash() { memset(bytes, 0xFF, sizeof(bytes)); }
  bool read(size_t slot, uint8_t* data, size_t size) {
    if (fault == Fault::READ) return false;
    memcpy(data, bytes[slot], size);
    return true;
  }
  bool erase(size_t slot) {
    if (fault == Fault::ERASE) return false;
    if (fault != Fault::FALSE_ERASE) memset(bytes[slot], 0xFF, sizeof(bytes[slot]));
    return true;
  }
  bool write(size_t slot, const uint8_t* data, size_t size) {
    ++writes;
    memcpy(bytes[slot], data, fault == Fault::TORN_WRITE ? size / 2 : size);
    return fault != Fault::TORN_WRITE && fault != Fault::LOST_ACK;
  }
};

SegmentRecord sample() {
  SegmentRecord record;
  record.start_epoch_s = kEpoch - 4U * 3600U;
  record.end_epoch_s = kEpoch;
  record.duration_s = 4U * 3600U;
  record.context_revision = 1;
  record.mean_room_c = record.mean_setpoint_c = 20;
  record.mean_outside_c = 5;
  record.mean_heat_w = 2200;
  record.room_trend_k_per_h = record.room_range_k = record.setpoint_range_c = 0;
  record.water_start_c = record.water_end_c = 30;
  return record;
}
PassiveContextView context() { return {kContext, sizeof(kContext), 1}; }

bool load(LearningJournalStore& store, Flash& flash, LearningJournalRecords& view) {
  store.setup(true);
  return store.load(
      context(), QualityConfig{}, kEpoch,
      [&](size_t slot, uint8_t* data, size_t size) { return flash.read(slot, data, size); }, view);
}
bool save(LearningJournalStore& store, Flash& flash, const SegmentRecord& record, uint64_t now = 1000,
          uint32_t revision = 1) {
  return store.save(
      {&record, 1, context()}, QualityConfig{}, kEpoch, now, revision, [&](size_t slot) { return flash.erase(slot); },
      [&](size_t slot, const uint8_t* data, size_t size) { return flash.write(slot, data, size); },
      [&](size_t slot, uint8_t* data, size_t size) { return flash.read(slot, data, size); });
}
bool reset(LearningJournalStore& store, Flash& flash) {
  return store.reset([&]() { return flash.erase(0) && flash.erase(1); },
                     [&](size_t slot, uint8_t* data, size_t size) { return flash.read(slot, data, size); });
}

void test_full_capacity_journal_survives_torn_write_and_reboot() {
  Flash flash;
  LearningJournalStore store;
  LearningJournalRecords view;
  assert(!load(store, flash, view));
  uint8_t max_context[kMaxPassiveContextBytes]{};
  SegmentRecord records[kMaxSegmentRecords];
  for (size_t index = 0; index < kMaxSegmentRecords; ++index) {
    records[index] = sample();
    const uint32_t offset = static_cast<uint32_t>(kMaxSegmentRecords - 1U - index) * 86400U;
    records[index].start_epoch_s -= offset;
    records[index].end_epoch_s -= offset;
  }
  const LearningDatasetView dataset{records, kMaxSegmentRecords, {max_context, sizeof(max_context), 1}};
  auto write = [&](uint64_t now, uint32_t revision) {
    return store.save(
        dataset, QualityConfig{}, kEpoch, now, revision, [&](size_t slot) { return flash.erase(slot); },
        [&](size_t slot, const uint8_t* data, size_t size) { return flash.write(slot, data, size); },
        [&](size_t slot, uint8_t* data, size_t size) { return flash.read(slot, data, size); });
  };
  assert(write(1000, 1));
  records[kMaxSegmentRecords - 1U].mean_heat_w = 3000;
  flash.fault = Fault::TORN_WRITE;
  assert(!write(kLater, 2));
  flash.fault = Fault::NONE;
  LearningJournalStore rebooted;
  assert(load(rebooted, flash, view));
  assert(view.record_count == kMaxSegmentRecords);
  assert(view[kMaxSegmentRecords - 1U].mean_heat_w == 2200);
  assert(view[0].end_epoch_s == records[0].end_epoch_s);
}

void test_save_restore_and_write_rate() {
  Flash flash;
  LearningJournalStore store;
  LearningJournalRecords view;
  assert(!load(store, flash, view));
  auto record = sample();
  assert(save(store, flash, record));
  assert(!save(store, flash, record, 2000, 2));
  assert(flash.writes == 1);
  record.mean_heat_w = 2300;
  assert(save(store, flash, record, kLater, 2));
  LearningJournalStore reboot;
  assert(load(reboot, flash, view));
  assert(view.record_count == 1 && view[0].mean_heat_w == 2300 && reboot.sequence == 2);
  assert(reset(reboot, flash));
  LearningJournalStore after_reset;
  assert(!load(after_reset, flash, view));
  assert(after_reset.available);
}

void test_failed_writes_do_not_destroy_previous_slot_or_start_retry_loops() {
  for (Fault fault : {Fault::ERASE, Fault::TORN_WRITE, Fault::LOST_ACK, Fault::READ}) {
    Flash flash;
    LearningJournalStore store;
    LearningJournalRecords view;
    load(store, flash, view);
    auto record = sample();
    assert(save(store, flash, record));
    record.mean_heat_w = 2300;
    flash.fault = fault;
    assert(!save(store, flash, record, kLater, 2));
    assert(!store.available && store.sequence == 1);
    const int writes = flash.writes;
    assert(!save(store, flash, record, 2 * kLater, 2));
    assert(flash.writes == writes);
    flash.fault = Fault::NONE;
    LearningJournalStore reboot;
    assert(load(reboot, flash, view));
    // A lost acknowledgement/readback can leave a complete newer slot. Both
    // outcomes are valid; no partially written record may be restored.
    assert(view[0].mean_heat_w == ((fault == Fault::LOST_ACK || fault == Fault::READ) ? 2300 : 2200));
  }
}

void test_reset_and_restore_failures_are_reported_without_claiming_success() {
  for (Fault fault : {Fault::ERASE, Fault::FALSE_ERASE, Fault::READ}) {
    Flash flash;
    LearningJournalStore store;
    LearningJournalRecords view;
    load(store, flash, view);
    assert(save(store, flash, sample()));
    flash.fault = fault;
    assert(!reset(store, flash));
    assert(!store.available && strcmp(store.status, "reset_failed") == 0);
  }
  Flash flash;
  flash.fault = Fault::READ;
  LearningJournalStore store;
  LearningJournalRecords view;
  assert(!load(store, flash, view));
  assert(!store.available && store.loaded);
}

void test_new_reset_cannot_reuse_previous_success() {
  Flash flash;
  LearningJournalStore store;
  LearningJournalRecords view;
  load(store, flash, view);
  assert(reset(store, flash));
  assert(store.persisted_records == 0 && strcmp(store.status, "cleared") == 0);
  // Before the next learner tick erases flash, the old successful reset must
  // no longer satisfy the webapp's paused + zero records + cleared condition.
  store.request_reset();
  assert(strcmp(store.status, "reset_pending") == 0);
  flash.fault = Fault::ERASE;
  assert(!reset(store, flash));
  assert(strcmp(store.status, "reset_failed") == 0);
}

void test_source_change_restores_but_obsolete_schema_does_not() {
  Flash flash;
  LearningJournalStore store;
  LearningJournalRecords view;
  load(store, flash, view);
  assert(save(store, flash, sample()));
  constexpr uint8_t changed[] = {2, 1, 9};
  LearningJournalStore different;
  different.setup(true);
  assert(different.load(
      {changed, sizeof(changed), 1}, QualityConfig{}, kEpoch,
      [&](size_t slot, uint8_t* data, size_t size) { return flash.read(slot, data, size); }, view));
  assert(different.available);
  flash.bytes[0][4] = 1;  // Pre-simplification schema must not restore obsolete context semantics.
  LearningJournalStore legacy;
  assert(!load(legacy, flash, view));
  assert(legacy.available);
}

ThermalInterval model_interval(uint64_t start) {
  ThermalInterval interval;
  interval.start_monotonic_ms = start;
  interval.end_monotonic_ms = start + 1800000;
  interval.context_revision = 1;
  interval.complete = interval.inputs_fresh = interval.generations_consistent = true;
  interval.operational_gates_passed = interval.hidden_heat_exclusion_valid = interval.hidden_heat_excluded = true;
  interval.indoor_start_c = 20;
  interval.indoor_end_c = 20.1;
  interval.mean_indoor_c = 20.05;
  interval.mean_outside_c = 5;
  interval.mean_heat_w = 4200;
  return interval;
}

ThermalModelState learned_model() {
  ThermalModelState model;
  assert(initialize_thermal_model(model, ThermalModelConfig{}));
  assert(observe_thermal_interval(model, model_interval(1000), ThermalModelConfig{}).accepted);
  return model;
}

void test_thermal_only_checkpoint_survives_reboot_and_torn_write() {
  Flash flash;
  LearningJournalStore store;
  LearningJournalRecords view;
  assert(!load(store, flash, view));
  auto model = learned_model();
  auto save_model = [&](uint64_t now) {
    return store.save(
        {nullptr, 0, context()}, QualityConfig{}, kEpoch, now, 0, [&](size_t slot) { return flash.erase(slot); },
        [&](size_t slot, const uint8_t* data, size_t size) { return flash.write(slot, data, size); },
        [&](size_t slot, uint8_t* data, size_t size) { return flash.read(slot, data, size); }, &model, kEpoch);
  };
  assert(save_model(1000));
  const auto before = model;
  ++model.accepted_samples;
  assert(!save_model(2000));
  flash.fault = Fault::TORN_WRITE;
  assert(!save_model(kLater));
  flash.fault = Fault::NONE;
  LearningJournalStore reboot;
  assert(load(reboot, flash, view));
  assert(view.record_count == 0);
  ThermalModelState restored;
  uint32_t epoch = 0;
  assert(view.restore_thermal(restored, ThermalModelConfig{}, 500, 9, epoch));
  assert(epoch == kEpoch && restored.accepted_samples == before.accepted_samples);
  assert(restored.theta_loss_scaled == before.theta_loss_scaled);
  assert(restored.theta_heat_scaled == before.theta_heat_scaled);
  assert(restored.covariance_00 == before.covariance_00 && restored.covariance_01 == before.covariance_01);
  assert(restored.information_11 == before.information_11);
  assert(restored.context_revision == 9 && restored.last_interval_end_monotonic_ms == 500);
  assert(!estimate_thermal_model(restored, ThermalModelConfig{}, 500).ready);
  reboot.persisted_thermal_samples = restored.accepted_samples;
  assert(!reboot.save_due(kLater, 0, 0, restored.accepted_samples));
  auto continuous = before;
  auto resumed = model_interval(500);
  resumed.context_revision = 9;
  assert(observe_thermal_interval(restored, resumed, ThermalModelConfig{}).accepted);
  assert(
      observe_thermal_interval(continuous, model_interval(before.last_interval_end_monotonic_ms), ThermalModelConfig{})
          .accepted);
  assert(restored.theta_loss_scaled == continuous.theta_loss_scaled);
  assert(restored.theta_heat_scaled == continuous.theta_heat_scaled);
  assert(restored.covariance_11 == continuous.covariance_11);
  assert(restored.accepted_samples == continuous.accepted_samples);
  // A season without observations behaves like a reboot, not a numeric reset.
  auto after_summer = before;
  assert(observe_thermal_interval(after_summer, model_interval(180ULL * 86400000ULL), ThermalModelConfig{}).accepted);
  assert(after_summer.reset_count == before.reset_count);
  assert(after_summer.theta_loss_scaled == continuous.theta_loss_scaled);
  assert(after_summer.covariance_11 == continuous.covariance_11);
  assert(reboot.save_due(kLater, 0, 0, restored.accepted_samples));
  assert(reset(reboot, flash));
  LearningJournalStore cleared;
  assert(!load(cleared, flash, view));
}

}  // namespace

int main() {
  test_full_capacity_journal_survives_torn_write_and_reboot();
  test_thermal_only_checkpoint_survives_reboot_and_torn_write();
  test_save_restore_and_write_rate();
  test_failed_writes_do_not_destroy_previous_slot_or_start_retry_loops();
  test_reset_and_restore_failures_are_reported_without_claiming_success();
  test_source_change_restores_but_obsolete_schema_does_not();
  test_new_reset_cannot_reuse_previous_success();
}
