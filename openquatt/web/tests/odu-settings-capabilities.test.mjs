import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import vm from "node:vm";

// Exercise the actual feature with isolated browser/transport dependencies.
// This does not replace the integrated odu-settings tests or a hardware test.
const feature = await readFile(new URL("../js/src/features/odu-settings.js", import.meta.url), "utf8");
const source = feature.replace(/^import .*;\n/gm, "").replace(/^export /gm, "");

function harness() {
  const state = { busyAction: "", oduSettingsDrafts: {}, oduSettingsStatuses: {}, oduSettingsError: "" };
  const requests = [];
  const responses = [];
  const context = vm.createContext({
    state, URLSearchParams, Date, Promise, Set, console,
    window: { setTimeout: (callback) => setTimeout(callback, 0) },
    hasEntity: () => false,
    getInstallationTopology: () => state.topology || "single",
    getBasePath: () => "",
    t: (key, values) => `${key}${values ? ` ${JSON.stringify(values)}` : ""}`,
    formatNumber: String,
    escapeHtml: (value) => String(value).replace(/[&<>"']/g, (char) => ({
      "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;",
    })[char]),
    render: () => {},
    renderNumberInputControl: () => "",
    renderOduEditorAction: (hp, action, label, disabled) => `<button data-hp="${hp}" data-oq-action="${action}"${disabled ? " disabled" : ""}>${label}</button>`,
    renderOduEditorPanel: ({ hp, statusLabel, body }) => `<section data-hp="${hp}">${statusLabel}${body}</section>`,
    renderOduEditorModal: ({ panels }) => panels,
    invokeActionMap: (handlers, action, button) => handlers[action]?.(button),
    fetchWithTimeout: async (url, options) => {
      requests.push({ url, options });
      assert.ok(responses.length, `Unexpected request: ${url}`);
      const payload = responses.shift();
      return { status: 200, ok: true, json: async () => payload };
    },
  });
  vm.runInContext(`${source}\nglobalThis.api = {
    isOduSettingsModeSupported, normalizeOduSettingsStatus, getOduSettingsEditorModel,
    renderOduSettingsModal, updateOduSettingsDraft, handleOduSettingsAction,
    refreshOduSettingsStatuses, getOduSettingsBackupProfiles, restoreOduSettingsBackupProfiles,
  };`, context);
  return { ...context.api, state, requests, responses };
}

function payload(variant = 1, mode = 1, hp = 1) {
  const values = { mode, start_temperature_c: 4, stop_delta_c: 3 };
  return {
    hp, variant, control_board_item: variant === 1 ? 55 : variant === 4 ? 4151 : 3639,
    available: true, identity_ready: true, identity_matches: true, loaded: true,
    profile_available: true, auto_reapply: true, csrf_token: "unit-test", status: "IN_SYNC",
    actual: values, desired: values, defaults: { ...values, mode: variant === 1 ? 1 : 3 },
  };
}

function install(h, data) {
  h.state.oduSettingsStatuses[data.hp] = h.normalizeOduSettingsStatus(data);
}

function profile(data) {
  return { variant: data.variant, control_board_item: data.control_board_item, ...data.desired, auto_reapply: true };
}

function optionValues(model) {
  return Array.from(model.modeOptions, ([value]) => value);
}

test("capabilities match the V1/V1.5/V2 matrix and reject unknown or malformed values", () => {
  const h = harness();
  for (const variant of [0, 1, 2, 3, 4, 5, 255]) {
    for (let mode = 0; mode <= 255; mode++) {
      const expected = variant === 1 ? mode <= 2 : [2, 3, 4].includes(variant) && mode <= 3;
      assert.equal(h.isOduSettingsModeSupported(mode, variant), expected, `${variant}/${mode}`);
    }
  }
  for (const mode of [-1, 1.5, NaN, null, undefined, "1", "", false]) {
    assert.equal(h.isOduSettingsModeSupported(mode, 1), false);
  }
});

