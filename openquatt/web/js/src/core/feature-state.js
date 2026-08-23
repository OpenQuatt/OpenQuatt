import { DEBUG_RECORDING_KEYS, ENTITY_DEFS } from "./config.js";
import { state } from "./state.js";

// Test-only Q flow-filter probes. Keep these out of the normal UI, but make
// them resolvable by the device-side debug recorder so field testers do not
// need Home Assistant to compare the three pulse-filter variants.
ENTITY_DEFS.controllerFlowEdge100 = {
  domain: "sensor",
  name: "Controller Flow test EDGE 100us",
  optional: true,
};
ENTITY_DEFS.controllerFlowPulse20 = {
  domain: "sensor",
  name: "Controller Flow test PULSE 20us",
  optional: true,
};

const REQUIRED_DEBUG_RECORDING_KEYS = [
  "otbMaxCapacity",
  "otbMinModulation",
  "flowSource",
  "qFlowSource",
  "controllerFlow",
  "controllerFlowEdge100",
  "controllerFlowPulse20",
  "cicFlowrate",
];
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
