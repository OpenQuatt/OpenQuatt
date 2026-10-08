#include <functional>
#include <mutex>
#include <thread>
#define ESP_LOGCONFIG(tag, ...) ((void)tag)
#define USE_ESP32
#define HTTP_GET 1
#define HTTP_POST 2
namespace esphome {
struct httpd_req_t {
  const char* uri;
};
struct AsyncWebServerRequest {
  static constexpr size_t URL_BUF_SIZE = 128;
  std::string raw_uri = "/wifisave?ssid=candidate&psk=secret";
  httpd_req_t native{};
  operator httpd_req_t*() {
    native.uri = raw_uri.c_str();
    return &native;
  }
  std::string url = "/wifisave", ssid = "candidate", password = "secret", body;
  std::map<std::string, std::string> headers;
  int verb = HTTP_GET, status = 0;
  std::function<void()> on_send;
  int method() { return verb; }
  std::string url_to(char*) { return url; }
  std::optional<std::string> get_header(const char* key) {
    auto it = headers.find(key);
    return it == headers.end() ? std::optional<std::string>{} : it->second;
  }
  std::string arg(const char* key) { return strcmp(key, "ssid") == 0 ? ssid : password; }
  void send(int code, const char* = "", const char* text = "") {
    status = code;
    body = text;
    if (on_send) on_send();
  }
};
struct AsyncWebHandler {
  virtual bool canHandle(AsyncWebServerRequest*) const { return false; }
  virtual void handleRequest(AsyncWebServerRequest*) {}
};
namespace setup_priority {
constexpr float WIFI = 250;
}
struct Component {
  bool failed = false;
  virtual void loop() {}
  virtual void setup() {}
  virtual void dump_config() {}
  virtual float get_setup_priority() const { return 0; }
  void mark_failed() { failed = true; }
  bool is_failed() { return failed; }
};
namespace web_server_base {
struct Base {
  int handlers = 0;
  void add_handler_without_auth(AsyncWebHandler*) { ++handlers; }
} base;
auto* global_web_server_base = &base;
}  // namespace web_server_base
namespace wifi {
WiFiComponent* global_wifi_component = nullptr;
}
void captive_portal::Portal::handleRequest(AsyncWebServerRequest* request) { request->send(200); }
}  // namespace esphome
// PRODUCTION_ROUTER
int main() {
  using namespace esphome;
  using namespace esphome::wifi;
  using namespace esphome::openquatt_captive_portal_router;
  WiFiComponent device;
  device.init_preferences_();
  global_wifi_component = &device;
  OpenQuattCaptivePortalRouter router;
  router.setup();
  assert(!router.is_failed() && router.get_setup_priority() > setup_priority::WIFI + 1);
  AsyncWebServerRequest request;
  assert(!router.canHandle(&request));
  // CP.start publishes active policy before a listener can execute requests.
  set_portal_routes_active(true);
  assert(router.canHandle(&request));
  request.on_send = [&router]() { router.job_status_(); };  // no queue mutex held during response
  router.handleRequest(&request);
  assert(request.status == 202 && request.body.find("Request 1 accepted") != std::string::npos);
  assert(device.get_provisioning_status().generation == 0 && disk.empty());
  AsyncWebServerRequest status;
  status.url = "/wifi/provisioning/status";
  router.handleRequest(&status);
  assert(status.body.find("QUEUED") != std::string::npos && status.body.find("false") != std::string::npos);
  AsyncWebServerRequest busy;
  router.handleRequest(&busy);
  assert(busy.status == 409);
  router.loop();
  router.handleRequest(&status);
  assert(status.body.find("CONNECTING") != std::string::npos);
  router.handleRequest(&busy);
  assert(busy.status == 409);
  device.acknowledge_stop();
  commit_ok = false;
  device.connect_candidate();
  router.handleRequest(&status);
  assert(status.body.find("STORAGE_FAILED") != std::string::npos && status.body.find("false") != std::string::npos);
  assert(captive_portal::portal.active && device.ap_enabled);
  commit_ok = true;
  // HTTP threads compete for exactly one fixed bounded queue slot.
  AsyncWebServerRequest first, second;
  std::thread one([&]() { router.handleRequest(&first); });
  std::thread two([&]() { router.handleRequest(&second); });
  one.join();
  two.join();
  assert((first.status == 202 && second.status == 409) || (first.status == 409 && second.status == 202));
  router.loop();
  device.acknowledge_stop();
  device.connect_candidate();
  router.handleRequest(&status);
  assert(status.body.find("SAVED") != std::string::npos && status.body.find("true") != std::string::npos);
  set_portal_routes_active(false);
  assert(!router.canHandle(&status));
  router.handleRequest(&busy);
  assert(busy.status == 503);
  // Repeated portal starts remain serviceable.
  captive_portal::portal.active = true;
  set_portal_routes_active(true);
  AsyncWebServerRequest foreign;
  foreign.headers = {{"Host", "device.local"}, {"Origin", "http://foreign.local"}};
  router.handleRequest(&foreign);
  assert(foreign.status == 403);
  foreign.headers["Origin"] = "http://device.local";
  foreign.headers["Referer"] = "https://foreign.local/";
  router.handleRequest(&foreign);
  assert(foreign.status == 403);
  foreign.headers["Referer"] = "https://device.local/";
  foreign.raw_uri = "/wifisave?ssid=candidate%00different&psk=secret";
  router.handleRequest(&foreign);
  assert(foreign.status == 400);
  foreign.raw_uri = "/wifisave?ssid=candidate&psk=secret";
  foreign.ssid = std::string("bad\0ssid", 8);
  router.handleRequest(&foreign);
  assert(foreign.status == 400);
  foreign.ssid = std::string(33, 's');
  router.handleRequest(&foreign);
  assert(foreign.status == 400);
  foreign.ssid = "wrong";
  foreign.password = std::string(65, 'p');
  router.handleRequest(&foreign);
  assert(foreign.status == 400);
  foreign.password = "wrong";
  router.handleRequest(&foreign);
  assert(foreign.status == 202);
  router.loop();
  device.acknowledge_stop();
  clock_now += 90000;
  router.loop();
  router.handleRequest(&status);
  assert(status.body.find("CANCELLED") != std::string::npos);
  // A lost AP before execution invalidates an accepted job, without a radio/storage call.
  foreign.ssid = "late";
  router.handleRequest(&foreign);
  assert(foreign.status == 202);
  const auto before = device.get_provisioning_status().generation;
  captive_portal::portal.active = false;
  set_portal_routes_active(false);
  router.loop();
  assert(device.get_provisioning_status().generation == before);
  assert(strcmp(router.job_status_().state, "CANCELLED") == 0);
}
