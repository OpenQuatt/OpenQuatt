#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace oq_measurement_archive {

constexpr size_t kSectorBytes = 4096;
constexpr uint32_t kOffset = 0x1E0000;
constexpr size_t kContextSectors = 16;
constexpr uint32_t kMeasurementOffset = kOffset + kContextSectors * kSectorBytes;
constexpr size_t kMeasurementSectors = 359;
constexpr uint32_t kWeatherOffset = kMeasurementOffset + kMeasurementSectors * kSectorBytes;
constexpr size_t kWeatherSectors = 207;
constexpr uint32_t kEndOffset = kWeatherOffset + kWeatherSectors * kSectorBytes;
constexpr uint16_t kSchema = 1;
constexpr uint32_t kRetentionS = 180U * 86400U;
constexpr int16_t kMissingTemperature = INT16_MIN;
constexpr int32_t kMissingHeat = INT32_MIN;
constexpr uint16_t kMissingWind = UINT16_MAX;

enum Flags : uint16_t {
  HP1_RUNNING = 1U << 0,
  HP2_RUNNING = 1U << 1,
  BOILER = 1U << 2,
  DEFROST = 1U << 3,
  COOLING = 1U << 4,
  LIMITED = 1U << 5,
  SERVICE = 1U << 6,
  UNKNOWN_OPERATION = 1U << 7,
  MISSING_HEAT = 1U << 8,
  MISSING_TEMPERATURE = 1U << 9,
  GAP = 1U << 10,
  TRANSITION = 1U << 11,
  PARTIAL = 1U << 12,
  NEW_SESSION = 1U << 13,
  SYNTHESIZED_ZERO_FLOW = 1U << 14,
};

// Prefix for a measurement context, followed by the existing installation
// context bytes. Row and sector formats stay unchanged. Contexts without this
// prefix have no recorded heat route.
enum class HeatBoundary : uint8_t { HEAT_PUMPS = 1, SYSTEM = 2 };
struct __attribute__((packed)) HeatRoute {
  uint32_t magic = 0x3252514F;  // OQR2
  HeatBoundary boundary = HeatBoundary::HEAT_PUMPS;
  uint8_t flow_source = 0, supply_source = 0, reserved = 0;
  uint32_t flow_endpoint = 0, supply_endpoint = 0;
  float supply_offset_c = 0;
};
static_assert(sizeof(HeatRoute) == 20);

inline uint32_t checksum(const void* bytes, size_t size) {
  auto* data = static_cast<const uint8_t*>(bytes);
  uint32_t crc = UINT32_MAX;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
  }
  return ~crc;
}

template <typename Row>
inline void seal(Row& row) {
  row.crc = checksum(&row, offsetof(Row, crc));
}
template <typename Row>
inline bool verified(const Row& row) {
  return row.crc == checksum(&row, offsetof(Row, crc));
}

struct __attribute__((packed)) Measurement {
  uint32_t epoch;
  int32_t heat_tenth_wh;
  int16_t room_start_centi, room_end_centi, outside_mean_centi, setpoint_centi;
  uint16_t duration_s, heat_valid_s, temperature_valid_s, flags;
  uint32_t crc;
};
static_assert(sizeof(Measurement) == 28);

struct __attribute__((packed)) Weather {
  uint32_t epoch, first_hour_epoch, current_epoch;
  uint16_t current_speed_centi, current_direction_centi, valid_mask;
  int16_t outside_centi[5];
  uint16_t speed_centi[5];
  uint8_t direction_two_degrees[5], reserved;
  uint32_t crc;
};
static_assert(sizeof(Weather) == 48);

struct __attribute__((packed)) Header {
  uint32_t magic;
  uint16_t schema, row_size;
  uint32_t sequence, context, boot, first_epoch, reserved, crc;
};
static_assert(sizeof(Header) == 32);

struct __attribute__((packed)) Context {
  uint32_t magic;
  uint16_t size, schema;
  uint8_t bytes[1024];
  uint32_t crc;
};
static_assert(sizeof(Context) == 1036);
constexpr size_t kContextsPerSector = kSectorBytes / sizeof(Context);
constexpr size_t kContextCapacity = kContextSectors * kContextsPerSector;
constexpr uint32_t kUnregisteredContext = kContextCapacity + 1;
static_assert(kContextCapacity <= 64);

