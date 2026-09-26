#include <atomic>
#include <cassert>
#include <cstdint>

#define ESP_LOGD(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGV(...) ((void)0)
uint32_t clock_ms = 100;
uint32_t millis() { return clock_ms; }
void delay(uint32_t) {}
// This is the upstream public API contract, not an implementation copy.
// Generic driver lifecycle tests belong in ESPHome; this fixture drives the
// actual OpenQuatt manager through enabled, stopping and confirmed-stop states.
class EthernetComponent {
 public:
  void enable() {
    ++start_calls_;
    stopped_ = false;
    if (start_succeeds_) disabled_ = false;
  }
  void disable() {
    ++stop_calls_;
    if (stop_succeeds_) disabled_ = true;
  }
  bool is_disabled() { return disabled_; }
  bool is_enabled() { return !disabled_; }
  bool is_driver_stopped() const { return disabled_ && stopped_; }
  void* get_eth_handle() { return eth_handle_; }
  void* eth_handle_{this};
  bool disabled_{false};
  bool stopped_{false};
  bool stop_succeeds_{true};
  bool start_succeeds_{true};
  int start_calls_{0};
  int stop_calls_{0};
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
  eth.stop_succeeds_ = false;
  assert(!manager.disable_ethernet_());
  assert(manager.writes_ == 0);
  assert(manager.prepare_ethernet_after_setup_());  // Failed stop never blocks WiFi startup.
  assert(manager.ethernet_prepared_ && manager.writes_ == 0);
  assert(!manager.power_down_w5500_() && !manager.wake_w5500_());
  assert(manager.writes_ == 0);

  eth.stop_succeeds_ = true;
  assert(!manager.disable_ethernet_());  // Stop accepted, STOP acknowledgement delayed/lost.
  assert(!manager.ensure_ethernet_enabled_());
  for (int retry = 0; retry < 3; ++retry) {
    assert(!manager.disable_ethernet_());
    assert(!manager.ensure_ethernet_enabled_());
  }
  assert(eth.start_calls_ == 0 && manager.writes_ == 0);
  eth.stopped_ = true;  // Upstream delivers the stop barrier.
  assert(manager.disable_ethernet_());
  assert(manager.w5500_powered_down_);

  manager.writes_succeed_ = false;
  assert(!manager.ensure_ethernet_enabled_());  // Failed wake never starts the driver.
  assert(eth.start_calls_ == 0);
  manager.writes_succeed_ = true;
  manager.reads_succeed_ = false;
  assert(!manager.ensure_ethernet_enabled_());  // Unverified wake is also unsafe.
  assert(eth.start_calls_ == 0);
  manager.reads_succeed_ = true;
  assert(manager.ensure_ethernet_enabled_());
  assert(eth.start_calls_ == 1 && !eth.is_disabled());
  const int awake_writes = manager.writes_;
  assert(!manager.power_down_w5500_() && !manager.wake_w5500_());
  assert(manager.writes_ == awake_writes);

  assert(!manager.disable_ethernet_());
  eth.stopped_ = true;
  eth.start_succeeds_ = false;
  assert(!manager.ensure_ethernet_enabled_());  // Ambiguous partial-start failure.
  const int ambiguous_writes = manager.writes_;
  assert(!manager.ensure_ethernet_enabled_());
  assert(manager.writes_ == ambiguous_writes && eth.start_calls_ == 2);
  eth.stopped_ = true;
  eth.start_succeeds_ = true;
  assert(manager.ensure_ethernet_enabled_());  // Recovery requires a real stop boundary.

  eth.eth_handle_ = nullptr;
  assert(!manager.ensure_ethernet_enabled_() && !manager.disable_ethernet_());
  assert(manager.prepare_ethernet_after_setup_());  // Unavailable driver allows WiFi fallback.
  global_eth_component = nullptr;
  assert(!manager.ensure_ethernet_enabled_() && !manager.disable_ethernet_());
  assert(manager.prepare_ethernet_after_setup_());
}
