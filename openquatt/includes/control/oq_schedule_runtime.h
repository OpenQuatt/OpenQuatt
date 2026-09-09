#pragma once

#include "oq_daily_window_logic.h"

namespace oq_schedule {

inline oq_daily_window::Output cooling_window() {
  const auto now = id(oq_time).now();
  if (!now.is_valid()) return oq_daily_window::evaluate(false, false, false, 0, 0, 0);
  if (!id(oq_cooling_schedule_start_time).has_state() || !id(oq_cooling_schedule_end_time).has_state()) {
    return oq_daily_window::evaluate(true, false, false, now.hour * 60 + now.minute, 0, 0);
  }
  const auto start = id(oq_cooling_schedule_start_time).state_as_esptime();
  const auto end = id(oq_cooling_schedule_end_time).state_as_esptime();
  return oq_daily_window::evaluate(true, true, true, now.hour * 60 + now.minute, start.hour * 60 + start.minute,
                                   end.hour * 60 + end.minute);
}

}  // namespace oq_schedule
