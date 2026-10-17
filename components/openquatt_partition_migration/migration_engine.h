#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace esphome::openquatt_partition_migration {

// Exact 16 MiB Q layout only. Never infer a table or write into a running slot.
enum class Result { FAILED, REBOOT, READY, INJECTED_RESET };

inline bool writable_region(uint32_t address, uint32_t size, uint32_t running, uint32_t image_size, bool erase) {
  const uint64_t end = static_cast<uint64_t>(address) + size;
  if (size == 0) return false;
  if (address >= 0x8000 && end <= 0x9000) return true;
  if (address >= (erase ? 0xF000U : 0x19000U) && end <= 0x1E000) return true;
  // The only app write is a verified recovery copy into inactive app0.
  const uint32_t limit = erase ? (image_size + 0xFFFU) & ~0xFFFU : image_size;
  return running == 0x520000 && image_size <= 0x500000 && address >= 0x20000 && end <= 0x20000ULL + limit;
}

class Flash {
 public:
  virtual ~Flash() = default;
  virtual bool read(uint32_t address, void* data, uint32_t size) = 0;
  virtual bool write(uint32_t address, const void* data, uint32_t size) = 0;
  virtual bool erase(uint32_t address, uint32_t size) = 0;
  virtual bool confirm_image() { return true; }
};

inline uint32_t crc_update(uint32_t crc, const uint8_t* data, size_t size) {
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
  }
  return crc;
}

struct Journal {
  uint32_t magic;
  uint32_t version;
  uint32_t nvs_crc;
  uint32_t ota_crc;
  uint32_t phy_crc;
  uint32_t image_size;
  uint32_t image_crc;
  uint32_t header_crc;
  uint32_t done;
  uint32_t injected;
};
static_assert(sizeof(Journal) == 40);

class Engine {
 public:
  Engine(Flash& flash, const uint8_t* old_table, const uint8_t* new_table, uint32_t running, uint32_t image_size,
         uint32_t image_crc, unsigned fault_point = 0)
      : flash_(flash),
        old_(old_table),
        next_(new_table),
        running_(running),
        image_size_(image_size),
        image_crc_(image_crc),
        fault_point_(fault_point) {}

  Result run() {
    if ((running_ != 0x20000 && running_ != 0x520000) || image_size_ == 0 || image_size_ > 0x500000 ||
        image_size_ % 4 != 0 || fault_point_ > 3)
      return Result::FAILED;
    bool old_layout = equal_(0x8000, old_, 0xC00);
    bool new_layout = equal_(0x8000, next_, 0xC00);
    if ((!old_layout && !new_layout) || !erased_(0x8C00, 0x400)) return Result::FAILED;
    Journal a{}, b{};
    if (!flash_.read(0x1C000, &a, sizeof(a)) || !flash_.read(0x1D000, &b, sizeof(b))) return Result::FAILED;
    bool va = valid_(a), vb = valid_(b);
    if (va && vb && std::memcmp(&a, &b, offsetof(Journal, done)) != 0) return Result::FAILED;
    if (!va && !vb) {
      // Torn/foreign journal is not silently treated as a fresh migration.
      if (!erased_(0x1C000, 0x2000)) return Result::FAILED;
      if (new_layout) return Result::READY;
      if (!flash_.confirm_image() || !anchor_()) return Result::FAILED;
      if (!copy_(0xF000, 0x19000, 0x2000) || !copy_(0x11000, 0x1B000, 0x1000)) return Result::FAILED;
      journal_ = {0x4F514D47U, 1U, 0U, 0U, 0U, image_size_, image_crc_, 0U, UINT32_MAX, UINT32_MAX};
      if (!checksum_(0x9000, 0x6000, journal_.nvs_crc) || !checksum_(0x19000, 0x2000, journal_.ota_crc) ||
          !checksum_(0x1B000, 0x1000, journal_.phy_crc))
        return Result::FAILED;
      journal_.header_crc =
          crc_update(UINT32_MAX, reinterpret_cast<const uint8_t*>(&journal_), offsetof(Journal, header_crc));
      if (!save_(0x1C000, journal_) || !save_(0x1D000, journal_)) return Result::FAILED;
      va = vb = true;
      a = b = journal_;
    } else {
      journal_ = va ? a : b;
    }
    const bool done = (va && a.done == 0) || (vb && b.done == 0);
    if (done) return new_layout ? Result::READY : Result::FAILED;
    // Until DONE, the original NVS and durable new metadata must be identical.
    if (journal_.image_size != image_size_ || journal_.image_crc != image_crc_ ||
        !matches_crc_(0x9000, 0x6000, journal_.nvs_crc) || !matches_crc_(0x19000, 0x2000, journal_.ota_crc) ||
        !matches_crc_(0x1B000, 0x1000, journal_.phy_crc) || !matches_crc_(0x20000, image_size_, image_crc_))
      return Result::FAILED;
    if (!va && !save_(0x1C000, journal_)) return Result::FAILED;
    if (!vb && !save_(0x1D000, journal_)) return Result::FAILED;
    journal_.injected = (va ? a.injected : UINT32_MAX) & (vb ? b.injected : UINT32_MAX);
    Result injection = checkpoint_(1);
    if (injection != Result::READY) return injection;
    if (old_layout) {
      // Repeated erase is deliberate. The old bootloader may have rebuilt old
      // otadata after a reset; none of that range belongs to the original NVS.
      if (!flash_.erase(0xF000, 0xA000) || !erased_(0xF000, 0xA000) || !matches_crc_(0x9000, 0x6000, journal_.nvs_crc))
        return Result::FAILED;
      injection = checkpoint_(2);
      if (injection != Result::READY) return injection;
      // The single primary sector is an unavoidable USB-recovery boundary.
      if (!flash_.erase(0x8000, 0x1000) || !flash_.write(0x8000, next_, 0xC00) || !equal_(0x8000, next_, 0xC00) ||
          !erased_(0x8C00, 0x400))
        return Result::FAILED;
    }
    injection = checkpoint_(3);
    if (injection != Result::READY) return injection;
    if (!matches_crc_(0x9000, 0x6000, journal_.nvs_crc) || !matches_crc_(0x19000, 0x2000, journal_.ota_crc) ||
        !matches_crc_(0x1B000, 0x1000, journal_.phy_crc))
      return Result::FAILED;
    uint32_t zero = 0;
    if (!flash_.write(0x1C000 + offsetof(Journal, done), &zero, sizeof(zero)) ||
        !flash_.write(0x1D000 + offsetof(Journal, done), &zero, sizeof(zero)) ||
        !word_is_(0x1C000 + offsetof(Journal, done), zero) || !word_is_(0x1D000 + offsetof(Journal, done), zero))
      return Result::FAILED;
    // Never enter ESPHome with the pre-commit partition cache, even on resume.
    return Result::REBOOT;
  }

