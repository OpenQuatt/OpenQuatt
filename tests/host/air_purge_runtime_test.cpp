#include <assert.h>
#include <math.h>
#include <stdint.h>

#include <optional>
#include <string>

// Minimal ESPHome entity stand-ins; exercise the real service runtime and cleanup.
namespace {
struct Entity {
  float state = 0;
  bool present = false;
  int index = 0;
  std::string option = "CM100";
  bool has_state() const { return present; }
  std::optional<size_t> active_index() const { return index; }
  std::string current_option() const { return option; }
  struct Call {
    Entity* entity;
    void set_option(const char* value) { entity->option = value; }
    void perform() {}
  };
  Call make_call() { return {this}; }
};
Entity boiler_active{};
Entity flow_rate_selected{};
Entity hp1_compressor_level{};
int hp1_last_applied_level{};
Entity hp1_working_mode{};
Entity hp2_compressor_level{};
int hp2_last_applied_level{};
Entity hp2_working_mode{};
int oq_air_purge_abort{};
int oq_air_purge_active{};
int oq_air_purge_phase{};
int oq_air_purge_remaining_s{};
Entity oq_air_purge_return_to_auto{};
uint32_t oq_air_purge_started_ms{};
int oq_air_purge_state{};
std::string oq_air_purge_status_value{};
int oq_air_purge_target_ipwm{};
std::string oq_boiler_power_test_status_value{};
Entity oq_cm_override{};
int oq_commissioning_abort_requested{};
int oq_commissioning_active{};
int oq_commissioning_boiler_request{};
int oq_commissioning_request_pending{};
uint32_t oq_commissioning_started_ms{};
int oq_commissioning_state_code{};
uint32_t oq_commissioning_state_since_ms{};
std::string oq_commissioning_status_value{};
int oq_commissioning_task_code{};
int oq_control_mode_code{};
int oq_flow_autotune_abort{};
int oq_flow_autotune_active{};
int oq_flow_autotune_req{};
int oq_flow_autotune_state{};
std::string oq_flow_autotune_status_value{};
int oq_hp_water_calibration_abort{};
int oq_hp_water_calibration_active{};
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
float oq_hp_water_calibration_result_supply_source_code{};
float oq_hp_water_calibration_result_supply_source_fingerprint{};
float oq_hp_water_calibration_spread_c{};
int oq_hp_water_calibration_stable_progress_s{};
int oq_hp_water_calibration_stable_required_s{};
uint32_t oq_hp_water_calibration_started_ms{};
std::string oq_hp_water_calibration_status_value{};
float oq_hp_water_calibration_supply_delta_c{};
int oq_hp_water_calibration_target_ipwm{};
int oq_manual_flow_active{};
std::string oq_manual_flow_status_value{};
int oq_manual_hp1_mode_code{};
int oq_manual_hp2_mode_code{};
int oq_manual_hp_active{};
std::string oq_manual_hp_guard_status_value{};
int oq_manual_hp_mode_allowed{};
std::string oq_manual_hp_status_value{};
int oq_manual_hp_stop_requested{};
int oq_quick_flow_test_active{};
int oq_quick_flow_test_remaining_s{};
uint32_t oq_quick_flow_test_started_ms{};
int oq_water_temp_hard_trip_active{};
}  // namespace

#define id(name) name
#define OQ_TOPOLOGY_DUO 1
#define ESP_LOGI(...) ((void)0)
#include "../../openquatt/includes/service/tasks/oq_air_purge_logic.h"

