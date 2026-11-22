#include <assert.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "../../components/openquatt_incident_manager/OpenQuattIncidentPolicy.h"
#include "../../openquatt/includes/control/oq_compressor_feedback_logic.h"
#include "../../openquatt/includes/control/oq_compressor_start_limit.h"
#include "../../openquatt/includes/control/oq_defrost_logic.h"
#include "../../openquatt/includes/control/oq_heating_curve_logic.h"
#include "../../openquatt/includes/control/oq_hp_boot_resume.h"
#include "../../openquatt/includes/control/oq_hp_restart_guard.h"
#include "../../openquatt/includes/control/oq_incident_actuator_logic.h"
#include "../../openquatt/includes/control/oq_input_source_logic.h"
#include "../../openquatt/includes/control/oq_power_house_demand_logic.h"
#include "../../openquatt/includes/control/oq_power_house_dispatch_logic.h"
#include "../../openquatt/includes/control/oq_thermal_actuator_logic.h"
#include "../../openquatt/includes/control/oq_thermal_request_logic.h"
#include "../../openquatt/includes/odu/oq_odu_frequency_table.h"
#include "../../openquatt/includes/incidents/oq_hp_incident_engine.h"

#ifndef OQ_TOPOLOGY_DUO
#define OQ_TOPOLOGY_DUO 1
#endif

struct FakeCall {
  void set_value(float) {}
  void perform() {}
};
struct FakeSensor {
  float state{0};
  bool present{true};
  bool has_state() const { return present; }
  FakeCall make_call() { return {}; }
};
struct FakeSelect {
  struct Call {
    FakeSelect& target;
    std::string option;
    void set_option(const char* value) { option = value; }
    void perform() { target.control(option); }
  };

  std::string option{"Heating"};
  std::optional<size_t> selected{17};
  bool present{true};
  std::function<void(const std::string&)> control;
  bool has_state() const { return present; }
  const std::string& current_option() const { return option; }
  std::optional<size_t> active_index() const { return selected; }
  void publish_state(const char* value) { option = value; }
  Call make_call() { return {*this, {}}; }
};
uint32_t millis();

struct FakeIncidentManager {
  // Match the incident manager's link-round timeout for fresh stopped evidence.
  static constexpr uint32_t kRestFeedbackFreshnessMs = 15000;

  struct FeedbackState {
    std::optional<uint16_t> mode;
    uint32_t mode_received_ms{0}, rest_mode_generation{0};
    uint32_t mode_generation{0}, frequency_generation{0};
    uint32_t command_mode_generation{0}, command_frequency_generation{0};
    uint8_t expected_mode{0};
    bool stop_feedback_armed{false};

    void arm(uint8_t new_mode) {
      command_mode_generation = mode_generation;
      command_frequency_generation = frequency_generation;
      expected_mode = new_mode;
      stop_feedback_armed = new_mode == 0;
    }
  };

  std::array<oq_incidents::DerivedOutputs, 2> outputs{};
  std::array<std::optional<oq_incidents::HpIncidentEngine>, 2> engines{};
  std::array<oq_hp_restart_guard::Policy, 2> restart_guards{};
  std::array<bool, 2> inhibited{};
  std::array<oq_boot_resume::Policy, 2> boot{};
  std::array<unsigned, 2> starts{};
  std::array<FeedbackState, 2> feedback{};
  bool boot_resume_pending(unsigned hp, uint32_t now) const { return boot[hp - 1].pending(now); }
  bool boot_protection_hold(unsigned hp, uint32_t now) const { return boot[hp - 1].protection_hold(now); }
  int boot_resume_level(unsigned hp) const { return boot[hp - 1].level(); }
  int update_boot_resume(unsigned hp, bool abort, bool safety, bool inputs, bool adopt, uint32_t now) {
    return boot[hp - 1].update(now, abort || get_outputs(hp).must_stop, safety, inputs, adopt);
  }
  oq_incidents::DerivedOutputs get_outputs(unsigned hp) const {
    const unsigned slot = hp - 1;
    auto output = engines[slot] ? engines[slot]->outputs() : outputs[slot];
    const auto& guard = restart_guards[slot];
    if (output.run_state == oq_incidents::RunState::STOPPED && guard.configured())
      output.available_for_start = output.available_for_start && guard.can_start(millis());
    return output;
  }
  bool startup_inhibited(unsigned hp) const { return inhibited[hp - 1]; }
  uint32_t minimum_off_remaining_ms(unsigned hp, uint32_t now) const {
    const auto& guard = restart_guards[hp - 1];
    return guard.configured() ? guard.remaining_ms(now) : 0;
  }
  void invalidate_restart_credit(unsigned hp) { restart_guards[hp - 1].invalidate(); }
  bool request_start(unsigned hp, uint8_t mode, uint32_t now) {
    const unsigned slot = hp - 1;
    const bool was_stopped = get_outputs(hp).run_state == oq_incidents::RunState::STOPPED;
    const auto& guard = restart_guards[slot];
    if (was_stopped && guard.configured() && !guard.can_start(now)) return false;
    if (auto& engine = engines[slot]; engine) {
      if (!engine->request_start(now)) return false;
    } else {
      auto& output = outputs[slot];
      if (!was_stopped || !output.available_for_start) return false;
      output.run_state = oq_incidents::RunState::START_REQUESTED;
      output.stop_confirmed = false;
    }
    feedback[slot].arm(mode);
    if (was_stopped) {
      invalidate_restart_credit(hp);
      ++starts[slot];
    }
    return true;
  }
  void request_stop(unsigned hp, uint32_t now) {
    const unsigned slot = hp - 1;
    if (auto& engine = engines[slot]; engine) {
      if (!engine->request_stop(now)) return;
    } else {
      auto& output = outputs[slot];
      output.run_state = oq_incidents::RunState::STOPPING;
      output.stop_confirmation_pending = true;
    }
    feedback[slot].arm(0);
  }

  void observe_mode(unsigned hp, uint16_t mode, uint32_t now) {
    auto& sample = feedback[hp - 1];
    sample.mode = mode;
    sample.mode_received_ms = now;
    ++sample.mode_generation;
    if (mode != 0) invalidate_restart_credit(hp);
  }

  void observe_frequency(unsigned hp, float hz, uint32_t now) {
    using namespace esphome::openquatt_incident_manager;
    using oq_incidents::RunState;
    const unsigned slot = hp - 1;
    auto& sample = feedback[slot];
    ++sample.frequency_generation;
    if (auto& engine = engines[slot]; engine) {
      const auto& outputs = engine->outputs();
      const auto state = outputs.run_state;
      const bool waiting_for_start =
          state == RunState::START_REQUESTED || state == RunState::WAIT_MODE || state == RunState::WAIT_COMPRESSOR;
      const bool waiting_for_stop = state == RunState::STOPPING || state == RunState::STOP_UNCONFIRMED;
      const bool post_command_mode =
          feedback_generation_is_newer(sample.mode_generation, sample.command_mode_generation);
      const bool post_command_feedback =
          post_command_feedback_complete(sample.mode_generation, sample.command_mode_generation,
                                         sample.frequency_generation, sample.command_frequency_generation);
      const bool fresh =
          run_observation_is_fresh(waiting_for_start, waiting_for_stop, outputs.stop_confirmation_pending,
                                   sample.stop_feedback_armed, post_command_feedback);
      const bool stopped_mode = sample.mode == 0 && post_command_mode;
      engine->observe_run({now, fresh, sample.mode == sample.expected_mode, stopped_mode, true, hz});
      if (hz > 0.5f) {
        invalidate_restart_credit(hp);
      } else if (fresh && stopped_mode && engine->outputs().stop_confirmed &&
                 now - sample.mode_received_ms <= kRestFeedbackFreshnessMs &&
                 feedback_generation_is_newer(sample.mode_generation, sample.rest_mode_generation)) {
        restart_guards[slot].observe_stopped(now);
        sample.rest_mode_generation = sample.mode_generation;
      }
      if (engine->outputs().stop_confirmed) sample.stop_feedback_armed = false;
      return;
    }
    const bool running = sample.mode.value_or(0) > 0 && hz > 0.0f;
    auto& output = outputs[slot];
    output.run_state = running ? oq_incidents::RunState::RUNNING : oq_incidents::RunState::STOPPED;
    output.running_confirmed = running;
    output.stop_confirmed = !running;
    output.stop_confirmation_pending = false;
  }
};
struct FakeDevice {
  uint8_t address{1};
  uint8_t device_address() const { return address; }
};
struct FakeDefrostService {
  oq_defrost::Cycle cycle{};
  bool automatic_enabled{true}, hold{false}, busy_value{false};
  bool forced_owned{true};
  uint32_t forced_deadline{10000}, sample_ms{1000};
  unsigned normal_write_calls{0}, queue_clear_calls{0};
  bool automatic() const { return automatic_enabled; }
  bool loaded() const { return true; }
  bool holding() const { return hold; }
  bool busy() const { return busy_value; }
  void release() { hold = false; }
  bool forced_write_pending(uint32_t now) const { return forced_owned && now <= forced_deadline; }
  bool sample_fresh(unsigned, uint32_t now) const { return now - sample_ms <= 30000; }
  bool normal_write_allowed(int, bool fault) {
    ++normal_write_calls;
    if (fault) ++queue_clear_calls;
    return !hold;
  }
};