constexpr uint64_t context_bit(uint32_t id) { return id && id <= kContextCapacity ? uint64_t{1} << (id - 1) : 0; }

template <typename Row>
constexpr size_t rows_per_sector() {
  return (kSectorBytes - sizeof(Header)) / sizeof(Row);
}
static_assert((kMeasurementSectors - 1) * rows_per_sector<Measurement>() >= 180U * 288U);
static_assert((kWeatherSectors - 1) * rows_per_sector<Weather>() >= 180U * 96U);

// All offsets are relative to the existing openquatt_data partition.
struct Flash {
  virtual ~Flash() = default;
  virtual bool read(uint32_t offset, void* data, size_t size) = 0;
  virtual bool write(uint32_t offset, const void* data, size_t size) = 0;
  virtual bool erase(uint32_t offset) = 0;
};

struct SectorIndex {
  uint32_t sequence = 0, first_epoch = 0;
  uint8_t used = 0, valid = 0;
  uint16_t context = 0;
};
static_assert(sizeof(SectorIndex) == 12);

struct ArchivePeriod {
  uint32_t first_epoch = 0, last_epoch = 0;
  size_t count = 0;
};

template <typename Row, size_t Sectors, uint32_t Offset, uint32_t Magic>
class Ring {
  static_assert(rows_per_sector<Row>() <= UINT8_MAX);

 public:
  bool open(Flash& flash, SectorIndex* index, uint8_t* scratch, uint32_t boot) {
    flash_ = &flash;
    index_ = index;
    scratch_ = scratch;
    boot_ = boot;
    healthy_ = false;
    sequence_ = last_epoch_ = 0;
    active_ = Sectors;
    for (size_t i = 0; i < Sectors; ++i) {
      if (!flash.read(Offset + i * kSectorBytes, scratch, kSectorBytes)) return false;
      index[i] = {};
      Header header;
      std::memcpy(&header, scratch, sizeof(header));
      if (header.magic != Magic || header.schema != kSchema || header.row_size != sizeof(Row) || !verified(header)) {
        // A future schema or non-erased unknown sector must never be erased by an older reader.
        if (header.magic == Magic && header.schema != kSchema && verified(header)) return false;
        continue;
      }
      auto& entry = index[i];
      if (!header.context || header.context > kUnregisteredContext) return false;
      entry.sequence = header.sequence;
      entry.first_epoch = header.first_epoch;
      entry.context = static_cast<uint16_t>(header.context);
      for (size_t row = 0; row < rows_per_sector<Row>(); ++row) {
        Row value;
        std::memcpy(&value, scratch + sizeof(Header) + row * sizeof(Row), sizeof(value));
        const auto* raw = reinterpret_cast<const uint8_t*>(&value);
        if (std::all_of(raw, raw + sizeof(value), [](uint8_t byte) { return byte == 0xFF; })) continue;
        entry.used = row + 1;
        if (verified(value) && value.epoch >= header.first_epoch) {
          if (entry.valid == 0) entry.first_epoch = value.epoch;
          ++entry.valid;
          last_epoch_ = std::max(last_epoch_, value.epoch);
        }
      }
      if (header.sequence >= sequence_) {
        sequence_ = header.sequence;
        context_ = header.context;
        active_ = i;
      }
    }
    // Resume unused slots after reboot. The collector marks its first new
    // observation window, avoiding a wasted sector for every restart.
    next_sector_ = active_ == Sectors ? 0 : (active_ + 1) % Sectors;
    healthy_ = true;
    return true;
  }

