#pragma once

#include "oq_ph_learning_platform.h"

#if OQ_PH_LEARNING_CORE_AVAILABLE

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "oq_ph_learning_aggregate.h"

namespace oq_power_house::learning {

constexpr uint32_t kLearningJournalMagic = 0x4F514C4AU;  // OQLJ
constexpr uint16_t kLearningJournalSchemaVersion = 2;
constexpr size_t kLearningJournalHeaderBytes = 40;
constexpr size_t kLearningJournalRecordBytes = 64;
constexpr size_t kLearningJournalCrcBytes = 4;
constexpr size_t kLearningJournalMaxBytes = kLearningJournalHeaderBytes + kMaxPassiveContextBytes +
                                            kMaxSegmentRecords * kLearningJournalRecordBytes + kLearningJournalCrcBytes;
static_assert(kLearningJournalMaxBytes <= 8192U, "learning journal must fit one firmware flash slot");

enum class LearningJournalStatus : uint8_t {
  OK = 0,
  BUFFER_TOO_SMALL,
  INVALID_ARGUMENT,
  INVALID_SCHEMA,
  INVALID_LENGTH,
  INVALID_SEQUENCE,
  INVALID_COUNT,
  CONTEXT_MISMATCH,
  CORRUPT,
  INVALID_RECORD,
  STALE_RECORD,
  TIME_DISCONTINUITY,
  NO_VALID_SLOT,
  AMBIGUOUS_SEQUENCE,
};

struct LearningJournalMetadata {
  LearningJournalStatus status = LearningJournalStatus::INVALID_ARGUMENT;
  uint32_t sequence = 0;
  uint32_t created_epoch_s = 0;
  uint16_t algorithm_version = 0;
  uint16_t record_count = 0;
  uint16_t context_size = 0;
  uint32_t source_generation = 0;
  uint32_t physical_context_generation = 0;
  uint32_t control_generation = 0;
  size_t encoded_size = 0;
};

struct LearningJournalSlotView {
  const uint8_t* bytes = nullptr;
  size_t size = 0;
};

struct LearningJournalSelection {
  LearningJournalStatus status = LearningJournalStatus::NO_VALID_SLOT;
  int8_t selected_slot = -1;
  LearningJournalMetadata metadata;
};

namespace learning_journal_detail {

struct Writer {
  uint8_t* bytes;
  size_t capacity;
  size_t position = 0;
  bool ok = true;
};

struct Reader {
  const uint8_t* bytes;
  size_t size;
  size_t position = 0;
  bool ok = true;
};

inline void write_u16(Writer& writer, uint16_t value) {
  if (!writer.ok || writer.position + 2U > writer.capacity) {
    writer.ok = false;
    return;
  }
  writer.bytes[writer.position++] = static_cast<uint8_t>(value);
  writer.bytes[writer.position++] = static_cast<uint8_t>(value >> 8U);
}

inline void write_u32(Writer& writer, uint32_t value) {
  if (!writer.ok || writer.position + 4U > writer.capacity) {
    writer.ok = false;
    return;
  }
  for (uint8_t shift = 0; shift < 32U; shift += 8U)
    writer.bytes[writer.position++] = static_cast<uint8_t>(value >> shift);
}

inline void write_float(Writer& writer, float value) {
  uint32_t bits = 0;
  static_assert(sizeof(bits) == sizeof(value), "journal requires 32-bit float");
  memcpy(&bits, &value, sizeof(bits));
  write_u32(writer, bits);
}

inline uint16_t read_u16(Reader& reader) {
  if (!reader.ok || reader.position + 2U > reader.size) {
    reader.ok = false;
    return 0;
  }
  const uint16_t value = static_cast<uint16_t>(reader.bytes[reader.position]) |
                         static_cast<uint16_t>(reader.bytes[reader.position + 1U]) << 8U;
  reader.position += 2U;
  return value;
}

inline uint32_t read_u32(Reader& reader) {
  if (!reader.ok || reader.position + 4U > reader.size) {
    reader.ok = false;
    return 0;
  }
  uint32_t value = 0;
  for (uint8_t shift = 0; shift < 32U; shift += 8U)
    value |= static_cast<uint32_t>(reader.bytes[reader.position++]) << shift;
  return value;
}

inline float read_float(Reader& reader) {
  const uint32_t bits = read_u32(reader);
  float value = NAN;
  if (reader.ok) memcpy(&value, &bits, sizeof(value));
  return value;
}

inline void write_record(Writer& writer, const SegmentRecord& record) {
  write_u32(writer, record.start_epoch_s);
  write_u32(writer, record.end_epoch_s);
  write_u32(writer, record.duration_s);
  write_u32(writer, record.source_generation);
  write_u32(writer, record.physical_context_generation);
  write_u32(writer, record.control_generation);
  write_float(writer, record.mean_room_c);
  write_float(writer, record.mean_setpoint_c);
  write_float(writer, record.mean_outside_c);
  write_float(writer, record.mean_heat_w);
  write_float(writer, record.mean_heat_uncertainty_w);
  write_float(writer, record.room_trend_k_per_h);
  write_float(writer, record.room_range_k);
  write_float(writer, record.setpoint_range_c);
  write_float(writer, record.water_start_c);
  write_float(writer, record.water_end_c);
}

inline SegmentRecord read_record(Reader& reader) {
  SegmentRecord record;
  record.start_epoch_s = read_u32(reader);
  record.end_epoch_s = read_u32(reader);
  record.duration_s = read_u32(reader);
  record.source_generation = read_u32(reader);
  record.physical_context_generation = read_u32(reader);
  record.control_generation = read_u32(reader);
  record.mean_room_c = read_float(reader);
  record.mean_setpoint_c = read_float(reader);
  record.mean_outside_c = read_float(reader);
  record.mean_heat_w = read_float(reader);
  record.mean_heat_uncertainty_w = read_float(reader);
  record.room_trend_k_per_h = read_float(reader);
  record.room_range_k = read_float(reader);
  record.setpoint_range_c = read_float(reader);
  record.water_start_c = read_float(reader);
  record.water_end_c = read_float(reader);
  return record;
}

inline LearningJournalStatus parse_metadata(const LearningJournalSlotView& slot, const uint8_t* expected_context,
                                            size_t expected_context_size, uint32_t now_epoch_s,
                                            const QualityConfig& quality, LearningJournalMetadata& metadata,
                                            SegmentRecord* output_records) {
  metadata = {};
  if (slot.bytes == nullptr || expected_context == nullptr || expected_context_size == 0 ||
      expected_context_size > kMaxPassiveContextBytes || now_epoch_s == 0 || !valid_quality_config(quality))
    return LearningJournalStatus::INVALID_ARGUMENT;
  if (slot.size < kLearningJournalHeaderBytes + kLearningJournalCrcBytes || slot.size > kLearningJournalMaxBytes)
    return LearningJournalStatus::INVALID_LENGTH;
  Reader reader{slot.bytes, slot.size};
  const uint32_t magic = read_u32(reader);
  const uint16_t schema = read_u16(reader);
  const uint16_t header_size = read_u16(reader);
  const uint32_t encoded_size = read_u32(reader);
  metadata.sequence = read_u32(reader);
  metadata.created_epoch_s = read_u32(reader);
  metadata.algorithm_version = read_u16(reader);
  metadata.record_count = read_u16(reader);
  metadata.context_size = read_u16(reader);
  const uint16_t reserved = read_u16(reader);
  metadata.source_generation = read_u32(reader);
  metadata.physical_context_generation = read_u32(reader);
  metadata.control_generation = read_u32(reader);
  metadata.encoded_size = encoded_size;
  if (!reader.ok || magic != kLearningJournalMagic || schema != kLearningJournalSchemaVersion ||
      header_size != kLearningJournalHeaderBytes || metadata.algorithm_version != kLearningAlgorithmVersion ||
      reserved != 0)
    return LearningJournalStatus::INVALID_SCHEMA;
  if (metadata.record_count > kMaxSegmentRecords) return LearningJournalStatus::INVALID_COUNT;
  if (encoded_size != slot.size || metadata.context_size == 0 || metadata.context_size > kMaxPassiveContextBytes ||
      encoded_size != kLearningJournalHeaderBytes + metadata.context_size +
                          static_cast<size_t>(metadata.record_count) * kLearningJournalRecordBytes +
                          kLearningJournalCrcBytes)
    return LearningJournalStatus::INVALID_LENGTH;
  if (metadata.sequence == 0 || metadata.created_epoch_s == 0) return LearningJournalStatus::INVALID_SEQUENCE;
  if (metadata.created_epoch_s > now_epoch_s) return LearningJournalStatus::TIME_DISCONTINUITY;
  if (metadata.source_generation == 0 || metadata.physical_context_generation == 0 || metadata.control_generation == 0)
    return LearningJournalStatus::INVALID_SCHEMA;
  const uint32_t stored_crc = static_cast<uint32_t>(slot.bytes[slot.size - 4U]) |
                              static_cast<uint32_t>(slot.bytes[slot.size - 3U]) << 8U |
                              static_cast<uint32_t>(slot.bytes[slot.size - 2U]) << 16U |
                              static_cast<uint32_t>(slot.bytes[slot.size - 1U]) << 24U;
  uint32_t crc = 0xFFFFFFFFU;
  for (size_t index = 0; index < slot.size - kLearningJournalCrcBytes; ++index) {
    crc ^= slot.bytes[index];
    for (uint8_t bit = 0; bit < 8U; ++bit) crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
  }
  crc ^= 0xFFFFFFFFU;
  if (stored_crc != crc) return LearningJournalStatus::CORRUPT;
  if (metadata.context_size != expected_context_size ||
      memcmp(slot.bytes + kLearningJournalHeaderBytes, expected_context, expected_context_size) != 0)
    return LearningJournalStatus::CONTEXT_MISMATCH;
  reader.position = kLearningJournalHeaderBytes + metadata.context_size;
  uint32_t previous_end_epoch_s = 0;
  for (uint16_t index = 0; index < metadata.record_count; ++index) {
    SegmentRecord record = read_record(reader);
    if (!reader.ok || validate_segment_record(record, quality) != LearningStatus::OK)
      return LearningJournalStatus::INVALID_RECORD;
    if (record.source_generation != metadata.source_generation ||
        record.physical_context_generation != metadata.physical_context_generation ||
        record.control_generation != metadata.control_generation)
      return LearningJournalStatus::INVALID_RECORD;
    if (record.end_epoch_s > now_epoch_s || now_epoch_s - record.end_epoch_s > kMaxRecordAgeS)
      return LearningJournalStatus::STALE_RECORD;
    if (index > 0 && record.start_epoch_s < previous_end_epoch_s) return LearningJournalStatus::TIME_DISCONTINUITY;
    previous_end_epoch_s = record.end_epoch_s;
    if (output_records != nullptr) output_records[index] = record;
  }
  if (reader.position != slot.size - kLearningJournalCrcBytes) return LearningJournalStatus::INVALID_LENGTH;
  metadata.status = LearningJournalStatus::OK;
  return metadata.status;
}

}  // namespace learning_journal_detail

inline uint32_t learning_journal_crc32(const uint8_t* bytes, size_t size) {
  if (bytes == nullptr && size != 0) return 0;
  uint32_t crc = 0xFFFFFFFFU;
  for (size_t index = 0; index < size; ++index) {
    crc ^= bytes[index];
    for (uint8_t bit = 0; bit < 8U; ++bit) crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
  }
  return crc ^ 0xFFFFFFFFU;
}

inline LearningJournalStatus encode_learning_journal(const LearningDatasetView& state, const QualityConfig& quality,
                                                     uint32_t sequence, uint32_t created_epoch_s, uint8_t* output,
                                                     size_t output_capacity, size_t& output_size) {
  using namespace learning_journal_detail;
  output_size = 0;
  if (sequence == 0 || created_epoch_s == 0 || output == nullptr || state.context.bytes == nullptr ||
      state.context.size == 0 || state.context.size > kMaxPassiveContextBytes ||
      (state.record_count > 0 && state.records == nullptr) || state.record_count > kMaxSegmentRecords ||
      state.context.source_generation == 0 || state.context.physical_context_generation == 0 ||
      state.context.control_generation == 0)
    return LearningJournalStatus::INVALID_ARGUMENT;
  const size_t required = kLearningJournalHeaderBytes + state.context.size +
                          state.record_count * kLearningJournalRecordBytes + kLearningJournalCrcBytes;
  if (output_capacity < required) return LearningJournalStatus::BUFFER_TOO_SMALL;
  Writer writer{output, output_capacity};
  write_u32(writer, kLearningJournalMagic);
  write_u16(writer, kLearningJournalSchemaVersion);
  write_u16(writer, kLearningJournalHeaderBytes);
  write_u32(writer, static_cast<uint32_t>(required));
  write_u32(writer, sequence);
  write_u32(writer, created_epoch_s);
  write_u16(writer, kLearningAlgorithmVersion);
  write_u16(writer, static_cast<uint16_t>(state.record_count));
  write_u16(writer, static_cast<uint16_t>(state.context.size));
  write_u16(writer, 0);
  write_u32(writer, state.context.source_generation);
  write_u32(writer, state.context.physical_context_generation);
  write_u32(writer, state.context.control_generation);
  if (!writer.ok || writer.position != kLearningJournalHeaderBytes) return LearningJournalStatus::BUFFER_TOO_SMALL;
  memcpy(output + writer.position, state.context.bytes, state.context.size);
  writer.position += state.context.size;
  uint32_t previous_end_epoch_s = 0;
  for (size_t index = 0; index < state.record_count; ++index) {
    const SegmentRecord& record = state.records[index];
    if (validate_segment_record(record, quality) != LearningStatus::OK ||
        record.source_generation != state.context.source_generation ||
        record.physical_context_generation != state.context.physical_context_generation ||
        record.control_generation != state.context.control_generation || record.end_epoch_s > created_epoch_s ||
        created_epoch_s - record.end_epoch_s > kMaxRecordAgeS ||
        (index > 0 && record.start_epoch_s < previous_end_epoch_s))
      return LearningJournalStatus::INVALID_RECORD;
    previous_end_epoch_s = record.end_epoch_s;
    write_record(writer, record);
  }
  if (!writer.ok || writer.position != required - kLearningJournalCrcBytes)
    return LearningJournalStatus::BUFFER_TOO_SMALL;
  write_u32(writer, learning_journal_crc32(output, writer.position));
  if (!writer.ok || writer.position != required) return LearningJournalStatus::BUFFER_TOO_SMALL;
  output_size = required;
  return LearningJournalStatus::OK;
}

inline LearningJournalMetadata inspect_learning_journal(const LearningJournalSlotView& slot,
                                                        const uint8_t* expected_context, size_t expected_context_size,
                                                        uint32_t now_epoch_s, const QualityConfig& quality) {
  LearningJournalMetadata metadata;
  metadata.status = learning_journal_detail::parse_metadata(slot, expected_context, expected_context_size, now_epoch_s,
                                                            quality, metadata, nullptr);
  return metadata;
}

inline LearningJournalSelection select_learning_journal_slot(const LearningJournalSlotView& first,
                                                             const LearningJournalSlotView& second,
                                                             const uint8_t* expected_context,
                                                             size_t expected_context_size, uint32_t now_epoch_s,
                                                             const QualityConfig& quality) {
  const LearningJournalMetadata a =
      inspect_learning_journal(first, expected_context, expected_context_size, now_epoch_s, quality);
  const LearningJournalMetadata b =
      inspect_learning_journal(second, expected_context, expected_context_size, now_epoch_s, quality);
  LearningJournalSelection selection;
  if (a.status != LearningJournalStatus::OK && b.status != LearningJournalStatus::OK) return selection;
  if (a.status == LearningJournalStatus::OK && b.status != LearningJournalStatus::OK) {
    selection.status = LearningJournalStatus::OK;
    selection.selected_slot = 0;
    selection.metadata = a;
    return selection;
  }
  if (b.status == LearningJournalStatus::OK && a.status != LearningJournalStatus::OK) {
    selection.status = LearningJournalStatus::OK;
    selection.selected_slot = 1;
    selection.metadata = b;
    return selection;
  }
  if (a.sequence == b.sequence) {
    if (first.size != second.size || memcmp(first.bytes, second.bytes, first.size) != 0) {
      selection.status = LearningJournalStatus::AMBIGUOUS_SEQUENCE;
      return selection;
    }
    selection.status = LearningJournalStatus::OK;
    selection.selected_slot = 0;
    selection.metadata = a;
    return selection;
  }
  selection.status = LearningJournalStatus::OK;
  selection.selected_slot = a.sequence > b.sequence ? 0 : 1;
  selection.metadata = selection.selected_slot == 0 ? a : b;
  return selection;
}

// Returned only after the entire slot passed schema/context/CRC/record checks.
// The owner must keep slot.bytes immutable until it has consumed this view.
struct LearningJournalRecords {
  LearningJournalSlotView slot;
  size_t context_size = 0;
  size_t record_count = 0;

  SegmentRecord operator[](size_t index) const {
    learning_journal_detail::Reader reader{slot.bytes, slot.size};
    reader.position = kLearningJournalHeaderBytes + context_size + index * kLearningJournalRecordBytes;
    return learning_journal_detail::read_record(reader);
  }
};

}  // namespace oq_power_house::learning

#endif  // OQ_PH_LEARNING_CORE_AVAILABLE
