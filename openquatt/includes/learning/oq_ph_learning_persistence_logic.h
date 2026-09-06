#pragma once

#include "oq_ph_learning_platform.h"

#if OQ_PH_LEARNING_CORE_AVAILABLE

#include <stdint.h>

namespace oq_power_house::learning {

constexpr uint8_t kJournalMaxTransientAttempts = 5U;
constexpr uint64_t kJournalRetryInitialMs = 5000ULL;
constexpr uint64_t kJournalRetryMaximumMs = 60000ULL;
constexpr uint64_t kJournalClockRetryMs = 30000ULL;
constexpr uint64_t kJournalSaveIntervalMs = 60ULL * 60ULL * 1000ULL;

inline uint64_t journal_retry_delay_ms(uint8_t failure_count) {
  uint64_t delay = kJournalRetryInitialMs;
  for (uint8_t attempt = 1U; attempt < failure_count && delay < kJournalRetryMaximumMs; ++attempt) {
    delay *= 2U;
    if (delay > kJournalRetryMaximumMs) delay = kJournalRetryMaximumMs;
  }
  return delay;
}

inline uint64_t journal_retry_at(uint64_t now_ms, uint64_t delay_ms) {
  return now_ms > UINT64_MAX - delay_ms ? UINT64_MAX : now_ms + delay_ms;
}

enum class JournalRestorePhase : uint8_t {
  PENDING = 0,
  WAITING_FOR_IO,
  WAITING_FOR_CLOCK,
  COMPLETE,
  RETRY_EXHAUSTED,
};

struct JournalRestoreRetryState {
  JournalRestorePhase phase = JournalRestorePhase::PENDING;
  uint8_t transient_failures = 0;
  uint64_t retry_at_ms = 0;
};

inline bool journal_restore_due(const JournalRestoreRetryState& state, uint64_t now_ms) {
  return state.phase == JournalRestorePhase::PENDING || ((state.phase == JournalRestorePhase::WAITING_FOR_IO ||
                                                          state.phase == JournalRestorePhase::WAITING_FOR_CLOCK) &&
                                                         now_ms >= state.retry_at_ms);
}

inline void journal_restore_io_failed(JournalRestoreRetryState& state, uint64_t now_ms) {
  if (state.transient_failures < UINT8_MAX) ++state.transient_failures;
  if (state.transient_failures >= kJournalMaxTransientAttempts) {
    state.phase = JournalRestorePhase::RETRY_EXHAUSTED;
    state.retry_at_ms = UINT64_MAX;
    return;
  }
  state.phase = JournalRestorePhase::WAITING_FOR_IO;
  state.retry_at_ms = journal_retry_at(now_ms, journal_retry_delay_ms(state.transient_failures));
}

inline void journal_restore_clock_not_ready(JournalRestoreRetryState& state, uint64_t now_ms) {
  state.phase = JournalRestorePhase::WAITING_FOR_CLOCK;
  state.retry_at_ms = journal_retry_at(now_ms, kJournalClockRetryMs);
}

inline void journal_restore_complete(JournalRestoreRetryState& state) {
  state.phase = JournalRestorePhase::COMPLETE;
  state.retry_at_ms = 0;
}

inline bool journal_restore_blocks_save(const JournalRestoreRetryState& state) {
  return state.phase != JournalRestorePhase::COMPLETE;
}

struct JournalSaveRetryState {
  uint8_t transient_failures = 0;
  uint64_t retry_at_ms = 0;
  bool exhausted = false;
};

inline bool journal_save_due(const JournalSaveRetryState& state, uint64_t now_ms, uint64_t last_success_ms) {
  if (state.exhausted) return false;
  if (state.transient_failures != 0U) return now_ms >= state.retry_at_ms;
  return last_success_ms == 0U || (now_ms >= last_success_ms && now_ms - last_success_ms >= kJournalSaveIntervalMs);
}

inline void journal_save_failed(JournalSaveRetryState& state, uint64_t now_ms) {
  if (state.transient_failures < UINT8_MAX) ++state.transient_failures;
  if (state.transient_failures >= kJournalMaxTransientAttempts) {
    state.exhausted = true;
    state.retry_at_ms = UINT64_MAX;
    return;
  }
  state.retry_at_ms = journal_retry_at(now_ms, journal_retry_delay_ms(state.transient_failures));
}

inline void journal_save_succeeded(JournalSaveRetryState& state) { state = {}; }

enum class JournalWriteAttemptResult : uint8_t {
  OK = 0,
  ERASE_FAILED,
  WRITE_FAILED,
  READBACK_FAILED,
  VERIFY_FAILED,
  VALIDATION_FAILED,
  COMMIT_MARKER_FAILED,
};

template <typename Erase, typename Write, typename ReadBack, typename Verify, typename Validate, typename Commit>
inline JournalWriteAttemptResult execute_journal_write_attempt(Erase erase, Write write, ReadBack read_back,
                                                               Verify verify, Validate validate, Commit commit) {
  if (!erase()) return JournalWriteAttemptResult::ERASE_FAILED;
  if (!write()) return JournalWriteAttemptResult::WRITE_FAILED;
  if (!read_back()) return JournalWriteAttemptResult::READBACK_FAILED;
  if (!verify()) return JournalWriteAttemptResult::VERIFY_FAILED;
  if (!validate()) return JournalWriteAttemptResult::VALIDATION_FAILED;
  if (!commit()) return JournalWriteAttemptResult::COMMIT_MARKER_FAILED;
  return JournalWriteAttemptResult::OK;
}

struct JournalResetOutcome {
  bool dirty_marked = false;
  bool slots_erased = false;
  bool headers_invalidated = false;
  bool dirty_clear_verified = false;

  bool prevents_restore() const { return dirty_marked || slots_erased || headers_invalidated; }
  bool data_inaccessible() const { return slots_erased || headers_invalidated; }
  bool erase_verified() const { return slots_erased; }
  bool complete() const { return slots_erased && dirty_clear_verified; }
};

template <typename SetDirty, typename EraseAndVerify, typename InvalidateAndVerify>
inline JournalResetOutcome execute_journal_reset(SetDirty set_dirty, EraseAndVerify erase_and_verify,
                                                 InvalidateAndVerify invalidate_and_verify) {
  JournalResetOutcome outcome;
  outcome.dirty_marked = set_dirty(true);
  outcome.slots_erased = erase_and_verify();
  if (!outcome.slots_erased) outcome.headers_invalidated = invalidate_and_verify();
  // Header invalidation prevents restore but is not deletion. Retain the dirty
  // marker so the next boot retries the full erase before allowing restore.
  if (outcome.headers_invalidated && !outcome.dirty_marked) outcome.dirty_marked = set_dirty(true);
  if (outcome.slots_erased) outcome.dirty_clear_verified = set_dirty(false);
  return outcome;
}

}  // namespace oq_power_house::learning

#endif  // OQ_PH_LEARNING_CORE_AVAILABLE
