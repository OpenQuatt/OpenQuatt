import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = {
  localStorage: {
    getItem: () => null,
  },
};

const { state } = await import("../js/src/core/state.js");
const { INSTALLATION_MONITORING_STATE_KEYS } = await import("../js/src/core/config.js");
const {
  FLOW_IPWM_MIN,
  getLowFlowDiagnosis,
  renderLowFlowDiagnosis,
} = await import("../js/src/core/lowflow-diagnosis.js");

function setEntities(entities) {
  state.entities = entities.flowOutputIpwm && !entities.flowControlMode
    ? { ...entities, flowControlMode: { value: "Flow Setpoint", state: "Flow Setpoint" } }
    : entities;
}

test("diagnose-entities worden tijdens servicebewaking opgehaald", () => {
  for (const key of [
    "flowSelected",
    "flowSetpoint",
    "flowOutputIpwm",
    "flowSource",
    "qFlowSource",
    "hp1PumpRelay",
    "hp2PumpRelay",
  ]) {
    assert.ok(INSTALLATION_MONITORING_STATE_KEYS.includes(key), key);
  }
});

test("gebruikt centrale actuatorgrenzen zonder magic-numbervergelijking", () => {
  assert.equal(FLOW_IPWM_MIN, 50);
});

test("pomp aangestuurd zonder flow wordt als no-flow gediagnosticeerd", () => {
  setEntities({
    lowflowFaultActive: { value: true, state: "ON" },
    flowSelected: { value: 0, state: "0" },
    flowSetpoint: { value: 800, state: "800" },
    flowOutputIpwm: { value: 50, state: "50" },
    hp1PumpRelay: { value: true, state: "ON" },
    flowSource: { value: "Outdoor unit", state: "Outdoor unit" },
  });
  const diagnosis = getLowFlowDiagnosis();
  assert.equal(diagnosis.active, true);
  assert.equal(diagnosis.scenario, "no-flow");
  assert.equal(diagnosis.requestingMore, true);
  assert.equal(diagnosis.pumpRunning, true);
});

test("regelaar net boven de ondergrens geldt nog als meer-flow-vraag", () => {
  setEntities({
    lowflowFaultActive: { value: true, state: "ON" },
    flowSelected: { value: 0, state: "0" },
    flowOutputIpwm: { value: FLOW_IPWM_MIN + 50, state: String(FLOW_IPWM_MIN + 50) },
    hp1PumpRelay: { value: true, state: "ON" },
  });
  assert.equal(getLowFlowDiagnosis().requestingMore, true);
  assert.equal(getLowFlowDiagnosis().scenario, "no-flow");

  setEntities({
    lowflowFaultActive: { value: true, state: "ON" },
    flowSelected: { value: 0, state: "0" },
    flowOutputIpwm: { value: 400, state: "400" },
    hp1PumpRelay: { value: true, state: "ON" },
  });
  assert.equal(getLowFlowDiagnosis().requestingMore, false);
  assert.equal(getLowFlowDiagnosis().scenario, "no-flow-unconfirmed");
});

test("handmatige PWM-regeling wordt niet als actieve meer-flow-vraag gediagnosticeerd", () => {
  setEntities({
    lowflowFaultActive: { value: true, state: "ON" },
    flowSelected: { value: 0, state: "0" },
    flowOutputIpwm: { value: 50, state: "50" },
    flowControlMode: { value: "Manual PWM", state: "Manual PWM" },
    hp1PumpRelay: { value: true, state: "ON" },
  });
  assert.equal(getLowFlowDiagnosis().requestingMore, false);
  assert.equal(getLowFlowDiagnosis().scenario, "no-flow-unconfirmed");
});

test("niet-aangestuurde pomp wordt apart benoemd", () => {
  setEntities({
    lowflowFaultActive: { value: true, state: "ON" },
    flowSelected: { value: 0, state: "0" },
    hp1PumpRelay: { value: false, state: "OFF" },
  });
  assert.equal(getLowFlowDiagnosis().scenario, "pump-off");
});

test("enige maar onvoldoende flow wordt apart benoemd", () => {
  setEntities({
    lowflowFaultActive: { value: true, state: "ON" },
    flowSelected: { value: 120, state: "120" },
    flowOutputIpwm: { value: 400, state: "400" },
    hp1PumpRelay: { value: true, state: "ON" },
  });
  assert.equal(getLowFlowDiagnosis().scenario, "low-flow");
});

test("ontbrekende flowmeting wordt apart benoemd", () => {
  setEntities({
    lowflowFaultActive: { value: true, state: "ON" },
    hp1PumpRelay: { value: true, state: "ON" },
  });
  assert.equal(getLowFlowDiagnosis().scenario, "no-measurement");
});

test("duo bewaart per pomprelais de eigen status", () => {
  setEntities({
    lowflowFaultActive: { value: true, state: "ON" },
    flowSelected: { value: 0, state: "0" },
    hp1PumpRelay: { value: false, state: "OFF" },
    hp2PumpRelay: { value: true, state: "ON" },
  });
  const diagnosis = getLowFlowDiagnosis();
  assert.deepEqual(diagnosis.pumpRelays, [
    { label: "HP1", running: false },
    { label: "HP2", running: true },
  ]);
  assert.equal(diagnosis.scenario, "no-flow-unconfirmed");
});

