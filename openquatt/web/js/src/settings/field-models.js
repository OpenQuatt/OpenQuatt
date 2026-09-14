import { getEntityValue, hasEntity } from "../core/entity-store.js";
import { state } from "../core/state.js";

function getFieldBusy(key, action = "save") {
  return state.loadingEntities
    || state.busyAction === `${action}-${key}`
    || (action === "save" && key === "strategy" && state.busyAction === "save-heatingEnableSource");
}

export function getSelectEntityOptions(entity = {}) {
  if (Array.isArray(entity.option)) return entity.option;
  if (Array.isArray(entity.options)) return entity.options;
  return [];
}

export function getSettingsSelectModel(key) {
  return {
    available: hasEntity(key),
    value: String(getEntityValue(key) || ""),
    options: getSelectEntityOptions(state.entities[key] || {}),
    busy: getFieldBusy(key),
  };
}

export function getSettingsChoiceModel(key, option, { model = getSettingsSelectModel(key), currentValue = model.value, busy = model.busy } = {}) {
  return {
    active: option === currentValue,
    busy,
  };
}

export function getSettingsSwitchModel(key, { enabled = Boolean(getEntityValue(key)), busy = getFieldBusy(key, "switch"), title = key, onLabel = "Aan", offLabel = "Uit" } = {}) {
  const label = enabled ? onLabel : offLabel;
  return {
    enabled,
    busy,
    label,
    nextState: enabled ? "off" : "on",
    ariaLabel: `${title}: ${label}`,
  };
}
