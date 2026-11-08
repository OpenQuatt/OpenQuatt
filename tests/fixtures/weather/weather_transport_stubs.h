#pragma once

#include "weather_runtime_stubs.h"

#include <cstdio>
#include <deque>
#include <sys/types.h>

using err_t = int;
inline constexpr err_t ERR_OK = 0, ERR_INPROGRESS = -5, ERR_MEM = -1, ERR_VAL = -6;
inline constexpr uint8_t LWIP_DNS_ADDRTYPE_IPV4 = 0;
using dns_found_callback = void (*)(const char*, const ip_addr_t*, void*);

struct ip4_addr_t {
  uint32_t addr = 0;
};
struct esp_tls_t {
  unsigned id = 0;
};
struct esp_tls_cfg_t {
  bool non_block = false, skip_common_name = false;
  int timeout_ms = 0;
  const char* common_name = nullptr;
  int (*crt_bundle_attach)(void*) = nullptr;
};
inline constexpr ssize_t ESP_TLS_ERR_SSL_WANT_READ = -0x6900, ESP_TLS_ERR_SSL_WANT_WRITE = -0x6880;
inline int esp_crt_bundle_attach(void*) { return 0; }

namespace weather_transport_fixture {
enum class DnsMode { CACHED, ASYNC, ERROR };
struct TransportEnvironment {
  DnsMode dns_mode = DnsMode::CACHED;
  bool queue_fails = false, defer_dns_start = false, reply_is_v4 = true;
  uint32_t dns_answer = 0xc000020aU;
  void (*queued_start)(void*) = nullptr;
  void* queued_start_argument = nullptr;
  dns_found_callback pending_found = nullptr;
  const char* pending_name = nullptr;
  void* pending_argument = nullptr;
  unsigned queue_calls = 0, dns_calls = 0;
  std::vector<std::string> lookup_names;
  std::function<void()> on_delay;

  bool allocation_fails = false, connect_fails = false, write_fails = false, zero_write = false, read_fails = false;
  unsigned connect_pending_calls = 0;
  uint64_t connect_step_ms = 0;
  std::deque<ssize_t> write_wants, read_wants;
  size_t write_chunk = 768, read_chunk = 512, fast_prefix = 0;
  uint64_t byte_interval_ms = 0, next_byte_ms = 0;
  std::string wire, request_bytes;
  size_t read_offset = 0, supplied_bytes = 0;
  unsigned init_calls = 0, connect_calls = 0, write_calls = 0, read_calls = 0, destroy_calls = 0, delay_calls = 0;
  esp_tls_t* active_tls = nullptr;
  std::vector<std::string> connected_addresses, certificate_names;
};
inline TransportEnvironment tx;

inline void advance(uint64_t milliseconds) {
  weather_fixture::env.now_ms += milliseconds;
  ++tx.delay_calls;
  if (tx.on_delay) tx.on_delay();
}

inline void start_queued_dns() {
  assert(tx.queued_start != nullptr);
  const auto function = std::exchange(tx.queued_start, nullptr);
  void* argument = std::exchange(tx.queued_start_argument, nullptr);
  function(argument);
}

inline void complete_dns(bool found = true) {
  assert(tx.pending_found != nullptr);
  const auto callback = std::exchange(tx.pending_found, nullptr);
  const char* name = std::exchange(tx.pending_name, nullptr);
  void* argument = std::exchange(tx.pending_argument, nullptr);
  ip_addr_t address{tx.dns_answer};
  callback(name, found ? &address : nullptr, argument);
}
}  // namespace weather_transport_fixture

#define pdMS_TO_TICKS(value) static_cast<TickType_t>(value)
inline void vTaskDelay(TickType_t ticks) { weather_transport_fixture::advance(ticks); }

#define IP_IS_V4(value) (::weather_transport_fixture::tx.reply_is_v4)
inline const ip4_addr_t* ip_2_ip4(const ip_addr_t* address) {
  static ip4_addr_t value;
  value.addr = address->address;
  return &value;
}
inline char* ip4addr_ntoa_r(const ip4_addr_t* address, char* output, size_t capacity) {
  const int length =
      std::snprintf(output, capacity, "%u.%u.%u.%u", static_cast<unsigned>(address->addr >> 24),
                    static_cast<unsigned>((address->addr >> 16) & 255),
                    static_cast<unsigned>((address->addr >> 8) & 255), static_cast<unsigned>(address->addr & 255));
  return length >= 0 && static_cast<size_t>(length) < capacity ? output : nullptr;
}

