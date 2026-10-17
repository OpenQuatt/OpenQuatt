#include "components/openquatt_partition_migration/migration_engine.h"
#include "components/openquatt_partition_migration/partition_tables.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <stdexcept>
#include <vector>

using namespace esphome::openquatt_partition_migration;
struct PowerLoss {};
class MemoryFlash : public Flash {
 public:
  std::vector<uint8_t> bytes = std::vector<uint8_t>(0x1000000, 0xFF);
  int mutation = 0;
  int cut = -1;
  bool partial = false;
  bool fail_read = false, fail_write = false, fail_erase = false, fail_confirm = false;
  bool drop_done = false;
  bool confirm_image() override { return !fail_confirm; }
  bool read(uint32_t address, void* data, uint32_t size) override {
    if (fail_read) return false;
    assert(address + size <= bytes.size());
    std::memcpy(data, bytes.data() + address, size);
    return true;
  }
  bool write(uint32_t address, const void* data, uint32_t size) override {
    if (fail_write) return false;
    if (drop_done && (address == 0x1C000 + offsetof(Journal, done) || address == 0x1D000 + offsetof(Journal, done)))
      return true;  // Lost persistence despite a successful write acknowledgement.
    assert(address + size <= bytes.size());
    const auto* source = static_cast<const uint8_t*>(data);
    bool interrupted = ++mutation == cut;
    uint32_t count = interrupted && partial ? size / 2 : size;
    for (uint32_t i = 0; i < count; ++i) {
      assert((bytes[address + i] & source[i]) == source[i]);
      bytes[address + i] &= source[i];
    }
    if (interrupted) throw PowerLoss{};
    return true;
  }
  bool erase(uint32_t address, uint32_t size) override {
    if (fail_erase) return false;
    assert(address % 0x1000 == 0 && size % 0x1000 == 0);
    for (uint32_t offset = 0; offset < size; offset += 4096) {
      bool interrupted = ++mutation == cut;
      uint32_t count = interrupted && partial ? 2048 : 4096;
      std::fill_n(bytes.data() + address + offset, count, 0xFF);
      if (interrupted) throw PowerLoss{};
    }
    return true;
  }
};

static constexpr uint32_t IMAGE_SIZE = 0x1000;
static MemoryFlash fixture(uint32_t running) {
  MemoryFlash flash;
  std::copy(OLD_TABLE.begin(), OLD_TABLE.end(), flash.bytes.begin() + 0x8000);
  std::fill_n(flash.bytes.data(), 0x8000, 0xA5);
  std::fill_n(flash.bytes.data() + 0xA20000, 0x5E0000, 0x5A);
  for (uint32_t i = 0; i < 0x9000; ++i) flash.bytes[0x9000 + i] = static_cast<uint8_t>(i * 17U + 13U);
  for (uint32_t i = 0; i < IMAGE_SIZE; ++i) flash.bytes[running + i] = static_cast<uint8_t>(i * 31U + 7U);
  return flash;
}
static uint32_t image_crc(const MemoryFlash& flash, uint32_t running) {
  return crc_update(UINT32_MAX, flash.bytes.data() + running, IMAGE_SIZE);
}
static Result run(MemoryFlash& flash, uint32_t running, uint32_t crc, unsigned checkpoint = 0) {
  Engine engine(flash, OLD_TABLE.data(), NEW_TABLE.data(), running, IMAGE_SIZE, crc, checkpoint);
  return engine.run();
}
static void assert_nvs(const MemoryFlash& flash, const std::vector<uint8_t>& before) {
  assert(std::equal(before.begin(), before.end(), flash.bytes.begin() + 0x9000));
}
static void assert_protected(const MemoryFlash& flash, const MemoryFlash& before, uint32_t running) {
  assert(std::equal(before.bytes.begin(), before.bytes.begin() + 0x8000, flash.bytes.begin()));
  assert(std::equal(before.bytes.begin() + 0xA20000, before.bytes.end(), flash.bytes.begin() + 0xA20000));
  assert(std::equal(before.bytes.begin() + running, before.bytes.begin() + running + 0x500000,
                    flash.bytes.begin() + running));
}
static void assert_completed(const MemoryFlash& flash, const MemoryFlash& before) {
  assert(std::equal(NEW_TABLE.begin(), NEW_TABLE.end(), flash.bytes.begin() + 0x8000));
  assert(std::equal(before.bytes.begin() + 0x9000, before.bytes.begin() + 0xF000, flash.bytes.begin() + 0x9000));
  assert(std::equal(before.bytes.begin() + 0xF000, before.bytes.begin() + 0x11000, flash.bytes.begin() + 0x19000));
  assert(std::equal(before.bytes.begin() + 0x11000, before.bytes.begin() + 0x12000, flash.bytes.begin() + 0x1B000));
}

