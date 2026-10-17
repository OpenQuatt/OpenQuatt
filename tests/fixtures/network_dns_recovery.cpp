#include <cassert>
#include <cstdint>
#include <initializer_list>

#define CONFIG_ESP_NETIF_SET_DNS_PER_DEFAULT_NETIF 1
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_IP_IS_ANY(ip) ((ip).address == 0)
using esp_err_t = int;
constexpr int ESP_OK = 0;
enum esp_netif_dns_type_t { ESP_NETIF_DNS_MAIN, ESP_NETIF_DNS_BACKUP, ESP_NETIF_DNS_FALLBACK };
struct Address {
  unsigned address;
};
struct esp_netif_dns_info_t {
  Address ip;
};
struct Interface {
  bool up{true};
  unsigned dns[3]{11, 22, 33};
} wifi, ethernet;
Interface* selected = &wifi;
void* netif_default = &wifi;
unsigned global_dns[3]{11, 22, 33};
bool tcpip_context = false;
int calls = 0, writes = 0, fail_read = 0, fail_write = 0;
bool fail_exec = false, fail_backup = false;
void (*interleaving)() = nullptr;
Interface* esp_netif_get_default_netif() {
  assert(tcpip_context);
  return selected;
}
bool esp_netif_is_netif_up(Interface* iface) {
  assert(tcpip_context);
  return iface->up;
}
void* esp_netif_get_netif_impl(Interface* iface) {
  assert(tcpip_context);
  return iface;
}
esp_err_t esp_netif_get_dns_info(Interface* iface, esp_netif_dns_type_t type, esp_netif_dns_info_t* out) {
  assert(tcpip_context);
  if (fail_read) return 1;
  out->ip.address = iface ? iface->dns[type] : global_dns[type];
  return ESP_OK;
}
esp_err_t esp_netif_set_dns_info(Interface* iface, esp_netif_dns_type_t type, esp_netif_dns_info_t* value) {
  assert(tcpip_context && iface == nullptr && value->ip.address != 0);
  if (fail_write || (fail_backup && type == ESP_NETIF_DNS_BACKUP)) return 1;
  ++writes;
  global_dns[type] = value->ip.address;
  return ESP_OK;
}
esp_err_t esp_netif_tcpip_exec(esp_err_t (*callback)(void*), void* context) {
  ++calls;
  if (fail_exec) return 1;
  // A DHCP renewal / route switch queued before this operation completes first.
  if (interleaving) {
    interleaving();
    interleaving = nullptr;
  }
  tcpip_context = true;
  const auto result = callback(context);
  tcpip_context = false;
  return result;
}
class OpenQuattNetworkManager {
 public:
  uint32_t last_dns_check_ms_{0};
  void restore_default_dns_(uint32_t now);
};
// PRODUCTION_METHOD
int main() {
  OpenQuattNetworkManager manager;
  manager.restore_default_dns_(999);
  assert(calls == 0);
  // Healthy ordering: Ethernet DHCP precedes WiFi's default/DNS installation.
  manager.restore_default_dns_(1000);
  assert(writes == 0);
  // Failing ordering: Ethernet DHCP clears main/backup after WiFi is default.
  global_dns[0] = global_dns[1] = 0;
  manager.restore_default_dns_(2000);
  assert(global_dns[0] == 11 && global_dns[1] == 22 && global_dns[2] == 33 && writes == 2);
  manager.restore_default_dns_(3000);
  assert(writes == 2);
  // Preserve valid global values and fallback even if cached entries differ.
  global_dns[0] = 99;
  global_dns[1] = 0;
  global_dns[2] = 88;
  manager.restore_default_dns_(4000);
  assert(global_dns[0] == 99 && global_dns[1] == 22 && global_dns[2] == 88);
  // Renewed cached DNS must be used, never a snapshot from a prior iteration.
  global_dns[0] = 0;
  interleaving = [] { wifi.dns[0] = 44; };
  manager.restore_default_dns_(5000);
  assert(global_dns[0] == 44);
  // A failover before callback execution must use the newly selected interface.
  global_dns[0] = 0;
  interleaving = [] {
    selected = &ethernet;
    netif_default = &ethernet;
    ethernet.dns[0] = 55;
  };
  manager.restore_default_dns_(6000);
  assert(global_dns[0] == 55);
  global_dns[0] = 0;
  ethernet.up = false;
  manager.restore_default_dns_(7000);
  assert(global_dns[0] == 0);
  ethernet.up = true;
  netif_default = &wifi;
  manager.restore_default_dns_(8000);
  assert(global_dns[0] == 0);
  selected = nullptr;
  manager.restore_default_dns_(9000);
  assert(global_dns[0] == 0);
  selected = &wifi;
  wifi.dns[0] = 0;
  manager.restore_default_dns_(10000);
  assert(global_dns[0] == 0);
  wifi.dns[0] = 66;
  fail_read = 1;
  manager.restore_default_dns_(11000);
  assert(global_dns[0] == 0);
  fail_read = 0;
  fail_write = 1;
  manager.restore_default_dns_(12000);
  assert(global_dns[0] == 0);
  fail_write = 0;
  manager.restore_default_dns_(13000);
  assert(global_dns[0] == 66);
  // Unsigned elapsed time remains valid across millis() wrap.
  manager.last_dns_check_ms_ = UINT32_MAX - 500;
  global_dns[0] = 0;
  manager.restore_default_dns_(499);
  assert(global_dns[0] == 66);
  // Partial success persists, and the missing backup is retried next time.
  manager.last_dns_check_ms_ = 0;
  global_dns[0] = global_dns[1] = 0;
  fail_backup = true;
  manager.restore_default_dns_(1000);
  assert(global_dns[0] == 66 && global_dns[1] == 0);
  fail_backup = false;
  manager.restore_default_dns_(2000);
  assert(global_dns[0] == 66 && global_dns[1] == 22);
  // An execution failure does not publish success or mutate DNS; retry recovers.
  global_dns[0] = 0;
  fail_exec = true;
  manager.restore_default_dns_(3000);
  assert(global_dns[0] == 0);
  fail_exec = false;
  manager.restore_default_dns_(4000);
  assert(global_dns[0] == 66);
}
