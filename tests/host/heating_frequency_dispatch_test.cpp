#include <cassert>

#include <array>
#include <cmath>
#include <cstdint>
#include <string>

#include "../fixtures/v1_eeprom.h"
#include "../fixtures/v1_5_eeprom.h"
#include "../fixtures/v2_old_amh6_eeprom.h"
#include "../fixtures/v2_new_amh6_eeprom.h"
#include "includes/control/oq_compressor_frequency_runtime.h"
#include "includes/control/oq_heating_curve_logic.h"
#include "includes/control/oq_incident_actuator_logic.h"
#include "includes/control/oq_power_house_dispatch_logic.h"
#include "includes/control/oq_thermal_actuator_logic.h"
#include "includes/control/oq_thermal_request_logic.h"
struct GenerationSelect {
  bool has_state() const { return true; }
  std::string current_option() const { return "V1"; }
} hp_generation;
#define id(value) value
#include "includes/performance/hp_perf_frequency.h"
#undef id

namespace {
using namespace oq_power_house_dispatch;

template <size_t N>
std::array<uint8_t, N * 2> response(const std::array<uint16_t, 512>& words, uint16_t address) {
  std::array<uint8_t, N * 2> result{};
  for (size_t i = 0; i < N; ++i) {
    const auto value = words[address - 2999 + i];
    result[2 * i] = value >> 8;
    result[2 * i + 1] = value & 255;
  }
  return result;
}

oq_odu::RuntimeFrequencySnapshot snapshot(const std::array<uint16_t, 512>& words, oq_odu::Variant variant) {
  const auto base = response<oq_odu::BASE_FREQUENCY_TABLE_REGISTER_COUNT>(words, oq_odu::BASE_FREQUENCY_TABLE_REGISTER);
  auto result = oq_odu::parse_base_frequency_table_response(base.data(), base.size(), variant);
  if (variant == oq_odu::Variant::V2_NEW_MODEL) {
    const auto extension =
        response<oq_odu::EXTENDED_FREQUENCY_TABLE_REGISTER_COUNT>(words, oq_odu::EXTENDED_FREQUENCY_TABLE_REGISTER);
    assert(
        oq_odu::apply_v2_extension_frequency_table_response(result, extension.data(), extension.size()).heating_valid);
  }
  return result;
}

oq_frequency_runtime::Context context(const oq_odu::RuntimeFrequencySnapshot& hp1,
                                      const oq_odu::RuntimeFrequencySnapshot& hp2) {
  return {false, hp1, hp2, 120, {}, {}, hp1.variant, hp2.variant, false};
}

HpInput modeled_hp(const oq_frequency_runtime::Context& frequency, bool hp1, int previous = 0, float supply = 35,
                   float ambient = 7) {
  HpInput result;
  result.candidate = {previous, true, false, false};
  result.levels[0] = {true, true, true, 0, 0};
  for (int level = 1; level <= frequency.automatic_level_limit(hp1, 2); ++level) {
    const auto prediction =
        oq_perf::predict_candidate(frequency, frequency.performance_variant(hp1), hp1, level, ambient, supply);
    const bool usable = prediction.usable_for_running_optimization();
    result.levels[level] = {prediction.frequency_policy_allowed,
                            usable,
                            usable,
                            prediction.performance.pth_w,
                            prediction.performance.pel_w,
                            static_cast<uint8_t>(prediction.runtime_frequency_hz)};
  }
  return result;
}

DispatchTuning limits() { return {10000, 12000}; }

void test_fixture_chain() {
  const std::array<oq_odu::RuntimeFrequencySnapshot, 4> tables = {
      snapshot(oq_test::v1::EEPROM_WORDS, oq_odu::Variant::V1),
      snapshot(oq_test::v1_5::EEPROM_WORDS, oq_odu::Variant::V1_5),
      snapshot(oq_test::v2_old_amh6::EEPROM_WORDS, oq_odu::Variant::V2_OLD_MODEL),
      snapshot(oq_test::v2_new_amh6::EEPROM_WORDS, oq_odu::Variant::V2_NEW_MODEL)};
  for (const auto& table : tables) {
    const auto frequency = context(table, table);
    const int maximum = frequency.automatic_level_limit(true, 2);
    assert(maximum == (table.variant == oq_odu::Variant::V2_NEW_MODEL ? 17 : 10));
    for (int level = 1; level <= maximum; ++level) {
      DispatchInput input;
      input.demand_level = 10;
      input.performance_valid = true;
      for (float ambient : {-15.0f, -7.0f, 0.0f, 2.0f, 7.0f, 12.0f}) {
        input.hp1 = modeled_hp(frequency, true, level, 35, ambient);
        if (input.hp1.levels[level].thermal_valid) break;
      }
      assert(input.hp1.levels[level].thermal_valid);
      input.requested_w = input.hp1.levels[level].thermal_w;
      const auto selected = decide_dispatch(input, limits(), {});
      assert(selected.output_valid && selected.hp1_level == level);
      const auto published =
          oq_request::make_published_request(2, selected.hp1_level, 0, oq_request::STRATEGY_POWER_HOUSE, maximum, 0);
      const int allowed = frequency.pick_allowed_level(true, 2, published.hp1_level, 1, maximum);
      const auto command = oq_odu::resolve_automatic_level(false, table, 2, allowed);
      assert(command.physical_level == level && command.control_level == level);
      assert(oq_thermal_actuator::valid_level_command(command.control_level, command.physical_level));
      assert(frequency.automatic_frequency_hz(true, 2, command.physical_level) == table.heating.hz[level]);
      assert(table.heating.hz[level] <= 90);
    }
  }
}

void test_extra_points_and_limits() {
  const auto table = snapshot(oq_test::v2_new_amh6::EEPROM_WORDS, oq_odu::Variant::V2_NEW_MODEL);
  auto frequency = context(table, table);
  DispatchInput input;
  input.demand_level = 10;
  input.performance_valid = true;
  input.hp1 = modeled_hp(frequency, true);
  for (int level : {4, 5, 6, 8, 11, 12, 14}) {
    for (float ambient : {-15.0f, -7.0f, 0.0f, 2.0f, 7.0f, 12.0f}) {
      input.hp1 = modeled_hp(frequency, true, 0, 35, ambient);
      if (input.hp1.levels[level].thermal_valid) break;
    }
    input.requested_w = input.hp1.levels[level].thermal_w;
    assert(decide_dispatch(input, limits(), {}).hp1_level == level);
  }
  for (int level : {18, 19, 20}) {
    assert(!oq_perf::predict_candidate(frequency, table.variant, true, level, 7, 35).usable_for_running_optimization());
    assert(oq_odu::resolve_automatic_level(false, table, 2, level).physical_level == 0);
  }
  frequency.cap_hz = 40;
  frequency.hp1_excluded = {30, 36};
  input.hp1 = modeled_hp(frequency, true);
  input.requested_w = 10000;
  assert(decide_dispatch(input, limits(), {}).hp1_level == 5);
  frequency.hp1_snapshot.heating.valid = false;
  input.hp1 = modeled_hp(frequency, true);
  assert(decide_dispatch(input, limits(), {}).hp1_level == 0);
  frequency = context(table, table);
  input.hp1 = modeled_hp(frequency, true, 0, 100);
  assert(decide_dispatch(input, limits(), {}).hp1_level == 0);

  // Same physical Hz may occupy several entries; hold the entry already selected.
  frequency.hp1_snapshot.heating.hz[6] = frequency.hp1_snapshot.heating.hz[5];
  assert(oq_odu::validate_frequency_table(frequency.hp1_snapshot.heating));
  input.hp1 = modeled_hp(frequency, true, 6);
  input.requested_w = input.hp1.levels[6].thermal_w;
  assert(decide_dispatch(input, limits(), {}).hp1_level == 6);
}

void test_future_v1_and_asymmetric_duo() {
  auto future = snapshot(oq_test::v1::EEPROM_WORDS, oq_odu::Variant::V1);
  future.heating.level_count = 21;
  for (int level = 1; level <= 20; ++level) future.heating.hz[level] = 30 + level * 3;
  assert(oq_odu::validate_frequency_table(future.heating));
  const auto frequency = context(future, future);
  assert(frequency.automatic_level_limit(true, 2) == 20);
  auto hp = modeled_hp(frequency, true, 20);
  DispatchInput input{0, 10, hp.levels[20].thermal_w, false, true, true, hp, {}};
  assert(decide_dispatch(input, limits(), {}).hp1_level == 20);
  assert(oq_odu::normalized_heating_demand_level(false, future, 20) == 10);

  // Runtime lead may use a different stand only after checking its own heat/power.
  input = {};
  input.duo = input.performance_valid = true;
  input.demand_level = 10;
  input.requested_w = 3000;
  input.hp1.candidate = input.hp2.candidate = {0, true, false, false};
  input.hp1.levels[7] = {true, true, true, 3000, 500, 48};
  input.hp2.levels[4] = {true, true, true, 3000, 480, 48};
  input.hp1.levels[4] = {true, true, true, 1500, 450, 36};
  const auto selected = decide_dispatch(input, limits(), {});
  assert(selected.hp1_level == 7 && selected.hp2_level == 0 && selected.reason == Reason::RUNTIME_LEAD);
  input.hp1.levels[7].electrical_w = 1000;
  assert(decide_dispatch(input, limits(), {}).hp2_level == 4);
  input.hp1.candidate.must_stop = true;
  assert(decide_dispatch(input, limits(), {}).hp1_level == 0);
  input.hp1.candidate = {7, false, false, true};
  const auto suspect = decide_dispatch(input, limits(), {});
  assert(suspect.hp1_level == 7 && suspect.hp2_level == 0);
}

void test_high_stand_guards() {
  using namespace oq_thermal_actuator;
  assert(!may_retain_command(0, false) && may_retain_command(17, false) && !may_retain_command(17, true));
  const auto stopped = oq_incident_actuator::decide({17, 17, true, true});
  assert(stopped.guarded_level == 0 && stopped.bypass_runtime_and_defrost_holds);
  assert(oq_incident_actuator::decide({17, 0, false, false}).guarded_level == 0);
  assert(decide_preflight(17, 0, false, 2, 1, false, false) == PreflightBlock::HP_REST);
  assert(decide_preflight(17, 0, false, 2, 0, false, false, 1) == PreflightBlock::START_LIMIT);
  assert(decide_preflight(17, 17, true, 2, 0, true, false) == PreflightBlock::SAFE_ZERO);
}

void test_allowed_entry_slew_and_suspect_filter() {
  const auto table = snapshot(oq_test::v2_new_amh6::EEPROM_WORDS, oq_odu::Variant::V2_NEW_MODEL);
  auto frequency = context(table, table);
  frequency.hp1_excluded = {26, 36};
  assert(frequency.limit_heating_slew(true, 5, 1, 119, 100, 20, 30) == 1);
  assert(frequency.limit_heating_slew(true, 5, 1, 120, 100, 20, 30) == 5);
  assert(frequency.limit_heating_slew(true, 1, 5, 129, 100, 20, 30) == 5);
  assert(frequency.limit_heating_slew(true, 1, 5, 130, 100, 20, 30) == 1);
  frequency.hp1_excluded = {20, 26};
  assert(frequency.limit_heating_slew(true, 7, 0, 119, 100, 20, 30) == 0);
  assert(frequency.limit_heating_slew(true, 7, 0, 120, 100, 20, 30) == 3);
  // A withdrawn incident availability forbids an upward policy fallback.
  const auto guarded = oq_incident_actuator::decide({7, 2, false, false});
  assert(guarded.guarded_level == 2);
  assert(frequency.pick_allowed_level(true, 2, guarded.guarded_level, 1, 2) == 0);
  frequency.hp1_excluded = {};
  frequency.cap_hz = 30;
  assert(frequency.limit_heating_slew(true, 17, 17, 101, 100, 20, 30) == 3);
  assert(frequency.limit_heating_slew(true, 17, 3, 101, 100, 20, 30) == 3);
  assert(oq_odu::normalized_heating_demand_level(false, table, 17) == 10);
  assert(oq_odu::normalized_heating_demand_level(false, {}, 17) > 0);
}
}  // namespace

int main() {
  static_assert(sizeof(LevelEstimate) == 12);
  static_assert(sizeof(DispatchInput) <= 544);
  test_fixture_chain();
  test_extra_points_and_limits();
  test_future_v1_and_asymmetric_duo();
  test_high_stand_guards();
  test_allowed_entry_slew_and_suspect_filter();
}
