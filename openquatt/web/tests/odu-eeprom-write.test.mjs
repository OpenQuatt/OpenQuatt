import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
const timers = new Map();
let nextTimer = 0;
globalThis.window = {
  location: { pathname: "/dev.html" }, localStorage: { getItem: () => null },
  setTimeout: (callback) => { timers.set(++nextTimer, callback); return nextTimer; },
  clearTimeout: (id) => timers.delete(id),
};
const { state } = await import("../js/src/core/state.js");
const { setLocale } = await import("../js/src/i18n/index.js");
const { normalizeEepromWriteStatus, eepromDiffLabel, eepromDiffValue, renderEepromWriteModal, handleEepromWriteAction, updateEepromWriteDraft } = await import("../js/src/features/odu-eeprom-write.js");
const flush = () => new Promise(setImmediate);

test("EEPROM status fails closed for missing capabilities and never infers persistence from a CRC", () => {
  const empty = normalizeEepromWriteStatus({});
  assert.equal(empty.available, false);
  assert.equal(empty.baseline, false);
  assert.equal(empty.coldSeen, false);
  assert.equal(empty.powerCycleSafe, false);
  assert.equal(empty.legacy, false);
  const pending = normalizeEepromWriteStatus({ state: 6, result: "POWER_CYCLE_REQUIRED", crc: 123, holding: true });
  assert.equal(pending.result, "POWER_CYCLE_REQUIRED");
  assert.equal(pending.holding, true);
  assert.equal(pending.coldSeen, false);
  assert.deepEqual(normalizeEepromWriteStatus({ diff: [{ address: "3275", target: 3 }] }).diff, []);
});

test("EEPROM diff uses actual units and the distinct heating/cooling extension layout", () => {
  setLocale("nl");
  assert.equal(eepromDiffValue(3237, 33), "3 °C");
  assert.equal(eepromDiffValue(3238, 3), "3 K");
  assert.equal(eepromDiffValue(3017, 42), "42 Hz");
  assert.equal(eepromDiffLabel({ address: 3050 }), "Verwarmen F11");
  assert.equal(eepromDiffLabel({ address: 3069 }), "Koelen F20");
  assert.equal(eepromDiffLabel({ address: 3275 }), "Ontdooimethode");
});

test("permanent storage needs separate consent and never retries an ambiguous commit", async (t) => {
  const originalFetch = globalThis.fetch;
  t.after(() => { globalThis.fetch = originalFetch; state.systemModal = ""; timers.clear(); });
  state.entities = { installationTopology: { state: "single", value: "single" } };
  state.root = null;
  const writes = [];
  let remote = { hp: 1, available: true, baseline: true, state: 3, generation: 9, result: "PREPARED", details_ready: true, csrf_token: "token", diff: [{ address: 3237, stored: 34, runtime: 33, target: 33 }] };
  globalThis.fetch = async (url, options) => {
    if (options?.method === "POST") {
      writes.push({ url, body: new URLSearchParams(options.body) });
      remote = { ...remote, state: 6, generation: 11, result: "POWER_CYCLE_REQUIRED", holding: true, diff: [] };
      throw new Error("lost accepted response");
    }
    return { ok: true, json: async () => remote };
  };
  assert.equal(handleEepromWriteAction("open-odu-eeprom-write"), true);
  await flush();
  const button = { dataset: { hp: "1", eepromAction: "commit" } };
  handleEepromWriteAction("odu-eeprom-write-run", button);
  await flush();
  assert.equal(writes.length, 0);
  updateEepromWriteDraft({ dataset: { oqEepromWriteHp: "1", eepromField: "consent" }, checked: true, closest: () => null });
  handleEepromWriteAction("odu-eeprom-write-run", button);
  await flush(); await flush();
  assert.equal(writes.length, 1);
  assert.equal(writes[0].body.get("generation"), "9");
  assert.equal(writes[0].body.get("csrf_token"), "token");
  const markup = renderEepromWriteModal();
  assert.match(markup, /Spanningsonderbreking vereist/);
  assert.match(markup, /data-eeprom-action="verify"[^>]*disabled/);
  assert.doesNotMatch(markup, /Permanent behoud bevestigd/);
  assert.doesNotMatch(markup, /data-eeprom-action="commit"/);
  handleEepromWriteAction("odu-eeprom-write-run", button);
  await flush();
  assert.equal(writes.length, 1);
  state.systemModal = "";
  for (const callback of timers.values()) callback();
  await flush();
  assert.equal(writes.length, 1);
});

test("locale switching renders unavailable storage in both catalogues without writes", () => {
  setLocale("en");
  assert.match(renderEepromWriteModal(), /Store permanently in outdoor unit/);
  setLocale("nl");
  assert.match(renderEepromWriteModal(), /Permanent bewaren in buitenunit/);
});