FakeSensor flow_rate_selected{};
FakeDevice hp1{};
bool hp1_compressor_command_revalidation_required{};
FakeSensor hp1_compressor_frequency{};
FakeSelect hp1_compressor_level{};
FakeSensor hp1_excluded_frequency_max_hz{};
FakeSensor hp1_excluded_frequency_min_hz{};
int hp1_generation_variant_code{};
bool hp1_is_online{};
int hp1_last_applied_level{};
FakeDefrostService hp1_odu_defrost{};
bool hp1_odu_generation_detection_complete{};
bool hp1_odu_generation_revalidation_required{};
bool hp1_runtime_frequency_revalidation_required{};
oq_odu::RuntimeFrequencySnapshotStorage hp1_runtime_frequency_snapshot_storage{};
FakeSelect hp1_set_working_mode{};
FakeSensor hp1_water_in_temp_offset{};
FakeDevice hp2{};
bool hp2_compressor_command_revalidation_required{};
FakeSensor hp2_compressor_frequency{};
FakeSelect hp2_compressor_level{};
FakeSensor hp2_excluded_frequency_max_hz{};
FakeSensor hp2_excluded_frequency_min_hz{};
int hp2_generation_variant_code{};
bool hp2_is_online{};
int hp2_last_applied_level{};
FakeDefrostService hp2_odu_defrost{};
bool hp2_odu_generation_detection_complete{};
bool hp2_odu_generation_revalidation_required{};
bool hp2_runtime_frequency_revalidation_required{};
oq_odu::RuntimeFrequencySnapshotStorage hp2_runtime_frequency_snapshot_storage{};
FakeSelect hp2_set_working_mode{};
FakeSensor hp2_water_in_temp_offset{};
FakeSelect hp_generation{};
bool oq_air_purge_abort{};
bool oq_air_purge_active{};
int oq_air_purge_phase{};
int oq_air_purge_remaining_s{};
int oq_air_purge_started_ms{};
int oq_air_purge_state{};
std::string oq_air_purge_status_value{};
int oq_air_purge_target_ipwm{};
std::string oq_boiler_power_test_status_value{};
bool oq_cold_start_hp_blocked{};
bool oq_cold_start_session_active{};
bool oq_commissioning_abort_requested{};
bool oq_commissioning_active{};
int oq_commissioning_boiler_request{};
bool oq_commissioning_request_pending{};
int oq_commissioning_started_ms{};
int oq_commissioning_state_code{};
int oq_commissioning_state_since_ms{};
std::string oq_commissioning_status_value{};
int oq_commissioning_task_code{};
int oq_control_mode_code{};
int oq_cooling_stop_reason_code{};
FakeSensor oq_day_max_frequency_hz{};
struct FakeSwitch {
  bool state{true};
  bool has_state() const { return false; }  // Match ESPHome Switch, including a restored ON state.
};
FakeSwitch oq_enabled{};
int oq_strategy_active_code{3};
bool oq_flow_autotune_abort{};
bool oq_flow_autotune_active{};
bool oq_flow_autotune_req{};
int oq_flow_autotune_state{};
std::string oq_flow_autotune_status_value{};
bool oq_hp_water_calibration_abort{};
bool oq_hp_water_calibration_active{};
int oq_hp_water_calibration_phase{};
int oq_hp_water_calibration_remaining_s{};
float oq_hp_water_calibration_result_expected_spread_c{};
float oq_hp_water_calibration_result_hp1_in_raw_avg_c{};
float oq_hp_water_calibration_result_hp1_out_raw_avg_c{};
float oq_hp_water_calibration_result_hp2_in_raw_avg_c{};
float oq_hp_water_calibration_result_hp2_out_raw_avg_c{};
float oq_hp_water_calibration_result_reference_c{};
float oq_hp_water_calibration_result_spread_before_c{};
float oq_hp_water_calibration_result_supply_offset_c{};
float oq_hp_water_calibration_result_supply_raw_avg_c{};
std::string oq_hp_water_calibration_result_supply_source{};
int oq_hp_water_calibration_result_supply_source_code{};
int oq_hp_water_calibration_result_supply_source_fingerprint{};
float oq_hp_water_calibration_spread_c{};
int oq_hp_water_calibration_stable_progress_s{};
int oq_hp_water_calibration_stable_required_s{};
int oq_hp_water_calibration_started_ms{};
std::string oq_hp_water_calibration_status_value{};
float oq_hp_water_calibration_supply_delta_c{};
int oq_hp_water_calibration_target_ipwm{};
FakeIncidentManager oq_incident_manager{};
bool oq_lowflow_fault_active{};
bool oq_manual_flow_active{};
std::string oq_manual_flow_status_value{};
FakeSensor oq_manual_hp1_level{};
FakeSelect oq_manual_hp1_mode{};
int oq_manual_hp1_mode_code{};
FakeSensor oq_manual_hp2_level{};
FakeSelect oq_manual_hp2_mode{};
int oq_manual_hp2_mode_code{};
bool oq_manual_hp_active{};
std::string oq_manual_hp_guard_status_value{};
bool oq_manual_hp_mode_allowed{};
std::string oq_manual_hp_status_value{};
bool oq_manual_hp_stop_requested{};
bool oq_quick_flow_test_active{};
int oq_quick_flow_test_remaining_s{};
int oq_quick_flow_test_started_ms{};
FakeSensor oq_silent_active{};
FakeSensor oq_silent_max_frequency_hz{};
bool oq_water_temp_hard_trip_active{};

uint32_t test_now_ms{1000};
uint32_t millis() { return test_now_ms; }
namespace oq_sources {
inline uint64_t monotonic_ms() { return test_now_ms; }
}  // namespace oq_sources
#define id(value) value
#include "../../openquatt/includes/control/oq_compressor_command_runtime.h"
#undef id

