#include "OpenQuattNetworkManager.h"

#include "esp_eth_driver.h"

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace openquatt_network {

static const char* const TAG = "openquatt.network";
static const uint32_t PREFERENCE_STORAGE_KEY = fnv1_hash("openquatt_network_preference");

void OpenQuattConnectionSelect::control(size_t index) {
  if (this->parent_ != nullptr) {
    this->parent_->request_preference(index);
  }
}

void OpenQuattNetworkManager::setup() {
  this->preference_store_ = global_preferences->make_preference<PreferenceStorage>(PREFERENCE_STORAGE_KEY, true);

  PreferenceStorage stored{};
  if (this->preference_store_.load(&stored) && stored.magic == PREFERENCE_MAGIC &&
      stored.preference <= static_cast<uint8_t>(Preference::ETHERNET)) {
    this->preference_ = static_cast<Preference>(stored.preference);
  }

  // Ethernet must initialize even for WiFi preference so its own SPI driver can
  // put the W5500 PHY into Power Down. For Ethernet preference, WiFi can remain
  // lazily uninitialized until failover is actually needed.
  if (this->preference_ == Preference::ETHERNET && wifi::global_wifi_component != nullptr) {
    wifi::global_wifi_component->set_enable_on_boot(false);
  }

  this->phase_started_ms_ = millis();
  this->publish_preference_();
  ESP_LOGI(TAG, "Preferred connection: %s", preference_name_(this->preference_));
}

void OpenQuattNetworkManager::loop() {
  const uint32_t now = millis();

  if (!this->ethernet_prepared_ && !this->prepare_ethernet_after_setup_()) {
    return;
  }

  this->update_connection_stability_(now);

  switch (this->phase_) {
    case Phase::STARTUP:
      this->handle_startup_(now);
      break;
    case Phase::STEADY:
      this->handle_steady_(now);
      break;
    case Phase::RECOVERY:
      this->handle_recovery_(now);
      break;
    case Phase::SWITCHING:
      this->handle_switching_(now);
      break;
  }

  this->publish_active_connection_();
}

void OpenQuattNetworkManager::dump_config() {
  ESP_LOGCONFIG(TAG,
                "OpenQuatt Network:\n"
                "  Preferred: %s\n"
                "  Detection timeout: %lu ms\n"
                "  Loss timeout: %lu ms\n"
                "  Stable time: %lu ms\n"
                "  Switch timeout: %lu ms",
                preference_name_(this->preference_), static_cast<unsigned long>(this->detection_timeout_ms_),
                static_cast<unsigned long>(this->loss_timeout_ms_), static_cast<unsigned long>(this->stable_time_ms_),
                static_cast<unsigned long>(this->switch_timeout_ms_));
}

void OpenQuattNetworkManager::request_preference(size_t index) {
  if (index > 2) {
    ESP_LOGW(TAG, "Ignoring invalid preferred connection index: %u", static_cast<unsigned>(index));
    return;
  }

  if (index == 0) {
    if (this->preference_ == Preference::AUTOMATIC && this->phase_ == Phase::STARTUP) {
      this->publish_preference_();
      return;
    }
    if (!this->save_preference_(Preference::AUTOMATIC)) {
      ESP_LOGE(TAG, "Automatic connection mode not started because preference persistence failed");
      this->publish_preference_();
      return;
    }
    this->begin_automatic_detection_(millis());
    return;
  }

  const Connection target = index == 1 ? Connection::WIFI : Connection::ETHERNET;
  if (this->phase_ == Phase::STEADY && this->preference_ == connection_preference_(target) && this->active_ == target &&
      this->is_connected_(target)) {
    this->publish_preference_();
    return;
  }

  this->begin_switch_(target, millis());
}

void OpenQuattNetworkManager::update_connection_stability_(uint32_t now) {
  const bool wifi_connected = this->is_connected_(Connection::WIFI);
  if (wifi_connected) {
    if (this->wifi_connected_since_ms_ == 0) {
      this->wifi_connected_since_ms_ = now == 0 ? 1 : now;
    }
  } else {
    this->wifi_connected_since_ms_ = 0;
  }

  const bool ethernet_connected = this->is_connected_(Connection::ETHERNET);
  if (ethernet_connected) {
    if (this->ethernet_connected_since_ms_ == 0) {
      this->ethernet_connected_since_ms_ = now == 0 ? 1 : now;
    }
  } else {
    this->ethernet_connected_since_ms_ = 0;
  }
}

void OpenQuattNetworkManager::handle_startup_(uint32_t now) {
  if ((now - this->last_interface_action_ms_) >= INTERFACE_ACTION_RETRY_MS) {
    this->last_interface_action_ms_ = now;
    if (this->preference_ == Preference::WIFI) {
      if (!this->w5500_powered_down_) {
        this->disable_ethernet_();
      }
    } else if (ethernet::global_eth_component != nullptr && ethernet::global_eth_component->is_disabled()) {
      this->ensure_ethernet_enabled_();
    }
  }

  if (this->preference_ == Preference::AUTOMATIC) {
    if (this->is_stable_(Connection::ETHERNET, now)) {
      this->activate_(Connection::ETHERNET, "initial detection");
      return;
    }

    if ((now - this->phase_started_ms_) >= this->detection_timeout_ms_ && this->is_stable_(Connection::WIFI, now)) {
      this->activate_(Connection::WIFI, "initial detection");
    }
    return;
  }

  const Connection preferred_connection = preference_connection_(this->preference_);
  if (this->is_stable_(preferred_connection, now)) {
    this->activate_(preferred_connection, "preferred connection available");
    return;
  }

  if ((now - this->phase_started_ms_) >= this->loss_timeout_ms_) {
    this->begin_recovery_(now);
  }
}

void OpenQuattNetworkManager::handle_steady_(uint32_t now) {
  if ((now - this->last_interface_action_ms_) >= INTERFACE_ACTION_RETRY_MS) {
    auto* ethernet = ethernet::global_eth_component;
    const bool ethernet_needs_disable = ethernet != nullptr && (!ethernet->is_disabled() || !this->w5500_powered_down_);
    const bool wifi_needs_disable =
        wifi::global_wifi_component != nullptr && !wifi::global_wifi_component->is_disabled();
    if (this->active_ == Connection::WIFI && ethernet_needs_disable) {
      this->last_interface_action_ms_ = now;
      this->disable_ethernet_();
    } else if (this->active_ == Connection::ETHERNET && wifi_needs_disable) {
      this->last_interface_action_ms_ = now;
      this->disable_wifi_();
    }
  }

  if (this->is_connected_(this->active_)) {
    this->disconnected_since_ms_ = 0;
    return;
  }

  if (this->disconnected_since_ms_ == 0) {
    this->disconnected_since_ms_ = now == 0 ? 1 : now;
  }
  if ((now - this->disconnected_since_ms_) >= this->loss_timeout_ms_) {
    this->begin_recovery_(now);
  }
}

void OpenQuattNetworkManager::handle_recovery_(uint32_t now) {
  if ((now - this->last_interface_action_ms_) >= INTERFACE_ACTION_RETRY_MS) {
    this->last_interface_action_ms_ = now;
    this->ensure_wifi_enabled_();
    this->ensure_ethernet_enabled_();
  }

  if (this->preference_ == Preference::AUTOMATIC) {
    if (this->is_stable_(Connection::ETHERNET, now)) {
      this->activate_(Connection::ETHERNET, "automatic recovery");
      return;
    }
    const bool ethernet_was_active = this->active_ == Connection::ETHERNET;
    if ((ethernet_was_active || (now - this->phase_started_ms_) >= this->detection_timeout_ms_) &&
        this->is_stable_(Connection::WIFI, now)) {
      this->activate_(Connection::WIFI, "automatic recovery");
    }
    return;
  }

  const Connection preferred_connection = preference_connection_(this->preference_);
  if (this->is_stable_(preferred_connection, now)) {
    this->activate_(preferred_connection, "preferred connection available");
    return;
  }

  if (this->active_ != Connection::NONE && this->is_stable_(this->active_, now)) {
    this->activate_(this->active_, "connection recovered");
    return;
  }

  const Connection alternate = preferred_connection == Connection::WIFI ? Connection::ETHERNET : Connection::WIFI;
  if (this->is_stable_(alternate, now)) {
    this->activate_(alternate, "automatic failover");
  }
}

void OpenQuattNetworkManager::handle_switching_(uint32_t now) {
  if ((now - this->last_interface_action_ms_) >= INTERFACE_ACTION_RETRY_MS) {
    this->last_interface_action_ms_ = now;
    if (this->switch_target_ == Connection::WIFI) {
      this->ensure_wifi_enabled_();
    } else {
      this->ensure_ethernet_enabled_();
    }
  }

  if (this->is_stable_(this->switch_target_, now)) {
    const Preference target_preference = connection_preference_(this->switch_target_);
    const bool preference_changed = this->preference_ != target_preference;
    if (!preference_changed || this->save_preference_(target_preference)) {
      this->activate_(this->switch_target_, "manual selection");
      return;
    }

    ESP_LOGE(TAG, "Connection switch not committed because preference persistence failed");
    if (this->switch_source_ != Connection::NONE && this->is_connected_(this->switch_source_)) {
      if (this->switch_target_ == Connection::WIFI) {
        this->disable_wifi_();
      } else {
        this->disable_ethernet_();
      }
      this->active_ = this->switch_source_;
      this->phase_ = Phase::STEADY;
      this->disconnected_since_ms_ = 0;
      this->publish_preference_();
      return;
    }

    this->activate_(this->switch_target_, "manual selection without persisted preference");
    return;
  }

  if ((now - this->phase_started_ms_) < this->switch_timeout_ms_) {
    return;
  }

  ESP_LOGW(TAG, "Connection switch to %s timed out", connection_name_(this->switch_target_));
  this->publish_preference_();
  if (this->switch_source_ != Connection::NONE && this->is_connected_(this->switch_source_)) {
    if (this->switch_target_ == Connection::WIFI) {
      this->disable_wifi_();
    } else {
      this->disable_ethernet_();
    }
    this->active_ = this->switch_source_;
    this->phase_ = Phase::STEADY;
    this->disconnected_since_ms_ = 0;
    return;
  }

  this->begin_recovery_(now);
}

bool OpenQuattNetworkManager::prepare_ethernet_after_setup_() {
  const uint32_t now = millis();
  auto* ethernet = ethernet::global_eth_component;
  if (ethernet == nullptr || ethernet->get_eth_handle() == nullptr) {
    // All component setup has completed before loop() runs. Do not let a
    // failed W5500 initialization block WiFi startup or recovery forever.
    ESP_LOGE(TAG, "W5500 driver unavailable; continuing with WiFi fallback");
    this->ethernet_prepared_ = true;
    return true;
  }

  this->last_interface_action_ms_ = now;
  ethernet->disable();
  if (this->preference_ == Preference::WIFI) {
    this->power_down_w5500_();
  } else {
    if (this->wake_w5500_()) {
      ethernet->enable();
    }
  }

  this->ethernet_prepared_ = true;
  this->phase_started_ms_ = now;
  return true;
}

bool OpenQuattNetworkManager::ensure_wifi_enabled_() {
  auto* wifi = wifi::global_wifi_component;
  if (wifi == nullptr) {
    return false;
  }
  if (wifi->is_disabled()) {
    wifi->enable();
  }
  return !wifi->is_disabled();
}

bool OpenQuattNetworkManager::ensure_ethernet_enabled_() {
  auto* ethernet = ethernet::global_eth_component;
  if (ethernet == nullptr || ethernet->get_eth_handle() == nullptr) {
    return false;
  }

  if (ethernet->is_disabled()) {
    // Always wake a stopped W5500 before restart. A prior Power Down write may
    // have reached the PHY even when its verification read failed.
    if (!this->wake_w5500_()) {
      return false;
    }
    ethernet->enable();
  }
  return ethernet->is_enabled();
}

void OpenQuattNetworkManager::disable_wifi_() {
  if (wifi::global_wifi_component != nullptr) {
    wifi::global_wifi_component->disable();
  }
}

bool OpenQuattNetworkManager::disable_ethernet_() {
  auto* ethernet = ethernet::global_eth_component;
  if (ethernet == nullptr || ethernet->get_eth_handle() == nullptr) {
    return false;
  }
  ethernet->disable();
  return this->power_down_w5500_();
}

bool OpenQuattNetworkManager::power_down_w5500_() {
  uint8_t initial = 0;
  if (!this->read_w5500_phycfgr_(&initial) || !this->write_w5500_phycfgr_(W5500_PHYCFGR_POWER_DOWN) ||
      !this->write_w5500_phycfgr_(W5500_PHYCFGR_POWER_DOWN_RESET)) {
    ESP_LOGE(TAG, "W5500 Power Down sequence failed");
    this->w5500_powered_down_ = false;
    return false;
  }

  delay(W5500_PHY_RESET_HOLD_MS);
  uint8_t final = 0;
  if (!this->write_w5500_phycfgr_(W5500_PHYCFGR_POWER_DOWN) || !this->read_w5500_phycfgr_(&final) ||
      (final & W5500_PHYCFGR_CONFIGURATION_MASK) != W5500_PHYCFGR_POWER_DOWN) {
    ESP_LOGE(TAG, "W5500 Power Down verification failed: PHYCFGR=0x%02X", final);
    this->w5500_powered_down_ = false;
    return false;
  }

  this->w5500_powered_down_ = true;
  ESP_LOGI(TAG, "W5500 PHY powered down: PHYCFGR 0x%02X -> 0x%02X", initial, final);
  return true;
}

bool OpenQuattNetworkManager::wake_w5500_() {
  if (!this->write_w5500_phycfgr_(W5500_PHYCFGR_ALL_CAPABLE) ||
      !this->write_w5500_phycfgr_(W5500_PHYCFGR_ALL_CAPABLE_RESET)) {
    ESP_LOGE(TAG, "W5500 wake sequence failed");
    return false;
  }

  delay(W5500_PHY_RESET_HOLD_MS);
  uint8_t final = 0;
  if (!this->write_w5500_phycfgr_(W5500_PHYCFGR_ALL_CAPABLE) || !this->read_w5500_phycfgr_(&final) ||
      (final & W5500_PHYCFGR_CONFIGURATION_MASK) != W5500_PHYCFGR_ALL_CAPABLE) {
    ESP_LOGE(TAG, "W5500 wake verification failed: PHYCFGR=0x%02X", final);
    return false;
  }

  this->w5500_powered_down_ = false;
  ESP_LOGI(TAG, "W5500 PHY awake: PHYCFGR=0x%02X", final);
  return true;
}

bool OpenQuattNetworkManager::read_w5500_phycfgr_(uint8_t* value) {
  auto* ethernet = ethernet::global_eth_component;
  if (ethernet == nullptr || ethernet->get_eth_handle() == nullptr || value == nullptr) {
    return false;
  }

  uint32_t register_value = 0;
  esp_eth_phy_reg_rw_data_t data{};
  data.reg_addr = W5500_PHYCFGR_REGISTER;
  data.reg_value_p = &register_value;
  const esp_err_t error = esp_eth_ioctl(ethernet->get_eth_handle(), ETH_CMD_READ_PHY_REG, &data);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "W5500 PHYCFGR read failed: %s", esp_err_to_name(error));
    return false;
  }
  *value = static_cast<uint8_t>(register_value);
  return true;
}

