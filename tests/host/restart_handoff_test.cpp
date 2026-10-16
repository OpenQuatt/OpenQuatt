#include <initializer_list>
#include "openquatt/includes/control/oq_power_house_demand_logic.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "components/openquatt_incident_manager/OpenQuattRestartHandoffPolicy.h"

namespace {

using esphome::openquatt_incident_manager::restart_handoff::BootContext;
using esphome::openquatt_incident_manager::restart_handoff::configuration_hash;
using esphome::openquatt_incident_manager::restart_handoff::consume_record;
using esphome::openquatt_incident_manager::restart_handoff::finalize_record;
using esphome::openquatt_incident_manager::restart_handoff::may_grant_after_durable_consume;
using esphome::openquatt_incident_manager::restart_handoff::may_restore_credit;
using esphome::openquatt_incident_manager::restart_handoff::persist_record;
using esphome::openquatt_incident_manager::restart_handoff::Record;
using esphome::openquatt_incident_manager::restart_handoff::ResumeState;
using esphome::openquatt_incident_manager::restart_handoff::StorageReadResult;
using esphome::openquatt_incident_manager::restart_handoff::valid_record;
namespace handoff = esphome::openquatt_incident_manager::restart_handoff;

constexpr uint32_t MINIMUM_OFF_MS = 240000U;

struct FakeStorage {
  bool present{false};
  Record record{};
  bool load_error{false};
  mutable uint8_t transient_read_errors{0U};
  bool erase_error{false};
  bool erase_commit_ok{true};
  bool erase_not_found{false};
  bool erase_readback_stale{false};
  bool write_ok{true};
  bool write_commit_ok{true};
  bool write_readback_stale{false};
  uint32_t erase_calls{0U};
  const ResumeState* exposed_resume{nullptr};

  StorageReadResult read(Record* target) const {
    if (exposed_resume != nullptr) {
      assert(exposed_resume->hp_mask == 0U);
      assert(!handoff::valid_power_house_resume(exposed_resume->power_house));
    }
    if (load_error || target == nullptr) return StorageReadResult::ERROR;
    if (transient_read_errors > 0U) {
      --transient_read_errors;
      return StorageReadResult::ERROR;
    }
    if (!present) return StorageReadResult::ABSENT;
    *target = record;
    return StorageReadResult::PRESENT;
  }

  bool erase_and_verify_absent() {
    ++erase_calls;
    if (erase_error || !erase_commit_ok) return false;
    if (!erase_not_found && !erase_readback_stale) present = false;
    Record verified{};
    return this->read(&verified) == StorageReadResult::ABSENT;
  }

