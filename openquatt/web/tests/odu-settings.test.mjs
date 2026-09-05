import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = {
  location: { pathname: "/dev.html" },
  setTimeout,
  clearTimeout,
  localStorage: { getItem: () => null },
};

const {
  getOduSettingsEndpoint,
  getOduSettingsHpIndexes,
  normalizeOduSettingsStatus,
  renderOduSettingsModal,
  shouldRefreshOduSettingsSurface,
  updateOduSettingsDraft,
} = await import("../js/src/features/odu-settings.js");
const { state } = await import("../js/src/core/state.js");

test("bodemplaatservice gebruikt per-HP endpoints en de installatie-topologie", () => {
  state.entities = { installationTopology: { value: "duo", state: "duo" } };
  state.optionalMissingEntities = {};
  assert.equal(getOduSettingsEndpoint(1, "status"), "/openquatt/odu-settings/hp1/status");
  assert.equal(getOduSettingsEndpoint(2, "save"), "/openquatt/odu-settings/hp2/save");
  assert.deepEqual(getOduSettingsHpIndexes(), [1, 2]);
});

test("status houdt actuele, opgeslagen en generatie-defaultwaarden gescheiden", () => {
  const status = normalizeOduSettingsStatus({
    hp: 1,
    loaded: true,
    profile_available: true,
    auto_reapply: true,
    identity_ready: true,
    identity_matches: true,
    variant: 2,
    actual: { mode: 3, start_temperature_c: 4, stop_delta_c: 3 },
    desired: { mode: 1, start_temperature_c: 2, stop_delta_c: 4 },
    defaults: { mode: 3, start_temperature_c: 4, stop_delta_c: 3 },
  });
  assert.deepEqual(status.actual, { mode: 3, startTemperatureC: 4, stopDeltaC: 3 });
  assert.deepEqual(status.desired, { mode: 1, startTemperatureC: 2, stopDeltaC: 4 });
  assert.equal(status.defaults.mode, 3);
  assert.equal(status.autoReapply, true);
});

