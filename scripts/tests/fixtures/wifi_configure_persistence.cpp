#include <functional>
namespace esphome {
template <typename T>
struct TemplateValue {
  T val{};
  T value() { return val; }
};
#define TEMPLATABLE_VALUE(type, name) TemplateValue<type> name##_;
template <typename... Ts>
struct Action {
  virtual void play(const Ts&...) = 0;
};
template <typename... Ts>
struct Trigger {
  int calls = 0;
  void trigger(const Ts&...) { ++calls; }
};
struct Component {
  virtual void loop() {}
  std::function<void()> timeout;
  void set_timeout(const char*, uint32_t, std::function<void()> value) { timeout = value; }
  void cancel_timeout(const char*) { timeout = {}; }
};
namespace wifi {
WiFiComponent* global_wifi_component = nullptr;
// PRODUCTION_WIFI_ACTION
}  // namespace wifi
}  // namespace esphome
int main() {
  using namespace esphome;
  using namespace esphome::wifi;
  WiFiComponent device;
  device.init_preferences_();
  global_wifi_component = &device;
  WiFiConfigureAction<> action;
  action.ssid_.val = "new";
  action.password_.val = "password";
  action.save_.val = true;
  action.connection_timeout_.val = 30000;
  for (bool invalid_ssid : {true, false}) {
    (invalid_ssid ? action.ssid_.val : action.password_.val) = std::string("new\0suffix", 10);
    action.play();
    assert(action.get_connect_trigger()->calls == 0 && action.get_error_trigger()->calls == (invalid_ssid ? 1 : 2));
    assert(!action.timeout && !device.pending_credentials_ && disk.empty() && cache.empty() && syncs == 0);
    assert(device.get_provisioning_status().generation == 0);
    action.ssid_.val = "new";
    action.password_.val = "password";
  }
  action.get_error_trigger()->calls = 0;
  action.play();
  device.connected_ = true;
  device.connected_ssid = "new";
  device.persist_pending_credentials_();
  action.loop();
  assert(action.get_connect_trigger()->calls == 0 && action.get_error_trigger()->calls == 0);
  device.acknowledge_stop();
  commit_ok = false;
  device.connect_candidate();
  action.loop();
  assert(action.get_connect_trigger()->calls == 0 && action.get_error_trigger()->calls == 1);
  assert(!action.timeout && device.ap_enabled);
  commit_ok = true;
  action.play();
  device.acknowledge_stop();
  device.connect_candidate();
  action.loop();
  assert(action.get_connect_trigger()->calls == 1 && action.get_error_trigger()->calls == 1);
  action.password_.val = "wrong";
  action.play();
  assert(device.pending_credentials_ && device.provisioning_stopping_);  // same SSID must reconnect
  auto timeout = action.timeout;
  timeout();
  device.acknowledge_stop();
  SavedWifiSettings saved{};
  assert(device.pref_.load(&saved) && strcmp(saved.password, "password") == 0);
  assert(action.get_connect_trigger()->calls == 1 && action.get_error_trigger()->calls == 2);
}
