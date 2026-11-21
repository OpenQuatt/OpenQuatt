#pragma once

#include "MeasurementArchive.h"

namespace oq_measurement_archive {

constexpr uint32_t kDaySeconds = 86400;

// The form supplies a UTC civil date. Bound the conversion to the epoch range
// of the archive and reject invalid calendar dates rather than normalizing them.
inline bool utc_day(const char* date, uint32_t& start) {
  if (!date || std::strlen(date) != 10 || date[4] != '-' || date[7] != '-') return false;
  for (size_t i = 0; i < 10; ++i)
    if (i != 4 && i != 7 && (date[i] < '0' || date[i] > '9')) return false;
  const auto decimal = [](const char* text, size_t length) {
    unsigned value = 0;
    for (size_t i = 0; i < length; ++i) value = value * 10 + (text[i] - '0');
    return value;
  };
  const unsigned year = decimal(date, 4), month = decimal(date + 5, 2), day = decimal(date + 8, 2);
  if (year < 1970 || year > 2106 || month == 0 || month > 12 || day == 0) return false;
  const auto leap = [](unsigned y) { return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0); };
  constexpr unsigned month_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (day > month_days[month - 1] + (month == 2 && leap(year))) return false;
  uint32_t days = day - 1;
  for (unsigned y = 1970; y < year; ++y) days += 365 + leap(y);
  for (unsigned m = 1; m < month; ++m) days += month_days[m - 1] + (m == 2 && leap(year));
  const uint64_t seconds = static_cast<uint64_t>(days) * kDaySeconds;
  if (seconds + kDaySeconds > UINT32_MAX) return false;
  start = static_cast<uint32_t>(seconds);
  return true;
}

// The worker archives the hour containing the receipt and the four following
// knots. Each GHI sample is the mean for the hour ending at that knot.
template <typename Append>
bool export_solar_forecast(const SolarRadiation& row, bool first, Append append) {
  constexpr uint32_t hour_s = 3600;
  if (!append("%s[%u,%u", first ? "" : ",", static_cast<unsigned>(row.epoch),
              static_cast<unsigned>(row.epoch - row.epoch % hour_s)))
    return false;
  for (size_t i = 0; i < 5; ++i) {
    const uint16_t value = row.wm2[i];
    if (!(value == kMissingSolar ? append(",null") : append(",%u", static_cast<unsigned>(value)))) return false;
  }
  return append("]");
}

}  // namespace oq_measurement_archive