namespace {

std::array<uint8_t, 5> command(uint16_t address, uint16_t value) {
  return {6, static_cast<uint8_t>(address >> 8), static_cast<uint8_t>(address), static_cast<uint8_t>(value >> 8),
          static_cast<uint8_t>(value)};
}

bool queued(bool is_hp1, uint16_t address, uint16_t value) {
  const auto pdu = command(address, value);
  const auto before_hp1 = hp1_odu_defrost.normal_write_calls;
  const auto before_hp2 = hp2_odu_defrost.normal_write_calls;
  const bool result = oq_command_runtime::queued_command(is_hp1, pdu.data(), pdu.size());
  assert(hp1_odu_defrost.normal_write_calls == before_hp1);
  assert(hp2_odu_defrost.normal_write_calls == before_hp2);
  assert(hp1_odu_defrost.queue_clear_calls == 0 && hp2_odu_defrost.queue_clear_calls == 0);
  return result;
}

void reset() {
  oq_enabled.state = true;
  test_now_ms = 1000;
  oq_water_temp_hard_trip_active = oq_lowflow_fault_active = false;
  oq_commissioning_active = oq_manual_hp_active = false;
  oq_manual_hp_stop_requested = false;
  oq_control_mode_code = 2;
  oq_cooling_stop_reason_code = 0;
  oq_commissioning_task_code = oq_commissioning::TASK_NONE;
  oq_incident_manager = {};
  for (auto& output : oq_incident_manager.outputs) {
    output.run_state = oq_incidents::RunState::RUNNING;
    output.running_confirmed = true;
    output.available_for_start = true;
  }
  hp1_odu_defrost = {};
  hp2_odu_defrost = {};
  for (auto* service : {&hp1_odu_defrost, &hp2_odu_defrost}) {
    service->cycle.observe_mode(2, test_now_ms);
    service->cycle.observe_bit(0, test_now_ms);
  }
  hp1.address = 1;
  hp2.address = 2;
  hp1_is_online = hp2_is_online = true;
  hp1_odu_generation_detection_complete = hp2_odu_generation_detection_complete = true;
  hp1_odu_generation_revalidation_required = hp2_odu_generation_revalidation_required = false;
  hp1_runtime_frequency_revalidation_required = hp2_runtime_frequency_revalidation_required = false;
  hp1_compressor_command_revalidation_required = hp2_compressor_command_revalidation_required = false;
  hp1_generation_variant_code = hp2_generation_variant_code = static_cast<int>(oq_odu::Variant::V2_NEW_MODEL);
  hp1_compressor_frequency.state = hp2_compressor_frequency.state = 60;
  hp1_set_working_mode = hp2_set_working_mode = {};
  hp1_compressor_level = hp2_compressor_level = {};
  hp_generation.option = "V2";
  flow_rate_selected = {1000};
  oq_command_runtime::minimum_defrost_flow_lph = 700;
  oq_day_max_frequency_hz = {90};
  oq_silent_max_frequency_hz = {55};
  oq_silent_active = {};
  hp1_excluded_frequency_min_hz = hp1_excluded_frequency_max_hz = {};
  hp2_excluded_frequency_min_hz = hp2_excluded_frequency_max_hz = {};
  hp1_water_in_temp_offset = hp2_water_in_temp_offset = {};
  oq_cold_start_session_active = oq_cold_start_hp_blocked = false;
  oq_odu::RuntimeFrequencySnapshot snapshot;
  snapshot.variant = oq_odu::Variant::V2_NEW_MODEL;
  snapshot.heating.valid = true;
  snapshot.heating.level_count = 21;
  snapshot.heating.hz = {0, 20, 26, 30, 36, 40, 45, 48, 52, 55, 60, 65, 68, 72, 76, 82, 85, 90, 95, 102, 110};
  hp1_runtime_frequency_snapshot_storage = hp2_runtime_frequency_snapshot_storage =
      oq_odu::encode_runtime_frequency_snapshot(snapshot);
  oq_sources::hp1 = {};
  oq_sources::hp1.outside.observe(-7, test_now_ms, true);
  oq_sources::hp1.water_in.observe(32.5, test_now_ms, true);
#if OQ_TOPOLOGY_DUO
  oq_sources::hp2 = {};
  oq_sources::hp2.outside.observe(-7, test_now_ms, true);
  oq_sources::hp2.water_in.observe(32.5, test_now_ms, true);
#endif
  oq_boot_resume_runtime::ota_hold_reader = nullptr;
  oq_boot_resume_runtime::minimum_flow_lph = 700;
  oq_boot_resume_runtime::read_sources = [](uint64_t) {
    return oq_boot_resume_runtime::Sources{true, true, true, 1000};
  };
  assert(queued(true, 3999, 4));  // Every gate test starts from a currently authorized pending request.
}

void boot_reads_hold_all_writes_and_abort_on_safety_loss() {
  reset();
  auto& boot = oq_incident_manager.boot[0];
  boot.begin(true, test_now_ms);
  for (const auto address : {1999, 3999, 2006, 2010, 2015}) {
    assert(!queued(true, address, 0));
    assert(!oq_command_runtime::guarded_write(true, address, 0));
  }
#if OQ_TOPOLOGY_DUO
  assert(!queued(false, 2010, 0));
  assert(!queued(false, 2015, 0));
  assert(queued(false, 2006, 0));
#endif
  const std::array<uint8_t, 5> read{3, 7, 207, 0, 1};
  assert(oq_command_runtime::queued_command(true, read.data(), read.size()));
  assert(boot.pending(test_now_ms));
  oq_water_temp_hard_trip_active = true;
  assert(queued(true, 3999, 0));
  assert(boot.stop_required(test_now_ms));
  assert(!queued(true, 3999, 2));
  reset();
  oq_incident_manager.boot[0].begin(true, test_now_ms);
  oq_boot_resume_runtime::read_sources = [](uint64_t) {
    return oq_boot_resume_runtime::Sources{true, false, true, 1000};
  };
  oq_boot_resume_runtime::update(true);
  assert(oq_incident_manager.boot[0].stop_required(test_now_ms));
  reset();
  oq_incident_manager.boot[0].begin(true, test_now_ms);
  oq_boot_resume_runtime::read_sources = nullptr;
  assert(oq_boot_resume_runtime::update(true, true, true) == 0);
  assert(oq_incident_manager.boot[0].pending(test_now_ms));
  test_now_ms += oq_boot_resume::SAFETY_ACQUISITION_MS;
  assert(oq_boot_resume_runtime::update(true, true, true) == 0);
  assert(oq_incident_manager.boot[0].stop_required(test_now_ms));
}

void ota_hold_blocks_writes_but_keeps_reads_and_releases_for_safety() {
  reset();
  oq_boot_resume_runtime::ota_hold_reader = [](uint8_t hp, uint32_t) { return hp == 1; };
  for (const auto address : {1999, 3999, 2006, 2010, 2015}) {
    assert(!queued(true, address, 0));
    assert(!oq_command_runtime::guarded_write(true, address, 0));
  }
  const std::array<uint8_t, 5> read{3, 7, 207, 0, 1};
  assert(oq_command_runtime::queued_command(true, read.data(), read.size()));
#if OQ_TOPOLOGY_DUO
  assert(!queued(false, 2010, 0));
  assert(!queued(false, 2015, 0));
  assert(queued(false, 2006, 0));
#endif
  oq_boot_resume_runtime::ota_hold_reader = [](uint8_t, uint32_t) { return false; };
  oq_water_temp_hard_trip_active = true;
  assert(queued(true, 3999, 0));
  assert(!queued(true, 3999, 2));
  reset();
}

void shared_pump_hold_expires_and_releases_on_permission_loss() {
#if OQ_TOPOLOGY_DUO
  for (bool running_hp1 : {true, false}) {
    for (bool lose_permission : {true, false}) {
      reset();
      auto& boot = oq_incident_manager.boot[running_hp1 ? 0 : 1];
      boot.begin(true, test_now_ms);
      for (uint16_t address : {2010, 2015}) {
        assert(!queued(!running_hp1, address, 0));
        assert(!oq_command_runtime::guarded_write(!running_hp1, address, 0));
      }
      assert(queued(!running_hp1, 3999, 0));  // The stopped peer may still receive a compressor stop.
      if (lose_permission) {
        oq_boot_resume_runtime::read_sources = [](uint64_t) {
          return oq_boot_resume_runtime::Sources{true, false, true, 1000};
        };
      } else {
        test_now_ms += oq_boot_resume::SAFETY_ACQUISITION_MS;
      }
      assert(queued(!running_hp1, 2010, 0));
      assert(queued(!running_hp1, 2015, 0));
      assert(boot.stop_required(test_now_ms));
    }
  }
#endif
}

void prepare_running_boot(bool hp1_running = true) {
  reset();
  auto& boot = oq_incident_manager.boot[hp1_running ? 0 : 1];
  boot.begin(true, test_now_ms);
  for (auto command : {std::pair<uint16_t, float>{3999, 2}, {1999, 17}, {2099, 2}, {2103, 60}})
    boot.observe(command.first, command.second, test_now_ms);
#if OQ_TOPOLOGY_DUO
  auto& raw = hp1_running ? oq_sources::hp1 : oq_sources::hp2;
#else
  assert(hp1_running);
  auto& raw = oq_sources::hp1;
#endif
  raw.working_mode.observe(2, test_now_ms, true);
  raw.compressor_frequency.observe(60, test_now_ms, true);
  raw.water_out.observe(35, test_now_ms, true);
  raw.defrost.observe(0, test_now_ms, true);
  raw.status_2119.observe(0, test_now_ms, true);
  assert(oq_boot_resume_runtime::capture(hp1_running).ready);
  assert(!oq_boot_resume_runtime::capture(hp1_running).abort);
}

void boot_adoption_requires_live_safety_and_comfort() {
  prepare_running_boot();
  assert(oq_boot_resume_runtime::update(true, true, false) == 0);  // Comfort still missing.
  assert(oq_incident_manager.boot[0].pending(test_now_ms));
  assert(oq_boot_resume_runtime::update(true, true, true) == 17);
  assert(!oq_incident_manager.boot[0].pending(test_now_ms));

  for (int fault = 0; fault < 9; ++fault) {
    prepare_running_boot();
    assert(oq_boot_resume_runtime::update(true, true, false) == 0);
    switch (fault) {
      case 0:
        oq_sources::hp1.defrost.observe(1, test_now_ms, true);
        break;
      case 1:
        oq_sources::hp1.status_2119.observe(8, test_now_ms, true);
        break;
      case 2:
        oq_sources::hp1.water_out.observe(NAN, test_now_ms, false);
        break;
      case 3:
        oq_boot_resume_runtime::read_sources = [](uint64_t) {
          return oq_boot_resume_runtime::Sources{true, true, true, 699};
        };
        break;
      case 4:
        oq_boot_resume_runtime::read_sources = [](uint64_t) {
          return oq_boot_resume_runtime::Sources{false, false, true, 1000};
        };
        break;
      case 5:
        test_now_ms += oq_boot_resume::TELEMETRY_FRESH_MS + 1;
        break;
      case 6:
        hp1_odu_generation_revalidation_required = true;
        break;
      case 7:
        oq_incident_manager.outputs[0].must_stop = true;
        break;
      case 8:
        oq_enabled.state = false;
        break;
    }
    assert(oq_boot_resume_runtime::update(true, true, true) == 0);
    assert(oq_incident_manager.boot[0].stop_required(test_now_ms));
  }
}

void forced_defrost_requires_live_ownership_and_gates() {
  reset();
  assert(queued(true, 3999, 4));
  hp1_odu_defrost.forced_owned = false;
  assert(!queued(true, 3999, 4));
  assert(hp1_compressor_command_revalidation_required);
  assert(!hp2_compressor_command_revalidation_required);
  reset();
  hp1_odu_defrost.forced_deadline = test_now_ms - 1;
  assert(!queued(true, 3999, 4));
  reset();
  hp1_is_online = false;
  assert(!queued(true, 3999, 4));
  reset();
  hp1_odu_generation_detection_complete = false;
  assert(!queued(true, 3999, 4));
  reset();
  hp1_odu_generation_revalidation_required = true;
  assert(!queued(true, 3999, 4));
  reset();
  oq_water_temp_hard_trip_active = true;
  assert(!queued(true, 3999, 4));
  reset();
  oq_lowflow_fault_active = true;
  assert(!queued(true, 3999, 4));
  reset();
  oq_incident_manager.outputs[0].must_stop = true;
  assert(!queued(true, 3999, 4));
  reset();
  hp1_odu_defrost.cycle.mode_ms = test_now_ms - 90001;
  assert(!queued(true, 3999, 4));

  reset();
  flow_rate_selected.state = 699;
  assert(!queued(true, 3999, 4));
  flow_rate_selected.state = 700;
  assert(queued(true, 3999, 4));
  flow_rate_selected.present = false;
  assert(!queued(true, 3999, 4));
  reset();
  flow_rate_selected.state = NAN;
  assert(!queued(true, 3999, 4));
  reset();
  oq_command_runtime::minimum_defrost_flow_lph = NAN;
  assert(!queued(true, 3999, 4));
#if OQ_TOPOLOGY_DUO
  reset();
  hp2_odu_defrost.busy_value = true;
  assert(!queued(true, 3999, 4));
  reset();
  hp2_odu_defrost.cycle.mode = 4;
  assert(!queued(true, 3999, 4));
  reset();
  hp2_odu_defrost.cycle.mode_seen = false;
  assert(!queued(true, 3999, 4));
#endif
  reset();
  hp1_odu_defrost.automatic_enabled = false;
  assert(!queued(true, 3999, 4));
  reset();
  oq_commissioning_active = true;
  assert(!queued(true, 3999, 4));
  reset();
  oq_incident_manager.outputs[0].running_confirmed = false;
  assert(!queued(true, 3999, 4));
  reset();
  oq_incident_manager.inhibited[0] = true;
  assert(!queued(true, 3999, 4));
  for (float hz : {0.0f, -1.0f, NAN, std::numeric_limits<float>::infinity()}) {
    reset();
    hp1_compressor_frequency.state = hz;
    assert(!queued(true, 3999, 4));
  }
  for (int mode : {0, 1, 4}) {
    reset();
    hp1_odu_defrost.cycle.mode = mode;
    assert(!queued(true, 3999, 4));
  }
  reset();
  hp1_odu_defrost.cycle.mode = 3;
  assert(queued(true, 3999, 4));
  reset();
  // The accepted request is still owned and mode telemetry fresh, but the
  // separate compressor sample has expired before the queued frame dispatch.
  test_now_ms = 32001;
  hp1_odu_defrost.forced_deadline = test_now_ms + 100;
  hp1_odu_defrost.cycle.observe_mode(2, test_now_ms);
  hp1_odu_defrost.cycle.observe_bit(0, test_now_ms);
  hp2_odu_defrost.cycle.observe_mode(2, test_now_ms);
  hp2_odu_defrost.cycle.observe_bit(0, test_now_ms);
  assert(!queued(true, 3999, 4));
}

void queued_safety_zero_is_readonly_and_temperature_independent() {
  reset();
  hp1_odu_defrost.hold = true;
  hp1_odu_defrost.cycle.mode = 4;
  hp1_odu_defrost.cycle.mode_seen = false;
  hp1_runtime_frequency_snapshot_storage.fill(0);
  oq_water_temp_hard_trip_active = true;
  assert(queued(true, 1999, 0));
  assert(queued(true, 3999, 0));
  assert(hp1_odu_defrost.normal_write_calls == 0);
  assert(hp1_odu_defrost.queue_clear_calls == 0);
  assert(!queued(true, 1999, 17));
  // An unresolved stop remains authoritative after the global fault clears.
  for (const auto state : {oq_incidents::RunState::STOPPING, oq_incidents::RunState::STOP_UNCONFIRMED}) {
    for (const bool is_hp1 : {true, false}) {
#if !OQ_TOPOLOGY_DUO
      if (!is_hp1) continue;
#endif
      reset();
      oq_incident_manager.outputs[is_hp1 ? 0 : 1].run_state = state;
      auto& service = is_hp1 ? hp1_odu_defrost : hp2_odu_defrost;
      service.hold = true;
      service.cycle.mode_seen = false;
      assert(queued(is_hp1, 3999, 0));
      assert(queued(is_hp1, 1999, 0));
      assert(!queued(is_hp1, 3999, 2));
      assert(!queued(is_hp1, 1999, 17));
      assert(!queued(is_hp1, 3999, 4));
    }
  }
  reset();
  hp1_odu_defrost.hold = true;
  assert(!queued(true, 1999, 0));  // Ordinary stop cannot override the ODU hold.
}

void queued_heating_rechecks_selected_level_and_frequency_policy() {
  reset();
  assert(queued(true, 1999, 17));
  assert(queued(true, 3999, 2));
  hp1_compressor_level.selected = 13;
  assert(!queued(true, 1999, 17));
  assert(hp1_compressor_command_revalidation_required);
  assert(queued(true, 1999, 13));
  reset();
  hp1_compressor_level.selected.reset();
  assert(!queued(true, 1999, 17));
  assert(!queued(true, 3999, 2));
  reset();
  hp1_set_working_mode.option = "Cooling";
  assert(!queued(true, 3999, 2));
  reset();
  assert(queued(true, 1999, 17));
  oq_silent_active.state = 1;
  oq_silent_max_frequency_hz.state = 72;  // An independently configured user cap changes before dispatch.
  assert(!queued(true, 1999, 17));
  assert(!queued(true, 3999, 2));
  hp1_compressor_level.selected = 13;
  assert(queued(true, 1999, 13));
  assert(queued(true, 3999, 2));
#if OQ_TOPOLOGY_DUO
  assert(!queued(false, 1999, 17));  // The silent cap is shared; both HPs obey it.
#endif
  reset();
  oq_manual_hp_active = true;
  oq_control_mode_code = 100;
  oq_commissioning_task_code = oq_commissioning::TASK_MANUAL_HP;
  oq_day_max_frequency_hz.state = 30;
  assert(queued(true, 1999, 17));  // Genuine manual ownership bypasses the user cap only.
  hp1_compressor_level.selected = 20;
  assert(queued(true, 1999, 20));  // Manual service retains its validated physical range above 90 Hz.
  reset();
  hp1_runtime_frequency_revalidation_required = true;
  assert(!queued(true, 1999, 17));
  reset();
  hp1_is_online = false;
  assert(!queued(true, 1999, 17));
  reset();
  hp1_odu_generation_detection_complete = false;
  assert(!queued(true, 1999, 17));
  reset();
  hp1_generation_variant_code = static_cast<int>(oq_odu::Variant::V1);
  assert(!queued(true, 1999, 17));
  reset();
  hp1_runtime_frequency_snapshot_storage.fill(0);
  assert(!queued(true, 1999, 17));
  reset();
  const auto pdu = command(1999, 17);
  assert(oq_command_runtime::queued_address(1, pdu.data(), pdu.size()));
  hp1_compressor_level.selected = 13;
  assert(!oq_command_runtime::queued_address(1, pdu.data(), pdu.size()));
#if OQ_TOPOLOGY_DUO
  assert(oq_command_runtime::queued_address(2, pdu.data(), pdu.size()));
#endif
  assert(oq_command_runtime::queued_address(42, pdu.data(), pdu.size()));
}

void queued_positive_commands_lose_permission_after_a_technical_stop() {
  for (const bool hp1 : {true, false}) {
#if !OQ_TOPOLOGY_DUO
    if (!hp1) continue;
#endif
    for (int fault = 0; fault < 5; ++fault) {
      reset();
      const auto level = command(1999, 17), mode = command(3999, 2);
      assert(oq_command_runtime::queued_command(hp1, level.data(), level.size()));
      assert(oq_command_runtime::queued_command(hp1, mode.data(), mode.size()));
      if (fault == 0) oq_lowflow_fault_active = true;
      if (fault == 1) oq_water_temp_hard_trip_active = true;
      auto& incident = oq_incident_manager.outputs[hp1 ? 0 : 1];
      if (fault == 2) incident.must_stop = true;
      if (fault == 3) incident.run_state = oq_incidents::RunState::STOPPING;
      if (fault == 4) incident.run_state = oq_incidents::RunState::STOP_UNCONFIRMED;
      assert(!oq_command_runtime::queued_command(hp1, level.data(), level.size()));
      assert(!oq_command_runtime::queued_command(hp1, mode.data(), mode.size()));
      assert(queued(hp1, 1999, 0));
      assert(queued(hp1, 3999, 0));
    }
  }
}

void queued_heating_mode_rechecks_user_frequency_constraints() {
  reset();
  assert(queued(true, 3999, 2));
  oq_day_max_frequency_hz.state = 55;
  assert(!queued(true, 3999, 2));
  assert(hp1_compressor_command_revalidation_required);
  assert(!queued(true, 1999, 17));
  hp1_compressor_level.selected = 9;  // 55 Hz respects the updated user ceiling.
  assert(queued(true, 3999, 2));
  assert(queued(true, 1999, 9));
  reset();
  assert(queued(true, 3999, 2));
  hp1_excluded_frequency_min_hz.state = 85;
  hp1_excluded_frequency_max_hz.state = 90;
  assert(!queued(true, 3999, 2));
  assert(hp1_compressor_command_revalidation_required);
  assert(!queued(true, 1999, 17));
#if OQ_TOPOLOGY_DUO
  assert(queued(false, 3999, 2));
  assert(!hp2_compressor_command_revalidation_required);
#endif
  reset();
  oq_manual_hp_active = true;
  oq_control_mode_code = 100;
  oq_commissioning_task_code = oq_commissioning::TASK_MANUAL_HP;
  oq_day_max_frequency_hz.state = 30;
  hp1_excluded_frequency_min_hz.state = 85;
  hp1_excluded_frequency_max_hz.state = 90;
  assert(queued(true, 3999, 2));  // The same established manual exception applies to mode and level.
  assert(queued(true, 1999, 17));
  hp1_compressor_level.selected = 20;
  assert(queued(true, 3999, 2));
  assert(queued(true, 1999, 20));
}

void confirmed_dew_stop_revalidates_queued_cooling_without_changing_heating_or_manual() {
  for (const bool first : {true, false}) {
    if (!OQ_TOPOLOGY_DUO && !first) continue;
    reset();
    auto& mode = first ? hp1_set_working_mode : hp2_set_working_mode;
    auto& service = first ? hp1_odu_defrost : hp2_odu_defrost;
    mode.option = "Cooling";
    assert(queued(first, 3999, 1) && queued(first, 1999, 17));
    oq_cooling_stop_reason_code = oq_cooling::WATER_STOP_DEW;
    assert(!queued(first, 3999, 1) && !queued(first, 1999, 17));
    assert(!oq_command_runtime::guarded_write(first, 3999, 1));
    assert(!oq_command_runtime::guarded_write(first, 1999, 17));
    // The actuator commits STOPPING before its zero write. Dispatch must not
    // mutate queues while rejecting old positive frames or accepting retries.
    oq_incident_manager.outputs[first ? 0 : 1].run_state = oq_incidents::RunState::STOPPING;
    service.hold = true;
    const auto clears = service.queue_clear_calls;
    assert(queued(first, 3999, 0) && queued(first, 1999, 0));
    assert(service.queue_clear_calls == clears);
    reset();
    oq_cooling_stop_reason_code = oq_cooling::WATER_STOP_DEW;
    assert(queued(first, 3999, 2) && queued(first, 1999, 17));
    mode.option = "Cooling";
    oq_manual_hp_active = true;
    oq_control_mode_code = 100;
    oq_commissioning_task_code = oq_commissioning::TASK_MANUAL_HP;
    (first ? oq_manual_hp1_mode_code : oq_manual_hp2_mode_code) = 1;
    (first ? oq_manual_hp1_level : oq_manual_hp2_level).state = 17;
    assert(queued(first, 3999, 1) && queued(first, 1999, 17));
    oq_manual_hp_stop_requested = true;
    assert(!queued(first, 3999, 1) && !queued(first, 1999, 17));
    oq_manual_hp_stop_requested = false;
    (first ? oq_manual_hp1_mode_code : oq_manual_hp2_mode_code) = 0;
    assert(!queued(first, 3999, 1) && !queued(first, 1999, 17));
    (first ? oq_manual_hp1_mode_code : oq_manual_hp2_mode_code) = 1;
    (first ? oq_manual_hp1_level : oq_manual_hp2_level).state = 0.1f;
    assert(!queued(first, 3999, 1) && !queued(first, 1999, 17));
    (first ? oq_manual_hp1_level : oq_manual_hp2_level).state = NAN;
    assert(!queued(first, 3999, 1) && !queued(first, 1999, 17));
  }
}

#if OQ_TOPOLOGY_DUO
namespace dispatch = oq_power_house_dispatch;

constexpr int kHouseLevel = 17;
constexpr int kHouseFrequencyHz = 90;
constexpr float kSingleHeatW = 6000.0f;
constexpr float kDuoHeatW = 2.0f * kSingleHeatW;

int hp1_last_commanded_physical_level{}, hp2_last_commanded_physical_level{};
bool hp1_prev_heating_mode_active{}, hp2_prev_heating_mode_active{};
uint32_t hp1_last_real_heat_start_ms{}, hp2_last_real_heat_start_ms{};
uint32_t hp1_last_level_change_ms{}, hp2_last_level_change_ms{};
uint32_t hp1_last_start_ms{}, hp2_last_start_ms{}, hp1_last_stop_ms{}, hp2_last_stop_ms{};
uint32_t hp1_runtime_accum_ms{}, hp2_runtime_accum_ms{};
int hp1_minutes{}, hp2_minutes{};
FakeSensor hp1_working_mode{}, hp2_working_mode{};
uint32_t oq_ph_request_last_loop_ms{}, oq_curve_request_last_loop_ms{};
int oq_request_strategy_code{oq_request::STRATEGY_POWER_HOUSE};
int oq_request_hp1_level{}, oq_request_hp2_level{};
int oq_actuator_hp1_req{}, oq_actuator_hp2_req{};
int oq_request_mode_code{}, oq_request_owner_hp{}, oq_request_topology_code{};
FakeSelect oq_request_reason{};
int oq_heat_mode_code{}, oq_heat_last_mode_code{};
uint32_t oq_heat_last_loop_ms{};
int oq_ph_request_hp1_level{}, oq_ph_request_hp2_level{}, oq_ph_request_owner_hp{}, oq_ph_request_reason_code{};
int oq_curve_dispatch_hp1_level{}, oq_curve_dispatch_hp2_level{}, oq_curve_request_owner_hp{},
    oq_curve_capacity_mode_code{};
int oq_cooling_request_hp1_level{}, oq_cooling_request_hp2_level{}, oq_cooling_request_owner_hp{},
    oq_cooling_request_reason_code{};
int oq_cooling_demand_raw{}, oq_demand_raw{}, oq_demand_filtered{}, oq_power_cap_f{20};
FakeSelect oq_curve_control_profile{};
int oq_curve_regime_code{};
uint32_t oq_boot_startup_inhibit_until_ms{}, oq_duo_request_hold_until_ms{};
bool oq_boot_startup_inhibit_active{}, oq_boot_startup_inhibit_logged{}, oq_boot_startup_inhibit_cleared_logged{};
FakeSensor oq_min_runtime_min{};

struct TestRequestRuntime {
  struct TickConfig {
    uint32_t now_ms, tick_ms, curve_ms, power_house_ms, minimum_off_s;
    int minimum_runtime_floor_s, demand_max_f;
  };

