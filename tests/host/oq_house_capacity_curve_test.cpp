#include <cassert>
#include <string>

struct TestGeneration {
  bool has_state() const { return true; }
  std::string current_option() const { return "V1.5"; }
} hp_generation;

#define id(value) value
#include "../../openquatt/includes/diagnostics/oq_house_capacity_curve.h"
#undef id

int main() {
  using namespace oq_house_capacity;
  Installation install;
  install.units[0] = {oq_odu::Variant::V1_5, {30, 39, 49, 55, 61, 67, 72, 79, 85, 90}};
  install.units[1] = install.units[0];
  install.available = install.duo = true;
  install.flow_lph = 800;
  install.max_water_c = 60;
  install.cap_hz = 90;
  install.limit_w = 3400;
  assert(power_at(install, -10, 35) == 6831);
  assert(power_at(install, -10, 45) == 6805);
  assert(power_at(install, -7, 45) == 7555);
  assert(power_at(install, 7, 45) == 11849);
  auto constrained = install;
  constrained.limit_w = 1400;
  assert(power_at(constrained, -10, 45) > 0 && power_at(constrained, -10, 45) < 6805);
  constrained.limit_w = 0;
  assert(power_at(constrained, -10, 45) == 0);
  constrained.max_water_c = 40;
  assert(power_at(constrained, -10, 45) == UNAVAILABLE);

  auto solo = install;
  solo.duo = false;
  solo.units[1] = {};
  assert(power_at(solo, -7, 35) == 3814);

  Curve curve;
  constexpr size_t legacy_points = LEGACY_MAX_OUTSIDE_C - MIN_OUTSIDE_C + 1;
  constexpr size_t legacy_steps = legacy_points * SUPPLY_C.size();
  for (size_t step = 0; step < legacy_steps - 1; ++step) curve.step(install);
  assert(!curve.complete());
  assert(curve.count == legacy_points - 1);
  assert(curve.points[legacy_points - 1].watts[0] == power_at(install, 7, 35));
  curve.step(install);
  assert(curve.complete() && curve.count == legacy_points);
  assert(curve.points[0].outside_centi == -1500 && curve.points[22].outside_centi == 700);
  assert(curve.points[22].watts[1] == 11849);
  curve.step(install);
  assert(curve.complete() && curve.count == legacy_points);
  curve.step(solo);
  assert(!curve.complete() && curve.count == 0);
  curve.step(install);
  assert(!curve.complete() && curve.count == 0);
  curve.step(install);
  assert(!curve.complete() && curve.count == 1);
  assert(curve.points[0].watts[0] == power_at(install, -15, 35));
  assert(curve.points[0].watts[1] == power_at(install, -15, 45));
  curve.step(solo);
  assert(!curve.complete() && curve.count == 0);
  curve.step({});
  assert(!curve.complete() && curve.count == 0);

  auto v2 = solo;
  v2.units[0] = {oq_odu::Variant::V2_NEW_MODEL, {30, 48, 61, 72, 80, 85, 90}};
  assert(power_at(v2, -5, 35) > 0);
  assert(power_at(v2, 0, 35) == UNAVAILABLE);
  for (size_t step = 0; step < MAX_POINTS * SUPPLY_C.size(); ++step) curve.step(v2);
  assert(curve.complete() && curve.count == MAX_POINTS);
  assert(curve.points[3].outside_centi == -1201 && curve.points[4].outside_centi == -1200);
  assert(curve.points[MAX_POINTS - 1].outside_centi == 2000);
}
