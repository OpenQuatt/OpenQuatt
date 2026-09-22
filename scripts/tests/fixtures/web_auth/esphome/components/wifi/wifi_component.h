#pragma once

namespace esphome::wifi {
inline bool test_clear_ok = true;
inline int test_clears = 0;
class WiFiComponent {
 public:
  bool clear_saved_sta_checked() {
    if (!test_clear_ok) return false;
    ++test_clears;
    return true;
  }
};
inline WiFiComponent component;
inline WiFiComponent* global_wifi_component = &component;
}  // namespace esphome::wifi