bool OpenQuattNetworkManager::write_w5500_phycfgr_(uint8_t value) {
  auto* ethernet = ethernet::global_eth_component;
  if (ethernet == nullptr || ethernet->get_eth_handle() == nullptr) {
    return false;
  }

  uint32_t register_value = value;
  esp_eth_phy_reg_rw_data_t data{};
  data.reg_addr = W5500_PHYCFGR_REGISTER;
  data.reg_value_p = &register_value;
  const esp_err_t error = esp_eth_ioctl(ethernet->get_eth_handle(), ETH_CMD_WRITE_PHY_REG, &data);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "W5500 PHYCFGR write failed: %s", esp_err_to_name(error));
    return false;
  }
  return true;
}

bool OpenQuattNetworkManager::is_connected_(Connection connection) const {
  if (connection == Connection::WIFI) {
    return wifi::global_wifi_component != nullptr && wifi::global_wifi_component->is_connected();
  }
  if (connection == Connection::ETHERNET) {
    return ethernet::global_eth_component != nullptr && ethernet::global_eth_component->is_connected();
  }
  return false;
}

bool OpenQuattNetworkManager::is_stable_(Connection connection, uint32_t now) const {
  const uint32_t connected_since =
      connection == Connection::WIFI ? this->wifi_connected_since_ms_ : this->ethernet_connected_since_ms_;
  return connected_since != 0 && (now - connected_since) >= this->stable_time_ms_;
}

