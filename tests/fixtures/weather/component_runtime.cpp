#include "weather_runtime_stubs.h"

namespace weather_fixture {
bool successful_weather_fetch = false;
}

// Compile the actual component implementation and its actual PSRAM helper.
// Only platform APIs and the separately tested network transport are replaced.
#include "components/openquatt_weather/OpenQuattWeather.cpp"

namespace esphome::openquatt_weather {
bool OpenQuattWeather::fetch_() {
  ++weather_fixture::env.fetch_calls;
  if (weather_fixture::successful_weather_fetch) {
    auto& series = storage_[0].candidate;
    series = {};
    series.generation = 1;
    series.fetched_epoch = weather_fixture::env.epoch;
    series.fetched_ms = weather_fixture::env.now_ms;
    series.latitude = storage_[0].request_location.latitude;
    series.longitude = storage_[0].request_location.longitude;
    series.current = {series.fetched_epoch - series.fetched_epoch % 900U, 10.5f, 85, 8, true};
    for (size_t i = 0; i < oq_weather::kHours; ++i) {
      series.time[i] = series.fetched_epoch - series.fetched_epoch % 3600U + i * 3600U;
      series.temperature[i] = 10.5f;
      series.wind_mps[i] = 2.5f;
    }
    storage_[0].error = "none";
    return true;
  }
  storage_[0].error = "fixture_transport_disabled";
  return false;
}
}  // namespace esphome::openquatt_weather

namespace {
using namespace esphome;
using weather_fixture::env;

constexpr char request_a[] = "0123456789abcdef0123456789abcdef";
constexpr char request_b[] = "11111111111111111111111111111111";
constexpr char request_c[] = "22222222222222222222222222222222";

class LocationText : public openquatt_weather::WeatherLocationText {
 public:
  void submit(const std::string& query) { control(query); }
};

class Device : public openquatt_weather::OpenQuattWeather {
 public:
  void run_notified_worker() {
    assert(env.notifications == 1);
    try {
      worker_entry_(this);
      assert(false && "persistent worker must park rather than return");
    } catch (const weather_fixture::WorkerParked&) {
      assert(env.notifications == 0);
    }
    assert(phase_.load(std::memory_order_acquire) == oq_weather::MailboxPhase::READY);
  }
  bool pending() const { return pending_save_; }
  bool configuration_error() const { return configuration_error_; }
  bool queued() const { return phase_.load(std::memory_order_acquire) == oq_weather::MailboxPhase::REQUESTED; }
  bool idle() const { return phase_.load(std::memory_order_acquire) == oq_weather::MailboxPhase::IDLE; }
  const char* request_query() const { return storage_[0].request_location.query; }
  const char* pending_query() const { return storage_[0].pending_location.query; }
  const char* accepted_query() const { return storage_[0].location.query; }
};

void reset_environment(const char* stored_location = "Brummen") {
  env = {};
  if (stored_location) {
    oq_weather::Location location;
    assert(oq_weather::parse_location(stored_location, std::strlen(stored_location), location) !=
           oq_weather::LocationKind::INVALID);
    const auto record = oq_weather::location_record(location);
    const auto* bytes = reinterpret_cast<const unsigned char*>(&record);
    env.committed.assign(bytes, bytes + sizeof(record));
    env.namespace_exists = true;
  }
}

std::string persisted_query() {
  assert(env.committed.size() == sizeof(oq_weather::LocationRecord));
  oq_weather::LocationRecord record;
  std::memcpy(&record, env.committed.data(), sizeof(record));
  assert(oq_weather::valid_location_record(record));
  return record.query;
}

struct Harness {
  Device device;
  LocationText location;
  time::RealTimeClock clock;
  openquatt_weather::WeatherEnabledSwitch enabled;
  binary_sensor::BinarySensor paused;
  text_sensor::TextSensor status;
  text_sensor::TextSensor resolved;
  ota::OTAComponent ota_owner;

