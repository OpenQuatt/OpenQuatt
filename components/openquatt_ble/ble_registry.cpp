#include "ble_registry.h"

namespace esphome::openquatt_ble {
namespace {
uint16_t le16(const uint8_t* p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }

bool decode_bthome(const uint8_t* data, size_t n, Frame& f) {
  if (n == 0 || (data[0] != 0x40 && data[0] != 0x44)) return false;
  f.protocol = Protocol::BTHOME;
  int previous = -1;
  for (size_t i = 1; i < n;) {
    uint8_t id = data[i++];
    if (id <= previous) return false;
    previous = id;
    size_t width;
    switch (id) {
      case 0x00:
      case 0x01:
      case 0x2E:
      case 0x57:
      case 0x58:
        width = 1;
        break;
      case 0x02:
      case 0x03:
      case 0x0C:
      case 0x45:
        width = 2;
        break;
      default:
        return false;  // Unknown widths cannot be skipped safely.
    }
    if (width > n - i) return false;
    switch (id) {
      case 0x00:
        f.counter = data[i];
        break;
      case 0x02:
      case 0x45:
      case 0x57:
      case 0x58:
        if (std::isfinite(f.temperature)) return false;
        f.temperature = width == 1 ? int8_t(data[i]) * (id == 0x58 ? 0.35f : 1.0f)
                                   : int16_t(le16(data + i)) / (id == 0x02 ? 100.0f : 10.0f);
        break;
      case 0x03:
      case 0x2E:
        if (std::isfinite(f.humidity)) return false;
        f.humidity = id == 0x03 ? le16(data + i) / 100.0f : float(data[i]);
        break;
      default:
        break;
    }
    i += width;
  }
  return true;
}

bool decode_atc(uint64_t mac, const uint8_t* p, size_t n, Frame& f) {
  if (n != 13 && n != 15) return false;
  const bool custom = n == 15;
  uint64_t embedded = 0;
  for (size_t i = 0; i < 6; ++i) embedded = (embedded << 8) | p[custom ? 5 - i : i];
  if (embedded != mac) return false;
  f.protocol = custom ? Protocol::PVVX : Protocol::ATC1441;
  f.temperature = custom ? int16_t(le16(p + 6)) / 100.0f : int16_t((uint16_t(p[6]) << 8) | p[7]) / 10.0f;
  f.humidity = custom ? le16(p + 8) / 100.0f : float(p[8]);
  f.counter = p[custom ? 13 : 12];
  return true;
}
}  // namespace

bool decode_advertisement(uint64_t mac, const uint8_t* data, size_t length, Frame& frame) {
  Frame result{};
  bool found = false;
  for (size_t i = 0; i < length;) {
    const size_t size = data[i++];
    if (size == 0) {
      while (i < length)
        if (data[i++] != 0) return false;
      break;
    }
    if (size > length - i) return false;
    if (size >= 3 && data[i] == 0x16) {
      const auto uuid = le16(data + i + 1);
      if (uuid == 0xFCD2 || uuid == 0x181A) {
        if (found) return false;
        found = uuid == 0xFCD2 ? decode_bthome(data + i + 3, size - 3, result)
                               : decode_atc(mac, data + i + 3, size - 3, result);
        if (!found) return false;
      }
    } else if (data[i] == 0x08 || data[i] == 0x09) {
      // A name is optional metadata. A malformed name must not discard a valid measurement.
      const bool complete = data[i] == 0x09;
      auto& name = result.advertised_name;
      const std::string_view text(reinterpret_cast<const char*>(data + i + 1), size - 1);
      if ((complete || !name.complete) && valid_sensor_name(text)) {
        std::memcpy(name.text, text.data(), text.size());
        name.text[text.size()] = '\0';
        name.complete = complete;
      }
    }
    i += size;
  }
  if (!found || !std::isfinite(result.temperature) || result.temperature < -40 || result.temperature > 85 ||
      (std::isfinite(result.humidity) && result.humidity > 100))
    return false;
  frame = result;
  return true;
}

void Measurement::ingest(const Frame& f, int rssi, uint32_t now) {
  rssi_dbm = rssi;
  if (f.advertised_name.text[0] && (f.advertised_name.complete || !advertised_name.complete))
    advertised_name = f.advertised_name;
  if (counter >= 0 && counter == f.counter) return;
  counter = f.counter;
  temperature = f.temperature;
  temperature_ms = now;
  have_temperature = true;
  if (std::isfinite(f.humidity)) {
    humidity = f.humidity;
    pair_dew_point = NAN;
    if (humidity > 0) {
      const float gamma = std::log(humidity / 100.0f) + (17.62f * temperature) / (243.12f + temperature);
      pair_dew_point = 243.12f * gamma / (17.62f - gamma);
    }
    pair_ms = now;
    have_pair = true;
  }
}

void Measurement::expire(uint32_t now) {
  // Latch expiry before the 32-bit millisecond clock rolls over again.
  if (now - temperature_ms > FRESH_MS) temperature = NAN;
  if (now - pair_ms > FRESH_MS) humidity = pair_dew_point = NAN;
  if (now - temperature_ms > INT32_MAX) have_temperature = false;
  if (now - pair_ms > INT32_MAX) have_pair = false;
}

Reading Measurement::read(uint32_t now) const {
  Reading r{};
  r.mac = mac;
  r.protocol = protocol;
  r.rssi_dbm = rssi_dbm;
  std::memcpy(r.advertised_name, advertised_name.text, sizeof(r.advertised_name));
  if (have_temperature) r.temperature_age_ms = now - temperature_ms;
  if (have_pair) r.pair_age_ms = now - pair_ms;
  if (std::isfinite(temperature) && r.temperature_age_ms <= FRESH_MS) {
    r.temperature = temperature;
  }
  if (std::isfinite(humidity) && r.pair_age_ms <= FRESH_MS) {
    r.humidity = humidity;
    r.dew_point = pair_dew_point;
  }
  return r;
}

DewPointInput highest_dew_point(const BleConfig& config, const Measurement (&paired)[CAPACITY], uint32_t now) {
  DewPointInput result{NAN, false};
  for (size_t i = 0; i < config.count; ++i) {
    if (!config.paired[i].use_dew_point) continue;
    result.required = true;
    if (!config.enabled) break;
    const float value = paired[i].read(now).dew_point;
    if (std::isfinite(value)) result.value = std::fmax(result.value, value);
  }
  return result;
}

bool valid_config(const BleConfig& c) {
  if (c.count > CAPACITY) return false;
  bool room_found = c.room_mac == 0;
  for (uint8_t i = 0; i < c.count; ++i) {
    const auto& s = c.paired[i];
    if (!s.mac || s.mac >= (uint64_t{1} << 48) || !std::memchr(s.name, 0, sizeof(s.name)) ||
        !valid_sensor_name(s.name) || s.protocol < Protocol::BTHOME || s.protocol > Protocol::PVVX)
      return false;
    for (uint8_t j = 0; j < i; ++j)
      if (c.paired[j].mac == s.mac) return false;
    room_found |= s.mac == c.room_mac;
  }
  return room_found;
}
}  // namespace esphome::openquatt_ble
