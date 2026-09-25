#pragma once

#include <math.h>
#include <stdint.h>

#ifdef ESP_PLATFORM
#include "esp_timer.h"
#endif

namespace oq_sources {

#ifdef ESP_PLATFORM
inline uint64_t monotonic_ms() { return static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL; }
#endif

// Small volatile receipt for one physical field. observe() always replaces the
// prior value, including invalid samples, so NaN/offline cannot leave old data valid.
struct RawFloatReceipt {
  float value = 0.0f;
  uint64_t received_ms = 0;
  bool received = false;
  bool valid = false;

  void observe(float next_value, uint64_t now_ms, bool next_valid) {
    value = next_value;
    received_ms = now_ms;
    received = true;
    valid = next_valid && isfinite(next_value);
  }

  void revoke(uint64_t now_ms) { observe(0.0f, now_ms, false); }

  void invalidate() { valid = false; }

  bool fresh(uint64_t now_ms, uint64_t max_age_ms) const {
    return received && valid && max_age_ms > 0 && now_ms >= received_ms && (now_ms - received_ms) <= max_age_ms;
  }
};

}  // namespace oq_sources
