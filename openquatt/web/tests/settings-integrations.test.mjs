import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = {
  requestAnimationFrame: (callback) => callback(),
  localStorage: {
    getItem: () => null,
  },
};

const { state } = await import("../js/src/core/state.js");
const { setRenderCallback } = await import("../js/src/core/render-scheduler.js");
const { SETTINGS_GROUP_KEY_MAP } = await import("../js/src/core/entity-sync.js");
const { handleViewAction } = await import("../js/src/features/view-actions.js");
const { captureSettingsFocusContinuity, restoreSettingsFocusContinuity } = await import("../js/src/core/settings-focus-continuity.js");
const { renderSettingsOpenThermCicSection, renderSettingsSensorSelectionSection } = await import("../js/src/settings/integrations.js");

const MQTT_SOURCE_SELECTS = [
  ["roomTempSource", "room-temperature"],
  ["roomSetpointSource", "room-setpoint"],
  ["outsideTempSource", "outside-temperature"],
  ["heatingEnableSource", "heating-enable"],
  ["coolingEnableSource", "cooling-enable"],
  ["coolingDewPointSource", "cooling-dew-point"],
];

function getSelectMarkup(markup, key) {
  const match = markup.match(new RegExp(`<select[^>]+data-oq-field="${key}"[^>]*>([\\s\\S]*?)<\\/select>`));
  assert.ok(match, `select ${key} ontbreekt`);
  return match[1];
}

function getInspectorMarkup(markup) {
  const match = markup.match(/<article\s+[\s\S]*?data-oq-source-inspector[\s\S]*?<\/article>/);
  assert.ok(match, "focus-inspector ontbreekt");
  return match[0];
}

function getCategoryMarkup(markup, key) {
  const match = markup.match(new RegExp(`<section[^>]+data-source-category="${key}"[\\s\\S]*?<\\/section>`));
  assert.ok(match, `broncategorie ${key} ontbreekt`);
  return match[0];
}

function assertMarkupOrder(markup, values) {
  let previousIndex = -1;
  values.forEach((value) => {
    const index = markup.indexOf(value);
    assert.ok(index > previousIndex, `${value} staat niet in de verwachte volgorde`);
    previousIndex = index;
  });
}

function renderFocusedSource(key) {
  state.settingsSourceFocusKey = key;
  return renderSettingsSensorSelectionSection();
}

function binaryEntity(active) {
  return { value: active, state: active ? "ON" : "OFF" };
}

function valueEntity(value, uom = "") {
  return { value, state: value == null ? "nan" : String(value), uom };
}

