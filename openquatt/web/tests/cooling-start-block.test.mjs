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
  getCoolingStartBlockModel,
} = await import("../js/src/settings/cooling.js");
const {
  getCoolingOverviewModel,
  getCoolingStartBlockTitle,
  getOverviewControlCards,
  getOverviewSystemSignal,
  isCoolingPreflowForCooling,
} = await import("../js/src/views/overview.js");

function numberEntity(value, uom = "") {
  return { value, state: String(value), min_value: 0, max_value: 10000, step: 0.1, uom };
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

// The firmware publishes the actual dispatch/actuator verdict through these
// two entities; the UI only maps them to a label plus countdown.
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
    silentModeOverride: textEntity("Off"),
    silentActive: binaryEntity(false),
    ...overrides,
  };
}

function coolingCard() {
  return getOverviewControlCards().find(({ key }) => key === "manualCoolingEnable");
}

test("aftellen gebruikt M:SS zonder verzonnen tijd bij onbekende blokkade", () => {
  assert.equal(formatCoolingStartBlockCountdown(190), "3:10");
  assert.equal(formatCoolingStartBlockCountdown(65), "1:05");
  assert.equal(
    formatCoolingStartBlockReason("Compressor restart protection", 190),
    "Wachten op compressor-herstartbeveiliging — nog 3:10",
  );
  assert.equal(
    formatCoolingStartBlockReason("Cooling minimum off-time", 310),
    "Wachten op koel-herstartbeveiliging — nog 5:10",
  );
  // Firmwarecontract: tijdgebonden redenen dragen altijd remaining_s > 0, de
  // overige altijd 0. Deze combinaties stuurt de firmware dus nooit andersom.
  assert.equal(
    formatCoolingStartBlockReason("Waiting for confirmed cooling stop", 0),
    "Wachten op bevestigde koelstop",
  );
  assert.equal(formatCoolingStartBlockReason("Compressor start blocked", 0), "Compressorstart geblokkeerd");
});

test("gepubliceerde herstartbeveiliging toont blokkade met juiste resterende tijd", () => {
  resetOverviewState(
    coolingBaseEntities({
      coolingStartBlockReason: textEntity("Compressor restart protection"),
      coolingStartBlockRemaining: numberEntity(190, "s"),
    }),
  );
  const block = getCoolingStartBlockModel();
  assert.equal(block.available, true);
  assert.equal(block.blocked, true);
  assert.equal(block.remainingS, 190);
  assert.equal(block.hasCountdown, true);

  const model = getCoolingOverviewModel();
  assert.equal(model.statusTitle, "Wacht op herstartbeveiliging");
  assert.match(model.statusCopy, /Wachten op compressor-herstartbeveiliging — nog 3:10/);

  // Eén primair oppervlak: het koelregelmodel. Kaart en Systeem-signaal liegen
  // niet over draaien, maar tonen de blokkade niet zelf.
  const card = coolingCard();
  assert.equal(card.status, "Aan");
  assert.match(card.copy, /Er is koelvraag\. Koeling start zodra dat kan/);

  const system = getOverviewSystemSignal();
  assert.equal(system.value, "Normaal");
});

test("niet-tijdgebonden redenen tonen nooit een countdown, ook niet bij een oude timer", () => {
  // Bij een overgang kan een nieuwe reden kort met een oude timer staan; de
  // allowlist houdt die timer weg bij redenen die er nooit een mogen dragen.
  // (Tussen twee tijdgebonden redenen kan kort een oude timer staan; dat
  // restrisico accepteren we voor twee losse entities.)
  resetOverviewState(
    coolingBaseEntities({
      coolingStartBlockReason: textEntity("Waiting for confirmed cooling stop"),
      coolingStartBlockRemaining: numberEntity(60, "s"),
    }),
  );
  const confirm = getCoolingStartBlockModel();
  assert.equal(confirm.hasCountdown, false);
  assert.equal(confirm.display, "Wachten op bevestigde koelstop");

  resetOverviewState(
    coolingBaseEntities({
      coolingStartBlockReason: textEntity("Compressor start blocked"),
      coolingStartBlockRemaining: numberEntity(60, "s"),
    }),
  );
  const other = getCoolingStartBlockModel();
  assert.equal(other.hasCountdown, false);
  assert.equal(other.display, "Compressorstart geblokkeerd");
});