  bool append(Row row, uint32_t context) {
    if (!healthy_ || !context || context > kUnregisteredContext || !row.epoch || row.epoch <= last_epoch_) return false;
    if (active_ == Sectors || index_[active_].used == rows_per_sector<Row>() || context != context_) {
      if (sequence_ == UINT32_MAX) return fail_();
      const size_t next = next_sector_;
      if (!flash_->erase(Offset + next * kSectorBytes)) return fail_();
      Header header{Magic, kSchema, sizeof(Row), sequence_ + 1, context, boot_, row.epoch, 0, 0};
      seal(header);
      if (!flash_->write(Offset + next * kSectorBytes, &header, sizeof(header)) ||
          !flash_->read(Offset + next * kSectorBytes, scratch_, sizeof(header)) ||
          std::memcmp(scratch_, &header, sizeof(header)) != 0)
        return fail_();
      active_ = next;
      next_sector_ = (next + 1) % Sectors;
      context_ = context;
      sequence_ = header.sequence;
      index_[active_] = {sequence_, row.epoch, 0, 0, static_cast<uint16_t>(context)};
    }
    if (row.epoch < index_[active_].first_epoch) return false;
    seal(row);
    auto& entry = index_[active_];
    const uint32_t offset = Offset + active_ * kSectorBytes + sizeof(Header) + entry.used * sizeof(Row);
    // A slot is consumed even when its write or acknowledgement fails.
    ++entry.used;
    if (!flash_->write(offset, &row, sizeof(row)) || !flash_->read(offset, scratch_, sizeof(row)) ||
        std::memcmp(scratch_, &row, sizeof(row)) != 0)
      return fail_();
    if (entry.valid == 0) entry.first_epoch = row.epoch;
    ++entry.valid;
    last_epoch_ = row.epoch;
    return true;
  }

  template <typename Consumer>
  bool visit(uint32_t cutoff, Consumer consume) {
    if (!healthy_) return false;
    // Iterate the circular sector order, beginning with the oldest sector.
    const size_t oldest = active_ == Sectors ? next_sector_ : (active_ + 1) % Sectors;
    for (size_t n = 0; n < Sectors; ++n) {
      const size_t i = (oldest + n) % Sectors;
      if (!index_[i].sequence) continue;
      if (!flash_->read(Offset + i * kSectorBytes, scratch_, kSectorBytes)) return fail_();
      Header header;
      std::memcpy(&header, scratch_, sizeof(header));
      if (!verified(header) || header.sequence != index_[i].sequence) return fail_();
      for (size_t j = 0; j < index_[i].used; ++j) {
        Row row;
        std::memcpy(&row, scratch_ + sizeof(Header) + j * sizeof(Row), sizeof(row));
        if (verified(row) && row.epoch >= cutoff && row.epoch >= header.first_epoch && !consume(row, header))
          return false;
      }
    }
    return true;
  }

  bool healthy() const { return healthy_; }
  uint32_t last_epoch() const { return last_epoch_; }
  ArchivePeriod period() const {
    ArchivePeriod result{};
    if (!index_) return result;
    result.last_epoch = last_epoch_;
    for (size_t i = 0; i < Sectors; ++i) {
      if (!index_[i].valid) continue;
      result.count += index_[i].valid;
      if (!result.first_epoch || index_[i].first_epoch < result.first_epoch) result.first_epoch = index_[i].first_epoch;
    }
    return result;
  }
  size_t count() const { return period().count; }
  uint64_t live_contexts() const {
    uint64_t result = 0;
    if (!index_) return result;
    for (size_t i = 0; i < Sectors; ++i)
      if (index_[i].valid) result |= context_bit(index_[i].context);
    return result;
  }

  // Caller copies under the writer mutex. The reader then uses its own
  // scratch and checks the sector generation before using any captured slots.
  size_t copy_index(SectorIndex (&target)[Sectors]) const {
    if (index_)
      std::memcpy(target, index_, sizeof(target));
    else
      std::fill_n(target, Sectors, SectorIndex{});
    return active_ == Sectors ? 0 : (active_ + 1) % Sectors;
  }

  template <typename Consumer>
  static bool visit_snapshot(Flash& flash, const SectorIndex (&index)[Sectors], size_t oldest, uint32_t from,
                             uint32_t until, uint8_t* scratch, Consumer consume) {
    // Only the final sector that starts before the requested day can overlap
    // that day. The copied index is in chronological ring order.
    size_t start = 0;
    for (size_t n = 0; n < Sectors; ++n) {
      const auto& entry = index[(oldest + n) % Sectors];
      if (entry.valid && entry.first_epoch <= from) start = n;
    }
    for (size_t n = start; n < Sectors; ++n) {
      const size_t i = (oldest + n) % Sectors;
      const auto& entry = index[i];
      if (!entry.valid || entry.first_epoch >= until) continue;
      if (entry.used > rows_per_sector<Row>() || !flash.read(Offset + i * kSectorBytes, scratch, kSectorBytes))
        return false;
      Header header;
      std::memcpy(&header, scratch, sizeof(header));
      if (!verified(header) || header.magic != Magic || header.schema != kSchema || header.row_size != sizeof(Row) ||
          header.sequence != entry.sequence)
        return false;
      for (size_t j = 0; j < entry.used; ++j) {
        Row row;
        std::memcpy(&row, scratch + sizeof(Header) + j * sizeof(Row), sizeof(row));
        if (verified(row) && row.epoch >= header.first_epoch && row.epoch >= from && row.epoch < until &&
            !consume(row, header))
          return false;
      }
    }
    return true;
  }

