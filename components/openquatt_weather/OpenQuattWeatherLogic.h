#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace oq_weather {

constexpr size_t kHours = 36;
constexpr uint32_t kMaxAgeS = 3U * 3600U;
constexpr uint32_t kMinimumCoverageS = 3U * 3600U;
constexpr uint32_t kCurrentMaxAgeS = 1800;
constexpr uint32_t kCurrentIntervalS = 900;
constexpr const char* kModel = "knmi_harmonie_arome_netherlands";

// Open-Meteo current values are model output for this 15-minute timestamp,
// not station observations or the issuance time of the underlying model run.
struct Current {
  uint32_t time_epoch = 0;
  float temperature_c = NAN, relative_humidity_pct = NAN, dew_point_c = NAN;
  bool valid = false;
};

// Optional research input only. It never participates in the learner,
// temperature forecast or cooling decision, and has its own validity gate.
struct CurrentWind {
  uint32_t time_epoch = 0;
  float speed_mps = NAN, direction_deg = NAN;
  bool valid = false, direction_valid = false;
};

inline bool valid_wind_direction(double degrees) { return std::isfinite(degrees) && degrees >= 0 && degrees <= 360; }
inline bool valid_wind_speed(double speed_mps) { return std::isfinite(speed_mps) && speed_mps >= 0 && speed_mps <= 75; }

inline bool valid_current_wind(const CurrentWind& wind, uint32_t now) {
  return wind.valid && wind.time_epoch != 0 && wind.time_epoch % kCurrentIntervalS == 0 && wind.time_epoch <= now &&
         now - wind.time_epoch <= kCurrentMaxAgeS && valid_wind_speed(wind.speed_mps);
}

inline bool valid_current(const Current& current, uint32_t now) {
  return current.valid && current.time_epoch != 0 && current.time_epoch % kCurrentIntervalS == 0 &&
         current.time_epoch <= now && now - current.time_epoch <= kCurrentMaxAgeS &&
         std::isfinite(current.temperature_c) && current.temperature_c >= -40 && current.temperature_c <= 55 &&
         std::isfinite(current.relative_humidity_pct) && current.relative_humidity_pct >= 0 &&
         current.relative_humidity_pct <= 100 && std::isfinite(current.dew_point_c) && current.dew_point_c >= -40 &&
         current.dew_point_c <= 35 && current.dew_point_c <= current.temperature_c + 0.5f;
}

struct Series {
  uint32_t fetched_epoch = 0;
  uint64_t fetched_ms = 0;
  uint32_t generation = 0;
  float latitude = NAN, longitude = NAN;
  uint32_t time[kHours]{};
  float temperature[kHours]{};
  float wind_mps[kHours]{};
  float wind_direction_deg[kHours]{};
  uint64_t wind_direction_valid_mask = 0;
  Current current;
  CurrentWind current_wind;
};

inline bool valid_series(const Series& series, double latitude, double longitude, uint32_t now) {
  if (!std::isfinite(latitude) || !std::isfinite(longitude) || now == 0 || series.fetched_epoch == 0 ||
      series.fetched_epoch > now || now - series.fetched_epoch > kMaxAgeS || !std::isfinite(series.latitude) ||
      !std::isfinite(series.longitude) || std::fabs(series.latitude - latitude) > 0.1 ||
      std::fabs(series.longitude - longitude) > 0.1 || series.time[0] == 0 || series.time[0] % 3600U != 0)
    return false;
  for (size_t i = 0; i < kHours; ++i) {
    if (!std::isfinite(series.temperature[i]) || series.temperature[i] < -40 || series.temperature[i] > 55 ||
        !std::isfinite(series.wind_mps[i]) || series.wind_mps[i] < 0 || series.wind_mps[i] > 75 ||
        (i > 0 && static_cast<uint64_t>(series.time[i - 1]) + 3600U != series.time[i]))
      return false;
  }
  return series.time[0] <= now && series.time[kHours - 1] >= now && series.time[kHours - 1] - now >= kMinimumCoverageS;
}

inline bool usable(const Series& series, uint32_t now, uint64_t now_ms) {
  if (series.generation == 0 || now_ms < series.fetched_ms ||
      now_ms - series.fetched_ms > static_cast<uint64_t>(kMaxAgeS) * 1000U)
    return false;
  // Reject clock jumps as well as old cached data; a new receipt never refreshes
  // the timestamps of the underlying forecast values.
  const int64_t elapsed_s = static_cast<int64_t>((now_ms - series.fetched_ms) / 1000U);
  const int64_t clock_elapsed = static_cast<int64_t>(now) - series.fetched_epoch;
  return std::abs(clock_elapsed - elapsed_s) <= 30 && valid_series(series, series.latitude, series.longitude, now);
}

inline bool usable_current(const Series& series, uint32_t now, uint64_t now_ms) {
  return usable(series, now, now_ms) && valid_current(series.current, now);
}

inline bool temperature_at(const Series& series, uint32_t epoch, float& value) {
  if (epoch < series.time[0] || epoch > series.time[kHours - 1]) return false;
  const size_t index = (epoch - series.time[0]) / 3600U;
  if (index == kHours - 1) {
    value = series.temperature[index];
  } else {
    const float fraction = (epoch - series.time[index]) / 3600.0f;
    value = series.temperature[index] + fraction * (series.temperature[index + 1] - series.temperature[index]);
  }
  return std::isfinite(value);
}

// Main-loop/worker ownership: neither side touches a mailbox owned by the other.
enum class MailboxPhase : uint8_t { IDLE, REQUESTED, READY };

}  // namespace oq_weather
