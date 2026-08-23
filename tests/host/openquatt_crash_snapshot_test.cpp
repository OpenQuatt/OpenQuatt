#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "components/openquatt_log_history/OpenQuattCrashSnapshot.h"

using esphome::openquatt_log_history::CRASH_BACKTRACE_CAPACITY;
using esphome::openquatt_log_history::CRASH_BUILD_IDENTITY_ELF_SHA256_VALID;
using esphome::openquatt_log_history::CRASH_SNAPSHOT_ADDRESSES_FOREIGN;
using esphome::openquatt_log_history::CRASH_SNAPSHOT_BUILD_GUARD_CONFLICT;
using esphome::openquatt_log_history::CRASH_SNAPSHOT_CAPTURED_BY_CURRENT_BUILD;
using esphome::openquatt_log_history::CRASH_SNAPSHOT_ESPHOME_FOREIGN_BUILD;
using esphome::openquatt_log_history::CRASH_SNAPSHOT_FAULT_ADDR_VALID;
using esphome::openquatt_log_history::crash_snapshot_finalize;
using esphome::openquatt_log_history::crash_snapshot_is_pending;
using esphome::openquatt_log_history::crash_snapshot_is_valid;
using esphome::openquatt_log_history::CRASH_SNAPSHOT_OTHER_CORE_BACKTRACE_VALID;
using esphome::openquatt_log_history::CRASH_SNAPSHOT_PENDING;
using esphome::openquatt_log_history::CRASH_SNAPSHOT_RAW_CAUSE_VALID;
using esphome::openquatt_log_history::crash_snapshot_resolve_build_guard;
using esphome::openquatt_log_history::crash_snapshot_reuse_durable;
using esphome::openquatt_log_history::CrashExceptionType;
using esphome::openquatt_log_history::CrashSnapshot;
using esphome::openquatt_log_history::EspHomeCrashLogParser;

static void consume(EspHomeCrashLogParser* parser, const char* line) { assert(parser->consume(line)); }

static CrashSnapshot parse_same_build_dual_core() {
  EspHomeCrashLogParser parser;
  parser.reset();
  consume(&parser, "*** CRASH DETECTED ON PREVIOUS BOOT ***");
  consume(&parser, "Reason: Fault - LoadProhibited (cause 28)");
  consume(&parser, "Crashed core: 0");
  consume(&parser, "PC:  0x40381234  (fault location)");
  consume(&parser, "EXCVADDR: 0x00000004  (faulting address)");
  consume(&parser, "BT0: 0x40381234  (backtrace)");
  consume(&parser, "BT1: 0x42001234  (backtrace)");
  consume(&parser, "Other core (1) backtrace:");
  consume(&parser, "BT0: 0x40389999  (backtrace)");
  consume(&parser, "Use: addr2line -pfiaC -e firmware.elf 0x40381234 0x42001234");
  consume(&parser, "Other core: addr2line -pfiaC -e firmware.elf 0x40389999");

  CrashSnapshot snapshot{};
  assert(parser.finish(&snapshot));
  return snapshot;
}

static void test_same_build_dual_core() {
  const CrashSnapshot snapshot = parse_same_build_dual_core();
  assert(snapshot.exception_type == CrashExceptionType::FAULT);
  assert(std::strcmp(snapshot.exception_type_name, "Fault") == 0);
  assert(std::strcmp(snapshot.reason, "LoadProhibited") == 0);
  assert((snapshot.flags & CRASH_SNAPSHOT_RAW_CAUSE_VALID) != 0U);
  assert(snapshot.raw_cause == 28U);
  assert(snapshot.crashed_core == 0U);
  assert(snapshot.pc == 0x40381234U);
  assert((snapshot.flags & CRASH_SNAPSHOT_FAULT_ADDR_VALID) != 0U);
  assert(snapshot.fault_addr == 4U);
  assert(snapshot.crashed_core_backtrace.core == 0U);
  assert(snapshot.crashed_core_backtrace.count == 2U);
  assert(snapshot.crashed_core_backtrace.addresses[1] == 0x42001234U);
  assert((snapshot.flags & CRASH_SNAPSHOT_OTHER_CORE_BACKTRACE_VALID) != 0U);
  assert(snapshot.other_core_backtrace.core == 1U);
  assert(snapshot.other_core_backtrace.count == 1U);
  assert(snapshot.other_core_backtrace.addresses[0] == 0x40389999U);
}

