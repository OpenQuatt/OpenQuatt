import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = {
  location: { pathname: "/" },
  setTimeout: globalThis.setTimeout,
  clearTimeout: globalThis.clearTimeout,
  localStorage: {
    getItem: () => null,
  },
};

const { state } = await import("../js/src/core/state.js");
const {
  formatCoolingStartBlockCountdown,
  formatCoolingStartBlockReason,
  getCoolingDuoWaitingModel,
  getCoolingStartBlockModel,
  isCoolingStartBlockTimeBound,
} = await import("../js/src/settings/cooling.js");
const {
  getCoolingOverviewModel,
  getCoolingStartBlockTitle,
  getOverviewControlCards,
  getOverviewSystemSignal,
  isCoolingPreflowForCooling,
} = await import("../js/src/views/overview.js");

function numberEntity(value, uom = "", extra = {}) {
  return {
    value,
    state: String(value),
    min_value: 0,
    max_value: 10000,
    step: 0.1,
    uom,
    ...extra,
  };
}

function textEntity(value) {
  return { value, state: String(value) };
}

function binaryEntity(active) {
  return { value: active, state: active ? "ON" : "OFF" };
}

function resetOverviewState(entities = {}) {
  state.entities = entities;
  state.drafts = {};
  state.inputDrafts = {};
  state.settingsAdvancedOpen = {};
  state.loadingEntities = false;
  state.entitySyncInFlight = false;
  state.busyAction = "";
  state.controlError = "";
  state.controlNotice = "";
  state.appView = "overview";
}

function coolingBaseEntities(overrides = {}) {
  return {
    openquattEnabled: binaryEntity(true),
    manualCoolingEnable: binaryEntity(true),
    coolingEnableSelected: binaryEntity(true),
    coolingEnableSource: textEntity("Manual"),
    coolingEnableEffectiveSource: textEntity("Manual"),
    coolingEnableValid: binaryEntity(true),
    coolingScheduleStartTime: textEntity("09:00:00"),
    coolingScheduleEndTime: textEntity("17:00:00"),
    coolingRequestActive: binaryEntity(true),
    coolingPermitted: binaryEntity(true),
    coolingBlockReason: textEntity("Ready"),
    coolingStartBlockReason: textEntity("Ready"),
    coolingStartBlockRemaining: numberEntity(0, "s"),
    coolingMinimumOffTimeRemaining: numberEntity(0, "s"),
    hp1MinimumOffRemaining: numberEntity(0, "s"),
    hp2MinimumOffRemaining: numberEntity(0, "s"),
    coolingStopConfirmationPending: binaryEntity(false),
    requestReason: textEntity("cooling_idle"),
    coolingRequestHp1Level: numberEntity(0, ""),
    coolingRequestHp2Level: numberEntity(0, ""),
    coolingRequestOwnerHp: numberEntity(0, ""),
    coolingDemandRaw: numberEntity(4, ""),
    coolingSupplyError: numberEntity(1.5, "°C"),
    coolingSupplyTarget: textEntity("18.0 °C"),
    supplyTemp: numberEntity(19.5, "°C"),
    coolingEffectiveMinSupplyTemp: textEntity("18.0 °C"),
    coolingGuardMode: textEntity("Dew point"),
    controlModeLabel: textEntity("CM5 - Cooling"),
    flowMode: textEntity("Adaptive"),
    hp1Compressor: numberEntity(0, ""),
    hp2Compressor: numberEntity(0, ""),
    hp1Freq: numberEntity(0, "Hz"),
    hp2Freq: numberEntity(0, "Hz"),
    silentModeOverride: textEntity("Off"),
    silentActive: binaryEntity(false),
    ...overrides,
  };
}

