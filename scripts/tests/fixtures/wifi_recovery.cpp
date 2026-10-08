#include <atomic>
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
uint32_t fail_save = 0, fail_load = 0, failed_write_key = 0;
bool commit_ok = true, corrupt_load = false;
int binds = 0, syncs = 0;
uint32_t clock_now = 100;
uint32_t millis() { return clock_now; }
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
    bool failed = false;
    // Real ESP32 semantics: process other writes, clear the cache even on a
    // failed set, and nvs_commit failure does not roll back successful blobs.
    for (const auto& [key, value] : cache) {
      if (key.first == failed_write_key)
        failed = true;
      else
        disk[key] = value;
    }
    cache.clear();
    return !failed && commit_ok;
  }
} preferences;
auto* global_preferences = &preferences;
struct Application {
  uint32_t get_config_version_hash() { return 12345; }
  uint32_t get_loop_component_start_time() { return 100; }
} App;
struct AsyncWebServerRequest;
namespace captive_portal {
struct Portal {
  bool active = true;
  void end() { active = false; }
  bool is_active() const { return active; }
  void handleRequest(AsyncWebServerRequest* request);
} portal;
auto* global_captive_portal = &portal;
}  // namespace captive_portal
namespace wifi {
constexpr size_t SSID_BUFFER_SIZE = 33;
constexpr int WIFI_COMPONENT_STATE_OFF = 0;
enum wifi_mode_t { WIFI_MODE_NULL, WIFI_MODE_STA, WIFI_MODE_AP, WIFI_MODE_APSTA };
constexpr int ESP_OK = 0;
bool s_wifi_started = true;
wifi_mode_t driver_mode = WIFI_MODE_APSTA;
bool mode_ok = true, query_ok = true;
int esp_wifi_get_mode(wifi_mode_t* value) {
  *value = driver_mode;
  return query_ok ? ESP_OK : -1;
}
// PRODUCTION_RECORDS
class WiFiAP {
 public:
  std::string ssid, password;
  void set_ssid(const char* value) { ssid = value; }
  void set_ssid(const std::string& value) { ssid = value; }
  void set_password(const char* value) { password = value; }
  void set_password(const std::string& value) { password = value; }
  const std::string& get_ssid() const { return ssid; }
  const std::string& get_password() const { return password; }
};
struct WiFiScanResult {
  std::string get_ssid() const { return "unused"; }
  int8_t get_rssi() const { return -40; }
};
class WiFiComponent {
 public:
  // PRODUCTION_STATUS
  bool preferences_initialized_{false}, provisioning_required_{false}, pending_credentials_{false};
  bool credential_reset_started_{false};
  bool provisioning_stopping_{false}, provisioning_station_stopped_{false}, connected_{false};
  uint32_t provisioning_stop_started_{0};
  std::atomic<uint32_t> provisioning_status_{0};
  int state_{0};
  ESPPreferenceObject pref_, fast_connect_pref_;
  std::vector<WiFiAP> sta_;
  std::string connected_ssid;
  bool ap_enabled = true, ap_setup_ = true;
  int connect_attempts = 0;
  bool has_sta() { return !sta_.empty(); }
  WiFiAP get_sta() { return sta_.empty() ? WiFiAP{} : sta_[0]; }
  void disable() {
    abort_pending_credentials_();
    connected_ = false;
  }
  void enable() {}
  bool has_ap() { return true; }
  bool is_disabled() { return false; }
  std::vector<WiFiScanResult> scans;
  const std::vector<WiFiScanResult>& get_scan_result() { return scans; }
  void set_sta(WiFiAP value) { sta_ = {value}; }
  void clear_sta() {
    if (pending_credentials_) abort_pending_credentials_();
    sta_.clear();
  }
  bool is_connected() { return connected_; }
  void start_connecting(const WiFiAP&) {
    ++connect_attempts;
    connected_ = false;
    connected_ssid.clear();
    driver_mode = WIFI_MODE_APSTA;
  }
  void update_connected_state_() {}
  const char* wifi_ssid_to(char*) { return connected_ssid.c_str(); }
  bool is_captive_portal_active_() { return captive_portal::portal.active; }
  bool wifi_mode_(std::optional<bool> sta, std::optional<bool> ap) {
    if (!mode_ok) return false;
    if (sta.has_value() && !*sta) driver_mode = ap_enabled ? WIFI_MODE_AP : WIFI_MODE_NULL;
    if (ap.has_value()) ap_enabled = *ap;
    return true;
  }
  void init_preferences_();
  bool clear_saved_sta_checked();
  void save_wifi_sta(const char* ssid, const char* password);
  void persist_pending_credentials_();
  void start_pending_credentials_();
  bool poll_pending_credentials_(uint32_t now);
  void abort_pending_credentials_();
  bool stop_sta_for_provisioning_();
  void stop_provisioning_ap_();
  void acknowledge_stop() {
    // WIFI_EVENT_STA_STOP clears IP/connection evidence in the real handler.
    connected_ = false;
    connected_ssid.clear();
    provisioning_station_stopped_ = true;
    start_pending_credentials_();
  }
  void connect_candidate() {
    assert(!provisioning_stopping_ && !sta_.empty());
    connected_ssid = sta_[0].ssid;
    connected_ = true;
    persist_pending_credentials_();
  }
};
// PRODUCTION_METHODS
}  // namespace wifi
}  // namespace esphome