  bool strategy_snapshot_initialized_{false}, guard_snapshot_initialized_{false};
  uint8_t startup_mask_{0}, last_guard_mask_{0};
  int last_cm_code_{-1}, last_strategy_hp1_{0}, last_strategy_hp2_{0};
  bool last_hp1_min_runtime_hold_{false}, last_hp2_min_runtime_hold_{false};
  std::string last_request_reason_;

  // These scenarios stay in automatic heating. Telemetry sinks have no hardware in the fixture.
  template <class... T>
  void run_manual_(T...) {
    assert(false);
  }
  template <class... T>
  void run_inactive_(T...) {
    assert(false);
  }
  template <class... T>
  void update_startup_event_(T...) {}
  template <class... T>
  void publish_debug_(T...) {}
  void publish_lead_(bool) {}
  void publish_optimizer_reason_(const char*) {}

#define id(value) value
#define OQ_REQUEST_SECONDARY_ID(suffix) id(hp2_##suffix)
#define ESP_LOGI(...) [](auto&&...) {}(__VA_ARGS__)
#include "oq_thermal_request_methods.inc"
#undef ESP_LOGI
#undef OQ_REQUEST_SECONDARY_ID
#undef id
};

namespace openquatt_decision_log {
enum : uint8_t {
  REASON_UNKNOWN,
  REASON_HP_FAULT,
  REASON_CANDIDATE_UNAVAILABLE,
  REASON_CANDIDATE_IN_REST,
  REASON_START_STOP_RATE_HIGH,
  REASON_NO_CANDIDATE,
  REASON_CANDIDATE_IN_DEFROST,
};
}  // namespace openquatt_decision_log

struct TestActuator {
  struct Cycle {
    struct {
      uint32_t now_ms;
    } config;
    oq_frequency_runtime::Context frequency;
    int request_mode_code;
    bool request_thermal_active;
    const char* request_mode_option;
    bool manual_service_active{false}, restart_by_minimum_off_time{false};
    struct {
      uint32_t remaining_ms{0};
      bool confirmation_pending{false};
    } cooling{};
    bool cooling_stop_planned{false}, cooling_stop_armed{false};
    uint8_t duo_defrost_start_block_mask{0};
    std::array<std::string, 2> service_guards{};
  };
  std::array<oq_thermal_actuator::CompressorStartLimit, 2> start_limits_{};
  std::array<uint32_t, 2> safe_write_ms{};
  std::array<bool, 2> cooling_confirmation{};