static void test_foreign_lowercase_labels() {
  EspHomeCrashLogParser parser;
  parser.reset();
  consume(&parser, "*** CRASH DETECTED ON PREVIOUS BOOT ***");
  consume(&parser, "Reason: Fault - StoreProhibited (cause 29)");
  consume(&parser, "Crashed core: 1");
  consume(&parser, "Captured by a different firmware build; addresses belong to that build's ELF");
  consume(&parser, "pc: 0x42001000");
  consume(&parser, "excvaddr: 0x00000008");
  consume(&parser, "bt0: 0x42001000");
  consume(&parser, "bt1: 0x42002000");
  consume(&parser, "other core (0):");
  consume(&parser, "bt2: 0x40370000");

  CrashSnapshot snapshot{};
  assert(parser.finish(&snapshot));
  assert((snapshot.flags & CRASH_SNAPSHOT_ESPHOME_FOREIGN_BUILD) != 0U);
  assert(snapshot.crashed_core == 1U);
  assert(snapshot.other_core_backtrace.core == 0U);
  assert(snapshot.crashed_core_backtrace.addresses[0] == 0x42001000U);
  assert(snapshot.other_core_backtrace.addresses[0] == 0x40370000U);
}

static void test_abort_without_optional_fields() {
  EspHomeCrashLogParser parser;
  parser.reset();
  consume(&parser, "*** CRASH DETECTED ON PREVIOUS BOOT ***");
  consume(&parser, "Reason: Abort");
  consume(&parser, "Crashed core: 0");
  consume(&parser, "PC:  0x40380000  (fault location)");

  CrashSnapshot snapshot{};
  assert(parser.finish(&snapshot));
  assert(snapshot.exception_type == CrashExceptionType::ABORT);
  assert(snapshot.reason[0] == '\0');
  assert((snapshot.flags & CRASH_SNAPSHOT_RAW_CAUSE_VALID) == 0U);
  assert((snapshot.flags & CRASH_SNAPSHOT_FAULT_ADDR_VALID) == 0U);
  assert(snapshot.crashed_core_backtrace.count == 0U);
  assert(snapshot.other_core_backtrace.count == 0U);
}

static void test_malformed_input_fails_closed() {
  {
    EspHomeCrashLogParser parser;
    parser.reset();
    consume(&parser, "*** CRASH DETECTED ON PREVIOUS BOOT ***");
    assert(!parser.consume("Reason: Fault - LoadProhibited"));
  }
  {
    EspHomeCrashLogParser parser;
    parser.reset();
    consume(&parser, "*** CRASH DETECTED ON PREVIOUS BOOT ***");
    assert(!parser.consume("Reason: Mystery"));
  }
  {
    EspHomeCrashLogParser parser;
    parser.reset();
    consume(&parser, "*** CRASH DETECTED ON PREVIOUS BOOT ***");
    consume(&parser, "Reason: Abort");
    assert(!parser.consume("Crashed core: 2"));
  }
  {
    EspHomeCrashLogParser parser;
    parser.reset();
    consume(&parser, "*** CRASH DETECTED ON PREVIOUS BOOT ***");
    consume(&parser, "Reason: Abort");
    consume(&parser, "Crashed core: 0");
    consume(&parser, "PC: 0x40380000");
    assert(!parser.consume("Other core (0) backtrace:"));
  }
  {
    EspHomeCrashLogParser parser;
    parser.reset();
    consume(&parser, "*** CRASH DETECTED ON PREVIOUS BOOT ***");
    consume(&parser, "Reason: Fault - LoadProhibited (cause 28)");
    consume(&parser, "Crashed core: 0");
    CrashSnapshot snapshot{};
    assert(!parser.finish(&snapshot));
  }
  {
    EspHomeCrashLogParser parser;
    parser.reset();
    consume(&parser, "*** CRASH DETECTED ON PREVIOUS BOOT ***");
    consume(&parser, "Reason: Abort");
    consume(&parser, "Crashed core: 0");
    consume(&parser, "PC: 0x40380000");
    for (size_t index = 0U; index < CRASH_BACKTRACE_CAPACITY; ++index) {
      char line[48];
      std::snprintf(line, sizeof(line), "BT%zu: 0x%08X", index, static_cast<unsigned>(0x40380000U + index));
      consume(&parser, line);
    }
    assert(!parser.consume("BT16: 0x40380010"));
  }
}

static void test_crc_corruption_and_reboot_idempotence() {
  CrashSnapshot persisted = parse_same_build_dual_core();
  persisted.flags |= CRASH_SNAPSHOT_PENDING;
  persisted.crash_id = {0x10U, 0x11U, 0x12U, 0x13U, 0x14U, 0x15U, 0x46U, 0x17U,
                        0x98U, 0x19U, 0x1AU, 0x1BU, 0x1CU, 0x1DU, 0x1EU, 0x1FU};
  persisted.marker_fingerprint.fill(0xA5U);
  crash_snapshot_finalize(&persisted);
  assert(crash_snapshot_is_pending(persisted));

  CrashSnapshot after_reboot = parse_same_build_dual_core();
  after_reboot.flags |= CRASH_SNAPSHOT_PENDING;
  after_reboot.crash_id.fill(0xFFU);
  after_reboot.marker_fingerprint = persisted.marker_fingerprint;
  crash_snapshot_finalize(&after_reboot);
  assert(crash_snapshot_reuse_durable(persisted, &after_reboot));
  assert(after_reboot.crash_id == persisted.crash_id);
  assert(std::memcmp(&after_reboot, &persisted, sizeof(persisted)) == 0);

  CrashSnapshot corrupt = persisted;
  corrupt.pc ^= 1U;
  assert(!crash_snapshot_is_valid(corrupt));
  corrupt = persisted;
  corrupt.crc ^= 1U;
  assert(!crash_snapshot_is_valid(corrupt));
}

