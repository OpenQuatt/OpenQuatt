#pragma once

#include <math.h>
#include <stdint.h>

namespace oq_input_source {

enum class Source : uint8_t {
  NONE = 0,
  AUTO,
  LOCAL,
  OUTDOOR,
  CIC,
  HA,
  API,
  MQTT,
  OPENTHERM,
  DISABLED,
  CIC_OR_HA,
};

struct NumericSample {
  float value = NAN;
  bool valid = false;
};

inline NumericSample numeric_sample(bool enabled, bool has_state, float value) {
  return {value, enabled && has_state && isfinite(value)};
}

struct BinarySample {
  bool value = false;
  bool valid = false;
};

struct TimedState {
  bool has_value = false;
  uint32_t last_update_ms = 0;

  void reset() {
    has_value = false;
    last_update_ms = 0;
  }

  void observe(uint32_t now_ms) {
    has_value = true;
    last_update_ms = now_ms;
  }
};

struct Freshness {
  float age_s = NAN;
  bool valid = false;
};

inline uint32_t seconds_to_millis(uint32_t seconds) {
  constexpr uint32_t max_seconds = UINT32_MAX / 1000U;
  return seconds > max_seconds ? UINT32_MAX : seconds * 1000U;
}

inline Freshness evaluate_freshness(const TimedState& state, uint32_t now_ms, uint32_t stale_s, bool entity_valid) {
  if (!state.has_value || !entity_valid) return {};
  const uint32_t age_ms = now_ms - state.last_update_ms;
  return {static_cast<float>(age_ms) / 1000.0f, stale_s == 0U || age_ms <= seconds_to_millis(stale_s)};
}

struct NumericSources {
  NumericSample local;
  NumericSample outdoor;
  NumericSample cic;
  NumericSample ha;
  NumericSample api;
  NumericSample mqtt;
  NumericSample opentherm;
};

inline NumericSample sample_for(Source source, const NumericSources& sources) {
  switch (source) {
    case Source::LOCAL:
      return sources.local;
    case Source::OUTDOOR:
      return sources.outdoor;
    case Source::CIC:
      return sources.cic;
    case Source::HA:
      return sources.ha;
    case Source::API:
      return sources.api;
    case Source::MQTT:
      return sources.mqtt;
    case Source::OPENTHERM:
      return sources.opentherm;
    default:
      return {};
  }
}

struct NumericSelection {
  float value = NAN;
  Source route = Source::NONE;
  bool valid = false;
  bool held = false;
};

struct HoldState {
  bool has_value = false;
  float value = NAN;
  uint32_t last_valid_ms = 0;
  Source source = Source::NONE;

  void reset() {
    has_value = false;
    value = NAN;
    last_valid_ms = 0;
    source = Source::NONE;
  }

  void remember(float current, uint32_t now_ms, Source current_source) {
    if (!isfinite(current) || current_source == Source::NONE) {
      reset();
      return;
    }
    has_value = true;
    value = current;
    last_valid_ms = now_ms;
    source = current_source;
  }

