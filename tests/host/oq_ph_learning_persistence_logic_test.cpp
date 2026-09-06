#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "../../openquatt/includes/learning/oq_ph_learning_persistence_logic.h"

namespace {
using namespace oq_power_house::learning;

struct ResetBackend {
  bool dirty = false;
  bool history_present = true;
  bool mark_dirty_ok = true;
  unsigned mark_dirty_failures_remaining = 0;
  bool clear_dirty_ok = true;
  bool erase_ok = true;
  bool invalidate_ok = true;
  unsigned erase_calls = 0;
  unsigned invalidate_calls = 0;

  bool set_dirty(bool value) {
    if (value && mark_dirty_failures_remaining != 0U) {
      --mark_dirty_failures_remaining;
      return false;
    }
    if ((value && !mark_dirty_ok) || (!value && !clear_dirty_ok)) return false;
    dirty = value;
    return true;
  }

  bool erase_and_verify() {
    ++erase_calls;
    if (!erase_ok) return false;
    history_present = false;
    return true;
  }

  bool invalidate_and_verify() {
    ++invalidate_calls;
    if (!invalidate_ok) return false;
    history_present = false;
    return true;
  }
};

JournalResetOutcome reset(ResetBackend& backend) {
  return execute_journal_reset([&backend](bool dirty) { return backend.set_dirty(dirty); },
                               [&backend]() { return backend.erase_and_verify(); },
                               [&backend]() { return backend.invalidate_and_verify(); });
}

void test_reset_after_runtime_allocation_failure_survives_reboot() {
  ResetBackend backend;
  backend.erase_ok = false;
  backend.invalidate_ok = false;

  // The request runs without learner storage. A durable dirty marker prevents
  // the still-present journal from being restored after the interrupted erase.
  const auto interrupted = reset(backend);
  assert(interrupted.dirty_marked);
  assert(!interrupted.erase_verified());
  assert(interrupted.prevents_restore());
  assert(backend.dirty && backend.history_present);

  // The next boot observes the marker and retries deletion before restore.
  backend.erase_ok = true;
  if (backend.dirty) {
    const auto reboot_cleanup = reset(backend);
    assert(reboot_cleanup.complete());
  }
  assert(!backend.dirty);
  assert(!backend.history_present);
}

void test_reset_uses_verified_header_invalidation_when_erase_or_nvs_fails() {
  ResetBackend backend;
  backend.mark_dirty_ok = false;
  backend.clear_dirty_ok = false;
  backend.erase_ok = false;
  backend.invalidate_ok = true;
  const auto fallback = reset(backend);
  assert(!fallback.dirty_marked);
  assert(!fallback.erase_verified());
  assert(fallback.data_inaccessible());
  assert(fallback.prevents_restore());
  assert(!fallback.complete());
  assert(!backend.history_present);
  assert(backend.erase_calls == 1U && backend.invalidate_calls == 1U);

  backend = {};
  backend.mark_dirty_failures_remaining = 1U;
  backend.erase_ok = false;
  const auto retried_marker = reset(backend);
  assert(retried_marker.headers_invalidated && retried_marker.dirty_marked);
  assert(backend.dirty && !backend.history_present);

  backend = {};
  backend.mark_dirty_ok = false;
  backend.erase_ok = false;
  backend.invalidate_ok = false;
  const auto failed = reset(backend);
  assert(!failed.prevents_restore());
  assert(backend.history_present);
}

void test_restore_retries_transient_reads_and_blocks_writes_if_exhausted() {
  JournalRestoreRetryState state;
  uint64_t now_ms = 1000;
  assert(journal_restore_due(state, now_ms));
  assert(journal_restore_blocks_save(state));

  for (uint8_t failure = 1; failure <= kJournalMaxTransientAttempts; ++failure) {
    journal_restore_io_failed(state, now_ms);  // injected partition-read failure
    if (failure < kJournalMaxTransientAttempts) {
      assert(state.phase == JournalRestorePhase::WAITING_FOR_IO);
      assert(!journal_restore_due(state, state.retry_at_ms - 1U));
      now_ms = state.retry_at_ms;
      assert(journal_restore_due(state, now_ms));
    }
  }
  assert(state.phase == JournalRestorePhase::RETRY_EXHAUSTED);
  assert(journal_restore_blocks_save(state));
  assert(!journal_restore_due(state, UINT64_MAX));
}

void test_restore_waits_for_utc_correction_without_consuming_io_budget() {
  JournalRestoreRetryState state;
  journal_restore_io_failed(state, 1000);  // one transient read failure first
  const uint8_t io_failures = state.transient_failures;
  const uint64_t retry_ms = state.retry_at_ms;
  assert(journal_restore_due(state, retry_ms));

  journal_restore_clock_not_ready(state, retry_ms);  // injected future-created journal
  assert(state.phase == JournalRestorePhase::WAITING_FOR_CLOCK);
  assert(state.transient_failures == io_failures);
  assert(!journal_restore_due(state, state.retry_at_ms - 1U));
  assert(journal_restore_due(state, state.retry_at_ms));

  journal_restore_complete(state);  // UTC corrected; same cached slot now validates
  assert(state.phase == JournalRestorePhase::COMPLETE);
  assert(!journal_restore_blocks_save(state));
}

void test_save_retries_write_and_readback_failures_before_hourly_cooldown() {
  JournalSaveRetryState state;
  uint64_t now_ms = 1000;
  assert(journal_save_due(state, now_ms, 0));

  journal_save_failed(state, now_ms);  // injected write failure
  assert(!journal_save_due(state, state.retry_at_ms - 1U, 0));
  now_ms = state.retry_at_ms;
  assert(journal_save_due(state, now_ms, 0));

  journal_save_failed(state, now_ms);  // injected readback failure
  now_ms = state.retry_at_ms;
  assert(journal_save_due(state, now_ms, 0));

  journal_save_succeeded(state);
  const uint64_t last_success_ms = now_ms;
  assert(!journal_save_due(state, now_ms + kJournalSaveIntervalMs - 1U, last_success_ms));
  assert(journal_save_due(state, now_ms + kJournalSaveIntervalMs, last_success_ms));
}

void test_write_attempt_failure_injection_stops_at_exact_stage() {
  const JournalWriteAttemptResult failures[]{
      JournalWriteAttemptResult::ERASE_FAILED,      JournalWriteAttemptResult::WRITE_FAILED,
      JournalWriteAttemptResult::READBACK_FAILED,   JournalWriteAttemptResult::VERIFY_FAILED,
      JournalWriteAttemptResult::VALIDATION_FAILED, JournalWriteAttemptResult::COMMIT_MARKER_FAILED,
  };
  for (size_t failure_index = 0; failure_index < sizeof(failures) / sizeof(failures[0]); ++failure_index) {
    size_t calls = 0;
    const auto step = [&calls, failure_index](size_t stage) {
      ++calls;
      return stage != failure_index;
    };
    const auto result = execute_journal_write_attempt([&step]() { return step(0); }, [&step]() { return step(1); },
                                                      [&step]() { return step(2); }, [&step]() { return step(3); },
                                                      [&step]() { return step(4); }, [&step]() { return step(5); });
    assert(result == failures[failure_index]);
    assert(calls == failure_index + 1U);
  }

  size_t calls = 0;
  const auto success = [&calls]() {
    ++calls;
    return true;
  };
  assert(execute_journal_write_attempt(success, success, success, success, success, success) ==
         JournalWriteAttemptResult::OK);
  assert(calls == 6U);
}

void test_save_failure_budget_is_bounded() {
  JournalSaveRetryState state;
  uint64_t now_ms = 1000;
  for (uint8_t failure = 0; failure < kJournalMaxTransientAttempts; ++failure) {
    journal_save_failed(state, now_ms);  // erase/write/read/readback/commit failure injection
    now_ms = state.retry_at_ms;
  }
  assert(state.exhausted);
  assert(!journal_save_due(state, UINT64_MAX, 0));
}

}  // namespace

int main() {
  test_reset_after_runtime_allocation_failure_survives_reboot();
  test_reset_uses_verified_header_invalidation_when_erase_or_nvs_fails();
  test_restore_retries_transient_reads_and_blocks_writes_if_exhausted();
  test_restore_waits_for_utc_correction_without_consuming_io_budget();
  test_save_retries_write_and_readback_failures_before_hourly_cooldown();
  test_write_attempt_failure_injection_stops_at_exact_stage();
  test_save_failure_budget_is_bounded();
}
