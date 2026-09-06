#pragma once

#include "oq_ph_learning_platform.h"

#if OQ_PH_LEARNING_CORE_AVAILABLE

#include "oq_ph_learning_journal.h"

namespace oq_power_house::learning {

constexpr uint64_t kJournalSaveIntervalMs = 60ULL * 60ULL * 1000ULL;

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
  uint32_t persisted_revision = 0;
  uint64_t last_write_ms = 0;
  const char* status = "not_initialized";

  void setup(bool storage_available) {
    available = storage_available;
    status = available ? "waiting_for_context" : "storage_unavailable";
  }

  void request_reset() { status = "reset_pending"; }

  template <typename Read>
  bool load(const PassiveContextView& context, const QualityConfig& quality, uint32_t epoch, Read read,
            LearningJournalRecords& records) {
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
    status = "restored_batch_rls_restarts";
    return true;
  }

  bool save_due(uint64_t now_ms, size_t record_count, uint32_t revision) const {
    return available && loaded && (record_count != persisted_records || revision != persisted_revision) &&
           (last_write_ms == 0 || (now_ms >= last_write_ms && now_ms - last_write_ms >= kJournalSaveIntervalMs));
  }

  template <typename Erase, typename Write, typename Read>
  bool save(const LearningDatasetView& dataset, const QualityConfig& quality, uint32_t epoch, uint64_t now_ms,
            uint32_t revision, Erase erase, Write write, Read read) {
    if (!save_due(now_ms, dataset.record_count, revision)) return false;
    if (sequence == UINT32_MAX) return fail("sequence_exhausted");
    const int slot = active_slot == 0 ? 1 : 0;
    size_t size = 0;
    if (encode_learning_journal(dataset, quality, sequence + 1, epoch, bytes[slot], sizeof(bytes[slot]), size) !=
        LearningJournalStatus::OK)
      return fail("encode_failed");
    // The previous flash slot is untouched. Reuse its RAM cache for readback.
    if (!erase(slot) || !write(slot, bytes[slot], size) || !read(slot, bytes[1 - slot], size) ||
        memcmp(bytes[slot], bytes[1 - slot], size) != 0)
      return fail("save_failed");
    active_slot = slot;
    ++sequence;
    persisted_records = dataset.record_count;
    persisted_revision = revision;
    last_write_ms = now_ms;
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
    persisted_revision = 0;
    last_write_ms = 0;
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

}  // namespace oq_power_house::learning

#endif  // OQ_PH_LEARNING_CORE_AVAILABLE