 private:
  bool fail_() {
    healthy_ = false;
    return false;
  }
  Flash* flash_ = nullptr;
  SectorIndex* index_ = nullptr;
  uint8_t* scratch_ = nullptr;
  uint32_t boot_ = 0, context_ = 0, sequence_ = 0, last_epoch_ = 0;
  size_t active_ = Sectors, next_sector_ = 0;
  bool healthy_ = false;
};

using Measurements = Ring<Measurement, kMeasurementSectors, kMeasurementOffset, 0x314D514F>;
using WeatherHistory = Ring<Weather, kWeatherSectors, kWeatherOffset, 0x3157514F>;

// Slots remain immutable while either ring references them. Reclaim only
// entire unreferenced sectors. The fallback ID has no flash slot.
class Contexts {
 public:
  bool open(Flash& flash, uint8_t* scratch) {
    flash_ = &flash;
    scratch_ = scratch;
    return true;
  }
  uint32_t revision() const { return revision_; }
  uint32_t bind(const void* bytes, size_t size, uint64_t live) {
    if (!size || size > sizeof(Context::bytes)) return 0;
    size_t empty = kContextCapacity;
    for (size_t i = 0; i < kContextCapacity; ++i) {
      if (!flash_->read(offset_(i), scratch_, sizeof(Context))) return 0;
      Context value;
      std::memcpy(&value, scratch_, sizeof(value));
      if (value.magic == 0xFFFFFFFF && empty == kContextCapacity && !(live & context_bit(i + 1))) {
        const auto* raw = reinterpret_cast<const uint8_t*>(&value);
        if (std::all_of(raw, raw + sizeof(value), [](uint8_t byte) { return byte == 0xFF; })) empty = i;
      }
      if (value.magic == kMagic && value.schema == kSchema && value.size == size && verified(value) &&
          std::memcmp(value.bytes, bytes, size) == 0)
        return i + 1;
    }
    if (empty == kContextCapacity) {
      constexpr uint64_t sector_slots = (uint64_t{1} << kContextsPerSector) - 1;
      for (size_t sector = 0; sector < kContextSectors; ++sector) {
        const size_t first = sector * kContextsPerSector;
        if (live & (sector_slots << first)) continue;
        // Readers reject a snapshot spanning any attempted slot reuse.
        ++revision_;
        if (!flash_->erase(offset_(first))) return 0;
        empty = first;
        break;
      }
      if (empty == kContextCapacity) return kUnregisteredContext;
    }
    Context value{};
    value.magic = kMagic;
    value.schema = kSchema;
    value.size = size;
    std::memcpy(value.bytes, bytes, size);
    seal(value);
    if (!flash_->write(offset_(empty), &value, sizeof(value)) ||
        !flash_->read(offset_(empty), scratch_, sizeof(value)) || std::memcmp(scratch_, &value, sizeof(value)))
      return 0;
    return empty + 1;
  }

  static bool read_heat_route(Flash& flash, uint32_t id, Context& scratch, HeatRoute& route) {
    route = {};
    route.magic = 0;  // Legacy or temporarily unregistered context.
    if (id == kUnregisteredContext) return true;
    if (id == 0 || id > kContextCapacity || !flash.read(offset_(id - 1), &scratch, sizeof(scratch)) ||
        scratch.magic != kMagic || scratch.schema != kSchema || scratch.size == 0 ||
        scratch.size > sizeof(scratch.bytes) || !verified(scratch))
      return false;
    if (scratch.size >= sizeof(HeatRoute)) std::memcpy(&route, scratch.bytes, sizeof(route));
    if (route.magic != HeatRoute{}.magic) route.magic = 0;
    return true;
  }

 private:
  static constexpr uint32_t kMagic = 0x3143514F;
  static uint32_t offset_(size_t i) {
    return kOffset + (i / kContextsPerSector) * kSectorBytes + (i % kContextsPerSector) * sizeof(Context);
  }
  Flash* flash_ = nullptr;
  uint8_t* scratch_ = nullptr;
  uint32_t revision_ = 0;
};

