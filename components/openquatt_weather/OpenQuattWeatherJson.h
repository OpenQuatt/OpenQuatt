#pragma once

#include <algorithm>
#include <cstring>
#include <ArduinoJson.h>

#include "OpenQuattWeatherLogic.h"
#include "OpenQuattWeatherLocation.h"

namespace oq_weather {

constexpr size_t kResponseBytes = 4096;
constexpr size_t kJsonArenaBytes = 8192;

// Request-local allocator over a reusable strict-PSRAM arena. ArduinoJson may
// shrink/reallocate pools, but can never escape this fixed budget.
class JsonArena : public ArduinoJson::Allocator {
 public:
  JsonArena(uint8_t* bytes, size_t size) : bytes_(bytes), size_(size) {}
  void* allocate(size_t size) override {
    constexpr size_t alignment = alignof(std::max_align_t);
    const size_t header = (sizeof(size_t) + alignment - 1) / alignment * alignment;
    if (size > size_ || header > size_ - size || used_ > size_ - size - header) return nullptr;
    uint8_t* block = bytes_ + used_;
    std::memcpy(block, &size, sizeof(size));
    const size_t needed = header + size;
    if (needed > SIZE_MAX - alignment + 1) return nullptr;
    used_ += (needed + alignment - 1) / alignment * alignment;
    return block + header;
  }
  void deallocate(void*) override {}
  void* reallocate(void* ptr, size_t size) override {
    if (!ptr) return allocate(size);
    constexpr size_t alignment = alignof(std::max_align_t);
    constexpr size_t header = (sizeof(size_t) + alignment - 1) / alignment * alignment;
    size_t old_size = 0;
    std::memcpy(&old_size, static_cast<uint8_t*>(ptr) - header, sizeof(old_size));
    if (size <= old_size) return ptr;
    void* next = allocate(size);
    if (next) std::memcpy(next, ptr, old_size);
    return next;
  }