void OpenQuattNetworkManager::begin_automatic_detection_(uint32_t now) {
  ESP_LOGI(TAG, "Starting automatic connection detection");
  this->ensure_wifi_enabled_();
  this->ensure_ethernet_enabled_();
  this->last_interface_action_ms_ = now;
  this->phase_ = Phase::STARTUP;
  this->phase_started_ms_ = now;
  this->disconnected_since_ms_ = 0;
}

void OpenQuattNetworkManager::begin_recovery_(uint32_t now) {
  ESP_LOGW(TAG, "Active connection lost; enabling WiFi and Ethernet");
  this->ensure_wifi_enabled_();
  this->ensure_ethernet_enabled_();
  this->last_interface_action_ms_ = now;
  this->phase_ = Phase::RECOVERY;
  this->phase_started_ms_ = now;
  this->disconnected_since_ms_ = 0;
}

void OpenQuattNetworkManager::begin_switch_(Connection target, uint32_t now) {
  ESP_LOGI(TAG, "Switching preferred connection to %s", connection_name_(target));
  this->switch_target_ = target;
  this->switch_source_ = this->active_;
  this->phase_ = Phase::SWITCHING;
  this->phase_started_ms_ = now;
  if (target == Connection::WIFI) {
    this->ensure_wifi_enabled_();
  } else {
    this->ensure_ethernet_enabled_();
  }
  this->last_interface_action_ms_ = now;
}

