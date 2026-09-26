#include <atomic>
#include <cassert>
#include <cstdint>

#define ESP_LOGD(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGV(...) ((void)0)
using esp_err_t = int;
using esp_event_base_t = int;
constexpr int ESP_OK = 0;
constexpr int ETHERNET_EVENT_START = 1;
constexpr int ETHERNET_EVENT_STOP = 2;
constexpr int ETHERNET_EVENT_CONNECTED = 3;
constexpr int ETHERNET_EVENT_DISCONNECTED = 4;
int stop_result = ESP_OK;
int start_result = ESP_OK;
int stop_calls = 0;
int start_calls = 0;
uint32_t clock_ms = 100;
uint32_t millis() { return clock_ms; }
void delay(uint32_t) {}
esp_err_t esp_eth_stop(void*) {
  ++stop_calls;
  return stop_result;
}
esp_err_t esp_eth_start(void*) {
  ++start_calls;
  return start_result;
}
enum class EthernetComponentState { STOPPED, CONNECTED };
class EthernetComponent {
 public:
  void enable();
  void disable();
  static void eth_event_handler(void*, esp_event_base_t, int32_t, void*);
  bool is_disabled() { return disabled_; }
  bool is_enabled() { return !disabled_; }
  bool is_driver_stopped() const { return disabled_ && driver_stopped_.load(std::memory_order_acquire); }
  bool is_connected() { return !disabled_ && state_ == EthernetComponentState::CONNECTED; }
  void* get_eth_handle() { return eth_handle_; }
  void ethernet_lazy_init_() {}
  void enable_loop() {}
  void enable_loop_soon_any_context() {}
  void* eth_handle_{this};
  bool disabled_{false};
  bool ethernet_initialized_{true};
  bool started_{true};
  bool connected_{true};
  std::atomic<bool> driver_stopped_{false};
  EthernetComponentState state_{EthernetComponentState::CONNECTED};
};
EthernetComponent* global_eth_component;
namespace ethernet {
EthernetComponent*& global_eth_component = ::global_eth_component;
}
class OpenQuattNetworkManager {
 public:
  enum class Preference { WIFI, AUTOMATIC };
  bool ensure_ethernet_enabled_();
  bool disable_ethernet_();
  bool prepare_ethernet_after_setup_();
  bool power_down_w5500_();
  bool wake_w5500_();
  bool read_w5500_phycfgr_(uint8_t* value) {
    *value = register_value_;
    return reads_succeed_;
  }
  bool write_w5500_phycfgr_(uint8_t value) {
    ++writes_;
    register_value_ = value;
    return writes_succeed_;
  }
  static constexpr uint8_t W5500_PHYCFGR_POWER_DOWN = 0xF0;
  static constexpr uint8_t W5500_PHYCFGR_POWER_DOWN_RESET = 0x70;
  static constexpr uint8_t W5500_PHYCFGR_ALL_CAPABLE = 0xF8;
  static constexpr uint8_t W5500_PHYCFGR_ALL_CAPABLE_RESET = 0x78;
  static constexpr uint8_t W5500_PHYCFGR_CONFIGURATION_MASK = 0xF8;
  static constexpr uint32_t W5500_PHY_RESET_HOLD_MS = 10;
  Preference preference_{Preference::WIFI};
  bool ethernet_prepared_{false};
  bool w5500_powered_down_{false};
  uint32_t last_interface_action_ms_{0};
  uint32_t phase_started_ms_{0};
  int writes_{0};
  bool writes_succeed_{true};
  bool reads_succeed_{true};
  uint8_t register_value_{0xF8};
};

// PRODUCTION_METHODS

int main() {
  EthernetComponent eth;
  global_eth_component = &eth;
  OpenQuattNetworkManager manager;
  stop_result = -1;
  assert(!manager.disable_ethernet_());
  assert(!eth.is_disabled());
  assert(eth.is_connected());
  assert(manager.writes_ == 0);
  assert(manager.prepare_ethernet_after_setup_());  // WiFi startup never blocks.
  assert(manager.ethernet_prepared_);
  assert(manager.writes_ == 0);
  assert(!manager.power_down_w5500_());
  assert(!manager.wake_w5500_());
  assert(manager.writes_ == 0);
  stop_result = ESP_OK;
  assert(!manager.disable_ethernet_());  // Stop accepted, event still pending.
  assert(eth.is_disabled());
  assert(!eth.is_connected());  // Reject stale CONNECTED state immediately.
  assert(!manager.ensure_ethernet_enabled_());
  assert(start_calls == 0 && manager.writes_ == 0);
  const int accepted_stop_calls = stop_calls;
  assert(!manager.disable_ethernet_());
  assert(stop_calls == accepted_stop_calls);  // No second driver stop.
  EthernetComponent::eth_event_handler(nullptr, 0, ETHERNET_EVENT_START, nullptr);
  assert(!eth.is_driver_stopped());  // Queued old START is not a stop boundary.
  EthernetComponent::eth_event_handler(nullptr, 0, ETHERNET_EVENT_STOP, nullptr);
  assert(eth.is_driver_stopped());
  assert(manager.disable_ethernet_());
  assert(manager.w5500_powered_down_);
  manager.writes_succeed_ = false;
  assert(!manager.ensure_ethernet_enabled_());  // Failed wake never starts driver.
  assert(start_calls == 0);
  manager.writes_succeed_ = true;
  assert(manager.ensure_ethernet_enabled_());
  assert(start_calls == 1 && !eth.is_disabled());
  assert(!eth.is_connected());  // Previous connection stability cannot survive restart.
  assert(!eth.is_driver_stopped());
  const int awake_writes = manager.writes_;
  assert(!manager.power_down_w5500_() && !manager.wake_w5500_());
  assert(manager.writes_ == awake_writes);
  // Stop again, then inject ambiguous start failure. No unsafe repeated PHY reset.
  assert(!manager.disable_ethernet_());
  EthernetComponent::eth_event_handler(nullptr, 0, ETHERNET_EVENT_STOP, nullptr);
  start_result = -1;
  assert(!manager.ensure_ethernet_enabled_());
  assert(eth.is_disabled() && !eth.is_driver_stopped());
  const int failed_start_writes = manager.writes_;
  assert(!manager.ensure_ethernet_enabled_());
  assert(manager.writes_ == failed_start_writes && start_calls == 2);
  // Accepted start cleanup allows retry only after STOP delivery.
  EthernetComponent::eth_event_handler(nullptr, 0, ETHERNET_EVENT_STOP, nullptr);
  start_result = ESP_OK;
  assert(manager.ensure_ethernet_enabled_());
  assert(start_calls == 3);
  assert(!manager.disable_ethernet_());
  EthernetComponent::eth_event_handler(nullptr, 0, ETHERNET_EVENT_STOP, nullptr);
  start_result = -1;
  stop_result = -1;
  assert(!manager.ensure_ethernet_enabled_());
  const int ambiguous_writes = manager.writes_;
  start_result = ESP_OK;
  assert(!manager.ensure_ethernet_enabled_());  // No STOP: do not blindly restart.
  assert(manager.writes_ == ambiguous_writes && start_calls == 4);
}
