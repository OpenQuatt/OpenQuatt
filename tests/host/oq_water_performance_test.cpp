#include <cassert>
#include <cmath>
#include <string>

struct GenerationSelect {
  bool has_state() const { return true; }
  std::string current_option() const { return "V2"; }
} hp_generation;
#define id(value) value
#include "includes/performance/oq_water_performance.h"
#undef id
#include "includes/control/oq_power_house_dispatch_logic.h"

struct Frequency {
  oq_odu::Variant performance_variant(bool hp1) const {
    return hp1 ? oq_odu::Variant::V2_OLD_MODEL : oq_odu::Variant::V2_NEW_MODEL;
  }
  int automatic_frequency_hz(bool hp1, int, int level) const {
    constexpr int first[] = {0, 20, 26, 30, 48, 55, 61, 72, 80, 85, 90};
    constexpr int second[] = {0, 20, 26, 30, 36, 40, 45, 48, 52, 55, 60, 65, 68, 72, 76, 82, 85, 90, 95, 102, 110};
    return hp1 ? (level <= 10 ? first[level] : -1) : second[level];
  }
};
int main() {
  using namespace oq_water_performance;
  constexpr std::array<oq_odu::Variant, 2> variants{oq_odu::Variant::V2_OLD_MODEL, oq_odu::Variant::V2_NEW_MODEL};
  constexpr float flow = 870.144f, capacity = flow * 4186.0f / 3600.0f;
  const auto own1 = observed(variants[0], 90, -7, 35.77f);
  const auto own2 = observed(variants[1], 82, -7, 40.75f);
  assert(own1.valid() && own2.valid());
  assert(std::fabs(own1.thermal_w - 5812.774f) < .1f);
  assert(std::fabs(own2.thermal_w - 4985.13f) < .1f);

  const auto duo = series(variants, {90, 82}, -7, 30, flow);
  assert(duo[0].valid() && duo[1].valid());
  assert(std::fabs(duo[0].thermal_w + duo[1].thermal_w - 10803.54156f) < 2);
  assert(std::fabs(duo[0].electrical_w + duo[1].electrical_w - 4136.95535f) < 2);
  for (unsigned hp = 0; hp < 2; ++hp) {
    const float inlet = hp == 0 ? 30 : duo[0].outlet_c;
    assert(std::fabs(capacity * (duo[hp].outlet_c - inlet) - duo[hp].thermal_w) <= 1.0f);
  }
  assert(duo[1].outlet_c > duo[0].outlet_c);
  assert(duo[1].thermal_w < solve(variants[1], 82, -7, 30, flow).thermal_w);

  for (const std::array<float, 2> hz : {std::array<float, 2>{30, 0}, {0, 30}, {0, 0}}) {
    const auto pair = series(variants, hz, -7, 30, flow);
    assert(pair[0].valid() && pair[1].valid());
    if (hz[0] == 0) assert(pair[0].thermal_w == 0 && pair[0].outlet_c == 30);
    if (hz[1] == 0) assert(pair[1].thermal_w == 0 && pair[1].outlet_c == pair[0].outlet_c);
  }
  assert(!observed(variants[0], 90.1f, -7, 35).valid());
  assert(!observed(variants[0], 90, NAN, 35).valid());
  assert(!observed(variants[0], 20, -7, 30).valid());  // Existing masked map point.
  assert(!solve(variants[0], 90, -7, 30, 299).valid());
  assert(!solve(variants[0], 90, -7, 30, NAN).valid());
  assert(!solve(variants[0], 90, -7, 100, flow).valid());

  // The initial upper guess crosses 49 C, but the actual F1 root is supported.
  for (auto variant : variants) {
    for (const auto point :
         {std::array<float, 3>{47.2f, 1000, 48.948952f}, {42.5f, 300, 48.402f}, {47.0f, 900, 48.94352f}}) {
      const auto root = solve(variant, 20, 14.6f, point[0], point[1]);
      assert(root.valid() && std::fabs(root.outlet_c - point[2]) < .01f);
      assert(std::fabs(point[1] * 4186 / 3600 * (root.outlet_c - point[0]) - root.thermal_w) <= 1);
    }
    // Clipping the search bracket must never invent a root outside the map.
    assert(!solve(variant, 20, 14.6f, 47.5f, 1000).valid());
    assert(!solve(variant, 20, 14.6f, 49, 1000).valid());
  }

  for (auto variant : {oq_odu::Variant::V1, oq_odu::Variant::V1_5}) {
    const auto cold = solve(variant, 90, -15, 35, 800);
    assert(cold.valid() && cold.outlet_c > 37 && cold.outlet_c < 39);
    assert(std::fabs(800 * 4186.0f / 3600 * (cold.outlet_c - 35) - cold.thermal_w) <= 1);
  }
  const auto cool_return = solve(variants[0], 30, 7, 15, 300);
  assert(cool_return.valid() && std::fabs(cool_return.outlet_c - 25.06614f) < .01f);
  assert(std::fabs(300 * 4186.0f / 3600 * (cool_return.outlet_c - 15) - cool_return.thermal_w) <= 1);
  assert(!solve(variants[0], 30, 7, 0, 300).valid());
  assert(!solve(variants[0], 72, -7, 65, 500).valid());  // New envelope caps this region at 60 Hz.
  const auto hot_return = solve(variants[0], 48, -7, 65, 500);
  assert(hot_return.valid() && std::fabs(hot_return.outlet_c - 68.13822f) < .01f);
  assert(std::fabs(500 * 4186.0f / 3600 * (hot_return.outlet_c - 65) - hot_return.thermal_w) <= 1);
  assert(solve(variants[0], 90, -7, 10, flow, true).valid());  // Existing cold-start boundary remains usable.
  const Frequency frequency;
  const CandidateModel model(frequency, -7, 30, flow, {35.77f, 40.75f}, true, false);
  oq_power_house_dispatch::DispatchInput in;
  in.duo = in.performance_valid = true;
  in.demand_level = 20;
  in.requested_w = 8000;
  in.hp1.candidate = in.hp2.candidate = {0, true, false, false, true};
  in.pair_power = &CandidateModel::dispatch;
  in.pair_context = &model;
  for (unsigned hp = 0; hp < 2; ++hp)
    for (int level = 0; level <= 20; ++level) {
      const int hz = level == 0 ? 0 : frequency.automatic_frequency_hz(hp == 0, 2, level);
      const auto p = model.unit(hp == 0, level);
      (hp == 0 ? in.hp1 : in.hp2).levels[level] = {hz >= 0 && hz <= 90, p.valid(),
                                                   p.valid(),           p.thermal_w,
                                                   p.electrical_w,      static_cast<uint8_t>(std::max(0, hz))};
    }
  const oq_power_house_dispatch::DispatchTuning limits{6000, 6500};
  const auto pair = oq_power_house_dispatch::make_candidate(in, limits, 10, 15, 8000);
  assert(pair.valid && std::fabs(pair.thermal_w - 10803.54156f) < 2);
  const auto chosen = oq_power_house_dispatch::decide_dispatch(in, limits, {});
  assert(chosen.output_valid && chosen.hp1_level + chosen.hp2_level > 0);
  assert(std::fabs(chosen.expected_w - model.pair(chosen.hp1_level, chosen.hp2_level).thermal_w) < .01f);
  in.hp2.candidate.minimum_off_ready = false;
  assert(!oq_power_house_dispatch::make_candidate(in, limits, 10, 15, 8000).valid);
  in.hp2.candidate.minimum_off_ready = true;
  in.hp2.levels[15].allowed = false;
  assert(!oq_power_house_dispatch::make_candidate(in, limits, 10, 15, 8000).valid);
  in.hp2.levels[15].allowed = true;
  assert(!oq_power_house_dispatch::make_candidate(in, {1000, 1100}, 10, 15, 8000).valid);
  auto peak = in;
  peak.duo = false;
  peak.hp1.candidate.previous_applied_level = 1;
  for (auto& level : peak.hp1.levels) level.allowed = false;
  peak.hp1.levels[1].allowed = true;
  peak.pair_power = [](const void*, int first, int) -> std::array<float, 2> {
    return first > 0 ? std::array<float, 2>{1000, 2000} : std::array<float, 2>{0, 0};
  };
  const auto blocked = oq_power_house_dispatch::decide_dispatch(peak, {900, 1000}, {});
  assert(blocked.hp1_level == 0 && blocked.reason == oq_power_house_dispatch::Reason::NO_CANDIDATE);
  peak.pair_power = [](const void*, int first, int) -> std::array<float, 2> {
    return first > 0 ? std::array<float, 2>{NAN, NAN} : std::array<float, 2>{0, 0};
  };
  const auto missing = oq_power_house_dispatch::decide_dispatch(peak, {900, 1000}, {});
  assert(missing.hp1_level == 1 && missing.reason == oq_power_house_dispatch::Reason::FALLBACK_HP1);
  const CandidateModel independent(frequency, -7, 30, NAN, {35.77f, 40.75f}, false, false);
  assert(std::fabs(independent.unit(false, 15).thermal_w - own2.thermal_w) < .01f);
}