void OpenQuattNetworkManager::activate_(Connection connection, const char* reason) {
  this->active_ = connection;
  this->phase_ = Phase::STEADY;
  this->disconnected_since_ms_ = 0;
  this->switch_target_ = Connection::NONE;
  this->switch_source_ = Connection::NONE;

  if (connection == Connection::WIFI) {
    this->disable_ethernet_();
  } else if (connection == Connection::ETHERNET) {
    this->disable_wifi_();
  }
  this->last_interface_action_ms_ = millis();

  ESP_LOGI(TAG, "Active connection: %s (%s)", connection_name_(connection), reason);
  this->publish_active_connection_();
}

bool OpenQuattNetworkManager::save_preference_(Preference preference) {
  PreferenceStorage stored{};
  stored.magic = PREFERENCE_MAGIC;
  stored.preference = static_cast<uint8_t>(preference);
  if (!this->preference_store_.save(&stored)) {
    return false;
  }

  this->preference_ = preference;
  this->publish_preference_();
  return true;
}

void OpenQuattNetworkManager::publish_preference_() {
  if (this->preferred_connection_select_ == nullptr) {
    return;
  }
  if (!this->preference_published_ || this->published_preference_ != this->preference_) {
    this->preferred_connection_select_->publish_state(preference_name_(this->preference_));
    this->published_preference_ = this->preference_;
    this->preference_published_ = true;
  }
}