 private:
  uint8_t* bytes_;
  size_t size_, used_ = 0;
};

struct JsonInput {
  const char* bytes;
  size_t size, position = 0;
  int read() { return position < size ? static_cast<unsigned char>(bytes[position++]) : -1; }
  size_t readBytes(char* target, size_t count) {
    count = std::min(count, size - position);
    std::memcpy(target, bytes + position, count);
    position += count;
    return count;
  }
  bool finished() const {
    for (size_t i = position; i < size; ++i)
      if (bytes[i] != ' ' && bytes[i] != '\n' && bytes[i] != '\r' && bytes[i] != '\t') return false;
    return true;
  }
};

inline bool parse_json(const char* data, size_t size, uint8_t* arena, size_t arena_size, Series& candidate,
                       double latitude, double longitude, uint32_t fetched_epoch, uint64_t fetched_ms) {
  if (!data || !arena || size == 0 || size >= kResponseBytes) return false;
  JsonArena allocator(arena, arena_size);
  ArduinoJson::JsonDocument document(&allocator);
  JsonInput input{data, size};
  const auto error = ArduinoJson::deserializeJson(document, input, ArduinoJson::DeserializationOption::NestingLimit(4));
  if (error || document.overflowed() || !input.finished()) return false;
  const auto root = document.as<ArduinoJson::JsonObjectConst>();
  if (!root["latitude"].is<double>() || !root["longitude"].is<double>() || !root["utc_offset_seconds"].is<int>() ||
      root["utc_offset_seconds"].as<int>() != 0 || std::strcmp(root["hourly_units"]["time"] | "", "unixtime") != 0 ||
      std::strcmp(root["hourly_units"]["temperature_2m"] | "", "°C") != 0 ||
      std::strcmp(root["hourly_units"]["wind_speed_10m"] | "", "m/s") != 0)
    return false;
  const auto times = root["hourly"]["time"].as<ArduinoJson::JsonArrayConst>();
  const auto temperatures = root["hourly"]["temperature_2m"].as<ArduinoJson::JsonArrayConst>();
  const auto wind = root["hourly"]["wind_speed_10m"].as<ArduinoJson::JsonArrayConst>();
  const auto direction = root["hourly"]["wind_direction_10m"].as<ArduinoJson::JsonArrayConst>();
  const bool direction_schema =
      std::strcmp(root["hourly_units"]["wind_direction_10m"] | "", "°") == 0 && direction.size() == kHours;
  if (times.size() != kHours || temperatures.size() != kHours || wind.size() != kHours) return false;
  candidate.fetched_epoch = fetched_epoch;
  candidate.fetched_ms = fetched_ms;
  candidate.latitude = root["latitude"].as<float>();
  candidate.longitude = root["longitude"].as<float>();
  candidate.wind_direction_valid_mask = 0;
  for (size_t i = 0; i < kHours; ++i) {
    if (!times[i].is<uint32_t>() || !temperatures[i].is<double>() || !wind[i].is<double>()) return false;
    candidate.time[i] = times[i].as<uint32_t>();
    candidate.temperature[i] = temperatures[i].as<float>();
    candidate.wind_mps[i] = wind[i].as<float>();
    candidate.wind_direction_deg[i] = NAN;
    if (direction_schema && direction[i].is<double>() && valid_wind_direction(direction[i].as<double>())) {
      candidate.wind_direction_deg[i] = direction[i].as<float>();
      candidate.wind_direction_valid_mask |= uint64_t{1} << i;
    }
  }
  // A malformed or absent current object may not preserve old model values.
  // It does not discard an otherwise valid hourly temperature/wind forecast.
  candidate.current = {};
  const auto current = root["current"].as<ArduinoJson::JsonObjectConst>();
  const auto units = root["current_units"].as<ArduinoJson::JsonObjectConst>();
  candidate.current_wind = {};
  if (std::strcmp(units["time"] | "", "unixtime") == 0 && std::strcmp(units["interval"] | "", "seconds") == 0 &&
      std::strcmp(units["wind_speed_10m"] | "", "m/s") == 0 && current["time"].is<uint32_t>() &&
      current["interval"].is<uint32_t>() && current["interval"].as<uint32_t>() == kCurrentIntervalS &&
      current["wind_speed_10m"].is<double>() && valid_wind_speed(current["wind_speed_10m"].as<double>())) {
    CurrentWind value;
    value.time_epoch = current["time"].as<uint32_t>();
    value.speed_mps = current["wind_speed_10m"].as<float>();
    value.valid = true;
    if (std::strcmp(units["wind_direction_10m"] | "", "°") == 0 && current["wind_direction_10m"].is<double>() &&
        valid_wind_direction(current["wind_direction_10m"].as<double>())) {
      value.direction_deg = current["wind_direction_10m"].as<float>();
      value.direction_valid = true;
    }
    if (valid_current_wind(value, fetched_epoch)) candidate.current_wind = value;
  }
  if (std::strcmp(units["time"] | "", "unixtime") == 0 && std::strcmp(units["interval"] | "", "seconds") == 0 &&
      std::strcmp(units["temperature_2m"] | "", "°C") == 0 &&
      std::strcmp(units["relative_humidity_2m"] | "", "%") == 0 && std::strcmp(units["dew_point_2m"] | "", "°C") == 0 &&
      current["time"].is<uint32_t>() && current["interval"].is<uint32_t>() &&
      current["interval"].as<uint32_t>() == kCurrentIntervalS && current["temperature_2m"].is<double>() &&
      current["relative_humidity_2m"].is<double>() && current["dew_point_2m"].is<double>()) {
    Current value;
    value.time_epoch = current["time"].as<uint32_t>();
    value.temperature_c = current["temperature_2m"].as<float>();
    value.relative_humidity_pct = current["relative_humidity_2m"].as<float>();
    value.dew_point_c = current["dew_point_2m"].as<float>();
    value.valid = true;
    if (valid_current(value, fetched_epoch)) candidate.current = value;
  }
  return valid_series(candidate, latitude, longitude, fetched_epoch);
}

inline const char* parse_geocoding(const char* data, size_t size, uint8_t* arena, size_t arena_size, double& latitude,
                                   double& longitude, char* name, size_t name_size) {
  if (!data || !arena || !name || size == 0 || size >= kResponseBytes) return "invalid_location_response";
  JsonArena allocator(arena, arena_size);
  ArduinoJson::JsonDocument document(&allocator);
  JsonInput input{data, size};
  const auto error = ArduinoJson::deserializeJson(document, input, ArduinoJson::DeserializationOption::NestingLimit(4));
  if (error || document.overflowed() || !input.finished()) return "invalid_location_response";
  if (!document.is<ArduinoJson::JsonObject>()) return "invalid_location_response";
  const auto root = document.as<ArduinoJson::JsonObjectConst>();
  if (!root["error"].isUnbound()) return "invalid_location_response";
  if (!root["results"].is<ArduinoJson::JsonArrayConst>()) {
    if (root["results"].isUnbound() && root["generationtime_ms"].is<double>() &&
        std::isfinite(root["generationtime_ms"].as<double>()) && root["generationtime_ms"].as<double>() >= 0)
      return "location_not_found";
    return "invalid_location_response";
  }
  const auto results = root["results"].as<ArduinoJson::JsonArrayConst>();
  if (results.size() == 0) return "location_not_found";
  if (results.size() > 2) return "invalid_location_response";
  for (const auto item : results) {
    if (!item.is<ArduinoJson::JsonObjectConst>() || !item["latitude"].is<double>() || !item["longitude"].is<double>() ||
        !oq_weather::valid_location_coordinates(item["latitude"].as<double>(), item["longitude"].as<double>()) ||
        std::strcmp(item["country_code"] | "", "NL") != 0 || std::strncmp(item["feature_code"] | "", "PPL", 3) != 0 ||
        !item["name"].is<const char*>())
      return "invalid_location_response";
  }
  if (results.size() == 2) return "location_ambiguous";
  const auto result = results[0];
  const char* resolved_name = result["name"] | "";
  const char* feature = result["feature_code"] | "";
  if (!result["latitude"].is<double>() || !result["longitude"].is<double>() ||
      std::strcmp(result["country_code"] | "", "NL") != 0 || std::strncmp(feature, "PPL", 3) != 0 ||
      !resolved_name[0] || std::strlen(resolved_name) >= name_size)
    return "invalid_location_response";
  const double lat = result["latitude"].as<double>(), lon = result["longitude"].as<double>();
  if (!valid_location_coordinates(lat, lon)) return "invalid_location_response";
  for (const char* ch = resolved_name; *ch; ++ch)
    if (static_cast<unsigned char>(*ch) < 0x20 || *ch == 0x7f) return "invalid_location_response";
  latitude = lat;
  longitude = lon;
  std::strcpy(name, resolved_name);
  return nullptr;
}

}  // namespace oq_weather
