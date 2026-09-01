#pragma once

#include <cstddef>
#include <cstdint>

#include "esphome/components/ethernet/ethernet_component.h"
#include "esphome/components/select/select.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/wifi/wifi_component.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"

namespace esphome {
namespace openquatt_network {

class OpenQuattNetworkManager;

class OpenQuattConnectionSelect : public select::Select {
 public:
  void set_parent(OpenQuattNetworkManager* parent) { this->parent_ = parent; }

 protected:
  void control(size_t index) override;

  OpenQuattNetworkManager* parent_{nullptr};
};

class OpenQuattNetworkManager : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::WIFI + 1.0f; }

  void set_active_connection_sensor(text_sensor::TextSensor* sensor) { this->active_connection_sensor_ = sensor; }
  void set_preferred_connection_select(OpenQuattConnectionSelect* select) {
    this->preferred_connection_select_ = select;
  }
  void set_detection_timeout(uint32_t timeout_ms) { this->detection_timeout_ms_ = timeout_ms; }
  void set_loss_timeout(uint32_t timeout_ms) { this->loss_timeout_ms_ = timeout_ms; }
  void set_stable_time(uint32_t stable_time_ms) { this->stable_time_ms_ = stable_time_ms; }
  void set_switch_timeout(uint32_t timeout_ms) { this->switch_timeout_ms_ = timeout_ms; }

  void request_preference(size_t index);

 protected:
  enum class Connection : uint8_t {
    NONE = 0,
    WIFI = 1,
    ETHERNET = 2,
  };

  enum class Preference : uint8_t {
    AUTOMATIC = 0,
    WIFI = 1,
    ETHERNET = 2,
  };

  enum class Phase : uint8_t {
    STARTUP,
    STEADY,
    RECOVERY,
    SWITCHING,
  };

  struct PreferenceStorage {
    uint32_t magic;
    uint8_t preference;
    uint8_t reserved[3];
  };

  static_assert(sizeof(PreferenceStorage) == 8U, "Network preference NVS budget changed");

  static constexpr uint32_t PREFERENCE_MAGIC = 0x4F514E32UL;
  static constexpr uint32_t W5500_PHYCFGR_REGISTER = 0x002E0000UL;
  static constexpr uint8_t W5500_PHYCFGR_POWER_DOWN = 0xF0;
  static constexpr uint8_t W5500_PHYCFGR_POWER_DOWN_RESET = 0x70;
  static constexpr uint8_t W5500_PHYCFGR_ALL_CAPABLE = 0xF8;
  static constexpr uint8_t W5500_PHYCFGR_ALL_CAPABLE_RESET = 0x78;
  static constexpr uint8_t W5500_PHYCFGR_CONFIGURATION_MASK = 0xF8;
  static constexpr uint32_t W5500_PHY_RESET_HOLD_MS = 10;
  static constexpr uint32_t INTERFACE_ACTION_RETRY_MS = 5000;

  void update_connection_stability_(uint32_t now);
  void handle_startup_(uint32_t now);
  void handle_steady_(uint32_t now);
  void handle_recovery_(uint32_t now);
  void handle_switching_(uint32_t now);

  bool prepare_ethernet_after_setup_();
  bool ensure_wifi_enabled_();
  bool ensure_ethernet_enabled_();
  void disable_wifi_();
  bool disable_ethernet_();
  bool power_down_w5500_();
  bool wake_w5500_();
  bool read_w5500_phycfgr_(uint8_t* value);
  bool write_w5500_phycfgr_(uint8_t value);

  bool is_connected_(Connection connection) const;
  bool is_stable_(Connection connection, uint32_t now) const;
  void begin_automatic_detection_(uint32_t now);
  void begin_recovery_(uint32_t now);
  void begin_switch_(Connection target, uint32_t now);
  void activate_(Connection connection, const char* reason);
  bool save_preference_(Preference preference);
  void publish_preference_();
  void publish_active_connection_();
  static Connection preference_connection_(Preference preference);
  static Preference connection_preference_(Connection connection);
  static const char* preference_name_(Preference preference);
  static const char* connection_name_(Connection connection);

  text_sensor::TextSensor* active_connection_sensor_{nullptr};
  OpenQuattConnectionSelect* preferred_connection_select_{nullptr};
  ESPPreferenceObject preference_store_;

  Preference preference_{Preference::AUTOMATIC};
  Connection active_{Connection::NONE};
  Connection switch_target_{Connection::NONE};
  Connection switch_source_{Connection::NONE};
  Preference published_preference_{Preference::AUTOMATIC};
  Connection published_active_{Connection::NONE};
  Phase phase_{Phase::STARTUP};

  uint32_t phase_started_ms_{0};
  uint32_t disconnected_since_ms_{0};
  uint32_t wifi_connected_since_ms_{0};
  uint32_t ethernet_connected_since_ms_{0};
  uint32_t last_interface_action_ms_{0};
  uint32_t detection_timeout_ms_{10000};
  uint32_t loss_timeout_ms_{30000};
  uint32_t stable_time_ms_{2000};
  uint32_t switch_timeout_ms_{30000};

  bool preference_published_{false};
  bool active_published_{false};
  bool ethernet_prepared_{false};
  bool w5500_powered_down_{false};
};

}  // namespace openquatt_network
}  // namespace esphome