test("aftellen gebruikt M:SS zonder verzonnen tijd bij onbekende blokkade", () => {
  assert.equal(formatCoolingStartBlockCountdown(190), "3:10");
  assert.equal(formatCoolingStartBlockCountdown(65), "1:05");
  assert.equal(formatCoolingStartBlockCountdown(0), "0:00");
  assert.equal(
    formatCoolingStartBlockReason("Compressor restart protection", 190),
    "Wachten op compressor-herstartbeveiliging — nog 3:10",
  );
  assert.equal(
    formatCoolingStartBlockReason("Cooling minimum off-time", 190),
    "Wachten op koel-herstartbeveiliging — nog 3:10",
  );
  assert.equal(
    formatCoolingStartBlockReason("Waiting for confirmed cooling stop", 600),
    "Wachten op bevestigde koelstop",
  );
  assert.equal(
    formatCoolingStartBlockReason("Compressor start blocked", 0),
    "Compressorstart geblokkeerd",
  );
  assert.ok(isCoolingStartBlockTimeBound("Compressor restart protection"));
  assert.ok(!isCoolingStartBlockTimeBound("Waiting for confirmed cooling stop"));
  assert.ok(!isCoolingStartBlockTimeBound("Ready"));
});

test("algemene herstartbeveiliging toont werkelijke blokkade met juiste resterende tijd", () => {
  resetOverviewState(
    coolingBaseEntities({
      coolingStartBlockReason: textEntity("Compressor restart protection"),
      coolingStartBlockRemaining: numberEntity(190, "s"),
      hp1MinimumOffRemaining: numberEntity(190, "s"),
    }),
  );
  const block = getCoolingStartBlockModel();
  assert.equal(block.available, true);
  assert.equal(block.blocked, true);
  assert.equal(block.remainingS, 190);
  assert.equal(block.hasCountdown, true);
  assert.match(block.display, /Wachten op compressor-herstartbeveiliging — nog 3:10/);

  const model = getCoolingOverviewModel();
  assert.equal(model.statusTitle, "Wacht op herstartbeveiliging");
  assert.match(model.statusCopy, /Wachten op compressor-herstartbeveiliging — nog 3:10/);
  assert.match(model.statusCopy, /automatisch zodra de blokkade is opgeheven/);

  const card = getOverviewControlCards().find(({ key }) => key === "manualCoolingEnable");
  assert.equal(card.status, "Wachten");
  assert.match(card.copy, /Wachten op compressor-herstartbeveiliging — nog 3:10/);
  assert.equal(card.tone, "orange");

  const system = getOverviewSystemSignal();
  assert.match(system.value, /Wachten op compressor-herstartbeveiliging — nog 3:10/);
  assert.equal(system.tone, "orange");
});

test("koel-herstartinstelling is onderscheiden van algemene herstartbeveiliging", () => {
  resetOverviewState(
    coolingBaseEntities({
      coolingStartBlockReason: textEntity("Cooling minimum off-time"),
      coolingStartBlockRemaining: numberEntity(310, "s"),
      coolingMinimumOffTimeRemaining: numberEntity(310, "s"),
    }),
  );
  const model = getCoolingOverviewModel();
  assert.equal(model.statusTitle, "Wacht op koel-herstart");
  assert.match(model.statusCopy, /Wachten op koel-herstartbeveiliging — nog 5:10/);
  assert.doesNotMatch(model.statusCopy, /compressor-herstartbeveiliging/);
});

test("bevestigde koelstop toont geen verzonnen afteltijd", () => {
  resetOverviewState(
    coolingBaseEntities({
      coolingStartBlockReason: textEntity("Waiting for confirmed cooling stop"),
      coolingStartBlockRemaining: numberEntity(0, "s"),
      coolingStopConfirmationPending: binaryEntity(true),
    }),
  );
  const block = getCoolingStartBlockModel();
  assert.equal(block.blocked, true);
  assert.equal(block.hasCountdown, false);
  assert.equal(block.display, "Wachten op bevestigde koelstop");

  const model = getCoolingOverviewModel();
  assert.equal(model.statusTitle, "Wacht op bevestigde koelstop");
  assert.doesNotMatch(model.statusCopy, /nog \d+:\d+/);
});

