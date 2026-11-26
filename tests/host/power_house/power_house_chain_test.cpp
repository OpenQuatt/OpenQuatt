#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <vector>

#include "openquatt/includes/control/oq_compressor_feedback_logic.h"
#include "openquatt/includes/control/oq_compressor_start_limit.h"
#include "openquatt/includes/control/oq_heating_curve_logic.h"
#include "openquatt/includes/control/oq_incident_actuator_logic.h"
#include "openquatt/includes/control/oq_input_source_logic.h"
#include "openquatt/includes/control/oq_power_house_demand_logic.h"
#include "openquatt/includes/control/oq_power_house_dispatch_logic.h"
#include "openquatt/includes/control/oq_thermal_actuator_logic.h"
#include "openquatt/includes/control/oq_thermal_request_logic.h"
#define OQ_TEST_INCIDENT_MANAGER_RUNTIME
#include "tests/fixtures/compressor_command_runtime.h"
#undef OQ_TEST_INCIDENT_MANAGER_RUNTIME
#include "tests/fixtures/supervisory_low_load_stub.h"

namespace {

using compressor_command_fixture::command;
using compressor_command_fixture::prepare_running_boot;
using compressor_command_fixture::reset;

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
  REASON_PH_COMFORT_HOLD,
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
    bool ph_restart_blocked{false};
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

  oq_incidents::HpIncidentEngine& monitor_hp2(const oq_incidents::EngineTuning& tuning, uint32_t minimum_off_ms = 0) {
    auto& runtime = oq_incident_manager.runtime;
    auto& unit = runtime.units[1];
    unit.configured = true;
    unit.engine = oq_incidents::HpIncidentEngine(tuning);
    assert(unit.restart_guard.configure(minimum_off_ms, runtime.LINK_ROUND_TIMEOUT_MS));
    runtime.manual_reset_persistence_.mark_persist_success(0, test_now_ms);
    runtime.observe_transport(2, true, test_now_ms);
    auto& incident = unit.engine;
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
  auto& incident = control.monitor_hp2({}, minimum_off_ms);
  auto& runtime = oq_incident_manager.runtime;
  auto& guard = runtime.units[1].restart_guard;

  const auto standby_poll = [&](uint32_t now_ms) {
    test_now_ms = now_ms;
    incident.observe_link_round(now_ms, true);
    bus.report(false);
  };
  // A queued reply can reach the manager while transport is known to be offline.
  runtime.observe_transport(2, false, ++test_now_ms);
  bus.report(false);
  ++test_now_ms;
  assert(guard.remaining_ms(test_now_ms) == minimum_off_ms);
  runtime.observe_transport(2, true, test_now_ms);
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

  // Enough zero-frequency readings can prove the stop while the old Standby
  // sample is already too old to earn rest credit.
  test_now_ms += runtime.LINK_ROUND_TIMEOUT_MS + 1;
  incident.observe_link_round(test_now_ms, true);
  bus.report_frequency(false, 0.0F);
  assert(incident.outputs().stop_confirmed);
  ++test_now_ms;
  assert(guard.remaining_ms(test_now_ms) == minimum_off_ms);
  request_duo_and_keep_hp2_off();

  standby_poll(test_now_ms + poll_ms);
  assert(incident.outputs().stop_confirmed && guard.remaining_ms(test_now_ms) == minimum_off_ms);
  const uint32_t rest_confirmed_ms = test_now_ms;
  const uint32_t release_ms = rest_confirmed_ms + minimum_off_ms;

  test_now_ms += poll_ms;
  bus.report_frequency(false, 0.0F);
  assert(guard.snapshot_credit_ms(test_now_ms) == 0);

  // New Standby/0 Hz replies keep the rest evidence valid until release.
  for (uint32_t poll_at_ms = rest_confirmed_ms + 2 * poll_ms; poll_at_ms < release_ms; poll_at_ms += poll_ms) {
    standby_poll(poll_at_ms);
    const auto decision = request_duo_and_keep_hp2_off();
    assert(decision.owner_hp == 1 && decision.deficit_w == kSingleHeatW);
    assert(oq_incident_manager.minimum_off_remaining_ms(2, test_now_ms) == release_ms - poll_at_ms);
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

}  // namespace

int main() {
  power_house_cancels_pending_duo_start_and_waits_for_new_feedback();
  power_house_uses_odu_outside_when_ha_expires_and_returns();
  power_house_waits_for_boot_adoption_before_staging_the_peer();
  power_house_start_timeout_blocks_late_hp2_confirmation();
  power_house_low_flow_cancels_a_queued_duo_start();
  power_house_stop_confirmation_requires_post_command_modbus_feedback();
  power_house_restarts_hp2_only_after_confirmed_minimum_off_time();
  return 0;
}
