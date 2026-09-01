#pragma once

#include <math.h>
#include <stdint.h>

#include "oq_supply_calibration_logic.h"

namespace oq_supply_hold {

inline bool source_matches(int32_t source_code, uint32_t source_fingerprint,
                           const oq_supply_calibration::SourceIdentity& current) {
  return current.ready && source_code == static_cast<int32_t>(current.code) &&
         source_fingerprint == current.fingerprint;
}

inline uint32_t timeout_ms(const oq_supply_calibration::SourceIdentity& current, uint32_t local_cic_timeout_ms,
                           uint32_t ha_timeout_ms) {
  return current.ready && current.code == oq_supply_calibration::SOURCE_HA_INPUT ? ha_timeout_ms : local_cic_timeout_ms;
}

inline bool can_hold(float last_valid_c, uint32_t last_valid_ms, int32_t source_code, uint32_t source_fingerprint,
                     const oq_supply_calibration::SourceIdentity& current, uint32_t now_ms, uint32_t hold_ms) {
  return isfinite(last_valid_c) && last_valid_ms > 0 && hold_ms > 0 &&
         source_matches(source_code, source_fingerprint, current) &&
         static_cast<uint32_t>(now_ms - last_valid_ms) < hold_ms;
}

struct State {
  bool remembered = false;
  float last_valid_c = NAN;
  uint32_t last_valid_ms = 0;
  int32_t source_code = 0;
  uint32_t source_fingerprint = 0;

  bool has_value() const { return remembered && isfinite(last_valid_c) && source_code != 0; }

  bool matches_source(const oq_supply_calibration::SourceIdentity& current) const {
    return source_matches(source_code, source_fingerprint, current);
  }

  void reset() {
    remembered = false;
    last_valid_c = NAN;
    last_valid_ms = 0;
    source_code = 0;
    source_fingerprint = 0;
  }

  void remember(float value_c, uint32_t now_ms, const oq_supply_calibration::SourceIdentity& current) {
    if (!isfinite(value_c) || !current.ready) {
      reset();
      return;
    }
    remembered = true;
    last_valid_c = value_c;
    last_valid_ms = now_ms;
    source_code = static_cast<int32_t>(current.code);
    source_fingerprint = current.fingerprint;
  }

  bool available(const oq_supply_calibration::SourceIdentity& current, uint32_t now_ms, uint32_t hold_ms) const {
    return has_value() && hold_ms > 0 && matches_source(current) &&
           static_cast<uint32_t>(now_ms - last_valid_ms) < hold_ms;
  }
};

}  // namespace oq_supply_hold