  bool write_commit_and_verify(const Record& next) {
    if (!write_ok) return false;
    record = next;
    present = true;
    if (!write_commit_ok) return false;
    if (write_readback_stale) record.hp1_credit_ms--;
    Record verified{};
    return this->read(&verified) == StorageReadResult::PRESENT && std::memcmp(&next, &verified, sizeof(next)) == 0;
  }
};

Record make_record() {
  Record record{};
  record.magic = esphome::openquatt_incident_manager::restart_handoff::kRecordMagic;
  record.version = esphome::openquatt_incident_manager::restart_handoff::kRecordVersion;
  record.state = esphome::openquatt_incident_manager::restart_handoff::kRecordArmed;
  record.minimum_off_ms = MINIMUM_OFF_MS;
  record.config_hash = configuration_hash(MINIMUM_OFF_MS, true);
  record.boot_partition_address = 0x210000U;
  record.hp1_credit_ms = MINIMUM_OFF_MS;
  record.hp2_credit_ms = 120000U;
  for (size_t index = 0U; index < sizeof(record.image_hash); ++index)
    record.image_hash[index] = static_cast<uint8_t>(index);
  finalize_record(&record);
  return record;
}

BootContext matching_context(const Record& record) {
  BootContext context{};
  context.software_reset = true;
  context.running_partition_matches_boot = true;
  context.image_valid = true;
  context.image_confirmed = true;
  context.minimum_off_ms = record.minimum_off_ms;
  context.config_hash = record.config_hash;
  context.boot_partition_address = record.boot_partition_address;
  std::memcpy(context.image_hash, record.image_hash, sizeof(context.image_hash));
  return context;
}

void test_only_exact_controlled_reboot_restores_credit() {
  const Record record = make_record();
  assert(valid_record(record));
  const BootContext context = matching_context(record);
  assert(may_restore_credit(record, context));
}

void test_pending_ota_boot_uses_same_exact_handoff_policy() {
  const Record record = make_record();

  auto pending_context = matching_context(record);
  pending_context.image_valid = false;
  pending_context.image_pending_verify = true;
  assert(may_restore_credit(record, pending_context));

  pending_context.image_hash[0] ^= 0x5AU;
  assert(!may_restore_credit(record, pending_context));

  auto wrong_partition = matching_context(record);
  wrong_partition.image_valid = false;
  wrong_partition.image_pending_verify = true;
  wrong_partition.boot_partition_address++;
  assert(!may_restore_credit(record, wrong_partition));
}

void test_corrupt_or_partial_arm_never_restores_credit() {
  Record corrupt = make_record();
  corrupt.hp1_credit_ms++;
  assert(!valid_record(corrupt));
  assert(!may_restore_credit(corrupt, matching_context(corrupt)));

  Record partial = make_record();
  partial.state = 0U;
  finalize_record(&partial);
  assert(!valid_record(partial));

  const Record valid = make_record();
  Record partially_consumed{};
  assert(!valid_record(partially_consumed));
  assert(!may_grant_after_durable_consume(true, partially_consumed, matching_context(valid)));
}

void test_replay_and_non_restart_paths_are_rejected() {
  const Record record = make_record();
  auto context = matching_context(record);
  context.software_reset = false;
  assert(!may_restore_credit(record, context));
  context = matching_context(record);
  context.running_partition_matches_boot = false;
  assert(!may_restore_credit(record, context));
  context = matching_context(record);
  context.boot_partition_address++;
  assert(!may_restore_credit(record, context));
  context = matching_context(record);
  context.image_valid = false;
  assert(!may_restore_credit(record, context));
}

void test_consume_failure_never_grants_in_memory_credit() {
  const Record record = make_record();
  const BootContext context = matching_context(record);
  assert(may_restore_credit(record, context));
  assert(!may_grant_after_durable_consume(false, record, context));
  assert(may_grant_after_durable_consume(true, record, context));
}

void test_storage_consume_clears_before_credit_and_cannot_replay() {
  FakeStorage storage{};
  storage.present = true;
  storage.record = make_record();
  const BootContext context = matching_context(storage.record);
  uint32_t hp1_credit = 0U;
  uint32_t hp2_credit = 0U;
  assert(consume_record(storage, context, &hp1_credit, &hp2_credit));
  assert(hp1_credit == MINIMUM_OFF_MS);
  assert(hp2_credit == 120000U);
  assert(storage.erase_calls == 1U);
  assert(!storage.present);

  hp1_credit = 1U;
  hp2_credit = 1U;
  assert(consume_record(storage, context, &hp1_credit, &hp2_credit));
  assert(hp1_credit == 0U && hp2_credit == 0U);
}

void test_storage_consume_failure_injection_fails_closed() {
  const Record record = make_record();
  const BootContext context = matching_context(record);
  uint32_t hp1_credit = 123U;
  uint32_t hp2_credit = 456U;

  FakeStorage corrupt{};
  corrupt.present = true;
  corrupt.record = record;
  corrupt.record.checksum++;
  assert(consume_record(corrupt, context, &hp1_credit, &hp2_credit));
  assert(corrupt.erase_calls == 1U && hp1_credit == 0U && hp2_credit == 0U);

  FakeStorage unreadable{};
  unreadable.load_error = true;
  unreadable.erase_readback_stale = true;
  assert(!consume_record(unreadable, context, &hp1_credit, &hp2_credit));
  assert(unreadable.erase_calls == 1U && hp1_credit == 0U && hp2_credit == 0U);

  // An initially ambiguous direct read is only accepted when the post-erase
  // direct read proves that no record exists; an erase-not-found alone is not
  // considered proof.
  FakeStorage ambiguous_then_absent{};
  ambiguous_then_absent.transient_read_errors = 1U;
  ambiguous_then_absent.erase_not_found = true;
  assert(consume_record(ambiguous_then_absent, context, &hp1_credit, &hp2_credit));
  assert(ambiguous_then_absent.erase_calls == 1U && hp1_credit == 0U && hp2_credit == 0U);

  FakeStorage erase_not_found_but_present{};
  erase_not_found_but_present.present = true;
  erase_not_found_but_present.record = record;
  erase_not_found_but_present.erase_not_found = true;
  assert(!consume_record(erase_not_found_but_present, context, &hp1_credit, &hp2_credit));
  assert(hp1_credit == 0U && hp2_credit == 0U);

  FakeStorage erase_commit_failed{};
  erase_commit_failed.present = true;
  erase_commit_failed.record = record;
  erase_commit_failed.erase_commit_ok = false;
  assert(!consume_record(erase_commit_failed, context, &hp1_credit, &hp2_credit));
  assert(hp1_credit == 0U && hp2_credit == 0U);
}

void test_storage_arm_failure_injection_requires_commit_and_readback() {
  const Record record = make_record();
  FakeStorage commit_failed{};
  commit_failed.write_commit_ok = false;
  assert(!persist_record(commit_failed, record));

  FakeStorage stale_readback{};
  stale_readback.write_readback_stale = true;
  assert(!persist_record(stale_readback, record));

  FakeStorage saved{};
  assert(persist_record(saved, record));
  assert(saved.present && std::memcmp(&saved.record, &record, sizeof(record)) == 0);
}

void test_failed_arm_with_visible_record_is_consumed_once_before_replay() {
  const Record record = make_record();
  const BootContext context = matching_context(record);
  FakeStorage storage{};
  storage.write_commit_ok = false;
  assert(!persist_record(storage, record));
  assert(storage.present);

  uint32_t hp1_credit = 0U;
  uint32_t hp2_credit = 0U;
  // Even if a failed commit left bytes visible, boot must erase and verify the
  // one-shot record before exposing its credit.
  assert(consume_record(storage, context, &hp1_credit, &hp2_credit));
  assert(!storage.present && hp1_credit == MINIMUM_OFF_MS && hp2_credit == 120000U);

  hp1_credit = 1U;
  hp2_credit = 1U;
  assert(consume_record(storage, context, &hp1_credit, &hp2_credit));
  assert(hp1_credit == 0U && hp2_credit == 0U);
}

void test_uncontrolled_reset_consumes_without_credit() {
  FakeStorage storage{};
  storage.present = true;
  storage.record = make_record();
  auto context = matching_context(storage.record);
  context.software_reset = false;
  uint32_t hp1_credit = 1U;
  uint32_t hp2_credit = 1U;
  assert(consume_record(storage, context, &hp1_credit, &hp2_credit));
  assert(!storage.present && hp1_credit == 0U && hp2_credit == 0U);
}

void test_consume_commit_failure_blocks_then_recovers_once() {
  FakeStorage storage{};
  storage.present = true;
  storage.record = make_record();
  const BootContext context = matching_context(storage.record);
  uint32_t hp1_credit = 1U;
  uint32_t hp2_credit = 1U;
  storage.erase_commit_ok = false;
  assert(!consume_record(storage, context, &hp1_credit, &hp2_credit));
  assert(storage.present && hp1_credit == 0U && hp2_credit == 0U);

  storage.erase_commit_ok = true;
  assert(consume_record(storage, context, &hp1_credit, &hp2_credit));
  assert(!storage.present && hp1_credit == MINIMUM_OFF_MS && hp2_credit == 120000U);

  assert(consume_record(storage, context, &hp1_credit, &hp2_credit));
  assert(hp1_credit == 0U && hp2_credit == 0U);
}

void test_image_and_configuration_mismatch_are_rejected() {
  const Record record = make_record();
  auto context = matching_context(record);
  context.image_hash[0] ^= 0xFFU;
  assert(!may_restore_credit(record, context));
  context = matching_context(record);
  context.config_hash = configuration_hash(MINIMUM_OFF_MS, false);
  assert(!may_restore_credit(record, context));
  context = matching_context(record);
  context.minimum_off_ms = MINIMUM_OFF_MS - 1U;
  assert(!may_restore_credit(record, context));
  context = matching_context(record);
  std::memset(context.image_hash, 0, sizeof(context.image_hash));
  assert(!may_restore_credit(record, context));
}

void test_invalid_credit_bounds_fail_closed() {
  Record record = make_record();
  record.hp1_credit_ms = MINIMUM_OFF_MS + 1U;
  finalize_record(&record);
  assert(!valid_record(record));
  record = make_record();
  record.hp1_credit_ms = 0U;
  record.hp2_credit_ms = 0U;
  finalize_record(&record);
  assert(!valid_record(record));
}

Record make_resume_record(uint8_t mask = 3U, uint8_t state = handoff::kRecordArmed) {
  Record record = make_record();
  record.state = state;
  record.resume_mask = mask;
  if ((mask & 1U) != 0U) record.hp1_credit_ms = 0U;
  if ((mask & 2U) != 0U) record.hp2_credit_ms = 0U;
  record.power_house = {1500.0f, 0.20f, 23.0f};
  finalize_record(&record);
  return record;
}

ResumeState poisoned_resume() { return {3U, true, {49999.0f, 0.20f, 35.0f}}; }

void assert_no_resume(const ResumeState& resume) {
  assert(resume.hp_mask == 0U);
  assert(!resume.completed_ota);
  assert(!handoff::valid_power_house_resume(resume.power_house));
}

void test_journal_resume_is_per_hp_durable_and_one_shot() {
  for (uint8_t mask : {1U, 2U, 3U}) {
    for (uint8_t state : {handoff::kRecordArmed, handoff::kRecordCompletedOta}) {
      FakeStorage storage{};
      const Record record = make_resume_record(mask, state);
      const auto context = matching_context(record);
      assert(valid_record(record) && handoff::may_restore_running(record, context));
      assert(persist_record(storage, record));
      uint32_t hp1 = 99U, hp2 = 99U;
      auto resume = poisoned_resume();
      storage.exposed_resume = &resume;  // Every direct read, including erase verification, must see no permit yet.
      assert(consume_record(storage, context, &hp1, &hp2, &resume));
      assert(storage.erase_calls == 1U && !storage.present);
      assert(hp1 == record.hp1_credit_ms && hp2 == record.hp2_credit_ms);
      assert(resume.hp_mask == mask);
      assert(resume.completed_ota == (state == handoff::kRecordCompletedOta));
      assert(std::memcmp(&resume.power_house, &record.power_house, sizeof(record.power_house)) == 0);
      assert(consume_record(storage, context, &hp1, &hp2, &resume));
      assert(hp1 == 0U && hp2 == 0U);
      assert_no_resume(resume);
    }
  }
}

void test_resume_requires_exact_current_boot_context() {
  const Record record = make_resume_record();
  for (int fault = 0; fault < 9; ++fault) {
    auto context = matching_context(record);
    switch (fault) {
      case 0:
        context.software_reset = false;
        break;  // Power loss, crash or watchdog reset.
      case 1:
        context.running_partition_matches_boot = false;
        break;
      case 2:
        context.boot_partition_address++;
        break;  // Rollback or another selected slot.
      case 3:
        context.image_hash[0] ^= 1U;
        break;  // Another image in the same slot.
      case 4:
        context.config_hash = configuration_hash(MINIMUM_OFF_MS, false);
        break;
      case 5:
        context.minimum_off_ms--;
        context.config_hash = configuration_hash(context.minimum_off_ms, true);
        break;
      case 6:
        context.image_valid = context.image_confirmed = false;
        break;
      case 7:
        context = {};
        break;
      case 8:
        context.config_hash = configuration_hash(MINIMUM_OFF_MS, true, true);
        break;  // Another controller hardware profile.
    }
    FakeStorage storage{};
    storage.present = true;
    storage.record = record;
    uint32_t hp1 = 99U, hp2 = 99U;
    auto resume = poisoned_resume();
    assert(!handoff::may_restore_running(record, context));
    assert(consume_record(storage, context, &hp1, &hp2, &resume));
    assert(!storage.present && storage.erase_calls == 1U && hp1 == 0U && hp2 == 0U);
    assert_no_resume(resume);
    assert(consume_record(storage, matching_context(record), &hp1, &hp2, &resume));
    assert_no_resume(resume);  // Rejected context must still consume, not defer a later replay.
  }
}

void test_only_completed_ota_can_resume_pending_first_boot() {
  for (uint8_t state : {handoff::kRecordArmed, handoff::kRecordCompletedOta}) {
    const Record record = make_resume_record(3U, state);
    auto context = matching_context(record);
    context.image_confirmed = context.image_valid = false;
    context.image_pending_verify = true;  // Also covers NEW as decoded by capture_boot_context.
    const bool ota = state == handoff::kRecordCompletedOta;
    assert(handoff::may_restore_running(record, context) == ota);
    FakeStorage storage{};
    storage.present = true;
    storage.record = record;
    uint32_t hp1 = 99U, hp2 = 99U;
    auto resume = poisoned_resume();
    assert(consume_record(storage, context, &hp1, &hp2, &resume));
    assert(hp1 == 0U && hp2 == 0U && !storage.present);
    if (ota)
      assert(resume.hp_mask == 3U && handoff::valid_power_house_resume(resume.power_house));
    else
      assert_no_resume(resume);

    context.image_pending_verify = false;
    context.image_valid = true;  // UNDEFINED/NOT_FOUND is accepted only for same-image planned restarts.
    assert(handoff::may_restore_running(record, context) == !ota);
    storage.present = true;
    storage.record = record;
    assert(consume_record(storage, context, &hp1, &hp2, &resume));
    assert(resume.hp_mask == (ota ? 0U : 3U));
    if (!ota) assert(handoff::may_adopt_running_image(resume, context));
    ResumeState ota_resume{3U, true, {}};
    assert(!handoff::may_adopt_running_image(ota_resume, context));
    context.image_confirmed = true;
    assert(handoff::may_adopt_running_image(ota_resume, context));
  }
}

void test_resume_legacy_partial_corrupt_and_conflicting_records_fail_closed() {
  for (int fault = 0; fault < 8; ++fault) {
    FakeStorage storage{};
    storage.present = true;
    storage.record = make_resume_record();
    const auto context = matching_context(storage.record);
    switch (fault) {
      case 0:
        storage.record.version = 1U;
        break;  // Legacy journal format.
      case 1:
        storage.record.state = 0U;
        break;  // Partial/unarmed record.
      case 2:
        storage.record.resume_mask = 4U;
        break;
      case 3:
        storage.record.hp1_credit_ms = 1U;
        break;  // A running HP cannot inherit off-time credit.
      case 4:
        storage.record.hp2_credit_ms = 1U;
        break;
      case 5:
        storage.record.magic ^= 1U;
        break;
      case 6:
        std::memset(storage.record.image_hash, 0, sizeof(storage.record.image_hash));
        break;
      case 7:
        storage.record.power_house.last_w += 1.0f;
        break;
    }
    if (fault != 7) finalize_record(&storage.record);  // Numeric corruption must also fail its existing checksum.
    assert(!valid_record(storage.record));
    uint32_t hp1 = 99U, hp2 = 99U;
    auto resume = poisoned_resume();
    assert(consume_record(storage, context, &hp1, &hp2, &resume));
    assert(!storage.present && hp1 == 0U && hp2 == 0U);
    assert_no_resume(resume);
  }
}

void test_resume_consume_failure_injection_exposes_no_state() {
  const auto record = make_resume_record();
  const auto context = matching_context(record);
  for (int fault = 0; fault < 5; ++fault) {
    FakeStorage storage{};
    storage.present = true;
    storage.record = record;
    switch (fault) {
      case 0:
        storage.erase_error = true;
        break;
      case 1:
        storage.erase_commit_ok = false;
        break;
      case 2:
        storage.erase_readback_stale = true;
        break;
      case 3:
        storage.erase_not_found = true;
        break;
      case 4:
        storage.load_error = true;
        break;  // Includes an unreadable old-size NVS blob.
    }
    uint32_t hp1 = 99U, hp2 = 99U;
    auto resume = poisoned_resume();
    storage.exposed_resume = &resume;
    assert(!consume_record(storage, context, &hp1, &hp2, &resume));
    assert(storage.erase_calls == 1U && hp1 == 0U && hp2 == 0U);
    assert_no_resume(resume);
  }
  FakeStorage ambiguous{};
  ambiguous.present = true;
  ambiguous.record = record;
  ambiguous.transient_read_errors = 1U;
  uint32_t hp1 = 99U, hp2 = 99U;
  auto resume = poisoned_resume();
  assert(consume_record(ambiguous, context, &hp1, &hp2, &resume));
  assert(!ambiguous.present && hp1 == 0U && hp2 == 0U);
  assert_no_resume(resume);  // Erase proof retires an ambiguous record; it does not authorize its unseen contents.
}

void test_cold_and_crash_consume_retries_keep_the_original_reset_context() {
  for (uint8_t state : {handoff::kRecordArmed, handoff::kRecordCompletedOta}) {
    const auto record = make_resume_record(1U, state);
    auto context = matching_context(record);
    context.software_reset = false;  // POWERON, PANIC and watchdog resets are all ineligible.
    if (state == handoff::kRecordCompletedOta) {
      context.image_confirmed = context.image_valid = false;
      context.image_pending_verify = true;
    }
    for (int fault = 0; fault < 4; ++fault) {
      FakeStorage storage{};
      storage.present = true;
      storage.record = record;
      switch (fault) {
        case 0:
          storage.erase_error = true;
          break;
        case 1:
          storage.erase_commit_ok = false;
          break;
        case 2:
          storage.erase_readback_stale = true;
          break;
        case 3:
          storage.load_error = true;
          break;
      }
      uint32_t hp1 = 99U, hp2 = 99U;
      auto resume = poisoned_resume();
      storage.exposed_resume = &resume;
      assert(!consume_record(storage, context, &hp1, &hp2, &resume));
      assert(hp1 == 0U && hp2 == 0U);
      assert_no_resume(resume);

      storage.erase_error = storage.erase_readback_stale = storage.load_error = false;
      storage.erase_commit_ok = true;
      // Retry within the same boot: never replace the original reset reason
      // with the ESP_RST_SW that an automatic reboot would have manufactured.
      assert(consume_record(storage, context, &hp1, &hp2, &resume));
      assert(!storage.present && hp1 == 0U && hp2 == 0U);
      assert_no_resume(resume);
      context.software_reset = true;
      assert(consume_record(storage, context, &hp1, &hp2, &resume));
      assert_no_resume(resume);  // A later planned restart cannot replay the rejected record.
      context.software_reset = false;
    }
  }
}

void test_incomplete_boot_consume_denies_subsequent_software_restarts() {
  for (uint8_t state : {handoff::kRecordArmed, handoff::kRecordCompletedOta}) {
    for (bool original_software_reset : {false, true}) {
      for (int fault = 0; fault < 3; ++fault) {
        FakeStorage storage{};
        storage.present = true;
        storage.record = make_resume_record(1U, state);
        volatile uint32_t marker = 0U;
        assert(handoff::begin_boot_consume(marker));
        assert(marker == handoff::kConsumeIncomplete);
        auto original = matching_context(storage.record);
        original.software_reset = original_software_reset;
        uint32_t hp1 = 0U, hp2 = 0U;
        ResumeState resume{};
        storage.exposed_resume = &resume;
        if (fault == 1) storage.erase_commit_ok = false;
        if (fault == 2) storage.load_error = true;
        // A failed NVS open cannot enter consume_record at all.
        if (fault != 0) assert(!consume_record(storage, original, &hp1, &hp2, &resume));
        assert_no_resume(resume);

        // Recovery, a failed-manager fallback or any other SW reboot must not
        // requalify the record left by this incomplete boot.
        auto retry = matching_context(storage.record);
        const bool previous_complete = handoff::begin_boot_consume(marker);
        assert(!previous_complete);
        if (!previous_complete) retry.software_reset = false;
        storage.erase_commit_ok = true;
        storage.load_error = false;
        assert(consume_record(storage, retry, &hp1, &hp2, &resume));
        assert(!storage.present && hp1 == 0U && hp2 == 0U);
        assert_no_resume(resume);
        handoff::finish_boot_consume(marker);
        assert(marker == 0U);

        // The deny marker does not permanently disable a later, newly armed
        // and durably consumed planned handoff.
        storage.record = make_resume_record(1U, state);
        storage.present = true;
        assert(handoff::begin_boot_consume(marker));
        assert(consume_record(storage, matching_context(storage.record), &hp1, &hp2, &resume));
        assert(resume.hp_mask == 1U && hp1 == 0U && hp2 == storage.record.hp2_credit_ms);
        handoff::finish_boot_consume(marker);
        assert(marker == 0U);
      }
    }
  }
}

void test_resume_failed_persistence_and_retry_cannot_bypass_consume() {
  const auto record = make_resume_record();
  const auto context = matching_context(record);
  for (int fault = 0; fault < 3; ++fault) {
    FakeStorage storage{};
    switch (fault) {
      case 0:
        storage.write_ok = false;
        break;
      case 1:
        storage.write_commit_ok = false;
        break;
      case 2:
        storage.write_readback_stale = true;
        break;
    }
    assert(!persist_record(storage, record));
    uint32_t hp1 = 99U, hp2 = 99U;
    auto resume = poisoned_resume();
    storage.exposed_resume = &resume;
    storage.erase_commit_ok = false;
    const bool first_consume = consume_record(storage, context, &hp1, &hp2, &resume);
    assert(first_consume == !storage.present);
    assert(hp1 == 0U && hp2 == 0U);
    assert_no_resume(resume);
    storage.erase_commit_ok = true;
    assert(consume_record(storage, context, &hp1, &hp2, &resume));
    assert(!storage.present);
    // A reported failed write may nevertheless have left a valid visible record.
    // Its contents are usable only after the later durable erase/readback succeeds.
    if (fault == 1)
      assert(resume.hp_mask == 3U);
    else
      assert_no_resume(resume);
    assert(consume_record(storage, context, &hp1, &hp2, &resume));
    assert_no_resume(resume);
  }
}

void test_power_house_memory_roundtrip_and_current_inputs() {
  FakeStorage storage{};
  const auto record = make_resume_record();
  const auto context = matching_context(record);
  assert(persist_record(storage, record));
  ResumeState resume{};
  uint32_t hp1 = 0U, hp2 = 0U;
  assert(consume_record(storage, context, &hp1, &hp2, &resume));
  const auto& restored = resume.power_house;
  assert(handoff::valid_power_house_resume(restored));
  assert(handoff::may_restore_power_house(restored, resume.hp_mask != 0U, 23.05f, 23.0f));
  assert(!handoff::may_restore_power_house(restored, false, 23.05f, 23.0f));
  assert(!handoff::may_restore_power_house(restored, true, 23.05f, 22.0f));
  assert(!handoff::may_restore_power_house(restored, true, NAN, 23.0f));
  // Reproduce an above-setpoint cycle carried by demand/comfort memory, using the real demand calculation.
  oq_power_house::DemandInput input{60000U, 3.0f, -10.0f, 16.0f, 6000.0f, 23.05f, 23.0f, 0.0f, 1.0f, true};
  oq_power_house::DemandTuning tuning{0.5f, 3000.0f, 0.1f, 0.3f, 10.0f, 5.0f, 20};
  const auto lost = oq_power_house::decide_demand(input, tuning, {});
  const oq_power_house::DemandState retained{restored.last_w, input.now_ms - 1U, restored.comfort_memory_c};
  const auto resumed = oq_power_house::decide_demand(input, tuning, retained);
  assert(lost.valid && lost.requested_w == 0.0f);
  assert(resumed.valid && resumed.requested_w > 1499.0f && resumed.next.comfort_memory_c > 0.19f);
  input.water_limit_factor = 0.0f;
  assert(oq_power_house::decide_demand(input, tuning, retained).requested_w == 0.0f);
  input.water_limit_factor = 1.0f;
  input.rated_w = 750.0f;
  const auto current_limit = oq_power_house::decide_demand(input, tuning, retained);
  assert(current_limit.valid && current_limit.requested_w <= 750.0f);
  input.room_c = NAN;
  assert(!oq_power_house::decide_demand(input, tuning, retained).valid);
  assert(consume_record(storage, context, &hp1, &hp2, &resume));
  assert_no_resume(resume);

  for (const auto bad : {handoff::PowerHouseResumeState{NAN, 0.1f, 23}, {1500, 0.21f, 23}, {1500, 0.1f, NAN}}) {
    storage.record = make_resume_record();
    storage.record.power_house = bad;
    finalize_record(&storage.record);
    storage.present = true;
    assert(consume_record(storage, context, &hp1, &hp2, &resume));
    assert(resume.hp_mask == 3U);  // Invalid optional memory cannot create valid numeric state.
    assert(!handoff::valid_power_house_resume(resume.power_house));
  }
}

}  // namespace

