#include <assert.h>

#include <map>
#include <vector>

#include "../../openquatt/includes/storage/oq_supply_calibration_migration_logic.h"

using namespace oq_supply_calibration;
using namespace oq_supply_calibration_migration;

struct FakeStorage {
  std::map<uint32_t, uint32_t> words;
  uint32_t durable[kRecordStorageWords]{};
  float offset{-0.37f};
  ReadResult offset_result{ReadResult::FOUND};
  ReadResult durable_result{ReadResult::FOUND};
  bool sync_ok{true};
  bool write_ok{true};
  bool acknowledge_without_persistence{false};
  bool corrupt_readback{false};
  uint32_t fail_erase{0U};
  uint32_t changed_word{0U};
  int writes{0};
  int offset_reads{0};
  std::vector<uint32_t> erased;

  void legacy(int32_t code = SOURCE_CIC) {
    words[kLegacySourceKey] = static_cast<uint32_t>(code);
    words[kLegacyFingerprintKey] = 12345U;
    words[kLegacyChecksumKey] = record_checksum(code, 12345U, offset);
  }
  ReadResult read_offset(float& output) {
    ++offset_reads;
    output = offset;
    return offset_result;
  }
  bool sync() { return sync_ok; }
  bool write_record(uint32_t key, const uint32_t (&record)[kRecordStorageWords]) {
    assert(key != 0U);
    ++writes;
    if (!write_ok) return false;
    if (!acknowledge_without_persistence) memcpy(durable, record, sizeof(durable));
    return true;
  }
  ReadResult read_record(uint32_t, uint32_t (&record)[kRecordStorageWords]) {
    memcpy(record, durable, sizeof(durable));
    if (corrupt_readback) record[kRecordChecksumIndex] ^= 1U;
    return durable_result;
  }
  bool erase_word(uint32_t key, ReadResult result, uint32_t expected) {
    if (key == fail_erase) return false;
    if (words.count(key) == 0U) return true;
    if (key == changed_word) words[key] ^= 1U;
    if (result != ReadResult::FOUND || words[key] != expected) return false;
    erased.push_back(key);
    words.erase(key);
    return true;
  }
};

Result attempt(FakeStorage& storage, uint32_t (&target)[kRecordStorageWords]) {
  const auto read = [&storage](uint32_t key) {
    return storage.words.count(key) != 0U ? ReadResult::FOUND : ReadResult::ABSENT;
  };
  // The runtime never dispatches a migration without a present source word.
  assert(storage.words.count(kLegacySourceKey) != 0U);
  const uint32_t code = storage.words[kLegacySourceKey];
  const ReadResult fingerprint_result = read(kLegacyFingerprintKey);
  const ReadResult checksum_result = read(kLegacyChecksumKey);
  const uint32_t fingerprint = fingerprint_result == ReadResult::FOUND ? storage.words[kLegacyFingerprintKey] : 0U;
  const uint32_t checksum = checksum_result == ReadResult::FOUND ? storage.words[kLegacyChecksumKey] : 0U;
  return migrate(storage, target, code, fingerprint_result, fingerprint, checksum_result, checksum);
}

