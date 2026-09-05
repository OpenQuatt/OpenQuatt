import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = {
  localStorage: {
    getItem: () => null,
  },
};

const { state } = await import("../js/src/core/state.js");
const { getSettingsServiceModel, renderHpWaterCalibrationWizard } = await import("../js/src/settings/service.js");
const { handleSystemAction } = await import("../js/src/features/system-actions.js");
const { getWaterSupplyCorrectionView, renderHpWaterSensorOffsetSettings, renderHpWaterSensorOffsetsModal, renderWaterSettingsFields } = await import("../js/src/settings/water.js");

test("kalibratieresultaat bevat de brongebonden aanvoer-offset", () => {
  state.loadingEntities = false;
  state.entities = {
    hp1WaterIn: { value: 25.1, uom: "°C" },
    hp1WaterInRaw: { value: 25.0, uom: "°C" },
    hp1WaterInOffset: { value: 0.1, uom: "°C" },
    hp1WaterInOffsetSuggested: { value: 0.15, uom: "°C" },
    hpWaterCalibrationResultHp1InRawAvg: { value: 25.0, uom: "°C" },
    hpWaterCalibrationResultReference: { value: 25.15, uom: "°C" },
    hpWaterCalibrationResultSpreadBefore: { value: 0.3, uom: "°C" },
    hpWaterCalibrationResultSupplyRawAvg: { value: 24.7, uom: "°C" },
    hpWaterCalibrationResultSupplySource: { value: "Local - PT1000", state: "Local - PT1000" },
    supplyTemp: { value: 24.7, uom: "°C" },
    waterSupplyCalibrationOffset: { value: 0, uom: "°C" },
    waterSupplyCalibrationOffsetSuggested: { value: 0.45, uom: "°C" },
    hpWaterCalibrationApply: { value: false },
  };

  const markup = renderHpWaterCalibrationWizard({
    status: "DONE: stable spread 0.30C",
    running: false,
    resultReady: true,
    startDisabled: false,
    abortDisabled: true,
    applyDisabled: false,
    busy: false,
    controlsAvailable: true,
  });

  assert.match(markup, /Aanvoer \(Local - PT1000\)/);
  assert.match(markup, /Aanvoerbron Local - PT1000/);
  assert.match(markup, /0\.45 °C/);
  assert.match(markup, /25\.15 °C/);
});

test("lege resultaatbron valt tijdens de meting terug op de actieve bron", () => {
  state.loadingEntities = false;
  state.entities = {
    hp1WaterIn: { value: 25.1, uom: "°C" },
    hp1WaterInRaw: { value: 25.0, uom: "°C" },
    hpWaterCalibrationResultSupplySource: { value: "", state: "" },
    waterSupplyTempEffectiveSource: { value: "CIC", state: "CIC" },
    supplyTemp: { value: 24.7, uom: "°C" },
  };

  const markup = renderHpWaterCalibrationWizard({
    status: "MEASURING",
    running: true,
    resultReady: false,
    startDisabled: true,
    abortDisabled: false,
    applyDisabled: true,
    busy: true,
    controlsAvailable: true,
  });

  assert.match(markup, /Aanvoer \(CIC\)/);
  assert.doesNotMatch(markup, /Aanvoer \(IDLE\)/);
  assert.doesNotMatch(markup, /Supply verschil|Voorlopige aanvoercorrectie/);
});

test("kalibratie mengt drie minuten en toont de verwachte totale duur", () => {
  state.loadingEntities = false;
  state.entities = {
    cm100Active: { value: true, state: "ON" },
    hpWaterCalibrationStatus: { value: "MIXING", state: "MIXING" },
    hpWaterCalibrationRemaining: { value: 300, uom: "s" },
    hpWaterCalibrationPhase: { value: 1 },
  };

  const markup = renderHpWaterCalibrationWizard({
    status: "MIXING",
    running: true,
    resultReady: false,
    startDisabled: true,
    abortDisabled: false,
    applyDisabled: true,
    busy: false,
    controlsAvailable: true,
  });
  const task = getSettingsServiceModel().tasks.find(({ key }) => key === "hp-water-calibration");

  assert.match(markup, /meting start over 180 s/);
  const taskMarkup = task.renderCard();
  assert.match(taskMarkup, /ongeveer 3 tot 5 minuten/);
  assert.match(taskMarkup, /mengt het water 3 minuten/);
});

test("sensorcorrecties tonen de actieve aanvoercorrectie alleen-lezen", () => {
  state.loadingEntities = false;
  state.entities = {
    supplyTemp: { value: 22.54, uom: "°C" },
    waterSupplyCalibrationOffset: { value: -0.6, uom: "°C" },
    waterSupplyCalibrationStatus: { value: "Calibrated: CIC", state: "Calibrated: CIC" },
    waterSupplyCalibrationRequired: { value: false, state: "OFF" },
    waterSupplyTempEffectiveSource: { value: "CIC", state: "CIC" },
    waterSupplyTempFallbackActive: { value: false, state: "OFF" },
  };

  const view = getWaterSupplyCorrectionView();
  const markup = renderHpWaterSensorOffsetSettings();

  assert.equal(view.rawValue, 23.14);
  assert.equal(view.offsetValue, -0.6);
  assert.equal(view.activeValue, 22.54);
  assert.match(markup, /Watertemperatuurcorrecties/);
  assert.match(markup, /Aanvoer \(CIC\)/);
  assert.match(markup, /Brongebonden kalibratie actief/);
  assert.match(markup, /23\.14 °C/);
  assert.match(markup, /-0\.60 °C/);
  assert.doesNotMatch(markup, /data-oq-settings-field="waterSupplyCalibrationOffset"/);
});

