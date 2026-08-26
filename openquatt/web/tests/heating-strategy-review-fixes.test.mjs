import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = {
  localStorage: {
    getItem: () => null,
  },
};

const { state } = await import("../js/src/core/state.js");
const { commitQuickStartStrategySelection } = await import("../js/src/core/control-actions.js");
const {
  getActiveConfiguredThermostatSource,
  getHeatingEnableAdvice,
  getHeatingEnableRecommendation,
} = await import("../js/src/core/heating-strategy-matrix.js");
const { renderHeatingStrategyAdviceModal } = await import("../js/src/features/heating-strategy-advice.js");

const CURVE_STRATEGY = "Water Temperature Control";
const POWER_HOUSE_STRATEGY = "Power House";

function resetState(entities = {}) {
  state.entities = entities;
  state.drafts = {};
  state.inputDrafts = {};
  state.quickStartModalOpen = true;
  state.systemModal = "";
  state.busyAction = "";
  state.controlError = "";
  state.controlNotice = "";
}

test("stooklijn gebruikt de gekoppelde HA-bron en negeert een aanwezige maar uitgeschakelde CIC", () => {
  resetState({
    strategy: { value: CURVE_STRATEGY },
    roomTempSource: { value: "HA input" },
    roomSetpointSource: { value: "HA input" },
    cicPollingEnabled: { value: false, state: "OFF" },
    cicFeedUrl: { value: "" },
    heatingEnableSource: { value: "Disabled" },
  });

  assert.equal(getActiveConfiguredThermostatSource(), "HA input");
  assert.equal(getHeatingEnableRecommendation(), "HA input");
});

test("uitgeschakelde OpenTherm-integratie wordt niet als harde gate aanbevolen", () => {
  resetState({
    strategy: { value: CURVE_STRATEGY },
    roomTempSource: { value: "OT thermostat" },
    roomSetpointSource: { value: "OT thermostat" },
    otEnabled: { value: false, state: "OFF" },
    heatingEnableSource: { value: "Disabled" },
  });

  assert.equal(getActiveConfiguredThermostatSource(), "");
  assert.equal(getHeatingEnableRecommendation(), "");
  assert.equal(getHeatingEnableAdvice().deviant, true);
});

test("ontbrekende OT- of CIC-activering faalt gesloten", () => {
  resetState({
    strategy: { value: CURVE_STRATEGY },
    roomTempSource: { value: "OT thermostat" },
    roomSetpointSource: { value: "OT thermostat" },
    heatingEnableSource: { value: "Disabled" },
  });
  assert.equal(getActiveConfiguredThermostatSource(), "");

  state.entities.roomTempSource = { value: "CIC" };
  state.entities.roomSetpointSource = { value: "CIC" };
  assert.equal(getActiveConfiguredThermostatSource(), "");
});

test("actieve, gekoppelde OT- en CIC-bronnen blijven beschikbaar", () => {
  resetState({
    strategy: { value: CURVE_STRATEGY },
    roomTempSource: { value: "OT thermostat" },
    roomSetpointSource: { value: "OT thermostat" },
    otEnabled: { value: true, state: "ON" },
    heatingEnableSource: { value: "Disabled" },
  });
  assert.equal(getActiveConfiguredThermostatSource(), "OT thermostat");

  state.entities.roomTempSource = { value: "CIC" };
  state.entities.roomSetpointSource = { value: "CIC" };
  state.entities.cicPollingEnabled = { value: true, state: "ON" };
  state.entities.cicFeedUrl = { value: "http://192.0.2.1/feed" };
  assert.equal(getActiveConfiguredThermostatSource(), "CIC");
});

test("strategieswitch schrijft de warmtetoestemming pas nadat de strategie is opgeslagen", async () => {
  resetState({
    strategy: { value: POWER_HOUSE_STRATEGY },
    roomTempSource: { value: "HA input" },
    roomSetpointSource: { value: "HA input" },
    heatingEnableSource: { value: "Disabled" },
  });
  const calls = [];
  const commit = async (key, value) => {
    calls.push([key, value]);
    if (key === "strategy") {
      state.controlError = "Heating Control Mode kon niet worden bijgewerkt. HTTP 503";
      return false;
    }
    return true;
  };

  assert.equal(await commitQuickStartStrategySelection(CURVE_STRATEGY, commit, async () => true), false);
  assert.deepEqual(calls, [
    ["strategy", CURVE_STRATEGY],
    ["strategy", POWER_HOUSE_STRATEGY],
  ]);
});