struct Sample {
  uint64_t monotonic_ms = 0;
  uint32_t epoch = 0, context = 0;
  float room_c = NAN, outside_c = NAN, heat_w = NAN, setpoint_c = NAN;
  uint16_t flags = 0;
};
inline int16_t temperature(double c) {
  return std::isfinite(c) && c >= -100 && c <= 150 ? static_cast<int16_t>(std::lround(c * 100)) : kMissingTemperature;
}
class Collector {
 public:
  void reset() { *this = {}; }
  template <typename Emit>
  bool observe(const Sample& sample, Emit emit) {
    if (!sample.epoch || !sample.monotonic_ms || !sample.context) {
      reset();
      return true;
    }
    if (previous_.monotonic_ms && sample.monotonic_ms == previous_.monotonic_ms) return true;
    if (previous_.monotonic_ms &&
        (sample.context != previous_.context || sample.monotonic_ms < previous_.monotonic_ms ||
         std::abs((static_cast<int64_t>(sample.epoch) - previous_.epoch) * 1000 -
                  static_cast<int64_t>(sample.monotonic_ms - previous_.monotonic_ms)) > 2000))
      reset();
    if (!previous_.monotonic_ms) {
      first_ = previous_ = sample;
      return true;
    }
    const uint64_t dt = sample.monotonic_ms - previous_.monotonic_ms;
    if (dt > 300000) {
      reset();
      flags_ = GAP;
      first_ = previous_ = sample;
      return true;
    }
    flags_ |= sample.flags | previous_.flags;
    if (sample.flags != previous_.flags || temperature(sample.setpoint_c) != temperature(previous_.setpoint_c))
      flags_ |= TRANSITION;
    if (dt <= 30000) {
      if (std::isfinite(sample.heat_w) && std::isfinite(previous_.heat_w) && std::abs(sample.heat_w) <= 50000 &&
          std::abs(previous_.heat_w) <= 50000) {
        heat_ws_ += (sample.heat_w + previous_.heat_w) * 0.5 * dt / 1000;
        heat_ms_ += dt;
      } else
        flags_ |= MISSING_HEAT;
      if (std::isfinite(sample.room_c) && std::isfinite(previous_.room_c) && std::isfinite(sample.outside_c) &&
          std::isfinite(previous_.outside_c)) {
        outside_ms_ += (sample.outside_c + previous_.outside_c) * 0.5 * dt;
        temperature_ms_ += dt;
      } else
        flags_ |= MISSING_TEMPERATURE;
    } else
      flags_ |= GAP;
    previous_ = sample;
    const uint64_t span = sample.monotonic_ms - first_.monotonic_ms;
    if (span < 300000) return true;
    const bool success = emit_row_(span > 330000, emit);
    reset();
    first_ = previous_ = sample;
    return success;
  }

  template <typename Emit>
  bool finish_partial(Emit emit) {
    const bool success = previous_.monotonic_ms > first_.monotonic_ms ? emit_row_(true, emit) : true;
    reset();
    return success;
  }

 private:
  template <typename Emit>
  bool emit_row_(bool partial, Emit& emit) const {
    Measurement row{};
    row.epoch = previous_.epoch;
    row.duration_s = std::min<uint64_t>((previous_.monotonic_ms - first_.monotonic_ms) / 1000, UINT16_MAX);
    row.heat_valid_s = heat_ms_ / 1000;
    row.temperature_valid_s = temperature_ms_ / 1000;
    row.flags = flags_ | (partial ? PARTIAL : 0);
    row.heat_tenth_wh = row.heat_valid_s ? static_cast<int32_t>(std::lround(heat_ws_ / 360)) : kMissingHeat;
    row.outside_mean_centi = temperature_ms_ ? temperature(outside_ms_ / temperature_ms_) : kMissingTemperature;
    row.room_start_centi = temperature(first_.room_c);
    row.room_end_centi = temperature(previous_.room_c);
    row.setpoint_centi = temperature(previous_.setpoint_c);
    return emit(row, previous_.context);
  }

  Sample first_, previous_;
  double heat_ws_ = 0, outside_ms_ = 0;
  uint64_t heat_ms_ = 0, temperature_ms_ = 0;
  uint16_t flags_ = 0;
};

}  // namespace oq_measurement_archive
