import { DEBUG_RECORDING_KEYS, ENTITY_DEFS } from "./config.js";
import { state } from "./state.js";

const TEST_FLOW_ENTITIES = {
  controllerFlowEdge100: "Controller Flow test EDGE 100us",
  controllerFlowPulse13: "Controller Flow test PULSE 13us",
  controllerFlowPulse20: "Controller Flow test PULSE 20us",
  controllerFlowPulse50: "Controller Flow test PULSE 50us",
  controllerFlowRawRisingHz: "Controller Flow test raw rising Hz",
  controllerFlowRawRisingCount: "Controller Flow test raw rising count 10s",
  controllerFlowPulseWidthMin: "Controller Flow test pulse width min",
  controllerFlowPulseWidthAvg: "Controller Flow test pulse width avg",
  controllerFlowPulseWidthMax: "Controller Flow test pulse width max",
  controllerFlowPulseWidthLt20: "Controller Flow test pulse width <20us",
  controllerFlowPulseWidth20To50: "Controller Flow test pulse width 20-50us",
  controllerFlowPulseWidth50To100: "Controller Flow test pulse width 50-100us",
  controllerFlowPulseWidthGe100: "Controller Flow test pulse width >=100us",
};

for (const [key, name] of Object.entries(TEST_FLOW_ENTITIES)) {
  ENTITY_DEFS[key] = { domain: "sensor", name, optional: true };
}

const REQUIRED_DEBUG_RECORDING_KEYS = [
  "otbMaxCapacity",
  "otbMinModulation",
  "flowSource",
  "qFlowSource",
  "controllerFlow",
  ...Object.keys(TEST_FLOW_ENTITIES),
  "cicFlowrate",
];
for (const key of REQUIRED_DEBUG_RECORDING_KEYS) {
  if (!DEBUG_RECORDING_KEYS.includes(key)) DEBUG_RECORDING_KEYS.push(key);
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
  if (foreignKey) throw new Error(`${domain} state beheert sleutel ${foreignKey} niet.`);
  Object.assign(state, patch);
}

export const updateDebugRecordingState = (patch) => updateFeatureState("debugRecording", patch);
export const updateEnergyHistoryState = (patch) => updateFeatureState("energyHistory", patch);
export const updateFirmwareState = (patch) => updateFeatureState("firmware", patch);
export const updateMqttState = (patch) => updateFeatureState("mqtt", patch);
export const updateWebServerLogState = (patch) => updateFeatureState("webServerLog", patch);
