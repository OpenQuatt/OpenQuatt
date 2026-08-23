import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;

const { DEBUG_RECORDING_KEYS, ENTITY_DEFS } = await import("../js/src/core/config.js");
await import("../js/src/core/feature-state.js");

const FIRMWARE_ENTITY_PACKAGES = [
  "../../oq_flow_control.yaml",
  "../../oq_installation_monitoring.yaml",
  "../../oq_boiler_control.yaml",
  "../../oq_boiler_opentherm.yaml",
];

const OBSERVABILITY_KEYS = [
  "flowOutputIpwm",
  "hp1CompressorStarts2h",
  "hp1CompressorStarts6h",
  "hp1CompressorStarts24h",
  "hp1CompressorStarts72h",
  "hp2CompressorStarts2h",
  "hp2CompressorStarts6h",
  "hp2CompressorStarts24h",
  "hp2CompressorStarts72h",
  "boilerActive",
  "boilerCommandValid",
  "boilerCommandActive",
  "boilerCommandAge",
  "boilerCommandSource",
  "boilerCommandTargetTemperature",
  "boilerBlockReason",
  "otbLinkAvailable",
  "otbChCommand",
  "otbControlSetpointCommand",
  "otbChActive",
  "otbFlameOn",
  "otbLastResponseAge",
  "otbResponseCount",
  "otbTransportErrorCount",
  "otbResponseTimeoutCount",
  "otbMaxCapacity",
  "otbMinModulation",
];

const FLOW_FILTER_TEST_KEYS = [
  "flowSource",
  "qFlowSource",
  "controllerFlow",
  "controllerFlowEdge100",
  "controllerFlowPulse13",
  "controllerFlowPulse20",
  "controllerFlowPulse50",
  "controllerFlowRawRisingHz",
  "controllerFlowRawRisingCount",
  "controllerFlowPulseWidthMin",
  "controllerFlowPulseWidthAvg",
  "controllerFlowPulseWidthMax",
  "controllerFlowPulseWidthLt20",
  "controllerFlowPulseWidth20To50",
  "controllerFlowPulseWidth50To100",
  "controllerFlowPulseWidthGe100",
  "cicFlowrate",
];

const FLOW_FILTER_PROBE_ENTITY_KEYS = FLOW_FILTER_TEST_KEYS.filter((key) => key.startsWith("controllerFlow"));
const ADDITIVE_KEYS = [...OBSERVABILITY_KEYS, ...FLOW_FILTER_TEST_KEYS];

test("debugobservability wordt additief achter het bestaande opnamecontract geplaatst", () => {
  const legacyTailIndex = DEBUG_RECORDING_KEYS.indexOf("otLinkProblem");

  assert.equal(legacyTailIndex, 134);
  assert.deepEqual(DEBUG_RECORDING_KEYS.slice(legacyTailIndex + 1), ADDITIVE_KEYS);
  assert.equal(new Set(DEBUG_RECORDING_KEYS).size, DEBUG_RECORDING_KEYS.length);
  assert.ok(DEBUG_RECORDING_KEYS.length <= 188, "debugrecorder heeft maximaal 188 entityvelden naast 4 systeemvelden");
});

test("elk nieuw debugveld heeft een opneembare entitydefinitie", () => {
  for (const key of ADDITIVE_KEYS) {
    assert.ok(ENTITY_DEFS[key], `entitydefinitie ontbreekt voor ${key}`);
    assert.match(ENTITY_DEFS[key].domain, /^(binary_sensor|number|select|sensor|switch|text_sensor)$/);
    assert.ok(key.length < 40, `debugsleutel past niet in DebugField.key: ${key}`);
    assert.ok(ENTITY_DEFS[key].name.length < 48, `entitynaam past niet in DebugField.name: ${ENTITY_DEFS[key].name}`);
  }
});

test("elk regulier observabilityveld verwijst naar een echte firmware-entity", async () => {
  const packages = await Promise.all(
    FIRMWARE_ENTITY_PACKAGES.map((path) => readFile(new URL(path, import.meta.url), "utf8")),
  );
  const firmwareSource = packages.join("\n");

  for (const key of OBSERVABILITY_KEYS) {
    assert.ok(firmwareSource.includes(`name: "${ENTITY_DEFS[key].name}"`), `firmware-entity ontbreekt voor ${key}`);
  }
});

test("Q flow-filter testvelden verwijzen naar de probe-entities", async () => {
  const profile = await readFile(new URL("../../profiles/heatpump_controller_q.yaml", import.meta.url), "utf8");

  for (const key of FLOW_FILTER_PROBE_ENTITY_KEYS) {
    assert.ok(profile.includes(`name: "${ENTITY_DEFS[key].name}"`), `Q flow probe-entity ontbreekt voor ${key}`);
  }
});

test("flowOutputIpwm publiceert de bestaande actuatoruitgang zonder tweede regelstate", async () => {
  const flowPackage = await readFile(new URL("../../oq_flow_control.yaml", import.meta.url), "utf8");
  const flowOutputSensor = flowPackage.match(
    /  - platform: template\n    id: oq_flow_output_ipwm\n[\s\S]*?(?=\n  - platform: template\n)/,
  )?.[0];

  assert.ok(flowOutputSensor, "flowOutputIpwm-sensorblok ontbreekt");
  assert.match(flowOutputSensor, /name: "Flow Output iPWM"/);
  assert.match(flowOutputSensor, /internal: true/);
  assert.match(flowOutputSensor, /update_interval: never/);
  assert.doesNotMatch(flowOutputSensor, /update_interval: 1s/);
  assert.equal((flowPackage.match(/id\(oq_flow_last_pwm\) = value;/g) || []).length, 2);
  assert.equal((flowPackage.match(/id\(oq_flow_output_ipwm\)\.publish_state\(\(float\) value\);/g) || []).length, 2);
  for (const value of ["service_pwm", "start_pwm", "at_pwm", "purge_pwm", "test_pwm", "pwm"]) {
    assert.ok(flowPackage.includes(`set_flow_output_pwm(${value});`), `eventpublicatie ontbreekt voor ${value}`);
    assert.ok(!flowPackage.includes(`id(oq_flow_last_pwm) = ${value};`), `directe niet-gepubliceerde write voor ${value}`);
  }
  assert.equal(ENTITY_DEFS.flowOutputIpwm.domain, "sensor");
  assert.equal(ENTITY_DEFS.flowOutputIpwm.name, "Flow Output iPWM");
});
