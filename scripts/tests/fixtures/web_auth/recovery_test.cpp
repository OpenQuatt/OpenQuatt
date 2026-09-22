#include <cassert>
#include <future>
#include <chrono>

#include "OpenQuattRecovery.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "lwip/sockets.h"
#include "esphome/core/application.h"

using namespace esphome;

class Recovery : public openquatt_recovery::OpenQuattRecovery {
 public:
  std::string token() const { return csrf_token_; }
  uint32_t generation() const { return state_.generation(); }
  void handoff() { prepare_reboot_handoff_(); }
};

int main() {
  api::APIServer api;
  api::global_api_server = &api;
  web_server_base::WebServerBase base;
  web_server_base::global_web_server_base = &base;
  openquatt_web_auth::OpenQuattWebAuth auth;
  auth.set_default_auth_enabled(false);
  auth.setup();
  binary_sensor::BinarySensor button;
  Recovery recovery;
  recovery.set_web_auth(&auth);
  recovery.set_button(&button);
  recovery.setup();
  base.init();
  AsyncWebServerRequest request;
  request.headers["Host"] = "openquatt.local";
  request.headers["Origin"] = "http://openquatt.local";
  request.url = "/recovery/web-auth";
  request.verb = HTTP_POST;
  request.arguments = {{"new_username", "admin"}, {"new_password", "new-secret"}};
  recovery.handleRequest(&request);
  assert(request.response_code == 403);
  request.url = "/api-security/reset";
  request.arguments["csrf_token"] = auth.get_csrf_token();
  request.arguments["confirm"] = "RESET_API_SECURITY";
  recovery.handleRequest(&request);
  assert(request.response_code == 403);  // open mode is not admin
  request.url = "/recovery/web-auth";
  recovery.loop();  // released first
  button.state = true;
  recovery.loop();
  test_millis.fetch_add(5000);
  recovery.loop();
  recovery.handleRequest(&request);
  assert(request.response_code == 403);  // not authorized before HTTPD barrier
  test_httpd_work();
  assert(test_closed_sockets == 1);
  assert(base.is_recovery_active());
  request.arguments["csrf_token"] = recovery.token();
  request.arguments["generation"] = std::to_string(recovery.generation());
  recovery.handleRequest(&request);
  assert(request.response_code == 403);  // main-loop barrier still pending
  recovery.loop();
  AsyncWebServerRequest status;
  status.url = "/recovery/status";
  status.headers["Origin"] = "http://foreign.example";
  recovery.handleRequest(&status);
  assert(status.response_headers.at("Access-Control-Allow-Origin").empty());
  assert(status.response_headers.at("Cache-Control") == "no-store");
  request.headers["Origin"] = "http://foreign.example";
  recovery.handleRequest(&request);
  assert(request.response_code == 403);
  request.headers.erase("Origin");
  recovery.handleRequest(&request);
  assert(request.response_code == 403);
  request.headers["Origin"] = "http://openquatt.local";
  request.arguments["generation"] = "0";
  recovery.handleRequest(&request);
  assert(request.response_code == 403);
  request.arguments["generation"] = std::to_string(recovery.generation());
  test_sync_ok = false;
  recovery.handleRequest(&request);
  assert(request.response_code == 202);
  recovery.handleRequest(&request);
  assert(request.response_code == 403);  // duplicate job cannot be queued
  recovery.loop();
  assert(!auth.is_auth_enabled());
  assert(base.is_recovery_active());
  test_sync_ok = true;
  recovery.handleRequest(&request);
  assert(request.response_code == 202);
  recovery.loop();
  assert(auth.is_auth_enabled());
  request.username = "admin";
  request.password = "new-secret";
  assert(!auth.request_is_authenticated_admin(&request));
  request.url = "/api-security/reset";
  request.arguments["confirm"] = "wrong";
  recovery.handleRequest(&request);
  assert(request.response_code == 403);
  request.arguments["confirm"] = "RESET_API_SECURITY";
  api::test_clear_ok = false;
  recovery.handleRequest(&request);
  assert(request.response_code == 202);
  recovery.loop();
  assert(test_reboots == 0);
  test_millis.fetch_add(500);
  recovery.loop();
  assert(test_reboots == 0 && api::test_saved_key && !api.client.removed);
  api::test_clear_ok = true;
  request.url = "/recovery/end";
  recovery.handleRequest(&request);
  assert(request.response_code == 202);
  recovery.loop();
  assert(!base.is_recovery_active());
  assert(auth.request_is_authenticated_admin(&request));
  recovery.handleRequest(&request);
  assert(request.response_code == 403);

  // A handoff is consumed once and must come from a planned software reset.
  recovery.handoff();
  test_reset_reason = ESP_RST_SW;
  Recovery restored;
  restored.set_web_auth(&auth);
  restored.set_button(&button);
  restored.setup();
  assert(base.is_recovery_active());
  assert(restored.generation() == recovery.generation() + 1);
  restored.loop();
  test_httpd_work();
  restored.loop();
  Recovery consumed;
  consumed.set_web_auth(&auth);
  consumed.set_button(&button);
  consumed.setup();
  assert(consumed.generation() == 0);
  test_millis.fetch_add(600000);
  restored.loop();
  assert(!base.is_recovery_active());
  assert(auth.request_is_authenticated_admin(&request));

  // Authenticated admin reset needs no physical window and must not create one.
  request.url = "/api-security/reset";
  request.arguments["csrf_token"] = auth.get_csrf_token();
  consumed.handleRequest(&request);
  assert(request.response_code == 202);
  test_millis.fetch_add(500);
  consumed.loop();
  assert(test_reboots == 1 && !api::test_saved_key && api::test_runtime_key);
  assert(api.client.removed);  // hostile packet cannot overwrite the clear in teardown
  Recovery admin_reboot;
  admin_reboot.set_web_auth(&auth);
  admin_reboot.set_button(&button);
  admin_reboot.setup();
  assert(admin_reboot.generation() == 0);
  button.state = false;
  admin_reboot.loop();
  button.state = true;
  admin_reboot.loop();
  test_millis.fetch_add(5000);
  admin_reboot.loop();
  test_httpd_work();
  admin_reboot.loop();
  request.arguments["csrf_token"] = admin_reboot.token();
  request.arguments["generation"] = std::to_string(admin_reboot.generation());
  api.client.removed = false;
  api::test_saved_key = true;
  admin_reboot.handleRequest(&request);
  assert(request.response_code == 202);
  test_millis.fetch_add(500);
  admin_reboot.loop();
  assert(test_reboots == 2 && !api::test_saved_key && api.client.removed);
  Recovery physical_reboot;
  physical_reboot.set_web_auth(&auth);
  physical_reboot.set_button(&button);
  physical_reboot.setup();
  assert(physical_reboot.generation() == admin_reboot.generation() + 1);
}
