#include "tests/fixtures/compressor_command_runtime.h"

namespace {

using compressor_command_fixture::command;
using compressor_command_fixture::prepare_running_boot;
using compressor_command_fixture::queued;
using compressor_command_fixture::reset;

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

void pending_heating_commands_follow_quiet_hours_and_each_units_scope() {
  reset();
  oq_day_max_frequency_hz.state = oq_silent_max_frequency_hz.state = 90;
  hp1_excluded_frequency_min_hz.state = hp1_excluded_frequency_max_hz.state = 90;
  hp1_excluded_frequency_scope.option = "Quiet hours";
#if OQ_TOPOLOGY_DUO
  hp2_excluded_frequency_min_hz.state = hp2_excluded_frequency_max_hz.state = 90;
  hp2_excluded_frequency_scope.option = "Off";
#endif
  assert(oq_frequency_runtime::capture().frequency_allowed(true, 2, 17));
  assert(queued(true, 3999, 2) && queued(true, 1999, 17));

  oq_silent_active.state = true;
  assert(!oq_frequency_runtime::capture().frequency_allowed(true, 2, 17));
  assert(oq_frequency_runtime::capture(false).frequency_allowed(true, 2, 17));
  assert(!queued(true, 3999, 2) && !queued(true, 1999, 17));
  assert(hp1_compressor_command_revalidation_required);
  assert(queued(true, 3999, 0) && queued(true, 1999, 0));
#if OQ_TOPOLOGY_DUO
  assert(oq_frequency_runtime::capture().frequency_allowed(false, 2, 17));
  assert(queued(false, 3999, 2) && queued(false, 1999, 17));
#endif
  oq_silent_active.state = false;
  assert(oq_frequency_runtime::capture().frequency_allowed(true, 2, 17));
  assert(queued(true, 3999, 2) && queued(true, 1999, 17));

  hp1_excluded_frequency_scope.option = "";
  assert(!queued(true, 1999, 17));  // Legacy or uninitialized scope still enforces the configured band.
  hp1_excluded_frequency_scope.option = "Off";
  assert(queued(true, 1999, 17));
  assert(hp1_excluded_frequency_max_hz.state == 90);
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
  pending_heating_commands_follow_quiet_hours_and_each_units_scope();
  return 0;
}
