import { DEBUG_RECORDING_KEYS, ENTITY_DEFS } from "./config.js";
import { state } from "./state.js";

const TEST_FLOW_ENTITIES = [
  ["Edge100", "EDGE 100us"],
  ["Pulse13", "PULSE 13us"],
  ["Pulse20", "PULSE 20us"],
  ["Pulse50", "PULSE 50us"],
  ["RawRisingHz", "raw rising Hz"],
  ["RawRisingCount", "raw rising count 10s"],
  ["PulseWidthMin", "pulse width min"],
  ["PulseWidthAvg", "pulse width avg"],
  ["PulseWidthMax", "pulse width max"],
  ["PulseWidthLt20", "pulse width <20us"],
  ["PulseWidth20To50", "pulse width 20-50us"],
  ["PulseWidth50To100", "pulse width 50-100us"],
  ["PulseWidthGe100", "pulse width >=100us"],
];

for (const key of ["otbMaxCapacity", "otbMinModulation", "flowSource", "qFlowSource", "controllerFlow"]) {
  if (!DEBUG_RECORDING_KEYS.includes(key)) DEBUG_RECORDING_KEYS.push(key);
}

for (const [suffix, label] of TEST_FLOW_ENTITIES) {
  const key = `controllerFlow${suffix}`;
  ENTITY_DEFS[key] = { domain: "sensor", name: `Controller Flow test ${label}`, optional: true };
  if (!DEBUG_RECORDING_KEYS.includes(key)) DEBUG_RECORDING_KEYS.push(key);
}

if (!DEBUG_RECORDING_KEYS.includes("cicFlowrate")) DEBUG_RECORDING_KEYS.push("cicFlowrate");

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