int main() {
  using namespace esphome;
  using namespace esphome::wifi;
  using State = WiFiComponent::WiFiProvisioningState;
  static_assert(sizeof(SavedWifiSettings) == 98);
  static_assert(sizeof(SavedWifiFastConnectSettings) == 8);
  auto reset_faults = [] {
    fail_save = fail_load = failed_write_key = 0;
    commit_ok = mode_ok = query_ok = true;
    corrupt_load = false;
    driver_mode = WIFI_MODE_APSTA;
    captive_portal::portal.active = true;
  };
  auto seed = [](WiFiComponent& component, const char* ssid, const char* password) {
    component.init_preferences_();
    SavedWifiSettings old{};
    strncpy(old.ssid, ssid, sizeof(old.ssid) - 1);
    strncpy(old.password, password, sizeof(old.password) - 1);
    component.pref_.save(&old);
    preferences.sync();
    WiFiAP ap;
    ap.set_ssid(ssid);
    ap.set_password(password);
    component.set_sta(ap);
    component.provisioning_required_ = false;
    component.connected_ = true;
    component.connected_ssid = ssid;
  };
  WiFiComponent blank;
  blank.init_preferences_();
  assert(blank.provisioning_required_);
  auto first = blank.begin_wifi_provisioning("wrong", "password");
  assert(blank.pending_credentials_ && disk.empty() && cache.empty());
  // Old connected/IP evidence must not be accepted before confirmed STOP.
  blank.connected_ = true;
  blank.connected_ssid = "wrong";
  blank.persist_pending_credentials_();
  assert(blank.pending_credentials_ && disk.empty());
  blank.acknowledge_stop();
  assert(!blank.connected_ && blank.connect_attempts == 1);
  blank.cancel_wifi_provisioning(first);
  blank.acknowledge_stop();
  assert(!blank.has_sta() && disk.empty());
  WiFiComponent reboot;
  reboot.init_preferences_();
  assert(reboot.provisioning_required_);
  const int previous_binds = binds;
  blank.init_preferences_();
  assert(binds == previous_binds && blank.pref_.key.first == 88491487);

  // Fresh and already-configured devices use the identical checked path.
  for (bool existing : {false, true}) {
    for (int failure = 0; failure < 5; ++failure) {
      disk.clear();
      cache.clear();
      reset_faults();
      WiFiComponent candidate;
      if (existing)
        seed(candidate, "previous", "proven");
      else
        candidate.init_preferences_();
      candidate.begin_wifi_provisioning("valid", "secret");
      assert(candidate.provisioning_required_ && candidate.get_provisioning_status().state == State::CONNECTING);
      candidate.acknowledge_stop();
      fail_save = failure == 0 ? 88491487 : 0;
      failed_write_key = failure == 1 ? 88491487 : 0;
      commit_ok = failure != 2;
      fail_load = failure == 3 ? 88491487 : 0;
      corrupt_load = failure == 4;
      candidate.connect_candidate();
      assert(candidate.provisioning_required_ && !candidate.pending_credentials_);
      assert(candidate.get_provisioning_status().state == State::STORAGE_FAILED);
      assert(candidate.ap_enabled && captive_portal::portal.active);
      candidate.stop_provisioning_ap_();
      assert(candidate.ap_enabled);
      const int previous_syncs = syncs;
      candidate.persist_pending_credentials_();
      assert(syncs == previous_syncs);  // no automatic persistence retry
      // A failed commit can still leave the new proven pair on flash. Never
      // promise rollback or turn this ambiguous result into success.
      if (failure == 2) {
        SavedWifiSettings after{};
        assert(candidate.pref_.load(&after) && strcmp(after.ssid, "valid") == 0);
      }
      reset_faults();
      candidate.begin_wifi_provisioning("valid", "secret");
      candidate.acknowledge_stop();
      candidate.connect_candidate();
      assert(!candidate.provisioning_required_ && candidate.get_provisioning_status().state == State::SAVED);
      assert(!candidate.ap_enabled && !candidate.ap_setup_ && !captive_portal::portal.active);
    }
  }

  disk.clear();
  cache.clear();
  reset_faults();
  WiFiComponent replaced;
  seed(replaced, "same-ssid", "correct");
  auto stale = replaced.begin_wifi_provisioning("same-ssid", "wrong");
  // A late old IP event with the same SSID does not prove the changed password.
  replaced.connected_ = true;
  replaced.connected_ssid = "same-ssid";
  replaced.persist_pending_credentials_();
  SavedWifiSettings still_proven{};
  assert(replaced.pref_.load(&still_proven) && strcmp(still_proven.password, "correct") == 0);
  replaced.acknowledge_stop();
  replaced.persist_pending_credentials_();
  assert(replaced.pending_credentials_ && replaced.connect_attempts == 1);
  auto current = replaced.begin_wifi_provisioning("new-ssid", "new-password");
  replaced.cancel_wifi_provisioning(stale);
  assert(replaced.pending_credentials_ && replaced.get_provisioning_status().generation == current);
  replaced.acknowledge_stop();
  replaced.connected_ = true;
  replaced.connected_ssid = "same-ssid";
  replaced.persist_pending_credentials_();
  assert(replaced.pending_credentials_);
  replaced.cancel_wifi_provisioning(current);
  assert(replaced.get_provisioning_status().state == State::CANCELLED);
  replaced.acknowledge_stop();
  assert(replaced.sta_[0].password == "correct" && !replaced.pending_credentials_);
  replaced.connect_candidate();
  assert(replaced.pref_.load(&still_proven) && strcmp(still_proven.password, "correct") == 0);
  auto successful = replaced.begin_wifi_provisioning("same-ssid", "new-correct");
  replaced.acknowledge_stop();
  replaced.connect_candidate();
  assert(replaced.get_provisioning_status().generation == successful &&
         replaced.get_provisioning_status().state == State::SAVED);

  for (int failure = 0; failure < 3; ++failure) {
    reset_faults();
    WiFiComponent fenced;
    fenced.init_preferences_();
    query_ok = failure != 0;
    mode_ok = failure != 1;
    fenced.begin_wifi_provisioning("same", "wrong");
    if (failure < 2) {
      assert(fenced.get_provisioning_status().state == State::CONNECT_FAILED);
    } else {
      fenced.connected_ = true;
      fenced.connected_ssid = "same";
      fenced.start_pending_credentials_();  // STOP missing: no connect or save.
      fenced.persist_pending_credentials_();
      assert(fenced.connect_attempts == 0 && fenced.pending_credentials_);
      fenced.poll_pending_credentials_(3200);
      assert(!fenced.pending_credentials_ && fenced.get_provisioning_status().state == State::CONNECT_FAILED);
    }
    assert(fenced.ap_enabled);
  }

  reset_faults();
  WiFiComponent transition;
  transition.init_preferences_();
  transition.begin_wifi_provisioning("candidate", "secret");
  transition.abort_pending_credentials_();  // disable/start hook aborts before reload
  transition.connected_ = true;
  transition.connected_ssid = "candidate";
  const int before_abort = syncs;
  transition.persist_pending_credentials_();
  assert(syncs == before_abort && transition.get_provisioning_status().state == State::CANCELLED);

  // Checked clear covers both native slots and realistic partial-write outcomes.
  for (int failure = 0; failure < 8; ++failure) {
    disk.clear();
    cache.clear();
    reset_faults();
    WiFiComponent saved;
    seed(saved, "valid", "secret");
    SavedWifiFastConnectSettings fast{{1, 2, 3, 4, 5, 6}, 11, 1};
    saved.fast_connect_pref_.save(&fast);
    preferences.sync();
    fail_save = failure == 0 ? 88491487 : failure == 1 ? 88491488 : 0;
    failed_write_key = failure == 2 ? 88491487 : failure == 3 ? 88491488 : 0;
    commit_ok = failure != 4;
    fail_load = failure == 5 ? 88491487 : failure == 6 ? 88491488 : 0;
    assert(saved.clear_saved_sta_checked() == (failure == 7));
    assert(saved.sta_[0].ssid == "valid");
    const int before_rejected_save = syncs;
    saved.begin_wifi_provisioning("must-not-return", "during-teardown");
    saved.persist_pending_credentials_();
    assert(syncs == before_rejected_save && !saved.pending_credentials_);
    if (failure == 2 || failure == 3 || failure == 4) assert(cache.empty());
    reset_faults();
    assert(saved.clear_saved_sta_checked());
    SavedWifiSettings empty{}, read{};
    SavedWifiFastConnectSettings empty_fast{}, read_fast{};
    assert(saved.pref_.load(&read) && memcmp(&read, &empty, sizeof(read)) == 0);
    assert(saved.fast_connect_pref_.load(&read_fast) && memcmp(&read_fast, &empty_fast, sizeof(read_fast)) == 0);
    cache.clear();  // cold boot after checked clear
    WiFiComponent after_reset;
    after_reset.init_preferences_();
    assert(after_reset.provisioning_required_);
  }
}