function setSourceSelectionState(mqttEnabled) {
  state.loadingEntities = false;
  state.drafts = {};
  state.settingsInfoOpen = "";
  state.settingsSourceFocusKey = "room-temperature";
  state.settingsSourceDetailOpen = true;
  state.mqttStatus = {
    enabled: mqttEnabled,
    input_enabled: {
      cooling_dew_point: true,
      outside_temperature: true,
      room_temperature: true,
      room_setpoint: true,
      heating_enable: true,
      cooling_enable: true,
    },
  };
  state.entities = {
    otEnabled: binaryEntity(true),
    cicPollingEnabled: binaryEntity(true),
    roomTempSource: { value: "OT thermostat", option: ["OT thermostat", "CIC", "HA input", "API input", "MQTT"] },
    roomSetpointSource: { value: "OT thermostat", option: ["OT thermostat", "CIC", "HA input", "API input", "MQTT"] },
    waterSupplySource: { value: "Local", option: ["Local", "CIC", "HA input"] },
    localWaterSupplyTempSource: { value: "PT1000", option: ["PT1000", "DS18B20"] },
    flowSource: { value: "Outdoor unit", option: ["Outdoor unit", "CIC"] },
    qFlowSource: { value: "Auto", option: ["Auto", "Local", "Outdoor unit"] },
    outdoorUnitFlowMode: { value: "Local aggregate HP1/HP2", option: ["Flowmeter HP1", "Flowmeter HP2", "Local aggregate HP1/HP2"] },
    outsideTempSource: { value: "Outdoor unit", option: ["Auto", "Outdoor unit", "HA input", "API input", "MQTT"] },
    heatingEnableSource: { value: "Disabled", option: ["Disabled", "OT thermostat", "CIC", "HA input", "API input", "MQTT"] },
    coolingEnableSource: { value: "Disabled", option: ["Disabled", "OT thermostat", "HA input", "API input", "MQTT"] },
    coolingDewPointSource: { value: "Auto", option: ["Auto", "Home Assistant", "API input", "MQTT"] },
    externalHeatDemandSource: { value: "Disabled", option: ["Disabled", "HA input", "API input"] },
    roomTemp: valueEntity(21.8, "°C"),
    roomTempEffectiveSource: valueEntity("OT thermostat"),
    roomSetpoint: valueEntity(20, "°C"),
    roomSetpointEffectiveSource: valueEntity("OT thermostat"),
    supplyTemp: valueEntity(31.4, "°C"),
    waterSupplyTempEffectiveSource: valueEntity("PT1000"),
    waterSupplyTempEsp: valueEntity(31.4, "°C"),
    waterSupplyTempPt1000: valueEntity(31.4, "°C"),
    waterSupplyTempDs18b20: valueEntity(31.2, "°C"),
    flowSelected: valueEntity(788, "L/h"),
    controllerFlow: valueEntity(782, "L/h"),
    flowLocal: valueEntity(788, "L/h"),
    hp1Flow: valueEntity(790, "L/h"),
    hp2Flow: valueEntity(786, "L/h"),
    outsideTempSelected: valueEntity(17.2, "°C"),
    outsideTempLocalAggregated: valueEntity(17.2, "°C"),
    heatingEnableSelected: binaryEntity(true),
    heatingEnableEffectiveSource: valueEntity("None"),
    coolingEnableSelected: binaryEntity(false),
    coolingEnableEffectiveSource: valueEntity("Manual"),
    manualCoolingEnable: binaryEntity(false),
    coolingDewPointSelected: valueEntity(15.8, "°C"),
    externalHeatDemandSelected: valueEntity(0, "kW"),
    powerHouseDemandSource: valueEntity("model"),
    cicRoomTemp: valueEntity(21.7, "°C"),
    cicRoomSetpoint: valueEntity(20, "°C"),
    cicWaterSupplyTemp: valueEntity(31.3, "°C"),
    cicFlowrate: valueEntity(780, "L/h"),
    cicChEnabled: binaryEntity(true),
    otRoomTemp: valueEntity(21.8, "°C"),
    otRoomSetpoint: valueEntity(20, "°C"),
    otThermostatChEnable: binaryEntity(true),
    otThermostatCoolingEnable: binaryEntity(false),
    roomTempHa: valueEntity(21.6, "°C"),
    roomTempHaValid: binaryEntity(true),
    apiInputRoomTemperature: valueEntity(0, "°C"),
    apiInputRoomTemperatureValid: binaryEntity(false),
    mqttRoomTemperature: { value: 20, uom: "°C" },
    mqttRoomTemperatureValid: { value: true, state: "ON" },
    mqttRoomSetpoint: { value: 20, uom: "°C" },
    mqttRoomSetpointValid: { value: true, state: "ON" },
    mqttOutsideTemperature: { value: 8, uom: "°C" },
    mqttOutsideTemperatureValid: { value: true, state: "ON" },
    mqttHeatingEnable: { value: true, state: "ON" },
    mqttHeatingEnableValid: { value: true, state: "ON" },
    mqttCoolingEnable: { value: false, state: "OFF" },
    mqttCoolingEnableValid: { value: true, state: "ON" },
    mqttCoolingDewPoint: { value: 14, uom: "°C" },
    mqttCoolingDewPointValid: { value: true, state: "ON" },
  };
}

test("integraties laden de aanvoerkalibratiestatus direct", () => {
  assert.ok(SETTINGS_GROUP_KEY_MAP.integrations.includes("waterSupplyCalibrationOffset"));
  assert.ok(SETTINGS_GROUP_KEY_MAP.integrations.includes("waterSupplyCalibrationRequired"));
  assert.ok(SETTINGS_GROUP_KEY_MAP.integrations.includes("waterSupplyCalibrationStatus"));
});

test("CIC-diagnostiek toont waterdruk alleen wanneer de sensor aanwezig is", () => {
  state.loadingEntities = false;
  state.drafts = {};
  state.entities = {
    cicPollingEnabled: { value: true, state: "ON" },
    cicJsonFeedOk: { value: true, state: "ON" },
  };

  const withoutPressureMarkup = renderSettingsOpenThermCicSection();
  assert.doesNotMatch(withoutPressureMarkup, /Waterdruk/);

  state.entities.cicBoilerWaterPressure = { value: 1.7, state: "1.70", uom: "bar" };
  const withPressureMarkup = renderSettingsOpenThermCicSection();
  assert.match(withPressureMarkup, /<dt>Waterdruk<\/dt>\s*<dd>1\.7 bar<\/dd>/);
  assert.ok(SETTINGS_GROUP_KEY_MAP.integrations.includes("cicBoilerWaterPressure"));
});