test("actieve blokkade met voldoende flow wordt als herstelhersteld weergegeven", () => {
  setEntities({
    lowflowFaultActive: { value: true, state: "ON" },
    flowSelected: { value: 300, state: "300" },
    hp1PumpRelay: { value: true, state: "ON" },
  });
  assert.equal(getLowFlowDiagnosis().scenario, "recovering");
});

test("ontbrekende pompaansturing blijft onbekend in plaats van te concluderen dat de pomp draait", () => {
  setEntities({
    lowflowFaultActive: { value: true, state: "ON" },
    flowSelected: { value: 0, state: "0" },
    flowOutputIpwm: { value: 50, state: "50" },
  });
  assert.equal(getLowFlowDiagnosis().scenario, "pump-unknown");
});

test("een lege of onbekende flowmeting is geen geldige nulmeting", () => {
  setEntities({
    lowflowFaultActive: { value: true, state: "ON" },
    flowSelected: { value: "", state: "" },
    hp1PumpRelay: { value: true, state: "ON" },
  });
  const diagnosis = getLowFlowDiagnosis();
  assert.equal(diagnosis.flowAvailable, false);
  assert.equal(diagnosis.scenario, "no-measurement");
});

test("Single Q V1 met Auto flowbron toont de effectieve lokale bron", () => {
  setEntities({
    lowflowFaultActive: { value: true, state: "ON" },
    flowSelected: { value: 0, state: "0" },
    hp1PumpRelay: { value: true, state: "ON" },
    flowSource: { value: "Outdoor unit", state: "Outdoor unit" },
    qFlowSource: { value: "Auto", state: "Auto" },
    hpGeneration: { value: "V1", state: "V1" },
    installationTopology: { value: "single", state: "single" },
  });
  const html = renderLowFlowDiagnosis();
  assert.equal(getLowFlowDiagnosis().flowSource, "Local");
  assert.match(html, /Lokaal/);
});

test("Duo V1 met Auto flowbron blijft op de outdoor/aggregate route", () => {
  setEntities({
    lowflowFaultActive: { value: true, state: "ON" },
    flowSelected: { value: 0, state: "0" },
    hp1PumpRelay: { value: true, state: "ON" },
    flowSource: { value: "Outdoor unit", state: "Outdoor unit" },
    qFlowSource: { value: "Auto", state: "Auto" },
    hpGeneration: { value: "V1", state: "V1" },
    installationTopology: { value: "duo", state: "duo" },
  });
  assert.equal(getLowFlowDiagnosis().flowSource, "Outdoor unit");
});

test("tijdens koelen wordt het koel-flowsetpoint als eerste setpoint getoond", () => {
  setEntities({
    lowflowFaultActive: { value: true, state: "ON" },
    flowSelected: { value: 0, state: "0" },
    flowSetpoint: { value: 800, state: "800" },
    coolingFlowSetpoint: { value: 650, state: "650" },
    controlModeLabel: { value: "CM5 - Cooling", state: "CM5 - Cooling" },
    hp1PumpRelay: { value: true, state: "ON" },
  });
  const diagnosis = getLowFlowDiagnosis();
  assert.equal(diagnosis.setpointLph, 650);
  assert.deepEqual(diagnosis.setpoints.map((setpoint) => setpoint.kind), ["cooling", "heating"]);
});

test("bij onbekende modus worden beide beschikbare flowsetpoints getoond", () => {
  setEntities({
    lowflowFaultActive: { value: true, state: "ON" },
    flowSelected: { value: 0, state: "0" },
    flowSetpoint: { value: 800, state: "800" },
    coolingFlowSetpoint: { value: 650, state: "650" },
    hp1PumpRelay: { value: true, state: "ON" },
  });
  assert.deepEqual(getLowFlowDiagnosis().setpoints.map((setpoint) => setpoint.kind), ["heating", "cooling"]);
});

test("diagnoseblok linkt direct naar de Waterpomptest zonder defect te concluderen", () => {
  setEntities({
    lowflowFaultActive: { value: true, state: "ON" },
    flowSelected: { value: 0, state: "0" },
    flowSetpoint: { value: 800, state: "800" },
    flowOutputIpwm: { value: 50, state: "50" },
    hp1PumpRelay: { value: true, state: "ON" },
    flowSource: { value: "Outdoor unit", state: "Outdoor unit" },
  });
  const html = renderLowFlowDiagnosis();
  assert.match(html, /data-oq-action="open-service-task-modal"/);
  assert.match(html, /data-service-task="manual-flow"/);
  assert.match(html, /data-group-id="installation"/);
  assert.match(html, /Waterpomptest openen/);
  assert.doesNotMatch(html, /defecte pomp/);
  assert.doesNotMatch(html, /defecte flowmeter/);
});

test("zonder actieve low-flowblokkade rendert geen diagnoseblok", () => {
  setEntities({
    lowflowFaultActive: { value: false, state: "OFF" },
    flowSelected: { value: 800, state: "800" },
  });
  assert.equal(renderLowFlowDiagnosis(), "");
});