  bool available(Source current_source, uint32_t now_ms, uint32_t hold_ms) const {
    return has_value && source == current_source && isfinite(value) && hold_ms > 0U && now_ms - last_valid_ms < hold_ms;
  }
};

inline NumericSelection select_direct(Source selected, const NumericSources& sources, bool hold_ha, uint32_t now_ms,
                                      uint32_t hold_ms, HoldState& hold) {
  if (!hold_ha || selected != Source::HA) hold.reset();
  const auto sample = sample_for(selected, sources);
  if (sample.valid) {
    if (hold_ha && selected == Source::HA)
      hold.remember(sample.value, now_ms, selected);
    else
      hold.reset();
    return {sample.value, selected, true, false};
  }
  if (hold_ha && selected == Source::HA && hold.available(selected, now_ms, hold_ms)) {
    return {hold.value, selected, true, true};
  }
  return {};
}

inline NumericSelection select_lowest_outside(const NumericSources& sources) {
  NumericSelection selected;
  const auto take = [&](Source route, const NumericSample& sample) {
    if (sample.valid && (!selected.valid || sample.value < selected.value)) {
      selected = {sample.value, route, true, false};
    }
  };
  take(Source::HA, sources.ha);
  take(Source::OUTDOOR, sources.outdoor);
  take(Source::API, sources.api);
  take(Source::MQTT, sources.mqtt);
  return selected;
}

inline NumericSelection select_outside(Source selected, const NumericSources& sources, uint32_t now_ms,
                                       uint32_t hold_ms, HoldState& hold) {
  if (selected == Source::AUTO) {
    hold.reset();
    return select_lowest_outside(sources);
  }
  return select_direct(selected, sources, true, now_ms, hold_ms, hold);
}

struct EnableSources {
  BinarySample opentherm;
  BinarySample cic;
  BinarySample ha;
  BinarySample api;
  BinarySample mqtt;
};

inline BinarySample valid_binary(const BinarySample& sample) { return {sample.valid && sample.value, sample.valid}; }

inline BinarySample binary_for(Source source, const EnableSources& sources) {
  switch (source) {
    case Source::OPENTHERM:
      return valid_binary(sources.opentherm);
    case Source::CIC:
      return valid_binary(sources.cic);
    case Source::HA:
      return valid_binary(sources.ha);
    case Source::API:
      return valid_binary(sources.api);
    case Source::MQTT:
      return valid_binary(sources.mqtt);
    case Source::CIC_OR_HA:
      return {(sources.cic.valid && sources.cic.value) || (sources.ha.valid && sources.ha.value),
              sources.cic.valid || sources.ha.valid};
    default:
      return {};
  }
}

inline BinarySample select_heating_enable(Source selected, const EnableSources& sources) {
  if (selected == Source::DISABLED) return {true, true};
  return binary_for(selected, sources);
}

inline BinarySample select_cooling_enable(Source selected, const EnableSources& sources, bool manual_enabled) {
  if (manual_enabled) return {true, true};
  if (selected == Source::DISABLED) return {false, true};
  return binary_for(selected, sources);
}

enum class FlowRoute : uint8_t { NONE = 0, CIC, CONTROLLER, HP1, HP2, AGGREGATE, PUMPS_STOPPED };
enum class ControllerFlowMode : uint8_t { OTHER = 0, LOCAL, AUTO };
enum class OutdoorFlowMode : uint8_t { AGGREGATE = 0, HP1, HP2 };

struct FlowInputs {
  Source selected = Source::NONE;
  bool q_hardware = false;
  bool duo = false;
  bool hp_generation_v1 = false;
  bool all_relevant_pumps_stopped = false;
  ControllerFlowMode controller_mode = ControllerFlowMode::OTHER;
  OutdoorFlowMode outdoor_mode = OutdoorFlowMode::AGGREGATE;
  NumericSample cic;
  NumericSample controller;
  NumericSample hp1;
  NumericSample hp2;
  NumericSample aggregate;
};

struct FlowSelection {
  float value = NAN;
  FlowRoute route = FlowRoute::NONE;
  bool valid = false;
};

inline FlowSelection flow_sample(const NumericSample& sample, FlowRoute route) {
  return sample.valid ? FlowSelection{sample.value, route, true} : FlowSelection{};
}

inline FlowSelection select_flow(const FlowInputs& input) {
  if (input.selected == Source::CIC) return flow_sample(input.cic, FlowRoute::CIC);
  if (input.selected != Source::OUTDOOR) return {};
  const bool controller_only =
      input.q_hardware && (input.controller_mode == ControllerFlowMode::LOCAL ||
                           (!input.duo && input.controller_mode == ControllerFlowMode::AUTO && input.hp_generation_v1));
  if (controller_only) return flow_sample(input.controller, FlowRoute::CONTROLLER);
  if (input.all_relevant_pumps_stopped) return {0.0f, FlowRoute::PUMPS_STOPPED, true};
  if (input.duo && input.outdoor_mode == OutdoorFlowMode::HP1) return flow_sample(input.hp1, FlowRoute::HP1);
  if (input.duo && input.outdoor_mode == OutdoorFlowMode::HP2) return flow_sample(input.hp2, FlowRoute::HP2);
  return flow_sample(input.aggregate, FlowRoute::AGGREGATE);
}

}  // namespace oq_input_source
