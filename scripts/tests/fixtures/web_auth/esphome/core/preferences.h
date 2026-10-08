#pragma once
#include <cstring>
#include <cstdint>
#include <vector>
namespace esphome {
inline std::vector<uint8_t> test_saved;
inline bool test_save_ok = true;
inline bool test_sync_ok = true;
inline bool test_load_ok = true;
inline bool test_readback_corrupt = false;
class ESPPreferenceObject {
 public:
  template <class T>
  bool load(T* value) {
    if (!test_load_ok || test_saved.size() != sizeof(T)) return false;
    std::memcpy(value, test_saved.data(), sizeof(T));
    if (test_readback_corrupt) reinterpret_cast<uint8_t*>(value)[0] ^= 1;
    return true;
  }
  template <class T>
  bool save(const T* value) {
    if (!test_save_ok) return false;
    const auto* first = reinterpret_cast<const uint8_t*>(value);
    test_saved.assign(first, first + sizeof(T));
    return true;
  }
};
class ESPPreferences {
 public:
  uint32_t nvs_handle{1};
  template <class T>
  ESPPreferenceObject make_preference(uint32_t, bool) {
    return {};
  }
  bool sync() { return test_sync_ok; }
};
inline ESPPreferences test_preferences;
inline ESPPreferences* global_preferences = &test_preferences;
}  // namespace esphome
