#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#define PACKED __attribute__((packed))
#define USE_WIFI_AP
#define USE_CAPTIVE_PORTAL
#define ESP_LOGE(...) ((void)0)

namespace esphome {
using Key = std::pair<uint32_t, bool>;
std::map<Key, std::vector<uint8_t>> disk, cache;
uint32_t fail_save = 0, fail_load = 0;
bool sync_ok = true, corrupt_load = false;
int binds = 0, syncs = 0;
class ESPPreferenceObject {
 public:
  Key key;
  template <class T>
  bool save(const T* value) {
    if (fail_save == key.first) return false;
    auto* bytes = reinterpret_cast<const uint8_t*>(value);
    cache[key] = {bytes, bytes + sizeof(T)};
    return true;
  }
  template <class T>
  bool load(T* value) {
    if (fail_load == key.first) return false;
    const auto& store = cache.count(key) ? cache : disk;
    auto item = store.find(key);
    if (item == store.end() || item->second.size() != sizeof(T)) return false;
    memcpy(value, item->second.data(), sizeof(T));
    if (corrupt_load) reinterpret_cast<uint8_t*>(value)[0] ^= 1;
    return true;
  }
};
struct Preferences {
  template <class T>
  ESPPreferenceObject make_preference(uint32_t key, bool flash) {
    ++binds;
    return {{key, flash}};
  }
  bool sync() {
    ++syncs;
    if (!sync_ok) return false;
    for (const auto& [key, value] : cache) disk[key] = value;
    cache.clear();
    return true;
  }
} preferences;
auto* global_preferences = &preferences;
struct Application {
  uint32_t get_config_version_hash() { return 12345; }
} App;
namespace captive_portal {
struct Portal {
  bool active = true;
  void end() { active = false; }
} portal;
auto* global_captive_portal = &portal;
}  // namespace captive_portal
namespace wifi {
constexpr size_t SSID_BUFFER_SIZE = 33;
// PRODUCTION_RECORDS
class WiFiAP {
 public:
  std::string ssid, password;
  void set_ssid(const char* value) { ssid = value; }
  void set_password(const char* value) { password = value; }
  const std::string& get_ssid() const { return ssid; }
  const std::string& get_password() const { return password; }
};
class WiFiComponent {
 public:
  bool preferences_initialized_{false}, provisioning_required_{false}, pending_credentials_{false};
  ESPPreferenceObject pref_, fast_connect_pref_;
  std::vector<WiFiAP> sta_;
  std::string connected_ssid;
  bool ap_enabled = true;
  bool connected = false;
  int connect_attempts = 0;
  bool has_sta() { return !sta_.empty(); }
  bool has_ap() { return true; }
  void set_sta(WiFiAP value) { sta_ = {value}; }
  void connect_soon_() {}
  const WiFiAP* get_selected_sta_() { return sta_.empty() ? nullptr : &sta_[0]; }
  bool is_connected_() { return connected; }
  void start_connecting(const WiFiAP&) {
    ++connect_attempts;
    connected = false;
    connected_ssid.clear();
  }
  void update_connected_state_() {}
  const char* wifi_ssid_to(char*) { return connected_ssid.c_str(); }
  bool is_captive_portal_active_() { return captive_portal::portal.active; }
  void wifi_mode_(std::optional<bool>, bool ap) { ap_enabled = ap; }
  void init_preferences_();
  bool clear_saved_sta_checked();
  void save_wifi_sta(const char* ssid, const char* password);
  void persist_pending_credentials_();
  void stop_provisioning_ap_();
};
// PRODUCTION_METHODS
}  // namespace wifi
}  // namespace esphome

