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
    void set_value(float value) { entity->state = value; }
    void set_option(const char* value) { entity->option = value; }
    void perform() {}
  };
  Call make_call() { return {this}; }
};
Entity boiler_active{};
Entity flow_rate_selected{};
Entity hp1_compressor_level{};
Entity hp1_working_mode{};
Entity hp2_compressor_level{};
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
Entity oq_flow_control_mode{};
Entity oq_flow_setpoint_lph{};
Entity oq_flow_kp{};
Entity oq_flow_ki{};
int oq_flow_last_good_pwm{};
int oq_flow_last_pwm{};
int oq_flow_autotune_pwm{};
float oq_flow_kp_suggested_value{};
float oq_flow_ki_suggested_value{};
std::string last_log;
template <typename... Args>
void log_message(const char*, const char* format, Args... args) {
  char buffer[512];
  if constexpr (sizeof...(args) == 0)
    snprintf(buffer, sizeof(buffer), "%s", format);
  else
    snprintf(buffer, sizeof(buffer), format, args...);
  last_log = buffer;
}
}  // namespace

#define id(name) name
#define OQ_TOPOLOGY_DUO 1
#define ESP_LOGI(...) log_message(__VA_ARGS__)
#define ESP_LOGW(...) log_message(__VA_ARGS__)
#include "../../openquatt/includes/service/tasks/oq_flow_autotune_logic.h"
#include "../../openquatt/includes/control/oq_flow_control_logic.h"

namespace {
const auto cfg = oq_flow_autotune::make_runtime_config(10, 50, 850, 5, 30, 30.0f, 30.0f, 0.001f, 2.0f, 0.0f, 0.1f);
uint32_t now_ms{};

void tick(oq_flow_autotune::FlowAutotuneRuntime& runtime, float pv) {
  flow_rate_selected.state = pv;
  now_ms += 10000;
  runtime.tick(cfg, now_ms);
}

void prepare(oq_flow_autotune::FlowAutotuneRuntime& runtime) {
  oq_commissioning::reset_task_runtime_state();
  oq_commissioning::clear_container(true);
  runtime.reset();
  oq_control_mode_code = 100;
  oq_cm_override.option = "CM100";
  oq_flow_setpoint_lph.state = 800;
  oq_flow_kp.state = 0.03f;
  oq_flow_ki.state = 0.0008f;
  oq_flow_kp_suggested_value = NAN;
  oq_flow_ki_suggested_value = NAN;
  oq_flow_last_pwm = 212;
  now_ms = 0;
  runtime.start(cfg, now_ms);
  tick(runtime, 550);
  assert(oq_flow_autotune_state == oq_flow_autotune::STATE_ARM);
}

void reach_step1(oq_flow_autotune::FlowAutotuneRuntime& runtime) {
  prepare(runtime);
  // Baseline fluctuation exceeds the old 12 L/h band, but is ordinary meter noise.
  for (float pv : {550.0f, 560.0f, 540.0f, 550.0f, 550.0f, 550.0f}) tick(runtime, pv);
  assert(oq_flow_autotune_state == oq_flow_autotune::STATE_STEP1);
  assert(oq_flow_autotune_pwm == 376);
}

void reach_validation_recover(oq_flow_autotune::FlowAutotuneRuntime& runtime) {
  reach_step1(runtime);
  for (int i = 0; i < 7; ++i) tick(runtime, i % 2 ? 582.156f : 579.066f);
  for (int i = 0; i < 6; ++i) tick(runtime, 551.874f);
  assert(oq_flow_autotune_state == oq_flow_autotune::STATE_STEP2);
  for (int i = 0; i < 7; ++i) tick(runtime, i % 2 ? 600.078f : 603.168f);
  assert(oq_flow_autotune_state == oq_flow_autotune::STATE_VALIDATE_RECOVER);
}

void expect_terminal(oq_flow_autotune::FlowAutotuneRuntime& runtime, const char* reason) {
  assert(oq_flow_autotune_status_value == reason);
  tick(runtime, 597);
  tick(runtime, 597);
  assert(oq_flow_autotune_status_value == reason);
  assert(!runtime.busy());
  assert(!oq_flow_autotune_active);
  assert(oq_commissioning_task_code == oq_commissioning::TASK_NONE);
  assert(oq_flow_kp.state == 0.03f && oq_flow_ki.state == 0.0008f);
  assert(oq_flow_setpoint_lph.state == 800);
  assert(isnan(oq_flow_kp_suggested_value) && isnan(oq_flow_ki_suggested_value));
}
}  // namespace

