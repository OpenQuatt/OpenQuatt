from pathlib import Path
import json
import tempfile
import unittest

from tests.python.test_http_handler_stack_runtime import HTTP_STUBS
from tests.support.cpp import COMPILE_TIMEOUT_SECONDS, RUN_TIMEOUT_SECONDS, compiler_command
from tests.support.process import run_process

ROOT = Path(__file__).resolve().parents[2]
COMPONENT = ROOT / "components/openquatt_service_status"

ADAPTERS = r"""
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
std::atomic<bool> fail_alloc{false}, fail_mutex{false}, fail_take{false};
std::atomic<int> live_buffers{0}, live_mutexes{0};
std::atomic<uint32_t> fake_millis{0};
thread_local bool source_access = true;
thread_local int lock_depth = 0;
std::function<void()> send_hook;
struct TestSemaphore { std::timed_mutex mutex; };
using SemaphoreHandle_t = TestSemaphore*;
constexpr int pdTRUE = 1;
#define pdMS_TO_TICKS(value) (value)
SemaphoreHandle_t xSemaphoreCreateMutex() {
  if (fail_mutex) return nullptr;
  ++live_mutexes;
  return new TestSemaphore;
}
int xSemaphoreTake(SemaphoreHandle_t semaphore, unsigned ticks) {
  assert(semaphore != nullptr && (!source_access || ticks == 0U));
  if (fail_take.exchange(false)) return 0;
  bool locked = ticks ? semaphore->mutex.try_lock_for(std::chrono::milliseconds(ticks)) : semaphore->mutex.try_lock();
  if (locked) ++lock_depth;
  return locked ? pdTRUE : 0;
}
void xSemaphoreGive(SemaphoreHandle_t semaphore) {
  assert(lock_depth == 1);
  --lock_depth;
  semaphore->mutex.unlock();
}
void vSemaphoreDelete(SemaphoreHandle_t semaphore) { delete semaphore; --live_mutexes; }
namespace esphome {
uint32_t millis() { return fake_millis.load(); }
class Component {
 public:
  ~Component() = default;
  virtual void setup() {}
  virtual void loop() {}
  virtual void dump_config() {}
  virtual float get_setup_priority() const { return 0; }
};
namespace setup_priority { constexpr float WIFI = 250; }
namespace globals {
template<typename T> class GlobalsComponent {
 public:
  explicit GlobalsComponent(T value) : value_(std::move(value)) {}
  T& value() { assert(source_access); return value_; }
 private:
  T value_;
};
}
namespace web_server { class WebServer {}; }
namespace web_server_base {
class WebServerBase { public: void add_handler(AsyncWebHandler*) {} };
inline WebServerBase* global_web_server_base = nullptr;
}
namespace openquatt_common {
template<typename T> class PsramBuffer {
 public:
  ~PsramBuffer() { release(); }
  void release() {
    if (data_) {
      assert(data_[0] == 'L' && data_[size_ + 1U] == 'R');
      data_.reset();
      --live_buffers;
    }
    size_ = 0;
  }
  bool allocate_external(size_t count) {
    assert(lock_depth == 0 && count == 32768U);
    release();
    if (fail_alloc) return false;
    data_.reset(new T[count + 2U]);
    data_[0] = 'L'; data_[count + 1U] = 'R'; size_ = count;
    ++live_buffers;
    return true;
  }
  void swap(PsramBuffer& other) { data_.swap(other.data_); std::swap(size_, other.size_); }
  explicit operator bool() const { return bool(data_); }
  T* data() { return data_ ? data_.get() + 1U : nullptr; }
  const T* data() const { return data_ ? data_.get() + 1U : nullptr; }
  size_t size() const { return size_; }
 private:
  std::unique_ptr<T[]> data_;
  size_t size_ = 0;
};
}
}
#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGCONFIG(tag, ...) ((void)(tag))
"""


