#pragma once
#include <cstring>
#include <cstdint>
#include <vector>
namespace esphome {
inline std::vector<uint8_t> test_saved;
inline bool test_save_ok = true;
inline bool test_sync_ok = true;
class ESPPreferenceObject {
 public:
  template <class T>
  bool load(T* value) {
    if (test_saved.size() != sizeof(T)) return false;
    std::memcpy(value, test_saved.data(), sizeof(T));
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
  template <class T>
  ESPPreferenceObject make_preference(uint32_t, bool) {
    return {};
  }
  bool sync() { return test_sync_ok; }
};
inline ESPPreferences test_preferences;
inline ESPPreferences* global_preferences = &test_preferences;
}  // namespace esphome
