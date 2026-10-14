#include <assert.h>

#include <array>
#include <limits>

#include "../../openquatt/includes/control/oq_compressor_temperature_policy.h"
#include "../../openquatt/includes/control/oq_compressor_frequency_runtime.h"

namespace {

using oq_odu::Variant;
using oq_temperature::Evaluation;
using oq_temperature::Inputs;
using oq_temperature::Policy;

template <size_t N>
oq_odu::RuntimeFrequencySnapshot snapshot(Variant variant, const std::array<uint8_t, N>& hz) {
  oq_odu::RuntimeFrequencySnapshot out;
  out.variant = variant;
  out.heating.level_count = N;
  out.heating.valid = true;
  for (size_t i = 0; i < N; ++i) out.heating.hz[i] = hz[i];
  assert(oq_odu::validate_frequency_table(out.heating));
  return out;
}

const auto V1 = snapshot(Variant::V1, std::array<uint8_t, 11>{0, 30, 39, 49, 55, 61, 67, 72, 79, 85, 90});
const auto V2 = snapshot(Variant::V2_NEW_MODEL, std::array<uint8_t, 21>{0,  20, 26, 30, 36, 40, 45, 48, 52,  55, 60,
                                                                        65, 68, 72, 76, 82, 85, 90, 95, 102, 110});

Inputs input(int ambient, int model_water, bool low_water_allowed = false) {
  return {ambient, model_water - oq_temperature::RETURN_OFFSET_CENTI_C, true, low_water_allowed};
}

Evaluation fresh(const oq_odu::RuntimeFrequencySnapshot& s, int ambient, int model_water,
                 bool low_water_allowed = false) {
  Policy p;
  const auto out = p.evaluate(s, input(ambient, model_water, low_water_allowed));
  assert(out.allows(0));
  assert(!out.allows(-1));
  assert(!out.allows(21));
  return out;
}

bool raw_envelope_allows(Variant variant, int hz, int ambient, int water) {
  const auto e = oq_temperature::envelope(variant, hz, ambient);
  return e.available && ambient >= e.ambient_min &&
         (ambient < e.ambient_max || (!e.ambient_max_exclusive && ambient == e.ambient_max)) && water <= e.water_max;
}

void v1_positive_weight_corners() {
  // CiC 4.2.0 source-mask maxima. These raw masks precede the independent 79 Hz water cap.
  constexpr std::array<int, 5> ambient{-1500, -700, 200, 700, 1200};
  constexpr std::array<int, 3> water{3500, 4500, 5500};
  constexpr int maximum[5][3] = {{90, 90, 85}, {90, 90, 79}, {90, 90, 79}, {90, 90, 79}, {79, 79, 79}};
  for (size_t a = 0; a < ambient.size(); ++a) {
    for (size_t w = 0; w < water.size(); ++w) {
      const auto out = fresh(V1, ambient[a], water[w]);
      assert(out.water_cap_hz == 79);
      for (int level = 1; level <= 10; ++level) {
        const int hz = V1.heating.hz[level];
        assert(raw_envelope_allows(Variant::V1, hz, ambient[a], water[w]) == (hz <= maximum[a][w]));
        assert(out.allows(level) == (hz <= 79));
      }
    }
  }
  // Every interior cell requires all four positive-weight corners. An exact axis
  // coordinate uses only that row/column; NaN neighbours with zero weight do not count.
  for (size_t a = 0; a + 1 < ambient.size(); ++a) {
    for (size_t w = 0; w + 1 < water.size(); ++w) {
      const int expected =
          std::min(std::min(maximum[a][w], maximum[a + 1][w]), std::min(maximum[a][w + 1], maximum[a + 1][w + 1]));
      for (int level = 1; level <= 10; ++level) {
        assert(raw_envelope_allows(Variant::V1, V1.heating.hz[level], (ambient[a] + ambient[a + 1]) / 2,
                                   (water[w] + water[w + 1]) / 2) == (V1.heating.hz[level] <= expected));
      }
    }
  }
  assert(raw_envelope_allows(Variant::V1, 90, 700, 4500));
  assert(!raw_envelope_allows(Variant::V1, 90, 701, 4500));
  assert(!raw_envelope_allows(Variant::V1, 90, 700, 4501));
  assert(raw_envelope_allows(Variant::V1, 85, -1500, 5500));
  assert(!raw_envelope_allows(Variant::V1, 85, -1499, 5500));
}

void v2_zones_and_runtime_frequencies() {
  // Test every canonical 0.01 C coordinate, not just the middle of each zone.
  // This independently describes the source's explicit zone transitions.
  for (int ambient = -1501; ambient <= 4500; ++ambient) {
    int maximum = ambient < -500   ? 90
                  : ambient < 0    ? 85
                  : ambient < 500  ? 80
                  : ambient < 1100 ? 72
                  : ambient < 1400 ? 61
                  : ambient < 1900 ? 55
                  : ambient < 2700 ? 48
                                   : 30;
    const auto out = fresh(V2, ambient, 3500);
    for (int level = 1; level <= 20; ++level) {
      const int hz = V2.heating.hz[level];
      const bool expected =
          ambient >= -1500 && ambient <= 4499 && hz <= maximum && (hz >= 30 || (ambient >= 500 && ambient < 1400));
      assert(out.allows(level) == expected);
    }
  }
  // Native anchors are 20,26,30,48,55,61,72,80,85,90. Intermediate runtime
  // frequencies require BOTH enclosing anchors, including the 26--30 interval.
  constexpr std::array<int, 10> anchors{20, 26, 30, 48, 55, 61, 72, 80, 85, 90};
  constexpr std::array<int, 8> zone_ambient{-700, -200, 200, 700, 1200, 1600, 2200, 3000};
  constexpr std::array<int, 8> zone_maximum{90, 85, 80, 72, 61, 55, 48, 30};
  for (size_t z = 0; z < zone_ambient.size(); ++z) {
    for (int hz = 19; hz <= 91; ++hz) {
      bool expected = false;
      for (size_t a = 0; a < anchors.size(); ++a) {
        const auto anchor_allowed = [&](int anchor) {
          return anchor <= zone_maximum[z] && (anchor >= 30 || (zone_ambient[z] >= 500 && zone_ambient[z] < 1400));
        };
        if (hz == anchors[a]) expected = anchor_allowed(hz);
        if (a + 1 < anchors.size() && hz > anchors[a] && hz < anchors[a + 1])
          expected = anchor_allowed(anchors[a]) && anchor_allowed(anchors[a + 1]);
      }
      assert(raw_envelope_allows(Variant::V2_NEW_MODEL, hz, zone_ambient[z], 3500) == expected);
      for (int water : {1800, 2500, 3500, 4500, 5499}) {
        assert(raw_envelope_allows(Variant::V2_NEW_MODEL, hz, zone_ambient[z], water) == expected);
      }
    }
  }
  auto legacy = V2;
  legacy.variant = Variant::V2_OLD_MODEL;
  assert(fresh(legacy, 700, 3500).allowed_levels == fresh(V2, 700, 3500).allowed_levels);
}

void water_caps_are_strict_and_have_no_hysteresis() {
  Policy v1;
  for (int water : {3299, 3300, 3301, 3300, 3299}) {
    const auto out = v1.evaluate(V1, input(-700, water, true));
    assert(out.water_cap_hz == (water <= 3300 ? 90 : 79));
    assert(out.allows(10) == (water <= 3300));
    assert(out.allows(8));
    assert((out.reasons & oq_temperature::HYSTERESIS) == 0);
  }
  Policy v2;
  for (int water : {3749, 3750, 3751, 3750, 5249, 5250, 5251, 5250}) {
    const auto out = v2.evaluate(V2, input(-700, water));
    const int expected = water <= 3750 ? 90 : water <= 5250 ? 72 : 55;
    assert(out.water_cap_hz == expected);
    for (int level = 1; level <= 20; ++level)
      assert(out.allows(level) == (V2.heating.hz[level] >= 30 && V2.heating.hz[level] <= expected));
    assert((out.reasons & oq_temperature::HYSTERESIS) == 0);
  }
  assert(fresh(V2, 2000, 5350).allows(7));  // 48 Hz, table remains stronger than 55 Hz water cap.
  assert(!fresh(V2, 2000, 5350).allows(9));
}

void strict_reentry_at_every_direction() {
  for (int distance : {19, 20, 21}) {
    Policy lower;
    assert(!lower.evaluate(V2, input(499, 3500)).allows(1));
    const auto low = lower.evaluate(V2, input(500 + distance, 3500));
    assert(low.allows(1) == (distance > 20));
    assert(low.allows(2) == (distance > 20));
    assert(low.allows(3));

    Policy upper;
    assert(!upper.evaluate(V2, input(1400, 3500)).allows(1));
    assert(upper.evaluate(V2, input(1400 - distance, 3500)).allows(1) == (distance > 20));

    Policy high_frequency;
    assert(!high_frequency.evaluate(V2, input(-500, 3500)).allows(17));
    assert(high_frequency.evaluate(V2, input(-500 - distance, 3500)).allows(17) == (distance > 20));

    Policy high_water;
    assert(!high_water.evaluate(V2, input(700, 5500)).allows(3));
    assert(high_water.evaluate(V2, input(700, 5499 - distance)).allows(3) == (distance > 20));

    Policy low_ambient;
    assert(!low_ambient.evaluate(V2, input(-1501, 3500)).allows(3));
    assert(low_ambient.evaluate(V2, input(-1500 + distance, 3500)).allows(3) == (distance > 20));
  }
  Policy both;
  assert(!both.evaluate(V2, input(1400, 5500)).allows(1));
  assert(!both.evaluate(V2, input(1379, 5480)).allows(1));
  assert(both.evaluate(V2, input(1379, 5478)).allows(1));
  // Returning far away on the wrong side is never permission.
  assert(!both.evaluate(V2, input(1500, 3500)).allows(1));
  assert(!both.evaluate(V2, input(400, 3500)).allows(1));
}

void invalid_inputs_and_domains_fail_closed() {
  assert(oq_temperature::centi_c(std::numeric_limits<double>::quiet_NaN()) == oq_temperature::INVALID_TEMPERATURE);
  assert(oq_temperature::centi_c(std::numeric_limits<double>::infinity()) == oq_temperature::INVALID_TEMPERATURE);
  assert(oq_temperature::centi_c(-100.01) == oq_temperature::INVALID_TEMPERATURE);
  assert(oq_temperature::centi_c(150.01) == oq_temperature::INVALID_TEMPERATURE);
  assert(oq_temperature::centi_c(5.21) == 521);
  assert(oq_temperature::centi_c(-5.21) == -521);
  Policy p;
  assert(p.evaluate(V2, input(700, 3500)).allows(1));
  auto in = input(700, 3500);
  in.fresh = false;
  assert(p.evaluate(V2, in).allowed_levels == 0);
  in.fresh = true;
  in.inlet_centi_c = oq_temperature::INVALID_TEMPERATURE;
  assert(p.evaluate(V2, in).allowed_levels == 0);
  in = input(700, 3500);
  in.ambient_centi_c = oq_temperature::INVALID_TEMPERATURE;
  assert(p.evaluate(V2, in).allowed_levels == 0);
  auto unknown = V2;
  unknown.variant = Variant::UNKNOWN;
  assert(p.evaluate(unknown, input(700, 3500)).allowed_levels == 0);
  auto invalid = V2;
  invalid.heating.valid = false;
  assert(p.evaluate(invalid, input(700, 3500)).allowed_levels == 0);
  invalid = V2;
  invalid.heating.hz[4] = 19;  // Not monotonic; valid flag alone cannot authorize it.
  assert(p.evaluate(invalid, input(700, 3500)).allowed_levels == 0);
  for (int ambient : {-1501, 4500}) assert(fresh(V2, ambient, 3500).allowed_levels == 0);
  for (int ambient : {-1501, 1201}) assert(fresh(V1, ambient, 3500).allowed_levels == 0);
  assert(fresh(V2, 700, 5500).allowed_levels == 0);
  assert(fresh(V2, 700, 6500).allowed_levels == 0);
  assert(fresh(V1, 700, 5501).allowed_levels == 0);
  assert(fresh(V1, 700, 3499).allowed_levels == 0);
  assert(fresh(V2, 700, 1799).allowed_levels == 0);
  assert(fresh(V1, 700, 3499, true).allows(8));
  assert(fresh(V2, 700, 1799, true).allows(1));
  // Empty masks retain only explicit stop permission, never a blind F1 fallback.
  const auto empty = fresh(V2, -1600, 3500);
  assert(empty.allows(0));
  assert(!empty.allows(1));
}

void independent_hps_profile_and_table_changes() {
  Policy hp1;
  Policy hp2;
  assert(!hp1.evaluate(V2, input(1400, 3500)).allows(1));
  assert(!hp1.evaluate(V2, input(1390, 3500)).allows(1));
  assert(hp2.evaluate(V2, input(1390, 3500)).allows(1));
  assert(hp2.evaluate(V1, input(700, 3500)).allows(8));
  // Identical Hz levels have identical decisions through invalidation/re-entry.
  auto duplicated = V2;
  duplicated.heating.hz[2] = 20;
  Policy duplicate_policy;
  for (int ambient : {700, 1400, 1381, 1380, 1379}) {
    const auto out = duplicate_policy.evaluate(duplicated, input(ambient, 3500));
    assert(out.allows(1) == out.allows(2));
  }
  // A profile switch clears old-boundary state and evaluates the new profile now.
  auto old_profile = V2;
  old_profile.variant = Variant::V2_OLD_MODEL;
  assert(hp1.evaluate(old_profile, input(1390, 3500)).allows(1));
  assert(!hp1.evaluate(old_profile, input(1400, 3500)).allows(1));
  // Same profile with a changed physical table must also discard prior state.
  auto remapped = old_profile;
  remapped.heating.hz[1] = 21;
  assert(hp1.evaluate(remapped, input(1390, 3500)).allows(1));
  assert(!hp1.evaluate(remapped, input(1400, 3500)).allows(1));
  hp1.reset();
  assert(hp1.evaluate(remapped, input(1390, 3500)).allows(1));
  // A changed map never reuses its previous allowed mask.
  remapped.heating.hz[1] = 19;
  assert(!hp1.evaluate(remapped, input(700, 3500)).allows(1));
}

void context_enforces_actual_frequency_masks() {
  oq_frequency_runtime::Context context;
  context.configured_v2 = true;
  context.hp1_snapshot = V2;
  context.hp2_snapshot = V2;
  context.cap_hz = 90;
  assert(context.temperature_enforced);
  // Even a validated frequency table and usable user cap are insufficient when
  // no current temperature decision has been captured.
  assert(!context.frequency_allowed(true, 2, 3));
  assert(context.pick_allowed_level(true, 2, 17, 1, 20) == 0);
  assert(context.pick_temperature_level(true, 2, 17) == 0);
  assert(context.pick_temperature_level(true, 2, 0) == 0);
  assert(context.temperature_allowed(true, 1, 3));

  context.hp1_temperature = fresh(V2, 200, 3500);
  // HP1's actual F15 is 82 Hz, rather than the native 80 Hz model anchor.
  // HP2 has its own validated mapping with an actual 80 Hz F15.
  context.hp2_snapshot.heating.hz[15] = 80;
  context.hp2_temperature = fresh(context.hp2_snapshot, 200, 3500);
  assert(context.automatic_frequency_hz(true, 2, 15) == 82);
  assert(context.automatic_frequency_hz(false, 2, 15) == 80);
  assert(!context.frequency_allowed(true, 2, 15));
  assert(context.frequency_allowed(false, 2, 15));
  assert(context.pick_allowed_level(true, 2, 17, 1, 20) == 14);
  assert(context.pick_allowed_level(false, 2, 17, 1, 20) == 15);
  assert(context.pick_temperature_level(true, 2, 17) == 14);
  assert(context.pick_temperature_level(false, 2, 17) == 15);
  assert(context.frequency_allowed(true, 2, 4));  // Physical intermediate 36 Hz remains usable.

  // All constraints intersect; neither a temperature mask nor a user setting
  // can widen the other, and per-HP excluded ranges stay independent.
  context.cap_hz = 70;
  assert(context.pick_allowed_level(true, 2, 17, 1, 20) == 12);
  context.hp1_excluded = {60, 68};
  assert(context.pick_allowed_level(true, 2, 17, 1, 20) == 9);
  assert(context.pick_allowed_level(false, 2, 17, 1, 20) == 12);
  context.cap_hz = 90;
  context.hp1_excluded = {};

  // Losing the low-frequency zone is never an instruction to pick F3 (30 Hz)
  // merely because that would be the next valid stand above F1/F2.
  assert(!context.frequency_allowed(true, 2, 1));
  assert(context.frequency_allowed(true, 2, 3));
  assert(context.pick_allowed_level(true, 2, 1, 1, 20) == 0);
  assert(context.pick_allowed_level(true, 2, 2, 1, 20) == 0);
  assert(context.pick_temperature_level(true, 2, 2) == 0);
  assert(context.limit_heating_slew(true, 3, 1, 101, 100, 60000, 60000) == 0);

  context.hp1_temperature = fresh(V2, -700, 3800);
  // Temperature removal of an active 90 Hz stand must lower immediately,
  // regardless of normal slew hold windows or the old requested stand.
  assert(context.limit_heating_slew(true, 17, 17, 101, 100, 60000, 60000) == 13);
  assert(context.pick_temperature_level(true, 2, 17) == 13);
  // Manual commands may bypass a user frequency exclusion but must still
  // project downward through the same temperature mask.
  context.hp1_excluded = {30, 90};
  assert(context.pick_allowed_level(true, 2, 17, 1, 20) == 0);
  assert(context.pick_temperature_level(true, 2, 17) == 13);
  context.hp1_temperature = fresh(V2, -700, 5500);
  assert(context.pick_temperature_level(true, 2, 20) == 0);
  assert(context.limit_heating_slew(true, 17, 17, 101, 100, 60000, 60000) == 0);
  assert(context.frequency_allowed(false, 2, 15));  // HP1's domain loss does not block HP2.
}

}  // namespace

int main() {
  v1_positive_weight_corners();
  v2_zones_and_runtime_frequencies();
  water_caps_are_strict_and_have_no_hysteresis();
  strict_reentry_at_every_direction();
  invalid_inputs_and_domains_fail_closed();
  independent_hps_profile_and_table_changes();
  context_enforces_actual_frequency_masks();
  return 0;
}