  uint32_t& last_safe_stop_write_ms(bool hp1) { return safe_write_ms[hp1 ? 0 : 1]; }
  bool& cooling_confirmation_pending_(bool hp1) { return cooling_confirmation[hp1 ? 0 : 1]; }
  static std::string& service_guard_(Cycle& cycle, bool hp1) { return cycle.service_guards[hp1 ? 0 : 1]; }
  static uint16_t cap_seconds_(uint32_t seconds) { return std::min(seconds, uint32_t{UINT16_MAX}); }
  void clear_retained_level(bool) {}
  oq_odu::RetainedLevel retained_level(bool, const oq_frequency_runtime::Context&) const { return {}; }
  void apply_active_mode_hold(bool, const oq_odu::RetainedLevel&, const char*) { assert(false); }
  void log_block_change(bool, const std::string&) {}
  void publish_defrost_hold_start(bool, bool, const oq_odu::RetainedLevel&, int) {}
  void publish_optimizer_reason(const char*) {}
  void publish_candidate_blocked(bool, uint8_t, int, uint16_t) {}
  void note_cooling_start_refuse_(bool, int, int, int, oq_thermal_actuator::PreflightBlock, uint16_t, const Cycle&) {}
  void emit_transition_(bool, bool, int, bool = false) const {}

#define id(value) value
#define OQ_ACTUATOR_SECONDARY_ID(suffix) id(hp2_##suffix)
#include "oq_thermal_actuator_methods.inc"
#undef OQ_ACTUATOR_SECONDARY_ID
#undef id
};

struct ModbusFrame {
  bool hp1;
  uint16_t address;
  uint16_t value;

  bool operator==(const ModbusFrame& other) const {
    return hp1 == other.hp1 && address == other.address && value == other.value;
  }
};

struct SimulatedModbus {
  struct Pump {
    uint16_t mode{0}, level{0};
  };
  struct Feedback {
    uint16_t mode;
    float hz;
  };
  std::array<Pump, 2> pumps{};
  std::vector<ModbusFrame> pending, sent, rejected;

  void attach(FakeSelect& select, bool first, uint16_t address) {
    select.option = address == 3999 ? "Standby" : "0";
    select.selected = 0;
    select.control = [this, &select, first, address](const std::string& option) {
      const bool mode_register = address == 3999;
      const auto mode_code = [](const std::string& mode) {
        assert(mode == "Standby" || mode == "Cooling" || mode == "Heating");
        return mode == "Heating" ? 2 : (mode == "Cooling" ? 1 : 0);
      };
      int value = mode_register ? mode_code(option) : std::stoi(option);
      bool safety_stop = false;
      if (!oq_command_runtime::guarded_write(first, address, value, false, &safety_stop)) {
        (first ? hp1_compressor_command_revalidation_required : hp2_compressor_command_revalidation_required) = true;
        if (!safety_stop) return;
        value = 0;
      }
      pending.push_back({first, address, static_cast<uint16_t>(value)});
      select.option = mode_register ? oq_request::request_mode_option(value) : std::to_string(value);
      select.selected = value;
    };
  }

  void deliver() {
    for (const auto& frame : pending) {
      const auto pdu = command(frame.address, frame.value);
      const uint8_t unit = frame.hp1 ? hp1.address : hp2.address;
      if (!oq_command_runtime::queued_address(unit, pdu.data(), pdu.size())) {
        rejected.push_back(frame);
        continue;
      }
      auto& heat_pump = pumps[frame.hp1 ? 0 : 1];
      if (frame.address == 3999)
        heat_pump.mode = frame.value;
      else
        heat_pump.level = frame.value;
      sent.push_back(frame);
    }
    pending.clear();
  }

  Feedback feedback(bool first) const {
    const auto& heat_pump = pumps[first ? 0 : 1];
    const bool running = heat_pump.mode > 0 && heat_pump.level > 0;
    const float hz =
        running ? oq_frequency_runtime::capture().automatic_frequency_hz(first, heat_pump.mode, heat_pump.level) : 0;
    return {heat_pump.mode, hz};
  }

  void report_mode(bool first, uint16_t mode) {
    auto& raw = first ? oq_sources::hp1 : oq_sources::hp2;
    raw.working_mode.observe(mode, test_now_ms, true);
    (first ? hp1_working_mode : hp2_working_mode).state = raw.working_mode.value;
    (first ? hp1_odu_defrost : hp2_odu_defrost).cycle.observe_mode(mode, test_now_ms);
    oq_incident_manager.observe_mode(first ? 1 : 2, mode, test_now_ms);
  }

  void report_frequency(bool first, float hz) {
    auto& raw = first ? oq_sources::hp1 : oq_sources::hp2;
    raw.compressor_frequency.observe(hz, test_now_ms, true);
    (first ? hp1_compressor_frequency : hp2_compressor_frequency).state = hz;
    oq_incident_manager.observe_frequency(first ? 1 : 2, hz, test_now_ms);
  }

  // A captured reply can be delivered later, independently of the current registers.
  void report(bool first, Feedback reply) {
    report_mode(first, reply.mode);
    report_frequency(first, reply.hz);
    (first ? hp1_odu_defrost : hp2_odu_defrost).cycle.observe_bit(0, test_now_ms);
  }
  void report(bool first) { report(first, feedback(first)); }
};

// Keep the synthetic performance table fixed so a transition can be traced
// from the thermal request to the actual Modbus queue callback.
struct PowerHouseDuo {
  static constexpr uint32_t kRequestCadenceMs = 30000;
  dispatch::DispatchState dispatch_state;
  oq_power_house::DemandState demand_state;
  TestRequestRuntime request;
  TestActuator actuator;
  SimulatedModbus bus;

