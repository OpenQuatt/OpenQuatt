#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "../odu/oq_odu_frequency_table.h"

// OpenQuatt's conservative heating command envelope, derived from CiC 4.2.0.
// Source ELF SHA256: 3f04ba2bed6c4ec1b4215482f4213e8ea7475c98c305712973b8035838c717b6.
// Mask axes: file 0x1fafe8 (V1), 0x200588 (V2); water caps: code 0x13d798-0x13d8ac.
// Performance interpolation is separate. Temperatures use canonical 0.01 C
// units; this preserves strict equality at 0.20 C re-entry and avoids float drift.
namespace oq_temperature {

inline constexpr int RETURN_OFFSET_CENTI_C = 250;
inline constexpr int REENTRY_CENTI_C = 20;
inline constexpr int INVALID_TEMPERATURE = INT32_MIN;

enum Reason : uint16_t {
  NONE = 0,
  PROFILE = 1,
  MEASUREMENT = 2,
  OUTSIDE_DOMAIN = 4,
  TABLE = 8,
  WATER_CAP = 16,
  HYSTERESIS = 32,
  NORMAL_CAP = 64,
};

inline int centi_c(double value) {
  if (!std::isfinite(value) || value < -100.0 || value > 150.0) return INVALID_TEMPERATURE;
  return static_cast<int>(std::lround(value * 100.0));
}

struct Inputs {
  int ambient_centi_c{INVALID_TEMPERATURE};
  int inlet_centi_c{INVALID_TEMPERATURE};
  bool fresh{false};
  bool low_water_boundary_allowed{false};
};

struct Evaluation {
  uint32_t allowed_levels{0};
  uint16_t reasons{NONE};
  int model_water_centi_c{INVALID_TEMPERATURE};
  int ambient_centi_c{INVALID_TEMPERATURE};
  int water_cap_hz{0};

  bool allows(int physical_level) const {
    return physical_level == 0 ||
           (physical_level > 0 && physical_level <= 20 && (allowed_levels & (1UL << physical_level)) != 0);
  }
};

inline bool v2_profile(oq_odu::Variant variant) {
  return variant == oq_odu::Variant::V2_OLD_MODEL || variant == oq_odu::Variant::V2_NEW_MODEL;
}

inline bool supported_profile(oq_odu::Variant variant) {
  return variant == oq_odu::Variant::V1 || variant == oq_odu::Variant::V1_5 || v2_profile(variant);
}

struct Envelope {
  int ambient_min{-1500};
  int ambient_max{4499};
  int water_max{5499};
  bool ambient_max_exclusive{false};
  bool available{true};
};

// Every frequency between two anchors requires both anchors to be valid.
// The known masks are contiguous in Hz within each ambient zone.
inline Envelope envelope(oq_odu::Variant variant, int hz, int ambient) {
  Envelope e;
  if (hz < 20 || hz > 90) {
    e.available = false;
    return e;
  }
  if (v2_profile(variant)) {
    if (hz < 30) {
      e.ambient_min = 500;
      e.ambient_max = 1400;
      e.ambient_max_exclusive = true;
    } else if (hz > 85) {
      e.ambient_max = -500;
      e.ambient_max_exclusive = true;
    } else if (hz > 80) {
      e.ambient_max = 0;
      e.ambient_max_exclusive = true;
    } else if (hz > 72) {
      e.ambient_max = 500;
      e.ambient_max_exclusive = true;
    } else if (hz > 61) {
      e.ambient_max = 1100;
      e.ambient_max_exclusive = true;
    } else if (hz > 55) {
      e.ambient_max = 1400;
      e.ambient_max_exclusive = true;
    } else if (hz > 48) {
      e.ambient_max = 1900;
      e.ambient_max_exclusive = true;
    } else if (hz > 30) {
      e.ambient_max = 2700;
      e.ambient_max_exclusive = true;
    }
    return e;
  }
  // V1 uses positive-weight corner intersection, not maximum interpolation.
  e.ambient_max = hz > 79 ? 700 : 1200;
  e.water_max = hz > 85 ? 4500 : (hz > 79 && ambient != -1500 ? 4500 : 5500);
  if (hz < 30) e.available = false;
  return e;
}

struct PointState {
  // Boundaries are recorded when a point becomes invalid. They remain active
  // until valid input is strictly >0.20 C on the valid side of each boundary.
  int16_t ambient_low{0};
  int16_t ambient_high{0};
  int16_t water_high{0};
  uint8_t blocked{0};
};

class Policy {
 public:
  void reset() { *this = {}; }

