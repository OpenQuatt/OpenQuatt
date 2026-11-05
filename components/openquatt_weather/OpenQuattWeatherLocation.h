#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace oq_weather {

constexpr size_t kLocationBytes = 65;
enum class LocationKind : uint8_t { EMPTY, NAME, COORDINATES, INVALID };

struct Location {
  char query[kLocationBytes]{};
  double latitude = NAN, longitude = NAN;
  LocationKind kind = LocationKind::EMPTY;
};

inline bool valid_location_coordinates(double latitude, double longitude) {
  return std::isfinite(latitude) && std::isfinite(longitude) && latitude >= 50.5 && latitude <= 53.8 &&
         longitude >= 3.0 && longitude <= 7.5;
}

inline LocationKind parse_location(const char* value, size_t length, Location& location) {
  location = {};
  location.kind = LocationKind::INVALID;
  if (!value || length >= kLocationBytes) return location.kind;
  size_t begin = 0, end = length;
  while (begin < end && value[begin] == ' ') ++begin;
  while (end > begin && value[end - 1] == ' ') --end;
  const size_t size = end - begin;
  if (size >= kLocationBytes) return location.kind;
  for (size_t i = begin; i < end; ++i)
    if (static_cast<unsigned char>(value[i]) < 0x20 || value[i] == 0x7f) return location.kind;
  std::memcpy(location.query, value + begin, size);
  if (size == 0) return location.kind = LocationKind::EMPTY;
  char* first_end = nullptr;
  const double latitude = std::strtod(location.query, &first_end);
  const bool numeric = first_end != location.query;
  while (*first_end == ' ') ++first_end;
  if (numeric && *first_end == ',') {
    char* last = nullptr;
    const char* second = first_end + 1;
    const double longitude = std::strtod(second, &last);
    while (*last == ' ') ++last;
    if (last == second || *last != '\0' || !valid_location_coordinates(latitude, longitude)) return location.kind;
    location.latitude = latitude;
    location.longitude = longitude;
    return location.kind = LocationKind::COORDINATES;
  }
  if (size < 2 || std::strstr(location.query, "://")) return location.kind;
  return location.kind = LocationKind::NAME;
}

inline bool encode_query(const char* query, char* output, size_t capacity) {
  constexpr char hex[] = "0123456789ABCDEF";
  size_t used = 0;
  for (const auto* value = reinterpret_cast<const unsigned char*>(query); *value; ++value) {
    const bool safe = (*value >= 'a' && *value <= 'z') || (*value >= 'A' && *value <= 'Z') ||
                      (*value >= '0' && *value <= '9') || *value == '-' || *value == '_' || *value == '.' ||
                      *value == '~';
    const size_t needed = safe ? 1 : 3;
    if (used + needed >= capacity) return false;
    if (safe)
      output[used++] = *value;
    else {
      output[used++] = '%';
      output[used++] = hex[*value >> 4];
      output[used++] = hex[*value & 15];
    }
  }
  if (used >= capacity) return false;
  output[used] = '\0';
  return true;
}

inline bool encode_geocoding_query(const char* query, char* output, size_t capacity) {
  const size_t size = std::strlen(query);
  // GeoNames indexes Dutch four-digit postcodes. Keep the saved user input
  // intact and reduce only an exact full-postcode match in the lookup request.
  if (size == 6 || (size == 7 && query[4] == ' ')) {
    bool postcode = query[0] >= '1' && query[0] <= '9';
    for (size_t i = 1; i < 4; ++i) postcode = postcode && query[i] >= '0' && query[i] <= '9';
    for (size_t i = size - 2; i < size; ++i)
      postcode = postcode && ((query[i] >= 'A' && query[i] <= 'Z') || (query[i] >= 'a' && query[i] <= 'z'));
    if (postcode) {
      const char prefix[]{query[0], query[1], query[2], query[3], '\0'};
      return encode_query(prefix, output, capacity);
    }
  }
  return encode_query(query, output, capacity);
}

struct LocationRecord {
  uint32_t version = 1;
  uint32_t checksum = 0;
  char query[kLocationBytes]{};
  uint8_t reserved[3]{};
};

inline uint32_t location_checksum(const LocationRecord& record) {
  uint32_t hash = 2166136261U ^ record.version;
  for (const unsigned char value : record.query) hash = (hash ^ value) * 16777619U;
  return hash;
}

inline LocationRecord location_record(const Location& location) {
  LocationRecord record;
  std::memcpy(record.query, location.query, sizeof(record.query));
  record.checksum = location_checksum(record);
  return record;
}

inline bool valid_location_record(const LocationRecord& record) {
  if (record.version != 1 || record.reserved[0] || record.reserved[1] || record.reserved[2] ||
      record.query[kLocationBytes - 1] != '\0' || record.checksum != location_checksum(record))
    return false;
  Location location;
  return parse_location(record.query, std::strlen(record.query), location) != LocationKind::INVALID;
}

template <typename Write, typename Commit, typename Read>
bool persist_location_verified(const LocationRecord& record, Write write, Commit commit, Read read) {
  if (!valid_location_record(record) || !write(record) || !commit()) return false;
  LocationRecord verify;
  return read(verify) && valid_location_record(verify) && verify.version == record.version &&
         std::memcmp(verify.query, record.query, sizeof(record.query)) == 0 && verify.checksum == record.checksum;
}

}  // namespace oq_weather