  explicit Harness(bool restored_enabled = false, bool legacy_enabled = false) {
    if (restored_enabled) {
      env.committed_consent = 1;
      env.namespace_exists = true;
    }
    enabled.state = legacy_enabled;
    device.set_location_text(&location);
    device.set_clock(&clock);
    device.set_enabled_switch(&enabled);
    device.set_pause_sensor(&paused);
    device.set_status(&status);
    device.set_resolved_location(&resolved);
    device.setup();
    assert(env.external_allocations == 1 && env.internal_allocations == 0);
  }
  void finish_job() {
    device.run_notified_worker();
    device.loop();
  }
  void ota_started() {
    paused.publish_state(true);
    device.on_ota_global_state(ota::OTA_STARTED, 0, 0, &ota_owner);
  }
  void ota_aborted() {
    paused.publish_state(false);
    device.on_ota_global_state(ota::OTA_ABORT, 0, 0, &ota_owner);
  }
};

float published_dew_point(Harness& h) {
  auto& endpoint = h.device.forecast_endpoint();
  assert(endpoint.begin_request([] { return static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL; }));
  const auto current = endpoint.request().current;
  endpoint.end_request();
  return current.valid ? current.dew_point_c : NAN;
}

void weather_waits_for_manifest(bool cancel) {
  using openquatt_weather::https_slot;
  reset_environment("52.09000, 6.15556");
  env.network_connected = true;
  Harness h(true);
  assert(https_slot.try_acquire());
  h.device.update();
  assert(h.device.idle() && env.task_creations == 0 && env.fetch_calls == 0);
  https_slot.release();
  h.device.update();
  assert(h.device.queued() && !https_slot.try_acquire());
  if (cancel) {
    h.ota_started();
    assert(!https_slot.try_acquire());
  }
  h.device.run_notified_worker();
  // Worker cleanup must make admission possible without a main-loop tick.
  assert(https_slot.try_acquire());
  h.device.loop();
  assert(!https_slot.try_acquire());  // Consuming READY must not release another owner.
  // Local consent persistence may still run while a manifest owns the slot.
  if (cancel) h.ota_aborted();
  h.enabled.turn_off();
  h.finish_job();
  assert(!h.enabled.state && !https_slot.try_acquire());
  https_slot.release();
}

void restored_switch_never_restores_weather(bool enabled_at_boot, bool cancel_with_ota) {
  reset_environment("52.09000, 6.15556");
  weather_fixture::successful_weather_fetch = true;
  env.network_connected = true;
  Harness h(enabled_at_boot);
  assert(h.enabled.state == enabled_at_boot);
  assert(std::isnan(published_dew_point(h)));
  assert(env.fetch_calls == 0 && env.notifications == 0);
  env.clock_valid = false;
  h.device.update();
  assert(env.notifications == 0 && std::isnan(published_dew_point(h)));
  env.clock_valid = true;
  h.device.update();
  if (!enabled_at_boot) {
    assert(env.notifications == 0 && env.task_creations == 0);
    return;
  }
  assert(h.device.queued());
  if (cancel_with_ota) h.ota_started();
  h.device.run_notified_worker();
  assert(std::isnan(published_dew_point(h)));  // The worker cannot publish HTTP snapshots.
  h.device.loop();
  if (cancel_with_ota) {
    assert(h.enabled.state && std::isnan(published_dew_point(h)));
    h.device.update();
    assert(env.notifications == 0);  // Restored ON cannot bypass the OTA hold.
    h.ota_aborted();
    assert(std::isnan(published_dew_point(h)));
    h.device.update();
    assert(h.device.queued());
    h.finish_job();
  }
  assert(published_dew_point(h) == 8);
  env.clock_valid = false;
  h.device.update();
  assert(std::isnan(published_dew_point(h)));
  env.clock_valid = true;
  h.enabled.turn_off();
  assert(h.enabled.state && std::isnan(published_dew_point(h)));
  h.finish_job();
  assert(!h.enabled.state);
  h.enabled.turn_on();
  assert(!h.enabled.state && std::isnan(published_dew_point(h)));
  h.finish_job();
  assert(h.enabled.state && std::isnan(published_dew_point(h)));
}

void assert_save_receipt(const Harness& h, const char* value, const char* request, openquatt_weather::SaveState state) {
  openquatt_weather::TextSnapshotValue snapshot;
  assert(openquatt_weather::copy_weather_text_snapshot(static_cast<const EntityBase*>(&h.location), snapshot));
  assert(std::strcmp(snapshot.text, value) == 0);
  assert(std::strcmp(snapshot.save_request, request) == 0);
  assert(snapshot.save_state == state);
}

void task_allocation_failure(bool second_attempt) {
  reset_environment();
  Harness h;
  assert(h.location.state == "Brummen");
  env.fail_task_creation = true;
  h.location.submit("Utrecht");
  assert(env.task_creations == 1);
  assert(!h.device.pending() && h.device.configuration_error());
  assert(h.status.state == "worker_allocation_failed");
  assert(h.location.state == "Brummen");
  assert(h.device.idle() && env.notifications == 0);
  if (second_attempt) {
    env.fail_task_creation = false;
    h.location.submit("Zutphen");
    h.device.update();
    h.device.loop();
    assert(env.task_creations == 1);  // A terminal creation failure is not retried.
    assert(!h.device.pending() && h.device.configuration_error());
    assert(h.status.state == "worker_allocation_failed");
    assert(h.location.state == "Brummen");
  }
  assert(env.write_calls == 0 && env.commit_calls == 0 && env.fetch_calls == 0);
  assert(persisted_query() == "Brummen");
}

void fresh_install_task_failure() {
  reset_environment(nullptr);
  Harness h;
  assert(h.location.state.empty() && !h.device.configuration_error());
  env.fail_task_creation = true;
  h.location.submit("Brummen");
  assert(h.location.state.empty());
  assert(h.status.state == "worker_allocation_failed");
  assert(!h.device.pending() && env.write_calls == 0);
}

void psram_failure_has_no_internal_fallback() {
  reset_environment(nullptr);
  env.namespace_exists = true;
  env.committed_consent = 1;
  env.fail_external_allocation = true;
  Harness h(false, true);
  assert(h.device.failed && h.status.state == "psram_allocation_failed" && !h.enabled.state);
  openquatt_weather::TextSnapshotValue snapshot;
  assert(openquatt_weather::copy_weather_text_snapshot(static_cast<EntityBase*>(&h.status), snapshot));
  assert(snapshot.consent_state == openquatt_weather::ConsentState::FAILED);
  h.location.submit("Brummen");
  h.enabled.turn_on();
  h.device.update();
  h.device.loop();
  assert(env.internal_allocations == 0 && env.task_creations == 0 && env.write_calls == 0);
  assert(env.committed_consent == 1 && h.status.state == "psram_allocation_failed");
}

void fetch_rearms_after_long_disabled_uptime() {
  reset_environment();
  Harness h;
  env.network_connected = true;
  h.device.update();
  assert(h.device.idle() && env.task_creations == 0);
  env.now_ms += (1ULL << 31) + 100000;
  h.enabled.turn_on();
  h.finish_job();
  h.device.update();
  assert(h.device.queued() && env.task_creations == 1);
  h.finish_job();
  assert(env.fetch_calls == 1);
  h.enabled.turn_off();
  h.finish_job();
  h.device.update();
  env.now_ms += (1ULL << 32) + 100000;
  h.enabled.turn_on();
  h.finish_job();
  h.device.update();
  assert(h.device.queued() && env.task_creations == 1);
  h.finish_job();
  assert(env.fetch_calls == 2);
}

void verified_publication_and_persistent_worker() {
  reset_environment();
  Harness h;
  env.observe_nvs = [&](const char*) { assert(h.location.state == "Brummen"); };
  h.location.submit("Utrecht");
  assert(h.device.pending() && h.device.queued());
  assert(h.location.state == "Brummen" && h.status.state == "location_saving");
  h.device.run_notified_worker();
  assert(h.location.state == "Brummen");  // Worker may not publish even after commit.
  assert(env.write_calls == 1 && env.commit_calls == 1 && env.verified_reads == 1);
  assert(persisted_query() == "Utrecht");
  h.device.loop();
  assert(h.location.state == "Utrecht" && !h.device.pending() && !h.device.configuration_error());
  env.observe_nvs = {};
  env.fail_task_creation = true;
  h.location.submit("Zutphen");
  h.finish_job();
  assert(h.location.state == "Zutphen");
  assert(env.task_creations == 1 && env.write_calls == 2 && env.commit_calls == 2 && env.verified_reads == 2);
  assert(env.fetch_calls == 0 && h.device.idle());
}

void queued_save_cancelled_by_ota() {
  reset_environment();
  Harness h;
  h.location.submit("Utrecht");
  assert(h.device.queued());
  h.ota_started();
  h.finish_job();
  assert(env.write_opens == 0 && env.write_calls == 0 && env.commit_calls == 0);
  assert(h.location.state == "Brummen" && persisted_query() == "Brummen");
  assert(h.device.pending() && !h.device.configuration_error() && h.device.idle());
  h.device.update();
  h.device.loop();
  assert(env.notifications == 0 && env.write_calls == 0);
  h.ota_aborted();
  h.device.loop();
  assert(h.device.queued() && h.device.pending());
  h.finish_job();
  assert(h.location.state == "Utrecht" && persisted_query() == "Utrecht");
  assert(!h.device.pending() && !h.device.configuration_error());
  assert(env.write_calls == 1 && env.commit_calls == 1 && env.verified_reads == 1);
}

void started_transaction_completes_through_ota() {
  reset_environment();
  Harness h;
  env.on_write = [&]() {
    assert(h.location.state == "Brummen");
    h.ota_started();  // The production CAS has already crossed the start boundary.
  };
  h.location.submit("Utrecht");
  h.device.run_notified_worker();
  assert(h.location.state == "Brummen");
  assert(env.write_calls == 1 && env.commit_calls == 1 && env.verified_reads == 1);
  assert(persisted_query() == "Utrecht");
  h.device.loop();
  assert(h.location.state == "Utrecht" && !h.device.pending() && !h.device.configuration_error());
  assert(env.notifications == 0 && h.device.idle());
}

void storage_failure(const std::string& boundary, bool request_receipt = false) {
  reset_environment();
  Harness h;
  if (boundary == "open")
    env.fail_write_open = true;
  else if (boundary == "write")
    env.fail_write = true;
  else if (boundary == "commit")
    env.fail_commit = true;
  else if (boundary == "read")
    env.fail_read = true;
  else if (boundary == "mismatch")
    env.corrupt_readback = true;
  else
    assert(false && "unknown failure boundary");
  if (request_receipt)
    h.device.set_location_query("Utrecht", request_a);
  else
    h.location.submit("Utrecht");
  h.finish_job();
  assert(h.location.state == "Brummen");
  assert(std::find(h.location.publications.begin(), h.location.publications.end(), "Utrecht") ==
         h.location.publications.end());
  assert(h.status.state == "location_persistence_failed");
  assert(!h.device.pending() && h.device.configuration_error() && h.device.idle());
  if (boundary == "open") assert(env.write_calls == 0 && env.commit_calls == 0);
  if (boundary == "write") assert(env.write_calls == 1 && env.commit_calls == 0);
  if (boundary == "commit") assert(env.write_calls == 1 && env.commit_calls == 1 && env.verified_reads == 0);
  if (boundary == "read" || boundary == "mismatch") assert(env.commit_calls == 1);
  if (request_receipt) assert_save_receipt(h, "Brummen", request_a, openquatt_weather::SaveState::FAILED);
}

void latest_location_wins(bool first_transaction_started) {
  reset_environment();
  Harness h;
  h.location.submit("Utrecht");
  assert(std::strcmp(h.device.request_query(), "Utrecht") == 0);
  if (first_transaction_started) {
    env.on_write = [&]() { h.location.submit("Zutphen"); };
  } else {
    h.location.submit("Zutphen");
    assert(std::strcmp(h.device.request_query(), "Utrecht") == 0);
    assert(std::strcmp(h.device.pending_query(), "Zutphen") == 0);
  }
  h.finish_job();
  assert(h.location.state == "Brummen");
  assert(h.device.pending() && h.device.queued());
  assert(std::strcmp(h.device.request_query(), "Zutphen") == 0);
  assert(env.write_calls == (first_transaction_started ? 1 : 0));
  h.finish_job();
  assert(h.location.state == "Zutphen" && persisted_query() == "Zutphen");
  assert(std::strcmp(h.device.accepted_query(), "Zutphen") == 0);
  assert(!h.device.pending() && !h.device.configuration_error());
  assert(std::find(h.location.publications.begin(), h.location.publications.end(), "Utrecht") ==
         h.location.publications.end());
  assert(env.task_creations == 1 && env.fetch_calls == 0);
}

void failed_verification_then_old_value_retry_does_not_acknowledge_storage() {
  using openquatt_weather::SaveState;
  reset_environment();
  std::vector<unsigned char> durable;
  {
    Harness h;
    env.fail_read = true;
    h.device.set_location_query("Utrecht", request_a);
    assert_save_receipt(h, "Brummen", request_a, SaveState::PENDING);
    h.finish_job();
    assert(persisted_query() == "Utrecht");  // Commit changed flash even though verification failed.
    assert(h.location.state == "Brummen");
    assert_save_receipt(h, "Brummen", request_a, SaveState::FAILED);

    env.fail_read = false;
    env.fail_write_open = true;
    h.device.set_location_query("Brummen", request_b);
    assert(h.device.pending() && h.device.queued());
    assert_save_receipt(h, "Brummen", request_b, SaveState::PENDING);
    h.finish_job();
    assert(h.location.state == "Brummen" && persisted_query() == "Utrecht");
    assert_save_receipt(h, "Brummen", request_b, SaveState::FAILED);
    assert(env.write_calls == 1 && env.commit_calls == 1);
    h.device.update();
    h.device.loop();
    assert(h.device.idle() && !h.device.pending());
    assert_save_receipt(h, "Brummen", request_b, SaveState::FAILED);
    durable = env.committed;
  }
  // A reboot must read the exact durable bytes, rather than the old UI value.
  env = {};
  env.committed = std::move(durable);
  env.namespace_exists = true;
  Harness reboot;
  assert(reboot.location.state == "Utrecht");
  assert_save_receipt(reboot, "Utrecht", "", SaveState::NONE);
}

void superseded_same_value_requires_the_latest_request(bool first_transaction_started) {
  using openquatt_weather::SaveState;
  reset_environment();
  Harness h;
  h.device.set_location_query("Utrecht", request_a);
  assert_save_receipt(h, "Brummen", request_a, SaveState::PENDING);
  const auto supersede = [&]() {
    h.device.set_location_query("Zutphen", request_b);
    assert_save_receipt(h, "Brummen", request_b, SaveState::PENDING);
    h.device.set_location_query("Utrecht", request_c);
    assert_save_receipt(h, "Brummen", request_c, SaveState::PENDING);
    assert(std::strcmp(h.device.request_query(), "Utrecht") == 0);
  };
  if (first_transaction_started)
    env.on_write = supersede;
  else
    supersede();
  h.device.run_notified_worker();
  assert_save_receipt(h, "Brummen", request_c, SaveState::PENDING);
  h.device.loop();
  assert(h.device.pending() && h.device.queued());
  assert_save_receipt(h, "Brummen", request_c, SaveState::PENDING);
  assert(env.write_calls == (first_transaction_started ? 1 : 0));
  env.on_write = {};
  h.finish_job();
  assert(h.location.state == "Utrecht" && persisted_query() == "Utrecht");
  assert_save_receipt(h, "Utrecht", request_c, SaveState::STORED);
  assert(std::count(h.location.publications.begin(), h.location.publications.end(), "Utrecht") == 1);
  assert(!h.device.pending() && h.device.idle());
}

void request_receipt_remains_pending_across_ota(bool first_transaction_started) {
  using openquatt_weather::SaveState;
  reset_environment();
  Harness h;
  h.device.set_location_query("Utrecht", request_a);
  assert_save_receipt(h, "Brummen", request_a, SaveState::PENDING);
  if (first_transaction_started)
    env.on_write = [&]() { h.ota_started(); };
  else
    h.ota_started();
  h.device.run_notified_worker();
  assert_save_receipt(h, "Brummen", request_a, SaveState::PENDING);
  h.device.loop();
  if (!first_transaction_started) {
    assert(env.write_calls == 0 && env.commit_calls == 0 && h.device.pending());
    assert_save_receipt(h, "Brummen", request_a, SaveState::PENDING);
    h.device.update();
    h.device.loop();
    assert_save_receipt(h, "Brummen", request_a, SaveState::PENDING);
    h.ota_aborted();
    h.device.loop();
    assert(h.device.queued());
    assert_save_receipt(h, "Brummen", request_a, SaveState::PENDING);
    h.finish_job();
  }
  assert(h.location.state == "Utrecht" && persisted_query() == "Utrecht");
  assert_save_receipt(h, "Utrecht", request_a, SaveState::STORED);
  assert(env.write_calls == 1 && env.commit_calls == 1 && env.verified_reads == 1);
}

void allocation_failure_never_stores_a_request_receipt(bool psram) {
  using openquatt_weather::SaveState;
  reset_environment(psram ? nullptr : "Brummen");
  env.fail_external_allocation = psram;
  Harness h;
  env.fail_task_creation = !psram;
  h.device.set_location_query("Utrecht", request_a);
  if (psram)
    assert_save_receipt(h, "", "", SaveState::NONE);  // Failed setup cannot acknowledge any request.
  else
    assert_save_receipt(h, "Brummen", request_a, SaveState::FAILED);
  assert(!h.device.pending() && env.write_calls == 0 && env.commit_calls == 0);
}
void assert_consent(const Harness& h, openquatt_weather::ConsentState state, bool last_stored_value) {
  openquatt_weather::TextSnapshotValue snapshot;
  assert(openquatt_weather::copy_weather_text_snapshot(static_cast<const EntityBase*>(&h.status), snapshot));
  assert(snapshot.consent_state == state);
  assert(std::strcmp(snapshot.text, h.status.state.c_str()) == 0);
  assert(h.enabled.state == last_stored_value);
  assert(openquatt_weather::copy_weather_text_snapshot(static_cast<const EntityBase*>(&h.location), snapshot));
  assert(snapshot.consent_state == openquatt_weather::ConsentState::NONE);
}

void consent_boot_and_durable_round_trip() {
  using openquatt_weather::ConsentState;
  reset_environment("52.09, 6.15");
  {
    Harness h(false, true);  // A stale optimistic preference must not authorize traffic.
    env.network_connected = true;
    assert_consent(h, ConsentState::OFF, false);
    h.device.update();
    assert(env.notifications == 0 && env.fetch_calls == 0);
    h.enabled.turn_on();
    assert_consent(h, ConsentState::PENDING, false);
    env.observe_nvs = [&](const char*) { assert_consent(h, ConsentState::PENDING, false); };
    h.device.run_notified_worker();
    env.observe_nvs = {};
    assert(env.committed_consent == 1 && env.verified_reads == 1);
    assert_consent(h, ConsentState::PENDING, false);
    h.device.loop();
    assert_consent(h, ConsentState::ON, true);

    h.enabled.turn_off();
    assert_consent(h, ConsentState::PENDING, true);
    assert(h.status.state == "consent_persistence_pending");
    h.device.run_notified_worker();
    assert(env.committed_consent == 0 && env.verified_reads == 2);
    assert_consent(h, ConsentState::PENDING, true);
    h.device.loop();
    assert_consent(h, ConsentState::OFF, false);
    assert(env.task_creations == 1 && env.fetch_calls == 0);
  }
  const auto location = env.committed;
  reset_environment(nullptr);
  env.committed = location;
  env.namespace_exists = true;
  env.committed_consent = 0;
  Harness reboot(false, true);
  assert_consent(reboot, ConsentState::OFF, false);
  env.network_connected = true;
  reboot.device.update();
  assert(env.notifications == 0);
}

void consent_invalid_boot() {
  using openquatt_weather::ConsentState;
  reset_environment();
  env.committed_consent = 2;
  Harness h(false, true);
  assert_consent(h, ConsentState::FAILED, false);
  assert(h.status.state == "consent_persistence_failed");
  env.network_connected = true;
  h.device.update();
  assert(env.notifications == 0 && env.fetch_calls == 0);
}

void consent_failure_and_explicit_off_retry(const std::string& boundary) {
  using openquatt_weather::ConsentState;
  reset_environment();
  Harness h(true);
  env.network_connected = true;
  env.fail_task_creation = boundary == "task";
  env.fail_write_open = boundary == "open";
  env.fail_write = boundary == "write";
  env.fail_commit = boundary == "commit";
  env.fail_read = boundary == "read";
  env.corrupt_readback = boundary == "mismatch";
  h.enabled.turn_off();
  if (boundary != "task") {
    assert_consent(h, ConsentState::PENDING, true);
    h.finish_job();
  }
  assert_consent(h, ConsentState::FAILED, true);
  assert(h.status.state == "consent_persistence_failed");
  h.device.update();
  h.device.loop();
  assert(h.device.idle() && env.notifications == 0 && env.fetch_calls == 0);

  env.fail_task_creation = env.fail_write_open = env.fail_write = env.fail_commit = env.fail_read = false;
  env.corrupt_readback = false;
  h.enabled.turn_off();
  assert_consent(h, ConsentState::PENDING, true);
  h.finish_job();
  assert_consent(h, ConsentState::OFF, false);
  assert(env.committed_consent == 0 && env.fetch_calls == 0);
}

void consent_latest_off_wins(bool first_transaction_started) {
  using openquatt_weather::ConsentState;
  reset_environment();
  Harness h;
  env.network_connected = true;
  h.enabled.turn_on();
  if (first_transaction_started)
    env.on_write = [&]() { h.enabled.turn_off(); };
  else
    h.enabled.turn_off();
  h.finish_job();
  assert_consent(h, ConsentState::PENDING, first_transaction_started);
  assert(h.device.queued() && env.write_calls == (first_transaction_started ? 1 : 0));
  h.device.update();
  h.finish_job();
  assert_consent(h, ConsentState::OFF, false);
  assert(env.committed_consent == 0 && env.fetch_calls == 0);
}

void consent_ota_defers_queued_write() {
  using openquatt_weather::ConsentState;
  reset_environment();
  Harness h(true);
  h.enabled.turn_off();
  h.ota_started();
  h.finish_job();
  assert_consent(h, ConsentState::PENDING, true);
  assert(env.committed_consent == 1 && env.write_calls == 0 && h.device.idle());
  h.ota_aborted();
  h.device.loop();
  assert(h.device.queued());
  h.finish_job();
  assert_consent(h, ConsentState::OFF, false);
  assert(env.committed_consent == 0 && env.write_calls == 1);
}
}  // namespace

