#include <assert.h>
#include <math.h>

#include "../../openquatt/includes/performance/oq_energy_logic.h"

namespace {

bool near(float actual, float expected, float tolerance = 0.01f) { return fabsf(actual - expected) < tolerance; }

}  // namespace

int main() {
  assert(isnan(oq_energy::ratio_or_nan(NAN, 1.0f, 0.01f)));
  assert(isnan(oq_energy::ratio_or_nan(1.0f, NAN, 0.01f)));
  assert(isnan(oq_energy::ratio_or_nan(1.0f, 0.009f, 0.01f)));
  assert(oq_energy::ratio_or_nan(1.0f, 0.01f, 0.01f) == 100.0f);
  assert(oq_energy::ratio_or_nan(4.0f, 2.0f, 0.01f) == 2.0f);

  const oq_energy::HpElectricalInputs idle{NAN, NAN, NAN, false, false, NAN, false, false};
  const oq_energy::HpElectricalInputs nominal{230.0f, 2.0f, 50.0f, true, true, 60.0f, true, true};
  assert(near(oq_energy::hp_input_power(idle), 5.150232f));
  assert(near(oq_energy::hp_input_power(nominal), 769.8295f));
  assert(oq_energy::hp_input_power({0.0f, 0.0f, 0.0f, false, true, 100.0f, false, false}) ==
         oq_energy::hp_input_power({0.0f, 0.0f, 0.0f, false, false, NAN, false, false}));
  assert(oq_energy::hp_input_power({0.0f, 0.0f, 0.0f, true, false, 100.0f, false, false}) ==
         oq_energy::hp_input_power({0.0f, 0.0f, 0.0f, true, false, NAN, false, false}));
  assert(oq_energy::hp_input_power({0.0f, 0.0f, 1000.0f, false, false, 0.0f, false, false}) == 0.0f);

  assert(near(oq_energy::hp_heating_power(2.0f, 20.0f, 25.0f, 360.0f, 4186.0f), 2093.0f));
  assert(near(oq_energy::hp_heating_power(2.0f, 25.0f, 20.0f, 360.0f, 4186.0f), -2093.0f));
  assert(oq_energy::hp_heating_power(1.0f, 20.0f, 25.0f, 360.0f, 4186.0f) == 0.0f);
  assert(oq_energy::hp_heating_power(2.0f, NAN, 25.0f, 360.0f, 4186.0f) == 0.0f);
  assert(near(oq_energy::hp_cooling_power(1.0f, 25.0f, 20.0f, 360.0f, 4186.0f), 2093.0f));
  assert(oq_energy::hp_cooling_power(1.0f, 20.0f, 25.0f, 360.0f, 4186.0f) == 0.0f);
  assert(oq_energy::hp_cooling_power(2.0f, 25.0f, 20.0f, 360.0f, 4186.0f) == 0.0f);
  assert(oq_energy::hp_cooling_power(1.0f, 25.0f, NAN, 360.0f, 4186.0f) == 0.0f);

  assert(oq_energy::nonnegative_sum(-5.0f, NAN) == 0.0f);
  assert(oq_energy::nonnegative_sum(100.0f, NAN) == 100.0f);
  assert(isnan(oq_energy::sum_available(NAN, NAN)));
  assert(oq_energy::sum_available(NAN, 200.0f) == 200.0f);
  assert(oq_energy::sum_available(100.0f, 200.0f) == 300.0f);
  assert(oq_energy::heating_input_power(true, 2.0f, 100.0f, 2.0f, 200.0f) == 0.0f);
  assert(oq_energy::heating_input_power(false, 2.0f, 100.0f, 1.0f, 200.0f) == 100.0f);
  assert(oq_energy::heating_input_power(false, 2.0f, NAN, 2.0f, 200.0f) == 200.0f);
  assert(oq_energy::cooling_input_power(false, 100.0f, 200.0f) == 0.0f);
  assert(oq_energy::cooling_input_power(true, NAN, 200.0f) == 200.0f);

  assert(isnan(oq_energy::instant_ratio_or_nan(NAN, 5.0f, 5.0f)));
  assert(isnan(oq_energy::instant_ratio_or_nan(10.0f, 4.999f, 5.0f)));
  assert(oq_energy::instant_ratio_or_nan(10.0f, 5.0f, 5.0f) == 2.0f);
  assert(oq_energy::instant_ratio_or_nan(10.0f, -5.0f, 5.0f) == -2.0f);
  return 0;
}