namespace {
const auto cfg = oq_air_purge::make_runtime_config(5);

void prepare(oq_air_purge::AirPurgeRuntime& runtime) {
  oq_commissioning::reset_task_runtime_state();
  oq_commissioning::clear_container(true);
  runtime.reset();
  oq_control_mode_code = 100;
  oq_water_temp_hard_trip_active = false;
  boiler_active.state = 0;
  hp1_compressor_level = {};
  hp2_compressor_level = {};
  hp1_last_applied_level = 0;
  hp2_last_applied_level = 0;
  hp1_working_mode.state = 0;
  hp2_working_mode.state = 0;
  flow_rate_selected.state = 0;
  oq_air_purge_return_to_auto.state = 0;
  oq_cm_override.present = true;
  oq_cm_override.option = "CM100";
}

void expect_terminal(const char* status, int state) {
  assert(oq_air_purge_status_value == status);
  assert(!oq_air_purge_active);
  assert(oq_air_purge_state == state);
  assert(oq_air_purge_phase == 0);
  assert(oq_air_purge_target_ipwm == 1000);
  assert(oq_air_purge_remaining_s == 0);
  assert(oq_commissioning_task_code == oq_commissioning::TASK_NONE);
  assert(oq_commissioning_state_code == state);
}

void test_issue_779_pulse_replay_completes() {
  oq_air_purge::AirPurgeRuntime runtime;
  prepare(runtime);
  runtime.start(cfg, 1000);
  // 60s quiet, then 25s hard / 15s rest. Telemetry at the transition
  // still contains the previous pulse's flow, as in the reported log.
  for (int elapsed = 5; elapsed <= 300; elapsed += 5) {
    const int cycle_pos = (elapsed - 60) % 40;
    flow_rate_selected.state = elapsed >= 60 && elapsed < 240 && cycle_pos > 5 && cycle_pos <= 25 ? 670 : 0;
    if (elapsed >= 245) flow_rate_selected.state = 200;
    runtime.tick(cfg, 1000 + elapsed * 1000);
    if (elapsed == 135) {
      assert(oq_air_purge_active);
      assert(oq_air_purge_target_ipwm == 800);
      assert(flow_rate_selected.state == 0);
    }
    if (elapsed < 300) assert(oq_air_purge_active);
  }
  expect_terminal("DONE", oq_air_purge::STATE_DONE);
  assert(oq_commissioning_active);  // Stay in CM100 when return_auto is off.
}

void test_no_flow_and_invalid_values_fail_at_boundary() {
  for (float flow : {0.0f, 19.9f, -1.0f, NAN, INFINITY}) {
    oq_air_purge::AirPurgeRuntime runtime;
    prepare(runtime);
    runtime.start(cfg, 1000);
    flow_rate_selected.state = flow;
    runtime.tick(cfg, 120999);
    assert(oq_air_purge_active);
    runtime.tick(cfg, 121000);
    expect_terminal("FAILED: no flow detected", oq_air_purge::STATE_FAILED);
    assert(oq_commissioning_active);
    assert(oq_cm_override.option == "CM100");
  }
}

void test_later_loss_and_delayed_completion_fail() {
  oq_air_purge::AirPurgeRuntime runtime;
  prepare(runtime);
  runtime.start(cfg, 1000);
  flow_rate_selected.state = 20;  // Inclusive threshold.
  runtime.tick(cfg, 101000);
  flow_rate_selected.state = 0;
  runtime.tick(cfg, 220999);
  assert(oq_air_purge_active);
  runtime.tick(cfg, 221000);
  expect_terminal("FAILED: no flow detected", oq_air_purge::STATE_FAILED);

  prepare(runtime);
  runtime.start(cfg, 1000);
  runtime.tick(cfg, 301000);  // Skipped ticks must not turn no flow into success.
  expect_terminal("FAILED: no flow detected", oq_air_purge::STATE_FAILED);
}

void test_new_session_does_not_inherit_flow() {
  oq_air_purge::AirPurgeRuntime runtime;
  prepare(runtime);
  runtime.start(cfg, 1000);
  flow_rate_selected.state = 670;
  runtime.tick(cfg, 101000);
  runtime.abort_or_clear();
  runtime.tick(cfg, 106000);
  expect_terminal("ABORTED", oq_air_purge::STATE_ABORT);
  flow_rate_selected.state = 0;
  runtime.start(cfg, 201000);
  runtime.tick(cfg, 320999);
  assert(oq_air_purge_active);
  runtime.tick(cfg, 321000);
  expect_terminal("FAILED: no flow detected", oq_air_purge::STATE_FAILED);
  runtime.abort_or_clear();
  assert(oq_air_purge_status_value == "IDLE");
  runtime.start(cfg, 401000);
  runtime.tick(cfg, 521000);
  expect_terminal("FAILED: no flow detected", oq_air_purge::STATE_FAILED);
}

void test_refused_start_does_not_extend_running_timeout() {
  oq_air_purge::AirPurgeRuntime runtime;
  prepare(runtime);
  runtime.start(cfg, 1000);
  runtime.tick(cfg, 101000);
  runtime.start(cfg, 111000);
  assert(oq_air_purge_status_value == "REFUSED: BUSY");
  assert(oq_air_purge_active);
  assert(oq_air_purge_started_ms == 1000);
  runtime.tick(cfg, 121000);
  expect_terminal("FAILED: no flow detected", oq_air_purge::STATE_FAILED);
}

void test_safety_stops_take_precedence() {
  for (int stop = 0; stop < 4; ++stop) {
    oq_air_purge::AirPurgeRuntime runtime;
    prepare(runtime);
    oq_air_purge_return_to_auto.state = 1;
    runtime.start(cfg, 1000);
    flow_rate_selected.state = 670;
    if (stop == 0) oq_water_temp_hard_trip_active = true;
    if (stop == 1) boiler_active.state = 1;
    if (stop == 2) oq_control_mode_code = 0;
    if (stop == 3) oq_commissioning_abort_requested = true;
    runtime.tick(cfg, 301000);
    expect_terminal(stop == 0   ? "FAILED: water temperature hard trip"
                    : stop == 1 ? "FAILED: boiler active"
                    : stop == 2 ? "ABORT: not CM100"
                                : "ABORTED",
                    stop < 2 ? oq_air_purge::STATE_FAILED : oq_air_purge::STATE_ABORT);
    assert(oq_cm_override.option == "CM100");  // Never return to Auto after failure/abort.
  }
}

void test_success_return_auto_and_millis_rollover() {
  oq_air_purge::AirPurgeRuntime runtime;
  prepare(runtime);
  const uint32_t started = UINT32_MAX - 60000;
  runtime.start(cfg, started);
  flow_rate_selected.state = 670;
  runtime.tick(cfg, started + 85000U);
  flow_rate_selected.state = 0;
  runtime.tick(cfg, started + 204999U);
  assert(oq_air_purge_active);
  runtime.tick(cfg, started + 205000U);
  expect_terminal("FAILED: no flow detected", oq_air_purge::STATE_FAILED);

  prepare(runtime);
  oq_air_purge_return_to_auto.state = 1;
  runtime.start(cfg, 1000);
  flow_rate_selected.state = 670;
  runtime.tick(cfg, 301000);
  expect_terminal("DONE", oq_air_purge::STATE_DONE);
  assert(oq_cm_override.option == "Auto");
  assert(!oq_commissioning_active);
}
}  // namespace

int main() {
  test_issue_779_pulse_replay_completes();
  test_no_flow_and_invalid_values_fail_at_boundary();
  test_later_loss_and_delayed_completion_fail();
  test_new_session_does_not_inherit_flow();
  test_refused_start_does_not_extend_running_timeout();
  test_safety_stops_take_precedence();
  test_success_return_auto_and_millis_rollover();
}
