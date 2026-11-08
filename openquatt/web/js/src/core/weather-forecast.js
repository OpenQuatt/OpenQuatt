import { state } from "./state.js";
import { getEntityValue } from "./entity-store.js";
import { getBasePath } from "./url-path.js";

const integer = (value) => Number.isInteger(value) && value >= 0 && value <= 0xffffffff;
const finiteRange = (value, low, high) => typeof value === "number" && Number.isFinite(value) && value >= low && value <= high;
const MODEL = "knmi_harmonie_arome_netherlands";
const SCENARIO_HORIZONS = [1800, 3600, 10800];
export const getWeatherForecastEndpoint = () => `${getBasePath()}/openquatt/weather/forecast`;

export function weatherLocationReceipt() {
  const entity = state.entities.weatherLocation;
  return JSON.stringify([entity?.save_request ?? "", entity?.save_status ?? ""]);
}

export function weatherConsentBlocksForecast() {
  const consent = state.entities.weatherStatus?.consent_state;
  return state.weatherOptOutUnconfirmed || (consent !== undefined && consent !== "on");
}

function normalizeForecastScenario(scenario, forecast) {
  const fail = () => { throw new Error("weather-scenario-format"); };
  if (!scenario || typeof scenario !== "object" || Array.isArray(scenario) || scenario.schema !== 1
    || typeof scenario.valid !== "boolean" || typeof scenario.reason !== "string" || !scenario.reason || scenario.reason.length > 48
    || (Object.hasOwn(scenario, "restored_model") && typeof scenario.restored_model !== "boolean")
    || ![scenario.issued_epoch, scenario.configuration, scenario.weather_generation, scenario.weather_fetched_epoch,
      scenario.weather_start_epoch, scenario.model_context, scenario.model_samples].every(integer)
    || ![scenario.room_start_c, scenario.held_heat_w, scenario.h_w_per_k, scenario.c_wh_per_k]
      .every(value => value === null || typeof value === "number" && Number.isFinite(value))
    || !Array.isArray(scenario.horizon_valid) || scenario.horizon_valid.length !== 3 || !scenario.horizon_valid.every(value => typeof value === "boolean")
    || !Array.isArray(scenario.temperature_c) || scenario.temperature_c.length !== 3) fail();
  for (let i = 0; i < 3; i++) {
    if (scenario.horizon_valid[i]) {
      if (!scenario.valid || typeof scenario.temperature_c[i] !== "number" || !Number.isFinite(scenario.temperature_c[i])
        || scenario.issued_epoch + SCENARIO_HORIZONS[i] <= forecast.now_epoch) fail();
    } else if (scenario.temperature_c[i] !== null) fail();
  }
  if (scenario.valid) {
    if (scenario.reason !== "ready" || !forecast.enabled || !forecast.valid || !scenario.horizon_valid.some(Boolean)
      || !scenario.issued_epoch || scenario.issued_epoch > forecast.now_epoch || !scenario.configuration || scenario.configuration !== forecast.configuration
      || !scenario.weather_generation || !scenario.weather_fetched_epoch || !scenario.model_context || !scenario.model_samples
      || scenario.weather_fetched_epoch > scenario.issued_epoch || scenario.weather_start_epoch % 3600 !== 0
      || scenario.weather_start_epoch > scenario.issued_epoch || scenario.weather_start_epoch + 35 * 3600 < scenario.issued_epoch + 10800
      || !finiteRange(scenario.room_start_c, 0, 40) || !finiteRange(scenario.held_heat_w, -50000, 50000)
      || !finiteRange(scenario.h_w_per_k, 20, 1500) || !finiteRange(scenario.c_wh_per_k, 500, 100000)) fail();
  } else if (scenario.reason === "ready" || scenario.horizon_valid.some(Boolean)) fail();
  return { ...scenario, horizon_valid: [...scenario.horizon_valid], temperature_c: [...scenario.temperature_c] };
}

