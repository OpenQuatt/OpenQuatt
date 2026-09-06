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

void test_incompatible_context_and_legacy_schema_start_empty() {
  Flash flash;
  LearningJournalStore store;
  LearningJournalRecords view;
  load(store, flash, view);
  assert(save(store, flash, sample()));
  constexpr uint8_t changed[] = {2, 1, 9};
  LearningJournalStore different;
  different.setup(true);
  assert(!different.load(
      {changed, sizeof(changed), 1}, QualityConfig{}, kEpoch,
      [&](size_t slot, uint8_t* data, size_t size) { return flash.read(slot, data, size); }, view));
  assert(different.available);
  flash.bytes[0][4] = 1;  // Pre-simplification schema must not restore obsolete context semantics.
  LearningJournalStore legacy;
  assert(!load(legacy, flash, view));
  assert(legacy.available);
}
}  // namespace

int main() {
  test_save_restore_and_write_rate();
  test_failed_writes_do_not_destroy_previous_slot_or_start_retry_loops();
  test_reset_and_restore_failures_are_reported_without_claiming_success();
  test_incompatible_context_and_legacy_schema_start_empty();
  test_new_reset_cannot_reuse_previous_success();
}