test("mixed Duo panels use their own variant and keep the existing choice not to expose off", () => {
  const h = harness();
  h.state.topology = "duo";
  install(h, payload(1, 1, 1));
  install(h, payload(2, 3, 2));
  assert.deepEqual(optionValues(h.getOduSettingsEditorModel(1)), [1, 2]);
  assert.deepEqual(optionValues(h.getOduSettingsEditorModel(2)), [1, 2, 3]);
  const markup = h.renderOduSettingsModal();
  const selects = [...markup.matchAll(/<select\b[^>]*>[\s\S]*?<\/select>/g)].map(([select]) => select);
  assert.equal(selects.length, 2);
  assert.doesNotMatch(selects[0], /value="3"/);
  assert.match(selects[1], /value="3" selected/);
  assert.doesNotMatch(markup, /<option value="0"/);
  for (const variant of [2, 3, 4]) {
    install(h, payload(variant, 3));
    assert.equal(h.getOduSettingsEditorModel(1).saveDisabled, false);
  }
});

test("an existing V1 mode 3 remains readable but cannot be saved or described as a supported mode", () => {
  const h = harness();
  install(h, payload(1, 3));
  const model = h.getOduSettingsEditorModel(1);
  assert.equal(model.status.actual.mode, 3);
  assert.equal(model.draft.mode, "3");
  assert.equal(model.saveDisabled, true);
  assert.equal(model.temperatureSettingsVisible, false);
  assert.equal(model.stopDeltaVisible, false);
  assert.equal(model.modeCopy, "oduSettings.modeUnknown");
  const markup = h.renderOduSettingsModal();
  assert.match(markup, /oduSettings.errInvalid/);
  assert.match(markup, /value="" selected disabled/);
  assert.doesNotMatch(markup, /oduSettings.modeDesc3|<option value="3"/);
  assert.equal(h.requests.length, 0);

  h.updateOduSettingsDraft({
    dataset: { oqOduSettingsHp: "1", oqOduSettingsField: "mode" }, value: "2", closest: () => null,
  });
  assert.equal(h.getOduSettingsEditorModel(1).saveDisabled, false);
  assert.equal(h.state.oduSettingsDrafts[1].startTemperatureC, "4");
  assert.equal(h.state.oduSettingsStatuses[1].actual.mode, 3, "Choosing a draft does not write the device");
});

test("unknown identity or an unavailable unit cannot produce a valid save", () => {
  const h = harness();
  for (const variant of [0, 5, 255]) {
    install(h, payload(variant, 1));
    const model = h.getOduSettingsEditorModel(1);
    assert.equal(model.saveDisabled, true);
    assert.deepEqual(optionValues(model), []);
  }
  h.state.oduSettingsStatuses[1] = { available: true, identityReady: true, variant: 2, loaded: true };
  assert.doesNotThrow(() => h.renderOduSettingsModal());
  for (const patch of [{ available: false }, { identity_ready: false }, { unsupported: true }, { busy: true }]) {
    install(h, { ...payload(), ...patch });
    assert.equal(h.getOduSettingsEditorModel(1).saveDisabled, true);
  }
});

test("status refresh keeps dirty input but revalidates it against the newly identified variant", async () => {
  const h = harness();
  install(h, payload(2, 3));
  h.state.oduSettingsDrafts[1] = { mode: "3", startTemperatureC: "5", stopDeltaC: "3", dirty: true };
  h.responses.push(payload(1, 1));
  await h.refreshOduSettingsStatuses({ force: true });
  const model = h.getOduSettingsEditorModel(1);
  assert.equal(model.draft.mode, "3");
  assert.equal(model.draft.startTemperatureC, "5");
  assert.equal(model.saveDisabled, true);
  assert.deepEqual(optionValues(model), [1, 2]);
});

test("a direct UI action for V1 mode 3 does not POST even if the DOM is modified", async () => {
  const h = harness();
  install(h, payload(1, 1));
  h.state.oduSettingsDrafts[1] = { mode: "3", startTemperatureC: "4", stopDeltaC: "3", dirty: true };
  await h.handleOduSettingsAction("odu-settings-save", { dataset: { hp: "1" } });
  assert.equal(h.requests.length, 0);
  assert.match(h.state.oduSettingsError, /oduSettings.invalidDraft/);
});

