#include <cassert>
#include <cmath>
#include <string>

struct GenerationSelect {
  bool has_state() const { return true; }
  std::string current_option() const { return "V2"; }
} hp_generation;
#define id(value) value
#include "includes/performance/oq_water_performance_shadow.h"
#undef id

int main() {
  using namespace oq_water_shadow;
  constexpr std::array<oq_odu::Variant, 2> variants{oq_odu::Variant::V2_OLD_MODEL, oq_odu::Variant::V2_NEW_MODEL};
  constexpr float flow = 870.144f, capacity = flow * 4186.0f / 3600.0f;
  const auto own1 = observed(variants[0], 90, -7, 35.77f);
  const auto own2 = observed(variants[1], 82, -7, 40.75f);
  assert(own1.valid() && own2.valid());
  assert(std::fabs(own1.thermal_w - 5829.51f) < .1f);
  assert(std::fabs(own2.thermal_w - 5002.60f) < .1f);

  const auto duo = series(variants, {90, 82}, -7, 30, flow);
  assert(duo[0].valid() && duo[1].valid());
  assert(std::fabs(duo[0].thermal_w + duo[1].thermal_w - 10835.01f) < 2);
  assert(std::fabs(duo[0].electrical_w + duo[1].electrical_w - 4160.83f) < 2);
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
}