  PowerHouseDuo() {
    hp1_last_applied_level = hp2_last_applied_level = 0;
    hp1_last_commanded_physical_level = hp2_last_commanded_physical_level = 0;
    hp1_prev_heating_mode_active = hp2_prev_heating_mode_active = false;
    hp1_last_real_heat_start_ms = hp2_last_real_heat_start_ms = 0;
    hp1_last_start_ms = hp2_last_start_ms = hp1_last_stop_ms = hp2_last_stop_ms = 0;
    hp1_last_level_change_ms = hp2_last_level_change_ms = 0;
    oq_ph_request_last_loop_ms = oq_curve_request_last_loop_ms = test_now_ms;
    oq_heat_last_loop_ms = 0;
    oq_min_runtime_min.state = 0;
    bus.attach(hp1_set_working_mode, true, 3999);
    bus.attach(hp2_set_working_mode, false, 3999);
    bus.attach(hp1_compressor_level, true, 1999);
    bus.attach(hp2_compressor_level, false, 1999);
    bus.report(true);
    bus.report(false);
  }

  oq_incidents::HpIncidentEngine& monitor_hp2(const oq_incidents::EngineTuning& tuning) {
    auto& incident = oq_incident_manager.engines[1].emplace(tuning);
    const uint32_t link_start_ms = test_now_ms;
    for (const uint32_t elapsed : {0U, tuning.link_recovery_ms / 2, tuning.link_recovery_ms}) {
      test_now_ms = link_start_ms + elapsed;
      incident.observe_link_round(test_now_ms, true);
      bus.report(false);
    }
    assert(incident.outputs().link_state == oq_incidents::LinkState::HEALTHY);
    assert(incident.outputs().stop_confirmed && incident.outputs().available_for_start);
    return incident;
  }

  float demand(float outside_c) {
    const oq_power_house::DemandInput input{test_now_ms, outside_c, -10.0f, 15.0f, 15000.0f,
                                            20.0f,       20.0f,     NAN,    1.0f,  false};
    const oq_power_house::DemandTuning tuning{0.5f, 1000.0f, 0.1f, 0.3f, 2.0f, 1.0f, NAN};
    const auto result = oq_power_house::decide_demand(input, tuning, demand_state);
    assert(result.valid);
    demand_state = result.next;
    return result.requested_w;
  }

  dispatch::DispatchDecision dispatch_tick(float watts) {
    dispatch::DispatchInput input{test_now_ms, watts, true, true, true, {}, {}};
    const bool pending_hp1 = oq_incident_manager.boot[0].pending(test_now_ms);
    const bool pending_hp2 = oq_incident_manager.boot[1].pending(test_now_ms);
    for (bool first : {true, false}) {
      auto& pump = first ? input.hp1 : input.hp2;
      auto& service = first ? hp1_odu_defrost : hp2_odu_defrost;
      const auto& boot = oq_incident_manager.boot[first ? 0 : 1];
      const auto incident = oq_incident_manager.get_outputs(first ? 1 : 2);
      const int previous = first ? hp1_last_applied_level : hp2_last_applied_level;
      const bool pending = first ? pending_hp1 : pending_hp2;
      const bool boot_hold = boot.protection_hold(test_now_ms);
      service.cycle.observe_mode((first ? hp1_working_mode : hp2_working_mode).state, test_now_ms);
      service.cycle.observe_bit(0, test_now_ms);
      pump.candidate = oq_hp_candidate::candidate_during_boot_resume(
          {previous, incident.available_for_start, incident.must_stop}, pending, boot.level(),
          pending_hp1 || pending_hp2, dispatch::kMaxLevel);
      pump.fixed_level = pending ? boot.level() : (boot_hold ? previous : 0);
      pump.stop_inhibited = pending || boot_hold;
      pump.levels[kHouseLevel] = {true, true, true, kSingleHeatW, 1800.0f, kHouseFrequencyHz};
    }
    dispatch_state = dispatch::observe_protection(
        dispatch_state, {test_now_ms, false, false, false, hp1_last_applied_level, hp2_last_applied_level});
    const auto decision = dispatch::decide_dispatch(input, {4500.0f, 5000.0f}, dispatch_state);
    assert(decision.output_valid);
    oq_ph_request_hp1_level = decision.hp1_level;
    oq_ph_request_hp2_level = decision.hp2_level;
    oq_ph_request_owner_hp = decision.owner_hp;
    oq_ph_request_reason_code = static_cast<int>(decision.reason);
    return decision;
  }

  void apply_request_tick(bool inputs_ready = true) {
    request.tick({test_now_ms, 5000, 15000, kRequestCadenceMs, 240, 0, 20});
    actuator.resume_boot_cycle_(true, inputs_ready, test_now_ms);
    actuator.resume_boot_cycle_(false, inputs_ready, test_now_ms);
    const int mode_code = oq_request_mode_code;
    TestActuator::Cycle cycle{{test_now_ms},
                              oq_frequency_runtime::capture(),
                              mode_code,
                              mode_code != 0,
                              oq_request::request_mode_option(mode_code)};
    const int applied_hp1 = actuator.apply_level_(true, oq_actuator_hp1_req, false, cycle);
    const int applied_hp2 = actuator.apply_level_(false, oq_actuator_hp2_req, false, cycle);
    actuator.record_transition(true, applied_hp1, test_now_ms, 0);
    actuator.record_transition(false, applied_hp2, test_now_ms, 0);
  }

