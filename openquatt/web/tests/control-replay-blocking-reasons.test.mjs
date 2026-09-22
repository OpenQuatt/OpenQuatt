import assert from "node:assert/strict";
import test from "node:test";
import { readFile } from "node:fs/promises";
import { loadReplayHarness } from "./helpers/replay-render-harness.mjs";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = { location: { pathname: "/" }, localStorage: { getItem: () => null } };
const { state } = await import("../js/src/core/state.js");
const { setLocale } = await import("../js/src/i18n/index.js");
const { getOverviewLikeHydrationKeys } = await import("../js/src/core/entity-sync.js");
const source = await readFile(new URL("../js/src/features/control-replay-view.js", import.meta.url), "utf8");
const { getControlWorkingBlockingReasons: reasons, getControlWorkingCurrent: current,
  getControlWorkingSignature: signature, renderControlWorkingNowCard: card } =
  await loadReplayHarness(source + "\nexport { getControlWorkingBlockingReasons, getControlWorkingCurrent, getControlWorkingSignature, renderControlWorkingNowCard };");
const value = (value) => ({ value, state: String(value) });
function setup(values = {}) {
  setLocale("nl");
  state.drafts = {};
  state.inputDrafts = {};
  state.entities = Object.fromEntries(Object.entries(values).map(([k, v]) => [k, value(v)]));
  state.decisionLog = null;
}
const off = { hp1Running: false, hp2Running: false, primaryReason: "keep_current" };

test("missing or active request never claims no heat demand", () => {
  for (const values of [{}, { strategyRequestActive: "nan" }, { strategyRequestActive: true }]) {
    setup(values);
    assert.match(reasons(off)[0], /bevestigt geen specifieke/);
  }
  setup({ strategyRequestActive: false });
  assert.match(reasons(off)[0], /vraagt momenteel geen warmte/);
  setup({ strategyRequestActive: false, coolingRequestActive: true });
  assert.doesNotMatch(reasons(off).join(" "), /geen warmte\b/);
});

test("actual CM4 and cooling buffer models preserve their live explanations", () => {
  setup({ controlModeLabel: "CM4", strategyRequestActive: true });
  const model = current([]);
  assert.equal(model.primaryReason, "fallback_blocked");
  assert.deepEqual(reasons(model), [model.copy]);
  for (const primaryReason of ["boiler_fallback", "boiler_assist", "defrost_hold", "buffer_stop"]) {
    assert.deepEqual(reasons({ ...off, primaryReason, copy: "Actuele reden" }), ["Actuele reden"]);
  }
});

test("low-load latch ON is permission; OFF blocks below restart threshold including hysteresis", () => {
  const values = { strategyActiveCode: 3, strategyRequestActive: true,
    strategyRequestedPower: 1900, lowLoadOnW: 2200, lowLoadOffW: 1600 };
  setup({ ...values, lowLoadLatch: true });
  assert.doesNotMatch(reasons(off).join(" "), /Laaglast/);
  setup({ ...values, lowLoadLatch: false });
  assert.match(reasons(off)[0], /1900 W.*2200 W/);
  assert.match(reasons(off)[1], /1600 W/);
  state.entities.strategyRequestedPower = value(2200);
  assert.doesNotMatch(reasons(off).join(" "), /Laaglast/);
  setup({ ...values, lowLoadLatch: false, strategyActiveCode: 2 });
  assert.doesNotMatch(reasons(off).join(" "), /Laaglast/);
});

test("multiple live blockers are collected; unknown data and drafts do not invent limits", () => {
  setup({ openquattEnabled: false, heatingBlockedByThermostat: true, strategyWaterTripActive: true });
  assert.equal(reasons(off).length, 3);
  state.drafts.openquattEnabled = true;
  assert.match(reasons(off)[0], /uitgeschakeld/);
  setup({ strategyActiveCode: 3, strategyRequestActive: true, lowLoadLatch: false, strategyRequestedPower: 900 });
  assert.doesNotMatch(reasons(off).join(" "), /0 W|NaN|Laaglast/);
});

test("render signature follows blocker changes without unrelated telemetry changing", () => {
  setup({ strategyRequestActive: true });
  const before = signature(off);
  state.entities.heatingBlockedByThermostat = value(true);
  assert.notEqual(signature(off), before);
  const blocked = signature(off);
  state.entities.heatingBlockedByThermostat = value(false);
  assert.notEqual(signature(off), blocked);
});

test("running heat pumps hide the section and cooling protection is not duplicated", () => {
  setup();
  for (const key of ["hp1Running", "hp2Running"]) {
    const model = { ...off, [key]: true };
    assert.deepEqual(reasons(model), []);
    assert.doesNotMatch(card(model), /Waarom staat/);
  }
  assert.deepEqual(reasons({ ...off, primaryReason: "restart_wait", coolingProtection: true,
    copy: "Veilige herstart" }), ["Veilige herstart"]);
});

test("English is supported and device-derived explanations are escaped", () => {
  setup({ openquattEnabled: false });
  setLocale("en");
  assert.equal(reasons(off)[0], "OpenQuatt is disabled.");
  assert.match(card(off), /Why is my heat pump off/);
  setup();
  const html = card({ ...off, primaryReason: "boiler_fallback", copy: "<script>alert(1)</script>" });
  assert.ok(html.includes("&lt;script&gt;"));
  assert.ok(!html.includes("<script>"));
});

test("control view hydrates every additional live blocking input", () => {
  setup();
  const keys = getOverviewLikeHydrationKeys("control", { forceFast: true });
  for (const key of ["strategyRequestActive", "strategyWaterTripActive", "strategyWaterHardTripActive",
    "heatingBlockedByThermostat", "controlModeOverride", "lowLoadLatch", "lowLoadOnW",
    "lowLoadOffW", "curveRestartInhibit", "curveRestartBlockedByRoom",
    "coolingStartBlockReason", "coolingStartBlockRemaining"]) assert.ok(keys.includes(key), key);
});

test("cooling start protection uses published countdown and clears after release", () => {
  setup({ coolingRequestActive: true, coolingStartBlockReason: "Compressor restart protection",
    coolingStartBlockRemaining: 190 });
  assert.match(reasons(off).join(" "), /3:10/);
  const before = signature(off);
  state.entities.coolingStartBlockRemaining = value(180);
  assert.notEqual(signature(off), before);
  state.entities.coolingStartBlockReason = value("Ready");
  assert.doesNotMatch(reasons(off).join(" "), /herstartbeveiliging/);
});
