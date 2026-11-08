#include "weather_transport_stubs.h"

#include "components/openquatt_weather/OpenQuattWeather.cpp"
// The Python harness copies the real transport verbatim except for its JSON
// include, selecting either the real ArduinoJson parser or the offline adapter.
#include "production_weather_transport.cpp"

namespace {
using namespace esphome;
using weather_fixture::env;
using weather_transport_fixture::DnsMode;
using weather_transport_fixture::tx;

class Device : public openquatt_weather::OpenQuattWeather {
 public:
  Device() { assert(storage_.allocate()); }
  void prepare(bool geocoding = false, const char* query = "Brummen") {
    assert(phase_.load() == oq_weather::MailboxPhase::IDLE);
    auto& state = storage_[0];
    state.job = geocoding ? Job::RESOLVE_LOCATION : Job::FETCH;
    const char* location = geocoding ? query : "52.09000,6.15000";
    assert(oq_weather::parse_location(location, std::strlen(location), state.request_location) !=
           oq_weather::LocationKind::INVALID);
    state.request_ms = env.now_ms;
    state.request_epoch = 1800000000U + static_cast<uint32_t>((env.now_ms - 1000U) / 1000U);
    state.request_generation = generation_;
    state.request_configuration = configuration_;
    cancel_.store(false, std::memory_order_release);
    work_gate_.store(oq_weather::WorkGate::QUEUED, std::memory_order_release);
    phase_.store(oq_weather::MailboxPhase::REQUESTED, std::memory_order_release);
  }
  bool run() {
    const bool result = fetch_();
    phase_.store(oq_weather::MailboxPhase::IDLE, std::memory_order_release);
    return result;
  }
  const char* error() const { return storage_[0].error; }
  bool dns_outstanding() const { return dns_outstanding_; }
  bool dns_done() const { return dns_done_.load(std::memory_order_acquire); }
  const oq_weather::Series& candidate() const { return storage_[0].candidate; }
  const char* resolved_name() const { return storage_[0].resolved_name; }
};

void reset() {
  assert(tx.active_tls == nullptr);
  env = {};
  tx = {};
}

std::string weather_json() {
  std::string result =
      R"({"latitude":52.09,"longitude":6.15,"utc_offset_seconds":0,"hourly_units":{"time":"unixtime","temperature_2m":"°C","wind_speed_10m":"m/s","wind_direction_10m":"°"},"hourly":{"time":[)";
  for (size_t i = 0; i < oq_weather::kHours; ++i) result += (i ? "," : "") + std::to_string(1800000000U + i * 3600U);
  result += R"(],"temperature_2m":[)";
  for (size_t i = 0; i < oq_weather::kHours; ++i) result += i ? ",5.5" : "5.5";
  result += R"(],"wind_speed_10m":[)";
  for (size_t i = 0; i < oq_weather::kHours; ++i) result += i ? ",1.25" : "1.25";
  result += R"(],"wind_direction_10m":[)";
  for (size_t i = 0; i < oq_weather::kHours; ++i) result += i ? ",359.5" : "359.5";
  return result +
         R"(]},"current_units":{"time":"unixtime","interval":"seconds","wind_speed_10m":"m/s","wind_direction_10m":"°"},"current":{"time":1800000000,"interval":900,"wind_speed_10m":1.25,"wind_direction_10m":359.5}})";
}

std::string geocoding_json() {
  return R"({"results":[{"name":"Brummen","latitude":52.09,"longitude":6.15,"country_code":"NL","feature_code":"PPL"}]})";
}

std::string response(const std::string& body) {
  return "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}

void cleanup_count(unsigned allocations) {
  assert(tx.init_calls == allocations && tx.destroy_calls == allocations && tx.active_tls == nullptr);
}

void successful_transfer(const std::string& framing) {
  reset();
  Device device;
  const auto body = weather_json();
  if (framing == "length")
    tx.wire = response(body);
  else if (framing == "close")
    tx.wire = "HTTP/1.0 200 OK\r\nConnection: close\r\n\r\n" + body;
  else {
    assert(framing == "chunked");
    char hex[32];
    std::snprintf(hex, sizeof(hex), "%zx", body.size());
    tx.wire =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n" + std::string(hex) + "\r\n" + body + "\r\n0\r\n\r\n";
  }
  device.prepare();
  assert(device.run());
  assert(std::strcmp(device.error(), "none") == 0);
  assert(device.candidate().generation != 0 && device.candidate().temperature[0] == 5.5);
  assert(tx.connected_addresses.front() == "192.0.2.10");
  assert(tx.certificate_names.front() == "api.open-meteo.com");
  assert(tx.request_bytes.find("GET /v1/forecast?") == 0);
  assert(tx.request_bytes.find("\r\nHost: api.open-meteo.com\r\n") != std::string::npos);
  assert(tx.request_bytes.find("models=knmi_harmonie_arome_netherlands") != std::string::npos);
  assert(tx.request_bytes.find("hourly=temperature_2m,wind_speed_10m,wind_direction_10m&") != std::string::npos);
  assert(tx.request_bytes.find(
             "current=temperature_2m,relative_humidity_2m,dew_point_2m,wind_speed_10m,wind_direction_10m&") !=
         std::string::npos);
#ifdef OQ_WEATHER_TEST_REAL_JSON
  assert(device.candidate().wind_direction_deg[35] == 359.5f && device.candidate().wind_direction_valid_mask != 0);
  assert(device.candidate().current_wind.valid && device.candidate().current_wind.direction_valid &&
         device.candidate().current_wind.direction_deg == 359.5f);
#endif
  assert(tx.request_bytes.find("wind_speed_unit=ms") != std::string::npos);
  cleanup_count(1);
}

void wants_and_partial_writes() {
  reset();
  Device device;
  tx.wire = response(weather_json());
  tx.connect_pending_calls = 2;
  tx.write_chunk = 7;
  tx.read_chunk = 11;
  tx.write_wants = {ESP_TLS_ERR_SSL_WANT_READ, ESP_TLS_ERR_SSL_WANT_WRITE};
  tx.read_wants = {ESP_TLS_ERR_SSL_WANT_READ, ESP_TLS_ERR_SSL_WANT_WRITE};
  device.prepare();
  assert(device.run());
  assert(tx.connect_calls == 3 && tx.write_calls > 3 && tx.read_calls > 3 && tx.delay_calls >= 6);
  assert(tx.request_bytes.find("GET ") == 0 && tx.request_bytes.find("\r\n\r\n") == tx.request_bytes.size() - 4);
  cleanup_count(1);
}

void postcode_geocoding() {
  for (const auto* query : {"6971", "6971AB", "6971 AB", "6971ab", "6971 ab", "6971aB"}) {
    reset();
    Device device;
    tx.wire = response(geocoding_json());
    device.prepare(true, query);
    assert(device.run() && std::strcmp(device.error(), "none") == 0);
    assert(std::strcmp(device.resolved_name(), "Brummen") == 0);
    assert(tx.request_bytes.find("GET /v1/search?name=6971&count=2&language=nl&format=json&countryCode=NL ") == 0);
    assert(tx.certificate_names.front() == "geocoding-api.open-meteo.com");
    assert(tx.request_bytes.find("\r\nHost: geocoding-api.open-meteo.com\r\n") != std::string::npos);
    cleanup_count(1);
  }
}

void drip_deadline(bool body_drip) {
  reset();
  Device device;
  tx.wire = response(weather_json());
  tx.read_chunk = 1;
  tx.byte_interval_ms = 1000;
  if (body_drip) tx.fast_prefix = tx.wire.find("\r\n\r\n") + 4;
  device.prepare();
  const uint64_t started = env.now_ms;
  assert(!device.run());
  assert(std::strcmp(device.error(), "request_deadline") == 0);
  assert(env.now_ms - started == oq_weather::kRequestDeadlineMs);
  assert(tx.supplied_bytes <= (body_drip ? tx.fast_prefix : 0) + 19);
  cleanup_count(1);
}

void excessive_headers(bool many_headers) {
  reset();
  Device device;
  tx.wire = "HTTP/1.1 200 OK\r\n";
  if (many_headers) {
    for (size_t i = 0; i < 100; ++i) tx.wire += "X:" + std::string(100, 'x') + "\r\n";
  } else {
    tx.wire += "X:" + std::string(20000, 'x') + "\r\n";
  }
  tx.wire += "Content-Length: 1\r\n\r\nx";
  device.prepare();
  assert(!device.run());
  assert(std::strcmp(device.error(), "invalid_http_response") == 0);
  assert(tx.supplied_bytes <=
         (many_headers ? oq_weather::HttpResponse::kHeaderBytes : oq_weather::HttpResponse::kLineBytes) + 512);
  assert(env.now_ms == 1000);
  cleanup_count(1);
}

void cancellation(const std::string& stage) {
  reset();
  Device device;
  tx.wire = response(weather_json());
  if (stage == "dns")
    tx.dns_mode = DnsMode::ASYNC;
  else if (stage == "connect") {
    tx.connect_pending_calls = 10000;
    tx.connect_step_ms = 50;
  } else {
    assert(stage == "read");
    tx.byte_interval_ms = 1000;
  }
  bool cancelled = false;
  tx.on_delay = [&]() {
    if (!cancelled) {
      cancelled = true;
      device.on_shutdown();
    }
  };
  device.prepare();
  assert(!device.run() && cancelled);
  assert(std::strcmp(device.error(), "cancelled") == 0);
  assert(env.now_ms <= 1060);
  if (stage == "dns") {
    assert(tx.init_calls == 0 && tx.destroy_calls == 0 && device.dns_outstanding());
    weather_transport_fixture::complete_dns();
    assert(device.dns_done());
  } else {
    cleanup_count(1);
    if (stage == "connect") assert(tx.write_calls == 0 && tx.read_calls == 0);
  }
}

void connect_deadline() {
  reset();
  Device device;
  tx.connect_pending_calls = 10000;
  tx.connect_step_ms = 50;
  device.prepare();
  const uint64_t start = env.now_ms;
  assert(!device.run());
  assert(std::strcmp(device.error(), "request_deadline") == 0);
  // The final nonblocking connect step may consume its configured 50 ms poll
  // plus the 10 ms yield; no additional connect is allowed after the deadline.
  assert(env.now_ms - start >= 20000 && env.now_ms - start <= 20060);
  assert(tx.write_calls == 0 && tx.read_calls == 0);
  cleanup_count(1);
}

void late_dns_and_reuse(bool cancel_before_deadline, bool queued_start) {
  reset();
  Device device;
  tx.dns_mode = DnsMode::ASYNC;
  tx.defer_dns_start = queued_start;
  if (cancel_before_deadline) {
    tx.on_delay = [&]() { device.on_shutdown(); };
  }
  device.prepare();
  const uint64_t started = env.now_ms;
  assert(!device.run());
  assert(std::strcmp(device.error(), cancel_before_deadline ? "cancelled" : "request_deadline") == 0);
  assert(device.dns_outstanding() && !device.dns_done());
  assert(tx.init_calls == 0 && tx.destroy_calls == 0);
  if (!cancel_before_deadline) assert(env.now_ms - started == oq_weather::kRequestDeadlineMs);
  tx.on_delay = {};
  device.prepare(true);
  assert(!device.run() && std::strcmp(device.error(), "dns_pending") == 0);
  assert(tx.queue_calls == 1);  // The abandoned callback slot may not be overwritten.
  if (queued_start) weather_transport_fixture::start_queued_dns();
  assert(tx.pending_argument == static_cast<openquatt_weather::OpenQuattWeather*>(&device));
  assert(tx.lookup_names.back() == "api.open-meteo.com");
  weather_transport_fixture::complete_dns();
  assert(device.dns_done());
  tx.dns_mode = DnsMode::CACHED;
  tx.defer_dns_start = false;
  tx.dns_answer = 0xc0000214U;
  tx.wire = response(geocoding_json());
  device.prepare(true, "Brummen & Gelderland");
  assert(device.run());
  assert(tx.queue_calls == 2 && tx.dns_calls == 2);
  assert(tx.lookup_names.back() == "geocoding-api.open-meteo.com");
  assert(tx.connected_addresses.back() == "192.0.2.20");
  assert(tx.certificate_names.back() == "geocoding-api.open-meteo.com");
  assert(tx.request_bytes.find("name=Brummen%20%26%20Gelderland") != std::string::npos);
  assert(std::strcmp(device.resolved_name(), "Brummen") == 0);
  cleanup_count(1);
}

void dns_failures(const std::string& mode) {
  reset();
  Device device;
  if (mode == "queue")
    tx.queue_fails = true;
  else if (mode == "lookup")
    tx.dns_mode = DnsMode::ERROR;
  else if (mode == "ipv6")
    tx.reply_is_v4 = false;
  else if (mode == "negative") {
    tx.dns_mode = DnsMode::ASYNC;
    tx.on_delay = []() {
      if (tx.pending_found) weather_transport_fixture::complete_dns(false);
    };
  } else
    assert(false && "unknown DNS fault");
  device.prepare();
  assert(!device.run());
  assert(std::strcmp(device.error(), mode == "queue" ? "dns_queue_failed" : "dns_failed") == 0);
  assert(tx.init_calls == 0 && tx.destroy_calls == 0);
  tx.queue_fails = false;
  tx.dns_mode = DnsMode::CACHED;
  tx.reply_is_v4 = true;
  tx.on_delay = {};
  tx.wire = response(weather_json());
  device.prepare();
  assert(device.run());
  cleanup_count(1);
}

void tls_failures(const std::string& mode) {
  reset();
  Device device;
  tx.wire = response(weather_json());
  const char* error = "invalid_http_response";
  if (mode == "init") {
    tx.allocation_fails = true;
    error = "tls_allocation_failed";
  } else if (mode == "connect") {
    tx.connect_fails = true;
    error = "tls_connect_failed";
  } else if (mode == "write") {
    tx.write_fails = true;
    error = "tls_write_failed";
  } else if (mode == "zero_write") {
    tx.zero_write = true;
    error = "tls_write_failed";
  } else if (mode == "read")
    tx.read_fails = true;
  else if (mode == "truncated")
    tx.wire.resize(tx.wire.size() - 5);
  else if (mode == "empty")
    tx.wire.clear();
  else
    assert(false && "unknown TLS fault");
  device.prepare();
  assert(!device.run() && std::strcmp(device.error(), error) == 0);
  assert(tx.init_calls == 1 && tx.destroy_calls == (mode == "init" ? 0U : 1U) && tx.active_tls == nullptr);
}
}  // namespace

int main(int argc, char** argv) {
  assert(argc == 2);
  const std::string scenario = argv[1];
  if (scenario.rfind("success_", 0) == 0)
    successful_transfer(scenario.substr(8));
  else if (scenario == "wants")
    wants_and_partial_writes();
  else if (scenario == "postcode_geocoding")
    postcode_geocoding();
  else if (scenario == "drip_header")
    drip_deadline(false);
  else if (scenario == "drip_body")
    drip_deadline(true);
  else if (scenario == "oversize_header")
    excessive_headers(false);
  else if (scenario == "oversize_metadata")
    excessive_headers(true);
  else if (scenario.rfind("cancel_", 0) == 0)
    cancellation(scenario.substr(7));
  else if (scenario == "connect_deadline")
    connect_deadline();
  else if (scenario == "late_dns_deadline")
    late_dns_and_reuse(false, false);
  else if (scenario == "late_dns_cancel")
    late_dns_and_reuse(true, false);
  else if (scenario == "late_dns_queued")
    late_dns_and_reuse(true, true);
  else if (scenario.rfind("dns_", 0) == 0)
    dns_failures(scenario.substr(4));
  else if (scenario.rfind("tls_", 0) == 0)
    tls_failures(scenario.substr(4));
  else
    assert(false && "unknown scenario");
}