  dispatch::DispatchDecision tick(float watts, bool inputs_ready = true) {
    const auto decision = dispatch_tick(watts);
    apply_request_tick(inputs_ready);
    return decision;
  }
};

void power_house_cancels_pending_duo_start_and_waits_for_new_feedback() {
  reset();
  PowerHouseDuo control;
  auto& bus = control.bus;
  assert(control.tick(kSingleHeatW).owner_hp == 1);
  bus.deliver();
  bus.report(true);
  assert(bus.pumps[0].mode == 2 && bus.pumps[0].level == kHouseLevel);

  test_now_ms += 5000;
  const auto duo = control.tick(kDuoHeatW);
  assert(duo.hp1_level == kHouseLevel && duo.hp2_level == kHouseLevel);
  const std::vector<ModbusFrame> pending_start{{false, 3999, 2}, {false, 1999, kHouseLevel}};
  assert(bus.pending == pending_start);
  assert(hp2_last_applied_level == kHouseLevel && bus.pumps[1].level == 0);

  test_now_ms += 5000;
  assert(control.tick(kSingleHeatW).owner_hp == 1);
  bus.sent.clear();
  bus.deliver();
  const std::vector<ModbusFrame> standby{{false, 3999, 0}, {false, 1999, 0}};
  assert(bus.sent == standby);
  assert(bus.rejected == pending_start);
  assert(hp2_last_applied_level == 0 && bus.pumps[1].mode == 0 && bus.pumps[1].level == 0);
  bus.report(false);

  test_now_ms += 5000;
  assert(control.tick(kDuoHeatW).hp2_level == kHouseLevel);
  assert(bus.pending == pending_start);
  bus.deliver();
  assert(bus.pumps[1].mode == 2 && bus.pumps[1].level == kHouseLevel);
  const uint32_t sent_ms = test_now_ms;
  const auto frequency = oq_odu::decode_runtime_frequency_snapshot(hp2_runtime_frequency_snapshot_storage);
  oq_compressor_feedback::Observer feedback;
  for (int step = 1; step <= 3; ++step) {
    test_now_ms += 5000;
    const bool confirmed = step == 3;
    if (confirmed) bus.report(false);
    assert(control.tick(kDuoHeatW).hp2_level == kHouseLevel);
    assert(bus.pending.empty());
    const auto& raw = oq_sources::hp2;
    const oq_compressor_feedback::Observation sample{
        test_now_ms,
        hp2_last_applied_level,
        static_cast<int>(raw.working_mode.value),
        raw.compressor_frequency.value,
        -5.0f,
        35.0f,
        raw.compressor_frequency.fresh(test_now_ms, oq_boot_resume::TELEMETRY_FRESH_MS),
        false,
        true,
        static_cast<uint32_t>(raw.compressor_frequency.received_ms),
        sent_ms,
    };
    const auto response = feedback.observe(frequency.heating.hz, hp2_generation_variant_code, sample);
    assert(response.missing == !confirmed);
    assert(response.actual_hz == (confirmed ? kHouseFrequencyHz : 0));
  }
  test_now_ms += dispatch::kTopologyHoldMs;
  assert(control.tick(kSingleHeatW).owner_hp == 1);
  bus.deliver();
  bus.report(false);
  assert(bus.pumps[0].level == kHouseLevel && bus.pumps[1].level == 0);
  assert(control.tick(0.0f).expected_w == 0.0f);
  bus.deliver();
  assert(bus.pumps[0].level == 0 && bus.pumps[1].level == 0);
  assert(bus.pumps[0].mode == 0 && bus.pumps[1].mode == 0);
}

void power_house_uses_odu_outside_when_ha_expires_and_returns() {
  using namespace oq_input_source;
  reset();
  PowerHouseDuo control;
  auto& bus = control.bus;
  TimedState ingress;
  HoldState hold;
  NumericSources sources;
  constexpr uint32_t stale_s = 180;
  const auto request_from_outside = [&](Source expected, float expected_demand_w) {
    const bool fresh = ha_live_valid_with_legacy(true, ingress, test_now_ms, stale_s);
    sources.ha = numeric_sample(fresh, true, -5.0f);
    sources.outdoor = numeric_sample(true, true, 5.0f);
    const auto selected = select_outside(Source::AUTO, sources, test_now_ms, 0, hold);
    assert(selected.valid && selected.route == expected);
    const float demand_w = control.demand(selected.value);
    assert(std::fabs(demand_w - expected_demand_w) < 1.0f);
    const auto decision = control.tick(demand_w);
    bus.deliver();
    bus.report(true);
    bus.report(false);
    return decision;
  };

  ingress.observe(test_now_ms);
  assert(request_from_outside(Source::HA, kDuoHeatW).expected_w == kDuoHeatW);
  assert(bus.pumps[0].level == kHouseLevel && bus.pumps[1].level == kHouseLevel);

  test_now_ms += stale_s * 1000;
  assert(request_from_outside(Source::HA, kDuoHeatW).hp2_level == kHouseLevel);
  ++test_now_ms;
  // The change is subject to the real demand slew, even though HA just expired.
  assert(request_from_outside(Source::OUTDOOR, kDuoHeatW).hp2_level == kHouseLevel);
  test_now_ms += 120000;
  const auto fallback = request_from_outside(Source::OUTDOOR, kSingleHeatW);
  assert(fallback.owner_hp == 1 && fallback.hp2_level == 0 && fallback.expected_w == kSingleHeatW);
  assert(bus.pumps[0].level == kHouseLevel && bus.pumps[1].level == 0);
  assert(bus.pumps[0].mode == 2 && bus.pumps[1].mode == 0);

  ++test_now_ms;
  ingress.observe(test_now_ms);
  assert(request_from_outside(Source::HA, kSingleHeatW).hp2_level == 0);
  test_now_ms += 120000;
  const auto recovered = request_from_outside(Source::HA, kDuoHeatW);
  assert(recovered.hp1_level == kHouseLevel && recovered.hp2_level == kHouseLevel);
  assert(bus.pumps[0].level == kHouseLevel && bus.pumps[1].level == kHouseLevel);
  assert(bus.rejected.empty());
}

void power_house_waits_for_boot_adoption_before_staging_the_peer() {
  for (bool running_hp1 : {true, false}) {
    prepare_running_boot(running_hp1);
    PowerHouseDuo control;
    auto& bus = control.bus;
    const unsigned running = running_hp1 ? 0 : 1;
    const unsigned idle = 1 - running;
    auto& boot = oq_incident_manager.boot[running];
    const auto& adopted_level = running_hp1 ? hp1_last_applied_level : hp2_last_applied_level;
    bus.pumps[running] = {2, kHouseLevel};
    (running_hp1 ? hp1_set_working_mode : hp2_set_working_mode).option = "Heating";
    (running_hp1 ? hp1_compressor_level : hp2_compressor_level).selected = kHouseLevel;
    bus.report(running_hp1);
    auto& raw = running_hp1 ? oq_sources::hp1 : oq_sources::hp2;
    raw.water_out = {};
    assert(!oq_boot_resume_runtime::capture(running_hp1).ready);
    const auto acquiring = control.tick(control.demand(-5.0f));
    assert(boot.pending(test_now_ms));
    assert(adopted_level == 0 && bus.pending.empty());
    assert(acquiring.owner_hp == (running_hp1 ? 1 : 2));
    assert(acquiring.expected_w == kSingleHeatW && acquiring.deficit_w == kSingleHeatW);
    raw.water_out.observe(35.0f, test_now_ms, true);
    assert(oq_boot_resume_runtime::capture(running_hp1).ready);
    control.tick(control.demand(-5.0f), false);
    assert(boot.pending(test_now_ms) && adopted_level == 0);
    assert(bus.pending.empty() && bus.pumps[idle].level == 0);

    test_now_ms += 5000;
    assert(control.tick(control.demand(-5.0f)).owner_hp == (running_hp1 ? 1 : 2));
    assert(adopted_level == kHouseLevel && boot.protection_hold(test_now_ms));
    assert(oq_ph_request_last_loop_ms == 0 && oq_curve_request_last_loop_ms == 0);
    assert(bus.pending.empty() && bus.pumps[idle].level == 0);

    ++test_now_ms;
    const auto resumed = control.tick(control.demand(-5.0f));
    assert(resumed.hp1_level == kHouseLevel && resumed.hp2_level == kHouseLevel);
    const std::vector<ModbusFrame> peer_start{{!running_hp1, 3999, 2}, {!running_hp1, 1999, kHouseLevel}};
    assert(bus.pending == peer_start);
    bus.deliver();
    assert(bus.sent == peer_start);
    bus.report(!running_hp1);
    assert(bus.pumps[running].level == kHouseLevel && bus.pumps[idle].level == kHouseLevel);
    assert(oq_incident_manager.starts[running] == 0 && oq_incident_manager.starts[idle] == 1);
    assert(bus.rejected.empty());
    assert(adopted_level == kHouseLevel && boot.protection_hold(test_now_ms));
  }
}

void power_house_start_timeout_blocks_late_hp2_confirmation() {
  using namespace oq_incidents;
  reset();
  const EngineTuning tuning;
  PowerHouseDuo control;
  auto& hp2_incident = control.monitor_hp2(tuning);
  auto& bus = control.bus;

  assert(control.tick(kSingleHeatW).owner_hp == 1);
  bus.deliver();
  bus.report(true);
  assert(bus.pumps[0].mode == 2 && bus.pumps[0].level == kHouseLevel);

  test_now_ms += 5000;
  const uint32_t start_ms = test_now_ms;
  assert(control.tick(kDuoHeatW).hp2_level == kHouseLevel);
  assert(hp2_incident.outputs().run_state == RunState::START_REQUESTED);
  bus.sent.clear();
  bus.deliver();
  const std::vector<ModbusFrame> hp2_start{{false, 3999, 2}, {false, 1999, kHouseLevel}};
  assert(bus.sent == hp2_start && oq_incident_manager.starts[1] == 1);
  const auto delayed_reply = bus.feedback(false);
  assert(delayed_reply.mode == 2 && delayed_reply.hz == kHouseFrequencyHz);

  // HP2 acknowledges Heating, but every delivered frequency reading remains
  // zero. Keep the link healthy so only the start watchdog can stop HP2.
  for (uint32_t elapsed_ms = 5000; elapsed_ms < tuning.start_timeout_ms; elapsed_ms += 5000) {
    test_now_ms = start_ms + elapsed_ms;
    hp2_incident.observe_link_round(test_now_ms, true);
    bus.report(false, {delayed_reply.mode, 0.0f});
    assert(hp2_incident.outputs().run_state == RunState::WAIT_COMPRESSOR);
    assert(!hp2_incident.outputs().start_timed_out && !hp2_incident.outputs().running_confirmed);
    assert(control.tick(kDuoHeatW).hp2_level == kHouseLevel && bus.pending.empty());
  }
  test_now_ms = start_ms + tuning.start_timeout_ms - 1;
  hp2_incident.tick(test_now_ms);
  assert(!hp2_incident.outputs().start_timed_out);

  ++test_now_ms;
  hp2_incident.tick(test_now_ms);
  const auto& timed_out = hp2_incident.outputs();
  assert(timed_out.link_state == LinkState::HEALTHY);
  assert(timed_out.start_timed_out && !timed_out.start_mode_ack_timed_out);
  assert(timed_out.must_stop && !timed_out.available_for_start &&
         timed_out.primary_incident_id == kStartFailedIncidentId);
  const auto single = control.tick(kDuoHeatW);
  assert(single.owner_hp == 1 && single.hp2_level == 0 && single.deficit_w == kSingleHeatW);
  assert(hp2_incident.outputs().run_state == RunState::STOPPING);
  bus.sent.clear();
  bus.deliver();
  const std::vector<ModbusFrame> hp2_stop{{false, 3999, 0}, {false, 1999, 0}};
  assert(bus.sent == hp2_stop && bus.rejected.empty());
  assert(bus.pumps[0].level == kHouseLevel && bus.pumps[1].mode == 0 && bus.pumps[1].level == 0);

  test_now_ms += 5000;
  bus.report(false);
  assert(!hp2_incident.outputs().stop_confirmed);
  test_now_ms += 5000;
  bus.report(false, delayed_reply);
  assert(oq_sources::hp2.compressor_frequency.value == kHouseFrequencyHz);
  assert(hp2_incident.outputs().run_state == RunState::STOPPING && !hp2_incident.outputs().running_confirmed);
  assert(hp2_incident.outputs().must_stop && !hp2_incident.outputs().available_for_start);
  assert(control.tick(kDuoHeatW).hp2_level == 0);
  bus.deliver();

  // The late positive sample interrupts the stop-confirmation streak. Two
  // new standby polls prove the stop, while the start failure stays latched.
  test_now_ms += 5000;
  bus.report(false);
  assert(!hp2_incident.outputs().stop_confirmed);
  test_now_ms += 5000;
  bus.report(false);
  assert(hp2_incident.outputs().stop_confirmed && hp2_incident.outputs().start_timed_out);
  assert(!hp2_incident.outputs().available_for_start);
  assert(hp2_incident.start_failure_reset_status() == StartFailureResetResult::READY);
  assert(control.tick(kDuoHeatW).hp2_level == 0);
  bus.deliver();
  assert(oq_incident_manager.starts[1] == 1);
  assert(std::all_of(bus.sent.begin(), bus.sent.end(),
                     [](const ModbusFrame& frame) { return !frame.hp1 && frame.value == 0; }));
  assert(bus.pumps[0].level == kHouseLevel && bus.pumps[1].mode == 0 && bus.pumps[1].level == 0);
}

void power_house_low_flow_cancels_a_queued_duo_start() {
  reset();
  PowerHouseDuo control;
  auto& bus = control.bus;
  const int minimum_runtime_s = 300;
  oq_min_runtime_min.state = minimum_runtime_s;

  assert(control.tick(kSingleHeatW).owner_hp == 1);
  bus.deliver();
  bus.report(true);
  assert(bus.pumps[0].mode == 2 && bus.pumps[0].level == kHouseLevel);

  test_now_ms += 5000;
  assert(control.tick(kDuoHeatW).hp2_level == kHouseLevel);
  const std::vector<ModbusFrame> hp2_start{{false, 3999, 2}, {false, 1999, kHouseLevel}};
  assert(bus.pending == hp2_start && bus.pumps[1].level == 0);
  const uint32_t duo_request_ms = test_now_ms;
  assert(oq_heat_last_loop_ms == duo_request_ms);

  test_now_ms += 5000;
  assert(test_now_ms - duo_request_ms < PowerHouseDuo::kRequestCadenceMs);
  assert(oq_request::min_runtime_window_active(test_now_ms, hp1_last_real_heat_start_ms, minimum_runtime_s * 1000U));
  // Feed the real request tick a flow fault before Power House publishes a new decision.
  flow_rate_selected.state = 0;
  oq_lowflow_fault_active = true;
  control.apply_request_tick();
  assert(oq_heat_last_loop_ms == test_now_ms);
  assert(oq_ph_request_hp1_level == kHouseLevel && oq_ph_request_hp2_level == kHouseLevel);
  assert(oq_actuator_hp1_req == 0 && oq_actuator_hp2_req == 0);
  bus.sent.clear();
  bus.deliver();
  const std::vector<ModbusFrame> safe_stop{{true, 3999, 0}, {true, 1999, 0}, {false, 3999, 0}, {false, 1999, 0}};
  assert(bus.sent == safe_stop && bus.rejected == hp2_start);
  assert(bus.pumps[0].mode == 0 && bus.pumps[0].level == 0);
  assert(bus.pumps[1].mode == 0 && bus.pumps[1].level == 0);
  bus.report(true);
  bus.report(false);

  // Re-evaluate the dropped demand while flow remains blocked. Clearing the
  // fault then exposes the current zero request, rather than the old Duo target.
  test_now_ms += PowerHouseDuo::kRequestCadenceMs;
  assert(control.tick(0).expected_w == 0.0f);
  bus.deliver();
  test_now_ms += 240000;  // Allow per-HP minimum off-time before the next request.
  bus.report(true);
  bus.report(false);
  flow_rate_selected.state = 1000;
  oq_lowflow_fault_active = false;
  control.apply_request_tick();
  assert(oq_actuator_hp1_req == 0 && oq_actuator_hp2_req == 0 && bus.pending.empty());
  assert(oq_incident_manager.starts[0] == 1 && oq_incident_manager.starts[1] == 1);

  test_now_ms += 5000;
  assert(control.tick(kSingleHeatW).owner_hp == 1);
  bus.deliver();
  assert(bus.pumps[0].mode == 2 && bus.pumps[0].level == kHouseLevel);
  assert(bus.pumps[1].mode == 0 && bus.pumps[1].level == 0 && oq_incident_manager.starts[1] == 1);
}

void power_house_stop_confirmation_requires_post_command_modbus_feedback() {
  using oq_incidents::RunState;
  enum class Reply { MODE, FREQUENCY };
  const std::initializer_list<Reply> orders[] = {
      {Reply::MODE, Reply::FREQUENCY, Reply::FREQUENCY},
      {Reply::FREQUENCY, Reply::MODE, Reply::FREQUENCY, Reply::FREQUENCY},
      {Reply::FREQUENCY, Reply::FREQUENCY, Reply::MODE, Reply::FREQUENCY, Reply::FREQUENCY},
      {Reply::MODE, Reply::MODE, Reply::FREQUENCY, Reply::FREQUENCY},
  };
  for (const auto& order : orders) {
    reset();
    PowerHouseDuo control;
    auto& hp2_incident = control.monitor_hp2({});
    auto& bus = control.bus;
    assert(control.tick(kDuoHeatW).hp2_level == kHouseLevel);
    bus.deliver();
    bus.report(true);
    assert(bus.pumps[1].mode == 2 && bus.pumps[1].level == kHouseLevel);

    // Standby/zero feedback received before the stop can be cached, but it
    // belongs to the preceding start command and must not confirm the stop.
    test_now_ms += 5000;
    bus.report(false, {0, 0.0f});
    assert(hp2_incident.outputs().run_state == RunState::WAIT_MODE);
    test_now_ms += 5000;
    assert(control.tick(kSingleHeatW).owner_hp == 1);
    bus.sent.clear();
    bus.deliver();
    const std::vector<ModbusFrame> hp2_stop{{false, 3999, 0}, {false, 1999, 0}};
    assert(bus.sent == hp2_stop);
    assert(hp2_incident.outputs().run_state == RunState::STOPPING && !hp2_incident.outputs().stop_confirmed);

    const auto stopped = bus.feedback(false);
    size_t delivered = 0;
    for (const auto part : order) {
      test_now_ms += 5000;
      if (part == Reply::MODE)
        bus.report_mode(false, stopped.mode);
      else
        bus.report_frequency(false, stopped.hz);
      const bool confirmed = ++delivered == order.size();
      assert(hp2_incident.outputs().stop_confirmed == confirmed);
      assert(hp2_incident.outputs().run_state == (confirmed ? RunState::STOPPED : RunState::STOPPING));
    }
    assert(bus.pumps[0].level == kHouseLevel && oq_incident_manager.starts[1] == 1);
  }
}

void power_house_restarts_hp2_only_after_confirmed_minimum_off_time() {
  using oq_incidents::RunState;
  constexpr uint32_t minimum_off_ms = 240000;
  constexpr uint32_t poll_ms = 5000;
  reset();
  PowerHouseDuo control;
  auto& bus = control.bus;
  auto& incident = control.monitor_hp2({});
  auto& guard = oq_incident_manager.restart_guards[1];
  assert(guard.configure(minimum_off_ms, FakeIncidentManager::kRestFeedbackFreshnessMs));

  const auto standby_poll = [&](uint32_t now_ms) {
    test_now_ms = now_ms;
    incident.observe_link_round(now_ms, true);
    bus.report(false);
  };
  const uint32_t idle_since_ms = test_now_ms;
  for (uint32_t elapsed = 0; elapsed <= minimum_off_ms + poll_ms; elapsed += poll_ms) {
    standby_poll(idle_since_ms + elapsed);
  }
  assert(guard.can_start(test_now_ms));
  assert(control.tick(kDuoHeatW).hp2_level == kHouseLevel);
  bus.deliver();
  bus.report(true);
  bus.report(false);
  assert(incident.outputs().running_confirmed && oq_incident_manager.starts[1] == 1);

  test_now_ms += poll_ms;
  assert(control.tick(kSingleHeatW).owner_hp == 1);
  bus.sent.clear();
  bus.deliver();
  const std::vector<ModbusFrame> hp2_stop{{false, 3999, 0}, {false, 1999, 0}};
  assert(bus.sent == hp2_stop && incident.outputs().run_state == RunState::STOPPING);
  const uint32_t stop_sent_ms = test_now_ms;

  const auto request_duo_and_keep_hp2_off = [&] {
    const auto decision = control.tick(kDuoHeatW);
    bus.deliver();
    assert(bus.pumps[0].mode == 2 && bus.pumps[0].level == kHouseLevel);
    assert(bus.pumps[1].mode == 0 && bus.pumps[1].level == 0);
    assert(hp2_last_applied_level == 0 && oq_incident_manager.starts[1] == 1);
    return decision;
  };

  test_now_ms += poll_ms;
  request_duo_and_keep_hp2_off();
  assert(!incident.outputs().stop_confirmed && guard.remaining_ms(test_now_ms) == minimum_off_ms);
  standby_poll(test_now_ms + poll_ms);
  request_duo_and_keep_hp2_off();
  assert(!incident.outputs().stop_confirmed && guard.remaining_ms(test_now_ms) == minimum_off_ms);
  standby_poll(test_now_ms + poll_ms);
  assert(incident.outputs().stop_confirmed && guard.remaining_ms(test_now_ms) == minimum_off_ms);
  const uint32_t stop_confirmed_ms = test_now_ms;
  const uint32_t release_ms = stop_confirmed_ms + minimum_off_ms;

  // New Standby/0 Hz replies keep the stop evidence valid. Sending Standby
  // and the first zero reading must not have started this timer earlier.
  for (uint32_t poll_at_ms = stop_confirmed_ms + poll_ms; poll_at_ms < release_ms; poll_at_ms += poll_ms) {
    standby_poll(poll_at_ms);
    const auto decision = request_duo_and_keep_hp2_off();
    assert(decision.owner_hp == 1 && decision.deficit_w == kSingleHeatW);
    if (poll_at_ms == stop_sent_ms + minimum_off_ms) {
      assert(oq_incident_manager.minimum_off_remaining_ms(2, test_now_ms) == stop_confirmed_ms - stop_sent_ms);
    }
  }
  test_now_ms = release_ms - 1;
  assert(oq_incident_manager.minimum_off_remaining_ms(2, test_now_ms) == 1);
  assert(request_duo_and_keep_hp2_off().hp2_level == 0);
  assert(std::all_of(bus.sent.begin(), bus.sent.end(),
                     [](const ModbusFrame& frame) { return !frame.hp1 && frame.value == 0; }));
  assert(bus.rejected.empty());

  ++test_now_ms;
  assert(guard.can_start(test_now_ms));
  assert(control.tick(kDuoHeatW).hp2_level == kHouseLevel);
  const std::vector<ModbusFrame> hp2_start{{false, 3999, 2}, {false, 1999, kHouseLevel}};
  assert(bus.pending == hp2_start);
  bus.sent.clear();
  bus.deliver();
  assert(bus.sent == hp2_start);
  bus.report(false);
  assert(incident.outputs().running_confirmed && bus.pumps[1].level == kHouseLevel);
  test_now_ms += poll_ms;
  assert(control.tick(kDuoHeatW).hp2_level == kHouseLevel && bus.pending.empty());
  assert(oq_incident_manager.starts[0] == 1 && oq_incident_manager.starts[1] == 2);
}
#endif

}  // namespace

