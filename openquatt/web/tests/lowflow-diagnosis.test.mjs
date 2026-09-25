import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = {
  localStorage: {
    getItem: () => null,
  },
};

const { state } = await import("../js/src/core/state.js");
const {
  FLOW_IPWM_MIN,
  getLowFlowDiagnosis,
  renderLowFlowDiagnosis,
} = await import("../js/src/core/lowflow-diagnosis.js");

function setEntities(entities) {
  state.entities = entities;
}

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

  setEntities({
    lowflowFaultActive: { value: true, state: "ON" },
    flowSelected: { value: 0, state: "0" },
    flowOutputIpwm: { value: 400, state: "400" },
    hp1PumpRelay: { value: true, state: "ON" },
  });
  assert.equal(getLowFlowDiagnosis().requestingMore, false);
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

test("duo neemt elke beschikbare pomprelais mee", () => {
  setEntities({
    lowflowFaultActive: { value: true, state: "ON" },
    flowSelected: { value: 0, state: "0" },
    hp1PumpRelay: { value: false, state: "OFF" },
    hp2PumpRelay: { value: true, state: "ON" },
  });
  const diagnosis = getLowFlowDiagnosis();
  assert.equal(diagnosis.pumpAvailable, true);
  assert.equal(diagnosis.pumpRunning, true);
  assert.equal(diagnosis.scenario, "no-flow");
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
