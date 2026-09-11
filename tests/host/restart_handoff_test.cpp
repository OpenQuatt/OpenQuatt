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
using esphome::openquatt_incident_manager::restart_handoff::StorageReadResult;
using esphome::openquatt_incident_manager::restart_handoff::valid_record;

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

  StorageReadResult read(Record* target) const {
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
  record.state = esphome::openquatt_incident_manager::restart_handoff::kRecordRestartArmed;
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

Record make_ota_record() {
  Record record = make_record();
  record.state = esphome::openquatt_incident_manager::restart_handoff::kRecordOtaArmed;
  record.boot_partition_address = 0x410000U;
  record.hp2_credit_ms = MINIMUM_OFF_MS;
  finalize_record(&record);
  return record;
}

BootContext matching_context(const Record& record) {
  BootContext context{};
  context.software_reset = true;
  context.running_partition_matches_boot = true;
  context.image_valid = true;
  context.minimum_off_ms = record.minimum_off_ms;
  context.config_hash = record.config_hash;
  context.boot_partition_address = record.boot_partition_address;
  std::memcpy(context.image_hash, record.image_hash, sizeof(context.image_hash));
  return context;
}

void test_only_exact_controlled_restart_restores_credit() {
  const Record record = make_record();
  assert(valid_record(record));
  const BootContext context = matching_context(record);
  assert(may_restore_credit(record, context));
}

void test_controlled_ota_restores_on_selected_partition() {
  const Record record = make_ota_record();
  assert(valid_record(record));

  auto pending_context = matching_context(record);
  pending_context.image_valid = false;
  pending_context.image_pending_verify = true;
  pending_context.image_hash[0] ^= 0x5AU;
  assert(may_restore_credit(record, pending_context));

  auto changed_image_context = matching_context(record);
  changed_image_context.image_hash[0] ^= 0xA5U;
  assert(may_restore_credit(record, changed_image_context));

  // Reinstalling the exact same binary is still a controlled OTA when the
  // reboot lands on the inactive partition recorded before flashing.
  const auto same_image_context = matching_context(record);
  assert(may_restore_credit(record, same_image_context));
}

void test_ota_handoff_rejects_wrong_partition_or_uncontrolled_boot() {
  const Record record = make_ota_record();

  auto wrong_partition = matching_context(record);
  wrong_partition.image_pending_verify = true;
  wrong_partition.image_valid = false;
  wrong_partition.boot_partition_address++;
  assert(!may_restore_credit(record, wrong_partition));

  auto uncontrolled_reset = matching_context(record);
  uncontrolled_reset.image_pending_verify = true;
  uncontrolled_reset.image_valid = false;
  uncontrolled_reset.software_reset = false;
  assert(!may_restore_credit(record, uncontrolled_reset));
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

}  // namespace

int main() {
  test_only_exact_controlled_restart_restores_credit();
  test_controlled_ota_restores_on_selected_partition();
  test_ota_handoff_rejects_wrong_partition_or_uncontrolled_boot();
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
