#pragma once

#include "../control/oq_curve_duo_dispatch.h"

namespace oq_performance_observer {
using Powers = std::array<std::array<float, 21>, 2>;
struct Reference {
  float outside_c = NAN, water_c = NAN;
};
using PhHook = void (*)(const oq_power_house_dispatch::DispatchInput&, const oq_power_house_dispatch::DispatchTuning&,
                        const oq_power_house_dispatch::DispatchState&, const oq_power_house_dispatch::DispatchDecision&,
                        Reference);
using CurveHook = void (*)(const oq_curve_duo::Input&, const oq_curve_duo::State&, const Powers&,
                           const oq_curve_duo::Decision&, Reference);
// Hooks are installed/removed only by the ESPHome main loop. HTTP switch
// callbacks merely set an atomic request in the observer runtime.
inline PhHook ph_hook = nullptr;
inline CurveHook curve_hook = nullptr;
}  // namespace oq_performance_observer
