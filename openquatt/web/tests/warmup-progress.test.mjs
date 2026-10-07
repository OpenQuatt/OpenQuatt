import assert from "node:assert/strict";
import test from "node:test";
import { setLocale } from "../js/src/i18n/index.js";
import { FAST_OVERVIEW_KEYS, SERVICE_STATUS_ENTITY_KEYS } from "../js/src/core/config.js";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = { location: { pathname: "/" }, localStorage: { getItem: () => null } };
const { state } = await import("../js/src/core/state.js");
const { getWarmupProgressText } = await import("../js/src/features/warmup-progress.js");
const { mergeServiceStatusPayload, refreshServiceStatusEntities } = await import("../js/src/core/entity-sync.js");
const { renderOverviewSummaryShell } = await import("../js/src/views/overview.js");
const { renderControlledWarmupField } = await import("../js/src/settings/heating.js");
const { patchOverviewDom } = await import("../js/src/views/heatpump.js");

function setProgress(status, seconds) {
  state.entities = {
    warmupEnabled: { value: true }, warmupActive: { value: status === 2 },
    warmupStatus: { value: status, state: String(status) },
    warmupElapsed: { value: seconds, state: String(seconds) },
  };
  state.drafts = {};
}

test("controller durations and end results have distinct NL/EN copy", () => {
  try {
    for (const locale of ["nl", "en"]) {
      setLocale(locale, { persist: false });
      for (const [code, phrase] of [[2, locale === "nl" ? "bezig" : "elapsed"],
        [3, locale === "nl" ? "afgerond" : "completed"],
        [4, locale === "nl" ? "afgebroken" : "cancelled"],
        [5, locale === "nl" ? "tijdslimiet" : "time limit"],
        [6, locale === "nl" ? "afgebroken" : "cancelled"],
        [7, locale === "nl" ? "afgebroken" : "cancelled"],
        [8, locale === "nl" ? "afgebroken" : "cancelled"],
        [9, locale === "nl" ? "afgebroken" : "cancelled"]]) {
        setProgress(code, 5700);
        const text = getWarmupProgressText();
        assert.ok(text.includes("01:35") && text.includes(phrase));
        assert.ok(renderOverviewSummaryShell("Power House").includes(text));
        assert.ok(renderControlledWarmupField().includes(text));
        assert.equal(getWarmupProgressText(), text); // Reads/reload do not run a browser timer.
      }
      setProgress(5, 28800);
      assert.match(getWarmupProgressText(), /08:00/);
      setProgress(2, 0);
      assert.match(getWarmupProgressText(), /00:00/);
    }
  } finally { setLocale("nl", { persist: false }); }
});

test("old firmware, boot and malformed progress never invent durations", () => {
  for (const [code, seconds] of [[0, 0], [10, 0], [-1, 0], [2.5, 0],
    [2, -1], [2, 28801], [2, 0.5], [2, NaN], [NaN, 60],
    [2, null], [2, ""], [2, " "], [2, {}], [2, false], [true, 60]]) {
    setProgress(code, seconds);
    assert.equal(getWarmupProgressText(), "");
  }
  setProgress(2, 60);
  delete state.entities.warmupElapsed;
  assert.equal(getWarmupProgressText(), "");
  state.entities = { warmupEnabled: { value: true }, warmupActive: { value: true } };
  assert.match(renderControlledWarmupField(), /Opwarmen in stappen/);
  assert.match(renderOverviewSummaryShell("Power House"), /data-oq-overview-warmup hidden/);
});

test("missing or failed status responses discard old timers", async () => {
  setProgress(3, 5700);
  mergeServiceStatusPayload({ entities: {} });
  assert.equal(getWarmupProgressText(), "");
  mergeServiceStatusPayload({ entities: { warmupStatus: { value: 2 }, warmupElapsed: { value: 60 } } });
  assert.match(getWarmupProgressText(), /00:01/);
  mergeServiceStatusPayload({ entities: { warmupStatus: { value: 3 } } });
  assert.equal(getWarmupProgressText(), "");
  setProgress(2, 5700);
  const oldFetch = globalThis.fetch;
  globalThis.fetch = async () => { throw new Error("offline"); };
  try {
    const result = await refreshServiceStatusEntities(["warmupStatus", "warmupElapsed"]);
    assert.equal(result.ok, false);
    assert.equal(getWarmupProgressText(), "");
  } finally { globalThis.fetch = oldFetch; }
});

test("overview patches only the notice when progress changes or disappears", () => {
  const notice = { textContent: "", hidden: true };
  const summary = { querySelector: (selector) => selector === "[data-oq-overview-warmup]" ? notice : null };
  const board = { className: "", querySelector: (selector) => selector === ".oq-overview-summary-shell" ? summary : null, querySelectorAll: () => [] };
  const root = { querySelector: (selector) => selector === ".oq-overview-board" ? board : null };
  Object.assign(state, { root, appView: "overview", overviewTheme: "light", entities: {}, drafts: {} });
  setProgress(2, 5700);
  patchOverviewDom();
  assert.equal(notice.hidden, false);
  assert.match(notice.textContent, /01:35/);
  setProgress(3, 6000);
  patchOverviewDom();
  assert.match(notice.textContent, /afgerond na 01:40/);
  state.entities = {};
  patchOverviewDom();
  assert.equal(notice.hidden, true);
  assert.equal(notice.textContent, "");
  state.root = null;
});

test("progress uses the existing service request and overview poll cadence", () => {
  for (const key of ["warmupStatus", "warmupElapsed"]) {
    assert.ok(SERVICE_STATUS_ENTITY_KEYS.has(key));
    assert.ok(FAST_OVERVIEW_KEYS.includes(key));
  }
});

test("switch-off records cancellation and hides editable tuning, retaining the result", () => {
  setProgress(1, 6000);
  state.entities.warmupEnabled = { value: false };
  assert.match(renderOverviewSummaryShell("Power House"), /afgebroken na 01:40/);
  assert.match(renderControlledWarmupField(), /afgebroken na 01:40/);
  assert.doesNotMatch(renderControlledWarmupField(), /data-oq-field="warmupStep"/);
});