int main() {
  // Every physical/configured source gets its own storage key and checksum.
  for (int32_t code = SOURCE_LOCAL_PT1000; code <= SOURCE_HA_INPUT; ++code) {
    FakeStorage storage;
    storage.legacy(code);
    uint32_t target[kRecordStorageWords]{};
    assert(attempt(storage, target) == Result::COMPLETE);
    assert(record_valid(load_record(target), code));
    assert(memcmp(target, storage.durable, sizeof(target)) == 0);
    assert(storage.words.empty());
    assert(storage.erased.back() == kLegacySourceKey);
  }

  // A new user calibration in RAM wins over stale legacy/durable values.
  {
    FakeStorage storage;
    storage.legacy();
    uint32_t target[kRecordStorageWords]{};
    const SourceIdentity new_source{SOURCE_CIC, 45678U, true};
    assert(store_record(target, new_source, 0.61f));
    assert(attempt(storage, target) == Result::COMPLETE);
    assert(storage.offset_reads == 0);
    assert(load_record(storage.durable).offset_c == 0.61f);
    assert(load_record(target).fingerprint == new_source.fingerprint);
  }

  // A restoring-global setup read failure can leave RAM zero while flash has a
  // newer valid per-source record. Recover that record instead of old legacy.
  {
    FakeStorage storage;
    storage.legacy();
    assert(store_record(storage.durable, {SOURCE_CIC, 45678U, true}, 0.61f));
    uint32_t target[kRecordStorageWords]{};
    storage.write_ok = false;  // Recovery needs no replacement space either.
    assert(attempt(storage, target) == Result::COMPLETE);
    assert(storage.offset_reads == 0);
    assert(storage.writes == 0);
    assert(load_record(target).fingerprint == 45678U);
    assert(load_record(target).offset_c == 0.61f);
    assert(memcmp(target, storage.durable, sizeof(target)) == 0);
    assert(storage.words.empty());
  }

  // Failed reads, flushes, writes and durability checks must keep every old key.
  for (int failure = 0; failure < 7; ++failure) {
    FakeStorage storage;
    storage.legacy();
    uint32_t target[kRecordStorageWords]{};
    if (failure == 0) storage.offset_result = ReadResult::ERROR;
    if (failure == 1) storage.sync_ok = false;
    if (failure == 2) storage.write_ok = false;
    if (failure == 3) storage.acknowledge_without_persistence = true;
    if (failure == 4) storage.corrupt_readback = true;
    if (failure == 5) storage.durable_result = ReadResult::ERROR;
    if (failure == 6) storage.durable_result = ReadResult::ABSENT;
    assert(attempt(storage, target) == Result::RETRY);
    assert(storage.words.size() == 3U);
    assert(storage.erased.empty());
    storage.offset_result = ReadResult::FOUND;
    storage.sync_ok = storage.write_ok = true;
    storage.acknowledge_without_persistence = storage.corrupt_readback = false;
    storage.durable_result = ReadResult::FOUND;
    assert(attempt(storage, target) == Result::COMPLETE);
    assert(storage.words.empty());
  }

  // Full NVS: a matching durable target needs no replacement allocation.
  {
    FakeStorage storage;
    storage.legacy();
    uint32_t target[kRecordStorageWords]{};
    assert(store_record(target, {SOURCE_CIC, 12345U, true}, storage.offset));
    memcpy(storage.durable, target, sizeof(target));
    storage.write_ok = false;
    assert(attempt(storage, target) == Result::COMPLETE);
    assert(storage.writes == 0);
    assert(storage.words.empty());
  }

  // An unexpected durable target type/length is kept, without seeding RAM that
  // would allow the restoring-global poller to overwrite it later.
  {
    FakeStorage storage;
    storage.legacy();
    storage.durable_result = ReadResult::UNEXPECTED;
    uint32_t target[kRecordStorageWords]{};
    assert(attempt(storage, target) == Result::KEPT);
    assert(!record_present(load_record(target)));
    assert(storage.words.size() == 3U);
    assert(storage.writes == 0);
  }

  // A user can recalibrate between retries; preserve their new RAM value.
  {
    FakeStorage storage;
    storage.legacy();
    storage.write_ok = false;
    uint32_t target[kRecordStorageWords]{};
    assert(attempt(storage, target) == Result::RETRY);
    assert(store_record(target, {SOURCE_CIC, 98765U, true}, -0.52f));
    storage.write_ok = true;
    assert(attempt(storage, target) == Result::COMPLETE);
    assert(load_record(storage.durable).fingerprint == 98765U);
    assert(load_record(storage.durable).offset_c == -0.52f);
  }

  // A restart during each erase boundary restores the durable target and can
  // finish even with missing fingerprint/checksum words. Source is always last.
  for (uint32_t boundary : {kLegacyFingerprintKey, kLegacyChecksumKey, kLegacySourceKey}) {
    FakeStorage storage;
    storage.legacy();
    storage.fail_erase = boundary;
    uint32_t target[kRecordStorageWords]{};
    assert(attempt(storage, target) == Result::RETRY);
    assert(storage.words.count(kLegacySourceKey) == 1U);
    uint32_t restored[kRecordStorageWords];
    memcpy(restored, storage.durable, sizeof(restored));
    storage.fail_erase = 0U;
    assert(attempt(storage, restored) == Result::COMPLETE);
    assert(storage.words.empty());
  }

  // Default zero records carry no calibration; cleanup is also restart-safe.
  {
    FakeStorage storage;
    storage.words = {{kLegacySourceKey, 0U}, {kLegacyFingerprintKey, 0U}, {kLegacyChecksumKey, 0U}};
    storage.fail_erase = kLegacyChecksumKey;
    uint32_t target[kRecordStorageWords]{};
    assert(attempt(storage, target) == Result::RETRY);
    assert(storage.writes == 0);
    storage.fail_erase = 0U;
    assert(attempt(storage, target) == Result::COMPLETE);
    assert(storage.words.empty());
  }

  // Malformed/ambiguous input cannot activate a target or trigger cleanup.
  for (int malformed = 0; malformed < 6; ++malformed) {
    FakeStorage storage;
    storage.legacy();
    uint32_t target[kRecordStorageWords]{};
    if (malformed == 0) storage.words[kLegacySourceKey] = 123U;
    if (malformed == 1) storage.words[kLegacyChecksumKey] ^= 1U;
    if (malformed == 2) storage.offset = 2.01f;
    if (malformed == 3) storage.offset = NAN;
    if (malformed == 4) storage.offset_result = ReadResult::ABSENT;
    if (malformed == 5) storage.words.erase(kLegacyFingerprintKey);
    assert(attempt(storage, target) == Result::KEPT);
    assert(!record_present(load_record(target)));
    assert(storage.erased.empty());
    assert(storage.writes == 0);
  }
  {
    FakeStorage storage;
    storage.legacy();
    uint32_t target[kRecordStorageWords]{};
    const uint32_t code = storage.words[kLegacySourceKey];
    for (ReadResult result : {ReadResult::ERROR, ReadResult::UNEXPECTED}) {
      assert(migrate(storage, target, code, result, 0U, ReadResult::FOUND, 0U) ==
             (result == ReadResult::ERROR ? Result::RETRY : Result::KEPT));
      assert(storage.erased.empty());
    }
  }

  // Guarded erasure detects changes after the snapshot instead of deleting them.
  {
    FakeStorage storage;
    storage.legacy();
    storage.changed_word = kLegacyFingerprintKey;
    uint32_t target[kRecordStorageWords]{};
    assert(attempt(storage, target) == Result::RETRY);
    assert(storage.words.size() == 3U);
    assert(storage.erased.empty());
    assert(record_valid(load_record(storage.durable), SOURCE_CIC));
  }
  return 0;
}
