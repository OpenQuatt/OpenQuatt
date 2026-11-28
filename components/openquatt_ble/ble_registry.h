#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>

namespace esphome::openquatt_ble {

constexpr size_t CAPACITY = 8;
constexpr uint32_t FRESH_MS = 300000;
constexpr uint32_t SEARCH_MS = 60000;
enum class Protocol : uint8_t { BTHOME = 1, ATC1441 = 2, PVVX = 3 };
struct PairedSensor {
  uint64_t mac{};
  char name[33]{};
  Protocol protocol{};
  bool use_dew_point{};
};

// Names are stored as fixed-size UTF-8 and returned as part of a JSON response.
inline bool valid_sensor_name(std::string_view text) {
  if (text.empty() || text.size() >= sizeof(PairedSensor::name) ||
      text.find_first_not_of(' ') == std::string_view::npos)
    return false;
  for (size_t i = 0; i < text.size();) {
    const auto first = static_cast<uint8_t>(text[i++]);
    if (first < 0x20 || first == 0x7F) return false;
    if (first < 0x80) continue;
    size_t following = 0;
    if (first >= 0xC2 && first <= 0xDF) following = 1;
    if (first >= 0xE0 && first <= 0xEF) following = 2;
    if (first >= 0xF0 && first <= 0xF4) following = 3;
    if (following == 0 || following > text.size() - i) return false;
    const auto second = static_cast<uint8_t>(text[i]);
    if ((first == 0xE0 && second < 0xA0) || (first == 0xED && second >= 0xA0) || (first == 0xF0 && second < 0x90) ||
        (first == 0xF4 && second >= 0x90))
      return false;
    for (size_t j = 0; j < following; ++j)
      if ((static_cast<uint8_t>(text[i++]) & 0xC0) != 0x80) return false;
  }
  return true;
}

struct BleConfig {
  bool enabled{};
  uint32_t revision{};
  uint64_t room_mac{};
  PairedSensor paired[CAPACITY]{};
  uint8_t count{};
};
struct AdvertisedName {
  char text[sizeof(PairedSensor::name)]{};
  bool complete{};
};

struct Reading {
  uint64_t mac{};
  Protocol protocol{};
  float temperature{NAN}, humidity{NAN}, dew_point{NAN};
  uint32_t temperature_age_ms{UINT32_MAX}, pair_age_ms{UINT32_MAX};
  int16_t rssi_dbm{INT16_MIN};
  char advertised_name[sizeof(PairedSensor::name)]{};
};
struct BleSnapshot {
  BleConfig config{};
  Reading paired[CAPACITY]{}, discovered[CAPACITY]{};
  uint8_t discovered_count{};
  bool searching{}, pending{}, ready{}, storage_error{}, dew_required{};
  float dew_point{NAN};
};
struct DewPointInput {
  float value;
  bool required;
};
struct Frame {
  Protocol protocol{};
  float temperature{NAN}, humidity{NAN};
  int counter{-1};
  AdvertisedName advertised_name{};
};

bool decode_advertisement(uint64_t mac, const uint8_t* data, size_t length, Frame& frame);

struct Measurement {
  uint64_t mac{};
  Protocol protocol{};
  float temperature{NAN}, pair_dew_point{NAN}, humidity{NAN};
  uint32_t temperature_ms{}, pair_ms{};
  bool have_temperature{}, have_pair{};
  int counter{-1};
  int16_t rssi_dbm{INT16_MIN};
  AdvertisedName advertised_name{};
  void ingest(const Frame& frame, int rssi, uint32_t now);
  void expire(uint32_t now);
  Reading read(uint32_t now) const;
};

DewPointInput highest_dew_point(const BleConfig& config, const Measurement (&paired)[CAPACITY], uint32_t now);
bool valid_config(const BleConfig& config);
enum class Persist { APPLIED, ROLLED_BACK, UNKNOWN };

// A failed commit or readback can still leave the candidate in NVS. Verify the previous
// record before continuing with it; unknown storage disables all dependent control.
template <typename Save, typename StillAllowed>
Persist persist_config(const BleConfig& current, const BleConfig& candidate, Save save, StillAllowed still_allowed) {
  if (save(candidate) && still_allowed()) return Persist::APPLIED;
  return save(current) ? Persist::ROLLED_BACK : Persist::UNKNOWN;
}
}  // namespace esphome::openquatt_ble
