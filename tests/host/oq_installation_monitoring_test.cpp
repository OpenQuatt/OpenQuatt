#ifndef OQ_TOPOLOGY_DUO
#define OQ_TOPOLOGY_DUO 1
#endif

#include "openquatt/includes/diagnostics/oq_installation_monitoring.h"

#include <cassert>
#include <cmath>

using oq_diagnostics::InstallationMonitor;

static void observe_start(InstallationMonitor& monitor, int hp, uint32_t start_ms) {
  assert(!monitor.observe_compressor_frequency(hp, 0.0f, start_ms - 20001U));
  assert(!monitor.observe_compressor_frequency(hp, 0.0f, start_ms - 1U));
  assert(monitor.observe_compressor_frequency(hp, 25.0f, start_ms));
}

static void counts_age_out_by_window() {
  InstallationMonitor::HistoryStorage storage{};
  InstallationMonitor monitor(&storage);
  observe_start(monitor, 1, 22000U);
  observe_start(monitor, 1, 82000U);
#if OQ_TOPOLOGY_DUO
  observe_start(monitor, 2, 95000U);
  assert(monitor.published_start_count(2, 95000U, InstallationMonitor::kTwoHoursMs) == 1.0f);
#else
  assert(monitor.published_start_count(2, 95000U, InstallationMonitor::kTwoHoursMs) == 0.0f);
#endif
  assert(monitor.published_start_count(1, 95000U, InstallationMonitor::kTwoHoursMs) == 2.0f);
  assert(monitor.start_count(1, 95000U, InstallationMonitor::kSixHoursMs) == 2U);
  assert(monitor.start_count(1, 95000U, InstallationMonitor::kDayMs) == 2U);
  assert(monitor.start_count(1, 95000U, 60000U) == 1U);
  assert(monitor.start_count(1, InstallationMonitor::kTwoHoursMs + 2000U, InstallationMonitor::kTwoHoursMs) == 1U);
  assert(monitor.start_count(1, InstallationMonitor::kTwoHoursMs + 62000U, InstallationMonitor::kTwoHoursMs) == 0U);
  assert(monitor.start_count(1, InstallationMonitor::kTwoHoursMs + 62000U, InstallationMonitor::kSeventyTwoHoursMs) ==
         2U);
  assert(monitor.start_count(1, InstallationMonitor::kSeventyTwoHoursMs + 62000U,
                             InstallationMonitor::kSeventyTwoHoursMs) == 0U);
}

static void missing_history_preserves_current_observation() {
  InstallationMonitor monitor(nullptr);
  assert(std::isnan(monitor.published_start_count(1, 2000U, InstallationMonitor::kTwoHoursMs)));
  observe_start(monitor, 1, 22000U);
  assert(monitor.last_start_age_minutes(1, 82000U) == 1.0f);
  assert(monitor.start_count(1, 82000U, 60000U) == 0U);
  assert(!monitor.compressor_cycling_warning_2h(82000U, 0U));
  assert(std::isnan(monitor.published_start_count(1, 82000U, InstallationMonitor::kTwoHoursMs)));

#if OQ_TOPOLOGY_DUO
  observe_start(monitor, 2, 52000U);
  observe_start(monitor, 1, 82000U);
  observe_start(monitor, 2, 112000U);
  assert(monitor.alternating_cycling_warning(112000U, 3U));
#endif
}

int main() {
  counts_age_out_by_window();
  missing_history_preserves_current_observation();
}
