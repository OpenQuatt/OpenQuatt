#include "OpenQuattWatchdogDetails.h"

#include <cinttypes>

#include "esp_attr.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esphome/core/build_info_data.h"
#include "esphome/core/log.h"

namespace {
using esphome::openquatt_crash_telemetry::detail::WatchdogDetails;

static volatile WatchdogDetails s_watchdog_details __attribute__((section(".noinit")));
static uint32_t s_build_time = static_cast<uint32_t>(esphome::ESPHOME_BUILD_TIME);
static uint32_t s_last_loop_us;

struct BootDetails {
  WatchdogDetails record{};
  bool valid;

  BootDetails()
      : valid(esphome::openquatt_crash_telemetry::detail::take_watchdog_details(s_watchdog_details, s_build_time,
                                                                                this->record)) {}
};
static const BootDetails s_boot_details;
}  // namespace

// Read only our own aligned heartbeat and the lock-free SYSTIMER in the ISR.
// ESP-IDF's overdue-task list is no longer locked when this hook runs.
extern "C" void IRAM_ATTR esp_task_wdt_isr_user_handler(void) {
  const uint32_t last_us = __atomic_load_n(&s_last_loop_us, __ATOMIC_RELAXED);
  if (last_us == 0U || s_build_time == 0U) {
    s_watchdog_details.magic = 0U;
    return;
  }
  esphome::openquatt_crash_telemetry::detail::capture_watchdog_age(
      s_watchdog_details, s_build_time, static_cast<uint32_t>(esp_timer_get_time()), last_us);
}

namespace esphome::openquatt_crash_telemetry {
void note_crash_telemetry_loop() {
  __atomic_store_n(&s_last_loop_us, static_cast<uint32_t>(esp_timer_get_time()), __ATOMIC_RELAXED);
}

void log_watchdog_details() {
  if (!s_boot_details.valid || esp_reset_reason() != ESP_RST_TASK_WDT) return;
  ESP_LOGE("esp32.crash", "  Crash telemetry loop heartbeat age: %" PRIu32 " ms",
           s_boot_details.record.loop_age_us / 1000U);
}
}  // namespace esphome::openquatt_crash_telemetry
