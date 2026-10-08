#include <cassert>
#include "OpenQuattRecovery.h"
#include "esp_system.h"
#include "esp_http_server.h"
#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#ifdef USE_WIFI
#include "esphome/components/wifi/wifi_component.h"
#endif
using namespace esphome;
class Recovery : public openquatt_recovery::OpenQuattRecovery {
 public:
  std::string token() const { return csrf_token_; }
  uint32_t generation() const { return state_.generation(); }
  void handoff() { prepare_reboot_handoff_(); }
};
struct Boot {
  web_server_base::WebServerBase base;
  openquatt_web_auth::OpenQuattWebAuth auth;
  binary_sensor::BinarySensor button;
  Recovery recovery;
  Boot(bool ready = true) {
    web_server_base::global_web_server_base = &base;
    auth.set_bootstrap_username("admin");
    auth.set_bootstrap_password("secret");
    recovery.set_web_auth(&auth);
    recovery.set_button(&button);
    recovery.set_restart_handler([]() { App.safe_reboot(); });
    assert(recovery.get_setup_priority() > auth.get_setup_priority());
    assert(auth.get_setup_priority() > setup_priority::WIFI + 1);
    recovery.setup();
    auth.setup();
    base.init();
    if (ready) recovery.set_runtime_ready();
  }
  void physical(uint32_t duration) {
    button.state = false;
    recovery.loop();
    button.state = true;
    recovery.loop();
    test_millis.fetch_add(duration);
    recovery.loop();
    button.state = false;
    recovery.loop();
  }
  void finish() {
    test_millis.fetch_add(500);
    recovery.loop();
    if (test_httpd_work) {
      test_httpd_work();
      test_httpd_work = {};
    }
    recovery.loop();
    recovery.loop();
  }
  void authorize(AsyncWebServerRequest& request) {
    request.headers = {{"Host", "openquatt.local"}, {"Origin", "http://openquatt.local"}};
    request.arguments["csrf_token"] = recovery.token();
    request.arguments["generation"] = std::to_string(recovery.generation());
  }
};
int main() {
  api::APIServer api;
  api::global_api_server = &api;
  Boot early(false);
  AsyncWebServerRequest early_request;
  early_request.url = "/number/set";
  assert(early.recovery.canHandle(&early_request));  // normal HTTP stays closed through App.setup
  early.recovery.set_runtime_ready();
  assert(!early.recovery.canHandle(&early_request));
  Boot normal;
  AsyncWebServerRequest request;
  request.verb = HTTP_POST;
  request.url = "/recovery/web-auth";
  request.arguments = {{"new_username", "next"}, {"new_password", "next-secret"}};
  normal.authorize(request);
  normal.recovery.handleRequest(&request);
  assert(request.response_code == 403);
  // Press held at boot is inert until release; 5s during a press does not reset.
  normal.button.state = true;
  normal.recovery.loop();
  test_millis.fetch_add(10000);
  normal.recovery.loop();
  normal.button.state = false;
  normal.recovery.loop();
  assert(test_reboots == 0);
  normal.physical(5000);
  assert(test_reboots == 0 && normal.recovery.generation() == 0);
  normal.finish();
  assert(test_reboots == 1);

  test_reset_reason = ESP_RST_SW;
  Boot physical(false);
  assert(physical.recovery.generation() == 1);
  request.url = "/number/set";
  assert(physical.recovery.canHandle(&request));
  physical.recovery.handleBody(&request, nullptr, 0, 0, 0);  // no normal mutation/OTA upload
  physical.recovery.handleUpload(&request, "firmware", 0, nullptr, 0, true);
  request.username = "admin";
  request.password = "secret";
  assert(!physical.auth.request_is_authenticated_admin(&request));
  request.url = "/api-security/reset";
  request.arguments["confirm"] = "RESET_API_SECURITY";
  physical.authorize(request);
  physical.recovery.handleRequest(&request);
  assert(request.response_code == 202);
  test_millis.fetch_add(500);
  physical.recovery.loop();
  assert(test_reboots == 1 && api::test_saved_key);  // API prefs not initialized yet
  api::test_clear_ok = false;
  physical.recovery.set_runtime_ready();
  physical.finish();
  assert(test_reboots == 1 && api::test_saved_key);
  test_millis.fetch_add(600000);
  physical.recovery.loop();
  assert(test_reboots == 1);  // partial persistence failure cannot expire/reboot
  request.url = "/recovery/web-auth";
  request.headers["Origin"] = "http://foreign.example";
  physical.recovery.handleRequest(&request);
  assert(request.response_code == 403);
  physical.authorize(request);
  request.arguments["generation"] = "0";
  physical.recovery.handleRequest(&request);
  assert(request.response_code == 403);
  physical.authorize(request);
  test_sync_ok = false;
  physical.recovery.handleRequest(&request);
  assert(request.response_code == 202);
  physical.recovery.handleRequest(&request);
  assert(request.response_code == 403);
  physical.finish();
  assert(test_reboots == 1);
  test_sync_ok = true;
  physical.recovery.handleRequest(&request);
  assert(request.response_code == 202);
  physical.finish();
  assert(test_reboots == 2);
  assert(!physical.auth.request_is_authenticated_admin(&request));  // never unlocks old process

  Boot after_auth;
  assert(after_auth.recovery.generation() == 0);
  request.username = "next";
  request.password = "next-secret";
  assert(after_auth.auth.request_is_authenticated_admin(&request));
  request.url = "/api-security/reset";
  request.arguments["csrf_token"] = after_auth.auth.get_csrf_token();
  api::test_clear_ok = true;
  after_auth.recovery.handleRequest(&request);
  assert(request.response_code == 202);
  bool draining = true;
  after_auth.auth.add_restart_blocker(&draining, [](void* p) { return *static_cast<bool*>(p); });
  test_millis.fetch_add(500);
  after_auth.recovery.loop();
  assert(test_reboots == 2);
  draining = false;
  after_auth.recovery.on_ota_global_state(ota::OTA_STARTED, 0, 0, nullptr);
  after_auth.recovery.loop();
  assert(test_reboots == 2 && api::test_saved_key);
  after_auth.recovery.on_ota_global_state(ota::OTA_ABORT, 0, 0, nullptr);
  after_auth.finish();
  assert(test_reboots == 3 && !api::test_saved_key && api::test_runtime_key && api.client.removed);

  Boot admin_reboot;
  assert(admin_reboot.recovery.generation() == 0);  // admin API reset grants no physical capability
  admin_reboot.recovery.handoff();
  test_reset_reason = 1;
  Boot cold;
  assert(cold.recovery.generation() == 0);
  test_reset_reason = ESP_RST_SW;
  Boot consumed;
  assert(consumed.recovery.generation() == 0);
  consumed.recovery.handoff();
  Boot expiring;
  test_millis.fetch_add(600000);
  expiring.recovery.loop();
  expiring.finish();
  assert(test_reboots == 4);
  assert(!expiring.auth.request_is_authenticated_admin(&request));

#ifdef USE_WIFI
  Boot wifi_hold;
  wifi_hold.physical(10000);
  test_millis.fetch_add(500);
  wifi::test_clear_ok = false;
  wifi_hold.finish();
  assert(test_reboots == 4 && wifi::test_clears == 0);
  wifi::test_clear_ok = true;
  wifi_hold.physical(10000);
  test_millis.fetch_add(500);
  wifi_hold.finish();
  assert(test_reboots == 5 && wifi::test_clears == 1);
  Boot wifi_reboot;
  assert(wifi_reboot.recovery.generation() == 1);
#endif

  // A failed HTTPD fence cannot authorize a reboot or persistence. An explicit
  // physical retry must establish a new fence rather than reuse stale state.
  const unsigned before_fence_failure = test_reboots;
  Boot fence_failure;
  fence_failure.physical(5000);
  test_queue_ok = false;
  fence_failure.finish();
  assert(test_reboots == before_fence_failure);
  test_queue_ok = true;
  fence_failure.physical(5000);
  fence_failure.finish();
  assert(test_reboots == before_fence_failure + 1);
  Boot fenced_recovery;
  assert(fenced_recovery.recovery.generation() != 0);

  // Web OTA can defer STARTED until after the recovery request is queued.
  // Neither that notification nor an aborted upload proves HTTPD has drained.
  ota::OTAComponent web_ota;
  fenced_recovery.recovery.set_web_ota(&web_ota);
  fenced_recovery.authorize(request);
  request.url = "/recovery/end";
  fenced_recovery.recovery.handleRequest(&request);
  assert(request.response_code == 202);
  test_millis.fetch_add(500);
  fenced_recovery.recovery.loop();
  assert(test_httpd_work && test_reboots == before_fence_failure + 1);
  fenced_recovery.recovery.on_ota_global_state(ota::OTA_STARTED, 0, 0, &web_ota);
  fenced_recovery.recovery.loop();
  assert(test_reboots == before_fence_failure + 1);
  test_httpd_work();
  test_httpd_work = {};
  fenced_recovery.recovery.loop();
  assert(test_reboots == before_fence_failure + 1);
  fenced_recovery.recovery.loop();
  assert(test_reboots == before_fence_failure + 2);

  // A completed OTA owns the reboot, even if a physical reset was pending.
  // Recovery must not clear state or leave an RTC capability for the new image.
  Boot completed_ota;
  completed_ota.physical(10000);
  completed_ota.recovery.on_ota_global_state(ota::OTA_COMPLETED, 100, 0, &web_ota);
#ifdef USE_WIFI
  const auto clears_before_ota = wifi::test_clears;
#endif
  completed_ota.finish();
  assert(test_reboots == before_fence_failure + 2);
#ifdef USE_WIFI
  assert(wifi::test_clears == clears_before_ota);
#endif
  Boot ota_reboot;
  assert(ota_reboot.recovery.generation() == 0);
}
