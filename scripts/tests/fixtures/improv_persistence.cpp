#include <array>
#include <cstdio>
#include <functional>
#include <span>
#define USE_WIFI
#define ESP_LOGV(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
#define YESNO(x) ((x) ? "YES" : "NO")
namespace improv {
enum State { STATE_STOPPED, STATE_AUTHORIZED, STATE_PROVISIONING, STATE_PROVISIONED };
enum Error { ERROR_NONE, ERROR_UNABLE_TO_CONNECT, ERROR_UNKNOWN_RPC };
enum Command { WIFI_SETTINGS, GET_CURRENT_STATE, GET_DEVICE_INFO, GET_WIFI_NETWORKS, GET_NETWORK_STATE };
constexpr uint8_t NETWORK_IS_ONLINE = 1, NETWORK_SUPPORTS_WIFI = 2, NETWORK_SUPPORTS_MODEM = 16;
constexpr size_t RPC_RESPONSE_MAX_SIZE = 255;
struct ImprovCommand {
  Command command;
  std::string ssid, password;
};
struct RpcResponseBuilder {
  RpcResponseBuilder(std::array<uint8_t, RPC_RESPONSE_MAX_SIZE>&, Command) {}
  void add_string(const char*, size_t = 0) {}
  std::span<const uint8_t> finish(bool) { return {}; }
};
}  // namespace improv
namespace esphome {
char* int8_to_str(char* buffer, int8_t value) { return buffer + snprintf(buffer, 5, "%d", value); }
namespace network {
bool is_connected() { return true; }
}  // namespace network
namespace wifi {
WiFiComponent* global_wifi_component = nullptr;
bool should_show_scan_entry(const std::vector<WiFiScanResult>&, const WiFiScanResult&, bool&) { return true; }
}  // namespace wifi
namespace improv_serial {
constexpr uint32_t IMPROV_SERIAL_TIMEOUT = 100, WIFI_SWITCH_TIMEOUT_MS = 90000, WIFI_CONNECT_TIMEOUT_MS = 30000;
class ImprovSerialComponent {
 public:
  uint32_t last_read_byte_{0}, provisioning_generation_{0};
  std::vector<uint8_t> rx_buffer_;
  improv::State state_{improv::STATE_AUTHORIZED};
  int responses = 0;
  improv::Error error = improv::ERROR_NONE;
  std::vector<improv::State> states;
  std::function<void()> timeout;
  std::optional<uint8_t> read_byte_() { return {}; }
  bool parse_improv_serial_byte_(uint8_t) { return true; }
  void set_timeout(const char*, uint32_t, std::function<void()> value) { timeout = value; }
  void cancel_timeout(const char*) { timeout = {}; }
  void send_current_state_(improv::State state) { states.push_back(state); }
  void set_error_(improv::Error value) { error = value; }
  void send_settings_response_(improv::Command) { ++responses; }
  void send_version_info_() {}
  void send_response_(std::span<const uint8_t>) {}
  void loop();
  void on_wifi_connect_timeout_();
  bool parse_improv_payload_(improv::ImprovCommand& command);
  void set_state_(improv::State state);
};
// PRODUCTION_IMPROV_METHODS
}  // namespace improv_serial
}  // namespace esphome

int main() {
  using namespace esphome;
  using namespace esphome::wifi;
  using namespace esphome::improv_serial;
  WiFiComponent fresh;
  fresh.init_preferences_();
  global_wifi_component = &fresh;
  ImprovSerialComponent serial;
  improv::ImprovCommand command{improv::WIFI_SETTINGS, "new", "secret"};
  for (bool invalid_ssid : {true, false}) {
    auto invalid = command;
    (invalid_ssid ? invalid.ssid : invalid.password) = std::string("new\0suffix", 10);
    assert(serial.parse_improv_payload_(invalid));
    assert(serial.error == improv::ERROR_UNABLE_TO_CONNECT && serial.state_ == improv::STATE_AUTHORIZED);
    assert(serial.responses == 0 && !serial.timeout && !fresh.pending_credentials_);
    assert(fresh.get_provisioning_status().generation == 0 && disk.empty() && cache.empty() && syncs == 0);
  }
  assert(serial.parse_improv_payload_(command));
  assert(serial.state_ == improv::STATE_PROVISIONING && serial.responses == 0);
  fresh.connected_ = true;
  fresh.connected_ssid = "new";  // stale old evidence
  fresh.persist_pending_credentials_();
  serial.loop();
  assert(serial.state_ == improv::STATE_PROVISIONING && serial.responses == 0);
  fresh.acknowledge_stop();
  commit_ok = false;  // connection works, but persistence fails after the blob write
  fresh.connect_candidate();
  serial.loop();
  assert(serial.state_ == improv::STATE_AUTHORIZED && serial.responses == 0);
  assert(serial.error == improv::ERROR_UNABLE_TO_CONNECT && fresh.provisioning_required_);
  assert(!serial.timeout && fresh.ap_enabled);
  commit_ok = true;
  serial.parse_improv_payload_(command);
  fresh.acknowledge_stop();
  fresh.connect_candidate();
  serial.loop();
  assert(serial.state_ == improv::STATE_PROVISIONED && serial.responses == 1 && !serial.timeout);

  // Replace existing credentials on the same SSID: start a fresh connection;
  // timeout/cancel leaves proven flash intact and restores its RAM pair.
  ImprovSerialComponent change;
  command.password = "wrong";
  change.parse_improv_payload_(command);
  fresh.connected_ = true;
  fresh.connected_ssid = "new";
  fresh.persist_pending_credentials_();
  change.loop();
  assert(change.state_ == improv::STATE_PROVISIONING && change.responses == 0);
  fresh.acknowledge_stop();
  auto timeout = change.timeout;
  timeout();
  fresh.acknowledge_stop();
  SavedWifiSettings proven{};
  assert(fresh.pref_.load(&proven) && strcmp(proven.password, "secret") == 0);
  assert(fresh.sta_[0].password == "secret" && !fresh.pending_credentials_);
  assert(change.state_ == improv::STATE_AUTHORIZED && change.responses == 0);

  // A portal submission supersedes serial generation; never answer success
  // for the stale serial transaction, even if the portal candidate later saves.
  change.parse_improv_payload_(command);
  auto serial_generation = change.provisioning_generation_;
  auto portal_generation = fresh.begin_wifi_provisioning("portal", "candidate");
  assert(serial_generation != portal_generation);
  change.loop();
  assert(change.state_ == improv::STATE_AUTHORIZED && change.responses == 0 && !change.timeout);
  fresh.acknowledge_stop();
  fresh.connect_candidate();
  change.loop();
  assert(change.responses == 0);
}