test("sensorcorrecties tonen een stale aanvoer-offset niet als actief", () => {
  state.loadingEntities = false;
  state.entities = {
    supplyTemp: { value: 22.54, uom: "°C" },
    waterSupplyCalibrationOffset: { value: -0.6, uom: "°C" },
    waterSupplyCalibrationStatus: { value: "Recalibration required: CIC", state: "Recalibration required: CIC" },
    waterSupplyCalibrationRequired: { value: true, state: "ON" },
    waterSupplyTempEffectiveSource: { value: "CIC", state: "CIC" },
    waterSupplyTempFallbackActive: { value: false, state: "OFF" },
  };

  const view = getWaterSupplyCorrectionView();
  const markup = renderHpWaterSensorOffsetSettings();

  assert.equal(view.rawValue, 22.54);
  assert.equal(view.offsetValue, 0);
  assert.match(markup, /Opnieuw kalibreren/);
  assert.doesNotMatch(markup, /-0\.60 °C/);
});

test("herkalibratiesignaal schakelt een nog niet ververste status fail-closed uit", () => {
  state.loadingEntities = false;
  state.entities = {
    supplyTemp: { value: 22.54, uom: "°C" },
    waterSupplyCalibrationOffset: { value: -0.6, uom: "°C" },
    waterSupplyCalibrationStatus: { value: "Calibrated: CIC", state: "Calibrated: CIC" },
    waterSupplyCalibrationRequired: { value: true, state: "ON" },
    waterSupplyTempEffectiveSource: { value: "CIC", state: "CIC" },
    waterSupplyTempFallbackActive: { value: false, state: "OFF" },
  };

  const view = getWaterSupplyCorrectionView();

  assert.equal(view.rawValue, 22.54);
  assert.equal(view.offsetValue, 0);
  assert.equal(view.statusLabel, "Opnieuw kalibreren");
});

test("sensorcorrecties passen de bronoffset niet toe op een runtime-fallback", () => {
  state.loadingEntities = false;
  state.entities = {
    supplyTemp: { value: 21.9, uom: "°C" },
    waterSupplyCalibrationOffset: { value: -0.6, uom: "°C" },
    waterSupplyCalibrationStatus: { value: "Calibrated: CIC", state: "Calibrated: CIC" },
    waterSupplyCalibrationRequired: { value: false, state: "OFF" },
    waterSupplyTempEffectiveSource: { value: "HP2 water out (fallback)", state: "HP2 water out (fallback)" },
    waterSupplyTempFallbackActive: { value: true, state: "ON" },
  };

  const view = getWaterSupplyCorrectionView();
  const markup = renderHpWaterSensorOffsetSettings();

  assert.equal(view.rawValue, 21.9);
  assert.equal(view.offsetValue, 0);
  assert.match(markup, /Fallback actief; correctie tijdelijk uit/);
  assert.doesNotMatch(markup, /-0\.60 °C/);
});

test("waterinstellingen openen sensorcorrecties in een modal", () => {
  state.loadingEntities = false;
  state.entities = {
    hp1WaterIn: { value: 21.46, uom: "°C" },
    hp1WaterInOffset: { value: -0.16, uom: "°C" },
    supplyTemp: { value: 22.54, uom: "°C" },
    waterSupplyCalibrationOffset: { value: -0.6, uom: "°C" },
    waterSupplyCalibrationStatus: { value: "Calibrated: CIC", state: "Calibrated: CIC" },
    waterSupplyTempEffectiveSource: { value: "CIC", state: "CIC" },
  };

  const settingsMarkup = renderWaterSettingsFields();
  const modalMarkup = renderHpWaterSensorOffsetsModal();

  assert.match(settingsMarkup, /data-oq-action="open-water-sensor-corrections-modal"/);
  assert.doesNotMatch(settingsMarkup, /data-oq-hp-offset-raw-key/);
  assert.match(modalMarkup, /role="dialog"/);
  assert.match(modalMarkup, /Watertemperatuurcorrecties/);
  assert.match(modalMarkup, /Waarom is de aanvoercorrectie niet handmatig aanpasbaar\?/);
  assert.match(modalMarkup, /aparte correctie voor lokale PT1000, lokale DS18B20, CIC en Home Assistant/);
  assert.match(modalMarkup, /CIC-correctie blijft geldig na een URL-wijziging/);
  assert.match(modalMarkup, /andere Home Assistant-invoer moet opnieuw worden gekalibreerd/);

  state.root = null;
  assert.equal(handleSystemAction("open-water-sensor-corrections-modal", {}), true);
  assert.equal(state.systemModal, "water-sensor-corrections");
  state.systemModal = "";
});