test("status en timer vervallen zodra de blokkade is opgeheven", () => {
  resetOverviewState(
    coolingBaseEntities({
      coolingStartBlockReason: textEntity("Compressor restart protection"),
      coolingStartBlockRemaining: numberEntity(5, "s"),
    }),
  );
  assert.equal(getCoolingOverviewModel().statusTitle, "Wacht op herstartbeveiliging");

  resetOverviewState(
    coolingBaseEntities({
      coolingStartBlockReason: textEntity("Ready"),
      coolingStartBlockRemaining: numberEntity(0, "s"),
      hp1Compressor: numberEntity(3, ""),
      hp1Freq: numberEntity(33, "Hz"),
    }),
  );
  const model = getCoolingOverviewModel();
  assert.ok(["Trekt aanvoer omlaag", "Benadert koeldoel", "Koelt rustig door"].includes(model.statusTitle));
  assert.doesNotMatch(model.statusCopy, /nog \d+:\d+/);

  const card = getOverviewControlCards().find(({ key }) => key === "manualCoolingEnable");
  assert.equal(card.status, "Actief");
});

test("voorloop, herstartwacht en koelbedrijf zijn herkenbaar onderscheiden", () => {
  resetOverviewState(
    coolingBaseEntities({
      controlModeLabel: textEntity("CM1 - Preflow/Postflow"),
      coolingDemandRaw: numberEntity(4, ""),
    }),
  );
  assert.equal(isCoolingPreflowForCooling(), true);
  assert.equal(getCoolingOverviewModel().statusTitle, "Voorloop voor koelen");

  resetOverviewState(
    coolingBaseEntities({
      coolingStartBlockReason: textEntity("Compressor restart protection"),
      coolingStartBlockRemaining: numberEntity(190, "s"),
    }),
  );
  assert.equal(getCoolingOverviewModel().statusTitle, "Wacht op herstartbeveiliging");

  resetOverviewState(
    coolingBaseEntities({
      hp1Compressor: numberEntity(3, ""),
      hp1Freq: numberEntity(33, "Hz"),
    }),
  );
  assert.ok(getCoolingOverviewModel().statusTitle !== "Wacht op herstartbeveiliging");
  assert.ok(getCoolingOverviewModel().statusTitle !== "Voorloop voor koelen");
});

test("duo met een draaiende unit toont actief plus wachtende unit", () => {
  resetOverviewState(
    coolingBaseEntities({
      hp1Compressor: numberEntity(3, ""),
      hp1Freq: numberEntity(33, "Hz"),
      hp2Compressor: numberEntity(0, ""),
      hp2Freq: numberEntity(0, "Hz"),
      hp2MinimumOffRemaining: numberEntity(125, "s"),
    }),
  );
  const duo = getCoolingDuoWaitingModel();
  assert.ok(duo);
  assert.equal(duo.waitingHp, 2);
  assert.equal(duo.remainingS, 125);
  assert.match(duo.display, /HP2 wacht nog 2:05/);

  const model = getCoolingOverviewModel();
  assert.match(model.statusCopy, /HP2 wacht nog 2:05/);
  assert.doesNotMatch(model.statusTitle, /Wacht op herstartbeveiliging/);
});

test("oude firmware zonder startblok-sensor valt terug op bestaande weergave", () => {
  const entities = coolingBaseEntities();
  delete entities.coolingStartBlockReason;
  delete entities.coolingStartBlockRemaining;
  delete entities.hp1MinimumOffRemaining;
  delete entities.hp2MinimumOffRemaining;
  delete entities.coolingStopConfirmationPending;
  delete entities.requestReason;
  resetOverviewState(entities);
  const block = getCoolingStartBlockModel();
  assert.equal(block.available, false);
  assert.equal(block.blocked, false);
  const model = getCoolingOverviewModel();
  assert.ok(model.statusTitle.length > 0);
  assert.doesNotMatch(model.statusCopy, /nog \d+:\d+/);
});

test("startblok-titel onderscheidt koel, algemeen, limiet en overig", () => {
  assert.equal(getCoolingStartBlockTitle("Cooling minimum off-time"), "Wacht op koel-herstart");
  assert.equal(getCoolingStartBlockTitle("Compressor restart protection"), "Wacht op herstartbeveiliging");
  assert.equal(getCoolingStartBlockTitle("Startup inhibit after reboot"), "Wacht op herstartbeveiliging");
  assert.equal(getCoolingStartBlockTitle("Compressor start limit (6/hour)"), "Startlimiet bereikt");
  assert.equal(getCoolingStartBlockTitle("Waiting for confirmed cooling stop"), "Wacht op bevestigde koelstop");
  assert.equal(getCoolingStartBlockTitle("Compressor start blocked"), "Start geblokkeerd");
});
