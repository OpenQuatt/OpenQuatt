#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

struct Mutex {
  bool held_by_http{false};
  bool acquired{false};
  bool http_context{false};
  bool owner_http{false};
  std::function<void()> before_http_take;
};
using SemaphoreHandle_t = Mutex*;
using TickType_t = uint32_t;
constexpr TickType_t portMAX_DELAY = UINT32_MAX;
constexpr int pdTRUE = 1;

int xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t timeout) {
  assert(timeout == (mutex->http_context ? portMAX_DELAY : 0));
  if (mutex->acquired) {
    assert(mutex->owner_http && !mutex->http_context);  // The simulated main loop cannot take HTTP's mutex.
    return 0;
  }
  if (mutex->held_by_http) {
    assert(!mutex->http_context);
    return 0;
  }
  if (mutex->http_context && mutex->before_http_take) {
    std::exchange(mutex->before_http_take, {})();
  }
  mutex->acquired = true;
  mutex->owner_http = mutex->http_context;
  return pdTRUE;
}

void xSemaphoreGive(SemaphoreHandle_t mutex) {
  assert(mutex->acquired && !mutex->held_by_http);
  mutex->acquired = false;
}

using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;

struct esp_partition_t {
  mutable std::vector<uint8_t> bytes;
  const Mutex* mutex;
  std::function<void(const void*)> after_write;
  std::function<void(size_t)> after_erase;
};

esp_err_t esp_partition_write(const esp_partition_t* p, size_t offset, const void* src, size_t length) {
  assert(p->mutex->acquired && offset + length <= p->bytes.size());
  const auto* input = static_cast<const uint8_t*>(src);
  for (size_t i = 0; i < length; ++i) {
    assert((p->bytes[offset + i] & input[i]) == input[i]);  // Flash cannot set an erased bit back to one.
    p->bytes[offset + i] = input[i];
  }
  p->after_write(src);
  return ESP_OK;
}

esp_err_t esp_partition_read(const esp_partition_t* p, size_t offset, void* dst, size_t length) {
  assert(p->mutex->acquired && offset + length <= p->bytes.size());
  std::memcpy(dst, p->bytes.data() + offset, length);
  return ESP_OK;
}

