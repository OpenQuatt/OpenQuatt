#pragma once

#include <mutex>

#include "PsramBuffer.h"
#include "ble_advertisement.h"
#include "ble_registry.h"
#include "esphome/core/component.h"
#include "nimble_scanner.h"

#ifndef USE_OPENQUATT_BLE_NIMBLE
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"
#endif

namespace esphome::openquatt_web_auth {
class OpenQuattWebAuth;
}
namespace esphome::openquatt_ble {
enum class Submit { ACCEPTED, BUSY, CONFLICT, INVALID, UNAVAILABLE };

class OpenQuattBle : public Component {
 public:
  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::LATE; }
#ifdef USE_OPENQUATT_BLE_NIMBLE
  void on_safe_shutdown() override {
    if (state_) radio_(false);
  }
#else
  void set_tracker(esp32_ble_tracker::ESP32BLETracker* tracker) { tracker_ = tracker; }
#endif
  void set_web_auth(openquatt_web_auth::OpenQuattWebAuth* auth) { auth_ = auth; }
  openquatt_web_auth::OpenQuattWebAuth* web_auth() const { return auth_; }
  bool snapshot(BleSnapshot& out);
  Submit submit_config(const BleConfig& candidate, uint32_t expected_revision);
  Submit submit_scan(bool active, uint32_t expected_revision);

 private:
  friend float room_temperature(uint32_t now_ms);
  friend uint32_t room_generation();
  friend DewPointInput dew_point(uint32_t now_ms);
  enum class Command { NONE, CONFIG, START, STOP };
  struct State {
    BleConfig config{}, candidate{};
    Measurement paired[CAPACITY]{}, discovered[CAPACITY]{};
    uint32_t recovery_epoch{}, search_start{}, room_generation{};
    uint32_t last_radio_start_attempt{};
    size_t discoveries{};
    Command command{};
    bool searching{}, ready{}, driver_failed{}, storage_error{}, storage_unknown{true};
    bool radio_start_pending{};
  };
  void register_http_handlers();
  void observe_(const Advertisement& adv);
  void apply_(uint32_t now);
  void radio_(bool enabled);
  bool ready_() const;
  bool radio_failed_() const;
  Submit admission_(uint32_t expected);
  bool observed_(const BleConfig& candidate, uint32_t now) const;
  openquatt_common::PsramObjectArray<State, 1> state_;
  std::mutex lock_;
#ifdef USE_OPENQUATT_BLE_NIMBLE
  NimbleScanner scanner_;
#else
  esp32_ble_tracker::ESP32BLETracker* tracker_{};
#endif
  openquatt_web_auth::OpenQuattWebAuth* auth_{};
};
float room_temperature(uint32_t now_ms);
uint32_t room_generation();
DewPointInput dew_point(uint32_t now_ms);
}  // namespace esphome::openquatt_ble
