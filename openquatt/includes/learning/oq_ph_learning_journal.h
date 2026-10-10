#pragma once

#include "oq_ph_learning_platform.h"

#if OQ_PH_LEARNING_CORE_AVAILABLE

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "oq_ph_learning_aggregate.h"
#include "oq_ph_thermal_model_logic.h"

namespace oq_power_house::learning {

constexpr uint32_t kLearningJournalMagic = 0x4F514C4AU;              // OQLJ
constexpr uint32_t kLearningMeasurementContextMarker = 0x4D435458U;  // MCTX
// Schema 7 appends a bounded daily checkpoint after the unchanged thermal tail.
constexpr uint16_t kLearningJournalSchemaVersion = 7;
constexpr size_t kLearningJournalHeaderBytes = 32;
constexpr size_t kLearningJournalRecordBytes = 52;
static_assert(kDailyTemperatureProfileSize * sizeof(int16_t) == 16U,
              "daily profile must preserve the 52-byte journal record");
constexpr size_t kLearningJournalCrcBytes = 4;
// 17 doubles, three counters and last accepted UTC timestamp; no boot-local clocks.
constexpr size_t kLearningJournalThermalBytes = 152;
// Explicit UTC/value serialization; no boot-local clocks or raw struct layout.
constexpr size_t kLearningJournalDailyBytes = 252;
constexpr size_t kLearningJournalMaxBytes =
    kLearningJournalHeaderBytes + kMaxPassiveContextBytes + kMaxSegmentRecords * kLearningJournalRecordBytes +
    kLearningJournalThermalBytes + kLearningJournalDailyBytes + kLearningJournalCrcBytes;
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
  uint16_t schema_version = 0;
  uint16_t algorithm_version = 0;
  uint16_t record_count = 0;
  uint16_t context_size = 0;
  uint32_t context_revision = 0;
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

// Schema 7 already stores the physical fields, but older firmware interleaves
// them with selector settings. Read that layout without changing the slot or CRC.
inline bool daily_context_matches(const uint8_t* stored, size_t stored_size, const uint8_t* expected,
                                  size_t expected_size) {
  if (stored == nullptr || expected == nullptr || stored_size > kMaxPassiveContextBytes ||
      expected_size > kMaxPassiveContextBytes)
    return false;
  if (stored_size == expected_size && memcmp(stored, expected, expected_size) == 0) return true;
  constexpr size_t prefix_size = 3U * sizeof(uint32_t);  // algorithm, topology, hardware
  constexpr size_t legacy_source_keys_size = 4U * 3U * sizeof(uint32_t);
  if (stored_size < prefix_size + legacy_source_keys_size || expected_size < prefix_size + 4U ||
      memcmp(stored, expected, prefix_size) != 0)
    return false;
  Reader old{stored, stored_size};
  Reader current{expected, expected_size};
  if (read_u32(current) != kLearningAlgorithmVersion) return false;
  read_u32(current);
  const uint32_t q_hardware = read_u32(current);
  if (q_hardware > 1U || read_u32(current) != kLearningMeasurementContextMarker) return false;
  old.position = prefix_size;
  if (read_u32(old) == kLearningMeasurementContextMarker) return false;
  old.position = prefix_size + legacy_source_keys_size;
  const auto string_length = [](Reader& reader) -> uint32_t {
    const uint32_t length = read_u32(reader);
    if (!reader.ok || length > reader.size - reader.position) {
      reader.ok = false;
      return 0U;
    }
    return length;
  };
  const auto skip_string = [&](Reader& reader) {
    const uint32_t length = string_length(reader);
    if (reader.ok) reader.position += length;
  };
  for (unsigned index = 0; index < 5U; ++index) skip_string(old);
  const uint32_t old_generation_size = string_length(old);
  const uint32_t generation_size = string_length(current);
  if (!old.ok || !current.ok || old_generation_size != generation_size ||
      memcmp(old.bytes + old.position, current.bytes + current.position, generation_size) != 0)
    return false;
  old.position += old_generation_size;
  current.position += generation_size;
  skip_string(old);  // CiC endpoint
  if (q_hardware) {
    skip_string(old);  // Q Flow Source
    const uint32_t old_meter_size = string_length(old);
    const uint32_t meter_size = string_length(current);
    if (!old.ok || !current.ok || old_meter_size != meter_size ||
        memcmp(old.bytes + old.position, current.bytes + current.position, meter_size) != 0)
      return false;
    old.position += old_meter_size;
    current.position += meter_size;
  }
  // Calibration floats remain exact, including flow pulses/liter on Q hardware.
  return old.ok && current.ok && old.size - old.position == current.size - current.position &&
         memcmp(old.bytes + old.position, current.bytes + current.position, current.size - current.position) == 0;
}

inline float read_float(Reader& reader) {
  const uint32_t bits = read_u32(reader);
  float value = NAN;
  if (reader.ok) memcpy(&value, &bits, sizeof(value));
  return value;
}

inline void write_double(Writer& writer, double value) {
  uint64_t bits = 0;
  static_assert(sizeof(bits) == sizeof(value), "journal requires 64-bit double");
  memcpy(&bits, &value, sizeof(bits));
  write_u32(writer, static_cast<uint32_t>(bits));
  write_u32(writer, static_cast<uint32_t>(bits >> 32U));
}

inline double read_double(Reader& reader) {
  const uint64_t low = read_u32(reader);
  const uint64_t bits = low | (static_cast<uint64_t>(read_u32(reader)) << 32U);
  double value = NAN;
  if (reader.ok) memcpy(&value, &bits, sizeof(value));
  return value;
}

inline void write_thermal(Writer& writer, const ThermalModelState* state, uint32_t epoch) {
  if (state == nullptr || state->accepted_samples == 0) {
    for (size_t i = 0; i < kLearningJournalThermalBytes / 4; ++i) write_u32(writer, 0);
    return;
  }
  const double values[]{state->theta_loss_scaled,
                        state->theta_heat_scaled,
                        state->covariance_00,
                        state->covariance_01,
                        state->covariance_11,
                        state->information_00,
                        state->information_01,
                        state->information_11,
                        state->residual_mean_k_per_h,
                        state->residual_square_mean_k2_per_h2,
                        state->outside_min_c,
                        state->outside_max_c,
                        state->heat_min_w,
                        state->heat_max_w,
                        state->effective_observation_hours,
                        state->bound_config.loss_feature_scale_kh,
                        state->bound_config.heat_feature_scale_wh};
  for (double value : values) write_double(writer, value);
  write_u32(writer, state->accepted_samples);
  write_u32(writer, state->rejected_samples);
  write_u32(writer, state->reset_count);
  write_u32(writer, epoch);
}

inline bool read_thermal(Reader& reader, ThermalModelState& state, uint32_t& epoch) {
  state = {};
  double* values[]{&state.theta_loss_scaled,
                   &state.theta_heat_scaled,
                   &state.covariance_00,
                   &state.covariance_01,
                   &state.covariance_11,
                   &state.information_00,
                   &state.information_01,
                   &state.information_11,
                   &state.residual_mean_k_per_h,
                   &state.residual_square_mean_k2_per_h2,
                   &state.outside_min_c,
                   &state.outside_max_c,
                   &state.heat_min_w,
                   &state.heat_max_w,
                   &state.effective_observation_hours,
                   &state.bound_config.loss_feature_scale_kh,
                   &state.bound_config.heat_feature_scale_wh};
  for (double* value : values) *value = read_double(reader);
  state.accepted_samples = read_u32(reader);
  state.rejected_samples = read_u32(reader);
  state.reset_count = read_u32(reader);
  epoch = read_u32(reader);
  if (!reader.ok) return false;
  if (state.accepted_samples == 0) return epoch == 0;
  state.initialized = state.config_bound = true;
  state.context_revision = 1;
  // Temporary nonzero clocks for structural validation; replaced on restore.
  state.last_interval_end_monotonic_ms = state.last_observation_monotonic_ms = 1;
  return epoch != 0 && thermal_detail::finite_state(state);
}

inline void write_daily(Writer& writer, const SegmentAccumulator* state) {
  if (state == nullptr || !state->active) {
    for (size_t i = 0; i < kLearningJournalDailyBytes / 4U; ++i) write_u32(writer, 0);
    return;
  }
  write_u32(writer, 1);
  write_u32(writer, state->start_epoch_s);
  write_u32(writer, state->last_epoch_s);
  write_u32(writer, state->context_revision);
  const float values[]{state->room_start_c, state->last_room_c,    state->last_setpoint_c, state->last_outside_c,
                       state->last_heat_w,  state->water_start_c,  state->water_end_c,     state->room_min_c,
                       state->room_max_c,   state->setpoint_min_c, state->setpoint_max_c};
  for (float value : values) write_float(writer, value);
  write_double(writer, state->missing_energy_uncertainty_ws);
  write_double(writer, state->integrated_duration_s);
  write_double(writer, state->room_integral);
  write_double(writer, state->setpoint_integral);
  write_double(writer, state->outside_integral);
  write_double(writer, state->heat_integral);
  for (float value : state->hourly_effective_outside_c) write_float(writer, value);
  write_double(writer, state->hour_effective_integral);
  write_double(writer, state->trend_w);
  write_double(writer, state->trend_wt);
  write_double(writer, state->trend_wtt);
  write_double(writer, state->trend_wr);
  write_double(writer, state->trend_wtr);
}

inline bool read_daily(Reader& reader, SegmentAccumulator& state) {
  state = {};
  const uint32_t active = read_u32(reader);
  if (active == 0) {
    for (size_t i = 1; i < kLearningJournalDailyBytes / 4U; ++i)
      if (read_u32(reader) != 0) return false;
    return reader.ok;
  }
  if (active != 1) return false;
  state.active = true;
  state.start_epoch_s = read_u32(reader);
  state.last_epoch_s = read_u32(reader);
  state.context_revision = read_u32(reader);
  float* values[]{&state.room_start_c, &state.last_room_c,    &state.last_setpoint_c, &state.last_outside_c,
                  &state.last_heat_w,  &state.water_start_c,  &state.water_end_c,     &state.room_min_c,
                  &state.room_max_c,   &state.setpoint_min_c, &state.setpoint_max_c};
  for (float* value : values) *value = read_float(reader);
  state.missing_energy_uncertainty_ws = read_double(reader);
  state.integrated_duration_s = read_double(reader);
  state.room_integral = read_double(reader);
  state.setpoint_integral = read_double(reader);
  state.outside_integral = read_double(reader);
  state.heat_integral = read_double(reader);
  for (float& value : state.hourly_effective_outside_c) value = read_float(reader);
  state.hour_effective_integral = read_double(reader);
  state.trend_w = read_double(reader);
  state.trend_wt = read_double(reader);
  state.trend_wtt = read_double(reader);
  state.trend_wr = read_double(reader);
  state.trend_wtr = read_double(reader);
  if (!reader.ok || !isfinite(state.integrated_duration_s) || state.integrated_duration_s < 0.0 ||
      state.integrated_duration_s >= static_cast<double>(kSegmentDurationMs) / 1000.0)
    return false;
  state.carried_duration_ms = static_cast<uint64_t>(llround(state.integrated_duration_s * 1000.0));
  state.start_monotonic_ms = state.last_monotonic_ms = 1;
  state.source_gap_pending = true;
  state.last_gap_observation_ms = 1;
  state.restart_pending = true;
  return true;
}

inline void write_record(Writer& writer, const SegmentRecord& record) {
  write_u32(writer, record.start_epoch_s);
  write_u32(writer, record.end_epoch_s);
  write_u32(writer, record.duration_s);
  write_u32(writer, record.context_revision);
  write_float(writer, record.mean_room_c);
  write_float(writer, record.mean_setpoint_c);
  write_float(writer, record.mean_outside_c);
  write_float(writer, record.mean_heat_w);
  write_float(writer, record.room_trend_k_per_h);
  if (is_daily_record(record)) {
    for (int16_t value : record.effective_outside_profile_centi) write_u16(writer, static_cast<uint16_t>(value));
  } else {
    write_float(writer, record.room_range_k);
    write_float(writer, record.setpoint_range_c);
    write_float(writer, record.water_start_c);
    write_float(writer, record.water_end_c);
  }
}

inline SegmentRecord read_record(Reader& reader, uint16_t schema) {
  SegmentRecord record;
  record.start_epoch_s = read_u32(reader);
  record.end_epoch_s = read_u32(reader);
  record.duration_s = read_u32(reader);
  record.context_revision = read_u32(reader);
  record.mean_room_c = read_float(reader);
  record.mean_setpoint_c = read_float(reader);
  record.mean_outside_c = read_float(reader);
  record.mean_heat_w = read_float(reader);
  record.room_trend_k_per_h = read_float(reader);
  if (schema >= 6 && is_daily_record(record)) {
    for (int16_t& value : record.effective_outside_profile_centi) {
      const uint16_t bits = read_u16(reader);
      const int32_t signed_value = bits <= INT16_MAX ? static_cast<int32_t>(bits) : static_cast<int32_t>(bits) - 65536;
      value = static_cast<int16_t>(signed_value);
    }
  } else {
    record.room_range_k = read_float(reader);
    record.setpoint_range_c = read_float(reader);
    record.water_start_c = read_float(reader);
    record.water_end_c = read_float(reader);
  }
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
  metadata.schema_version = schema;
  const uint16_t header_size = read_u16(reader);
  const uint32_t encoded_size = read_u32(reader);
  metadata.sequence = read_u32(reader);
  metadata.created_epoch_s = read_u32(reader);
  metadata.algorithm_version = read_u16(reader);
  metadata.record_count = read_u16(reader);
  metadata.context_size = read_u16(reader);
  const uint16_t reserved = read_u16(reader);
  metadata.context_revision = read_u32(reader);
  metadata.encoded_size = encoded_size;
  if (!reader.ok || magic != kLearningJournalMagic || (schema < 4 || schema > kLearningJournalSchemaVersion) ||
      header_size != kLearningJournalHeaderBytes ||
      metadata.algorithm_version < kEarliestRestorableLearningAlgorithmVersion ||
      metadata.algorithm_version > kLearningAlgorithmVersion || reserved != 0)
    return LearningJournalStatus::INVALID_SCHEMA;
  if (metadata.record_count > kMaxSegmentRecords) return LearningJournalStatus::INVALID_COUNT;
  if (encoded_size != slot.size || metadata.context_size == 0 || metadata.context_size > kMaxPassiveContextBytes ||
      encoded_size != kLearningJournalHeaderBytes + metadata.context_size +
                          static_cast<size_t>(metadata.record_count) * kLearningJournalRecordBytes +
                          (schema >= 5 ? kLearningJournalThermalBytes : 0) +
                          (schema >= 7 ? kLearningJournalDailyBytes : 0) + kLearningJournalCrcBytes)
    return LearningJournalStatus::INVALID_LENGTH;
  if (metadata.sequence == 0 || metadata.created_epoch_s == 0) return LearningJournalStatus::INVALID_SEQUENCE;
  if (metadata.created_epoch_s > now_epoch_s) return LearningJournalStatus::TIME_DISCONTINUITY;
  if (metadata.context_revision == 0) return LearningJournalStatus::INVALID_SCHEMA;
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
  // Calibration is a collection boundary, not ownership of retained data.
  // Legacy schema-4 contexts remain readable after changing selected sources.
  reader.position = kLearningJournalHeaderBytes + metadata.context_size;
  uint32_t previous_end_epoch_s = 0;
  for (uint16_t index = 0; index < metadata.record_count; ++index) {
    SegmentRecord record = read_record(reader, schema);
    // Schemas 4/5 always contain the original four-hour float layout. A
    // forged daily duration must not make those bytes a valid daily profile.
    if (!reader.ok || (schema < 6 && is_daily_record(record)) ||
        validate_segment_record(record, quality) != LearningStatus::OK)
      return LearningJournalStatus::INVALID_RECORD;
    if (record.context_revision != metadata.context_revision) return LearningJournalStatus::INVALID_RECORD;
    if (record.end_epoch_s > now_epoch_s) return LearningJournalStatus::TIME_DISCONTINUITY;
    // Age is record selection, not corruption of the slot or its thermal model.
    if (index > 0 && record.start_epoch_s < previous_end_epoch_s) return LearningJournalStatus::TIME_DISCONTINUITY;
    previous_end_epoch_s = record.end_epoch_s;
    if (output_records != nullptr) output_records[index] = record;
  }
  if (schema >= 5) {
    ThermalModelState thermal;
    uint32_t thermal_epoch = 0;
    if (!read_thermal(reader, thermal, thermal_epoch) || thermal_epoch > metadata.created_epoch_s)
      return LearningJournalStatus::INVALID_RECORD;
  }
  if (schema >= 7) {
    SegmentAccumulator daily;
    // Eligibility belongs to the daily consumer: a changed quality limit must
    // not discard otherwise valid historical records and thermal parameters.
    if (!read_daily(reader, daily) ||
        (daily.active && (daily.context_revision != metadata.context_revision || daily.start_epoch_s == 0 ||
                          daily.last_epoch_s < daily.start_epoch_s || daily.last_epoch_s > metadata.created_epoch_s ||
                          daily.start_epoch_s < previous_end_epoch_s)))
      return LearningJournalStatus::INVALID_RECORD;
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
                                                     size_t output_capacity, size_t& output_size,
                                                     const ThermalModelState* thermal = nullptr,
                                                     uint32_t thermal_epoch = 0,
                                                     const SegmentAccumulator* daily = nullptr) {
  using namespace learning_journal_detail;
  output_size = 0;
  if (sequence == 0 || created_epoch_s == 0 || output == nullptr || state.context.bytes == nullptr ||
      state.context.size == 0 || state.context.size > kMaxPassiveContextBytes ||
      (state.record_count > 0 && state.records == nullptr) || state.record_count > kMaxSegmentRecords ||
      state.context.context_revision == 0 || !valid_quality_config(quality))
    return LearningJournalStatus::INVALID_ARGUMENT;
  if (thermal != nullptr && thermal->accepted_samples > 0 &&
      (!thermal_detail::finite_state(*thermal) || thermal_epoch == 0 || thermal_epoch > created_epoch_s))
    return LearningJournalStatus::INVALID_ARGUMENT;
  if (daily != nullptr && daily->active &&
      (!valid_daily_checkpoint(*daily, quality) || daily->context_revision != state.context.context_revision ||
       daily->last_epoch_s > created_epoch_s))
    return LearningJournalStatus::INVALID_ARGUMENT;
  const size_t required = kLearningJournalHeaderBytes + state.context.size +
                          state.record_count * kLearningJournalRecordBytes + kLearningJournalThermalBytes +
                          kLearningJournalDailyBytes + kLearningJournalCrcBytes;
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
  write_u32(writer, state.context.context_revision);
  if (!writer.ok || writer.position != kLearningJournalHeaderBytes) return LearningJournalStatus::BUFFER_TOO_SMALL;
  memcpy(output + writer.position, state.context.bytes, state.context.size);
  writer.position += state.context.size;
  uint32_t previous_end_epoch_s = 0;
  for (size_t index = 0; index < state.record_count; ++index) {
    const SegmentRecord& record = state.records[index];
    if (validate_segment_record(record, quality) != LearningStatus::OK ||
        record.context_revision != state.context.context_revision || record.end_epoch_s > created_epoch_s ||
        created_epoch_s - record.end_epoch_s > kMaxRecordAgeS ||
        (index > 0 && record.start_epoch_s < previous_end_epoch_s))
      return LearningJournalStatus::INVALID_RECORD;
    previous_end_epoch_s = record.end_epoch_s;
    write_record(writer, record);
  }
  if (daily != nullptr && daily->active && daily->start_epoch_s < previous_end_epoch_s)
    return LearningJournalStatus::INVALID_RECORD;
  write_thermal(writer, thermal, thermal_epoch);
  write_daily(writer, daily);
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

  bool has_daily_checkpoint() const { return daily_checkpoint_epoch() != 0; }

  uint32_t daily_checkpoint_epoch() const {
    if (slot.bytes == nullptr || slot.size < kLearningJournalHeaderBytes + kLearningJournalCrcBytes) return 0;
    learning_journal_detail::Reader reader{slot.bytes, slot.size};
    reader.position = 4;
    if (learning_journal_detail::read_u16(reader) != 7) return 0;
    reader.position = kLearningJournalHeaderBytes + context_size + record_count * kLearningJournalRecordBytes +
                      kLearningJournalThermalBytes;
    if (learning_journal_detail::read_u32(reader) != 1) return 0;
    learning_journal_detail::read_u32(reader);  // start epoch
    const uint32_t epoch = learning_journal_detail::read_u32(reader);
    return reader.ok ? epoch : 0;
  }

  uint32_t daily_checkpoint_start_epoch() const {
    if (!has_daily_checkpoint()) return 0;
    learning_journal_detail::Reader reader{slot.bytes, slot.size};
    reader.position = kLearningJournalHeaderBytes + context_size + record_count * kLearningJournalRecordBytes +
                      kLearningJournalThermalBytes + 4U;
    return learning_journal_detail::read_u32(reader);
  }

  uint32_t daily_checkpoint_elapsed_s() const {
    if (!has_daily_checkpoint()) return 0;
    learning_journal_detail::Reader reader{slot.bytes, slot.size};
    // Four u32 fields, eleven floats and the uncertainty double precede duration.
    reader.position = kLearningJournalHeaderBytes + context_size + record_count * kLearningJournalRecordBytes +
                      kLearningJournalThermalBytes + 4U * 4U + 11U * 4U + 8U;
    const double duration = learning_journal_detail::read_double(reader);
    if (!reader.ok || !isfinite(duration) || duration < 0.0 || duration >= 86400.0) return 0;
    return static_cast<uint32_t>(duration);
  }

  bool restore_daily(SegmentAccumulator& output, const PassiveContextView& context,
                     const QualityConfig& quality) const {
    if (!has_daily_checkpoint() || context.context_revision == 0 ||
        !learning_journal_detail::daily_context_matches(slot.bytes + kLearningJournalHeaderBytes, context_size,
                                                        context.bytes, context.size))
      return false;
    learning_journal_detail::Reader reader{slot.bytes, slot.size};
    reader.position = kLearningJournalHeaderBytes + context_size + record_count * kLearningJournalRecordBytes +
                      kLearningJournalThermalBytes;
    SegmentAccumulator state;
    if (!learning_journal_detail::read_daily(reader, state) || !valid_daily_checkpoint(state, quality)) return false;
    state.context_revision = context.context_revision;
    output = state;
    return true;
  }

  bool restore_thermal(ThermalModelState& output, const ThermalModelConfig& config, uint64_t now_ms, uint32_t revision,
                       uint32_t& thermal_epoch) const {
    ThermalModelState state;
    learning_journal_detail::Reader reader{slot.bytes, slot.size};
    reader.position = 4;
    if (learning_journal_detail::read_u16(reader) < 5) return false;
    reader.position = kLearningJournalHeaderBytes + context_size + record_count * kLearningJournalRecordBytes;
    if (!learning_journal_detail::read_thermal(reader, state, thermal_epoch) || state.accepted_samples == 0 ||
        state.bound_config.loss_feature_scale_kh != config.loss_feature_scale_kh ||
        state.bound_config.heat_feature_scale_wh != config.heat_feature_scale_wh || now_ms == 0)
      return false;
    state.bound_config = config;
    state.context_revision = revision;
    state.last_interval_end_monotonic_ms = state.last_observation_monotonic_ms = now_ms;
    // Stored parameters remain visible; readiness requires a fresh live interval.
    state.recent_data_valid = false;
    if (!thermal_detail::finite_state(state)) return false;
    output = state;
    return true;
  }

  SegmentRecord operator[](size_t index) const {
    learning_journal_detail::Reader reader{slot.bytes, slot.size};
    reader.position = 4;
    const uint16_t schema = learning_journal_detail::read_u16(reader);
    reader.position = kLearningJournalHeaderBytes + context_size + index * kLearningJournalRecordBytes;
    return learning_journal_detail::read_record(reader, schema);
  }
};

}  // namespace oq_power_house::learning

#endif  // OQ_PH_LEARNING_CORE_AVAILABLE
