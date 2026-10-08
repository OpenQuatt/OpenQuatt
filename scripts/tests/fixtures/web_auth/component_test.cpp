#include <cassert>
#include <future>
#include <chrono>
#include "OpenQuattWebAuth.h"
#include "nvs.h"
#include "esphome/core/helpers.h"
using namespace esphome;

int main() {
  web_server_base::WebServerBase base;
  web_server_base::global_web_server_base = &base;
  openquatt_web_auth::OpenQuattWebAuth auth;
  auth.set_bootstrap_username("admin");
  auth.set_bootstrap_password("secret");
  assert(!auth.request_is_authenticated(nullptr));
  auth.setup();
  base.init();
  auto* route = base.get_server()->handlers[0];
  AsyncWebServerRequest request;
  request.username = "admin";
  request.password = "secret";
  request.verb = HTTP_POST;
  request.url = "/auth/change";
  request.headers = {{"Host", "openquatt.local"}, {"Origin", "http://openquatt.local"}};
  request.arguments = {{"current_password", "secret"},
                       {"new_username", "next"},
                       {"new_password", "replacement"},
                       {"csrf_token", auth.get_csrf_token()}};
  const auto original = test_saved;
  request.headers["Origin"] = "http://foreign.example";
  route->handleRequest(&request);
  assert(request.response_code == 409 && test_saved == original);
  request.headers["Origin"] = "http://openquatt.local";
  request.arguments["current_password"] = "incorrect";
  route->handleRequest(&request);
  assert(request.response_code == 409 && test_saved == original);
  request.arguments["current_password"] = "secret";
  test_save_ok = false;
  route->handleRequest(&request);
  assert(request.response_code == 202 && test_saved == original);  // HTTP never writes NVS
  auth.loop();
  assert(!auth.pending_reboot() && auth.verify_current_password("secret"));
  test_save_ok = true;
  test_sync_ok = false;
  route->handleRequest(&request);
  assert(request.response_code == 202);
  auth.loop();
  // Native sync failure may leave the disk changed. The running pair is immutable.
  assert(test_saved != original && !auth.pending_reboot());
  assert(auth.request_is_authenticated_admin(&request));
  test_sync_ok = true;
  test_readback_corrupt = true;
  route->handleRequest(&request);
  auth.loop();
  assert(!auth.pending_reboot());
  test_readback_corrupt = false;
  route->handleRequest(&request);
  assert(request.response_code == 202);
  request.arguments["new_password"] = "must-not-overwrite-queued-job";
  route->handleRequest(&request);
  assert(request.response_code == 500);
  auth.loop();
  assert(auth.pending_reboot() && auth.verify_current_password("secret"));
  AsyncWebServerRequest status;
  status.username = "admin";
  status.password = "secret";
  status.before_send = [&]() {
    auto read = std::async(std::launch::async, [&]() { return auth.get_active_username(); });
    assert(read.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
  };
  route->handleRequest(&status);
  assert(status.response_body.find("\"pending_reboot\":true") != std::string::npos);
  assert(auth.get_active_username() == "admin");

  // CPU reset constructs a new snapshot from durable storage.
  web_server_base::WebServerBase next_base;
  web_server_base::global_web_server_base = &next_base;
  openquatt_web_auth::OpenQuattWebAuth next;
  next.setup();
  request.username = "next";
  request.password = "replacement";
  assert(next.request_is_authenticated_admin(&request));
  assert(next.set_open_access());
  assert(next.request_is_authenticated_admin(&request));  // unchanged until another boot
  openquatt_web_auth::OpenQuattWebAuth open;
  open.setup();
  request.username.clear();
  request.password.clear();
  assert(open.request_is_authenticated(&request));
  assert(!open.request_is_authenticated_admin(&request));

  // Corrupt or unavailable existing storage may never fall back to open policy.
  test_saved[0] ^= 1;
  openquatt_web_auth::OpenQuattWebAuth corrupt;
  corrupt.set_default_auth_enabled(false);
  corrupt.setup();
  assert(!corrupt.ready() && !corrupt.request_is_authenticated(&request));
  test_saved.clear();
  test_probe_ok = false;
  openquatt_web_auth::OpenQuattWebAuth unavailable;
  unavailable.set_default_auth_enabled(false);
  unavailable.setup();
  assert(!unavailable.ready() && !unavailable.request_is_authenticated(&request));
  test_probe_ok = true;
}