int main() {
  assert(!writable_region(0, 4096, 0x520000, IMAGE_SIZE, true));
  assert(!writable_region(0x9000, 0x6000, 0x520000, IMAGE_SIZE, true));
  assert(!writable_region(0xF000, 256, 0x520000, IMAGE_SIZE, false));
  assert(!writable_region(0x20000, IMAGE_SIZE, 0x20000, IMAGE_SIZE, true));
  assert(!writable_region(0x520000, IMAGE_SIZE, 0x520000, IMAGE_SIZE, false));
  assert(!writable_region(0xA20000, 4096, 0x520000, IMAGE_SIZE, true));
  assert(!writable_region(UINT32_MAX - 2, 100, 0x520000, IMAGE_SIZE, true));
  assert(writable_region(0x8000, 4096, 0x20000, IMAGE_SIZE, true));
  assert(writable_region(0xF000, 0xA000, 0x20000, IMAGE_SIZE, true));
  assert(writable_region(0x20000, IMAGE_SIZE, 0x520000, IMAGE_SIZE, false));
  unsigned cuts = 0, recoveries = 0, usb_boundaries = 0;
  for (uint32_t slot : {0x20000U, 0x520000U}) {
    MemoryFlash before = fixture(slot), normal = before;
    const uint32_t crc = image_crc(before, slot);
    assert(run(normal, slot, crc) == Result::REBOOT);
    assert_completed(normal, before);
    assert_protected(normal, before, slot);
    int mutations = normal.mutation;
    assert(run(normal, slot, crc) == Result::READY);
    assert(normal.mutation == mutations);
    // A later full release has a different image CRC. Completed migration must
    // still be a no-op, including after ordinary NVS/OTA metadata changes.
    normal.bytes[slot + 128] ^= 1;
    assert(run(normal, slot, image_crc(normal, slot)) == Result::READY);
    assert(normal.mutation == mutations);
    // A fresh install already has the target table and no migration journals.
    MemoryFlash fresh = normal;
    std::fill_n(fresh.bytes.data() + 0x1C000, 0x2000, 0xFF);
    assert(run(fresh, slot, image_crc(fresh, slot)) == Result::READY);
    assert(fresh.mutation == mutations);
    // Ordinary use may change NVS/otadata after DONE. Never erase/replay it.
    normal.bytes[0x9100] ^= 1;
    normal.bytes[0x19000] ^= 1;
    assert(run(normal, slot, image_crc(normal, slot)) == Result::READY);
    assert(normal.mutation == mutations);
    for (unsigned checkpoint : {1U, 2U, 3U}) {
      MemoryFlash reset = before;
      assert(run(reset, slot, crc, checkpoint) == Result::INJECTED_RESET);
      uint32_t recovery_slot = checkpoint == 2 ? 0x20000 : slot;
      if (checkpoint == 2) {
        // Old bootloader can recreate old otadata while selecting app0.
        std::fill_n(reset.bytes.data() + 0xF000, 32, 0xA5);
      }
      assert(run(reset, recovery_slot, crc, checkpoint) == Result::REBOOT);
      assert_completed(reset, before);
      assert(run(reset, recovery_slot, crc, checkpoint) == Result::READY);
    }
    std::vector<uint8_t> original_nvs(before.bytes.begin() + 0x9000, before.bytes.begin() + 0xF000);
    // Exhaust every mutation boundary, including torn writes/erases. Recovery
    // may fail closed at a corrupted journal/table; original NVS must survive.
    for (int cut = 1; cut <= mutations; ++cut) {
      for (bool partial : {false, true}) {
        MemoryFlash interrupted = before;
        interrupted.cut = cut;
        interrupted.partial = partial;
        try {
          run(interrupted, slot, crc);
          assert(false);
        } catch (const PowerLoss&) {
        }
        assert_nvs(interrupted, original_nvs);
        assert_protected(interrupted, before, slot);
        interrupted.cut = -1;
        bool old = std::equal(OLD_TABLE.begin(), OLD_TABLE.end(), interrupted.bytes.begin() + 0x8000);
        bool next = std::equal(NEW_TABLE.begin(), NEW_TABLE.end(), interrupted.bytes.begin() + 0x8000);
        uint32_t boot = slot;
        if (old && interrupted.bytes[0xF000] == 0xFF) boot = 0x20000;
        Result outcome = run(interrupted, boot, crc);
        if (outcome == Result::REBOOT || outcome == Result::READY) {
          assert_completed(interrupted, before);
          ++recoveries;
        } else {
          assert(outcome == Result::FAILED);
          ++usb_boundaries;
        }
        if (!old && !next) assert(outcome == Result::FAILED);
        assert_nvs(interrupted, original_nvs);
        assert_protected(interrupted, before, slot);
        ++cuts;
      }
    }
    MemoryFlash unknown = before;
    unknown.bytes[0x8000] ^= 1;
    assert(run(unknown, slot, crc) == Result::FAILED && unknown.mutation == 0);
    MemoryFlash dirty_journal = before;
    dirty_journal.bytes[0x1C000] = 0;
    assert(run(dirty_journal, slot, crc) == Result::FAILED && dirty_journal.mutation == 0);
    MemoryFlash fresh64 = before;
    std::copy(NEW_TABLE.begin(), NEW_TABLE.end(), fresh64.bytes.begin() + 0x8000);
    assert(run(fresh64, slot, crc) == Result::READY && fresh64.mutation == 0);
    MemoryFlash damaged_nvs = before;
    assert(run(damaged_nvs, slot, crc, 1) == Result::INJECTED_RESET);
    damaged_nvs.bytes[0x9100] ^= 1;
    int count = damaged_nvs.mutation;
    assert(run(damaged_nvs, slot, crc) == Result::FAILED && damaged_nvs.mutation == count);
    for (unsigned fault = 0; fault < 4; ++fault) {
      MemoryFlash failed_api = before;
      failed_api.fail_read = fault == 0;
      failed_api.fail_write = fault == 1;
      failed_api.fail_erase = fault == 2;
      failed_api.fail_confirm = fault == 3;
      assert(run(failed_api, slot, crc) == Result::FAILED);
      assert_nvs(failed_api, original_nvs);
      if (fault == 0 || fault == 3) assert(failed_api.mutation == 0);
    }
    MemoryFlash lost_ack = before;
    lost_ack.drop_done = true;
    assert(run(lost_ack, slot, crc) == Result::FAILED);
    assert_nvs(lost_ack, original_nvs);
    lost_ack.drop_done = false;
    assert(run(lost_ack, slot, crc) == Result::REBOOT);
    assert_completed(lost_ack, before);
  }
  std::printf(
      "PASS: both slots, 6 checkpoint resets, replay/no-op and %u mutation cuts (%u resumed, %u fail-closed USB "
      "boundaries)\n",
      cuts, recoveries, usb_boundaries);
}
