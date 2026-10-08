#pragma once

#include "oq_ph_learning_platform.h"

#if OQ_PH_LEARNING_CORE_AVAILABLE

#include "oq_ph_learning_journal.h"

namespace oq_power_house::learning {

constexpr uint64_t kJournalSaveIntervalMs = 60ULL * 60ULL * 1000ULL;
constexpr uint64_t kJournalDailySaveIntervalMs = 15ULL * 60ULL * 1000ULL;

// Reset has one outcome: both slots erased, or an explicit failure. There are
// no alternate tombstones, NVS transactions or background recovery attempts.
template <typename EraseAll, typename Read>
inline bool erase_learning_journal(EraseAll erase_all, Read read) {
  if (!erase_all()) return false;
  uint8_t header[kLearningJournalHeaderBytes];
  for (size_t slot = 0; slot < 2; ++slot) {
    if (!read(slot, header, sizeof(header))) return false;
    for (uint8_t byte : header)
      if (byte != 0xFFU) return false;
  }
  return true;
}

// Concrete A/B store. Allocate with the runtime in PSRAM. An I/O failure disables
// persistence for this boot; collecting in RAM continues. No retry state machine.
struct LearningJournalStore {
  uint8_t bytes[2][kLearningJournalMaxBytes]{};
  bool available = false;
  bool loaded = false;
  int active_slot = -1;
  uint32_t sequence = 0;
  size_t persisted_records = 0;
  uint32_t persisted_latest_end_epoch = 0;
  uint32_t persisted_revision = 0;
  uint32_t persisted_thermal_samples = 0;
  uint32_t persisted_daily_checkpoint_epoch = 0;
  uint32_t persisted_daily_start_epoch = 0;
  uint64_t last_write_ms = 0;
  uint64_t last_daily_write_ms = 0;
  const char* status = "not_initialized";

  void setup(bool storage_available) {
    available = storage_available;
    status = available ? "waiting_for_context" : "storage_unavailable";
  }

  void request_reset() { status = "reset_pending"; }

  template <typename Read>
  bool load(const PassiveContextView& context, const QualityConfig& quality, uint32_t epoch, Read read,
            LearningJournalRecords& records, uint64_t now_ms = 0) {
    if (loaded || !available || epoch == 0) return false;
    loaded = true;
    LearningJournalSlotView slots[2];
    for (size_t slot = 0; slot < 2; ++slot) {
      if (!read(slot, bytes[slot], sizeof(bytes[slot]))) return fail("restore_failed");
      learning_journal_detail::Reader header{bytes[slot], sizeof(bytes[slot])};
      header.position = 8;
      const size_t size = learning_journal_detail::read_u32(header);
      slots[slot] = {bytes[slot], size <= sizeof(bytes[slot]) ? size : 0};
    }
    const auto selected = select_learning_journal_slot(slots[0], slots[1], context.bytes, context.size, epoch, quality);
    if (selected.status != LearningJournalStatus::OK) {
      status = "no_compatible_history";
      return false;
    }
    active_slot = selected.selected_slot;
    sequence = selected.metadata.sequence;
    persisted_records = selected.metadata.record_count;
    records = {slots[active_slot], selected.metadata.context_size, selected.metadata.record_count};
    persisted_latest_end_epoch = records.record_count == 0 ? 0 : records[records.record_count - 1].end_epoch_s;
    persisted_daily_checkpoint_epoch = records.daily_checkpoint_epoch();
    persisted_daily_start_epoch = records.daily_checkpoint_start_epoch();
    last_write_ms = last_daily_write_ms = now_ms;
    status = "restored";
    return true;
  }

  bool save_due(uint64_t now_ms, size_t record_count, uint32_t revision, uint32_t thermal_samples = 0,
                uint32_t latest_end_epoch = 0, const SegmentAccumulator* daily = nullptr, bool force = false) const {
    if (!available || !loaded) return false;
    if (force) return true;
    const uint32_t daily_epoch = daily != nullptr && daily->active ? daily->last_epoch_s : 0;
    // Discarding or completing a day must durably clear its old checkpoint,
    // otherwise the next reboot could resurrect the discarded accumulator.
    if (persisted_daily_checkpoint_epoch != 0 &&
        (daily_epoch == 0 || daily->start_epoch_s != persisted_daily_start_epoch))
      return true;
    const bool daily_dirty = daily_epoch != persisted_daily_checkpoint_epoch;
    const bool history_dirty = record_count != persisted_records || latest_end_epoch != persisted_latest_end_epoch ||
                               revision != persisted_revision || thermal_samples != persisted_thermal_samples;
    const bool daily_due = last_daily_write_ms == 0 || (now_ms >= last_daily_write_ms &&
                                                        now_ms - last_daily_write_ms >= kJournalDailySaveIntervalMs);
    const bool history_due =
        last_write_ms == 0 || (now_ms >= last_write_ms && now_ms - last_write_ms >= kJournalSaveIntervalMs);
    return (daily_dirty && daily_due) || (history_dirty && history_due);
  }