test("modal maakt modus en grenzen instelbaar en berekent de stoptemperatuur", (t) => {
  const originalDocument = globalThis.document;
  globalThis.document = { activeElement: null, body: {}, querySelector: () => null };
  t.after(() => { globalThis.document = originalDocument; });
  state.entities = { installationTopology: { value: "single", state: "single" } };
  state.systemModal = "odu-bottom-plate-settings";
  state.oduSettingsStatuses = {
    1: normalizeOduSettingsStatus({
      hp: 1,
      loaded: true,
      identity_ready: true,
      identity_matches: false,
      variant: 2,
      actual: { mode: 3, start_temperature_c: 4, stop_delta_c: 3 },
      defaults: { mode: 3, start_temperature_c: 4, stop_delta_c: 3 },
    }),
  };
  state.oduSettingsDrafts = {};
  const modal = renderOduSettingsModal();
  assert.match(modal, /1 · Volgt buitentemperatuur/);
  assert.match(modal, /2 · Tijdens ontdooien/);
  assert.match(modal, /3 · Onbekend \(standaard V1\.5 en V2\)/);
  assert.doesNotMatch(modal, /0 · Uit/);
  assert.match(modal, /niet officieel gedocumenteerd/);
  assert.match(modal, /onder 0 °C aan te gaan, boven 0 °C uit te gaan en tijdens ontdooien actief te zijn/);
  assert.match(modal, /standaardinstelling voor Quatt buitenunit V1\.5 en V2/);
  assert.match(modal, /data-oq-odu-settings-field="mode"/);
  assert.match(modal, /data-oq-odu-settings-field="startTemperatureC"/);
  assert.match(modal, /data-oq-odu-settings-field="stopDeltaC"/);
  assert.match(modal, /data-oq-odu-temperature-settings hidden/);
  assert.match(modal, /7 °C/);
  assert.match(modal, /Na herstart automatisch opnieuw toepassen/);
  assert.match(modal, /ook aanpassen terwijl de compressor draait/);
  assert.match(modal, /opnieuw toe, ook als de compressor draait/);
  assert.doesNotMatch(modal, /zodra de buitenunit in standby/);
  assert.match(modal, /Quatt buitenunit V1\.5/);
  assert.match(modal, /oq-helper-modal--wide oq-settings-odu-modal/);
  assert.match(modal, /oq-settings-odu-modal-body" data-oq-modal-scroll="body"/);
  assert.equal(shouldRefreshOduSettingsSurface(), true);

  assert.doesNotMatch(modal, /Technische details/);

  state.oduSettingsDrafts[1] = {
    mode: "1",
    startTemperatureC: "",
    stopDeltaC: "3",
    autoReapply: false,
    dirty: true,
  };
  const invalidModal = renderOduSettingsModal();
  assert.match(invalidModal, /data-oq-odu-stop-temperature>—<\/strong>/);
  assert.match(invalidModal, /data-oq-action="odu-settings-save" data-hp="1" disabled/);

  state.oduSettingsDrafts[1] = { ...state.oduSettingsDrafts[1], mode: "0", startTemperatureC: "4" };
  const unknownModeModal = renderOduSettingsModal();
  assert.match(unknownModeModal, /<option value="" selected disabled>Kies een regelmethode<\/option>/);
  assert.match(unknownModeModal, /data-oq-action="odu-settings-save" data-hp="1" disabled/);

  state.oduSettingsStatuses = {};
  state.oduSettingsError = "Status ophalen mislukt. Controleer de verbinding met OpenQuatt.";
  const failedModal = renderOduSettingsModal();
  assert.match(failedModal, /Status niet beschikbaar/);
  assert.doesNotMatch(failedModal, /Status laden\.\.\./);
});

test("voorbeeld volgt gewijzigde temperatuurgrenzen direct", () => {
  const startOutput = { textContent: "4 °C of kouder" };
  const stopOutput = { textContent: "7 °C of warmer" };
  const panel = {
    querySelector(selector) {
      if (selector === "[data-oq-odu-temperature-settings]") return null;
      if (selector === "[data-oq-odu-mode-description]") return null;
      if (selector === "[data-oq-odu-start-temperature]") return startOutput;
      if (selector === "[data-oq-odu-stop-temperature]") return stopOutput;
      return null;
    },
  };
  const input = {
    dataset: { oqOduSettingsHp: "1", oqOduSettingsField: "startTemperatureC" },
    value: "3",
    checked: false,
    closest(selector) {
      assert.equal(selector, ".oq-settings-odu-runtime-panel");
      return panel;
    },
  };
  state.oduSettingsDrafts = {
    1: { mode: "3", startTemperatureC: "4", stopDeltaC: "3", autoReapply: false, dirty: false },
  };

  assert.equal(updateOduSettingsDraft(input), true);
  assert.equal(startOutput.textContent, "3 °C of kouder");
  assert.equal(stopOutput.textContent, "6 °C of warmer");

  input.dataset.oqOduSettingsField = "stopDeltaC";
  input.value = "4";
  assert.equal(updateOduSettingsDraft(input), true);
  assert.equal(startOutput.textContent, "3 °C of kouder");
  assert.equal(stopOutput.textContent, "7 °C of warmer");
});

test("regelmethode wisselt toelichting en temperatuurvelden direct", () => {
  const temperatureSettings = { hidden: false };
  const modeOutput = { textContent: "" };
  const panel = {
    querySelector(selector) {
      if (selector === "[data-oq-odu-temperature-settings]") return temperatureSettings;
      if (selector === "[data-oq-odu-mode-description]") return modeOutput;
      return null;
    },
  };
  const input = {
    dataset: { oqOduSettingsHp: "1", oqOduSettingsField: "mode" },
    value: "2",
    checked: false,
    closest: () => panel,
  };
  state.oduSettingsDrafts = {
    1: { mode: "1", startTemperatureC: "4", stopDeltaC: "3", autoReapply: false, dirty: false },
  };

  assert.equal(updateOduSettingsDraft(input), true);
  assert.equal(temperatureSettings.hidden, true);
  assert.equal(modeOutput.textContent, "De verwarming schakelt in zodra een ontdooicyclus start en twee minuten nadat deze is afgelopen weer uit.");

  input.value = "1";
  assert.equal(updateOduSettingsDraft(input), true);
  assert.equal(temperatureSettings.hidden, false);
  assert.equal(modeOutput.textContent, "De verwarming schakelt op basis van de ingestelde buitentemperatuurgrenzen.");
});

test("opslaan herstelt na geldige invoer maar blijft geblokkeerd bij onbeschikbare of bezige buitenunit", () => {
  const saveButton = { disabled: true };
  const input = {
    dataset: { oqOduSettingsHp: "1", oqOduSettingsField: "startTemperatureC" },
    value: "4",
    closest: () => ({ querySelector: (selector) => selector === '[data-oq-action="odu-settings-save"]' ? saveButton : null }),
  };
  state.busyAction = "";
  state.oduSettingsStatuses = { 1: normalizeOduSettingsStatus({ hp: 1, available: true, identity_ready: true, loaded: true }) };
  state.oduSettingsDrafts = { 1: { mode: "1", startTemperatureC: "", stopDeltaC: "3", autoReapply: false } };
  updateOduSettingsDraft(input);
  assert.equal(saveButton.disabled, false);
  input.value = "";
  updateOduSettingsDraft(input);
  assert.equal(saveButton.disabled, true);
  input.value = "4";
  for (const blocked of [{ available: false }, { identityReady: false }, { unsupported: true }, { busy: true }]) {
    state.oduSettingsStatuses[1] = { available: true, identityReady: true, ...blocked };
    updateOduSettingsDraft(input);
    assert.equal(saveButton.disabled, true);
  }
  state.oduSettingsStatuses[1] = { available: true, identityReady: true };
  state.busyAction = "odu-settings-hp1-save";
  updateOduSettingsDraft(input);
  assert.equal(saveButton.disabled, true);
  state.busyAction = "";
  updateOduSettingsDraft(input);
  assert.equal(saveButton.disabled, false);
});

test("firmwarecontract schrijft sheet 3237-3239 via Modbus 3236-3238", async () => {
  const [logic, source, packageSource, webAccess] = await Promise.all([
    readFile(new URL("../../../openquatt/includes/odu/oq_odu_bottom_plate_settings.h", import.meta.url), "utf8"),
    readFile(new URL("../../../components/openquatt_odu_settings/OpenQuattOduSettings.cpp", import.meta.url), "utf8"),
    readFile(new URL("../../oq_HP_io.yaml", import.meta.url), "utf8"),
    readFile(new URL("../../oq_web_access.yaml", import.meta.url), "utf8"),
  ]);
  assert.match(logic, /BOTTOM_PLATE_START_ADDRESS = 3236U/);
  assert.match(logic, /variant == Variant::V1 \? 1U : 3U/);
  assert.match(source, /create_write_single_command/);
  assert.match(source, /queue_readback_/);
  assert.doesNotMatch(source, /PENDING_SAFE|queue_guard_/);
  assert.match(packageSource, /set_odu_identity/);
  assert.match(packageSource, /notify_odu_offline/);
  assert.match(webAccess, /openquatt_odu_settings/);
});

test("netwerkfout vervangt de laadstatus door duidelijke niet-beschikbaarcopy", async () => {
  const featureSource = await readFile(new URL("../js/src/features/odu-settings.js", import.meta.url), "utf8");
  const mockSource = await readFile(new URL("../js/mock-device.js", import.meta.url), "utf8");
  assert.match(featureSource, /failed to fetch\|networkerror\|load failed/i);
  assert.match(featureSource, /Status ophalen mislukt\. Controleer de verbinding met OpenQuatt\./);
  assert.match(featureSource, /changed \|\| hadError/);
  assert.match(mockSource, /pathname\.match\(\/\\\/openquatt\\\/odu-settings/);
});
