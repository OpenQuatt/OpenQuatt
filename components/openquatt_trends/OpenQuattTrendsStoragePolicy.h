#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace openquatt_trends {

enum class TrendBlockFormat : uint8_t { INVALID = 0U, LEGACY_V1 = 1U, CURRENT_V2 = 2U };

constexpr TrendBlockFormat trend_block_format(uint32_t magic, uint16_t version, uint16_t sample_count,
                                              uint32_t payload_bytes) {
  if (magic != 0x4F545247U || sample_count == 0U || sample_count > 12U) {
    return TrendBlockFormat::INVALID;
  }
  if (version == 1U && payload_bytes == static_cast<uint32_t>(sample_count) * 22U) {
    return TrendBlockFormat::LEGACY_V1;
  }
  if (version == 2U && payload_bytes == static_cast<uint32_t>(sample_count) * 26U) {
    return TrendBlockFormat::CURRENT_V2;
  }
  return TrendBlockFormat::INVALID;
}

struct TrendStorageCapabilities {
  bool ram_history_available;
  bool flash_archive_available;
};

enum class TrendArchiveLoadAction : uint8_t {
  WAIT = 0U,
  MARK_EMPTY_AS_SEEDED = 1U,
  MERGE_FLASH_INTO_RAM = 2U,
};

constexpr TrendStorageCapabilities trend_storage_capabilities(bool ram_history_allocated, bool flash_index_allocated,
                                                              bool flash_partition_available) {
  return TrendStorageCapabilities{
      ram_history_allocated,
      flash_index_allocated && flash_partition_available,
  };
}

constexpr TrendArchiveLoadAction trend_archive_load_action(bool archive_scanned, size_t indexed_block_count,
                                                           bool archive_seeded) {
  if (!archive_scanned || archive_seeded) {
    return TrendArchiveLoadAction::WAIT;
  }
  if (indexed_block_count == 0U) {
    return TrendArchiveLoadAction::MARK_EMPTY_AS_SEEDED;
  }
  return TrendArchiveLoadAction::MERGE_FLASH_INTO_RAM;
}

}  // namespace openquatt_trends
}  // namespace esphome
