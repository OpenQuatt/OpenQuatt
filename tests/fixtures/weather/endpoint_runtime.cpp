#include "weather_endpoint_stubs.h"
#include <atomic>
#include <thread>
#include <cstdio>
#include "components/openquatt_weather/OpenQuattWeather.cpp"
#include "components/openquatt_weather/OpenQuattWeatherEndpoint.cpp"

namespace esphome::openquatt_weather {
bool OpenQuattWeather::fetch_() { return false; }
}  // namespace esphome::openquatt_weather

namespace {
using namespace esphome;
using weather_fixture::env;
class Device : public openquatt_weather::OpenQuattWeather {
 public:
  void register_endpoint() { setup_forecast_endpoint_(); }
  void install(uint32_t serial = 7) {
    auto& series = storage_[0].current;
    series.fetched_epoch = env.epoch;
    series.fetched_ms = env.now_ms;
    series.generation = serial;
    series.latitude = 52.09;
    series.longitude = 6.15;
    series.current = {env.epoch - env.epoch % 900U, 10.5f, 85, 8, true};
    series.current_wind = {env.epoch - env.epoch % 900U, 2.5f, 359.5f, true, true};
    series.wind_direction_valid_mask = (uint64_t{1} << oq_weather::kHours) - 1;
    for (size_t i = 0; i < oq_weather::kHours; ++i) {
      series.time[i] = env.epoch - env.epoch % 3600U + i * 3600U;
      series.temperature[i] = serial == 111 ? 11.1f : serial == 222 ? 22.2f : 10.5f;
      series.wind_mps[i] = serial == 111 ? 1.11f : serial == 222 ? 2.22f : 2.5f;
      series.wind_direction_deg[i] = serial == 111 ? 111 : serial == 222 ? 222 : 0;
    }
    publish_forecast_();
  }
  void refresh() { publish_forecast_(); }
  void clear_current() {
    storage_[0].current.current = {};
    publish_forecast_();
  }
  void clear_wind() {
    storage_[0].current.current_wind = {};
    storage_[0].current.wind_direction_valid_mask = 0;
    publish_forecast_();
  }
  void partial_direction() {
    auto& series = storage_[0].current;
    series.wind_direction_valid_mask &= ~uint64_t{2};
    series.wind_direction_deg[0] = 360;
    series.wind_direction_deg[1] = 180;  // Retained bytes without a valid bit must not be serialized.
    series.current_wind.direction_valid = false;
    publish_forecast_();
  }
  void fetch_again() {
    storage_[0].current.fetched_epoch = env.epoch;
    storage_[0].current.fetched_ms = env.now_ms;
    publish_forecast_();
  }
  void set_current_epoch(uint32_t epoch) {
    storage_[0].current.current.time_epoch = epoch;
    publish_forecast_();
  }
  static size_t storage_bytes() { return sizeof(Storage); }
  const auto& issued_scenario() const { return storage_[0].issued_scenario; }
  bool has_scalar() const { return std::isfinite(storage_[0].forecast.latest[0]); }
  void observation(bool ready = true, double room = 0, double heat = 0) {
    oq_power_house::forecast::Observation value;
    value.monotonic_ms = env.now_ms;
    value.epoch = env.epoch;
    value.context = 1;
    value.model_samples = 155;
    value.valid = true;
    value.model_ready = ready;
    value.room_c = room;
    value.outside_c = 10.5;
    value.heat_w = heat;
    value.h_w_per_k = 220;
    value.c_wh_per_k = 8000;
    observe(value);
  }
  void finish_job() {
    try {
      worker_entry_(this);
    } catch (const weather_fixture::WorkerParked&) {
    }
    loop();
  }
};
struct Harness {
  Device device;
  openquatt_weather::WeatherLocationText location;
  time::RealTimeClock clock;
  openquatt_weather::WeatherEnabledSwitch enabled;
  binary_sensor::BinarySensor paused;
  text_sensor::TextSensor resolved, status, issued_epoch;
  sensor::Sensor temperatures[3];
  openquatt_web_auth::OpenQuattWebAuth auth;
  explicit Harness(bool psram_failure = false) {
    env = {};
    env.fail_external_allocation = psram_failure;
    web_server_base::endpoint_server.clear();
    device.set_clock(&clock);
    device.set_web_auth(&auth);
    device.set_location_text(&location);
    device.set_enabled_switch(&enabled);
    device.set_pause_sensor(&paused);
    device.set_resolved_location(&resolved);
    device.set_status(&status);
    device.set_issued_epoch(&issued_epoch);
    device.set_temperature_30m(&temperatures[0]);
    device.set_temperature_1h(&temperatures[1]);
    device.set_temperature_3h(&temperatures[2]);
    device.setup();
    device.register_endpoint();
    assert(web_server_base::endpoint_server.handlers.size() == 1);
  }
  ~Harness() { web_server_base::endpoint_server.clear(); }
  AsyncWebHandler* handler() { return web_server_base::endpoint_server.handlers[0]; }
  AsyncWebServerRequest get() {
    AsyncWebServerRequest request;
    assert(handler()->canHandle(&request));
    handler()->handleRequest(&request);
    return request;
  }
  void start() {
    enabled.turn_on();
    device.finish_job();
    assert(enabled.state);
    device.install();
  }
  void stop() {
    enabled.turn_off();
    device.finish_job();
    if (enabled.state) {
      device.loop();  // A cancelled location save may leave the mailbox idle.
      device.finish_job();
    }
    assert(!enabled.state);
  }
};

void schema_and_guards() {
  Harness h;
  auto cold = h.get();
  assert(cold.response.status == 200 && cold.response.body.find("\"valid\":false") != std::string::npos);
  h.start();
  h.resolved.state = "Brummen \"plaats\" \\ \n";
  h.device.refresh();
  env.forbid_clock_read = true;
  auto value = h.get();
  env.forbid_clock_read = false;
  const auto& json = value.response.body;
  assert(value.response.finished && value.response.largest_chunk <= 256);
  assert(value.response.headers.at("Cache-Control") == "no-store");
  assert(value.url_copies == 0);
  assert(json.find("\"schema\":1") != std::string::npos && json.find("\"source\":\"model\"") != std::string::npos);
  assert(json.find("\"now_epoch\":1800000000") != std::string::npos);
  assert(json.find("\"wind_mps\":[2.5,2.5") != std::string::npos);
  assert(json.find("Brummen \\\"plaats\\\" \\\\ \\u000a") != std::string::npos);
  assert(json.find("\"dew_point_c\":8") != std::string::npos);
  AsyncWebServerRequest denied;
  denied.authenticated = false;
  h.handler()->handleRequest(&denied);
  assert(denied.response.status == 401 && denied.response.body.empty());
  h.device.set_web_auth(nullptr);
  h.handler()->handleRequest(&denied);
  assert(denied.response.status == 401);
  AsyncWebServerRequest wrong;
  wrong.method_value = HTTP_POST;
  assert(!h.handler()->canHandle(&wrong));
  wrong.method_value = HTTP_GET;
  wrong.url += "-other";
  assert(!h.handler()->canHandle(&wrong));
  wrong.url = "/openquatt/weather/forecast?ignored=1";
  assert(h.handler()->canHandle(&wrong));
  wrong.url += std::string(1024, 'x');
  assert(h.handler()->canHandle(&wrong));  // Query length cannot change the fixed GET route.
  for (const char* path :
       {"/openquatt/weather/forecast%00-other", "/openquatt/weather/forecast%3Fignored=1",
        "/openquatt/weather/%66orecast", "/openquatt/weather/forecast/", "/openquatt/weather/forecast#fragment"}) {
    wrong.url = path;
    assert(!h.handler()->canHandle(&wrong));
  }
  wrong.url = "/openquatt/weather/forecast" + std::string(1024, 'x');
  assert(!h.handler()->canHandle(&wrong));
  assert(wrong.url_copies == 0);
}

void socket_and_busy() {
  Harness h;
  h.start();
  assert(h.device.forecast_endpoint().begin_request([] { return env.now_ms; }));
  auto busy = h.get();
  assert(busy.response.status == 429);
  h.device.forecast_endpoint().end_request();
  AsyncWebServerRequest failed;
  failed.response.fail_at = 2;
  h.handler()->handleRequest(&failed);
  assert(!failed.response.finished && failed.response.closed);
  assert(h.get().response.finished);  // A failed socket must release request ownership.
}

void unavailable_storage() {
  Harness h(true);
  assert(h.device.failed);
  assert(h.get().response.status == 503 && env.internal_allocations == 0);
}

void model_time_and_lifecycle() {
  Harness h;
  h.start();
  h.device.refresh();
  assert(h.get().response.body.find("\"dew_point_c\":8") != std::string::npos);
  env.now_ms += 1800000;
  env.epoch += 1800;
  h.device.refresh();
  assert(h.get().response.body.find("\"dew_point_c\":8") != std::string::npos);
  env.now_ms += 1000;
  ++env.epoch;
  h.device.fetch_again();
  h.device.refresh();
  assert(h.get().response.body.find("\"dew_point_c\":null") != std::string::npos);
  auto old = h.get();
  assert(old.response.body.find("\"dew_point_c\":null") != std::string::npos);
  h.device.install();
  h.device.refresh();
  assert(h.get().response.body.find("\"dew_point_c\":8") != std::string::npos);
  ++env.epoch;  // Future and non-aligned model times are rejected.
  h.device.set_current_epoch(env.epoch + 900);
  h.device.refresh();
  assert(h.get().response.body.find("\"dew_point_c\":null") != std::string::npos);
  h.device.install();
  env.epoch += 100;
  h.device.refresh();
  assert(h.get().response.body.find("\"dew_point_c\":null") != std::string::npos);
  env.epoch -= 100;
  h.paused.publish_state(true);
  h.device.refresh();
  assert(h.get().response.body.find("\"dew_point_c\":null") != std::string::npos);
  assert(h.get().response.body.find("\"wind_mps\":[") == std::string::npos);
  h.paused.publish_state(false);
  h.device.install();
  ota::OTAComponent owner;
  h.device.on_ota_global_state(ota::OTA_STARTED, 0, 0, &owner);
  h.device.refresh();
  assert(h.get().response.body.find("\"dew_point_c\":null") != std::string::npos);
  h.device.on_ota_global_state(ota::OTA_ABORT, 0, 0, &owner);
  h.device.install();
  h.device.set_location_query("Utrecht");
  h.device.refresh();
  assert(h.get().response.body.find("\"dew_point_c\":null") != std::string::npos);
  h.stop();
  h.device.refresh();
  assert(h.get().response.body.find("\"dew_point_c\":null") != std::string::npos);
  assert(h.get().response.body.find("\"enabled\":false") != std::string::npos);
}

void stale_publication() {
  Harness h;
  h.start();
  env.now_ms += 15001;
  auto value = h.get();
  assert(value.response.body.find("\"valid\":false") != std::string::npos);
  assert(value.response.body.find("\"now_epoch\":0") != std::string::npos);
  assert(value.response.body.find("\"dew_point_c\":null") != std::string::npos);
  h.device.refresh();
  assert(h.get().response.body.find("\"wind_mps\":[") != std::string::npos);
  env.clock_valid = false;
  h.device.refresh();
  assert(h.get().response.body.find("\"dew_point_c\":null") != std::string::npos);
  h.device.refresh();
  assert(h.get().response.body.find("\"valid\":false") != std::string::npos);
}

void concurrent_publication() {
  Harness h;
  h.start();
  std::atomic<bool> started{false};
  std::atomic<bool> stop{false};
  std::thread writer([&] {
    started.store(true, std::memory_order_release);
    for (unsigned i = 0; !stop.load(std::memory_order_acquire); ++i) {
      h.device.install(i % 2 ? 111 : 222);
      if (i % 16 == 0) std::this_thread::yield();
    }
  });
  while (!started.load(std::memory_order_acquire)) std::this_thread::yield();
  for (unsigned i = 0; i < 200; ++i) {
    const auto value = h.get();
    assert(value.response.status == 200 && value.response.finished);
    const auto& json = value.response.body;
    if (json.find("\"generation\":111") != std::string::npos)
      assert(json.find("\"temperature_c\":[11.1,11.1") != std::string::npos &&
             json.find("\"wind_mps\":[1.11,1.11") != std::string::npos &&
             json.find("\"wind_direction_deg\":[111,111") != std::string::npos);
    else if (json.find("\"generation\":222") != std::string::npos)
      assert(json.find("\"temperature_c\":[22.2,22.2") != std::string::npos &&
             json.find("\"wind_mps\":[2.22,2.22") != std::string::npos &&
             json.find("\"wind_direction_deg\":[222,222") != std::string::npos);
    else
      assert(json.find("\"generation\":7") != std::string::npos &&
             json.find("\"temperature_c\":[10.5,10.5") != std::string::npos);
    assert(value.response.body.find("\"dew_point_c\":8") != std::string::npos);
  }
  stop.store(true, std::memory_order_release);
  writer.join();
}
void diagnostic_snapshots_survive_without_storage() {
  for (const bool fail_psram : {false, true}) {
    Harness h(fail_psram);
    assert(h.device.failed == fail_psram);
    openquatt_weather::TextSnapshotValue value;
    const auto check = [&](text_sensor::TextSensor& entity, const char* expected) {
      assert(openquatt_weather::copy_weather_text_snapshot(static_cast<EntityBase*>(&entity), value));
      assert(std::strcmp(value.text, expected) == 0);
      assert(value.save_request[0] == '\0' && value.save_state == openquatt_weather::SaveState::NONE);
    };
    if (fail_psram) check(h.status, "psram_allocation_failed");
    if (fail_psram) assert(h.get().response.status == 503);
    // Storage must not fall back through RAMAllocator's internal path. SDK
    // callback registration has separate, setup-only bookkeeping allocations.
    assert(env.internal_allocations == 0);
    openquatt_weather::published_weather_snapshots.store(nullptr, std::memory_order_release);
  }
}

void atomic_scenario_origin_and_new_fetch() {
  Harness h;
  h.start();
  h.device.observation(true, 0, 0);
  const auto original = h.device.issued_scenario();
  assert(original.room_start_c == 0 && original.held_heat_w == 0 && original.model_samples == 155);
  assert(original.weather_generation == 7 && original.configuration != 0);
  std::puts(h.get().response.body.c_str());
  env.now_ms += 10000;
  env.epoch += 10;
  h.device.observation(true, 20, 3000);
  h.device.install(8);
  const auto& after = h.device.issued_scenario();
  assert(after.epoch == original.epoch && after.room_start_c == 0 && after.held_heat_w == 0);
  assert(after.weather_generation == 7 && after.weather_fetched_epoch == original.weather_fetched_epoch);
  for (size_t i = 0; i < 3; ++i) assert(after.temperature_c[i] == original.temperature_c[i]);
  std::puts(h.get().response.body.c_str());
  // A live observation alone cannot relabel a previously issued scenario.
  env.now_ms += 10000;
  env.epoch += 10;
  h.device.observation(false);
  assert(!h.device.has_scalar());
  std::puts(h.get().response.body.c_str());
  env.now_ms += 10000;
  env.epoch += 10;
  h.device.observation(true, 20, 3000);
  std::puts(h.get().response.body.c_str());
}

void atomic_scenario_failure_gates() {
  for (const std::string gate : {"off", "pause", "ota", "pending", "save_failed", "invalid_query", "model", "clock"}) {
    Harness h;
    h.start();
    h.device.observation();
    assert(h.device.has_scalar());
    if (gate == "off") h.stop();
    if (gate == "pause") h.paused.publish_state(true);
    if (gate == "ota") {
      ota::OTAComponent owner;
      h.device.on_ota_global_state(ota::OTA_STARTED, 0, 0, &owner);
    }
    if (gate == "pending" || gate == "save_failed") {
      env.fail_commit = gate == "save_failed";
      h.device.set_location_query("Utrecht");
      if (gate == "save_failed") h.device.finish_job();
    }
    if (gate == "invalid_query") h.device.set_location_query("999, 999");
    if (gate == "model") {
      env.now_ms += 10000;
      env.epoch += 10;
      h.device.observation(false);
    }
    if (gate == "clock") {
      env.epoch += 5;  // Within weather tolerance, outside issue clock coherence.
      h.device.refresh();
    }
    if (gate == "invalid_query" || gate == "clock") assert(h.device.has_scalar());
    assert(h.issued_epoch.state.empty());
    for (const auto& sensor : h.temperatures) assert(std::isnan(sensor.state));
    const auto frame = h.get();
    if (gate == "clock") assert(frame.response.body.find("\"reason\":\"clock_invalid\"") != std::string::npos);
    std::puts(frame.response.body.c_str());
  }
}

void invalid_location_receipt_cannot_precede_atomic_invalidation() {
  Harness h;
  h.start();
  h.device.observation();
  assert(!h.issued_epoch.state.empty());
  for (const auto& sensor : h.temperatures) assert(std::isfinite(sensor.state));
  bool observed = false;
  h.location.frontend = [&] {
    openquatt_weather::TextSnapshotValue receipt;
    assert(openquatt_weather::copy_weather_text_snapshot(static_cast<EntityBase*>(&h.location), receipt));
    assert(receipt.save_state == openquatt_weather::SaveState::FAILED);
    assert(h.issued_epoch.state.empty());
    for (const auto& sensor : h.temperatures) assert(std::isnan(sensor.state));
    const auto frame = h.get();
    std::puts(frame.response.body.c_str());
    observed = true;
  };
  h.device.set_location_query("999, 999", "0123456789abcdef0123456789abcdef");
  assert(observed && h.device.has_scalar());
}
void wind_research_gates() {
  for (const std::string gate :
       {"normal", "partial", "missing", "no_dew_point", "stale", "expired", "disabled", "location", "ota"}) {
    Harness h;
    h.start();
    env.now_ms = (uint64_t{1} << 40) + 1000;
    h.device.install();
    if (gate == "partial") h.device.partial_direction();
    if (gate == "missing") h.device.clear_wind();
    if (gate == "no_dew_point") h.device.clear_current();
    if (gate == "stale") env.now_ms += 15001;
    if (gate == "expired") {
      env.now_ms += 1801000;
      env.epoch += 1801;
      h.device.refresh();
    }
    if (gate == "disabled") h.stop();
    if (gate == "location") h.device.set_location_query("Utrecht");
    if (gate == "ota") {
      ota::OTAComponent owner;
      h.device.on_ota_global_state(ota::OTA_STARTED, 0, 0, &owner);
    }
    std::puts(h.get().response.body.c_str());
  }
}
}  // namespace

int main(int argc, char** argv) {
  assert(argc == 2);
  const std::string scenario = argv[1];
  if (scenario == "schema")
    schema_and_guards();
  else if (scenario == "socket")
    socket_and_busy();
  else if (scenario == "unavailable")
    unavailable_storage();
  else if (scenario == "lifecycle")
    model_time_and_lifecycle();
  else if (scenario == "stale")
    stale_publication();
  else if (scenario == "concurrency")
    concurrent_publication();
  else if (scenario == "diagnostics")
    diagnostic_snapshots_survive_without_storage();
  else if (scenario == "atomic")
    atomic_scenario_origin_and_new_fetch();
  else if (scenario == "atomic_gates")
    atomic_scenario_failure_gates();
  else if (scenario == "receipt_interleave")
    invalid_location_receipt_cannot_precede_atomic_invalidation();
  else if (scenario == "wind")
    wind_research_gates();
  else if (scenario == "memory") {
    std::printf("Storage=%zu Snapshot=%zu Series=%zu\n", Device::storage_bytes(),
                sizeof(openquatt_weather::ForecastSnapshot), sizeof(oq_weather::Series));
    assert(Device::storage_bytes() <= 21U * 1024U);
  } else
    assert(false);
}
