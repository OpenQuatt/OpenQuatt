#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <vector>

struct Mutex {
  bool held_by_http{false};
  bool acquired{false};
};
using SemaphoreHandle_t = Mutex*;
using TickType_t = uint32_t;
constexpr TickType_t portMAX_DELAY = UINT32_MAX;
constexpr int pdTRUE = 1;

int xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t timeout) {
  assert(timeout == 0);      // Every tested entry point runs on the main loop.
  assert(!mutex->acquired);  // No recursive takes.
  if (mutex->held_by_http) {
    return 0;
  }
  mutex->acquired = true;
  return pdTRUE;
}

void xSemaphoreGive(SemaphoreHandle_t mutex) {
  assert(mutex->acquired && !mutex->held_by_http);
  mutex->acquired = false;
}

#define ESP_LOGW(...) ((void)0)

// COMPONENT_LOCK

struct Clock {
  struct Time {
    int year{2026};
    int month{11};
    int day_of_month{14};
    int hour{0};
    bool is_valid() const { return true; }
  } sample;
  Time now() const { return sample; }
};

class OpenQuattEnergyHistory {
 public:
  static constexpr uint32_t UNKNOWN_WH = UINT32_MAX;
  static constexpr size_t HOURLY_SLOT_COUNT = 168;
  static constexpr size_t FLASH_SLOT_SIZE = 64;
  static constexpr size_t HOUR_FLASH_SLOT_SIZE = 1024;

  // COMPONENT_RECORDS
  // COMPONENT_DECLARATIONS

  struct State {
    PendingSample pending_samples[PENDING_SAMPLE_CAPACITY]{};
    EnergyHistoryHourRecord hour_records[HOURLY_SLOT_COUNT]{};
  } state;
  struct Archived {
    uint32_t date;
    uint32_t wh;
    bool partial;
  };
  struct HourArchive {
    uint32_t date;
    uint8_t hour;
    uint32_t wh;
    bool partial;
  };
  std::vector<Archived> day_writes;
  std::vector<HourArchive> hour_writes;
  unsigned clears{0};
  unsigned refreshes{0};
  Mutex mutex;
  Clock clock;
  SemaphoreHandle_t hour_state_mutex_{&mutex};
  Clock* clock_{&clock};
  size_t pending_sample_count_{0};
  size_t hour_flash_slot_count_{0};
  bool history_enabled{true};
  bool clear_requested_{false};
  bool retention_refresh_requested_{false};
  bool flush_requested_{false};
  bool has_current_day_{false};
  uint32_t active_date_key_{0};
  EnergyHistoryValues current_values_{};
  bool has_last_hour_sample_{false};
  uint32_t last_hour_sample_date_key_{0};
  EnergyHistoryValues last_hour_sample_values_{};
  EnergyHistoryHourDayRecord hour_flash_record_buffer_{};
  uint32_t next_hour_sequence_{0};

  State* state_() { return &state; }
  bool runtime_storage_ready() const { return true; }
  bool enabled_() const { return history_enabled; }
  bool is_partition_ready_() const { return true; }
  bool read_hour_day_record_(uint32_t, EnergyHistoryHourDayRecord*) const { return false; }
  bool hour_day_record_valid_(const EnergyHistoryHourDayRecord&) const { return false; }
  uint32_t recovered_hour_mask_(const EnergyHistoryHourDayRecord&) const { return 0; }

  bool write_record_(uint32_t date, const EnergyHistoryValues& values, bool partial) {
    assert(mutex.acquired);
    day_writes.push_back({date, values.electrical_input_wh, partial});
    return true;
  }

  bool write_hour_day_record_(uint32_t date, bool partial) {
    assert(mutex.acquired);
    for (const auto& row : state.hour_records) {
      if (row.valid && row.date_key == date) {
        hour_writes.push_back({date, row.hour, row.values.electrical_input_wh, partial});
      }
    }
    return true;
  }

  void configure_hour_flash_window_() {
    assert(mutex.acquired);
    ++refreshes;
  }
  void scan_hour_archive_() { assert(mutex.acquired); }

  void clear_history_locked_() {
    assert(mutex.acquired);
    ++clears;
    has_current_day_ = false;
    clear_hour_records_();
  }

