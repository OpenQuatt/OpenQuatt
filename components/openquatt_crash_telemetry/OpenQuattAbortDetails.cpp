#include "OpenQuattAbortDetails.h"

#include "esp_attr.h"
#include "esp_system.h"
#include "esp_memory_utils.h"
#include "esphome/core/build_info_data.h"
#include "esphome/core/log.h"
#include "freertos/FreeRTOS.h"
#include "soc/soc_caps.h"

namespace {
using esphome::openquatt_crash_telemetry::detail::AbortDetails;

static volatile AbortDetails s_abort_details[SOC_CPU_CORES_NUM] __attribute__((section(".noinit")));
// A core writes only its own slot. The latch rejects nested aborts on that
// core without relying on atomics, which can take locks on some ESP32 targets.
static volatile bool s_capture_started[SOC_CPU_CORES_NUM];
static uint32_t s_build_time = static_cast<uint32_t>(esphome::ESPHOME_BUILD_TIME);
static_assert(SOC_CPU_CORES_NUM <= 2, "Abort capture supports one or two cores");
struct BootDetails {
  AbortDetails records[SOC_CPU_CORES_NUM];
  bool valid[SOC_CPU_CORES_NUM];

  BootDetails() {
    // C++ startup runs before normal component/task setup. This snapshot is
    // immutable afterwards: another core can panic during report replay without
    // racing the normal-runtime reader of the previous boot's text.
    for (size_t core = 0; core < SOC_CPU_CORES_NUM; ++core) {
      this->valid[core] = esphome::openquatt_crash_telemetry::detail::snapshot_abort_details(
          s_abort_details[core], s_build_time, this->records[core]);
    }
  }
};
static const BootDetails s_boot_details;
}  // namespace

extern "C" void __attribute__((noreturn)) __real_panic_abort(const char* details);

// Called by ESP-IDF's esp_system_abort, including __assert_func. Keep the
// original panic handler and ESPHome's backtrace capture intact.
extern "C" void IRAM_ATTR __attribute__((noreturn)) __wrap_panic_abort(const char* details) {
  const uint32_t core = static_cast<uint32_t>(xPortGetCoreID());
  if (core < SOC_CPU_CORES_NUM && !s_capture_started[core]) {
    s_capture_started[core] = true;
    esphome::openquatt_crash_telemetry::detail::capture_abort_details(
        s_abort_details[core], details, s_build_time, core, [](uintptr_t address) __attribute__((always_inline)) {
          return esp_ptr_in_dram(reinterpret_cast<const void*>(address));
        });
  }
  __real_panic_abort(details);
}

namespace esphome::openquatt_crash_telemetry {
void log_abort_details(uint8_t crashed_core) {
  // Called only for an ESPHome Abort replay from this build. Core matching
  // prevents attaching the other core's details if two aborts race.
  if (crashed_core >= SOC_CPU_CORES_NUM || !s_boot_details.valid[crashed_core] || esp_reset_reason() != ESP_RST_PANIC ||
      s_boot_details.records[crashed_core].core != crashed_core) {
    return;
  }
  // Read only at normal runtime, after the panic-time writer has rebooted.
  const auto& record = s_boot_details.records[crashed_core];
  ESP_LOGE("esp32.crash", "  Abort details: %s%s", record.text, record.truncated != 0U ? " [truncated]" : "");
}
}  // namespace esphome::openquatt_crash_telemetry