int main() {
  using namespace esphome;
  using namespace esphome::wifi;
  static_assert(sizeof(SavedWifiSettings) == 98);
  static_assert(sizeof(SavedWifiFastConnectSettings) == 8);
  WiFiComponent blank;
  blank.init_preferences_();
  assert(blank.provisioning_required_);
  blank.save_wifi_sta("wrong", "password");
  assert(blank.pending_credentials_ && disk.empty() && cache.empty());
  blank.connected_ssid = "old";
  blank.persist_pending_credentials_();
  assert(blank.pending_credentials_ && blank.ap_enabled && disk.empty());
  // Cold boot after incorrect input still sees an empty store.
  WiFiComponent reboot;
  reboot.init_preferences_();
  assert(reboot.provisioning_required_);
  // Binding cannot change now that runtime STA is present.
  const int previous_binds = binds;
  blank.init_preferences_();
  assert(binds == previous_binds && blank.pref_.key.first == 88491487);

  for (int failure = 0; failure < 4; ++failure) {
    disk.clear();
    cache.clear();
    WiFiComponent candidate;
    candidate.init_preferences_();
    candidate.save_wifi_sta("valid", "secret");
    candidate.connected_ssid = "valid";
    candidate.connected = true;
    fail_save = failure == 0 ? 88491487 : 0;
    sync_ok = failure != 1;
    fail_load = failure == 2 ? 88491487 : 0;
    corrupt_load = failure == 3;
    captive_portal::portal.active = true;
    candidate.persist_pending_credentials_();
    assert(candidate.provisioning_required_ && !candidate.pending_credentials_);
    assert(candidate.ap_enabled && captive_portal::portal.active);
    candidate.stop_provisioning_ap_();
    assert(candidate.ap_enabled);
    const int previous_syncs = syncs;
    candidate.persist_pending_credentials_();
    assert(syncs == previous_syncs);  // no automatic storage retries
    fail_save = fail_load = 0;
    sync_ok = true;
    corrupt_load = false;
    candidate.save_wifi_sta("valid", "secret");                      // explicit retry
    assert(candidate.connected && candidate.connect_attempts == 1);  // exact proven pair, like Improv
    candidate.persist_pending_credentials_();
    assert(!candidate.provisioning_required_);
    assert(!candidate.ap_enabled && !captive_portal::portal.active);
  }

  disk.clear();
  cache.clear();
  WiFiComponent replaced;
  replaced.init_preferences_();
  replaced.save_wifi_sta("same-ssid", "correct");
  replaced.connected_ssid = "same-ssid";
  replaced.connected = true;
  sync_ok = false;
  replaced.persist_pending_credentials_();
  sync_ok = true;
  replaced.save_wifi_sta("same-ssid", "wrong");
  assert(!replaced.connected && replaced.connect_attempts == 2);
  replaced.persist_pending_credentials_();
  assert(replaced.provisioning_required_ && replaced.pending_credentials_ && replaced.ap_enabled);
  replaced.save_wifi_sta("new-ssid", "new-password");
  assert(replaced.connect_attempts == 3);  // replace an in-flight attempt, too
  replaced.connected_ssid = "same-ssid";
  replaced.persist_pending_credentials_();
  assert(replaced.provisioning_required_ && replaced.pending_credentials_);
  replaced.connected_ssid = "new-ssid";
  replaced.persist_pending_credentials_();
  assert(!replaced.provisioning_required_);

  for (int failure = 0; failure < 7; ++failure) {
    WiFiComponent saved;
    saved.init_preferences_();
    assert(!saved.provisioning_required_);
    saved.save_wifi_sta("valid", "secret");
    SavedWifiFastConnectSettings fast{{1, 2, 3, 4, 5, 6}, 11, 1};
    saved.fast_connect_pref_.save(&fast);
    preferences.sync();
    fail_save = failure == 0 ? 88491487 : failure == 1 ? 88491488 : 0;
    sync_ok = failure != 2;
    fail_load = failure == 3 ? 88491487 : failure == 4 ? 88491488 : 0;
    corrupt_load = failure == 5;
    assert(saved.clear_saved_sta_checked() == (failure == 6));
    assert(saved.sta_[0].ssid == "valid");  // no runtime mutation before reboot
    fail_save = fail_load = 0;
    sync_ok = true;
    corrupt_load = false;
    assert(saved.clear_saved_sta_checked());
    SavedWifiSettings empty{};
    SavedWifiFastConnectSettings empty_fast{}, read_fast{};
    SavedWifiSettings read{};
    assert(saved.pref_.load(&read) && memcmp(&read, &empty, sizeof(read)) == 0);
    assert(saved.fast_connect_pref_.load(&read_fast) && memcmp(&read_fast, &empty_fast, sizeof(read_fast)) == 0);
    cache.clear();  // power cycle after checked clear
    WiFiComponent after_reset;
    after_reset.init_preferences_();
    assert(after_reset.provisioning_required_);
    after_reset.save_wifi_sta("valid", "secret");
    after_reset.connected_ssid = "valid";
    after_reset.persist_pending_credentials_();
  }
}