static CrashSnapshot build_guard_snapshot(const char* captured_elf, const char* current_elf, bool marked_foreign) {
  CrashSnapshot snapshot{};
  if (captured_elf != nullptr) {
    snapshot.captured_build.flags |= CRASH_BUILD_IDENTITY_ELF_SHA256_VALID;
    std::strcpy(snapshot.captured_build.elf_sha256, captured_elf);
  }
  if (current_elf != nullptr) {
    snapshot.current_build.flags |= CRASH_BUILD_IDENTITY_ELF_SHA256_VALID;
    std::strcpy(snapshot.current_build.elf_sha256, current_elf);
  }
  if (marked_foreign) snapshot.flags |= CRASH_SNAPSHOT_ESPHOME_FOREIGN_BUILD;
  return snapshot;
}

static void test_build_guard_conflicts_do_not_drop_capture() {
  static constexpr char ELF_A[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  static constexpr char ELF_B[] = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

  CrashSnapshot same = build_guard_snapshot(ELF_A, ELF_A, false);
  crash_snapshot_resolve_build_guard(&same);
  assert((same.flags & CRASH_SNAPSHOT_CAPTURED_BY_CURRENT_BUILD) != 0U);
  assert((same.flags & CRASH_SNAPSHOT_ADDRESSES_FOREIGN) == 0U);

  CrashSnapshot ambiguous_mismatch = build_guard_snapshot(ELF_A, ELF_B, false);
  crash_snapshot_resolve_build_guard(&ambiguous_mismatch);
  assert((ambiguous_mismatch.flags & CRASH_SNAPSHOT_CAPTURED_BY_CURRENT_BUILD) == 0U);
  assert((ambiguous_mismatch.flags & CRASH_SNAPSHOT_ADDRESSES_FOREIGN) != 0U);
  assert((ambiguous_mismatch.flags & CRASH_SNAPSHOT_BUILD_GUARD_CONFLICT) != 0U);
  assert(ambiguous_mismatch.captured_build.flags == 0U);

  CrashSnapshot ambiguous_foreign_mismatch = build_guard_snapshot(ELF_A, ELF_B, true);
  crash_snapshot_resolve_build_guard(&ambiguous_foreign_mismatch);
  assert((ambiguous_foreign_mismatch.flags & CRASH_SNAPSHOT_CAPTURED_BY_CURRENT_BUILD) == 0U);
  assert((ambiguous_foreign_mismatch.flags & CRASH_SNAPSHOT_ADDRESSES_FOREIGN) != 0U);
  assert((ambiguous_foreign_mismatch.flags & CRASH_SNAPSHOT_BUILD_GUARD_CONFLICT) != 0U);
  assert(ambiguous_foreign_mismatch.captured_build.flags == 0U);

  CrashSnapshot false_foreign = build_guard_snapshot(ELF_A, ELF_A, true);
  crash_snapshot_resolve_build_guard(&false_foreign);
  assert((false_foreign.flags & CRASH_SNAPSHOT_CAPTURED_BY_CURRENT_BUILD) == 0U);
  assert((false_foreign.flags & CRASH_SNAPSHOT_ADDRESSES_FOREIGN) != 0U);
  assert((false_foreign.flags & CRASH_SNAPSHOT_BUILD_GUARD_CONFLICT) != 0U);
  assert(false_foreign.captured_build.flags == 0U);

  CrashSnapshot unknown_foreign = build_guard_snapshot(nullptr, ELF_B, true);
  crash_snapshot_resolve_build_guard(&unknown_foreign);
  assert((unknown_foreign.flags & CRASH_SNAPSHOT_ADDRESSES_FOREIGN) != 0U);
  assert((unknown_foreign.flags & CRASH_SNAPSHOT_BUILD_GUARD_CONFLICT) == 0U);
}

int main() {
  test_same_build_dual_core();
  test_foreign_lowercase_labels();
  test_abort_without_optional_fields();
  test_malformed_input_fails_closed();
  test_crc_corruption_and_reboot_idempotence();
  test_build_guard_conflicts_do_not_drop_capture();
  return 0;
}
