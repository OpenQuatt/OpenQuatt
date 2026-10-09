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

bool load(LearningJournalStore& store, Flash& flash, LearningJournalRecords& view, uint32_t epoch = kEpoch,
          uint64_t now_ms = 0) {
  store.setup(true);
  return store.load(
      context(), QualityConfig{}, epoch,
      [&](size_t slot, uint8_t* data, size_t size) { return flash.read(slot, data, size); }, view, now_ms);
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
    if (index % 2U != 0U) {
      records[index].start_epoch_s = records[index].end_epoch_s - 86400U;
      records[index].duration_s = 86400U;
      for (int16_t& point : records[index].effective_outside_profile_centi) point = 500;
    }
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

void test_full_daily_buffer_append_checkpoints_without_revision_or_thermal_progress() {
  Flash flash;
  LearningJournalStore store;
  LearningJournalRecords view;
  assert(!load(store, flash, view));
  SegmentRecord records[kMaxSegmentRecords];
  for (size_t index = 0; index < kMaxSegmentRecords; ++index) {
    records[index] = sample();
    const uint32_t offset = static_cast<uint32_t>(kMaxSegmentRecords - 1U - index) * 86400U;
    records[index].end_epoch_s -= offset;
    records[index].start_epoch_s = records[index].end_epoch_s - 86400U;
    records[index].duration_s = 86400U;
    for (int16_t& point : records[index].effective_outside_profile_centi) point = 500;
  }
  auto checkpoint = [&](uint32_t epoch, uint64_t now) {
    return store.save(
        {records, kMaxSegmentRecords, context()}, QualityConfig{}, epoch, now, 0,
        [&](size_t slot) { return flash.erase(slot); },
        [&](size_t slot, const uint8_t* data, size_t size) { return flash.write(slot, data, size); },
        [&](size_t slot, uint8_t* data, size_t size) { return flash.read(slot, data, size); });
  };
  assert(checkpoint(kEpoch, 1000));
  assert(store.persisted_latest_end_epoch == kEpoch && store.persisted_revision == 0);
  assert(!checkpoint(kEpoch, kLater));
  for (size_t index = 1; index < kMaxSegmentRecords; ++index) records[index - 1] = records[index];
  records[kMaxSegmentRecords - 1].start_epoch_s = kEpoch;
  records[kMaxSegmentRecords - 1].end_epoch_s = kEpoch + 86400U;
  records[kMaxSegmentRecords - 1].mean_heat_w = 3000;
  assert(!checkpoint(kEpoch + 86400U, 2000));  // Same one-hour write throttle.
  assert(checkpoint(kEpoch + 86400U, kLater));
  assert(flash.writes == 2 && store.sequence == 2);
  assert(store.persisted_records == kMaxSegmentRecords && store.persisted_revision == 0);
  assert(store.persisted_thermal_samples == 0 && store.persisted_latest_end_epoch == kEpoch + 86400U);

  LearningJournalStore reboot;
  reboot.setup(true);
  assert(reboot.load(
      context(), QualityConfig{}, kEpoch + 86400U,
      [&](size_t slot, uint8_t* data, size_t size) { return flash.read(slot, data, size); }, view, 1000));
  assert(view.record_count == kMaxSegmentRecords);
  assert(view[kMaxSegmentRecords - 1].end_epoch_s == kEpoch + 86400U);
  assert(view[kMaxSegmentRecords - 1].mean_heat_w == 3000);
  assert(reboot.persisted_latest_end_epoch == kEpoch + 86400U);
  assert(!reboot.save_due(kLater, kMaxSegmentRecords, 0, 0, kEpoch + 86400U));
  assert(reset(reboot, flash));
  assert(reboot.persisted_latest_end_epoch == 0);
}

void test_daily_record_checkpoint_survives_torn_write_and_reboot() {
  Flash flash;
  LearningJournalStore store;
  LearningJournalRecords view;
  assert(!load(store, flash, view));
  auto record = sample();
  record.start_epoch_s = kEpoch - 86400U;
  record.duration_s = 86400U;
  for (int16_t& point : record.effective_outside_profile_centi) point = 500;
  assert(save(store, flash, record));
  record.mean_heat_w = 2300;
  for (int16_t& point : record.effective_outside_profile_centi) point = 600;
  record.mean_outside_c = 6;
  flash.fault = Fault::TORN_WRITE;
  assert(!save(store, flash, record, kLater, 2));
  flash.fault = Fault::NONE;
  LearningJournalStore reboot;
  assert(load(reboot, flash, view));
  assert(view.record_count == 1 && is_daily_record(view[0]));
  assert(view[0].mean_heat_w == 2200);
  for (int16_t point : view[0].effective_outside_profile_centi) assert(point == 500);
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

SegmentAccumulator daily_checkpoint(uint32_t duration_s = 600, uint32_t start_epoch = kEpoch) {
  SegmentAccumulator state;
  LearningSnapshot snapshot;
  snapshot.context_revision = 1;
  snapshot.room_c = snapshot.setpoint_c = 20;
  snapshot.outside_c = 5;
  snapshot.heat_to_water_w = 2200;
  snapshot.mean_water_c = 30;
  for (uint32_t elapsed = 0; elapsed <= duration_s; elapsed += 60) {
    snapshot.monotonic_ms = 9000ULL + elapsed * 1000ULL;
    snapshot.epoch_s = start_epoch + elapsed;
    assert(!observe_snapshot(state, snapshot, QualityConfig{}).has_record);
  }
  assert(valid_daily_checkpoint(state, QualityConfig{}));
  return state;
}

bool save_day(LearningJournalStore& store, Flash& flash, const SegmentRecord& record, const SegmentAccumulator* daily,
              uint32_t epoch, uint64_t now, uint32_t revision = 1, bool force = false,
              const ThermalModelState* thermal = nullptr) {
  return store.save(
      {&record, 1, context()}, QualityConfig{}, epoch, now, revision, [&](size_t slot) { return flash.erase(slot); },
      [&](size_t slot, const uint8_t* data, size_t size) { return flash.write(slot, data, size); },
      [&](size_t slot, uint8_t* data, size_t size) { return flash.read(slot, data, size); }, thermal,
      thermal != nullptr ? epoch : 0, daily, force);
}

void test_daily_progress_uses_fifteen_minutes_and_history_alone_still_uses_hour() {
  Flash flash;
  LearningJournalStore store;
  LearningJournalRecords view;
  assert(!load(store, flash, view));
  auto record = sample();
  auto daily = daily_checkpoint();
  constexpr uint64_t first = 1000;
  assert(save_day(store, flash, record, &daily, daily.last_epoch_s, first));
  assert(store.persisted_daily_checkpoint_epoch == daily.last_epoch_s);
  assert(!save_day(store, flash, record, &daily, daily.last_epoch_s, first + kJournalDailySaveIntervalMs));
  daily = daily_checkpoint(1200);
  assert(!save_day(store, flash, record, &daily, daily.last_epoch_s, first + kJournalDailySaveIntervalMs - 1U));
  constexpr uint64_t second = first + kJournalDailySaveIntervalMs;
  assert(save_day(store, flash, record, &daily, daily.last_epoch_s, second));
  record.mean_heat_w = 2300;
  assert(!save_day(store, flash, record, &daily, daily.last_epoch_s, second + kJournalDailySaveIntervalMs, 2));
  assert(!save_day(store, flash, record, &daily, daily.last_epoch_s, second + kJournalSaveIntervalMs - 1U, 2));
  assert(save_day(store, flash, record, &daily, daily.last_epoch_s, second + kJournalSaveIntervalMs, 2));
  assert(flash.writes == 3);
  LearningJournalStore reboot;
  assert(load(reboot, flash, view, daily.last_epoch_s, 5000));
  assert(reboot.persisted_daily_checkpoint_epoch == daily.last_epoch_s);
  reboot.persisted_revision = 2;
  assert(!reboot.save_due(5000 + kJournalDailySaveIntervalMs, 1, 2, 0, record.end_epoch_s, &daily));
  daily = daily_checkpoint(1800);
  assert(!reboot.save_due(5000 + kJournalDailySaveIntervalMs - 1U, 1, 2, 0, record.end_epoch_s, &daily));
  assert(reboot.save_due(5000 + kJournalDailySaveIntervalMs, 1, 2, 0, record.end_epoch_s, &daily));
}

void test_daily_active_to_inactive_is_cleared_immediately_once_and_reset_clears_markers() {
  Flash flash;
  LearningJournalStore store;
  LearningJournalRecords view;
  assert(!load(store, flash, view));
  const auto record = sample();
  const auto daily = daily_checkpoint();
  assert(save_day(store, flash, record, &daily, daily.last_epoch_s, 1000));
  assert(save_day(store, flash, record, nullptr, daily.last_epoch_s, 1001));
  assert(store.persisted_daily_checkpoint_epoch == 0);
  assert(!save_day(store, flash, record, nullptr, daily.last_epoch_s, 1002));
  assert(!save_day(store, flash, record, nullptr, daily.last_epoch_s, kLater));
  assert(flash.writes == 2);
  LearningJournalStore reboot;
  assert(load(reboot, flash, view, daily.last_epoch_s));
  assert(!view.has_daily_checkpoint());
  assert(reset(reboot, flash));
  assert(reboot.persisted_daily_checkpoint_epoch == 0 && reboot.persisted_daily_start_epoch == 0 &&
         reboot.last_daily_write_ms == 0);
}

void test_force_bypasses_dirty_and_time_gates_but_respects_storage_and_encode_failures() {
  Flash flash;
  LearningJournalStore store;
  const auto record = sample();
  const auto daily = daily_checkpoint();
  assert(!save_day(store, flash, record, &daily, daily.last_epoch_s, 1000, 1, true));
  store.setup(true);
  assert(!save_day(store, flash, record, &daily, daily.last_epoch_s, 1000, 1, true));
  LearningJournalRecords view;
  assert(!load(store, flash, view));
  assert(save_day(store, flash, record, &daily, daily.last_epoch_s, 1000, 1, true));
  assert(save_day(store, flash, record, &daily, daily.last_epoch_s, 1001, 1, true));
  assert(flash.writes == 2 && store.sequence == 2);
  auto invalid = daily;
  invalid.heat_integral = NAN;
  assert(!save_day(store, flash, record, &invalid, daily.last_epoch_s, 1002, 1, true));
  assert(!store.available && strcmp(store.status, "encode_failed") == 0);
  assert(!save_day(store, flash, record, &daily, daily.last_epoch_s, kLater, 1, true));
  assert(flash.writes == 2);
}

void test_forced_day_write_faults_preserve_complete_records_thermal_and_daily_on_reboot() {
  for (Fault fault : {Fault::ERASE, Fault::TORN_WRITE, Fault::LOST_ACK, Fault::READ}) {
    Flash flash;
    LearningJournalStore store;
    LearningJournalRecords view;
    assert(!load(store, flash, view));
    auto record = sample();
    auto daily = daily_checkpoint();
    auto thermal = learned_model();
    assert(save_day(store, flash, record, &daily, daily.last_epoch_s, 1000, 1, false, &thermal));
    const auto before = daily;
    daily = daily_checkpoint(1200);
    record.mean_heat_w = 2300;
    ++thermal.accepted_samples;
    flash.fault = fault;
    assert(!save_day(store, flash, record, &daily, daily.last_epoch_s, 1001, 2, true, &thermal));
    assert(!store.available && store.sequence == 1);
    const int writes = flash.writes;
    flash.fault = Fault::NONE;
    assert(!save_day(store, flash, record, &daily, daily.last_epoch_s, 1002, 2, true, &thermal));
    assert(flash.writes == writes);
    LearningJournalStore reboot;
    assert(load(reboot, flash, view, daily.last_epoch_s));
    const bool newer = fault == Fault::LOST_ACK || fault == Fault::READ;
    assert(view[0].mean_heat_w == (newer ? 2300 : 2200));
    SegmentAccumulator restored;
    assert(view.restore_daily(restored, context(), QualityConfig{}));
    assert(restored.last_epoch_s == (newer ? daily.last_epoch_s : before.last_epoch_s));
    assert(restored.heat_integral == (newer ? daily.heat_integral : before.heat_integral));
    ThermalModelState restored_model;
    uint32_t epoch = 0;
    assert(view.restore_thermal(restored_model, ThermalModelConfig{}, 1000, 1, epoch));
    assert(restored_model.accepted_samples == (newer ? 2U : 1U));
    assert(epoch == restored.last_epoch_s);
  }
}

void test_daily_crc_corruption_and_failed_clear_restore_the_previous_checkpoint() {
  Flash flash;
  LearningJournalStore store;
  LearningJournalRecords view;
  assert(!load(store, flash, view));
  const auto record = sample();
  const auto daily = daily_checkpoint();
  const auto thermal = learned_model();
  assert(save_day(store, flash, record, &daily, daily.last_epoch_s, 1000, 1, false, &thermal));
  const auto progressed = daily_checkpoint(1200);
  assert(save_day(store, flash, record, &progressed, progressed.last_epoch_s, 1001, 1, true, &thermal));
  DailyRestartTicket rtc{};
  rtc.arm(store, true);
  const auto boot_ticket = rtc.consume(true);
  const size_t byte =
      kLearningJournalHeaderBytes + sizeof(kContext) + kLearningJournalRecordBytes + kLearningJournalThermalBytes + 20U;
  flash.bytes[store.active_slot][byte] ^= 1;
  LearningJournalStore reboot;
  assert(load(reboot, flash, view, progressed.last_epoch_s));
  assert(!boot_ticket.matches(reboot.sequence, view));  // CRC fallback must not revive another grant.
  assert(view.daily_checkpoint_epoch() == daily.last_epoch_s && view[0].mean_heat_w == record.mean_heat_w);
  ThermalModelState restored_model;
  uint32_t epoch = 0;
  assert(view.restore_thermal(restored_model, ThermalModelConfig{}, 1000, 1, epoch));
  assert(restored_model.accepted_samples == thermal.accepted_samples && epoch == daily.last_epoch_s);
  flash.fault = Fault::TORN_WRITE;
  assert(!save_day(reboot, flash, record, nullptr, progressed.last_epoch_s, 1002));
  assert(!reboot.available && reboot.persisted_daily_checkpoint_epoch == daily.last_epoch_s);
  flash.fault = Fault::NONE;
  LearningJournalStore after_clear_failure;
  assert(load(after_clear_failure, flash, view, progressed.last_epoch_s));
  assert(view.daily_checkpoint_epoch() == daily.last_epoch_s);
}

void test_discard_and_reseed_same_epoch_clears_old_day_once_before_periodic_checkpoint() {
  Flash flash;
  LearningJournalStore store;
  LearningJournalRecords view;
  assert(!load(store, flash, view));
  const auto record = sample();
  const auto old_day = daily_checkpoint();
  assert(save_day(store, flash, record, &old_day, old_day.last_epoch_s, 1000));
  auto replacement = daily_checkpoint(0, old_day.last_epoch_s);
  assert(replacement.last_epoch_s == old_day.last_epoch_s && replacement.start_epoch_s != old_day.start_epoch_s);
  assert(save_day(store, flash, record, &replacement, replacement.last_epoch_s, 1001));
  assert(store.persisted_daily_checkpoint_epoch == 0 && store.persisted_daily_start_epoch == 0);
  LearningJournalStore reboot;
  assert(load(reboot, flash, view, replacement.last_epoch_s));
  assert(!view.has_daily_checkpoint() && view.record_count == 1);
  assert(!save_day(store, flash, record, &replacement, replacement.last_epoch_s, 1002));
  replacement = daily_checkpoint(600, old_day.last_epoch_s);
  assert(
      !save_day(store, flash, record, &replacement, replacement.last_epoch_s, 1001 + kJournalDailySaveIntervalMs - 1U));
  assert(save_day(store, flash, record, &replacement, replacement.last_epoch_s, 1001 + kJournalDailySaveIntervalMs));
  assert(store.persisted_daily_checkpoint_epoch == replacement.last_epoch_s);
  assert(store.persisted_daily_start_epoch == replacement.start_epoch_s && flash.writes == 3);
  const auto forced_replacement = daily_checkpoint(0, replacement.last_epoch_s);
  assert(save_day(store, flash, record, &forced_replacement, forced_replacement.last_epoch_s,
                  1002 + kJournalDailySaveIntervalMs, 1, true));
  assert(store.persisted_daily_start_epoch == forced_replacement.start_epoch_s);
  LearningJournalStore forced_reboot;
  assert(load(forced_reboot, flash, view, forced_replacement.last_epoch_s));
  SegmentAccumulator restored;
  assert(view.restore_daily(restored, context(), QualityConfig{}));
  assert(restored.start_epoch_s == forced_replacement.start_epoch_s && restored.integrated_duration_s == 0);
}

void test_restart_ticket_is_one_use_and_bound_to_verified_day() {
  Flash flash;
  LearningJournalStore store;
  LearningJournalRecords view;
  assert(!load(store, flash, view));
  const auto day = daily_checkpoint();
  assert(save_day(store, flash, sample(), &day, day.last_epoch_s, 1000, 1, true));
  DailyRestartTicket rtc{};
  rtc.arm(store, true);
  const auto boot = rtc.consume(true);
  LearningJournalStore reboot;
  assert(load(reboot, flash, view, day.last_epoch_s));
  assert(boot.matches(reboot.sequence, view));
  assert(!rtc.consume(true).matches(reboot.sequence, view));
  assert(!boot.matches(reboot.sequence + 1, view));
  auto corrupt = boot;
  corrupt.journal_crc ^= 1;
  assert(!corrupt.matches(reboot.sequence, view));
  corrupt = boot;
  ++corrupt.checkpoint_epoch;
  assert(!corrupt.matches(reboot.sequence, view));
  rtc.arm(store, true);
  assert(!rtc.consume(false).matches(reboot.sequence, view));  // Power loss, reset or crash.
  rtc.arm(store, false);
  assert(!rtc.consume(true).matches(reboot.sequence, view));
  rtc.arm(store, true);
  rtc.clear();  // An aborted OTA cannot leave a grant for a later reset.
  assert(!rtc.consume(true).matches(reboot.sequence, view));
}

void test_failed_day_invalidation_cannot_reuse_previous_restart_ticket() {
  for (auto fault : {Fault::ERASE, Fault::TORN_WRITE, Fault::LOST_ACK, Fault::READ}) {
    Flash flash;
    LearningJournalStore store;
    LearningJournalRecords view;
    assert(!load(store, flash, view));
    const auto day = daily_checkpoint();
    const auto record = sample();
    const auto thermal = learned_model();
    assert(save_day(store, flash, record, &day, day.last_epoch_s, 1000, 1, true, &thermal));
    DailyRestartTicket rtc{};
    rtc.arm(store, true);
    const auto first_boot = rtc.consume(true);
    LearningJournalStore running;
    assert(load(running, flash, view, day.last_epoch_s));
    assert(first_boot.matches(running.sequence, view));
    flash.fault = fault;
    assert(!save_day(running, flash, record, nullptr, day.last_epoch_s + 30, 1001, 1, false, &thermal));
    // The planned-restart save also fails: it must not arm from the old slot.
    const bool saved = save_day(running, flash, record, nullptr, day.last_epoch_s + 31, 1002, 1, true, &thermal);
    rtc.arm(running, saved);
    const auto next_boot = rtc.consume(true);
    flash.fault = Fault::NONE;
    LearningJournalStore reboot;
    assert(load(reboot, flash, view, day.last_epoch_s + 60));
    assert(!next_boot.matches(reboot.sequence, view));
    assert(view.record_count == 1 && view[0].mean_heat_w == record.mean_heat_w);
    ThermalModelState model;
    uint32_t epoch = 0;
    assert(view.restore_thermal(model, ThermalModelConfig{}, 1000, 1, epoch));
    assert(model.accepted_samples == thermal.accepted_samples);
  }
}

void test_ota_retry_rearms_only_the_verified_checkpoint() {
  Flash flash;
  LearningJournalStore store;
  LearningJournalRecords view;
  assert(!load(store, flash, view));
  const auto day = daily_checkpoint();
  const bool saved = save_day(store, flash, sample(), &day, day.last_epoch_s, 1000, 1, true);
  assert(saved);
  DailyRestartTicket rtc{};
  rtc.arm(store, saved);  // First OTA START prepares the prefix.
  rtc.clear();            // ERROR/ABORT leaves RAM prepared, but revokes the grant.
  const auto writes = flash.writes;
  rtc.arm(store, saved);  // Retry START reuses only the verified prefix.
  const auto boot = rtc.consume(true);
  assert(flash.writes == writes);
  LearningJournalStore reboot;
  assert(load(reboot, flash, view, day.last_epoch_s));
  assert(boot.matches(reboot.sequence, view));
  // Boot consumes even if learner setup never runs and this boot crashes.
  // A subsequent software reset must not rediscover the original grant.
  assert(!rtc.consume(true).matches(reboot.sequence, view));
  rtc.arm(store, false);  // Neither retry nor shutdown can promote a failed write.
  assert(!rtc.consume(true).matches(reboot.sequence, view));
}

}  // namespace

int main() {
  test_ota_retry_rearms_only_the_verified_checkpoint();
  test_restart_ticket_is_one_use_and_bound_to_verified_day();
  test_failed_day_invalidation_cannot_reuse_previous_restart_ticket();
  test_discard_and_reseed_same_epoch_clears_old_day_once_before_periodic_checkpoint();
  test_daily_progress_uses_fifteen_minutes_and_history_alone_still_uses_hour();
  test_daily_active_to_inactive_is_cleared_immediately_once_and_reset_clears_markers();
  test_force_bypasses_dirty_and_time_gates_but_respects_storage_and_encode_failures();
  test_forced_day_write_faults_preserve_complete_records_thermal_and_daily_on_reboot();
  test_daily_crc_corruption_and_failed_clear_restore_the_previous_checkpoint();
  test_full_daily_buffer_append_checkpoints_without_revision_or_thermal_progress();
  test_daily_record_checkpoint_survives_torn_write_and_reboot();
  test_full_capacity_journal_survives_torn_write_and_reboot();
  test_thermal_only_checkpoint_survives_reboot_and_torn_write();
  test_save_restore_and_write_rate();
  test_failed_writes_do_not_destroy_previous_slot_or_start_retry_loops();
  test_reset_and_restore_failures_are_reported_without_claiming_success();
  test_source_change_restores_but_obsolete_schema_does_not();
  test_new_reset_cannot_reuse_previous_success();
}