// The controller owns weather access and calculates the scenario.
// The browser checks receipt identity and freshness before drawing it.
export function normalizeWeatherForecast(payload, receivedAt = Date.now(), receivedMono = performance.now()) {
  const fail = () => { throw new Error("weather-forecast-format"); };
  if (!payload || payload.schema !== 1 || payload.model !== MODEL || typeof payload.enabled !== "boolean" || typeof payload.valid !== "boolean"
    || typeof payload.configured_location !== "string" || new TextEncoder().encode(payload.configured_location).length > 64
    || typeof payload.location !== "string" || new TextEncoder().encode(payload.location).length > 127
    || ![payload.now_epoch, payload.fetched_epoch, payload.generation, payload.configuration, payload.start_epoch].every(integer)
    || payload.step_s !== 3600 || !payload.current || typeof payload.current.valid !== "boolean") fail();
  const current = payload.current;
  if (!integer(current.time_epoch)) fail();
  if (payload.valid) {
    if (!payload.enabled || !payload.generation || !payload.now_epoch || !payload.fetched_epoch || payload.fetched_epoch > payload.now_epoch
      || payload.now_epoch - payload.fetched_epoch > 10800 || payload.start_epoch % 3600 !== 0
      || payload.start_epoch > payload.now_epoch || payload.start_epoch + 35 * 3600 < payload.now_epoch + 10800
      || !Array.isArray(payload.temperature_c) || payload.temperature_c.length !== 36 || !payload.temperature_c.every((v) => finiteRange(v, -40, 55))
      || !Array.isArray(payload.wind_mps) || payload.wind_mps.length !== 36 || !payload.wind_mps.every((v) => finiteRange(v, 0, 75))) fail();
  }
  if (current.valid && (!payload.valid || current.source !== "model" || !current.time_epoch || current.time_epoch % 900 !== 0
    || current.time_epoch > payload.now_epoch || payload.now_epoch - current.time_epoch > 1800
    || !finiteRange(current.temperature_c, -40, 55) || !finiteRange(current.relative_humidity_pct, 0, 100)
    || !finiteRange(current.dew_point_c, -40, 35) || current.dew_point_c > current.temperature_c + 0.5)) fail();
  return {
    ...payload, current: { ...current },
    ...(Object.hasOwn(payload, "scenario") ? { scenario: normalizeForecastScenario(payload.scenario, payload) } : {}),
    temperature_c: payload.valid ? [...payload.temperature_c] : [], wind_mps: payload.valid ? [...payload.wind_mps] : [],
    receivedAt, receivedMono, endpoint: getWeatherForecastEndpoint(),
  };
}

export function weatherForecastCanDisplay(data, now = Date.now(), mono = performance.now()) {
  if (weatherConsentBlocksForecast() || ["save-weatherLocation", "switch-weatherEnabled"].includes(state.busyAction) || !data?.valid || data.enabled !== true || getEntityValue("weatherEnabled") !== true
    || data.endpoint !== getWeatherForecastEndpoint()
    || data.configured_location !== String(getEntityValue("weatherLocation") ?? "")
    || Object.hasOwn(data, "locationReceipt") && data.locationReceipt !== weatherLocationReceipt()) return false;
  const elapsed = (mono - data.receivedMono) / 1000;
  if (!Number.isFinite(elapsed) || elapsed < 0 || Math.abs((now - data.receivedAt) / 1000 - elapsed) > 30) return false;
  const controllerNow = data.now_epoch + elapsed;
  return controllerNow - data.fetched_epoch <= 10800 && data.start_epoch + 35 * 3600 - controllerNow >= 10800;
}

export function weatherForecastIsCurrent(data, now = Date.now(), mono = performance.now()) {
  return !state.weatherForecastError && weatherForecastCanDisplay(data, now, mono) && mono - data.receivedMono <= 30000;
}

export function weatherCurrentModelIsCurrent(data, now = Date.now(), mono = performance.now()) {
  return weatherForecastIsCurrent(data, now, mono) && data.current.valid
    && data.now_epoch + (mono - data.receivedMono) / 1000 - data.current.time_epoch <= 1800;
}

export function weatherScenarioCanDisplay(data, now = Date.now(), mono = performance.now()) {
  // Keep a frozen issue through a brief transport interruption. Weather can
  // outlive it, but a room estimate needs recent controller confirmation.
  return Boolean(data?.scenario?.valid && data.scenario.restored_model !== true && data.scenario.configuration === data.configuration
    && getEntityValue("houseLearningEnabled") === true && state.busyAction !== "switch-houseLearningEnabled"
    && !["pending", "uncertain"].includes(state.houseLearningReset)
    && mono - data.receivedMono <= 30000 && weatherForecastCanDisplay(data, now, mono));
}

export function weatherScenarioNow(data, now = Date.now(), mono = performance.now()) {
  return weatherForecastCanDisplay(data, now, mono) ? data.now_epoch + (mono - data.receivedMono) / 1000 : NaN;
}