test("a concurrent download cannot authorize commit without the complete review diff", async (t) => {
  const originalFetch = globalThis.fetch;
  t.after(() => { globalThis.fetch = originalFetch; state.systemModal = ""; timers.clear(); });
  let writes = 0;
  globalThis.fetch = async (_url, options) => {
    if (options?.method === "POST") ++writes;
    return { ok: true, json: async () => ({ hp: 1, available: true, baseline: true, state: 3, generation: 20, result: "PREPARED", details_ready: false }) };
  };
  handleEepromWriteAction("open-odu-eeprom-write");
  await flush();
  updateEepromWriteDraft({ dataset: { oqEepromWriteHp: "1", eepromField: "consent" }, checked: true, closest: () => null });
  handleEepromWriteAction("odu-eeprom-write-run", { dataset: { hp: "1", eepromAction: "commit" } });
  await flush();
  assert.equal(writes, 0);
  assert.match(renderEepromWriteModal(), /data-eeprom-action="commit"[^>]*disabled/);
});

test("legacy backups disclose HP binding and uncertain writers require a read-only status check", async (t) => {
  const originalFetch = globalThis.fetch;
  t.after(() => { globalThis.fetch = originalFetch; state.systemModal = ""; timers.clear(); });
  setLocale("nl");
  const writes = [];
  let remote = { hp: 1, available: true, baseline: true, legacy: true, state: 5, generation: 30, holding: true, result: "COMMIT_FAILED", csrf_token: "token" };
  globalThis.fetch = async (url, options) => {
    if (options?.method === "POST") {
      writes.push(url);
      assert.match(url, /\/check-writer$/);
      remote = { ...remote, result: "WRITER_ACTIVE", power_cycle_safe: false };
    }
    return { ok: true, json: async () => remote };
  };
  handleEepromWriteAction("open-odu-eeprom-write");
  await flush();
  let markup = renderEepromWriteModal();
  assert.match(markup, /geen serienummer/);
  assert.match(markup, /HP-aansluiting/);
  assert.match(markup, /Houd de buitenunit onder spanning/);
  assert.match(markup, /data-eeprom-action="check-writer"/);
  assert.doesNotMatch(markup, /data-eeprom-action="verify"/);
  assert.doesNotMatch(markup, /Maak uitsluitend deze buitenunit/);
  handleEepromWriteAction("odu-eeprom-write-run", { dataset: { hp: "1", eepromAction: "check-writer" } });
  await flush(); await flush();
  assert.equal(writes.length, 1);
  assert.match(renderEepromWriteModal(), /Schrijven is nog niet aantoonbaar gestopt/);
  // A fresh backend confirmation changes the instructions, without a commit retry.
  remote = { ...remote, power_cycle_safe: true, result: "POWER_CYCLE_REQUIRED" };
  handleEepromWriteAction("open-odu-eeprom-write");
  await flush();
  markup = renderEepromWriteModal();
  assert.match(markup, /data-eeprom-action="verify"[^>]*disabled/);
  assert.doesNotMatch(markup, /data-eeprom-action="check-writer"/);
  assert.equal(writes.length, 1);
  setLocale("en");
  assert.match(renderEepromWriteModal(), /provides no serial number/);
  assert.match(renderEepromWriteModal(), /HP connection/);
  setLocale("nl");
});

test("an already interrupted uncertain job can be verified only with fresh disconnect evidence and consent", async (t) => {
  const originalFetch = globalThis.fetch;
  t.after(() => { globalThis.fetch = originalFetch; state.systemModal = ""; timers.clear(); });
  const actions = [];
  globalThis.fetch = async (url, options) => {
    if (options?.method === "POST") actions.push(url);
    return { ok: true, json: async () => ({ hp: 1, available: true, baseline: true, state: 4, generation: 31, holding: true, cold_seen: true }) };
  };
  handleEepromWriteAction("open-odu-eeprom-write");
  await flush();
  assert.match(renderEepromWriteModal(), /data-eeprom-action="verify"[^>]*disabled/);
  const button = { dataset: { hp: "1", eepromAction: "verify" } };
  handleEepromWriteAction("odu-eeprom-write-run", button);
  await flush();
  assert.equal(actions.length, 0);
  updateEepromWriteDraft({ dataset: { oqEepromWriteHp: "1", eepromField: "cold" }, checked: true, closest: () => null });
  handleEepromWriteAction("odu-eeprom-write-run", button);
  await flush(); await flush();
  assert.equal(actions.length, 1);
  assert.match(actions[0], /\/verify$/);
});
