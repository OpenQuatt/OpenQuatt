#include <assert.h>
#include <stdint.h>

#include <functional>
#include <memory>
#include <new>
#include <string>
#include <vector>
#include <inttypes.h>

namespace {
bool fail_session_allocation = false;
}

void* operator new(size_t size, const std::nothrow_t&) noexcept {
  if (fail_session_allocation) return nullptr;
  try {
    return ::operator new(size);
  } catch (const std::bad_alloc&) {
    return nullptr;
  }
}
void operator delete(void* pointer, const std::nothrow_t&) noexcept { ::operator delete(pointer); }

#define OQ_WEB_OTA_HOST_TEST 1
#define USE_WEBSERVER_OTA 1
#define USE_ESP32 1
#define USE_OTA_STATE_LISTENER 1

struct httpd_req_t {
  void* sess_ctx = nullptr;
  void (*free_ctx)(void*) = nullptr;
};

constexpr int HTTP_POST = 1;
class AsyncWebServerResponse {
 public:
  std::string body;
  void addHeader(const char*, const char*) {}
};
class AsyncWebServerRequest {
 public:
  static constexpr size_t URL_BUF_SIZE = 32;
  httpd_req_t raw;
  std::string response;
  int method() const { return HTTP_POST; }
  std::string url_to(char (&)[URL_BUF_SIZE]) const { return "/update"; }
  size_t contentLength() const { return 100000; }
  operator httpd_req_t*() { return &raw; }
  AsyncWebServerResponse* beginResponse(int, const char*, const char* body) {
    auto* result = new AsyncWebServerResponse;
    result->body = body;
    return result;
  }
  void send(AsyncWebServerResponse* result) {
    response = result->body;
    delete result;
  }
  void close() {
    auto* context = raw.sess_ctx;
    auto cleanup = raw.free_ctx;
    raw.sess_ctx = nullptr;
    raw.free_ctx = nullptr;
    if (context != nullptr && cleanup != nullptr) cleanup(context);
  }
};
class AsyncWebHandler {
 public:
  virtual ~AsyncWebHandler() = default;
  virtual void handleRequest(AsyncWebServerRequest*) {}
  virtual void handleUpload(AsyncWebServerRequest*, const std::string&, size_t, uint8_t*, size_t, bool) {}
  virtual bool canHandle(AsyncWebServerRequest*) const { return false; }
  virtual bool isRequestHandlerTrivial() const { return true; }
};

namespace esphome {
inline uint32_t millis() { return 2000; }
struct Application {
  void safe_reboot() {}
} App;
namespace ota {
enum OTAState { OTA_STARTED, OTA_IN_PROGRESS, OTA_ABORT, OTA_ERROR, OTA_COMPLETED };
enum OTAResponseTypes { OTA_RESPONSE_OK, OTA_RESPONSE_ERROR_UNKNOWN };
struct BackendState {
  int writes = 0, aborts = 0, ends = 0;
};
inline std::vector<std::shared_ptr<BackendState>> backends;
inline bool fail_backend_allocation = false;
inline OTAResponseTypes begin_result = OTA_RESPONSE_OK;
inline OTAResponseTypes write_result = OTA_RESPONSE_OK;
inline OTAResponseTypes end_result = OTA_RESPONSE_OK;
class Backend {
 public:
  std::shared_ptr<BackendState> state = std::make_shared<BackendState>();
  OTAResponseTypes begin(size_t) { return begin_result; }
  OTAResponseTypes write(uint8_t*, size_t) {
    ++state->writes;
    return write_result;
  }
  OTAResponseTypes end() {
    ++state->ends;
    return end_result;
  }
  void abort() { ++state->aborts; }
};
using OTABackendPtr = std::unique_ptr<Backend>;
inline OTABackendPtr make_ota_backend() {
  if (fail_backend_allocation) return nullptr;
  auto backend = std::make_unique<Backend>();
  backends.push_back(backend->state);
  return backend;
}
}  // namespace ota
namespace web_server {
class WebServerOTAComponent {
 public:
  std::vector<ota::OTAState> events;
  int reboots = 0;
  void notify_state_deferred_(ota::OTAState state, float, uint8_t) { events.push_back(state); }
  void set_timeout(int, std::function<void()>) { ++reboots; }
  void mark_failed() {}
  void setup();
  void dump_config();
};
}  // namespace web_server
namespace web_server_base {
class Base {
 public:
  void add_handler(AsyncWebHandler*) {}
};
inline Base* global_web_server_base = nullptr;
}  // namespace web_server_base
}  // namespace esphome

