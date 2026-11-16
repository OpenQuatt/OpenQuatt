#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace esphome::openquatt_daily_energy {
constexpr size_t CHANNEL_COUNT = 7;
using Totals = std::array<float, CHANNEL_COUNT>;

struct DatedTotals {
  uint32_t date_key{};
  Totals wh{};
};

static_assert(sizeof(DatedTotals) == 32U, "Daily energy preference must match the NVS budget");

class DailyEnergyState {
 public:
  const DatedTotals& totals() const { return totals_; }

  bool prepare(uint32_t date_key, uint32_t now_ms, const DatedTotals& saved = {}, const Totals& checkpoint = {}) {
    if (date_key == 0U || date_key < totals_.date_key) {
      last_update_ms_.fill(now_ms);
      return false;
    }
    if (date_key != totals_.date_key) {
      const bool first_day = totals_.date_key == 0U;
      totals_ = {date_key, {}};
      if (first_day) {
        for (size_t i = 0; i < CHANNEL_COUNT; ++i) {
          if (std::isfinite(checkpoint[i]) && checkpoint[i] >= 0.0f) {
            totals_.wh[i] = checkpoint[i];
          }
          if (saved.date_key == date_key && std::isfinite(saved.wh[i]) && saved.wh[i] > totals_.wh[i]) {
            totals_.wh[i] = saved.wh[i];
          }
        }
      }
      // An interval without a power sample on the new date cannot be assigned
      // to that date. Restart each channel's integration at this observation.
      last_update_ms_.fill(now_ms);
    }
    return true;
  }

  bool add_power(size_t index, float watts, uint32_t now_ms) {
    if (!std::isfinite(watts) || watts < 0.0f) {
      last_update_ms_[index] = now_ms;
      return false;
    }
    const uint32_t elapsed_ms = now_ms - last_update_ms_[index];
    last_update_ms_[index] = now_ms;
    const float energy = totals_.wh[index] + watts * (static_cast<float>(elapsed_ms) / 3600000.0f);
    if (!std::isfinite(energy) || energy == totals_.wh[index]) {
      return false;
    }
    totals_.wh[index] = energy;
    return true;
  }

 private:
  DatedTotals totals_{};
  std::array<uint32_t, CHANNEL_COUNT> last_update_ms_{};
};
}  // namespace esphome::openquatt_daily_energy