int main(int argc, char** argv) {
  assert(argc == 2);
  const std::string scenario = argv[1];
  if (scenario == "task_failure_first")
    task_allocation_failure(false);
  else if (scenario == "task_failure_second")
    task_allocation_failure(true);
  else if (scenario == "fresh_task_failure")
    fresh_install_task_failure();
  else if (scenario == "psram_failure")
    psram_failure_has_no_internal_fallback();
  else if (scenario == "long_disabled")
    fetch_rearms_after_long_disabled_uptime();
  else if (scenario == "verified_publication")
    verified_publication_and_persistent_worker();
  else if (scenario == "queued_ota")
    queued_save_cancelled_by_ota();
  else if (scenario == "started_ota")
    started_transaction_completes_through_ota();
  else if (scenario.rfind("storage_receipt_", 0) == 0)
    storage_failure(scenario.substr(16), true);
  else if (scenario.rfind("storage_", 0) == 0)
    storage_failure(scenario.substr(8));
  else if (scenario == "latest_queued")
    latest_location_wins(false);
  else if (scenario == "latest_started")
    latest_location_wins(true);
  else if (scenario == "receipt_verify_retry")
    failed_verification_then_old_value_retry_does_not_acknowledge_storage();
  else if (scenario == "receipt_latest_queued")
    superseded_same_value_requires_the_latest_request(false);
  else if (scenario == "receipt_latest_started")
    superseded_same_value_requires_the_latest_request(true);
  else if (scenario == "receipt_ota_queued")
    request_receipt_remains_pending_across_ota(false);
  else if (scenario == "receipt_ota_started")
    request_receipt_remains_pending_across_ota(true);
  else if (scenario == "receipt_task_failure")
    allocation_failure_never_stores_a_request_receipt(false);
  else if (scenario == "receipt_psram_failure")
    allocation_failure_never_stores_a_request_receipt(true);
  else if (scenario == "consent_round_trip")
    consent_boot_and_durable_round_trip();
  else if (scenario == "consent_invalid_boot")
    consent_invalid_boot();
  else if (scenario.rfind("consent_failure_", 0) == 0)
    consent_failure_and_explicit_off_retry(scenario.substr(16));
  else if (scenario == "consent_latest_queued")
    consent_latest_off_wins(false);
  else if (scenario == "consent_latest_started")
    consent_latest_off_wins(true);
  else if (scenario == "consent_ota")
    consent_ota_defers_queued_write();
  else if (scenario == "restore_on")
    restored_switch_never_restores_weather(true, false);
  else if (scenario == "restore_off")
    restored_switch_never_restores_weather(false, false);
  else if (scenario == "restore_on_ota")
    restored_switch_never_restores_weather(true, true);
  else if (scenario == "manifest_slot_failure")
    weather_waits_for_manifest(false);
  else if (scenario == "manifest_slot_cancel")
    weather_waits_for_manifest(true);
  else
    assert(false && "unknown scenario");
}
