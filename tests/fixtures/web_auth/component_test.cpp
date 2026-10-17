#include <atomic>
#include <cassert>
#include <thread>
#include <future>
#include <chrono>

#include "OpenQuattWebAuth.h"

using namespace esphome;

int main() {
  web_server_base::WebServerBase base;
  web_server_base::global_web_server_base = &base;
  openquatt_web_auth::OpenQuattWebAuth auth;
  auth.set_default_auth_enabled(false);
  auth.setup();
  base.init();
  auto* route = base.get_server()->handlers[0];

  AsyncWebServerRequest request;
  request.headers["Host"] = "openquatt.local";
  request.headers["Origin"] = "http://openquatt.local";
  request.verb = HTTP_POST;
  request.url = "/auth/change";
  request.arguments = {{"new_username", "admin"}, {"new_password", "secret"}, {"csrf_token", auth.get_csrf_token()}};
  route->handleRequest(&request);
  assert(request.response_code == 409);  // open LAN is not an administrator or setup window
  assert(!auth.request_is_authenticated_admin(&request));
  auth.begin_recovery_guard("random-ram-secret");
  assert(auth.set_runtime_credentials("admin", "secret"));
  request.username = "admin";
  request.password = "secret";
  assert(!auth.request_is_authenticated_admin(&request));  // stored login does not lift recovery
  auth.end_recovery_guard();
  request.username.clear();
  request.password.clear();
  request.arguments["csrf_token"] = auth.get_csrf_token();
  route->handleRequest(&request);
  assert(request.challenged);
  const auto username_snapshot = auth.get_active_username();
  assert(username_snapshot == "admin");
  assert(auth.is_auth_enabled());
  // Knowing only current_password without Digest authentication is insufficient.
  request.arguments["current_password"] = "secret";
  request.arguments["new_password"] = "replacement";
  route->handleRequest(&request);
  assert(request.challenged);
  assert(auth.verify_current_password("secret"));

  request.username = "admin";
  request.password = "secret";
  request.challenged = false;
  request.arguments["csrf_token"] = auth.get_csrf_token();
  test_save_ok = false;
  route->handleRequest(&request);
  assert(request.response_code == 500);
  assert(auth.verify_current_password("secret"));
  assert(auth.request_is_authenticated_admin(&request));
  test_save_ok = true;
  test_sync_ok = false;
  route->handleRequest(&request);
  assert(request.response_code == 500);
  assert(auth.verify_current_password("secret"));
  test_sync_ok = true;
  route->handleRequest(&request);
  assert(request.response_code == 200);
  assert(!auth.request_is_authenticated_admin(&request));
  assert(auth.verify_current_password("replacement"));

  request.password = "replacement";
  request.url = "/auth/disable";
  request.arguments["current_password"] = "replacement";
  request.headers["Origin"] = "http://foreign.example";
  route->handleRequest(&request);
  assert(request.response_code == 409 && auth.is_auth_enabled());
  request.headers["Origin"] = "http://openquatt.local";
  request.arguments["csrf_token"] = "stale-token";
  route->handleRequest(&request);
  assert(request.response_code == 409 && auth.is_auth_enabled());
  request.arguments["csrf_token"] = auth.get_csrf_token();
  request.arguments["current_password"] = "incorrect";
  route->handleRequest(&request);
  assert(request.response_code == 409 && auth.is_auth_enabled());
  request.arguments["current_password"] = "replacement";
  route->handleRequest(&request);
  assert(request.response_code == 200 && !auth.is_auth_enabled());
  assert(!auth.request_is_authenticated_admin(&request));
  assert(auth.set_runtime_credentials("admin", "replacement"));

  // HTTP status reads race with physical recovery/expiry on the main loop.
  // Each response must observe one coherent component state.
  std::atomic<bool> done{false};
  std::thread writer([&]() {
    for (int i = 0; i < 2000; ++i) {
      auth.begin_recovery_guard("random-ram-secret");
      auth.end_recovery_guard();
      assert(auth.set_runtime_credentials("admin", "replacement"));
    }
    done.store(true);
  });
  AsyncWebServerRequest status;
  status.username = "admin";
  status.password = "replacement";
  do {
    status.response_body.clear();
    route->handleRequest(&status);
    if (!status.response_body.empty()) {
      const bool enabled = status.response_body.find("\"enabled\":true") != std::string::npos;
      const bool empty_username = status.response_body.find("\"username\":\"\"") != std::string::npos;
      assert(enabled != empty_username);
    }
    (void)auth.get_csrf_token();
    (void)auth.get_credential_source();
  } while (!done.load());
  writer.join();
  assert(username_snapshot == "admin");
  assert(auth.set_open_access());
  status.before_send = [&]() {
    auto main_loop_read = std::async(std::launch::async, [&]() { return auth.get_active_username(); });
    assert(main_loop_read.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
  };
  route->handleRequest(&status);  // socket writes cannot retain the component-state lock
  assert(auth.get_active_username().empty());
  assert(username_snapshot == "admin");
}