  template <typename Erase, typename Write, typename Read>
  bool save(const LearningDatasetView& dataset, const QualityConfig& quality, uint32_t epoch, uint64_t now_ms,
            uint32_t revision, Erase erase, Write write, Read read, const ThermalModelState* thermal = nullptr,
            uint32_t thermal_epoch = 0, const SegmentAccumulator* daily = nullptr, bool force = false) {
    const uint32_t thermal_samples = thermal != nullptr ? thermal->accepted_samples : 0;
    // At full capacity, append+evict changes the newest record while the count
    // stays constant. Completed records are immutable and ordered by time.
    const uint32_t latest_end_epoch =
        dataset.records != nullptr && dataset.record_count > 0 && dataset.record_count <= kMaxSegmentRecords
            ? dataset.records[dataset.record_count - 1].end_epoch_s
            : 0;
    if (!save_due(now_ms, dataset.record_count, revision, thermal_samples, latest_end_epoch, daily, force))
      return false;
    if (sequence == UINT32_MAX) return fail("sequence_exhausted");
    // A break can discard and reseed in one tick. Clear the stored day first;
    // checkpointing the replacement still observes the 15-minute write budget.
    const SegmentAccumulator* checkpoint = daily;
    if (!force && persisted_daily_checkpoint_epoch != 0 && daily != nullptr && daily->active &&
        daily->start_epoch_s != persisted_daily_start_epoch)
      checkpoint = nullptr;
    const int slot = active_slot == 0 ? 1 : 0;
    size_t size = 0;
    if (encode_learning_journal(dataset, quality, sequence + 1, epoch, bytes[slot], sizeof(bytes[slot]), size, thermal,
                                thermal_epoch, checkpoint) != LearningJournalStatus::OK)
      return fail("encode_failed");
    // The previous flash slot is untouched. Reuse its RAM cache for readback.
    if (!erase(slot) || !write(slot, bytes[slot], size) || !read(slot, bytes[1 - slot], size) ||
        memcmp(bytes[slot], bytes[1 - slot], size) != 0)
      return fail("save_failed");
    active_slot = slot;
    ++sequence;
    persisted_records = dataset.record_count;
    persisted_latest_end_epoch = latest_end_epoch;
    persisted_revision = revision;
    persisted_thermal_samples = thermal_samples;
    persisted_daily_checkpoint_epoch = checkpoint != nullptr && checkpoint->active ? checkpoint->last_epoch_s : 0;
    persisted_daily_start_epoch = checkpoint != nullptr && checkpoint->active ? checkpoint->start_epoch_s : 0;
    last_write_ms = now_ms;
    last_daily_write_ms = now_ms;
    status = "saved_verified";
    return true;
  }

  template <typename EraseAll, typename Read>
  bool reset(EraseAll erase_all, Read read) {
    loaded = true;
    if (!erase_learning_journal(erase_all, read)) return fail("reset_failed");
    available = true;
    active_slot = -1;
    sequence = 0;
    persisted_records = 0;
    persisted_latest_end_epoch = 0;
    persisted_revision = 0;
    persisted_thermal_samples = 0;
    persisted_daily_checkpoint_epoch = 0;
    persisted_daily_start_epoch = 0;
    last_write_ms = 0;
    last_daily_write_ms = 0;
    status = "cleared";
    return true;
  }

 private:
  bool fail(const char* reason) {
    available = false;
    status = reason;
    return false;
  }
};

// A one-use software-restart grant, separate from fallible flash invalidation.
// Keep this POD uninitialized in RTC memory; a boot consumes it before collecting.
struct DailyRestartTicket {
  uint32_t magic;
  uint32_t sequence;
  uint32_t checkpoint_epoch;
  uint32_t journal_crc;

  void clear() volatile { magic = 0; }

  bool matches(uint32_t saved_sequence, const LearningJournalRecords& records) const {
    if (magic != 0x4F514452U || sequence != saved_sequence || !records.has_daily_checkpoint() ||
        checkpoint_epoch != records.daily_checkpoint_epoch() || records.slot.size < kLearningJournalCrcBytes)
      return false;
    learning_journal_detail::Reader reader{records.slot.bytes, records.slot.size};
    reader.position = records.slot.size - kLearningJournalCrcBytes;
    return journal_crc == learning_journal_detail::read_u32(reader) && reader.ok;
  }

  DailyRestartTicket consume(bool software_restart) volatile {
    const DailyRestartTicket result = software_restart && magic == 0x4F514452U
                                          ? DailyRestartTicket{magic, sequence, checkpoint_epoch, journal_crc}
                                          : DailyRestartTicket{};
    clear();
    return result;
  }

  void arm(const LearningJournalStore& store, bool verified_write) volatile {
    clear();
    if (!verified_write || !store.available || store.active_slot < 0 || store.active_slot > 1 ||
        store.persisted_daily_checkpoint_epoch == 0)
      return;
    learning_journal_detail::Reader reader{store.bytes[store.active_slot], kLearningJournalMaxBytes};
    reader.position = 8;
    const size_t size = learning_journal_detail::read_u32(reader);
    if (!reader.ok || size < kLearningJournalCrcBytes || size > reader.size) return;
    reader.position = size - kLearningJournalCrcBytes;
    journal_crc = learning_journal_detail::read_u32(reader);
    sequence = store.sequence;
    checkpoint_epoch = store.persisted_daily_checkpoint_epoch;
    if (reader.ok) magic = 0x4F514452U;
  }
};
static_assert(sizeof(DailyRestartTicket) == 16U, "Daily restart grant must stay small");

}  // namespace oq_power_house::learning

#endif  // OQ_PH_LEARNING_CORE_AVAILABLE
