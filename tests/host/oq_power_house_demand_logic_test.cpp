#include <assert.h>
#include <math.h>

#include "../../openquatt/includes/control/oq_power_house_demand_logic.h"

namespace {

bool nearly_equal(float a, float b) { return fabsf(a - b) < 0.01f; }

constexpr float kT0 = 16.0f;
constexpr float kTc = -10.0f;
constexpr float kRated = 7020.0f;

// One shared operating point at 3 degC outside, i.e. half rated power.
float reference_modelled_w() { return oq_power_house::modelled_house_power_w(kT0, kTc, 3.0f, kRated); }

// The modelled feedforward is the reference every fallback must return to.
void test_modelled_house_power() {
  using oq_power_house::modelled_house_power_w;

  // No heating demand at and above the zero-power outdoor temperature.
  assert(nearly_equal(modelled_house_power_w(kT0, kTc, kT0, kRated), 0.0f));
  assert(nearly_equal(modelled_house_power_w(kT0, kTc, kT0 + 5.0f, kRated), 0.0f));

  // Full rated power at and below the full-load point.
  assert(nearly_equal(modelled_house_power_w(kT0, kTc, kTc, kRated), kRated));
  assert(nearly_equal(modelled_house_power_w(kT0, kTc, kTc - 10.0f, kRated), kRated));

  // Linear in between: the midpoint of the outdoor range is half the power.
  assert(nearly_equal(modelled_house_power_w(kT0, kTc, 3.0f, kRated), kRated * 0.5f));

  // An unusable house model yields no modelled value at all.
  assert(isnan(modelled_house_power_w(kTc, kT0, 3.0f, kRated)));
  assert(isnan(modelled_house_power_w(kT0, kTc, NAN, kRated)));
  assert(isnan(modelled_house_power_w(kT0, kT0, 3.0f, kRated)));
}

// An absent or unusable external demand must degrade to the modelled value,
// never to zero: a dropped planner link falls back to today's behaviour.
void test_fallback_to_model() {
  using oq_power_house::select_feedforward;

  const float modelled_w = reference_modelled_w();

  const auto no_source = select_feedforward(modelled_w, NAN, false, kRated);
  assert(!no_source.external);
  assert(nearly_equal(no_source.house_power_w, modelled_w));

  // A valid flag with a NaN payload is still not a usable demand.
  const auto nan_payload = select_feedforward(modelled_w, NAN, true, kRated);
  assert(!nan_payload.external);
  assert(nearly_equal(nan_payload.house_power_w, modelled_w));

  const auto infinite_payload = select_feedforward(modelled_w, INFINITY, true, kRated);
  assert(!infinite_payload.external);
  assert(nearly_equal(infinite_payload.house_power_w, modelled_w));

  // Without a usable rating there is no scale to clamp against, so the model
  // stays in charge.
  const auto no_rating = select_feedforward(modelled_w, 3000.0f, true, 0.0f);
  assert(!no_rating.external);
  assert(nearly_equal(no_rating.house_power_w, modelled_w));

  const auto nan_rating = select_feedforward(modelled_w, 3000.0f, true, NAN);
  assert(!nan_rating.external);
  assert(nearly_equal(nan_rating.house_power_w, modelled_w));
}

// A valid external demand replaces the feedforward and is clamped to the same
// [0, rated] window that P_raw already uses.
void test_external_demand() {
  using oq_power_house::select_feedforward;

  const float modelled_w = reference_modelled_w();

  const auto normal = select_feedforward(modelled_w, 3000.0f, true, kRated);
  assert(normal.external);
  assert(nearly_equal(normal.house_power_w, 3000.0f));

  // Zero watt is a legitimate request: a planner must be able to say "no heat
  // right now". It must not be mistaken for a missing value.
  const auto zero = select_feedforward(modelled_w, 0.0f, true, kRated);
  assert(zero.external);
  assert(nearly_equal(zero.house_power_w, 0.0f));

  const auto above_rated = select_feedforward(modelled_w, kRated + 5000.0f, true, kRated);
  assert(above_rated.external);
  assert(nearly_equal(above_rated.house_power_w, kRated));

  const auto negative = select_feedforward(modelled_w, -500.0f, true, kRated);
  assert(negative.external);
  assert(nearly_equal(negative.house_power_w, 0.0f));

  const auto exactly_rated = select_feedforward(modelled_w, kRated, true, kRated);
  assert(exactly_rated.external);
  assert(nearly_equal(exactly_rated.house_power_w, kRated));
}

}  // namespace

int main() {
  test_modelled_house_power();
  test_fallback_to_model();
  test_external_demand();
  return 0;
}