  void capture(int day, int hour, float electricity) {
    clock.sample.day_of_month = day;
    clock.sample.hour = hour;
    capture_day_totals(electricity, electricity, 0, 2 * electricity, 0, 0, 2 * electricity);
  }

  EnergyHistoryHourRecord hour(uint32_t date, uint8_t hour) const {
    for (const auto& row : state.hour_records) {
      if (row.valid && row.date_key == date && row.hour == hour) {
        return row;
      }
    }
    assert(false);
    return {};
  }
};

// COMPONENT_DEFINITIONS

int main() {
  constexpr uint32_t day = 20261114U;
  {
    OpenQuattEnergyHistory history;
    history.capture(14, 17, 0.1f);
    history.mutex.held_by_http = true;
    history.capture(14, 17, 0.13f);
    history.capture(14, 18, 0.15f);
    history.loop();
    assert(history.pending_sample_count_ == 2);
    assert(history.hour(day, 17).values.electrical_input_wh == 100);
    assert(!history.mutex.acquired);
    history.mutex.held_by_http = false;
    history.loop();
    assert(history.pending_sample_count_ == 0);
    assert(history.hour(day, 17).values.electrical_input_wh == 130);
    const auto after_boundary = history.hour(day, 18);
    assert(after_boundary.values.electrical_input_wh == 20 && !after_boundary.includes_recovered_energy);
    history.loop();
    assert(history.hour(day, 18).values.electrical_input_wh == 20);
  }

  {
    OpenQuattEnergyHistory history;
    history.capture(14, 23, 0.5f);
    history.mutex.held_by_http = true;
    history.capture(14, 23, 0.56f);
    history.capture(15, 0, 0.012f);
    history.loop();
    assert(history.day_writes.empty() && history.hour_writes.empty());
    history.mutex.held_by_http = false;
    history.loop();
    assert(history.day_writes.size() == 1 && history.hour_writes.size() == 1);
    assert(history.day_writes[0].date == day && history.day_writes[0].wh == 560 && !history.day_writes[0].partial);
    assert(history.hour_writes[0].date == day && history.hour_writes[0].hour == 23 && history.hour_writes[0].wh == 560);
    const auto midnight = history.hour(day + 1, 0);
    assert(midnight.values.electrical_input_wh == 12 && !midnight.includes_recovered_energy);
    history.capture(15, 0, 0.022f);
    assert(history.hour(day + 1, 0).values.electrical_input_wh == 22);
    assert(history.day_writes.size() == 1);
  }

  {
    OpenQuattEnergyHistory history;
    history.capture(14, 23, 0.5f);
    history.mutex.held_by_http = true;
    history.capture(14, 23, 0.56f);
    history.capture(15, 0, 0.012f);
    history.history_enabled = false;
    history.mutex.held_by_http = false;
    history.loop();
    assert(history.pending_sample_count_ == 2 && history.active_date_key_ == day);
    assert(history.day_writes.empty());
    history.refresh_hourly_retention();
    assert(history.refreshes == 1 && history.pending_sample_count_ == 2);
    history.history_enabled = true;
    history.capture(15, 0, 0.022f);
    assert(history.day_writes.size() == 1 && history.day_writes[0].date == day && history.day_writes[0].wh == 560);
    assert(history.hour(day + 1, 0).values.electrical_input_wh == 22);
  }

  {
    OpenQuattEnergyHistory history;
    history.capture(14, 18, 0.05f);
    history.mutex.held_by_http = true;
    history.capture(14, 18, 0.057f);
    history.refresh_hourly_retention();
    assert(!history.force_flush());
    history.on_shutdown();
    assert(history.refreshes == 0 && history.day_writes.empty());
    history.mutex.held_by_http = false;
    history.loop();
    assert(history.refreshes == 1 && history.day_writes.size() == 1);
    assert(history.day_writes[0].wh == 57 && history.day_writes[0].partial);
    history.loop();
    assert(history.refreshes == 1 && history.day_writes.size() == 1);

    history.mutex.held_by_http = true;
    history.capture(14, 18, 0.065f);
    assert(!history.force_flush());
    history.clear_history();
    assert(history.pending_sample_count_ == 0 && history.clears == 0);
    history.mutex.held_by_http = false;
    history.loop();
    assert(history.clears == 1 && !history.has_current_day_ && history.day_writes.size() == 1);
  }
}