test("backup restore rejects V1 mode 3 before POST and continues with a valid second unit", async () => {
  const h = harness();
  h.state.topology = "duo";
  const v1 = payload(1, 3);
  const v15 = payload(2, 3, 2);
  h.responses.push(v1, v15, v15);
  const results = await h.restoreOduSettingsBackupProfiles({ hp1: profile(v1), hp2: profile(v15) });
  assert.equal(results[0].applied, false);
  assert.equal(results[0].reason, "oduSettings.errInvalid");
  assert.equal(results[1].applied, true);
  const posts = h.requests.filter(({ options }) => options.method === "POST");
  assert.equal(posts.length, 1);
  assert.match(posts[0].url, /hp2\/save$/);
});

test("valid backups including off keep their wire values for all known variants", async () => {
  for (const [variant, modes] of [[1, [0, 1, 2]], [2, [0, 1, 2, 3]], [3, [3]], [4, [3]]]) {
    for (const mode of modes) {
      const h = harness();
      const data = payload(variant, mode);
      h.responses.push(data, data);
      const [result] = await h.restoreOduSettingsBackupProfiles({ hp1: profile(data) });
      assert.equal(result.applied, true, `${variant}/${mode}`);
      const posts = h.requests.filter(({ options }) => options.method === "POST");
      assert.equal(posts.length, 1);
      const body = new URLSearchParams(posts[0].options.body);
      assert.equal(body.get("mode"), String(mode));
      assert.equal(body.get("start_temperature_c"), "4");
      assert.equal(body.get("stop_delta_c"), "3");
    }
  }
});

test("backup export does not copy an unsupported legacy profile into a new backup", async () => {
  const h = harness();
  h.state.topology = "duo";
  h.responses.push(payload(1, 3, 1), payload(2, 3, 2));
  const profiles = await h.getOduSettingsBackupProfiles();
  assert.deepEqual(Object.keys(profiles), ["hp2"]);
  assert.equal(profiles.hp2.mode, 3);
  assert.equal(h.requests.some(({ options }) => options.method === "POST"), false);
});

test("service checks capabilities before persistence, every write and successful readback", async () => {
  const cpp = await readFile(new URL("../../../components/openquatt_odu_settings/OpenQuattOduSettings.cpp", import.meta.url), "utf8");
  const assertBefore = (body, guard, effect) => {
    const first = body.indexOf(guard);
    const second = body.indexOf(effect);
    assert.ok(first >= 0 && second > first, `${guard} must precede ${effect}`);
  };
  const save = cpp.slice(cpp.indexOf("OpenQuattOduSettings::request_save("), cpp.indexOf("bool OpenQuattOduSettings::identity_matches_profile_"));
  assertBefore(save, "valid_bottom_plate_settings(settings, this->variant_)", "this->desired_ = settings");
  assert.match(save, /RequestResult::INVALID_SETTINGS/);
  const persist = cpp.slice(cpp.indexOf("bool OpenQuattOduSettings::persist_profile_("), cpp.indexOf("bool OpenQuattOduSettings::begin_operation_("));
  assertBefore(persist, "valid_bottom_plate_profile(profile)", "this->profile_pref_.save(&profile)");
  const write = cpp.slice(cpp.indexOf("void OpenQuattOduSettings::queue_next_write_("), cpp.indexOf("void OpenQuattOduSettings::queue_readback_("));
  assertBefore(write, "valid_bottom_plate_settings(this->desired_, this->variant_)", "write_single_register(");
  assert.match(write, /identity_matches_profile_\(\)/);
  const readback = cpp.slice(cpp.indexOf("void OpenQuattOduSettings::queue_readback_("), cpp.indexOf("void OpenQuattOduSettings::release_bus_("));
  assertBefore(readback, "valid_bottom_plate_settings(actual, this->variant_)", 'finish_operation_("IN_SYNC"');
  assert.match(cpp, /profile_pref_\.load\(&stored\) && oq_odu::valid_bottom_plate_profile\(stored\)/);
});
