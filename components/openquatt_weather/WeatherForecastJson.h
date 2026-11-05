#pragma once

#include <algorithm>
#include <cstdio>
#include <cstring>
#include "WeatherForecastSnapshot.h"

namespace esphome::openquatt_weather {

// Sink returns false after the first socket error. At most 256 bytes are kept
// on the HTTP stack; neither JSON nor network I/O runs under the cache mutex.
template <typename Sink>
class ForecastJsonWriter {
 public:
  explicit ForecastJsonWriter(Sink sink) : sink_(sink) {}
  bool flush() {
    if (failed_) return false;
    if (used_ && !sink_(bytes_, used_)) {
      failed_ = true;
      return false;
    }
    used_ = 0;
    return true;
  }
  bool literal(const char* value) {
    for (; *value; ++value)
      if (!byte(*value)) return false;
    return true;
  }
  bool byte(char value) {
    if (failed_ || (used_ == sizeof(bytes_) && !flush())) return false;
    bytes_[used_++] = value;
    return true;
  }
  bool string(const char* value) {
    if (!byte('"')) return false;
    for (; *value; ++value) {
      const auto ch = static_cast<unsigned char>(*value);
      if (ch == '"' || ch == '\\') {
        if (!byte('\\') || !byte(*value)) return false;
      } else if (ch < 0x20) {
        char escaped[7];
        std::snprintf(escaped, sizeof(escaped), "\\u%04x", ch);
        if (!literal(escaped)) return false;
      } else if (!byte(*value)) {
        return false;
      }
    }
    return byte('"');
  }
  bool integer(uint32_t value) {
    char number[16];
    std::snprintf(number, sizeof(number), "%u", static_cast<unsigned>(value));
    return literal(number);
  }
  bool integer64(uint64_t value) {
    char number[32];
    std::snprintf(number, sizeof(number), "%llu", static_cast<unsigned long long>(value));
    return literal(number);
  }
  bool number(float value) {
    if (!std::isfinite(value)) return literal("null");
    char number[32];
    std::snprintf(number, sizeof(number), "%.7g", static_cast<double>(value));
    return literal(number);
  }
  bool values(const float* values) {
    if (!byte('[')) return false;
    for (size_t i = 0; i < oq_weather::kHours; ++i)
      if ((i && !byte(',')) || !number(values[i])) return false;
    return byte(']');
  }
  bool directions(const float* values, uint64_t valid_mask) {
    if (!byte('[')) return false;
    for (size_t i = 0; i < oq_weather::kHours; ++i)
      if ((i && !byte(',')) ||
          !number((valid_mask & (uint64_t{1} << i)) && oq_weather::valid_wind_direction(values[i]) ? values[i] : NAN))
        return false;
    return byte(']');
  }