inline err_t tcpip_try_callback(void (*callback)(void*), void* argument) {
  auto& tx = weather_transport_fixture::tx;
  ++tx.queue_calls;
  if (tx.queue_fails) return ERR_MEM;
  if (tx.defer_dns_start) {
    assert(tx.queued_start == nullptr);
    tx.queued_start = callback;
    tx.queued_start_argument = argument;
  } else {
    callback(argument);
  }
  return ERR_OK;
}
inline err_t dns_gethostbyname_addrtype(const char* hostname, ip_addr_t* address, dns_found_callback callback,
                                        void* argument, uint8_t family) {
  auto& tx = weather_transport_fixture::tx;
  assert(family == LWIP_DNS_ADDRTYPE_IPV4);
  ++tx.dns_calls;
  tx.lookup_names.emplace_back(hostname);
  if (tx.dns_mode == weather_transport_fixture::DnsMode::ERROR) return ERR_VAL;
  if (tx.dns_mode == weather_transport_fixture::DnsMode::CACHED) {
    address->address = tx.dns_answer;
    return ERR_OK;
  }
  assert(tx.pending_found == nullptr);
  tx.pending_found = callback;
  tx.pending_name = hostname;
  tx.pending_argument = argument;
  return ERR_INPROGRESS;
}

inline esp_tls_t* esp_tls_init() {
  auto& tx = weather_transport_fixture::tx;
  ++tx.init_calls;
  assert(tx.active_tls == nullptr);
  if (tx.allocation_fails) return nullptr;
  tx.active_tls = new esp_tls_t{tx.init_calls};
  tx.read_offset = tx.supplied_bytes = 0;
  tx.request_bytes.clear();
  tx.next_byte_ms = weather_fixture::env.now_ms + tx.byte_interval_ms;
  return tx.active_tls;
}
inline int esp_tls_conn_new_async(const char* address, size_t length, int port, const esp_tls_cfg_t* config,
                                  esp_tls_t* tls) {
  auto& tx = weather_transport_fixture::tx;
  assert(tls != nullptr && tls == tx.active_tls && port == 443);
  assert(std::strlen(address) == length);
  assert(config->non_block && !config->skip_common_name && config->timeout_ms <= 50);
  assert(config->common_name != nullptr && config->crt_bundle_attach == esp_crt_bundle_attach);
  ++tx.connect_calls;
  tx.connected_addresses.emplace_back(address, length);
  tx.certificate_names.emplace_back(config->common_name);
  if (tx.connect_step_ms) weather_transport_fixture::advance(tx.connect_step_ms);
  if (tx.connect_fails) return -1;
  if (tx.connect_pending_calls) {
    --tx.connect_pending_calls;
    return 0;
  }
  return 1;
}
inline ssize_t esp_tls_conn_write(esp_tls_t* tls, const void* bytes, size_t size) {
  auto& tx = weather_transport_fixture::tx;
  assert(tls != nullptr && tls == tx.active_tls);
  ++tx.write_calls;
  if (tx.write_fails) return -1;
  if (tx.zero_write) return 0;
  if (!tx.write_wants.empty()) {
    const auto result = tx.write_wants.front();
    tx.write_wants.pop_front();
    return result;
  }
  const size_t count = std::min(size, tx.write_chunk);
  tx.request_bytes.append(static_cast<const char*>(bytes), count);
  return static_cast<ssize_t>(count);
}
inline ssize_t esp_tls_conn_read(esp_tls_t* tls, void* bytes, size_t capacity) {
  auto& tx = weather_transport_fixture::tx;
  assert(tls != nullptr && tls == tx.active_tls);
  ++tx.read_calls;
  if (tx.read_fails) return -1;
  if (!tx.read_wants.empty()) {
    const auto result = tx.read_wants.front();
    tx.read_wants.pop_front();
    return result;
  }
  if (tx.read_offset == tx.wire.size()) return 0;
  const bool prefix = tx.read_offset < tx.fast_prefix;
  if (!prefix && weather_fixture::env.now_ms < tx.next_byte_ms) return ESP_TLS_ERR_SSL_WANT_READ;
  size_t count = std::min(capacity, std::min(tx.read_chunk, tx.wire.size() - tx.read_offset));
  if (prefix) count = std::min(count, tx.fast_prefix - tx.read_offset);
  std::memcpy(bytes, tx.wire.data() + tx.read_offset, count);
  tx.read_offset += count;
  tx.supplied_bytes += count;
  if (!prefix) tx.next_byte_ms = weather_fixture::env.now_ms + tx.byte_interval_ms;
  return static_cast<ssize_t>(count);
}
inline int esp_tls_conn_destroy(esp_tls_t* tls) {
  auto& tx = weather_transport_fixture::tx;
  assert(tls != nullptr && tls == tx.active_tls);
  ++tx.destroy_calls;
  tx.active_tls = nullptr;
  delete tls;
  return 0;
}