esp_err_t esp_partition_erase_range(const esp_partition_t* p, size_t offset, size_t length) {
  assert(p->mutex->acquired && offset + length <= p->bytes.size());
  std::fill_n(p->bytes.begin() + offset, length, 0xFFU);
  p->after_erase(length);
  return ESP_OK;
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
  static constexpr uint32_t RECORD_MAGIC = 0x3148454F;
  static constexpr uint16_t RECORD_VERSION = 1;
  static constexpr uint16_t FLAG_PARTIAL = 1;
  static constexpr size_t BASE_OFFSET = 0;
  static constexpr size_t HOUR_FLASH_BASE_OFFSET = 256;
  static constexpr size_t FLASH_SECTOR_SIZE = 128;
  static constexpr uint16_t MIN_DATE_YEAR = 2020;
  static constexpr uint16_t MAX_DATE_YEAR = 2099;
  static constexpr size_t DATE_BITMAP_BYTES = ((MAX_DATE_YEAR - MIN_DATE_YEAR + 1) * 12 * 31 + 7) / 8;

  // COMPONENT_RECORDS
  // COMPONENT_DECLARATIONS

  struct State {
    PendingSample pending_samples[PENDING_SAMPLE_CAPACITY]{};
    EnergyHistoryHourRecord hour_records[HOURLY_SLOT_COUNT]{};
    EnergyHistoryValues hour_import_values[24]{};
    uint8_t stored_day_bitmap[DATE_BITMAP_BYTES]{};
    uint8_t stored_hour_day_bitmap[DATE_BITMAP_BYTES]{};
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
  uint32_t archive_clear_generation_{0};
  std::string csrf_token_{"test-secret"};
  uint32_t next_sequence_{0};
  uint32_t record_count_{0};
  uint32_t stored_day_count_{0};
  uint32_t oldest_date_key_{0};
  uint32_t newest_date_key_{0};
  uint32_t last_write_timestamp_s_{0};
  size_t flash_total_bytes_{256};
  size_t flash_slot_count_{4};
  esp_partition_t flash{std::vector<uint8_t>(flash_total_bytes_, 0xFFU), &mutex, {}, {}};
  const esp_partition_t* flash_partition_{&flash};
  std::function<void()> while_import_writes;

  OpenQuattEnergyHistory() {
    flash.after_write = [this](const void* data) {
      EnergyHistoryRecord record{};
      std::memcpy(&record, data, sizeof(record));
      assert(record_crc_(record) == record.crc32);
      day_writes.push_back({record.date_key, record.values.electrical_input_wh, (record.flags & FLAG_PARTIAL) != 0});
      if (mutex.owner_http && while_import_writes) {
        std::exchange(while_import_writes, {})();
      }
    };
    flash.after_erase = [this](size_t length) {
      if (length == flash_total_bytes_) {
        ++clears;
      }
    };
  }

  std::string import_days(const char* records, const std::string& token = "") {
    mutex.http_context = true;
    uint32_t generation = 0;
    const auto effective_token = token.empty() ? csrf_token_ + "." + std::to_string(archive_clear_generation_) : token;
    assert(parse_import_csrf_token(effective_token, &generation));
    const auto result = import_history_records(records, generation);
    mutex.http_context = false;
    return result;
  }

  void run_main(const std::function<void()>& step) {
    assert(mutex.http_context);
    mutex.http_context = false;
    step();
    mutex.http_context = true;
  }

  State* state_() { return &state; }
  bool runtime_storage_ready() const { return true; }
  bool enabled_() const { return history_enabled; }
  bool is_partition_ready_() const { return true; }
  bool read_hour_day_record_(uint32_t, EnergyHistoryHourDayRecord*) const { return false; }
  bool hour_day_record_valid_(const EnergyHistoryHourDayRecord&) const { return false; }
  uint32_t recovered_hour_mask_(const EnergyHistoryHourDayRecord&) const { return 0; }

  bool time_is_valid_() const { return true; }
  uint64_t current_time_ms_() const { return 1000U; }
  bool is_hour_partition_ready_() const { return false; }
  size_t max_hour_flash_total_bytes_() const { return 0; }
  bool write_hour_day_import_record_locked_(uint32_t, uint32_t, const EnergyHistoryValues*, bool) { return false; }

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

  EnergyHistoryRecord flash_day(uint32_t slot_index) const {
    EnergyHistoryRecord record{};
    std::memcpy(&record, flash.bytes.data() + slot_index * FLASH_SLOT_SIZE, sizeof(record));
    assert(record_valid_(record));
    return record;
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

  {
    OpenQuattEnergyHistory history;
    history.capture(14, 23, 0.56f);
    history.while_import_writes = [&] {
      history.run_main([&] { history.capture(15, 0, 0.012f); });
      assert(history.pending_sample_count_ == 1 && history.active_date_key_ == day);
      history.mutex.before_http_take = [&] { history.run_main([&] { history.loop(); }); };
    };
    const auto result = history.import_days("day|20261113|300|550|0\nday|20261114|999|999|0\nday|20261112|200|350|0\n");
    assert(result.find(R"("written":2)") != std::string::npos);
    assert(result.find(R"("duplicates":1)") != std::string::npos);
    assert(history.record_count_ == 3 && history.next_sequence_ == 3);
    assert(history.flash_day(0).sequence == 0 && history.flash_day(0).date_key == day - 1);
    assert(history.flash_day(1).sequence == 1 && history.flash_day(1).date_key == day);
    assert(history.flash_day(1).values.electrical_input_wh == 560);
    assert(history.flash_day(2).sequence == 2 && history.flash_day(2).date_key == day - 2);
    assert(history.hour(day + 1, 0).values.electrical_input_wh == 12);
  }

  for (const char* input : {"day|20261113|300|550|0\n", "day|20261113|300|550|0\nday|20261112|200|350|0\n"}) {
    OpenQuattEnergyHistory history;
    history.while_import_writes = [&] {
      history.run_main([&] { history.clear_history(); });
      assert(history.clear_requested_ && history.clears == 0);
      history.mutex.before_http_take = [&] { history.run_main([&] { history.loop(); }); };
    };
    assert(history.import_days(input) == R"({"ok":false,"error":"archive_cleared"})");
    assert(history.clears == 1 && history.record_count_ == 0 && history.next_sequence_ == 0);
    assert(std::all_of(history.flash.bytes.begin(), history.flash.bytes.end(), [](uint8_t b) { return b == 0xFFU; }));
    assert(history.import_days("day|20261113|300|550|0\n").find(R"("written":1)") != std::string::npos);
    assert(history.flash_day(0).sequence == 0 && history.flash_day(0).date_key == day - 1);
  }

  {
    OpenQuattEnergyHistory history;
    const std::string old_token = "test-secret.0";
    const char* first = "day|20261113|300|550|0\n";
    const char* second = "day|20261112|200|350|0\n";
    uint32_t generation = 0;
    assert(!history.parse_import_csrf_token("wrong-secret.0", &generation));
    assert(!history.parse_import_csrf_token("test-secret", &generation));
    assert(!history.parse_import_csrf_token("test-secret.bad", &generation));
    assert(history.import_days(first, old_token).find(R"("written":1)") != std::string::npos);
    history.clear_history();
    assert(history.import_days(second, old_token) == R"({"ok":false,"error":"archive_cleared"})");
    assert(history.record_count_ == 0 && history.next_sequence_ == 0);
    assert(history.import_days(second, "test-secret.1").find(R"("written":1)") != std::string::npos);
    assert(history.flash_day(0).date_key == day - 2);
  }
}