int main() {
  confirmed_dew_stop_revalidates_queued_cooling_without_changing_heating_or_manual();
  boot_reads_hold_all_writes_and_abort_on_safety_loss();
  boot_adoption_requires_live_safety_and_comfort();
  ota_hold_blocks_writes_but_keeps_reads_and_releases_for_safety();
  shared_pump_hold_expires_and_releases_on_permission_loss();
  forced_defrost_requires_live_ownership_and_gates();
  queued_safety_zero_is_readonly_and_temperature_independent();
  queued_heating_rechecks_selected_level_and_frequency_policy();
  queued_positive_commands_lose_permission_after_a_technical_stop();
  queued_heating_mode_rechecks_user_frequency_constraints();
#if OQ_TOPOLOGY_DUO
  power_house_cancels_pending_duo_start_and_waits_for_new_feedback();
  power_house_uses_odu_outside_when_ha_expires_and_returns();
  power_house_waits_for_boot_adoption_before_staging_the_peer();
  power_house_start_timeout_blocks_late_hp2_confirmation();
  power_house_low_flow_cancels_a_queued_duo_start();
  power_house_stop_confirmation_requires_post_command_modbus_feedback();
  power_house_restarts_hp2_only_after_confirmed_minimum_off_time();
#endif
  return 0;
}
