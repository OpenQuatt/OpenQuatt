#include <assert.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

#include "../../openquatt/includes/odu/oq_odu_frequency_table.h"

#ifndef OQ_TOPOLOGY_DUO
#define OQ_TOPOLOGY_DUO 1
#endif

struct FakeSensor {
  float state = NAN;
  bool present = true;
  bool has_state() const { return present; }
};
struct FakeSelect {
  std::string option = "V1.5";
  bool has_state() const { return true; }
  const std::string& current_option() const { return option; }
};
struct FakeIncidentOutput {
  enum class LinkState { ONLINE, SUSPECT };
  bool available_for_start = true, must_stop = false;
  LinkState link_state = LinkState::ONLINE;
};
struct FakeIncidentManager {
  FakeIncidentOutput hp1, hp2;
  FakeIncidentOutput get_outputs(int index) const { return index == 1 ? hp1 : hp2; }
};

FakeSelect hp_generation;
FakeIncidentManager oq_incident_manager;
FakeSensor oq_day_max_frequency_hz{90}, oq_silent_max_frequency_hz{55}, oq_silent_active{0};
FakeSensor hp1_excluded_frequency_min_hz{0}, hp1_excluded_frequency_max_hz{0};
FakeSensor hp2_excluded_frequency_min_hz{0}, hp2_excluded_frequency_max_hz{0};
FakeSensor oq_system_supply_temp{35}, outside_temp_selected{7};
FakeSensor hp1_water_out_temp{35}, hp2_water_out_temp{35};
FakeSensor hp1_4_way_valve{0}, hp2_4_way_valve{0};
float oq_power_limit_soft_w = 10000, oq_power_limit_peak_w = 12000;
float oq_P_hp_cap_w = 0;
int oq_control_mode_code = 2, oq_heat_mode_code = 0;
int hp1_last_applied_level = 0, hp2_last_applied_level = 0;
uint32_t hp1_last_stop_ms = 0, hp2_last_stop_ms = 0;
uint32_t hp1_water_out_temp_last_update_ms = 900000, hp2_water_out_temp_last_update_ms = 900000;
bool hp1_is_online = true, hp2_is_online = true;
bool hp1_odu_generation_detection_complete = true, hp2_odu_generation_detection_complete = true;
uint8_t hp1_generation_variant_code = static_cast<uint8_t>(oq_odu::Variant::V1_5);
uint8_t hp2_generation_variant_code = static_cast<uint8_t>(oq_odu::Variant::V1_5);
bool oq_cold_start_session_active = false, oq_cold_start_hp_blocked = false;
oq_odu::RuntimeFrequencySnapshotStorage hp1_runtime_frequency_snapshot_storage{};
oq_odu::RuntimeFrequencySnapshotStorage hp2_runtime_frequency_snapshot_storage{};
uint32_t test_now_ms = 900000;
uint32_t millis() { return test_now_ms; }

#define id(value) value
#include "../../openquatt/includes/diagnostics/oq_heating_capacity_runtime.h"
#undef id

namespace {
float capacity() { return oq_heating_capacity::current_estimate(240000, 60000, 0.764f); }
void near(float actual, float expected) { assert(std::fabs(actual - expected) < 0.1f); }
void initialize_frequency() {
  oq_odu::RuntimeFrequencySnapshot snapshot;
  snapshot.variant = oq_odu::Variant::V1_5;
  snapshot.heating.level_count = 11;
  snapshot.heating.valid = true;
  for (int level = 1; level <= 10; ++level)
    snapshot.heating.hz[level] = static_cast<uint8_t>(oq_perf::V1_HEATING_FREQUENCIES_HZ[level - 1]);
  hp1_runtime_frequency_snapshot_storage = oq_odu::encode_runtime_frequency_snapshot(snapshot);
  hp2_runtime_frequency_snapshot_storage = hp1_runtime_frequency_snapshot_storage;
}
}  // namespace