 private:
  Sink sink_;
  char bytes_[256];
  size_t used_ = 0;
  bool failed_ = false;
};

template <typename Sink>
bool write_scenario_json(const ScenarioSnapshot& value, ForecastJsonWriter<Sink>& out) {
  const auto& issue = value.issued;
  if (!out.literal("{\"schema\":1,\"valid\":") || !out.literal(value.valid ? "true" : "false") ||
      !out.literal(",\"reason\":") || !out.string(scenario_reason_name(value.reason)) ||
      !out.literal(",\"issued_epoch\":") || !out.integer(issue.epoch) || !out.literal(",\"configuration\":") ||
      !out.integer(issue.configuration) || !out.literal(",\"weather_generation\":") ||
      !out.integer(issue.weather_generation) || !out.literal(",\"weather_fetched_epoch\":") ||
      !out.integer(issue.weather_fetched_epoch) || !out.literal(",\"weather_start_epoch\":") ||
      !out.integer(issue.weather_start_epoch) || !out.literal(",\"model_context\":") ||
      !out.integer(issue.model_context) || !out.literal(",\"model_samples\":") || !out.integer(issue.model_samples) ||
      !out.literal(",\"room_start_c\":") || !out.number(issue.room_start_c) || !out.literal(",\"held_heat_w\":") ||
      !out.number(issue.held_heat_w) || !out.literal(",\"h_w_per_k\":") || !out.number(issue.h_w_per_k) ||
      !out.literal(",\"c_wh_per_k\":") || !out.number(issue.c_wh_per_k) || !out.literal(",\"horizon_valid\":["))
    return false;
  for (size_t i = 0; i < 3; ++i)
    if ((i && !out.byte(',')) || !out.literal(value.valid && (value.horizon_valid_mask & (1U << i)) ? "true" : "false"))
      return false;
  if (!out.literal("],\"temperature_c\":[")) return false;
  for (size_t i = 0; i < 3; ++i)
    if ((i && !out.byte(',')) ||
        !out.number(value.valid && (value.horizon_valid_mask & (1U << i)) ? issue.temperature_c[i] : NAN))
      return false;
  return out.literal("]}");
}

template <typename Sink>
bool write_forecast_json(const ForecastSnapshot& value, Sink sink) {
  ForecastJsonWriter<Sink> out(sink);
  if (!out.literal("{\"schema\":1,\"model\":") || !out.string(oq_weather::kModel) || !out.literal(",\"location\":") ||
      !out.string(value.location) || !out.literal(",\"configured_location\":") ||
      !out.string(value.configured_location) || !out.literal(",\"enabled\":") ||
      !out.literal(value.enabled ? "true" : "false") || !out.literal(",\"valid\":") ||
      !out.literal(value.valid ? "true" : "false") || !out.literal(",\"now_epoch\":") ||
      !out.integer(value.now_epoch) || !out.literal(",\"fetched_epoch\":") || !out.integer(value.fetched_epoch) ||
      !out.literal(",\"generation\":") || !out.integer(value.generation) || !out.literal(",\"configuration\":") ||
      !out.integer(value.configuration) || !out.literal(",\"start_epoch\":") || !out.integer(value.start_epoch) ||
      !out.literal(",\"step_s\":3600,\"fetched_ms\":") || !out.integer64(value.fetched_ms) ||
      !out.literal(",\"published_ms\":") || !out.integer64(value.published_ms) || !out.literal(",\"latitude\":") ||
      !out.number(value.valid ? value.latitude : NAN) || !out.literal(",\"longitude\":") ||
      !out.number(value.valid ? value.longitude : NAN))
    return false;
  if (value.valid &&
      (!out.literal(",\"temperature_c\":") || !out.values(value.temperature_c) || !out.literal(",\"wind_mps\":") ||
       !out.values(value.wind_mps) || !out.literal(",\"wind_direction_deg\":") ||
       !out.directions(value.wind_direction_deg, value.wind_direction_valid_mask)))
    return false;
  const auto& wind = value.current_wind;
  if (!out.literal(",\"current_wind\":{\"schema\":1,\"source\":\"model\",\"height_m\":10,"
                   "\"direction_convention\":\"from_true_north_clockwise\",\"used_by_model\":false,\"valid\":") ||
      !out.literal(wind.valid ? "true" : "false") || !out.literal(",\"direction_valid\":") ||
      !out.literal(wind.valid && wind.direction_valid ? "true" : "false") || !out.literal(",\"time_epoch\":") ||
      !out.integer(wind.time_epoch) || !out.literal(",\"speed_mps\":") ||
      !out.number(wind.valid ? wind.speed_mps : NAN) || !out.literal(",\"direction_deg\":") ||
      !out.number(wind.valid && wind.direction_valid ? wind.direction_deg : NAN) || !out.byte('}'))
    return false;
  const auto& current = value.current;
  return out.literal(",\"current\":{\"source\":\"model\",\"valid\":") &&
         out.literal(current.valid ? "true" : "false") && out.literal(",\"time_epoch\":") &&
         out.integer(current.time_epoch) && out.literal(",\"temperature_c\":") &&
         out.number(current.valid ? current.temperature_c : NAN) && out.literal(",\"relative_humidity_pct\":") &&
         out.number(current.valid ? current.relative_humidity_pct : NAN) && out.literal(",\"dew_point_c\":") &&
         out.number(current.valid ? current.dew_point_c : NAN) && out.literal("},\"scenario\":") &&
         write_scenario_json(value.scenario, out) && out.literal("}") && out.flush();
}

}  // namespace esphome::openquatt_weather
