#pragma once

#include <stdint.h>

namespace oq_daily_window {

enum class Status : uint8_t { TIME_INVALID = 0, CONFIG_INVALID, WINDOW_DISABLED, OUT_OF_WINDOW, IN_WINDOW };

struct Output {
  bool active = false;
  bool valid = false;
  Status status = Status::TIME_INVALID;
};

inline Output evaluate(bool time_valid, bool start_valid, bool end_valid, int current_minute, int start_minute,
                       int end_minute) {
  if (!time_valid) return {};
  if (!start_valid || !end_valid || current_minute < 0 || current_minute > 1439 || start_minute < 0 ||
      start_minute > 1439 || end_minute < 0 || end_minute > 1439) {
    return {false, false, Status::CONFIG_INVALID};
  }
  if (start_minute == end_minute) return {false, true, Status::WINDOW_DISABLED};
  const bool active = start_minute < end_minute ? current_minute >= start_minute && current_minute < end_minute
                                                : current_minute >= start_minute || current_minute < end_minute;
  return {active, true, active ? Status::IN_WINDOW : Status::OUT_OF_WINDOW};
}

inline const char* status_name(Status status) {
  switch (status) {
    case Status::CONFIG_INVALID:
      return "configuration_invalid";
    case Status::WINDOW_DISABLED:
      return "window_disabled";
    case Status::OUT_OF_WINDOW:
      return "out_of_window";
    case Status::IN_WINDOW:
      return "in_window";
    default:
      return "time_invalid";
  }
}

}  // namespace oq_daily_window
