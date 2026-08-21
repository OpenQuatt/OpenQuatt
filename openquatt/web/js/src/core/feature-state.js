import { DEBUG_RECORDING_KEYS } from "./config.js";
import { state } from "./state.js";

const REQUIRED_DEBUG_RECORDING_KEYS = ["otbMaxCapacity", "otbMinModulation"];
for (const key of REQUIRED_DEBUG_RECORDING_KEYS) {
  if (!DEBUG_RECORDING_KEYS.includes(key)) {
    DEBUG_RECORDING_KEYS.push(key);
  }
}

const stateDomains = {
  debugRecording: (key) => key.startsWith("debugRecording"),
  energyHistory: (key) => key.startsWith("energyHistory"),
  firmware: (key) => key === "updateModalOpen" || key.startsWith("update") || key.startsWith("firmware"),
  mqtt: (key) => key.startsWith("mqtt"),
  webServerLog: (key) => key.startsWith("webServerLog"),
};

function updateFeatureState(domain, patch) {
  const ownsKey = stateDomains[domain];
  const foreignKey = Object.keys(patch).find((key) => !ownsKey(key));
  if (foreignKey) {
    throw new Error(`${domain} state beheert sleutel ${foreignKey} niet.`);
  }
  Object.assign(state, patch);
}

export const updateDebugRecordingState = (patch) => updateFeatureState("debugRecording", patch);
export const updateEnergyHistoryState = (patch) => updateFeatureState("energyHistory", patch);
export const updateFirmwareState = (patch) => updateFeatureState("firmware", patch);
export const updateMqttState = (patch) => updateFeatureState("mqtt", patch);
export const updateWebServerLogState = (patch) => updateFeatureState("webServerLog", patch);