int main() {
  oq_flow_autotune::FlowAutotuneRuntime runtime;
  reach_step1(runtime);
  for (int i = 0; i < 7; ++i) tick(runtime, i % 2 ? 582.156f : 579.066f);
  assert(oq_flow_autotune_state == oq_flow_autotune::STATE_RECOVER);
  for (int i = 0; i < 6; ++i) tick(runtime, 551.874f);
  assert(oq_flow_autotune_state == oq_flow_autotune::STATE_STEP2);
  assert(oq_flow_autotune_pwm == 360);
  // Recorded #799 readings, not a reconstruction of the autotune sampling phase.
  const float recorded[] = {596.988f, 600.0779f, 593.8979f, 596.988f, 596.988f, 593.8979f,
                            596.988f, 596.988f,  600.0779f, 596.988f, 588.336f, 596.988f};
  for (float pv : recorded) {
    if (oq_flow_autotune_state != oq_flow_autotune::STATE_STEP2) break;
    tick(runtime, pv);
  }
  assert(oq_flow_autotune_state == oq_flow_autotune::STATE_VALIDATE_RECOVER);
  assert(oq_flow_autotune_status_value == "VALIDATION_RECOVER");
  assert(oq_flow_kp.state != 0.03f);
  // Cancelling validation must restore the real settings and create no suggestion.
  oq_flow_autotune_abort = true;
  tick(runtime, 597);
  expect_terminal(runtime, "ABORTED");

  // Second #799 recording: still approaching 800 L/h when the old 120 s limit expired.
  // Only this prefix is recorded; the subsequent noisy plateau is a synthetic continuation.
  reach_validation_recover(runtime);
  for (float pv : {600.0779f, 603.168f, 612.438f, 633.45f, 651.3719f, 672.384f, 690.924f, 690.924f, 711.936f, 732.9479f,
                   742.218f, 745.308f, 760.14f, 766.3199f}) {
    tick(runtime, pv);
    assert(oq_flow_autotune_state == oq_flow_autotune::STATE_VALIDATE_RECOVER);
  }
  for (int i = 0; i < 8 && oq_flow_autotune_state == oq_flow_autotune::STATE_VALIDATE_RECOVER; ++i)
    tick(runtime, i % 2 ? 794.01f : 790.92f);
  assert(oq_flow_autotune_state == oq_flow_autotune::STATE_VALIDATE);
  assert(oq_flow_setpoint_lph.state == 840);
  assert(isnan(oq_flow_kp_suggested_value));
  for (int i = 0; i < 30 && runtime.busy(); ++i) tick(runtime, i % 2 ? 843.09f : 840.0f);
  assert(!runtime.busy());
  assert(oq_flow_autotune_status_value.find("DONE (CLOSED-LOOP)") == 0);
  assert(!isnan(oq_flow_kp_suggested_value) && !isnan(oq_flow_ki_suggested_value));
  assert(oq_flow_kp.state == 0.03f && oq_flow_ki.state == 0.0008f);
  assert(oq_flow_setpoint_lph.state == 800);

  // The real PI intentionally leaves a 9 L/h offset alone; recovery must accept its plateau.
  reach_validation_recover(runtime);
  oq_flow_control::State pi;
  pi.sp_f = 800;
  const auto pi_result = oq_flow_control::update_pi(pi, {791, 800, 212, oq_flow_kp.state, oq_flow_ki.state, 10});
  assert(pi_result.error == 0 && pi_result.pwm == 212);
  for (int i = 0; i < 7; ++i) tick(runtime, 791);
  assert(oq_flow_autotune_state == oq_flow_autotune::STATE_VALIDATE);
  oq_flow_autotune_abort = true;
  tick(runtime, 791);
  expect_terminal(runtime, "ABORTED");

  // The minimum step must exceed the recovery/validation bands and the real PI deadband.
  reach_step1(runtime);
  oq_flow_setpoint_lph.state = 300;
  for (int i = 0; i < 7; ++i) tick(runtime, 579);
  for (int i = 0; i < 6; ++i) tick(runtime, 552);
  for (int i = 0; i < 7; ++i) tick(runtime, i % 2 ? 600.078f : 603.168f);
  for (int i = 0; i < 7; ++i) tick(runtime, 309);
  assert(oq_flow_autotune_state == oq_flow_autotune::STATE_VALIDATE);
  assert(oq_flow_setpoint_lph.state == 340);
  for (int i = 0; i < 30; ++i) tick(runtime, 309);
  assert(oq_flow_autotune_state == oq_flow_autotune::STATE_VALIDATE_RECOVER);
  assert(oq_flow_setpoint_lph.state == 300);
  assert(isnan(oq_flow_kp_suggested_value));
  oq_flow_autotune_abort = true;
  tick(runtime, 309);
  tick(runtime, 309);
  assert(!runtime.busy() && oq_flow_setpoint_lph.state == 300);
  assert(oq_flow_kp.state == 0.03f && oq_flow_ki.state == 0.0008f);

  // A wider recovery band still rejects an ongoing one-directional drift and a missed target.
  for (bool drift : {false, true}) {
    reach_validation_recover(runtime);
    for (int i = 0; i < 25; ++i) tick(runtime, drift ? 780 + i : 760);
    expect_terminal(runtime, "FAILED: VALIDATION_BASELINE");
  }
  for (float invalid : {0.0f, NAN}) {
    reach_validation_recover(runtime);
    tick(runtime, invalid);
    expect_terminal(runtime, "ABORT: FLOW_INVALID");
  }
  reach_validation_recover(runtime);
  oq_control_mode_code = 0;
  tick(runtime, 791);
  expect_terminal(runtime, "ABORT: not CM100");

  reach_step1(runtime);
  for (int i = 0; i < 13; ++i) tick(runtime, 580 + 20 * i);
  assert(oq_flow_autotune_state == oq_flow_autotune::STATE_ABORT);
  expect_terminal(runtime, "ABORT: NO_STEADY_STATE");

  // Slow ramps pass the wider short window, but never form a long plateau.
  for (float slope : {3.0f, -3.0f, 1.0f, -1.0f, 5.0f, 10.0f, -5.0f}) {
    reach_step1(runtime);
    for (int i = 0; i < 13; ++i) tick(runtime, 580 + slope * i);
    expect_terminal(runtime, "ABORT: NO_STEADY_STATE");
  }

  // Drifting initial and recovery baselines must never start a pump step.
  prepare(runtime);
  for (int i = 0; i < 7; ++i) tick(runtime, 500 + 15 * i);
  expect_terminal(runtime, "ABORT: NO_BASELINE_FLOW");
  reach_step1(runtime);
  for (int i = 0; i < 7; ++i) tick(runtime, 579);
  assert(oq_flow_autotune_state == oq_flow_autotune::STATE_RECOVER);
  for (int i = 0; i < 7; ++i) tick(runtime, 550 + 3 * i);
  expect_terminal(runtime, "ABORT: NO_BASELINE_FLOW");

  // Smooth decaying tails are practically settled despite remaining monotonic.
  prepare(runtime);
  for (int i = 1; i <= 7; ++i) tick(runtime, 550 + 30 * expf(-(float)i));
  assert(oq_flow_autotune_state == oq_flow_autotune::STATE_STEP1);
  for (int i = 1; i <= 13; ++i) {
    if (oq_flow_autotune_state != oq_flow_autotune::STATE_STEP1) break;
    tick(runtime, 550 + 70 * (1 - expf(-(float)i)));
  }
  assert(oq_flow_autotune_state == oq_flow_autotune::STATE_RECOVER);
  for (int i = 1; i <= 7; ++i) tick(runtime, 550 + 30 * expf(-(float)i));
  assert(oq_flow_autotune_state == oq_flow_autotune::STATE_STEP2);

  // t63 is measured against the final 620 L/h plateau, not the transient 570 L/h window.
  reach_step1(runtime);
  for (int i = 0; i < 13; ++i) tick(runtime, fminf(560 + 10 * i, 620));
  assert(oq_flow_autotune_state == oq_flow_autotune::STATE_RECOVER);
  assert(last_log.find("t63=44.2") != std::string::npos);
  assert(last_log.find("tau1=44s") != std::string::npos);
  const float smooth[] = {560, 570, 580, 590, 600, 610, 620};
  assert(fabsf(oq_flow_autotune::response_t63_s(smooth, 7, 550, 620, 10) - 44.24f) < 0.001f);

  // A real ramp followed by a noisy plateau must still complete within the deadline.
  reach_step1(runtime);
  for (float pv : {560.0f, 570.0f, 580.0f}) tick(runtime, pv);
  for (int i = 0; i < 8; ++i) {
    if (oq_flow_autotune_state != oq_flow_autotune::STATE_STEP1) break;
    tick(runtime, i % 2 ? 603.0f : 597.0f);
  }
  assert(oq_flow_autotune_state == oq_flow_autotune::STATE_RECOVER);

  // A single apparently steady window followed by drift is not a settled result at timeout.
  reach_step1(runtime);
  for (int i = 0; i < 3; ++i) tick(runtime, 579);
  assert(oq_flow_autotune_state == oq_flow_autotune::STATE_STEP1);
  for (int i = 0; i < 10; ++i) tick(runtime, 610 + 20 * i);
  expect_terminal(runtime, "ABORT: NO_STEADY_STATE");

  reach_step1(runtime);
  for (int i = 0; i < 7; ++i) tick(runtime, 500);
  assert(oq_flow_autotune_state == oq_flow_autotune::STATE_ABORT);
  expect_terminal(runtime, "FAILED: INVALID_GAIN");

  for (float invalid : {0.0f, NAN}) {
    reach_step1(runtime);
    tick(runtime, invalid);
    expect_terminal(runtime, "ABORT: FLOW_INVALID");
  }

  reach_step1(runtime);
  oq_control_mode_code = 0;
  tick(runtime, 579);
  expect_terminal(runtime, "ABORT: not CM100");

  prepare(runtime);
  for (int i = 0; i < 7; ++i) tick(runtime, 0);
  expect_terminal(runtime, "ABORT: NO_BASELINE_FLOW");

  // A nonstandard faster cadence fails closed instead of overrunning the bounded history.
  reach_step1(runtime);
  auto fast_cfg = cfg;
  fast_cfg.sample_time_s = 2;
  for (int i = 0; i < 33; ++i) {
    flow_rate_selected.state = 580 + 3 * i;
    now_ms += 2000;
    runtime.tick(fast_cfg, now_ms);
  }
  expect_terminal(runtime, "FAILED: INVALID_GAIN");

  // A new explicit run discards the previous terminal outcome.
  prepare(runtime);
  assert(oq_flow_autotune_status_value == "SETTLING");
}
