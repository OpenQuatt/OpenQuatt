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
  assert.match(modal, /3 · Temperatuur- en ontdooiregeling/);
  assert.match(modal, /data-oq-odu-settings-field="mode"/);
  assert.match(modal, /data-oq-odu-settings-field="startTemperatureC"/);
  assert.match(modal, /data-oq-odu-settings-field="stopDeltaC"/);
  assert.match(modal, /7 °C/);
  assert.match(modal, /Na herstart automatisch opnieuw toepassen/);
  assert.match(modal, /V1\.5: modus 3/);
  assert.equal(shouldRefreshOduSettingsSurface(), true);

  assert.doesNotMatch(modal, /Technische details/);

  state.oduSettingsDrafts[1] = {
    mode: "3",
    startTemperatureC: "",
    stopDeltaC: "3",
    autoReapply: false,
    dirty: true,
  };
  const invalidModal = renderOduSettingsModal();
  assert.match(invalidModal, /data-oq-odu-stop-temperature>—<\/strong>/);
  assert.match(invalidModal, /data-oq-action="odu-settings-save" data-hp="1" disabled/);

  state.oduSettingsStatuses = {};
  state.oduSettingsError = "Status ophalen mislukt. Controleer de verbinding met OpenQuatt.";
  const failedModal = renderOduSettingsModal();
  assert.match(failedModal, /Status niet beschikbaar/);
  assert.doesNotMatch(failedModal, /Status laden\.\.\./);
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
  assert.match(source, /PENDING_SAFE/);
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
