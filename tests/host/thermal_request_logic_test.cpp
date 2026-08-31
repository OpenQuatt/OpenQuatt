#include <assert.h>
#include <limits>
#include <string>
#include "../../openquatt/includes/control/oq_thermal_request_logic.h"

int main() {
  using oq_request::make_published_request;
  using oq_request::min_runtime_hold_required;
  using oq_request::min_runtime_window_active;
  const auto automatic_request = make_published_request(2, 20, 20, 1);
  assert(automatic_request.hp1_level == 10 && automatic_request.hp2_level == 10);
  const auto manual_request = make_published_request(2, 20, 20, 4, 20, 10);
  assert(manual_request.hp1_level == 20 && manual_request.hp2_level == 10);
  constexpr uint32_t start_ms = 1000UL;
  constexpr uint32_t min_runtime_ms = 300000UL;
  const bool runtime_active = min_runtime_window_active(start_ms + 120000UL, start_ms, min_runtime_ms);
  assert(runtime_active && min_runtime_window_active(start_ms, start_ms, min_runtime_ms));
  // Unsigned elapsed-time arithmetic keeps the hold valid across millis() wrap.
  constexpr uint32_t wrap_start_ms = UINT32_MAX - 1000UL;
  assert(min_runtime_window_active(1000UL, wrap_start_ms, 5000UL));
  assert(!min_runtime_window_active(1000UL, 0, min_runtime_ms));
  assert(!min_runtime_window_active(start_ms, start_ms, 0));
  const bool runtime_expired = min_runtime_window_active(start_ms + min_runtime_ms, start_ms, min_runtime_ms);
  assert(!runtime_expired);
  struct HoldCase {
    int request, applied;
    bool inhibit, window, cooling, expected;
  };
  const HoldCase cases[] = {{0, 1, false, runtime_active, true, true},   {0, 4, false, runtime_active, false, true},
                            {1, 1, false, runtime_active, true, false},  {0, 0, false, runtime_active, false, false},
                            {0, 1, false, runtime_expired, true, false}, {0, 1, true, runtime_active, true, false}};
  for (const auto& c : cases)
    assert(min_runtime_hold_required(c.request, c.inhibit, c.window, c.applied, c.cooling) == c.expected);

  const auto cooling = oq_request::resolve_mode_context(5, 0);
  assert(cooling.cooling && cooling.curve && !cooling.power_house && cooling.cm_allows_hp);
  assert(cooling.thermal_mode_code == 1 && cooling.strategy_code == oq_request::STRATEGY_COOLING);
  const auto inactive_power_house = oq_request::resolve_mode_context(1, 0);
  assert(inactive_power_house.power_house && !inactive_power_house.cm_allows_hp);

  oq_request::StrategyRequestInput source{cooling, true, 3, 4, 2, 2, 5, 6, 1, 2, 7, 8, 2};
  auto selected = oq_request::select_strategy_request(source);
  assert(selected.hp1_level == 3 && selected.hp2_level == 4 && selected.owner_hp == 2);
  assert(std::string(selected.reason) == "cooling_owner_hp2");
  source.duo = false;
  selected = oq_request::select_strategy_request(source);
  assert(selected.hp1_level == 3 && selected.hp2_level == 0 && selected.owner_hp == 2);

  source.mode = oq_request::resolve_mode_context(2, 1);
  source.duo = true;
  selected = oq_request::select_strategy_request(source);
  assert(selected.hp1_level == 5 && selected.hp2_level == 6 && std::string(selected.reason) == "curve_dual");
  source.duo = false;
  source.curve_owner = 0;
  selected = oq_request::select_strategy_request(source);
  assert(selected.hp1_level == 5 && selected.hp2_level == 0 && std::string(selected.reason) == "curve_single_hp1");
  source.mode = oq_request::resolve_mode_context(2, 0);
  source.duo = true;
  selected = oq_request::select_strategy_request(source);
  assert(selected.hp1_level == 7 && selected.hp2_level == 8 && selected.reason == nullptr);

  oq_request::ManualRequestInput manual{true, 2, 2, 20, 4, 10, 10, 0, 0, false, false, false, false, false};
  auto manual_result = oq_request::arbitrate_manual_request(manual);
  assert(manual_result.mode_allowed && manual_result.mode_code == 2);
  assert(manual_result.hp1_level == 10 && manual_result.hp2_level == 4);
  manual.hp2_mode_code = 1;
  manual_result = oq_request::arbitrate_manual_request(manual);
  assert(manual_result.mode_conflict && !manual_result.mode_allowed);
  assert(manual_result.hp1_level == 0 && manual_result.hp2_level == 0);
  manual.hp2_mode_code = 0;
  manual.hp1_mode_code = 0;
  manual.hp1_requested_level = 0;
  manual.hp1_hold_level = 1;
  manual.hp1_cooling_hold = true;
  manual.stop_requested = true;
  manual_result = oq_request::arbitrate_manual_request(manual);
  assert(manual_result.mode_code == 1 && manual_result.hp1_level == 1);
  manual.safety_stop = true;
  manual_result = oq_request::arbitrate_manual_request(manual);
  assert(manual_result.hp1_level == 0 && manual_result.mode_code == 0);
  manual = {true, 2, 0, 4, 0, 10, 10, 0, 0, false, false, false, false, true};
  manual_result = oq_request::arbitrate_manual_request(manual);
  assert(!manual_result.mode_allowed && manual_result.reason == oq_request::MANUAL_STARTUP_INHIBIT);
  assert(manual_result.desired_hp1_level == 4 && manual_result.hp1_level == 0);

  assert(!oq_request::thermal_mode_matches(std::numeric_limits<float>::infinity(), 1));
  assert(!oq_request::thermal_mode_matches(std::numeric_limits<float>::quiet_NaN(), 1));
  assert(oq_request::thermal_mode_matches(1.0f, 1));
  assert(!oq_request::finite_value_at_least(false, 900.0f, 400.0f) &&
         !oq_request::finite_value_at_least(true, std::numeric_limits<float>::infinity(), 400.0f) &&
         !oq_request::finite_value_at_least(true, std::numeric_limits<float>::quiet_NaN(), 400.0f) &&
         oq_request::finite_value_at_least(true, 400.0f, 400.0f));
  assert(oq_request::minimum_runtime_seconds(false, 900.0f, 300) == 300 &&
         oq_request::minimum_runtime_seconds(true, std::numeric_limits<float>::quiet_NaN(), 300) == 300 &&
         oq_request::minimum_runtime_seconds(true, 0.0f, 300) == 300 &&
         oq_request::minimum_runtime_seconds(true, 600.0f, 300) == 600);
  assert(oq_request::hold_request_mode_code(1, 0, true, false) == 1);
  assert(oq_request::hold_request_mode_code(1, 0, false, false) == 2);
  assert(oq_request::defrost_hold_level(true, false, 0, 4) == 4);
  assert(oq_request::defrost_hold_level(true, true, 4, 4) == 0);

  const auto too_soon = oq_request::cadence_decision(1500, 1000, 1000);
  assert(!too_soon.due);
  const auto wrapped_due = oq_request::cadence_decision(500, UINT32_MAX - 800, 1000);
  assert(wrapped_due.due && wrapped_due.dt_ms == 1301);
  assert(oq_request::deadline_pending(500, 1500));
  assert(oq_request::deadline_pending(UINT32_MAX - 500, 500));
  assert(!oq_request::deadline_pending(1500, 1500));

  assert(oq_request::limit_level_slew(6, 2, false, 5000, 0, 10000, 10000) == 3);
  assert(oq_request::limit_level_slew(6, 2, false, 5000, 4000, 10000, 10000) == 2);
  assert(oq_request::limit_level_slew(0, 4, true, 5000, 4000, 10000, 10000) == 0);
  assert(oq_request::limit_level_slew(4, 2, false, 500, UINT32_MAX - 1000, 2000, 2000) == 2);
  int duo_hp1 = 2;
  int duo_hp2 = 2;
  oq_request::limit_duo_to_one_change(duo_hp1, duo_hp2, 1, 1, false);
  assert(duo_hp1 == 1 && duo_hp2 == 2);
  return 0;
}
