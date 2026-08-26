import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = {
  localStorage: {
    getItem: () => null,
  },
};

const { state } = await import("../js/src/core/state.js");
const { SETTINGS_GROUP_KEY_MAP } = await import("../js/src/core/entity-sync.js");
const { renderSettingsOpenThermCicSection, renderSettingsSensorSelectionSection } = await import("../js/src/settings/integrations.js");

const MQTT_SOURCE_SELECT_KEYS = [
  "roomTempSource",
  "roomSetpointSource",
  "outsideTempSource",
  "heatingEnableSource",
  "coolingEnableSource",
  "coolingDewPointSource",
];

function getSelectMarkup(markup, key) {
  const match = markup.match(new RegExp(`<select[^>]+data-oq-field="${key}"[^>]*>([\\s\\S]*?)<\\/select>`));
  assert.ok(match, `select ${key} ontbreekt`);
  return match[1];
}

function setSourceSelectionState(mqttEnabled) {
  state.loadingEntities = false;
  state.drafts = {};
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
    roomTempSource: { value: "OT thermostat", option: ["OT thermostat", "MQTT"] },
    roomSetpointSource: { value: "OT thermostat", option: ["OT thermostat", "MQTT"] },
    outsideTempSource: { value: "Outdoor unit", option: ["Outdoor unit", "MQTT"] },
    heatingEnableSource: { value: "Disabled", option: ["Disabled", "MQTT"] },
    coolingEnableSource: { value: "Disabled", option: ["Disabled", "MQTT"] },
    coolingDewPointSource: { value: "Auto", option: ["Auto", "MQTT"] },
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

test("MQTT verdwijnt uit alle bronselecties en metingen wanneer de integratie uitstaat", () => {
  setSourceSelectionState(true);
  const enabledMarkup = renderSettingsSensorSelectionSection();
  MQTT_SOURCE_SELECT_KEYS.forEach((key) => {
    assert.match(getSelectMarkup(enabledMarkup, key), /<option value="MQTT"/);
  });
  assert.match(enabledMarkup, /<div class="oq-settings-source-row has-status">\s*<div class="oq-settings-source-row-label">MQTT/);

  setSourceSelectionState(false);
  const disabledMarkup = renderSettingsSensorSelectionSection();
  MQTT_SOURCE_SELECT_KEYS.forEach((key) => {
    assert.doesNotMatch(getSelectMarkup(disabledMarkup, key), /<option value="MQTT"/);
  });
  assert.doesNotMatch(disabledMarkup, /MQTT/);

  MQTT_SOURCE_SELECT_KEYS.forEach((key) => {
    state.entities[key].value = "MQTT";
  });
  const disabledCurrentMarkup = renderSettingsSensorSelectionSection();
  MQTT_SOURCE_SELECT_KEYS.forEach((key) => {
    const selectMarkup = getSelectMarkup(disabledCurrentMarkup, key);
    assert.match(selectMarkup, /<option value="" selected disabled>Kies een beschikbare bron<\/option>/);
    assert.doesNotMatch(selectMarkup, /<option value="MQTT"/);
  });
  assert.match(disabledCurrentMarkup, /Huidige bron niet beschikbaar: MQTT staat uit/);
  assert.doesNotMatch(disabledCurrentMarkup, /MQTT-topic staat uit/);
});

test("MQTT als buitentemperatuurbron waarschuwt voor ontbrekende opstartwaarde en CM98", () => {
  setSourceSelectionState(true);
  state.entities.outsideTempSource.value = "MQTT";

  const mqttMarkup = renderSettingsSensorSelectionSection();
  assert.match(mqttMarkup, /Na een \(her\)start is de MQTT-buitentemperatuur pas geldig na een nieuwe live publicatie\./);
  assert.match(mqttMarkup, /kan OpenQuatt naar CM98 \(antivriescirculatie\) gaan/);
  assert.match(mqttMarkup, /De wachttijd hangt af van het publicatie-interval\./);
  assert.match(mqttMarkup, /Overweeg daarom Auto; dan kan OpenQuatt tijdens het wachten een andere geldige buitentemperatuurbron gebruiken\./);

  state.entities.outsideTempSource.value = "Outdoor unit";
  const outdoorUnitMarkup = renderSettingsSensorSelectionSection();
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

  const markup = renderSettingsSensorSelectionSection();

  assert.match(markup, /<div class="oq-settings-source-row-label">PT1000<\/div>\s*<strong>—<\/strong>/);
  assert.doesNotMatch(markup, /<div class="oq-settings-source-row-label">PT1000<\/div>\s*<strong>0 °C<\/strong>/);
  assert.match(markup, /<div class="oq-settings-source-row-label">DS18B20<\/div>\s*<strong>0 °C<\/strong>/);
  assert.match(markup, /<div class="oq-settings-source-row-label">Bron<\/div>\s*<strong>HP2 uitgaand water \(fallback\)<\/strong>/);
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

  const markup = renderSettingsSensorSelectionSection();

  assert.match(markup, /Gebruikte waarde\s*<div class="oq-settings-info oq-settings-source-info oq-settings-source-info--error oq-settings-source-info--circle"/);
  assert.match(markup, /<button[^>]*data-oq-action="toggle-settings-info"[^>]*data-info-id="supplyTemp-info"[^>]*aria-expanded="false"[^>]*>i<\/button>/s);
  assert.match(markup, /Ruwe waarde; kalibreer via Service\./);
  assert.match(markup, /Ruwe metingen/);
  assert.doesNotMatch(markup, /<span>Kalibratie<\/span>/);
  assert.match(markup, /De aanvoerbron of bronconfiguratie is gewijzigd\./);
  assert.match(markup, /voer de temperatuurkalibratie opnieuw uit/);

  state.entities.waterSupplyCalibrationStatus = { value: "Calibrated: CIC", state: "Calibrated: CIC" };
  state.entities.waterSupplyCalibrationRequired = { value: false, state: "OFF" };
  state.entities.waterSupplyCalibrationOffset = { value: -0.6, uom: "°C" };
  const calibratedMarkup = renderSettingsSensorSelectionSection();

  assert.match(calibratedMarkup, /Gebruikte waarde\s*<div class="oq-settings-info oq-settings-source-info oq-settings-source-info--valid oq-settings-source-info--circle"/);
  assert.match(calibratedMarkup, /aria-label="Uitleg bij Gebruikte waarde"/);
  assert.match(calibratedMarkup, /Gekalibreerd; ruwe metingen hieronder\./);
  assert.doesNotMatch(calibratedMarkup, /<span>Kalibratie<\/span>|voer de temperatuurkalibratie opnieuw uit/);

  state.settingsInfoOpen = "supplyTemp-info";
  const openInfoMarkup = renderSettingsSensorSelectionSection();
  assert.match(openInfoMarkup, /data-info-id="supplyTemp-info"[^>]*aria-expanded="true"/s);
  assert.match(openInfoMarkup, /class="oq-settings-info-popover"\s*>/);
  state.settingsInfoOpen = "";
});