test("focuspaneel groepeert alle signalen in vaste volgorde en rendert één inspector", () => {
  setSourceSelectionState(true);
  Object.assign(state.entities, {
    outsideTempSource: { value: "Auto", option: ["Auto", "Outdoor unit", "HA input", "API input", "MQTT"] },
    outsideTempHa: valueEntity(16.6, "°C"),
    outsideTempHaValid: binaryEntity(true),
  });
  state.mqttStatus.input_enabled.outside_temperature = false;
  const markup = renderSettingsSensorSelectionSection();

  assert.match(markup, /data-oq-source-workspace/);
  assert.equal((markup.match(/data-source-category=/g) || []).length, 4);
  assert.equal((markup.match(/data-oq-action="select-settings-source"/g) || []).length, 9);
  assert.equal((markup.match(/data-oq-focus-key="settings-source-[^"]+"/g) || []).length, 10);
  assert.equal((markup.match(/\sdata-oq-source-inspector(?:\s|>)/g) || []).length, 1);
  const expectedSources = [
    ["room-outside", ["room-temperature", "room-setpoint", "outside-temperature"]],
    ["water-circuit", ["water-supply", "flow-source"]],
    ["heating", ["external-heat-demand", "heating-enable"]],
    ["cooling", ["cooling-enable", "cooling-dew-point"]],
  ];
  assertMarkupOrder(markup, expectedSources.map(([category]) => `data-source-category="${category}"`));
  expectedSources.forEach(([category, sources]) => {
    assertMarkupOrder(getCategoryMarkup(markup, category), sources.map((source) => `data-source-key="${source}"`));
  });
  assert.match(getInspectorMarkup(markup), /data-source-key="room-temperature"/);
  assert.match(getInspectorMarkup(markup), /data-oq-focus-key="settings-source-detail-back"/);
  assert.match(getCategoryMarkup(markup, "room-outside"), /aria-label="Ingesteld: Auto\. Gebruikt: HA-invoer"/);
  assert.match(getCategoryMarkup(markup, "room-outside"), /<span>Auto<\/span>[\s\S]*?<span class="oq-settings-source-signal-source-arrow" aria-hidden="true">→<\/span>[\s\S]*?<span>HA-invoer<\/span>/);
  assert.doesNotMatch(markup, /Bronwaarden tonen|>Relevant<|>Alles</);
});

test("focus blijft behouden bij nieuwe bronwaarden en valt veilig terug wanneer het signaal ontbreekt", () => {
  setSourceSelectionState(true);
  state.settingsInfoOpen = "mqttRoomTemperature-info";
  assert.equal(handleViewAction("select-settings-source", { dataset: { sourceKey: "cooling-dew-point" } }), true);
  assert.equal(state.settingsSourceFocusKey, "cooling-dew-point");
  assert.equal(state.settingsSourceDetailOpen, true);
  assert.equal(state.settingsInfoOpen, "");

  let markup = renderSettingsSensorSelectionSection();
  assert.match(getInspectorMarkup(markup), /data-source-key="cooling-dew-point"/);
  assert.match(getSelectMarkup(markup, "coolingDewPointSource"), /<option value="Auto" selected>/);
  assert.equal((getInspectorMarkup(markup).match(/<select\b/g) || []).length, 1);

  state.entities.coolingDewPointSelected = valueEntity(16.1, "°C");
  markup = renderSettingsSensorSelectionSection();
  assert.equal(state.settingsSourceFocusKey, "cooling-dew-point");
  assert.match(getInspectorMarkup(markup), /data-source-key="cooling-dew-point"/);
  assert.match(getInspectorMarkup(markup), /16\.1 °C/);

  state.settingsSourceFocusKey = "niet-bestaand-signaal";
  markup = renderSettingsSensorSelectionSection();
  assert.equal(state.settingsSourceFocusKey, "room-temperature");
  assert.match(getInspectorMarkup(markup), /data-source-key="room-temperature"/);
});

test("mobiele bronnavigatie draagt focus en scroll over tussen lijst en inspector", () => {
  setSourceSelectionState(true);
  const originalRequestAnimationFrame = window.requestAnimationFrame;
  let queuedFrame = null;
  const events = [];
  const backButton = {
    offsetParent: {},
    focus: (options) => events.push(["back-focus", options]),
  };
  const inspector = {
    querySelector: () => backButton,
    scrollIntoView: (options) => events.push(["inspector-scroll", options]),
  };
  const signal = {
    focus: (options) => events.push(["signal-focus", options]),
    scrollIntoView: (options) => events.push(["signal-scroll", options]),
  };
  state.root = {
    querySelector: (selector) => selector === "[data-oq-source-inspector]" ? inspector : signal,
  };
  setRenderCallback(() => {});
  window.requestAnimationFrame = (callback) => {
    queuedFrame = callback;
  };

  handleViewAction("select-settings-source", { dataset: { sourceKey: "water-supply" } });
  queuedFrame();
  assert.deepEqual(events, [
    ["inspector-scroll", { block: "start", behavior: "auto" }],
    ["back-focus", { preventScroll: true }],
  ]);

  events.length = 0;
  handleViewAction("close-settings-source-detail", { dataset: {} });
  queuedFrame();
  assert.deepEqual(events, [
    ["signal-focus", { preventScroll: true }],
    ["signal-scroll", { block: "nearest", behavior: "auto" }],
  ]);

  state.root = null;
  window.requestAnimationFrame = originalRequestAnimationFrame;
});

test("bronnavigatiefocus wordt over een poll-render hersteld zonder invoervelden te blokkeren", () => {
  setSourceSelectionState(true);
  const originalDocument = globalThis.document;
  const focusEvents = [];
  const activeSignal = {
    dataset: { oqFocusKey: "settings-source-room-temperature" },
    closest: () => null,
  };
  const replacementSignal = {
    disabled: false,
    focus: (options) => focusEvents.push(options),
  };
  state.appView = "settings";
  state.focusedField = "";
  state.root = {
    contains: (element) => element === activeSignal,
    querySelector: (selector) => selector === '[data-oq-focus-key="settings-source-room-temperature"]'
      ? replacementSignal
      : null,
  };
  globalThis.document = { activeElement: activeSignal };

  const continuity = captureSettingsFocusContinuity(state.root, state.appView, globalThis.document);
  assert.equal(continuity.focusKey, "settings-source-room-temperature");
  assert.equal(continuity.field, "");
  assert.equal(state.focusedField, "");

  state.entities.roomTemp = valueEntity(22.1, "°C");
  assert.match(renderFocusedSource("room-temperature"), /22\.1 °C/);
  globalThis.document.activeElement = { closest: () => null };
  restoreSettingsFocusContinuity(state.root, continuity, globalThis.document);
  assert.deepEqual(focusEvents, [{ preventScroll: true }]);

  state.root = null;
  if (originalDocument === undefined) {
    delete globalThis.document;
  } else {
    globalThis.document = originalDocument;
  }
});

test("Auto zonder geldige kandidaat toont een bronprobleem", () => {
  setSourceSelectionState(true);
  const missing = { value: Number.NaN, state: "nan", uom: "°C" };
  Object.assign(state.entities, {
    outsideTempSource: { value: "Auto", option: ["Auto", "Outdoor unit", "HA input", "API input", "MQTT"] },
    outsideTempSelected: missing,
    outsideTempLocalAggregated: missing,
    outsideTempHa: missing,
    outsideTempHaValid: binaryEntity(false),
    apiInputOutsideTemperature: missing,
    apiInputOutsideTemperatureValid: binaryEntity(false),
    mqttOutsideTemperature: missing,
    mqttOutsideTemperatureValid: binaryEntity(false),
  });

  const markup = renderFocusedSource("outside-temperature");
  assert.match(getInspectorMarkup(markup), /data-oq-source-warning/);
  assert.match(markup, /aria-label="Bronprobleem: De ingestelde bron levert momenteel geen geldige waarde\."/);
});

test("Relevant toont geldige bronnen en pint een ongeldige ingestelde of effectieve bron", () => {
  setSourceSelectionState(true);
  let inspector = getInspectorMarkup(renderFocusedSource("room-temperature"));
  assert.match(inspector, /data-source-kind="ha"\s+data-source-state="valid"/);
  assert.match(inspector, /data-source-kind="mqtt"\s+data-source-state="valid"/);
  assert.match(inspector, /data-source-kind="ot"\s+data-source-state="available"\s+data-source-effective="true"/);
  assert.doesNotMatch(inspector, /data-source-kind="api"/);

  state.entities.roomTempHaValid = binaryEntity(false);
  state.entities.mqttRoomTemperatureValid = binaryEntity(false);
  inspector = getInspectorMarkup(renderFocusedSource("room-temperature"));
  assert.doesNotMatch(inspector, /data-source-kind="ha"|data-source-kind="api"|data-source-kind="mqtt"/);

  state.entities.roomTempSource.value = "API input";
  inspector = getInspectorMarkup(renderFocusedSource("room-temperature"));
  assert.match(inspector, /data-oq-source-warning/);
  assert.match(inspector, /data-source-kind="api"\s+data-source-state="missing"/);
  assert.match(inspector, /data-source-kind="ot"\s+data-source-state="available"\s+data-source-effective="true"/);

  state.entities.roomTempSource.value = "MQTT";
  inspector = getInspectorMarkup(renderFocusedSource("room-temperature"));
  assert.match(inspector, /data-oq-source-warning/);
  assert.match(inspector, /data-source-kind="mqtt"\s+data-source-state="invalid"/);

  state.entities.roomTempSource.value = "HA input";
  inspector = getInspectorMarkup(renderFocusedSource("room-temperature"));
  assert.match(inspector, /data-oq-source-warning/);
  assert.match(inspector, /data-source-kind="ha"\s+data-source-state="invalid"/);

  state.entities.roomTempSource.value = "OT thermostat";
  state.entities.roomTempEffectiveSource = valueEntity("API input");
  inspector = getInspectorMarkup(renderFocusedSource("room-temperature"));
  assert.match(inspector, /data-source-kind="api"\s+data-source-state="missing"\s+data-source-effective="true"/);
  assert.doesNotMatch(inspector, /data-oq-source-warning/);
});

test("API-invoer onderscheidt wachten van verouderde data en behoudt geldige false/0-waarden", () => {
  setSourceSelectionState(true);
  state.entities.roomTempSource.value = "API input";

  let inspector = getInspectorMarkup(renderFocusedSource("room-temperature"));
  assert.match(inspector, /data-source-kind="api"\s+data-source-state="missing"/);
  assert.match(inspector, /Wacht op data/);

  state.entities.apiInputRoomTemperatureAge = valueEntity(180, "s");
  inspector = getInspectorMarkup(renderFocusedSource("room-temperature"));
  assert.match(inspector, /data-source-kind="api"\s+data-source-state="stale"/);
  assert.match(inspector, /Verouderd/);

  Object.assign(state.entities, {
    apiInputCoolingEnable: binaryEntity(false),
    apiInputCoolingEnableValid: binaryEntity(true),
  });
  inspector = getInspectorMarkup(renderFocusedSource("cooling-enable"));
  assert.match(inspector, /data-source-kind="api"\s+data-source-state="valid"/);
  assert.match(inspector, /Geblokkeerd/);

  Object.assign(state.entities, {
    externalHeatDemandSource: { value: "API input", option: ["Disabled", "HA input", "API input"] },
    apiInputExternalHeatDemand: valueEntity(0, "kW"),
    apiInputExternalHeatDemandValid: binaryEntity(true),
  });
  inspector = getInspectorMarkup(renderFocusedSource("external-heat-demand"));
  assert.match(inspector, /data-source-kind="api"\s+data-source-state="valid"/);
  assert.match(inspector, /<strong>0 kW<\/strong>/);
});

test("Power House vertaalt de firmwarebron naar de werkelijk gebruikte externe route", () => {
  setSourceSelectionState(true);
  Object.assign(state.entities, {
    externalHeatDemandSource: { value: "API input", option: ["Disabled", "HA input", "API input"] },
    apiInputExternalHeatDemand: valueEntity(1.4, "kW"),
    apiInputExternalHeatDemandValid: binaryEntity(true),
    powerHouseDemandSource: valueEntity("external"),
  });

  let markup = renderFocusedSource("external-heat-demand");
  assert.match(markup, /aria-label="Ingesteld: API-invoer\. Gebruikt: API-invoer"/);
  assert.match(getInspectorMarkup(markup), /data-source-kind="api"\s+data-source-state="valid"\s+data-source-effective="true"/);

  state.entities.powerHouseDemandSource = valueEntity("model");
  markup = renderFocusedSource("external-heat-demand");
  assert.match(markup, /aria-label="Ingesteld: API-invoer\. Gebruikt: Huismodel"/);
  assert.doesNotMatch(getInspectorMarkup(markup), /data-source-kind="api"[^>]+data-source-effective="true"/);

  delete state.entities.powerHouseDemandSource;
  markup = renderFocusedSource("external-heat-demand");
  assert.match(markup, /aria-label="Ingesteld: API-invoer\. Gebruikt: —"/);
});

test("ongeldige Home Assistant-dauwpuntbron blijft zichtbaar als gekozen bronprobleem", () => {
  setSourceSelectionState(true);
  Object.assign(state.entities, {
    coolingDewPointSource: { value: "Home Assistant", option: ["Auto", "Home Assistant", "API input", "MQTT"] },
    coolingDewPointHa: valueEntity(15.2, "°C"),
    coolingDewPointHaValid: binaryEntity(false),
  });

  const markup = renderFocusedSource("cooling-dew-point");
  assert.match(getInspectorMarkup(markup), /Huidige bron niet beschikbaar: HA-bron ongeldig/);
  assert.match(getSelectMarkup(markup, "coolingDewPointSource"), /<option value="Home Assistant" selected disabled>Kies een beschikbare bron<\/option>/);
  assert.doesNotMatch(getSelectMarkup(markup, "coolingDewPointSource"), />Home Assistant<\/option>/);
  assert.match(getInspectorMarkup(markup), /data-source-kind="ha"\s+data-source-state="invalid"/);
});

test("uitgeschakelde CIC en OpenTherm verdwijnen uit keuzes en metingen", () => {
  setSourceSelectionState(true);
  state.entities.roomTempSource.value = "HA input";
  state.entities.cicPollingEnabled = binaryEntity(false);
  state.entities.otEnabled = binaryEntity(false);

  let markup = renderFocusedSource("room-temperature");
  let inspector = getInspectorMarkup(markup);
  assert.doesNotMatch(getSelectMarkup(markup, "roomTempSource"), /<option value="(?:CIC|OT thermostat)"/);
  assert.doesNotMatch(inspector, /data-source-kind="(?:cic|ot)"/);

  [
    ["OT thermostat", /OpenTherm staat uit/, />OT-thermostaat<\/option>/, /data-source-kind="ot"/],
    ["CIC", /CIC-polling staat uit/, />CIC<\/option>/, /data-source-kind="cic"/],
  ].forEach(([source, warning, option, measurement]) => {
    state.entities.roomTempSource.value = source;
    markup = renderFocusedSource("room-temperature");
    inspector = getInspectorMarkup(markup);
    assert.match(inspector, warning);
    assert.match(getSelectMarkup(markup, "roomTempSource"), new RegExp(`<option value="${source}" selected disabled>Kies een beschikbare bron<\\/option>`));
    assert.doesNotMatch(getSelectMarkup(markup, "roomTempSource"), option);
    assert.doesNotMatch(inspector, measurement);
  });

  state.entities.coolingEnableSource.value = "CIC";
  markup = renderFocusedSource("cooling-enable");
  inspector = getInspectorMarkup(markup);
  assert.match(inspector, /Huidige legacybron niet beschikbaar: CIC-polling staat uit; kies een nieuwe bron\./);
  assert.match(getSelectMarkup(markup, "coolingEnableSource"), /<option value="CIC" selected disabled>Kies een beschikbare bron<\/option>/);
  assert.doesNotMatch(getSelectMarkup(markup, "coolingEnableSource"), />CIC \(legacy\)<\/option>/);
});

test("secundaire bronselecties blijven alleen zichtbaar wanneer hun hoofdkeuze ze gebruikt", () => {
  setSourceSelectionState(true);

  let markup = renderFocusedSource("water-supply");
  assert.match(getSelectMarkup(markup, "localWaterSupplyTempSource"), /<option value="PT1000" selected>/);
  state.entities.waterSupplySource.value = "CIC";
  markup = renderFocusedSource("water-supply");
  assert.doesNotMatch(getInspectorMarkup(markup), /data-oq-field="localWaterSupplyTempSource"/);

  state.entities.flowSource.value = "Outdoor unit";
  state.entities.qFlowSource.value = "Auto";
  markup = renderFocusedSource("flow-source");
  getSelectMarkup(markup, "qFlowSource");
  getSelectMarkup(markup, "outdoorUnitFlowMode");

  state.entities.qFlowSource.value = "Local";
  markup = renderFocusedSource("flow-source");
  getSelectMarkup(markup, "qFlowSource");
  assert.doesNotMatch(getInspectorMarkup(markup), /data-oq-field="outdoorUnitFlowMode"/);

  state.entities.flowSource.value = "CIC";
  markup = renderFocusedSource("flow-source");
  assert.doesNotMatch(getInspectorMarkup(markup), /data-oq-field="qFlowSource"|data-oq-field="outdoorUnitFlowMode"/);
});

test("MQTT verdwijnt uit alle bronselecties en metingen wanneer de integratie uitstaat", () => {
  setSourceSelectionState(true);
  MQTT_SOURCE_SELECTS.forEach(([key, sourceKey]) => {
    const enabledMarkup = renderFocusedSource(sourceKey);
    assert.match(getSelectMarkup(enabledMarkup, key), /<option value="MQTT"/);
  });
  assert.match(renderFocusedSource("room-temperature"), /data-source-kind="mqtt"\s+data-source-state="valid"/);

  setSourceSelectionState(false);
  MQTT_SOURCE_SELECTS.forEach(([key, sourceKey]) => {
    const disabledMarkup = renderFocusedSource(sourceKey);
    assert.doesNotMatch(getSelectMarkup(disabledMarkup, key), /<option value="MQTT"/);
    assert.doesNotMatch(getInspectorMarkup(disabledMarkup), /data-source-kind="mqtt"/);
    state.entities[key].value = "MQTT";
    const disabledCurrentMarkup = renderFocusedSource(sourceKey);
    const selectMarkup = getSelectMarkup(disabledCurrentMarkup, key);
    assert.match(selectMarkup, /<option value="MQTT" selected disabled>Kies een beschikbare bron<\/option>/);
    assert.doesNotMatch(selectMarkup, />MQTT<\/option>/);
    assert.match(getInspectorMarkup(disabledCurrentMarkup), /Huidige bron niet beschikbaar: MQTT staat uit/);
    assert.doesNotMatch(getInspectorMarkup(disabledCurrentMarkup), /data-source-kind="mqtt"/);
  });
});

test("een uitgeschakeld MQTT-inputtopic verdwijnt alleen bij het bijbehorende signaal", () => {
  setSourceSelectionState(true);
  state.mqttStatus.input_enabled.room_temperature = false;

  let markup = renderFocusedSource("room-temperature");
  assert.doesNotMatch(getSelectMarkup(markup, "roomTempSource"), /<option value="MQTT"/);
  assert.doesNotMatch(getInspectorMarkup(markup), /data-source-kind="mqtt"/);
  assert.match(getSelectMarkup(renderFocusedSource("room-setpoint"), "roomSetpointSource"), /<option value="MQTT"/);

  state.entities.roomTempSource.value = "MQTT";
  markup = renderFocusedSource("room-temperature");
  assert.match(getSelectMarkup(markup, "roomTempSource"), /<option value="MQTT" selected disabled>Kies een beschikbare bron<\/option>/);
  assert.match(getInspectorMarkup(markup), /Huidige bron niet beschikbaar: MQTT-topic staat uit/);
  assert.doesNotMatch(getInspectorMarkup(markup), /data-source-kind="mqtt"/);
});

test("MQTT als buitentemperatuurbron waarschuwt voor ontbrekende opstartwaarde en CM98", () => {
  setSourceSelectionState(true);
  state.entities.outsideTempSource.value = "MQTT";

  const mqttMarkup = renderFocusedSource("outside-temperature");
  assert.match(mqttMarkup, /Na een \(her\)start is de MQTT-buitentemperatuur pas geldig na een nieuwe live publicatie\./);
  assert.match(mqttMarkup, /kan OpenQuatt naar CM98 \(antivriescirculatie\) gaan/);
  assert.match(mqttMarkup, /De wachttijd hangt af van het publicatie-interval\./);
  assert.match(mqttMarkup, /Overweeg daarom Auto; dan kan OpenQuatt tijdens het wachten een andere geldige buitentemperatuurbron gebruiken\./);

  state.entities.outsideTempSource.value = "Outdoor unit";
  const outdoorUnitMarkup = renderFocusedSource("outside-temperature");
  assert.doesNotMatch(outdoorUnitMarkup, /kan OpenQuatt naar CM98 \(antivriescirculatie\) gaan/);
});

test("bronstatusbadges openen hun uitleg op aanraken", () => {
  setSourceSelectionState(true);
  const markup = renderSettingsSensorSelectionSection();
  assert.match(markup, /data-info-id="mqttRoomTemperature-info"[^>]*aria-expanded="false"[^>]*>Geldig<\/button>/s);

  state.settingsInfoOpen = "mqttRoomTemperature-info";
  const openMarkup = renderSettingsSensorSelectionSection();
  assert.match(openMarkup, /data-info-id="mqttRoomTemperature-info"[^>]*aria-expanded="true"[^>]*>Geldig<\/button>/s);
  assert.match(openMarkup, /MQTT heeft een geldige, recente waarde ontvangen\./);
  state.settingsInfoOpen = "";
});

test("ongeldige PT1000 toont geen misleidende nulwaarde", () => {
  setSourceSelectionState(false);
  Object.assign(state.entities, {
    waterSupplySource: { value: "Local", option: ["Local"] },
    localWaterSupplyTempSource: { value: "PT1000", option: ["PT1000", "DS18B20"] },
    supplyTemp: { value: 31.4, state: "31.4", uom: "°C" },
    waterSupplyTempEffectiveSource: { value: "HP2 water out (fallback)", state: "HP2 water out (fallback)" },
    waterSupplyTempEsp: { value: null, state: "nan", uom: "°C" },
    waterSupplyTempPt1000: { value: null, state: "nan", uom: "°C" },
    waterSupplyTempDs18b20: { value: 0, state: "0.0", uom: "°C" },
    pt1000ReadProblem: { value: true, state: "ON" },
  });

  const markup = renderFocusedSource("water-supply");

  assert.match(markup, /<div class="oq-settings-source-row-label">PT1000<\/div>\s*<strong>—<\/strong>/);
  assert.doesNotMatch(markup, /<div class="oq-settings-source-row-label">PT1000<\/div>\s*<strong>0 °C<\/strong>/);
  assert.match(markup, /<div class="oq-settings-source-row-label">DS18B20<\/div>\s*<strong>0 °C<\/strong>/);
  assert.match(getInspectorMarkup(markup), /<span>Gebruikt<\/span>[\s\S]*?<span>HP2 uitgaand water \(fallback\)<\/span>/);
  assert.match(getInspectorMarkup(markup), /De ingestelde lokale PT1000-bron levert geen geldige waarde; OpenQuatt gebruikt een fallback\./);
  assert.match(markup, /aria-label="Bronprobleem: De ingestelde lokale PT1000-bron levert geen geldige waarde; OpenQuatt gebruikt een fallback\."/);
});

test("gewijzigde bronconfiguratie toont dat de aanvoerkalibratie opnieuw moet", () => {
  setSourceSelectionState(false);
  Object.assign(state.entities, {
    waterSupplySource: { value: "CIC", option: ["Local", "CIC", "HA input"] },
    supplyTemp: { value: 31.2, uom: "°C" },
    waterSupplyTempEffectiveSource: { value: "CIC", state: "CIC" },
    waterSupplyCalibrationStatus: { value: "Recalibration required: CIC", state: "Recalibration required: CIC" },
    waterSupplyCalibrationRequired: { value: true, state: "ON" },
  });

  const markup = renderFocusedSource("water-supply");

  assert.match(markup, /class="oq-settings-info oq-settings-source-info oq-settings-source-info--error oq-settings-source-info--circle"/);
  assert.match(markup, /<button[^>]*data-oq-action="toggle-settings-info"[^>]*data-info-id="supplyTemp-info"[^>]*aria-expanded="false"[^>]*>i<\/button>/s);
  assert.match(markup, /Ruwe waarde; kalibreer via Service\./);
  assert.match(markup, /Ruwe metingen/);
  assert.doesNotMatch(markup, /<span>Kalibratie<\/span>/);
  assert.match(markup, /De aanvoerbron of bronconfiguratie is gewijzigd\./);
  assert.match(markup, /voer de temperatuurkalibratie opnieuw uit/);

  state.entities.waterSupplyCalibrationStatus = { value: "Calibrated: CIC", state: "Calibrated: CIC" };
  state.entities.waterSupplyCalibrationRequired = { value: false, state: "OFF" };
  state.entities.waterSupplyCalibrationOffset = { value: -0.6, uom: "°C" };
  const calibratedMarkup = renderFocusedSource("water-supply");

  assert.match(calibratedMarkup, /class="oq-settings-info oq-settings-source-info oq-settings-source-info--valid oq-settings-source-info--circle"/);
  assert.match(calibratedMarkup, /aria-label="Uitleg bij Gebruikte waarde"/);
  assert.match(calibratedMarkup, /Gekalibreerd; ruwe metingen hieronder\./);
  assert.doesNotMatch(calibratedMarkup, /<span>Kalibratie<\/span>|voer de temperatuurkalibratie opnieuw uit/);

  state.settingsInfoOpen = "supplyTemp-info";
  const openInfoMarkup = renderFocusedSource("water-supply");
  assert.match(openInfoMarkup, /data-info-id="supplyTemp-info"[^>]*aria-expanded="true"/s);
  assert.match(openInfoMarkup, /class="oq-settings-info-popover"\s*>/);
  state.settingsInfoOpen = "";
});