  Evaluation evaluate(const oq_odu::RuntimeFrequencySnapshot& snapshot, const Inputs& in) {
    if (!same_profile_(snapshot)) {
      points_ = {};
      variant_ = snapshot.variant;
      table_ = snapshot.heating;
    }
    Evaluation out;
    out.ambient_centi_c = in.ambient_centi_c;
    if (!supported_profile(snapshot.variant) || !snapshot.heating.valid ||
        !oq_odu::validate_frequency_table(snapshot.heating)) {
      out.reasons = PROFILE;
      return out;
    }
    if (!in.fresh || in.ambient_centi_c == INVALID_TEMPERATURE || in.inlet_centi_c == INVALID_TEMPERATURE) {
      out.reasons = MEASUREMENT;
      return out;
    }
    const bool v2 = v2_profile(snapshot.variant);
    const int water = in.inlet_centi_c + RETURN_OFFSET_CENTI_C;
    out.model_water_centi_c = water;
    const int minimum_water = v2 ? 1800 : 3500;
    // Below the performance axis only the existing startup guards may authorize
    // a boundary estimate. Above it there is no extrapolation or W55/W65 guess.
    if (water < minimum_water && !in.low_water_boundary_allowed) {
      out.reasons = OUTSIDE_DOMAIN;
      return out;
    }
    out.water_cap_hz = v2 ? (water > 5250 ? 55 : (water > 3750 ? 72 : 90)) : (water > 3300 ? 79 : 90);
    for (int level = 1; level < snapshot.heating.level_count; ++level) {
      const int hz = snapshot.heating.hz[level];
      if (hz > 90) {
        out.reasons |= NORMAL_CAP;
        continue;
      }
      const Envelope e = envelope(snapshot.variant, hz, in.ambient_centi_c);
      auto& state = points_[level];
      bool raw_allowed = e.available;
      if (in.ambient_centi_c < e.ambient_min) {
        state.ambient_low = static_cast<int16_t>(e.ambient_min);
        state.blocked |= 1;
        raw_allowed = false;
      }
      if (in.ambient_centi_c > e.ambient_max || (e.ambient_max_exclusive && in.ambient_centi_c == e.ambient_max)) {
        state.ambient_high = static_cast<int16_t>(e.ambient_max);
        state.blocked |= 2;
        raw_allowed = false;
      }
      if (water > e.water_max) {
        state.water_high = static_cast<int16_t>(e.water_max);
        state.blocked |= 4;
        raw_allowed = false;
      }
      if (!raw_allowed) {
        out.reasons |= TABLE;
        if (in.ambient_centi_c < -1500 || in.ambient_centi_c > (v2 ? 4499 : 1200) || water > (v2 ? 5499 : 5500))
          out.reasons |= OUTSIDE_DOMAIN;
        continue;
      }
      if ((state.blocked & 1) && in.ambient_centi_c - state.ambient_low > REENTRY_CENTI_C) state.blocked &= ~1;
      if ((state.blocked & 2) && state.ambient_high - in.ambient_centi_c > REENTRY_CENTI_C) state.blocked &= ~2;
      if ((state.blocked & 4) && state.water_high - water > REENTRY_CENTI_C) state.blocked &= ~4;
      if (state.blocked) {
        out.reasons |= HYSTERESIS;
        continue;
      }
      if (hz > out.water_cap_hz) {
        out.reasons |= WATER_CAP;
        continue;
      }
      out.allowed_levels |= 1UL << level;
    }
    return out;
  }

 private:
  oq_odu::Variant variant_{oq_odu::Variant::UNKNOWN};
  oq_odu::FrequencyTable table_{};
  std::array<PointState, 21> points_{};

  bool same_profile_(const oq_odu::RuntimeFrequencySnapshot& s) const {
    return variant_ == s.variant && table_.valid == s.heating.valid && table_.level_count == s.heating.level_count &&
           table_.hz == s.heating.hz;
  }
};

}  // namespace oq_temperature