template <typename... Args>
void log_message(const char*, const char*, Args...) {}
#define ESP_LOGI(...) log_message(__VA_ARGS__)
#define ESP_LOGW(...) log_message(__VA_ARGS__)
#define ESP_LOGD(...) log_message(__VA_ARGS__)
#define ESP_LOGE(...) log_message(__VA_ARGS__)
#define ESP_LOGCONFIG(...) log_message(__VA_ARGS__)
#include "../../components/web_server/ota/ota_web_server.cpp"

namespace {
using namespace esphome;
using namespace esphome::web_server;

void reset() {
  ota::backends.clear();
  fail_session_allocation = ota::fail_backend_allocation = false;
  ota::begin_result = ota::write_result = ota::end_result = ota::OTA_RESPONSE_OK;
}
void upload(OTARequestHandler& handler, AsyncWebServerRequest& request, size_t index = 0, bool final = false) {
  uint8_t data[]{0xE9, 1};
  handler.handleUpload(&request, "firmware.bin", index, data, sizeof(data), final);
}
int events(const WebServerOTAComponent& component, ota::OTAState state) {
  int count = 0;
  for (auto event : component.events) count += event == state;
  return count;
}

void test_interrupted_request_aborts_once_and_retry_starts_clean() {
  reset();
  WebServerOTAComponent component;
  OTARequestHandler handler(&component);
  AsyncWebServerRequest first, retry;
  // IDF sends an empty start marker before the first actual bytes.
  handler.handleUpload(&first, "firmware.bin", 0, nullptr, 0, false);
  assert(component.events.empty() && first.raw.sess_ctx == nullptr);
  upload(handler, first);
  assert(events(component, ota::OTA_STARTED) == 1 && ota::backends[0]->writes == 1);
  // EOF, timeout and parser-error all reach the same HTTPD socket cleanup.
  first.close();
  first.close();
  assert(ota::backends[0]->aborts == 1 && ota::backends[0]->ends == 0);
  assert(events(component, ota::OTA_ABORT) == 1 && component.reboots == 0);
  upload(handler, retry, 0, true);
  assert(ota::backends[1]->writes == 1 && ota::backends[1]->ends == 1);
  retry.close();
  assert(events(component, ota::OTA_ABORT) == 1 && events(component, ota::OTA_COMPLETED) == 1);
}

void test_old_close_and_data_cannot_abort_or_write_new_session() {
  reset();
  WebServerOTAComponent component;
  OTARequestHandler handler(&component);
  AsyncWebServerRequest old, current;
  upload(handler, old);
  upload(handler, current);
  assert(ota::backends[0]->aborts == 1 && events(component, ota::OTA_ABORT) == 1);
  upload(handler, old, 2);
  handler.handleRequest(&old);
  assert(ota::backends[1]->writes == 1 && ota::backends[1]->aborts == 0);
  old.close();
  assert(ota::backends[1]->aborts == 0 && events(component, ota::OTA_ABORT) == 1);
  upload(handler, current, 2, true);
  current.close();
  assert(ota::backends[1]->ends == 1 && events(component, ota::OTA_COMPLETED) == 1);
}

void test_completed_upload_has_no_close_abort() {
  reset();
  WebServerOTAComponent component;
  OTARequestHandler handler(&component);
  AsyncWebServerRequest request;
  upload(handler, request, 0, true);
  handler.handleRequest(&request);
  assert(request.response == "Update Successful!" && component.reboots == 1);
  request.close();
  assert(ota::backends[0]->aborts == 0 && events(component, ota::OTA_ABORT) == 0);
}

void test_keep_alive_session_can_own_a_later_upload() {
  reset();
  WebServerOTAComponent component;
  OTARequestHandler handler(&component);
  AsyncWebServerRequest request;
  upload(handler, request, 0, true);
  void* context = request.raw.sess_ctx;
  upload(handler, request);
  assert(request.raw.sess_ctx == context && events(component, ota::OTA_STARTED) == 2);
  request.close();
  assert(ota::backends[0]->aborts == 0 && ota::backends[1]->aborts == 1);
  assert(events(component, ota::OTA_COMPLETED) == 1 && events(component, ota::OTA_ABORT) == 1);
}

void test_complete_body_without_multipart_final_aborts() {
  reset();
  WebServerOTAComponent component;
  OTARequestHandler handler(&component);
  AsyncWebServerRequest request;
  upload(handler, request);
  handler.handleRequest(&request);
  assert(request.response == "Update Failed!" && ota::backends[0]->aborts == 1);
  request.close();
  assert(events(component, ota::OTA_ABORT) == 1 && component.reboots == 0);
}

void test_terminal_errors_do_not_gain_an_extra_abort_on_close() {
  for (int failure = 0; failure < 4; ++failure) {
    reset();
    WebServerOTAComponent component;
    OTARequestHandler handler(&component);
    AsyncWebServerRequest request;
    if (failure == 0) ota::fail_backend_allocation = true;
    if (failure == 1) ota::begin_result = ota::OTA_RESPONSE_ERROR_UNKNOWN;
    if (failure == 2) ota::write_result = ota::OTA_RESPONSE_ERROR_UNKNOWN;
    if (failure == 3) ota::end_result = ota::OTA_RESPONSE_ERROR_UNKNOWN;
    upload(handler, request, 0, true);
    request.close();
    assert(events(component, ota::OTA_STARTED) == 1 && events(component, ota::OTA_ERROR) == 1);
    assert(events(component, ota::OTA_ABORT) == 0 && component.reboots == 0);
    if (failure == 2) assert(ota::backends[0]->aborts == 1);
  }
}

void unrelated_cleanup(void*) {}
void test_unknown_context_and_allocation_failure_refuse_before_start() {
  for (bool allocation_failure : {false, true}) {
    reset();
    WebServerOTAComponent component;
    OTARequestHandler handler(&component);
    AsyncWebServerRequest request;
    int unrelated = 0;
    if (allocation_failure) {
      fail_session_allocation = true;
    } else {
      request.raw.sess_ctx = &unrelated;
      request.raw.free_ctx = unrelated_cleanup;
    }
    upload(handler, request);
    upload(handler, request, 2, true);
    handler.handleRequest(&request);
    assert(component.events.empty() && ota::backends.empty() && component.reboots == 0);
    assert(request.response == "Update Failed!");
    if (!allocation_failure) {
      assert(request.raw.sess_ctx == &unrelated && request.raw.free_ctx == unrelated_cleanup);
    } else {
      assert(request.raw.sess_ctx == nullptr && request.raw.free_ctx == nullptr);
    }
    request.close();
  }
}

void test_unknown_context_does_not_terminate_an_existing_upload() {
  reset();
  WebServerOTAComponent component;
  OTARequestHandler handler(&component);
  AsyncWebServerRequest current, unrelated;
  upload(handler, current);
  int other_context = 0;
  unrelated.raw.sess_ctx = &other_context;
  unrelated.raw.free_ctx = unrelated_cleanup;
  upload(handler, unrelated);
  upload(handler, unrelated, 2, true);
  handler.handleRequest(&unrelated);
  unrelated.close();
  assert(events(component, ota::OTA_ABORT) == 0 && ota::backends[0]->writes == 1);
  upload(handler, current, 2, true);
  current.close();
  assert(events(component, ota::OTA_COMPLETED) == 1 && component.reboots == 1);
}
}  // namespace

int main() {
  test_interrupted_request_aborts_once_and_retry_starts_clean();
  test_old_close_and_data_cannot_abort_or_write_new_session();
  test_completed_upload_has_no_close_abort();
  test_keep_alive_session_can_own_a_later_upload();
  test_complete_body_without_multipart_final_aborts();
  test_terminal_errors_do_not_gain_an_extra_abort_on_close();
  test_unknown_context_and_allocation_failure_refuse_before_start();
  test_unknown_context_does_not_terminate_an_existing_upload();
}
