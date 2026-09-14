#include <assert.h>
#include <math.h>

#include <string>

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

  const oq_energy::HpElectricalInputs v2_nominal{230.0f, 2.0f, 500.0f, true, true, 30.0f, false, false};
  const auto v2_old = oq_energy::hp_input_power_for_variant(oq_odu::Variant::V2_OLD_MODEL, v2_nominal, true);
  const auto v2_new = oq_energy::hp_input_power_for_variant(oq_odu::Variant::V2_NEW_MODEL, v2_nominal, true);
  assert(v2_old.status == oq_energy::HpInputPowerStatus::V2_ESTIMATED);
  assert(std::string(oq_energy::hp_input_power_status_name(v2_old.status)) == "v2_estimated");
  assert(v2_old.available() && near(v2_old.watts, 501.13745f, 0.002f));
  assert(v2_old.watts == v2_new.watts);
  auto v2_heaters = v2_nominal;
  v2_heaters.bottom_plate_heater = true;
  v2_heaters.crankcase_heater = true;
  assert(
      near(oq_energy::hp_input_power_for_variant(oq_odu::Variant::V2_OLD_MODEL, v2_heaters, true).watts - v2_old.watts,
           173.62f, 0.002f));
  assert(oq_energy::hp_input_power_for_variant(oq_odu::Variant::V2_OLD_MODEL, v2_nominal, false).status ==
         oq_energy::HpInputPowerStatus::INVALID_OR_STALE);
  assert(oq_energy::hp_input_power_for_variant(oq_odu::Variant::UNKNOWN, v2_nominal, true).status ==
         oq_energy::HpInputPowerStatus::UNKNOWN_VARIANT);
  auto invalid_v2 = v2_nominal;
  invalid_v2.voltage_v = 0.0f;
  assert(!oq_energy::hp_input_power_for_variant(oq_odu::Variant::V2_NEW_MODEL, invalid_v2, true).available());
  invalid_v2 = v2_nominal;
  invalid_v2.pump_power_w = NAN;
  assert(!oq_energy::hp_input_power_for_variant(oq_odu::Variant::V2_NEW_MODEL, invalid_v2, true).available());
  invalid_v2.pump_relay_running = false;
  assert(oq_energy::hp_input_power_for_variant(oq_odu::Variant::V2_NEW_MODEL, invalid_v2, true).available());

  oq_energy::HpElectricalFreshness freshness{true, 1000U, 100U, 950U, 950U, 950U, 950U, 950U};
  assert(oq_energy::v2_input_telemetry_fresh(freshness, true));
  freshness.fan_updated_ms = 899U;
  assert(!oq_energy::v2_input_telemetry_fresh(freshness, true));
  freshness.fan_updated_ms = 950U;
  freshness.pump_updated_ms = 0U;
  assert(oq_energy::v2_input_telemetry_fresh(freshness, false));
  assert(!oq_energy::v2_input_telemetry_fresh(freshness, true));
  freshness.online = false;
  assert(!oq_energy::v2_input_telemetry_fresh(freshness, false));
  assert(oq_energy::measurement_fresh(5U, UINT32_MAX - 4U, 10U));

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
  assert(isnan(oq_energy::nonnegative_sum_required(NAN)));
  assert(oq_energy::nonnegative_sum_required(100.0f, NAN, false) == 100.0f);
  assert(isnan(oq_energy::nonnegative_sum_required(100.0f, NAN, true)));
  assert(oq_energy::nonnegative_sum_required(100.0f, 200.0f, true) == 300.0f);
  assert(isnan(oq_energy::sum_available(NAN, NAN)));
  assert(oq_energy::sum_available(NAN, 200.0f) == 200.0f);
  assert(oq_energy::sum_available(100.0f, 200.0f) == 300.0f);
  assert(oq_energy::heating_input_power(true, 2.0f, 100.0f, 2.0f, 200.0f) == 0.0f);
  assert(oq_energy::heating_input_power(false, 2.0f, 100.0f, 1.0f, 200.0f) == 100.0f);
  assert(oq_energy::heating_input_power(false, 2.0f, NAN, 2.0f, 200.0f) == 200.0f);
  assert(isnan(oq_energy::heating_input_power_required(false, 2.0f, NAN)));
  assert(isnan(oq_energy::heating_input_power_required(false, 2.0f, 100.0f, true, 2.0f, NAN)));
  assert(oq_energy::heating_input_power_required(false, 2.0f, 100.0f, true, 1.0f, NAN) == 100.0f);
  assert(oq_energy::cooling_input_power(false, 100.0f, 200.0f) == 0.0f);
  assert(oq_energy::cooling_input_power(true, NAN, 200.0f) == 200.0f);

  assert(isnan(oq_energy::instant_ratio_or_nan(NAN, 5.0f, 5.0f)));
  assert(isnan(oq_energy::instant_ratio_or_nan(10.0f, 4.999f, 5.0f)));
  assert(oq_energy::instant_ratio_or_nan(10.0f, 5.0f, 5.0f) == 2.0f);
  assert(oq_energy::instant_ratio_or_nan(10.0f, -5.0f, 5.0f) == -2.0f);
  return 0;
}
