import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = {
  location: { pathname: "/dev.html" }, setTimeout, clearTimeout,
  localStorage: { getItem: () => null }, confirm: () => false,
};

const {
  getOduDefrostEndpoint, getOduDefrostHpIndexes, getOduDefrostPresentation,
  normalizeOduDefrostStatus, renderOduDefrostModal, shouldRefreshOduDefrostSurface,
  refreshOduDefrostStatuses, handleOduDefrostAction, getDefrostModeName, canSaveDefrostMode, isDefrostModeSupported,
} = await import("../js/src/features/odu-defrost.js");
const { state } = await import("../js/src/core/state.js");
const { renderSystemModal } = await import("../js/src/features/header-status.js");
const { setLocale } = await import("../js/src/i18n/index.js");

test("defrost gebruikt per-HP endpoints en toont alleen ondersteunde modusknoppen", () => {
  state.entities = { installationTopology: { value: "duo", state: "duo" } };
  assert.equal(getOduDefrostEndpoint(1, "status"), "/openquatt/odu-defrost/hp1/status");
  assert.equal(getOduDefrostEndpoint(2, "trigger"), "/openquatt/odu-defrost/hp2/trigger");
  assert.equal(getOduDefrostEndpoint(1, "save"), "/openquatt/odu-defrost/hp1/save");
  assert.deepEqual(getOduDefrostHpIndexes(), [1, 2]);
  state.systemModal = "odu-defrost";
  state.oduDefrostError = "";
  state.oduDefrostStatusFailed = false;
  state.busyAction = "";
  state.oduDefrostStatuses = { 1: normalizeOduDefrostStatus({ hp: 1, variant: 2, online: true, fresh: true, identity_ready: true, loaded: true, auto_defrost_control_ok: true, can_trigger: true, defrost_mode: 4, operation_mode: 1, state: "LOADED", guard: "READY", ambient_c: 4.2, coil_c: -2.1, compressor_hz: 30, max_duration_s: 600 }) };
  const modal = renderOduDefrostModal();
  assert.match(modal, /Handmatig ontdooien/);
  assert.match(modal, /Ontdooimethode/);
  assert.match(modal, /Ta − Tevap/);
  assert.match(modal, /Instellingen uitlezen/);
  assert.match(modal, /Alleen de ontdooimethode/);
  assert.match(modal, /Technische metingen/);
  assert.match(modal, /Nog niet waargenomen/);
  assert.doesNotMatch(modal, /Spiraal|Gereconstrueerde drempels|Startbevestiging|Vereiste delta/);
  const beforeDetails = modal.slice(0, modal.indexOf('<details class="oq-settings-odu-technical"'));
  assert.doesNotMatch(beforeDetails, /<dt>Verdampertemperatuur<|Compressorfrequentie|Actieve duur/);
  assert.doesNotMatch(modal, /oq-settings-odu-technical" open/);
  assert.doesNotMatch(modal, /data-oq-field=|<select/);
  assert.equal(shouldRefreshOduDefrostSurface(), true);
});

test("V1 verbergt mode 4 maar behoudt handmatige defrost en herstel naar ondersteunde methoden", () => {
  const v1Ready = { hp: 1, variant: 1, online: true, fresh: true, identity_ready: true, loaded: true, auto_defrost_control_ok: true, can_trigger: true, csrf_token: "test", state: "LOADED", guard: "READY" };
  assert.equal(isDefrostModeSupported(0, 1), true);
  assert.equal(isDefrostModeSupported(1, 1), true);
  assert.equal(isDefrostModeSupported(3, 1), true);
  assert.equal(isDefrostModeSupported(4, 1), false);
  assert.equal(isDefrostModeSupported(4, 2), true);
  assert.equal(isDefrostModeSupported(4, 0), false);

  state.entities = { installationTopology: { value: "single", state: "single" } };
  state.systemModal = "odu-defrost";
  state.oduDefrostError = "";
  state.oduDefrostStatusFailed = false;
  state.busyAction = "";
  state.oduDefrostStatuses = { 1: normalizeOduDefrostStatus({
    ...v1Ready, defrost_mode: 0, operation_mode: 2, compressor_hz: 0,
  }) };
  let modal = renderOduDefrostModal();
  assert.match(modal, /odu-defrost-save-1/);
  assert.match(modal, /odu-defrost-save-3/);
  assert.doesNotMatch(modal, /odu-defrost-save-4/);
  assert.match(modal, /data-oq-action="odu-defrost-trigger"/);

  state.oduDefrostStatuses = { 1: normalizeOduDefrostStatus({
    ...v1Ready, defrost_mode: 4, operation_mode: 2, compressor_hz: 0,
  }) };
  modal = renderOduDefrostModal();
  assert.match(modal, /Niet-ondersteunde ontdooimethode/);
  assert.match(modal, /odu-defrost-save-0/);
  assert.match(modal, /odu-defrost-save-1/);
  assert.match(modal, /odu-defrost-save-3/);
  assert.doesNotMatch(modal, /odu-defrost-save-4/);
});

test("eigen wachtende aanvraag wordt niet als andere actie getoond", () => {
  const status = normalizeOduDefrostStatus({ state: "WAITING", guard: "BUSY", busy: true, manual: true });
  assert.deepEqual(getOduDefrostPresentation(status), ["Aanvraag verstuurd; wachten op bevestiging van de buitenunit", "warning"]);
});

test("normalisatie behandelt onbekende tellers en ontbrekende sensoren expliciet", () => {
  const status = normalizeOduDefrostStatus({ hp: 1, elapsed_s: -1, ambient_c: null, guard: "OFFLINE" });
  assert.equal(status.elapsedS, -1);
  assert.equal(status.ambientC, null);
  assert.deepEqual(getOduDefrostPresentation(status), ["Buitenunit niet bereikbaar", "warning"]);
  assert.deepEqual(getOduDefrostPresentation(status, true), ["Status niet beschikbaar", "warning"]);
});

test("defrostpresentatie volgt de actieve taal en localegetallen", () => {
  const status = normalizeOduDefrostStatus({
    hp: 1, variant: 2, online: true, fresh: true, identity_ready: true, loaded: true,
    auto_defrost_control_ok: true, can_trigger: true, defrost_mode: 4,
    operation_mode: 2, state: "LOADED", guard: "READY", ambient_c: 1.5,
  });
  state.entities = { installationTopology: { value: "single", state: "single" } };
  state.systemModal = "odu-defrost";
  state.oduDefrostError = "";
  state.oduDefrostStatusFailed = false;
  state.oduDefrostStatuses = { 1: status };
  setLocale("en", { persist: false, applyDocument: false });
  try {
    const modal = renderOduDefrostModal();
    assert.match(modal, /Manual defrost/);
    assert.match(modal, /Heating/);
    assert.match(modal, /Defrost method/);
    assert.match(modal, /Only the defrost method/);
    assert.match(modal, /1.5 °C/);
    assert.deepEqual(getOduDefrostPresentation(status), ["Values loaded", ""]);
  } finally {
    setLocale("nl", { persist: false, applyDocument: false });
  }
  assert.match(renderOduDefrostModal(), /Handmatig ontdooien/);
  assert.match(renderOduDefrostModal(), /1,5 °C/);
});

test("handmatige actie is fail-closed zolang status niet vers en volledig bruikbaar is", () => {
  state.entities = { installationTopology: { value: "single", state: "single" } };
  state.systemModal = "odu-defrost";
  for (const [patch, disabled] of [[{}, false], [{ fresh: false }, true], [{ loaded: false }, true], [{ autoDefrostControlOk: false }, true], [{ canTrigger: false }, true], [{ busy: true }, true]]) {
    state.oduDefrostStatusFailed = false;
    state.oduDefrostStatuses = { 1: { online: true, fresh: true, identityReady: true, loaded: true, autoDefrostControlOk: true, canTrigger: true, state: "LOADED", guard: "READY", ...patch } };
    const button = renderOduDefrostModal().match(/<button[^>]*data-oq-action="odu-defrost-trigger"[^>]*>/)[0];
    assert.equal(/\bdisabled\b/.test(button), disabled);
  }
  state.oduDefrostStatusFailed = true;
  assert.match(renderOduDefrostModal(), /data-oq-action="odu-defrost-trigger" data-hp="1" disabled/);
});

test("eerste laadactie vereist geen reeds geladen control-register en -1°C blijft zichtbaar", () => {
  state.oduDefrostStatusFailed = false;
  state.oduDefrostStatuses = { 1: normalizeOduDefrostStatus({online: true, fresh: true, identity_ready: true, ambient_c: -1}) };
  const modal = renderOduDefrostModal();
  const load = modal.match(/<button[^>]*data-oq-action="odu-defrost-load"[^>]*>/)[0];
  assert.doesNotMatch(load, /\bdisabled\b/);
  assert.match(modal, /-1 °C/);
});

const flush = () => new Promise((resolve) => setImmediate(resolve));
const ready = { hp: 1, variant: 2, online: true, fresh: true, identity_ready: true, loaded: true, auto_defrost_control_ok: true, can_trigger: true, csrf_token: "test", state: "LOADED", guard: "READY" };
function reset() {
  state.entities = { installationTopology: {value: "single", state: "single"} };
  state.systemModal = "odu-defrost";
  state.busyAction = "";
  state.oduDefrostEpoch = 0;
  state.oduDefrostError = "";
  state.oduDefrostStatusFailed = false;
  state.oduDefrostFetchPromise = null;
  state.oduDefrostStatuses = {1: normalizeOduDefrostStatus(ready)};
}
const response = (payload) => ({ok:true, status:200, json:async () => payload});

test("eigen dialoog annuleert zonder aanvraag en controleert actuele vrijgave", async () => {
  reset();
  let posts = 0;
  window.confirm = () => { throw new Error("Native confirm must not be used"); };
  globalThis.fetch = async () => { posts++; return response(ready); };
  handleOduDefrostAction("odu-defrost-trigger", {dataset:{hp:"1"}});
  assert.equal(state.systemModal, "odu-defrost-confirm-1");
  assert.equal(shouldRefreshOduDefrostSurface(), true);
  assert.match(renderOduDefrostModal(), /data-oq-action="odu-defrost-confirm"/);
  handleOduDefrostAction("odu-defrost-cancel", {});
  assert.equal(state.systemModal, "odu-defrost");
  assert.equal(posts, 0);
  handleOduDefrostAction("odu-defrost-trigger", {dataset:{hp:"1"}});
  state.oduDefrostStatuses[1].canTrigger = false;
  handleOduDefrostAction("odu-defrost-confirm", {});
  await flush();
  assert.equal(posts, 0);
});

test("late GET overschrijft geen POST en dubbel klikken verstuurt maar één aanvraag", async () => {
  reset();
  let finishGet, finishPost;
  let posts = 0;
  globalThis.fetch = (_url, options) => options?.method === "POST"
    ? (++posts, new Promise((resolve) => {finishPost = resolve;}))
    : new Promise((resolve) => {finishGet = resolve;});
  const poll = refreshOduDefrostStatuses({force:true});
  await flush();
  handleOduDefrostAction("odu-defrost-trigger", {dataset:{hp:"1"}});
  assert.equal(posts, 0);
  handleOduDefrostAction("odu-defrost-confirm", {});
  handleOduDefrostAction("odu-defrost-confirm", {});
  await flush();
  assert.equal(posts, 1);
  assert.equal(await refreshOduDefrostStatuses({force:true}), false);
  finishPost(response({...ready, state:"WAITING", busy:true}));
  await flush();
  finishGet(response(ready));
  await poll;
  assert.equal(state.oduDefrostStatuses[1].state, "WAITING");
});

test("POST-netwerkfout blijft onzeker zonder retry; GET herstelt bediening", async () => {
  reset();
  let posts = 0;
  globalThis.fetch = async (_url, options) => {
    if (options?.method === "POST") { posts++; throw new Error("network failed"); }
    return response(ready);
  };
  handleOduDefrostAction("odu-defrost-trigger", {dataset:{hp:"1"}});
  handleOduDefrostAction("odu-defrost-confirm", {});
  await flush();
  assert.equal(posts, 1);
  assert.equal(state.oduDefrostStatusFailed, true);
  assert.match(state.oduDefrostError, /niet bevestigd/);
  await refreshOduDefrostStatuses({force:true});
  assert.equal(posts, 1);
  assert.equal(state.oduDefrostStatusFailed, false);
  globalThis.fetch = async () => { throw new Error("offline"); };
  await refreshOduDefrostStatuses({force:true, silent:true});
  assert.equal(state.oduDefrostStatusFailed, true);
});

test("moduskeuze toont alleen bewezen modi en vereist stilstaande compressor", () => {
  assert.equal(getDefrostModeName(2), "Gereserveerd");
  const stopped = normalizeOduDefrostStatus({ ...ready, defrost_mode: 0, compressor_hz: 0 });
  const running = normalizeOduDefrostStatus({ ...ready, defrost_mode: 0, compressor_hz: 35 });
  assert.equal(canSaveDefrostMode(stopped, false), true);
  assert.equal(canSaveDefrostMode(running, false), false);
  assert.equal(canSaveDefrostMode({ ...stopped, busy: true }, false), false);
  assert.equal(canSaveDefrostMode({ ...stopped, active: true }, false), false);
  assert.equal(canSaveDefrostMode(stopped, true), false);
  state.oduDefrostStatuses = { 1: stopped };
  state.systemModal = "odu-defrost";
  state.oduDefrostStatusFailed = false;
  state.busyAction = "";
  const modal = renderOduDefrostModal();
  assert.match(modal, /odu-defrost-save-1/);
  assert.match(modal, /odu-defrost-save-3/);
  assert.match(modal, /odu-defrost-save-4/);
  assert.doesNotMatch(modal, /odu-defrost-save-2/);
  assert.deepEqual(getOduDefrostPresentation(normalizeOduDefrostStatus({ ...ready, state: "SAVED" })), ["Ontdooimethode opgeslagen en teruggelezen", "success"]);
  assert.deepEqual(getOduDefrostPresentation(normalizeOduDefrostStatus({ ...ready, state: "STALE" })), ["Waarde is intussen gewijzigd; laad opnieuw", "warning"]);
});

test("modusbevestiging toont oud→nieuw, annuleert zonder POST en blokkeert dubbelklik", async () => {
  state.oduDefrostStatuses = { 1: normalizeOduDefrostStatus({ ...ready, defrost_mode: 0, compressor_hz: 0 }) };
  state.systemModal = "odu-defrost";
  state.busyAction = "";
  state.oduDefrostSave = null;
  state.oduDefrostStatusFailed = false;
  state.oduDefrostError = "";
  let posts = 0;
  let lastBody = "";
  window.confirm = () => { throw new Error("Native confirm must not be used"); };
  globalThis.fetch = async (_url, options) => {
    if (options?.method === "POST") { posts++; lastBody = String(options.body || ""); return response({ ...ready, defrost_mode: 4, state: "SAVED" }); }
    return response(ready);
  };
  handleOduDefrostAction("odu-defrost-save-4", { dataset: { hp: "1" } });
  assert.equal(state.systemModal, "odu-defrost-save-confirm-1");
  assert.deepEqual(state.oduDefrostSave, { hp: 1, desired: 4, expected: 0 });
  assert.match(renderOduDefrostModal(), /van .* naar .*HP1/s);
  assert.match(renderSystemModal(), /data-oq-action="odu-defrost-save-confirm"/);
  handleOduDefrostAction("odu-defrost-cancel", {});
  assert.equal(state.systemModal, "odu-defrost");
  assert.equal(posts, 0);
  handleOduDefrostAction("odu-defrost-save-4", { dataset: { hp: "1" } });
  handleOduDefrostAction("odu-defrost-save-confirm", {});
  handleOduDefrostAction("odu-defrost-save-confirm", {});
  await flush();
  assert.equal(posts, 1);
  assert.match(lastBody, /mode=4/);
  assert.match(lastBody, /expected_mode=0/);
  assert.equal(state.oduDefrostStatuses[1].state, "SAVED");
});

test("stale modus en fout geven geen fictief succes", async () => {
  reset();
  state.oduDefrostStatuses = { 1: normalizeOduDefrostStatus({ ...ready, defrost_mode: 0, compressor_hz: 0 }) };
  globalThis.fetch = async (_url, options) => {
    if (options?.method === "POST") return { ok: false, status: 409, json: async () => ({ error: "stale" }) };
    return response(ready);
  };
  handleOduDefrostAction("odu-defrost-save-4", { dataset: { hp: "1" } });
  handleOduDefrostAction("odu-defrost-save-confirm", {});
  await flush();
  assert.equal(state.oduDefrostStatusFailed, true);
  assert.match(state.oduDefrostError, /gewijzigd|niet bevestigd/);
  assert.notEqual(state.oduDefrostStatuses[1].state, "SAVED");
});
