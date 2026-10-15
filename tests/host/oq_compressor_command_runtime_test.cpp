#include <assert.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

#include "../../openquatt/includes/control/oq_defrost_logic.h"
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
  std::string option{"Heating"};
  std::optional<size_t> selected{17};
  bool present{true};
  bool has_state() const { return present; }
  const std::string& current_option() const { return option; }
  std::optional<size_t> active_index() const { return selected; }
  void publish_state(const char* value) { option = value; }
};
struct FakeIncidentOutput {
  oq_incidents::RunState run_state{oq_incidents::RunState::RUNNING};
  bool must_stop{false};
  bool running_confirmed{true};
};
struct FakeIncidentManager {
  std::array<FakeIncidentOutput, 2> outputs{};
  std::array<bool, 2> inhibited{};
  FakeIncidentOutput get_outputs(unsigned hp) const { return outputs[hp - 1]; }
  bool startup_inhibited(unsigned hp) const { return inhibited[hp - 1]; }
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
  bool holding() const { return hold; }
  bool busy() const { return busy_value; }
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
FakeSensor oq_day_max_frequency_hz{};
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
  test_now_ms = 1000;
  oq_water_temp_hard_trip_active = oq_lowflow_fault_active = false;
  oq_commissioning_active = oq_manual_hp_active = false;
  oq_control_mode_code = 2;
  oq_commissioning_task_code = oq_commissioning::TASK_NONE;
  oq_incident_manager = {};
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
  oq_temperature_runtime::policies[0].reset();
  oq_temperature_runtime::policies[1].reset();
  assert(queued(true, 3999, 4));  // Every gate test starts from a currently authorized pending request.
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

void queued_heating_rechecks_selected_level_and_current_temperature() {
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
  oq_sources::hp1.water_in.observe(36, test_now_ms, true);  // Model38.5: cap falls90->72Hz.
  assert(!queued(true, 1999, 17));
  assert(!queued(true, 3999, 2));
  hp1_compressor_level.selected = 13;
  assert(queued(true, 1999, 13));
  assert(queued(true, 3999, 2));
#if OQ_TOPOLOGY_DUO
  assert(queued(false, 1999, 17));
  assert(!hp2_compressor_command_revalidation_required);
#endif
  reset();
  oq_manual_hp_active = true;
  oq_control_mode_code = 100;
  oq_commissioning_task_code = oq_commissioning::TASK_MANUAL_HP;
  oq_day_max_frequency_hz.state = 30;
  assert(queued(true, 1999, 17));  // Genuine manual ownership bypasses the user cap only.
  oq_sources::hp1.water_in.observe(36, test_now_ms, true);
  assert(!queued(true, 1999, 17));
  reset();
  hp1_runtime_frequency_revalidation_required = true;
  assert(!queued(true, 1999, 17));
  reset();
  oq_sources::hp1.water_in.observe(NAN, test_now_ms, false);
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
  oq_sources::hp1.water_in.observe(36, test_now_ms, true);
  assert(!queued(true, 3999, 2));
  assert(!queued(true, 1999, 17));
}

}  // namespace

int main() {
  forced_defrost_requires_live_ownership_and_gates();
  queued_safety_zero_is_readonly_and_temperature_independent();
  queued_heating_rechecks_selected_level_and_current_temperature();
  queued_heating_mode_rechecks_user_frequency_constraints();
  return 0;
}
