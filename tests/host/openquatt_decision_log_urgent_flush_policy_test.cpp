#include <cassert>
#include <cstdint>

#include "components/openquatt_decision_log/OpenQuattUrgentFlushPolicy.h"

using esphome::openquatt_decision_log::UrgentFlushPolicy;

int main() {
  constexpr uint64_t kSecond = 1000000ULL;
  constexpr uint64_t kCoalesce = 2ULL * kSecond;
  constexpr uint64_t kMinInterval = 15ULL * kSecond;
  constexpr uint64_t kRetry = 30ULL * kSecond;

  UrgentFlushPolicy policy;
  policy.request(1ULL * kSecond, 10U);
  policy.request(2ULL * kSecond, 11U);
  assert(policy.pending());
  assert(policy.requested_event_seq() == 11U);
  assert(!policy.should_attempt(2ULL * kSecond, kCoalesce, kMinInterval));
  assert(policy.should_attempt(3ULL * kSecond, kCoalesce, kMinInterval));

  policy.mark_attempt(3ULL * kSecond);
  assert(!policy.should_attempt(17ULL * kSecond, kCoalesce, kMinInterval));

  // A newer safety edge arriving while the previous target is written stays
  // protected and receives a fresh coalescing window.
  policy.request(4ULL * kSecond, 12U);
  assert(policy.protects_unpersisted_sequence(11U, 9U));
  assert(policy.protects_unpersisted_sequence(12U, 9U));
  policy.mark_target_persisted(5ULL * kSecond, 11U);
  assert(policy.pending());
  assert(policy.requested_event_seq() == 12U);
  assert(!policy.should_attempt(19ULL * kSecond, kCoalesce, kMinInterval));
  assert(policy.should_attempt(20ULL * kSecond, kCoalesce, kMinInterval));

  policy.mark_failure(20ULL * kSecond, kRetry);
  assert(!policy.should_attempt(49ULL * kSecond, kCoalesce, kMinInterval));
  assert(policy.should_attempt(50ULL * kSecond, kCoalesce, kMinInterval));

  policy.mark_target_persisted(50ULL * kSecond, 12U);
  assert(!policy.pending());

  // Sequence comparisons remain valid across uint32 wraparound.
  policy.request(60ULL * kSecond, UINT32_MAX);
  policy.request(61ULL * kSecond, 0U);
  assert(policy.requested_event_seq() == 0U);

  policy.clear(62ULL * kSecond);
  assert(!policy.pending());
  assert(!policy.protects_unpersisted_sequence(0U, UINT32_MAX - 1U));
  return 0;
}