test("mislukte gate-write zet een geslaagde strategieswitch terug", async () => {
  resetState({
    strategy: { value: POWER_HOUSE_STRATEGY },
    roomTempSource: { value: "HA input" },
    roomSetpointSource: { value: "HA input" },
    heatingEnableSource: { value: "Disabled" },
  });
  const calls = [];
  let rejectedGateWrite = false;
  const commit = async (key, value) => {
    calls.push([key, value]);
    if (key === "heatingEnableSource" && value === "HA input" && !rejectedGateWrite) {
      rejectedGateWrite = true;
      state.controlError = "Heating Enable Source kon niet worden bijgewerkt. HTTP 503";
      return false;
    }
    return true;
  };

  assert.equal(await commitQuickStartStrategySelection(CURVE_STRATEGY, commit, async () => true), false);
  assert.deepEqual(calls, [
    ["strategy", CURVE_STRATEGY],
    ["heatingEnableSource", "HA input"],
    ["heatingEnableSource", "Disabled"],
    ["strategy", POWER_HOUSE_STRATEGY],
  ]);
  assert.match(state.controlError, /strategieswitch is daarom teruggezet/);
});

test("onbevestigde strategy-write past de gate niet aan en herstelt de vorige strategie", async () => {
  resetState({
    strategy: { value: POWER_HOUSE_STRATEGY },
    roomTempSource: { value: "HA input" },
    roomSetpointSource: { value: "HA input" },
    heatingEnableSource: { value: "Disabled" },
  });
  const calls = [];
  const confirmations = [];
  const commit = async (key, value) => {
    calls.push([key, value]);
    return true;
  };
  const confirm = async (key, value) => {
    confirmations.push([key, value]);
    return value === POWER_HOUSE_STRATEGY;
  };

  assert.equal(await commitQuickStartStrategySelection(CURVE_STRATEGY, commit, confirm), false);
  assert.deepEqual(calls, [
    ["strategy", CURVE_STRATEGY],
    ["strategy", POWER_HOUSE_STRATEGY],
  ]);
  assert.deepEqual(confirmations, calls);
  assert.match(state.controlError, /warmtetoestemming is niet aangepast/i);
});

test("onbevestigde gate-write herstelt gate en strategie", async () => {
  resetState({
    strategy: { value: POWER_HOUSE_STRATEGY },
    roomTempSource: { value: "HA input" },
    roomSetpointSource: { value: "HA input" },
    heatingEnableSource: { value: "Disabled" },
  });
  const calls = [];
  let rejectedGateConfirmation = false;
  const commit = async (key, value) => {
    calls.push([key, value]);
    return true;
  };
  const confirm = async (key, value) => {
    if (key === "heatingEnableSource" && value === "HA input" && !rejectedGateConfirmation) {
      rejectedGateConfirmation = true;
      return false;
    }
    return true;
  };

  assert.equal(await commitQuickStartStrategySelection(CURVE_STRATEGY, commit, confirm), false);
  assert.deepEqual(calls, [
    ["strategy", CURVE_STRATEGY],
    ["heatingEnableSource", "HA input"],
    ["heatingEnableSource", "Disabled"],
    ["strategy", POWER_HOUSE_STRATEGY],
  ]);
  assert.match(state.controlError, /strategieswitch is daarom teruggezet/);
});

test("adviesmodal toont opslaanfouten binnen de dialoog", () => {
  resetState({
    strategy: { value: CURVE_STRATEGY },
    roomTempSource: { value: "HA input" },
    roomSetpointSource: { value: "HA input" },
    heatingEnableSource: { value: "Disabled" },
  });
  state.systemModal = "heating-strategy-advice";
  state.controlError = "Warmtetoestemming kon niet worden opgeslagen. HTTP 503";

  const markup = renderHeatingStrategyAdviceModal();

  assert.match(markup, /role="alert"/);
  assert.match(markup, /Warmtetoestemming kon niet worden opgeslagen\. HTTP 503/);
  assert.match(markup, /data-heating-enable-target="HA input"/);
});

test("adviesmodal biedt geen inactieve thermostaatbron als toepasbare keuze aan", () => {
  resetState({
    strategy: { value: CURVE_STRATEGY },
    roomTempSource: { value: "OT thermostat" },
    roomSetpointSource: { value: "OT thermostat" },
    otEnabled: { value: false, state: "OFF" },
    heatingEnableSource: { value: "Disabled" },
  });
  state.systemModal = "heating-strategy-advice";

  const markup = renderHeatingStrategyAdviceModal();

  assert.match(markup, /Thermostaatbron activeren/);
  assert.doesNotMatch(markup, /data-oq-action="apply-heating-strategy-advice"/);
});