 private:
  bool valid_(const Journal& value) const {
    return value.magic == 0x4F514D47U && value.version == 1U && value.image_size > 0 && value.image_size <= 0x500000 &&
           value.header_crc ==
               crc_update(UINT32_MAX, reinterpret_cast<const uint8_t*>(&value), offsetof(Journal, header_crc));
  }
  bool checksum_(uint32_t address, uint32_t size, uint32_t& crc) {
    uint8_t buffer[256];
    crc = UINT32_MAX;
    while (size != 0) {
      uint32_t count = size < sizeof(buffer) ? size : sizeof(buffer);
      if (!flash_.read(address, buffer, count)) return false;
      crc = crc_update(crc, buffer, count);
      address += count;
      size -= count;
    }
    return true;
  }
  bool matches_crc_(uint32_t address, uint32_t size, uint32_t expected) {
    uint32_t actual;
    return checksum_(address, size, actual) && actual == expected;
  }
  bool equal_(uint32_t address, const uint8_t* expected, uint32_t size) {
    uint8_t buffer[256];
    while (size != 0) {
      uint32_t count = size < sizeof(buffer) ? size : sizeof(buffer);
      if (!flash_.read(address, buffer, count) || std::memcmp(buffer, expected, count) != 0) return false;
      address += count;
      expected += count;
      size -= count;
    }
    return true;
  }
  bool erased_(uint32_t address, uint32_t size) {
    uint8_t buffer[256];
    while (size != 0) {
      uint32_t count = size < sizeof(buffer) ? size : sizeof(buffer);
      if (!flash_.read(address, buffer, count)) return false;
      for (uint32_t i = 0; i < count; ++i)
        if (buffer[i] != 0xFF) return false;
      address += count;
      size -= count;
    }
    return true;
  }
  bool copy_(uint32_t source, uint32_t dest, uint32_t size) {
    if (!flash_.erase(dest, (size + 0xFFFU) & ~0xFFFU)) return false;
    uint8_t a[256], b[256];
    while (size != 0) {
      uint32_t count = size < sizeof(a) ? size : sizeof(a);
      if (!flash_.read(source, a, count) || !flash_.write(dest, a, count) || !flash_.read(dest, b, count) ||
          std::memcmp(a, b, count) != 0)
        return false;
      source += count;
      dest += count;
      size -= count;
    }
    return true;
  }
  bool anchor_() {
    if (matches_crc_(0x20000, image_size_, image_crc_)) return true;
    return running_ == 0x520000 && copy_(running_, 0x20000, image_size_) &&
           matches_crc_(0x20000, image_size_, image_crc_);
  }
  bool save_(uint32_t address, const Journal& value) {
    Journal readback{};
    return flash_.erase(address, 0x1000) && flash_.write(address, &value, sizeof(value)) &&
           flash_.read(address, &readback, sizeof(readback)) && std::memcmp(&value, &readback, sizeof(value)) == 0;
  }
  Result checkpoint_(unsigned point) {
    if (fault_point_ != point || (journal_.injected & (1U << point)) == 0) return Result::READY;
    uint32_t mask = journal_.injected & ~(1U << point);
    if (!flash_.write(0x1C000 + offsetof(Journal, injected), &mask, sizeof(mask)) ||
        !flash_.write(0x1D000 + offsetof(Journal, injected), &mask, sizeof(mask)) ||
        !word_is_(0x1C000 + offsetof(Journal, injected), mask) ||
        !word_is_(0x1D000 + offsetof(Journal, injected), mask))
      return Result::FAILED;
    journal_.injected = mask;
    return Result::INJECTED_RESET;
  }
  bool word_is_(uint32_t address, uint32_t expected) {
    uint32_t actual = 0;
    return flash_.read(address, &actual, sizeof(actual)) && actual == expected;
  }
  Flash& flash_;
  const uint8_t* old_;
  const uint8_t* next_;
  uint32_t running_;
  uint32_t image_size_;
  uint32_t image_crc_;
  unsigned fault_point_;
  Journal journal_{};
};

}  // namespace esphome::openquatt_partition_migration
