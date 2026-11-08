#include "OpenQuattWeather.h"

#include <cstdio>
#include <cstring>
#include "OpenQuattWeatherJson.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "esp_tls.h"
#include "esp_heap_caps.h"
#include "lwip/dns.h"
#include "lwip/ip4_addr.h"
#include "lwip/tcpip.h"

namespace esphome::openquatt_weather {

static uint64_t transport_ms() { return static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL; }

void OpenQuattWeather::sample_transport_memory_() {
  const uint32_t block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  auto& low = storage_[0].internal_largest_low;
  if (low == 0 || block < low) low = block;
}

bool OpenQuattWeather::cancelled_() const {
  return cancel_.load(std::memory_order_acquire) || oq_weather::request_expired(storage_[0].request_ms, transport_ms());
}

void OpenQuattWeather::dns_complete_(const char*, const ip_addr_t* address, void* argument) {
  auto* self = static_cast<OpenQuattWeather*>(argument);
  self->dns_address_.store(address && IP_IS_V4(address) ? ip_2_ip4(address)->addr : 0, std::memory_order_relaxed);
  self->dns_done_.store(true, std::memory_order_release);
}

void OpenQuattWeather::dns_start_(void* argument) {
  auto* self = static_cast<OpenQuattWeather*>(argument);
  ip_addr_t address{};
  const char* hostname = self->dns_hostname_.load(std::memory_order_acquire);
  const err_t result = dns_gethostbyname_addrtype(hostname, &address, dns_complete_, self, LWIP_DNS_ADDRTYPE_IPV4);
  if (result == ERR_OK)
    dns_complete_(hostname, &address, self);
  else if (result != ERR_INPROGRESS)
    dns_complete_(hostname, nullptr, self);
}

bool OpenQuattWeather::resolve_dns_(const char* hostname, uint32_t& address) {
  auto& state = storage_[0];
  // A canceled lookup may still be queued in lwIP. Never reuse its callback
  // storage/hostname until completion, and never hand it a pointer into a job.
  if (dns_outstanding_ && !dns_done_.load(std::memory_order_acquire)) {
    state.error = "dns_pending";
    return false;
  }
  dns_outstanding_ = true;
  dns_done_.store(false, std::memory_order_relaxed);
  dns_address_.store(0, std::memory_order_relaxed);
  dns_hostname_.store(hostname, std::memory_order_release);
  if (tcpip_try_callback(dns_start_, this) != ERR_OK) {
    dns_outstanding_ = false;
    state.error = "dns_queue_failed";
    return false;
  }
  while (!dns_done_.load(std::memory_order_acquire)) {
    if (cancelled_()) return false;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  dns_outstanding_ = false;
  address = dns_address_.load(std::memory_order_relaxed);
  if (address == 0) {
    state.error = "dns_failed";
    return false;
  }
  return !cancelled_();
}

bool OpenQuattWeather::fetch_() {
  auto& state = storage_[0];
  state.error = "cancelled";
  state.internal_largest_low = 0;
  sample_transport_memory_();
  if (cancelled_()) return false;
  const char* hostname = state.job == Job::RESOLVE_LOCATION ? "geocoding-api.open-meteo.com" : "api.open-meteo.com";
  int length = 0;
  if (state.job == Job::RESOLVE_LOCATION) {
    if (!oq_weather::encode_geocoding_query(state.request_location.query, state.request_bytes,
                                            sizeof(state.request_bytes))) {
      state.error = "invalid_location";
      return false;
    }
    length = std::snprintf(state.path, sizeof(state.path),
                           "/v1/search?name=%s&count=2&language=nl&format=json&countryCode=NL", state.request_bytes);
  } else {
    if (!oq_weather::valid_location_coordinates(state.request_location.latitude, state.request_location.longitude)) {
      state.error = "invalid_location";
      return false;
    }
    length = std::snprintf(state.path, sizeof(state.path),
                           "/v1/forecast?latitude=%.5f&longitude=%.5f&models=%s"
                           "&hourly=temperature_2m,wind_speed_10m,wind_direction_10m"
                           "&current=temperature_2m,relative_humidity_2m,dew_point_2m,wind_speed_10m,wind_direction_10m"
                           "&wind_speed_unit=ms"
                           "&forecast_hours=36&timezone=UTC&timeformat=unixtime&temperature_unit=celsius",
                           state.request_location.latitude, state.request_location.longitude, oq_weather::kModel);
  }
  if (length < 0 || static_cast<size_t>(length) >= sizeof(state.path)) {
    state.error = "request_size";
    return false;
  }
  length = std::snprintf(state.request_bytes, sizeof(state.request_bytes),
                         "GET %s HTTP/1.1\r\nHost: %s\r\nAccept: application/json\r\nAccept-Encoding: identity\r\n"
                         "Connection: close\r\nUser-Agent: OpenQuatt-weather-experiment\r\n\r\n",
                         state.path, hostname);
  if (length < 0 || static_cast<size_t>(length) >= sizeof(state.request_bytes)) {
    state.error = "request_size";
    return false;
  }

  uint32_t address = 0;
  state.error = "dns_failed";
  if (!resolve_dns_(hostname, address)) {
    if (cancelled_()) state.error = cancel_.load() ? "cancelled" : "request_deadline";
    return false;
  }
  ip4_addr_t ipv4{};
  ipv4.addr = address;
  char numeric_address[16];
  if (!ip4addr_ntoa_r(&ipv4, numeric_address, sizeof(numeric_address))) return false;

  // Numeric connect avoids the synchronous getaddrinfo inside esp-tls. The
  // original fixed hostname remains both SNI and certificate identity.
  esp_tls_cfg_t config{};
  config.non_block = true;
  config.timeout_ms = 50;
  config.common_name = hostname;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  esp_tls_t* tls = esp_tls_init();
  sample_transport_memory_();
  state.error = "tls_allocation_failed";
  if (!tls) return false;
  bool ok = false;
  do {
    state.error = "tls_connect_failed";
    int connected = 0;
    while (!cancelled_() && connected == 0) {
      connected = esp_tls_conn_new_async(numeric_address, std::strlen(numeric_address), 443, &config, tls);
      sample_transport_memory_();
      if (connected == 0) vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (connected != 1 || cancelled_()) break;
    state.error = "tls_write_failed";
    size_t sent = 0;
    while (!cancelled_() && sent < static_cast<size_t>(length)) {
      const ssize_t count = esp_tls_conn_write(tls, state.request_bytes + sent, length - sent);
      sample_transport_memory_();
      if (count == ESP_TLS_ERR_SSL_WANT_READ || count == ESP_TLS_ERR_SSL_WANT_WRITE) {
        vTaskDelay(pdMS_TO_TICKS(10));
        continue;
      }
      if (count <= 0) break;
      sent += count;
    }
    if (sent != static_cast<size_t>(length) || cancelled_()) break;
    state.http.reset(state.response, sizeof(state.response));
    state.error = "invalid_http_response";
    bool read_failed = false;
    while (!cancelled_() && !state.http.done()) {
      const ssize_t count = esp_tls_conn_read(tls, state.tls_chunk, sizeof(state.tls_chunk));
      sample_transport_memory_();
      if (count == ESP_TLS_ERR_SSL_WANT_READ || count == ESP_TLS_ERR_SSL_WANT_WRITE) {
        vTaskDelay(pdMS_TO_TICKS(10));
        continue;
      }
      if (count < 0 || (count > 0 && !state.http.feed(state.tls_chunk, count))) {
        read_failed = true;
        break;
      }
      if (count == 0) {
        (void)state.http.finish();
        break;
      }
    }
    if (cancelled_() || read_failed || !state.http.finish()) break;
    state.error = "invalid_forecast";
    if (state.job == Job::RESOLVE_LOCATION) {
      const char* error = oq_weather::parse_geocoding(
          state.response, state.http.size(), state.json_arena, sizeof(state.json_arena), state.result_location.latitude,
          state.result_location.longitude, state.resolved_name, sizeof(state.resolved_name));
      ok = error == nullptr;
      if (error) state.error = error;
    } else {
      const uint64_t now_ms = transport_ms();
      const uint32_t epoch = state.request_epoch + static_cast<uint32_t>((now_ms - state.request_ms) / 1000U);
      ok = oq_weather::parse_json(state.response, state.http.size(), state.json_arena, sizeof(state.json_arena),
                                  state.candidate, state.request_location.latitude, state.request_location.longitude,
                                  epoch, now_ms);
      state.candidate.generation = state.request_generation;
    }
  } while (false);
  // Only this worker owns/destroys TLS and the socket, including after cancel.
  esp_tls_conn_destroy(tls);
  if (cancelled_()) {
    state.error = cancel_.load() ? "cancelled" : "request_deadline";
    return false;
  }
  if (ok) state.error = "none";
  return ok;
}

}  // namespace esphome::openquatt_weather