def runtime_source():
    header = (COMPONENT / "OpenQuattServiceStatus.h").read_text()
    implementation = (COMPONENT / "OpenQuattServiceStatus.cpp").read_text()
    stubs = HTTP_STUBS.replace("int chunks=0;", "bool partial_send=false; int attempts=0, fail_at=0; int chunks=0;")
    stubs = stubs.replace("if(r->fail_send)return -1;",
                          "assert(lock_depth==0); if(send_hook)send_hook();"
                          "if(++r->attempts == r->fail_at || r->fail_send){"
                          "if(r->partial_send && p)r->body.append(p,n/2);return -1;}")
    stubs = stubs.replace("int httpd_resp_send_chunk(",
                          "extern thread_local int lock_depth; extern std::function<void()> send_hook;\nint httpd_resp_send_chunk(")
    stubs = "#include <functional>\n" + stubs
    internal_headers = ''.join((COMPONENT / name).read_text().replace("#pragma once", "")
                               for name in ["warmup_progress_snapshot.h", "snapshot_buffer_writer.h"])
    return (stubs + ADAPTERS + internal_headers + header[header.index("namespace esphome {"):]
            + implementation[implementation.index("namespace esphome {"):])


MAIN = r"""
using namespace esphome::openquatt_service_status;
class TestParent : public OpenQuattServiceStatus {
 public:
  void publish() { assert(source_access); refresh_snapshot_(); }
  void reset_clock(uint32_t now) { last_snapshot_ms_ = now; fake_millis = now; }
};
void render(OpenQuattServiceStatusRequestHandler& handler, AsyncWebServerRequest& request) {
  source_access = false;
  handler.handleRequest(&request);
  source_access = true;
}
void coherent(const std::string& body) {
  const std::string prefix = "\"commissioningStatus\":{\"state\":\"";
  size_t begin = body.find(prefix); assert(begin != std::string::npos); begin += prefix.size();
  const auto end = body.find('"', begin);
  const std::string state = body.substr(begin, end - begin);
  const auto value = body.find("\"value\":\"", end) + 9U;
  assert(body.substr(value, state.size()) == state && body[value + state.size()] == '"');
  assert(!state.empty() && std::all_of(state.begin(), state.end(), [&](char c) { return c == state[0]; }));
}
int main() {
  {
    TestParent parent;
    OpenQuattServiceStatusRequestHandler handler(&parent);
    AsyncWebServerRequest not_ready("/openquatt/service/status");
    render(handler, not_ready); assert(not_ready.raw.status == "503 Service Unavailable");
    parent.setup(); parent.publish(); assert(live_buffers == 2 && live_mutexes == 1);
    AsyncWebServerRequest original("/openquatt/service/status?test=1");
    assert(handler.canHandle(&original)); render(handler, original);
    assert(original.raw.status == "200 OK" && original.raw.ended && !original.raw.closed);
    assert(original.raw.type == "application/json; charset=utf-8");
    assert(original.raw.headers["Cache-Control"] == "no-store");
    assert(static_cast<size_t>(original.raw.chunks) == (original.raw.body.size() + 1023U) / 1024U);
    assert(live_buffers == 2);
    std::cout << original.raw.body << '\n';
    for (const char* path : {"/openquatt/service/status/other", "/other"}) {
      AsyncWebServerRequest request(path); assert(!handler.canHandle(&request));
    }
    AsyncWebServerRequest wrong_method("/openquatt/service/status", HTTP_POST);
    assert(!handler.canHandle(&wrong_method));

    fail_alloc = true;
    AsyncWebServerRequest unavailable("/openquatt/service/status"); render(handler, unavailable);
    assert(unavailable.raw.status == "503 Service Unavailable" && unavailable.raw.chunks == 0);
    assert(unavailable.raw.body == R"({"ok":false,"error":"serialization_unavailable"})");
    fail_alloc = false;
    fail_take = true;
    AsyncWebServerRequest busy("/openquatt/service/status"); render(handler, busy);
    assert(busy.raw.status == "503 Service Unavailable" && live_buffers == 2);
    AsyncWebServerRequest retry("/openquatt/service/status"); render(handler, retry);
    assert(retry.raw.body == original.raw.body && retry.raw.ended && live_buffers == 2);

    for (size_t length : {0U, 1U, 1023U, 1024U, 1025U, 8193U}) {
      httpd_req_t request; const std::string text(length, 'x');
      assert(send_snapshot(&request, text.data(), text.size()));
      assert(request.body == text && static_cast<size_t>(request.chunks) == (length + 1023U) / 1024U);
      assert(request.ended && !request.closed);
    }
    for (int failure : {1, 2, 3}) {
      httpd_req_t request; request.fail_at = failure;
      assert(!send_snapshot(&request, std::string(2048U, 'x').data(), 2048U));
      assert(request.closed && !request.ended && request.attempts == failure);
    }
    {
      httpd_req_t request; request.fail_at = 2; request.partial_send = true;
      assert(!send_snapshot(&request, std::string(4096U, 's').data(), 4096U));
      assert(request.body == std::string(1536U, 's') && request.closed && !request.ended && request.attempts == 2);
    }
    {
      std::array<char, 6> buffer{'L', 0, 0, 0, 0, 'R'};
      SnapshotBufferWriter writer(buffer.data() + 1U, 4U);
      assert(writer.write("1234") && !writer.write("5") && !writer.write("x"));
      assert(buffer[0] == 'L' && buffer[5] == 'R' && writer.size() == 4U);
    }

    std::string escaped = "quote\" slash\\ \b\f\n\r\t "; escaped += '\x01'; escaped += '\x1f'; escaped += " °C";
    OpenQuattServiceStatus::StringGlobal status(escaped);
    OpenQuattServiceStatus::FloatGlobal result(1234.0F), gain(0.01230F);
    parent.set_commissioning_status(&status); parent.set_boiler_result_w(&result); parent.set_flow_kp_suggested(&gain);
    parent.set_warmup_progress(3, 120); parent.publish();
    AsyncWebServerRequest text("/openquatt/service/status"); render(handler, text);
    std::cout << text.raw.body << '\n';
    status.value() = std::string(6000U, 'z'); parent.publish();
    AsyncWebServerRequest long_text("/openquatt/service/status"); render(handler, long_text);
    assert(long_text.raw.ended && !long_text.raw.closed && live_buffers == 2);
    std::cout << long_text.raw.body << '\n';
    for (int failure = 1; failure <= long_text.raw.chunks + 1; ++failure) {
      AsyncWebServerRequest broken("/openquatt/service/status"); broken.raw.fail_at = failure; render(handler, broken);
      assert(broken.raw.closed && !broken.raw.ended && broken.raw.attempts == failure && live_buffers == 2);
    }

    // Publish/reuse both cache buffers repeatedly while the old response is in flight.
    bool changed = false;
    send_hook = [&]() {
      if (changed) return;
      changed = true; assert(lock_depth == 0); source_access = true;
      for (int i = 0; i < 20; ++i) { status.value() = std::string(i % 2 ? 100U : 8000U, 'b'); parent.publish(); }
      source_access = false;
    };
    AsyncWebServerRequest in_flight("/openquatt/service/status"); render(handler, in_flight);
    send_hook = {};
    assert(changed && in_flight.raw.body == long_text.raw.body && live_buffers == 2);
    AsyncWebServerRequest next("/openquatt/service/status"); render(handler, next); coherent(next.raw.body);

    // Cache freshness, skipped publication, expiry, retry and timer wrap.
    parent.reset_clock(100U); parent.publish();
    status.value() = std::string(100U, 'c'); fake_millis = 349U; parent.loop();
    AsyncWebServerRequest cached("/openquatt/service/status"); render(handler, cached); assert(cached.raw.body == next.raw.body);
    fake_millis = 350U; parent.loop();
    AsyncWebServerRequest fresh("/openquatt/service/status"); render(handler, fresh); assert(fresh.raw.body != next.raw.body);
    fail_take = true; status.value() = std::string(100U, 'd'); parent.publish();
    AsyncWebServerRequest skipped("/openquatt/service/status"); render(handler, skipped); assert(skipped.raw.body == fresh.raw.body);
    fake_millis = 1351U;
    AsyncWebServerRequest stale("/openquatt/service/status"); render(handler, stale); assert(stale.raw.status == "503 Service Unavailable");
    parent.publish();
    status.value() = std::string(20000U, 'x'); parent.publish();
    AsyncWebServerRequest overflow("/openquatt/service/status"); render(handler, overflow); assert(overflow.raw.status == "503 Service Unavailable");
    status.value() = std::string(100U, 'e'); parent.publish();
    parent.reset_clock(UINT32_MAX - 10U); parent.publish();
    fake_millis = 238U;
    AsyncWebServerRequest age_wrapped("/openquatt/service/status"); render(handler, age_wrapped);
    assert(age_wrapped.raw.status == "200 OK");
    fake_millis = 239U; parent.loop();
    AsyncWebServerRequest wrapped("/openquatt/service/status"); render(handler, wrapped); assert(wrapped.raw.status == "200 OK");
    parent.reset_clock(UINT32_MAX - 10U); parent.publish(); fake_millis = 989U;
    AsyncWebServerRequest age_limit("/openquatt/service/status"); render(handler, age_limit);
    assert(age_limit.raw.status == "200 OK");
    fake_millis = 990U;
    AsyncWebServerRequest age_expired("/openquatt/service/status"); render(handler, age_expired);
    assert(age_expired.raw.status == "503 Service Unavailable");
    parent.loop();

    // Two real reader threads race with one source owner. Source reads on HTTPD abort.
    std::atomic<bool> go{false}; std::atomic<int> successes{0};
    auto reader = [&]() {
      source_access = false; while (!go.load()) std::this_thread::yield();
      for (int i = 0; i < 150; ++i) {
        AsyncWebServerRequest request("/openquatt/service/status"); handler.handleRequest(&request);
        if (request.raw.status == "200 OK") { coherent(request.raw.body); ++successes; }
        else assert(request.raw.status == "503 Service Unavailable");
        assert(request.raw.ended && !request.raw.closed);
      }
    };
    std::thread first(reader), second(reader); go = true;
    for (int i = 0; i < 500; ++i) { status.value() = std::string(i % 2 ? 50U : 6000U, i % 2 ? 'f' : 'g'); parent.publish(); }
    first.join(); second.join(); assert(successes > 0 && live_buffers == 2);
  }
  assert(live_buffers == 0 && live_mutexes == 0);
  {
    fail_alloc = true; TestParent parent; parent.setup(); fail_alloc = false;
    OpenQuattServiceStatusRequestHandler handler(&parent);
    AsyncWebServerRequest before("/openquatt/service/status"); render(handler, before); assert(before.raw.status == "503 Service Unavailable");
    fake_millis += 1000U; parent.loop();
    AsyncWebServerRequest after("/openquatt/service/status"); render(handler, after); assert(after.raw.status == "200 OK");
    // Losing the second cache allocation must invalidate the first publication.
    fail_alloc = true; parent.publish(); fail_alloc = false;
    AsyncWebServerRequest second_buffer_oom("/openquatt/service/status"); render(handler, second_buffer_oom);
    assert(second_buffer_oom.raw.status == "503 Service Unavailable");
    parent.publish();
    AsyncWebServerRequest recovered("/openquatt/service/status"); render(handler, recovered);
    assert(recovered.raw.status == "200 OK");
  }
  {
    fail_mutex = true; TestParent parent; parent.setup(); fail_mutex = false;
    OpenQuattServiceStatusRequestHandler handler(&parent);
    AsyncWebServerRequest request("/openquatt/service/status"); render(handler, request); assert(request.raw.status == "503 Service Unavailable");
  }
  assert(live_buffers == 0 && live_mutexes == 0);
}
"""

