"""Execute production scheduling/collection methods with peripheral stubs."""

from pathlib import Path
import re
import tempfile
import unittest

from tests.support.cpp import COMPILE_TIMEOUT_SECONDS, RUN_TIMEOUT_SECONDS, compiler_command
from tests.support.process import run_process


ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "components/openquatt_performance_telemetry/OpenQuattPerformanceTelemetry.cpp").read_text()


class PerformanceScheduleRuntimeTest(unittest.TestCase):
    def test_production_loop_and_batch_lifecycle(self) -> None:
        methods = []
        for name in ("loop", "reset_collection_", "process_sample_", "append_record_", "close_window_"):
            match = re.search(r"(?:void|bool) OpenQuattPerformanceTelemetry::" + name + r"\([^\n]*\) \{.*?\n\}", SOURCE, re.S)
            self.assertIsNotNone(match, name)
            methods.append(match.group())
        harness = r'''
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <string>
#include "components/openquatt_performance_telemetry/OpenQuattPerformanceTelemetryPolicy.h"
using namespace esphome::openquatt_performance_telemetry;
#define ESP_LOGW(...) ((void)0)
int64_t mono = 0;
int64_t esp_timer_get_time() { return mono; }
struct Clock {
  struct Value { int64_t timestamp=120030; bool valid=true; bool is_valid() const { return valid; } } value;
  Value now() const { return value; }
};
class OpenQuattPerformanceTelemetry {
 public:
  struct MinuteRecord { uint32_t start_s=0; int generation=1; };
  struct Minute { uint32_t start_s=0; int samples=0; bool invalid=false; } minute_;
  static constexpr uint32_t SAMPLE_SECONDS=10, MINUTE_SECONDS=60;
  static constexpr size_t RECORDS_PER_BATCH=15;
  std::array<MinuteRecord,30> records_{};
  std::array<int,2> hp_runtime_{};
  std::atomic<bool> enabled_{true};
  bool setup_complete=true, pending_publish_allowed_=false;
  int64_t next_publish_us_=PERFORMANCE_PUBLISH_INTERVAL_US;
  uint32_t last_sample_slot_s_=0, active_window_s_=0, pending_window_s_=0, next_retry_ms_=0;
  size_t active_record_count_=0, pending_record_count_=0;
  unsigned consecutive_failures_=0, sent=0, finished=0;
  std::string pending_batch_id_;
  Clock local_clock; Clock* clock_=&local_clock;
  bool setup_complete_() { return setup_complete; }
  void handle_transport_result_() {}
  void clear_pending_() { pending_record_count_=0; pending_publish_allowed_=false; }
  void try_publish_pending_() {
    if (pending_record_count_ && pending_publish_allowed_) { ++sent; clear_pending_(); }
  }
  std::string random_uuid_() { return "batch"; }
  bool sample_minute_(uint32_t) { ++minute_.samples; return true; }
  void finish_minute_() { ++finished; }
  void loop(); void reset_collection_(); void process_sample_(uint32_t timestamp_s);
  bool append_record_(const MinuteRecord& record); void close_window_(bool allow_publish);
};
'''
        harness += "\n".join(methods)
        harness += r'''
int main() {
  OpenQuattPerformanceTelemetry c;
  c.minute_={120000,3,false}; c.last_sample_slot_s_=120030;
  c.append_record_({119940,1});
  mono=899999999; c.loop(); assert(c.sent==0);
  mono=900000000; c.loop();
  assert(c.sent==1 && c.next_publish_us_==1800000000);
  assert(c.minute_.samples==3 && c.minute_.start_s==120000);
  c.local_clock.value.timestamp=120060; c.loop();
  assert(c.finished==1 && c.minute_.samples==1);
  c.loop(); assert(c.finished==1);
  c.enabled_=false; c.reset_collection_(); mono=1200000000; c.loop();
  c.enabled_=true; c.reset_collection_(); c.loop();
  assert(c.next_publish_us_==1800000000);
  c.local_clock.value.timestamp=119000; c.loop();
  assert(c.next_publish_us_==1800000000);
  c.append_record_({119040,1}); c.append_record_({119100,2});
  c.loop(); assert(c.sent==1 && c.pending_record_count_==1);
  mono=1800000000; c.loop(); assert(c.sent==2);
  assert(c.next_publish_us_==2700000000);
  c.enabled_=false; mono=3600000001; c.loop();
  assert(c.next_publish_us_==4500000000);
  c.enabled_=true; c.loop(); assert(c.sent==2);
}
'''
        args = compiler_command("-I", str(ROOT))
        with tempfile.TemporaryDirectory(prefix="openquatt-performance-schedule-") as directory:
            unit = Path(directory) / "test.cpp"
            binary = Path(directory) / "test"
            unit.write_text(harness)
            compiled = run_process(
                args + [str(unit), "-o", str(binary)],
                timeout=COMPILE_TIMEOUT_SECONDS, capture_output=True, text=True,
            )
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            executed = run_process([str(binary)], timeout=RUN_TIMEOUT_SECONDS, capture_output=True, text=True)
            self.assertEqual(executed.returncode, 0, executed.stderr)


if __name__ == "__main__":
    unittest.main()