int main() {
  initialize_frequency();
  const float solo_max = oq_perf::predict_heating_hz(oq_odu::Variant::V1_5, 90, 7, 35).pth_w;
  const float max = capacity();
  near(max, solo_max * (OQ_TOPOLOGY_DUO ? 2 : 1));
  assert(max > 0);
  // The sensor is independent of demand and the reset Power House telemetry.
  oq_heat_mode_code = 1;
  oq_P_hp_cap_w = 0;
  near(capacity(), max);
  oq_heat_mode_code = 0;
  oq_P_hp_cap_w = 123;
  near(capacity(), max);
  assert(oq_P_hp_cap_w == 123 && hp1_last_applied_level == 0 && hp2_last_applied_level == 0);

  oq_silent_active.state = 1;
  const float silent = capacity();
  assert(silent > 0 && silent < max);
  oq_silent_active.state = 0;
  hp1_excluded_frequency_min_hz.state = 61;
  hp1_excluded_frequency_max_hz.state = 90;
  assert(capacity() < max);
  hp1_excluded_frequency_min_hz.state = hp1_excluded_frequency_max_hz.state = 0;
  oq_power_limit_soft_w = 900;
  oq_power_limit_peak_w = 1000;
  assert(capacity() >= 0 && capacity() < max);
  oq_power_limit_soft_w = 10000;
  oq_power_limit_peak_w = 12000;

  oq_incident_manager.hp1.must_stop = true;
  near(capacity(), OQ_TOPOLOGY_DUO ? solo_max : 0);
  oq_incident_manager.hp2.must_stop = true;
  near(capacity(), 0);
  oq_incident_manager.hp1.must_stop = oq_incident_manager.hp2.must_stop = false;
  hp1_last_stop_ms = test_now_ms - 239999;
  near(capacity(), OQ_TOPOLOGY_DUO ? solo_max : 0);
  hp1_last_stop_ms = test_now_ms - 240000;
  near(capacity(), max);
  hp1_last_stop_ms = 0;

  // Missing/stale temperatures, models and runtime tables never invent 0 W.
  outside_temp_selected.state = NAN;
  assert(std::isnan(capacity()));
  outside_temp_selected.state = 7;
  oq_system_supply_temp.state = NAN;
#if OQ_TOPOLOGY_DUO
  assert(std::isnan(capacity()));  // Do not substitute one outlet for a serveable Duo.
  oq_incident_manager.hp2.must_stop = true;
#endif
  near(capacity(), solo_max);
  hp1_water_out_temp_last_update_ms = test_now_ms - 60001;
  assert(std::isnan(capacity()));
  hp1_water_out_temp_last_update_ms = test_now_ms - 60000;
  near(capacity(), solo_max);
  hp1_is_online = false;
  assert(std::isnan(capacity()));
  hp1_is_online = true;
  oq_system_supply_temp.state = 35;
  oq_incident_manager.hp2.must_stop = false;
  hp1_odu_generation_detection_complete = false;
  assert(std::isnan(capacity()));
  hp1_odu_generation_detection_complete = true;
  hp1_runtime_frequency_snapshot_storage.fill(0);
  assert(std::isnan(capacity()));
  initialize_frequency();
#if OQ_TOPOLOGY_DUO
  hp2_odu_generation_detection_complete = false;
  assert(std::isnan(capacity()));  // Do not silently publish a partial Duo estimate.
  oq_incident_manager.hp2.must_stop = true;
  near(capacity(), solo_max);
  hp2_odu_generation_detection_complete = true;
  oq_incident_manager.hp2.must_stop = false;
#endif
  oq_day_max_frequency_hz.state = NAN;
  assert(std::isnan(capacity()));
  oq_day_max_frequency_hz.state = 10;
  near(capacity(), 0);  // A known cap below all available frequencies is genuine zero.
  oq_day_max_frequency_hz.state = 90;
  hp1_excluded_frequency_min_hz.state = NAN;
  assert(std::isnan(capacity()));
  hp1_excluded_frequency_min_hz.state = 0;
  oq_incident_manager.hp1.link_state = FakeIncidentOutput::LinkState::SUSPECT;
  assert(std::isnan(capacity()));
  oq_incident_manager.hp1.link_state = FakeIncidentOutput::LinkState::ONLINE;
  oq_power_limit_peak_w = NAN;
  assert(std::isnan(capacity()));
  oq_power_limit_peak_w = 12000;
  oq_control_mode_code = 5;
  assert(std::isnan(capacity()));
  oq_control_mode_code = 2;
  hp1_4_way_valve.state = 1;
  near(capacity(), solo_max * (0.764f + (OQ_TOPOLOGY_DUO ? 1 : 0)));
  hp1_4_way_valve.state = 0;
  near(capacity(), max);  // Invalid -> valid recovers immediately, without retained state.

  // Partly masked model rows and out-of-map frequencies retain valid lower levels.
  outside_temp_selected.state = 12;
  assert(capacity() > 0);
  outside_temp_selected.state = 7;
  oq_odu::RuntimeFrequencySnapshot out_of_map_snapshot =
      oq_odu::decode_runtime_frequency_snapshot(hp1_runtime_frequency_snapshot_storage);
  out_of_map_snapshot.heating.hz[10] = 95;
  hp1_runtime_frequency_snapshot_storage = oq_odu::encode_runtime_frequency_snapshot(out_of_map_snapshot);
  oq_day_max_frequency_hz.state = 120;
  assert(capacity() > 0);
  oq_day_max_frequency_hz.state = 90;
  initialize_frequency();

  // V2 old/new models and an extended runtime table also use this sensor.
  hp_generation.option = "V2";
  for (const auto variant : {oq_odu::Variant::V2_OLD_MODEL, oq_odu::Variant::V2_NEW_MODEL}) {
    oq_odu::RuntimeFrequencySnapshot snapshot;
    snapshot.variant = variant;
    snapshot.heating.level_count = variant == oq_odu::Variant::V2_NEW_MODEL ? 21 : 11;
    snapshot.heating.valid = true;
    for (int level = 1; level < snapshot.heating.level_count; ++level)
      snapshot.heating.hz[level] = static_cast<uint8_t>(20 + level * 3);
    if (variant == oq_odu::Variant::V2_OLD_MODEL) {
      const uint8_t frequencies[] = {20, 26, 30, 48, 55, 61, 72, 80, 85, 90};
      for (int level = 1; level <= 10; ++level) snapshot.heating.hz[level] = frequencies[level - 1];
    }
    hp1_generation_variant_code = hp2_generation_variant_code = static_cast<uint8_t>(variant);
    hp1_runtime_frequency_snapshot_storage = hp2_runtime_frequency_snapshot_storage =
        oq_odu::encode_runtime_frequency_snapshot(snapshot);
    assert(capacity() > 0);
    // At 7/35, the 80/85/90 Hz V2-old points are masked; lower levels remain usable.
    oq_system_supply_temp.state = 12;
    assert(std::isnan(capacity()));
    oq_cold_start_session_active = true;
    assert(capacity() > 0);
    oq_cold_start_hp_blocked = true;
    assert(std::isnan(capacity()));
    oq_cold_start_session_active = oq_cold_start_hp_blocked = false;
    oq_system_supply_temp.state = 35;
  }

  assert(std::isnan(oq_heating_capacity::fresh_outlet(100, 60000, true, 0, true, 35)));
  near(oq_heating_capacity::fresh_outlet(100, 60000, true, UINT32_MAX - 100, true, 35), 35);
  assert(std::isnan(
      oq_heating_capacity::fresh_outlet(100, 60000, true, 90, true, std::numeric_limits<float>::infinity())));
  return 0;
}
