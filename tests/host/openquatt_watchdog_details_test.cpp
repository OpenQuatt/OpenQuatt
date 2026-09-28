#include <cassert>

#include "../../components/openquatt_crash_telemetry/OpenQuattWatchdogDetails.h"

using namespace esphome::openquatt_crash_telemetry::detail;

int main() {
  constexpr uint32_t build = 123456U;
  WatchdogDetails record{};
  WatchdogDetails snapshot{};
  capture_watchdog_age(record, build, 1200U, 700U);
  assert(take_watchdog_details(record, build, snapshot));
  assert(snapshot.loop_age_us == 500U);
  assert(!take_watchdog_details(record, build, snapshot));

  capture_watchdog_age(record, build, 20U, UINT32_MAX - 19U);
  assert(take_watchdog_details(record, build, snapshot));
  assert(snapshot.loop_age_us == 40U);

  capture_watchdog_age(record, build, 1200U, 0U);
  assert(!take_watchdog_details(record, build, snapshot));
  capture_watchdog_age(record, 0U, 1200U, 700U);
  assert(!take_watchdog_details(record, build, snapshot));

  capture_watchdog_age(record, build, 1200U, 700U);
  assert(!take_watchdog_details(record, build + 1U, snapshot));
  capture_watchdog_age(record, build, 1200U, 700U);
  record.loop_age_us++;
  assert(!take_watchdog_details(record, build, snapshot));
  capture_watchdog_age(record, build, 1200U, 700U);
  record.magic = 0U;
  assert(!take_watchdog_details(record, build, snapshot));
}
