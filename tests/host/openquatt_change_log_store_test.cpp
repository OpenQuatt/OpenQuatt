#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "components/openquatt_change_log/OpenQuattChangeLogStore.h"

using namespace esphome::openquatt_change_log;

namespace {

constexpr uint32_t kAt = 1762329600U;  // 2025-11-05

enum class Fault { NONE, ERASE, WRITE, TORN_WRITE, FALSE_ERASE, READ };

// NOR semantics: erase sets bytes to 0xFF and a write can only clear bits.
struct FakeFlash {
  uint8_t bytes[SECTOR_COUNT * SECTOR_SIZE];
  Fault fault = Fault::NONE;
  int erases = 0;
  FakeFlash() { memset(bytes, 0xFF, sizeof(bytes)); }

  bool read(size_t offset, void* data, size_t size) {
    if (fault == Fault::READ || offset + size > sizeof(bytes)) return false;
    memcpy(data, bytes + offset, size);
    return true;
  }
  bool erase_sector(size_t sector) {
    if (fault == Fault::ERASE || sector >= SECTOR_COUNT) return false;
    ++erases;
    if (fault != Fault::FALSE_ERASE) memset(bytes + (sector * SECTOR_SIZE), 0xFF, SECTOR_SIZE);
    return true;
  }
  bool write(size_t offset, const void* data, size_t size) {
    if (offset + size > sizeof(bytes)) return false;
    const size_t written = fault == Fault::TORN_WRITE ? size / 2U : size;
    const auto* source = static_cast<const uint8_t*>(data);
    for (size_t index = 0U; index < written; ++index) bytes[offset + index] &= source[index];
    return fault != Fault::WRITE && fault != Fault::TORN_WRITE;
  }
};

ChangeLogText text(const char* value) { return ChangeLogText{value, strlen(value)}; }

ChangeLogRecord make(uint32_t seq, const char* label = "Minimale koel-aanvoer", const char* from = "18.0 °C",
                     const char* to = "20.0 °C") {
  ChangeLogRecord record{};
  assert(encode_record(seq, kAt + seq, text(label), text(from), text(to), &record));
  return record;
}

std::vector<uint32_t> load_sequences(ChangeLogFlashStore<FakeFlash>& store) {
  std::vector<uint32_t> sequences;
  assert(store.load([&](const ChangeLogRecord& record) { sequences.push_back(record.seq); }));
  return sequences;
}

void append_range(ChangeLogFlashStore<FakeFlash>& store, uint32_t first, uint32_t last) {
  for (uint32_t seq = first; seq <= last; ++seq) assert(store.append(make(seq)));
}

void test_record_round_trip_and_text_rules() {
  ChangeLogRecord record = make(7U, "Stille modus", "Uit", "Schema");
  ChangeLogEntry entry{};
  assert(decode_record(record, &entry));
  assert(entry.seq == 7U && entry.at == kAt + 7U);
  assert(strcmp(entry.label, "Stille modus") == 0 && strcmp(entry.from, "Uit") == 0 && strcmp(entry.to, "Schema") == 0);

  // Empty values are allowed, an empty label is not.
  assert(encode_record(1U, kAt, text("Label"), text(""), text(""), &record));
  assert(!encode_record(1U, kAt, text(""), text("a"), text("b"), &record));

  const std::string exact(TEXT_LIMIT - 2U, 'x');
  assert(encode_record(1U, kAt, text(exact.c_str()), text("a"), text("b"), &record));
  assert(decode_record(record, nullptr));
  assert(!encode_record(1U, kAt, text(exact.c_str()), text("ab"), text("b"), &record));

  const char* rejected[] = {"tab\there",        "line\nbreak", "\x7f",     "\xc0\x80",     "\xed\xa0\x80", "\xe2\x82",
                            "\xf4\x90\x80\x80", "\xff",        "\xc2\x85", "\xe2\x80\xae", "\xe2\x81\xa6"};
  for (const char* value : rejected) {
    assert(!encode_record(1U, kAt, text(value), text(""), text(""), &record));
    assert(!encode_record(1U, kAt, text("Label"), text(value), text(""), &record));
  }
  assert(
      encode_record(1U, kAt, text("Temperatuur \xe2\x80\x93 \xf0\x9f\x94\xa5"), text("\xc2\xb0"), text(""), &record));
  // The ellipsis the web UI uses for shortened text stays allowed.
  assert(encode_record(1U, kAt, text("Lange naam\xe2\x80\xa6"), text("\xc2\xa0"), text(""), &record));

  assert(!encode_record(0U, kAt, text("Label"), text(""), text(""), &record));
  assert(!encode_record(UINT32_MAX, kAt, text("Label"), text(""), text(""), &record));
  assert(!encode_record(1U, MIN_TIMESTAMP_S - 1U, text("Label"), text(""), text(""), &record));
  assert(!encode_record(1U, MAX_TIMESTAMP_S, text("Label"), text(""), text(""), &record));

  record = make(3U);
  record.text[0] = 'Q';
  assert(!decode_record(record, nullptr));
  record = make(3U);
  record.text_length = static_cast<uint8_t>(record.text_length - 1U);
  record.checksum = record_checksum(record);
  assert(!decode_record(record, nullptr));
}

void test_timestamp_parser() {
  uint32_t at = 0U;
  assert(parse_timestamp("1762329600", 10U, &at) && at == 1762329600U);
  assert(!parse_timestamp("", 0U, &at));
  assert(!parse_timestamp("17623a9600", 10U, &at));
  assert(!parse_timestamp("-176232960", 10U, &at));
  assert(!parse_timestamp("99999999999", 11U, &at));
  assert(!parse_timestamp("4294967295", 10U, &at));
  assert(!parse_timestamp("0", 1U, &at));
}

void test_fresh_flash_starts_at_the_first_sector() {
  FakeFlash flash;
  ChangeLogFlashStore<FakeFlash> store(flash);
  assert(load_sequences(store).empty());
  assert(store.healthy() && store.last_sequence() == 0U);
  assert(store.append(make(1U)));
  assert(flash.erases == 1);

  ChangeLogFlashStore<FakeFlash> rebooted(flash);
  const auto sequences = load_sequences(rebooted);
  assert(sequences.size() == 1U && sequences[0] == 1U && rebooted.last_sequence() == 1U);
}

void test_rotation_keeps_more_than_capacity_and_replays_in_order() {
  FakeFlash flash;
  ChangeLogFlashStore<FakeFlash> store(flash);
  load_sequences(store);
  const uint32_t total = static_cast<uint32_t>((RECORDS_PER_SECTOR * SECTOR_COUNT) + 30U);
  append_range(store, 1U, total);
  assert(flash.erases == 4);

  ChangeLogFlashStore<FakeFlash> rebooted(flash);
  const auto sequences = load_sequences(rebooted);
  assert(sequences.size() == (RECORDS_PER_SECTOR * (SECTOR_COUNT - 1U)) + 30U);
  assert(sequences.size() >= CAPACITY);
  for (size_t index = 1U; index < sequences.size(); ++index) assert(sequences[index] == sequences[index - 1U] + 1U);
  assert(sequences.back() == total && rebooted.last_sequence() == total);

  // Continuing after the reboot appends to the head sector instead of erasing it again.
  assert(rebooted.append(make(total + 1U)));
  assert(flash.erases == 4);

  ChangeLogRecord storage[CAPACITY];
  ChangeLogRing ring;
  ring.bind(storage, CAPACITY);
  ChangeLogFlashStore<FakeFlash> replay(flash);
  assert(replay.load([&](const ChangeLogRecord& record) { ring.push(record); }));
  assert(ring.size() == CAPACITY);
  assert(ring.newest(0U).seq == total + 1U && ring.newest(CAPACITY - 1U).seq == total + 2U - CAPACITY);
}

void test_full_head_rotates_after_reboot() {
  FakeFlash flash;
  ChangeLogFlashStore<FakeFlash> store(flash);
  load_sequences(store);
  append_range(store, 1U, static_cast<uint32_t>(RECORDS_PER_SECTOR));
  assert(flash.erases == 1);

  ChangeLogFlashStore<FakeFlash> rebooted(flash);
  assert(load_sequences(rebooted).size() == RECORDS_PER_SECTOR);
  assert(rebooted.append(make(static_cast<uint32_t>(RECORDS_PER_SECTOR) + 1U)));
  assert(flash.erases == 2);
  ChangeLogFlashStore<FakeFlash> again(flash);
  assert(load_sequences(again).size() == RECORDS_PER_SECTOR + 1U);
}

void test_torn_record_is_skipped_and_never_rewritten() {
  FakeFlash flash;
  ChangeLogFlashStore<FakeFlash> store(flash);
  load_sequences(store);
  append_range(store, 1U, 5U);
  flash.fault = Fault::TORN_WRITE;
  assert(!store.append(make(6U)));
  assert(!store.healthy() && !store.append(make(7U)));

  // Power loss: the torn slot stays used, and the next boot appends after it.
  flash.fault = Fault::NONE;
  ChangeLogFlashStore<FakeFlash> rebooted(flash);
  const auto sequences = load_sequences(rebooted);
  assert(sequences.size() == 5U && rebooted.skipped_records() == 1U);
  assert(rebooted.append(make(6U)));
  ChangeLogFlashStore<FakeFlash> again(flash);
  const auto after = load_sequences(again);
  assert(after.size() == 6U && after.back() == 6U && again.skipped_records() == 1U);
}

void test_io_failures_disable_persistence() {
  {
    FakeFlash flash;
    ChangeLogFlashStore<FakeFlash> store(flash);
    load_sequences(store);
    flash.fault = Fault::ERASE;
    assert(!store.append(make(1U)) && !store.healthy());
  }
  {
    FakeFlash flash;
    ChangeLogFlashStore<FakeFlash> store(flash);
    load_sequences(store);
    append_range(store, 1U, static_cast<uint32_t>(RECORDS_PER_SECTOR * SECTOR_COUNT));
    // An erase that reports success but leaves old data fails header verification.
    flash.fault = Fault::FALSE_ERASE;
    assert(!store.append(make(static_cast<uint32_t>(RECORDS_PER_SECTOR * SECTOR_COUNT) + 1U)));
    assert(!store.healthy());
  }
  {
    FakeFlash flash;
    ChangeLogFlashStore<FakeFlash> store(flash);
    load_sequences(store);
    flash.fault = Fault::WRITE;
    assert(!store.append(make(1U)) && !store.healthy());
  }
  {
    FakeFlash flash;
    ChangeLogFlashStore<FakeFlash> store(flash);
    load_sequences(store);
    append_range(store, 1U, 3U);
    flash.fault = Fault::READ;
    ChangeLogFlashStore<FakeFlash> rebooted(flash);
    assert(!rebooted.load([](const ChangeLogRecord&) {}));
    assert(!rebooted.healthy() && !rebooted.append(make(4U)));
  }
}

void test_corrupt_sector_header_is_ignored() {
  FakeFlash flash;
  ChangeLogFlashStore<FakeFlash> store(flash);
  load_sequences(store);
  append_range(store, 1U, static_cast<uint32_t>(RECORDS_PER_SECTOR * 2U));
  flash.bytes[0] = 0x00U;  // first sector header magic

  ChangeLogFlashStore<FakeFlash> rebooted(flash);
  const auto sequences = load_sequences(rebooted);
  assert(sequences.size() == RECORDS_PER_SECTOR && sequences.front() == RECORDS_PER_SECTOR + 1U);
}

void test_sequence_order_survives_wraparound() {
  assert(sequence_is_newer(1U, UINT32_MAX));
  assert(!sequence_is_newer(UINT32_MAX, 1U));
  assert(!sequence_is_newer(5U, 5U));

  // Sector headers near the counter limit still replay oldest first.
  FakeFlash flash;
  const uint32_t header_sequences[SECTOR_COUNT] = {0U, UINT32_MAX - 1U, UINT32_MAX};
  for (size_t sector = 0U; sector < SECTOR_COUNT; ++sector) {
    const ChangeLogSectorHeader header = make_sector_header(header_sequences[sector]);
    assert(flash.write(ChangeLogFlashStore<FakeFlash>::sector_offset(sector), &header, sizeof(header)));
  }
  const uint32_t record_sequences[SECTOR_COUNT] = {30U, 10U, 20U};
  for (size_t sector = 0U; sector < SECTOR_COUNT; ++sector) {
    const ChangeLogRecord record = make(record_sequences[sector]);
    assert(flash.write(ChangeLogFlashStore<FakeFlash>::record_offset(sector, 0U), &record, sizeof(record)));
  }
  ChangeLogFlashStore<FakeFlash> store(flash);
  const auto sequences = load_sequences(store);
  assert(sequences.size() == 3U && sequences[0] == 10U && sequences[1] == 20U && sequences[2] == 30U);
  assert(store.append(make(31U)));
  assert(flash.erases == 0);
}

void test_ring_and_queue() {
  ChangeLogRecord ring_storage[4];
  ChangeLogRing ring;
  ring.bind(ring_storage, 4U);
  for (uint32_t seq = 1U; seq <= 6U; ++seq) ring.push(make(seq));
  assert(ring.size() == 4U && ring.newest(0U).seq == 6U && ring.newest(3U).seq == 3U);

  ChangeLogRecord queue_storage[3];
  ChangeLogQueue queue;
  queue.bind(queue_storage, 3U);
  assert(queue.push(make(1U)) && queue.push(make(2U)) && queue.push(make(3U)));
  assert(queue.full() && !queue.push(make(4U)));
  ChangeLogRecord record{};
  assert(queue.pop(&record) && record.seq == 1U);
  assert(queue.push(make(4U)));
  assert(queue.pop(&record) && record.seq == 2U);
  assert(queue.pop(&record) && record.seq == 3U);
  assert(queue.pop(&record) && record.seq == 4U);
  assert(!queue.pop(&record) && queue.size() == 0U);
}

void test_rate_limiter_allows_a_burst_then_refills_slowly() {
  ChangeLogRateLimiter limiter;
  const uint32_t start = UINT32_MAX - 1000U;  // crosses the millis() wrap
  for (uint32_t index = 0U; index < ChangeLogRateLimiter::BURST; ++index) assert(limiter.try_take(start));
  assert(!limiter.try_take(start + 1000U));
  assert(limiter.try_take(start + ChangeLogRateLimiter::REFILL_MS));
  assert(!limiter.try_take(start + ChangeLogRateLimiter::REFILL_MS + 10U));
  const uint32_t later = start + (ChangeLogRateLimiter::REFILL_MS * 100U);
  for (uint32_t index = 0U; index < ChangeLogRateLimiter::BURST; ++index) assert(limiter.try_take(later));
  assert(!limiter.try_take(later));
}

void test_json_escapes_text_and_fits_the_worst_case() {
  ChangeLogRecord storage[CAPACITY];
  ChangeLogRing ring;
  ring.bind(storage, CAPACITY);
  ring.push(make(1U, "Old", "1", "2"));
  ring.push(make(2U, "Quote \" and \\ slash", "\xc2\xb0", ""));
  char buffer[512];
  const size_t length = write_change_log_json(buffer, sizeof(buffer), ring, true);
  assert(length == strlen(buffer));
  const char* expected =
      "{\"ok\":true,\"capacity\":100,\"persistent\":true,\"entries\":["
      "{\"seq\":2,\"at\":1762329602,\"label\":\"Quote \\\" and \\\\ slash\",\"from\":\"\xc2\xb0\",\"to\":\"\"},"
      "{\"seq\":1,\"at\":1762329601,\"label\":\"Old\",\"from\":\"1\",\"to\":\"2\"}]}";
  assert(strcmp(buffer, expected) == 0);
  assert(write_change_log_json(buffer, 64U, ring, true) == 0U);

  ChangeLogRing empty;
  empty.bind(storage, CAPACITY);
  assert(write_change_log_json(buffer, sizeof(buffer), empty, false) > 0U);
  assert(strcmp(buffer, "{\"ok\":true,\"capacity\":100,\"persistent\":false,\"entries\":[]}") == 0);

  const std::string quotes(TEXT_LIMIT - 2U, '"');
  for (uint32_t index = 0U; index < CAPACITY; ++index) {
    ChangeLogRecord record{};
    assert(encode_record(UINT32_MAX - 1U - index, MAX_TIMESTAMP_S - 1U, text(quotes.c_str()), text("\\"), text("\""),
                         &record));
    ring.push(record);
  }
  std::vector<char> large(JSON_BUFFER_SIZE);
  const size_t worst = write_change_log_json(large.data(), large.size(), ring, false);
  assert(worst > 0U && worst < JSON_BUFFER_SIZE);
}

}  // namespace

int main() {
  test_record_round_trip_and_text_rules();
  test_timestamp_parser();
  test_fresh_flash_starts_at_the_first_sector();
  test_rotation_keeps_more_than_capacity_and_replays_in_order();
  test_full_head_rotates_after_reboot();
  test_torn_record_is_skipped_and_never_rewritten();
  test_io_failures_disable_persistence();
  test_corrupt_sector_header_is_ignored();
  test_sequence_order_survives_wraparound();
  test_ring_and_queue();
  test_rate_limiter_allows_a_burst_then_refills_slowly();
  test_json_escapes_text_and_fits_the_worst_case();
  puts("openquatt_change_log_store_test: ok");
  return 0;
}
