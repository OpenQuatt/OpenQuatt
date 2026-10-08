#include <atomic>
#include <cassert>
#include <thread>
#include "OpenQuattWebAuth.h"
using namespace esphome;
class Handler : public AsyncWebHandler {
 public:
  int calls{0};
  void handleRequest(AsyncWebServerRequest*) override { ++calls; }
  void handleUpload(AsyncWebServerRequest*, const std::string&, size_t, uint8_t*, size_t, bool) override { ++calls; }
  void handleBody(AsyncWebServerRequest*, uint8_t*, size_t, size_t, size_t) override { ++calls; }
};
int main() {
  web_server_base::WebServerBase base;
  web_server_base::global_web_server_base = &base;
  openquatt_web_auth::OpenQuattWebAuth auth;
  auth.set_bootstrap_username("admin");
  auth.set_bootstrap_password("secret");
  auth.setup();
  Handler normal;
  base.add_handler(&normal);
  base.init();
  auto* middleware = base.get_server()->handlers[1];
  AsyncWebServerRequest request;
  middleware->handleRequest(&request);
  middleware->handleBody(&request, nullptr, 0, 0, 0);
  middleware->handleUpload(&request, "ota", 0, nullptr, 0, true);
  assert(normal.calls == 0 && request.challenged);
  request.username = "admin";
  request.password = "secret";
  std::atomic<bool> done{false};
  std::thread writer([&]() {
    for (int i = 0; i < 2000; ++i) assert(auth.set_runtime_credentials("future", i % 2 ? "one" : "two"));
    done.store(true);
  });
  do {
    assert(auth.request_is_authenticated_admin(&request));
    middleware->handleRequest(&request);
  } while (!done.load());
  writer.join();
  assert(auth.get_active_username() == "admin" && auth.verify_current_password("secret"));
  auth.close_normal_access();
  assert(!openquatt_web_auth::normal_web_access_allowed() && !auth.request_is_authenticated_admin(&request));
}
