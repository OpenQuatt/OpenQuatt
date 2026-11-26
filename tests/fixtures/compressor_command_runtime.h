#pragma once

#include <assert.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>

#include "components/openquatt_incident_manager/OpenQuattIncidentPolicy.h"
#include "openquatt/includes/control/oq_defrost_logic.h"
#include "openquatt/includes/control/oq_hp_boot_resume.h"
#include "openquatt/includes/control/oq_hp_restart_guard.h"
#include "openquatt/includes/odu/oq_odu_frequency_table.h"
#include "openquatt/includes/incidents/oq_hp_incident_engine.h"

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
  std::array<oq_incidents::DerivedOutputs, 2> outputs{};
  std::array<oq_hp_restart_guard::Policy, 2> restart_guards{};
  std::array<bool, 2> inhibited{};
  std::array<oq_boot_resume::Policy, 2> boot{};
  bool boot_resume_pending(unsigned hp, uint32_t now) const { return boot[hp - 1].pending(now); }
  bool boot_protection_hold(unsigned hp, uint32_t now) const { return boot[hp - 1].protection_hold(now); }
  int boot_resume_level(unsigned hp) const { return boot[hp - 1].level(); }
  int update_boot_resume(unsigned hp, bool abort, bool safety, bool inputs, bool adopt, uint32_t now) {
    return boot[hp - 1].update(now, abort || get_outputs(hp).must_stop, safety, inputs, adopt);
  }
  oq_incidents::DerivedOutputs get_outputs(unsigned hp) const {
    const unsigned slot = hp - 1;
    auto output = outputs[slot];
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
};

#ifdef OQ_TEST_INCIDENT_MANAGER_RUNTIME
#include "tests/fixtures/incident_manager_runtime.h"
using CommandFixtureIncidentManager = esphome::openquatt_incident_manager::TestIncidentManager;
#else
using CommandFixtureIncidentManager = FakeIncidentManager;
#endif

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
FakeSelect hp1_excluded_frequency_scope{"Always", 17, true, {}};
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
FakeSelect hp2_excluded_frequency_scope{"Always", 17, true, {}};
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
CommandFixtureIncidentManager oq_incident_manager{};
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
#include "openquatt/includes/control/oq_compressor_command_runtime.h"
#undef id

namespace compressor_command_fixture {

inline std::array<uint8_t, 5> command(uint16_t address, uint16_t value) {
  return {6, static_cast<uint8_t>(address >> 8), static_cast<uint8_t>(address), static_cast<uint8_t>(value >> 8),
          static_cast<uint8_t>(value)};
}

inline bool queued(bool is_hp1, uint16_t address, uint16_t value) {
  const auto pdu = command(address, value);
  const auto before_hp1 = hp1_odu_defrost.normal_write_calls;
  const auto before_hp2 = hp2_odu_defrost.normal_write_calls;
  const bool result = oq_command_runtime::queued_command(is_hp1, pdu.data(), pdu.size());
  assert(hp1_odu_defrost.normal_write_calls == before_hp1);
  assert(hp2_odu_defrost.normal_write_calls == before_hp2);
  assert(hp1_odu_defrost.queue_clear_calls == 0 && hp2_odu_defrost.queue_clear_calls == 0);
  return result;
}

inline void reset() {
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
  hp1_excluded_frequency_scope.option = hp2_excluded_frequency_scope.option = "Always";
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

inline void prepare_running_boot(bool hp1_running = true) {
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

}  // namespace compressor_command_fixture
