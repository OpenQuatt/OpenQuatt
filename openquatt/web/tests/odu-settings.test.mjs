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
  getOduSettingsEditorModel,
  handleOduSettingsAction,
  normalizeOduSettingsStatus,
  renderOduSettingsModal,
  restoreOduSettingsBackupProfiles,
  shouldRefreshOduSettingsSurface,
  updateOduSettingsDraft,
} = await import("../js/src/features/odu-settings.js");
const { state } = await import("../js/src/core/state.js");
const { setRenderCallback } = await import("../js/src/core/render-scheduler.js");

test("rendering en live invoer delen dezelfde bodemplaatwaarden en save-gates", () => {
  state.entities = { installationTopology: { value: "single" } };
  state.oduSettingsError = "";
  state.controlNotice = "";
  state.busyAction = "";
  state.oduSettingsDrafts = { 1: { mode: "1", startTemperatureC: "4", stopDeltaC: "3", autoReapply: false, dirty: true } };
  for (const patch of [{}, { available: false }, { identityReady: false }, { unsupported: true }, { busy: true }]) {
    state.oduSettingsStatuses = { 1: { available: true, identityReady: true, loaded: true, ...patch } };
    const model = getOduSettingsEditorModel(1);
    const markup = renderOduSettingsModal();
    const button = markup.match(/<button[^>]*data-oq-action="odu-settings-save"[^>]*>/)[0];
    assert.equal(/\bdisabled\b/.test(button), model.saveDisabled);
    const saveButton = { disabled: !model.saveDisabled };
    updateOduSettingsDraft({
      dataset: { oqOduSettingsHp: "1", oqOduSettingsField: "startTemperatureC" }, value: "4",
      closest: () => ({ querySelector: selector => selector.includes("odu-settings-save") ? saveButton : null }),
    });
    assert.equal(saveButton.disabled, model.saveDisabled);
    assert.doesNotMatch(markup, /data-oq-field=/);
    assert.match(markup, /data-oq-odu-stop-temperature>7 °C of warmer/);
  }
});

test("lege of ongeldige temperatuur toont dezelfde onbekende grens bij render en live invoer", () => {
  state.oduSettingsStatuses = { 1: { available: true, identityReady: true, loaded: true } };
  for (const value of ["", "invalid", "0", "-30", "30"]) {
    state.oduSettingsDrafts = { 1: { mode: "1", startTemperatureC: value, stopDeltaC: "3" } };
    const model = getOduSettingsEditorModel(1);
    assert.equal(model.saveDisabled, ["", "invalid"].includes(value));
    assert.ok(renderOduSettingsModal().includes(`data-oq-odu-start-temperature>${model.startCopy}</strong>`));
    assert.ok(renderOduSettingsModal().includes(`data-oq-odu-stop-temperature>${model.stopCopy}</strong>`));
  }
});

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

test("actie en backupherstel melden alleen expliciet bevestigde firmwarestatussen als geslaagd", { timeout: 3000 }, async (t) => {
  const originalFetch = globalThis.fetch;
  const originalTimer = window.setTimeout;
  t.after(() => {
    globalThis.fetch = originalFetch;
    window.setTimeout = originalTimer;
    setRenderCallback(null);
  });
  window.setTimeout = (callback, delay) => setTimeout(callback, delay === 500 ? 0 : delay);
  state.entities = { installationTopology: { value: "single", state: "single" } };
  const values = { mode: 1, start_temperature_c: 4, stop_delta_c: 3 };
  const initial = {
    hp: 1, available: true, loaded: true, identity_ready: true, identity_matches: true,
    profile_available: true, variant: 2, control_board_item: 3639, csrf_token: "test-token",
    actual: values, desired: values, defaults: values, status: "LOADED",
  };
  const outcomes = [
    { status: "OFFLINE", available: false },
    { status: "UNAVAILABLE", available: false },
    { status: "READY" },
    { status: "IDENTITY_REQUIRED" },
    { status: "VERIFY_FAILED", write_uncertain: true },
    { status: "PERSIST_FAILED" },
    { status: "IDENTITY_MISMATCH", identity_matches: false },
    { status: "IN_SYNC", write_uncertain: true },
    { status: "IN_SYNC", loaded: false },
    { status: "IN_SYNC", profile_available: false },
    { status: "IN_SYNC", unsupported: true },
    { status: "IN_SYNC" },
    { status: "PENDING_SAFE" },
    { status: "LOADED" },
  ];
  for (const outcome of outcomes) {
    const completed = { ...initial, ...outcome, busy: false };
    for (const action of ["load", "save", "restore"]) {
      const responses = action === "restore" ? [initial] : [];
      responses.push({ ...initial, status: "SAVE_REQUESTED", busy: true }, completed);
      globalThis.fetch = async () => {
        assert.ok(responses.length, "unexpected request");
        return new Response(JSON.stringify(responses.shift()), { status: 200 });
      };
      const success = action === "load" ? outcome.status === "LOADED"
        : (outcome.status === "IN_SYNC" || outcome.status === "PENDING_SAFE") && Object.keys(outcome).length === 1;
      state.oduSettingsStatuses = { 1: normalizeOduSettingsStatus(initial) };
      state.oduSettingsDrafts = {};
      if (action === "restore") {
        const [result] = await restoreOduSettingsBackupProfiles({ hp1: { ...values, variant: 2, control_board_item: 3639, auto_reapply: false } });
        assert.equal(result.applied, success, `restore: ${JSON.stringify(outcome)}`);
        if (success) assert.equal(result.pending, outcome.status === "PENDING_SAFE");
      } else {
        await new Promise((resolve) => {
          setRenderCallback(() => { if (!state.busyAction) resolve(); });
          handleOduSettingsAction(`odu-settings-${action}`, { dataset: { hp: "1" } });
        });
        setRenderCallback(null);
        assert.equal(Boolean(state.controlNotice), success, `${action}: ${JSON.stringify(outcome)}`);
        assert.equal(Boolean(state.oduSettingsError), !success);
      }
      assert.equal(responses.length, 0);
    }
  }
});
