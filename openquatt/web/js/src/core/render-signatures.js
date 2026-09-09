import { COOLING_SCHEDULE_EFFECTIVE_SOURCE_KEY, COOLING_SCHEDULE_SOURCE_KEY, SETTINGS_KEYS } from "./config.js";
import { getApiSecurityStatusSignature } from "../features/security-actions.js";
import { state } from "./state.js";

export function getEntitySignatureFragment(key) {
  const entity = state.entities[key];
  if (!entity) {
    if (state.optionalMissingEntities?.[key]) {
      return `${key}:__optional_missing__`;
    }
    return `${key}:__missing__`;
  }

  const value = entity.state ?? entity.value ?? "";
  const options = Array.isArray(entity.option)
    ? entity.option.join(",")
    : Array.isArray(entity.options)
      ? entity.options.join(",")
      : "";
  const meta = [
    entity.min_value ?? "",
    entity.max_value ?? "",
    entity.step ?? "",
    entity.uom ?? "",
  ].join(",");
  return `${key}:${value}::${options}::${meta}`;
}

export function getRenderSignature(value) {
  try {
    return JSON.stringify(value);
  } catch (error) {
    return String(value ?? "");
  }
}

export function getSettingsRenderSignature() {
  return [
    state.appView,
    state.settingsGroup,
    state.busyAction,
    state.loadingEntities ? "loading" : "ready",
    state.incidentMonitoringSignature,
    state.incidentMonitoringError,
    getRenderSignature(state.incidentAction),
    getApiSecurityStatusSignature(),
    getEntitySignatureFragment("setupComplete"),
    ...SETTINGS_KEYS.map((key) => getEntitySignatureFragment(key)),
  ].join("|");
}

export function getOverviewControlsRenderSignature() {
  return [
    state.appView,
    state.busyAction,
    getEntitySignatureFragment("openquattEnabled"),
    getEntitySignatureFragment("openquattResumeAt"),
    getEntitySignatureFragment("manualCoolingEnable"),
    getEntitySignatureFragment(COOLING_SCHEDULE_SOURCE_KEY),
    getEntitySignatureFragment("coolingEnableSelected"),
    getEntitySignatureFragment(COOLING_SCHEDULE_EFFECTIVE_SOURCE_KEY),
    getEntitySignatureFragment("silentModeOverride"),
    getEntitySignatureFragment("controlModeLabel"),
    getEntitySignatureFragment("coolingPermitted"),
    getEntitySignatureFragment("coolingRequestActive"),
    getEntitySignatureFragment("coolingBlockReason"),
    getEntitySignatureFragment("silentActive"),
  ].join("|");
}