void OpenQuattNetworkManager::publish_active_connection_() {
  if (this->active_connection_sensor_ == nullptr) {
    return;
  }

  Connection visible = this->active_;
  if (!this->is_connected_(visible)) {
    visible = Connection::NONE;
  }
  if (!this->active_published_ || this->published_active_ != visible) {
    this->active_connection_sensor_->publish_state(connection_name_(visible));
    this->published_active_ = visible;
    this->active_published_ = true;
  }
}

OpenQuattNetworkManager::Connection OpenQuattNetworkManager::preference_connection_(Preference preference) {
  switch (preference) {
    case Preference::WIFI:
      return Connection::WIFI;
    case Preference::ETHERNET:
      return Connection::ETHERNET;
    case Preference::AUTOMATIC:
    default:
      return Connection::NONE;
  }
}

OpenQuattNetworkManager::Preference OpenQuattNetworkManager::connection_preference_(Connection connection) {
  return connection == Connection::ETHERNET ? Preference::ETHERNET : Preference::WIFI;
}

const char* OpenQuattNetworkManager::preference_name_(Preference preference) {
  switch (preference) {
    case Preference::WIFI:
      return "WiFi";
    case Preference::ETHERNET:
      return "Ethernet";
    case Preference::AUTOMATIC:
    default:
      return "Automatic";
  }
}

const char* OpenQuattNetworkManager::connection_name_(Connection connection) {
  switch (connection) {
    case Connection::WIFI:
      return "WiFi";
    case Connection::ETHERNET:
      return "Ethernet";
    case Connection::NONE:
    default:
      return "Not connected";
  }
}

}  // namespace openquatt_network
}  // namespace esphome