int main() {
  test_power_house_memory_roundtrip_and_current_inputs();
  test_journal_resume_is_per_hp_durable_and_one_shot();
  test_resume_requires_exact_current_boot_context();
  test_only_completed_ota_can_resume_pending_first_boot();
  test_resume_legacy_partial_corrupt_and_conflicting_records_fail_closed();
  test_resume_consume_failure_injection_exposes_no_state();
  test_cold_and_crash_consume_retries_keep_the_original_reset_context();
  test_incomplete_boot_consume_denies_subsequent_software_restarts();
  test_resume_failed_persistence_and_retry_cannot_bypass_consume();
  test_only_exact_controlled_reboot_restores_credit();
  test_pending_ota_boot_uses_same_exact_handoff_policy();
  test_corrupt_or_partial_arm_never_restores_credit();
  test_replay_and_non_restart_paths_are_rejected();
  test_consume_failure_never_grants_in_memory_credit();
  test_storage_consume_clears_before_credit_and_cannot_replay();
  test_storage_consume_failure_injection_fails_closed();
  test_storage_arm_failure_injection_requires_commit_and_readback();
  test_failed_arm_with_visible_record_is_consumed_once_before_replay();
  test_uncontrolled_reset_consumes_without_credit();
  test_consume_commit_failure_blocks_then_recovers_once();
  test_image_and_configuration_mismatch_are_rejected();
  test_invalid_credit_bounds_fail_closed();
  return 0;
}
