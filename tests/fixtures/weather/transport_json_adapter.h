#pragma once

// Local verification can use the exact ArduinoJson dependency. CI's transport
// lifecycle test stays offline and library-independent; parser validation has
// its own tests and is explicitly not claimed for the fallback adapter.
#ifdef OQ_WEATHER_TEST_REAL_JSON
#include "components/openquatt_weather/OpenQuattWeatherJson.h"
#else
#include "components/openquatt_weather/OpenQuattWeatherLogic.h"
#include "components/openquatt_weather/OpenQuattWeatherLocation.h"

namespace oq_weather {
inline bool parse_json(const char* bytes, size_t size, uint8_t*, size_t, Series& candidate, double latitude,
                       double longitude, uint32_t epoch, uint64_t milliseconds) {
  if (!bytes || !size || !std::strstr(bytes, "\"temperature_2m\"")) return false;
  candidate.fetched_epoch = epoch;
  candidate.fetched_ms = milliseconds;
  candidate.latitude = latitude;
  candidate.longitude = longitude;
  for (size_t i = 0; i < kHours; ++i) {
    candidate.time[i] = epoch - epoch % 3600U + i * 3600U;
    candidate.temperature[i] = 5.5;
  }
  return true;
}
inline const char* parse_geocoding(const char* bytes, size_t size, uint8_t*, size_t, double& latitude,
                                   double& longitude, char* name, size_t capacity) {
  if (!bytes || !size || !std::strstr(bytes, "\"name\":\"Brummen\"") || capacity < 8)
    return "invalid_location_response";
  latitude = 52.09;
  longitude = 6.15;
  std::strcpy(name, "Brummen");
  return nullptr;
}
}  // namespace oq_weather
#endif