test("koel-herstart, startlimiet en bevestigde stop zijn onderscheiden", () => {
  resetOverviewState(
    coolingBaseEntities({
      coolingStartBlockReason: textEntity("Cooling minimum off-time"),
      coolingStartBlockRemaining: numberEntity(310, "s"),
    }),
  );
  assert.equal(getCoolingOverviewModel().statusTitle, "Wacht op koel-herstart");
  assert.match(getCoolingOverviewModel().statusCopy, /Wachten op koel-herstartbeveiliging — nog 5:10/);

  resetOverviewState(
    coolingBaseEntities({
      coolingStartBlockReason: textEntity("Compressor start limit (6/hour)"),
      coolingStartBlockRemaining: numberEntity(420, "s"),
    }),
  );
  assert.equal(getCoolingOverviewModel().statusTitle, "Startlimiet bereikt");
  assert.match(getCoolingOverviewModel().statusCopy, /Startlimiet bereikt \(6\/uur\) — nog 7:00/);

  resetOverviewState(
    coolingBaseEntities({
      coolingStartBlockReason: textEntity("Waiting for confirmed cooling stop"),
      coolingStartBlockRemaining: numberEntity(0, "s"),
    }),
  );
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
      hp1Compressor: numberEntity(3, ""),
    }),
  );
  const model = getCoolingOverviewModel();
  assert.ok(["Trekt aanvoer omlaag", "Benadert koeldoel", "Koelt rustig door"].includes(model.statusTitle));
  assert.equal(coolingCard().status, "Actief");
});

test("voorloop, herstartwacht en koelbedrijf zijn herkenbaar onderscheiden", () => {
  resetOverviewState(
    coolingBaseEntities({ controlModeLabel: textEntity("CM1 - Preflow/Postflow") }),
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
    coolingBaseEntities({ hp1Compressor: numberEntity(3, "") }),
  );
  const model = getCoolingOverviewModel();
  assert.ok(model.statusTitle !== "Wacht op herstartbeveiliging");
  assert.ok(model.statusTitle !== "Voorloop voor koelen");
});

test("draaiende HP betekent actief koelbedrijf zonder wachttekst", () => {
  resetOverviewState(
    coolingBaseEntities({ hp1Compressor: numberEntity(3, "") }),
  );
  const model = getCoolingOverviewModel();
  assert.doesNotMatch(model.statusCopy, /nog \d+:\d+/);
  assert.doesNotMatch(model.statusCopy, /extra capaciteit/);
});

test("oude firmware zonder startblok-sensor valt terug op bestaande weergave", () => {
  const entities = coolingBaseEntities();
  delete entities.coolingStartBlockReason;
  delete entities.coolingStartBlockRemaining;
  resetOverviewState(entities);
  const block = getCoolingStartBlockModel();
  assert.equal(block.available, false);
  assert.equal(block.blocked, false);
  assert.ok(getCoolingOverviewModel().statusTitle.length > 0);
});

test("startblok-titel onderscheidt koel, algemeen, limiet en overig", () => {
  for (const [reason, title] of [
    ["Cooling minimum off-time", "Wacht op koel-herstart"],
    ["Compressor restart protection", "Wacht op herstartbeveiliging"],
    ["Startup inhibit after reboot", "Wacht op herstartbeveiliging"],
    ["Compressor start limit (6/hour)", "Startlimiet bereikt"],
    ["Waiting for confirmed cooling stop", "Wacht op bevestigde koelstop"],
    ["Compressor start blocked", "Start geblokkeerd"],
  ]) {
    assert.equal(getCoolingStartBlockTitle(reason), title);
  }
});