class ServiceStatusRuntimeTest(unittest.TestCase):
    def test_json_compatibility_chunk_bounds_and_failure_recovery(self):
        with tempfile.TemporaryDirectory(prefix="oq-service-status-") as directory:
            binary = Path(directory) / "service-status"
            source = runtime_source() + MAIN
            command = compiler_command("-x", "c++", "-", "-o", str(binary))
            compiled = run_process(command, input=source, capture_output=True, text=True,
                                   timeout=COMPILE_TIMEOUT_SECONDS)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            result = run_process([str(binary)], check=True, capture_output=True, text=True,
                                 timeout=RUN_TIMEOUT_SECONDS)
        original, escaped, long_text = result.stdout.splitlines()
        self.assertEqual(original, (ROOT / "tests/fixtures/service_status/response.json").read_text().strip())
        payload = json.loads(escaped)["entities"]
        self.assertEqual(payload["commissioningStatus"]["value"], 'quote" slash\\ \b\f\n\r\t \x01\x1f °C')
        self.assertEqual(payload["commissioningStatus"]["state"], payload["commissioningStatus"]["value"])
        self.assertEqual(payload["warmupStatus"]["value"], 3)
        self.assertEqual(payload["warmupElapsed"]["value"], 120)
        self.assertEqual(payload["boilerPowerTestResult"], {"state": 1234, "value": 1234, "uom": "W"})
        self.assertEqual(payload["flowKpSuggested"], {"state": 0.0123, "value": 0.0123})
        self.assertEqual(payload["flowKiSuggested"]["value"], "nan")
        self.assertEqual(json.loads(long_text)["entities"]["commissioningStatus"]["value"], 'z' * 6000)


if __name__ == "__main__":
    unittest.main()
